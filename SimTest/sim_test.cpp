// Rides the bike simulation without Unreal: on flat tarmac, then (if given) along a road line over
// a height field exported by Tools/export_buttertubs.py. Checks the bike behaves like the Godot
// game's: pulls away, holds a lean, stops, and rides the road without crashing.
//
//   cmake -S SimTest -B build/simtest && cmake --build build/simtest && build/simtest/sim_test [Data/Buttertubs]
#include "../Source/TorqueShedProto/Sim/BikeSim.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TorqueSim;

static int Failures = 0;
static void Check(bool Ok, const char* What, double Value)
{
	std::printf("  %-52s %10.3f  %s\n", What, Value, Ok ? "ok" : "FAIL");
	if (!Ok) ++Failures;
}

static RayHit FlatRay(const V3& From, const V3& To)
{
	RayHit H;
	if (From.y < 0.0 || To.y > 0.0) return H;
	const double T = From.y / (From.y - To.y);
	H.Hit = true;
	H.Position = From + (To - From) * T;
	H.Normal = UP;
	H.Surface = SURF_TARMAC;
	return H;
}

static void FlatTests()
{
	std::printf("Flat tarmac\n");
	const double Dt = 1.0 / 120.0;
	BikeSim S;
	S.Setup(BikeSpec::SpeedTwin1200());
	S.Ray = FlatRay;
	S.Place(V3(0, 0, 0), 0.0);
	for (int i = 0; i < 240; ++i) S.Step(Dt);   // settle on the springs
	Check(!S.IsCrashed && std::fabs(S.LeanAngle) < 0.05, "settles upright (lean, rad)", S.LeanAngle);
	Check(S.Wheels[0].Contact && S.Wheels[1].Contact, "both wheels down", 1.0);
	const double Static = S.Wheels[0].Load + S.Wheels[1].Load;
	Check(std::fabs(Static - S.Build.TotalMass * 9.81) < 150.0, "loads carry the weight (N)", Static);

	// Full throttle from rest.
	S.Throttle = 1.0;
	double T60 = -1.0;
	for (int i = 0; i < 120 * 12; ++i)
	{
		S.Step(Dt);
		if (T60 < 0.0 && S.Speed >= 26.82) T60 = i * Dt;
		if (S.IsCrashed) break;
	}
	Check(!S.IsCrashed, "full throttle: no crash", 0.0);
	Check(T60 > 2.5 && T60 < 6.0, "0-60 mph (s)", T60);
	std::printf("  speed after 12 s %.1f m/s, gear %d, %.0f rpm, lean %.3f\n", S.Speed, S.Gear, S.Rpm, S.LeanAngle);

	// Back off to about 25 m/s and hold a right-hand lean.
	S.Throttle = 0.0;
	for (int i = 0; i < 120 * 20 && S.Speed > 25.0; ++i) S.Step(Dt);
	S.Throttle = 0.35;
	S.LeanInput = 0.5;
	double MaxLean = 0.0;
	for (int i = 0; i < 120 * 6; ++i)
	{
		S.Step(Dt);
		MaxLean = std::max(MaxLean, S.LeanAngle);
		if (S.IsCrashed) break;
	}
	Check(!S.IsCrashed, "held lean: no crash", 0.0);
	Check(S.LeanAngle > 0.2 && S.LeanAngle < 0.75, "holds a right lean (rad)", S.LeanAngle);
	Check(MaxLean < S.LeanAngle + 0.2, "no big overshoot (peak, rad)", MaxLean);
	std::printf("  speed %.1f m/s, steer %.4f rad, wobble %.4f\n", S.Speed, S.Steer, S.Wobble);

	// Straighten up.
	S.LeanInput = 0.0;
	for (int i = 0; i < 120 * 4; ++i) S.Step(Dt);
	Check(!S.IsCrashed && std::fabs(S.LeanAngle) < 0.06, "stands back up (rad)", S.LeanAngle);

	// Emergency stop from about 60 mph.
	S.Throttle = 1.0;
	for (int i = 0; i < 120 * 20 && S.Speed < 26.8; ++i) S.Step(Dt);
	S.Throttle = 0.0;
	S.FrontBrake = 1.0;
	S.RearBrake = 1.0;
	const V3 From = S.Position;
	const double V0 = S.Speed;
	for (int i = 0; i < 120 * 10 && S.Speed > 0.3; ++i)
	{
		S.Step(Dt);
		if (S.IsCrashed) break;
	}
	const double Dist = (S.Position - From).Length();
	Check(!S.IsCrashed, "emergency stop: no crash", 0.0);
	std::printf("  braking from %.1f m/s\n", V0);
	Check(Dist > 28.0 && Dist < 55.0, "stopping distance from 60 mph (m)", Dist);

	// The rear brake alone: ABS keeps the wheel turning; with ABS off it locks, at town and road speeds.
	for (double From : {15.0, 25.0})
	for (bool Abs : {true, false})
	{
		if (Abs) std::printf("  rear brake alone from %.0f m/s\n", From);
		S.Build.AbsRear = Abs;
		S.FrontBrake = 0.0;
		S.RearBrake = 0.0;
		S.Throttle = 1.0;
		for (int i = 0; i < 120 * 20 && S.Speed < From; ++i) S.Step(Dt);
		S.Throttle = 0.0;
		S.RearBrake = 1.0;
		double Worst = 0.0;
		for (int i = 0; i < 120 && S.Speed > 3.0; ++i)
		{
			S.Step(Dt);
			Worst = std::min(Worst, S.Wheels[1].Kappa);
		}
		S.RearBrake = 0.0;
		if (Abs) Check(Worst > -0.3, "rear brake with ABS: wheel keeps turning (slip)", Worst);
		else Check(Worst < -0.6, "rear brake without ABS: wheel locks (slip)", Worst);
		Check(!S.IsCrashed, "rear brake: no crash", 0.0);
	}
	S.Build.AbsRear = true;

	// Gears by hand.
	S.AutoShift = false;
	const int G0 = S.Gear;
	S.ShiftUp();
	Check(S.Gear == std::min(G0 + 1, S.Build.GearCount()), "shifts up by hand", S.Gear);
	S.ShiftDown();
	Check(S.Gear == G0, "shifts down by hand", S.Gear);
	S.AutoShift = true;
}

