// Storms and rare skies (skies plan, stage 7), plain C++ so the same code runs in the game (ATorqueSky)
// and in SimTest's sky_test.
//
// Lightning: the storm cells round the viewer (a lattice fixed to the ground and carried on the wind,
// as the rain shafts are, each with the weather the grids give it) flash at a rate set by how thundery
// the air there is. Which second a cell flashes in comes from a hash of the cell and the clock, so every
// machine in co-op sees the same flash in the same place with nothing sent. Thunder follows at the speed
// of sound (about 3 s a km) and is made here, sample by sample (no recordings: nothing to license): a
// crack for a near strike, then the rumble, longer and duller the farther away.
//
// Meteors (the sporadic ones and the year's showers) come the same way, from the clock, and the
// geomagnetic latitude says how far south the aurora reaches for a Kp index.
#pragma once

#include "SkyModel.h"

#include <cstdint>
#include <vector>

namespace TorqueStorm
{
// How thundery a weather is, 0 .. 1: the feed's thunder codes (95 .. 99), or unstable air with heavy rain.
double Thunderiness(const TorqueWeather::State& W, const TorqueSky::Looks& L);
// Hail in it, 0 .. 1 (the hail codes, or a strong storm with a low freezing level), and snow falling, 0 .. 1.
double Haily(const TorqueWeather::State& W, const TorqueSky::Looks& L);
double Snowy(const TorqueWeather::State& W);

// One storm cell of the lattice: where (latitude and longitude, carried on the wind), its weather's
// thunderiness, the cloud base there, and its lattice place (for the clock's hash).
struct Cell
{
	int I = 0, J = 0;
	double Lat = 0.0, Lon = 0.0;
	double Storm = 0.0;
	double BaseAsl = 1000.0;
	double Hail = 0.0;
};
// The cells round (Lat, Lon) that are thundery at all, from the grids, or (with none) Here everywhere.
std::vector<Cell> StormCells(const TorqueSky::MapGrids& G, const TorqueWeather::State& Here, double Lat, double Lon, const TorqueSky::Looks& L);

// A flash.
struct Strike
{
	double At = 0.0;          // seconds since 1970, UTC
	double Lat = 0.0, Lon = 0.0;
	bool bGround = false;     // a fork to the ground (or a flash inside the cloud)
	double Power = 1.0;       // 0.3 .. 1
	uint32_t Seed = 0;        // its fork's shape, its thunder's
	double BaseAsl = 1000.0;  // the cloud base over it
	int Strokes = 1;          // return strokes (the flicker)
};
// The flashes in [FromUnix, ToUnix) of these cells, in time order.
std::vector<Strike> StrikesBetween(const std::vector<Cell>& Cells, double FromUnix, double ToUnix, const TorqueSky::Looks& L);
// How bright a flash is T seconds after it began, 0 .. 1 (each return stroke a sharp peak, about 60 ms
// apart, dying in a few tens of ms), and 0 after about half a second.
double FlashAt(const Strike& S, double T);
// When its thunder arrives (seconds after the flash) from this far away, and how loud, 0 .. 1 (0 unheard).
double ThunderDelay(double DistanceM, const TorqueSky::Looks& L);
double ThunderLoudness(double DistanceM, double Power, const TorqueSky::Looks& L);

// Thunder, made: mono samples at Rate per second, peaking at about Loud x 30000. Near (under 2 km) it
// opens with a tearing crack; farther off only the low rumble, rolling for longer.
std::vector<int16_t> ThunderSound(double DistanceM, double Loud, uint32_t Seed, int Rate);

// Meteors (stage 7): the year's main showers (their peak, how wide, the zenith hourly rate) and the
// sporadic ones: how many an hour on this day of the year a dark sky shows overhead.
double MeteorRate(int DayOfYear, const TorqueSky::Looks& L);
const char* ShowerOn(int DayOfYear);   // the shower at its best now ("Perseids"), or "" for none
// The meteor in the sky at this moment, if any: where it starts (degrees up, bearing from north), the
// way it runs (degrees, 0 straight down, clockwise on the sky as seen), how long (degrees), how far
// along it is (0 .. 1) and how bright (0 .. 1). PerHour: as Sky::MeteorsPerHour.
struct Meteor
{
	bool bOn = false;
	double El = 0.0, Az = 0.0, Dir = 0.0, Length = 0.0, Along = 0.0, Bright = 0.0;
};
Meteor MeteorAt(double Unix, double PerHour);

// Geomagnetic latitude (degrees) of a place for a pole, and the bearing from the place towards the pole.
double GeomagneticLat(double Lat, double Lon, double PoleLat, double PoleLon);
double BearingTo(double Lat, double Lon, double ToLat, double ToLon);
} // namespace TorqueStorm
