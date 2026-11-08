// LayoutTest: lays out every style's reference bike with the layout solver (Fit/BikeLayout, the port
// of the Godot game's BikeRig.solve) and checks the result hangs together, then tries engines and
// frames that should and shouldn't go together, and geometry mode's handles. Plain C++, no Unreal.
//
//     cmake -S SimTest -B build/simtest && cmake --build build/simtest && build/simtest/layout_test [--dump]
//
// --dump prints every reference bike's datums (ours: x forward, y up, z right, metres) as JSON lines.
#include "BikeLayout.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace TorqueFit;

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

std::string P3(const V3& V)
{
	char B[96];
	std::snprintf(B, sizeof(B), "[%.4f,%.4f,%.4f]", V.x, V.y, V.z);
	return B;
}

bool Same(const Layout& A, const Layout& B)
{
	auto Eq = [](const V3& X, const V3& Y) { return X.x == Y.x && X.y == Y.y && X.z == Y.z; };
	if (A.Spine.size() != B.Spine.size()) return false;
	for (size_t I = 0; I < A.Spine.size(); ++I)
		if (!Eq(A.Spine[I], B.Spine[I])) return false;
	return Eq(A.Crank, B.Crank) && Eq(A.TankNose, B.TankNose) && Eq(A.TankRear, B.TankRear) && Eq(A.SeatFront, B.SeatFront)
		&& Eq(A.Tail, B.Tail) && Eq(A.Peg, B.Peg) && A.Fit.Tier == B.Fit.Tier && A.Fit.Reason == B.Fit.Reason;
}
} // namespace

