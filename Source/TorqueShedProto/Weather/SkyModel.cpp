#include "SkyModel.h"

#include "../Fit/FitJson.h"

#include <algorithm>
#include <cmath>

namespace TorqueSky
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;
double Clamp01(double V) { return V < 0.0 ? 0.0 : V > 1.0 ? 1.0 : V; }
double Smooth(double T) { T = Clamp01(T); return T * T * (3.0 - 2.0 * T); }
double Lerp(double A, double B, double T) { return A + (B - A) * T; }
} // namespace

double Curve::At(double X) const
{
	if (P.empty()) return 0.0;
	if (X <= P.front().first) return P.front().second;
	if (X >= P.back().first) return P.back().second;
	for (size_t I = 1; I < P.size(); ++I)
		if (X <= P[I].first)
		{
			const double Span = P[I].first - P[I - 1].first;
			return Span <= 0.0 ? P[I].second : Lerp(P[I - 1].second, P[I].second, (X - P[I - 1].first) / Span);
		}
	return P.back().second;
}

double Curve::AtLog(double X) const
{
	if (P.empty()) return 0.0;
	if (X <= P.front().first) return P.front().second;
	if (X >= P.back().first) return P.back().second;
	for (size_t I = 1; I < P.size(); ++I)
		if (X <= P[I].first)
		{
			const double Span = P[I].first - P[I - 1].first;
			const double A = std::log10(std::max(P[I - 1].second, 1e-9)), B = std::log10(std::max(P[I].second, 1e-9));
			return std::pow(10.0, Span <= 0.0 ? B : Lerp(A, B, (X - P[I - 1].first) / Span));
		}
	return P.back().second;
}

namespace
{
struct NamedNumber { const char* Name; double Looks::*Field; };
struct NamedCurve { const char* Name; Curve Looks::*Field; };

const NamedNumber Numbers[] = {
	{"overcast_lux", &Looks::OvercastLux}, {"rain_cloud_lux", &Looks::RainCloudLux},
	{"day_from_lux", &Looks::DayFromLux}, {"day_full_lux", &Looks::DayFullLux},
	{"sun_lowest_deg", &Looks::SunLowestDeg}, {"sun_fade_from_deg", &Looks::SunFadeFromDeg}, {"sun_fade_to_deg", &Looks::SunFadeToDeg},
	{"sun_dim_dull", &Looks::SunDimDull}, {"sun_dim_wet", &Looks::SunDimWet}, {"sun_wide_deg", &Looks::SunWideDeg},
	{"disc_hide_over_deg", &Looks::DiscHideOverDeg}, {"sky_light_dull", &Looks::SkyLightDull},
	{"exposure_dull_stops", &Looks::ExposureDullStops}, {"exposure_wet_stops", &Looks::ExposureWetStops},
	{"direct_trust", &Looks::DirectTrust}, {"direct_trust_from_deg", &Looks::DirectTrustFromDeg},
	{"humid_from", &Looks::HumidFrom}, {"humid_swell", &Looks::HumidSwell},
	{"dull_mie", &Looks::DullMie}, {"dull_rayleigh", &Looks::DullRayleigh},
	{"dull_mie_clouds", &Looks::DullMieClouds}, {"dull_rayleigh_clouds", &Looks::DullRayleighClouds},
	{"fog_dull", &Looks::FogDull}, {"fog_wet", &Looks::FogWet}, {"fog_night_glow", &Looks::FogNightGlow},
	{"low_deck_m", &Looks::LowDeckM}, {"low_heap_m", &Looks::LowHeapM}, {"cape_depth_per_100", &Looks::CapeDepthPer100},
	{"cape_most_m", &Looks::CapeMostM}, {"mid_base_m", &Looks::MidBaseM}, {"mid_depth_m", &Looks::MidDepthM},
	{"low_base_least_m", &Looks::LowBaseLeastM}, {"low_base_most_m", &Looks::LowBaseMostM},
	{"density_fair", &Looks::DensityFair}, {"density_deck", &Looks::DensityDeck}, {"density_rain", &Looks::DensityRain},
	{"albedo_fair", &Looks::AlbedoFair}, {"albedo_rain", &Looks::AlbedoRain},
	{"deck_from", &Looks::DeckFrom}, {"deck_to", &Looks::DeckTo},
};
const NamedCurve Curves[] = {
	{"clear_lux", &Looks::ClearLux}, {"haze_by_km", &Looks::HazeByKm}, {"cover_shown", &Looks::CoverShown},
	{"legacy_shadow", &Looks::LegacyShadow},
};

// The members of an object and of every object inside it (so the file can group its figures under
// headings: "light": {...}, "air": {...}, "cloud": {...}).
void Gather(const TorqueFit::Json& J, std::vector<const std::pair<std::string, TorqueFit::Json>*>& Out)
{
	if (!J.IsObject()) return;
	for (const auto& KV : J.O)
	{
		if (KV.second.IsObject()) Gather(KV.second, Out);
		else Out.push_back(&KV);
	}
}
} // namespace

