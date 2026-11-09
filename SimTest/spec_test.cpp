// Checks Fit/BikeRideSpec (how a bike of catalogue parts rides) against the original game: every
// style's reference bike and a sweep of single-part swaps on them, BikeBuild's numbers dumped from
// Godot by Tools/make_godot_reference.py --spec (SimTest/data/godot_spec.json). Each field is
// compared to a part in 10^5 (Godot works in float and the dump keeps six figures).
#include "BikeRideSpec.h"
#include "BikeSim.h"
#include "PartsDb.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace TorqueFit;
using TorqueSim::BikeSpec;
using TorqueSim::V3;

namespace
{
int Failures = 0;

void Expect(bool Ok, const std::string& What)
{
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What.c_str());
	if (!Ok) ++Failures;
}

bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::stringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

std::string Root()
{
	for (const char* R : {".", "..", "../..", "../../.."})
	{
		std::ifstream F(std::string(R) + "/Data/Fitment/layout.json");
		if (F) return R;
	}
	return ".";
}

double Num(const Json& J, const std::string& K) { return J.Num(K, 1e30); }
V3 Vec(const Json* J) { return J && J->A.size() == 3 ? V3(J->A[0].N, J->A[1].N, J->A[2].N) : V3(1e30, 1e30, 1e30); }

// The fields compared, and the worst miss of each across every bike.
struct Field
{
	std::string Name;
	double Worst = 0.0;
	std::string WorstAt;
	int Bad = 0;
};
std::map<std::string, Field> Fields;

void Same(const std::string& Name, double Port, double Godot, const std::string& Bike)
{
	Field& F = Fields[Name];
	F.Name = Name;
	const double Miss = std::fabs(Port - Godot) / std::max(1.0, std::fabs(Godot));
	if (Miss > F.Worst)
	{
		F.Worst = Miss;
		F.WorstAt = Bike + " (port " + std::to_string(Port) + ", Godot " + std::to_string(Godot) + ")";
	}
	if (Miss > 1e-5) ++F.Bad;
}

