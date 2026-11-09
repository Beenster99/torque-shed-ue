// The weather as one set of numbers (live weather plan, stages 1 and 2), plain C++ so the same code
// runs in the game (ATorqueGameState) and in SimTest's weather_test.
//
// Where the weather comes from (the source: the live feed, a fixed kind, or changing by itself) only
// ever fills in a State; everything the game shows or feels reads a State, so it does not care which.
// Sunny, overcast and wet roads, the three kinds the game had, are presets inside it, and a change
// eases from one State to the next over time instead of snapping.
#pragma once

#include <string>
#include <vector>

namespace TorqueWeather
{
// The air and the road where the rider is.
struct State
{
	double Cloud = 0.1;          // cloud cover, 0 clear .. 1 overcast
	double Rain = 0.0;           // rain (and, until Phase 2's snow, anything falling), mm per hour
	double Snow = 0.0;           // snowfall, cm per hour (shown from Phase 2; counted as wet until then)
	double Wind = 3.0;           // mean wind at 10 m, metres per second
	double WindFrom = 225.0;     // where the wind comes from: degrees from north, clockwise
	double Gust = 6.0;           // gusts at 10 m, metres per second
	double Air = 14.0;           // air temperature, degrees C
	double Visibility = 40000.0; // metres
	double RoadWet = 0.0;        // how wet the road is, 0 dry .. 1 soaked (roads remember: see RoadWetAfter)
	int Code = 1;                // the WMO weather code, for the words ("light rain")
};

enum EKind { Sunny = 0, Overcast = 1, Wet = 2 };

// The three kinds the game always had, as States.
State Preset(int Kind);
// The kind a State is nearest (for the bike's grip until stage 4, the HUD and the logs).
int NearestKind(const State& S);

// A blend: T 0 is A, 1 is B. The wind turns the short way round; the code is the nearer one's.
State Blend(const State& A, const State& B, double T);

// How the light reads a State: 0 for the sunny preset's sky, 1 for the overcast and wet presets'.
double Dullness(const State& S);
// How wet the day looks (the darker light and thicker haze of the old "wet roads"): 0 dry, 1 the wet preset.
double WetLook(const State& S);
// How much thicker the haze is than the presets', from the visibility: 1 down to 8 km, more in fog.
double FogBoost(const State& S);

// Roads remember. A road's film of water, mm: rain adds to it, drying takes it away (faster when
// warm, windy or clear), and past about 1 mm it runs off. How wet it looks follows the film.
double RoadWaterStep(double WaterMm, double RainMmPerHour, double Air, double Wind, double Cloud, double Seconds);
double RoadWetFromWater(double WaterMm);
// The road now, from the rain of the last few hours (oldest first, mm in each hour) and the air.
double RoadWaterAfter(const std::vector<double>& RainPerHour, double Air, double Wind, double Cloud);

// Moves from one State to another over a set time, smoothly (ease in and out).
struct Fader
{
	State Now, From, To;
	double T = 1.0, Seconds = 0.0;
	void Set(const State& S) { Now = From = To = S; T = 1.0; }
	void FadeTo(const State& S, double OverSeconds);
	// True while it moved.
	bool Tick(double Dt);
	bool Fading() const { return T < 1.0; }
};

// Words.
std::string CodeWords(int Code);            // "light rain"
std::string Compass(double FromDegrees);    // "SW"
// "9 °C, light rain, wind SW 18 mph" (km/h when not Imperial), with gusts when they stand out.
std::string Describe(const State& S, bool bImperial);

// The live feed (stage 2): asked by latitude and longitude, so it works anywhere on Earth.
// Open-Meteo (free for non-commercial use, no account; data CC BY 4.0) is the main feed.
std::string OpenMeteoUrl(double Lat, double Lon);
// The State from its answer; false (with why) when the answer isn't one. Time is its seconds since 1970.
bool ParseOpenMeteo(const std::string& Body, State& Out, long long* Time = nullptr, std::string* Error = nullptr);
// MET Norway's locationforecast (free) is the backup: no past rain, so the road is judged from now.
std::string MetNoUrl(double Lat, double Lon);
bool ParseMetNo(const std::string& Body, State& Out, long long* Time = nullptr, std::string* Error = nullptr);
int MetNoSymbolCode(const std::string& Symbol);

// A State kept as text (the last live weather, saved for a start with no internet) and back.
std::string Save(const State& S);
bool Load(const std::string& Text, State& Out);
} // namespace TorqueWeather
