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
		break;
	case Wet:
		S.Cloud = 1.0; S.Rain = 1.0; S.Wind = 7.0; S.Gust = 11.0; S.Air = 10.0; S.Visibility = 10000.0; S.RoadWet = 1.0; S.Code = 61;
		S.Puddles = 0.4;   // (the edges, as the wet roads always had puddles)
		S.LowCloud = 0.9; S.CloudBase = 900.0;
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
	return S;
}

double Dullness(const State& S) { return Smooth((S.Cloud - 0.2) / 0.7); }

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

std::string OpenMeteoUrl(double Lat, double Lon)
{
	return "https://api.open-meteo.com/v1/forecast?" + LatLon("latitude", Lat, "longitude", Lon) +
		"&current=temperature_2m,dew_point_2m,precipitation,snowfall,cloud_cover,cloud_cover_low,wind_speed_10m,wind_direction_10m,wind_gusts_10m,weather_code,visibility"
		"&hourly=precipitation&past_hours=8&forecast_hours=1&wind_speed_unit=ms&timeformat=unixtime";
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
	S.Wind = std::max(Cur->Num("wind_speed_10m", 0.0), 0.0);
	S.WindFrom = Cur->Num("wind_direction_10m", S.WindFrom);
	S.Gust = std::max(Cur->Num("wind_gusts_10m", S.Wind), S.Wind);
	S.Code = static_cast<int>(Cur->Num("weather_code", 3.0));
	const bool bFog = S.Code == 45 || S.Code == 48;
	S.Visibility = Cur->Get("visibility") && Cur->Get("visibility")->IsNumber() ? std::max(Cur->Num("visibility"), 20.0) : (bFog ? 400.0 : 30000.0);
	const long long Now = static_cast<long long>(Cur->Num("time", 0.0));
	S.LowCloud = Clamp01(Cur->Num("cloud_cover_low", S.Cloud * 60.0) / 100.0);
	S.CloudBase = Cur->Get("dew_point_2m") ? CloudBaseFrom(Root.Num("elevation", 0.0), S.Air, Cur->Num("dew_point_2m")) : 5000.0;

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
	return Buf;
}

bool Load(const std::string& Text, State& Out)
{
	std::istringstream In(Text);
	std::string Tag;
	State S;
	if (!(In >> Tag) || Tag != "w1") return false;
	if (!(In >> S.Cloud >> S.Rain >> S.Snow >> S.Wind >> S.WindFrom >> S.Gust >> S.Air >> S.Visibility >> S.RoadWet >> S.Code)) return false;
	In >> S.LowCloud >> S.CloudBase;   // (an older save without them keeps the defaults)
	if (In.fail()) { S.LowCloud = 0.0; S.CloudBase = 5000.0; }
	else if (!(In >> S.Puddles)) S.Puddles = 0.0;
	Out = S;
	return true;
}
} // namespace TorqueWeather