// --- Buttertubs ---------------------------------------------------------------------------------

struct HeightField
{
	int Nx = 0, Nz = 0;
	double X0 = 0, Z0 = 0, Step = 2.0;
	std::vector<float> H;
	std::vector<unsigned char> Road;   // 1 where it's tarmac
	double At(double X, double Z) const
	{
		const double Fx = (X - X0) / Step, Fz = (Z - Z0) / Step;
		int Ix = (int)std::floor(Fx), Iz = (int)std::floor(Fz);
		if (Ix < 0 || Iz < 0 || Ix >= Nx - 1 || Iz >= Nz - 1) return -1e9;
		const double Tx = Fx - Ix, Tz = Fz - Iz;
		auto V = [&](int A, int B) { return (double)H[(size_t)B * Nx + A]; };
		// The two triangles of the cell, as the exported mesh has them (split from (0,0) to (1,1)).
		if (Tx >= Tz) return V(Ix, Iz) + (V(Ix + 1, Iz) - V(Ix, Iz)) * Tx + (V(Ix + 1, Iz + 1) - V(Ix + 1, Iz)) * Tz;
		return V(Ix, Iz) + (V(Ix + 1, Iz + 1) - V(Ix, Iz + 1)) * Tx + (V(Ix, Iz + 1) - V(Ix, Iz)) * Tz;
	}
	bool OnRoad(double X, double Z) const
	{
		int Ix = (int)std::lround((X - X0) / Step), Iz = (int)std::lround((Z - Z0) / Step);
		if (Ix < 0 || Iz < 0 || Ix >= Nx || Iz >= Nz) return false;
		return Road[(size_t)Iz * Nx + Ix] != 0;
	}
};