void Compare(const BikeLayout& Lay, const std::string& Bike, const Json& G)
{
	RideSpecInput In;
	if (const Json* P = G.Get("parts"))
		for (const auto& [Slot, Id] : P->O) In.Parts[Slot] = Id.S;
	BikeSpec S;
	if (!RideSpec(Lay, In, S))
	{
		Same("loads", 0.0, 1.0, Bike);
		return;
	}
	Same("bike_mass", S.BikeMass, Num(G, "bike_mass"), Bike);
	Same("total_mass", S.TotalMass, Num(G, "total_mass"), Bike);
	Same("wheelbase", S.Wheelbase, Num(G, "wheelbase"), Bike);
	Same("com_height", S.ComHeight, Num(G, "com_height"), Bike);
	const V3 Co = Vec(G.Get("com_offset"));
	Same("com_offset.y", S.ComOffset.y, Co.y, Bike);
	Same("com_offset.z", S.ComOffset.z, Co.z, Bike);
	{
		// Bike.setup's inertia from BikeBuild's: rough plus the spread, at least 60% of rough.
		const V3 Add = Vec(G.Get("inertia_add"));
		const double M = Num(G, "total_mass"), Wb = Num(G, "wheelbase");
		const V3 Rough(M * Wb * Wb * 0.16, M * Wb * Wb * 0.13, M * 0.11);
		Same("inertia.x", S.Inertia.x, std::max(Rough.x + Add.x, Rough.x * 0.6), Bike);
		Same("inertia.y", S.Inertia.y, std::max(Rough.y + Add.y, Rough.y * 0.6), Bike);
		Same("inertia.z", S.Inertia.z, std::max(Rough.z + Add.z, Rough.z * 0.6), Bike);
	}
	Same("rake_deg", S.RakeDeg, Num(G, "rake_deg"), Bike);
	Same("seat_height", S.SeatHeight, Num(G, "seat_height"), Bike);
	Same("cda", S.CdA, Num(G, "cda"), Bike);
	Same("steer_rate", S.SteerRate, Num(G, "steer_rate"), Bike);
	Same("lean_clearance", S.LeanClearance, Num(G, "lean_clearance"), Bike);
	Same("flick", S.Flick, Num(G, "flick"), Bike);
	Same("front_travel", S.FrontTravel, Num(G, "front_travel"), Bike);
	Same("front_spring", S.FrontSpring, Num(G, "front_spring"), Bike);
	Same("front_bump", S.FrontBump, Num(G, "front_bump"), Bike);
	Same("front_rebound", S.FrontRebound, Num(G, "front_rebound"), Bike);
	Same("front_progression", S.FrontProgression, Num(G, "front_progression"), Bike);
	Same("front_preload", S.FrontPreload, Num(G, "front_preload"), Bike);
	Same("front_knee", S.FrontKnee, Num(G, "front_knee"), Bike);
	Same("rear_travel", S.RearTravel, Num(G, "rear_travel"), Bike);
	Same("rear_spring", S.RearSpring, Num(G, "rear_spring"), Bike);
	Same("rear_bump", S.RearBump, Num(G, "rear_bump"), Bike);
	Same("rear_rebound", S.RearRebound, Num(G, "rear_rebound"), Bike);
	Same("rear_progression", S.RearProgression, Num(G, "rear_progression"), Bike);
	Same("rear_preload", S.RearPreload, Num(G, "rear_preload"), Bike);
	Same("rear_knee", S.RearKnee, Num(G, "rear_knee"), Bike);
	const V3 Fp = Vec(G.Get("fork_path"));
	Same("fork_path.y", S.ForkPath.y, Fp.y, Bike);
	Same("fork_path.z", S.ForkPath.z, Fp.z, Bike);
	Same("fork_link", S.ForkLink, Num(G, "fork_link"), Bike);
	const V3 Sp = Vec(G.Get("swing_pivot"));
	Same("swing_pivot.y", S.SwingPivot.y, Sp.y, Bike);
	Same("swing_pivot.z", S.SwingPivot.z, Sp.z, Bike);
	Same("swing_length", S.SwingLength, Num(G, "swing_length"), Bike);
	Same("sprocket_r_front", S.SprocketRFront, Num(G, "sprocket_r_front"), Bike);
	Same("sprocket_r_rear", S.SprocketRRear, Num(G, "sprocket_r_rear"), Bike);
	Same("front_radius", S.FrontRadius, Num(G, "front_radius"), Bike);
	Same("rear_radius", S.RearRadius, Num(G, "rear_radius"), Bike);
	Same("front_inertia", S.FrontInertia, Num(G, "front_inertia"), Bike);
	Same("rear_inertia", S.RearInertia, Num(G, "rear_inertia"), Bike);
	Same("rolling", S.Rolling, Num(G, "rolling"), Bike);
	Same("fork_offset", S.ForkOffset, Num(G, "fork_offset"), Bike);
	Same("trail", S.Trail, Num(G, "trail"), Bike);
	Same("steer_mass", S.SteerMass, Num(G, "steer_mass"), Bike);
	Same("steer_inertia", S.SteerInertia, Num(G, "steer_inertia"), Bike);
	Same("damper_damping", S.DamperDamping, Num(G, "damper_damping"), Bike);
	Same("damper_friction", S.DamperFriction, Num(G, "damper_friction"), Bike);
	Same("front_brake_torque", S.FrontBrakeTorque, Num(G, "front_brake_torque"), Bike);
	Same("rear_brake_torque", S.RearBrakeTorque, Num(G, "rear_brake_torque"), Bike);
	Same("abs_front", S.AbsFront ? 1.0 : 0.0, G.Flag("abs_front") ? 1.0 : 0.0, Bike);
	Same("abs_rear", S.AbsRear ? 1.0 : 0.0, G.Flag("abs_rear") ? 1.0 : 0.0, Bike);
	Same("traction", S.TractionControl ? S.TractionStrength : 0.0, Num(G, "traction"), Bike);
	Same("primary", S.Primary, Num(G, "primary"), Bike);
	Same("final_drive", S.FinalDrive, Num(G, "final_drive"), Bike);
	Same("shift_time", S.ShiftTime, Num(G, "shift_time"), Bike);
	Same("drivetrain_efficiency", S.DrivetrainEfficiency, Num(G, "drivetrain_efficiency"), Bike);
	if (const Json* R = G.Get("gear_ratios"))
	{
		Same("gears", (double)S.GearRatios.size(), (double)R->A.size(), Bike);
		for (size_t I = 0; I < R->A.size() && I < S.GearRatios.size(); ++I) Same("gear_ratios", S.GearRatios[I], R->A[I].N, Bike);
	}
	for (int End = 0; End < 2; ++End)
	{
		const TorqueSim::TyreParams& T = End == 0 ? S.TyreFront : S.TyreRear;
		const Json* Gt = G.Get(End == 0 ? "tyre_front" : "tyre_rear");
		const Json* Gg = G.Get(End == 0 ? "grip_front" : "grip_rear");
		const std::string P = End == 0 ? "tyre_front." : "tyre_rear.";
		if (!Gt || !Gg) continue;
		Same(P + "construction", T.Construction == Gt->Str("construction") ? 1.0 : 0.0, 1.0, Bike);
		Same(P + "kappa_peak", T.KappaPeak, Num(*Gt, "kappa_peak"), Bike);
		Same(P + "alpha_peak", T.AlphaPeak, Num(*Gt, "alpha_peak"), Bike);
		Same(P + "camber", T.Camber, Num(*Gt, "camber"), Bike);
		Same(P + "relax", T.Relax, Num(*Gt, "relax"), Bike);
		Same(P + "load_sens", T.LoadSens, Num(*Gt, "load_sens"), Bike);
		Same(P + "crown", T.Crown, Num(*Gt, "crown"), Bike);
		Same(P + "B", T.B, Num(*Gt, "B"), Bike);
		Same(P + "C", T.C, Num(*Gt, "C"), Bike);
		Same(P + "grip.tarmac", T.GripTarmac, Num(*Gg, "tarmac"), Bike);
		Same(P + "grip.dirt", T.GripGravel, Num(*Gg, "dirt"), Bike);
		Same(P + "grip.grass", T.GripGrass, Num(*Gg, "grass"), Bike);
	}
}
TorqueSim::RayHit FlatGround(const V3& From, const V3& To)
{
	TorqueSim::RayHit H;
	if (From.y < 0.0 || To.y > 0.0) return H;
	H.Hit = true;
	H.Position = From + (To - From) * (From.y / (From.y - To.y));
	H.Normal = V3(0.0, 1.0, 0.0);
	H.Surface = TorqueSim::SURF_TARMAC;
	return H;
}

