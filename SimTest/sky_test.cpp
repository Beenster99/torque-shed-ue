// The sky (skies plan, stages 1 and 2): the light table, the sun through cloud (thin high cloud is no
// longer overcast), twilight, the air across the whole range of visibility, the cloud layer drawn, the
// presets' light kept as it always was, the shipped looks file, and the recorded days.
#include "SkyModel.h"
#include "WeatherModel.h"
#include "FitJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace TorqueSky;
using TorqueWeather::State;

static int Fails = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Fails;
}
static bool Near(double A, double B, double Tol) { return std::fabs(A - B) <= Tol; }
static double Smooth(double T) { T = T < 0.0 ? 0.0 : T > 1.0 ? 1.0 : T; return T * T * (3.0 - 2.0 * T); }
static std::string ReadFile(const std::string& Path)
{
	std::ifstream F(Path, std::ios::binary);
	std::stringstream Ss;
	Ss << F.rdbuf();
	return Ss.str();
}

static Sky At(const State& W, double Elevation, bool bClouds = false, bool bReal = false, const Looks& L = Looks())
{
	Inputs In;
	In.Weather = W;
	In.SunElevation = Elevation;
	In.bClouds = bClouds;
	In.bRealSun = bReal;
	In.GroundAsl = 300.0;
	return Compute(In, L);
}

// The light the game had before the sky had its own model (TorqueGameState::ApplyWeather, Nov 12), by day.
struct OldLight { double Sun, SkyLight, Exposure, Mie, Rayleigh, Fog; };
static OldLight Old(const State& S)
{
	const double Dull = Smooth((S.Cloud - 0.2) / 0.7), Wet = TorqueWeather::WetLook(S);
	auto L = [](double A, double B, double T) { return A + (B - A) * T; };
	return {L(1.0, L(0.14, 0.10, Wet), Dull), L(1.0, 2.5, Dull), Dull * L(1.6, 1.9, Wet), L(1.0, 12.0, Dull), L(1.0, 0.08, Dull), L(1.0, L(2.0, 3.0, Wet), Dull)};
}

static void PresetsKeepTheirLight(const Looks& L, const char* Which)
{
	bool bSame = true;
	for (int K = 0; K < 3; ++K)
	{
		const State W = TorqueWeather::Preset(K);
		const OldLight O = Old(W);
		const Sky S = At(W, 30.0, false, false, L);
		std::printf("  %s %s: sun %.3f (was %.3f), sky light %.2f (%.2f), exposure +%.2f (%.2f), mie %.2f (%.2f), rayleigh %.3f (%.3f), fog %.2f (%.2f)\n",
			Which, K == 0 ? "sunny" : K == 1 ? "overcast" : "wet", S.SunScale, O.Sun, S.SkyLightScale, O.SkyLight, S.ExposureStops, O.Exposure,
			S.MieScale, O.Mie, S.RayleighScale, O.Rayleigh, S.FogScale, O.Fog);
		bSame = bSame && Near(S.SunScale, O.Sun, 1e-6) && Near(S.SkyLightScale, O.SkyLight, 1e-6) && Near(S.ExposureStops, O.Exposure, 1e-6)
			&& Near(S.MieScale, O.Mie, 1e-6) && Near(S.RayleighScale, O.Rayleigh, 1e-6) && Near(S.FogScale, O.Fog, 1e-6);
	}
	Check(bSame, "the three presets give the light they always had");
}