bool LoadLooks(const std::string& Text, Looks& Out, std::string* Error)
{
	using TorqueFit::Json;
	Json Root;
	std::string Why;
	if (!Json::Parse(Text, Root, &Why) || !Root.IsObject())
	{
		if (Error) *Error = "not JSON: " + Why;
		return false;
	}
	std::vector<const std::pair<std::string, Json>*> All;
	Gather(Root, All);
	for (const auto* KV : All)
	{
		for (const NamedNumber& N : Numbers)
			if (KV->first == N.Name && KV->second.IsNumber()) Out.*(N.Field) = KV->second.N;
		for (const NamedCurve& C : Curves)
			if (KV->first == C.Name && KV->second.IsArray())
			{
				Curve Read;
				for (const Json& Point : KV->second.A)
					if (Point.IsArray() && Point.A.size() == 2 && Point.A[0].IsNumber() && Point.A[1].IsNumber())
						Read.P.emplace_back(Point.A[0].N, Point.A[1].N);
				std::sort(Read.P.begin(), Read.P.end());
				if (!Read.P.empty()) Out.*(C.Field) = Read;
			}
	}
	return true;
}

double ClearSkyLux(double SunElevation, const Looks& L) { return L.ClearLux.AtLog(SunElevation); }

double ClearDirect(double SunElevation)
{
	if (SunElevation <= 0.0) return 0.0;
	// Kasten and Young's air mass, and the 0.7 ^ (AM ^ 0.678) clear-sky beam (Meinel).
	const double SinE = std::sin(SunElevation * DegToRad);
	const double AirMass = 1.0 / (SinE + 0.50572 * std::pow(SunElevation + 6.07995, -1.6364));
	return 1361.0 * std::pow(0.7, std::pow(AirMass, 0.678)) * SinE;
}

void SunAt(double LatDeg, int DayOfYear, double SolarHours, double& Elevation, double& Bearing)
{
	const double Lat = LatDeg * DegToRad, Dec = TorqueWeather::SunDeclination(DayOfYear) * DegToRad;
	const double H = 15.0 * (SolarHours - 12.0) * DegToRad;
	const double SinE = std::sin(Lat) * std::sin(Dec) + std::cos(Lat) * std::cos(Dec) * std::cos(H);
	Elevation = std::asin(std::min(std::max(SinE, -1.0), 1.0)) / DegToRad;
	Bearing = std::fmod(std::atan2(-std::cos(Dec) * std::sin(H), std::sin(Dec) * std::cos(Lat) - std::cos(Dec) * std::sin(Lat) * std::cos(H)) / DegToRad + 360.0, 360.0);
}

double Ev100(double Lux) { return std::log2(std::max(Lux, 1e-6) / 2.5); }

