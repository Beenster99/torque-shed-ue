// The ridden motorbike: one rigid body with ray-cast wheels, the slip-based tyre model, an
// engine and gearbox, and a rider who balances it by steering.
//
// A port of the riding core of the Godot game's scripts/bike.gd (Bike), keeping its names, its
// order of work each tick and its comments' reasoning, so the two can be compared line by line.
// Left out for the prototype: the clutch lever, neutral, Direct bars, heat, weather, fuel,
// burnouts, feet down, towing, stability control and the ghost. The rigid body is integrated
// here (as Godot's Jolt body was) rather than by Chaos, so the riding is the same on any
// machine and can be tested without the engine; the world is seen only through ray casts.
#pragma once

#include "BikeContacts.h"
#include "BikeSpec.h"
#include <functional>
#include <string>
#include <vector>

namespace TorqueSim
{
struct RayHit
{
	bool Hit = false;
	V3 Position;
	V3 Normal{0, 1, 0};
	int Surface = SURF_GRASS;
};

// Casts a ray in the simulation's world frame: from, to. Set by the game (Unreal line traces) or a test.
using RayFn = std::function<RayHit(const V3& From, const V3& To)>;

// A cattle grid across the road: steel bars standing a little proud of the tarmac. The wheels ride
// up onto it (a jolt) and over the bars (a fine ripple), on less grip than tarmac.
struct CattleGrid
{
	V3 Centre;                  // on the road's centre line, sim frame
	V3 Along{0, 0, -1};         // the road's direction there, level
	double HalfLength = 1.15;   // along the road
	double HalfWidth = 2.65;    // across it
	double Pitch = 0.16;        // bar spacing
	double Proud = 0.025;       // how far the bar tops stand above the road
};

struct Wheel
{
	bool IsFront = false;
	V3 Mount;
	double Radius = 0.3, Travel = 0.12, Spring = 30000, Bump = 2000, Rebound = 3000, Inertia = 0.7;
	double Prog = 0.2, Preload = 0.008, Knee = 1.0;
	double Omega = 0.0, Spin = 0.0, Compression = 0.0, Load = 0.0, Raw = 0.0;
	bool Contact = false, HasContact = false;
	double DriveTorque = 0.0, BrakeTorque = 0.0, BrakeUsed = 0.0, Slip = 0.0;
	int Surf = SURF_TARMAC;
	double Kappa = 0.0, Alpha = 0.0, Sy = 0.0, Camber = 0.0, GripUsed = 0.0, Mu = 0.0, FSide = 0.0, REff = 0.3;
	double SlipShare = 1.0;
	bool Eased = false;
	V3 Force, FLat, LongDir{0, 0, -1}, ContactLocal, NormalLocal{0, 1, 0}, ContactPos, Normal{0, 1, 0};
	V3 Axle;
	V3 Centre;                  // the axle now, body space (for drawing the wheel)
	double Rx = 0.0, Ry = 0.0, FSusp = 0.0;
	bool Bottomed = false;
};

class BikeSim
{
public:
	static constexpr double G = 9.81;

	BikeSpec Build;
	RayFn Ray;
	std::vector<CattleGrid> Grids;
	// Walls, props and other riders' bikes (BikeContacts.h), set by the game; none, the bike only meets
	// the ground. The bike's probe spheres (frame, wheel bumpers, rider) are kept out of them.
	const ContactWorld* Contacts = nullptr;

	// Rider inputs.
	double Throttle = 0.0;
	double FrontBrake = 0.0, RearBrake = 0.0;
	double LeanInput = 0.0;     // -1 lean left .. 1 lean right
	double BodyShift = 0.0;     // -1 weight back .. 1 forward
	double Assist = 0.5;        // Riding realism: 0 full sim .. 1 easy
	bool AutoShift = true;

	// The engine and the clutch. Gear 0 is neutral (NEUTRAL). ClutchIn: the rider holding the lever
	// (the engine spins free of the box in any gear); Clutch how far it's in now, 0..1. EngineOn off
	// lets it roll with no drive and the revs fall to nothing. FuelLitres: the petrol in the tank
	// (the game keeps it; at 0 the engine can't start). EngineKw: the power it's making now, and
	// Overrun the throttle shut in gear above tickover (for the fuel it burns).
	static constexpr int NEUTRAL = 0;
	bool ClutchIn = false;
	double Clutch = 0.0;
	bool EngineOn = true;
	double FuelLitres = 15.0;
	double EngineKw = 0.0;
	bool Overrun = false;
	// 0..1, how hard the back tyre's spinning on the spot (a burnout).
	double Burnout = 0.0;

	// Rigid body state (world: +Y up). Position is the body origin (axle height, mid wheelbase).
	V3 Position;
	M3 Basis;
	V3 LinearVelocity, AngularVelocity;
	V3 CenterOfMass;            // body space