static bool LoadField(const std::string& Dir, HeightField& F, std::vector<V3>& Line)
{
	std::ifstream Meta(Dir + "/sim_heights.txt");
	if (!Meta) return false;
	Meta >> F.Nx >> F.Nz >> F.X0 >> F.Z0 >> F.Step;
	std::ifstream Bin(Dir + "/sim_heights.f32", std::ios::binary);
	F.H.resize((size_t)F.Nx * F.Nz);
	Bin.read((char*)F.H.data(), F.H.size() * sizeof(float));
	std::ifstream RoadBin(Dir + "/sim_road.u8", std::ios::binary);
	F.Road.resize(F.H.size());
	RoadBin.read((char*)F.Road.data(), F.Road.size());
	std::ifstream L(Dir + "/sim_line.txt");
	double X, Y, Z;
	while (L >> X >> Y >> Z) Line.push_back(V3(X, Y, Z));
	return !Line.empty() && Bin && RoadBin;
}

static void ButtertubsTest(const std::string& Dir)
{
	HeightField F;
	std::vector<V3> Line;
	if (!LoadField(Dir, F, Line))
	{
		std::printf("Buttertubs: no data in %s (run Tools/export_buttertubs.py)\n", Dir.c_str());
		return;
	}
	std::printf("Buttertubs (%zu line points)\n", Line.size());
	const double Dt = 1.0 / 120.0;
	BikeSim S;
	S.Setup(BikeSpec::SpeedTwin1200());
	S.Ray = [&](const V3& From, const V3& To) {
		RayHit H;
		// March down the ray against the height field.
		const int Steps = 24;
		V3 Prev = From;
		double PrevD = From.y - F.At(From.x, From.z);
		for (int i = 1; i <= Steps; ++i)
		{
			const V3 P = From + (To - From) * (double(i) / Steps);
			const double D = P.y - F.At(P.x, P.z);
			if (D <= 0.0)
			{
				const double T = PrevD / (PrevD - D);
				H.Hit = true;
				H.Position = Prev + (P - Prev) * T;
				H.Position.y = F.At(H.Position.x, H.Position.z);
				const double E = 0.5;
				const double Hx = F.At(H.Position.x + E, H.Position.z) - F.At(H.Position.x - E, H.Position.z);
				const double Hz = F.At(H.Position.x, H.Position.z + E) - F.At(H.Position.x, H.Position.z - E);
				H.Normal = V3(-Hx / (2 * E), 1.0, -Hz / (2 * E)).Normalized();
				H.Surface = F.OnRoad(H.Position.x, H.Position.z) ? SURF_TARMAC : SURF_GRASS;
				return H;
			}
			Prev = P;
			PrevD = D;
		}
		return H;
	};
	const V3 D0 = Line[1] - Line[0];
	// Heading: 0 faces -Z, positive turns left (towards -X).
	const double Heading = std::atan2(-D0.x, -D0.z);
	S.Place(V3(Line[0].x, F.At(Line[0].x, Line[0].z), Line[0].z), Heading);
	// Left alone at the start, the bike should sit still, not roll off down the pass.
	for (int i = 0; i < 120 * 18; ++i) S.Step(Dt);
	Check(std::fabs(S.Speed) < 3.0, "sits at the start with no input (m/s after 18 s)", S.Speed);
	std::printf("  at rest: gear %d, %.0f rpm, lean %.3f\n", S.Gear, S.Rpm, S.LeanAngle);
	S.Place(V3(Line[0].x, F.At(Line[0].x, Line[0].z), Line[0].z), Heading);
	for (int i = 0; i < 120; ++i) S.Step(Dt);

	// A simple rider: follow the line a little ahead, at a speed that suits the bend coming up.
	size_t Near = 0;
	double MaxOff = 0.0, OffSum = 0.0, MaxLean = 0.0, Time = 0.0;
	int Samples = 0, Crashes = 0;
	const double Want = 16.0;   // m/s, about 36 mph: brisk on a single-track pass
	while (Near + 2 < Line.size() && Time < 600.0)
	{
		const V3 P = S.Position;
		double Best = 1e18;
		for (size_t k = Near; k < std::min(Line.size(), Near + 40); ++k)
		{
			const double D = std::hypot(Line[k].x - P.x, Line[k].z - P.z);
			if (D < Best) { Best = D; Near = k; }
		}
		size_t Ahead = Near;
		double Acc = 0.0;
		const double Look = 6.0 + 0.6 * std::fabs(S.Speed);
		while (Ahead + 1 < Line.size() && Acc < Look)
		{
			Acc += std::hypot(Line[Ahead + 1].x - Line[Ahead].x, Line[Ahead + 1].z - Line[Ahead].z);
			++Ahead;
		}
		const V3 Tg = Line[Ahead] - P;
		const V3 Fwd = -S.Basis.Z;
		const double HeadNow = std::atan2(-Fwd.x, -Fwd.z);
		double HeadTo = std::atan2(-Tg.x, -Tg.z);
		double Err = HeadTo - HeadNow;
		while (Err > Pi) Err -= TAU;
		while (Err < -Pi) Err += TAU;
		// Pure pursuit: the curvature to the look-ahead point, as the lean that turn needs.
		const double Dist = std::max(std::hypot(Tg.x, Tg.z), 1.0);
		const double Curv = 2.0 * std::sin(Err) / Dist;        // + turning left
		const double LeanWant = std::atan(S.Speed * S.Speed * Curv / 9.81);
		S.LeanInput = Clamp(-LeanWant / 0.55, -1.0, 1.0);       // input + leans right
		// Slow for the tightest bend in the next 60 m.
		double Sharp = 0.0;
		Acc = 0.0;
		for (size_t k = Near + 1; k + 1 < Line.size() && Acc < 60.0; ++k)
		{
			const V3 A = Line[k] - Line[k - 1], B = Line[k + 1] - Line[k];
			const double La = std::hypot(A.x, A.z), Lb = std::hypot(B.x, B.z);
			Acc += La;
			if (La < 0.5 || Lb < 0.5) continue;
			const double Turn = std::acos(Clamp((A.x * B.x + A.z * B.z) / (La * Lb), -1.0, 1.0));
			Sharp = std::max(Sharp, Turn / (0.5 * (La + Lb)));
		}
		const double Corner = Sharp > 1e-3 ? std::sqrt(9.81 * std::tan(0.5) / Sharp) : 99.0;
		const double Target = std::min(Want, Corner);
		const double Gap = Target - S.Speed;
		S.Throttle = Clamp(Gap * 0.4, 0.0, 1.0);
		S.FrontBrake = Clamp(-Gap * 0.25, 0.0, 0.8);
		S.RearBrake = S.FrontBrake * 0.5;
		for (int i = 0; i < 6; ++i) S.Step(Dt);
		Time += 6 * Dt;
		if (S.IsCrashed)
		{
			++Crashes;
			std::printf("  crashed at line point %zu, %.0f s in (lean %.2f, speed %.1f)\n", Near, Time, S.LeanAngle, S.Speed);
			const V3 D = Line[std::min(Near + 1, Line.size() - 1)] - Line[Near];
			S.Place(V3(Line[Near].x, F.At(Line[Near].x, Line[Near].z), Line[Near].z), std::atan2(-D.x, -D.z));
			if (Crashes > 5) break;
			continue;
		}
		MaxOff = std::max(MaxOff, Best);
		OffSum += Best;
		++Samples;
		MaxLean = std::max(MaxLean, std::fabs(S.LeanAngle));
	}
	Check(Near + 3 >= Line.size(), "reaches the end of the 3 km", (double)Near);
	Check(Crashes == 0, "no crashes on the way", (double)Crashes);
	Check(MaxOff < 3.0, "stays within 3 m of the centre line (m)", MaxOff);
	std::printf("  %.0f s, mean off line %.2f m, most lean %.0f deg\n", Time, OffSum / std::max(Samples, 1),
		MaxLean * 180.0 / Pi);
}

