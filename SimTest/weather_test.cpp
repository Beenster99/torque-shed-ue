// The weather engine (live weather plan, stages 1 and 2): the presets, the fades, roads that
// remember rain, the words, and reading the live feeds' answers.
#include "WeatherModel.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace TorqueWeather;

static int Fails = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Fails;
}
static bool Near(double A, double B, double Tol) { return std::fabs(A - B) <= Tol; }

int main()
{
	// The presets are the three kinds the game had, and read back as them.
	for (int K = 0; K < 3; ++K) Check(NearestKind(Preset(K)) == K, "each preset reads back as its kind");
	Check(Near(Dullness(Preset(Sunny)), 0.0, 1e-9) && Near(Dullness(Preset(Overcast)), 1.0, 1e-9) && Near(Dullness(Preset(Wet)), 1.0, 1e-9),
		"the light reads the presets as before: sunny 0, overcast and wet 1");
	Check(Near(WetLook(Preset(Sunny)), 0, 1e-9) && Near(WetLook(Preset(Overcast)), 0, 1e-9) && Near(WetLook(Preset(Wet)), 1, 1e-9), "only the wet preset looks wet");
	Check(FogBoost(Preset(Sunny)) == 1.0 && FogBoost(Preset(Overcast)) == 1.0 && FogBoost(Preset(Wet)) == 1.0, "the presets' haze is as before");

	// Fades: halfway is in between, the end is exact, and nothing jumps.
	Fader F;
	F.Set(Preset(Sunny));
	F.FadeTo(Preset(Wet), 30.0);
	double Last = Dullness(F.Now), MaxStep = 0.0;
	for (int I = 0; I < 310; ++I)
	{
		F.Tick(0.1);
		MaxStep = std::fmax(MaxStep, std::fabs(Dullness(F.Now) - Last));
		Last = Dullness(F.Now);
		if (I == 149) Check(F.Now.RoadWet > 0.3 && F.Now.RoadWet < 0.7, "halfway through a fade the road is half wet");
	}
	Check(!F.Fading() && F.Now.RoadWet == 1.0 && F.Now.Code == 61, "a fade ends exactly on its target");
	Check(MaxStep < 0.02, "a 30 s fade never steps the light by more than 2% a tenth of a second");
	State A = Preset(Sunny), B = Preset(Sunny);
	A.WindFrom = 350.0;
	B.WindFrom = 10.0;
	Check(Near(Blend(A, B, 0.5).WindFrom, 0.0, 0.01) || Near(Blend(A, B, 0.5).WindFrom, 360.0, 0.01), "the wind turns the short way round north");

	// Roads remember.
	const double Soaked = RoadWaterAfter({0, 0, 2, 2, 0, 0}, 10, 4, 1);
	const double JustRained = RoadWaterAfter({0, 0, 0, 0, 0, 1.5}, 10, 4, 1);
	const double LongAgo = RoadWaterAfter({3, 0, 0, 0, 0, 0}, 14, 6, 0.3);
	std::printf("      road after rain 2-4 h ago %.2f, rain this hour %.2f, rain 6 h ago on a breezy day %.2f\n",
		RoadWetFromWater(Soaked), RoadWetFromWater(JustRained), RoadWetFromWater(LongAgo));
	Check(RoadWetFromWater(JustRained) > 0.95, "rain in the last hour: the road is wet");
	Check(RoadWetFromWater(Soaked) > 0.5, "heavy rain two hours ago on a grey day: still wet");
	Check(RoadWetFromWater(LongAgo) < 0.05, "rain six hours ago on a breezy brighter day: dry");
	Check(RoadWetFromWater(RoadWaterAfter({0, 0, 0, 0, 0, 0}, 10, 4, 1)) == 0.0, "no rain: dry");
	double W = 0.0;
	int Minutes = 0;
	while (RoadWetFromWater(W) < 0.9 && Minutes < 240) { W = RoadWaterStep(W, 0.3, 10, 4, 1, 60); ++Minutes; }
	std::printf("      drizzle (0.3 mm/h) wets the road in %d min\n", Minutes);
	Check(Minutes >= 10 && Minutes <= 45, "drizzle wets the road in 10 to 45 minutes");
	W = 0.5;
	Minutes = 0;
	while (RoadWetFromWater(W) > 0.1 && Minutes < 600) { W = RoadWaterStep(W, 0.0, 11, 4, 1, 60); ++Minutes; }
	std::printf("      a soaked road dries on a grey 11 C day in %d min\n", Minutes);
	Check(Minutes >= 120 && Minutes <= 300, "a soaked road takes 2 to 5 hours to dry on a grey day");

	// Words.
	State Rainy = Preset(Wet);
	Rainy.Air = 9.2;
	Rainy.Wind = 8.0;
	Rainy.Gust = 8.5;
	Rainy.WindFrom = 225.0;
	std::printf("      %s\n", Describe(Rainy, true).c_str());
	Check(Describe(Rainy, true) == "9 \xC2\xB0" "C, light rain, wind SW 18 mph", "the plan's example line");
	Check(Describe(Rainy, false) == "9 \xC2\xB0" "C, light rain, wind SW 29 km/h", "in km/h");
	Rainy.Gust = 16.0;
	Check(Describe(Rainy, true).find("gusting 36") != std::string::npos, "gusts that stand out are named");
	Check(Compass(0) == "N" && Compass(359) == "N" && Compass(90) == "E" && Compass(200) == "S" && Compass(300) == "NW", "compass points");

	// Open-Meteo's answer (the shape its docs give, with timeformat=unixtime).
	const std::string Om = R"({"latitude":54.35,"longitude":-2.2,"elevation":420,"utc_offset_seconds":0,"timezone":"GMT",
		"current_units":{"time":"unixtime","interval":"seconds"},
		"current":{"time":1794265200,"interval":900,"temperature_2m":7.4,"dew_point_2m":6.6,"cloud_cover_low":95,"precipitation":0.3,"snowfall":0.0,"cloud_cover":100,
			"wind_speed_10m":6.1,"wind_direction_10m":238,"wind_gusts_10m":14.2,"weather_code":63,"visibility":4200},
		"hourly":{"time":[1794243600,1794247200,1794250800,1794254400,1794258000,1794261600,1794265200],
			"precipitation":[0.0,0.0,0.4,1.1,0.8,0.6,0.5]}})";
	State Live;
	long long T = 0;
	std::string Err;
	const bool bOk = ParseOpenMeteo(Om, Live, &T, &Err);
	std::printf("      Open-Meteo: %s (road %.2f, rain %.1f mm/h, vis %.0f m) %s\n", Describe(Live, true).c_str(), Live.RoadWet, Live.Rain, Live.Visibility, Err.c_str());
	Check(bOk && T == 1794265200, "Open-Meteo's answer reads");
	Check(Near(Live.Rain, 1.2, 1e-6) && Near(Live.Cloud, 1.0, 1e-9) && Live.Code == 63 && Near(Live.Visibility, 4200, 1e-6), "rain as a rate, cloud as a fraction");
	Check(Live.RoadWet > 0.95 && NearestKind(Live) == Wet, "it has rained all afternoon: wet roads");
	Check(FogBoost(Live) > 1.5, "4 km visibility thickens the haze");
	Check(!ParseOpenMeteo(R"({"error":true,"reason":"Latitude must be in range"})", Live, nullptr, &Err) && Err.find("Latitude") != std::string::npos, "the service's errors come through");
	Check(!ParseOpenMeteo("<html>", Live), "a broken answer is refused");
	Check(OpenMeteoUrl(54.353529, -2.205817).find("latitude=54.3535&longitude=-2.2058") != std::string::npos, "four decimals in the address");
	Check(OpenMeteoUrl(-33.86, 151.21).find("latitude=-33.8600&longitude=151.2100") != std::string::npos, "anywhere on Earth (Sydney)");

	// MET Norway's answer (compact).
	const std::string Met = R"({"type":"Feature","properties":{"timeseries":[{"time":"2026-11-09T22:00:00Z","data":{
		"instant":{"details":{"air_pressure_at_sea_level":1002.1,"air_temperature":5.9,"cloud_area_fraction":96.1,"relative_humidity":93.0,
			"wind_from_direction":250.3,"wind_speed":7.2}},
		"next_1_hours":{"summary":{"symbol_code":"lightrainshowers_night"},"details":{"precipitation_amount":0.4}}}}]}})";
	State M;
	const bool bMet = ParseMetNo(Met, M, &T, &Err);
	std::printf("      MET Norway: %s (road %.2f) at %lld\n", Describe(M, false).c_str(), M.RoadWet, T);
	Check(bMet && M.Code == 80 && Near(M.Rain, 0.4, 1e-9) && M.RoadWet > 0.9, "MET Norway's answer reads");
	Check(T == 1794261600, "its time reads as UTC");
	Check(MetNoSymbolCode("heavyrain") == 65 && MetNoSymbolCode("clearsky_day") == 0 && MetNoSymbolCode("fog") == 45 && MetNoSymbolCode("rainandthunder") == 95,
		"MET Norway's symbols");

	// Low cloud on the pass top: a deck based at 450 m swallows the summit (526 m) and not Thwaite (300 m).
	Check(Near(Live.CloudBase, 420.0 + 125.0 * (7.4 - 6.6), 0.5) && Near(Live.LowCloud, 0.95, 1e-9), "the cloud base from the dew point, above the grid's height");
	State Low = Preset(Overcast);
	Low.LowCloud = 0.95;
	Low.CloudBase = 450.0;
	std::printf("      in cloud at Thwaite %.2f, at the summit %.2f; haze %.1fx and %.1fx\n", InCloud(Low, 300.0), InCloud(Low, 526.0), HazeAt(Low, 300.0), HazeAt(Low, 526.0));
	Check(InCloud(Low, 300.0) == 0.0 && InCloud(Low, 526.0) > 0.9, "low cloud: the summit is in it, the dale is clear");
	Check(HazeAt(Low, 526.0) > 10.0 && HazeAt(Low, 300.0) == 1.0, "in the cloud the haze closes in");
	Check(InCloud(Preset(Overcast), 526.0) == 0.0 && InCloud(Preset(Wet), 526.0) == 0.0, "the presets keep the summit out of cloud (as before)");
	Low.LowCloud = 0.3;
	Check(InCloud(Low, 600.0) == 0.0, "a few scraps of low cloud don't swallow the road");
	Check(Near(DewPointFrom(10.0, 100.0), 10.0, 0.01) && Near(DewPointFrom(20.0, 50.0), 9.3, 0.2), "dew point from humidity");

	// Puddles: the edges after a few millimetres, the middle only after hours of heavy rain, and they drain.
	{
		const double Shower = PuddlesAfter({0, 0, 0, 0, 0, 0, 0.5, 0.5}, 10);
		const double Afternoon = PuddlesAfter({0, 0, 0, 0, 1, 1.5, 1, 1}, 10);
		const double Soaking = PuddlesAfter({2, 3, 3, 4, 3, 3, 2, 3}, 10);
		const double After = PuddlesAfter({2, 3, 3, 4, 0, 0, 0, 0}, 10);
		std::printf("      puddles: shower %.2f, an afternoon's rain %.2f, a soaking day %.2f, four dry hours after it %.2f\n", Shower, Afternoon, Soaking, After);
		Check(Shower < 0.25, "a short shower: hardly any standing water");
		Check(Afternoon >= 0.35 && Afternoon < 0.6, "a few hours of rain: the edges fill, not the middle");
		Check(Soaking > 0.75, "a soaking day: puddles reach across the road");
		Check(After < Soaking * 0.6, "they drain once it stops");
		Check(Preset(Wet).Puddles == 0.4 && Preset(Sunny).Puddles == 0.0, "the wet preset has edge puddles, as before");
		Check(Live.Puddles > 0.25, "the live afternoon of rain has edge puddles");
	}

	// Lying snow: none on a normal day, a white land once a few centimetres lie, and kept through a save.
	{
		State Deep = Preset(Overcast);
		Deep.SnowDepth = 0.05;
		Deep.Air = -2.0;
		State Dust = Deep;
		Dust.SnowDepth = 0.008;
		State Falling = Preset(Overcast);
		Falling.Snow = 2.0;
		Falling.Air = -1.0;
		State Back;
		const bool Loaded = Load(Save(Deep), Back);
		std::printf("      snow cover: deep %.2f, a dusting %.2f, falling on bare ground %.2f\n", SnowCover(Deep), SnowCover(Dust), SnowCover(Falling));
		Check(SnowCover(Preset(Sunny)) == 0.0 && SnowCover(Preset(Wet)) == 0.0 && SnowCover(Live) == 0.0, "no snow on the presets or the live afternoon");
		Check(SnowCover(Deep) == 1.0 && SnowCover(Dust) > 0.05 && SnowCover(Dust) < 0.3, "a few centimetres whiten the land, a dusting only a little");
		Check(SnowCover(Falling) > 0.2 && SnowCover(Falling) <= 0.6, "snow falling in the cold starts to settle");
		Check(Loaded && Near(Back.SnowDepth, 0.05, 1e-3), "the snow depth survives a save");
	}

	// Rain to see, wind to sway.
	Check(RainShow(Preset(Sunny)) == 0.0 && RainShow(Preset(Wet)) > 0.4 && RainShow(Preset(Wet)) < 0.5, "light rain shows nearly half a downpour");
	State Pour = Preset(Wet);
	Pour.Rain = 12.0;
	Check(RainShow(Pour) == 1.0, "a downpour is the most");
	State Calm = Preset(Sunny), Breeze = Preset(Sunny), Gale = Preset(Sunny);
	Calm.Wind = 0.0; Calm.Gust = 0.0;
	Breeze.Wind = 6.0; Breeze.Gust = 9.0;
	Gale.Wind = 20.0; Gale.Gust = 30.0;
	std::printf("      sway: calm %.2f, breeze %.2f, gale %.2f\n", SwayFromWind(Calm), SwayFromWind(Breeze), SwayFromWind(Gale));
	Check(SwayFromWind(Preset(Sunny)) > 0.85 && SwayFromWind(Preset(Sunny)) < 1.15, "the sunny preset sways as the plants were made");
	Check(SwayFromWind(Calm) == 0.15 && Near(SwayFromWind(Breeze), 1.0, 0.1) && SwayFromWind(Gale) == 2.5, "sway: calm, the usual breeze, a gale");

	// Saved and loaded (the last live weather, for a start with no internet).
	State Back;
	Check(Load(Save(Live), Back) && Back.Code == Live.Code && Near(Back.Air, Live.Air, 0.01) && Near(Back.RoadWet, Live.RoadWet, 0.001), "saved weather loads back");
	Check(!Load("nonsense", Back), "a broken save is refused");
	Check(Back.LowCloud == Live.LowCloud || Near(Back.CloudBase, Live.CloudBase, 1.0), "low cloud saved too");
	Check(Load("w1 1 0 0 3 225 6 14 40000 0 3", Back) && Back.CloudBase == 5000.0 && Back.Puddles == 0.0, "an older save (no low cloud) still loads");
	Check(Load(Save(Live), Back) && Near(Back.Puddles, Live.Puddles, 0.001), "puddles saved too");

	// Stage 4: what it does to the ride.
	Check(WetGrip(Preset(Sunny)) == 1.0 && WetGrip(Preset(Overcast)) == 1.0, "dry presets: full grip");
	{
		State Damp = Preset(Overcast);
		Damp.RoadWet = 0.3;
		State Drenched = Preset(Wet);
		Drenched.Puddles = 0.0;
		State Flooded = Drenched;
		Flooded.Puddles = 1.0;
		std::printf("  grip: damp %.2f, soaked %.2f, soaked with standing water %.2f\n", WetGrip(Damp), WetGrip(Drenched), WetGrip(Flooded));
		Check(WetGrip(Damp) > 0.85 && WetGrip(Damp) < 0.95, "a damp road takes a little grip");
		Check(Near(WetGrip(Drenched), 0.68, 0.005), "a soaked road grips as the old wet roads did (0.68)");
		Check(WetGrip(Flooded) < WetGrip(Drenched) - 0.04 && WetGrip(Flooded) >= 0.6, "standing water takes a little more");
	}
	{
		State Windy = Preset(Overcast);
		Windy.Wind = 9.0; Windy.Gust = 18.0;
		double Lo = 1e9, Hi = 0.0, Mean = 0.0, From = 0.0, FromLo = 1e9, FromHi = -1e9;
		int Gusts = 0;
		bool In = false;
		for (int I = 0; I < 6000; ++I)   // ten minutes, every 0.1 s
		{
			const double Wr = WindAtRider(Windy, I * 0.1, 0.0, From);
			Lo = std::min(Lo, Wr); Hi = std::max(Hi, Wr); Mean += Wr / 6000.0;
			FromLo = std::min(FromLo, From); FromHi = std::max(FromHi, From);
			const double G = GustShare(I * 0.1);
			const bool Now = In ? G > 0.3 : G > 0.6;
			if (Now && !In) ++Gusts;
			In = Now;
		}
		std::printf("  gusty day (9 m/s, gusts 18): at the rider %.1f .. %.1f m/s, mean %.1f, %d gusts in 10 min, from %.0f .. %.0f deg\n", Lo, Hi, Mean, Gusts, FromLo, FromHi);
		Check(Near(Lo, 9.0 * 0.65, 0.1) && Near(Hi, 18.0 * 0.65, 0.3), "rider-height wind runs from the steady wind to the gusts");
		Check(Gusts >= 5 && Gusts <= 40, "a few gusts a minute at most");
		Check(FromHi - FromLo < 40.0 && FromHi - FromLo > 10.0, "the direction wanders a little");
		Check(WindAtRider(Windy, 3.0, 1.0, From) < WindAtRider(Windy, 3.0, 0.0, From) * 0.25, "shelter takes most of it");
	}

	// Stage 5: weather across distance.
	{
		const int N = 3;
		const double Step = 2500.0;
		const std::string Url = OpenMeteoGridUrl(54.35, -2.2, N, Step);
		size_t Commas = 0;
		for (char Ch : Url.substr(0, Url.find("&longitude"))) Commas += Ch == ',';
		Check(Commas == 8 && Url.find("timezone=auto") != std::string::npos, "the grid is asked for in one call (9 points)");
		// A shower over the western column, dry elsewhere; a 10 m/s westerly; the ground 300 m, the
		// eastern points 500 m.
		std::string Body = "[";
		for (int I = 0; I < N * N; ++I)
		{
			const int Col = I % N;
			char Point[900];
			std::snprintf(Point, sizeof Point,
				R"(%s{"latitude":54.35,"longitude":-2.2,"elevation":%d,"utc_offset_seconds":3600,"current":{"time":1794265200,"interval":900,"temperature_2m":10.0,"dew_point_2m":8.0,"cloud_cover_low":50,"precipitation":%s,"snowfall":0.0,"cloud_cover":%d,"wind_speed_10m":10.0,"wind_direction_10m":270,"wind_gusts_10m":15.0,"weather_code":%d,"visibility":20000},"hourly":{"time":[1794261600],"precipitation":[0.0]}})",
				I ? "," : "", Col == 2 ? 500 : 300, Col == 0 ? "1.0" : "0.0", Col == 0 ? 100 : 40, Col == 0 ? 63 : 2);
			Body += Point;
		}
		Body += "]";
		Grid G;
		int Utc = 0;
		std::string Why;
		Check(ParseOpenMeteoGrid(Body, 54.35, -2.2, N, Step, G, nullptr, &Utc, &Why) && G.Valid(), "the grid's answer reads");
		Check(Utc == 3600, "  with the clock's offset from UTC");
		Check(!ParseOpenMeteoGrid(Body, 54.35, -2.2, 5, Step, G, nullptr, nullptr, &Why), "  a grid of the wrong size is refused");
		ParseOpenMeteoGrid(Body, 54.35, -2.2, N, Step, G);
		double WestLat = 0.0, WestLon = 0.0, MidLat = 0.0, MidLon = 0.0;
		Offset(54.35, -2.2, -2500.0, 0.0, WestLat, WestLon);
		Offset(54.35, -2.2, -1250.0, 0.0, MidLat, MidLon);
		const State Centre = GridAt(G, 54.35, -2.2, -2000.0, 0.0);
		const State West = GridAt(G, WestLat, WestLon, -2000.0, 0.0);
		const State Between = GridAt(G, MidLat, MidLon, -2000.0, 0.0);
		std::printf("  rain: centre %.1f, half way west %.1f, west %.1f mm/h\n", Centre.Rain, Between.Rain, West.Rain);
		Check(Centre.Rain == 0.0 && Near(West.Rain, 4.0, 0.01) && Near(Between.Rain, 2.0, 0.01), "the shower is where it is, blending between points");
		const State Drifted = GridAt(G, 54.35, -2.2, -2000.0, 250.0);
		std::printf("  250 s later on the westerly: centre %.1f mm/h\n", Drifted.Rain);
		Check(Near(Drifted.Rain, 4.0, 0.05), "  it drifts east on the wind (2.5 km in 250 s at 10 m/s)");
		Check(Near(GridAt(G, 54.35, -2.2, -2000.0, 1e6).Rain, 4.0, 0.05), "  no further than half the grid");
		const State Up = GridAt(G, 54.35, -2.2, 800.0, 0.0);
		Check(Near(Up.Air, 10.0 - 0.0065 * 500.0, 0.01), "the air is colder higher up (0.65 C per 100 m)");
		Check(Near(Up.WindFrom, 270.0, 0.5), "the wind's direction comes through");
		double A1 = 0.0, B1 = 0.0, A2 = 0.0, B2 = 0.0;
		SnapCentre(54.3500, -2.2000, Step, A1, B1);
		SnapCentre(54.3501, -2.2003, Step, A2, B2);
		Check(A1 == A2 && B1 == B2, "a little riding keeps the same grid centre");
		const State Base = Preset(Overcast);
		const State Local = AddDifference(Base, West, Centre, 1.0);
		Check(Near(Local.Rain, Base.Rain + 4.0, 0.01) && Local.Cloud == 1.0 && Local.Code == 63, "the shared weather made local");
		Check(AddDifference(Base, West, Centre, 0.0).Rain == Base.Rain, "  and none of it at 0");
		std::printf("  sun: declination 21 Jun %.1f, 21 Dec %.1f; equation of time 3 Nov %+.1f min\n", SunDeclination(172), SunDeclination(355), EquationOfTime(307));
		Check(Near(SunDeclination(172), 23.4, 0.3) && Near(SunDeclination(355), -23.4, 0.3), "the sun's height follows the date");
		Check(Near(EquationOfTime(307), 16.4, 1.0), "the sundial runs ahead in early November");
	}

	std::printf(Fails ? "\n%d FAILED\n" : "\nall passed\n", Fails);
	return Fails ? 1 : 0;
}
