#include "RideModes.h"

#include "BikeSim.h"

#include <algorithm>
#include <cmath>

namespace TorqueSim
{
namespace
{
// (BikeTune's TnAidScale: the factory level is 3.)
const double LevelScale[6] = {0.0, 0.4, 0.7, 1.0, 1.25, 1.5};
int Within(int V, int Lo, int Hi) { return std::max(Lo, std::min(Hi, V)); }
} // namespace

const char* ModeName(RideMode M)
{
	switch (M)
	{
	case RideMode::Wet: return "Wet";
	case RideMode::Urban: return "Urban";
	case RideMode::Sport: return "Sport";
	case RideMode::Enduro: return "Enduro";
	case RideMode::Race: return "Race";
	default: return "Touring";
	}
}

const char* ModeKey(RideMode M)
{
	switch (M)
	{
	case RideMode::Wet: return "wet";
	case RideMode::Urban: return "urban";
	case RideMode::Sport: return "sport";
	case RideMode::Enduro: return "enduro";
	case RideMode::Race: return "race";
	default: return "touring";
	}
}

const char* RowName(ModeRow R)
{
	switch (R)
	{
	case ModeRow::Power: return "Power";
	case ModeRow::Response: return "Throttle";
	case ModeRow::Traction: return "Traction control";
	case ModeRow::Abs: return "ABS";
	case ModeRow::Wheelie: return "Wheelie control";
	case ModeRow::Stability: return "Stability control";
	case ModeRow::EngineBrake: return "Engine braking";
	default: return "Suspension";
	}
}

const char* RowHelp(ModeRow R)
{
	switch (R)
	{
	case ModeRow::Power: return "How much of the engine's power you get at full throttle.";
	case ModeRow::Response: return "How sharply the engine answers the twistgrip. Soft is kind in the wet.";
	case ModeRow::Traction: return "Cuts the power when the back wheel spins. 5 steps in soonest, 1 lets it slide.";
	case ModeRow::Abs: return "Stops the wheels locking under the brakes. Front only lets the back lock, for loose ground.";
	case ModeRow::Wheelie: return "Holds the front wheel down. 5 keeps it on the road, 1 lets it lift.";
	case ModeRow::Stability: return "Calms a weave or a slide. 5 is firmest.";
	case ModeRow::EngineBrake: return "How hard the engine slows the bike when you shut the throttle.";
	default: return "How the forks and shock are damped: soft for comfort, firm for sport.";
	}
}

std::vector<RideMode> ModesFor(const BikeSpec::AidCaps& Caps)
{
	std::vector<RideMode> Out;
	if (!Caps.Modes) return Out;
	Out = {RideMode::Wet, RideMode::Urban, RideMode::Touring, RideMode::Sport};
	if (Caps.Enduro) Out.push_back(RideMode::Enduro);
	if (Caps.Race) Out.push_back(RideMode::Race);
	return Out;
}

ModeSet Template(RideMode M, const BikeSpec::AidCaps& Caps)
{
	// Touring: the desk's levels, everything else as built.
	ModeSet T;
	T.Abs = Caps.Desk[0] > 0 ? 2 : 0;
	T.Traction = Within(Caps.Desk[1], 0, 5);
	T.Lean = Within(Caps.Desk[2], 0, 5);
	T.Wheelie = Within(Caps.Desk[3], 0, 5);
	T.Stability = Within(Caps.Desk[4], 0, 5);
	ModeSet S = T;
	switch (M)
	{
	case RideMode::Wet:
		S.Power = 1; S.Response = 1; S.Traction = 5; S.Lean = 5; S.Wheelie = 5; S.Stability = 5; S.Abs = 2; S.EngineBrake = 1; S.Susp = 0;
		break;
	case RideMode::Urban:
		S.Power = 2; S.Response = 1; S.Traction = 4; S.Lean = 4; S.Wheelie = 4; S.Stability = 4; S.Abs = 2; S.EngineBrake = 2; S.Susp = 0;
		break;
	case RideMode::Sport:
		S.Power = 3; S.Response = 3; S.Traction = 2; S.Lean = 2; S.Wheelie = 2; S.Stability = 3; S.Abs = 2; S.EngineBrake = 2; S.Susp = 2;
		break;
	case RideMode::Enduro:
		S.Power = 2; S.Response = 1; S.Traction = 1; S.Lean = 1; S.Wheelie = 1; S.Stability = 1; S.Abs = 1; S.EngineBrake = 1; S.Susp = 3;
		break;
	case RideMode::Race:
		S.Power = 3; S.Response = 3; S.Traction = 1; S.Lean = 1; S.Wheelie = 1; S.Stability = 2; S.Abs = 2; S.EngineBrake = 3; S.Susp = 2;
		break;
	default:
		break;
	}
	return S;
}

bool RowOn(ModeRow R, const BikeSpec::AidCaps& Caps)
{
	switch (R)
	{
	case ModeRow::Power:
	case ModeRow::Response:
	case ModeRow::EngineBrake: return Caps.Modes;
	case ModeRow::Traction: return Caps.Traction;
	case ModeRow::Abs: return Caps.Abs;
	case ModeRow::Wheelie: return Caps.Wheelie;
	case ModeRow::Stability: return Caps.Stability;
	default: return Caps.SemiActive;
	}
}

void RowRange(ModeRow R, int& Lo, int& Hi)
{
	switch (R)
	{
	case ModeRow::Power:
	case ModeRow::Response:
	case ModeRow::EngineBrake: Lo = 1; Hi = 3; break;
	case ModeRow::Abs: Lo = 0; Hi = 2; break;
	case ModeRow::Susp: Lo = 0; Hi = 3; break;
	default: Lo = 0; Hi = 5; break;
	}
}

int RowValue(ModeRow R, const ModeSet& S)
{
	switch (R)
	{
	case ModeRow::Power: return S.Power;
	case ModeRow::Response: return S.Response;
	case ModeRow::Traction: return S.Traction;
	case ModeRow::Abs: return S.Abs;
	case ModeRow::Wheelie: return S.Wheelie;
	case ModeRow::Stability: return S.Stability;
	case ModeRow::EngineBrake: return S.EngineBrake;
	default: return S.Susp;
	}
}

void SetRow(ModeRow R, ModeSet& S, int Value)
{
	int Lo = 0, Hi = 0;
	RowRange(R, Lo, Hi);
	const int V = Within(Value, Lo, Hi);
	switch (R)
	{
	case ModeRow::Power: S.Power = V; break;
	case ModeRow::Response: S.Response = V; break;
	case ModeRow::Traction:
		// (the lean-angle side of it goes with it, keeping what the desk set them apart by)
		S.Lean = Within(S.Lean + V - S.Traction, V > 0 ? 1 : 0, 5);
		if (V == 0) S.Lean = 0;
		S.Traction = V;
		break;
	case ModeRow::Abs: S.Abs = V; break;
	case ModeRow::Wheelie: S.Wheelie = V; break;
	case ModeRow::Stability: S.Stability = V; break;
	case ModeRow::EngineBrake: S.EngineBrake = V; break;
	default: S.Susp = V; break;
	}
}

std::string RowText(ModeRow R, int Value)
{
	switch (R)
	{
	case ModeRow::Power: return Value <= 1 ? "Low" : Value == 2 ? "Medium" : "High";
	case ModeRow::Response: return Value <= 1 ? "Soft" : Value == 2 ? "Smooth" : "Sharp";
	case ModeRow::EngineBrake: return Value <= 1 ? "Light" : Value == 2 ? "Normal" : "Strong";
	case ModeRow::Abs: return Value <= 0 ? "Off" : Value == 1 ? "Front only" : "On";
	case ModeRow::Susp: return Value <= 0 ? "Comfort" : Value == 1 ? "Road" : Value == 2 ? "Sport" : "Off-road";
	default: return Value <= 0 ? std::string("Off") : std::to_string(Value);
	}
}

double AidStrength(double Firm, int Level)
{
	return std::max(0.0, std::min(1.2, Firm * LevelScale[Within(Level, 0, 5)]));
}

namespace
{
// The damping and the preload a suspension setting makes of the bike's own (bump, rebound, preload added in metres).
void SuspOf(int Susp, double& Bump, double& Rebound, double& Preload)
{
	switch (Susp)
	{
	case 0: Bump = 0.8; Rebound = 0.85; Preload = 0.0; break;
	case 2: Bump = 1.25; Rebound = 1.3; Preload = 0.002; break;
	case 3: Bump = 0.9; Rebound = 1.1; Preload = 0.004; break;
	default: Bump = 1.0; Rebound = 1.0; Preload = 0.0; break;
	}
}
} // namespace

void ApplyMode(BikeSpec& Live, const BikeSpec& Base, const ModeSet& S)
{
	const BikeSpec::AidCaps& C = Base.Aids;
	if (!C.Modes) return;
	Live.PowerMode = S.Power <= 1 ? 0.6 : S.Power == 2 ? 0.8 : 1.0;
	// (smooth is the twistgrip as built; sharp gives more for the first of it)
	Live.ThrottleSoft = S.Response <= 1 ? 0.6 : S.Response == 2 ? 0.0 : -0.2;
	Live.EngineBrake = S.EngineBrake <= 1 ? 0.55 : S.EngineBrake == 2 ? 1.0 : 1.45;
	Live.TractionStrength = C.Traction ? AidStrength(C.Firm, S.Traction) : 0.0;
	Live.TractionControl = Live.TractionStrength > 0.0;
	Live.LeanTraction = C.Lean ? AidStrength(C.Firm, S.Lean) : 0.0;
	Live.AntiWheelie = C.Wheelie ? AidStrength(C.Firm, S.Wheelie) : 0.0;
	Live.Stability = C.Stability ? AidStrength(C.Firm, S.Stability) : 0.0;
	Live.AbsFront = C.Abs && S.Abs >= 1;
	Live.AbsRear = C.Abs && S.Abs >= 2;
	double Bump = 1.0, Rebound = 1.0, Preload = 0.0;
	if (C.SemiActive) SuspOf(S.Susp, Bump, Rebound, Preload);
	Live.FrontBump = Base.FrontBump * Bump;
	Live.RearBump = Base.RearBump * Bump;
	Live.FrontRebound = Base.FrontRebound * Rebound;
	Live.RearRebound = Base.RearRebound * Rebound;
	Live.FrontPreload = Base.FrontPreload + Preload;
	Live.RearPreload = Base.RearPreload + Preload;
}

void ApplyMode(BikeSim& Sim, const BikeSpec& Base, const ModeSet& S)
{
	if (!Base.Aids.Modes) return;
	ApplyMode(Sim.Build, Base, S);
	for (int I = 0; I < 2; ++I)
	{
		const bool Front = Sim.Wheels[I].IsFront;
		Sim.Wheels[I].Bump = Front ? Sim.Build.FrontBump : Sim.Build.RearBump;
		Sim.Wheels[I].Rebound = Front ? Sim.Build.FrontRebound : Sim.Build.RearRebound;
		Sim.Wheels[I].Preload = Front ? Sim.Build.FrontPreload : Sim.Build.RearPreload;
	}
}

double CruiseThrottle(double Speed, double Target, double& Held, double Dt)
{
	// What it held, nudged by how far off the speed is: gentle, as a real one is.
	const double Error = Target - Speed;
	Held = std::max(0.0, std::min(1.0, Held + Error * 0.12 * Dt));
	return std::max(0.0, std::min(1.0, Held + Error * 0.25));
}
} // namespace TorqueSim
