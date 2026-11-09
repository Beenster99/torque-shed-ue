// What a bay's bike makes, as Torque Shed's BikeBuild works it out (bike_build.gd _compute_engine,
// engine_torque, dyno_run): the engine block's size and torque per cc, its peak and redline, then
// every engine part's multiplier (head, fuel system, air filter, plugs, oil filter, exhaust, ECU)
// and how worn each is, the drivetrain's losses and the bike's mass from its parts (parts.json).
// The numbers are Torque Shed's, for the bay screens (power, torque, weight) and the dyno room's
// runs, and a catalogue bike's torque curve on the road (its riding spec is Fit/BikeRideSpec's).
//
// Not ported yet: tuning (map, ignition, limiter), fluids (petrol and oil levels and age) and
// engine heat against the cooling, which Torque Shed adds on top.
#pragma once

#include "CoreMinimal.h"

class UTorqueBuildSubsystem;

struct FTorqueEngineNumbers
{
	bool bValid = false;            // an engine is fitted
	float PeakTorque = 0.0f;        // N m at the crank, after every multiplier
	float PeakTorqueRpm = 4000.0f, Redline = 8000.0f, IdleRpm = 1000.0f;
	float Drivetrain = 0.92f;       // to the rear wheel
	float TorqueMult = 1.0f;
	float Mass = 0.0f;              // kg, the bike with its fluids
	int32 Gears = 6;
	TArray<FString> Notes;          // what's holding it back ("Air filter is clogged.")
};

// A rolling-road run (dyno_run): power and torque at the rear wheel from just off idle to the
// limiter, and the peaks. The host stores it on the bike with the time it started, so everyone
// plays the same run.
struct FTorqueDynoRun
{
	int32 N = 0;                    // the bike's run number
	double Start = 0.0;             // server world seconds the run started
	TArray<float> Rpm, Nm, Hp;
	float PeakHp = 0.0f, PeakHpRpm = 0.0f, PeakNm = 0.0f, PeakNmRpm = 0.0f;
	float Redline = 8000.0f, IdleRpm = 1000.0f;
	int32 Gears = 6;
	float Heat = 0.0f, TempC = 90.0f;
	bool bFan = false;
	float Mass = 0.0f;
};

namespace TorqueShedPower
{
FTorqueEngineNumbers Numbers(const UTorqueBuildSubsystem& B, int32 Bay);
float EngineTorque(const FTorqueEngineNumbers& E, float Rpm);
// The crank's peak power, hp (BikeBuild.hp).
float PeakHp(const FTorqueEngineNumbers& E, float* OutRpm = nullptr);
FTorqueDynoRun DynoRun(const FTorqueEngineNumbers& E, int32 Samples = 48);
// A hot engine fades over the sweep (shed_state _heat_run); Heat 0..1.
void HeatRun(FTorqueDynoRun& Run, float Heat);
float DynoTemp(float Heat);

// Playing a run back (dyno_room.gd): its length, and what the engine is doing T seconds in.
float RunTime(const FTorqueDynoRun& Run);
struct FSweep { float Rpm = 0.0f; int32 Gear = 0; float Throttle = 0.0f; float Progress = -1.0f; };
FSweep SweepState(const FTorqueDynoRun& Run, float T);

constexpr float FanHeat = 0.08f;      // how much hotter the engine gets in one run with the fan on
constexpr float NoFanHeat = 0.4f;     // and without it
}
