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
	// The light in the shade (build 147, beenster: "11am on a sunny day and I need to put my headlights on
	// in the shadow of a hill"). The sky light by day, times the level's (on top of SkyLightDull), by the
	// sun's height in degrees: the lower the sun, the more of the day's light is the sky's, and the engine's
	// capture of a low sun's sky comes out dim. Less of it as the sky dulls (the dull sky light has its own).
	// (OFF by default, 147's light: build 148 with 1.9 / 1.5 stops + the sun's full difference / 0.6 local
	// contrast looked washed out at every quality. The 147 shade that was too dark was at Medium, where
	// Lumen is off. The figures stay in sky_looks.json to raise gently if a shade at Epic is still dark.)
	Curve SkyFill{{{-6.0, 1.0}, {90.0, 1.0}}};
	// By day the camera may open this many stops below the level's lowest exposure (a hill's shadow, under
	// trees; only under a bright sky: build 148's drizzle came out washed out with it), and further as the sun is lower than the level's own (ExposureBuiltDeg): the clear sky's light
	// at that height against now, times ExposureFollowSun, at most ExposureFollowMost stops.
	double ExposureShadeOpen = 0.0, ExposureBuiltDeg = 32.0, ExposureFollowSun = 0.0, ExposureFollowMost = 1.5;
	// The engine's local exposure: how much of the scene's contrast stays in the shadows and in the
	// highlights (lower lifts the shade without lifting the sky). The level's were 0.7 and 0.75.
	double LocalShadowContrast = 0.7, LocalHighlightContrast = 0.75;

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
	double LowHeapM = 600.0, CapeDepthPer100 = 250.0, CapeMostM = 4500.0;
	// Middle cloud's base, metres above sea level.
	double MidBaseM = 3200.0;
	// The cloud base is never lower than this above the ground under the viewer, nor higher than
	// LowBaseMostM above sea level (the dew point's base runs off to 5 km on a dry day).
	double LowBaseLeastM = 150.0, LowBaseMostM = 2600.0;
	// How dense the cloud is (MPC_Sky Density: the material's extinction scale) in rain, at least
	// (each kind's own density is its row in Kinds).
	double DensityRain = 1.6;
	// What the material's extinction is multiplied by, on top of every kind's density (build 143's bench:
	// overcast and full cover drew as a pale blue haze, the cloud far too thin to see through as grey).
	// Tune on the bench with no rebuild: higher is thicker, greyer cloud.
	double ExtinctionScale = 8.0;
	// How grey the cloud's own colour is (MPC_Sky Albedo): white, and under rain.
	double AlbedoFair = 1.0, AlbedoRain = 0.7;
	// How the cloud scatters the sun (the cloud material's advanced output, MPC_Sky Cloud*; build 147's
	// cumulus came out grey and blotchy: single scattering only). The forward lobe (the silver lining
	// looking towards the sun), the back lobe and how the two are blended; the light that has bounced
	// about inside the cloud (without it a cumulus is grey all through): its share, how much the cloud
	// shades it, how forward it still goes. CloudMsOctaves (0 .. 2) is built into the material
	// (Tools/ue_build_level.py reads it from this file).
	double CloudPhaseG = 0.6, CloudPhaseG2 = -0.3, CloudPhaseBlend = 0.3;
	double CloudMsContribution = 0.6, CloudMsOcclusion = 0.35, CloudMsEccentricity = 0.6, CloudMsOctaves = 2.0;
	// Under a dull sky the bounced light is turned down to this share (build 148: a drizzly overcast came
	// out washed out and white; a deck's base is grey because little light gets through it).
	double CloudMsDull = 0.2;
	// The cloud's ray marching (build 148: the drizzle's grey sky sparkled with white dots that flickered
	// every frame, the deck too dense for the samples): the engine's samples along the view and the
	// shadow, times its own (more is smoother and costs the GPU), and the most the material's density may
	// be (times 0.1 per metre; the rain deck asked for 12.8). CloudGroundLight (0 or 1, built into the
	// material) lights the cloud's base from the ground.
	double CloudViewSamples = 2.0, CloudShadowSamples = 2.0, DensityMost = 8.0, CloudGroundLight = 0.0;
	// The rebuilt clouds (option A, beenster Nov 16: M_RideCloudV2, Tools/cloud_preview.py shows them).
	// The only clouds since Nov 17 (the old ones removed). The low layer's form size
	// (metres, times 1.3 .. 0.7 by its lumps) and edge-detail size, the middle layer's, the size of the
	// weather's own patches and how patchy; how much the detail eats the edges (and more by raggedness),
	// how crisp the skins, how much of the billows (the forms a third the size), and the extinction per
	// metre at a kind's density 1 (a real cumulus is about 0.05 .. 0.1; the old cloud reached 1.28 and
	// sparkled as the engine's samples hit or missed it).
	double V2ShapeM = 8000.0, V2DetailM = 600.0, V2MidShapeM = 5000.0, V2MidDetailM = 400.0, V2BroadM = 40000.0, V2Patchy = 0.35;
	double V2Erode = 0.5, V2ErodeRagged = 0.3, V2EdgeSharp = 3.0, V2Billow = 0.3, V2Extinction = 0.1, V2DeckThin = 0.5;
	// A deep layer (a shower cloud 9 km tall) is marched in long steps, so its edge detail grows by
	// sqrt(depth / V2DetailDeepM): fine grain a long step can't resolve shimmers (beenster Nov 16).
	double V2DetailDeepM = 3000.0;
	// Cover from V2ClosedFrom to V2ClosedFull closes the sky into one lumpy sheet (below it, gaps: 7/8
	// stratocumulus still shows its cells).
	double V2ClosedFrom = 0.85, V2ClosedFull = 0.92;
	// The edge detail (to half) and crisp skins fade out over this many km from the camera (far off they are finer
	// than a pixel and only sparkle: build 155 "a constant shimmering").
	double V2DetailFadeKm = 10.0;
	// The deck closes: from this SunBlock to that, the drawn clouds' own shadows give way to a dimmed sun
	// (so the sun is never dimmed twice).
	double DeckFrom = 0.7, DeckTo = 0.95;
	// With real cloud drawn, how much the closing deck still dims the sun itself: 0 never (the drawn deck
	// shades the ground and its own underside: build 145's bench showed a sun dimmed to a seventh left the
	// deck lit only by the blue sky light, a flat pale blue haze, not grey), 1 the stage 2 way.
	double DeckDimsSun = 0.0;
	// The old noise cloud shadows (M_CloudShadow, MPC_Weather CloudShadow) where clouds aren't drawn
	// (the Low setting): 1 none .. 0.45 most of the ground, by the cover.
	Curve LegacyShadow{{{0.0, 1.0}, {0.1, 0.6}, {0.6, 0.45}, {1.0, 0.45}}};

	// --- Kinds of cloud (stage 3) ---
	KindLook Kinds[KindCount] = {
		/* cumulus */       {0.0, 0.45, 0.3, 0.8, 0.15, 0.0, 1.0, 0.0},
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
	// with a 22 degree halo now and then; altostratus a ground glass sun (wider still).
	double VeilWidenDeg = 1.0, GlassWidenDeg = 6.0, HaloMost = 0.6;

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
	// Under a low base: the air thickens into the cloud over the last BaseFogBandM below it (FogScale
	// times up to 1 + BaseFogBoost, under a closed low deck).
	double BaseFogBandM = 300.0, BaseFogBoost = 3.0;
	// The weather map (the cloud drawn across the sky from the real weather round about, not one cover
	// everywhere): MapPx texels a side over MapSizeM metres round the camera, painted again every
	// MapRepaintS seconds or after MapMoveM; MapGain of the difference from the camera's cover is drawn.
	double MapPx = 64.0, MapSizeM = 90000.0, MapRepaintS = 20.0, MapMoveM = 4000.0, MapGain = 1.0, MapRainMm = 10.0;
	// Rain shafts under showers seen from afar: where the map's rain is over ShaftRainMm, on a lattice
	// ShaftSpacingM apart, between ShaftNearestM and ShaftFarthestM from the camera, at most ShaftMost,
	// each about ShaftRadiusM wide and ShaftOpacity at its heaviest (ShaftFullMm).
	double ShaftRainMm = 0.8, ShaftFullMm = 4.0, ShaftSpacingM = 6000.0, ShaftNearestM = 2500.0, ShaftFarthestM = 40000.0;
	double ShaftMost = 8.0, ShaftRadiusM = 1500.0, ShaftOpacity = 0.4;
	double ShaftLight = 0.012;   // how much of the sun's light a shaft gives back (its grey)
	// The rainbow drawn at full strength (its opacity).
	double RainbowOpacity = 0.55;

	// --- The night (stage 5) ---
	// Once the sun is MoonTakesOverDeg down (its light gone) and the moon is up, the light points at the
	// moon: MoonLightOfSun of the level's sun at a full moon high up (the real 0.25 lux would be black at
	// the night's exposure; a moonlit road wants to be seen), less with the phase and near the horizon.
	// The sky then lit by it is held to MoonSkyScale of its air (a moonlit sky is a deep, dark blue).
	double MoonTakesOverDeg = -12.0, MoonLightOfSun = 0.001, MoonSkyScale = 0.15;
	// The moon's disc, nits per lux of the level's sun at full (2,500 nits against 75,000 lux: about 0.03),
	// and its earthshine (the dark part, of the lit's).
	double MoonDiscOfSun = 0.03, EarthShine = 0.004;
	// The stars come out from StarsFromDeg (sun below the horizon) to StarsFullDeg, StarGain nits for
	// Sirius, the Milky Way MilkyWayGain at its brightest; a bright moon washes out MoonWashes of them.
	double StarsFromDeg = -6.0, StarsFullDeg = -15.0, StarGain = 60.0, MilkyWayGain = 0.6, MoonWashes = 0.6;
	// The glow of towns low round the horizon at night, nits (a dark Dales sky: low).
	double SkyGlow = 0.08;

	// --- Storms (stage 7) ---
	// A thunderstorm: the feed's thunder codes (95 .. 99), or unstable air (CAPE from .. full, J/kg) with
	// heavy enough rain (mm/h). Its cloud towers to ThunderTopM and spreads an anvil from AnvilFrom of it.
	double ThunderCapeFrom = 500.0, ThunderCapeFull = 1500.0, ThunderRainMm = 2.0, ThunderTopM = 10500.0;
	double AnvilFrom = 0.35, AnvilFull = 0.8;
	// Lightning: each storm cell (a lattice StormSpacingM apart, out to StormFarthestM) flashes
	// FlashesPerMin times a minute at full strength (fewer as it weakens), GroundShare of them forks to the
	// ground. A flash lights the cloud round it (FlashCloudGlow, nits, over FlashRadiusM) and the land
	// (a light of FlashLightCd candelas at the cloud base); a fork is drawn within BoltFarthestM.
	double FlashesPerMin = 1.5, GroundShare = 0.3, StormSpacingM = 6000.0, StormFarthestM = 40000.0;
	double FlashCloudGlow = 30.0, FlashRadiusM = 5000.0, FlashLightCd = 2.0e8, BoltFarthestM = 25000.0, BoltWidthM = 600.0;
	double BoltNits = 4000.0;   // a fork's brightness at its flash's peak (times the level's sun over the real one's)
	// Thunder: sound at SoundMps (about 3 s a km), heard out to ThunderFarthestM, full loudness within
	// ThunderNearM, ThunderVolume overall.
	double SoundMps = 343.0, ThunderFarthestM = 22000.0, ThunderNearM = 1500.0, ThunderVolume = 1.0;
	// Hail (white curtains under a storm): the hail codes, or a storm this unstable (J/kg) with the
	// freezing level under HailFreezeM.
	double HailCape = 1500.0, HailFreezeM = 3500.0;
	// Under snow cloud the light goes flat and bright: the sky light times SnowSkyLight, the sun's share
	// times SnowSunShare (shadows soften).
	double SnowSkyLight = 1.5, SnowSunShare = 0.7;

	// --- Rare skies (stage 7) ---
	// Sun dogs: bright patches 22 degrees either side of a low sun (under 40 degrees) in thin ice cloud.
	double SunDogMost = 0.8;
	// Contrails that linger: the air 9 km up colder than ContrailColdC and damp (RH from .. full, %).
	double ContrailColdC = -40.0, ContrailRhFrom = 55.0, ContrailRhFull = 85.0, ContrailMost = 0.7;
	// A fogbow (a broad white bow in thin fog with the sun behind you) and a glory (coloured rings round
	// your shadow on fog below, seen from above it).
	double FogbowMost = 0.5, GloryMost = 0.6;
	// The aurora, from NOAA's Kp index: the oval's edge at AuroraOvalDeg - AuroraKpStep x Kp geomagnetic
	// latitude (the pole at AuroraPoleLat, AuroraPoleLon), seen low in the north up to AuroraSeenBeyond
	// degrees further south; AuroraGain nits at its brightest.
	double AuroraPoleLat = 80.7, AuroraPoleLon = -72.7, AuroraOvalDeg = 67.5, AuroraKpStep = 1.8, AuroraSeenBeyond = 4.0, AuroraGain = 0.4;
	// Noctilucent cloud: summer nights (days of the year from .. to) at these latitudes, the sun 6 to 16
	// degrees down, low in the north; NlcGain nits.
	double NlcFromDay = 145.0, NlcToDay = 220.0, NlcLatFrom = 50.0, NlcLatTo = 65.0, NlcGain = 0.25;
	// Meteors: the sporadic ones an hour with no shower on, a share of them seen from one place, and how
	// bright the brightest are (nits).
	double MeteorSporadic = 6.0, MeteorSeenShare = 0.5, MeteorGain = 30.0;
	// Saharan dust (Open-Meteo's air quality dust, micrograms per cubic metre): from .. full, the haze
	// times 1 + DustMie, the sun warmed (redder) by DustSunWarm.
	double DustFrom = 20.0, DustFull = 150.0, DustMie = 3.0, DustSunWarm = 0.6;
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
	double ExposureOpenStops = 0.0;   // by day, how far below the level's lowest exposure the camera may open
	double SkyFill = 1.0;        // the sky light in the shade, times the level's (on top of SkyLightScale)
	double CloudMs = 1.0;        // the cloud's bounced light, times CloudMsContribution (less under a dull sky)
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
	double BaseFog = 0.0;        // how close under a closed low base the viewer is, 0 .. 1 (the air thickens into it)
	// The night (stage 5).
	double MoonTakeover = 0.0;   // 0 the light is the sun's .. 1 it points at the moon
	double MoonLight = 0.0;      // the light's brightness then, times the level's sun
	double MoonDisc = 0.0;       // the moon's disc, nits per lux of the level's sun (times it in the game)
	double Stars = 0.0;          // how much of the stars shows, 0 .. 1 (times StarGain)
	double MilkyWay = 0.0;       // and of the Milky Way
	double Glow = 0.0;           // the towns' glow, nits
	// Storms (stage 7).
	double Thunder = 0.0;        // how thundery the sky here is, 0 .. 1 (the lightning's rate, the cloud's height)
	double Anvil = 0.0;          // MPC_Sky Anvil: the storm cloud's flat top spreading, 0 .. 1
	double Snowing = 0.0;        // snow falling here, 0 .. 1 (flat bright light; white curtains)
	double Hail = 0.0;           // hail in the storm, 0 .. 1 (white curtains)
	// Rare skies (stage 7).
	double SunDogs = 0.0, Contrails = 0.0, Fogbow = 0.0, Glory = 0.0;
	double Aurora = 0.0;         // how strong, 0 .. 1 (times AuroraGain)
	double AuroraTopDeg = 0.0;   // how high its top stands, degrees above the horizon
	double AuroraBearing = 0.0;  // the way it lies (the geomagnetic pole's way), degrees from north
	double Noctilucent = 0.0;    // 0 .. 1 (times NlcGain), low in the sky over the sun's way
	double MeteorsPerHour = 0.0; // seen from here now (the stars' darkness and the cloud allowing)
	double Dust = 0.0;           // Saharan dust, 0 .. 1
	double SunWarm = 0.0;        // how much warmer (redder) the sun's light is than the level's, 0 .. 1
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
	// Stage 5: the moon from here (degrees up; below -90 none) and its phase angle (0 full .. 180 new).
	double MoonElevation = -99.0;
	double MoonPhaseAngle = 180.0;
	// Stage 7: the place and the day (the aurora, noctilucent cloud, meteor showers), NOAA's planetary
	// Kp index (below 0: not known) and the dust in the air (micrograms per cubic metre; below 0: not known).
	double Lat = 54.3, Lon = -2.2;
	int DayOfYear = 172;
	double Kp = -1.0;
	double Dust = -1.0;
	double MeteorRate = -1.0;    // meteors an hour overhead in a dark sky (below 0: the date's)
};

