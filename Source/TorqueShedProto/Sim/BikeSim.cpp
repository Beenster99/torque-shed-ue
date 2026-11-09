// Port of the riding core of the Godot game's scripts/bike.gd. Function names follow the GDScript
// ones (_update_steering is UpdateSteering, and so on); the comments carry over the reasons.
#include "BikeSim.h"

#include <vector>

namespace TorqueSim
{
namespace
{
const double MAX_LEAN = DegToRad(52.0);
const double ROAD_LEAN = DegToRad(30.0);
const double ROAD_INPUT = 0.75;
const double LONG_SLIP_SPEED = 3.0;
const double LAT_SLIP_SPEED = 2.5;
const double RELAX_SPEED = 4.0;
const double STEER_LOCK = DegToRad(38.0);
const double TIGHT_TURN = 2.4;
const double TIGHT_TURN_OPEN = 0.065;
const double TIGHT_SPEED = 4.4704;
const double LOCK_SHARE = 0.97;
const double HEAD_DAMPING = 1.2;
const double ARM_DAMPING = 2.5;
const double ARM_DAMPING_SLOW = 12.0;
const double ARM_TRIM = 0.12;
const double ARM_INERTIA = 0.35;
const double ARM_HOLD = 120.0;
const double ARM_HOLD_SLOW = 500.0;
const double MAX_BAR_TORQUE = 45.0;
const double BAR_PUSH_FAST = 1.0;
const double BAR_PUSH_FROM = 15.0;
const double BAR_PUSH_FULL = 40.0;
const double TYRE_RATE = 180000.0;
const double PNEUMATIC_TRAIL = 0.022;
const double BODY_SWAY = 0.5;
const double RIDER_FEEL = 0.035;
const double LEAN_RATE_GAIN = 3.0;
const double LEAN_RATE_MAX = 1.0;
const double LEAN_CATCH = 2.0;
const double STEER_CATCH = 30.0;
const double STEER_CATCH_FAST = 0.8;
// The share of the tyres' grip a rider leans on in a steady turn (the rest kept in hand). Godot's 0.8
// left road bikes short of the sideways g road tests get (a naked or sports bike 1.04 g against
// 1.1-1.2); 0.86 brings every style into its real range (SimTest/realism_test).
const double GRIP_IN_HAND = 0.86;
const double BALANCE_FROM = 0.35;
const double BALANCE_RATE_GAIN = 5.5;
const double BALANCE_RATE_MAX = 1.5;
const double BALANCE_CATCH = 3.0;
const double HANG_OFF = 0.12;
const double CRASH_LEAN = DegToRad(78.0);
const double CRASH_PITCH = DegToRad(72.0);
const double WHEELIE_PITCH = DegToRad(22.0);
const double STOPPIE_PITCH = DegToRad(14.0);
const double STOPPIE_SKIM = DegToRad(4.0);
const double RIDER_SPIN_FEEL = 1.5;
const double CARELESS_SLIP = 0.9;
// How near the tyre's peak slip a hard stop works (the rider, or ABS). Godot's 0.55 stopped every road
// bike 1-3 m shorter from 60 mph than road tests do (a naked bike 32.7 m against 34-37); 0.45 puts
// them in range (SimTest/realism_test).
const double BRAKE_KEEP = 0.45;
const double AIR_DENSITY = 1.2;
const double AIR_REV_PITCH = 7.0;
} // namespace

namespace
{
// Where the fallen bike touches the ground, body space (x right, y up, z back; the origin is at axle
// height midway between the wheels): the tyres' edges, bar ends, mirrors, pegs, engine covers,
// silencers, headlight, tank and tail.
// The first CRASH_TYRE_POINTS are the tyres' treads.
const size_t CRASH_TYRE_POINTS = 32;
std::vector<V3> CrashPoints(const BikeSpec& B)
{
	std::vector<V3> P;
	const double Half = B.Wheelbase * 0.5;
	for (int Front = 0; Front < 2; ++Front)
	{
		const double Z = Front ? -Half : Half, R = Front ? B.FrontRadius : B.RearRadius;
		for (int A = 0; A < 8; ++A)
		{
			const double T = A * TAU / 8.0;
			for (double X : {-0.07, 0.07}) P.push_back(V3(X, R * std::sin(T), Z + R * std::cos(T)));
		}
	}
	for (double S : {-1.0, 1.0})
	{
		P.push_back(V3(0.40 * S, 0.62, -0.42));    // bar ends
		P.push_back(V3(0.33 * S, 0.85, -0.38));    // mirrors
		P.push_back(V3(0.20 * S, 0.02, 0.10));     // footpegs
		P.push_back(V3(0.22 * S, 0.12, -0.18));    // engine covers
		P.push_back(V3(0.20 * S, 0.10, 0.55));     // silencers
		P.push_back(V3(0.16 * S, 0.50, -0.10));    // tank sides
		P.push_back(V3(0.14 * S, 0.45, 0.45));     // seat and tail sides
	}
	P.push_back(V3(0.0, 0.48, -0.78));             // headlight
	P.push_back(V3(0.0, 0.62, -0.15));             // tank top
	P.push_back(V3(0.0, 0.55, 0.50));              // seat
	return P;
}
} // namespace

void BikeSim::Setup(const BikeSpec& Spec)
{
	Build = Spec;
	BaseCom = V3(0.0, Build.ComHeight - Build.RearRadius, 0.06 + Build.ComOffset.z);
	CenterOfMass = BaseCom;
	const double Rake = DegToRad(Build.RakeDeg);
	SteerAxis = V3(0.0, std::cos(Rake), std::sin(Rake));
	const V3 Ahead(0.0, std::sin(Rake), -std::cos(Rake));
	OffsetArm = Ahead * Build.ForkOffset;
	SteerPivot = V3(0.0, 0.0, -Build.Wheelbase * 0.5) - OffsetArm;
	TrailNow = TrailSeen = TrailSprings = Build.Trail;
	RakeNow = Rake;

	auto Make = [&](bool Front) {
		Wheel W;
		W.IsFront = Front;
		W.Radius = Front ? Build.FrontRadius : Build.RearRadius;
		W.Travel = Front ? Build.FrontTravel : Build.RearTravel;
		W.Spring = Front ? Build.FrontSpring : Build.RearSpring;
		W.Bump = Front ? Build.FrontBump : Build.RearBump;
		W.Rebound = Front ? Build.FrontRebound : Build.RearRebound;
		W.Inertia = Front ? Build.FrontInertia : Build.RearInertia;
		W.Prog = Front ? Build.FrontProgression : Build.RearProgression;
		W.Preload = Front ? Build.FrontPreload : Build.RearPreload;
		W.Knee = Front ? Build.FrontKnee : Build.RearKnee;
		W.Mount = V3(0.0, W.Travel, (Front ? -0.5 : 0.5) * Build.Wheelbase);
		W.Axle = V3(0.0, W.Mount.y - W.Travel, W.Mount.z);
		W.Centre = W.Axle;
		W.REff = W.Radius;
		return W;
	};
	Wheels[0] = Make(true);
	Wheels[1] = Make(false);
	Gear = 1;
	Rpm = Build.IdleRpm;

	// The probe spheres, body space (origin at axle height midway between the wheels, -z ahead), as
	// the Godot bike's collision shapes: the frame and engine low along the wheelbase, bumpers ahead
	// of the front tyre and behind the back one, the bars' ends, and the rider (hips, chest, head).
	Probes.clear();
	const double Half = Build.Wheelbase * 0.5;
	const double Seat = Build.SeatHeight - Build.RearRadius;   // the saddle above the axles
	for (const double Z : {-0.55, 0.0, 0.55}) Probes.push_back({V3(0.0, 0.12, Z * Half), 0.24, false});
	Probes.push_back({V3(0.0, 0.0, -Half - Build.FrontRadius * 0.8), 0.15, false});
	Probes.push_back({V3(0.0, 0.0, Half + Build.RearRadius * 0.8), 0.15, false});
	const double BarHalf = Build.BarWidthMm * 0.0005;
	for (const double X : {-BarHalf, BarHalf}) Probes.push_back({V3(X, Seat + 0.2, -Half + 0.3), 0.07, false});
	Probes.push_back({V3(0.0, Seat + 0.15, 0.1), 0.2, true});
	Probes.push_back({V3(0.0, Seat + 0.5, -0.1), 0.2, true});
	Probes.push_back({V3(0.0, Seat + 0.85, -0.2), 0.15, true});
}

void BikeSim::Place(const V3& Pos, double Heading)
{
	IsCrashed = false;
	KnockedOff = false;
	CrashTimer = 0.0;
	Basis = M3::AxisAngle(UP, Heading);
	Position = Pos + UP * (Build.RearRadius + 0.05);
	LinearVelocity = AngularVelocity = V3();
	Steer = SteerVel = BarTorque = Wobble = 0.0;
	SteerAim = SteerSlow = 0.0;
	HavePrev = false;
	RoadPitch = 0.0;
	Trim = LeanI = Hang = 0.0;
	for (double& S : Seen) S = 0.0;
	Gear = 1;
	Rpm = Build.IdleRpm;
	ThrottleEff = 0.0;
	Traction = 1.0;
	for (Wheel& W : Wheels)
	{
		W.Omega = W.Compression = W.Sy = W.Rx = W.Ry = 0.0;
		W.Contact = W.HasContact = false;
	}
}

// --- the rigid body (what Jolt did for the Godot body) -------------------------------------------

void BikeSim::ApplyForce(const V3& F, const V3& Offset)
{
	Force += F;
	Torque += (Offset - Basis * CenterOfMass).Cross(F);
}

V3 BikeSim::InvInertiaWorld(const V3& T) const
{
	const V3 L = Basis.Transposed() * T;
	const V3 I = Build.Inertia;
	return Basis * V3(L.x / I.x, L.y / I.y, L.z / I.z);
}

void BikeSim::Integrate(double Delta)
{
	// Semi-implicit Euler about the centre of mass, the way the physics engine stepped the body.
	const double M = Build.TotalMass;
	LinearVelocity += (Force / M - UP * G) * Delta;
	AngularVelocity += InvInertiaWorld(Torque) * Delta;
	AngularVelocity = AngularVelocity * (1.0 - 0.05 * Delta);   // Bike.setup's angular_damp
	const V3 ComWorld = Position + Basis * CenterOfMass;
	const V3 NewCom = ComWorld + LinearVelocity * Delta;
	const double W = AngularVelocity.Length();
	if (W > 1e-9) Basis = M3::AxisAngle(AngularVelocity / W, W * Delta) * Basis;
	Basis.Orthonormalize();
	Position = NewCom - Basis * CenterOfMass;
	Force = Torque = V3();
}

// --- each tick -------------------------------------------------------------------------------------

void BikeSim::Step(double Delta)
{
	const V3 Fwd = -Basis.Z;
	Speed = LinearVelocity.Dot(Fwd);
	MeasureAttitude();
	if (IsCrashed)
	{
		Throttle = 0.0;
		CrashTimer += Delta;
		// The ground under each point is looked up once a tick (a ray each); the steps between use
		// it as a plane.
		if (CrashPts.empty()) CrashPts = CrashPoints(Build);
		CrashGround.resize(CrashPts.size());
		for (size_t I = 0; I < CrashPts.size(); ++I)
		{
			const V3 P = Position + Basis * CrashPts[I];
			CrashGround[I] = Ray ? Ray(P + UP * 2.0, P - UP * 1.5) : RayHit();
		}
		const int Sub = std::max(1, (int)std::ceil(Delta * 480.0));
		for (int I = 0; I < Sub; ++I) StepCrashed(Delta / Sub);
		ResolveContacts(Delta);
		return;
	}

	const double Shift = BodyShift * 0.13;
	const double HangTo = LeanInput * HANG_OFF * Clamp((std::fabs(Speed) - 3.0) / 5.0, 0.0, 1.0);
	Hang = MoveToward(Hang, HangTo, Delta * 0.4);
	CenterOfMass = BaseCom + V3(Hang * Build.RiderMass / Build.TotalMass, 0.0, -Shift);

	UpdateSteering(Delta);
	UpdateEngine(Delta);
	// Stopped with the throttle shut, the rider puts a foot down and holds the brakes (Torque Shed's
	// feet down, reduced to this), so the bike neither rolls back down a hill nor topples.
	const bool Parked = std::fabs(Speed) < 1.0 && Throttle < 0.05;
	double RearGrip = Parked ? std::max(RearBrake, 1.0) : RearBrake;
	if (!Wheels[0].Contact && Wheels[1].Contact && PitchAngle > DegToRad(1.5))
	{
		const double Over = PitchAngle - WheelieTarget();
		const double Rising = AngularVelocity.Dot(Basis.X);
		const double Ahead = Over + std::max(Rising, 0.0) * 0.45;   // where it'll be in a moment
		if (Ahead > 0.0) RearGrip = std::max(RearGrip, Clamp(Ahead * 4.0, 0.0, 1.0) * Steadiness());
	}
	double FrontGrip = Parked ? std::max(FrontBrake, 0.8) : FrontBrake;
	if (!Wheels[1].Contact && Wheels[0].Contact && PitchAngle < -DegToRad(3.0))
	{
		const double Over = -PitchAngle - (BodyShift >= 0.25 ? STOPPIE_PITCH : STOPPIE_SKIM);
		const double Falling = -AngularVelocity.Dot(Basis.X);
		FrontGrip *= Clamp(1.0 - (Over * 5.0 + Falling * 0.8) * Steadiness(), 0.0, 1.0);
	}
	Wheels[0].BrakeTorque = FrontGrip * Build.FrontBrakeTorque;
	Wheels[1].BrakeTorque = RearGrip * Build.RearBrakeTorque;
	for (Wheel& W : Wheels) WheelForces(W, Delta);
	MeasureGeometry(Delta);
	ApplyWheelGyro();
	ApplyAero();
	ApplyRiderBalance(Delta);
	ApplyFeet();
	CheckCrash();
	Integrate(Delta);
	ResolveContacts(Delta);
}

// --- walls, props and other bikes ----------------------------------------------------------------

namespace
{
const double HIT_BOUNCE = 0.1;        // walls give back little
const double HIT_BOUNCE_BIKE = 0.3;   // bikes a little more
const double HIT_FRICTION = 0.6;      // scraping along stone or another bike
// The rider comes off when they strike something faster than this (m/s), as Torque Shed's rider
// shape did (bike.gd _on_body_shape_entered: over 4 m/s).
const double RIDER_KNOCK = 4.0;
// And when the bike itself goes into something hard faster than this (m/s into it): a rider isn't
// still sat on after a 13 mph wall. (Godot's Jolt body pitched them over instead, the rider shape
// then striking the wall.)
const double BIKE_KNOCK = 6.0;
} // namespace

void BikeSim::AsCapsules(std::vector<ContactCapsule>& Out) const
{
	const double Half = Build.Wheelbase * 0.5;
	const double Seat = Build.SeatHeight - Build.RearRadius;
	ContactCapsule Low;
	Low.A = BodyToWorld(V3(0.0, 0.1, Half + Build.RearRadius * 0.6));
	Low.B = BodyToWorld(V3(0.0, 0.1, -Half - Build.FrontRadius * 0.6));
	Low.Radius = 0.3;
	Low.Velocity = LinearVelocity;
	Out.push_back(Low);
	if (IsCrashed) return;   // the rider is off and down beside it
	ContactCapsule Up;
	Up.A = BodyToWorld(V3(0.0, Seat + 0.2, 0.05));
	Up.B = BodyToWorld(V3(0.0, Seat + 0.8, -0.15));
	Up.Radius = 0.22;
	Up.Velocity = LinearVelocity;
	Up.Rider = true;
	Out.push_back(Up);
}

void BikeSim::ResolveContacts(double Delta)
{
	SinceHit += Delta;
	if (!Contacts) return;
	const double M = Build.TotalMass;
	double Hardest = 0.0;
	bool HardestBike = false;
	for (const Probe& Pr : Probes)
	{
		if (Pr.Rider && IsCrashed) continue;   // thrown off: the rider's no longer on it
		const V3 P = Position + Basis * Pr.Local;
		Hits.clear();
		Contacts->Query(P, Pr.Radius, Hits);
		for (const ContactHit& H : Hits)
		{
			// Out of it, and no further in: an impulse at the probe stops the motion into what was hit
			// (bouncing back a little) and drags along it, turning the bike as well where it struck.
			Position += H.Normal * H.Depth;
			const V3 Com = Position + Basis * CenterOfMass;
			const V3 R = (Position + Basis * Pr.Local) - Com;
			const V3 V = LinearVelocity + AngularVelocity.Cross(R) - H.OtherVelocity;
			const double Vn = V.Dot(H.Normal);
			if (Vn >= 0.0) continue;
			const V3 Rn = R.Cross(H.Normal);
			const double Kn = 1.0 / M + H.Normal.Dot(InvInertiaWorld(Rn).Cross(R));
			const double J = -(1.0 + (H.Bike ? HIT_BOUNCE_BIKE : HIT_BOUNCE)) * Vn / std::max(Kn, 1e-9);
			V3 Impulse = H.Normal * J;
			const V3 Vt = V - H.Normal * Vn;
			const double Slide = Vt.Length();
			if (Slide > 1e-4)
			{
				const V3 T = Vt / Slide;
				const V3 Rt = R.Cross(T);
				const double Kt = 1.0 / M + T.Dot(InvInertiaWorld(Rt).Cross(R));
				Impulse -= T * std::min(Slide / std::max(Kt, 1e-9), HIT_FRICTION * J);
			}
			LinearVelocity += Impulse / M;
			AngularVelocity += InvInertiaWorld(R.Cross(Impulse));
			const double Into = -Vn;
			if (Into > Hardest) { Hardest = Into; HardestBike = H.Bike; }
			if (!IsCrashed && ((Pr.Rider && Into > RIDER_KNOCK) || (!Pr.Rider && Into > BIKE_KNOCK)))
			{
				IsCrashed = true;
				CrashTimer = 0.0;
				KnockedOff = true;
			}
		}
	}
	if (Hardest > 0.5)
	{
		// A new knock (not the same scrape going on): for the sound and the HUD.
		if (SinceHit > 0.25 || Hardest > LastHitSpeed * 1.5) ++HitCount;
		LastHitSpeed = Hardest;
		LastHitBike = HardestBike;
		SinceHit = 0.0;
	}
}

// --- off: the bike as a tumbling body on the ground ----------------------------------------------


double BikeSim::GroundGap()
{
	if (CrashPts.empty()) CrashPts = CrashPoints(Build);
	double Gap = 1e9;
	for (const V3& Local : CrashPts)
	{
		const V3 P = Position + Basis * Local;
		const RayHit Hit = Ray ? Ray(P + UP * 2.0, P - UP * 3.0) : RayHit();
		if (Hit.Hit) Gap = std::min(Gap, (P - Hit.Position).Dot(Hit.Normal));
	}
	return Gap;
}

void BikeSim::StepCrashed(double Delta)
{
	// Gravity, and each touching point pushed out of the ground with a spring and damper and dragged
	// back by sliding friction: the bike falls, scrapes along on its side, tumbles and comes to rest.
	static const double MU_SLIDE = 0.5;
	static const double SKIN = 0.02;   // how far a point may sink before the body is lifted out
	const double M = Build.TotalMass;
	const double K = M * G / 0.03;     // three points carry the weight at 1 cm deep
	const double C = 2.0 * std::sqrt(K * M / 3.0);
	const V3 Com = Position + Basis * CenterOfMass;
	double Lift = 0.0;
	V3 LiftDir = UP;
	bool Touching = false;
	for (size_t I = 0; I < CrashPts.size() && I < CrashGround.size(); ++I)
	{
		const V3& Local = CrashPts[I];
		const V3 P = Position + Basis * Local;
		const RayHit& Hit = CrashGround[I];
		if (!Hit.Hit) continue;
		const V3 N = Hit.Normal;
		const double Depth = (Hit.Position - P).Dot(N);
		if (Depth <= 0.0) continue;
		Touching = true;
		const V3 R = P - Com;
		const V3 V = LinearVelocity + AngularVelocity.Cross(R);
		const double Vn = V.Dot(N);
		const double Fn = std::max(K * std::min(Depth, 0.1) - C * Vn, 0.0);
		V3 Vt = V - N * Vn;
		// A tyre still on its tread rolls: little drag along the bike, the full scrape across it.
		const bool Tread = I < CRASH_TYRE_POINTS && std::fabs(N.Dot(Basis.X)) < 0.5;
		double Mu = MU_SLIDE;
		if (Tread)
		{
			const V3 Along = (Basis.Z - N * Basis.Z.Dot(N)).Normalized();
			const double Roll = Vt.Dot(Along);
			Vt -= Along * (Roll * 0.95);
		}
		const double Slide = Vt.Length();
		const V3 Ft = Vt * (-Mu * Fn / std::max(Slide, 0.2));
		ApplyForce(N * Fn + Ft, P - Position);
		if (Depth - SKIN > Lift) { Lift = Depth - SKIN; LiftDir = N; }
	}
	Integrate(Delta);
	if (Lift > 0.0)
	{
		// Never left inside the ground: lift the body out and stop it moving further in.
		Position += LiftDir * Lift;
		const double In = LinearVelocity.Dot(LiftDir);
		if (In < 0.0) LinearVelocity -= LiftDir * In;
	}
	if (Touching)
	{
		AngularVelocity = AngularVelocity * std::exp(-1.5 * Delta);
		if (LinearVelocity.Length() < 0.08 && AngularVelocity.Length() < 0.08)
		{
			LinearVelocity = LinearVelocity * 0.5;
			AngularVelocity = AngularVelocity * 0.5;
		}
	}
}

void BikeSim::MeasureAttitude()
{
	const V3 Up = Basis.Y;
	const V3 Fwd = -Basis.Z;
	V3 FwdFlat(Fwd.x, 0.0, Fwd.z);
	if (FwdFlat.Length() < 0.01) FwdFlat = V3(Basis.Y.x, 0.0, Basis.Y.z);
	FwdFlat = FwdFlat.Normalized();
	const V3 RightFlat = FwdFlat.Cross(UP);
	LeanAngle = std::atan2(Up.Dot(RightFlat), Up.Dot(UP));
	LeanRate = AngularVelocity.Dot(FwdFlat);
	PitchAngle = std::asin(Clamp(Fwd.y, -1.0, 1.0));
	if (Wheels[0].Contact && Wheels[1].Contact && Wheels[0].HasContact && Wheels[1].HasContact)
	{
		const V3 D = Wheels[0].ContactPos - Wheels[1].ContactPos;
		RoadPitch = std::atan2(D.y, std::max(D.Dot(FwdFlat), 0.1));
	}
}

void BikeSim::MeasureGeometry(double Delta)
{
	const double Rake = DegToRad(Build.RakeDeg);
	const Wheel& F = Wheels[0];
	const double RiseF = F.Compression * Build.ForkPath.y;
	const double RiseR = Build.RearAxleAt(Wheels[1].Compression).y - Build.RearAxleAt(0.0).y;
	Dive = std::atan2(RiseF - RiseR, Build.Wheelbase);
	RakeNow = Rake - Dive;
	const double Offset = Build.ForkOffset + F.Compression * Build.ForkPath.Dot(V3(0.0, std::sin(Rake), -std::cos(Rake)));
	TrailSprings = (Build.FrontRadius * std::sin(RakeNow) - Offset) / std::cos(RakeNow);
	TrailSeen = Lerp(TrailSeen, TrailNow, 1.0 - std::exp(-Delta / 0.04));
}

double BikeSim::TargetLean() const
{
	const double V = std::fabs(Speed);
	const double Top = LeanForInput(1.0);
	const double Tight = std::atan(V * V / (G * TightTurn(V)));
	const double HoldBack = Lerp(0.85, 1.0, Clamp((V - 4.0) / 21.0, 0.0, 1.0)) * TyreLean();
	const double SpeedFactor = Clamp(Tight / std::max(Top, 0.01), 0.15, 1.0);
	const double Want = Sign(LeanInput) * std::min(LeanForInput(std::fabs(LeanInput)) * SpeedFactor, HoldBack);
	const double Rc = Crown();
	const double Arm = std::max(Build.ComHeight - Rc, 0.2);
	return Want + std::asin(Clamp(Rc * std::sin(Want) / Arm, -1.0, 1.0));
}

double BikeSim::TightTurn(double V) const
{
	const double Open = TIGHT_TURN + TIGHT_TURN_OPEN * V * V;
	if (V >= TIGHT_SPEED) return Open;
	const double AtTen = TIGHT_TURN + TIGHT_TURN_OPEN * TIGHT_SPEED * TIGHT_SPEED;
	const double X = V / TIGHT_SPEED;
	return Lerp(LockTurn(V), AtTen, X * X * X * X);
}

double BikeSim::LockTurn(double V) const
{
	const double Upright = Build.Wheelbase / (std::tan(STEER_LOCK * LOCK_SHARE) * std::cos(DegToRad(Build.RakeDeg)));
	double R = Upright;
	for (int i = 0; i < 3; ++i) R = Upright * std::cos(std::atan(V * V / (G * R)));
	return R;
}

double BikeSim::LeanForInput(double X) const
{
	const double Top = GripLean();
	const double Road = std::min(BalanceLean(ROAD_LEAN), Top * 0.85);
	X = Clamp(X, 0.0, 1.0);
	if (X <= ROAD_INPUT) return Road * X / ROAD_INPUT;
	const double T = (X - ROAD_INPUT) / (1.0 - ROAD_INPUT);
	const double S = Road / ROAD_INPUT * (1.0 - ROAD_INPUT);
	return std::min(Road + S * T + (Top - Road - S) * T * T, Top);
}

double BikeSim::GripLean() const { return std::min(TyreLean(), BalanceLean(Build.LeanClearance)); }

double BikeSim::TyreLean() const
{
	const int Surf = Wheels[1].Surf;
	const double Mu = std::min(Build.TyreGrip(true, Surf), Build.TyreGrip(false, Surf));
	return std::min(std::atan(Mu * GRIP_IN_HAND), MAX_LEAN);
}

double BikeSim::Crown() const { return (Build.TyreFront.Crown + Build.TyreRear.Crown) * 0.5; }

double BikeSim::BalanceLean(double Lean) const
{
	const double Rc = Crown();
	const double Arm = std::max(Build.ComHeight - Rc, 0.2);
	return std::atan2(Arm * std::sin(Lean), Rc + Arm * std::cos(Lean));
}

double BikeSim::MaxSteer() const
{
	const double V = std::fabs(Speed);
	const double FastS = Lerp(DegToRad(35.0), DegToRad(3.5), Clamp(V / 45.0, 0.0, 1.0));
	return Lerp(STEER_LOCK, FastS, SmoothStep(2.5, TIGHT_SPEED, V));
}

// The front end turns about its raked steering axis, a real part of the bike with its own weight
// to swing: the road's push on the tyre, the spinning wheel, the head bearings and the rider's
// hands all act on it. The rider aims the bars where the lean they want needs them and their arms
// hold the bars on that aim. At walking pace, feet down, they just point the bars.
void BikeSim::UpdateSteering(double Delta)
{
	const double V = std::fabs(Speed);
	const double Before = Steer;
	const double Target = TargetLean();
	const double Rake = RakeNow;
	const double K = 1.0 - std::exp(-Delta / RIDER_FEEL);
	Seen[0] = Lerp(Seen[0], LeanAngle, K);
	Seen[1] = Lerp(Seen[1], Seen[0], K);
	Seen[2] = Lerp(Seen[2], LeanRate, K);
	Seen[3] = Lerp(Seen[3], Seen[2], K);
	double Cmd;
	if (V < 2.0)
	{
		Cmd = LeanInput * MaxSteer();
		LeanI = 0.0;
	}
	else
	{
		const double Turn = std::atan(Build.Wheelbase * G * std::tan(BalanceLean(Seen[1])) / (V * V));
		const double SteerEq = std::atan(std::tan(Turn) * std::cos(Seen[1]) / std::cos(Rake));
		const double Gain = Clamp(8.0 / V, 0.25, 2.5);
		const double Err = Target - Seen[1];
		if (std::fabs(Err) < 0.12) LeanI = Clamp(LeanI + Err * Delta, -0.25, 0.25);
		else LeanI = MoveToward(LeanI, 0.0, Delta * 0.5);
		const double Push = Clamp(std::pow(0.09 / std::max(Build.Trail * std::cos(Rake), 0.02), 2.0), 0.5, 1.3);
		const double Flick = Build.Flick;
		const double RateMax = LEAN_RATE_MAX * Flick;
		double RWant = Clamp(Err * LEAN_RATE_GAIN, -RateMax, RateMax);
		const double Stop = std::sqrt(2.0 * LEAN_CATCH * Flick * Flick * std::fabs(Err));
		RWant = Clamp(RWant, -Stop, Stop) * Push;
		const double Catch = Clamp(STEER_CATCH / (V * V), 0.004, 0.15) * (1.0 + STEER_CATCH_FAST * Fast(V));
		Cmd = SteerEq + Catch * (Seen[3] - RWant) - Push * Gain * 0.25 * LeanI;
	}
	Cmd = Clamp(Cmd, -MaxSteer(), MaxSteer());
	const double AimWas = SteerAim;
	SteerAim = MoveToward(SteerAim, Cmd, Build.SteerRate * Delta);
	const double AimRate = (SteerAim - AimWas) / Delta;
	const double Riding = Clamp((V - 2.0) / 2.5, 0.0, 1.0);

	double SelfT = FrontSelfTorque();
	SelfT += FrontCarriedTorque(Delta);
	const double Is = Build.SteerInertia + ARM_INERTIA;
	const double ArmC = Lerp(ARM_DAMPING_SLOW, ARM_DAMPING, Clamp((V - 10.0) / 15.0, 0.0, 1.0));
	double C = HEAD_DAMPING + Build.DamperDamping + ArmC;
	C += Build.DamperFriction / std::max(std::fabs(SteerVel), 0.08);
	double Ks = 0.0;
	{
		const double Lever = Build.BarWidthMm / 720.0;
		const double Hold = Lerp(ARM_HOLD_SLOW, ARM_HOLD, Riding) * Lever;
		double Most = BarStrength(V);
		const double KArm = Hold + 0.5 * FrontStiffness(V) * Riding;
		Trim = Riding > 0.0 ? Clamp(Trim + KArm * (SteerAim - Steer) * Delta / ARM_TRIM * Riding, -Most, Most) : 0.0;
		const double Raw = KArm * (SteerAim - Steer) + Trim + ArmC * AimRate;
		const double Th = std::tanh(Raw / Most);
		Ks = KArm * (1.0 - Th * Th);
		BarTorque = Most * Th;
	}
	// Stepped implicitly in the damping and the arms' hold, so stiff arms stay steady.
	const double Vel = (SteerVel + Delta * (SelfT + BarTorque) / Is) / (1.0 + Delta * C / Is + Delta * Delta * Ks / Is);
	double Dyn = Steer + Vel * Delta;
	Dyn = Clamp(Dyn, -STEER_LOCK, STEER_LOCK);
	Steer = Lerp(SteerAim, Dyn, Riding);
	const double VelWas = SteerVel;
	SteerVel = (Steer - Before) / Delta;
	// What swung the front end round its axis didn't turn the rest of the bike.
	ApplyTorque(Basis * SteerAxis * (Is * (SteerVel - VelWas) / Delta * Riding));
	SteerSlow = Lerp(SteerSlow, Steer, 1.0 - std::exp(-Delta / 0.08));
	Wobble = std::max(std::fabs(Steer - SteerSlow), Wobble * std::exp(-Delta / 0.3));
}

double BikeSim::BarStrength(double V) const
{
	const double Lever = Build.BarWidthMm / 720.0;
	const double F = Fast(V);
	return MAX_BAR_TORQUE * Lerp(Lever, 1.0, F) * (1.0 + BAR_PUSH_FAST * F);
}

double BikeSim::Fast(double V) { return Clamp((V - BAR_PUSH_FROM) / (BAR_PUSH_FULL - BAR_PUSH_FROM), 0.0, 1.0); }

double BikeSim::FrontCarriedTorque(double Delta)
{
	const V3 WNow = AngularVelocity;
	const V3 VNow = LinearVelocity;
	if (!HavePrev)
	{
		PrevW = WNow;
		PrevV = VNow;
		HavePrev = true;
	}
	const V3 WDot = (WNow - PrevW) / Delta;
	const V3 ACm = (VNow - PrevV) / Delta;
	PrevW = WNow;
	PrevV = VNow;
	const V3 Axis = Basis * SteerAxis;
	const V3 RPiv = Basis * (SteerPivot - CenterOfMass);
	const V3 APiv = ACm + WDot.Cross(RPiv) + WNow.Cross(WNow.Cross(RPiv));
	const V3 R = Basis * (M3::AxisAngle(SteerAxis, -Steer) * OffsetArm);
	return Build.SteerMass * R.Cross(APiv + UP * G).Dot(Axis) + Build.SteerInertia * WDot.Dot(Axis);
}

double BikeSim::FrontSelfTorque()
{
	Wheel& F = Wheels[0];
	double T = FrontGyro().Dot(Basis * SteerAxis);
	if (!F.Contact || !F.HasContact)
	{
		TrailNow = Build.Trail;
		return T;
	}
	const V3 Axis = SteerAxis;
	const V3 Pivot = SteerPivot;
	const V3 At = F.ContactLocal;
	const V3 LongDir = F.LongDir;
	V3 Tq = (At - Pivot).Cross(F.Force);
	const double Pt = PNEUMATIC_TRAIL * Clamp(1.0 - std::fabs(F.Sy), 0.0, 1.0) * F.SlipShare;
	Tq += (-LongDir * Pt).Cross(F.FLat);
	const V3 N = F.NormalLocal;
	const double Along = Axis.Dot(N);
	if (std::fabs(Along) > 0.2)
	{
		const V3 Meet = Pivot + Axis * ((At - Pivot).Dot(N) / Along);
		TrailNow = (Meet - At).Dot(LongDir);
	}
	return T - Tq.Dot(Axis);
}

V3 BikeSim::FrontGyro() const
{
	const V3 Axis = Basis * SteerAxis;
	const V3 H = -Basis.X.Rotated(Axis, -Steer) * Build.FrontInertia * Wheels[0].Omega;
	return (AngularVelocity - Axis * SteerVel).Cross(H);
}

void BikeSim::ApplyWheelGyro()
{
	const V3 Axis = Basis * SteerAxis;
	const V3 Front = FrontGyro();
	const V3 HRear = -Basis.X * Build.RearInertia * Wheels[1].Omega;
	ApplyTorque(-(Front - Axis * Front.Dot(Axis)) - AngularVelocity.Cross(HRear));
}

double BikeSim::FrontStiffness(double V, bool Live) const
{
	const TyreParams& Tp = Build.TyreFront;
	const double Mu = Tp.GripTarmac;
	const double Fz = Build.TotalMass * G * 0.5;
	const double Trail = Live ? TrailSprings : Build.Trail;
	const double Rake = Live ? RakeNow : DegToRad(Build.RakeDeg);
	const double CAlpha = Mu * Fz * TyreModel::StiffnessAtZero(Tp) / std::tan(Tp.AlphaPeak);
	return std::max(Trail, 0.0) * std::cos(Rake) * CAlpha * Clamp(V / 6.0, 0.15, 1.0);
}

void BikeSim::UpdateEngine(double Delta)
{
	Wheel& Rear = Wheels[1];
	const double Ratio = Build.OverallRatio(Gear);
	const double WheelRpm = Rear.Omega * Ratio * 60.0 / TAU;
	const double IdleRpm = Build.IdleRpm;
	const double ClutchRpm = IdleRpm + 900.0;
	const bool Engaged = WheelRpm >= ClutchRpm;
	const bool Shifting = ShiftTimer > 0.0;
	ShiftTimer -= Delta;

	if (Shifting)
	{
		const double F = Clamp(1.0 - ShiftTimer / std::max(Build.ShiftTime, 0.01), 0.0, 1.0);
		Rpm = Lerp(ShiftFromRpm, std::max(WheelRpm, IdleRpm), F * F);
	}
	else if (Engaged)
	{
		Rpm = WheelRpm;
	}
	else
	{
		// Slipping clutch: the engine holds launch revs while the wheel catches up.
		const double Launch = Lerp(IdleRpm, Build.PeakTorqueRpm * 0.75, ThrottleEff);
		Rpm = Lerp(Rpm, std::max(Launch, WheelRpm), 1.0 - std::exp(-10.0 * Delta));
	}

	// The rider's wrist: with realism at 0.5 and up the throttle is rolled on and off smoothly.
	const double Smooth = Clamp(Assist * 2.0, 0.0, 1.0);
	const double Rate = Lerp(1000.0, Throttle > ThrottleEff ? 5.0 : 8.0, Smooth);
	ThrottleEff = MoveToward(ThrottleEff, Throttle, Rate * Delta);
	ShiftLoad = ThrottleEff > ShiftLoad + 0.35 ? ThrottleEff : Lerp(ShiftLoad, ThrottleEff, 1.0 - std::exp(-1.5 * Delta));
	const double VGround = std::max(std::fabs(Speed), 1.0);
	const double SpinRatio = (Rear.Omega * Rear.REff - VGround) / VGround;
	double TcTarget = 1.0;
	const double AidStrength = Build.TractionControl ? Build.TractionStrength : 0.0;
	const double Tc = std::max(std::min(1.0, Assist), AidStrength);
	double TcFrom = 0.12, TcGain = 4.0;
	if (Build.TractionControl) TcFrom = Lerp(0.1, 0.06, Clamp(AidStrength, 0.0, 1.0));
	else
	{
		TcFrom = Build.TyreRear.KappaPeak;
		TcGain = RIDER_SPIN_FEEL / TcFrom;
	}
	if (Tc > 0.05 && SpinRatio > TcFrom && Rear.Contact)
		TcTarget = Clamp(1.0 - (SpinRatio - TcFrom) * TcGain * Tc, 0.15, 1.0);
	if (!Wheels[0].Contact && Rear.Contact && PitchAngle > DegToRad(1.5))
	{
		const double Over = PitchAngle - WheelieTarget();
		const double Rising = AngularVelocity.Dot(Basis.X);
		const double Ahead = Over + std::max(Rising, 0.0) * 0.45;
		if (Ahead > 0.0) TcTarget = std::min(TcTarget, Clamp(1.0 - Ahead * 8.0 * Steadiness(), 0.0, 1.0));
	}
	Traction = Lerp(Traction, TcTarget, 1.0 - std::exp(-25.0 * Delta));
	double EngineNm = Build.EngineTorque(Rpm) * ThrottleEff * Traction;
	if (Rpm >= Build.Redline) EngineNm = 0.0;   // rev limiter
	if (Engaged) EngineNm -= (1.0 - ThrottleEff) * Build.PeakTorque * 0.25 * Clamp(Rpm / Build.Redline, 0.0, 1.0);
	if (Shifting) EngineNm = 0.0;
	Rear.DriveTorque = EngineNm * Ratio * Build.DrivetrainEfficiency;
	Rpm = Clamp(Rpm, IdleRpm * 0.9, Build.Redline * 1.02);

	SinceShift += Delta;
	if (AutoShift && !Shifting && SinceShift > 0.6)
	{
		const double RoadRpm = std::fabs(Speed) / Build.RearRadius * Ratio * 60.0 / TAU;
		const double UpAt = AutoUpRpm(ShiftLoad);
		const double FloorRpm = AutoFloorRpm(ShiftLoad);
		if (Gear < Build.GearCount() && RoadRpm > UpAt && RoadRpm * Build.OverallRatio(Gear + 1) / Ratio > FloorRpm * 1.1)
			Select(Gear + 1);
		else if (Gear > 1)
		{
			const double LowerRpm = RoadRpm * Build.OverallRatio(Gear - 1) / Ratio;
			if (RoadRpm < FloorRpm && LowerRpm < std::min(Build.Redline * 0.8, UpAt)) Select(Gear - 1);
		}
	}
}

double BikeSim::AutoUpRpm(double Load) const
{
	const double Easy = std::max(Build.PeakTorqueRpm * 0.55, Build.IdleRpm * 3.5);
	return Lerp(Easy, Build.Redline * 0.9, std::pow(Clamp(Load, 0.0, 1.0), 1.5));
}

double BikeSim::AutoFloorRpm(double Load) const
{
	const double Lug = std::max(Build.PeakTorqueRpm * 0.4, Build.IdleRpm * 2.0);
	return Lerp(Lug, Build.PeakTorqueRpm * 0.7, std::pow(Clamp(Load, 0.0, 1.0), 2.0));
}

void BikeSim::Select(int NewGear)
{
	if (NewGear == Gear) return;
	ShiftFromRpm = Rpm;
	Gear = NewGear;
	ShiftTimer = Build.ShiftTime;
	SinceShift = 0.0;
}

int BikeSim::GearForSpeed() const
{
	for (int Gi = 1; Gi <= Build.GearCount(); ++Gi)
		if (std::fabs(Speed) / Build.RearRadius * Build.OverallRatio(Gi) * 60.0 / TAU < Build.Redline * 0.8) return Gi;
	return Build.GearCount();
}

double BikeSim::WheelieTarget() const
{
	// Without asking for one (weight right back), the rider keeps the front down: a big twin lifts it
	// at full throttle in first, and a rider who isn't after a wheelie rolls off. Asked for, the
	// wheel comes up to a balance point a little higher the further back the weight is.
	// Asked for, nothing holds it up: the rider keeps it there with the throttle and their weight.
	// Only near the loop-over point does the rider's instinct (and more so with more assist) roll
	// off and cover the rear brake.
	double T = 0.0;
	if (BodyShift <= -0.6) T = Lerp(DegToRad(78.0), DegToRad(46.0), Clamp(Assist, 0.0, 1.0));
	return T;
}

void BikeSim::WheelForces(Wheel& W, double Delta)
{
	const M3& B = Basis;
	const V3 Up = B.Y;
	const bool IsFront = W.IsFront;
	const TyreParams& Tp = IsFront ? Build.TyreFront : Build.TyreRear;
	const double Radius = W.Radius;
	const double RollK = Build.Rolling;
	const double Crown = std::min(Tp.Crown, Radius * 0.6);
	const double Travel = W.Travel;
	const double QWas = W.Compression;
	// Where the axle would sit fully extended, and the way it moves as the suspension squashes.
	V3 Axle0, Path;
	if (IsFront)
	{
		const M3 Turn = M3::AxisAngle(SteerAxis, -Steer);
		Axle0 = W.Axle + Turn * OffsetArm - OffsetArm;
		Path = Turn * Build.ForkPath;
	}
	else
	{
		Path = Build.RearPathAt(QWas);
		Axle0 = Build.RearAxleAt(QWas) - Path * QWas;
	}
	const V3 Mount = BodyToWorld(Axle0 + Path * Travel);
	const double RayLen = Travel + Radius + Crown * 2.0;
	const RayHit Hit = Ray ? Ray(Mount, Mount - Up * RayLen) : RayHit();
	const double InertiaW = W.Inertia;

	V3 Heading = -B.Z;
	V3 AxleDir = B.X;
	if (IsFront)
	{
		const V3 Axis = B * SteerAxis;
		Heading = Heading.Rotated(Axis, -Steer);
		AxleDir = AxleDir.Rotated(Axis, -Steer);
	}
	double RawComp = -1.0;
	V3 N = UP;
	double CosCam = 1.0;
	int HitSurf = SURF_GRASS;
	const V3 PathW = B * Path;
	double Slide = 1.0;
	if (Hit.Hit)
	{
		N = Hit.Normal;
		HitSurf = Hit.Surface;
		const double SinCam = Clamp(AxleDir.Dot(N), -0.95, 0.95);
		CosCam = std::sqrt(1.0 - SinCam * SinCam);
		Slide = std::max(PathW.Dot(N), 0.2);
		const double HMount = (Mount - Hit.Position).Dot(N);
		const double Extended = (HMount - Crown - (Radius - Crown) * CosCam) / Slide;
		RawComp = Travel - Extended;
		for (const CattleGrid& Gr : Grids)
		{
			const V3 D = Hit.Position - Gr.Centre;
			const double U = D.x * Gr.Along.x + D.z * Gr.Along.z;
			const double Across = std::fabs(D.x * Gr.Along.z - D.z * Gr.Along.x);
			if (std::fabs(U) > Gr.HalfLength || Across > Gr.HalfWidth || std::fabs(D.y) > 1.0) continue;
			// Up on the bar tops, dipping a little between them: the dip fades out with speed, where
			// the sim's tick couldn't follow it (the sound carries the buzz then).
			const double Dip = 0.003 * (0.5 - 0.5 * std::cos(TAU * U / Gr.Pitch)) * Clamp(1.0 - std::fabs(Speed) / 6.0, 0.0, 1.0);
			// The tyre wraps round the edge (taking about half the step in its carcass) and rolls up
			// onto the first bar over the length it takes to climb it.
			const double Climb = std::sqrt(2.0 * Radius * Gr.Proud);
			const double Rise = SmoothStep(0.0, Climb, Gr.HalfLength - std::fabs(U));
			RawComp += (Gr.Proud * 0.5 * Rise - Dip) / Slide;
			HitSurf = SURF_GRID;
			break;
		}
	}

	if (RawComp < 0.0)
	{
		W.Contact = false;
		W.Load = 0.0;
		W.Compression = MoveToward(W.Compression, 0.0, Delta * 2.0);
		W.Centre = Axle0 + Path * W.Compression;
		double OmegaAir = W.Omega + W.DriveTorque * Delta / InertiaW;
		const double Spun = OmegaAir;
		OmegaAir = MoveToward(OmegaAir, 0.0, W.BrakeTorque * Delta / InertiaW);
		W.BrakeUsed = std::fabs(Spun - OmegaAir) * InertiaW / Delta;
		W.Omega = OmegaAir * 0.999;
		W.Spin = std::fmod(W.Spin + W.Omega * Delta, TAU);
		W.Slip = W.GripUsed = W.Kappa = W.Alpha = W.Sy = W.FSide = W.FSusp = 0.0;
		W.Eased = W.Bottomed = false;
		W.Force = W.FLat = V3();
		return;
	}

	// The spring is preloaded: until the road pushes harder than that only the tyre squashes.
	const double Rate = W.Spring;
	const double PreGap = Rate * W.Preload / TYRE_RATE;
	const double Comp = Clamp(RawComp - PreGap, 0.0, Travel);
	const bool WasTouching = W.Contact;
	const double Closing = -(LinearVelocity + AngularVelocity.Cross(Mount - Position)).Dot(N) / Slide;
	const double CompVel = WasTouching ? (Comp - QWas) / Delta : Closing;
	const double RawVel = WasTouching ? (RawComp - W.Raw) / Delta : Closing;
	W.Raw = RawComp;
	W.Compression = Comp;
	W.Centre = Axle0 + Path * Comp;
	double DampForce;
	if (CompVel > 0.0)
	{
		const double Knee = W.Knee;
		DampForce = W.Bump * (CompVel < Knee ? CompVel : Knee + (CompVel - Knee) * 0.45);
	}
	else DampForce = W.Rebound * CompVel;
	double FSusp;
	if (RawComp <= PreGap) FSusp = TYRE_RATE * RawComp;
	else FSusp = BikeSpec::SpringForce(Rate, Comp, Travel, W.Prog, W.Preload) + DampForce;
	W.Bottomed = RawComp - PreGap > Travel;
	if (W.Bottomed)
	{
		const double Over = RawComp - PreGap - Travel;
		FSusp += Over * 160000.0 + RawVel * (RawVel > 0.0 ? 9000.0 : 14000.0);
	}
	FSusp = std::max(FSusp, 0.0);
	W.FSusp = FSusp;
	W.Contact = true;
	W.Surf = HitSurf;

	// Where the crown touches: down the wheel's plane to the crown's centre, then onto the road.
	V3 Down = -N - AxleDir * (-N).Dot(AxleDir);
	Down = Down.Length() > 0.01 ? Down.Normalized() : -Up;
	const V3 Centre = BodyToWorld(Axle0 + Path * Comp);
	V3 Contact = Centre + Down * (Radius - Crown) - N * Crown;
	Contact -= N * (Contact - Hit.Position).Dot(N);
	const double REff = (Radius - Crown) + Crown * CosCam;
	W.REff = REff;
	W.ContactPos = Contact;
	W.HasContact = true;
	W.Normal = N;
	const V3 Arm = Contact - Position;

	V3 LongDir = N.Cross(AxleDir);
	LongDir = LongDir.Length() > 0.1 ? LongDir.Normalized() : (Heading - N * Heading.Dot(N)).Normalized();
	const V3 LatDir = N.Cross(LongDir);

	// The tyre's load: the suspension pushes only along its own travel.
	double Push = FSusp;
	if (!IsFront)
	{
		const double Dt = W.DriveTorque;
		const V3 Pull = B * Build.ChainPullAt(Axle0 + Path * Comp, Dt >= 0.0);
		Push -= std::fabs(Dt) / Build.SprocketRRear * Pull.Dot(PathW);
	}
	double Along = N.Dot(PathW) + Clamp(W.Rx, -1.6, 1.6) * LongDir.Dot(PathW) + Clamp(W.Ry, -1.6, 1.6) * LatDir.Dot(PathW);
	if (W.BrakeTorque > 0.0 && std::fabs(W.Omega) > 0.5)
	{
		const double React = std::max(-Clamp(W.Rx, -1.6, 1.6), 0.0) * REff * Sign(W.Omega);
		if (!IsFront) Along += React / Build.SwingLength;
		else if (Build.ForkLink > 0.0) Along -= React / Build.ForkLink;
	}
	const double Fz = Clamp(Push / std::max(Along, 0.5), 0.0, Build.TotalMass * G * 6.0);
	W.Load = Fz;
	ApplyForce(N * Fz, Arm);
	V3 VContact = LinearVelocity + AngularVelocity.Cross(Arm);
	if (IsFront) VContact += (B * SteerAxis * -SteerVel).Cross(Contact - BodyToWorld(SteerPivot));
	const double VLong = VContact.Dot(LongDir);
	const double VLat = VContact.Dot(LatDir);
	const double VAbs = std::fabs(VLong);
	const double MuSurf = Build.TyreGrip(IsFront, HitSurf);
	const double Mu = TyreModel::MuAt(MuSurf, Fz, Build.TotalMass * G * 0.5, Tp);
	const double FPeak = Mu * Fz;
	const double Bc = TyreModel::StiffnessAtZero(Tp);
	const double Kp = Tp.KappaPeak;
	const double Ap = Tp.AlphaPeak;
	W.Mu = Mu;

	// Lateral: slip angle and camber thrust as one normalised slip, built over the relaxation length.
	const double Alpha = std::atan2(VLat, std::max(VAbs, LAT_SLIP_SPEED));
	const double CamLeft = std::asin(Clamp(-Down.Dot(LatDir), -1.0, 1.0));
	W.Camber = -CamLeft;
	W.Alpha = Alpha;
	const double RollingIn = Clamp((VAbs - 1.0) / 3.0, 0.0, 1.0);
	const double TanA = std::tan(Clamp(Alpha, -TyreModel::MAX_SLIP_ANGLE, TyreModel::MAX_SLIP_ANGLE));
	const double SyNow = -TanA / std::tan(Ap) + Tp.Camber * CamLeft / (std::max(Mu, 0.1) * Bc) * RollingIn;
	double Sy = SyNow;
	if (WasTouching)
	{
		const double Keep = std::exp(-std::max(VAbs, RELAX_SPEED) * Delta / Tp.Relax);
		Sy = SyNow + (W.Sy - SyNow) * Keep;
	}
	W.Sy = Sy;
	const double SySlip = -TanA / std::tan(Ap);
	W.SlipShare = std::fabs(SyNow) > 1e-4 ? Clamp(SySlip / SyNow, 0.0, 1.0) : 1.0;

	// Longitudinal: the new spin solved implicitly against the tyre's own curve.
	const double Vden = std::max(VAbs, LONG_SLIP_SPEED);
	double Omega = W.Omega + W.DriveTorque * Delta / InertiaW;
	const double OmegaDriven = Omega;
	Omega = MoveToward(Omega, 0.0, W.BrakeTorque * Delta / InertiaW);
	double OmegaNew = SolveSpin(Omega, VLong, Vden, REff, Kp, Sy, FPeak, InertiaW / Delta, Tp);
	bool Eased = false;
	const double Care = BrakeCare(IsFront);
	if (Care > 0.0 && VLong > 1.0 && W.BrakeTorque > 0.0)
	{
		// The rider (or ABS) eases the brake before it goes past the grip left with the cornering.
		const double Left = std::max(std::sqrt(std::max(1.0 - Sy * Sy, 0.0)), 0.25);
		const double Allowed = Left * BRAKE_KEEP + CARELESS_SLIP * (1.0 - Care) / Care;
		if ((OmegaNew * REff - VLong) / Vden / Kp < -Allowed)
		{
			OmegaNew = std::max((VLong - Allowed * Kp * Vden) / REff, 0.0);
			Eased = true;
		}
	}
	W.Eased = Eased;
	const double Sx = (OmegaNew * REff - VLong) / Vden / Kp;
	double Gx, Gy;
	TyreModel::Combined(Sx, Sy, Tp, Gx, Gy);
	double Fx = FPeak * Gx;
	double Fy = FPeak * Gy;
	W.BrakeUsed = W.BrakeTorque > 0.0
		? Clamp(Sign(OmegaDriven) * (InertiaW / Delta * (OmegaDriven - OmegaNew) - REff * Fx), 0.0, W.BrakeTorque) : 0.0;
	W.Kappa = Sx * Kp;
	const double Rho = std::sqrt(Sx * Sx + Sy * Sy);
	W.GripUsed = Rho <= 1.0 ? std::sqrt(Gx * Gx + Gy * Gy) : Rho;
	const double SlipRatio = Clamp(Rho - 1.0, 0.0, 1.0);
	// At a crawl the tyre can't push harder sideways than it takes to stop the sideways creep.
	const double FyCap = std::fabs(VLat) * Build.TotalMass * 0.5 / Delta;
	Fy = Clamp(Fy, -FyCap, FyCap);
	Fx -= Sign(VLong) * RollK * Fz;
	W.Slip = SlipRatio;
	W.Rx = Fx / std::max(Fz, 1.0);
	W.Ry = Lerp(W.Ry, Fy / std::max(Fz, 1.0), 1.0 - std::exp(-Delta / 0.15));
	W.Omega = OmegaNew;
	W.Spin = std::fmod(W.Spin + OmegaNew * Delta, TAU);
	const V3 F = LongDir * Fx + LatDir * Fy;
	W.FSide = F.Dot(B.X);
	const M3 ToBike = B.Transposed();
	W.Force = ToBike * (F + N * Fz);
	W.FLat = ToBike * (LatDir * Fy);
	W.LongDir = ToBike * LongDir;
	W.ContactLocal = WorldToBody(Contact);
	W.NormalLocal = ToBike * N;
	ApplyForce(F, Arm);
}

double BikeSim::SolveSpin(double Omega0, double VLong, double Vden, double R, double Kp, double Sy, double FPeak,
	double IDt, const TyreParams& Tp)
{
	// Where the wheel's spin change balances the tyre's grip: I (w - w0) / dt = -r Fx(w), solved
	// by regula falsi (Illinois) between w0 and rolling speed, where the answer always lies.
	auto Fx = [&](double W) {
		double Gx, Gy;
		TyreModel::Combined((W * R - VLong) / Vden / Kp, Sy, Tp, Gx, Gy);
		return Gx;
	};
	const double Roll = VLong / R;
	double A = Omega0, B = Roll;
	double Fa = R * FPeak * Fx(A);
	if (std::fabs(Fa) < 1e-6 || std::fabs(A - B) < 1e-5) return A;
	double Fb = IDt * (B - Omega0);
	int Side = 0;
	for (int i = 0; i < 10; ++i)
	{
		const double C = (A * Fb - B * Fa) / (Fb - Fa);
		const double Fc = IDt * (C - Omega0) + R * FPeak * Fx(C);
		if (std::fabs(Fc) < 0.5 || std::fabs(B - A) < 1e-4) return C;
		if ((Fc > 0.0) == (Fb > 0.0))
		{
			B = C;
			Fb = Fc;
			if (Side == -1) Fa *= 0.5;
			Side = -1;
		}
		else
		{
			A = C;
			Fa = Fc;
			if (Side == 1) Fb *= 0.5;
			Side = 1;
		}
	}
	return (A * Fb - B * Fa) / (Fb - Fa);
}

double BikeSim::BrakeCare(bool IsFront) const
{
	if (IsFront ? Build.AbsFront : Build.AbsRear) return 1.0;
	if (IsFront) return Clamp(Assist * 2.0, 0.0, 1.0);
	return 0.0;
}

void BikeSim::ApplyAero()
{
	const V3 V = LinearVelocity;
	ApplyCentralForce(V * (-0.5 * AIR_DENSITY * Build.CdA * V.Length()));
}

void BikeSim::ApplyFeet()
{
	// Below walking pace the rider's feet hold the bike up: a stiff, damped roll spring that fades
	// out by 2.5 m/s, where steering balances it again.
	if (!(Wheels[0].Contact || Wheels[1].Contact)) return;
	const double Fade = Clamp(1.0 - std::fabs(Speed) / 2.5, 0.0, 1.0);
	if (Fade <= 0.0) return;
	const V3 Fwd = -Basis.Z;
	V3 FwdFlat(Fwd.x, 0.0, Fwd.z);
	if (FwdFlat.Length() < 0.01) return;
	FwdFlat = FwdFlat.Normalized();
	const double K = Build.TotalMass * G * Build.ComHeight * 3.0;
	const double C = 2.0 * std::sqrt(K * 60.0);
	ApplyTorque(FwdFlat * (-(K * LeanAngle + C * LeanRate) * Fade));
}

void BikeSim::ApplyRiderBalance(double Delta)
{
	const M3& B = Basis;
	const V3 Fwd = -B.Z;
	const V3 FwdFlat = V3(Fwd.x, 0.0, Fwd.z).Normalized();
	const bool AnyContact = Wheels[0].Contact || Wheels[1].Contact;
	const double Mass = Build.TotalMass;
	const double Strength = Clamp((Assist - BALANCE_FROM) / (1.0 - BALANCE_FROM), 0.0, 1.0);
	double FeetDown = 1.0 - Clamp(std::fabs(Speed) - 2.0, 0.0, 1.0);
	const double Err = TargetLean() - LeanAngle;
	double RHelp = Clamp(Err * BALANCE_RATE_GAIN, -BALANCE_RATE_MAX, BALANCE_RATE_MAX);
	const double Stop = std::sqrt(2.0 * BALANCE_CATCH * std::fabs(Err));
	RHelp = Clamp(RHelp, -Stop, Stop);
	const double Used = std::max(Wheels[0].GripUsed, Wheels[1].GripUsed);
	double Tau = 4.0 * (RHelp - LeanRate) * Mass * Strength * Clamp((1.05 - Used) / 0.25, 0.0, 1.0);
	FeetDown *= Clamp((DegToRad(55.0) - std::fabs(LeanAngle)) / DegToRad(15.0), 0.0, 1.0);
	Tau = Lerp(Tau, (22.0 * Err - 4.0 * LeanRate) * Mass, FeetDown);
	// The rider's body sways on their hips and soaks up a quick rocking of the bike (a weave).
	RateSlow = Lerp(RateSlow, LeanRate, 1.0 - std::exp(-Delta / 0.5));
	if (AnyContact && !IsCrashed) Tau -= BODY_SWAY * Mass * (LeanRate - RateSlow);
	if (!AnyContact) Tau *= 0.4;
	ApplyTorque(FwdFlat * Tau);
	// Pitch control by shifting body weight, mostly useful in the air.
	const V3 Right = B.X;
	double PitchTau = -BodyShift * (AnyContact ? 60.0 : 220.0);
	const double PitchRate = AngularVelocity.Dot(Right);
	if (!AnyContact)
	{
		const double Revs = Throttle - 0.33;
		PitchTau += (Revs - RearBrake * 0.6) * AIR_REV_PITCH * Build.Inertia.x;
	}
	else if (Wheels[0].Contact != Wheels[1].Contact)
		PitchTau -= PitchRate * Build.Inertia.x * Lerp(0.2, 0.8, Assist);   // a little steadying; the rest is the rider's
	ApplyTorque(Right * PitchTau);
	if (!AnyContact) ApplyTorque(-B.Y * AngularVelocity.Dot(B.Y) * Mass * 0.6);
}

void BikeSim::CheckCrash()
{
	if (std::fabs(LeanAngle) > CRASH_LEAN || std::fabs(PitchAngle) > CRASH_PITCH || Basis.Y.y < -0.2)
	{
		IsCrashed = true;
		CrashTimer = 0.0;
	}
}
} // namespace TorqueSim