// --- Wheelies -----------------------------------------------------------------------------------
// beenster's ask: a wheelie needs the weight back to come up and the throttle held to stay up.

static void RollInFirst(BikeSim& S, double Assist)
{
	const double Dt = 1.0 / 120.0;
	S.Setup(BikeSpec::SpeedTwin1200());
	S.Ray = FlatRay;
	S.Place(V3(0, 0, 0), 0.0);
	S.Assist = Assist;
	S.AutoShift = false;
	S.BodyShift = S.FrontBrake = S.RearBrake = S.LeanInput = 0.0;
	for (int i = 0; i < 240; ++i) S.Step(Dt);
	S.Throttle = 0.3;
	for (int i = 0; i < 120 * 10 && S.Speed < 7.0; ++i) S.Step(Dt);
}

// Weight back and full throttle until the front is up at the given pitch.
static bool LiftTo(BikeSim& S, double Deg)
{
	const double Dt = 1.0 / 120.0;
	S.BodyShift = -1.0;
	S.Throttle = 1.0;
	for (int i = 0; i < 120 * 3; ++i)
	{
		S.Step(Dt);
		if (S.IsCrashed) return false;
		if (S.PitchAngle > DegToRad(Deg)) return true;
	}
	return false;
}

// A rider holding the wheelie on the throttle at about 28 degrees; returns seconds with the front up.
static double HoldOnThrottle(BikeSim& S, double Seconds)
{
	const double Dt = 1.0 / 120.0;
	double Up = 0.0, I = 0.0;
	for (int i = 0; i < (int)(Seconds * 120); ++i)
	{
		const double Err = DegToRad(28.0) - S.PitchAngle;
		const double Rate = S.AngularVelocity.Dot(S.Basis.X);
		I += Err * Dt;
		S.Throttle = Clamp(0.45 + 2.5 * Err - 0.6 * Rate + 0.5 * I, 0.0, 1.0);
		S.Step(Dt);
		if (S.IsCrashed) break;
		if (S.Wheels[0].Contact) break;
		Up += Dt;
	}
	return Up;
}