Sky Compute(const Inputs& In, const Looks& L)
{
	const TorqueWeather::State& W = In.Weather;
	const double E = In.SunElevation;
	Sky S;
	S.SunElevation = E;
	S.LightElevation = std::max(E, L.SunLowestDeg);

	// How much the cloud hides the sun: the layers' guess, and the feed's own sunshine when the sun is
	// the real one and high enough for the figure to mean much.
	double Block = TorqueWeather::SunBlock(W);
	if (In.bRealSun && W.DirectSun >= 0.0 && E > L.DirectTrustFromDeg)
	{
		const double Clear = ClearDirect(E);
		if (Clear > 1.0)
		{
			const double Measured = 1.0 - Clamp01(W.DirectSun / Clear);
			const double Trust = Clamp01(L.DirectTrust) * Smooth((E - L.DirectTrustFromDeg) / 6.0);
			Block = Lerp(Block, Measured, Trust);
		}
	}
	S.Block = Block;
	S.Dull = Smooth((Block - 0.2) / 0.7);
	S.WetLook = TorqueWeather::WetLook(W);

	// The light table.
	S.ClearLux = ClearSkyLux(E, L);
	const double Through = std::pow(std::max(Lerp(L.OvercastLux, L.RainCloudLux, S.WetLook), 1e-4), Block);
	S.GroundLux = S.ClearLux * Through;
	const double From = std::log10(std::max(L.DayFromLux, 1e-6)), Full = std::log10(std::max(L.DayFullLux, L.DayFromLux * 1.01));
	S.DayLight = Smooth((std::log10(std::max(S.ClearLux, 1e-9)) - From) / (Full - From));

	// The clouds drawn (stage 2) do their own shading while the sky is broken; only as the deck closes
	// does the sun itself dim and widen, so it is never dimmed twice.
	const double Deck = In.bClouds ? Smooth((Block - L.DeckFrom) / std::max(L.DeckTo - L.DeckFrom, 1e-3)) : 1.0;
	const double DullSun = S.Dull * Deck;
	S.SunShare = Lerp(1.0, Lerp(L.SunDimDull, L.SunDimWet, S.WetLook), DullSun);
	const double Fade = Smooth((E - L.SunFadeToDeg) / std::max(L.SunFadeFromDeg - L.SunFadeToDeg, 1e-3));
	S.SunScale = S.SunShare * Fade;
	S.SunAngle = Lerp(0.0, L.SunWideDeg, DullSun);   // (added to the level's own width)
	S.SunDisc = Clamp01(1.0 - S.SunAngle / std::max(L.DiscHideOverDeg, 1e-3));
	S.SkyLightScale = Lerp(1.0, L.SkyLightDull, S.Dull);
	S.ExposureStops = S.Dull * Lerp(L.ExposureDullStops, L.ExposureWetStops, S.WetLook);

	// The air: how far you can see across the whole range (crisp at 50 km, pale at 12), swelled by damp.
	const double Km = W.Visibility / 1000.0;
	const double ByKm = Km >= 8.0 ? L.HazeByKm.At(Km) : 1.0;
	const double Humid = 1.0 + (L.HumidSwell - 1.0) * Clamp01((W.Humidity - L.HumidFrom) / std::max(100.0 - L.HumidFrom, 1e-3));
	const double Air = ByKm * Humid;
	const double Drawn = In.bClouds ? 1.0 : 0.0;
	S.MieScale = Lerp(1.0, Lerp(L.DullMie, L.DullMieClouds, Drawn), S.Dull) * Air;
	S.RayleighScale = Lerp(1.0, Lerp(L.DullRayleigh, L.DullRayleighClouds, Drawn), S.Dull);
	S.FogScale = Lerp(1.0, Lerp(L.FogDull, L.FogWet, S.WetLook), S.Dull) * std::max(In.Haze, 0.0) * Air;
	S.FogGlow = Lerp(L.FogNightGlow, 1.0, S.DayLight);
	S.FogSunGlow = S.DayLight;

	// The cloud layers.
	const double Rain = TorqueWeather::RainShow(W);
	S.Low.Cover = Clamp01(W.LowCloud);
	S.Low.BaseAsl = std::max(std::min(W.CloudBase, L.LowBaseMostM), In.GroundAsl + L.LowBaseLeastM);
	const double DeckLow = Smooth((S.Low.Cover - 0.6) / 0.3);
	const double Heap = L.LowHeapM + std::min(std::max(W.Cape, 0.0) / 100.0 * L.CapeDepthPer100, L.CapeMostM);
	S.Low.TopAsl = S.Low.BaseAsl + Lerp(Heap, L.LowDeckM, DeckLow) + 1500.0 * Rain;
	S.Low.Wind = W.Wind850;
	S.Low.WindFrom = W.Wind850From;
	S.Mid.Cover = Clamp01(W.MidCloud);
	S.Mid.BaseAsl = std::max(L.MidBaseM, S.Low.BaseAsl + 300.0);
	S.Mid.TopAsl = S.Mid.BaseAsl + L.MidDepthM;
	S.Mid.Wind = W.Wind700;
	S.Mid.WindFrom = W.Wind700From;
	S.High.Cover = Clamp01(W.HighCloud);
	S.High.BaseAsl = 8000.0;
	S.High.TopAsl = 9500.0;
	S.High.Wind = W.Wind300;
	S.High.WindFrom = W.Wind300From;

	// The layer drawn (stage 2 draws one; stage 3 splits them): the low and middle cloud together, at
	// the low cloud's height unless the middle cloud is most of it.
	S.Cover = 1.0 - (1.0 - S.Low.Cover) * (1.0 - S.Mid.Cover);
	const double MidShare = S.Mid.Cover / std::max(S.Low.Cover + S.Mid.Cover, 1e-6);
	const double M = Smooth((MidShare - 0.6) / 0.3);
	S.BaseAsl = Lerp(S.Low.BaseAsl, S.Mid.BaseAsl, M);
	S.TopAsl = Lerp(S.Low.TopAsl, S.Mid.TopAsl, M);
	S.CoverShown = Clamp01(L.CoverShown.At(S.Cover));
	S.Shape = Smooth((S.Cover - 0.55) / 0.4) * (1.0 - Smooth(std::max(W.Cape, 0.0) / 800.0));
	S.Density = Lerp(Lerp(L.DensityFair, L.DensityDeck, S.Shape), L.DensityRain, Rain);
	S.Albedo = Lerp(L.AlbedoFair, L.AlbedoRain, std::max(Rain, S.WetLook * 0.5));
	{
		// Carried on the wind at its height (blowing towards: from the west, it goes east).
		auto Towards = [](double Speed, double FromDeg, double& East, double& North) {
			East = -std::sin(FromDeg * DegToRad) * Speed;
			North = -std::cos(FromDeg * DegToRad) * Speed;
		};
		double LowE = 0.0, LowN = 0.0, MidE = 0.0, MidN = 0.0;
		Towards(S.Low.Wind, S.Low.WindFrom, LowE, LowN);
		Towards(S.Mid.Wind, S.Mid.WindFrom, MidE, MidN);
		S.DriftE = Lerp(LowE, MidE, M);
		S.DriftN = Lerp(LowN, MidN, M);
	}
	S.CloudShadows = In.bClouds ? 1.0 - Deck : 0.0;
	S.LegacyShadow = In.bClouds ? 1.0 : L.LegacyShadow.At(W.Cloud);
	return S;
}