// The sky.
Sky Compute(const Inputs& In, const Looks& L);

// The rebuilt clouds' edge detail size, metres, for a layer DepthM deep (see Looks::V2DetailDeepM).
double V2DetailSize(double DetailM, double DepthM, const Looks& L);

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

// The weather map (stage 4): the real weather round about (the near grid, eased into the wide one)
// across MapSizeM metres round (Lat, Lon), each grid carried on the wind for its seconds since fetched.
// Bgra: Px x Px texels, row 0 the north, column 0 the west, 4 bytes each: B rain (0 .. MapRainMm mm/h),
// G middle cover, R low cover, A how unstable (CAPE 0 .. 2000). Here*: the covers at the centre (the
// material draws the difference from them, so the cloud over the camera stays the shared weather's).
struct WeatherMap
{
	int Px = 0;
	double SizeM = 0.0;
	std::vector<unsigned char> Bgra;
	double HereLow = 0.0, HereMid = 0.0;
	bool Valid() const { return Px > 0 && (int)Bgra.size() == Px * Px * 4; }
};
struct MapGrids
{
	const TorqueWeather::Grid* Near = nullptr;
	double NearDriftS = 0.0;
	const TorqueWeather::Grid* Wide = nullptr;
	double WideDriftS = 0.0;
	bool Any() const { return (Near && Near->Valid()) || (Wide && Wide->Valid()); }
};
// The weather at a place from the grids (the near one inside its square, the wide one beyond).
TorqueWeather::State MapWeatherAt(const MapGrids& G, double Lat, double Lon);
WeatherMap PaintWeatherMap(const MapGrids& G, double Lat, double Lon, const Looks& L);
// Rain shafts under the showers round (Lat, Lon): metres east and north of it, how wide, how strong (0 .. 1).
// White (stage 7): 0 rain's grey .. 1 a white curtain of snow or hail.
struct RainShaft { double East = 0.0, North = 0.0, RadiusM = 0.0, Strength = 0.0, White = 0.0; };
std::vector<RainShaft> FindRainShafts(const MapGrids& G, double Lat, double Lon, const Looks& L);

