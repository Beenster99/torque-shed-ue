// [race tracks] Race circuits on a ride (the plan "UK race tracks you can ride"): what the game knows of each
// circuit beyond the map (Data/RideUK/circuits.json: its start line, its sectors, which way round it is
// raced, and the road courses' gates), the index of a ride's circuit ground (gravel traps, run-off), and
// the lap timer.
//
// The circuits themselves are roads in the ride's region (WorldCircuits.cpp: classes "raceway" and
// "pit_lane"), drawn and ridden like any road but painted as a circuit (RidePlan::CircuitMarks).
//
// The lap timer (CircuitLaps) is plain C++, fed the rider's place each frame: crossing a circuit's start
// line starts a lap; its sectors' lines split it (in the order they come, either way round); crossing the
// start line again after the sectors ends it. A lap with the rider more than OffTrack metres off the
// circuit's tarmac, or shorter than half the circuit, is not counted. On a road course (the TT, the North
// West 200, Oliver's Mount, Dundrod) the gates are places on the public road, passed within GateReach in
// order. Each rider's own game times their laps (Coop/TorqueLaps.h) and tells the others their best.
#pragma once

#include "WorldGeo.h"
#include "WorldRegion.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
class RidePlan;

// What Data/RideUK/circuits.json says of a circuit or a road course.
struct CircuitNote
{
	std::string Name;                  // as shown (a place name only: no logos, no series)
	std::vector<std::string> Match;    // words in the map's name that make it this one ("Cadwell")
	Geo At;                            // its middle (matched within 3 km when no word does)
	Geo Start;                         // its start and finish line (on its track): Lat 0 none
	std::vector<Geo> Sectors;          // its sector lines, in the order raced
	std::vector<std::string> SectorNames;
	bool Clockwise = true;             // which way round it is raced (seen from above)
	bool Road = false;                 // a road course: Gates on the public road, not a track
	std::vector<Geo> Gates;            // a road course's gates, in order (the start not among them)
	std::vector<std::string> GateNames;
	bool Test = false;                 // one of the plan's two test circuits
	std::string About;
};
struct CircuitNotes
{
	std::vector<CircuitNote> Circuits;
	// The note of a circuit named Name (the map's) whose middle is at At, or null.
	const CircuitNote* Find(const std::string& Name, const Geo& At) const;
};
// Data/RideUK/circuits.json read (thread safe, read once at start); false where it isn't one.
bool SetCircuitNotes(const std::string& Text);
std::shared_ptr<const CircuitNotes> CircuitNotesNow();
bool ReadCircuitNotes(const std::string& Text, CircuitNotes& Out);

// A ride's circuit ground: its areas in 64 m squares (RidePlan::IndexCircuits), and each circuit's start
// and sector lines on its track as laid (for the paint and the lap timer).
struct CircuitIndex
{
	static constexpr double Square = 64.0;
	std::unordered_map<long long, std::vector<int>> Areas;   // into Region::CircuitAreas
	static long long KeyOf(double X, double Z);
	struct Line
	{
		P2 At, Across;        // its middle and its direction across the track (unit)
		double Half = 6.0;    // half the track's width there
		int Circuit = -1;     // into Region::Circuits
		int Kind = 0;         // 0 the start, 1 + n sector n's, -1 a road course's gate (Name its name)
		std::string Name;
	};
	std::vector<Line> Lines;
	// For each circuit (Region::Circuits): its note's index in CircuitNotes (-1 none) and the note.
	std::vector<std::shared_ptr<const CircuitNote>> Notes;
};

// The lap timer: one rider's laps of the circuits on a ride.
class CircuitLaps
{
public:
	static constexpr double OffTrack = 6.0;     // metres off the tarmac's edge that spoil a lap
	static constexpr double GateReach = 60.0;   // a road course's gate is passed within this (at the road nearest its note's place)
	struct Lap
	{
		std::string Circuit;        // its name
		double Seconds = 0.0;
		std::vector<double> Splits; // each sector's time, the last to the line
		bool Counted = true;
	};
	struct State
	{
		std::string Circuit;        // the circuit being lapped ("" none)
		bool Running = false;       // a lap under way
		double Started = 0.0;       // the clock when it started
		int NextSector = 0;         // the next of the circuit's lines to cross (sectors, then the start)
		int Way = 0;                // 1 the sectors in their order, -1 the other way round, 0 not known yet
		bool Spoilt = false;        // off the track this lap
		std::vector<double> Splits;
		double Run = 0.0;           // metres ridden this lap
	};
	// The rider at (X, Z) at the clock's Now (seconds, any steady clock): returns a lap just finished, if
	// one was. Last is where the rider was the frame before (a line is crossed between the two).
	bool Step(const RidePlan& Plan, double X, double Z, double Now, Lap& Done);
	const State& Now() const { return S; }
	// The best counted lap of each circuit this ride (by its name), and the last lap.
	std::unordered_map<std::string, Lap> Best;
	Lap Last;
	bool HasLast = false;
	void Reset() { S = State(); HasLast = false; Prev = P2(); HavePrev = false; }
	// Put somewhere else (a respawn, the start): no line crossed on the way, the lap under way dropped.
	void Jumped() { S = State(); HavePrev = false; }

private:
	State S;
	P2 Prev;
	bool HavePrev = false;
	int GateWas = -1;
};

// Seconds as a lap time: "1:32.457".
std::string LapText(double Seconds);
} // namespace TorqueWorld
