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

	// The Godot game's own tank shapes, parts and reference bikes, for the comparisons against it. A
	// redrawn tank (Data/Parts/Redrawn, Tools/import_blender_parts.py --redrawn) carries loft keys measured
	// off its new mesh in "keys" and the Godot shape's in "godot_keys"; the Godot parts the overlay took off
	// sale are kept in Data/Parts/Redrawn/godot_removed.json; and Godot's reference bikes are the ones it
	// dumped (SimTest/data/godot_reference.json), where the overlay gave a style a new reference part.
	PartsDb GDb;
	BikeLayout GLay;
	{
		Json PartsJson, Removed, GRef;
		std::string RemovedText, GRefText, GPartsText = PartsText;
		if (ReadFile(R + "/Data/Parts/Redrawn/godot_removed.json", RemovedText) && Json::Parse(RemovedText, Removed, &Error)
			&& Json::Parse(PartsText, PartsJson, &Error))
		{
			for (const auto& [Kind, Parts] : Removed.O)
				if (Json* Into = PartsJson.GetMutable(Kind))
					for (const auto& [Id, P] : Parts.O) Into->Set(Id, P);
			GPartsText = PartsJson.Dump(0);
		}
		Json LayJson;
		std::string GodotText = LayoutText;
		if (Json::Parse(LayoutText, LayJson, &Error))
		{
			// The rider's lines as Torque Shed has them (the port sits some styles further back, pegs lower).
			if (Json* Lines = LayJson.GetMutable("lines"))
				for (auto& Member : Lines->O)
					if (Json* Rider = Member.second.GetMutable("lines") ? Member.second.GetMutable("lines")->GetMutable("5_rider") : nullptr)
					{
						if (const Json* Gp = Rider->Get("godot_peg_mm")) Rider->Set("peg_mm", Json(*Gp));
						for (const char* Own : {"hip_back_mm", "peg_by_case", "bar_move_mm"})
							if (Rider->Get(Own))
							{
								Json Zero;
								Zero.Kind = Json::Number;
								Rider->Set(Own, Zero);
							}
					}
			if (Json* Shapes = LayJson.GetMutable("tank_shapes"))
				for (auto& Member : Shapes->O)
					if (const Json* Gk = Member.second.Get("godot_keys"))
						*Member.second.GetMutable("keys") = Json(*Gk);
			if (ReadFile(R + "/SimTest/data/godot_reference.json", GRefText) && Json::Parse(GRefText, GRef, &Error) && GRef.Get("styles"))
				if (Json* Builds = LayJson.GetMutable("reference_builds"))
					for (const auto& [St, G] : GRef.Get("styles")->O)
						if (Json* Bd = Builds->GetMutable(St))
							if (const Json* Gp = G.Get("parts"))
								for (const auto& [Slot, Id] : Gp->O)
									if (Bd->Get(Slot)) Bd->Set(Slot, Id);
			GodotText = LayJson.Dump(0);
		}
		if (!GDb.LoadFromText(GPartsText, Error) || !GLay.Load(GodotText, GDb, Error))
		{
			std::printf("FAIL %s\n", Error.c_str());
			return 1;
		}
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
		// (plus the port's own move back along the seat, "hip_back_mm")
		const double RiderU = Lay.LineNumber(St, "4_seat", "rider_u", 0.66) + Lay.LineNumber(St, "5_rider", "hip_back_mm", 0.0) / 1000.0 / L.Stats.Wheelbase;
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
		// Each style's reference bike, and the same bike with each tank drawn new for this game (a tank
		// shape with a "like": the part of the same id) that suits the style.
		std::vector<std::pair<std::string, PartsMap>> Bikes;
		for (const std::string& St : Lay.Styles())
		{
			Bikes.push_back({St, Lay.ReferenceBuild(St)});
			if (const Json* Shapes = Lay.Tables().Get("tank_shapes"))
				for (const auto& [Name, Sh] : Shapes->O)
				{
					if (!Sh.Get("like") || !Lay.Part(Name)) continue;
					const std::vector<std::string> Suits = Lay.StylesOf(Name);
					if (std::find(Suits.begin(), Suits.end(), St) == Suits.end()) continue;
					PartsMap B = Lay.ReferenceBuild(St);
					if (B["tank"] == Name) continue;
					B["tank"] = Name;
					Bikes.push_back({St + " with " + Name, B});
				}
		}
		for (const auto& [St, B] : Bikes)
		{
			const std::string Style = St.substr(0, St.find(' '));
			const Layout L = Lay.Solve(B);
			const Bodywork W = Body.Fit(B, L);
			std::map<std::string, std::vector<V3>> Pts;   // Godot frame, as the Godot test measures
			bool Loaded = true;
			const Json* TankEnt = nullptr;
			for (const FittedPart& P : W.Parts)
			{
				PartMesh M;
				std::string Bytes, MorphBytes;
				const Json* Ent = Index.Get("parts") ? Index.Get("parts")->Get(P.Glb.substr(0, P.Glb.size() - 4)) : nullptr;
				if (P.Slot == "tank") TankEnt = Ent;
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
			// A redrawn tank's tunnel is drawn arched over the frame, not flat along it like Godot's: it has
			// to rest on the floor line at its lowest point there (neither sunk into the frame nor floating).
			if (TankEnt && TankEnt->Flag("redrawn"))
			{
				double Rest = 1e9;
				for (const auto& Q : Low) Rest = std::min(Rest, Q.second);
				Roof = Low.empty() ? 1e9 : std::fabs(Rest);
			}
			const V3 Hip = ToGodot(L.Hip);
			double SeatTop = -1e9, S0 = 1e9, S1 = -1e9;
			for (const V3& V : Pts["saddle"])
			{
				S0 = std::min(S0, V.z);
				S1 = std::max(S1, V.z);
				if (std::fabs(V.z - Hip.z) < 0.03 && std::fabs(V.x) < 0.04) SeatTop = std::max(SeatTop, V.y);
			}
			double TailGap = 0.0;
			if (Pts.count("tail") && Style != "mx" && Style != "supermoto")
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

	std::printf("Tank trim (the filler cap and knee pads on the tank's real surface):\n");
	{
		Json Index;
		std::string IndexText;
		ReadFile(R + "/Data/Parts/Blender/index.json", IndexText);
		Json::Parse(IndexText, Index, &Error);
		const Json* Known = Index.Get("parts");
		BikeParts Placer(Lay, Known ? *Known : Index);
		auto Load = [&](const std::string& Glb, PartMesh& M) {
			std::string Bytes, MorphBytes, Err;
			const Json* Ent = Known ? Known->Get(Glb.substr(0, Glb.size() - 4)) : nullptr;
			if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + Glb, Bytes) || !M.LoadGlb(Bytes, Err)) return false;
			return !Ent->Get("morph") || (ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Err));
		};
		Placer.SetMeshSource(Load);
		std::string Sunk, PadsWrong;
		int Tanks = 0, WithPads = 0;
		for (const std::string& Id : Db.Ids("tank"))
		{
			const Json* Part = Db.Find("tank", Id);
			const Json* Styles = Part ? Part->Get("styles") : nullptr;
			if (!Styles || Styles->A.empty()) continue;
			PartsMap B = Lay.ReferenceBuild(Styles->A[0].S);
			B["tank"] = Id;
			const Layout L = Lay.Solve(B);
			std::vector<V3> Shell;                    // the tank as drawn (ours: x forward, y up, z right)
			std::vector<std::vector<V3>> Chrome, Rubber;
			for (const FittedPart& P : Placer.Fit(B, L))
			{
				if (P.Slot != "tank") continue;
				if (!P.Code)
				{
					PartMesh M;
					if (!Load(P.Glb, M)) continue;
					for (const PartSurface& S : BikeParts::Draw(M, P).Surfaces) Shell.insert(Shell.end(), S.Pos.begin(), S.Pos.end());
					continue;
				}
				for (const PartSurface& S : P.Made->Surfaces)
				{
					if (S.Material == "rubber") Rubber.push_back(S.Pos);
					else if (S.Material == "chrome") Chrome.push_back(S.Pos);
				}
			}
			if (Shell.empty()) continue;
			++Tanks;
			double Top = -1e9, HalfW = 0.0;
			for (const V3& V : Shell) { Top = std::max(Top, V.y); HalfW = std::max(HalfW, std::fabs(V.z)); }
			// The cap: the chrome on the tank's top (not a badge on its side). Somewhere it must stand at least
			// 3 mm over the tank's surface straight under it.
			std::vector<V3> Near;   // (the shell round the piece being measured)
			auto SurfaceUnder = [&](const V3& At) {
				double Y = -1e9;
				for (const V3& V : Near)
					if (std::fabs(V.x - At.x) < 0.008 && std::fabs(V.z - At.z) < 0.008) Y = std::max(Y, V.y);
				return Y;
			};
			bool Capped = false;
			for (const std::vector<V3>& Piece : Chrome)
			{
				V3 Mid;
				double Rad = 0.0;
				for (const V3& V : Piece) Mid = Mid + V;
				Mid = Mid * (1.0 / static_cast<double>(Piece.size()));
				for (const V3& V : Piece) Rad = std::max(Rad, std::hypot(V.x - Mid.x, V.z - Mid.z));
				if (Rad < 0.02) continue;   // (its screws and hinge tab)
				double Local = 0.0;
				for (const V3& V : Shell)
					if (std::fabs(V.x - Mid.x) < 0.01) Local = std::max(Local, std::fabs(V.z));
				if (std::fabs(Mid.z) > Local * 0.6) continue;   // a badge on the flank
				Near.clear();
				for (const V3& V : Shell)
					if (std::hypot(V.x - Mid.x, V.z - Mid.z) < Rad + 0.02) Near.push_back(V);
				double Proud = -1e9;
				for (const V3& V : Piece)
				{
					const double Y = SurfaceUnder(V);
					if (Y > -1e8) Proud = std::max(Proud, V.y - Y);
				}
				if (Proud < -1e8) continue;   // (off the tank: nothing under it)
				Capped = true;
				if (Proud < 0.003)
				{
					char W[96];
					std::snprintf(W, sizeof(W), " %s(%.1fmm)", Id.c_str(), Proud * 1000.0);
					Sunk += W;
				}
			}
			if (!Capped) Sunk += " " + Id + "(no cap)";
			// Knee pads: a pair, one on each flank, off the top and lying on the paint.
			if (!Rubber.empty())
			{
				++WithPads;
				if (Rubber.size() != 2) PadsWrong += " " + Id + "(" + std::to_string(Rubber.size()) + " pads)";
				for (const std::vector<V3>& Piece : Rubber)
				{
					V3 Mid;
					double Hi = -1e9;
					for (const V3& V : Piece) { Mid = Mid + V; Hi = std::max(Hi, V.y); }
					Mid = Mid * (1.0 / static_cast<double>(Piece.size()));
					double Gap = 1e9;
					for (const V3& V : Shell) Gap = std::min(Gap, (V - Mid).Length());
					if (std::fabs(Mid.z) < HalfW * 0.6 || Hi > Top - 0.015 || Gap > 0.012) PadsWrong += " " + Id;
				}
			}
		}
		Expect(Tanks >= 30 && Sunk.empty(), std::to_string(Tanks) + " tanks' filler caps stand proud of the tank's real top" + Sunk);
		Expect(WithPads >= 3 && PadsWrong.empty(), std::to_string(WithPads) + " tanks with knee pads, a pair on the flanks lying on the paint (none over the top)" + PadsWrong);
	}

	std::printf("Perimeter frames (redrawn as real twin-spar frames):\n");
	{
		Json Index;
		std::string IndexText;
		ReadFile(R + "/Data/Parts/Blender/index.json", IndexText);
		Json::Parse(IndexText, Index, &Error);
		const Json* Known = Index.Get("parts");
		BikeParts Placer(Lay, Known ? *Known : Index);
		auto Load = [&](const std::string& Glb, PartMesh& M) {
			std::string Bytes, MorphBytes, Err;
			const Json* Ent = Known ? Known->Get(Glb.substr(0, Glb.size() - 4)) : nullptr;
			if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + Glb, Bytes) || !M.LoadGlb(Bytes, Err)) return false;
			return !Ent->Get("morph") || (ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Err));
		};
		Placer.SetMeshSource(Load);
		std::vector<std::string> Frames;
		for (const std::string& Id : Db.Ids("frame"))
			if (Db.Text("frame", Id, "style") == "perimeter") Frames.push_back(Id);
		Expect(Frames.size() >= 3, std::to_string(Frames.size()) + " perimeter frames in the catalogue");
		for (const std::string& Base : {std::string("sport"), std::string("race")})
			for (const std::string& Frame : Frames)
				for (const bool Bare : {false, true})
				{
					PartsMap B = Lay.ReferenceBuild(Base);
					B["frame"] = Frame;
					if (Bare) B["fairing"] = "none";
					const Layout L = Lay.Solve(B);
					std::map<std::string, std::vector<V3>> Pts;   // each slot's drawn vertices (ours)
					for (const FittedPart& P : Placer.Fit(B, L))
					{
						if (P.Slot != "frame" && P.Slot != "head" && P.Slot != "saddle" && P.Slot != "air_filter") continue;
						PartMesh M;
						if (!P.Made && !Load(P.Glb, M)) continue;
						for (const PartSurface& S : BikeParts::Draw(M, P).Surfaces) Pts[P.Slot].insert(Pts[P.Slot].end(), S.Pos.begin(), S.Pos.end());
					}
					// (Half-width of a slot's vertices in a slice across the bike at X, above Y.)
					auto HalfW = [&](const std::string& Slot, double X, double Y, double Slice) {
						double W = 0.0;
						for (const V3& V : Pts[Slot])
							if (std::fabs(V.x - X) < Slice && V.y > Y) W = std::max(W, std::fabs(V.z));
						return W;
					};
					double Widest = 0.0;
					for (double X = L.Pivot.x; X < L.HeadBottom.x; X += 0.01) Widest = std::max(Widest, HalfW("frame", X, L.Pivot.y + 0.1, 0.005));
					const double NearHead = HalfW("frame", L.HeadBottom.x - 0.1, L.Pivot.y + 0.1, 0.005);
					// Into the top end or the airbox: frame vertices inside that slot's own slice, side to side
					// and up and down.
					auto Into = [&](const std::string& Slot) {
						int N = 0;
						for (const V3& V : Pts["frame"])
						{
							double Lo = 1e9, Hi = -1e9, Zlo = 1e9, Zhi = -1e9;
							for (const V3& H : Pts[Slot])
								if (std::fabs(H.x - V.x) < 0.005 && (H.z > 0.0) == (V.z > 0.0))
								{
									Lo = std::min(Lo, H.y); Hi = std::max(Hi, H.y);
									Zlo = std::min(Zlo, H.z); Zhi = std::max(Zhi, H.z);
								}
							if (V.y > Lo + 0.005 && V.y < Hi - 0.005 && V.z > Zlo + 0.005 && V.z < Zhi - 0.005) ++N;
						}
						return N;
					};
					// (Inside a full race fairing the frame keeps Godot's slim width, which the fairing is shaped
					// round, so only a bike without one is held to clearing its top end.)
					const int IntoHead = Into("head"), IntoAirbox = Into("air_filter");
					const bool Shown = Bare || !B.count("fairing") || Db.Text("fairing", B.at("fairing"), "style") != "race";
					// The subframe's rails under the seat: no wider than the saddle once in behind the air filter (its
					// struts up from the pivot plates may stand out).
					const double SeatW = HalfW("saddle", L.SeatFront.x - 0.3, -1.0, 0.03);
					const double RailW = HalfW("frame", L.SeatFront.x - 0.3, L.SeatFront.y - 0.06, 0.03);
					char Buf[260];
					std::snprintf(Buf, sizeof(Buf), " (%s, %s%s): widest %.0f mm, %.0f mm 100 mm behind the head stock, %d points into the head, %d into the airbox, rails %.0f mm under a %.0f mm seat",
						Frame.c_str(), Base.c_str(), Bare ? ", no fairing" : "", Widest * 1000.0, NearHead * 1000.0, IntoHead, IntoAirbox, RailW * 1000.0, SeatW * 1000.0);
					const double GodotW = std::max(0.15, L.Hard.CaseHalfW + 0.05) + 0.022;
					Expect(NearHead <= Widest * 0.75 && Widest <= GodotW + 0.001, std::string("spars splay out gradually from the head stock, not round the bars, no wider than Godot's") + Buf);
					if (Shown) Expect(IntoHead == 0 && IntoAirbox == 0, "spars pass outside the top end and the airbox" + std::string(Buf));
					Expect(RailW > 0.0 && RailW <= SeatW + 0.015, "subframe tucked under the seat (to 15 mm of its edge)" + std::string(Buf));
				}
	}

	std::printf("Against the original game (SimTest/data/godot_reference.json, dumped from Godot):\n");
	{
		Json Ref, Index;
		std::string RefText, IndexText;
		const bool Have = ReadFile(R + "/SimTest/data/godot_reference.json", RefText) && Json::Parse(RefText, Ref, &Error)
			&& ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Json::Parse(IndexText, Index, &Error);
		Expect(Have && Ref.Get("styles"), "the Godot reference and the parts index load");
		const Json* Known = Have ? Index.Get("parts") : nullptr;
		BikeParts Placer(GLay, Known ? *Known : Index);
		// Parts redrawn for this game (the index's "redrawn": Data/Parts/Redrawn): Godot has no mesh of
		// theirs to match, so their slots sit these comparisons out. (The bodywork rules above still hold a
		// redrawn tank to its frame.)
		std::set<std::string> RedrawnSlots;
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
			RedrawnSlots.clear();
			// (A perimeter frame is redrawn for this game as a real twin-spar frame, so its slot sits these
			// comparisons out too; "Perimeter frames" below holds it to the bike instead.)
			const bool Perimeter = B.count("frame") && GDb.Find("frame", B.at("frame"))
				&& GDb.Find("frame", B.at("frame"))->Str("style") == "perimeter";
			for (const FittedPart& P : Placer.Fit(B, L))
			{
				if (Perimeter && P.Slot == "frame") { RedrawnSlots.insert("frame"); continue; }
				PartMesh M;
				std::string Bytes, MorphBytes;
				const Json* Ent = Known ? Known->Get(P.Glb.substr(0, P.Glb.size() - 4)) : nullptr;
				if (Ent && Ent->Flag("redrawn")) { RedrawnSlots.insert(P.Slot); continue; }
				if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) || !M.LoadGlb(Bytes, Error)) continue;
				if (Ent->Get("morph") && (!ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) || !M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Error))) continue;
				if (P.Code)
				{
					// A shape Godot draws in code: no glb tag there, so it counts toward its slot's whole box
					// ("+" + slot) and its own name's ("@" + slot + "/" + name).
					// (Godot's reference keeps the last mesh of a name in the slot, as here.)
					const std::string Key = P.Name.empty() ? "" : "@" + P.Slot + "/" + P.Name;
					if (!Key.empty()) Ours[Key] = Box();
					for (const PartSurface& S : P.Made->Surfaces)
					{
						for (const V3& V : S.Pos)
						{
							Ours["+" + P.Slot].Add(ToGodot(V));
							if (!Key.empty()) Ours[Key].Add(ToGodot(V));
						}
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
						Ours["+" + Slot].Add(ToGodot(V));
						Ours[Slot + "/" + S.Material].Add(ToGodot(V));   // (the saddle's pad is its leather)
						if (P.Glb.rfind("tail/cowl/", 0) == 0) Ours["tail cowl"].Add(ToGodot(V));
						Tags[Slot][S.Material].Add(ToGodot(V));
					}
					++Tags[Slot][S.Material].Meshes;   // (Godot's: one mesh a surface of each piece)
					// (What a full fairing hides: the whole part, or its surfaces of those tags.)
					if (P.Hidden || std::find(P.HiddenTags.begin(), P.HiddenTags.end(), S.Material) != P.HiddenTags.end())
					{
						for (const V3& V : S.Pos) Ours["hidden:" + Slot + "/" + S.Material].Add(ToGodot(V));
						++Ours["hidden:" + Slot + "/" + S.Material].Meshes;
					}
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
				if (RedrawnSlots.count(Slot)) continue;
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
		// What a full fairing hides, slot by slot, and the service caps on its panels (Gc: Godot's
		// "covered", slot -> {hidden: box with its meshes, cap: box}), against ours. Returns what's off.
		auto CoverCheck = [&](const std::map<std::string, Box>& Ours, const Json* Gc) {
			// (Blender surfaces, slot by slot and tag by tag: ours "hidden:slot/tag".)
			std::string Wrong;
			auto Near = [&](const Box& Bx, const Json* Gb) { return std::max(Off(Bx.Lo, Vec(Gb->Get("min"))), Off(Bx.Hi, Vec(Gb->Get("max")))); };
			for (const auto& [Key, Bx] : Ours)
			{
				if (Key.rfind("hidden:", 0) != 0) continue;
				const std::string What = Key.substr(7), Slot = What.substr(0, What.find('/')), Tag = What.substr(What.find('/') + 1);
				const Json* Gs = Gc ? Gc->Get(Slot) : nullptr;
				const Json* Gh = Gs && Gs->Get("hidden") ? Gs->Get("hidden")->Get(Tag) : nullptr;
				if (!Gh) { Wrong += " " + What + " hidden (Godot shows it)"; continue; }
				const double D = Near(Bx, Gh);
				if (D >= 5e-4) Wrong += " " + What + " hidden " + std::to_string(static_cast<int>(D * 1000.0 + 0.5)) + "mm";
				if (static_cast<int>(Gh->Num("meshes", 0.0)) != Bx.Meshes)
					Wrong += " " + What + " hidden " + std::to_string(Bx.Meshes) + " (Godot " + std::to_string(static_cast<int>(Gh->Num("meshes", 0.0))) + ")";
			}
			if (Gc)
				for (const auto& [Slot, Gs] : Gc->O)
				{
					if (const Json* Gh = Gs.Get("hidden"))
						for (const auto& [Tag, Gb] : Gh->O)
							if (!Ours.count("hidden:" + Slot + "/" + Tag)) Wrong += " " + Slot + "/" + Tag + " shown (Godot hides it)";
					if (const Json* Cap = Gs.Get("cap"))
					{
						auto It = Ours.find("@" + Slot + "/ServiceCap");
						const double D = It != Ours.end() ? Near(It->second, Cap) : 1e9;
						if (D >= 5e-4) Wrong += " " + Slot + " service cap " + (It != Ours.end() ? std::to_string(static_cast<int>(D * 1000.0 + 0.5)) + "mm" : "missing");
					}
				}
			return Wrong;
		};
		// Every slot is checked whole against Godot's (Blender and code meshes) but these, whose
		// code-drawn pieces aren't all ported yet.
		// The tank's trim is left out: its filler cap and knee pads are laid on the tank's drawn surface,
		// standing proud of it ("Tank trim" below), where Godot sets them on the loft's sections.
		const std::set<std::string> NotWhole = {"tank"};
		auto IsWhole = [&](const std::string& Slot) { return NotWhole.count(Slot) == 0; };
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
				// (A slot with nothing of ours to measure, such as one sitting the comparisons out, is left
				// to the slot-by-slot check, which says when one is missing.)
				auto Mine = Ours.find("+" + Slot);
				const double D = Mine != Ours.end() ? Near(Mine->second, Gs) : 0.0;
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
		for (const std::string& St : GLay.Styles())
		{
			const Json* G = Have && Ref.Get("styles") ? Ref.Get("styles")->Get(St) : nullptr;
			if (!G) { Expect(false, St + ": in the Godot reference"); continue; }
			// The same parts as Godot's reference bike.
			PartsMap B = GLay.ReferenceBuild(St);
			bool SameParts = true;
			for (const auto& [Slot, Id] : B)
				if (const Json* Gp = G->Get("parts")->Get(Slot)) SameParts = SameParts && Gp->S == Id;
			const Layout L = GLay.Solve(B);
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
				if (!Gb || RedrawnSlots.count(Slot)) continue;
				if (!Ours.count(Slot)) { MeshOff = 1e9; MeshWorst = Slot + " missing"; continue; }
				++Compared;
				const double D = std::max(Off(Ours[Slot].Lo, Vec(Gb->Get("min"))), Off(Ours[Slot].Hi, Vec(Gb->Get("max"))));
				if (D > MeshOff) { MeshOff = D; MeshWorst = Slot; }
			}
			char Buf[220];
			std::snprintf(Buf, sizeof(Buf), ": same parts, datums within %.2f mm (%s), tier %s, %d meshes within %.2f mm (%s)",
				RigOff * 1000.0, Worst.c_str(), L.Fit.Tier.c_str(), Compared, MeshOff * 1000.0, MeshWorst.c_str());
			Expect(SameParts && RigOff < 1e-4 && TierOk && Compared >= (RedrawnSlots.count("tank") ? 3 : 4) && MeshOff < 5e-4, St + Buf);
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
			std::string CodeWrong;
			for (const auto& [Slot, Gs] : GSlots->O) CodeWrong += CodeCheck(Ours, Slot, &Gs, IsWhole(Slot));
			Expect(CodeWrong.empty(), St + ": the swingarm whole and every code-drawn shape ported as Godot draws them" + CodeWrong);
			const std::string CoverWrong = CoverCheck(Ours, G->Get("covered"));
			Expect(CoverWrong.empty(), St + ": what a full fairing hides, and its service caps, as Godot" + CoverWrong);
		}
		// Every part of the kinds ported, one at a time on its style's reference bike, as Godot draws it.
		const Json* Sweep = Have ? Ref.Get("sweep") : nullptr;
		int Builds = 0, Right = 0;
		std::string Bad;
		for (const auto& [Key, Sw] : Sweep ? Sweep->O : decltype(Ref.O)())
		{
			// (the TFT dashes are this game's own units now, not Godot's housing: checked further down)
			if (Key.rfind("dashboard/tft_", 0) == 0) continue;
			PartsMap B = GLay.ReferenceBuild(Sw.Str("base"));
			for (const auto& [Slot, Id] : Sw.Get("parts")->O) B[Slot] = Id.S;
			const Layout L = GLay.Solve(B);
			std::map<std::string, Box> Ours;
			std::map<std::string, std::map<std::string, Box>> Tags, Running;
			Collect(B, L, Ours, Tags);
			for (const auto& [Slot, ByTag] : Tags)
				if (Sw.Get("glb")->Get(Slot)) Running[Slot] = ByTag;
			int Slots = 0;
			std::string Wrong = Compare(Running, Sw.Get("glb"), false, Slots);
			for (const auto& [Slot, Gt] : Sw.Get("glb")->O)
				if (!Tags.count(Slot) && !Partial.count(Slot) && !RedrawnSlots.count(Slot)) Wrong += " " + Slot + " missing";
			if (const Json* Gs = Sw.Get("slot"))
			{
				const std::string Swept = Key.substr(0, Key.find('/'));
				Wrong += CodeCheck(Ours, Swept, Gs, IsWhole(Swept));
			}
			Wrong += CoverCheck(Ours, Sw.Get("covered"));
			if (Sw.Str("fit_tier") != L.Fit.Tier) Wrong += " tier " + L.Fit.Tier + " (Godot: " + Sw.Str("fit_tier") + ")";
			++Builds;
			if (Wrong.empty()) ++Right;
			else if (Bad.size() < 4000) Bad += "\n      " + Key + ":" + Wrong;
		}
		Expect(Builds > 300 && Right == Builds, std::to_string(Right) + " of " + std::to_string(Builds)
			+ " single-part swaps (every part of each kind ported: running gear, engines, heads, ancillaries, exhausts, bars, controls, master cylinders, clocks, lights, mirrors, indicators, front mudguards, frames, fairings, tails, stands, rearsets, guards, boards, luggage and kit) placed as Godot does" + Bad);
	}

	std::printf("The clocks' working pieces, for the ridden bike to drive (FittedPart::Gauge):\n");
	{
		Json Index;
		std::string IndexText;
		const bool Have = ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Json::Parse(IndexText, Index, &Error);
		const Json* Known = Have ? Index.Get("parts") : nullptr;
		BikeParts Placer(Lay, Known ? *Known : Index);
		// Unreal's frame from ours (x forward, y up, z right): x, z, y.
		auto Ue = [](const V3& V) { return V3(V.x, V.z, V.y); };
		auto Cross = [](const V3& A, const V3& B) { return V3(A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x); };
		int Dashes = 0;
		std::string Bad;
		for (const std::string& Id : Db.Ids("dashboard"))
		{
			++Dashes;
			PartsMap B = Lay.ReferenceBuild("naked");
			B["dashboard"] = Id;
			const std::string Style = Lay.Part(Id) ? Lay.Part(Id)->Str("style") : "";
			int Speed = 0, Revs = 0, Readout = 0, Leds = 0, Screens = 0;
			const std::string Unit = Lay.Part(Id) ? Lay.Part(Id)->Str("unit") : "";
			std::string Wrong;
			for (const FittedPart& P : Placer.Fit(B, Lay.Solve(B)))
			{
				if (P.Gauge.empty()) continue;
				if (P.Slot != "dashboard" || P.Node != "steer" || !P.Code || !P.Made) Wrong += " " + P.Gauge + " not a clock piece on the steering";
				// Out of the face towards the rider and up, and the text reading to the rider's right (a text
				// render faces its +x, Axis, its up Up, and reads along its -y: Up x Axis in Unreal).
				const V3 Axis = Ue(P.GaugeAxis), Up = Ue(P.GaugeUp);
				if (!(Axis.x < -0.3 && Axis.z > 0.3 && Up.x > 0.3 && Up.z > 0.3)) Wrong += " " + P.Gauge + " faces " + P3(Axis) + " up " + P3(Up);
				if (Cross(Up, Axis).y > -0.9) Wrong += " " + P.Gauge + " reads the wrong way";
				if (P.Gauge == "speed" || P.Gauge == "rpm")
				{
					(P.Gauge == "speed" ? Speed : Revs) += 1;
					// The needle's middle is 16 mm up the dial from its pivot (bike_model.gd).
					V3 Mid;
					size_t N = 0;
					for (const PartSurface& S : P.Made->Surfaces)
						for (const V3& Q : S.Pos) { Mid += Q; ++N; }
					Mid = Mid * (1.0 / std::max<size_t>(N, 1));
					if ((Mid - (P.GaugeAt + P.GaugeUp * 0.016)).Length() > 0.001) Wrong += " " + P.Gauge + " needle off its pivot " + P3(Mid) + " vs " + P3(P.GaugeAt);
				}
				else if (P.Gauge == "readout") ++Readout;
				else if (P.Gauge.rfind("screen_", 0) == 0)
				{
					// [TFT] The lit face: its shape and size in its name, as the part's, its picture's corners the face's.
					++Screens;
					const Json* Glass = Lay.Part(Id)->Get("glass");
					const std::string Shape = Lay.Part(Id)->Str("screen");
					const std::string Want = "screen_" + Shape + "_" + std::to_string(int(std::lround(Glass->A[0].N * 1000.0))) + "x" + std::to_string(int(std::lround(Glass->A[1].N * 1000.0)));
					if (P.Gauge != Want) Wrong += " " + P.Gauge + " should be " + Want;
					V3 Mid;
					double Far = 0.0, U0 = 1.0, U1 = 0.0, V0 = 1.0, V1 = 0.0;
					size_t N = 0;
					for (const PartSurface& S : P.Made->Surfaces)
					{
						for (const V3& Q : S.Pos) { Mid += Q; ++N; }
						for (double Uu : S.U) { U0 = std::min(U0, Uu); U1 = std::max(U1, Uu); }
						for (double Vv : S.V) { V0 = std::min(V0, Vv); V1 = std::max(V1, Vv); }
					}
					Mid = Mid * (1.0 / std::max<size_t>(N, 1));
					for (const PartSurface& S : P.Made->Surfaces)
						for (const V3& Q : S.Pos) Far = std::max(Far, (Q - Mid).Length());
					if (Shape != "round" && (Mid - P.GaugeAt).Length() > 0.001) Wrong += " " + P.Gauge + " face off its middle";
					const double Reach = Shape == "round" ? Glass->A[0].N * 0.5 : std::hypot(Glass->A[0].N, Glass->A[1].N) * 0.5;
					if (std::fabs(Far - Reach) > 0.002) Wrong += " " + P.Gauge + " face the wrong size";
					if (U0 > 0.01 || U1 < 0.99 || V0 > 0.01 || V1 < 0.99) Wrong += " " + P.Gauge + " picture doesn't fill the face";
				}
				else if (P.Gauge.rfind("led", 0) == 0) ++Leds;
			}
			const bool Dial = Style == "analog" || Style == "twin";
			const bool Tft = Style == "tft";
			const int WantLeds = Tft ? (Unit == "colour" || Unit == "touring" ? 6 : 0) : (!Dial && Style != "lcd") ? 6 : 0;
			if (Speed != (Dial ? 1 : 0) || Revs != (Style == "twin" ? 1 : 0) || Readout != (Tft ? 0 : 1) || Leds != WantLeds || Screens != (Tft ? 1 : 0))
				Wrong += " speed " + std::to_string(Speed) + ", revs " + std::to_string(Revs) + ", readout " + std::to_string(Readout) + ", shift lights " + std::to_string(Leds) + ", screens " + std::to_string(Screens);
			if (!Wrong.empty()) Bad += "\n      " + Id + " (" + Style + "):" + Wrong;
		}
		Expect(Have && Dashes >= 7 && Bad.empty(), std::to_string(Dashes) + " dashes: dials have their needles (the twin's revs too), clocks, the LCD and the race logger a readout, a TFT its lit screen of the part's shape and size, the bigger TFTs and the race logger their shift lights, all facing the rider" + Bad);
	}

	std::printf(Failures ? "\n%d FAILED\n" : "\nall ok\n", Failures);
	return Failures ? 1 : 0;
}
