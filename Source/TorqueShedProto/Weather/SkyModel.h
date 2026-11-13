// The sky as one set of numbers (skies plan, stages 1 and 2), plain C++ so the same code runs in the
// game (ATorqueSky) and in SimTest's sky_test.
//
// The weather state (Weather/WeatherModel.h), where the sun stands and the viewer's height go in; one
// sky state comes out, and every piece of the sky (sun, sky light, air, fog, clouds, camera) reads only
// that. Every tuning figure is a Looks, read from Data/Sky/sky_looks.json at start, so a look can be
// retuned with no recompile. Its defaults are the look the game had before the sky had its own model,
// so a missing or broken file changes nothing.
#pragma once

#include "WeatherModel.h"

#include <string>
#include <utility>
#include <vector>

namespace TorqueFit { struct Json; }

namespace TorqueSky
{
// A curve through points (x, y), straight between them and flat beyond the ends.
struct Curve
{
	std::vector<std::pair<double, double>> P;
	double At(double X) const;
	// The same, between the points in log10 of y (for light, which spans a billion to one).
	double AtLog(double X) const;
};

// The kinds of cloud (stage 3), by layer: low, low and middle (nimbostratus), middle, high.
enum Kind
{
	Cumulus, Stratocumulus, Stratus, Nimbostratus, Towering, Altocumulus, Altostratus, Cirrus, Cirrostratus, KindCount
};
const char* KindName(int K);

// How a kind of cloud looks (a row of sky_looks.json's "kinds"): the drawn layer blends the rows of its
// kinds by their weights, so the sky never snaps from one kind to the next.
struct KindLook
{
	double Shape = 0.5;      // the height profile: 0 heaped (a flat base, a rounded top) .. 1 a flat sheet
	double Lumps = 0.5;      // 0 big shapes (the 7 km noise) .. 1 small ones (the 1.8 km noise)
	double Ragged = 0.4;     // how much the fine noise eats the edges, 0 .. 1
	double Density = 0.8;    // MPC_Sky density (the material's extinction scale)
	double BaseDark = 0.2;   // how much darker the cloud's base is than its top, 0 .. 1
	double DepthM = 700.0;   // how deep, metres
	double CapeDepth = 0.0;  // and this share of the depth CAPE gives a heaped cloud (cumulus 1)
	double Sheet = 0.0;      // high cloud only: 0 wisps (cirrus) .. 1 a veil (cirrostratus)
};

struct Looks
{
	// --- Light (stage 1) ---
	// The light table: the ground's light under a clear sky, real lux, by the sun's height (degrees).
	Curve ClearLux{{{-18.0, 0.0007}, {-12.0, 0.008}, {-9.0, 0.2}, {-6.0, 3.4}, {-3.0, 60.0}, {-0.83, 400.0}, {0.0, 650.0},
		{5.0, 6000.0}, {10.0, 15000.0}, {20.0, 33000.0}, {30.0, 52000.0}, {45.0, 76000.0}, {60.0, 96000.0}, {90.0, 105000.0}}};
	// What the cloud lets through when it hides the whole sun (SunBlock 1): an ordinary grey day
	// (100,000 lux down to about 15,000), and a thick rain cloud (about 3,500).
	double OvercastLux = 0.15, RainCloudLux = 0.035;
	// The day's light for the lamps, the exposure and the haze's glow (DayLight): 0 at this many lux and
	// below (well into civil twilight), 1 from this many up.
	double DayFromLux = 1.0, DayFullLux = 2000.0;
	// Where the light points when the sun is down: never lower than this (degrees). The atmosphere goes on
	// being lit from below the horizon, so the glow sits where the sun went down; the planet's shadow
	// keeps the ground out of it. (2 puts back the old way: the light held just above the horizon.)
	double SunLowestDeg = -10.0;
	// The sun's light fades out between these heights (degrees, below the horizon), as the glow goes.
	double SunFadeFromDeg = -2.0, SunFadeToDeg = -10.0;
	// The sun behind cloud (with no clouds drawn, or a closed deck): its light at full SunBlock, dry and
	// wet (the old 0.14 and 0.10), and how wide the light grows (degrees; its disc hides as it does).
	double SunDimDull = 0.14, SunDimWet = 0.10, SunWideDeg = 40.0, DiscHideOverDeg = 1.5;
	// The sky light under a dull sky, times the level's.
	double SkyLightDull = 2.5;
	// The camera under a dull sky: stops brighter, dry and wet.
	double ExposureDullStops = 1.6, ExposureWetStops = 1.9;
	// The feed's direct sunshine set against what a clear sky would give: how much it may move the
	// cover's guess (0 never, 1 wholly), from this sun height (degrees) up.
	double DirectTrust = 0.5, DirectTrustFromDeg = 8.0;

