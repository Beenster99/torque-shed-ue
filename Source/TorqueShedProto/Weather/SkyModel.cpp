#include "SkyModel.h"

#include <initializer_list>

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
	{"low_heap_m", &Looks::LowHeapM}, {"cape_depth_per_100", &Looks::CapeDepthPer100},
	{"cape_most_m", &Looks::CapeMostM}, {"mid_base_m", &Looks::MidBaseM},
	{"low_base_least_m", &Looks::LowBaseLeastM}, {"low_base_most_m", &Looks::LowBaseMostM},
	{"density_rain", &Looks::DensityRain}, {"extinction_scale", &Looks::ExtinctionScale},
	{"albedo_fair", &Looks::AlbedoFair}, {"albedo_rain", &Looks::AlbedoRain},
	{"deck_from", &Looks::DeckFrom}, {"deck_to", &Looks::DeckTo},
	{"convect_from_deg", &Looks::ConvectFromDeg}, {"convect_full_deg", &Looks::ConvectFullDeg}, {"convect_cape", &Looks::ConvectCape},
	{"shower_cape_from", &Looks::ShowerCapeFrom}, {"shower_cape_to", &Looks::ShowerCapeTo},
	{"shower_rain_mm", &Looks::ShowerRainMm}, {"steady_rain_mm", &Looks::SteadyRainMm},
	{"towering_cape_from", &Looks::ToweringCapeFrom}, {"towering_cape_to", &Looks::ToweringCapeTo},
	{"stratus_cover_from", &Looks::StratusCoverFrom}, {"stratus_cover_to", &Looks::StratusCoverTo},
	{"stratus_base_from_m", &Looks::StratusBaseFromM}, {"stratus_base_to_m", &Looks::StratusBaseToM},
	{"broken_to", &Looks::BrokenTo}, {"broken_sheet", &Looks::BrokenSheet},
	{"mid_sheet_from", &Looks::MidSheetFrom}, {"mid_sheet_to", &Looks::MidSheetTo},
	{"veil_from", &Looks::VeilFrom}, {"veil_to", &Looks::VeilTo},
	{"high_base_m", &Looks::HighBaseM}, {"high_opacity", &Looks::HighOpacity},
	{"veil_widen_deg", &Looks::VeilWidenDeg}, {"glass_widen_deg", &Looks::GlassWidenDeg},
	{"glass_shadows", &Looks::GlassShadows}, {"halo_most", &Looks::HaloMost},
	{"fog_shallow_m", &Looks::FogShallowM}, {"fog_thick_m", &Looks::FogThickM}, {"fog_top_fade_m", &Looks::FogTopFadeM},
	{"mist_humid_from", &Looks::MistHumidFrom}, {"mist_humid_full", &Looks::MistHumidFull}, {"mist_calm_ms", &Looks::MistCalmMs},
	{"mist_windy_ms", &Looks::MistWindyMs}, {"mist_clear_to", &Looks::MistClearTo}, {"mist_gone_deg", &Looks::MistGoneDeg},
	{"mist_depth_m", &Looks::MistDepthM}, {"mist_extinction", &Looks::MistExtinction}, {"rainbow_sun_most_deg", &Looks::RainbowSunMostDeg}, {"rainbow_rain_mm", &Looks::RainbowRainMm},
};
struct NamedKindFigure { const char* Name; double KindLook::*Field; };
const NamedKindFigure KindFigures[] = {
	{"shape", &KindLook::Shape}, {"lumps", &KindLook::Lumps}, {"ragged", &KindLook::Ragged}, {"density", &KindLook::Density},
	{"base_dark", &KindLook::BaseDark}, {"depth_m", &KindLook::DepthM}, {"cape_depth", &KindLook::CapeDepth}, {"sheet", &KindLook::Sheet},
};
const char* const KindNames[KindCount] = {
	"cumulus", "stratocumulus", "stratus", "nimbostratus", "towering", "altocumulus", "altostratus", "cirrus", "cirrostratus",
};