int main(int argc, char** argv)
{
	const bool Dump = argc > 1 && std::string(argv[1]) == "--dump";
	const std::string R = Root();
	std::string PartsText, LayoutText, Error;
	if (!ReadFile(R + "/Data/Parts/parts.json", PartsText) || !ReadFile(R + "/Data/Fitment/layout.json", LayoutText))
	{
		std::printf("FAIL can't read Data/Parts/parts.json or Data/Fitment/layout.json under %s\n", R.c_str());
		return 1;
	}
	PartsDb Db;
	BikeLayout Lay;
	if (!Db.LoadFromText(PartsText, Error) || !Lay.Load(LayoutText, Db, Error))
	{
		std::printf("FAIL %s\n", Error.c_str());
		return 1;
	}

	std::printf("Reference bikes (one per style):\n");
	Expect(Lay.Styles().size() == 14, "14 styles");
	for (const std::string& St : Lay.Styles())
	{
		const PartsMap B = Lay.ReferenceBuild(St);
		const Layout L = Lay.Solve(B);
		if (Dump)
			std::printf("{\"style\":\"%s\",\"laid_out_as\":\"%s\",\"family\":\"%s\",\"head\":%s,\"pivot\":%s,\"crank\":%s,\"output\":%s,"
				"\"tank_nose\":%s,\"tank_rear\":%s,\"seat_front\":%s,\"hip\":%s,\"tail\":%s,\"peg\":%s,\"grip\":%s,\"fit\":\"%s\",\"why\":\"%s\"}\n",
				St.c_str(), L.Style.c_str(), L.Family.c_str(), P3(L.Head).c_str(), P3(L.Pivot).c_str(), P3(L.Crank).c_str(),
				P3(L.Output).c_str(), P3(L.TankNose).c_str(), P3(L.TankRear).c_str(), P3(L.SeatFront).c_str(), P3(L.Hip).c_str(),
				P3(L.Tail).c_str(), P3(L.Peg).c_str(), P3(L.GripR).c_str(), L.Fit.Tier.c_str(), L.Fit.Reason.c_str());
		const std::string N = St + " (" + B.at("frame") + ", " + B.at("engine") + ")";
		Expect(L.Style == St, N + ": laid out to its own style's lines (got " + L.Style + ")");
		Expect(L.Fit.Tier == "bolts on" || St == "cruiser", N + ": its engine bolts into its frame (" + L.Fit.Tier + (L.Fit.Reason.empty() ? "" : ": " + L.Fit.Reason) + ")");
		const double Ahead = L.Output.x - L.Pivot.x;
		Expect(Ahead > 0.0 && Ahead < 0.2, N + ": gearbox output just ahead of the pivot");
		Expect(L.TankNose.x < L.Head.x && L.TankNose.x > L.TankRear.x, N + ": tank runs back from behind the head");
		Expect(L.TankNose.y < L.Head.y, N + ": tank nose under the head stock");
		Expect(L.SeatFront.x <= L.TankRear.x + 0.02 || L.SeatOverlap, N + ": seat starts at the tank's tail");
		Expect(L.Hip.x < L.SeatFront.x && L.Tail.x < L.Hip.x, N + ": hip on the seat, tail behind it");
		Expect(L.Peg.y < L.Hip.y && L.Peg.y > L.Ground(), N + ": pegs under the rider, off the ground");
		Expect(L.GripR.x > L.Hip.x && L.GripR.y > L.Hip.y, N + ": grips ahead of and above the hip");
		Expect(L.EngineMin.y > L.Ground() && L.EngineMax.y < L.TankRear.y + 0.3, N + ": engine between the ground and the tank");
		Expect(L.FrontClear >= 0.0, N + ": front tyre clears the engine on full bump");
		Expect(L.Spine.size() >= 10 && L.TankFloor.size() == 9, N + ": backbone and tank floor drawn");
		Expect(Same(L, Lay.Solve(B)), N + ": solves the same every time (co-op)");
		// As the Godot game's bike_rig_tests: the rider on the seat line, the sump at the ground line,
		// and every part of the stock bike bolting on (and off, where it can come off).
		const double HipU = (L.Stats.Wheelbase * 0.5 - L.Hip.x) / L.Stats.Wheelbase;
		const double RiderU = Lay.LineNumber(St, "4_seat", "rider_u", 0.66);
		const double Behind = (L.Family == "cradle" || L.Family == "softail" || L.Family == "chopper" || L.Family == "cruiser") ? 0.14 : 0.08;
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.2f (line %.2f)", HipU, RiderU);
		Expect(HipU >= RiderU - 0.03 && HipU <= RiderU + Behind, N + ": rider sits on the seat line at " + Buf);
		const double Low = L.Crank.y + L.Hard.Sump.y + L.Stats.RearRadius;
		std::snprintf(Buf, sizeof(Buf), "%.0f mm", Low * 1000.0);
		Expect(Low >= Lay.LineNumber(St, "8_ground", "clear_mm", 150.0) / 1000.0 - 0.001, N + ": sump at the ground line, " + Buf);
		std::string Off;
		for (const std::string& Slot : Lay.Slots())
		{
			const std::string Id = B.count(Slot) ? B.at(Slot) : "none";
			const PartTier T = Lay.PartFit(B, Slot, Id);
			if (T.Tier != "bolts on") Off += " " + Slot + ":" + T.Reason;
		}
		Expect(Off.empty(), N + ": every part on it bolts on" + Off);
	}

	std::printf("Engines in frames:\n");
	{
		PartsMap B = Lay.ReferenceBuild("mx");
		PartTier T = Lay.PartFit(B, "engine", "boxer_1100");
		Expect(T.Tier == "won't fit", "a boxer in an MX frame won't fit: " + T.Reason);
		T = Lay.PartFit(B, "engine", "single_250");
		Expect(T.Tier == "bolts on", "a 250 single in an MX frame bolts on");
		B = Lay.ReferenceBuild("bobber");
		Expect(Lay.Solve(B).Fit.Tier == "bolts on", "the bobber bolts together as it comes");
		B["engine"] = "inline_1200";
		Expect(Lay.Solve(B).Fit.Tier == "fabricate", "an inline 1200 in the bobber's softail needs fabrication: " + Lay.Solve(B).Fit.Reason);
		B = Lay.ReferenceBuild("bobber");
		T = Lay.PartFit(B, "engine", "four_1000");
		Expect(T.Tier == "fabricate" && T.Reason.rfind("Needs fabrication", 0) == 0 && !T.Work.empty(), "a four in a softail: " + T.Reason);
		bool Hours = true;
		for (const FabJob& J : T.Work) Hours = Hours && J.Hours > 0.0 && J.Cost > 0;
		Expect(Hours, "each fabrication job has hours and a cost");
		std::vector<std::string> Done;
		for (const FabJob& J : T.Work) Done.push_back(J.What);
		Expect(Lay.PartFit(B, "engine", "four_1000", Done).Tier == "bolts on", "fabrication already done isn't asked for again");
		B = Lay.ReferenceBuild("classic");
		T = Lay.PartFit(B, "tank", "none");
		Expect(T.Tier == "won't fit" && !T.Reason.empty(), "the tank can't come off: " + T.Reason);
		T = Lay.PartFit(B, "frame", "steel_cradle");
		Expect(T.Tier == "bolts on", "a steel cradle takes the classic's twin");
		T = Lay.PartFit(B, "saddle", "none");
		Expect(T.Tier == "won't fit", "the saddle can't come off: " + T.Reason);
		T = Lay.PartFit(B, "mirrors", "none");
		Expect(T.Tier == "bolts on", "mirrors can come off");
		T = Lay.PartFit(B, "engine", "drag_bar");
		Expect(T.Tier == "won't fit", "a handlebar isn't an engine");
	}

	std::printf("Geometry mode's handles:\n");
	{
		const PartsMap B = Lay.ReferenceBuild("cafe");
		const Layout A = Lay.Solve(B);
		LayoutAdjust Adj;
		Adj.TankUpMm = 20.0;
		Adj.PegsMm = 30.0;
		Adj.HoopUpMm = 15.0;
		const Layout C = Lay.Solve(B, Adj);
		Expect(std::fabs((C.TankNose.y - A.TankNose.y) - 0.02) < 1e-9, "tank mounts up 20 mm");
		Expect(std::fabs((A.Peg.x - C.Peg.x) - 0.03) < 1e-9, "pegs 30 mm back");
		Expect(C.Tail.y > A.Tail.y, "seat hoop raised");
		Adj.TankUpMm = 400.0;
		Expect(std::fabs((Lay.Solve(B, Adj).TankNose.y - A.TankNose.y) - 0.03) < 1e-9, "handles stay in their range (30 mm)");
		Expect(BikeLayout::AdjustRanges().size() == 4, "four handles");
	}

	std::printf("Every frame with every style's engine:\n");
	{
		int Bolts = 0, Fab = 0, No = 0;
		for (const std::string& St : Lay.Styles())
			for (const std::string& St2 : Lay.Styles())
			{
				PartsMap B = Lay.ReferenceBuild(St);
				B["engine"] = Lay.ReferenceBuild(St2).at("engine");
				const Layout L = Lay.Solve(B);
				(L.Fit.Tier == "bolts on" ? Bolts : L.Fit.Tier == "fabricate" ? Fab : No)++;
				const bool Finite = std::isfinite(L.Crank.y) && std::isfinite(L.TankNose.y) && std::isfinite(L.Tail.y) && std::isfinite(L.Peg.x);
				if (!Finite) Expect(false, St + " frame with " + St2 + " engine: finite layout");
			}
		std::printf("  %d bolt on, %d need fabrication, %d won't fit (of 196)\n", Bolts, Fab, No);
		Expect(Bolts > 0 && Fab > 0 && No > 0, "all three tiers turn up");
	}

	std::printf(Failures ? "\n%d FAILED\n" : "\nall ok\n", Failures);
	return Failures ? 1 : 0;
}
