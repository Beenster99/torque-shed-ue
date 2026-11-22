// Stopping and pulling away (beenster, build 167: "when my riders foot is down the bike shouldnt move" and
// "bikes should [not] pull away in 3rd gear or higher, they should stall").
//   1. Ridden to a stop and let go of the brakes (throttle shut), on the flat and on a 10% hill either way:
//      once the bike is stopped with the rider's feet down it stays where it is.
//   2. From a standstill, letting the clutch out with the throttle open: 1st and 2nd pull away; 3rd and
//      up stall the engine. Rolling along in 3rd it carries on; slowed to a crawl in top it stalls.
//
//     build/simtest/stop_test
#include "../Source/TorqueShedProto/Sim/BikeSim.h"

#include <cmath>
#include <cstdio>

using namespace TorqueSim;

namespace
{
int Failures = 0;
void Check(bool Ok, const char* What, double Value)
{
	std::printf("  %-60s %10.3f  %s\n", What, Value, Ok ? "ok" : "FAIL");
	if (!Ok) ++Failures;
}

constexpr double Dt = 1.0 / 120.0;
double Grade = 0.0;   // rise per metre along +x (the bike faces -z at heading 0, so it's turned to face +x)

RayHit SlopeRay(const V3& From, const V3& To)
{
	RayHit H;
	auto Over = [](const V3& P) { return P.y - Grade * P.x; };
	const double A = Over(From), B = Over(To);
	if (A < 0.0 || B > 0.0) return H;
	H.Hit = true;
	H.Position = From + (To - From) * (A / (A - B));
	H.Normal = V3(-Grade, 1.0, 0.0).Normalized();
	H.Surface = SURF_TARMAC;
	return H;
}

void Settle(BikeSim& S)
{
	S.Setup(BikeSpec::SpeedTwin1200());
	S.Ray = SlopeRay;
	S.Place(V3(0.0, 0.0, 0.0), -1.5708);   // facing +x, up the hill when Grade > 0
	for (int I = 0; I < 240; ++I) S.Step(Dt);
}

// Rides at about 6 m/s, then brakes to a stop and lets go of everything: how far it goes after it stops.
void StopAndHold(const char* Name, double G)
{
	Grade = G;
	BikeSim S;
	Settle(S);
	S.AutoShift = true;
	for (int I = 0; I < 120 * 10 && S.Speed < 6.0; ++I)
	{
		S.Throttle = 0.4;
		S.Step(Dt);
	}
	S.Throttle = 0.0;
	S.FrontBrake = 0.6;
	S.RearBrake = 0.3;
	int I = 0;
	for (; I < 120 * 10 && std::fabs(S.Speed) > 0.05; ++I) S.Step(Dt);
	S.FrontBrake = S.RearBrake = 0.0;
	const V3 At = S.Position;
	double Peak = 0.0;
	for (int J = 0; J < 120 * 8; ++J)
	{
		S.Step(Dt);
		Peak = std::max(Peak, std::fabs(S.Speed));
	}
	const double Moved = (S.Position - At).Length();
	char What[128];
	std::snprintf(What, sizeof What, "%s: stopped, brakes off 8 s: moved (cm)", Name);
	Check(Moved < 0.02 && !S.IsCrashed, What, Moved * 100.0);
	std::snprintf(What, sizeof What, "%s: fastest it crept (m/s)", Name);
	Check(Peak < 0.05, What, Peak);
}

// From a standstill in Gear, the clutch held in, revved, then let out: whether the engine keeps running.
bool PullAway(int Gear, double& Speed)
{
	Grade = 0.0;
	BikeSim S;
	Settle(S);
	S.AutoShift = false;
	S.Gear = Gear;
	S.ClutchIn = true;
	S.Throttle = 0.5;
	for (int I = 0; I < 60; ++I) S.Step(Dt);
	S.ClutchIn = false;
	for (int I = 0; I < 120 * 3; ++I) S.Step(Dt);
	Speed = S.Speed;
	return S.EngineOn;
}

// The same, but just put in gear and the throttle opened (no clutch: the game's own pull-away).
bool OpenUp(int Gear, double& Speed)
{
	Grade = 0.0;
	BikeSim S;
	Settle(S);
	S.AutoShift = false;
	S.Gear = Gear;
	S.Throttle = 0.6;
	for (int I = 0; I < 120 * 3; ++I) S.Step(Dt);
	Speed = S.Speed;
	return S.EngineOn;
}
} // namespace

