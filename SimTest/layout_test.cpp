// LayoutTest: lays out every style's reference bike with the layout solver (Fit/BikeLayout, the port
// of the Godot game's BikeRig.solve) and checks the result hangs together, then tries engines and
// frames that should and shouldn't go together, and geometry mode's handles. Plain C++, no Unreal.
//
//     cmake -S SimTest -B build/simtest && cmake --build build/simtest && build/simtest/layout_test [--dump]
//
// --dump prints every reference bike's datums (ours: x forward, y up, z right, metres) as JSON lines.
#include "BikeBody.h"
#include "BikeParts.h"
#include "BikeLayout.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <vector>
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

	std::printf("Bodywork on its lines (as the Godot game's rig_body_tests):\n");
	{
		Json Index;
		std::string IndexText;
		if (!ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) || !Json::Parse(IndexText, Index, &Error))
			Expect(false, "Data/Parts/Blender/index.json loads");
		const BikeBody Body(Lay);
		for (const std::string& St : Lay.Styles())
		{
			const PartsMap B = Lay.ReferenceBuild(St);
			const Layout L = Lay.Solve(B);
			const Bodywork W = Body.Fit(B, L);
			std::map<std::string, std::vector<V3>> Pts;   // Godot frame, as the Godot test measures
			bool Loaded = true;
			for (const FittedPart& P : W.Parts)
			{
				PartMesh M;
				std::string Bytes, MorphBytes;
				const Json* Ent = Index.Get("parts") ? Index.Get("parts")->Get(P.Glb.substr(0, P.Glb.size() - 4)) : nullptr;
				Loaded = Loaded && Ent && ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) && M.LoadGlb(Bytes, Error);
				if (Loaded && Ent->Get("morph"))
					Loaded = ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Error);
				if (!Loaded) break;
				for (const PartSurface& S : M.Deformed(P.Weights, P.Warp).Surfaces)
					for (const V3& V : S.Pos) Pts[P.Slot].push_back(ToGodot(V));
			}
			Expect(Loaded && Pts.count("tank") && Pts.count("saddle"), St + ": tank and seat parts load and fit");
			if (!Loaded || !Pts.count("tank") || !Pts.count("saddle")) continue;
			std::vector<V3> Floor;
			for (const V3& P : L.TankFloor) Floor.push_back(ToGodot(P));
			auto FloorY = [&](double Z) {
				size_t K = 1;
				while (K < Floor.size() - 1 && Floor[K].z < Z) ++K;
				const V3 A = Floor[K - 1], D = Floor[K] - Floor[K - 1];
				double F = std::fabs(D.z) < 1e-5 ? 0.0 : (Z - A.z) / D.z;
				if (K > 1 && K < Floor.size() - 1) F = std::min(std::max(F, 0.0), 1.0);
				return A.y + D.y * F;
			};
			double Z0 = 1e9, Z1 = -1e9, Top = -1e9;
			for (const V3& V : Pts["tank"]) { Z0 = std::min(Z0, V.z); Z1 = std::max(Z1, V.z); Top = std::max(Top, V.y); }
			const double Span = Z1 - Z0;
			std::vector<std::pair<double, double>> Low;
			double R0 = 1e9, R1 = -1e9;
			for (const V3& V : Pts["tank"])
			{
				if (std::fabs(V.x) > 0.002 || V.z < Z0 + Span * 0.2 || V.z > Z1 - Span * 0.2) continue;
				const double On = FloorY(V.z);
				if (V.y < On + 0.03) { Low.push_back({V.z, V.y - On}); R0 = std::min(R0, V.z); R1 = std::max(R1, V.z); }
			}
			double Roof = 0.0;
			for (const auto& Q : Low)
				if (Q.first > R0 + 0.02 && Q.first < R1 - 0.02) Roof = std::max(Roof, std::fabs(Q.second));
			const V3 Hip = ToGodot(L.Hip);
			double SeatTop = -1e9, S0 = 1e9, S1 = -1e9;
			for (const V3& V : Pts["saddle"])
			{
				S0 = std::min(S0, V.z);
				S1 = std::max(S1, V.z);
				if (std::fabs(V.z - Hip.z) < 0.03 && std::fabs(V.x) < 0.04) SeatTop = std::max(SeatTop, V.y);
			}
			double TailGap = 0.0;
			if (Pts.count("tail") && St != "mx" && St != "supermoto")
			{
				double T0 = 1e9;
				for (const V3& V : Pts["tail"]) T0 = std::min(T0, V.z);
				TailGap = T0 - S1;
			}
			char Buf[200];
			std::snprintf(Buf, sizeof(Buf), ": roof %.0f mm off the floor, top %.0f mm over its line, seat %.0f mm off at the hip, gaps %.0f and %.0f mm",
				Roof * 1000.0, (Top - L.TankTopMax) * 1000.0, (SeatTop - Hip.y) * 1000.0, (S0 - Z1) * 1000.0, TailGap * 1000.0);
			Expect(Roof <= 0.006 && Top - L.TankTopMax <= 0.121 && std::fabs(SeatTop - Hip.y) <= 0.04 && S0 - Z1 <= 0.02 && TailGap <= 0.02, St + Buf);
		}
	}

	std::printf("Placed parts (engine and top end so far):\n");
	{
		Json Index;
		std::string IndexText;
		ReadFile(R + "/Data/Parts/Blender/index.json", IndexText);
		Json::Parse(IndexText, Index, &Error);
		const Json* Known = Index.Get("parts");
		const BikeParts Placer(Lay, Known ? *Known : Index);
		for (const std::string& St : Lay.Styles())
		{
			const PartsMap B = Lay.ReferenceBuild(St);
			const Layout L = Lay.Solve(B);
			std::string Bad;
			int Engine = 0, Head = 0;
			for (const FittedPart& P : Placer.Fit(B, L))
			{
				if (P.Slot != "engine" && P.Slot != "head") continue;
				(P.Slot == "engine" ? Engine : Head)++;
				PartMesh M;
				std::string Bytes;
				if (!ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) || !M.LoadGlb(Bytes, Error)) { Bad += " " + P.Glb + " won't load"; continue; }
				double Lo = 1e9, Hi = -1e9;
				for (const PartSurface& S : M.Deformed(P.Weights, P.Warp).Surfaces)
					for (const V3& V : S.Pos) { Lo = std::min(Lo, V.y); Hi = std::max(Hi, V.y); }
				// On the bike: off the ground, and (the heads) under the tank's top.
				if (Lo < L.Ground() - 0.005 || Hi > L.TankNose.y + 0.35) Bad += " " + P.Glb + " out of place";
			}
			Expect(Engine == 1 && Head >= 1 && Bad.empty(), St + ": crankcase and top end on the crank" + Bad);
		}
		// Godot hides the plain headlight inside a full race or adventure fairing, and only there.
		std::string Wrong;
		for (const std::string& St : Lay.Styles())
		{
			const PartsMap B = Lay.ReferenceBuild(St);
			const bool Inside = St == "sport" || St == "race" || St == "adventure";
			int Lamps = 0;
			for (const FittedPart& P : Placer.Fit(B, Lay.Solve(B)))
				if (P.Slot == "headlight")
				{
					++Lamps;
					if (P.Hidden != Inside) Wrong += " " + St;
				}
			if (!Lamps) Wrong += " " + St + "(no headlight)";
		}
		Expect(Wrong.empty(), "the headlight hidden inside a full fairing (sport, race, adventure), shown on the rest" + Wrong);
	}

	std::printf("Against the original game (SimTest/data/godot_reference.json, dumped from Godot):\n");
	{
		Json Ref, Index;
		std::string RefText, IndexText;
		const bool Have = ReadFile(R + "/SimTest/data/godot_reference.json", RefText) && Json::Parse(RefText, Ref, &Error)
			&& ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Json::Parse(IndexText, Index, &Error);
		Expect(Have && Ref.Get("styles"), "the Godot reference and the parts index load");
		const Json* Known = Have ? Index.Get("parts") : nullptr;
		BikeParts Placer(Lay, Known ? *Known : Index);
		Placer.SetMeshSource([&](const std::string& Glb, PartMesh& M) {
			std::string Bytes, MorphBytes, Err;
			const Json* Ent = Known ? Known->Get(Glb.substr(0, Glb.size() - 4)) : nullptr;
			if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + Glb, Bytes) || !M.LoadGlb(Bytes, Err)) return false;
			return !Ent->Get("morph") || (ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Err));
		});
		auto Vec = [](const Json* J) { return J && J->A.size() == 3 ? V3(J->A[0].N, J->A[1].N, J->A[2].N) : V3(1e9, 1e9, 1e9); };
		struct Box { V3 Lo{1e9, 1e9, 1e9}, Hi{-1e9, -1e9, -1e9}; int Meshes = 0; void Add(const V3& V) {
			Lo = V3(std::min(Lo.x, V.x), std::min(Lo.y, V.y), std::min(Lo.z, V.z)); Hi = V3(std::max(Hi.x, V.x), std::max(Hi.y, V.y), std::max(Hi.z, V.z)); } };
		auto Off = [](const V3& A, const V3& B) { return std::max(std::fabs(A.x - B.x), std::max(std::fabs(A.y - B.y), std::fabs(A.z - B.z))); };
		auto Collect = [&](const PartsMap& B, const Layout& L, std::map<std::string, Box>& Ours, std::map<std::string, std::map<std::string, Box>>& Tags) {
			std::map<std::string, int> Names;
			for (const FittedPart& P : Placer.Fit(B, L))
			{
				PartMesh M;
				std::string Bytes, MorphBytes;
				const Json* Ent = Known ? Known->Get(P.Glb.substr(0, P.Glb.size() - 4)) : nullptr;
				if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) || !M.LoadGlb(Bytes, Error)) continue;
				if (Ent->Get("morph") && (!ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) || !M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Error))) continue;
				if (P.Code)
				{
					// A shape Godot draws in code: no glb tag there, so it counts toward its slot's whole box
					// and its own name's ("@" + slot + "/" + name, numbered as Godot numbers siblings).
					std::string Key;
					if (!P.Name.empty())
					{
						const int N = ++Names[P.Slot + "/" + P.Name];
						Key = "@" + P.Slot + "/" + P.Name + (N > 1 ? std::to_string(N) : "");
					}
					for (const PartSurface& S : P.Made->Surfaces)
						for (const V3& V : S.Pos)
						{
							Ours[P.Slot].Add(ToGodot(V));
							if (!Key.empty()) Ours[Key].Add(ToGodot(V));
						}
					continue;
				}
				for (const PartSurface& S : BikeParts::Draw(M, P).Surfaces)
				{
					// (A "tag__slot" surface is that slot's piece: a head glb's barrels are the engine's.)
					const size_t Cut = S.Material.find("__");
					const std::string Slot = Cut != std::string::npos ? S.Material.substr(Cut + 2) : P.Slot;
					for (const V3& V : S.Pos)
					{
						Ours[Slot].Add(ToGodot(V));
						Ours[Slot + "/" + S.Material].Add(ToGodot(V));   // (the saddle's pad is its leather)
						if (P.Glb.rfind("tail/cowl/", 0) == 0) Ours["tail cowl"].Add(ToGodot(V));
						Tags[Slot][S.Material].Add(ToGodot(V));
					}
					++Tags[Slot][S.Material].Meshes;   // (Godot's: one mesh a surface of each piece)
				}
			}
		};
		// Slots only some of whose Blender pieces are ported: there, ours only have to lie inside Godot's.
		// (Swingarm: its swept tubes. Tail: the tray, number plate and lamp round the cowl. The frame's
		// are mostly its tubes, swept from a Blender section: not compared yet.)
		const std::set<std::string> Partial = {"swingarm"};
		auto Inside = [&](const Box& In, const Json* Gb) {
			const V3 Lo = Vec(Gb->Get("min")), Hi = Vec(Gb->Get("max"));
			return In.Lo.x >= Lo.x - 5e-4 && In.Lo.y >= Lo.y - 5e-4 && In.Lo.z >= Lo.z - 5e-4
				&& In.Hi.x <= Hi.x + 5e-4 && In.Hi.y <= Hi.y + 5e-4 && In.Hi.z <= Hi.z + 5e-4;
		};
		// Each slot's surface tags' boxes against Godot's (GSlots: slot -> {glb: tag -> box}, or with
		// Nested false slot -> tag -> box). Returns what's off.
		auto Compare = [&](const std::map<std::string, std::map<std::string, Box>>& Tags, const Json* GSlots, bool Nested, int& Slots) {
			std::string Wrong;
			for (const auto& [Slot, ByTag] : Tags)
			{
				const bool Part = Partial.count(Slot) > 0;
				const Json* Gs = GSlots->Get(Slot);
				const Json* Gt = Gs && Nested ? Gs->Get("glb") : Gs;
				++Slots;
				for (const auto& [Tag, Bx] : ByTag)
				{
					// (A fairing set's lamps Godot redraws as two code meshes, one a side: no tag.)
					if (Slot == "fairing" && Tag == "lens_white") continue;
					const Json* Gb = Gt ? Gt->Get(Tag) : nullptr;
					const double D = Gb ? std::max(Off(Bx.Lo, Vec(Gb->Get("min"))), Off(Bx.Hi, Vec(Gb->Get("max")))) : 1e9;
					if (Gb && std::getenv("LAYOUT_COUNTS") && static_cast<int>(Gb->Num("meshes", 0.0)) != Bx.Meshes)
						Wrong += " " + Slot + "/" + Tag + " meshes " + std::to_string(Bx.Meshes) + " (Godot " + std::to_string(static_cast<int>(Gb->Num("meshes", 0.0))) + ")";
					if (D >= 5e-4 && !(Part && Gb && Inside(Bx, Gb)))
					{
						char W[320];
						std::snprintf(W, sizeof(W), " %s/%s %s", Slot.c_str(), Tag.c_str(), Gb ? (std::to_string(static_cast<int>(D * 1000.0 + 0.5)) + "mm").c_str() : "not in Godot");
						Wrong += W;
						if (Gb && std::getenv("LAYOUT_BOXES"))
						{
							// (LAYOUT_BOXES=1: each box that's off, ours then Godot's, Godot frame.)
							const V3 Gl = Vec(Gb->Get("min")), Gh = Vec(Gb->Get("max"));
							std::snprintf(W, sizeof(W), " [%.3f %.3f %.3f .. %.3f %.3f %.3f vs %.3f %.3f %.3f .. %.3f %.3f %.3f]",
								Bx.Lo.x, Bx.Lo.y, Bx.Lo.z, Bx.Hi.x, Bx.Hi.y, Bx.Hi.z, Gl.x, Gl.y, Gl.z, Gh.x, Gh.y, Gh.z);
							Wrong += W;
						}
					}
				}
				if (Gt)
					for (const auto& [Tag, Gb] : Gt->O)
						if (!ByTag.count(Tag) && !Part) Wrong += " " + Slot + "/" + Tag + " missing";
			}
			return Wrong;
		};
		// The shapes Godot draws in code: each one we name against Godot's mesh of that name in the slot,
		// and with Whole the slot's whole box (Blender and code meshes) against Godot's. Returns what's off.
		auto CodeCheck = [&](const std::map<std::string, Box>& Ours, const std::string& Slot, const Json* Gs, bool Whole) {
			std::string Wrong;
			if (!Gs) return Wrong;
			auto Near = [&](const Box& Bx, const Json* Gb) { return std::max(Off(Bx.Lo, Vec(Gb->Get("min"))), Off(Bx.Hi, Vec(Gb->Get("max")))); };
			const std::string Pre = "@" + Slot + "/";
			const Json* Gn = Gs->Get("named");
			for (auto It = Ours.lower_bound(Pre); It != Ours.end() && It->first.rfind(Pre, 0) == 0; ++It)
			{
				const Json* Gb = Gn ? Gn->Get(It->first.substr(Pre.size())) : nullptr;
				const double D = Gb ? Near(It->second, Gb) : 1e9;
				if (D >= 5e-4) Wrong += " " + It->first.substr(1) + (Gb ? " " + std::to_string(static_cast<int>(D * 1000.0 + 0.5)) + "mm" : " not in Godot");
			}
			if (Whole)
			{
				auto Mine = Ours.find(Slot);
				const double D = Mine != Ours.end() ? Near(Mine->second, Gs) : 1e9;
				if (D >= 5e-4)
				{
					char W[200] = "";
					if (Mine != Ours.end() && std::getenv("LAYOUT_BOXES"))
					{
						const V3 Gl = Vec(Gs->Get("min")), Gh = Vec(Gs->Get("max"));
						const Box& Bx = Mine->second;
						std::snprintf(W, sizeof(W), " [%.3f %.3f %.3f .. %.3f %.3f %.3f vs %.3f %.3f %.3f .. %.3f %.3f %.3f]",
							Bx.Lo.x, Bx.Lo.y, Bx.Lo.z, Bx.Hi.x, Bx.Hi.y, Bx.Hi.z, Gl.x, Gl.y, Gl.z, Gh.x, Gh.y, Gh.z);
					}
					Wrong += " " + Slot + " whole " + (Mine != Ours.end() ? std::to_string(static_cast<int>(D * 1000.0 + 0.5)) + "mm" : "missing") + W;
				}
			}
			return Wrong;
		};
		for (const std::string& St : Lay.Styles())
		{
			const Json* G = Have && Ref.Get("styles") ? Ref.Get("styles")->Get(St) : nullptr;
			if (!G) { Expect(false, St + ": in the Godot reference"); continue; }
			// The same parts as Godot's reference bike.
			PartsMap B = Lay.ReferenceBuild(St);
			bool SameParts = true;
			for (const auto& [Slot, Id] : B)
				if (const Json* Gp = G->Get("parts")->Get(Slot)) SameParts = SameParts && Gp->S == Id;
			const Layout L = Lay.Solve(B);
			// The rig's datums, to a tenth of a millimetre.
			const Json* Rig = G->Get("rig");
			const std::pair<const char*, V3> Keys[] = {{"crank", L.Crank}, {"output", L.Output}, {"hip", L.Hip}, {"seat_surface", L.Hip},
				{"knee_l", L.KneeL}, {"knee_r", L.KneeR}, {"peg", L.Peg}, {"grip_r", L.GripR}, {"seat_front", L.SeatFront}, {"tail", L.Tail},
				{"stay_head", L.StayHead}, {"screen_base", L.ScreenBase}, {"tank_nose", L.TankNose}, {"tank_rear", L.TankRear}};
			double RigOff = 0.0;
			std::string Worst;
			for (const auto& [Key, Ours] : Keys)
			{
				const double D = Off(ToGodot(Ours), Vec(Rig->Get(Key)));
				if (D > RigOff) { RigOff = D; Worst = Key; }
			}
			const bool TierOk = Rig->Str("fit_tier") == L.Fit.Tier;
			// The bodywork and engine meshes' boxes, to half a millimetre.
			std::map<std::string, Box> Ours;
			std::map<std::string, std::map<std::string, Box>> Tags;
			Collect(B, L, Ours, Tags);
			const Json* GSlots = G->Get("slots");
			auto Named = [&](const char* Slot, const char* Name) -> const Json* {
				const Json* S = GSlots->Get(Slot);
				return S && S->Get("named") ? S->Get("named")->Get(Name) : nullptr;
			};
			std::pair<std::string, const Json*> Meshes[] = {{"tank", Named("tank", "TankBody")}, {"saddle/leather", Named("saddle", "SeatPad")},
				{"tail cowl", Named("tail", "TailCowl")}, {"engine", GSlots->Get("engine")}, {"head", GSlots->Get("head")}};
			double MeshOff = 0.0;
			std::string MeshWorst;
			int Compared = 0;
			for (const auto& [Slot, Gb] : Meshes)
			{
				if (!Gb) continue;
				if (!Ours.count(Slot)) { MeshOff = 1e9; MeshWorst = Slot + " missing"; continue; }
				++Compared;
				const double D = std::max(Off(Ours[Slot].Lo, Vec(Gb->Get("min"))), Off(Ours[Slot].Hi, Vec(Gb->Get("max"))));
				if (D > MeshOff) { MeshOff = D; MeshWorst = Slot; }
			}
			char Buf[220];
			std::snprintf(Buf, sizeof(Buf), ": same parts, datums within %.2f mm (%s), tier %s, %d meshes within %.2f mm (%s)",
				RigOff * 1000.0, Worst.c_str(), L.Fit.Tier.c_str(), Compared, MeshOff * 1000.0, MeshWorst.c_str());
			Expect(SameParts && RigOff < 1e-4 && TierOk && Compared >= 4 && MeshOff < 5e-4, St + Buf);
			// Every Blender part placed so far, surface tag by surface tag, against Godot's meshes of that
			// tag in the slot.
			int Slots = 0;
			std::string Wrong = Compare(Tags, GSlots, true, Slots);
			// (LAYOUT_MISSING=1: Godot's Blender surfaces we don't place yet, slot/tag.)
			if (std::getenv("LAYOUT_MISSING"))
				for (const auto& [Slot, Gs] : GSlots->O)
					if (const Json* Gt = Gs.Get("glb"))
						for (const auto& [Tag, Gb] : Gt->O)
							if (!(Tags.count(Slot) && Tags[Slot].count(Tag))) Wrong += " " + Slot + "/" + Tag + " not placed";
			Expect(Wrong.empty(), St + ": " + std::to_string(Slots) + " slots' Blender parts where Godot puts them, surface by surface" + Wrong);
			const std::string CodeWrong = CodeCheck(Ours, "swingarm", GSlots->Get("swingarm"), true) + CodeCheck(Ours, "frame", GSlots->Get("frame"), false);
			Expect(CodeWrong.empty(), St + ": the swingarm whole and its code-drawn shapes as Godot draws them" + CodeWrong);
		}
		// Every part of the kinds ported, one at a time on its style's reference bike, as Godot draws it.
		const Json* Sweep = Have ? Ref.Get("sweep") : nullptr;
		int Builds = 0, Right = 0;
		std::string Bad;
		for (const auto& [Key, Sw] : Sweep ? Sweep->O : decltype(Ref.O)())
		{
			PartsMap B = Lay.ReferenceBuild(Sw.Str("base"));
			for (const auto& [Slot, Id] : Sw.Get("parts")->O) B[Slot] = Id.S;
			const Layout L = Lay.Solve(B);
			std::map<std::string, Box> Ours;
			std::map<std::string, std::map<std::string, Box>> Tags, Running;
			Collect(B, L, Ours, Tags);
			// (A phone satnav goes where it best keeps clear of what's round the bars, and Godot counts
			// code-drawn pieces there not ported yet (mirror stalks and glasses, dial faces and needles,
			// the brake hose), so its place can differ: its pieces match Godot's when put at Godot's place.)
			const Json* Nav = Lay.Part(B.count("satnav") ? B.at("satnav") : "none");
			const bool PhoneNav = Nav && Nav->Str("style") == "phone";
			for (const auto& [Slot, ByTag] : Tags)
				if (Sw.Get("glb")->Get(Slot) && !(PhoneNav && Slot == "satnav")) Running[Slot] = ByTag;
			int Slots = 0;
			std::string Wrong = Compare(Running, Sw.Get("glb"), false, Slots);
			for (const auto& [Slot, Gt] : Sw.Get("glb")->O)
				if (!Tags.count(Slot) && !Partial.count(Slot)) Wrong += " " + Slot + " missing";
			if (const Json* Gs = PhoneNav ? nullptr : Sw.Get("slot"))
			{
				const std::string Swept = Key.substr(0, Key.find('/'));
				Wrong += CodeCheck(Ours, Swept, Gs, Swept == "swingarm");
			}
			if (Sw.Str("fit_tier") != L.Fit.Tier) Wrong += " tier " + L.Fit.Tier + " (Godot: " + Sw.Str("fit_tier") + ")";
			++Builds;
			if (Wrong.empty()) ++Right;
			else if (Bad.size() < 4000) Bad += "\n      " + Key + ":" + Wrong;
		}
		Expect(Builds > 300 && Right == Builds, std::to_string(Right) + " of " + std::to_string(Builds)
			+ " single-part swaps (every part of each kind ported: running gear, engines, heads, ancillaries, exhausts, bars, controls, master cylinders, clocks, lights, mirrors, indicators, front mudguards, frames, fairings, tails, stands, rearsets, guards, boards, luggage and kit) placed as Godot does" + Bad);
	}

	std::printf(Failures ? "\n%d FAILED\n" : "\nall ok\n", Failures);
	return Failures ? 1 : 0;
}
