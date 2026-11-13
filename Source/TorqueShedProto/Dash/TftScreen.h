// A modern bike's TFT dash (beenster's plan, Nov 13 2026): what the screen shows, in four designs the
// rider picks between (Dial, Sweep, Classic, Track), on four shapes of screen (the 5 and 6.5 inch
// landscape units, the 10 inch widescreen, the 4 inch round one) and the phone sat nav's upright one.
// Views: the ride (revs, speed, gear and the rest), the sat nav's map, the two side by side on the
// widescreen, the ride mode picker and the settings.
//
// Plain C++: Draw fills a Paint (TftPaint.h) from the bike's state; the game puts that on the bike's
// screen, on the HUD's overlay and in the HUD's map tile. Our own designs: no maker's name, logo or
// lettering.
#pragma once

#include "TftPaint.h"

#include <string>
#include <vector>

namespace TorqueTft
{
struct MapScene;   // TftMap.h

enum class Shape { Wide, Ultra, Round, Phone };
// The screen's own pixels for a shape.
void SizeOf(Shape S, float& W, float& H);

enum class Design { Dial, Sweep, Classic, Track, Count };
const char* DesignName(Design D);

enum class View { Ride, Map, Split, Modes, Settings, Count };

// A row of the settings view, or of the mode picker's list of what a mode holds.
struct Row
{
	std::string Label, Value;
	int Level = -1, Levels = 0;   // pips to draw (Level of Levels), -1 none
	bool Locked = false;          // can't be changed just now (moving; no such part)
};

// One of the aids' boxes on the ride view ("TC", 3).
struct AidBox
{
	std::string Label;
	int Level = 0;        // 0 off
	bool Active = false;  // stepping in this moment (flashes)
};

struct State
{
	// The ride.
	double Speed = 0.0;             // in the units shown
	bool Metric = false;
	double Rpm = 0.0, Redline = 8500.0;
	double RevMax = 10000.0, RevStep = 1000.0;     // the rev counter's scale
	double SpeedMax = 160.0, SpeedStep = 20.0;     // the classic design's speedo scale (units shown)
	int Gear = 0;                   // 0 neutral
	bool EngineOn = true;
	double Fuel = 1.0;              // 0..1 of the tank
	double RangeKm = 0.0, TripKm = 0.0, TotalKm = 0.0, TopKmh = 0.0;
	double KmPerLitre = 0.0;        // this ride's, 0 none yet
	int Hour = 12, Minute = 0;
	double AirC = 15.0, EngineC = 80.0;
	bool HasRevs = true, HasShiftLight = true;
	// Lamps.
	bool IndLeft = false, IndRight = false, HighBeam = false, Lights = false, Fog = false;
	bool LowFuel = false, Hot = false, AbsOff = false, Oil = false;
	double Blink = 0.0;             // seconds, for what flashes
	// Modes and aids.
	std::string Mode = "TOURING";
	std::vector<AidBox> Aids;       // up to four shown
	bool Cruise = false;
	double CruiseSpeed = 0.0;
	// Tyres (shown on the trip box's third page).
	double TyreBar[2] = {2.3, 2.5}, TyreC[2] = {30.0, 30.0};
	// The sat nav's words (the map itself is the scene).
	bool Nav = false, NavOff = false;
	int NavTurn = 9;                // -3 sharp left .. 3 sharp right, 9 the finish
	std::string NavDistance, NavOnto, NavLeft, NavEta, NavRoad, NavPlace;
	int Limit = 0;                  // the road's limit as its sign shows it, 0 none
	bool LimitNational = false, LimitAssumed = false;
	bool Night = false;
	std::string Notice;             // a line across the foot of the screen ("Close the throttle to change mode")

	// The screen's own state.
	Design Look = Design::Dial;
	View Now = View::Ride;
	int TripPage = 0;               // 0 trip, 1 fuel, 2 tyres
	// The mode picker: every mode offered, the one in use, the one the bar's on, and that one's rows.
	std::vector<std::string> Modes;
	int ModeNow = 0, ModeCursor = 0;
	std::vector<Row> ModeRows;
	// The settings: its rows, the one the bar's on, whether it's being changed, and a line on it.
	std::vector<Row> Settings;
	int SetCursor = 0;
	bool SetEditing = false;
	std::string SetHelp;
};

// The whole screen. Map is the sat nav's scene (null: none, the map views say so).
void Draw(Paint& P, Shape S, const State& St, const MapScene* Map);
// The map alone with its turn banner and limit sign, to fill W x H: the HUD's tile.
void DrawMapTile(Paint& P, const State& St, const MapScene* Map);
// What a number reads as: "12", "3.4".
std::string Whole(double V);
std::string Fixed(double V, int Decimals);
} // namespace TorqueTft
