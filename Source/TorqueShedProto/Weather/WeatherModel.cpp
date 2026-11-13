#include "WeatherModel.h"

#include "../Fit/FitJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace TorqueWeather
{
namespace
{
double Clamp01(double V) { return V < 0.0 ? 0.0 : V > 1.0 ? 1.0 : V; }
double Smooth(double T) { T = Clamp01(T); return T * T * (3.0 - 2.0 * T); }
double Lerp(double A, double B, double T) { return A + (B - A) * T; }
// A bearing from A towards B, the short way round.
double TurnLerp(double A, double B, double T)
{
	const double Turn = std::fmod(B - A + 540.0, 360.0) - 180.0;
	return std::fmod(A + Turn * T + 360.0, 360.0);
}
// A figure that may not be known (below 0): known only when both are.
double LerpKnown(double A, double B, double T) { return A < 0.0 || B < 0.0 ? -1.0 : Lerp(A, B, T); }
// A road's film of water that reads as fully wet, and the most it holds before running off, mm.
constexpr double WetFilm = 0.2, MaxFilm = 0.5;
} // namespace

State Preset(int Kind)
{
	State S;
	switch (Kind)
	{
	case Overcast:
		S.Cloud = 1.0; S.Rain = 0.0; S.Wind = 6.0; S.Gust = 10.0; S.Air = 11.0; S.Visibility = 20000.0; S.RoadWet = 0.0; S.Code = 3;
		S.LowCloud = 0.6; S.CloudBase = 1500.0;
		S.MidCloud = 0.9; S.HighCloud = 0.4; S.Humidity = 85.0;
		break;
	case Wet:
		S.Cloud = 1.0; S.Rain = 1.0; S.Wind = 7.0; S.Gust = 11.0; S.Air = 10.0; S.Visibility = 10000.0; S.RoadWet = 1.0; S.Code = 61;
		S.Puddles = 0.4;   // (the edges, as the wet roads always had puddles)
		S.LowCloud = 0.9; S.CloudBase = 900.0;
		S.MidCloud = 0.9; S.HighCloud = 0.3; S.Humidity = 90.0; S.FreezingLevel = 1800.0;
		break;
	default:
		break;   // sunny: the State's own defaults
	}
	return S;
}

int NearestKind(const State& S)
{
	if (S.RoadWet >= 0.5) return Wet;
	return Dullness(S) >= 0.5 ? Overcast : Sunny;
}

State Blend(const State& A, const State& B, double T)
{
	T = Clamp01(T);
	State S;
	S.Cloud = Lerp(A.Cloud, B.Cloud, T);
	S.Rain = Lerp(A.Rain, B.Rain, T);
	S.Snow = Lerp(A.Snow, B.Snow, T);
	S.SnowDepth = Lerp(A.SnowDepth, B.SnowDepth, T);
	S.Wind = Lerp(A.Wind, B.Wind, T);
	const double Turn = std::fmod(B.WindFrom - A.WindFrom + 540.0, 360.0) - 180.0;
	S.WindFrom = std::fmod(A.WindFrom + Turn * T + 360.0, 360.0);
	S.Gust = Lerp(A.Gust, B.Gust, T);
	S.Air = Lerp(A.Air, B.Air, T);
	// Visibility eases in steps that look even (fog thickens as fast at 500 m as at 20 km).
	S.Visibility = std::exp(Lerp(std::log(std::max(A.Visibility, 10.0)), std::log(std::max(B.Visibility, 10.0)), T));
	S.RoadWet = Lerp(A.RoadWet, B.RoadWet, T);
	S.LowCloud = Lerp(A.LowCloud, B.LowCloud, T);
	S.CloudBase = Lerp(A.CloudBase, B.CloudBase, T);
	S.Puddles = Lerp(A.Puddles, B.Puddles, T);
	S.Code = T < 0.5 ? A.Code : B.Code;
	S.MidCloud = Lerp(A.MidCloud, B.MidCloud, T);
	S.HighCloud = Lerp(A.HighCloud, B.HighCloud, T);
	S.Humidity = Lerp(A.Humidity, B.Humidity, T);
	S.Cape = Lerp(A.Cape, B.Cape, T);
	S.FreezingLevel = Lerp(A.FreezingLevel, B.FreezingLevel, T);
	S.DirectSun = LerpKnown(A.DirectSun, B.DirectSun, T);
	S.DiffuseSun = LerpKnown(A.DiffuseSun, B.DiffuseSun, T);
	S.Wind850 = Lerp(A.Wind850, B.Wind850, T);
	S.Wind850From = TurnLerp(A.Wind850From, B.Wind850From, T);
	S.Wind700 = Lerp(A.Wind700, B.Wind700, T);
	S.Wind700From = TurnLerp(A.Wind700From, B.Wind700From, T);
	S.Wind300 = Lerp(A.Wind300, B.Wind300, T);
	S.Wind300From = TurnLerp(A.Wind300From, B.Wind300From, T);
	return S;
}

double SunBlock(const State& S)
{
	// Low and middle cloud hide the sun in full; high cloud is thin (a veil of cirrus lets most of it
	// through, still casting shadows), so it counts for a quarter. The layers overlap at random, and
	// together they never hide more than the total the feed gives.
	const double Low = Clamp01(S.LowCloud), Mid = Clamp01(S.MidCloud), High = Clamp01(S.HighCloud);
	const double Layers = 1.0 - (1.0 - Low) * (1.0 - Mid) * (1.0 - 0.25 * High);
	return std::min(Layers, Clamp01(S.Cloud));
}

double Dullness(const State& S) { return Smooth((SunBlock(S) - 0.2) / 0.7); }

double WetLook(const State& S) { return std::max(Clamp01(S.RoadWet), Clamp01(S.Rain / 1.0)); }

double FogBoost(const State& S)
{
	const double Vis = std::max(S.Visibility, 50.0);
	return Vis >= 8000.0 ? 1.0 : std::min(8000.0 / Vis, 12.0);
}

double InCloud(const State& S, double HeightAsl)
{
	// Only a real low deck (most of the sky): a few scraps of low cloud don't swallow the road.
	const double Deck = Smooth((S.LowCloud - 0.55) / 0.3);
	return Deck * Smooth((HeightAsl - (S.CloudBase - 40.0)) / 80.0);
}

double HazeAt(const State& S, double HeightAsl) { return std::max(FogBoost(S), Lerp(1.0, 12.0, InCloud(S, HeightAsl))); }

double CloudBaseFrom(double Elevation, double Air, double DewPoint) { return Elevation + 125.0 * std::max(Air - DewPoint, 0.0); }

double DewPointFrom(double Air, double Humidity)
{
	// Magnus.
	const double Rh = std::min(std::max(Humidity, 1.0), 100.0) / 100.0;
	const double G = std::log(Rh) + 17.62 * Air / (243.12 + Air);
	return 243.12 * G / (17.62 - G);
}

double RainShow(const State& S)
{
	const double R = std::max(S.Rain, 0.0);
	return R < 0.05 ? 0.0 : std::min(std::sqrt(R / 5.0), 1.0);
}

double SwayFromWind(const State& S)
{
	const double W = std::max(S.Wind * 0.7 + S.Gust * 0.3, 0.0);
	return std::min(std::max(0.15 + 0.85 * std::pow(W / 7.0, 1.3), 0.15), 2.5);
}

double WetGrip(const State& S)
{
	const double Standing = Clamp01((S.Puddles - 0.3) / 0.7);
	return std::max(1.0 - 0.32 * Clamp01(S.RoadWet) - 0.06 * Standing * Standing, 0.6);
}

double GustShare(double T)
{
	// Three slow waves at unrelated rates; a gust where they line up, shaped to a peak.
	const double Sum = 0.5 * std::sin(T * 0.37 + 1.3) + 0.3 * std::sin(T * 0.93 + 4.1) + 0.2 * std::sin(T * 2.11 + 0.7);
	const double Up = Clamp01((Sum - 0.15) / 0.75);
	return Up * Up * (3.0 - 2.0 * Up);
}

double WindAtRider(const State& S, double T, double Shelter, double& FromDegrees)
{
	const double Steady = std::max(S.Wind, 0.0), Gust = std::max(S.Gust, Steady);
	const double At10 = Steady + (Gust - Steady) * GustShare(T);
	FromDegrees = S.WindFrom + 12.0 * std::sin(T * 0.21 + 2.0) + 6.0 * std::sin(T * 0.77);
	return At10 * 0.65 * (1.0 - 0.8 * Clamp01(Shelter));
}

double RoadWaterStep(double WaterMm, double RainMmPerHour, double Air, double Wind, double Cloud, double Seconds)
{
	// Drying, mm an hour: a British grey day takes three or four hours, a warm breezy bright one under two.
	double Dry = 0.02 + 0.006 * std::max(Air, 0.0) + 0.01 * std::max(Wind, 0.0) + 0.15 * (1.0 - Clamp01(Cloud));
	if (RainMmPerHour > 0.05) Dry *= 0.1;   // (the air is saturated while it rains)
	const double W = WaterMm + (std::max(RainMmPerHour, 0.0) - Dry) * Seconds / 3600.0;
	return std::min(std::max(W, 0.0), MaxFilm);
}

double RoadWetFromWater(double WaterMm) { return Smooth(WaterMm / WetFilm); }

double RoadWaterAfter(const std::vector<double>& RainPerHour, double Air, double Wind, double Cloud)
{
	double W = 0.0;
	// Each hour in four steps, so a wet hour followed by dry ones dries as it would.
	for (double R : RainPerHour)
		for (int I = 0; I < 4; ++I) W = RoadWaterStep(W, R, Air, Wind, Cloud, 900.0);
	return W;
}

double SnowCover(const State& S)
{
	const double Lying = Clamp01((S.SnowDepth - 0.004) / 0.036);
	const double Falling = S.Air < 1.5 ? Clamp01(S.Snow / 1.5) * Clamp01((1.5 - S.Air) / 2.0) * 0.6 : 0.0;
	return std::max(Lying, Falling);
}

double PuddlesAfter(const std::vector<double>& RainPerHour, double Air)
{
	// A store that the rain fills and that drains and dries over a few hours (faster when warm).
	double Store = 0.0;
	const double Drain = 0.16 + 0.01 * std::max(Air, 0.0);   // share lost an hour
	for (double R : RainPerHour) Store = Store * (1.0 - Drain) + std::max(R, 0.0);
	return Smooth(Store / 18.0) * 0.55 + Clamp01(Store / 4.0) * 0.45;
}

void Fader::FadeTo(const State& S, double OverSeconds)
{
	if (OverSeconds <= 0.0)
	{
		Set(S);
		return;
	}
	From = Now;
	To = S;
	T = 0.0;
	Seconds = OverSeconds;
}

bool Fader::Tick(double Dt)
{
	if (T >= 1.0) return false;
	T = std::min(T + Dt / std::max(Seconds, 1e-3), 1.0);
	Now = Blend(From, To, Smooth(T));
	if (T >= 1.0) Now = To;
	return true;
}

std::string CodeWords(int Code)
{
	switch (Code)
	{
	case 0: return "clear";
	case 1: return "mostly clear";
	case 2: return "partly cloudy";
	case 3: return "overcast";
	case 45: case 48: return "fog";
	case 51: return "light drizzle";
	case 53: return "drizzle";
	case 55: return "heavy drizzle";
	case 56: case 57: return "freezing drizzle";
	case 61: return "light rain";
	case 63: return "rain";
	case 65: return "heavy rain";
	case 66: case 67: return "freezing rain";
	case 71: return "light snow";
	case 73: return "snow";
	case 75: return "heavy snow";
	case 77: return "snow grains";
	case 80: return "light showers";
	case 81: return "showers";
	case 82: return "heavy showers";
	case 85: return "light snow showers";
	case 86: return "snow showers";
	case 95: return "thunderstorm";
	case 96: case 99: return "thunderstorm with hail";
	default: return "cloudy";
	}
}

std::string Compass(double FromDegrees)
{
	static const char* Points[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
	const double D = std::fmod(std::fmod(FromDegrees, 360.0) + 360.0, 360.0);
	return Points[static_cast<int>(std::floor((D + 22.5) / 45.0)) % 8];
}

std::string Describe(const State& S, bool bImperial)
{
	const double Per = bImperial ? 2.23694 : 3.6;
	const char* Unit = bImperial ? "mph" : "km/h";
	char Buf[160];
	const long Temp = std::lround(S.Air);
	std::string Text = std::to_string(Temp == 0 ? 0L : Temp) + " \xC2\xB0" "C, " + CodeWords(S.Code);
	if (S.Wind < 0.5)
		Text += ", still air";
	else
	{
		std::snprintf(Buf, sizeof(Buf), ", wind %s %ld %s", Compass(S.WindFrom).c_str(), std::lround(S.Wind * Per), Unit);
		Text += Buf;
		if (S.Gust > S.Wind * 1.5 && S.Gust - S.Wind > 4.0)
		{
			std::snprintf(Buf, sizeof(Buf), ", gusting %ld", std::lround(S.Gust * Per));
			Text += Buf;
		}
	}
	if (S.Visibility < 1000.0)
	{
		std::snprintf(Buf, sizeof(Buf), ", visibility %ld m", std::lround(S.Visibility / 10.0) * 10);
		Text += Buf;
	}
	return Text;
}

// --- The feeds ---------------------------------------------------------------------------------------

namespace
{
// Four decimals (about 10 m): enough, and MET Norway refuses more.
std::string LatLon(const char* LatName, double Lat, const char* LonName, double Lon)
{
	char Buf[96];
	std::snprintf(Buf, sizeof(Buf), "%s=%.4f&%s=%.4f", LatName, Lat, LonName, Lon);
	return Buf;
}

// "2026-11-09T22:00:00Z" as seconds since 1970 (UTC).
long long ParseIsoUtc(const std::string& Iso)
{
	if (Iso.size() < 16 || Iso[4] != '-' || Iso[7] != '-' || Iso[10] != 'T' || Iso[13] != ':') return 0;
	auto Part = [&Iso](size_t At, size_t Len) { return std::atoi(Iso.substr(At, Len).c_str()); };
	int Y = Part(0, 4);
	const int Mo = Part(5, 2), D = Part(8, 2), H = Part(11, 2), Mi = Part(14, 2), Se = Iso.size() >= 19 ? Part(17, 2) : 0;
	// Days from the civil date (Howard Hinnant's).
	Y -= Mo <= 2;
	const long long Era = (Y >= 0 ? Y : Y - 399) / 400;
	const long long Yoe = Y - Era * 400;
	const long long Doy = (153LL * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
	const long long Doe = Yoe * 365 + Yoe / 4 - Yoe / 100 + Doy;
	const long long Days = Era * 146097 + Doe - 719468;
	return Days * 86400 + H * 3600 + Mi * 60 + Se;
}
} // namespace

namespace
{
// What each point is asked for: the ground's weather, then the sky's (skies plan stage 1: the three
// cloud layers, how unstable the air is, the freezing level, the sunshine and the wind aloft).
constexpr const char* CurrentFields =
	"temperature_2m,dew_point_2m,relative_humidity_2m,precipitation,snowfall,snow_depth,cloud_cover,cloud_cover_low,"
	"cloud_cover_mid,cloud_cover_high,wind_speed_10m,wind_direction_10m,wind_gusts_10m,weather_code,visibility,"
	"cape,freezing_level_height,direct_radiation,diffuse_radiation,"
	"wind_speed_850hPa,wind_direction_850hPa,wind_speed_700hPa,wind_direction_700hPa,wind_speed_300hPa,wind_direction_300hPa";
} // namespace

std::string OpenMeteoUrl(double Lat, double Lon)
{
	return "https://api.open-meteo.com/v1/forecast?" + LatLon("latitude", Lat, "longitude", Lon) +
		"&current=" + std::string(CurrentFields) +
		"&hourly=precipitation&past_hours=8&forecast_hours=1&wind_speed_unit=ms&timeformat=unixtime";
}

namespace
{
bool ParseOpenMeteoPoint(const TorqueFit::Json& Root, State& Out, long long* Time, std::string* Error);
}

bool ParseOpenMeteo(const std::string& Body, State& Out, long long* Time, std::string* Error)
{
	using TorqueFit::Json;
	Json Root;
	std::string Why;
	if (!Json::Parse(Body, Root, &Why) || !Root.IsObject())
	{
		if (Error) *Error = "not JSON: " + Why;
		return false;
	}
	return ParseOpenMeteoPoint(Root, Out, Time, Error);
}

namespace
{
bool ParseOpenMeteoPoint(const TorqueFit::Json& Root, State& Out, long long* Time, std::string* Error)
{
	using TorqueFit::Json;
	if (Root.Flag("error"))
	{
		if (Error) *Error = "the service said: " + Root.Str("reason");
		return false;
	}
	const Json* Cur = Root.Get("current");
	if (!Cur || !Cur->IsObject() || !Cur->Get("temperature_2m") || !Cur->Get("cloud_cover"))
	{
		if (Error) *Error = "no current conditions in the answer";
		return false;
	}
	State S;
	const double Interval = std::max(Cur->Num("interval", 900.0), 60.0);
	S.Air = Cur->Num("temperature_2m", S.Air);
	S.Cloud = Clamp01(Cur->Num("cloud_cover", 0.0) / 100.0);
	S.Rain = std::max(Cur->Num("precipitation", 0.0), 0.0) * 3600.0 / Interval;
	S.Snow = std::max(Cur->Num("snowfall", 0.0), 0.0) * 3600.0 / Interval;
	S.SnowDepth = std::max(Cur->Num("snow_depth", 0.0), 0.0);
	S.Wind = std::max(Cur->Num("wind_speed_10m", 0.0), 0.0);
	S.WindFrom = Cur->Num("wind_direction_10m", S.WindFrom);
	S.Gust = std::max(Cur->Num("wind_gusts_10m", S.Wind), S.Wind);
	S.Code = static_cast<int>(Cur->Num("weather_code", 3.0));
	const bool bFog = S.Code == 45 || S.Code == 48;
	S.Visibility = Cur->Get("visibility") && Cur->Get("visibility")->IsNumber() ? std::max(Cur->Num("visibility"), 20.0) : (bFog ? 400.0 : 30000.0);
	const long long Now = static_cast<long long>(Cur->Num("time", 0.0));
	S.LowCloud = Clamp01(Cur->Num("cloud_cover_low", S.Cloud * 60.0) / 100.0);
	S.CloudBase = Cur->Get("dew_point_2m") ? CloudBaseFrom(Root.Num("elevation", 0.0), S.Air, Cur->Num("dew_point_2m")) : 5000.0;
	// The sky's (stage 1 of the skies plan). Without the layers (an older answer), the middle takes the
	// total, so the light reads as it always did.
	auto Known = [Cur](const char* Name) { const Json* V = Cur->Get(Name); return V && V->IsNumber(); };
	S.MidCloud = Known("cloud_cover_mid") ? Clamp01(Cur->Num("cloud_cover_mid") / 100.0) : S.Cloud;
	S.HighCloud = Known("cloud_cover_high") ? Clamp01(Cur->Num("cloud_cover_high") / 100.0) : 0.0;
	S.Humidity = Known("relative_humidity_2m") ? std::min(std::max(Cur->Num("relative_humidity_2m"), 0.0), 100.0) : S.Humidity;
	S.Cape = Known("cape") ? std::max(Cur->Num("cape"), 0.0) : 0.0;
	S.FreezingLevel = Known("freezing_level_height") ? Cur->Num("freezing_level_height") : S.FreezingLevel;
	S.DirectSun = Known("direct_radiation") ? std::max(Cur->Num("direct_radiation"), 0.0) : -1.0;
	S.DiffuseSun = Known("diffuse_radiation") ? std::max(Cur->Num("diffuse_radiation"), 0.0) : -1.0;
	auto Aloft = [&](const char* Speed, const char* From, double& OutSpeed, double& OutFrom, double Scale) {
		// (Missing: the surface wind, stronger with height, as a fair guess.)
		OutSpeed = Known(Speed) ? std::max(Cur->Num(Speed), 0.0) : S.Wind * Scale;
		OutFrom = Known(From) ? Cur->Num(From) : S.WindFrom;
	};
	Aloft("wind_speed_850hPa", "wind_direction_850hPa", S.Wind850, S.Wind850From, 1.6);
	Aloft("wind_speed_700hPa", "wind_direction_700hPa", S.Wind700, S.Wind700From, 2.3);
	Aloft("wind_speed_300hPa", "wind_direction_300hPa", S.Wind300, S.Wind300From, 4.5);

	// The road: the rain of the last few hours (each hour's value is the hour before it), then this
	// quarter hour's.
	std::vector<double> Hours;
	if (const Json* Hourly = Root.Get("hourly"))
	{
		const Json* Times = Hourly->Get("time");
		const Json* Rain = Hourly->Get("precipitation");
		if (Times && Rain && Times->IsArray() && Rain->IsArray())
			for (size_t I = 0; I < Times->A.size() && I < Rain->A.size(); ++I)
				if (Now == 0 || static_cast<long long>(Times->A[I].N) <= Now) Hours.push_back(Rain->A[I].IsNumber() ? std::max(Rain->A[I].N, 0.0) : 0.0);
	}
	double Water = RoadWaterAfter(Hours, S.Air, S.Wind, S.Cloud);
	S.Puddles = PuddlesAfter(Hours, S.Air);
	Water = RoadWaterStep(Water, S.Rain, S.Air, S.Wind, S.Cloud, Interval);
	S.RoadWet = RoadWetFromWater(Water);
	Out = S;
	if (Time) *Time = Now;
	return true;
}
} // namespace

// --- Weather across distance (stage 5) ----------------------------------------------------------------

namespace
{
constexpr double MetresPerDegree = 111320.0;
constexpr double DegToRad = 3.14159265358979323846 / 180.0;
double Lerp1(double A, double B, double T) { return A + (B - A) * T; }
}

void Offset(double Lat, double Lon, double East, double North, double& OutLat, double& OutLon)
{
	OutLat = Lat + North / MetresPerDegree;
	OutLon = Lon + East / (MetresPerDegree * std::max(std::cos(Lat * DegToRad), 0.05));
}

void MetresBetween(double FromLat, double FromLon, double ToLat, double ToLon, double& East, double& North)
{
	North = (ToLat - FromLat) * MetresPerDegree;
	East = (ToLon - FromLon) * MetresPerDegree * std::max(std::cos(FromLat * DegToRad), 0.05);
}

void SnapCentre(double Lat, double Lon, double StepM, double& OutLat, double& OutLon)
{
	const double DLat = StepM / MetresPerDegree;
	OutLat = std::round(Lat / DLat) * DLat;
	const double DLon = StepM / (MetresPerDegree * std::max(std::cos(OutLat * DegToRad), 0.05));
	OutLon = std::round(Lon / DLon) * DLon;
}

std::string OpenMeteoGridUrl(double Lat, double Lon, int N, double StepM)
{
	std::string Lats, Lons;
	char Buf[32];
	const double Half = (N - 1) * 0.5;
	for (int Row = 0; Row < N; ++Row)
		for (int Col = 0; Col < N; ++Col)
		{
			double PLat = 0.0, PLon = 0.0;
			Offset(Lat, Lon, (Col - Half) * StepM, (Half - Row) * StepM, PLat, PLon);
			std::snprintf(Buf, sizeof(Buf), "%s%.4f", Lats.empty() ? "" : ",", PLat);
			Lats += Buf;
			std::snprintf(Buf, sizeof(Buf), "%s%.4f", Lons.empty() ? "" : ",", PLon);
			Lons += Buf;
		}
	return "https://api.open-meteo.com/v1/forecast?latitude=" + Lats + "&longitude=" + Lons +
		"&current=" + std::string(CurrentFields) +
		"&hourly=precipitation&past_hours=8&forecast_hours=1&wind_speed_unit=ms&timeformat=unixtime&timezone=auto";
}

bool ParseOpenMeteoGrid(const std::string& Body, double Lat, double Lon, int N, double StepM, Grid& Out,
	long long* Time, int* UtcOffset, std::string* Error)
{
	using TorqueFit::Json;
	Json Root;
	std::string Why;
	if (!Json::Parse(Body, Root, &Why))
	{
		if (Error) *Error = "not JSON: " + Why;
		return false;
	}
	if (Root.IsObject())
	{
		// (One point asked for, or the service's error.)
		if (Root.Flag("error"))
		{
			if (Error) *Error = "the service said: " + Root.Str("reason");
			return false;
		}
		if (N != 1)
		{
			if (Error) *Error = "one point in the answer, not a grid";
			return false;
		}
	}
	const size_t Want = static_cast<size_t>(N) * static_cast<size_t>(N);
	if (Root.IsArray() && Root.A.size() != Want)
	{
		if (Error) *Error = "the answer has " + std::to_string(Root.A.size()) + " points, not " + std::to_string(Want);
		return false;
	}
	Grid G;
	G.N = N;
	G.Lat = Lat;
	G.Lon = Lon;
	G.StepM = StepM;
	long long Latest = 0;
	for (size_t I = 0; I < Want; ++I)
	{
		const Json& P = Root.IsArray() ? Root.A[I] : Root;
		State S;
		long long When = 0;
		if (!ParseOpenMeteoPoint(P, S, &When, Error)) return false;
		G.Points.push_back(S);
		G.Elevation.push_back(P.Num("elevation", 0.0));
		Latest = std::max(Latest, When);
		if (I == Want / 2 && UtcOffset) *UtcOffset = static_cast<int>(P.Num("utc_offset_seconds", 0.0));
	}
	Out = G;
	if (Time) *Time = Latest;
	return true;
}

State GridAt(const Grid& G, double Lat, double Lon, double AslM, double DriftSeconds)
{
	if (!G.Valid()) return State();
	// Carried on the wind (the grid's mean): what is here now was upwind when it was fetched.
	double Ex = 0.0, Ny = 0.0;
	for (const State& P : G.Points)
	{
		const double To = (P.WindFrom + 180.0) * DegToRad;
		Ex += std::sin(To) * P.Wind;
		Ny += std::cos(To) * P.Wind;
	}
	Ex /= static_cast<double>(G.Points.size());
	Ny /= static_cast<double>(G.Points.size());
	const double Half = (G.N - 1) * 0.5;
	double DriftE = Ex * std::max(DriftSeconds, 0.0), DriftN = Ny * std::max(DriftSeconds, 0.0);
	const double Most = Half * G.StepM, Len = std::sqrt(DriftE * DriftE + DriftN * DriftN);
	if (Len > Most) { DriftE *= Most / Len; DriftN *= Most / Len; }
	double East = 0.0, North = 0.0;
	MetresBetween(G.Lat, G.Lon, Lat, Lon, East, North);
	const double Col = std::min(std::max((East - DriftE) / G.StepM + Half, 0.0), (double)(G.N - 1));
	const double Row = std::min(std::max(Half - (North - DriftN) / G.StepM, 0.0), (double)(G.N - 1));
	const int C0 = std::min((int)Col, G.N - 2), R0 = std::min((int)Row, G.N - 2);
	const double Tx = Col - C0, Ty = Row - R0;
	auto At = [&G](int R, int C) -> const State& { return G.Points[R * G.N + C]; };
	const State& A = At(R0, C0);
	const State& B = At(R0, C0 + 1);
	const State& C = At(R0 + 1, C0);
	const State& D = At(R0 + 1, C0 + 1);
	auto Mix = [Tx, Ty](double a, double b, double c, double d) { return Lerp1(Lerp1(a, b, Tx), Lerp1(c, d, Tx), Ty); };
	State S;
	S.Cloud = Mix(A.Cloud, B.Cloud, C.Cloud, D.Cloud);
	S.Rain = Mix(A.Rain, B.Rain, C.Rain, D.Rain);
	S.Snow = Mix(A.Snow, B.Snow, C.Snow, D.Snow);
	S.SnowDepth = Mix(A.SnowDepth, B.SnowDepth, C.SnowDepth, D.SnowDepth);
	S.Wind = Mix(A.Wind, B.Wind, C.Wind, D.Wind);
	S.Gust = Mix(A.Gust, B.Gust, C.Gust, D.Gust);
	S.Air = Mix(A.Air, B.Air, C.Air, D.Air);
	S.Visibility = std::exp(Mix(std::log(std::max(A.Visibility, 1.0)), std::log(std::max(B.Visibility, 1.0)),
		std::log(std::max(C.Visibility, 1.0)), std::log(std::max(D.Visibility, 1.0))));
	S.RoadWet = Mix(A.RoadWet, B.RoadWet, C.RoadWet, D.RoadWet);
	S.LowCloud = Mix(A.LowCloud, B.LowCloud, C.LowCloud, D.LowCloud);
	S.CloudBase = Mix(A.CloudBase, B.CloudBase, C.CloudBase, D.CloudBase);
	S.Puddles = Mix(A.Puddles, B.Puddles, C.Puddles, D.Puddles);
	S.MidCloud = Mix(A.MidCloud, B.MidCloud, C.MidCloud, D.MidCloud);
	S.HighCloud = Mix(A.HighCloud, B.HighCloud, C.HighCloud, D.HighCloud);
	S.Humidity = Mix(A.Humidity, B.Humidity, C.Humidity, D.Humidity);
	S.Cape = Mix(A.Cape, B.Cape, C.Cape, D.Cape);
	S.FreezingLevel = Mix(A.FreezingLevel, B.FreezingLevel, C.FreezingLevel, D.FreezingLevel);
	S.DirectSun = A.DirectSun < 0.0 || B.DirectSun < 0.0 || C.DirectSun < 0.0 || D.DirectSun < 0.0 ? -1.0 : Mix(A.DirectSun, B.DirectSun, C.DirectSun, D.DirectSun);
	S.DiffuseSun = A.DiffuseSun < 0.0 || B.DiffuseSun < 0.0 || C.DiffuseSun < 0.0 || D.DiffuseSun < 0.0 ? -1.0 : Mix(A.DiffuseSun, B.DiffuseSun, C.DiffuseSun, D.DiffuseSun);
	{
		// The wind aloft as vectors (a wind turning through north stays a wind).
		auto Vec = [&Mix, &A, &B, &C, &D](double State::*Speed, double State::*From, double& OutSpeed, double& OutFrom) {
			auto X = [Speed, From](const State& P) { return std::sin(P.*From * DegToRad) * P.*Speed; };
			auto Y = [Speed, From](const State& P) { return std::cos(P.*From * DegToRad) * P.*Speed; };
			const double Vx = Mix(X(A), X(B), X(C), X(D)), Vy = Mix(Y(A), Y(B), Y(C), Y(D));
			OutSpeed = Mix(A.*Speed, B.*Speed, C.*Speed, D.*Speed);
			OutFrom = std::fmod(std::atan2(Vx, Vy) / DegToRad + 360.0, 360.0);
		};
		Vec(&State::Wind850, &State::Wind850From, S.Wind850, S.Wind850From);
		Vec(&State::Wind700, &State::Wind700From, S.Wind700, S.Wind700From);
		Vec(&State::Wind300, &State::Wind300From, S.Wind300, S.Wind300From);
	}
	{
		auto X = [](const State& P) { return std::sin(P.WindFrom * DegToRad) * std::max(P.Wind, 0.1); };
		auto Y = [](const State& P) { return std::cos(P.WindFrom * DegToRad) * std::max(P.Wind, 0.1); };
		const double Wx = Mix(X(A), X(B), X(C), X(D)), Wy = Mix(Y(A), Y(B), Y(C), Y(D));
		S.WindFrom = std::fmod(std::atan2(Wx, Wy) / DegToRad + 360.0, 360.0);
	}
	S.Code = At(R0 + (Ty >= 0.5 ? 1 : 0), C0 + (Tx >= 0.5 ? 1 : 0)).Code;
	const double Ground = Mix(G.Elevation[R0 * G.N + C0], G.Elevation[R0 * G.N + C0 + 1], G.Elevation[(R0 + 1) * G.N + C0], G.Elevation[(R0 + 1) * G.N + C0 + 1]);
	if (AslM > -1000.0) S.Air -= 0.0065 * (AslM - Ground);
	return S;
}

State AddDifference(const State& Base, const State& Here, const State& Ref, double W)
{
	W = Clamp01(W);
	State S = Base;
	if (W <= 0.0) return S;
	auto Add = [W](double B, double H, double R) { return B + (H - R) * W; };
	S.Cloud = Clamp01(Add(Base.Cloud, Here.Cloud, Ref.Cloud));
	S.Rain = std::max(Add(Base.Rain, Here.Rain, Ref.Rain), 0.0);
	S.Snow = std::max(Add(Base.Snow, Here.Snow, Ref.Snow), 0.0);
	S.SnowDepth = std::max(Add(Base.SnowDepth, Here.SnowDepth, Ref.SnowDepth), 0.0);
	S.Wind = std::max(Add(Base.Wind, Here.Wind, Ref.Wind), 0.0);
	S.Gust = std::max(Add(Base.Gust, Here.Gust, Ref.Gust), S.Wind);
	S.Air = Add(Base.Air, Here.Air, Ref.Air);
	S.Visibility = std::max(Base.Visibility * std::pow(std::max(Here.Visibility, 1.0) / std::max(Ref.Visibility, 1.0), W), 20.0);
	S.RoadWet = Clamp01(Add(Base.RoadWet, Here.RoadWet, Ref.RoadWet));
	S.LowCloud = Clamp01(Add(Base.LowCloud, Here.LowCloud, Ref.LowCloud));
	S.CloudBase = std::max(Add(Base.CloudBase, Here.CloudBase, Ref.CloudBase), 0.0);
	S.Puddles = Clamp01(Add(Base.Puddles, Here.Puddles, Ref.Puddles));
	double D = std::fmod(Here.WindFrom - Ref.WindFrom + 540.0, 360.0) - 180.0;
	S.WindFrom = std::fmod(Base.WindFrom + D * W + 360.0, 360.0);
	if (W >= 0.5 && Here.Code != Ref.Code) S.Code = Here.Code;
	S.MidCloud = Clamp01(Add(Base.MidCloud, Here.MidCloud, Ref.MidCloud));
	S.HighCloud = Clamp01(Add(Base.HighCloud, Here.HighCloud, Ref.HighCloud));
	S.Humidity = std::min(std::max(Add(Base.Humidity, Here.Humidity, Ref.Humidity), 0.0), 100.0);
	S.Cape = std::max(Add(Base.Cape, Here.Cape, Ref.Cape), 0.0);
	S.FreezingLevel = Add(Base.FreezingLevel, Here.FreezingLevel, Ref.FreezingLevel);
	if (Base.DirectSun >= 0.0 && Here.DirectSun >= 0.0 && Ref.DirectSun >= 0.0) S.DirectSun = std::max(Add(Base.DirectSun, Here.DirectSun, Ref.DirectSun), 0.0);
	if (Base.DiffuseSun >= 0.0 && Here.DiffuseSun >= 0.0 && Ref.DiffuseSun >= 0.0) S.DiffuseSun = std::max(Add(Base.DiffuseSun, Here.DiffuseSun, Ref.DiffuseSun), 0.0);
	// (The wind aloft is the same across a few km: the shared one stands.)
	return S;
}

double SunDeclination(int DayOfYear)
{
	return 23.44 * std::sin(2.0 * 3.14159265358979323846 * (284 + DayOfYear) / 365.0);
}

double EquationOfTime(int DayOfYear)
{
	const double B = 2.0 * 3.14159265358979323846 * (DayOfYear - 81) / 364.0;
	return 9.87 * std::sin(2.0 * B) - 7.53 * std::cos(B) - 1.5 * std::sin(B);
}

std::string MetNoUrl(double Lat, double Lon)
{
	return "https://api.met.no/weatherapi/locationforecast/2.0/compact?" + LatLon("lat", Lat, "lon", Lon);
}

int MetNoSymbolCode(const std::string& Symbol)
{
	std::string Sym = Symbol.substr(0, Symbol.find('_'));   // "lightrain_day" -> "lightrain"
	const bool bThunder = Sym.find("thunder") != std::string::npos;
	const bool bShowers = Sym.find("showers") != std::string::npos;
	if (bThunder) return 95;
	if (Sym == "clearsky") return 0;
	if (Sym == "fair") return 1;
	if (Sym == "partlycloudy") return 2;
	if (Sym == "cloudy") return 3;
	if (Sym == "fog") return 45;
	const bool bLight = Sym.rfind("light", 0) == 0;
	const bool bHeavy = Sym.rfind("heavy", 0) == 0;
	if (Sym.find("sleet") != std::string::npos) return 66;
	if (Sym.find("snow") != std::string::npos) return bShowers ? (bHeavy ? 86 : 85) : bLight ? 71 : bHeavy ? 75 : 73;
	if (Sym.find("rain") != std::string::npos) return bShowers ? (bLight ? 80 : bHeavy ? 82 : 81) : bLight ? 61 : bHeavy ? 65 : 63;
	return 3;
}

bool ParseMetNo(const std::string& Body, State& Out, long long* Time, std::string* Error)
{
	using TorqueFit::Json;
	Json Root;
	std::string Why;
	if (!Json::Parse(Body, Root, &Why) || !Root.IsObject())
	{
		if (Error) *Error = "not JSON: " + Why;
		return false;
	}
	const Json* Props = Root.Get("properties");
	const Json* Series = Props ? Props->Get("timeseries") : nullptr;
	if (!Series || !Series->IsArray() || Series->A.empty())
	{
		if (Error) *Error = "no forecast in the answer";
		return false;
	}
	const Json& First = Series->A[0];
	const Json* Data = First.Get("data");
	const Json* Instant = Data ? Data->Get("instant") : nullptr;
	const Json* Details = Instant ? Instant->Get("details") : nullptr;
	if (!Details || !Details->Get("air_temperature"))
	{
		if (Error) *Error = "no conditions in the forecast";
		return false;
	}
	State S;
	S.Air = Details->Num("air_temperature", S.Air);
	S.Cloud = Clamp01(Details->Num("cloud_area_fraction", 0.0) / 100.0);
	S.Wind = std::max(Details->Num("wind_speed", 0.0), 0.0);
	S.WindFrom = Details->Num("wind_from_direction", S.WindFrom);
	S.Gust = std::max(Details->Num("wind_speed_of_gust", S.Wind * 1.6), S.Wind);
	S.Rain = 0.0;
	S.Code = S.Cloud > 0.85 ? 3 : S.Cloud > 0.4 ? 2 : S.Cloud > 0.1 ? 1 : 0;
	S.LowCloud = Clamp01(Details->Num("cloud_area_fraction_low", S.Cloud * 60.0) / 100.0);
	S.MidCloud = Clamp01(Details->Num("cloud_area_fraction_medium", S.Cloud * 100.0) / 100.0);
	S.HighCloud = Clamp01(Details->Num("cloud_area_fraction_high", 0.0) / 100.0);
	S.Humidity = Details->Num("relative_humidity", S.Humidity);
	S.Wind850 = S.Wind * 1.6; S.Wind850From = S.WindFrom;
	S.Wind700 = S.Wind * 2.3; S.Wind700From = S.WindFrom;
	S.Wind300 = S.Wind * 4.5; S.Wind300From = S.WindFrom;
	if (Details->Get("relative_humidity"))
	{
		double Elev = 0.0;
		if (const Json* Geometry = Root.Get("geometry"))
			if (const Json* Coords = Geometry->Get("coordinates"); Coords && Coords->IsArray() && Coords->A.size() >= 3) Elev = Coords->A[2].N;
		S.CloudBase = CloudBaseFrom(Elev, S.Air, DewPointFrom(S.Air, Details->Num("relative_humidity")));
	}
	if (const Json* Next = Data->Get("next_1_hours"))
	{
		if (const Json* Sum = Next->Get("summary")) S.Code = MetNoSymbolCode(Sum->Str("symbol_code", "cloudy"));
		if (const Json* NextDetails = Next->Get("details")) S.Rain = std::max(NextDetails->Num("precipitation_amount", 0.0), 0.0);
	}
	if (S.Code >= 71 && S.Code <= 86 && S.Code != 80 && S.Code != 81 && S.Code != 82) S.Snow = S.Rain;   // (water equivalent, near enough)
	S.Visibility = S.Code == 45 ? 400.0 : 30000.0;
	// No past rain here: the road as if this hour's rain had been falling for the last hour.
	S.RoadWet = RoadWetFromWater(RoadWaterAfter({S.Rain}, S.Air, S.Wind, S.Cloud));
	S.Puddles = PuddlesAfter({S.Rain}, S.Air);
	Out = S;
	if (Time) *Time = ParseIsoUtc(First.Str("time"));
	return true;
}

std::string Save(const State& S)
{
	char Buf[256];
	std::snprintf(Buf, sizeof(Buf), "w1 %.3f %.3f %.3f %.3f %.1f %.3f %.2f %.0f %.3f %d %.3f %.0f %.3f", S.Cloud, S.Rain, S.Snow, S.Wind, S.WindFrom, S.Gust,
		S.Air, S.Visibility, S.RoadWet, S.Code, S.LowCloud, S.CloudBase, S.Puddles);
	std::string Out = Buf;
	// (w2: the sky's figures after the lying snow, always written)
	Out[1] = '2';
	std::snprintf(Buf, sizeof(Buf), " %.3f %.3f %.3f %.1f %.0f %.0f %.1f %.1f %.1f %.0f %.1f %.0f %.1f %.0f", S.SnowDepth, S.MidCloud, S.HighCloud, S.Humidity,
		S.Cape, S.FreezingLevel, S.DirectSun, S.DiffuseSun, S.Wind850, S.Wind850From, S.Wind700, S.Wind700From, S.Wind300, S.Wind300From);
	Out += Buf;
	return Out;
}

bool Load(const std::string& Text, State& Out)
{
	std::istringstream In(Text);
	std::string Tag;
	State S;
	if (!(In >> Tag) || (Tag != "w1" && Tag != "w2")) return false;
	if (!(In >> S.Cloud >> S.Rain >> S.Snow >> S.Wind >> S.WindFrom >> S.Gust >> S.Air >> S.Visibility >> S.RoadWet >> S.Code)) return false;
	In >> S.LowCloud >> S.CloudBase;   // (an older save without them keeps the defaults)
	if (In.fail()) { S.LowCloud = 0.0; S.CloudBase = 5000.0; }
	else if (!(In >> S.Puddles)) S.Puddles = 0.0;
	else if (!(In >> S.SnowDepth)) S.SnowDepth = 0.0;
	State Sky = S;
	if (Tag == "w2" && (In >> Sky.MidCloud >> Sky.HighCloud >> Sky.Humidity >> Sky.Cape >> Sky.FreezingLevel >> Sky.DirectSun >> Sky.DiffuseSun
		>> Sky.Wind850 >> Sky.Wind850From >> Sky.Wind700 >> Sky.Wind700From >> Sky.Wind300 >> Sky.Wind300From))
		S = Sky;
	else
	{
		// (A w1 save: no layers, so the middle takes the total and the light reads as it did.)
		S.MidCloud = S.Cloud;
		S.HighCloud = 0.0;
	}
	Out = S;
	return true;
}
} // namespace TorqueWeather