static double SecondsToFrontDown(BikeSim& S, double Most)
{
	const double Dt = 1.0 / 120.0;
	for (int i = 0; i < (int)(Most * 120); ++i)
	{
		S.Step(Dt);
		if (S.IsCrashed) return -1.0;
		if (S.Wheels[0].Contact) return i * Dt;
	}
	return Most;
}

static void WheelieTests()
{
	const double Dt = 1.0 / 120.0;
	for (double Assist : {0.0, 0.6})
	{
		std::printf("Wheelies at realism %.1f\n", Assist);
		BikeSim S;

		RollInFirst(S, Assist);
		S.Throttle = 1.0;
		double Most = 0.0;
		for (int i = 0; i < 120 * 2; ++i)
		{
			S.Step(Dt);
			Most = std::max(Most, S.PitchAngle);
		}
		Check(!S.IsCrashed && Most < DegToRad(6.0), "full throttle, weight centred: front stays low (deg)", Most * 180.0 / Pi);

		RollInFirst(S, Assist);
		const bool Lifted = LiftTo(S, 20.0);
		Check(Lifted, "weight back, full throttle: front up to 20 deg (deg)", S.PitchAngle * 180.0 / Pi);

		RollInFirst(S, Assist);
		LiftTo(S, 25.0);
		S.Throttle = 0.0;
		const double Shut = SecondsToFrontDown(S, 3.0);
		Check(Shut >= 0.0 && Shut < 1.5, "throttle shut at 25 deg: front down (s)", Shut);

		RollInFirst(S, Assist);
		LiftTo(S, 25.0);
		const double Held = HoldOnThrottle(S, 3.0);
		Check(!S.IsCrashed && Held >= 2.9, "weight back, throttle worked: held up (s)", Held);

		RollInFirst(S, Assist);
		LiftTo(S, 25.0);
		S.BodyShift = 0.0;
		const double Centred = HoldOnThrottle(S, 3.0);
		Check(!S.IsCrashed && Centred < 1.5, "weight centred, throttle worked: comes down (s)", Centred);
	}
}