int main()
{
	std::printf("Stopped with the feet down\n");
	StopAndHold("flat", 0.0);
	StopAndHold("facing up a 10% hill", 0.10);
	StopAndHold("facing down a 10% hill", -0.10);
	StopAndHold("facing up a 25% hill", 0.25);
	StopAndHold("facing down a 25% hill", -0.25);
	{
		// [168 robot] Stopped on a steep hill holding the front brake (as anyone does): it stays put. (It used
		// to start the rider walking it backwards; that now takes the stick pulled back as well.)
		Grade = 0.25;
		BikeSim S;
		Settle(S);
		S.FrontBrake = 1.0;
		for (int I = 0; I < 120; ++I) S.Step(Dt);
		const V3 At = S.Position;
		for (int I = 0; I < 120 * 12; ++I) S.Step(Dt);
		Check(!S.Paddling && (S.Position - At).Length() < 0.02, "front brake held 12 s on a 25% hill: stays put (moved, cm)", (S.Position - At).Length() * 100.0);
	}
	{
		// [168 robot] Rolling back down a steep hill (as on Buttertubs) with nothing held: the locked tyres
		// alone only slowed it to a creep of about 0.4 m/s, too quick for the feet to plant. They do now.
		Grade = 0.25;
		BikeSim S;
		Settle(S);
		S.LinearVelocity = S.LinearVelocity + V3(-1.0, 0.0, 0.0);
		for (int I = 0; I < 120 * 3; ++I) S.Step(Dt);
		const V3 At = S.Position;
		for (int I = 0; I < 120 * 12; ++I) S.Step(Dt);
		Check(S.FeetHeld && (S.Position - At).Length() < 0.02, "rolling back down a 25% hill: feet hold it (moved in 12 s, cm)", (S.Position - At).Length() * 100.0);
	}

	std::printf("Pulling away\n");
	for (int Gear = 1; Gear <= 5; ++Gear)
	{
		double V = 0.0;
		const bool Ran = PullAway(Gear, V);
		char What[128];
		std::snprintf(What, sizeof What, "gear %d, clutch let out: %s (speed m/s)", Gear, Gear <= 2 ? "pulls away" : "stalls");
		Check(Gear <= 2 ? (Ran && V > 3.0) : !Ran, What, V);
		const bool Ran2 = OpenUp(Gear, V);
		std::snprintf(What, sizeof What, "gear %d, throttle opened: %s (speed m/s)", Gear, Gear <= 2 ? "pulls away" : "stalls");
		Check(Gear <= 2 ? (Ran2 && V > 3.0) : !Ran2, What, V);
	}
	{
		// Rolling along in 3rd at 25 mph and opening it: no stall.
		Grade = 0.0;
		BikeSim S;
		Settle(S);
		S.AutoShift = true;
		S.Throttle = 0.5;
		for (int I = 0; I < 120 * 10 && S.Speed < 11.0; ++I) S.Step(Dt);
		S.AutoShift = false;
		S.Gear = 3;
		S.Throttle = 0.4;
		for (int I = 0; I < 120 * 3; ++I) S.Step(Dt);
		Check(S.EngineOn && S.Speed > 10.0, "rolling at 25 mph in 3rd and opened: runs on (m/s)", S.Speed);
		// Braked to a crawl in top without the clutch: it stalls.
		S.Gear = S.Build.GearCount();
		S.Throttle = 0.0;
		S.FrontBrake = 0.6;
		for (int I = 0; I < 120 * 6 && std::fabs(S.Speed) > 0.3; ++I) S.Step(Dt);
		for (int I = 0; I < 120; ++I) S.Step(Dt);
		Check(!S.EngineOn, "braked to a stop in top, no clutch: stalls (m/s)", S.Speed);
		// And it starts again in neutral.
		S.FrontBrake = 0.0;
		S.Gear = BikeSim::NEUTRAL;
		S.StartEngine(0.0);
		for (int I = 0; I < 120 * 3; ++I) S.Step(Dt);
		Check(S.EngineOn, "restarted in neutral", S.Rpm);
	}
	std::printf(Failures ? "FAIL: %d failed\n" : "all passed\n", Failures);
	return Failures ? 1 : 0;
}