	// --- Air (stage 1) ---
	// The haze by how far you can see (km): times the level's, over 8 km (below that the fog takes
	// over: WeatherModel's FogBoost). Crisp polar air at 50 km and more, summer haze at 12.
	Curve HazeByKm{{{8.0, 1.0}, {10.0, 1.0}, {40.0, 1.0}, {60.0, 0.7}, {100.0, 0.5}}};
	// Damp air swells the haze: times this much more at 100% humidity, from HumidFrom %.
	double HumidFrom = 90.0, HumidSwell = 1.5;
	// A dull sky whitens the air (the faked overcast): Mie times, Rayleigh (the blue) times; and with
	// real cloud drawn overhead, less of it is needed (stage 2).
	double DullMie = 12.0, DullRayleigh = 0.08, DullMieClouds = 4.0, DullRayleighClouds = 0.35;
	// The height fog under a dull sky, dry and wet: times the level's.
	double FogDull = 2.0, FogWet = 3.0;
	// The haze's own glow by night (a trace for the moon and the towns), as a share of the day's.
	double FogNightGlow = 0.006;

	// --- Cloud (stage 2) ---
	// The cover as drawn, by the real cover: perspective bunches clouds towards the horizon, so half
	// the sky must look half covered from the saddle (set from the bench's five straight-up shots).
	Curve CoverShown{{{0.0, 0.0}, {0.1, 0.12}, {0.25, 0.3}, {0.5, 0.52}, {0.75, 0.74}, {1.0, 1.0}}};
	// A heaped cloud's depth, metres: fair weather cumulus, and taller with CAPE (metres more per
	// 100 J/kg, up to CapeMostM). (Every other kind's depth is its row in Kinds.)
	double LowHeapM = 1000.0, CapeDepthPer100 = 250.0, CapeMostM = 4500.0;
	// Middle cloud's base, metres above sea level.
	double MidBaseM = 3200.0;
	// The cloud base is never lower than this above the ground under the viewer, nor higher than
	// LowBaseMostM above sea level (the dew point's base runs off to 5 km on a dry day).
	double LowBaseLeastM = 150.0, LowBaseMostM = 2600.0;
	// How dense the cloud is (MPC_Sky Density: the material's extinction scale) in rain, at least
	// (each kind's own density is its row in Kinds).
	double DensityRain = 1.6;
	// How grey the cloud's own colour is (MPC_Sky Albedo): white, and under rain.
	double AlbedoFair = 1.0, AlbedoRain = 0.7;
	// The deck closes: from this SunBlock to that, the drawn clouds' own shadows give way to a dimmed sun
	// (so the sun is never dimmed twice).
	double DeckFrom = 0.7, DeckTo = 0.95;
	// The old noise cloud shadows (M_CloudShadow, MPC_Weather CloudShadow) where clouds aren't drawn
	// (the Low setting): 1 none .. 0.45 most of the ground, by the cover.
	Curve LegacyShadow{{{0.0, 1.0}, {0.1, 0.6}, {0.6, 0.45}, {1.0, 0.45}}};