// --- Crashes ------------------------------------------------------------------------------------
// beenster: the bike sank into the ground in a crash. Off, it should fall, slide and tumble to rest
// on top of the ground, on the flat and on a bumpy hillside.

// A road running down a hillside (falling away to +x), with dips along it and a lumpy verge.
static double Hill(double X, double Z) { return -0.15 * X + 0.4 * std::sin(Z * 0.12) + 0.08 * std::sin(X * 1.7 + Z * 0.5) * Clamp(std::fabs(X) - 2.0, 0.0, 1.0); }

static RayHit HillRay(const V3& From, const V3& To)
{
	RayHit H;
	const int Steps = 48;
	V3 Prev = From;
	double PrevD = From.y - Hill(From.x, From.z);
	for (int i = 1; i <= Steps; ++i)
	{
		const V3 P = From + (To - From) * (double(i) / Steps);
		const double D = P.y - Hill(P.x, P.z);
		if (D <= 0.0)
		{
			const double T = PrevD / (PrevD - D);
			H.Hit = true;
			H.Position = Prev + (P - Prev) * T;
			H.Position.y = Hill(H.Position.x, H.Position.z);
			const double E = 0.05;
			const double Hx = Hill(H.Position.x + E, H.Position.z) - Hill(H.Position.x - E, H.Position.z);
			const double Hz = Hill(H.Position.x, H.Position.z + E) - Hill(H.Position.x, H.Position.z - E);
			H.Normal = V3(-Hx / (2 * E), 1.0, -Hz / (2 * E)).Normalized();
			H.Surface = SURF_TARMAC;
			return H;
		}
		Prev = P;
		PrevD = D;
	}
	return H;
}

static void CrashTests()
{
	const double Dt = 1.0 / 120.0;
	struct Case { const char* Name; bool OnHill; int How; };
	for (const Case& K : {Case{"flat, looped wheelie", false, 0}, Case{"flat, lowside at 20 m/s", false, 1},
			 Case{"hillside, lowside at 20 m/s", true, 1}, Case{"hillside, highside at 25 m/s", true, 2}})
	{
		std::printf("Crash: %s\n", K.Name);
		const double Ground0 = K.OnHill ? Hill(0, 0) : 0.0;
		BikeSim S;
		S.Setup(BikeSpec::SpeedTwin1200());
		S.Ray = K.OnHill ? RayFn(HillRay) : RayFn(FlatRay);
		S.Place(V3(0, Ground0, 0), 0.0);
		S.Assist = K.How == 0 ? 0.0 : 0.6;
		for (int i = 0; i < 240; ++i) S.Step(Dt);
		Check(!S.IsCrashed, "upright before the crash", 0.0);
		if (K.How == 0)
		{
			S.AutoShift = false;
			S.Throttle = 1.0;
			S.BodyShift = -1.0;
			for (int i = 0; i < 120 * 6 && !S.IsCrashed; ++i) S.Step(Dt);
		}
		else
		{
			const double V = K.How == 1 ? 20.0 : 25.0;
			S.Throttle = 1.0;
			for (int i = 0; i < 120 * 20 && S.Speed < V; ++i) S.Step(Dt);
			// Thrown down: the bike off at speed and rolling onto its side (or flicked over the top).
			S.IsCrashed = true;
			const V3 Fwd = -S.Basis.Z;
			S.AngularVelocity = Fwd * (K.How == 1 ? 2.5 : -5.0) + S.Basis.X * (K.How == 2 ? -1.5 : 0.0);
			if (K.How == 2) S.LinearVelocity += UP * 2.5;
		}
		Check(S.IsCrashed, "came off (m/s)", S.LinearVelocity.Length());
		const V3 From = S.Position;
		double Lowest = 1e9;
		bool Finite = true;
		int Resting = 0;
		double Rest = -1.0;
		for (int i = 0; i < 120 * 8; ++i)
		{
			S.Step(Dt);
			const double Gap = S.GroundGap();
			if (!std::isfinite(Gap) || !std::isfinite(S.Position.y)) { Finite = false; break; }
			Lowest = std::min(Lowest, Gap);
			Resting = S.LinearVelocity.Length() < 0.1 && S.AngularVelocity.Length() < 0.1 ? Resting + 1 : 0;
			if (Rest < 0.0 && Resting > 60) Rest = i * Dt;
		}
		const double Gap = S.GroundGap();
		const double Slid = V3(S.Position.x - From.x, 0.0, S.Position.z - From.z).Length();
		Check(Finite, "no NaNs", 0.0);
		Check(Lowest > -0.06, "never sinks into the ground (deepest part, m)", Lowest);
		Check(Rest > 0.0, "comes to rest (s)", Rest);
		Check(Gap > -0.04 && Gap < 0.03, "lies on the ground at the end (lowest part, m)", Gap);
		std::printf("  ends %s\n", S.Basis.Y.y > 0.3 ? "wheels down" : S.Basis.Y.y < -0.3 ? "upside down" : "on its side");
		std::printf("  slid %.1f m\n", Slid);
	}
}