void StateFromJson(const TorqueFit::Json& J, TorqueWeather::State& S)
{
	if (!J.IsObject()) return;
	const std::string PresetName = J.Str("preset");
	if (PresetName == "sunny") S = TorqueWeather::Preset(TorqueWeather::Sunny);
	else if (PresetName == "overcast") S = TorqueWeather::Preset(TorqueWeather::Overcast);
	else if (PresetName == "wet") S = TorqueWeather::Preset(TorqueWeather::Wet);
	struct F { const char* Name; double TorqueWeather::State::*Field; double Scale; };
	static const F Fields[] = {
		{"cloud", &TorqueWeather::State::Cloud, 0.01}, {"low", &TorqueWeather::State::LowCloud, 0.01},
		{"mid", &TorqueWeather::State::MidCloud, 0.01}, {"high", &TorqueWeather::State::HighCloud, 0.01},
		{"rain", &TorqueWeather::State::Rain, 1.0}, {"snow", &TorqueWeather::State::Snow, 1.0},
		{"wind", &TorqueWeather::State::Wind, 1.0}, {"wind_from", &TorqueWeather::State::WindFrom, 1.0},
		{"gust", &TorqueWeather::State::Gust, 1.0}, {"air", &TorqueWeather::State::Air, 1.0},
		{"visibility", &TorqueWeather::State::Visibility, 1.0}, {"road_wet", &TorqueWeather::State::RoadWet, 1.0},
		{"cloud_base", &TorqueWeather::State::CloudBase, 1.0}, {"puddles", &TorqueWeather::State::Puddles, 1.0},
		{"snow_depth", &TorqueWeather::State::SnowDepth, 1.0}, {"humidity", &TorqueWeather::State::Humidity, 1.0},
		{"cape", &TorqueWeather::State::Cape, 1.0}, {"freezing", &TorqueWeather::State::FreezingLevel, 1.0},
		{"direct", &TorqueWeather::State::DirectSun, 1.0}, {"diffuse", &TorqueWeather::State::DiffuseSun, 1.0},
		{"wind850", &TorqueWeather::State::Wind850, 1.0}, {"wind850_from", &TorqueWeather::State::Wind850From, 1.0},
		{"wind700", &TorqueWeather::State::Wind700, 1.0}, {"wind700_from", &TorqueWeather::State::Wind700From, 1.0},
		{"wind300", &TorqueWeather::State::Wind300, 1.0}, {"wind300_from", &TorqueWeather::State::Wind300From, 1.0},
	};
	for (const F& Each : Fields)
		if (const TorqueFit::Json* V = J.Get(Each.Name); V && V->IsNumber()) S.*(Each.Field) = V->N * Each.Scale;
	if (const TorqueFit::Json* V = J.Get("code"); V && V->IsNumber()) S.Code = static_cast<int>(V->N);
}
} // namespace TorqueSky
