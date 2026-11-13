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
	// The low cloud's depth, metres: a deck (stratus) or fair weather cumulus, and taller with CAPE
	// (metres more per 100 J/kg, up to CapeMostM).
	double LowDeckM = 700.0, LowHeapM = 1000.0, CapeDepthPer100 = 250.0, CapeMostM = 4500.0;
	// Middle cloud's base and depth, metres above sea level / deep.
	double MidBaseM = 3200.0, MidDepthM = 1500.0;
	// The cloud base is never lower than this above the ground under the viewer, nor higher than
	// LowBaseMostM above sea level (the dew point's base runs off to 5 km on a dry day).
	double LowBaseLeastM = 150.0, LowBaseMostM = 2600.0;
	// How dense the cloud is (MPC_Sky Density: the material's extinction scale), a thin fair day's
	// and a deck's, and darker still in rain.
	double DensityFair = 0.6, DensityDeck = 1.0, DensityRain = 1.6;
	// How grey the cloud's own colour is (MPC_Sky Albedo): white, and under rain.
	double AlbedoFair = 1.0, AlbedoRain = 0.7;
	// The deck closes: from this SunBlock to that, the drawn clouds' own shadows give way to a dimmed sun
	// (so the sun is never dimmed twice).
	double DeckFrom = 0.7, DeckTo = 0.95;
	// The old noise cloud shadows (M_CloudShadow, MPC_Weather CloudShadow) where clouds aren't drawn
	// (the Low setting): 1 none .. 0.45 most of the ground, by the cover.
	Curve LegacyShadow{{{0.0, 1.0}, {0.1, 0.6}, {0.6, 0.45}, {1.0, 0.45}}};
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