// --- Cattle grids -------------------------------------------------------------------------------
// beenster: ride over a cattle grid and feel it. A jolt up onto the bars, nothing that throws the bike.

static void GridTests()
{
	const double Dt = 1.0 / 120.0;
	std::printf("Cattle grid\n");
	for (double V : {5.0, 15.0, 25.0})
	{
		double Peak[2] = {0, 0}, MaxLean = 0.0;
		int OnGrid = 0;
		bool Crashed = false;
		for (int WithGrid = 0; WithGrid < 2; ++WithGrid)
		{
			BikeSim S;
			S.Setup(BikeSpec::SpeedTwin1200());
			S.Ray = FlatRay;
			S.Place(V3(0, 0, 0), 0.0);
			for (int i = 0; i < 240; ++i) S.Step(Dt);
			S.Throttle = 1.0;
			for (int i = 0; i < 120 * 20 && S.Speed < V; ++i) S.Step(Dt);
			CattleGrid G;
			G.Centre = S.Position - S.Basis.Z * 12.0;
			G.Centre.y = 0.0;
			G.Along = V3(-S.Basis.Z.x, 0.0, -S.Basis.Z.z).Normalized();
			if (WithGrid) S.Grids.push_back(G);
			for (int i = 0; i < 120 * 4; ++i)
			{
				S.Throttle = 0.3;
				S.Step(Dt);
				if (S.IsCrashed) { Crashed = true; break; }
				for (const Wheel& W : S.Wheels)
				{
					Peak[WithGrid] = std::max(Peak[WithGrid], W.Load);
					if (W.Contact && W.Surf == SURF_GRID) ++OnGrid;
				}
				if (WithGrid) MaxLean = std::max(MaxLean, std::fabs(S.LeanAngle));
			}
		}
		std::printf("  at %.0f m/s: %d wheel-ticks on the bars\n", V, OnGrid);
		Check(!Crashed && MaxLean < 0.1, "crosses the grid upright (lean, rad)", MaxLean);
		Check(OnGrid > 0, "the wheels find the bars (wheel-ticks)", OnGrid);
		Check(Peak[1] > Peak[0] * 1.15 && Peak[1] < Peak[0] * 2.5, "a jolt, not a wall (peak load vs no grid)", Peak[1] / Peak[0]);
	}
}

int main(int Argc, char** Argv)
{
	FlatTests();
	WheelieTests();
	CrashTests();
	GridTests();
	if (Argc > 1) ButtertubsTest(Argv[1]);
	std::printf(Failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", Failures);
	return Failures ? 1 : 0;
}
