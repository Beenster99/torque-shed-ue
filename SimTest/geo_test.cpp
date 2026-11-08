// GeoTest: geometry mode's fit (Fit/BikeGeometry, the port of the Godot game's GeoGizmo and
// BikeModel.apply_geometry). Its maths is checked case by case against the real game
// (SimTest/data/godot_geo.json, dumped from Godot by Tools/make_godot_reference.py --geo), then the
// finer control added on top, the text co-op sends, and a fit applied to every reference bike.
// Plain C++, no Unreal.
//
//     cmake -S SimTest -B build/simtest && cmake --build build/simtest && build/simtest/geo_test
#include "BikeGeometry.h"
#include "BikeLayout.h"
#include "BikeParts.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>
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

V3 Vec(const Json* J) { return J && J->A.size() == 3 ? V3(J->A[0].N, J->A[1].N, J->A[2].N) : V3(1e9, 1e9, 1e9); }
double Off(const V3& A, const V3& B) { return std::max(std::fabs(A.x - B.x), std::max(std::fabs(A.y - B.y), std::fabs(A.z - B.z))); }
double OffB(const GAffine& A, const GAffine& B) { return std::max(Off(A.X, B.X), std::max(Off(A.Y, B.Y), Off(A.Z, B.Z))); }
GAffine Basis(const Json* J)
{
	GAffine B;
	if (J && J->A.size() == 3)
	{
		B.X = Vec(&J->A[0]);
		B.Y = Vec(&J->A[1]);
		B.Z = Vec(&J->A[2]);
	}
	return B;
}
GeoOffset OffOf(const Json* J)
{
	GeoOffset O;
	if (J)
	{
		if (J->Get("p")) O.P = Vec(J->Get("p"));
		if (J->Get("r")) O.R = Vec(J->Get("r"));
		if (J->Get("s")) O.S = Vec(J->Get("s"));
	}
	return O;
}
// Two offsets the same, Godot's in float: p and s to a hair, and the turns as the same rotation.
bool SameOff(const GeoOffset& A, const GeoOffset& B, double* Worst = nullptr)
{
	const double D = std::max(std::max(Off(A.P, B.P), Off(A.S, B.S)), OffB(Geo::RotationOf(A), Geo::RotationOf(B)));
	if (Worst) *Worst = std::max(*Worst, D);
	return D < 2e-5;
}
GeoSnap SnapOf(const Json& C) { return C.Flag("snap") ? GeoSnap::Coarse : GeoSnap::Off; }
} // namespace

