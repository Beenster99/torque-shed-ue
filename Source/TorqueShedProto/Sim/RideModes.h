// Ride modes, as a modern bike's dash has them: Wet, Urban, Touring, Sport, and Enduro or Race on
// the bikes they suit. Each is a template of settings (how much of the power and how sharply, how
// soon traction control, wheelie control and ABS step in, how hard the engine brakes, how the
// suspension is damped) that the rider picks from the saddle and can then alter a setting at a time.
//
// Touring is the bike as the dyno desk set it up: its levels are the desk's, so a bike ridden in
// Touring rides exactly as it did before there were modes.
//
// Plain C++ (SimTest/modes_test).
#pragma once

#include "BikeSpec.h"

#include <string>
#include <vector>

namespace TorqueSim
{
class BikeSim;

enum class RideMode { Wet, Urban, Touring, Sport, Enduro, Race, Count };

struct ModeSet
{
	int Power = 3;         // 1 low, 2 medium, 3 high
	int Response = 2;      // 1 soft, 2 smooth, 3 sharp
	int Traction = 3;      // 0 off .. 5 soonest
	int Lean = 3;          // lean-angle traction control, 0 .. 5 (kept in step with Traction on the screen)
	int Wheelie = 3;       // 0 off .. 5
	int Stability = 3;     // 0 off .. 5
	int Abs = 2;           // 0 off, 1 front only (the back can lock: off-road), 2 both wheels
	int EngineBrake = 2;   // 1 light, 2 as built, 3 strong
	int Susp = 1;          // 0 comfort, 1 road, 2 sport, 3 off-road
	bool operator==(const ModeSet& O) const
	{
		return Power == O.Power && Response == O.Response && Traction == O.Traction && Lean == O.Lean && Wheelie == O.Wheelie &&
			Stability == O.Stability && Abs == O.Abs && EngineBrake == O.EngineBrake && Susp == O.Susp;
	}
	bool operator!=(const ModeSet& O) const { return !(*this == O); }
};

// One of a mode's settings, as the screen lists and alters them.
enum class ModeRow { Power, Response, Traction, Abs, Wheelie, Stability, EngineBrake, Susp, Count };

const char* ModeName(RideMode M);            // "Touring"
const char* ModeKey(RideMode M);             // "touring" (saved under it)
const char* RowName(ModeRow R);              // "Traction control"
const char* RowHelp(ModeRow R);              // a line on what it does
// The modes this bike's dash offers, in the screen's order (none without a dash that has them).
std::vector<RideMode> ModesFor(const BikeSpec::AidCaps& Caps);
// A mode as it comes, on this bike.
ModeSet Template(RideMode M, const BikeSpec::AidCaps& Caps);
// Whether this bike has the setting at all, its lowest and highest value, and its value in a set.
bool RowOn(ModeRow R, const BikeSpec::AidCaps& Caps);
void RowRange(ModeRow R, int& Lo, int& Hi);
int RowValue(ModeRow R, const ModeSet& S);
void SetRow(ModeRow R, ModeSet& S, int Value);
// How a value reads: "High", "3", "Off", "Front only", "Comfort".
std::string RowText(ModeRow R, int Value);

// How firmly an aid steps in at a level (0 off .. 5), for a bike whose factory level is Firm.
double AidStrength(double Firm, int Level);

// Sets a spec (the bike as built: Base) to a mode. Only what a mode alters is written.
void ApplyMode(BikeSpec& Live, const BikeSpec& Base, const ModeSet& S);
// The same on a bike being ridden: its spec, and its wheels' damping and preload as they are now.
void ApplyMode(BikeSim& Sim, const BikeSpec& Base, const ModeSet& S);

// Cruise control: the throttle (0..1) that holds Target m/s, given the speed and what it gave last
// (call each frame; Held carries it).
double CruiseThrottle(double Speed, double Target, double& Held, double Dt);
} // namespace TorqueSim
