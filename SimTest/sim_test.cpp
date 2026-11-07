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

	// The rear brake alone: ABS keeps the wheel turning; with ABS off it locks.
	for (bool Abs : {true, false})
	{
		S.Build.AbsRear = Abs;
		S.FrontBrake = 0.0;
		S.RearBrake = 0.0;
		S.Throttle = 1.0;
		for (int i = 0; i < 120 * 20 && S.Speed < 15.0; ++i) S.Step(Dt);
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

int main(int Argc, char** Argv)
{
	FlatTests();
	if (Argc > 1) ButtertubsTest(Argv[1]);
	std::printf(Failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", Failures);
	return Failures ? 1 : 0;
}