// A weather State from a JSON object naming any of its figures (the bench's and the tests' skies).
void StateFromJson(const TorqueFit::Json& J, TorqueWeather::State& InOut);

// A preset sky to load (Data/Sky/presets.json; Settings > Gameplay > Preset sky): its key (kept in the
// settings), the menu's words, and its weather; or a recorded day (Data/Sky/days/<Recorded>.json, read
// by the game). Sun: the sun's height the clock moves to today (Evening: after noon), or -99 to keep the clock.
struct NamedSky
{
	std::string Key, Label, Group, Note, Recorded;
	TorqueWeather::State Weather;
	double Sun = -99.0;
	bool bEvening = false;
	// Showers round about (a made-up wide ring, so the weather map and rain shafts have something to
	// show off a live day): how many, and how heavy (mm/h).
	int Showers = 0;
	double ShowerRainMm = 4.0;
	// The moon to show (the sky's clock moved up to a fortnight to find it): "full", "new", "half" (first
	// quarter: up in the afternoon and evening), or empty for today's.
	std::string Moon;
	// Stage 7, to show off a rare sky: the Kp index (the aurora), the dust (micrograms per cubic metre),
	// the day of the year (noctilucent cloud, a meteor shower) and the meteors an hour; -1 for the real ones.
	double Kp = -1.0, Dust = -1.0, MeteorsPerHour = -1.0;
	int Day = -1;
	bool HasSun() const { return Sun > -90.0; }
};
// The presets file's skies, in its order (any without a key or a weather left out).
std::vector<NamedSky> SkiesFromJson(const TorqueFit::Json& Root);
// A wide ring (7 x 7, 12 km apart) of Base's weather round (Lat, Lon), with Showers heavy showers
// scattered over it (never on the centre, so the weather here stays Base's); the same Seed, the same ring.
TorqueWeather::Grid ShowersAround(const TorqueWeather::State& Base, double Lat, double Lon, int Showers, double RainMm, unsigned Seed);
// The one with this key (nullptr when there's none).
const NamedSky* FindSky(const std::vector<NamedSky>& Skies, const std::string& Key);
} // namespace TorqueSky