// Each style's reference bike ridden on its own spec, as the Unreal game rides a catalogue bike
// (BikeRideSpec, the engine's curve Godot's): settled on its springs, then 10 s at half throttle on
// flat tarmac, hands off. It stays up, both wheels down, and gets going.
void Ride(const BikeLayout& Lay, const std::string& Style, const Json& G)
{
	RideSpecInput In;
	if (const Json* P = G.Get("parts"))
		for (const auto& [Slot, Id] : P->O) In.Parts[Slot] = Id.S;
	BikeSpec S;
	if (!RideSpec(Lay, In, S)) { Expect(false, Style + " rides"); return; }
	S.IdleRpm = Num(G, "idle_rpm");
	S.Redline = Num(G, "redline");
	S.PeakTorque = Num(G, "peak_torque");
	S.PeakTorqueRpm = Num(G, "peak_torque_rpm");
	S.TorqueRpm.clear();
	S.TorqueNm.clear();
	if (const Json* R = G.Get("torque_rpm"))
		for (const Json& V : R->A) S.TorqueRpm.push_back(V.N);
	if (const Json* N = G.Get("torque_nm"))
		for (const Json& V : N->A) S.TorqueNm.push_back(V.N);
	TorqueSim::BikeSim Sim;
	Sim.Setup(S);
	Sim.Ray = FlatGround;
	Sim.Place(V3(0.0, 0.0, 0.0), 0.0);
	const double Dt = 1.0 / 120.0;
	for (int I = 0; I < 240; ++I) Sim.Step(Dt);
	const bool Settled = !Sim.IsCrashed && Sim.Wheels[0].Contact && Sim.Wheels[1].Contact && std::fabs(Sim.LeanAngle) < 0.05;
	const double Load = Sim.Wheels[0].Load + Sim.Wheels[1].Load;
	Sim.Throttle = 0.5;
	double WorstLean = 0.0;
	for (int I = 0; I < 1200 && !Sim.IsCrashed; ++I)
	{
		Sim.Step(Dt);
		WorstLean = std::max(WorstLean, std::fabs(Sim.LeanAngle));
	}
	char Line[256];
	std::snprintf(Line, sizeof Line, "%s rides 10 s: %.1f m/s, gear %d, %.0f rpm, worst lean %.1f deg, wheels carry %.0f of %.0f N%s", Style.c_str(), Sim.Speed,
		Sim.Gear, Sim.Rpm, WorstLean * 57.2958, Load, S.TotalMass * 9.81, Sim.IsCrashed ? ", crashed" : "");
	Expect(Settled && !Sim.IsCrashed && std::fabs(Load - S.TotalMass * 9.81) < 0.08 * S.TotalMass * 9.81 && Sim.Speed > 10.0 && WorstLean < 0.1 &&
		Sim.Wheels[0].Spin != 0.0 && Sim.Wheels[1].Spin != 0.0, Line);
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string PartsText, LayoutText, RefText, Error;
	PartsDb Db;
	BikeLayout Lay;
	Json Ref;
	const bool Loaded = ReadFile(R + "/Data/Parts/parts.json", PartsText) && ReadFile(R + "/Data/Fitment/layout.json", LayoutText) &&
		Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error);
	Expect(Loaded, "the parts and layout load " + Error);
	const bool Have = ReadFile(R + "/SimTest/data/godot_spec.json", RefText) && Json::Parse(RefText, Ref, &Error);
	Expect(Have, "the Godot riding numbers load (SimTest/data/godot_spec.json)");
	if (!Loaded || !Have) return 1;
	// Godot's numbers are for Godot's parts: the parts this repo's overlay took off sale
	// (Data/Parts/Redrawn/godot_removed.json, Tools/parts_overlay.py) are put back for the comparisons.
	PartsDb GDb;
	BikeLayout GLay;
	{
		Json PartsJson, Removed;
		std::string RemovedText, GPartsText = PartsText;
		if (ReadFile(R + "/Data/Parts/Redrawn/godot_removed.json", RemovedText) && Json::Parse(RemovedText, Removed, &Error)
			&& Json::Parse(PartsText, PartsJson, &Error))
		{
			for (const auto& [Kind, Parts] : Removed.O)
				if (Json* Into = PartsJson.GetMutable(Kind))
					for (const auto& [Id, P] : Parts.O) Into->Set(Id, P);
			GPartsText = PartsJson.Dump(0);
		}
		Expect(GDb.LoadFromText(GPartsText, Error) && GLay.Load(LayoutText, GDb, Error), "Godot's parts load " + Error);
	}
	int Bikes = 0;
	for (const char* Group : {"styles", "sweep"})
		if (const Json* G = Ref.Get(Group))
			for (const auto& [Name, Spec] : G->O)
			{
				Compare(GLay, std::string(Group) + "/" + Name, Spec);
				++Bikes;
			}
	// The drawn bike (the layout solver's) sits on the wheels the ride uses: its axles a wheelbase
	// apart, level, and its wheels the ride's size, so the meshes on the wheels and fork line up.
	for (const std::string& Style : Lay.Styles())
	{
		RideSpecInput In;
		In.Parts = Lay.ReferenceBuild(Style);
		BikeSpec S;
		if (!RideSpec(Lay, In, S)) { Expect(false, Style + " rides"); continue; }
		const Layout L = Lay.Solve(RideParts(Lay, In.Parts));
		const double Wb = (L.FrontAxle - L.RearAxle).Length();
		char Line[256];
		std::snprintf(Line, sizeof Line, "%s: drawn wheelbase %.4f m, ride's %.4f; wheels %.4f/%.4f, ride's %.4f/%.4f", Style.c_str(), Wb, S.Wheelbase,
			L.Stats.FrontRadius, L.Stats.RearRadius, S.FrontRadius, S.RearRadius);
		Expect(std::fabs(Wb - S.Wheelbase) < 1e-4 && std::fabs(L.FrontAxle.y - L.RearAxle.y) < 1e-6 &&
			std::fabs(L.Stats.FrontRadius - S.FrontRadius) < 1e-4 && std::fabs(L.Stats.RearRadius - S.RearRadius) < 1e-4, Line);
		// The swingarm drawn on the pivot the ride swings it about, the fork on the ride's rake.
		const V3 Axis = L.SteerAxis.Normalized();
		const double Rake = std::atan2(std::fabs(Axis.x), std::fabs(Axis.y)) * 180.0 / 3.14159265358979;
		std::snprintf(Line, sizeof Line, "%s: drawn swing pivot %.3f, %.3f and rake %.2f, ride's %.3f, %.3f and %.2f", Style.c_str(), L.Pivot.x, L.Pivot.y, Rake,
			-S.SwingPivot.z, S.SwingPivot.y, S.RakeDeg);
		Expect(std::fabs(L.Pivot.x + S.SwingPivot.z) < 1e-4 && std::fabs(L.Pivot.y - S.SwingPivot.y) < 1e-4 && std::fabs(Rake - S.RakeDeg) < 0.01, Line);
	}
	std::printf("\nEach style ridden on its own spec:\n");
	if (const Json* G = Ref.Get("styles"))
		for (const auto& [Name, Spec] : G->O) Ride(GLay, Name, Spec);
	std::printf("\nBikeBuild's riding numbers on %d bikes (14 styles and their swaps), against Godot:\n", Bikes);
	for (const auto& [Name, F] : Fields)
	{
		char Line[512];
		std::snprintf(Line, sizeof Line, "%-24s worst %.2g%s%s", Name.c_str(), F.Worst, F.Bad ? " at " : "", F.Bad ? F.WorstAt.c_str() : "");
		Expect(F.Bad == 0, Line);
	}
	std::printf("\n%s (%d failures)\n", Failures ? "FAILED" : "PASSED", Failures);
	return Failures ? 1 : 0;
}
