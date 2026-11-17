// The sky (skies plan, stages 1 and 2): the light table, the sun through cloud (thin high cloud is no
// longer overcast), twilight, the air across the whole range of visibility, the cloud layer drawn, the
// presets' light kept as it always was, the shipped looks file, and the recorded days.
#include "SkyModel.h"
#include "NightSky.h"
#include "StormSky.h"
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
		// (build 147: fair weather cumulus 600 m deep before CAPE, was 1000; CAPE 600 still heaps it well over 1.5 km)
		std::printf("  cumulus (half cover, CAPE 600): shape %.2f, %0.f m deep; the overcast's middle deck: shape %.2f\n", S.Shape, S.Low.TopAsl - S.Low.BaseAsl, D.MidDrawn.Look.Shape);
		Check(S.Shape < 0.2 && D.MidDrawn.Look.Shape > 0.8 && S.Low.TopAsl - S.Low.BaseAsl > 1500.0, "heaped cumulus on an unstable day, a flat deck under overcast");
		Check(Broken.CloudShadows > 0.99 && Broken.SunScale > 0.99, "broken cloud drawn: its own shadows, the sun not dimmed as well");
		Check(D.CloudShadows > 0.99 && D.SunScale > 0.99, "a closed deck drawn: the sun stays whole and the deck shades it (its underside lit grey, not by the blue sky alone)");
		{
			Looks Old2 = L;
			Old2.DeckDimsSun = 1.0;
			const Sky D2 = Compute(In, Old2);
			In.Weather = Deck;
			const Sky D3 = Compute(In, Old2);
			Check(D3.CloudShadows < 0.01 && Near(D3.SunScale, Old(Deck).Sun, 1e-6) && D2.SunScale > 0.99, "  (deck_dims_sun 1: the stage 2 way, the sun dimmed, no shadows)");
			In.Weather = Cu;
		}
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
		Check(Glass.SunDisc == 0.0 && Glass.SunAngle > Veil.SunAngle && Glass.Halo < 0.01, "altostratus: a ground glass sun, no halo");
		Check(Veil.HighShown > 0.9 && Veil.HighSheet > 0.9 && Compute([&] { Inputs I = In; I.Weather = Make(0, 0, 0.3, 0, 1, 0, 3000); return I; }(), L).HighSheet < 0.2,
			"  the high sheet: a veil at full cover, wisps when light");

		// Two layers in the one volume: each in its own band.
		In.Weather = Make(0.5, 0.6, 0.0, 0.0, 2, 100.0, 1000.0);
		const Sky Two = Compute(In, L);
		std::printf("  two layers: volume %.0f-%.0f m, low band %.2f-%.2f, middle band %.2f-%.2f\n", Two.BaseAsl, Two.TopAsl,
			Two.LowDrawn.Bottom, Two.LowDrawn.Top, Two.MidDrawn.Bottom, Two.MidDrawn.Top);
		Check(Two.LowDrawn.Bottom == 0.0 && Two.LowDrawn.Top < Two.MidDrawn.Bottom && Near(Two.MidDrawn.Top, 1.0, 1e-9), "low and middle cloud drawn as two bands, a gap between");
	}

	std::printf("\nWeather you can see coming (stage 4): fog with a top, valley mist, rainbows\n");
	{
		State Still = TorqueWeather::Preset(TorqueWeather::Sunny);
		Still.LowCloud = 0.0; Still.MidCloud = 0.0; Still.Cloud = 0.05; Still.Wind = 1.0; Still.Humidity = 97.0; Still.Visibility = 400.0;
		Inputs In;
		In.Weather = Still;
		In.SunElevation = 2.0;
		In.GroundAsl = 250.0;
		In.ValleyAsl = 250.0;
		In.Haze = TorqueWeather::FogBoost(Still);
		const Sky Down = Compute(In, L);
		In.GroundAsl = 600.0;
		In.ViewAsl = 602.0;
		const Sky Up = Compute(In, L);
		std::printf("  a still, clear dawn, 400 m visibility: mist %.2f to %.0f m, fog top %.0f m; fog x%.1f in it, x%.1f on the hill above\n",
			Down.Mist, Down.MistTopAsl, Down.FogTopAsl, Down.FogScale, Up.FogScale);
		Check(Down.Mist > 0.7 && Down.FogTopAsl > 250.0 && Down.FogTopAsl < 400.0, "radiation fog lies in the valley with a top, mist in the hollows");
		Check(Up.FogScale < Down.FogScale / 5.0, "  a hill road climbs out of it into clear air");
		State Windy = Still;
		Windy.Wind = 7.0;
		In.Weather = Windy;
		In.GroundAsl = 250.0; In.ViewAsl = -1.0;
		const Sky Blown = Compute(In, L);
		Check(Blown.Mist < 0.05 && Blown.FogTopAsl < 0.0, "  but fog on a wind is deep, and no mist forms");
		In.Weather = Still;
		In.SunElevation = 25.0;
		Check(Compute(In, L).Mist < 0.01, "  the mist burns off as the sun climbs");

		State Shower = TorqueWeather::Preset(TorqueWeather::Sunny);
		Shower.Rain = 1.0; Shower.LowCloud = 0.4; Shower.Cloud = 0.4; Shower.Code = 80;
		Inputs R;
		R.Weather = Shower;
		R.SunElevation = 20.0;
		const double Bow = Compute(R, L).Rainbow;
		R.SunElevation = 50.0;
		const double High = Compute(R, L).Rainbow;
		R.SunElevation = 20.0;
		R.Weather = TorqueWeather::Preset(TorqueWeather::Wet);
		const double Grey = Compute(R, L).Rainbow;
		R.Weather = TorqueWeather::Preset(TorqueWeather::Sunny);
		R.RainNear = 2.0;
		const double Near = Compute(R, L).Rainbow;
		std::printf("  rainbow: a shower with the sun at 20 deg %.2f, at 50 deg %.2f, under a grey wet sky %.2f, a shower near on a sunny day %.2f\n", Bow, High, Grey, Near);
		Check(Bow > 0.5 && High < 0.01 && Grey < 0.05 && Near > 0.5, "a rainbow when it rains with the sun out and low behind you");
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

	std::printf("\nStage 4: under a low base, the weather map, rain shafts\n");
	{
		const Looks& L4 = L;
		Inputs In;
		In.Weather = TorqueWeather::Preset(TorqueWeather::Overcast);
		In.Weather.LowCloud = 1.0; In.Weather.CloudBase = 700.0;
		In.SunElevation = 30.0; In.bClouds = true; In.GroundAsl = 300.0;
		In.ViewAsl = 302.0;
		const Sky Low = Compute(In, L4);
		In.ViewAsl = 650.0;
		const Sky Up = Compute(In, L4);
		Check(Low.BaseFog < 0.01 && Up.BaseFog > 0.5 && Up.FogScale > 1.5 * Low.FogScale, "riding up under a closed 700 m base, the air thickens into it");
		In.Weather.LowCloud = 0.3;
		Check(Compute(In, L4).BaseFog < 0.01, "  not under broken cloud");

		// A wide ring with a heavy shower 24 km east of the centre and broken cloud elsewhere; calm.
		TorqueWeather::Grid Wide;
		Wide.N = 7; Wide.Lat = 54.3; Wide.Lon = -2.2; Wide.StepM = 12000.0;
		for (int I = 0; I < 49; ++I)
		{
			State P = TorqueWeather::Preset(TorqueWeather::Sunny);
			P.LowCloud = 0.3; P.Cloud = 0.3; P.Wind = 0.0;
			if (I == 3 * 7 + 5) { P.Rain = 5.0; P.LowCloud = 0.9; P.Cloud = 0.9; P.Cape = 900.0; }
			Wide.Points.push_back(P);
			Wide.Elevation.push_back(300.0);
		}
		MapGrids G;
		G.Wide = &Wide;
		const WeatherMap M = PaintWeatherMap(G, Wide.Lat, Wide.Lon, L4);
		const int Mid = M.Px / 2;
		const int ShowerCol = static_cast<int>(std::lround((24000.0 / M.SizeM + 0.5) * M.Px - 0.5));
		auto Tex = [&M](int Row, int Col, int Ch) { return M.Bgra[(static_cast<size_t>(Row) * M.Px + Col) * 4 + Ch] / 255.0; };
		Check(M.Valid() && M.Px == 64 && Near(M.HereLow, 0.3, 0.01), "the map: 64 x 64 over 90 km, the camera's cover at its centre");
		Check(Tex(Mid, ShowerCol, 0) > 0.4 && Tex(Mid, ShowerCol, 2) > 0.8 && Tex(Mid, Mid, 0) == 0.0 && Near(Tex(Mid, Mid, 2), 0.3, 0.01),
			"  the shower 24 km east is wet and cloudy on the map, the centre dry and broken");
		Check(Tex(Mid, M.Px - 1, 2) < 0.5 && Tex(0, Mid, 2) < 0.35, "  row 0 the north, the last column the east");
		const std::vector<RainShaft> Shafts = FindRainShafts(G, Wide.Lat, Wide.Lon, L4);
		bool bEast = !Shafts.empty();
		for (const RainShaft& R : Shafts) bEast = bEast && R.East > 12000.0 && std::fabs(R.North) < 12000.0 && R.Strength > 0.3 && R.RadiusM > 500.0;
		Check(bEast && Shafts.size() <= 8, "rain shafts stand under the shower (east), none where it's dry, at most 8");
		Check(FindRainShafts(G, Wide.Lat, Wide.Lon + 24000.0 / 65000.0, L4).size() <= Shafts.size(), "  none closer than 2.5 km (riding into the shower)");
		// Carried on the wind: a westerly 10 m/s for 20 minutes moves them 12 km east.
		for (State& P : Wide.Points) { P.Wind = 10.0; P.WindFrom = 270.0; }
		G.WideDriftS = 1200.0;
		const std::vector<RainShaft> Later = FindRainShafts(G, Wide.Lat, Wide.Lon, L4);
		double E0 = 0.0, E1 = 0.0;
		for (const RainShaft& R : Shafts) E0 += R.East / static_cast<double>(Shafts.size());
		for (const RainShaft& R : Later) E1 += R.East / static_cast<double>(std::max<size_t>(Later.size(), 1));
		Check(!Later.empty() && E1 > E0 + 6000.0, "  and drift east on a westerly with their shower");
		MapGrids None;
		Check(!PaintWeatherMap(None, 54.3, -2.2, L4).Valid() && FindRainShafts(None, 54.3, -2.2, L4).empty(), "no grids (not live): no map, no shafts");
	}

	std::printf("\nStage 5: the moon and the stars' clock\n");
	{
		auto Sep = [](double E1, double B1, double E2, double B2) {
			const double R = 3.14159265358979 / 180.0;
			const double C = std::sin(E1 * R) * std::sin(E2 * R) + std::cos(E1 * R) * std::cos(E2 * R) * std::cos((B1 - B2) * R);
			return std::acos(std::max(-1.0, std::min(1.0, C))) / R;
		};
		// The total solar eclipse of 8 April 2024, totality over Dallas (32.78 N, 96.80 W) at 18:40 UT: the moon on the sun.
		const double Eclipse = 1712601600.0;
		const TorqueNight::Moon Mo = TorqueNight::MoonAt(Eclipse, 32.78, -96.80);
		double SunE = 0.0, SunB = 0.0;
		TorqueNight::HorizonOf(Mo.SunRaDeg, Mo.SunDecDeg, Eclipse, 32.78, -96.80, SunE, SunB);
		std::printf("  eclipse: moon %.2f / %.2f, sun %.2f / %.2f (%.2f apart), lit %.3f\n", Mo.Elevation, Mo.Bearing, SunE, SunB, Sep(Mo.Elevation, Mo.Bearing, SunE, SunB), Mo.Lit);
		Check(Sep(Mo.Elevation, Mo.Bearing, SunE, SunB) < 0.3 && Mo.Lit < 0.01, "the moon covers the sun over Dallas at totality on 8 April 2024 (within 0.3 degrees), new");
		Check(Near(SunE, 64.5, 1.0) && Near(SunB, 182.0, 6.0), "  the sun 64 degrees up, about due south (Dallas' solar noon is 18:29 UT)");
		// The total lunar eclipse of 14 March 2025, 06:58 UT: full, opposite the sun.
		const TorqueNight::Moon Full = TorqueNight::MoonAt(1741935480.0, 54.3, -2.2);
		Check(Full.Lit > 0.999 && Full.Elongation > 178.5, "full and opposite the sun at the lunar eclipse of 14 March 2025");
		const TorqueNight::Moon Quarter = TorqueNight::MoonAt(1705549980.0, 54.3, -2.2);
		const TorqueNight::Moon Last = TorqueNight::MoonAt(1706205240.0, 54.3, -2.2);
		Check(Near(Quarter.Lit, 0.5, 0.04) && Quarter.bWaxing && Last.Lit > 0.99, "first quarter 18 January 2024 half lit and waxing; full on the 25th");
		Check(Near(TorqueNight::MoonBrightness(0.0), 1.0, 1e-9) && TorqueNight::MoonBrightness(90.0) < 0.15 && TorqueNight::MoonBrightness(90.0) > 0.05,
			"  a half moon gives about a tenth of a full one's light");
		// The Pole Star (RA 2h 31m 49s, Dec +89 15' 51\") stands as high as the latitude, any time.
		bool bPole = true;
		for (double T = 1.7e9; T < 1.7e9 + 86400.0; T += 3600.0)
		{
			double E = 0.0, B = 0.0;
			TorqueNight::HorizonOf(37.954, 89.264, T, 54.3, -2.2, E, B);
			bPole = bPole && std::fabs(E - 54.3) < 0.8 && (B < 2.0 || B > 358.0);
		}
		Check(bPole, "the Pole Star stands at the latitude (54.3) all night, due north");
		// The night's rules.
		const Looks LN;
		Inputs In;
		In.Weather = TorqueWeather::Preset(TorqueWeather::Sunny);
		In.Weather.Cloud = 0.0; In.Weather.LowCloud = 0.0; In.Weather.HighCloud = 0.0;
		In.bClouds = true;
		In.MoonPhaseAngle = 0.0;
		In.MoonElevation = 40.0;
		In.SunElevation = -20.0;
		const Sky FullHigh = Compute(In, LN);
		In.SunElevation = -8.0;
		const Sky Dusk = Compute(In, LN);
		In.SunElevation = -20.0; In.MoonPhaseAngle = 90.0;
		const Sky Half = Compute(In, LN);
		In.MoonElevation = -10.0;
		const Sky NoMoon = Compute(In, LN);
		In.SunElevation = 30.0; In.MoonElevation = 30.0; In.MoonPhaseAngle = 0.0;
		const Sky Day = Compute(In, LN);
		Check(FullHigh.MoonTakeover == 1.0 && Near(FullHigh.MoonLight, LN.MoonLightOfSun, 1e-4) && Dusk.MoonTakeover == 0.0,
			"a full moon high at night takes the light; not while the sun's twilight lasts");
		Check(Half.MoonLight < 0.15 * FullHigh.MoonLight && NoMoon.MoonLight == 0.0 && NoMoon.MoonTakeover == 0.0, "  a half moon gives a tenth; no moon, none");
		Check(FullHigh.RayleighScale < 0.3 && FullHigh.MieScale < 0.3, "  the sky it lights stays a dark blue (its air held down)");
		Check(NoMoon.Stars > 0.99 && NoMoon.MilkyWay > 0.9 && FullHigh.Stars < 0.8 && FullHigh.MilkyWay < 0.5 && Dusk.Stars < 0.3 && Day.Stars == 0.0,
			"the stars and the Milky Way at a moonless night; washed by a full moon; none by day, few at dusk");
		Check(Day.MoonDisc > 0.0 && Day.MoonTakeover == 0.0, "  the moon's disc shows by day too (its light doesn't)");
		// Sidereal time: J2000.0 (2000-01-01 12:00 UT) at Greenwich is 280.46 degrees.
		Check(Near(TorqueNight::SiderealDeg(946728000.0, 0.0), 280.46, 0.01), "  the stars' clock at J2000");
	}

	std::printf("\nThe preset skies (Data/Sky/presets.json, Settings > Gameplay > Preset sky)\n");
	{
		TorqueFit::Json Root;
		const bool bRead = TorqueFit::Json::Parse(ReadFile(std::string(TORQUE_SKY_DATA) + "/presets.json"), Root);
		const std::vector<NamedSky> Skies = SkiesFromJson(Root);
		const TorqueFit::Json* List = Root.Get("skies");
		Check(bRead && List && List->IsArray() && Skies.size() == List->A.size() && Skies.size() >= 30, "every preset sky reads (30 or more, no key twice)");
		bool bDays = true, bSane = true;
		for (const NamedSky& Each : Skies)
		{
			if (!Each.Recorded.empty())
			{
				State Day;
				bDays = bDays && TorqueWeather::ParseOpenMeteo(ReadFile(std::string(TORQUE_SKY_DATA) + "/days/" + Each.Recorded + ".json"), Day);
				continue;
			}
			const State& W = Each.Weather;
			const bool bWetRain = W.Rain < 0.5 || W.RoadWet >= 0.5;   // (heavy rain on a dry road reads wrong)
			const Sky S = At(W, Each.HasSun() ? Each.Sun : 35.0, true);
			const bool Ok = !Each.Label.empty() && !Each.Group.empty() && W.Cloud >= 0.0 && W.Cloud <= 1.0 && W.LowCloud <= W.Cloud + 1e-9
				&& W.MidCloud <= 1.0 && W.HighCloud <= 1.0 && bWetRain && std::isfinite(S.GroundLux) && std::isfinite(S.FogScale);
			if (!Ok) std::printf("  !! %s\n", Each.Key.c_str());
			bSane = bSane && Ok;
		}
		Check(bSane, "  each has a label, a group, sane cover, and wet roads under real rain");
		Check(bDays, "  the recorded days it names are there");
		const NamedSky* Fog = FindSky(Skies, "valley_fog");
		const NamedSky* Gold = FindSky(Skies, "golden_hour");
		Check(Fog && Fog->HasSun() && !Fog->bEvening && Gold && Gold->bEvening && FindSky(Skies, "full_moon") && FindSky(Skies, "full_moon")->Moon == "full" && FindSky(Skies, "cumulus") && !FindSky(Skies, "cumulus")->HasSun(),
			"  dawn and evening skies move the clock; the rest keep it");
		const NamedSky* Far = FindSky(Skies, "distant_showers");
		Check(Far && Far->Showers == 7 && Far->ShowerRainMm == 5.0, "  showers round about read");
		if (Far)
		{
			const TorqueWeather::Grid Ring = ShowersAround(Far->Weather, 54.3, -2.2, Far->Showers, Far->ShowerRainMm, 7u);
			int Wet = 0;
			for (const State& P : Ring.Points) Wet += P.Rain > 1.0 ? 1 : 0;
			MapGrids RG;
			RG.Wide = &Ring;
			Check(Ring.Valid() && Wet == 7 && Ring.Centre().Rain == 0.0 && Near(MapWeatherAt(RG, 54.3, -2.2).LowCloud, Far->Weather.LowCloud, 1e-9)
				&& !FindRainShafts(RG, 54.3, -2.2, L).empty(), "  a made-up ring: 7 showers, dry here (the preset's own weather), rain shafts under them");
		}
		const NamedSky* Storm = FindSky(Skies, "thunderstorm");
		Check(Storm && At(Storm->Weather, 35.0, true).Dominant(0) == Towering, "  the thunderstorm draws towering cloud");
		const NamedSky* Grey = FindSky(Skies, "low_grey");
		Check(Grey && At(Grey->Weather, 35.0, true).Dominant(0) == Stratus, "  low grey draws a stratus lid");
		const NamedSky* Veil = FindSky(Skies, "cirrostratus");
		Check(Veil && At(Veil->Weather, 35.0, true).Dominant(2) == Cirrostratus, "  the cirrostratus veil draws cirrostratus");
	}

	// Stage 7: storms and rare skies.
	{
		using namespace TorqueStorm;
		TorqueFit::Json Root;
		TorqueFit::Json::Parse(ReadFile(std::string(TORQUE_SKY_DATA) + "/presets.json"), Root);
		const std::vector<NamedSky> Skies = SkiesFromJson(Root);
		const NamedSky* Storm = FindSky(Skies, "thunderstorm");
		const NamedSky* Cu = FindSky(Skies, "cumulus");
		if (Storm && Cu)
		{
			const Sky T = At(Storm->Weather, 35.0, true);
			const Sky F = At(Cu->Weather, 35.0, true);
			std::printf("  thunderstorm: thunder %.2f, top %.0f m, anvil %.2f; cumulus: thunder %.2f, top %.0f m\n", T.Thunder, T.TopAsl, T.Anvil, F.Thunder, F.TopAsl);
			Check(T.Thunder > 0.9 && T.TopAsl > 9000.0 && T.Anvil > 0.5, "a thunderstorm towers to 9 km and more, with an anvil");
			Check(F.Thunder == 0.0 && F.Anvil == 0.0 && F.TopAsl < 5000.0, "  fair cumulus doesn't");
		}

		// A thunderstorm day replayed: storms over the eastern columns of a wide ring, dry elsewhere.
		TorqueWeather::Grid Ring;
		Ring.N = 7;
		Ring.Lat = 54.3;
		Ring.Lon = -2.2;
		Ring.StepM = 12000.0;
		for (int I = 0; I < 49; ++I)
		{
			State P = TorqueWeather::Preset(TorqueWeather::Overcast);
			P.Wind = 0.0;
			if (I % 7 >= 5) { P.Code = 95; P.Cape = 1800.0; P.Rain = 10.0; P.CloudBase = 900.0; }
			Ring.Points.push_back(P);
			Ring.Elevation.push_back(300.0);
		}
		MapGrids RG;
		RG.Wide = &Ring;
		const State Here = MapWeatherAt(RG, 54.3, -2.2);
		const std::vector<Cell> Cells = StormCells(RG, Here, 54.3, -2.2, L);
		const double T0 = 1786000000.0;
		const std::vector<Strike> Hour = StrikesBetween(Cells, T0, T0 + 3600.0, L);
		std::vector<Strike> Halves = StrikesBetween(Cells, T0, T0 + 1234.5, L);
		const std::vector<Strike> Rest = StrikesBetween(Cells, T0 + 1234.5, T0 + 3600.0, L);
		Halves.insert(Halves.end(), Rest.begin(), Rest.end());
		bool bPlace = true, bSame = Halves.size() == Hour.size();
		int Ground = 0;
		double Nearest = 1e9;
		for (size_t I = 0; I < Hour.size(); ++I)
		{
			double E = 0.0, N = 0.0;
			TorqueWeather::MetresBetween(54.3, -2.2, Hour[I].Lat, Hour[I].Lon, E, N);
			bPlace = bPlace && E > 12000.0 && Thunderiness(MapWeatherAt(RG, Hour[I].Lat, Hour[I].Lon), L) > 0.3;
			Nearest = std::min(Nearest, std::sqrt(E * E + N * N));
			Ground += Hour[I].bGround ? 1 : 0;
			if (bSame) bSame = Halves[I].At == Hour[I].At && Halves[I].Lat == Hour[I].Lat;
		}
		std::printf("  storm day: %d cells thundery, %d flashes in an hour (%d to the ground), the nearest %.1f km away\n",
			static_cast<int>(Cells.size()), static_cast<int>(Hour.size()), Ground, Nearest / 1000.0);
		Check(Hour.size() > 100 && Hour.size() < 4000 && bPlace, "  flashes come only where the storms are (the east), a few a minute each");
		Check(Ground > 0 && Ground < static_cast<int>(Hour.size()) / 2, "  some fork to the ground, most stay in the cloud");
		Check(bSame, "  the same flashes, the same moments, however the hour is cut up (co-op machines agree)");
		Check(StormCells(MapGrids(), Cu ? Cu->Weather : Here, 54.3, -2.2, L).empty(), "  no storm, no flashes");
		Check(Near(ThunderDelay(1000.0, L), 2.92, 0.05) && Near(ThunderDelay(10000.0, L), 29.2, 0.3), "thunder follows 3 s a km");
		Check(ThunderLoudness(500.0, 1.0, L) > ThunderLoudness(8000.0, 1.0, L) && ThunderLoudness(30000.0, 1.0, L) == 0.0, "  louder near, unheard past 22 km");
		Strike One;
		One.Seed = 1234u;
		One.Strokes = 3;
		double Second = 0.0;
		for (double T = 0.03; T < 0.4; T += 0.002) Second = std::max(Second, FlashAt(One, T) > 0.4 && FlashAt(One, T - 0.03) < FlashAt(One, T) ? 1.0 : Second);
		Check(FlashAt(One, 0.0) > 0.9 && FlashAt(One, 0.8) == 0.0 && Second > 0.0, "a flash flickers (return strokes) and is gone in under a second");
		const std::vector<int16_t> NearT = ThunderSound(800.0, 0.9, 7u, 22050), FarT = ThunderSound(12000.0, 0.9, 7u, 22050);
		auto Crossings = [](const std::vector<int16_t>& V, size_t Upto) {
			size_t Z = 0;
			for (size_t I = 1; I < std::min(Upto, V.size()); ++I) Z += (V[I - 1] < 0) != (V[I] < 0) ? 1 : 0;
			return static_cast<double>(Z) / static_cast<double>(std::min(Upto, V.size()));
		};
		int PeakNear = 0;
		for (int16_t V : NearT) PeakNear = std::max(PeakNear, std::abs(static_cast<int>(V)));
		std::printf("  thunder: near %.1f s, far %.1f s; peak %d; zero crossings near %.3f, far %.3f\n", static_cast<double>(NearT.size()) / 22050.0, static_cast<double>(FarT.size()) / 22050.0,
			PeakNear, Crossings(NearT, 22050), Crossings(FarT, 22050));
		Check(PeakNear > 20000 && PeakNear <= 32767 && FarT.size() > NearT.size(), "thunder is made: loud, and far thunder rolls longer");
		Check(Crossings(NearT, 22050) > Crossings(FarT, 22050) * 1.5, "  a near strike cracks (more high notes), far thunder is a low rumble");
		Check(ThunderSound(800.0, 0.9, 7u, 22050) == NearT, "  the same strike, the same thunder");

		// Hail and snow: white curtains, a flat bright light.
		const NamedSky* SnowS = FindSky(Skies, "snow_showers");
		const NamedSky* Far = FindSky(Skies, "distant_showers");
		if (SnowS && Far)
		{
			const TorqueWeather::Grid SnowRing = ShowersAround(SnowS->Weather, 54.3, -2.2, 6, 2.0, 3u);
			const TorqueWeather::Grid RainRing = ShowersAround(Far->Weather, 54.3, -2.2, 6, 5.0, 3u);
			MapGrids SG, RG2;
			SG.Wide = &SnowRing;
			RG2.Wide = &RainRing;
			const std::vector<RainShaft> SS = FindRainShafts(SG, 54.3, -2.2, L), RS = FindRainShafts(RG2, 54.3, -2.2, L);
			Check(!SS.empty() && SS[0].White > 0.9 && !RS.empty() && RS[0].White < 0.1, "snow showers hang white curtains, rain showers grey");
		}
		if (const NamedSky* Heavy = FindSky(Skies, "heavy_snow"))
		{
			State Grey = Heavy->Weather;
			Grey.Snow = 0.0;
			Grey.Code = 3;
			Check(At(Heavy->Weather, 20.0, true).SkyLightScale > At(Grey, 20.0, true).SkyLightScale * 1.2, "  under snow cloud the light goes flat and bright");
		}

		// Rare skies.
		if (const NamedSky* Veil = FindSky(Skies, "cirrostratus"))
		{
			const double Low = At(Veil->Weather, 15.0, true).SunDogs, High = At(Veil->Weather, 55.0, true).SunDogs;
			std::printf("  sun dogs in a veil: sun 15 deg %.2f, 55 deg %.2f\n", Low, High);
			Check(Low > 0.3 && High == 0.0, "sun dogs either side of a low sun in a veil");
		}
		State Jet = TorqueWeather::Preset(TorqueWeather::Sunny);
		Jet.T300 = -52.0;
		Jet.Rh300 = 85.0;
		const double Trails = At(Jet, 30.0, true).Contrails;
		Jet.Rh300 = 30.0;
		const double DryTrails = At(Jet, 30.0, true).Contrails;
		Jet.Rh300 = -1.0;
		Check(Trails > 0.4 && DryTrails == 0.0 && At(Jet, 30.0, true).Contrails == 0.0, "contrails linger in cold, damp air 9 km up (none in dry air, none unknown)");
		if (const NamedSky* Fog = FindSky(Skies, "valley_fog"))
		{
			Inputs In;
			In.Weather = Fog->Weather;
			In.SunElevation = 8.0;
			In.bClouds = true;
			In.GroundAsl = 300.0;
			In.ValleyAsl = 280.0;
			In.ViewAsl = 285.0;
			const Sky InFog = Compute(In, L);
			In.ViewAsl = 600.0;
			const Sky OnTop = Compute(In, L);
			std::printf("  valley fog: in it fogbow %.2f glory %.2f; above it fogbow %.2f glory %.2f (fog top %.0f m)\n", InFog.Fogbow, InFog.Glory, OnTop.Fogbow,
				OnTop.Glory, OnTop.FogTopAsl);
			Check(InFog.Fogbow > 0.2 && InFog.Glory == 0.0 && OnTop.Glory > 0.2 && OnTop.Fogbow < 0.05, "a fogbow in the fog, a glory looking down on it");
		}
		const double York = GeomagneticLat(53.96, -1.08, L.AuroraPoleLat, L.AuroraPoleLon);
		Inputs Night;
		Night.Weather = TorqueWeather::Preset(TorqueWeather::Sunny);
		Night.SunElevation = -25.0;
		Night.Lat = 53.96;
		Night.Lon = -1.08;
		Night.Kp = 2.0;
		const Sky Quiet = Compute(Night, L);
		Night.Kp = 7.0;
		const Sky Storming = Compute(Night, L);
		Night.SunElevation = 10.0;
		const Sky ByDay = Compute(Night, L);
		std::printf("  aurora over York (geomagnetic %.1f deg): Kp 2 %.2f, Kp 7 %.2f (top %.0f deg, bearing %.0f)\n", York, Quiet.Aurora, Storming.Aurora,
			Storming.AuroraTopDeg, Storming.AuroraBearing);
		Check(Near(York, 56.0, 1.5) && Quiet.Aurora == 0.0 && Storming.Aurora > 0.5 && ByDay.Aurora == 0.0 && (Storming.AuroraBearing > 290.0 || Storming.AuroraBearing < 20.0),
			"the aurora: none at Kp 2, low in the north at Kp 7 over York, never by day");
		int NlcNights = 0, NlcWinter = 0;
		Inputs Summer;
		Summer.Weather = TorqueWeather::Preset(TorqueWeather::Sunny);
		Summer.SunElevation = -10.0;
		Summer.Lat = 54.3;
		for (int D = 160; D < 200; ++D) { Summer.DayOfYear = D; NlcNights += Compute(Summer, L).Noctilucent > 0.5 ? 1 : 0; }
		for (int D = 330; D < 365; ++D) { Summer.DayOfYear = D; NlcWinter += Compute(Summer, L).Noctilucent > 0.0 ? 1 : 0; }
		std::printf("  noctilucent cloud: %d of 40 midsummer nights, %d of 35 winter ones\n", NlcNights, NlcWinter);
		Check(NlcNights >= 5 && NlcNights <= 30 && NlcWinter == 0, "  noctilucent cloud on some midsummer nights, never in winter");
		std::printf("  meteors an hour: 12 Aug %.0f, 14 Dec %.0f, 1 Mar %.0f (%s, %s)\n", MeteorRate(224, L), MeteorRate(347, L), MeteorRate(60, L), ShowerOn(224), ShowerOn(347));
		Check(MeteorRate(224, L) > 40.0 && MeteorRate(60, L) < 5.0 && std::string(ShowerOn(224)) == "Perseids" && std::string(ShowerOn(347)) == "Geminids",
			"meteor showers on their nights (the Perseids, the Geminids)");
		int Seen = 0;
		bool bWas = false;
		for (double T = 0.0; T < 3600.0; T += 0.05)
		{
			const Meteor M = MeteorAt(T0 + T, 60.0);
			Seen += M.bOn && !bWas ? 1 : 0;
			bWas = M.bOn;
		}
		std::printf("  at 60 an hour: %d seen in an hour\n", Seen);
		Check(Seen >= 35 && Seen <= 90, "  about the rate asked for, each from the clock (the same for everyone)");
		Inputs Dusty;
		Dusty.Weather = TorqueWeather::Preset(TorqueWeather::Sunny);
		Dusty.SunElevation = 4.0;
		const Sky Clean = Compute(Dusty, L);
		Dusty.Dust = 200.0;
		const Sky Sahara = Compute(Dusty, L);
		Check(Sahara.MieScale > Clean.MieScale * 2.0 && Sahara.SunWarm > 0.5 && Clean.SunWarm == 0.0, "Saharan dust: a thicker, warmer haze and a red sun");
		State Saved = Jet;
		Saved.Rh300 = 77.0;
		Saved.T300 = -49.5;
		State Back;
		Check(TorqueWeather::Load(TorqueWeather::Save(Saved), Back) && Near(Back.Rh300, 77.0, 0.5) && Near(Back.T300, -49.5, 0.05), "the air 9 km up is saved with the weather");
	}

	// The shade by day (build 147, beenster: "11am on a sunny day and I need to put my headlights on in the
	// shadow of a hill"; a November 11am sun at 17 degrees, Kettleshulme).
	{
		std::printf("\nthe shade by day (build 147)\n");
		Looks Lk;
		std::string Why;
		LoadLooks(ReadFile(std::string(TORQUE_SKY_DATA) + "/sky_looks.json"), Lk, &Why);
		const State Sunny = TorqueWeather::Preset(TorqueWeather::Sunny);
		const Sky Nov = At(Sunny, 17.0, true, false, Lk), June = At(Sunny, 58.0, true, false, Lk), Night = At(Sunny, -20.0, true, false, Lk);
		const Sky Grey = At(TorqueWeather::Preset(TorqueWeather::Overcast), 17.0, true, false, Lk);
		std::printf("  sun 17: sky light x%.2f, may open %.2f stops; sun 58: x%.2f, %.2f; night: x%.2f, %.2f; overcast at 17: x%.2f\n",
			Nov.SkyFill, Nov.ExposureOpenStops, June.SkyFill, June.ExposureOpenStops, Night.SkyFill, Night.ExposureOpenStops, Grey.SkyFill);
		Check(Near(Nov.SkyFill, 1.0, 1e-6) && Near(Nov.ExposureOpenStops, 0.0, 1e-6), "as the file has it: 147's light (148's lift looked washed out at every quality)");
		Looks Lift = Lk;
		Lift.SkyFill = Curve{{{0.0, 1.3}, {60.0, 1.0}}};
		Lift.ExposureShadeOpen = 0.5;
		Lift.ExposureFollowSun = 0.5;
		const Sky NovLift = At(Sunny, 17.0, true, false, Lift), JuneLift = At(Sunny, 58.0, true, false, Lift);
		Check(NovLift.SkyFill > 1.1 && NovLift.ExposureOpenStops > 0.8 && JuneLift.SkyFill < NovLift.SkyFill && JuneLift.ExposureOpenStops < NovLift.ExposureOpenStops,
			"the lift when asked for: more for a low sun than a high one");
		Check(Near(Night.SkyFill, 1.0, 1e-6) && Near(Night.ExposureOpenStops, 0.0, 1e-6), "at night neither (the night's exposure is its own)");
		Check(Grey.SkyFill < 1.05, "under overcast the dull sky light has it already");
		// (build 148: a drizzly overcast came out washed out and white)
		const Sky Drizzle = At(TorqueWeather::Preset(TorqueWeather::Wet), 17.0, true, false, Lk);
		std::printf("  overcast at 17: may open %.2f, bounced light x%.2f; drizzle: %.2f, x%.2f; clear: x%.2f\n",
			Grey.ExposureOpenStops, Grey.CloudMs, Drizzle.ExposureOpenStops, Drizzle.CloudMs, Nov.CloudMs);
		Check(Grey.ExposureOpenStops < 0.2 && Drizzle.ExposureOpenStops < 0.2, "under a dull sky the camera opens no further than it did before 148");
		Check(Grey.CloudMs < 0.5 && Nov.CloudMs > 0.95, "a dull deck keeps a grey base (less bounced light), fair cumulus keeps it all");
		Check(Lk.DensityMost >= 1.0 && Lk.CloudViewSamples >= 1.0, "the cloud's density capped and its samples raised (148's flickering white dots)");
		Check(Lk.CloudMsOctaves == 2.0 && Lk.CloudPhaseG > 0.3 && Lk.CloudMsContribution > 0.3, "the cloud scatters the sun many times, forward (white cumulus, silver linings)");
		Check(V2DetailSize(600.0, 1200.0, Lk) == 600.0 && V2DetailSize(600.0, 9600.0, Lk) > 1000.0,
			"a 9 km shower cloud gets coarser edge detail than fair cumulus (no shimmer)");
	}
	std::printf(Fails ? "\n%d FAILED\n" : "\nall passed\n", Fails);
	return Fails ? 1 : 0;
}