int main()
{
	const std::string R = Root();
	std::string Text, Error;
	Json Ref;
	std::printf("Against the original game (SimTest/data/godot_geo.json, dumped from Godot):\n");
	const bool Have = ReadFile(R + "/SimTest/data/godot_geo.json", Text) && Json::Parse(Text, Ref, &Error);
	Expect(Have, "the Godot geometry-mode cases load");
	if (Have)
	{
		{
			const Json* S = Ref.Get("slots");
			bool Same = S && S->A.size() == Geo::Slots().size();
			for (size_t I = 0; Same && I < S->A.size(); ++I) Same = S->A[I].S == Geo::Slots()[I];
			Expect(Same, "the slots a fit can move are PartsDB.SLOTS");
		}
		int Bad = 0, N = 0;
		double Worst = 0.0;
		for (const Json& C : Ref.Get("euler")->A)
		{
			const V3 Deg = Vec(C.Get("deg"));
			const GAffine B = Geo::FromEuler(Deg * (3.141592653589793 / 180.0));
			const GAffine Back = Geo::FromEuler(Geo::EulerOf(Basis(C.Get("basis"))));
			const double D = std::max(OffB(B, Basis(C.Get("basis"))), OffB(Back, Basis(C.Get("basis"))));
			Worst = std::max(Worst, D);
			// (Away from the locked ends, the angles read back as Godot reads them, too.)
			const bool Locked = std::fabs(std::fabs(Deg.x) - 90.0) < 0.01;
			const bool Angles = Locked || Off(Geo::EulerOf(Basis(C.Get("basis"))) * (180.0 / 3.141592653589793), Vec(C.Get("back"))) < 1e-3;
			Bad += D > 2e-6 || !Angles;
			++N;
		}
		Expect(Bad == 0, std::to_string(N - Bad) + " of " + std::to_string(N) + " turns as Basis.from_euler and get_euler (YXZ) work them out, worst " + std::to_string(Worst));
		auto Run = [&](const char* Key, const char* What, auto&& Do) {
			int B = 0, Count = 0;
			double W = 0.0;
			for (const Json& C : Ref.Get(Key)->A)
			{
				B += !SameOff(Do(C), OffOf(C.Get("out")), &W);
				++Count;
			}
			Expect(B == 0, std::to_string(Count - B) + " of " + std::to_string(Count) + " " + What + ", worst " + std::to_string(W));
		};
		Run("moved", "move-arrow drags as GeoGizmo.moved (snapped and not, clamped to 1.5 m)",
			[](const Json& C) { return Geo::Moved(OffOf(C.Get("start")), static_cast<int>(C.Num("axis")), C.Num("amount"), SnapOf(C)); });
		Run("rotated", "ring drags as GeoGizmo.rotated (on top of how it's turned)",
			[](const Json& C) { return Geo::Rotated(OffOf(C.Get("start")), static_cast<int>(C.Num("axis")), C.Num("deg"), SnapOf(C)); });
		Run("scaled", "scale-box drags as GeoGizmo.scaled (each axis and the middle box)",
			[](const Json& C) { return Geo::Scaled(OffOf(C.Get("start")), static_cast<int>(C.Num("axis")), C.Num("factor"), SnapOf(C)); });
		Bad = 0;
		N = 0;
		Worst = 0.0;
		for (const Json& C : Ref.Get("xform")->A)
		{
			const GAffine T = Geo::OffsetXform(OffOf(C.Get("off")), Vec(C.Get("pivot")));
			const double D = std::max(OffB(T, Basis(C.Get("basis"))), Off(T.O, Vec(C.Get("origin"))));
			Worst = std::max(Worst, D);
			Bad += D > 2e-5;
			++N;
		}
		Expect(Bad == 0, std::to_string(N - Bad) + " of " + std::to_string(N) + " offsets about a part's middle as GeoGizmo.offset_xform, worst " + std::to_string(Worst));
		Bad = 0;
		N = 0;
		for (const Json& C : Ref.Get("sanitize")->A)
		{
			BikeGeo In;
			for (const auto& [K, V] : C.Get("in")->O)
			{
				if (K == "rig")
				{
					In.Rig.TankUpMm = V.Num("tank_up");
					In.Rig.PegsMm = V.Num("pegs");
					In.Rig.HoopUpMm = V.Num("hoop_up");
					In.Rig.HoopDeg = V.Num("hoop_deg");
				}
				else
					In.Slots[K] = OffOf(&V);
			}
			const BikeGeo S = Geo::Sanitize(In);
			const Json* Out = C.Get("out");
			bool Ok = S.Slots.size() + (Out->Get("rig") ? 1 : 0) == Out->O.size();
			for (const auto& [K, V] : Out->O)
			{
				if (K == "rig")
					Ok = Ok && std::fabs(S.Rig.TankUpMm - V.Num("tank_up")) < 1e-6 && std::fabs(S.Rig.PegsMm - V.Num("pegs")) < 1e-6
						&& std::fabs(S.Rig.HoopUpMm - V.Num("hoop_up")) < 1e-6 && std::fabs(S.Rig.HoopDeg - V.Num("hoop_deg")) < 1e-6;
				else
					Ok = Ok && S.Slots.count(K) && Off(S.Slots.at(K).P, Vec(V.Get("p"))) < 1e-6 && Off(S.Slots.at(K).R, Vec(V.Get("r"))) < 1e-4
						&& Off(S.Slots.at(K).S, Vec(V.Get("s"))) < 1e-6;
			}
			if (!(In.Rig.TankUpMm == 0.0 && In.Rig.PegsMm == 0.0 && In.Rig.HoopUpMm == 0.0 && In.Rig.HoopDeg == 0.0) && !Out->Get("rig"))
				Ok = Ok && S.Rig.TankUpMm == 0.0 && S.Rig.PegsMm == 0.0 && S.Rig.HoopUpMm == 0.0 && S.Rig.HoopDeg == 0.0;
			Bad += !Ok;
			++N;
		}
		Expect(Bad == 0, std::to_string(N - Bad) + " of " + std::to_string(N) + " fits made safe as GeoGizmo.sanitize (unknown slots, ranges, turns wrapped, the rig's handles)");
		Bad = 0;
		N = 0;
		for (size_t I = 0; I < Ref.Get("line")->A.size(); ++I)
		{
			const Json& L = Ref.Get("line")->A[I];
			const Json& P = Ref.Get("plane")->A[I];
			const Json& A = Ref.Get("angle")->A[I];
			const double T = Geo::LineParam(Vec(L.Get("origin")), Vec(L.Get("dir")), Vec(L.Get("ro")), Vec(L.Get("rd")));
			V3 Hit;
			const bool Hits = Geo::PlanePoint(Vec(P.Get("center")), Vec(P.Get("axis")).Normalized(), Vec(P.Get("ro")), Vec(P.Get("rd")), Hit);
			const bool GodotHits = P.Get("hit") && P.Get("hit")->IsArray();
			const double Deg = Geo::AngleAbout(Vec(A.Get("a")), Vec(A.Get("b")), Vec(A.Get("axis")));
			const bool Ok = std::fabs(T - L.Num("t")) < 1e-4 * std::max(1.0, std::fabs(T)) && Hits == GodotHits && (!Hits || Off(Hit, Vec(P.Get("hit"))) < 1e-4)
				&& std::fabs(Deg - A.Num("deg")) < 1e-3;
			Bad += !Ok;
			++N;
		}
		Expect(Bad == 0, std::to_string(N - Bad) + " of " + std::to_string(N) + " mouse drags read as GeoGizmo.line_param, plane_point and angle_about");
	}

	std::printf("Finer control (new on top of Godot's):\n");
	{
		GeoOffset O;
		O = Geo::Moved(O, 0, 0.01234, GeoSnap::Fine);
		Expect(std::fabs(O.P.x - 0.012) < 1e-12, "fine snap moves in 1 mm steps (12.34 mm reads 12 mm)");
		Expect(std::fabs(Geo::Moved(GeoOffset(), 0, 0.01234, GeoSnap::Coarse).P.x - 0.01) < 1e-12, "Godot's snap is still 1 cm");
		O = Geo::Rotated(GeoOffset(), 2, 7.3, GeoSnap::Fine);
		Expect(std::fabs(O.R.z - 7.5) < 1e-9 && O.R.x == 0.0 && O.R.y == 0.0, "fine snap turns in half degrees");
		O = Geo::Scaled(GeoOffset(), Geo::Uniform, 1.0349, GeoSnap::Fine);
		Expect(std::fabs(O.S.x - 1.03) < 1e-9 && std::fabs(O.S.z - 1.03) < 1e-9, "fine snap sizes in 1% steps");
		O = Geo::Nudged(GeoOffset(), GeoHandle::Move, 1, 3, true);
		Expect(std::fabs(O.P.y - 0.003) < 1e-12, "three fine nudges up move it 3 mm");
		O = Geo::Nudged(O, GeoHandle::Move, 1, -1, false);
		Expect(std::fabs(O.P.y + 0.007) < 1e-12, "a coarse nudge down moves it 1 cm");
		O = Geo::Nudged(GeoOffset(), GeoHandle::Rotate, 0, -2, true);
		Expect(std::fabs(O.R.x + 1.0) < 1e-9, "two fine nudges turn it a degree");
		O = Geo::Nudged(GeoOffset(), GeoHandle::Scale, Geo::Uniform, 2, true);
		Expect(std::fabs(O.S.y - 1.02) < 1e-12, "two fine nudges size it 2% bigger");
		O = Geo::WithValue(GeoOffset(), GeoHandle::Move, 2, -4.5);
		Expect(std::fabs(O.P.z + 0.045) < 1e-12, "a typed -4.5 cm moves it 45 mm");
		O = Geo::WithValue(O, GeoHandle::Rotate, 1, 200.0);
		Expect(std::fabs(O.R.y + 160.0) < 1e-9, "a typed 200 degrees wraps to -160");
		O = Geo::WithValue(O, GeoHandle::Scale, 0, 1000.0);
		Expect(O.S.x == Geo::MaxScale, "a typed size is kept in range");
		// The part's own axes: turned 90 degrees about y, its x runs along the bike's -z.
		GeoOffset T = Geo::Rotated(GeoOffset(), 1, 90.0, GeoSnap::Off);
		const V3 Ax = Geo::PartAxis(T, 0);
		Expect(Off(Ax, V3(0, 0, -1)) < 1e-9, "a part turned a quarter about y has its own x along -z");
		T = Geo::Nudged(T, GeoHandle::Move, 0, 5, true, true);
		Expect(Off(T.P, V3(0, 0, -0.005)) < 1e-12, "nudged along its own x, it moves along the bike's -z");
		T = Geo::MovedAlong(GeoOffset(), V3(1, 1, 0).Normalized(), 0.0123, GeoSnap::Fine);
		Expect(std::fabs(T.P.x - 0.012 / std::sqrt(2.0)) < 1e-12 && std::fabs(T.P.y - T.P.x) < 1e-15, "a snapped move along its own axis snaps the distance along it");
	}

	std::printf("The fit as co-op sends it (text, exact):\n");
	{
		BikeGeo G;
		G.Rig.TankUpMm = 12.0;
		G.Rig.HoopDeg = -2.5;
		G.Set("handlebars", Geo::Rotated(Geo::Moved(GeoOffset(), 1, 0.0234567, GeoSnap::Off), 0, 12.3456789, GeoSnap::Off));
		G.Set("exhaust", Geo::Scaled(GeoOffset(), 2, 1.1, GeoSnap::Off));
		G.Set("tank", GeoOffset());
		const std::string Txt = Geo::ToText(G);
		BikeGeo Back;
		Expect(Geo::FromText(Txt, Back) && Back == Geo::Sanitize(G), "reads back exactly what was sent: " + Txt.substr(0, 60) + "...");
		Expect(!G.Slots.count("tank"), "a part left as dropped isn't kept");
		Expect(Geo::ToText(Back) == Txt, "and writes the same text again (every machine agrees)");
		BikeGeo Junk;
		Expect(!Geo::FromText("not json", Junk) && Junk.Empty(), "junk is no fit");
		Expect(Geo::FromText("{\"frame\": {\"p\": [9, 0, 0]}, \"bogus\": {\"p\": [0.1, 0, 0]}}", Junk) && Junk.Slots.size() == 1 && Junk.Of("frame").P.x == Geo::MaxMove,
			"a sent fit is made safe on the way in");
		Expect(Geo::ToText(BikeGeo()) == "{}", "no fit is {}");
	}

	std::printf("A fit on every reference bike (Blender and code-drawn parts):\n");
	{
		std::string PartsText, LayoutText, IndexText;
		PartsDb Db;
		BikeLayout Lay;
		Json Index;
		const bool Loaded = ReadFile(R + "/Data/Parts/parts.json", PartsText) && ReadFile(R + "/Data/Fitment/layout.json", LayoutText)
			&& ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error)
			&& Json::Parse(IndexText, Index, &Error);
		Expect(Loaded, "the parts, layout and Blender parts load");
		if (!Loaded) return 1;
		const Json* Known = Index.Get("parts");
		BikeParts Placer(Lay, *Known);
		auto Glb = [&](const std::string& Path, PartMesh& M) {
			std::string Bytes, MorphBytes, Err;
			const Json* Ent = Known->Get(Path.substr(0, Path.size() - 4));
			if (!Ent || !ReadFile(R + "/Data/Parts/Blender/" + Path, Bytes) || !M.LoadGlb(Bytes, Err)) return false;
			return !Ent->Get("morph") || (ReadFile(R + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M.LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Err));
		};
		Placer.SetMeshSource(Glb);
		int Moved = 0, Bikes = 0, Pivots = 0, Clashes = 0;
		double Slowest = 0.0;
		std::string Bad;
		for (const std::string& St : Lay.Styles())
		{
			const PartsMap B = Lay.ReferenceBuild(St);
			const Layout L = Lay.Solve(B);
			const std::vector<FittedPart> Parts = Placer.Fit(B, L);
			std::vector<PartMesh> Drawn(Parts.size());
			GeoPivots Pv;
			for (size_t I = 0; I < Parts.size(); ++I)
			{
				if (Parts[I].Made) Drawn[I] = *Parts[I].Made;
				else
				{
					PartMesh M;
					if (!Glb(Parts[I].Glb, M)) continue;
					Drawn[I] = BikeParts::Draw(M, Parts[I]);
				}
				Pv.Add(Parts[I], Drawn[I]);
			}
			++Bikes;
			// The bars up 2 cm and turned 10 degrees about the bike's x; the tank down into the frame.
			BikeGeo G;
			G.Set("handlebars", Geo::Rotated(Geo::Moved(GeoOffset(), 1, 0.02, GeoSnap::Off), 0, 10.0, GeoSnap::Off));
			Pivots += Pv.Has("handlebars") && Pv.Has("tank");
			const GAffine Xf = Pv.Xform(G, "handlebars");
			bool Ok = true;
			std::vector<PartMesh> After = Drawn;
			for (size_t I = 0; I < Parts.size(); ++I)
			{
				Pv.Bake(G, Parts[I], After[I]);
				for (size_t S = 0; S < After[I].Surfaces.size(); ++S)
				{
					const bool Bars = GeoPivots::SlotOf(Parts[I], Drawn[I].Surfaces[S]) == "handlebars";
					for (size_t V = 0; V < After[I].Surfaces[S].Pos.size(); ++V)
					{
						const V3 Want = Bars ? Xf.Apply(Drawn[I].Surfaces[S].Pos[V]) : Drawn[I].Surfaces[S].Pos[V];
						if (Off(After[I].Surfaces[S].Pos[V], Want) > 1e-12) Ok = false;
					}
				}
			}
			// The pivot stays put under a turn: the bars' middle only rises the 2 cm.
			const V3 Mid = Pv.Pivot("handlebars");
			const V3 MidAfter = ToGodot(Xf.Apply(FromGodot(Mid)));
			Ok = Ok && Off(MidAfter, Mid + V3(0, 0.02, 0)) < 1e-12;
			// As Godot's offset_xform about the same middle, in its own frame.
			const GAffine Gd = Geo::OffsetXform(G.Of("handlebars"), Mid);
			const V3 Probe = Mid + V3(0.1, -0.05, 0.2);
			Ok = Ok && Off(ToGodot(Xf.Apply(FromGodot(Probe))), Gd.Apply(Probe)) < 1e-12;
			Moved += Ok;
			if (!Ok) Bad += " " + St;
			// Gaps: how near the bars come to everything else, quick enough to show as they're dragged.
			std::vector<std::pair<const FittedPart*, const PartMesh*>> Now;
			for (size_t I = 0; I < Parts.size(); ++I) Now.push_back({&Parts[I], &After[I]});
			const auto T0 = std::chrono::steady_clock::now();
			const std::vector<GeoGap> Near = Gaps(Now, "handlebars", 0.05);
			Slowest = std::max(Slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count());
			bool Sorted = true;
			for (size_t I = 0; I < Near.size(); ++I) Sorted = Sorted && Near[I].Gap >= 0.0 && Near[I].Gap < 0.05 && (I == 0 || Near[I].Gap >= Near[I - 1].Gap) && Near[I].Slot != "handlebars";
			Clashes += Sorted;
			if (std::getenv("GEO_GAPS"))
			{
				std::printf("    %s:", St.c_str());
				for (const GeoGap& Gp : Near) std::printf(" %s %.1f", Gp.Slot.c_str(), Gp.Gap * 1000.0);
				std::printf("\n");
			}
		}
		Expect(Pivots == Bikes, "every bike has its bars' and tank's middles");
		Expect(Moved == Bikes, std::to_string(Moved) + " of " + std::to_string(Bikes) + " bikes draw a moved and turned part about its middle, everything else as it was" + Bad);
		Expect(Clashes == Bikes, std::to_string(Clashes) + " of " + std::to_string(Bikes) + " bikes list the gaps round the moved bars, nearest first");
		char Ms[64];
		std::snprintf(Ms, sizeof(Ms), "%.0f ms", Slowest * 1000.0);
		Expect(Slowest < 0.25, std::string("the gaps take at most ") + Ms + " on a whole bike");
	}

	std::printf("Gaps between two parts:\n");
	{
		// Two 10 cm boxes 5 mm apart along x (ours), the second then moved 3 mm nearer, then into the first.
		auto BoxPart = [](const std::string& Slot, double X) {
			FittedPart F;
			F.Slot = Slot;
			auto M = std::make_shared<PartMesh>();
			PartSurface S;
			S.Material = "paint";
			for (int I = 0; I < 8; ++I)
			{
				S.Pos.push_back(V3(X + ((I & 1) ? 0.1 : 0.0), (I & 2) ? 0.1 : 0.0, (I & 4) ? 0.1 : 0.0));
				S.Nrm.push_back(V3(0, 1, 0));
				S.U.push_back(0.0);
				S.V.push_back(0.0);
			}
			const unsigned Q[6][4] = {{0, 2, 4, 6}, {1, 3, 5, 7}, {0, 1, 4, 5}, {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}};
			for (const auto& F4 : Q) S.Idx.insert(S.Idx.end(), {F4[0], F4[1], F4[2], F4[1], F4[3], F4[2]});
			M->Surfaces.push_back(S);
			F.Made = M;
			return F;
		};
		const FittedPart A = BoxPart("tank", 0.0), B = BoxPart("saddle", 0.105);
		GeoPivots Pv;
		Pv.Add(A, *A.Made);
		Pv.Add(B, *B.Made);
		auto GapAfter = [&](double MoveX) {
			BikeGeo G;
			// (Ours x is Godot's -z.)
			G.Set("saddle", Geo::Moved(GeoOffset(), 2, MoveX, GeoSnap::Off));
			PartMesh Ma = *A.Made, Mb = *B.Made;
			Pv.Bake(G, B, Mb);
			const std::vector<GeoGap> Gs = Gaps({{&A, &Ma}, {&B, &Mb}}, "saddle", 0.03);
			return Gs.empty() ? 1.0 : Gs[0].Gap;
		};
		Expect(std::fabs(GapAfter(0.0) - 0.005) < 1e-9, "5 mm apart reads 5 mm");
		Expect(std::fabs(GapAfter(0.003) - 0.002) < 1e-9, "moved 3 mm nearer, it reads 2 mm");
		Expect(GapAfter(0.02) == 0.0, "moved into it, it reads 0 (touching)");
		Expect(GapAfter(-0.05) == 1.0, "moved 5 cm away, nothing's near");
	}

	std::printf(Failures == 0 ? "\nall ok\n" : "\n%d FAILED\n", Failures);
	return Failures == 0 ? 0 : 1;
}