	// --- Kinds of cloud (stage 3) ---
	KindLook Kinds[KindCount] = {
		/* cumulus */       {0.0, 0.6, 0.55, 0.7, 0.15, 0.0, 1.0, 0.0},
		/* stratocumulus */ {0.75, 0.55, 0.35, 0.9, 0.3, 700.0, 0.0, 0.0},
		/* stratus */       {1.0, 0.1, 0.1, 1.0, 0.15, 400.0, 0.0, 0.0},
		/* nimbostratus */  {0.9, 0.2, 0.6, 1.6, 0.6, 3000.0, 0.0, 0.0},
		/* towering */      {0.0, 0.3, 0.45, 1.4, 0.55, 2500.0, 1.0, 0.0},
		/* altocumulus */   {0.55, 1.0, 0.65, 0.6, 0.1, 600.0, 0.0, 0.0},
		/* altostratus */   {1.0, 0.05, 0.15, 0.5, 0.15, 1500.0, 0.0, 0.0},
		/* cirrus */        {0.0, 0.0, 0.0, 0.35, 0.0, 0.0, 0.0, 0.0},
		/* cirrostratus */  {0.0, 0.0, 0.0, 0.25, 0.0, 0.0, 0.0, 1.0},
	};
	// The rules (the plan's table), each a band to blend across, never a switch:
	// heating: the sun this high (degrees) starts cumulus off, full from ConvectFullDeg;
	double ConvectFromDeg = 5.0, ConvectFullDeg = 25.0;
	// unstable air: CAPE (J/kg) at which cumulus are sure (with none, half a chance by day);
	double ConvectCape = 300.0;
	// rain counts as showers (towering cumulus) when the code says so or the air is this unstable;
	double ShowerCapeFrom = 200.0, ShowerCapeTo = 600.0;
	// rain this heavy (mm/h) is a shower's or a front's for sure;
	double ShowerRainMm = 0.15, SteadyRainMm = 1.0;
	// towering cumulus without rain yet: from this CAPE, sure at that;
	double ToweringCapeFrom = 800.0, ToweringCapeTo = 2000.0;
	// stratus: the low cover closing (from, to), its base under these metres above the ground;
	double StratusCoverFrom = 0.7, StratusCoverTo = 0.95, StratusBaseFromM = 300.0, StratusBaseToM = 800.0;
	// broken low cover (cumulus) up to this, a sheet (stratocumulus) from that;
	double BrokenTo = 0.65, BrokenSheet = 0.95;
	// a middle sheet (altostratus) from this cover, sure at that;
	double MidSheetFrom = 0.55, MidSheetTo = 0.9;
	// a high veil (cirrostratus) from this cover, sure at that.
	double VeilFrom = 0.45, VeilTo = 0.85;
	// --- The high cloud (stage 3): a sheet on the sky, not a volume ---
	double HighBaseM = 9000.0;
	// How solid the sheet is at full cover, wisps and veil.
	double HighOpacity = 0.75;
	// The sun through cloud: a veil makes it milky (the light widens this many degrees, its disc softens)
	// with a 22 degree halo now and then; altostratus a ground glass sun (wider still, faint shadows).
	double VeilWidenDeg = 1.0, GlassWidenDeg = 6.0, GlassShadows = 0.25, HaloMost = 0.6;