// The "kinds" object, wherever the file puts it.
const TorqueFit::Json* FindKinds(const TorqueFit::Json& J)
{
	if (!J.IsObject()) return nullptr;
	for (const auto& KV : J.O)
	{
		if (KV.first == "kinds" && KV.second.IsObject()) return &KV.second;
		if (const TorqueFit::Json* Inner = FindKinds(KV.second)) return Inner;
	}
	return nullptr;
}
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
		if (KV.first == "kinds") continue;   // (read on its own: its rows share figure names)
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
	if (const Json* Kinds = FindKinds(Root))
		for (int K = 0; K < KindCount; ++K)
			if (const Json* Row = Kinds->Get(KindNames[K]); Row && Row->IsObject())
				for (const NamedKindFigure& F : KindFigures)
					if (const Json* V = Row->Get(F.Name); V && V->IsNumber()) Out.Kinds[K].*(F.Field) = V->N;
	return true;
}

const char* KindName(int K) { return K >= 0 && K < KindCount ? KindNames[K] : "none"; }

int Sky::Dominant(int Layer) const
{
	static const int LowKinds[] = {Cumulus, Stratocumulus, Stratus, Nimbostratus, Towering};
	static const int MidKinds[] = {Altocumulus, Altostratus, Nimbostratus};
	static const int HighKinds[] = {Cirrus, Cirrostratus};
	const int* List = Layer == 0 ? LowKinds : Layer == 1 ? MidKinds : HighKinds;
	const int Count = Layer == 0 ? 5 : Layer == 1 ? 3 : 2;
	const double LayerCover = Layer == 0 ? Low.Cover : Layer == 1 ? Mid.Cover : High.Cover;
	if (LayerCover < 0.05) return -1;
	int Best = -1;
	double Most = 0.0;
	for (int I = 0; I < Count; ++I)
		if (Weight[List[I]] > Most) { Most = Weight[List[I]]; Best = List[I]; }
	return Best;
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

	// Stage 4: fog with a top, valley mist, a rainbow. Radiation fog and mist need a calm, clear night
	// (or its morning); the sun climbing burns them off.
	{
		const double Valley = In.ValleyAsl >= 0.0 ? std::min(In.ValleyAsl, In.GroundAsl) : In.GroundAsl;
		const double Eye = In.ViewAsl >= 0.0 ? In.ViewAsl : In.GroundAsl + 2.0;
		const double LowMidCover = 1.0 - (1.0 - Clamp01(W.LowCloud)) * (1.0 - Clamp01(W.MidCloud));
		const double Calm = 1.0 - Smooth((W.Wind - L.MistCalmMs) / std::max(L.MistWindyMs - L.MistCalmMs, 0.1));
		const double ClearAbove = 1.0 - Smooth((LowMidCover - L.MistClearTo) / std::max(1.0 - L.MistClearTo, 0.05) * 1.5);
		const double Burn = 1.0 - Smooth(E / std::max(L.MistGoneDeg, 1.0));
		const double Damp = Smooth((W.Humidity - L.MistHumidFrom) / std::max(L.MistHumidFull - L.MistHumidFrom, 0.5));
		const double Radiation = Calm * std::max(ClearAbove, 1.0 - Smooth((W.LowCloud - 0.3) / 0.4));
		S.Mist = Clamp01(Damp * Calm * ClearAbove * Burn);
		S.MistTopAsl = Valley + L.MistDepthM * (0.5 + S.Mist);
		// Fog from the feed (under 1 km): lying in the valleys with a top when it's radiation fog.
		const double Foggy = 1.0 - Smooth((W.Visibility - 1000.0) / 3000.0);
		if (Foggy > 0.0 && Radiation > 0.5)
		{
			const double Thick = 1.0 - Smooth((W.Visibility - 200.0) / 800.0);
			S.FogTopAsl = Valley + Lerp(L.FogShallowM, L.FogThickM, Thick);
			// Above its top the fog's haze goes (the viewer looks out over it into clear air).
			const double Out = Smooth((Eye - S.FogTopAsl) / std::max(L.FogTopFadeM, 1.0));
			const double FogPart = TorqueWeather::FogBoost(W);
			if (FogPart > 1.0 && In.Haze <= FogPart + 1e-6)
				S.FogScale /= Lerp(1.0, FogPart, Out * Radiation);
		}
		// A rainbow: rain falling here or near, the sun out (not hidden by cloud) and lower than 42 degrees.
		const double Wet = Smooth(std::max(W.Rain, In.RainNear) / std::max(L.RainbowRainMm, 0.01));
		const double SunOut = 1.0 - Smooth((S.Block - 0.4) / 0.4);
		S.Rainbow = Wet * SunOut * Smooth(E / 3.0) * (1.0 - Smooth((E - (L.RainbowSunMostDeg - 6.0)) / 6.0));
	}

	// The cloud layers.
	const double Rain = TorqueWeather::RainShow(W);
	S.Low.Cover = Clamp01(W.LowCloud);
	S.Low.BaseAsl = std::max(std::min(W.CloudBase, L.LowBaseMostM), In.GroundAsl + L.LowBaseLeastM);
	S.Low.Wind = W.Wind850;
	S.Low.WindFrom = W.Wind850From;
	S.Mid.Cover = Clamp01(W.MidCloud);
	S.Mid.Wind = W.Wind700;
	S.Mid.WindFrom = W.Wind700From;
	S.High.Cover = Clamp01(W.HighCloud);
	S.High.BaseAsl = L.HighBaseM;
	S.High.TopAsl = L.HighBaseM + 500.0;
	S.High.Wind = W.Wind300;
	S.High.WindFrom = W.Wind300From;

	// The kinds (stage 3), by the plan's rules, each a band blended across. The low layer's share goes
	// first to towering cumulus (showers, or very unstable air by day), then nimbostratus (steady rain
	// under a closed sky), stratus (a closed sky low on the ground), cumulus (broken cloud, the sun's
	// heat), and what is left is stratocumulus.
	{
		const int C = W.Code;
		const bool bShowerCode = (C >= 80 && C <= 82) || C == 85 || C == 86 || C >= 95;
		const double Showery = bShowerCode ? 1.0 : Smooth((W.Cape - L.ShowerCapeFrom) / std::max(L.ShowerCapeTo - L.ShowerCapeFrom, 1.0));
		const double Heat = Smooth((E - L.ConvectFromDeg) / std::max(L.ConvectFullDeg - L.ConvectFromDeg, 1e-3));
		const double LowMid = 1.0 - (1.0 - S.Low.Cover) * (1.0 - S.Mid.Cover);
		const double BaseAgl = S.Low.BaseAsl - In.GroundAsl;
		const double Tow = std::max(Smooth(W.Rain / std::max(L.ShowerRainMm, 1e-3)) * Showery,
			Heat * Smooth((W.Cape - L.ToweringCapeFrom) / std::max(L.ToweringCapeTo - L.ToweringCapeFrom, 1.0)));
		const double Ns = Smooth(W.Rain / std::max(L.SteadyRainMm, 1e-3)) * (1.0 - Showery) * Smooth((LowMid - 0.7) / 0.25);
		const double St = Smooth((S.Low.Cover - L.StratusCoverFrom) / std::max(L.StratusCoverTo - L.StratusCoverFrom, 1e-3))
			* (1.0 - Smooth((BaseAgl - L.StratusBaseFromM) / std::max(L.StratusBaseToM - L.StratusBaseFromM, 1.0)));
		const double Broken = 1.0 - Smooth((S.Low.Cover - L.BrokenTo) / std::max(L.BrokenSheet - L.BrokenTo, 1e-3));
		const double Cu = Broken * Heat * Lerp(0.5, 1.0, Smooth(std::max(W.Cape, 0.0) / std::max(L.ConvectCape, 1.0)));
		double Rest = 1.0;
		auto Take = [&Rest](double Share) { const double Got = Rest * Clamp01(Share); Rest -= Got; return Got; };
		S.Weight[Towering] = Take(Tow);
		S.Weight[Nimbostratus] = Take(Ns);
		S.Weight[Stratus] = Take(St);
		S.Weight[Cumulus] = Take(Cu);
		S.Weight[Stratocumulus] = Rest;
		// The middle: nimbostratus fills it as it does the low; the rest a sheet (altostratus) as it
		// closes, small cells in rows (altocumulus) while it's broken.
		const double MidRest = 1.0 - S.Weight[Nimbostratus];
		const double Sheet = Smooth((S.Mid.Cover - L.MidSheetFrom) / std::max(L.MidSheetTo - L.MidSheetFrom, 1e-3));
		S.Weight[Altostratus] = MidRest * Sheet;
		S.Weight[Altocumulus] = MidRest * (1.0 - Sheet);
		// The high: wisps of cirrus, a veil of cirrostratus as it closes.
		S.Weight[Cirrostratus] = Smooth((S.High.Cover - L.VeilFrom) / std::max(L.VeilTo - L.VeilFrom, 1e-3));
		S.Weight[Cirrus] = 1.0 - S.Weight[Cirrostratus];
	}
	auto Blend = [&L, &S](std::initializer_list<int> Kinds) {
		KindLook Out{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
		double Sum = 0.0;
		for (int K : Kinds) Sum += S.Weight[K];
		for (int K : Kinds)
		{
			const double Wt = Sum > 1e-9 ? S.Weight[K] / Sum : 1.0 / static_cast<double>(Kinds.size());
			const KindLook& R = L.Kinds[K];
			Out.Shape += R.Shape * Wt; Out.Lumps += R.Lumps * Wt; Out.Ragged += R.Ragged * Wt; Out.Density += R.Density * Wt;
			Out.BaseDark += R.BaseDark * Wt; Out.DepthM += R.DepthM * Wt; Out.CapeDepth += R.CapeDepth * Wt; Out.Sheet += R.Sheet * Wt;
		}
		return Out;
	};
	const double Heap = L.LowHeapM + std::min(std::max(W.Cape, 0.0) / 100.0 * L.CapeDepthPer100, L.CapeMostM);
	S.LowDrawn.Look = Blend({Cumulus, Stratocumulus, Stratus, Nimbostratus, Towering});
	S.MidDrawn.Look = Blend({Altocumulus, Altostratus, Nimbostratus});
	S.Low.TopAsl = S.Low.BaseAsl + S.LowDrawn.Look.DepthM + S.LowDrawn.Look.CapeDepth * Heap;
	S.Mid.BaseAsl = std::max(L.MidBaseM, S.Low.BaseAsl + 300.0);
	// (nimbostratus is one thick cloud from the low base up: the middle layer joins it)
	S.Mid.BaseAsl = Lerp(S.Mid.BaseAsl, std::max(S.Low.TopAsl - 600.0, S.Low.BaseAsl + 300.0), S.Weight[Nimbostratus]);
	S.Mid.TopAsl = S.Mid.BaseAsl + std::max(S.MidDrawn.Look.DepthM, 300.0);
	// A wet look darkens both.
	const double WetGrey = std::max(Rain, S.WetLook * 0.5);
	auto Towards = [](double Speed, double FromDeg, double& East, double& North) {
		// Carried on the wind at its height (blowing towards: from the west, it goes east).
		East = -std::sin(FromDeg * DegToRad) * Speed;
		North = -std::cos(FromDeg * DegToRad) * Speed;
	};
	S.LowDrawn.Cover = S.Low.Cover;
	S.LowDrawn.CoverShown = Clamp01(L.CoverShown.At(S.Low.Cover));
	S.LowDrawn.Albedo = Lerp(L.AlbedoFair, L.AlbedoRain, WetGrey);
	Towards(S.Low.Wind, S.Low.WindFrom, S.LowDrawn.DriftE, S.LowDrawn.DriftN);
	S.MidDrawn.Cover = S.Mid.Cover;
	S.MidDrawn.CoverShown = Clamp01(L.CoverShown.At(S.Mid.Cover));
	S.MidDrawn.Albedo = Lerp(L.AlbedoFair, L.AlbedoRain, WetGrey * 0.7);
	Towards(S.Mid.Wind, S.Mid.WindFrom, S.MidDrawn.DriftE, S.MidDrawn.DriftN);
	Towards(S.High.Wind, S.High.WindFrom, S.HighDriftE, S.HighDriftN);
	S.LowDrawn.Look.Density = Lerp(S.LowDrawn.Look.Density, std::max(S.LowDrawn.Look.Density, L.DensityRain), Rain);

	// The volume: from the lowest drawn base to the highest drawn top, each layer a band in it.
	const bool bLow = S.Low.Cover > 0.01, bMid = S.Mid.Cover > 0.01;
	S.BaseAsl = bLow || !bMid ? S.Low.BaseAsl : S.Mid.BaseAsl;
	S.TopAsl = std::max(bLow || !bMid ? S.Low.TopAsl : 0.0, bMid ? S.Mid.TopAsl : 0.0);
	const double Span = std::max(S.TopAsl - S.BaseAsl, 1.0);
	auto Band = [&S, Span](const Layer& Ly, Sky::Drawn& D) {
		D.Bottom = Clamp01((Ly.BaseAsl - S.BaseAsl) / Span);
		D.Top = std::max(Clamp01((Ly.TopAsl - S.BaseAsl) / Span), D.Bottom + 1e-3);
	};
	Band(S.Low, S.LowDrawn);
	Band(S.Mid, S.MidDrawn);

	// The low layer's figures under their stage 2 names (the material's Cover, Density, Albedo, Shape).
	S.Cover = 1.0 - (1.0 - S.Low.Cover) * (1.0 - S.Mid.Cover);
	S.CoverShown = S.LowDrawn.CoverShown;
	S.Shape = bLow || !bMid ? S.LowDrawn.Look.Shape : S.MidDrawn.Look.Shape;
	S.Density = S.LowDrawn.Look.Density;
	S.Albedo = S.LowDrawn.Albedo;
	S.DriftE = bLow || !bMid ? S.LowDrawn.DriftE : S.MidDrawn.DriftE;
	S.DriftN = bLow || !bMid ? S.LowDrawn.DriftN : S.MidDrawn.DriftN;

	// The high sheet, and the sun through it: a veil makes a milky sun with a halo; a middle sheet a
	// ground glass sun with faint shadows.
	S.HighShown = Clamp01(L.CoverShown.At(S.High.Cover));
	S.HighSheet = Lerp(L.Kinds[Cirrus].Sheet, L.Kinds[Cirrostratus].Sheet, S.Weight[Cirrostratus]);
	const double Veil = S.Weight[Cirrostratus] * S.High.Cover;
	const double Glass = S.Weight[Altostratus] * S.Mid.Cover * (1.0 - S.Low.Cover);
	S.Halo = L.HaloMost * Veil * (1.0 - S.Cover) * Smooth(E / 5.0);
	S.SunAngle = std::min(S.SunAngle + L.VeilWidenDeg * Veil + L.GlassWidenDeg * Glass, std::max(L.SunWideDeg, S.SunAngle));
	S.SunDisc = Clamp01(1.0 - S.SunAngle / std::max(L.DiscHideOverDeg, 1e-3));
	S.CloudShadows = In.bClouds ? (1.0 - Deck) * (1.0 - (1.0 - L.GlassShadows) * Glass) : 0.0;
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