int main()
{
	const Looks L;
	std::printf("The presets\n");
	PresetsKeepTheirLight(L, "default");

	std::printf("\nThe light table (real lux)\n");
	{
		const State Clear = TorqueWeather::Preset(TorqueWeather::Sunny);
		State Grey = Clear;   // an ordinary overcast: a full low deck, dry
		Grey.Cloud = 1.0; Grey.LowCloud = 1.0; Grey.MidCloud = 0.5;
		State Thick = Grey;   // thick rain cloud
		Thick.Rain = 3.0; Thick.RoadWet = 1.0;
		struct Moment { const char* Name; State W; double Elev, Lo, Hi; };
		const Moment Moments[] = {
			{"clear, sun at 60 (a summer noon)", Clear, 60.0, 90000.0, 110000.0},
			{"clear, sun at 58 (Hawes, midsummer noon)", Clear, 58.6, 88000.0, 108000.0},
			{"clear, sun at 45", Clear, 45.0, 68000.0, 84000.0},
			{"clear, sun at 30", Clear, 30.0, 47000.0, 57000.0},
			{"clear, sun at 20", Clear, 20.0, 30000.0, 36000.0},
			{"clear, sun at 12 (a November noon)", Clear, 12.4, 15000.0, 22000.0},
			{"clear, sun at 5", Clear, 5.0, 5400.0, 6600.0},
			{"clear, sunset (-0.83)", Clear, -0.83, 360.0, 440.0},
			{"clear, sun at -3", Clear, -3.0, 54.0, 66.0},
			{"clear, end of civil twilight (-6)", Clear, -6.0, 3.0, 3.8},
			{"clear, end of nautical twilight (-12)", Clear, -12.0, 0.007, 0.009},
			{"clear, night (-18)", Clear, -18.0, 0.0006, 0.0008},
			{"overcast, sun at 60", Grey, 60.0, 10000.0, 20000.0},
			{"overcast, sun at 45", Grey, 45.0, 8000.0, 15000.0},
			{"overcast, sun at 12", Grey, 12.4, 1500.0, 4000.0},
			{"overcast, sunset", Grey, -0.83, 30.0, 100.0},
			{"thick rain cloud, sun at 60", Thick, 60.0, 2000.0, 5000.0},
			{"thick rain cloud, sun at 45", Thick, 45.0, 1600.0, 4000.0},
			{"thick rain cloud, sun at 20", Thick, 20.0, 700.0, 2000.0},
			{"thick rain cloud, sun at 5", Thick, 5.0, 100.0, 400.0},
		};
		bool bAll = true;
		for (const Moment& M : Moments)
		{
			const Sky S = At(M.W, M.Elev);
			const bool Ok = S.GroundLux >= M.Lo && S.GroundLux <= M.Hi;
			std::printf("  %s %-42s %10.4g lux (EV100 %5.1f)  want %g .. %g\n", Ok ? "  " : "!!", M.Name, S.GroundLux, Ev100(S.GroundLux), M.Lo, M.Hi);
			bAll = bAll && Ok;
		}
		Check(bAll, "twenty moments give the light table (clear noon 100,000; overcast 10-20,000; rain cloud 2-5,000; sunset 400; civil twilight 3)");
		double Last = 0.0;
		bool bRises = true;
		for (double E = -20.0; E <= 90.0; E += 0.5)
		{
			const double Lux = At(Clear, E).GroundLux;
			bRises = bRises && Lux >= Last;
			Last = Lux;
		}
		Check(bRises, "the light rises with the sun, never dipping");
	}

	std::printf("\nThe sun through cloud\n");
	{
		State Veil = TorqueWeather::Preset(TorqueWeather::Sunny);   // the whole sky in thin high cloud
		Veil.Cloud = 1.0; Veil.LowCloud = 0.0; Veil.MidCloud = 0.0; Veil.HighCloud = 1.0;
		const Sky S = At(Veil, 35.0);
		std::printf("  a sky of cirrus: block %.2f, dull %.2f, sun %.2f (the old model: dull %.2f, sun %.2f)\n", S.Block, S.Dull, S.SunScale,
			Smooth((Veil.Cloud - 0.2) / 0.7), Old(Veil).Sun);
		Check(S.Dull < 0.05 && S.SunScale > 0.95 && TorqueWeather::Dullness(Veil) < 0.05, "thin high cloud no longer reads as overcast: a bright day with shadows");
		Check(TorqueWeather::NearestKind(Veil) == TorqueWeather::Sunny, "  and the bike and the HUD call it sunny");
		State Low = Veil;
		Low.LowCloud = 1.0;
		Check(At(Low, 35.0).Dull > 0.99, "a full low deck is overcast");
		State Mid = Veil;
		Mid.MidCloud = 1.0;
		Check(At(Mid, 35.0).Dull > 0.99, "so is a full middle deck (altostratus)");

		// The feed's sunshine against the cover's guess: the model says clear, the sun is behind something.
		State Hidden = TorqueWeather::Preset(TorqueWeather::Sunny);
		Hidden.DirectSun = 0.0;
		const Sky Guess = At(Hidden, 40.0, false, false), Real = At(Hidden, 40.0, false, true);
		std::printf("  no direct sun at 40 degrees on a 'clear' sky: block %.2f made clock, %.2f real sun\n", Guess.Block, Real.Block);
		Check(Guess.Block < 0.05 && Real.Block > 0.4 && Real.Block < 0.6, "the feed's sunshine moves the guess (halfway), only for the real sun");
		Hidden.DirectSun = ClearDirect(40.0);
		Check(At(Hidden, 40.0, false, true).Block < 0.05, "  and a clear sky's sunshine leaves it clear");
		Check(Near(ClearDirect(60.0), 800.0, 60.0) && ClearDirect(-1.0) == 0.0 && ClearDirect(10.0) < ClearDirect(30.0), "a clear sky's direct sunshine (about 800 W/m2 at 60 degrees)");
	}

	std::printf("\nTwilight\n");
	{
		const State Clear = TorqueWeather::Preset(TorqueWeather::Sunny);
		for (double E : {10.0, 6.0, 2.0, 0.0, -1.0, -3.0, -4.5, -6.0, -8.0, -12.0})
		{
			const Sky S = At(Clear, E);
			std::printf("  sun %+5.1f: day light %.2f, sun's light %.2f, the light points at %+.1f\n", E, S.DayLight, S.SunScale, S.LightElevation);
		}
		Check(At(Clear, 6.0).DayLight > 0.95 && At(Clear, 0.0).DayLight > 0.8, "full day light until the sun is down");
		Check(At(Clear, -3.0).DayLight > 0.3 && At(Clear, -3.0).DayLight < 0.7, "half light at -3: dusk lingers (the old light was out by -4)");
		Check(At(Clear, -6.0).DayLight < 0.15 && At(Clear, -8.0).DayLight < 0.02, "dark at the end of civil twilight");
		Check(At(Clear, -1.0).SunScale > 0.99 && At(Clear, -6.0).SunScale > 0.2 && At(Clear, -12.0).SunScale == 0.0,
			"the atmosphere stays lit by the sun below the horizon, fading by -10");
		Check(At(Clear, -5.0).LightElevation == -5.0 && At(Clear, -30.0).LightElevation == -10.0, "the light points at the real sun, down to -10");
		double E = 0.0, B = 0.0;
		SunAt(54.3, 172, 12.0, E, B);
		Check(Near(E, 90.0 - 54.3 + 23.4, 0.5) && Near(B, 180.0, 0.5), "the sun at a Hawes midsummer noon: 59 degrees, due south");
		SunAt(54.3, 355, 12.0, E, B);
		Check(Near(E, 90.0 - 54.3 - 23.4, 0.5), "  and midwinter: 12 degrees");
	}

	std::printf("\nThe air\n");
	{
		State Crisp = TorqueWeather::Preset(TorqueWeather::Sunny), Hazy = Crisp, Base = Crisp;
		Crisp.Visibility = 60000.0;
		Hazy.Visibility = 12000.0;
		const Sky C = At(Crisp, 30.0), H = At(Hazy, 30.0), B = At(Base, 30.0);
		std::printf("  haze: 60 km %.2f, 40 km %.2f, 12 km %.2f (times the level's)\n", C.FogScale, B.FogScale, H.FogScale);
		Check(C.FogScale < 0.8 * B.FogScale && C.MieScale < 0.8 * B.MieScale, "crisp polar air at 60 km: far hills sharper than the level's");
		Check(H.FogScale >= B.FogScale, "summer haze at 12 km is no clearer");
		State Damp = Base;
		Damp.Humidity = 100.0;
		Check(Near(At(Damp, 30.0).FogScale, 1.5 * B.FogScale, 1e-6), "saturated air swells the haze by half");
	}

	std::printf("\nThe cloud drawn (stage 2)\n");
	{
		bool bMono = true;
		double Last = -1.0;
		for (double C = 0.0; C <= 1.0001; C += 0.05)
		{
			const double Shown = L.CoverShown.At(C);
			bMono = bMono && Shown >= Last;
			Last = Shown;
		}
		Check(bMono && L.CoverShown.At(0.0) == 0.0 && L.CoverShown.At(1.0) == 1.0, "the cover drawn follows the real cover, clear to overcast");
		State Day = TorqueWeather::Preset(TorqueWeather::Sunny);
		Day.LowCloud = 1.0; Day.Cloud = 1.0;
		Day.CloudBase = 450.0;
		Inputs In;
		In.Weather = Day;
		In.SunElevation = 20.0;
		In.GroundAsl = 300.0;
		Sky S = Compute(In, L);
		std::printf("  a 450 m base over a 300 m dale: drawn %.0f to %.0f m, cover %.2f\n", S.BaseAsl, S.TopAsl, S.Cover);
		Check(Near(S.BaseAsl, 450.0, 1e-6) && S.Low.TopAsl > S.BaseAsl + 300.0 && S.Dominant(0) == Stratus, "the base is the dew point's: a stratus lid some hundreds of metres deep");
		Check(S.BaseAsl < 526.0, "  a 450 m base hides the top of Buttertubs (526 m)");
		In.GroundAsl = 400.0;
		Check(Near(Compute(In, L).BaseAsl, 550.0, 1e-6), "the base never sits on the ground under the viewer (150 m clear)");
		Day.CloudBase = 5000.0;
		In.Weather = Day;
		Check(Compute(In, L).BaseAsl == L.LowBaseMostM, "a dry day's 5 km base is held to a real low cloud's");
		State Wind = Day;
		Wind.Wind850 = 10.0; Wind.Wind850From = 270.0;
		In.Weather = Wind;
		S = Compute(In, L);
		Check(Near(S.DriftE, 10.0, 1e-6) && Near(S.DriftN, 0.0, 1e-6), "a westerly at cloud height carries the cloud east");
		State Alto = TorqueWeather::Preset(TorqueWeather::Sunny);
		Alto.Cloud = 1.0; Alto.MidCloud = 1.0; Alto.LowCloud = 0.0;
		In.Weather = Alto;
		Check(Compute(In, L).BaseAsl == L.MidBaseM, "a middle deck alone is drawn at its own height");
		State Cu = TorqueWeather::Preset(TorqueWeather::Sunny);
		Cu.LowCloud = 0.5; Cu.Cloud = 0.5; Cu.Cape = 600.0;
		In.Weather = Cu;
		S = Compute(In, L);
		const State Deck = TorqueWeather::Preset(TorqueWeather::Overcast);
		In.Weather = Deck;
		In.bClouds = true;
		const Sky D = Compute(In, L);
		In.Weather = Cu;
		const Sky Broken = Compute(In, L);
		std::printf("  cumulus (half cover, CAPE 600): shape %.2f, %0.f m deep; the overcast's middle deck: shape %.2f\n", S.Shape, S.Low.TopAsl - S.Low.BaseAsl, D.MidDrawn.Look.Shape);
		Check(S.Shape < 0.2 && D.MidDrawn.Look.Shape > 0.8 && S.Low.TopAsl - S.Low.BaseAsl > 2000.0, "heaped cumulus on an unstable day, a flat deck under overcast");
		Check(Broken.CloudShadows > 0.99 && Broken.SunScale > 0.99, "broken cloud drawn: its own shadows, the sun not dimmed as well");
		Check(D.CloudShadows < 0.01 && Near(D.SunScale, Old(Deck).Sun, 1e-6), "a closed deck: the sun dimmed as before, no shadows from it");
		Check(Broken.LegacyShadow == 1.0 && Near(At(Cu, 20.0).LegacyShadow, 0.6 - 0.15 * 0.8, 1e-6), "the old noise shadows only where clouds aren't drawn");
	}

	std::printf("\nThe kinds of cloud (stage 3)\n");
	{
		auto Make = [](double Low, double Mid, double High, double Rain, int Code, double Cape, double Base) {
			State W = TorqueWeather::Preset(TorqueWeather::Sunny);
			W.LowCloud = Low; W.MidCloud = Mid; W.HighCloud = High; W.Rain = Rain; W.Code = Code; W.Cape = Cape; W.CloudBase = Base;
			W.Cloud = 1.0 - (1.0 - Low) * (1.0 - Mid) * (1.0 - High);
			return W;
		};
		struct Case { const char* What; State W; double Sun; int Layer; int Want; };
		const Case Cases[] = {
			{"broken low cloud, unstable air, midday: cumulus", Make(0.4, 0.0, 0.0, 0.0, 2, 400.0, 1200.0), 40.0, 0, Cumulus},
			{"mostly covered, stable, a winter high: stratocumulus", Make(0.85, 0.0, 0.0, 0.0, 3, 0.0, 1100.0), 12.0, 0, Stratocumulus},
			{"broken cloud at night: stratocumulus, not cumulus", Make(0.5, 0.0, 0.0, 0.0, 2, 300.0, 1100.0), -10.0, 0, Stratocumulus},
			{"closed, base 450 m over a 300 m dale, drizzle: stratus", Make(1.0, 0.2, 0.0, 0.2, 53, 0.0, 450.0), 15.0, 0, Stratus},
			{"steady rain under a closed sky: nimbostratus", Make(1.0, 1.0, 0.5, 3.0, 63, 0.0, 700.0), 20.0, 0, Nimbostratus},
			{"  and it fills the middle too", Make(1.0, 1.0, 0.5, 3.0, 63, 0.0, 700.0), 20.0, 1, Nimbostratus},
			{"showers: towering cumulus", Make(0.6, 0.2, 0.0, 1.5, 81, 500.0, 900.0), 35.0, 0, Towering},
			{"broken middle cloud: altocumulus", Make(0.0, 0.4, 0.0, 0.0, 2, 0.0, 3000.0), 30.0, 1, Altocumulus},
			{"a closed middle sheet: altostratus", Make(0.0, 1.0, 0.2, 0.0, 3, 0.0, 3000.0), 30.0, 1, Altostratus},
			{"light high cloud: cirrus", Make(0.0, 0.0, 0.3, 0.0, 1, 0.0, 3000.0), 30.0, 2, Cirrus},
			{"heavy high cloud: cirrostratus", Make(0.0, 0.0, 0.95, 0.0, 2, 0.0, 3000.0), 30.0, 2, Cirrostratus},
		};
		bool bAll = true;
		for (const Case& C : Cases)
		{
			Inputs In;
			In.Weather = C.W;
			In.SunElevation = C.Sun;
			In.GroundAsl = 300.0;
			In.bClouds = true;
			const Sky S = Compute(In, L);
			const int Got = S.Dominant(C.Layer);
			std::printf("  %s %-58s %s (%.2f)\n", Got == C.Want ? "  " : "!!", C.What, KindName(Got), Got >= 0 ? S.Weight[Got] : 0.0);
			bAll = bAll && Got == C.Want;
		}
		Check(bAll, "the rule table picks each of the nine kinds");

		// Blended, never switched: the weights move smoothly as a figure sweeps across its band.
		double Jump = 0.0;
		Sky Prev{};
		for (int I = 0; I <= 200; ++I)
		{
			const double T = I / 200.0;
			Inputs In;
			In.Weather = Make(T, T, T, 0.0, 2, 200.0, 900.0);
			In.SunElevation = 25.0;
			In.GroundAsl = 300.0;
			const Sky S = Compute(In, L);
			if (I > 0)
				for (int K = 0; K < KindCount; ++K) Jump = std::max(Jump, std::fabs(S.Weight[K] - Prev.Weight[K]));
			Prev = S;
		}
		std::printf("  the biggest step in any kind's weight as the cover sweeps 0 to 100%% in 0.5%% steps: %.3f\n", Jump);
		Check(Jump < 0.05, "kinds blend across a band, never flip on a threshold");
		double Sum = 0.0;
		for (int K = Cumulus; K <= Towering; ++K) Sum += Prev.Weight[K];
		Check(Near(Sum, 1.0, 1e-9), "  a layer's weights add up to one");

		// The sun through cloud.
		Inputs In;
		In.SunElevation = 30.0;
		In.GroundAsl = 300.0;
		In.bClouds = true;
		In.Weather = Make(0.0, 0.0, 0.0, 0.0, 0, 0.0, 3000.0);
		const Sky Clear = Compute(In, L);
		In.Weather = Make(0.0, 0.0, 0.95, 0.0, 2, 0.0, 3000.0);
		const Sky Veil = Compute(In, L);
		In.Weather = Make(0.0, 1.0, 0.0, 0.0, 3, 0.0, 3000.0);
		const Sky Glass = Compute(In, L);
		std::printf("  sun width / disc / halo / shadows: clear %.2f %.2f %.2f %.2f; a veil %.2f %.2f %.2f %.2f; altostratus %.2f %.2f %.2f %.2f\n",
			Clear.SunAngle, Clear.SunDisc, Clear.Halo, Clear.CloudShadows, Veil.SunAngle, Veil.SunDisc, Veil.Halo, Veil.CloudShadows,
			Glass.SunAngle, Glass.SunDisc, Glass.Halo, Glass.CloudShadows);
		Check(Veil.Halo > 0.3 && Veil.SunDisc < 0.5 && Veil.SunDisc > 0.0 && Clear.Halo == 0.0, "a veil: a milky sun with a halo");
		Check(Glass.SunDisc == 0.0 && Glass.CloudShadows < 0.5 && Glass.Halo < 0.01, "altostratus: a ground glass sun, faint shadows, no halo");
		Check(Veil.HighShown > 0.9 && Veil.HighSheet > 0.9 && Compute([&] { Inputs I = In; I.Weather = Make(0, 0, 0.3, 0, 1, 0, 3000); return I; }(), L).HighSheet < 0.2,
			"  the high sheet: a veil at full cover, wisps when light");

		// Two layers in the one volume: each in its own band.
		In.Weather = Make(0.5, 0.6, 0.0, 0.0, 2, 100.0, 1000.0);
		const Sky Two = Compute(In, L);
		std::printf("  two layers: volume %.0f-%.0f m, low band %.2f-%.2f, middle band %.2f-%.2f\n", Two.BaseAsl, Two.TopAsl,
			Two.LowDrawn.Bottom, Two.LowDrawn.Top, Two.MidDrawn.Bottom, Two.MidDrawn.Top);
		Check(Two.LowDrawn.Bottom == 0.0 && Two.LowDrawn.Top < Two.MidDrawn.Bottom && Near(Two.MidDrawn.Top, 1.0, 1e-9), "low and middle cloud drawn as two bands, a gap between");
	}

	std::printf("\nThe shipped looks (Data/Sky/sky_looks.json)\n");
	{
		Looks File;
		std::string Why;
		const bool bRead = LoadLooks(ReadFile(std::string(TORQUE_SKY_DATA) + "/sky_looks.json"), File, &Why);
		Check(bRead, "it reads");
		PresetsKeepTheirLight(File, "file");
		Looks Bad;
		Check(!LoadLooks("{ nope", Bad) && Bad.OvercastLux == L.OvercastLux, "a broken file keeps the defaults");
		Looks Some;
		LoadLooks(R"({"light": {"overcast_lux": 0.2, "clear_lux": [[0, 1], [90, 2]]}, "cloud": {"deck_to": 0.9}})", Some);
		Check(Some.OvercastLux == 0.2 && Some.ClearLux.P.size() == 2 && Some.DeckTo == 0.9 && Some.RainCloudLux == L.RainCloudLux, "  a figure it names is taken; the rest keep theirs");
		Looks K;
		LoadLooks(R"({"cloud": {"kinds": {"stratus": {"depth_m": 250, "shape": 0.9}}, "shape": 7}})", K);
		Check(K.Kinds[Stratus].DepthM == 250.0 && K.Kinds[Stratus].Shape == 0.9 && K.Kinds[Cumulus].Shape == L.Kinds[Cumulus].Shape,
			"  a kind's row is read (and its figure names don't leak into the others)");
	}

	std::printf("\nThe recorded days (Data/Sky/days, real Open-Meteo hours over Wensleydale)\n");
	{
		// The kind expected in each layer (low, middle, high; -1 none, -2 either of two: see Or).
		struct Day { const char* Name; double MinDull, MaxDull; int Kinds[3]; int Or; };
		const Day Days[] = {
			{"clear_day", 0.0, 0.02, {-1, -1, -1}, -1}, {"high_veil", 0.0, 0.35, {-1, -1, Cirrostratus}, -1},
			{"fair_cumulus", 0.0, 1.0, {Cumulus, -1, Cirrostratus}, -1}, {"low_grey", 0.95, 1.0, {Stratus, Altostratus, -1}, -1},
			{"frontal_rain", 0.95, 1.0, {Nimbostratus, Nimbostratus, Cirrostratus}, -1}, {"foggy_dawn", 0.9, 1.0, {Stratus, Altostratus, Cirrostratus}, Stratocumulus},
			{"storm", 0.0, 1.0, {Towering, Altocumulus, -1}, -1}, {"dusk_clear", 0.0, 0.02, {-1, -1, -1}, -1},
		};
		bool bAll = true;
		for (const Day& D : Days)
		{
			const std::string Text = ReadFile(std::string(TORQUE_SKY_DATA) + "/days/" + D.Name + ".json");
			State W;
			std::string Why;
			TorqueFit::Json Root;
			if (!TorqueWeather::ParseOpenMeteo(Text, W, nullptr, &Why) || !TorqueFit::Json::Parse(Text, Root))
			{
				std::printf("  !! %s: %s\n", D.Name, Why.c_str());
				bAll = false;
				continue;
			}
			const TorqueFit::Json* Rec = Root.Get("recorded");
			const double Elev = Rec ? Rec->Num("sun_elevation", 30.0) : 30.0;
			Inputs In;
			In.Weather = W;
			In.SunElevation = Elev;
			In.bRealSun = true;
			In.GroundAsl = Root.Num("elevation", 300.0);
			In.bClouds = true;
			const Sky S = Compute(In, L);
			bool bKinds = true;
			for (int Ly = 0; Ly < 3; ++Ly)
			{
				const int Got = S.Dominant(Ly);
				bKinds = bKinds && (Got == D.Kinds[Ly] || (Ly == 0 && D.Or >= 0 && Got == D.Or));
			}
			std::printf("     kinds: low %s, middle %s, high %s\n", KindName(S.Dominant(0)), KindName(S.Dominant(1)), KindName(S.Dominant(2)));
			const bool Ok = S.Dull >= D.MinDull && S.Dull <= D.MaxDull && bKinds;
			std::printf("  %s %-13s sun %5.1f: cloud %3.0f%% (low %3.0f mid %3.0f high %3.0f), direct %4.0f W/m2 -> block %.2f, dull %.2f (old %.2f), %7.0f lux, cloud drawn %.2f at %4.0f-%4.0f m, wind aloft %2.0f m/s from %3.0f\n",
				Ok ? "  " : "!!", D.Name, Elev, W.Cloud * 100.0, W.LowCloud * 100.0, W.MidCloud * 100.0, W.HighCloud * 100.0, W.DirectSun, S.Block, S.Dull,
				Smooth((W.Cloud - 0.2) / 0.7), S.GroundLux, S.CoverShown, S.BaseAsl, S.TopAsl, W.Wind850, W.Wind850From);
			bAll = bAll && Ok;
		}
		Check(bAll, "each recorded sky reads as the sky it was (a high veil no longer overcast, a low grey day dull), and the rules pick its kinds");
	}

	std::printf("\nThe weather state keeps the sky's figures\n");
	{
		State W = TorqueWeather::Preset(TorqueWeather::Overcast);
		W.Cape = 420.0; W.DirectSun = 123.0; W.Wind700From = 300.0; W.HighCloud = 0.35;
		State Back;
		Check(TorqueWeather::Load(TorqueWeather::Save(W), Back) && Back.Cape == 420.0 && Back.DirectSun == 123.0 && Back.Wind700From == 300.0
			&& Near(Back.HighCloud, 0.35, 1e-6) && Near(Back.MidCloud, W.MidCloud, 1e-6), "saved and loaded");
		State Old1;
		Check(TorqueWeather::Load("w1 0.800 0.000 0.000 6.000 225.0 10.000 11.00 20000 0.000 3 0.600 1500 0.000", Old1) && Old1.MidCloud == 0.8 && Old1.HighCloud == 0.0,
			"an old save (no layers) reads with its light as it was");
		const State Half = TorqueWeather::Blend(TorqueWeather::Preset(TorqueWeather::Sunny), W, 0.5);
		Check(Near(Half.MidCloud, 0.5 * W.MidCloud, 1e-6) && Half.DirectSun < 0.0, "blends (a figure known on one side only stays unknown)");
		TorqueFit::Json J;
		TorqueFit::Json::Parse(R"({"preset": "wet", "high": 80, "cape": 300, "wind850_from": 90})", J);
		State FromJ;
		StateFromJson(J, FromJ);
		Check(FromJ.Rain == 1.0 && Near(FromJ.HighCloud, 0.8, 1e-9) && FromJ.Cape == 300.0 && FromJ.Wind850From == 90.0, "a sky named in JSON (the bench's)");
	}

	std::printf(Fails ? "\n%d FAILED\n" : "\nall passed\n", Fails);
	return Fails ? 1 : 0;
}