	// Read by the camera, sound and HUD.
	int Gear = 1;
	double Rpm = 1000.0;
	double Steer = 0.0, SteerVel = 0.0, BarTorque = 0.0;
	double Speed = 0.0, LeanAngle = 0.0, LeanRate = 0.0, PitchAngle = 0.0, RoadPitch = 0.0;
	double TrailNow = 0.1, TrailSeen = 0.1, RakeNow = 0.4, Dive = 0.0, TrailSprings = 0.1, Wobble = 0.0;
	bool IsCrashed = false;
	// The last knock (for sound, the HUD and the tests): how hard (m/s into what was hit), whether it
	// was another bike, and a count that goes up with each new one. KnockedOff: the last crash was a
	// hit (the rider struck something, or the bike went into something hard), not a fall.
	double LastHitSpeed = 0.0;
	bool LastHitBike = false, KnockedOff = false;
	int HitCount = 0;
	// How far the bike's lowest part (tyres, bars, pegs, covers...) is above the ground, m; negative
	// is into it. For tests and checks.
	double GroundGap();
	// How much of the throttle the rider's wrist or traction control lets through (1 all of it), and
	// the throttle as the wrist has rolled it: for the HUD's traction light and the tests.
	double TractionNow() const { return Traction; }
	double ThrottleNow() const { return ThrottleEff; }
	double CrashTimer = 0.0;
	Wheel Wheels[2];

	void Setup(const BikeSpec& Spec);
	// Upright on the ground at `Pos` (world), heading `Heading` radians (0 = -Z, + turning left as Godot's yaw).
	void Place(const V3& Pos, double Heading);
	void Step(double Delta);
	// The rider's foot on the lever (with AutoShift off): one gear up or down. False at the end of the box.
	// One click: up from first finds neutral at a walking pace or slower (as down from second does),
	// neutral goes to second (up) or first (down). False at the end of the box.
	bool ShiftUp();
	bool ShiftDown();
	// The key: the starter cranks for a moment (longer the harder it struggles, 0..1: worn plugs, a
	// tired engine, frost), then it fires. Says why it won't ("" when it's cranking). And off.
	std::string StartEngine(double Struggle = 0.0);
	void StopEngine();
	bool IsStarting() const { return Starting > 0.0; }

	V3 BodyToWorld(const V3& P) const { return Position + Basis * P; }
	// This bike as other riders' bikes meet it (ContactWorld::Capsules): the bike low, rear tyre to
	// front, and the rider from the saddle to the shoulders, moving with it. A fallen bike lies along
	// the ground, so the same capsules follow it down.
	void AsCapsules(std::vector<ContactCapsule>& Out) const;
	V3 WorldToBody(const V3& P) const { return Basis.Transposed() * (P - Position); }

private:
	// Accumulated for the step, as RigidBody3D.apply_force / apply_torque.
	V3 Force, Torque;
	void ApplyForce(const V3& F, const V3& Offset);   // offset from the body origin, world
	void ApplyTorque(const V3& T) { Torque += T; }
	void ApplyCentralForce(const V3& F) { Force += F; }
	void Integrate(double Delta);
	V3 InvInertiaWorld(const V3& T) const;

	void MeasureAttitude();
	void MeasureGeometry(double Delta);
	double TargetLean() const;
	double TightTurn(double V) const;
	double LockTurn(double V) const;
	double LeanForInput(double X) const;
	double GripLean() const;
	double TyreLean() const;
	double Crown() const;
	double BalanceLean(double Lean) const;
	double MaxSteer() const;
	void UpdateSteering(double Delta);
	double BarStrength(double V) const;
	static double Fast(double V);
	double FrontCarriedTorque(double Delta);
	double FrontSelfTorque();
	V3 FrontGyro() const;
	void ApplyWheelGyro();
	double FrontStiffness(double V, bool Live = true) const;
	void UpdateEngine(double Delta);
	double AutoUpRpm(double Load) const;
	double AutoFloorRpm(double Load) const;
	void Select(int NewGear);
	int GearForSpeed() const;
	double WheelieTarget() const;
	void WheelForces(Wheel& W, double Delta);
	static double SolveSpin(double Omega0, double VLong, double Vden, double R, double Kp, double Sy, double FPeak,
		double IDt, const TyreParams& Tp);
	double BrakeCare(bool IsFront) const;
	void ApplyAero();
	void ApplyRiderBalance(double Delta);
	void ApplyFeet();
	void CheckCrash();
	void StepCrashed(double Delta);   // off: falls, slides and tumbles on the ground
	struct Probe
	{
		V3 Local;
		double Radius = 0.2;
		bool Rider = false;
	};
	std::vector<Probe> Probes;
	std::vector<ContactHit> Hits;
	double SinceHit = 1.0;
	void ResolveContacts(double Delta);
	std::vector<V3> CrashPts;
	std::vector<RayHit> CrashGround;
	double Steadiness() const { return Lerp(0.45, 1.0, Assist); }

	V3 BaseCom;
	V3 SteerAxis{0, 1, 0}, SteerPivot, OffsetArm;
	double SteerAim = 0.0, SteerSlow = 0.0, LeanI = 0.0, Hang = 0.0, RateSlow = 0.0, RateQuick = 0.0, Trim = 0.0;
	V3 PrevW, PrevV;
	bool HavePrev = false;
	double Seen[4] = {0, 0, 0, 0};
	double Starting = 0.0;
	double ShiftTimer = 0.0, SinceShift = 0.0, Traction = 1.0, ShiftFromRpm = 0.0, ThrottleEff = 0.0, ShiftLoad = 0.0;
};
} // namespace TorqueSim