	// --- Weather you can see coming (stage 4) ---
	// Fog with a top: fog that formed overnight in calm, clear air lies this deep over the low ground
	// (metres, thin .. thick as the visibility falls); fog that came in on the wind (advection, hill fog)
	// is deep: no top here.
	double FogShallowM = 40.0, FogThickM = 120.0;
	// How fast the fog's haze goes as the viewer climbs out of its top (metres to clear air).
	double FogTopFadeM = 40.0;
	// Valley mist after a clear, still night: air within this many % of saturation (88 %: about two
	// degrees from the dew point), wind under these m/s, the sky this clear; it thins as the sun climbs
	// to MistGoneDeg (about one to three hours after sunrise) and lies MistDepthM deep in the hollows.
	double MistHumidFrom = 86.0, MistHumidFull = 95.0, MistCalmMs = 1.5, MistWindyMs = 4.0, MistClearTo = 0.35;
	double MistGoneDeg = 14.0, MistDepthM = 25.0;
	// How thick the mist is drawn (the Local Fog Volumes' radial extinction at full mist; fog lying with
	// a top is three times this).
	double MistExtinction = 0.6;
	// A rainbow: rain about (falling, or in the cells the wind brings), the sun out and low behind you.
	double RainbowSunMostDeg = 42.0, RainbowRainMm = 0.2;
};

// Looks from sky_looks.json's text: whatever it names over the defaults. False (with why) when the text
// isn't JSON; a figure it doesn't name keeps its default.
bool LoadLooks(const std::string& Text, Looks& Out, std::string* Error = nullptr);

// One cloud layer.
struct Layer
{
	double Cover = 0.0;              // 0 .. 1, the real cover
	double BaseAsl = 0.0, TopAsl = 0.0;   // metres above sea level
	double Wind = 0.0, WindFrom = 0.0;    // the wind that carries it, m/s and degrees from
};

struct Sky
{
	// The sun.
	double SunElevation = 0.0;   // degrees, as given
	double LightElevation = 0.0; // where the light points (never below Looks.SunLowestDeg)
	double SunShare = 1.0;       // the share of the sun's direct light that reaches the ground, 0 .. 1
	double SunScale = 1.0;       // the light's brightness, times the level's sun
	double SunAngle = 0.0;       // the light's width, degrees (the level's at 0; the input's)
	double SunDisc = 1.0;        // the disc drawn in the sky, 0 hidden .. 1
	// The light.
	double ClearLux = 0.0;       // the ground's light under a clear sky now, real lux
	double GroundLux = 0.0;      // and under this sky
	double DayLight = 1.0;       // 0 at night .. 1 by day: the lamps, the exposure and the haze's glow
	double Block = 0.0;          // SunBlock (after the feed's sunshine has had its say)
	double Dull = 0.0, WetLook = 0.0;
	// The air.
	double SkyLightScale = 1.0;  // times the level's sky light
	double MieScale = 1.0, RayleighScale = 1.0;   // times the level's atmosphere
	double FogScale = 1.0;       // times the level's height fog (the viewer's haze included)
	double FogGlow = 1.0, FogSunGlow = 1.0;       // the fog's own glow and its glow towards the sun, times the level's
	double ExposureStops = 0.0;  // added to the level's exposure bias
	// The cloud you see (stage 2): one layer drawn, the low and middle cloud as one.
	Layer Low, Mid, High;
	double Cover = 0.0;          // the real cover of the drawn layer
	double CoverShown = 0.0;     // MPC_Sky Cover
	double BaseAsl = 0.0, TopAsl = 0.0;   // the drawn layer, metres above sea level
	double Density = 0.0, Albedo = 1.0;   // MPC_Sky Density, Albedo
	double Shape = 0.0;          // MPC_Sky Shape: 0 heaped (cumulus) .. 1 a flat deck
	double DriftE = 0.0, DriftN = 0.0;    // the drawn layer's wind, metres a second towards east and north
	double CloudShadows = 1.0;   // the drawn clouds' shadows on the ground, 0 .. 1 (the light's strength)
	// The kinds (stage 3): each kind's weight in its layer (a layer's add up to 1 when it has cloud), and
	// the two volume layers drawn in the one cloud. The fields above (Cover .. DriftN) are the low layer's;
	// BaseAsl .. TopAsl is the whole volume, both layers in it.
	double Weight[KindCount] = {};
	struct Drawn
	{
		double Cover = 0.0, CoverShown = 0.0;   // the real cover, and MPC_Sky's
		KindLook Look;                          // its kinds' looks blended
		double Bottom = 0.0, Top = 1.0;         // its band in the volume, 0 .. 1 of the volume's height
		double DriftE = 0.0, DriftN = 0.0;      // its wind, m/s towards east and north
		double Albedo = 1.0;
	};
	Drawn LowDrawn, MidDrawn;
	// The high sheet: its cover drawn, wisps .. veil, the wind that carries it, and the sun's halo.
	double HighShown = 0.0, HighSheet = 0.0, HighDriftE = 0.0, HighDriftN = 0.0, Halo = 0.0;
	int Dominant(int Layer) const;   // the kind with the most weight in a layer (0 low, 1 middle, 2 high), -1 none
	// Weather you can see coming (stage 4).
	double FogTopAsl = -1.0;     // the top of fog lying in the valleys, metres above sea level (-1: no top, deep fog or none)
	double Mist = 0.0;           // valley mist in the hollows, 0 none .. 1 thick
	double MistTopAsl = 0.0;     // and its top over the low ground
	double Rainbow = 0.0;        // how strong a rainbow would be opposite the sun, 0 .. 1
	double LegacyShadow = 1.0;   // MPC_Weather CloudShadow
};

struct Inputs
{
	TorqueWeather::State Weather;
	double SunElevation = 30.0;  // degrees above the horizon
	bool bRealSun = false;       // the sun is the real one now (Real time of day), so the feed's sunshine fits it
	double Haze = 1.0;           // WeatherModel's HazeAt for the viewer (the fog of low visibility, or being in cloud)
	double GroundAsl = 0.0;      // the ground under the viewer, metres above sea level
	bool bClouds = true;         // the volumetric clouds are drawn (Medium graphics and up)
	// Stage 4: where the viewer's eye is (metres above sea level; below 0: on the ground, GroundAsl + 2) and
	// the low ground round about (the lowest of the weather's points; below 0: GroundAsl).
	double ViewAsl = -1.0;
	double ValleyAsl = -1.0;
	// Rain within the wider ring (stage 4's second grid), mm/h, at its wettest: a shower you can see coming.
	double RainNear = 0.0;
};

// The sky.
Sky Compute(const Inputs& In, const Looks& L);

// The pieces, for the tests.
// The ground's light under a clear sky, real lux.
double ClearSkyLux(double SunElevation, const Looks& L);
// What a clear sky's sun gives a level surface, W per square metre (for setting the feed's direct
// sunshine against): the sun through the air mass at that height.
double ClearDirect(double SunElevation);
// The sun's height (degrees) and bearing (degrees from north, clockwise) at a place, a day of the year
// and a solar time (hours, 12 noon).
void SunAt(double LatDeg, int DayOfYear, double SolarHours, double& Elevation, double& Bearing);
// EV100 for a light, lux (an incident meter's).
double Ev100(double Lux);

// A weather State from a JSON object naming any of its figures (the bench's and the tests' skies).
void StateFromJson(const TorqueFit::Json& J, TorqueWeather::State& InOut);
} // namespace TorqueSky
