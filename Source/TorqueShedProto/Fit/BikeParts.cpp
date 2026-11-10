#include "BikeParts.h"
#include "BikePartsKit.h"
#include "CodeShapes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>

namespace TorqueFit
{
namespace
{
using namespace PartsKit;

// BlenderParts' reference spacing and per-cylinder bores (blender_parts.gd).
constexpr double TopEndSpacing = 0.12;
double PerCylinderBore(const std::string& Layout)
{
	if (Layout == "2stroke") return 0.045;
	if (Layout == "vtwin" || Layout == "boxer") return 0.05;
	return 0.0;
}

constexpr double ChainPitch = 0.015875;   // m: a 5/8" chain (BikeModel.CHAIN_PITCH)
constexpr double ChainRun = 8.0 * ChainPitch;
constexpr double DrumArm = 0.076;
constexpr double DrumShoeR = 0.1;
constexpr double BoxArmReach = 0.55;
constexpr int TyreSections = 32;
// How many sections go round a tyre of each tread (Tools/make_tyre_treads.py SECTIONS: the same
// numbers). Torque Shed bends 32 of every tread round; the redrawn road treads are longer, so a
// groove can sweep from the middle to the shoulder as a real one does.
int TyreSectionsFor(const std::string& Tread)
{
	static const std::map<std::string, int> Count = {{"sport", 10}, {"touring", 12}, {"sipes", 12}, {"cruiser", 16},
		{"trail", 14}, {"blocks", 24}};
	const auto It = Count.find(Tread);
	return It != Count.end() ? It->second : TyreSections;
}
constexpr double TyreBulge = 0.434;

double SprocketRadius(int Teeth)
{
	return ChainPitch / (2.0 * std::sin(TorqueSim::Pi / std::max(static_cast<double>(Teeth), 6.0))) + 0.003;
}

// The tank as it is drawn on the bike (its glb with its keys and warp), Godot frame, so the
// filler cap and the knee pads sit on its real surface and not on the loft's rough sections (a
// redrawn tank's surface can stand well off them).
struct TankSurface
{
	struct Tri { V3 A, B, C, Na, Nb, Nc; };
	std::vector<Tri> Tris;
	bool Empty() const { return Tris.empty(); }
	// Just the triangles touching a box (Lo..Hi), to cast many rays at one place.
	TankSurface Near(const V3& Lo, const V3& Hi) const
	{
		TankSurface Part;
		for (const Tri& T : Tris)
		{
			const V3 Mn(std::min({T.A.x, T.B.x, T.C.x}), std::min({T.A.y, T.B.y, T.C.y}), std::min({T.A.z, T.B.z, T.C.z}));
			const V3 Mx(std::max({T.A.x, T.B.x, T.C.x}), std::max({T.A.y, T.B.y, T.C.y}), std::max({T.A.z, T.B.z, T.C.z}));
			if (Mx.x >= Lo.x && Mn.x <= Hi.x && Mx.y >= Lo.y && Mn.y <= Hi.y && Mx.z >= Lo.z && Mn.z <= Hi.z) Part.Tris.push_back(T);
		}
		return Part;
	}
	// The first point a ray from From along Dir (unit) meets, and the smooth normal there turned to
	// face back along the ray. False when it misses.
	bool Hit(const V3& From, const V3& Dir, V3& OutAt, V3& OutNrm) const
	{
		double Best = 1e9;
		for (const Tri& T : Tris)
		{
			const V3 E1 = T.B - T.A, E2 = T.C - T.A;
			const V3 Pv = Dir.Cross(E2);
			const double Det = E1.Dot(Pv);
			if (std::fabs(Det) < 1e-12) continue;
			const V3 Tv = From - T.A;
			const double Bu = Tv.Dot(Pv) / Det;
			if (Bu < 0.0 || Bu > 1.0) continue;
			const V3 Qv = Tv.Cross(E1);
			const double Bv = Dir.Dot(Qv) / Det;
			if (Bv < 0.0 || Bu + Bv > 1.0) continue;
			const double Along = E2.Dot(Qv) / Det;
			if (Along <= 0.0 || Along >= Best) continue;
			Best = Along;
			OutAt = From + Dir * Along;
			V3 Smooth = T.Na * (1.0 - Bu - Bv) + T.Nb * Bu + T.Nc * Bv;
			if (Smooth.Length() < 1e-9) Smooth = E1.Cross(E2);
			Smooth = Smooth.Normalized();
			OutNrm = Smooth.Dot(Dir) > 0.0 ? Smooth * -1.0 : Smooth;
		}
		return Best < 1e9;
	}
};

// A tank surface's triangles binned by where they lie across one axis (Along: 0 for rays along x, 1
// for rays along y), so a ray along that axis only tries the triangles in its cell.
struct BinnedSurface
{
	int Along = 0;
	double Cell = 0.01, Lo0 = 0.0, Lo1 = 0.0;
	int N0 = 0, N1 = 0;
	std::vector<TankSurface> Bins;
	BinnedSurface(const TankSurface& From, int InAlong, double InCell = 0.01) : Along(InAlong), Cell(InCell)
	{
		auto Axes = [this](const V3& P) { return Along == 0 ? std::make_pair(P.y, P.z) : std::make_pair(P.x, P.z); };
		double Hi0 = -1e9, Hi1 = -1e9;
		Lo0 = Lo1 = 1e9;
		for (const TankSurface::Tri& T : From.Tris)
			for (const V3* P : {&T.A, &T.B, &T.C})
			{
				const auto [U, W] = Axes(*P);
				Lo0 = std::min(Lo0, U); Hi0 = std::max(Hi0, U);
				Lo1 = std::min(Lo1, W); Hi1 = std::max(Hi1, W);
			}
		if (From.Tris.empty()) return;
		N0 = std::max(1, static_cast<int>((Hi0 - Lo0) / Cell) + 1);
		N1 = std::max(1, static_cast<int>((Hi1 - Lo1) / Cell) + 1);
		Bins.resize(static_cast<size_t>(N0) * static_cast<size_t>(N1));
		for (const TankSurface::Tri& T : From.Tris)
		{
			double A0 = 1e9, B0 = -1e9, A1 = 1e9, B1 = -1e9;
			for (const V3* P : {&T.A, &T.B, &T.C})
			{
				const auto [U, W] = Axes(*P);
				A0 = std::min(A0, U); B0 = std::max(B0, U);
				A1 = std::min(A1, W); B1 = std::max(B1, W);
			}
			for (int I = Index(A0, Lo0, N0); I <= Index(B0, Lo0, N0); ++I)
				for (int J = Index(A1, Lo1, N1); J <= Index(B1, Lo1, N1); ++J)
					Bins[static_cast<size_t>(I) * static_cast<size_t>(N1) + static_cast<size_t>(J)].Tris.push_back(T);
		}
	}
	int Index(double V, double Lo, int Count) const { return std::max(0, std::min(Count - 1, static_cast<int>((V - Lo) / Cell))); }
	bool Hit(const V3& From, const V3& Dir, V3& OutAt, V3& OutNrm) const
	{
		if (Bins.empty()) return false;
		const double U = Along == 0 ? From.y : From.x, W = From.z;
		if (U < Lo0 - Cell || W < Lo1 - Cell || U > Lo0 + (N0 + 1) * Cell || W > Lo1 + (N1 + 1) * Cell) return false;
		return Bins[static_cast<size_t>(Index(U, Lo0, N0)) * static_cast<size_t>(N1) + static_cast<size_t>(Index(W, Lo1, N1))].Hit(From, Dir, OutAt, OutNrm);
	}
};
} // namespace

GAffine GAffine::Rotation(const V3& Axis, double Angle)
{
	GAffine A;
	A.X = V3(1, 0, 0).Rotated(Axis, Angle);
	A.Y = V3(0, 1, 0).Rotated(Axis, Angle);
	A.Z = V3(0, 0, 1).Rotated(Axis, Angle);
	return A;
}

PartMesh::Warp GAffine::Warp() const
{
	const GAffine Me = *this;
	return [Me](const V3& Ours) { return FromGodot(Me.Apply(ToGodot(Ours))); };
}

std::vector<BikeParts::Cylinder> BikeParts::Cylinders(const PartsMap& Parts, const Layout& L) const
{
	std::vector<Cylinder> Out;
	const Json* Block = Lay.Part(PartIn(Parts, "engine"));
	if (!Block) return Out;
	const std::string LayoutName = Block->Str("layout", "parallel");
	const int Cyl = static_cast<int>(Block->Num("cylinders", 2.0));
	const double S = L.Hard.Scale;
	const double BoreR = TorqueSim::Clamp(Block->Num("bore", 80.0) * 0.0006, 0.022, 0.062);
	const double HeadK = L.Hard.HeadK;
	const V3 E = ToGodot(L.Crank);
	const std::string FrameStyle = Lay.Part(PartIn(Parts, "frame")) ? Lay.Part(PartIn(Parts, "frame"))->Str("style") : "";
	const bool Bay = FrameStyle == "softail" || FrameStyle == "chopper" || FrameStyle == "cruiser";
	if (LayoutName == "vtwin")
	{
		// (BikeBuild.v_angle: a V-twin's own, else 90.)
		const double V = Block->Num("v_angle", 90.0);
		const double Half = (V > 0.0 && V < 90.0) ? TorqueSim::DegToRad(V) * 0.5 : -1.0;
		for (double Dir : {-1.0, 1.0})
		{
			V3 Up = V3(0.0, std::cos(0.6), 0.6 * Dir).Normalized();
			if (Half > 0.0) Up = V3(0.0, std::cos(Half), std::sin(Half) * Dir);
			else if (Bay) Up = V3(0.0, std::cos(TorqueSim::Pi / 8.0), std::sin(TorqueSim::Pi / 8.0) * Dir);
			Up = Up * HeadK;
			Out.push_back({E + V3(0.0, 0.13 * S, 0.07 * Dir * S) + Up * 0.12 * S, Up});
		}
		return Out;
	}
	if (LayoutName == "boxer")
	{
		for (double Dir : {-1.0, 1.0})
		{
			const V3 Up = V3(Dir, 0.08, -0.05).Normalized() * HeadK;
			Out.push_back({E + V3(0.1 * Dir * S, 0.03, -0.06) + Up * 0.1 * S, Up});
		}
		return Out;
	}
	const double Lean = LayoutName == "twin" ? 0.04 : (LayoutName == "2stroke" ? 0.4 : 0.25);
	const V3 Up = V3(0.0, 1.0, -Lean).Normalized() * HeadK;
	for (int C = 0; C < Cyl; ++C)
	{
		const double Cx = (C - (Cyl - 1) * 0.5) * std::max(0.12 * S, BoreR * 2.4);
		Out.push_back({E + V3(Cx, 0.14 * S, -0.04) + Up * 0.1 * S, Up});
	}
	return Out;
}

void BikeParts::Engine(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	const std::string EngineId = PartIn(Parts, "engine");
	const Json* Block = Lay.Part(EngineId);
	if (!Block) return;
	const double S = L.Hard.Scale;
	// The crankcase: on the crank, scaled by the engine's size (BlenderParts.engine_xform).
	const std::string Case = "engine/" + EngineId + ".glb";
	if (HasGlb(Case))
	{
		FittedPart P;
		P.Slot = "engine";
		P.Glb = Case;
		P.Warp = (GAffine::At(ToGodot(L.Crank)) * GAffine::Scale(V3(S, S, S))).Warp();
		Out.push_back(P);
	}
	// The top end (BlenderParts.top_end_pieces), the Rig built look's barrels at width 1.
	const std::string Head = PartIn(Parts, "head");
	if (Head == "none") return;
	const std::string LayoutName = Block->Str("layout", "parallel");
	const std::vector<Cylinder> Cyls = Cylinders(Parts, L);
	if (Cyls.empty()) return;
	auto TopEnd = [&](const std::vector<Cylinder>& Cs, double EngS) {
		const V3 Up = Cs[0].Up;
		GAffine Turn;
		const V3 Axis = GodotUp.Cross(Up);
		if (Axis.Length() > 0.0001) Turn = GAffine::Rotation(Axis.Normalized(), AngleTo(GodotUp, Up));
		double Across = EngS;
		if (Cs.size() > 1)
			Across = std::fabs(Cs.back().Base.x - Cs[0].Base.x) / (TopEndSpacing * static_cast<double>(Cs.size() - 1));
		GAffine T = Turn * GAffine::Scale(V3(Across, Up.Length(), EngS));
		T.O = Cs[0].Base;
		return T;
	};
	const double BoreR = TorqueSim::Clamp(Block->Num("bore", 80.0) * 0.0006, 0.022, 0.062);
	const double PerBore = PerCylinderBore(LayoutName);
	if (PerBore == 0.0)
	{
		const std::string Path = "head/" + LayoutName + "/" + Head + ".glb";
		if (!HasGlb(Path)) return;
		FittedPart P;
		P.Slot = "head";
		P.Glb = Path;
		P.Warp = TopEnd(Cyls, S).Warp();
		Out.push_back(P);
		return;
	}
	std::vector<FittedPart> Pieces;
	for (size_t I = 0; I < Cyls.size(); ++I)
	{
		std::string Path = "head/" + LayoutName + "/" + Head + "_" + std::to_string(I + 1) + ".glb";
		if (!HasGlb(Path)) Path = "head/" + LayoutName + "/" + Head + ".glb";
		if (!HasGlb(Path)) return;   // (no Blender top end for this layout and head)
		const V3 Up = Cyls[I].Up;
		const double B = 1.0 * BoreR / PerBore;   // the Rig built look draws the barrels at width 1
		const GAffine T = TopEnd({Cyls[I]}, 1.0);
		GAffine Xf = T * GAffine::Scale(V3(B, 1.0, B));
		Xf.O = T.O;
		(void)Up;
		FittedPart P;
		P.Slot = "head";
		P.Glb = Path;
		P.Warp = Xf.Warp();
		Pieces.push_back(P);
	}
	Out.insert(Out.end(), Pieces.begin(), Pieces.end());
}

bool BikeParts::HasGlb(const std::string& Path) const
{
	return Path.size() > 4 && Index.Get(Path.substr(0, Path.size() - 4)) != nullptr;
}

bool BikeParts::Mount(const std::string& Path, const std::string& Name, V3& At) const
{
	const Json* E = Path.size() > 4 ? Index.Get(Path.substr(0, Path.size() - 4)) : nullptr;
	const Json* M = E && E->Get("mounts") ? E->Get("mounts")->Get(Name) : nullptr;
	const Json* A = M ? M->Get("at") : nullptr;
	if (!A || A->A.size() != 3) return false;
	At = ToGodot(V3(A->A[0].N, A->A[1].N, A->A[2].N));
	return true;
}

bool BikeParts::Put(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Path, const GAffine& Xf, const std::string& Node) const
{
	if (!HasGlb(Path)) return false;
	FittedPart P;
	P.Slot = Slot;
	P.Glb = Path;
	P.Node = Node;
	P.Warp = Xf.Warp();
	Out.push_back(P);
	return true;
}

PartMesh BikeParts::Draw(const PartMesh& Glb, const FittedPart& P)
{
	if (P.Made) return *P.Made;
	if (P.Copies.empty()) return Glb.Deformed(P.Weights, P.Warp);
	PartMesh Out;
	for (const PartMesh::Warp& W : P.Copies)
	{
		const PartMesh One = Glb.Deformed(P.Weights, W);
		if (Out.Surfaces.empty())
		{
			Out = One;
			continue;
		}
		for (size_t S = 0; S < One.Surfaces.size() && S < Out.Surfaces.size(); ++S)
		{
			PartSurface& To = Out.Surfaces[S];
			const PartSurface& From = One.Surfaces[S];
			const unsigned Base = static_cast<unsigned>(To.Pos.size());
			To.Pos.insert(To.Pos.end(), From.Pos.begin(), From.Pos.end());
			To.Nrm.insert(To.Nrm.end(), From.Nrm.begin(), From.Nrm.end());
			To.U.insert(To.U.end(), From.U.begin(), From.U.end());
			To.V.insert(To.V.end(), From.V.begin(), From.V.end());
			for (unsigned I : From.Idx) To.Idx.push_back(Base + I);
		}
	}
	return Out;
}

void BikeParts::Wheels(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (build_from: each wheel's rim, tyre and brake on its axle, at rest; BikeModel._draw_rim,
	// _blender_rim, _draw_tyre, _blender_tyre and BlenderParts.tyre_hoop.)
	for (const bool Front : {true, false})
	{
		const std::string Side = Front ? "front" : "rear";
		const std::string WheelId = PartIn(Parts, Side + "_wheel");
		const Json* W = Lay.Part(WheelId);
		const V3 Axle = ToGodot(Front ? L.FrontAxle : L.RearAxle);
		const double RimR = Front ? L.Stats.FrontRimRadius : L.Stats.RearRimRadius;
		const double RimW = RimWidthOf(W);
		if (Put(Out, Side + "_wheel", "wheel/" + WheelId + ".glb", GAffine::At(Axle), Side + "_wheel") && !Front)
			Put(Out, "rear_wheel", "wheel/fitting/cush_drive.glb", GAffine::At(Axle), "rear_wheel");
		// The tyre: its tread's section (across -1..1, bead 0 to crown 1, 1/32 of the way round along
		// +Z) bent round the hoop from just inside the rim to the crown.
		const Json* T = Lay.Part(PartIn(Parts, Side + "_tyre"));
		const std::string Tread = TreadPattern(T);
		const std::string TyrePath = "tyre/" + (Tread.empty() ? std::string("slick") : Tread) + ".glb";
		if (HasGlb(TyrePath))
		{
			const double Radius = Front ? L.Stats.FrontRadius : L.Stats.RearRadius;
			const double Width = Front ? L.Stats.FrontTyreWidth : L.Stats.RearTyreWidth;
			const double Half = Width * 0.5;
			const double Bead = std::min(RimW * 0.5 - 0.004, Half * 0.9);
			const double Base = RimR - 0.006;
			const double Span = std::max(Radius - RimR, 0.02) + 0.006;
			const int Sections = TyreSectionsFor(Tread.empty() ? std::string("slick") : Tread);
			const double Step = 2.0 * TorqueSim::Pi / Sections;
			FittedPart P;
			P.Slot = Side + "_tyre";
			P.Glb = TyrePath;
			P.Node = Side + "_wheel";
			for (int K = 0; K < Sections; ++K)
				P.Copies.push_back(GodotWarpToOurs([=](const V3& V) {
					const double A = (K + V.z) * Step;
					const double R = Base + V.y * Span;
					double X = V.x * Half;
					if (V.y < TyreBulge && std::fabs(V.x) >= 0.9)
						X = (V.x < 0.0 ? -1.0 : 1.0) * TorqueSim::Lerp(Bead, Half, TorqueSim::Clamp((std::fabs(V.x) - 0.9) / 0.1, 0.0, 1.0));
					return Axle + V3(X, std::cos(A) * R, std::sin(A) * R);
				}));
			Out.push_back(P);
			// A whitewall (or a coloured wall stripe) round both sidewalls, a hair proud of the
			// rubber at the wall's widest point.
			const Json* TLook = T ? T->Get("look") : nullptr;
			const Json* Wall = TLook ? TLook->Get("wall") : nullptr;
			if (Wall && Wall->A.size() >= 3)
			{
				CodeShapes::GodotNames Named;
				const double Mid = TorqueSim::Lerp(RimR, Radius, 0.42);
				for (double Sd : {-1.0, 1.0})
					CodeShapes::Emit(Out, Side + "_tyre", TyrePath, Side + "_wheel", Named("WallBand"), CodeShapes::Torus(Mid, (Radius - RimR) * PartNum(TLook, "wall_w", 0.18)),
						AtWith(GAffine::Rotation(V3(0.0, 0.0, 1.0), TorqueSim::Pi * 0.5) * GAffine::Scale(V3(1.0, 0.12, 1.0)), Axle + V3(Sd * (Width * 0.5 + 0.0012), 0.0, 0.0)), "paint_2");
			}
		}
		Brake(Parts, L, Front, RimR, RimW, Out);
	}
}

void BikeParts::Brake(const PartsMap& Parts, const Layout& L, bool Front, double RimR, double RimW, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_brake and _draw_drum: the disc on the wheel, the caliper and pads on the hub.)
	const std::string Side = Front ? "front" : "rear";
	const std::string Wheel = Side + "_wheel", Hub = Side + "_hub";
	const V3 Axle = ToGodot(Front ? L.FrontAxle : L.RearAxle);
	const double X = std::max(Front ? 0.068 : 0.07, RimW * 0.5 + 0.012);
	const std::string DiscId = PartIn(Parts, Side + "_disc");
	const Json* Disc = Lay.Part(DiscId);
	const double DiscR = std::min(PartNum(Disc, "diameter", 280.0) / 2000.0, RimR - 0.03);
	const std::string CaliperId = PartIn(Parts, Side + "_caliper");
	const std::string PadsId = PartIn(Parts, Side + "_pads");
	if (PartStr(Disc, "style", "round") == "drum")
	{
		const double Own = std::max(PartNum(Disc, "diameter", 180.0) / 2000.0 * 1.1, 0.07);
		const double R = TorqueSim::Clamp(PartNum(Disc, "diameter", 180.0) / 2000.0 * 1.1, 0.07, RimR - 0.05);
		const std::string Shoes = PartStr(Disc, "shoes", "sls");
		const std::vector<double> Sides = Shoes == "4ls" ? std::vector<double>{1.0, -1.0} : std::vector<double>{1.0};
		const std::string Path = "disc/" + DiscId + ".glb", Plate = "disc/" + DiscId + "_plate.glb";
		const double K = R / Own;
		if (HasGlb(Path) && HasGlb(Plate))
			for (double Sx : Sides)
			{
				Put(Out, Side + "_disc", Path, AtWith(GAffine::Scale(V3(1.0, K, K)), Axle + V3(0.018 * Sx, 0.0, 0.0)), Wheel);
				Put(Out, Side + "_disc", Plate, AtWith(GAffine::Scale(V3(Sx, K, K * (Front ? 1.0 : -1.0))), Axle + V3((X - 0.012) * Sx, 0.0, 0.0)), Hub);
			}
		// The torque stay: to the fork leg at the front, forward along the swingarm at the back.
		auto HubCode = [&](const std::string& InSlot, const std::string& Glb, const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
			CodeShapes::Emit(Out, InSlot, Glb, Hub, Name, M, GAffine::At(Axle) * Xf, Tag);
		};
		auto HubTube = [&](const std::string& InSlot, const std::string& Glb, const std::string& Name, const V3& A, const V3& B, double Rad, const std::string& Tag) {
			HubCode(InSlot, Glb, Name, CodeShapes::Tube(A, B, Rad), CodeShapes::TubeAt(A, B), Tag);
		};
		if (Front) HubTube(Side + "_disc", Plate, "", V3(X, -R * 0.6, -R * 0.2), V3(X, 0.05, 0.02), 0.007, "brushed_steel");
		else HubTube(Side + "_disc", Plate, "", V3(X, -R * 0.5, -R * 0.3), V3(X, -0.03, -0.32), 0.008, "brushed_steel");
		// The brake arm on each cam (two on a twin leading shoe, the link rod between them).
		std::vector<std::pair<double, double>> Cams = {{R * 0.6, R * 0.5}};
		if (Shoes != "sls") Cams.push_back({-R * 0.6, -R * 0.5});
		std::vector<V3> Tips;
		for (const auto& Cam : Cams)
		{
			const V3 At(X + 0.01, Cam.first, Cam.second * (Front ? 1.0 : -1.0));
			const V3 Tip = At + V3(0.0, 0.07, Front ? 0.03 : -0.03);
			Tips.push_back(Tip);
			const V3 Up = (Tip - At).Normalized();
			const double S = (Tip - At).Length() / DrumArm;
			Put(Out, Side + "_caliper", "caliper/drum_arm.glb",
				AtWith(BasisCols(GodotRight, Up, GodotRight.Cross(Up)) * GAffine::Scale(V3(S, S, S)), Axle + At), Hub);
		}
		if (Front)
		{
			// The cable's end at the front arm (BikeModel._cable_end), from the joint on the fork leg.
			const Json* ForkPart = Lay.Part(PartIn(Parts, "fork"));
			const std::string ForkStyle = PartStr(ForkPart, "style", "conventional");
			const Json* Look = ForkPart ? ForkPart->Get("look") : nullptr;
			const double ForkX = ForkStyle == "girder" ? 0.09 : (ForkStyle == "springer" || ForkStyle == "leading_link") ? 0.085 : 0.09 * PartNum(Look, "wide", 1.0);
			const V3 Axis = ToGodot(L.SteerAxis);
			const V3 J = V3(ForkX + 0.035, 0.0, 0.04) - Axis * 0.3 + Axis * L.Stats.ForkLength;
			const V3 Tip0 = V3(X + 0.01, Cams[0].first, Cams[0].second) + V3(0.0, 0.07, 0.03);
			const V3 From = (J + (Tip0 - J) * 0.5 + V3(0.02, 0.0, 0.03)) - Tip0;
			const V3 At = Tip0 + From.Normalized() * 0.012;
			Put(Out, "front_caliper", "controls/cable_end.glb", AtWith(BasisUp(From.Normalized()), Axle + At + From.Normalized() * 0.006), Hub);
			HubCode("front_caliper", "caliper/drum_arm.glb", "BrakeCable", CodeShapes::Sweep({J, J + (Tip0 - J) * 0.5 + V3(0.02, 0.0, 0.03), Tip0}, 0.0042, 6), GAffine(), "rubber");
		}
		else HubTube("rear_caliper", "caliper/drum_arm.glb", "BrakeRod", Tips[0], V3(X + 0.02, 0.0, -0.45), 0.004, "paint");   // (forward to the pedal)
		if (Tips.size() > 1) HubTube(Side + "_caliper", "caliper/drum_arm.glb", "LinkRod", Tips[0], Tips[1], 0.004, "paint");
		// The shoes, inside the drum.
		for (double Sx : Sides)
			for (double Kk : {-1.0, 1.0})
			{
				const GAffine Turn = Kk > 0.0 ? GAffine() : GAffine::Rotation(GodotRight, TorqueSim::Pi);
				Put(Out, Side + "_pads", "pads/drum_shoe.glb",
					AtWith(Turn * GAffine::Scale(V3(1.0, R / DrumShoeR, R / DrumShoeR)), Axle + V3((X - 0.022) * Sx, 0.0, 0.0)), Hub);
			}
		return;
	}
	const double OwnR = std::max(PartNum(Disc, "diameter", 280.0) / 2000.0, 0.001);
	Put(Out, Side + "_disc", "disc/" + DiscId + ".glb", AtWith(GAffine::Scale(V3(1.0, DiscR / OwnR, DiscR / OwnR)), Axle + V3(X, 0.0, 0.0)), Wheel);
	const double Pistons = PartNum(Lay.Part(CaliperId), "pistons", 2.0);
	const double CalH = 0.06 + 0.008 * static_cast<double>(static_cast<int>(Pistons));
	const V3 CalAt(X + 0.012, DiscR * 0.62, Front ? DiscR * 0.62 : -DiscR * 0.62);
	const GAffine CalBasis = GAffine::Rotation(GodotRight, Front ? -TorqueSim::Pi * 0.25 : TorqueSim::Pi * 0.25);
	Put(Out, Side + "_caliper", "caliper/" + CaliperId + ".glb", AtWith(CalBasis, Axle + CalAt), Hub);
	if (Front)
	{
		// The hose from the bottom yoke down the back of the fork leg to the caliper, and its clip.
		const Json* ForkPart = Lay.Part(PartIn(Parts, "fork"));
		const std::string ForkStyle = PartStr(ForkPart, "style", "conventional");
		const Json* Look = ForkPart ? ForkPart->Get("look") : nullptr;
		const double ForkX = ForkStyle == "girder" ? 0.09 : (ForkStyle == "springer" || ForkStyle == "leading_link") ? 0.085 : 0.09 * PartNum(Look, "wide", 1.0);
		const V3 Axis = ToGodot(L.SteerAxis);
		const V3 J = V3(ForkX + 0.035, 0.0, 0.04) - Axis * 0.3 + Axis * L.Stats.ForkLength;
		const V3 Slide = V3(ForkX + 0.035, 0.0, 0.045) + Axis * (L.Stats.ForkLength * 0.42);
		const V3 Into = CalAt + CalBasis.Apply(V3(0.0, CalH * 0.5 + 0.004, 0.0)) - CalBasis.O;
		const std::string Stand = "caliper/" + CaliperId + ".glb";
		CodeShapes::Emit(Out, "front_caliper", Stand, Hub, "BrakeHose",
			CodeShapes::Sweep({J, J + (Slide - J) * 0.5 + V3(0.012, 0.0, 0.0), Slide, Into}, 0.005, 8), GAffine::At(Axle), "rubber");
		CodeShapes::Emit(Out, "front_caliper", Stand, Hub, "", CodeShapes::Box(V3(0.012, 0.025, 0.02)), GAffine::At(Axle + Slide), "black_metal");
	}
	const V3 PadAt = CalAt + CalBasis.Y * (CalH * 0.5 + 0.012);
	Put(Out, Side + "_pads", "pads/" + PadsId + ".glb", AtWith(CalBasis, Axle + PadAt), Hub);
}

void BikeParts::Fork(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._blender_fork: the stanchions on the steering node, the sliders on the front hub.)
	const std::string Id = PartIn(Parts, "fork");
	const std::string Upper = "fork/" + Id + "_upper.glb", Lower = "fork/" + Id + "_lower.glb";
	if (!HasGlb(Upper) || !HasGlb(Lower)) return;
	const Json* F = Lay.Part(Id);
	const V3 Axis = ToGodot(L.SteerAxis);
	const double K = L.Stats.ForkLength / std::max(PartNum(F, "length", L.Stats.ForkLength), 0.01);
	const GAffine Along = BasisCols(GodotRight, Axis, GodotRight.Cross(Axis)) * GAffine::Scale(V3(1.0, K, 1.0));
	Put(Out, "fork", Upper, AtWith(Along, ToGodot(L.Head)), "steer");
	// (A leading-link fork's links are drawn level on the hub.)
	Put(Out, "fork", Lower, AtWith(PartStr(F, "style", "") == "leading_link" ? GAffine() : Along, ToGodot(L.FrontAxle)), "front_hub");
}

GAffine BikeParts::SwingarmAt(const Layout& L) const
{
	// (update_pose_from at rest: the swingarm turned about its pivot to point at the rear axle.)
	const V3 Pivot = ToGodot(L.Pivot);
	const V3 ToAxle = ToGodot(L.RearAxle) - Pivot;
	return AtWith(GAffine::Rotation(GodotRight, -std::atan2(ToAxle.y, ToAxle.z)), Pivot);
}

double BikeParts::ArmLength(const Layout& L) const
{
	return (ToGodot(L.RearAxle) - ToGodot(L.Pivot)).Length();
}

std::string BikeParts::ArmType(const PartsMap& Parts) const
{
	// (BikeModel._arm_type: the swingarm part's own, else the frame's.)
	const Json* Own = Lay.Part(PartIn(Parts, "swingarm"));
	if (Own) return Own->Str("arm", "box_section");
	const Json* Frame = Lay.Part(PartIn(Parts, "frame"));
	const Json* Geo = Frame ? Frame->Get("geo") : nullptr;
	return PartStr(Geo, "swingarm", "box");
}

void BikeParts::Swingarm(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_swingarm, _draw_own_swingarm and _arm_bridge: the Blender pieces, and the
	// shells, blocks and castings it draws in code.)
	const GAffine Arm = SwingarmAt(L);
	const double ArmLen = ArmLength(L);
	const double ArmX = std::max(0.11, std::max(L.Stats.RearTyreWidth, RimWidthOf(Lay.Part(PartIn(Parts, "rear_wheel")))) * 0.5 + 0.035);
	const std::string Kind = ArmType(Parts);
	const bool Own = Lay.Part(PartIn(Parts, "swingarm")) != nullptr;
	const std::string Slot = Own ? "swingarm" : "frame";
	auto Bridge = [&](const std::string& InSlot) {
		Put(Out, InSlot, "swingarm/bridge.glb", Arm * AtWith(GAffine::Scale(V3(ArmX * 2.0 + 0.02, 1.0, 1.0)), V3(0.0, 0.0, 0.04)), "swingarm");
	};
	// (Its round tubes and bends, swept from the frame's tube section in the swingarm's space.)
	auto ArmTube = [&](const std::string& InSlot, const V3& A, const V3& B, double R) {
		Sweep(Out, InSlot, "frame/tube/round.glb", {Arm.Apply(A), Arm.Apply(B)}, {R}, "swingarm");
	};
	auto ArmBend = [&](const std::string& InSlot, std::vector<V3> Path, double R) {
		for (V3& P : Path) P = Arm.Apply(P);
		Sweep(Out, InSlot, "frame/tube/round.glb", Path, {R}, "swingarm");
	};
	// A code-drawn shape in the swingarm's space (Xf its node's transform there), Tag its finish:
	// "paint" the arm's own colour, the steel and dark materials as their nearest tags.
	CodeShapes::GodotNames Named;
	auto ArmCode = [&](const std::string& InSlot, const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, InSlot, "swingarm/bridge.glb", "swingarm", Named(Name), M, Arm * Xf, Tag);
	};
	auto ArmShell = [&](const std::string& InSlot, const std::string& Name, const std::vector<CodeShapes::Sec>& Secs) {
		ArmCode(InSlot, Name, CodeShapes::Loft(Secs), GAffine(), "paint");
	};
	auto ArmBox = [&](const std::string& InSlot, const std::string& Name, const V3& Centre, const V3& Size, const std::string& Tag) {
		ArmCode(InSlot, Name, CodeShapes::Box(Size), GAffine::At(Centre), Tag);
	};
	// (rotation.z = PI / 2: a cylinder's axis turned from up to across the bike.)
	const GAffine Across = GAffine::Rotation(V3(0.0, 0.0, 1.0), TorqueSim::Pi * 0.5);
	auto Lerp = [](double A, double B, double F) { return A + (B - A) * F; };
	// (_to_arm: a point of the bike's at rest as the swingarm sees it.)
	const V3 PivotG = ToGodot(L.Pivot), ToAxle = ToGodot(L.RearAxle) - PivotG;
	auto ToArm = [&](const V3& P) { return GAffine::Rotation(GodotRight, std::atan2(ToAxle.y, ToAxle.z)).Apply(P - PivotG); };
	const V3 SeatFrontG = ToGodot(L.SeatFront);
	if (Own)
	{
		if (Kind == "single_gull")
		{
			// A deep cast beam down the chain side bowing up and out in a gull-wing curve, lightening
			// holes through it, an eccentric hub carrier and the wheel on one big centre nut.
			const double X = -ArmX - 0.012;
			std::vector<CodeShapes::Sec> Secs;
			for (int K = 0; K < 9; ++K)
			{
				const double F = K / 8.0;
				Secs.push_back({V3(X - 0.018 * std::sin(F * TorqueSim::Pi), 0.035 * std::sin(std::pow(F, 0.8) * TorqueSim::Pi), Lerp(0.0, ArmLen - 0.03, F)),
					Lerp(0.045, 0.032, F), Lerp(0.085, 0.06, F), Lerp(0.07, 0.055, F), 3.4});
			}
			ArmShell("swingarm", "SingleSidedArm", Secs);
			for (double F : {0.3, 0.52, 0.72})
			{
				const V3 At(X - 0.018 * std::sin(F * TorqueSim::Pi) - 0.04, 0.035 * std::sin(std::pow(F, 0.8) * TorqueSim::Pi) + 0.005, ArmLen * F);
				ArmCode("swingarm", "ArmHole", CodeShapes::Sphere(0.026 * (1.0 - F * 0.35)), GAffine::At(At) * GAffine::Scale(V3(0.25, 0.8, 1.5)), "black_metal");
			}
			ArmCode("swingarm", "", CodeShapes::Cylinder(0.085, 0.06), GAffine::At(V3(X + 0.02, 0.0, ArmLen)) * Across, "paint");
			ArmCode("swingarm", "", CodeShapes::Torus(0.07, 0.006), GAffine::At(V3(X - 0.012, 0.0, ArmLen)) * Across, "brushed_steel");
			ArmCode("swingarm", "CentreNut", CodeShapes::Cylinder(0.038, 0.032, 0.035, 6), GAffine::At(V3(ArmX - 0.02, 0.0, ArmLen)) * Across, "brushed_steel");
			ArmBox("swingarm", "", V3(-ArmX * 0.5, 0.0, 0.04), V3(ArmX + 0.03, 0.075, 0.055), "paint");
			return;
		}
		if (Kind == "box_section")
		{
			// Deep, tapered box-section arms with the axle in slotted end blocks, an adjuster behind each.
			for (double Side : {-1.0, 1.0})
			{
				const double X = ArmX * Side;
				std::vector<CodeShapes::Sec> Secs;
				for (int K = 0; K < 7; ++K)
				{
					const double F = K / 6.0;
					Secs.push_back({V3(X, 0.012 * std::sin(F * TorqueSim::Pi), Lerp(0.0, ArmLen - 0.02, F)), 0.022, Lerp(0.055, 0.032, F), Lerp(0.05, 0.03, F), 7.0});
				}
				ArmShell("swingarm", "BoxArm", Secs);
				ArmBox("swingarm", "", V3(X, 0.0, ArmLen), V3(0.05, 0.06, 0.09), "paint");
				ArmTube("swingarm", V3(X, 0.0, ArmLen + 0.045), V3(X, 0.0, ArmLen + 0.075), 0.006);
			}
			ArmTube("swingarm", V3(-ArmX, 0.0, ArmLen * 0.36), V3(ArmX, 0.0, ArmLen * 0.36), 0.016);
		}
		else if (Kind == "banana")
		{
			// The arms bow up in a long curve for the expansion chambers, the right-hand one the most.
			for (double Side : {-1.0, 1.0})
			{
				const double X = ArmX * Side, Bow = Side > 0.0 ? 0.11 : 0.07;
				std::vector<CodeShapes::Sec> Secs;
				for (int K = 0; K < 9; ++K)
				{
					const double F = K / 8.0;
					Secs.push_back({V3(X, Bow * std::sin(std::pow(F, 0.85) * TorqueSim::Pi) * (1.0 - 0.25 * F), Lerp(0.0, ArmLen, F)), 0.02, Lerp(0.045, 0.03, F), Lerp(0.04, 0.028, F), 4.0});
				}
				ArmShell("swingarm", "BananaArm", Secs);
				ArmBox("swingarm", "", V3(X, 0.0, ArmLen), V3(0.04, 0.055, 0.075), "paint");
			}
			ArmTube("swingarm", V3(-ArmX, 0.06, ArmLen * 0.4), V3(ArmX, 0.08, ArmLen * 0.4), 0.014);
		}
		else if (Kind == "braced_mx")
		{
			// Tall box arms with a truss underneath each, a chain slider and a chain guide.
			for (double Side : {-1.0, 1.0})
			{
				const double X = ArmX * Side;
				std::vector<CodeShapes::Sec> Secs;
				for (int K = 0; K < 7; ++K)
				{
					const double F = K / 6.0;
					Secs.push_back({V3(X, 0.0, Lerp(0.0, ArmLen - 0.02, F)), 0.02, Lerp(0.05, 0.03, F), Lerp(0.06, 0.03, F), 6.0});
				}
				ArmShell("swingarm", "MxArm", Secs);
				ArmBend("swingarm", {V3(X, -0.05, 0.06), V3(X, -0.1, ArmLen * 0.38), V3(X, -0.05, ArmLen * 0.78)}, 0.013);
				for (double F : {0.22, 0.42, 0.62})
				{
					const double Y = -0.05 - 0.05 * std::sin(TorqueSim::Clamp(F / 0.78, 0.0, 1.0) * TorqueSim::Pi);
					ArmTube("swingarm", V3(X, -0.03, ArmLen * F), V3(X, Y + 0.005, ArmLen * F), 0.007);
				}
				ArmBox("swingarm", "", V3(X, 0.0, ArmLen), V3(0.045, 0.06, 0.08), "paint");
			}
			ArmBox("swingarm", "ChainSlider", V3(-ArmX, 0.04, ArmLen * 0.22), V3(0.03, 0.025, ArmLen * 0.4), "black_metal");
			ArmBox("swingarm", "ChainGuide", V3(-ArmX - 0.005, -0.07, ArmLen * 0.8), V3(0.03, 0.06, 0.12), "black_metal");
		}
		else if (Kind == "hardtail")
		{
			for (double Side : {-1.0, 1.0})
			{
				const double X = ArmX * Side;
				const V3 Axle(X, 0.0, ArmLen);
				const V3 Top = ToArm(V3(0.1 * Side, SeatFrontG.y - 0.02, PivotG.z + 0.05));
				ArmTube("swingarm", V3(0.09 * Side, 0.0, 0.0), Axle, 0.019);
				ArmTube("swingarm", Axle, Top, 0.018);
				ArmBox("swingarm", "", Axle, V3(0.016, 0.075, 0.09), "paint");   // the axle plate
			}
			const V3 Cross = ToArm(V3(0.0, SeatFrontG.y - 0.06, PivotG.z + 0.12));
			ArmTube("swingarm", Cross - V3(ArmX * 0.85, 0.0, 0.0), Cross + V3(ArmX * 0.85, 0.0, 0.0), 0.013);
		}
		if (Kind == "box_section" || Kind == "banana" || Kind == "braced_mx" || Kind == "hardtail")
		{
			Bridge("swingarm");
			return;
		}
	}
	if (Kind == "single")
	{
		// A single-sided arm (a Ducati's, an RC30's): one deep cast beam down the chain side, the
		// wheel hung off it on a big centre nut.
		const double X = -ArmX - 0.01;
		std::vector<CodeShapes::Sec> Secs;
		for (int K = 0; K < 7; ++K)
		{
			const double F = K / 6.0;
			Secs.push_back({V3(X, 0.01 * std::sin(F * TorqueSim::Pi), Lerp(0.0, ArmLen, F)), Lerp(0.04, 0.03, F), Lerp(0.075, 0.06, F), Lerp(0.06, 0.05, F), 3.0});
		}
		ArmShell(Slot, "SingleSidedArm", Secs);
		ArmCode(Slot, "", CodeShapes::Cylinder(0.075, 0.05), GAffine::At(V3(X + 0.02, 0.0, ArmLen)) * Across, "paint");
		ArmCode(Slot, "", CodeShapes::Cylinder(0.035, 0.03, 0.035, 6), GAffine::At(V3(ArmX - 0.02, 0.0, ArmLen)) * Across, "chrome");
		ArmBox(Slot, "", V3(-ArmX * 0.5, 0.0, 0.04), V3(ArmX + 0.03, 0.07, 0.05), "paint");
		return;
	}
	if (Kind == "braced")
	{
		for (double Side : {-1.0, 1.0})
		{
			std::vector<CodeShapes::Sec> Secs;
			for (int K = 0; K < 6; ++K)
			{
				const double F = K / 5.0;
				Secs.push_back({V3(ArmX * Side, 0.0, Lerp(0.0, ArmLen + 0.03, F)), 0.02, Lerp(0.06, 0.035, F), Lerp(0.045, 0.03, F), 5.0});
			}
			ArmShell(Slot, "", Secs);
		}
		// The brace arching over the right-hand arm.
		ArmBend(Slot, {V3(ArmX, 0.05, 0.08), V3(ArmX, 0.11, ArmLen * 0.4), V3(ArmX, 0.04, ArmLen * 0.78)}, 0.014);
		for (double F : {0.3, 0.55}) ArmTube(Slot, V3(ArmX, 0.03, ArmLen * F), V3(ArmX, 0.1, ArmLen * F), 0.008);
	}
	else if (Kind == "softail")
	{
		for (double Side : {-1.0, 1.0})
		{
			const double X = ArmX * Side;
			const V3 Axle(X, 0.0, ArmLen);
			const V3 Post = ToArm(V3(X, SeatFrontG.y - 0.05, PivotG.z + 0.14));
			const V3 Top = ToArm(V3(0.09 * Side, PivotG.y + 0.1, PivotG.z - 0.02));
			ArmTube(Slot, V3(0.09 * Side, 0.0, 0.0), Axle, 0.02);
			ArmTube(Slot, Axle, Post, 0.017);
			ArmTube(Slot, Post, Top, 0.017);
			ArmBox(Slot, "", Axle, V3(0.02, 0.06, 0.07), "paint");   // the axle plate
		}
		const V3 Cross = ToArm(V3(0.0, SeatFrontG.y - 0.05, PivotG.z + 0.14));
		ArmTube(Slot, Cross - V3(ArmX, 0.0, 0.0), Cross + V3(ArmX, 0.0, 0.0), 0.014);
	}
	if (Kind != "braced" && Kind != "softail")
		for (double Side : {-1.0, 1.0})
			Put(Out, Slot, "swingarm/box_arm.glb", Arm * AtWith(GAffine::Scale(V3(1.0, 1.0, ArmLen / BoxArmReach)), V3(ArmX * Side, 0.0, 0.0)), "swingarm");
	Bridge(Slot);
}

void BikeParts::FinalDrive(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_final_drive and _blender_chain: the sprockets and the three runs of chain.)
	const GAffine Arm = SwingarmAt(L);
	const double ArmLen = ArmLength(L);
	const Json* Fd = Lay.Part(PartIn(Parts, "final_drive"));
	const int FrontT = static_cast<int>(PartNum(Fd, "front", 15.0)), RearT = static_cast<int>(PartNum(Fd, "rear", 45.0));
	Put(Out, "final_drive", "final_drive/front_" + std::to_string(FrontT) + ".glb", Arm * GAffine::At(V3(-0.13, 0.0, 0.0)), "swingarm");
	Put(Out, "final_drive", "final_drive/rear_" + std::to_string(RearT) + ".glb", GAffine::At(ToGodot(L.RearAxle) + V3(-0.12, 0.0, 0.0)), "rear_wheel");
	if (PartIn(Parts, "chain") == "none") return;
	const double FrontR = SprocketRadius(FrontT), RearR = SprocketRadius(RearT);
	// (A new chain's slack, 30 mm, sags its bottom run 10 mm: BikeModel takes it from the chain's condition.)
	const double Slack = 30.0;
	const double Sag = TorqueSim::Clamp((Slack - 20.0) / 1000.0, 0.0, 0.07);
	const V3 A(-0.125, -FrontR, 0.0), B(-0.125, -RearR, ArmLen);
	const V3 Low = (A + B) * 0.5 + V3(0.0, -Sag, 0.0);
	const std::pair<V3, V3> Runs[] = {{V3(-0.125, FrontR, 0.0), V3(-0.125, RearR, ArmLen)}, {A, Low}, {Low, B}};
	for (const auto& Run : Runs)
	{
		const V3 D = Run.second - Run.first;
		const int N = std::max(1, static_cast<int>(std::ceil(D.Length() / ChainRun - 0.05)));
		const V3 Dir = D.Normalized();
		const GAffine Basis = BasisCols(GodotRight, Dir.Cross(GodotRight), Dir * (D.Length() / (N * ChainRun)));
		for (int I = 0; I < N; ++I)
			if (!Put(Out, "chain", "chain/run.glb", Arm * AtWith(Basis, Run.first + D * (static_cast<double>(I) / N)), "swingarm")) break;
	}
}

void BikeParts::Shocks(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_shock, _blender_strut and _update_struts at rest: each shock from its top
	// mount on the frame down to its bottom one on the swingarm, stretched to fit.)
	const std::string Id = PartIn(Parts, "shock");
	const std::string Path = "shock/" + Id + ".glb";
	V3 BottomMount;
	if (!HasGlb(Path) || !Mount(Path, "bottom", BottomMount)) return;
	const double Rest = BottomMount.Length();
	const GAffine Arm = SwingarmAt(L);
	const double ArmLen = ArmLength(L);
	const V3 Pivot = ToGodot(L.Pivot);
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	auto Strut = [&](const V3& Top, const V3& Bottom) {
		const V3 Low = Arm.Apply(Bottom);
		const V3 Dir = Low - Top;
		const double Len = Dir.Length();
		Put(Out, "shock", Path, AtWith(BasisUp(-Dir / std::max(Len, 0.001)) * GAffine::Scale(V3(1.0, Len / Rest, 1.0)), Top), "");
	};
	if (PartStr(Lay.Part(Id), "layout", "mono") == "twin")
	{
		const std::string Arm2 = ArmType(Parts);
		const bool Softail = Arm2 == "softail" || Arm2 == "hardtail";
		for (double Side : {-1.0, 1.0})
		{
			if (Softail)
			{
				Strut(V3(0.07 * Side, Pivot.y - 0.03, Pivot.z - 0.28), V3(0.07 * Side, -0.05, 0.1));
				continue;
			}
			V3 Top = Pivot + V3(0.12 * Side, 0.3, ArmLen - 0.2);
			Top.y = std::min(Top.y, SeatY - 0.07);
			Strut(Top, V3(0.12 * Side, 0.02, ArmLen - 0.08));
			if (Id == "twin_shock_gas")
			{
				// Its gas reservoir, on the swingarm.
				const V3 A(0.155 * Side, 0.22, ArmLen - 0.17), B(0.155 * Side, 0.32, ArmLen - 0.21);
				CodeShapes::Emit(Out, "shock", Path, "swingarm", "", CodeShapes::Tube(A, B, 0.016), Arm * CodeShapes::TubeAt(A, B), "chrome");
			}
		}
		return;
	}
	Strut(V3(0.0, SeatY - 0.14, Pivot.z + 0.02), V3(0.0, 0.07, 0.14));
}

bool BikeParts::Sweep(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Path, const std::vector<V3>& Route,
	const std::vector<double>& Radii, const std::string& Node) const
{
	const Json* E = Path.size() > 4 ? Index.Get(Path.substr(0, Path.size() - 4)) : nullptr;
	struct Curve { std::vector<V3> Pts, Tan, Side; std::vector<double> Arc, Radii; };
	auto C = std::make_shared<Curve>();
	C->Pts = SmoothCurve(Route, 6);
	if (!E || C->Pts.size() < 2 || !E->Get("bounds")) return false;
	C->Radii = Radii.size() > 1 ? SmoothFloats(Radii, 6) : Radii;
	// The section's length along its +Z (Godot): ours' -x.
	const double Length = -E->Get("bounds")->Get("min")->A[0].N;
	const int Count = static_cast<int>(C->Pts.size());
	const V3 T0 = (C->Pts[1] - C->Pts[0]).Normalized();
	V3 Side = T0.Cross(GodotUp);
	if (Side.Length() < 0.01) Side = T0.Cross(GodotRight);
	Side = Side.Normalized();
	C->Arc.push_back(0.0);
	for (int I = 0; I < Count; ++I)
	{
		const V3 T = (C->Pts[std::min(I + 1, Count - 1)] - C->Pts[std::max(I - 1, 0)]).Normalized();
		if (I > 0)
		{
			const V3 Axis = C->Tan[I - 1].Cross(T);
			if (Axis.Length() > 0.00001) Side = Side.Rotated(Axis.Normalized(), AngleTo(C->Tan[I - 1], T));
			C->Arc.push_back(C->Arc[I - 1] + (C->Pts[I] - C->Pts[I - 1]).Length());
		}
		Side = (Side - T * Side.Dot(T)).Normalized();
		C->Tan.push_back(T);
		C->Side.push_back(Side);
	}
	const double Total = C->Arc.back();
	const int Copies = std::max(1, static_cast<int>(std::lround(Total / Length)));
	const double Stretch = Total / (Copies * Length);
	FittedPart P;
	P.Slot = Slot;
	P.Glb = Path;
	P.Node = Node;
	for (int Cp = 0; Cp < Copies; ++Cp)
		P.Copies.push_back(GodotWarpToOurs([C, Cp, Length, Stretch, Count](const V3& V) {
			const double D = (Cp * Length + V.z) * Stretch;
			int I = static_cast<int>(std::lower_bound(C->Arc.begin(), C->Arc.end(), D) - C->Arc.begin());
			I = std::min(std::max(I - 1, 0), Count - 2);
			const double F = TorqueSim::Clamp((D - C->Arc[I]) / std::max(C->Arc[I + 1] - C->Arc[I], 0.000001), 0.0, 1.0);
			const V3 At = MixV(C->Pts[I], C->Pts[I + 1], F);
			const V3 Tan = MixV(C->Tan[I], C->Tan[I + 1], F).Normalized();
			V3 Sd = MixV(C->Side[I], C->Side[I + 1], F);
			Sd = (Sd - Tan * Sd.Dot(Tan)).Normalized();
			const V3 Up = Tan.Cross(Sd).Normalized();
			double R = C->Radii[0];
			if (C->Radii.size() > 1)
			{
				const double K = (I + F) / (Count - 1) * static_cast<double>(C->Radii.size() - 1);
				const size_t K0 = static_cast<size_t>(std::floor(K));
				R = TorqueSim::Lerp(C->Radii[K0], C->Radii[std::min(K0 + 1, C->Radii.size() - 1)], K - std::floor(K));
			}
			return At + Sd * (V.x * R) + Up * (V.y * R);
		}));
	Out.push_back(P);
	return true;
}

void BikeParts::Can(std::vector<FittedPart>& Out, const V3& A, const V3& B, const std::string& Shape, double R) const
{
	// (BlenderParts.can_path and can_xform: the reference can is 0.5 m long and 0.05 m round.)
	std::string Path = "exhaust/can/" + Shape + ".glb";
	if (!HasGlb(Path)) Path = "exhaust/can/round.glb";
	if (Shape == "slash" || Shape == "open") R = 0.05;
	const V3 Dir = (B - A).Normalized();
	V3 Up = GodotUp - Dir * Dir.Dot(GodotUp);
	if (Up.Length() < 0.001) Up = V3(0.0, 0.0, 1.0) - Dir * Dir.z;
	Up = Up.Normalized();
	V3 Side = Up.Cross(Dir) * (R / 0.05);
	if (A.x < 0.0) Side = -Side;
	Put(Out, "exhaust", Path, BasisCols(Side, Up * (R / 0.05), Dir * ((B - A).Length() / 0.5), A), "");
}

void BikeParts::Exhaust(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_exhaust: the header flanges on the ports, the headers swept from the Blender
	// pipe section along each route, and the can.)
	const Json* P = Lay.Part(PartIn(Parts, "exhaust"));
	if (!P) return;
	const Json* Lk = P->Get("look");
	const Json* Engine = Lay.Part(PartIn(Parts, "engine"));
	const std::string EngLayout = PartStr(Engine, "layout", "parallel");
	const Json* Frame = Lay.Part(PartIn(Parts, "frame"));
	const Json* Geo = Frame ? Frame->Get("geo") : nullptr;
	const std::string Style = P->Str("style", "stock");
	const std::string CanShape = PartStr(Lk, "can", P->Str("can", "round"));
	std::string Route = P->Str("route", PartStr(Geo, "exhaust", "low"));
	if (EngLayout == "2stroke" && Route == "low") Route = "chamber";
	const std::string Header = PartStr(Lk, "header", "");
	const std::string Pipe = std::string("exhaust/pipe/") + (Header.find("wrap") != std::string::npos ? "wrap" : "plain") + ".glb";
	const std::vector<Cylinder> Cyls = Cylinders(Parts, L);
	std::vector<V3> Ports;
	for (size_t I = 0; I < Cyls.size(); ++I)
	{
		const bool Rear = EngLayout == "vtwin" && I == 1;
		const V3 Port = Rear ? Cyls[I].Base + Cyls[I].Up * 0.1 + V3(0.07, 0.0, 0.0) : Cyls[I].Base + Cyls[I].Up * 0.11 + V3(0.0, 0.0, -0.08);
		Ports.push_back(Port);
		const V3 OutDir = Rear ? GodotRight : V3(0.0, -0.05, -0.05).Normalized();
		Put(Out, "exhaust", "exhaust/fitting/flange.glb", AtWith(BasisUp(OutDir), Port - OutDir * 0.005), "");
	}
	const V3 E = ToGodot(L.Crank);
	const double S = L.Hard.Scale;
	const V3 Pivot = ToGodot(L.Pivot);
	const V3 Tail = ToGodot(L.Tail);
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	const double Zr = L.Stats.Wheelbase * 0.5;
	const double Under = E.y - 0.19 * S;
	const double FrontZ = E.z - 0.16 * S - 0.05;
	const double R = Cyls.size() > 2 ? 0.02 : 0.022;
	auto Bend = [&](const std::vector<V3>& Route2, const std::vector<double>& Radii) { Sweep(Out, "exhaust", Pipe, Route2, Radii, ""); };
	if (Route == "shotgun")
	{
		for (size_t I = 0; I < Ports.size(); ++I)
		{
			const double Dy = -0.075 * static_cast<double>(I);
			const V3 Port = Ports[I];
			const V3 End(0.17, Pivot.y - 0.02 + Dy * 0.7, Zr + 0.2);
			Bend({Port, Port + V3(0.05, -0.03, -0.03), V3(0.15, E.y - 0.06 + Dy * 0.6, E.z + 0.04),
				V3(0.17, Pivot.y - 0.06 + Dy * 0.8, Pivot.z + 0.05), V3(0.17, Pivot.y - 0.05 + Dy * 0.75, Pivot.z + 0.3)}, {0.026});
			Can(Out, V3(0.17, Pivot.y - 0.05 + Dy * 0.75, Pivot.z + 0.3), End, CanShape, 0.04);
		}
	}
	else if (Route == "high")
	{
		const V3 Join(0.17, E.y + 0.08, E.z + 0.14);
		for (const V3& Port : Ports)
			Bend({Port, Port + V3(0.0, -0.03, -0.06), V3(0.12, E.y - 0.02, E.z - 0.14 * S), V3(0.16, E.y + 0.02, E.z + 0.0), Join}, {R});
		const V3 CanA(0.18, SeatY - 0.2, Pivot.z + 0.02);
		Bend({Join, MixV(Join, CanA, 0.5) + V3(0.005, 0.0, 0.0), CanA}, {0.026});
		Can(Out, CanA, V3(0.18, SeatY - 0.12, Tail.z - 0.02), CanShape, 0.045);
	}
	else if (Route == "upswept")
	{
		const V3 Join(0.13, E.y - 0.04, E.z + 0.12);
		for (const V3& Port : Ports)
			Bend({Port, Port + V3(0.0, -0.06, -0.05), V3(0.09, E.y - 0.12, FrontZ + 0.02), Join}, {R});
		const V3 CanA(0.15, E.y + 0.14, Pivot.z + 0.02);
		Bend({Join, V3(0.145, E.y + 0.04, Pivot.z - 0.08), CanA}, {0.025});
		Can(Out, CanA, V3(0.15, SeatY - 0.04, Zr + 0.0), CanShape, 0.045);
	}
	else if (Route == "underseat")
	{
		const V3 Gather(0.0, Under + 0.01, E.z + 0.04);
		for (const V3& Port : Ports)
			Bend({Port, Port + V3(0.0, -0.06, -0.04), V3(Port.x * 0.5, Under + 0.02, FrontZ + 0.06), Gather}, {R});
		const V3 Up(0.0, SeatY - 0.24, Pivot.z + 0.1);
		Bend({Gather, V3(0.0, Under + 0.03, Pivot.z - 0.1), V3(0.0, Pivot.y + 0.06, Pivot.z - 0.02), Up}, {0.03});
		for (double X : {-0.05, 0.05})
			Can(Out, Up + V3(X, 0.08, 0.04), V3(X, SeatY - 0.1, Tail.z + 0.06), CanShape, 0.035);
		Bend({Up, Up + V3(0.0, 0.06, 0.02), Up + V3(0.05, 0.08, 0.04)}, {0.025});
		Bend({Up + V3(0.0, 0.06, 0.02), Up + V3(-0.05, 0.08, 0.04)}, {0.025});
	}
	else if (Route == "chamber")
	{
		for (size_t I = 0; I < Ports.size(); ++I)
		{
			const V3 Port = Ports[I];
			const double X = Port.x * 0.6 + 0.02;
			const double Di = static_cast<double>(I);
			const V3 End(0.12 + 0.03 * Di, Pivot.y - 0.05, Pivot.z + 0.06);
			Bend({Port, Port + V3(0.0, -0.06, -0.06), V3(X, Under - 0.02, FrontZ + 0.06), V3(X + 0.02, Under - 0.01, E.z + 0.12), End},
				{0.022, 0.03, 0.055, 0.06, 0.024});
			Can(Out, End, V3(0.14 + 0.03 * Di, 0.16 + 0.04 * Di, Zr - 0.12), CanShape, 0.04);
		}
	}
	else
	{
		// Low: down in front of the engine, under it, and back past the pivot to the can. A classic
		// twin with stock pipes has a pipe and a can each side.
		const bool PairDefault = Style == "stock" && Cyls.size() == 2 && (EngLayout == "twin" || EngLayout == "parallel" || EngLayout == "boxer");
		const Json* PairJ = Lk ? Lk->Get("pair") : nullptr;
		const bool Both = PairJ ? PairJ->B : PairDefault;
		const bool Stubby = Route == "stubby";
		for (double Sx : Both ? std::vector<double>{-1.0, 1.0} : std::vector<double>{1.0})
		{
			V3 Collector(0.15 * Sx, Pivot.y - 0.2, Pivot.z + 0.05);
			if (Stubby) Collector = V3(0.11, Under + 0.02, Pivot.z - 0.14);
			for (const V3& Port : Ports)
			{
				if (Both && (Port.x < 0.0 ? -1.0 : Port.x > 0.0 ? 1.0 : 0.0) != Sx && std::fabs(Port.x) > 0.01) continue;
				Bend({Port, Port + V3(0.0, -0.05, -0.05), V3(0.06 * Sx + Port.x * 0.4, Under + 0.02, FrontZ), V3(0.11 * Sx, Under, E.z + 0.06), Collector}, {R});
			}
			V3 CanA = Collector + V3(0.0, 0.03, 0.12);
			V3 CanB(0.16 * Sx, 0.2, Zr - 0.08);
			if (Stubby)
			{
				CanA = Collector + V3(0.02, 0.02, 0.06);
				CanB = V3(0.15, Pivot.y - 0.06, Pivot.z + 0.2);
			}
			Bend({Collector, MixV(Collector, CanA, 0.5), CanA}, {0.026});
			Can(Out, CanA, CanB, CanShape, Style == "stock" ? 0.06 : 0.05);
		}
	}
}

void BikeParts::Ancillaries(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	const std::vector<Cylinder> Cyls = Cylinders(Parts, L);
	const Json* Engine = Lay.Part(PartIn(Parts, "engine"));
	const std::string EngLayout = PartStr(Engine, "layout", "parallel");
	const V3 E = ToGodot(L.Crank);
	const double S = L.Hard.Scale;
	const double BlockW = L.Hard.CaseHalfW * 2.0;
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	auto TankBaseY = [&](double Z) {
		if (Body.TankA.x == Body.TankB.x && Body.TankA.y == Body.TankB.y && Body.TankA.z == Body.TankB.z) return SeatY;
		return TorqueSim::Lerp(Body.TankA.y, Body.TankB.y, TorqueSim::Clamp((Z - Body.TankA.z) / std::max(Body.TankB.z - Body.TankA.z, 0.01), 0.0, 1.0));
	};

	// Spark plugs, their caps turning out sideways, and the coil under the tank (_draw_spark_plugs).
	if (HasGlb("spark_plugs/plug.glb"))
	{
		std::vector<V3> Ends;
		for (size_t I = 0; I < Cyls.size(); ++I)
		{
			const V3 Up = Cyls[I].Up, Un = Up.Normalized();
			const V3 At = Cyls[I].Base + Up * 0.2 + V3(0.0, 0.0, 0.02);
			V3 OutDir = Up;
			const bool Straight = std::fabs(Up.x) >= 0.5;
			if (!Straight) OutDir = V3(Cyls.size() == 2 && I == 0 ? -1.0 : 1.0, 0.0, 0.0);
			V3 Side = OutDir - Un * OutDir.Dot(Un);
			if (Side.Length() < 0.01) Side = V3(0.0, 0.0, -1.0) - Un * Un.Dot(V3(0.0, 0.0, -1.0));
			Side = Side.Normalized();
			const GAffine B = BasisCols(Side, Up, Side.Cross(Up).Normalized(), At);
			Put(Out, "spark_plugs", "spark_plugs/plug.glb", B, "");
			Put(Out, "spark_plugs", Straight ? "spark_plugs/cap_straight.glb" : "spark_plugs/cap.glb", B, "");
			Ends.push_back(Straight ? At + Up * 0.075 : At + Up * 0.025 + Side * 0.05);
		}
		if (!Ends.empty())
		{
			double Top = -1e9;
			V3 Mid;
			for (const V3& End : Ends) { Top = std::max(Top, End.y); Mid += End; }
			Mid = Mid / static_cast<double>(Ends.size());
			const V3 Coil(0.0, Top - 0.11, Mid.z + 0.16);
			Put(Out, "spark_plugs", "spark_plugs/coil.glb", GAffine::At(Coil), "");
			// The HT leads from the caps, sagging back into the coil's towers.
			CodeShapes::GodotNames Named;
			for (size_t I = 0; I < Ends.size(); ++I)
			{
				const V3 End = Ends[I];
				const V3 Into = Coil + V3(I % 2 == 0 ? -0.012 : 0.012, 0.0, -0.05);
				const V3 Sag = End + (Into - End) * 0.5 + V3(0.0, -0.03, 0.0);
				CodeShapes::Emit(Out, "spark_plugs", "spark_plugs/coil.glb", "", Named("HTLead"),
					CodeShapes::Sweep({End, End + V3(0.0, 0.012, 0.02), Sag, Into}, 0.0045, 6), GAffine(), "rubber");
			}
		}
	}

	// Where the intake meets each cylinder (_intake_points).
	const std::vector<Intake> Points = Intakes(Parts, L);
	V3 Mid;
	for (const Intake& P : Points) Mid += P.Port;
	if (!Points.empty()) Mid = Mid / static_cast<double>(Points.size());

	// Carbs or throttle bodies (_draw_fuel, _fuel_glb).
	{
		const std::string Id = PartIn(Parts, "fuel");
		const Json* F = Lay.Part(Id);
		const std::string Style = PartStr(F, "style", "carbs");
		const std::string Glb = Id == "flat_slide_carbs" ? "fuel/carb_flat.glb" : Id == "race_injection" ? "fuel/throttle_body_race.glb"
			: Style == "fi" ? "fuel/throttle_body.glb" : "fuel/carb_round.glb";
		if (Style == "carb" && Points.size() > 1)
		{
			// One carb on the manifold where the runners meet.
			for (const Intake& P : Points)
				CodeShapes::Emit(Out, "fuel", Glb, "", "", CodeShapes::Tube(P.Port, P.Port + V3(0.0, 0.0, 0.05), 0.02), CodeShapes::TubeAt(P.Port, P.Port + V3(0.0, 0.0, 0.05)), "paint");
			Put(Out, "fuel", Glb, AtWith(BasisAlong(V3(0.0, 0.15, 1.0)), Mid + V3(0.0, 0.0, 0.03)), "");
		}
		else
			for (const Intake& P : Points) Put(Out, "fuel", Glb, AtWith(BasisAlong(P.Dir), P.Port), "");
	}

	// The air filter (_draw_air_filter, _blender_airbox): pods or stacks, a cone, a V-twin's round
	// cleaner, or the airbox under the tank with its element's housing out to the right.
	{
		constexpr double AirFilterOut = 0.21;
		const std::string Style = PartStr(Lay.Part(PartIn(Parts, "air_filter")), "style", "airbox");
		// (The rubber boots and hoses from the carbs out to it.)
		auto Boot = [&](const std::string& Glb, const CodeShapes::GMesh& M, const GAffine& Xf) { CodeShapes::Emit(Out, "air_filter", Glb, "", "", M, Xf, "rubber"); };
		if (Style == "pods" || Style == "stacks")
		{
			const bool Stacks = Style == "stacks";
			for (const Intake& P : Points)
			{
				const double Side = P.Port.x < -0.01 ? -1.0 : 1.0;
				const V3 Dir = (P.Dir + V3(Side * 0.9, 0.0, 0.0)).Normalized();
				const double HalfLen = Stacks ? 0.025 : 0.045;
				const double Reach = std::max(Stacks ? 0.11 : 0.13, (AirFilterOut - std::fabs(P.Port.x)) / std::fabs(Dir.x) - HalfLen);
				const std::string Glb = Stacks ? "filter/stack.glb" : "filter/pod.glb";
				const V3 At = P.Port + Dir * Reach;
				if (Reach > 0.14) Boot(Glb, CodeShapes::Tube(P.Port + Dir * 0.06, At - Dir * HalfLen, 0.024), CodeShapes::TubeAt(P.Port + Dir * 0.06, At - Dir * HalfLen));
				Put(Out, "air_filter", Glb, AtWith(BasisUp(Dir), At), "");
			}
		}
		else if (Style == "cone")
		{
			const V3 Dir = V3(0.75, 0.05, 0.66).Normalized();
			V3 At(std::max(AirFilterOut, Mid.x + 0.12) - Dir.x * 0.07, Mid.y + 0.02, Mid.z + 0.16);
			At.y = std::min(At.y, TankBaseY(At.z) - 0.07);
			Boot("filter/cone.glb", CodeShapes::Sweep({Mid + V3(0.0, 0.0, 0.07), Mid + V3(0.0, 0.01, 0.13), At - Dir * 0.07}, 0.035, 12), GAffine());
			Put(Out, "air_filter", "filter/cone.glb", AtWith(BasisUp(Dir), At), "");
		}
		else if (EngLayout == "vtwin" && !Points.empty())
		{
			// (Out past the tank's edge: its meshes' bounds, else its widest section.)
			double Edge = AirFilterOut;
			bool Measured = false;
			if (Meshes)
				for (const FittedPart& P : Body.Parts)
				{
					PartMesh M;
					if (P.Slot != "tank" || !Meshes(P.Glb, M)) continue;
					for (const PartSurface& Sf : Draw(M, P).Surfaces)
						for (const V3& V : Sf.Pos) Edge = std::max(Edge, ToGodot(V).x);
					Measured = true;
				}
			if (!Measured)
				for (const LoftSection& Sec : Body.TankSecs) Edge = std::max(Edge, Sec.W);
			const Intake& P = Points[0];
			const double Reach = std::max(0.1, (Edge - P.Port.x) / P.Dir.x);
			if (Reach > 0.11)
				Boot("filter/cleaner.glb", CodeShapes::Tube(P.Port + P.Dir * 0.06, P.Port + P.Dir * (Reach - 0.02), 0.03), CodeShapes::TubeAt(P.Port + P.Dir * 0.06, P.Port + P.Dir * (Reach - 0.02)));
			Put(Out, "air_filter", "filter/cleaner.glb", AtWith(BasisUp(P.Dir), P.Port + P.Dir * Reach), "");
		}
		else if (!Points.empty())
		{
			V3 At = Mid + V3(0.0, 0.04, 0.16);
			At.y = std::min(At.y, TankBaseY(At.z) - 0.08);
			const double Wide = std::max(0.2, BlockW * 0.8);
			const double OutX = std::max(AirFilterOut, Wide * 0.5 + 0.05);
			if (Put(Out, "air_filter", "filter/airbox.glb", AtWith(GAffine::Scale(V3(Wide / 0.2, 1.0, 1.0)), At), ""))
			{
				const GAffine Turn = GAffine::Rotation(V3(0.0, 0.0, 1.0), TorqueSim::Pi * 0.5);
				const double Length = OutX - Wide * 0.5 + 0.01;
				Put(Out, "air_filter", "filter/housing.glb", AtWith(Turn * GAffine::Scale(V3(1.0, Length, 1.0)), V3((Wide * 0.5 + OutX) * 0.5, At.y, At.z)), "");
				Put(Out, "air_filter", "filter/lid.glb", AtWith(Turn, V3(OutX, At.y, At.z)), "");
			}
		}
	}

	// The spin-on oil filter, low on the right of the cases (out of the front on a boxer).
	{
		V3 At, Dir;
		if (EngLayout == "boxer")
		{
			const double Off = 0.02;   // (Rig built: a little lower and further out)
			At = E + V3(0.05 * S, -0.08 * S - Off, -0.17 * S - 0.03 - Off);
			Dir = V3(0.0, 0.0, -1.0);
		}
		else
		{
			At = E + V3(BlockW * 0.5 + 0.07 * 0.5 - 0.003, -0.1 * S - 0.015, -0.11 * S - 0.005);
			Dir = GodotRight;
		}
		Put(Out, "oil_filter", "filter/oil.glb", AtWith(BasisUp(Dir), At), "");
	}

	// Cooling (_draw_cooling): fins on each barrel, an oil cooler, a radiator, or twin side radiators.
	{
		const std::string Style = PartStr(Lay.Part(PartIn(Parts, "cooling")), "style", "fins");
		const double FrontZ = E.z - 0.3;
		if (Style == "fins")
			for (const Cylinder& C : Cyls)
				Put(Out, "cooling", "radiator/fins.glb", AtWith(BasisUp(C.Up) * GAffine::Scale(V3(1.0, C.Up.Length(), 1.0)), C.Base), "");
		else if (Style == "oil")
		{
			Put(Out, "cooling", "radiator/oil_cooler.glb", GAffine::At(V3(0.0, E.y + 0.2, FrontZ + 0.035)), "");
			// Its lines back to the engine.
			for (double Sd : {-1.0, 1.0})
			{
				const V3 A(0.1 * Sd, E.y + 0.15, FrontZ + 0.04), B = E + V3(0.08 * Sd, -0.05, -0.17);
				CodeShapes::Emit(Out, "cooling", "radiator/oil_cooler.glb", "", "", CodeShapes::Tube(A, B, 0.007), CodeShapes::TubeAt(A, B), "paint");
			}
		}
		else if (Style == "radiator" || Style == "race")
		{
			const bool Big = Style == "race";
			const double W = Big ? 0.42 : 0.3, H = Big ? 0.34 : 0.28;
			const V3 C(0.0, E.y + 0.25, FrontZ);
			const GAffine Rb = GAffine::Rotation(GodotRight, -0.25);
			Put(Out, "cooling", "radiator/core.glb", AtWith(Rb * GAffine::Scale(V3(W, H, 1.0)), C), "");
			CodeShapes::GodotNames Named;
			auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
				CodeShapes::Emit(Out, "cooling", "radiator/core.glb", "", Named(Name), M, Xf, Tag);
			};
			// A grille of slim slats in front, the fan behind, the shrouds that steer air through the core.
			for (int K = 0; K < 7; ++K)
				Code("RadiatorGrille", CodeShapes::Box(V3(W - 0.01, 0.006, 0.012)), AtWith(Rb, C + Rb.Apply(V3(0.0, TorqueSim::Lerp(-H * 0.42, H * 0.42, K / 6.0), -0.032))), "black_metal");
			Code("", CodeShapes::Cylinder(std::min(W, H) * 0.38, 0.03), AtWith(Rb * GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5), C + Rb.Apply(V3(0.0, 0.0, 0.045))), "black_metal");
			for (double Sx : {-1.0, 1.0})
				Code("RadiatorShroud", CodeShapes::Box(V3(0.008, H * 0.95, 0.12)), AtWith(Rb * GAffine::Rotation(GodotUp, -0.45 * Sx), C + Rb.Apply(V3((W * 0.5 + 0.035) * Sx, 0.0, -0.02))), "paint");
			// Coolant hoses: the top one to the thermostat on the head, the bottom one to the pump.
			const V3 Top = C + Rb.Apply(V3(W * 0.32, H * 0.5 + 0.005, 0.03));
			V3 Head = E + V3(0.0, 0.3 * S, -0.04);
			if (!Cyls.empty())
			{
				V3 Tops;
				for (const Cylinder& Cy : Cyls) Tops += Cy.Base + Cy.Up * 0.13;
				Head = Tops / static_cast<double>(Cyls.size()) + V3(0.0, 0.02, -0.085);
			}
			Code("Thermostat", CodeShapes::Box(V3(0.04, 0.03, 0.03)), GAffine::At(Head), "paint");
			Code("CoolantHose", CodeShapes::Sweep({Top, Top + V3(0.0, 0.03, 0.04), Head + (Top - Head) * 0.4 + V3(0.0, 0.03, 0.0), Head}, 0.014, 10), GAffine(), "rubber");
			const V3 Bottom = C + Rb.Apply(V3(-W * 0.32, -H * 0.5 - 0.005, 0.03));
			const V3 Pump = E + V3(-BlockW * 0.3, -0.05, -0.17);
			Code("CoolantHose", CodeShapes::Sweep({Bottom, Bottom + V3(0.0, -0.04, 0.03), Pump + V3(0.0, 0.02, -0.04), Pump}, 0.015, 10), GAffine(), "rubber");
			Code("", CodeShapes::Cylinder(0.025, 0.03), AtWith(GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5), Pump), "paint");
		}
		else if (Style == "twin")
			for (double Side : {-1.0, 1.0})
				Put(Out, "cooling", "radiator/side.glb",
					AtWith(GAffine::Rotation(GodotUp, 0.35 * Side) * GAffine::Scale(V3(Side, 1.0, 1.0)), V3(0.16 * Side, E.y + 0.24, FrontZ + 0.08)), "");
	}

	// The gearbox (_blender_gearbox): the case stretched to the engine's width, the clutch cover,
	// the gear change shaft's arm turned to the lever, a quickshifter and a kickstart.
	{
		const std::string Id = PartIn(Parts, "gearbox");
		const std::string Root = "gearbox/" + Id + "/";
		if (HasGlb(Root + "case.glb"))
		{
			Put(Out, "gearbox", Root + "case.glb", AtWith(GAffine::Scale(V3(BlockW * 0.8 / 0.16, 1.0, 1.0)), E + V3(0.0, -0.06, 0.22)), "");
			Put(Out, "gearbox", Root + "clutch.glb", GAffine::At(E + V3(BlockW * 0.5, -0.02, 0.1)), "");
			const V3 Peg = ToGodot(L.Peg);
			const V3 Shaft = E + V3(-BlockW * 0.5 - 0.06, -0.09, 0.2);
			const V3 Lever(-std::fabs(Peg.x) + 0.015, Peg.y + 0.04, Peg.z + 0.015);
			const V3 Arm = V3(0.0, -0.06, -0.08).Normalized();
			const V3 To = Lever - Shaft;
			const GAffine Turn = GAffine::Rotation(GodotRight, std::atan2(Arm.y, Arm.z) - std::atan2(To.y, To.z));
			Put(Out, "gearbox", "gearbox/selector.glb", AtWith(Turn, Shaft), "");
			const V3 Start = Shaft + Turn.Apply(Arm) * 0.032;
			const Json* G = Lay.Part(Id);
			const Json* Qs = G ? G->Get("quickshifter") : nullptr;
			if ((Start - Lever).Length() > 0.02)
			{
				// The linkage rod from the shaft's arm to the gear lever, its ball joint at the lever.
				CodeShapes::GodotNames Named;
				CodeShapes::Emit(Out, "gearbox", "gearbox/selector.glb", "", Named("GearLinkage"), CodeShapes::Tube(Start, Lever, 0.0055), CodeShapes::TubeAt(Start, Lever), "chrome");
				CodeShapes::Emit(Out, "gearbox", "gearbox/selector.glb", "", Named("GearLinkage"), CodeShapes::Sphere(0.008), GAffine::At(Lever), "chrome");
				if (Qs && Qs->B)
				{
					const V3 Along = (Lever - Start).Normalized();
					Put(Out, "gearbox", "gearbox/quickshifter.glb", AtWith(BasisUp(Along), Start + Along * std::min(0.06, (Start - Lever).Length() * 0.4)), "");
				}
			}
			if (EngLayout == "single" || EngLayout == "2stroke" || EngLayout == "twin")
				Put(Out, "gearbox", "gearbox/kickstart.glb", GAffine::At(E + V3(BlockW * 0.5 + 0.035, -0.04, 0.2)), "");
		}
	}
}

void BikeParts::TankTrim(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	// The code-drawn pieces of BikeModel._draw_tank on the Blender tank's sections: an adventure
	// tank's knee panels, the filler cap, a cruiser's console, accent stripes, the side badges,
	// knee pads, a sports tank's chest pad and an enduro tank's radiator shrouds.
	if (Body.TankSecs.size() < 3) return;
	const Json* P = Lay.Part(PartIn(Parts, "tank"));
	if (!P) return;
	const Json* Look = P->Get("look");
	const std::string Style = P->Str("style", "cafe");
	std::string Stand = "tank/cafe.glb";
	for (const FittedPart& F : Body.Parts)
		if (F.Slot == "tank") Stand = F.Glb;
	const int Steps = static_cast<int>(Body.TankSecs.size()) - 1;
	auto At = [&](double F) { return Body.TankSecs[static_cast<size_t>(std::round(Steps * F))]; };
	auto SecOf = [&](const LoftSection& Ls, double By) {
		CodeShapes::Sec S{Ls.C, Ls.W * By + (By != 1.0 ? 0.001 : 0.0), Ls.Up * By + (By != 1.0 ? 0.001 : 0.0), Ls.Down * By + (By != 1.0 ? 0.001 : 0.0), Body.TankN};
		S.Bw = Body.TankBw;
		return S;
	};
	// (_grown: sections From..To a little bigger all round, for a panel laid over the tank.)
	auto Grown = [&](double By, int From, int To) {
		std::vector<CodeShapes::Sec> Secs;
		for (int I = From; I <= std::min(To, Steps); ++I) Secs.push_back(SecOf(Body.TankSecs[static_cast<size_t>(I)], By));
		return Secs;
	};
	// (_on_loft.)
	auto OnLoft = [&](const LoftSection& S, double A, double Grow) {
		const double E = 2.0 / Body.TankN;
		const double Ca = std::cos(A), Sa = std::sin(A);
		const double W = S.W * (Sa < 0.0 ? Body.TankBw : 1.0);
		const double X = (Ca > 0.0 ? 1.0 : Ca < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Ca), E) * W;
		const double Y = (Sa > 0.0 ? 1.0 : Sa < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Sa), E) * (Sa > 0.0 ? S.Up : S.Down);
		return S.C + V3(X, Y, 0.0) * Grow;
	};
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "tank", Stand, "", Named(Name), M, Xf, Tag);
	};
	const double Pi = TorqueSim::Pi;
	if (Look && Look->Flag("panels"))
	{
		const int From = static_cast<int>(std::round(Steps * 0.3));
		Code("TankPanel", CodeShapes::Loft(Grown(1.012, From, Steps - 1), 28, true, -0.85, 0.05), GAffine(), "plastic");
		Code("TankPanel", CodeShapes::Loft(Grown(1.012, From, Steps - 1), 28, true, Pi - 0.05, Pi + 0.85), GAffine(), "plastic");
	}
	// The tank as drawn, for laying the cap and the pads on its real surface.
	TankSurface Skin;
	if (Meshes)
		for (const FittedPart& F : Body.Parts)
		{
			PartMesh M;
			if (F.Slot != "tank" || !Meshes(F.Glb, M)) continue;
			// Its points only (keys mixed, then warped): Draw's normals take seven warps a point, and the
			// faces' own are plenty here.
			PartMesh Shell = F.Copies.empty() ? M.Deformed(F.Weights, nullptr) : Draw(M, F);
			for (PartSurface& Sf : Shell.Surfaces)
			{
				if (F.Copies.empty() && F.Warp)
					for (V3& Pt : Sf.Pos) Pt = F.Warp(Pt);
				std::vector<V3> Smooth(Sf.Pos.size(), V3());
				for (size_t K = 0; K + 2 < Sf.Idx.size(); K += 3)
				{
					const unsigned Ia = Sf.Idx[K], Ib = Sf.Idx[K + 1], Ic = Sf.Idx[K + 2];
					const V3 Face = (Sf.Pos[Ib] - Sf.Pos[Ia]).Cross(Sf.Pos[Ic] - Sf.Pos[Ia]);
					Smooth[Ia] = Smooth[Ia] + Face;
					Smooth[Ib] = Smooth[Ib] + Face;
					Smooth[Ic] = Smooth[Ic] + Face;
				}
				for (size_t K = 0; K + 2 < Sf.Idx.size(); K += 3)
				{
					const unsigned Ia = Sf.Idx[K], Ib = Sf.Idx[K + 1], Ic = Sf.Idx[K + 2];
					Skin.Tris.push_back({ToGodot(Sf.Pos[Ia]), ToGodot(Sf.Pos[Ib]), ToGodot(Sf.Pos[Ic]),
						ToGodot(Smooth[Ia]), ToGodot(Smooth[Ib]), ToGodot(Smooth[Ic])});
				}
			}
		}
	double TankTop = -1e9;
	for (const LoftSection& Sec : Body.TankSecs) TankTop = std::max(TankTop, Sec.C.y + Sec.Up);
	// The filler cap a third of the way back: plain, a raised Monza flip cap, a flush aircraft cap or a pair.
	// Each stands on the tank's real top, square to its surface and proud of it all the way round its rim
	// (the tank can curve or slope away under it), with its foot sunk a little into the paint so no gap shows.
	const LoftSection CapS = At(0.3);
	const std::string CapKind = PartStr(Look, "cap", "stock");
	struct CapPlace { GAffine Basis; V3 Foot, Nrm; double Bottom = 0.0, Top = 0.0; };
	auto FillerCap = [&](double Across, double Radius, double Proud) {
		V3 Foot = CapS.C + V3(Across, CapS.Up - 0.004, 0.0), Nrm(0.0, 1.0, 0.0);
		double Lo = 0.0, Hi = 0.0;
		const TankSurface There = Skin.Near(V3(CapS.C.x + Across - Radius - 0.02, -1e9, CapS.C.z - Radius - 0.02),
			V3(CapS.C.x + Across + Radius + 0.02, 1e9, CapS.C.z + Radius + 0.02));
		if (!There.Empty())
		{
			V3 Hit, HitN;
			if (There.Hit(V3(CapS.C.x + Across, TankTop + 0.5, CapS.C.z), V3(0.0, -1.0, 0.0), Hit, HitN))
			{
				Foot = Hit;
				// The tank's lie under the cap: a plane fitted through where its rim comes down on the paint
				// (steadier on a creased or rippled top than the normal at one point).
				std::vector<V3> Ring;
				for (int I = 0; I < 16; ++I)
				{
					const double Ang = I * Pi / 8.0;
					V3 RimHit, RimN;
					if (There.Hit(V3(Foot.x + std::sin(Ang) * Radius, TankTop + 0.5, Foot.z + std::cos(Ang) * Radius), V3(0.0, -1.0, 0.0), RimHit, RimN))
						Ring.push_back(RimHit - Foot);
				}
				if (Ring.size() >= 8)
				{
					double Sxx = 0.0, Sxz = 0.0, Szz = 0.0, Sxy = 0.0, Szy = 0.0;
					for (const V3& D : Ring)
					{
						Sxx += D.x * D.x; Sxz += D.x * D.z; Szz += D.z * D.z;
						Sxy += D.x * D.y; Szy += D.z * D.y;
					}
					const double Det = Sxx * Szz - Sxz * Sxz;
					if (std::fabs(Det) > 1e-12)
					{
						const double Gx = (Sxy * Szz - Szy * Sxz) / Det, Gz = (Szy * Sxx - Sxy * Sxz) / Det;
						Nrm = V3(-Gx, 1.0, -Gz).Normalized();
					}
					for (const V3& D : Ring)
					{
						const double H = D.Dot(Nrm);
						Lo = std::min(Lo, H);
						Hi = std::max(Hi, H);
					}
				}
			}
		}
		CapPlace Out1;
		Out1.Basis = BasisUp(Nrm);
		Out1.Foot = Foot;
		Out1.Nrm = Nrm;
		Out1.Bottom = Lo - 0.003;
		Out1.Top = Hi + Proud;
		return Out1;
	};
	auto Puck = [&](const GAffine& Basis, const V3& Base, const V3& Nrm, double From, double To, double Radius, double TopR, const std::string& Tag) {
		const double Tall = std::max(To - From, 0.002);
		Code("", CodeShapes::Cylinder(Radius, Tall, TopR, 32), AtWith(Basis, Base + Nrm * ((From + To) * 0.5)), Tag);
	};
	if (CapKind == "monza")
	{
		// A raised flip cap: a tapered chrome body and its hinged lid's lever towards the back.
		const auto C = FillerCap(0.0, 0.045, 0.03);
		Puck(C.Basis, C.Foot, C.Nrm, C.Bottom, C.Top, 0.045, 0.04, "chrome");
		Code("", CodeShapes::Box(V3(0.03, 0.012, 0.03)), AtWith(C.Basis, C.Foot + C.Nrm * (C.Top - 0.002) + C.Basis.Apply(V3(0.0, 0.0, 0.03))), "chrome");
	}
	else if (CapKind == "flush")
	{
		// An aircraft cap: a chrome bezel ring a few millimetres proud with the lid inset in it, a hinge tab
		// at the front and the bezel's screws.
		const auto C = FillerCap(0.0, 0.045, 0.004);
		Puck(C.Basis, C.Foot, C.Nrm, C.Bottom, C.Top, 0.045, 0.043, "chrome");
		Puck(C.Basis, C.Foot, C.Nrm, C.Top - 0.002, C.Top + 0.0015, 0.033, 0.032, "chrome");
		Puck(C.Basis, C.Foot, C.Nrm, C.Top - 0.001, C.Top + 0.002, 0.026, 0.026, "black_metal");
		Code("", CodeShapes::Box(V3(0.018, 0.004, 0.01)), AtWith(C.Basis, C.Foot + C.Nrm * (C.Top + 0.001) + C.Basis.Apply(V3(0.0, 0.0, -0.034))), "chrome");
		for (int I = 0; I < 6; ++I)
		{
			const double Ang = (I + 0.5) * Pi / 3.0;
			Code("", CodeShapes::Cylinder(0.0032, 0.0024, 0.0026, 10), AtWith(C.Basis, C.Foot + C.Nrm * (C.Top + 0.0008) + C.Basis.Apply(V3(std::sin(Ang), 0.0, std::cos(Ang)) * 0.0395)), "chrome");
		}
	}
	else if (CapKind == "twin")
		for (double X : {-0.05, 0.05})
		{
			const auto C = FillerCap(X, 0.03, 0.016);
			Puck(C.Basis, C.Foot, C.Nrm, C.Bottom, C.Top, 0.03, 0.028, "chrome");
		}
	else
	{
		const auto C = FillerCap(0.0, 0.035, 0.016);
		Puck(C.Basis, C.Foot, C.Nrm, C.Bottom, C.Top, 0.035, 0.033, "chrome");
	}
	const bool Console = Look && Look->Flag("console");
	if (Console)
	{
		std::vector<V3> Path;
		for (int I = static_cast<int>(std::round(Steps * 0.35)); I < static_cast<int>(std::round(Steps * 0.85)); ++I)
			Path.push_back(OnLoft(Body.TankSecs[static_cast<size_t>(I)], Pi * 0.5, 1.0));
		Code("", CodeShapes::Sweep(Path, 0.03, 10), GAffine(), "chrome");
		if (Path.size() > 1)
			Code("", CodeShapes::Cylinder(0.035, 0.012), AtWith(GAffine::Rotation(GodotRight, -0.3), Path[1] + V3(0.0, 0.02, 0.0)), "black_metal");
	}
	if (Look && Look->Get("accent"))
		for (double Ang : Console ? std::vector<double>{Pi * 0.5 - 0.3, Pi * 0.5 + 0.3} : std::vector<double>{Pi * 0.5 - 0.12, Pi * 0.5 + 0.12})
		{
			std::vector<V3> Path;
			for (int I = 1; I < Steps; ++I) Path.push_back(OnLoft(Body.TankSecs[static_cast<size_t>(I)], Ang, 1.004));
			Code("TankStripe", CodeShapes::Sweep(Path, 0.009, 4), GAffine(), "paint_2");
		}
	// The badges on the sides: a chrome oval, a long winged script or none.
	const std::string Badge = PartStr(Look, "badge", "oval");
	if (Badge != "none")
		for (double Sd : {-1.0, 1.0})
		{
			const LoftSection& B = CapS;
			const V3 Scale = Badge == "wing" ? V3(0.1, 0.35, 2.6) : V3(0.15, 0.55, 1.1);
			Code("", CodeShapes::Sphere(0.03), AtWith(GAffine::Scale(Scale), B.C + V3(B.W * 0.97 * Sd, B.Up * 0.05, 0.0)), "chrome");
		}
	// Knee pads where real tanks have them, on the flanks where the rider's knees grip: a classic tank's
	// ribbed rubber knee pads, and a sports tank's grip pads (on a sports tank they used to be one rubber
	// slab over the top at the seat end, which no real bike has). Each pad is laid on the tank's real
	// surface a few millimetres thick, with a rounded edge.
	const bool Classic = Look && Look->Get("pads") ? Look->Flag("pads") : ((Style == "cafe" || Style == "peanut") && !P->Flag("lightweight"));
	const bool Grips = Style == "sport" && !(Look && Look->Flag("no_pad"));
	if (Classic || Grips)
	{
		// Along the tank: where the knee comes (as the bodywork sets the knee recesses), kept on the tank's back half.
		const double Front = Body.TankSecs.front().C.z, Length = Body.TankSecs.back().C.z - Front;
		const double Tk = Length > 0.01 ? TorqueSim::Clamp((ToGodot(L.KneeR).z - Front) / Length, 0.55, 0.8) : 0.72;
		const LoftSection PadS = At(Tk);
		const double HalfLong = Grips ? 0.085 : 0.065, HalfTall = Grips ? 0.065 : 0.045;
		const double Thick = Grips ? 0.003 : 0.004, Power = Grips ? 3.0 : 2.4;
		const double Mid = PadS.C.y + (Grips ? 0.0 : -PadS.Down * 0.1);
		for (double Sd : {-1.0, 1.0})
		{
			CodeShapes::GMesh M;
			const int Rings = 12, Round = 64;
			const BinnedSurface Flank(Skin.Near(V3(Sd > 0.0 ? 0.0 : -1e9, Mid - HalfTall - 0.02, PadS.C.z - HalfLong - 0.02),
				V3(Sd > 0.0 ? 1e9 : 0.0, Mid + HalfTall + 0.02, PadS.C.z + HalfLong + 0.02)), 0);
			std::vector<unsigned> Ids;
			bool Whole = false;
			// The whole pad must lie on the flank (not round onto the top or off the edge into the frame's
			// tunnel), so a tank with a small flank there gets a smaller pad.
			for (double Size : {1.0, 0.85, 0.72, 0.6})
			{
				M = CodeShapes::GMesh();
				Ids.clear();
				Whole = true;
				// Rings out from the middle (0) to the edge (Rings), the last one down at the paint for the edge.
				for (int J = 0; J <= Rings + 1 && Whole; ++J)
					for (int I = 0; I < Round; ++I)
					{
						const double R = std::min(static_cast<double>(J), static_cast<double>(Rings)) / Rings;
						const double Ang = I * 2.0 * Pi / Round;
						const double Ca = std::cos(Ang), Sa = std::sin(Ang);
						// (A superellipse outline: rounded-square for the grips, near oval for the classic pads.)
						const double Ex = (Ca < 0.0 ? -1.0 : 1.0) * std::pow(std::fabs(Ca), 2.0 / Power);
						const double Ey = (Sa < 0.0 ? -1.0 : 1.0) * std::pow(std::fabs(Sa), 2.0 / Power);
						const double Zs = PadS.C.z + Ex * HalfLong * Size * R, Ys = Mid + Ey * HalfTall * Size * R;
						V3 Hit(Sd * PadS.W, Ys, Zs), HitN(Sd, 0.0, 0.0);
						if (!Skin.Empty() && (!Flank.Hit(V3(Sd * (PadS.W + 0.4), Ys, Zs), V3(-Sd, 0.0, 0.0), Hit, HitN) || HitN.x * Sd < 0.3))
						{
							Whole = false;
							break;
						}
						// Its thickness: full over the face, rounding off over the outer fifth, and on the face fine
						// ribs (the classic pad's along the tank, the grip's a diagonal tread).
						const double Edge = TorqueSim::Clamp((1.0 - R) / 0.2, 0.0, 1.0);
						double Lift = J > Rings ? -0.001 : Thick * (0.35 + 0.65 * std::sqrt(Edge * (2.0 - Edge)));
						if (J <= Rings && Edge >= 1.0)
						{
							const double Rib = Grips ? std::sin((Zs + Ys) * 2.0 * Pi / 0.009) : std::sin(Ys * 2.0 * Pi / 0.008);
							Lift += 0.0007 * std::max(Rib, 0.0);
						}
						Ids.push_back(M.Add(Hit + HitN * Lift, HitN, R * Ex * 0.5 + 0.5, R * Ey * 0.5 + 0.5));
					}
				if (Whole) break;
			}
			if (!Whole) continue;
			for (int J = 1; J <= Rings + 1; ++J)
				for (int I = 0; I < Round; ++I)
				{
					const int Nx = (I + 1) % Round;
					const unsigned A = Ids[static_cast<size_t>((J - 1) * Round + I)], B = Ids[static_cast<size_t>((J - 1) * Round + Nx)];
					const unsigned C = Ids[static_cast<size_t>(J * Round + I)], D = Ids[static_cast<size_t>(J * Round + Nx)];
					M.Quad(A, B, C, D);
				}
			// Smooth normals off the pad's own faces (the ribs and the rounded edge catch the light).
			std::vector<V3> Acc(M.P.size(), V3());
			for (size_t K = 0; K + 2 < M.Idx.size(); K += 3)
			{
				const V3 Face = (M.P[M.Idx[K + 1]] - M.P[M.Idx[K]]).Cross(M.P[M.Idx[K + 2]] - M.P[M.Idx[K]]);
				for (size_t Q = 0; Q < 3; ++Q) Acc[M.Idx[K + Q]] = Acc[M.Idx[K + Q]] + Face;
			}
			for (size_t K = static_cast<size_t>(Round); K < M.N.size(); ++K)   // (the middle keeps the tank's normal)
				if (Acc[K].Length() > 1e-12) M.N[K] = Acc[K].Normalized() * (Acc[K].Dot(M.N[K]) < 0.0 ? -1.0 : 1.0);
			Code(Grips ? "TankPad" : "", M, GAffine(), "rubber");
		}
	}
	if (Style == "enduro")
		for (double Sd : {-1.0, 1.0})
		{
			// Radiator shrouds flaring forward either side of the tank.
			const V3 C = Body.TankSecs[2].C;
			Code("", CodeShapes::Loft({{C + V3(0.14 * Sd, 0.02, 0.07), 0.012, 0.04, 0.03, 3.0}, {C + V3(0.16 * Sd, -0.06, 0.035), 0.012, 0.08, 0.1, 3.0},
				{C + V3(0.155 * Sd, -0.12, -0.01), 0.012, 0.06, 0.1, 3.0}, {C + V3(0.13 * Sd, -0.16, -0.035), 0.01, 0.02, 0.04, 3.0}}), GAffine(), "paint");
		}
}

void BikeParts::SaddleTrim(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	// The code-drawn pieces of BikeModel._draw_saddle round the Blender seat: the stitching, the
	// front seam and the piping on its sections, a two-up seat's grab rail and pillion pegs, a
	// touring back, a sprung seat's springs and a sissy bar.
	if (!Body.HasSeat || Body.SeatSecs.size() < 17) return;
	const Json* P = Lay.Part(PartIn(Parts, "saddle"));
	const Json* Look = P ? P->Get("look") : nullptr;
	const std::string Style = PartStr(P, "style", "bench");
	const std::string ShapeKey = PartStr(Look, "shape", Style);
	std::string Stand = "saddle/bench.glb";
	for (const FittedPart& F : Body.Parts)
		if (F.Slot == "saddle") Stand = F.Glb;
	const std::vector<LoftSection>& Secs = Body.SeatSecs;
	const int Steps = 16;
	const double N = Body.SeatN;
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	const V3 Tail = ToGodot(L.Tail), Peg = ToGodot(L.Peg), Pivot = ToGodot(L.Pivot);
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "saddle", Stand, "", Named(Name), M, Xf, Tag);
	};
	auto Bend = [&](const std::string& Name, const std::vector<V3>& Path, double R, int Sides, const std::string& Tag) {
		Code(Name, CodeShapes::Sweep(Path, R, Sides), GAffine(), Tag);
	};
	auto Tube = [&](const std::string& Name, const V3& A, const V3& B, double R, const std::string& Tag) {
		Code(Name, CodeShapes::Tube(A, B, R), CodeShapes::TubeAt(A, B), Tag);
	};
	// (_on_loft: a point on a section's superellipse at angle A, grown out by Grow; its underside 0.85 wide.)
	auto OnLoft = [N](const LoftSection& S, double A, double Grow) {
		const double E = 2.0 / N;
		const double Ca = std::cos(A), Sa = std::sin(A);
		const double W = S.W * (Sa < 0.0 ? 0.85 : 1.0);
		const double X = (Ca > 0.0 ? 1.0 : Ca < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Ca), E) * W;
		const double Y = (Sa > 0.0 ? 1.0 : Sa < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Sa), E) * (Sa > 0.0 ? S.Up : S.Down);
		return S.C + V3(X, Y, 0.0) * Grow;
	};
	if (Style == "touring" && !(Look && Look->Get("shape")))
		Code("", CodeShapes::Box(V3(0.3, 0.26, 0.06)), AtWith(GAffine::Rotation(GodotRight, 0.2), V3(0.0, SeatY + 0.22, Body.SeatZ1 + 0.04)), "leather");
	// Stitching: tuck-and-roll pleats across the top, ribs along it, or diamond quilting.
	const std::string Pattern = PartStr(Look, "pattern", "");
	if (Pattern == "tuck")
		for (int I = 2; I < Steps - 1; I += 2)
		{
			std::vector<V3> Path;
			for (int K = 0; K < 7; ++K) Path.push_back(OnLoft(Secs[static_cast<size_t>(I)], 0.35 + (TorqueSim::Pi - 0.7) * K / 6.0, 1.006));
			Bend("", Path, 0.0035, 4, "fabric");
		}
	else if (Pattern == "ribs")
		for (double A : {1.2, 1.45, 1.7, 1.95})
		{
			std::vector<V3> Path;
			for (int I = 1; I < Steps; ++I) Path.push_back(OnLoft(Secs[static_cast<size_t>(I)], A, 1.006));
			Bend("", Path, 0.0035, 4, "fabric");
		}
	else if (Pattern == "diamond")
		for (int I = 1; I < Steps - 2; I += 2)
			for (int K = 0; K < 3; ++K)
			{
				const double A = 0.9 + (TorqueSim::Pi - 1.8) * K / 2.0;
				const size_t U = static_cast<size_t>(I);
				Bend("", {OnLoft(Secs[U], A - 0.25, 1.006), OnLoft(Secs[U + 1], A, 1.006), OnLoft(Secs[U + 2], A - 0.25, 1.006)}, 0.003, 4, "fabric");
			}
	// The front seam and the piping round the edge.
	const LoftSection& Mid = Secs[static_cast<size_t>(std::round(Steps * 0.3))];
	Code("", CodeShapes::Box(V3(Mid.W * 2.02, 0.006, 0.006)), GAffine::At(Mid.C + V3(0.0, Mid.Up * 0.2, 0.0)), "fabric");
	for (double A : {0.12, TorqueSim::Pi - 0.12})
	{
		std::vector<V3> Path;
		for (int I = 1; I < Steps; ++I) Path.push_back(OnLoft(Secs[static_cast<size_t>(I)], A, 1.01));
		Bend("Piping", Path, 0.0038, 5, "fabric");
	}
	// A two-up seat: a grab rail round the back and pillion pegs on hangers.
	const double Sissy = PartNum(Look, "sissy", 0.0);
	static const std::set<std::string> TwoUpShapes = {"bench", "touring", "stepped", "king_queen", "hump_long"};
	if (TwoUpShapes.count(ShapeKey) && Sissy <= 0.0 && !(P && P->Flag("solo")))
	{
		const std::string Rail = Look && Look->Get("accent") ? "chrome" : "black_metal";
		const LoftSection& Back = Secs[static_cast<size_t>(Steps - 1)];
		const LoftSection& FrontS = Secs[static_cast<size_t>(std::round(Steps * 0.55))];
		const double Ry = Back.C.y - Back.Down * 0.3;
		std::vector<V3> Loop;
		for (double Sd : {1.0, -1.0})
		{
			const double W0 = FrontS.W + 0.03, W1 = Back.W + 0.028;
			std::vector<V3> Pts = {V3(W0 * Sd, Ry - 0.01, FrontS.C.z), V3(W1 * Sd, Ry, Back.C.z), V3(W1 * 0.55 * Sd, Ry + 0.005, Back.C.z + 0.07)};
			if (Sd < 0.0) std::reverse(Pts.begin(), Pts.end());
			Loop.insert(Loop.end(), Pts.begin(), Pts.end());
		}
		Loop.insert(Loop.begin() + 3, V3(0.0, Ry + 0.008, Back.C.z + 0.085));
		Bend("GrabRail", Loop, 0.011, 10, Rail);
		for (double Sd : {-1.0, 1.0})
		{
			const V3 At = Sd > 0.0 ? Loop.front() : Loop.back();
			Tube("", At, At + V3(-0.02 * Sd, -0.09, 0.02), 0.009, Rail);
		}
		for (double Sd : {-1.0, 1.0})
		{
			// Behind the swingarm pivot, whatever the rider's own pegs are doing (forward controls).
			const V3 At((Peg.x + 0.02) * Sd, std::max(Peg.y + 0.07, Pivot.y + 0.06), std::max(Peg.z + 0.3, Pivot.z + 0.16));
			const V3 A = At + V3(-0.02 * Sd, 0.0, 0.0), B((Peg.x - 0.02) * Sd, SeatY - 0.12, At.z - 0.06);
			const V3 D = B - A;
			Code("", CodeShapes::Box(V3(0.03, 0.008, D.Length())), AtWith(GAffine::Rotation(GodotRight, std::atan2(-D.y, D.z)), (A + B) * 0.5), "black_metal");
			Tube("PillionPeg", At + V3(-0.015 * Sd, 0.0, 0.0), At + V3(0.065 * Sd, 0.0, 0.0), 0.011, "rubber");
		}
	}
	// A sprung solo seat on a pair of coil springs at the back.
	if (Look && Look->Flag("springs"))
	{
		const LoftSection& Rear = Secs[static_cast<size_t>(Steps)];
		for (double Sd : {-1.0, 1.0})
		{
			const V3 Top = Rear.C + V3(0.07 * Sd, -Rear.Down, -0.02);
			const V3 Foot(Top.x, SeatY - 0.1, Top.z + 0.03);
			for (int K = 0; K < 6; ++K)
				Code("", CodeShapes::Torus(0.017, 0.004), GAffine::At(Top + (Foot - Top) * ((K + 0.5) / 6.0)), "chrome");
		}
	}
	// A sissy bar: a chrome loop standing up behind the seat, Sissy metres tall.
	if (Sissy > 0.0)
	{
		const V3 Root(0.0, Tail.y + 0.02, Tail.z - 0.02);
		for (double Sd : {-1.0, 1.0})
			Bend("", {Root + V3(0.1 * Sd, -0.08, -0.04), Root + V3(0.1 * Sd, Sissy * 0.6, 0.02), Root + V3(0.06 * Sd, Sissy, 0.04), Root + V3(0.0, Sissy + 0.02, 0.045)}, 0.009, 12, "chrome");
		if (Look->Flag("backrest"))
			Code("", CodeShapes::Box(V3(0.2, 0.16, 0.05)), AtWith(GAffine::Rotation(GodotRight, 0.15), Root + V3(0.0, Sissy * 0.55, 0.0)), "leather");
	}
}

BikeParts::Bars BikeParts::Handlebars(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	Bars Bar;
	// Everything here is drawn in the steering's space (BikeModel._steer_node: at the head, square
	// to the bike at rest), then moved to the head.
	const Json* P = Lay.Part(PartIn(Parts, "handlebars"));
	const Json* Look = P ? P->Get("look") : nullptr;
	const double Hw = PartNum(P, "width", 720.0) / 2000.0;
	const double Rise = PartNum(P, "rise", 0.08);
	const V3 H = ToGodot(L.Head);
	const std::string Tube = "frame/tube/round.glb";
	auto TubeAB = [&](const V3& A, const V3& B, double R) { Sweep(Out, "handlebars", Tube, {A + H, B + H}, {R}, "steer"); };
	auto Bend = [&](std::vector<V3> Route, double R, const V3& At, const std::string& Node) {
		for (V3& V : Route) V += At;
		Sweep(Out, "handlebars", Tube, Route, {R}, Node);
	};
	constexpr double GripLen = 0.1;
	constexpr double BarStraight = 0.17;
	std::vector<V3> Line;   // the bar's line as drawn, the middle out to the right end
	V3 Tip;
	bool HaveTip = false;
	const std::string Style = Look && Look->Get("bend") ? std::string("Look") : PartStr(P, "style", "Flat");
	if (Style == "Look")
	{
		// (_bent_bar: one bar bent to its look's shape, each end straight for the grip and levers.)
		const std::string BendKind = PartStr(Look, "bend", "tracker");
		const double Pull = PartNum(Look, "pull", 0.08), Drop = PartNum(Look, "drop", 0.0);
		const double Rh = PartNum(Look, "risers", 0.02), Thick = PartNum(Look, "thick", 0.012);
		std::vector<V3> Right;
		if (BendKind == "drag") Right = {V3(0.0, Rh, 0.0), V3(Hw * 0.6, Rh + Rise * 0.5, Pull * 0.4), V3(Hw, Rh + Rise, Pull)};
		else if (BendKind == "buckhorn") Right = {V3(0.0, Rh, 0.0), V3(0.1, Rh, 0.0), V3(0.17, Rh + Rise, -0.02), V3(Hw * 0.85, Rh + Rise + 0.02, Pull * 0.8), V3(Hw, Rh + Rise - Drop - 0.03, Pull)};
		else if (BendKind == "z") Right = {V3(0.0, Rh, 0.0), V3(0.1, Rh, 0.0), V3(0.12, Rh + 0.02, 0.0), V3(0.13, Rh + Rise - 0.02, 0.01), V3(0.15, Rh + Rise, 0.02), V3(Hw, Rh + Rise - Drop, Pull)};
		else if (BendKind == "t") Right = {V3(0.0, Rh + Rise, 0.0), V3(Hw * 0.5, Rh + Rise, Pull * 0.3), V3(Hw, Rh + Rise - Drop, Pull)};
		else if (BendKind == "clubman") Right = {V3(0.0, Rh, 0.0), V3(0.08, Rh, 0.0), V3(0.15, Rh - Drop * 0.7, Pull * 0.5), V3(Hw, Rh - Drop, Pull)};
		else if (BendKind == "ape") Right = {V3(0.0, Rh, 0.0), V3(0.09, Rh, 0.0), V3(0.12, Rh + Rise * 0.6, -0.01), V3(0.14, Rh + Rise, 0.02), V3(Hw * 0.8, Rh + Rise, Pull * 0.7), V3(Hw, Rh + Rise - Drop - 0.02, Pull)};
		else Right = {V3(0.0, Rh, 0.0), V3(0.1, Rh, 0.0), V3(0.18, Rh + Rise * 0.8, Pull * 0.4), V3(Hw, Rh + Rise - Drop, Pull)};
		{
			// (_straight_ends: the end run straight the way the bar heads into it, within reason.)
			const V3 End = Right.back(), Prev = Right[Right.size() - 2];
			const double Dx = std::max(End.x - Prev.x, 0.02);
			const V3 OutDir = V3(1.0, TorqueSim::Clamp((End.y - Prev.y) / Dx, -0.35, 0.25), TorqueSim::Clamp((End.z - Prev.z) / Dx, -0.1, 0.6)).Normalized();
			const V3 Start = End - OutDir * BarStraight, Lead = End - OutDir * (BarStraight + 0.03);
			std::vector<V3> Kept = {Right[0]};
			for (size_t I = 1; I + 1 < Right.size(); ++I)
				if (Right[I].x < Lead.x - 0.015) Kept.push_back(Right[I]);
			Kept.insert(Kept.end(), {Lead, Start, End});
			Right = Kept;
		}
		std::vector<V3> Path;
		for (size_t I = Right.size() - 1; I > 0; --I) Path.push_back(V3(-Right[I].x, Right[I].y, Right[I].z));
		Path.insert(Path.end(), Right.begin(), Right.end());
		Bend(Path, Thick, H, "steer");
		const std::vector<V3> Drawn = SmoothCurve(Path, 6);
		Line.assign(Drawn.begin() + static_cast<long>((Right.size() - 1) * 6), Drawn.end());
		if (BendKind == "t")
			TubeAB(V3(0.0, -0.01, 0.0), V3(0.0, Rh + Rise, 0.0), 0.022);
		else if (Rh > 0.025)
			for (double Sd : {-1.0, 1.0}) TubeAB(V3(0.04 * Sd, -0.01, 0.0), V3(0.04 * Sd, Rh + 0.005, 0.0), 0.016);
		else
			Put(Out, "handlebars", "controls/bar_clamp.glb", GAffine::At(V3(0.0, Rh, 0.0) + H), "steer");
		if (Look->Flag("brace"))
		{
			// The cross brace, where the bar has risen 60% of the way to its end, no further out than 0.16.
			const double Top = Right.back().y;
			V3 At = Line.back();
			for (size_t I = 1; I < Line.size(); ++I)
				if (Line[I].x >= 0.16 && Line[I - 1].x <= 0.16)
				{
					At = MixV(Line[I - 1], Line[I], (0.16 - Line[I - 1].x) / std::max(Line[I].x - Line[I - 1].x, 0.0001));
					break;
				}
			for (size_t I = 1; I < Line.size(); ++I)
				if (Line[I].y >= TorqueSim::Lerp(Rh, Top, 0.6) && Line[I].x <= 0.16)
				{
					At = Line[I];
					break;
				}
			TubeAB(V3(-At.x, At.y, At.z), At, 0.009);
			// (Its pad is code-drawn: only the clocks need to know where it is.)
			if (Look->Flag("pad")) { Bar.HasPad = true; Bar.Pad = V3(0.0, At.y, At.z); }
		}
		Tip = Right.back();
		HaveTip = true;
		Bar.R = Thick;
	}
	else if (Style == "Clip-ons")
	{
		for (double Sd : {-1.0, 1.0}) TubeAB(V3(0.09 * Sd, -0.07, 0.0), V3(Hw * Sd, Rise, 0.13), 0.013);
		Line = {V3(0.09, -0.07, 0.0), V3(Hw, Rise, 0.13)};
		Bar.R = 0.013;
	}
	else if (Style == "Ape hanger")
	{
		for (double Sd : {-1.0, 1.0})
		{
			const V3 PostTop(0.12 * Sd, Rise, 0.0);
			TubeAB(V3(0.05 * Sd, 0.02, 0.0), PostTop, 0.012);
			TubeAB(PostTop, V3(Hw * Sd, Rise - 0.03, 0.14), 0.012);
		}
		Line = {V3(0.05, 0.02, 0.0), V3(0.12, Rise, 0.0), V3(Hw, Rise - 0.03, 0.14)};
		Bar.R = 0.012;
	}
	else if (Style == "MX braced")
	{
		for (double Sd : {-1.0, 1.0})
		{
			const V3 Mid(0.14 * Sd, Rise, 0.0);
			TubeAB(Mid, V3(Hw * Sd, Rise + 0.03, 0.1), 0.014);
			TubeAB(V3(0.04 * Sd, 0.02, 0.0), Mid, 0.016);
		}
		TubeAB(V3(-0.14, Rise, 0.0), V3(0.14, Rise, 0.0), 0.014);
		TubeAB(V3(-0.12, Rise + 0.07, 0.01), V3(0.12, Rise + 0.07, 0.01), 0.01);
		Line = {V3(0.0, Rise, 0.0), V3(0.14, Rise, 0.0), V3(Hw, Rise + 0.03, 0.1)};
		Bar.R = 0.014;
	}
	else
	{
		TubeAB(V3(-Hw, Rise, 0.06), V3(Hw, Rise, 0.06), 0.012);
		for (double Sd : {-1.0, 1.0}) TubeAB(V3(0.04 * Sd, 0.0, 0.0), V3(0.04 * Sd, Rise, 0.06), 0.014);
		Line = {V3(0.0, Rise, 0.06), V3(Hw, Rise, 0.06)};
		Bar.R = 0.012;
	}
	if (!HaveTip || (Tip.x == 0.0 && Tip.y == 0.0 && Tip.z == 0.0)) Tip = Line.back();
	const V3 OutR = (Tip - Line[Line.size() - 2]).Normalized();
	Bar.Line = Line;
	Bar.Tip = Tip;
	Bar.Out = OutR;
	auto BarEnd = [&](double Sd) { return V3(Tip.x * Sd, Tip.y, Tip.z); };
	auto BarOut = [&](double Sd) { return V3(OutR.x * Sd, OutR.y, OutR.z); };
	// (_bar_end_basis: X along the bar to the rider's right, Y up off it, Z back to the rider.)
	auto EndBasis = [&](double Sd) {
		const V3 X = BarOut(Sd) * Sd;
		const V3 Z = X.Cross(GodotUp).Normalized();
		return BasisCols(X, Z.Cross(X).Normalized(), Z);
	};
	for (double Sd : {-1.0, 1.0})
		Put(Out, "handlebars", "controls/grip.glb", AtWith(BasisUp(BarOut(Sd)), BarEnd(Sd) - BarOut(Sd) * GripLen + H), "steer");

	// The controls on the bar (_draw_controls).
	const GAffine Bl = EndBasis(-1.0), Br = EndBasis(1.0);
	const V3 LEnd = BarEnd(-1.0), REnd = BarEnd(1.0);
	const V3 SwL = LEnd + Bl.X * 0.118, Thr = REnd + Br.X * -0.103, Perch = LEnd + Bl.X * 0.152;
	Put(Out, "handlebars", "controls/switch_left.glb", AtWith(Bl, SwL + H), "steer");
	Put(Out, "handlebars", "controls/throttle.glb", AtWith(Br, Thr + H), "steer");
	for (double Sd : {-1.0, 1.0})
		Put(Out, "handlebars", "controls/bar_end.glb", AtWith(BasisUp(BarOut(Sd)), BarEnd(Sd) + H), "steer");
	Put(Out, "handlebars", "controls/clutch_perch.glb", AtWith(Bl, Perch + H), "steer");
	Put(Out, "handlebars", "controls/lever.glb", AtWith(Bl, Perch + H), "steer");

	// The front brake's master cylinder just inboard of the right grip, and its lever (the clutch
	// lever's, mirrored and reaching to where the code's ends: _draw_front_master).
	{
		const std::string Type = PartStr(Lay.Part(PartIn(Parts, "front_master")), "type", "");
		const V3 GripR = REnd - BarOut(1.0) * (GripLen * 0.5);
		const V3 MAt = GripR + Br.X * -0.08, MTip = GripR + Br.Apply(V3(0.06, -0.012, -0.05));
		const double Reach = Br.X.Dot(MTip - MAt);
		constexpr double LeverReach = 0.127;   // BlenderParts.LEVER_REACH
		Put(Out, "front_master", Type.rfind("Radial", 0) == 0 ? "controls/master_radial.glb" : "controls/master_axial.glb", AtWith(Br, MAt + H), "steer");
		Put(Out, "front_master", "controls/lever.glb", AtWith(Br * GAffine::Scale(V3(-Reach / LeverReach, 1.0, 1.0)), MAt + H), "steer");
		// (_front_brake_line: the hose, or a drum's cable, from the master down to its joint on the
		// fork leg, and the banjo at the master; code-drawn.)
		const Json* ForkPart = Lay.Part(PartIn(Parts, "fork"));
		const std::string ForkStyle = PartStr(ForkPart, "style", "conventional");
		const double ForkX = ForkStyle == "girder" ? 0.09 : (ForkStyle == "springer" || ForkStyle == "leading_link") ? 0.085
			: 0.09 * PartNum(ForkPart ? ForkPart->Get("look") : nullptr, "wide", 1.0);
		const V3 Joint = V3(ForkX + 0.035, 0.0, 0.04) - ToGodot(L.SteerAxis) * 0.3;
		const bool Drum = PartStr(Lay.Part(PartIn(Parts, "front_disc")), "style", "") == "drum";
		const std::vector<V3> Hose = {MAt + Br.Apply(V3(0.0, -0.01, -0.025)), MixV(MAt, Joint, 0.3) + V3(0.0, 0.0, -0.16), Joint};
		CodeShapes::Emit(Out, "front_master", "controls/lever.glb", "steer", "BrakeHose", CodeShapes::Sweep(Hose, Drum ? 0.0042 : 0.005, 8), GAffine::At(H), "rubber");
		CodeShapes::Emit(Out, "front_master", "controls/lever.glb", "steer", "", CodeShapes::Cylinder(0.0075, 0.016), GAffine::At(MAt + Br.Apply(V3(0.0, -0.01, -0.03)) + H), "chrome");
	}

	// The clutch and throttle cables (_draw_cables): down past the head stock, then on in the frame
	// to the clutch arm and the carbs, a Blender-made adjuster where each goes in.
	const V3 Axis = ToGodot(L.SteerAxis);
	const V3 Jl = Axis * -0.24 + V3(-0.055, 0.0, 0.035), Jr = Axis * -0.24 + V3(0.055, 0.0, 0.035);
	constexpr double CableR = 0.0042;
	Bend({Perch + Bl.Apply(V3(0.012, 0.0, -0.05)), MixV(Perch, Jl, 0.35) + V3(0.0, 0.03, -0.14), Jl}, CableR, H, "steer");
	Bend({Thr + Br.Apply(V3(-0.01, 0.0, -0.02)), MixV(Thr, Jr, 0.35) + V3(0.0, 0.03, -0.14), Jr}, CableR, H, "steer");
	auto CableEnd = [&](const V3& At, const V3& From) {
		const V3 F = From.Normalized();
		Put(Out, "handlebars", "controls/cable_end.glb", AtWith(BasisUp(F), At + F * 0.006), "");
	};
	const V3 FJl = H + Jl, FJr = H + Jr;
	const V3 E = ToGodot(L.Crank);
	const V3 ClutchEnd = E + V3(-L.Hard.CaseHalfW - 0.02, 0.07 * L.Hard.Scale, 0.15 * L.Hard.Scale);
	Bend({FJl, FJl + V3(-0.01, -0.06, 0.08), ClutchEnd + V3(-0.015, 0.1, -0.08), ClutchEnd}, CableR, V3(), "");
	CableEnd(ClutchEnd, V3(-0.015, 0.1, -0.08));
	const std::vector<Intake> In = Intakes(Parts, L);
	if (!In.empty())
	{
		const V3 ThrEnd = In[0].Port + In[0].Dir * 0.05 + V3(0.03, 0.045, 0.0);
		Bend({FJr, FJr + V3(0.01, -0.04, 0.08), ThrEnd + V3(0.0, 0.06, -0.06), ThrEnd}, CableR, V3(), "");
		CableEnd(ThrEnd, V3(0.0, 0.06, -0.06));
	}
	return Bar;
}

std::vector<SegTri> BikeParts::BarTriangles(const std::vector<FittedPart>& Out, const Bars& Bar, const V3& H) const
{
	// (BikeModel._bars_mesh: the bars and everything on them, as triangles in steering space.)
	std::vector<SegTri> BarTris;
	if (!Meshes) return BarTris;
	for (const FittedPart& F : Out)
	{
		if (F.Slot != "handlebars") continue;
		PartMesh Glb;
		if (!Meshes(F.Glb, Glb)) continue;
		const PartMesh M = Draw(Glb, F);
		// (BikeModel._bars_mesh walks each mesh's parents up to the steering: what hangs off the
		// bike itself, the cables' frame runs, is taken in the bike's space as it is.)
		const V3 Off = F.Node == "steer" ? H : V3();
		for (const PartSurface& S : M.Surfaces)
			for (size_t I = 0; I + 2 < S.Idx.size(); I += 3)
				BarTris.push_back({ToGodot(S.Pos[S.Idx[I]]) - Off, ToGodot(S.Pos[S.Idx[I + 1]]) - Off, ToGodot(S.Pos[S.Idx[I + 2]]) - Off});
	}
	// A braced bar's pad, code-drawn: a CapsuleMesh (radius 0.025, 0.2 end to end, 64 round and
	// 8 rings) turned across the bar, as Godot tessellates it.
	if (Bar.HasPad)
	{
		constexpr double PadR = 0.025, PadHalf = 0.1 - 0.025;
		constexpr int Segs = 64, Rings = 8;
		std::vector<std::vector<V3>> Rows;
		auto Row = [&](double W, double Y) {
			std::vector<V3> R;
			for (int I = 0; I <= Segs; ++I)
			{
				const double U = 2.0 * TorqueSim::Pi * I / Segs;
				// (Local Y along the capsule, then turned a quarter about Z: local (x, y, z) -> (-y, x, z).)
				const V3 Local(-std::sin(U) * PadR * W, Y, -std::cos(U) * PadR * W);
				R.push_back(Bar.Pad + V3(0.0, 0.005, 0.0) + V3(-Local.y, Local.x, Local.z));
			}
			Rows.push_back(R);
		};
		for (int J = 0; J <= Rings + 1; ++J)
		{
			const double V = static_cast<double>(J) / (Rings + 1);
			Row(std::sin(0.5 * TorqueSim::Pi * V), PadR * std::cos(0.5 * TorqueSim::Pi * V) + PadHalf);
		}
		for (int J = 0; J <= Rings + 1; ++J) Row(1.0, PadHalf - 2.0 * PadHalf * J / (Rings + 1));
		for (int J = 0; J <= Rings + 1; ++J)
		{
			const double V = static_cast<double>(J) / (Rings + 1);
			Row(std::cos(0.5 * TorqueSim::Pi * V), -PadHalf - PadR * std::sin(0.5 * TorqueSim::Pi * V));
		}
		for (size_t J = 1; J < Rows.size(); ++J)
			for (int I = 1; I <= Segs; ++I)
			{
				BarTris.push_back({Rows[J - 1][I - 1], Rows[J - 1][I], Rows[J][I - 1]});
				BarTris.push_back({Rows[J - 1][I], Rows[J][I], Rows[J][I - 1]});
			}
	}
	return BarTris;
}

bool BikeParts::SegmentHits(const std::vector<SegTri>& Tris, const V3& From, const V3& To)
{
	// (TriangleMesh.intersect_segment: Geometry3D.segment_intersects_triangle, both faces, its
	// epsilons too: a triangle small beside the segment counts as edge on and is missed, as in Godot.)
	const V3 D = To - From;
	for (const SegTri& T : Tris)
	{
		const V3 E1 = T.B - T.A, E2 = T.C - T.A, Pv = D.Cross(E2);
		const double Det = E1.Dot(Pv);
		if (std::fabs(Det) < 0.00001) continue;
		const double Inv = 1.0 / Det;
		const V3 Tv = From - T.A;
		const double U = Tv.Dot(Pv) * Inv;
		if (U < 0.0 || U > 1.0) continue;
		const V3 Q = Tv.Cross(E1);
		const double V = D.Dot(Q) * Inv;
		if (V < 0.0 || U + V > 1.0) continue;
		const double Tt = E2.Dot(Q) * Inv;
		if (Tt > 0.00001 && Tt <= 1.0) return true;
	}
	return false;
}

V3 BikeParts::EyeAt(const Layout& L) const
{
	// (BikeModel._eye_point: the rider's eye over the hips, bike's space.)
	return V3(0.0, L.Stats.SeatHeight - L.Stats.RearRadius + 0.88, ToGodot(L.Hip).z - 0.02);
}

double BikeParts::Dashboard(const PartsMap& Parts, const Layout& L, const Bars& Bar, std::vector<FittedPart>& Out, std::vector<V3>* FaceOut) const
{
	// (BikeModel._draw_dashboard, in the steering's space: the clocks lean back 0.9 rad to the rider.)
	const Json* P = Lay.Part(PartIn(Parts, "dashboard"));
	const std::string Style = PartStr(P, "style", "analog");
	const V3 H = ToGodot(L.Head);
	V3 At(0.0, 0.1, -0.06);
	const Json* Fairing = Lay.Part(PartIn(Parts, "fairing"));
	const Json* FLook = Fairing ? Fairing->Get("look") : nullptr;
	if (PartStr(Fairing, "style", "") == "race" && PartStr(FLook, "shape", "race") != "seeley") At = V3(0.0, 0.01, 0.02);   // behind a race screen
	const GAffine Tilt = GAffine::Rotation(GodotRight, 0.9);
	std::vector<std::pair<std::string, V3>> Housings;   // glb, where
	std::vector<V3> Face;                                // points over the faces (_add_face)
	auto AddFace = [&](const V3& C, double Hx, double Hz, bool Round) {
		if (Round)
		{
			Face.push_back(C);
			for (const std::pair<double, int>& Ring : {std::pair<double, int>(0.5, 8), std::pair<double, int>(1.0, 12)})
				for (int I = 0; I < Ring.second; ++I)
				{
					const double A = 2.0 * TorqueSim::Pi * I / Ring.second;
					Face.push_back(C + (Tilt.X * (std::cos(A) * Hx) + Tilt.Z * (std::sin(A) * Hz)) * Ring.first);
				}
			return;
		}
		for (int I = 0; I < 5; ++I)
			for (int J = 0; J < 5; ++J) Face.push_back(C + Tilt.X * ((I * 0.5 - 1.0) * Hx) + Tilt.Z * ((J * 0.5 - 1.0) * Hz));
	};
	if (Style == "analog" || Style == "twin")
	{
		for (double X : Style == "twin" ? std::vector<double>{-0.06, 0.06} : std::vector<double>{0.0})
		{
			const V3 C = At + V3(X, 0.0, 0.0);
			Housings.push_back({"dashboard/dial.glb", C});
			AddFace(C + Tilt.Y * 0.028, 0.04, 0.04, true);
		}
	}
	else if (Style == "lcd" || Style == "tft")
	{
		const bool Tft = Style == "tft";
		const V3 Size = Tft ? V3(0.2, 0.03, 0.12) : V3(0.16, 0.03, 0.09);
		Housings.push_back({Tft ? "dashboard/tft.glb" : "dashboard/lcd.glb", At});
		AddFace(At + Tilt.Y * 0.018, Size.x * 0.5 - 0.01, Size.z * 0.5 - 0.01, false);
	}
	else
	{
		Housings.push_back({"dashboard/bar.glb", At});
		AddFace(At + Tilt.Y * 0.018 + Tilt.Z * 0.012, 0.09, 0.025, false);
	}

	// (_lift_clear_of_bars: the clocks rise in 5 mm steps, up to 0.25, until the bars and what's on
	// them hide none of the faces from the rider's eye, nor from 3 cm under it. It needs the bars'
	// meshes, so without a mesh source the clocks stay where they are.)
	double Lift = 0.0;
	if (Meshes)
	{
		const std::vector<SegTri> BarTris = BarTriangles(Out, Bar, H);
		auto Blocked = [&](const V3& From, const V3& To) { return SegmentHits(BarTris, From, To); };
		const V3 Eye = EyeAt(L) - H;
		auto Hidden = [&](const V3& From0, double Up) {
			for (const V3& Q : Face)
			{
				V3 From = Q + V3(0.0, Up, 0.0);
				From += (From0 - From).Normalized() * 0.002;
				if (Blocked(From, From0)) return true;
			}
			return false;
		};
		const V3 Low = Eye + V3(0.0, -0.03, 0.0);
		if (!BarTris.empty() && !Face.empty())
			while (Lift < 0.25 && (Hidden(Eye, Lift) || Hidden(Low, Lift))) Lift += 0.005;
	}
	for (const auto& [Glb, C] : Housings) Put(Out, "dashboard", Glb, AtWith(Tilt, C + V3(0.0, Lift, 0.0) + H), "steer");
	// The code-drawn faces, ticks, needles, screens and shift lights, raised with the clocks.
	const V3 Up(0.0, Lift, 0.0);
	auto Code = [&](const CodeShapes::GMesh& M, const GAffine& Basis, const V3& C, const std::string& Tag) {
		CodeShapes::Emit(Out, "dashboard", Housings.empty() ? "dashboard/dial.glb" : Housings[0].first, "steer", "", M, AtWith(Basis, C + Up + H), Tag);
	};
	// The last piece put on works for the game (FittedPart::Gauge): At in the clocks' space, as Code's.
	auto Works = [&](const std::string& What, const V3& At2, const V3& Axis, const V3& Upward) {
		FittedPart& F = Out.back();
		F.Gauge = What;
		F.GaugeAt = FromGodot(At2 + Up + H);
		F.GaugeAxis = FromGodot(Axis);
		F.GaugeUp = FromGodot(Upward);
	};
	auto Leds = [&](const V3& C, double Width) {
		static const char* Tags[6] = {"lens_white", "lens_white", "lens_amber", "lens_amber", "lens_red", "lens_red"};
		for (int I = 0; I < 6; ++I)
		{
			const V3 Led = C + Tilt.X * (-Width * 0.5 + Width * (I + 0.5) / 6.0);
			Code(CodeShapes::Box(V3(0.016, 0.004, 0.01)), Tilt, Led, Tags[I]);
			Works("led" + std::to_string(I), Led, Tilt.Y, Tilt.Z * -1.0);
		}
	};
	// (_dash_screen: where the readout is written, its text reading up the face, away from the rider.)
	auto Readout = [&](const V3& At2) { Works("readout", At2, Tilt.Y, Tilt.Z * -1.0); };
	if (Style == "analog" || Style == "twin")
	{
		const bool Twin = Style == "twin";
		for (size_t K = 0; K < Housings.size(); ++K)
		{
			const V3 C = Housings[K].second;
			Code(CodeShapes::Cylinder(0.043, 0.004), Tilt, C + Tilt.Y * 0.026, "dial");
			// The little window under the needle (or between the two dials) for the gear and fuel.
			if (K == 0) Readout(At + Tilt.Y * 0.032 + Tilt.Apply(V3(0.0, 0.0, Twin ? 0.046 : 0.022)));
			for (int I = 0; I < 9; ++I)
			{
				const double A = -2.35 + 4.7 * I / 8.0;
				const V3 TickAt = C + Tilt.Y * 0.029 + Tilt.Apply(V3(std::sin(A) * 0.036, 0.0, -std::cos(A) * 0.036));
				Code(CodeShapes::Box(V3(0.003, 0.002, 0.008)), Tilt * GAffine::Rotation(GodotUp, -A), TickAt, "black_metal");
				// (The ridden bike lays its own numbered scale over these, matched to the bike, and hides them.)
				Works("tick", TickAt, Tilt.Y, Tilt.Z * -1.0);
			}
			// The needle on its pivot (at rest: the game turns it with the speed or revs; the twin's
			// left dial the speed, its right the revs).
			Code(CodeShapes::Box(V3(0.004, 0.003, 0.034)), Tilt, C + Tilt.Y * 0.031 + Tilt.Apply(V3(0.0, 0.0, -0.016)), "lens_red");
			Works(Twin && K == 1 ? "rpm" : "speed", C + Tilt.Y * 0.031, Tilt.Y, Tilt.Z * -1.0);
		}
	}
	else if (Style == "lcd" || Style == "tft")
	{
		const bool Tft = Style == "tft";
		const V3 Size = Tft ? V3(0.2, 0.03, 0.12) : V3(0.16, 0.03, 0.09);
		Code(CodeShapes::Box(V3(Size.x - 0.02, 0.004, Size.z - 0.02)), Tilt, At + Tilt.Y * 0.016, "glass");
		Readout(At + Tilt.Y * 0.019);
		if (Tft) Leds(At + Tilt.Y * 0.017 + Tilt.Apply(V3(0.0, 0.0, -Size.z * 0.5 - 0.008)), 0.14);
	}
	else
	{
		Code(CodeShapes::Box(V3(0.18, 0.004, 0.05)), Tilt, At + Tilt.Y * 0.016 + Tilt.Apply(V3(0.0, 0.0, 0.012)), "glass");
		Readout(At + Tilt.Y * 0.019 + Tilt.Apply(V3(0.0, 0.0, 0.012)));
		Leds(At + Tilt.Y * 0.017 + Tilt.Apply(V3(0.0, 0.0, -0.028)), 0.18);
	}
	// Raised more than a little, the clocks stand on a stalk from the top yoke.
	if (Lift > 0.02)
	{
		const V3 A(0.0, 0.0, At.z), B = At + V3(0.0, Lift - 0.012, 0.0);
		CodeShapes::Emit(Out, "dashboard", Housings[0].first, "steer", "", CodeShapes::Tube(A, B, 0.009), GAffine::At(H) * CodeShapes::TubeAt(A, B), "black_metal");
	}
	if (FaceOut)
		for (const V3& Q : Face) FaceOut->push_back(Q + V3(0.0, Lift, 0.0));
	return Lift;
}

void BikeParts::RearMaster(const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_rear_master: on a stalk inboard of the right footrest, ahead of the pivot.)
	const V3 At(0.15, 0.06, ToGodot(L.Pivot).z - 0.04);
	Sweep(Out, "rear_master", "frame/tube/round.glb", {At, At + V3(0.0, 0.1, 0.03)}, {0.014}, "");
	Put(Out, "rear_master", "controls/rear_master.glb", GAffine::At(At + V3(0.0, 0.16, 0.08)), "");
}

void BikeParts::Headlight(const PartsMap& Parts, std::vector<FittedPart>& Out, const V3& H) const
{
	// (BikeModel._draw_headlight and _blender_lamp: the lamp's body on the steering, in front of
	// the head. Its trim (rings, grilles, visors, brackets, the lamps' eyes) is code-drawn.)
	const Json* P = Lay.Part(PartIn(Parts, "headlight"));
	if (!P) return;
	const Json* Look = P->Get("look");
	const V3 At(0.0, -0.08, -0.13);
	const double Size = P->Num("size", 0.08) * PartNum(Look, "size", 1.0);
	auto Lamp = [&](const std::string& Glb, const GAffine& B, const V3& C) { Put(Out, "headlight", "headlight/" + Glb + ".glb", AtWith(B, C + H), "steer"); };
	auto Sized = [&](const V3& S) { return GAffine::Scale(S * Size); };
	const std::string Kind = PartStr(Look, "lamp", P->Str("style", "round"));
	if (Kind == "bullet") Lamp("bullet", Sized(V3(1, 1, 1)), At);
	else if (Kind == "oval") Lamp("round", Sized(V3(1.3, 0.8, 1.0)), At);
	else if (Kind == "quad")
	{
		for (double X : {-0.045, 0.045})
			for (double Y : {-0.04, 0.04}) Lamp("cell", GAffine(), At + V3(X, Y, 0.0));
	}
	else if (Kind == "stacked")
	{
		for (double Y : {-0.055, 0.055}) Lamp("round", Sized(V3(0.7, 0.7, 0.7)), At + V3(0.0, Y, 0.0));
	}
	else if (Kind == "square") Lamp("square", Sized(V3(1, 1, 1)), At);
	else if (Kind == "bugeye")
	{
		for (double Sd : {-1.0, 1.0})
		{
			const GAffine B = GAffine::Rotation(GodotUp, -Sd * 0.3) * GAffine::Rotation(V3(0.0, 0.0, 1.0), -Sd * 0.28);
			Lamp("bugeye", B * Sized(V3(1, 1, 1)), At + V3(Sd * Size * 1.04, 0.0, 0.0));
		}
	}
	else if (Kind == "triple")
	{
		// (The left's the right-hand housing mirrored across.)
		for (double Sd : {-1.0, 1.0})
			Lamp("triple", GAffine::Scale(V3(Sd, 1.0, 1.0)) * GAffine::Rotation(GodotUp, -0.38) * GAffine::Rotation(V3(0.0, 0.0, 1.0), -0.3) * Sized(V3(1, 1, 1)),
				At + V3(Sd * Size * 1.15, Size * 0.1, 0.0));
	}
	else if (Kind == "nacelle") Lamp("nacelle", Sized(V3(1, 1, 1)), At);
	else if (Kind == "bucket") Lamp("bucket", Sized(V3(1, 1, 1)), At + V3(0.0, 0.0, 0.03));
	else if (Kind == "twin")
	{
		for (double X : {-0.07, 0.07}) Lamp("round", Sized(V3(1.0, 1.0, 0.7 / 0.75)), At + V3(X, 0.0, 0.01));
	}
	else if (Kind == "plate") Lamp("plate", GAffine::Rotation(GodotRight, 0.25), At + V3(0.0, -0.02, -0.02));
	else if (Kind == "projector") Lamp("projector", GAffine(), At);
	else Lamp("round", Sized(V3(1, 1, 1)), At);

	// The code-drawn trim: the lamps' eyes, brows and hot spots, the look's chrome ring, grille,
	// visor and brackets, a bug-eye pair's bracket plate and fly-screen, the arms to the fork legs.
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "headlight", "headlight/round.glb", "steer", Named(Name), M, GAffine::At(H) * Xf, Tag);
	};
	auto Arm = [&](const V3& A, const V3& B, double R, const std::string& Tag) { Code("", CodeShapes::Tube(A, B, R), CodeShapes::TubeAt(A, B), Tag); };
	const GAffine Flat = GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5);   // (a cylinder or ring turned to face forward)
	V3 Front = At + V3(0.0, 0.0, -Size * 0.7);
	double FaceR = Size * 0.9;
	if (Kind == "bullet") Front = At + V3(0.0, 0.0, -Size * 0.45);
	else if (Kind == "nacelle") Front = At + V3(0.0, 0.0, -Size * 0.52);
	else if (Kind == "quad" || Kind == "stacked") FaceR = 0.0;
	else if (Kind == "square")
	{
		FaceR = 0.0;
		Code("", CodeShapes::Cylinder(Size * 0.26, 0.004), AtWith(Flat, At + V3(0.0, 0.0, -Size * 0.43)), "lens_white");
		if (!(Look && Look->Flag("brackets")))
			for (double Sd : {-1.0, 1.0}) Arm(At + V3(Size * 2.3 * 0.45 * Sd, 0.0, Size * 0.2), V3(0.09 * Sd, -0.02, 0.0), 0.008, "black_metal");
	}
	else if (Kind == "bugeye")
	{
		FaceR = 0.0;
		const double Gap = Size * 1.04;
		for (double Sd : {-1.0, 1.0})
		{
			const V3 C = At + V3(Sd * Gap, 0.0, 0.0);
			const GAffine B = GAffine::Rotation(GodotUp, -Sd * 0.3) * GAffine::Rotation(V3(0.0, 0.0, 1.0), -Sd * 0.28);
			const GAffine Oval = B * GAffine::Scale(V3(0.88, 1.18, 1.0));
			const V3 FaceAt = C + B.Apply(V3(0.0, 0.0, -Size)), Eye = FaceAt + B.Apply(V3(0.0, 0.0, -0.006));
			// (_bugeye_eye: the reflector's ring and the bulb's hot spot in front of the glass.)
			Code("", CodeShapes::Torus(Size * 0.55, 0.0018), AtWith(Oval * Flat, Eye), "chrome");
			Code("", CodeShapes::Cylinder(Size * 0.3, 0.003), AtWith(Oval * Flat, Eye), "lens_white");
			Arm(C + B.Apply(V3(0.0, -Size * 0.3, Size * 0.9)), V3(0.09 * Sd, -0.02, 0.0), 0.008, "black_metal");
		}
		Code("LampBracket", CodeShapes::Box(V3(Gap * 2.0, 0.012, Size * 0.9)), GAffine::At(At + V3(0.0, -Size * 0.55, Size * 0.95)), "black_metal");
		if (!Lay.Part(PartIn(Parts, "fairing")))
		{
			// A small tinted fly-screen over the pair, raked back.
			const V3 Dir(0.0, std::cos(0.55), std::sin(0.55)), Base = At + V3(0.0, Size, -Size * 0.2);
			std::vector<CodeShapes::Sec> Secs;
			for (int K = 0; K < 4; ++K)
			{
				const double F = K / 3.0;
				Secs.push_back({Base + Dir * (0.1 * F), Gap * 1.9 * (1.0 - 0.3 * F), 0.03, 0.0, 2.0});
			}
			Code("FlyScreen", CodeShapes::Loft(Secs, 28, true, 0.25, TorqueSim::Pi - 0.25), GAffine(), "glass");
		}
	}
	else if (Kind == "triple")
	{
		FaceR = 0.0;
		const double Gap = Size * 1.15;
		for (double Sd : {-1.0, 1.0})
		{
			// (_triple_trim: the projector's eye, the light brow over it and the arm to the fork leg.)
			const V3 C = At + V3(Sd * Gap, Size * 0.1, 0.0);
			const GAffine B = GAffine::Rotation(GodotUp, -Sd * 0.38) * GAffine::Rotation(V3(0.0, 0.0, 1.0), -Sd * 0.3);
			Code("", CodeShapes::Cylinder(Size * 0.17, 0.004), AtWith(B * Flat, C + B.Apply(V3(-Sd * Size * 0.4, -Size * 0.08, -Size * 0.25))), "lens_white");
			if (!Look || !Look->Get("brow") || Look->Flag("brow"))
				Code("DRL", CodeShapes::Box(V3(Size * 1.8, Size * 0.07, 0.008)), AtWith(B, C + B.Apply(V3(0.0, Size * 0.28, -Size * 0.25))), "lens_white");
			Arm(C + B.Apply(V3(Sd * Size * 0.3, -Size * 0.2, Size * 0.9)), V3(0.09 * Sd, -0.02, 0.0), 0.008, "black_metal");
		}
		Code("LampBracket", CodeShapes::Box(V3(Gap * 1.2, Size * 0.6, Size * 0.9)), GAffine::At(At + V3(0.0, -Size * 0.05, Size * 0.5)), "black_metal");
	}
	else if (Kind == "projector")
		Code("", CodeShapes::Box(V3(0.15, 0.008, 0.004)), GAffine::At(At + V3(0.0, -0.035, -0.047)), "lens_white");
	if (FaceR > 0.0)
	{
		if (Look && Look->Flag("ring")) Code("", CodeShapes::Torus(FaceR, 0.009), AtWith(Flat, Front + V3(0.0, 0.0, -0.004)), "chrome");
		if (Look && Look->Flag("grille"))
		{
			for (int K = 0; K < 3; ++K)
				Code("", CodeShapes::Box(V3(FaceR * 1.7, 0.006, 0.006)), GAffine::At(Front + V3(0.0, (K - 1) * FaceR * 0.5, -0.012)), "black_metal");
			Code("", CodeShapes::Box(V3(0.006, FaceR * 1.7, 0.006)), GAffine::At(Front + V3(0.0, 0.0, -0.014)), "black_metal");
		}
		if (Look && Look->Flag("visor"))
			Code("Visor", CodeShapes::Loft({{Front + V3(0.0, FaceR * 0.15, 0.02), FaceR * 1.05, FaceR * 1.05, FaceR * 1.05, 2.0},
				{Front + V3(0.0, FaceR * 0.15, -0.05), FaceR, FaceR * 0.95, FaceR * 0.95, 2.0}}, 28, true, 0.2, TorqueSim::Pi - 0.2), GAffine(), "paint");
	}
	if (Look && Look->Flag("brackets"))
		for (double Sd : {-1.0, 1.0}) Arm(At + V3(Size * 1.05 * Sd, 0.0, 0.0), V3(0.09 * Sd, -0.02, 0.0), 0.007, "chrome");
}

void BikeParts::Mirrors(const PartsMap& Parts, const Layout& L, const Bars& Bar, const std::map<std::string, V3>& FairingMounts, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_mirrors: each head turned to the rider's eye on its stalk, which is
	// code-drawn; the Blender pieces are the bar clamp and the head's shell.)
	const Json* P = Lay.Part(PartIn(Parts, "mirrors"));
	if (!P) return;
	const Json* Look = P->Get("look");
	std::string Style = PartStr(Look, "shape", P->Str("style", "round"));
	const double K = PartNum(Look, "size", 1.0);
	const Json* Fairing = Lay.Part(PartIn(Parts, "fairing"));
	if (Style == "fairing" && !Fairing) Style = "sport";   // nothing to bolt to: shown as stalks
	const V3 H = ToGodot(L.Head);
	const V3 Eye(0.0, L.Stats.SeatHeight - L.Stats.RearRadius + 0.88, ToGodot(L.Hip).z - 0.02);
	constexpr double ClampBack = 0.18, Rise = 0.04, Depth = 0.02;
	// (_mirror_aim: half way between back at the rider's eyes and back past their shoulder.)
	auto Aim = [&](const V3& At, bool OnBars) {
		const V3 Pm = OnBars ? At + H : At;
		const double Sx = Pm.x > 0.0 ? 1.0 : Pm.x < 0.0 ? -1.0 : 0.0;
		const V3 AimDir = V3(0.16 * Sx, 0.0, 1.0).Normalized();
		const V3 N = ((Eye - Pm).Normalized() + AimDir).Normalized();
		const V3 X = GodotUp.Cross(N).Normalized();
		return BasisCols(X, N.Cross(X), N);
	};
	// (The code-drawn pieces: on the steering in its space, else the bike's. Godot numbers like-named
	// pieces on the same node in turn; each head's glass is in its own head.)
	CodeShapes::GodotNames Named, NamedOnBike;
	auto Code = [&](bool OnBars, const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "mirrors", "mirror/clamp.glb", OnBars ? "steer" : "", OnBars ? Named(Name) : NamedOnBike(Name), M,
			OnBars ? GAffine::At(H) * Xf : Xf, Tag);
	};
	double BarLen = 0.0;
	for (size_t I = 1; I < Bar.Line.size(); ++I) BarLen += (Bar.Line[I] - Bar.Line[I - 1]).Length();
	// (bar_point: the point on the right of the bar `Back` in from its end, and the bar's way there.)
	auto BarPoint = [&](double Back, V3& At, V3& Along) {
		double Left = std::max(Back, 0.0);
		for (size_t I = Bar.Line.size() - 1; I > 0; --I)
		{
			const V3 A = Bar.Line[I - 1], B = Bar.Line[I];
			const double D = (B - A).Length();
			if (Left <= D || I == 1)
			{
				At = MixV(B, A, TorqueSim::Clamp(Left / std::max(D, 0.0001), 0.0, 1.0));
				Along = (B - A).Normalized();
				return;
			}
			Left -= D;
		}
	};
	// (_mirror_clamp: a collar on the bar inboard of the levers; the stalk starts on its boss.)
	auto Clamp = [&](double Sd) {
		V3 C, Along;
		BarPoint(std::min(ClampBack, BarLen - 0.03), C, Along);
		C.x *= Sd;
		Along.x *= Sd;
		const V3 UpDir = (GodotUp - Along * Along.y).Normalized();
		Put(Out, "mirrors", "mirror/clamp.glb", AtWith(BasisUp(Along) * GAffine::Scale(V3(Bar.R + 0.005, 1.0, Bar.R + 0.005)), C + H), "steer");
		const V3 Top = C + UpDir * (Bar.R + 0.012);
		Code(true, "", CodeShapes::Tube(C, Top, 0.009), CodeShapes::TubeAt(C, Top), "paint");   // the boss
		return Top;
	};
	// (_mirror_on_stalk and _mirror_head: the head slides back along its face's line until the
	// stalk meets it from behind, and its shell is scaled to its size.)
	auto Head = [&](const V3& From, V3 At, double Sx, double Sy, double R, bool OnBars, double StalkRise) {
		const V3 Last = StalkRise > 0.0 ? From + GodotUp * StalkRise : From;
		const double Need = -(Depth * 0.5 + R * 1.5 + 0.012);
		for (int I = 0; I < 3; ++I)
		{
			const V3 N = Aim(At, OnBars).Z;
			const double D = (Last - At).Dot(N);
			if (D <= Need) break;
			At += N * (D - Need);
		}
		Sx *= K;
		Sy *= K;
		const bool Round = Sy <= 0.0;
		const GAffine Basis = Aim(At, OnBars);
		Put(Out, "mirrors", Round ? "mirror/round.glb" : "mirror/rect.glb",
			AtWith(Basis * GAffine::Scale(V3(Sx, Round ? Sx : Sy, 1.0)), At + (OnBars ? H : V3())), OnBars ? "steer" : "");
		// The glass on the head's face (in the head: its name is its own).
		const GAffine HeadXf = AtWith(Basis, At);
		if (Round)
			Code(OnBars, "", CodeShapes::Cylinder(Sx * 0.88, 0.002), HeadXf * GAffine::At(V3(0.0, 0.0, 0.011)) * GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5), "chrome");
		else
			Code(OnBars, "", CodeShapes::Box(V3(Sx - 0.01, Sy - 0.01, 0.002)), HeadXf * GAffine::At(V3(0.0, 0.0, 0.011)), "chrome");
		Out.back().Name = "MirrorGlass";
		// The stalk from its mount to a ball joint behind the head, round the rim to the back if the
		// head faces it, and a short stem from the joint into the head's back.
		constexpr double Stem = 0.016;
		std::vector<V3> Path = {From};
		if (StalkRise > 0.0) Path.push_back(From + GodotUp * StalkRise);
		const V3 Back = Basis.Z * -1.0;
		const V3 Knuckle = At + Back * (Depth * 0.5 + Stem), StemEnd = At + Back * (Depth * 0.5 - 0.004);
		const V3 Rel = Last - At;
		const double RelDepth = Rel.Dot(Basis.Z);
		if (RelDepth > -(Depth * 0.5 + R))
		{
			const V3 Flat = Rel - Basis.Z * RelDepth;
			const double Reach = (Round ? Sx : std::sqrt(Sx * Sx + Sy * Sy) * 0.5) + R + 0.015;
			const V3 RoundTo = Flat.Length() > 0.001 ? Flat.Normalized() * std::max(Flat.Length(), Reach) : Basis.Y * -Reach;
			Path.push_back(At + RoundTo + Back * (Depth * 0.5 + Stem));
		}
		Path.push_back(Knuckle);
		for (size_t I = 0; I + 1 < Path.size(); ++I)
		{
			Code(OnBars, "MirrorStalk", CodeShapes::Tube(Path[I], Path[I + 1], R), CodeShapes::TubeAt(Path[I], Path[I + 1]), "paint");
			if (I > 0) Code(OnBars, "", CodeShapes::Sphere(R), GAffine::At(Path[I]), "paint");
		}
		Code(OnBars, "MirrorJoint", CodeShapes::Sphere(R * 1.5), GAffine::At(Knuckle), "paint");
		Code(OnBars, "MirrorStem", CodeShapes::Tube(Knuckle, StemEnd, R * 0.9), CodeShapes::TubeAt(Knuckle, StemEnd), "paint");
	};
	for (double Sd : {-1.0, 1.0})
	{
		if (Style == "bar_end")
		{
			const V3 From = Bar.End(Sd) + Bar.OutAt(Sd) * 0.019;
			Head(From, From + Bar.OutAt(Sd) * 0.012 + V3(0.06 * Sd, -0.035, 0.05), 0.045, 0.0, 0.008, true, 0.0);
		}
		else if (Style == "sport")
		{
			const V3 From = Clamp(Sd);
			Head(From, From + V3(0.12 * Sd, 0.15, 0.0), 0.13, 0.06, 0.007, true, Rise);
		}
		else if (Style == "rect")
		{
			const V3 From = Clamp(Sd);
			Head(From, From + V3(0.12 * Sd, 0.27, 0.0), 0.13, 0.09, 0.008, true, Rise);
		}
		else if (Style == "fairing" && Fairing->Flag("frame_mount"))
		{
			// (On the set's own mounts on its upper panels, else off the code fairing's edge by the lamp.)
			const auto M = FairingMounts.find(Sd > 0.0 ? "mirror_r" : "mirror_l");
			if (FairingMounts.count("mirror_r") && M != FairingMounts.end())
				Head(M->second, M->second + V3(0.05 * Sd, 0.03, 0.03), 0.11, 0.055, 0.01, false, 0.0);
			else
			{
				const double Wide = Fairing->Str("style") == "touring" ? 0.27 : 0.2;
				const V3 Lp = H + V3(0.0, -0.08, -0.13);
				Head(Lp + V3((Wide - 0.01) * Sd, 0.08, 0.04), Lp + V3((Wide + 0.05) * Sd, 0.1, 0.06), 0.11, 0.055, 0.01, false, 0.0);
			}
		}
		else if (Style == "fairing")
		{
			const V3 Lamp(0.0, -0.08, -0.13);
			Head(Lamp + V3(0.12 * Sd, 0.06, 0.03), Lamp + V3(0.2 * Sd, 0.08, 0.05), 0.1, 0.05, 0.009, true, 0.0);
		}
		else
		{
			const V3 From = Clamp(Sd);
			Head(From, From + V3(0.08 * Sd, 0.2, 0.0), 0.05, 0.0, 0.007, true, Rise);
		}
	}
}

void BikeParts::Indicators(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_indicators and _blender_indicator: a pair at the front on the steering and
	// a pair by the rear light, each head a Blender piece on a code-drawn stalk. With no set
	// fitted, a road bike keeps its little stock stalks, drawn with the headlight and the tail.)
	const Json* P = Lay.Part(PartIn(Parts, "indicators"));
	const V3 H = ToGodot(L.Head), Tail = ToGodot(L.Tail);
	const Json* TailPart = Lay.Part(PartIn(Parts, "tail"));
	V3 Rear;
	bool HasRear = false;
	if (TailPart)
	{
		const Json* At = TailPart->Get("light_at");
		const bool Two = At && At->IsArray() && At->A.size() >= 2;
		Rear = Tail + V3(0.0, Two ? At->A[0].N : 0.0, Two ? At->A[1].N : 0.25);
		HasRear = true;
	}
	auto Set = [&](const std::string& Style, bool Front, bool Back, const std::string& FrontSlot, const std::string& BackSlot) {
		const V3 RearAt = HasRear ? Rear : Tail + V3(0.0, -0.04, 0.12);
		const double Reach = Style == "stalk" ? 0.09 : Style == "bullet" ? 0.07 : Style == "mini" ? 0.05 : Style == "flush" ? 0.0 : Style == "touring" ? 0.13 : 0.08;
		const bool Known = Style == "flush" || Style == "bullet" || Style == "mini" || Style == "touring";
		const std::string Glb = "indicator/" + (Known ? Style : std::string("stalk")) + ".glb";
		for (double Sd : {-1.0, 1.0})
			for (bool AtFront : {true, false})
			{
				if ((AtFront && !Front) || (!AtFront && !Back)) continue;
				const V3 Base = AtFront ? V3(0.08 * Sd, -0.05, -0.08) : RearAt + V3(0.05 * Sd, -0.02, -0.04);
				V3 Tip = Base + V3(Reach * Sd, 0.0, 0.0);
				if (Style == "flush")
					Tip = AtFront ? V3(0.075 * Sd, -0.08, -0.17) : (HasRear ? Rear : Tail + V3(0.0, -0.04, 0.12)) + V3(0.06 * Sd, 0.0, 0.0);
				if (!Put(Out, AtFront ? FrontSlot : BackSlot, Glb,
						AtWith(AtFront ? GAffine() : GAffine::Rotation(GodotUp, TorqueSim::Pi), Tip + (AtFront ? H : V3())), AtFront ? "steer" : "")
					|| Style == "flush")
					continue;
				// Its stalk out from the base, code-drawn.
				const double R = Style == "bullet" ? 0.005 : Style == "mini" ? 0.004 : Style == "touring" ? 0.008 : 0.007;
				const V3 Off = AtFront ? H : V3();
				CodeShapes::Emit(Out, AtFront ? FrontSlot : BackSlot, Glb, AtFront ? "steer" : "", "", CodeShapes::Tube(Base, Tip, R),
					GAffine::At(Off) * CodeShapes::TubeAt(Base, Tip), Style == "bullet" ? "chrome" : "black_metal");
			}
	};
	if (P)
	{
		Set(P->Str("style", "stalk"), true, true, "indicators", "indicators");
		return;
	}
	// (_stock_indicators: none on a race or MX frame, none at the front behind a fairing.)
	const std::vector<std::string> FrameStyles = Lay.StylesOf(PartIn(Parts, "frame"));
	bool Competition = !FrameStyles.empty();
	for (const std::string& S : FrameStyles) Competition = Competition && (S == "race" || S == "mx");
	if (Competition) return;
	const bool Front = Lay.Part(PartIn(Parts, "headlight")) && !Lay.Part(PartIn(Parts, "fairing"));
	if (Front) Set("stalk", true, false, "headlight", "headlight");
	if (HasRear) Set("stalk", false, true, "tail", "tail");
}

bool BikeParts::SectionAlong(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Path, const std::vector<V3>& Pts,
	const std::vector<V3>& Ups, double Width, bool Tip, const std::string& Node) const
{
	// (BlenderParts.section_along: the section's 0..1 along its +Z laid along the points' line,
	// its X across (narrowing to a point for a tip) and its Y out along the ups.)
	if (!HasGlb(Path) || Pts.size() < 2) return false;
	auto Lengths = std::make_shared<std::vector<double>>(1, 0.0);
	for (size_t I = 1; I < Pts.size(); ++I) Lengths->push_back(Lengths->back() + (Pts[I] - Pts[I - 1]).Length());
	const double Total = std::max(Lengths->back(), 0.0001);
	FittedPart P;
	P.Slot = Slot;
	P.Glb = Path;
	P.Node = Node;
	P.Warp = GodotWarpToOurs([Pts, Ups, Lengths, Total, Width, Tip](const V3& V) {
		const double At = TorqueSim::Clamp(V.z, 0.0, 1.0) * Total;
		size_t K = 1;
		while (K < Pts.size() - 1 && (*Lengths)[K] < At) ++K;
		const double F = TorqueSim::Clamp((At - (*Lengths)[K - 1]) / std::max((*Lengths)[K] - (*Lengths)[K - 1], 0.0001), 0.0, 1.0);
		const V3 Along = (Pts[K] - Pts[K - 1]).Normalized();
		V3 Up = MixV(Ups[K - 1], Ups[K], F);
		Up = (Up - Along * Up.Dot(Along)).Normalized();
		const V3 Across = Up.Cross(Along).Normalized();
		const double W = Width * (Tip ? std::sqrt(std::max(1.0 - V.z * V.z, 0.0)) : 1.0);
		return MixV(Pts[K - 1], Pts[K], F) + Across * (V.x * W) + Up * V.y;
	});
	Out.push_back(P);
	return true;
}

void BikeParts::GuardTip(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Section, const std::vector<V3>& Path,
	const std::vector<V3>& Ups, double Width, const std::string& Node) const
{
	// (BikeModel._guard_tip: its face square to the path at each point.)
	const size_t Rows = Path.size();
	std::vector<V3> Normals;
	for (size_t I = 0; I < Rows; ++I)
	{
		const V3 D = (Path[std::min(I + 1, Rows - 1)] - Path[I > 0 ? I - 1 : 0]).Normalized();
		Normals.push_back((Ups[I] - D * Ups[I].Dot(D)).Normalized());
	}
	SectionAlong(Out, Slot, "guard/" + Section + "_tip.glb", Path, Normals, Width, true, Node);
}

void BikeParts::GuardArc(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Section, const V3& C, double Radius,
	double Width, double A0, double A1, int Segs, const std::string& Node) const
{
	// (BikeModel._guard: plates round the wheel, each bent from the section, and the leading end
	// rounded off carrying on round it.)
	const double Step = (A1 - A0) / Segs;
	auto Round = [](double A) { return V3(0.0, std::cos(A), std::sin(A)); };
	for (int I = 0; I < Segs; ++I)
	{
		const double A = A0 + Step * (I + 0.5);
		std::vector<V3> Pts, Ups;
		for (int K = 0; K < 3; ++K)
		{
			Ups.push_back(Round(A + Step * (K - 1) * 0.5));
			Pts.push_back(C + Ups.back() * Radius);
		}
		SectionAlong(Out, Slot, "guard/" + Section + ".glb", Pts, Ups, Width, false, Node);
	}
	constexpr int TipRows = 6;
	const double Reach = TorqueSim::Clamp(Width * 0.4, 0.035, 0.08) / Radius;
	std::vector<V3> Path, Ups;
	for (int I = 0; I < TipRows; ++I)
	{
		const double A = A0 - Reach * I / (TipRows - 1.0);
		Ups.push_back(Round(A));
		Path.push_back(C + Ups.back() * Radius);
	}
	GuardTip(Out, Slot, Section, Path, Ups, Width, Node);
}

void BikeParts::FrontMudguard(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (BikeModel._draw_front_mudguard: a low guard round the tyre on the fork legs, or a high one
	// under the bottom yoke on the steering. Its stays, rib and brackets are code-drawn.)
	const Json* P = Lay.Part(PartIn(Parts, "front_mudguard"));
	if (!P) return;
	const std::string Material = P->Str("material", "plastic");
	std::string Style = P->Str("style", "hugger");
	const double R = L.Stats.FrontRadius;
	double W = L.Stats.FrontTyreWidth + 0.03;
	const std::string Section = Style == "classic" ? "valanced" : (Material == "plastic" && (Style == "high" || Style == "enduro")) ? "plastic" : "steel";
	if (Style == "high" || Style == "enduro")
	{
		// (Under an adventure or touring fairing's beak the guard hugs the tyre.)
		const Json* F = Lay.Part(PartIn(Parts, "fairing"));
		if (F && F->Flag("frame_mount"))
		{
			const Json* FL = F->Get("look");
			const std::string Shape = PartStr(FL, "shape", F->Str("style", ""));
			if (Shape == "adventure" || Shape == "touring") Style = "hugger";
		}
	}
	if (Style == "high" || Style == "enduro")
	{
		const V3 H = ToGodot(L.Head);
		const V3 Axle = V3(0.0, 0.0, -L.Stats.Wheelbase * 0.5) - H;
		const V3 Top = Axle + V3(0.0, R + 0.1, 0.0);
		const V3 Yoke = ToGodot(L.SteerAxis) * -0.16;
		// (Laid out in the steering's space, then moved to the head.)
		const V3 Base = V3(0.0, std::min(Top.y, Yoke.y - 0.03), TorqueSim::Lerp(Top.z, Yoke.z, 0.5)) + H;
		const bool Long = Style == "enduro";
		const V3 Back = Base + V3(0.0, -0.04, Long ? 0.14 : 0.08);
		const V3 Mid = Base + V3(0.0, 0.0, -0.08);
		const V3 Nose = Base + V3(0.0, 0.06, Long ? -0.3 : -0.34);
		// (_plank and _guard_blade: a flat blade A to B, its face up off the line; _plank_tip.)
		struct Plank { V3 A, B; double W; };
		std::vector<Plank> Blades = {{Back, Base, W + 0.02}, {Base, Mid, W + 0.01}, {Mid, Nose, W}};
		if (Long) Blades.push_back({Nose, Nose + V3(0.0, -0.05, -0.03), W * 0.8});
		auto UpOf = [](const Plank& Pl) {
			const V3 D = (Pl.B - Pl.A).Normalized();
			return V3(0.0, D.z, -D.y);
		};
		for (const Plank& Pl : Blades)
		{
			const V3 M = (Pl.A + Pl.B) * 0.5, D = Pl.B - Pl.A;
			const V3 Up = UpOf(Pl);
			SectionAlong(Out, "front_mudguard", "guard/" + Section + ".glb", {M - D * 0.5, M, M + D * 0.5}, {Up, Up, Up}, Pl.W, false, "steer");
		}
		auto PlankTip = [&](const Plank& Pl, bool AtB) {
			const V3 Dir = (Pl.B - Pl.A).Normalized() * (AtB ? 1.0 : -1.0);
			const V3 Start = (Pl.A + Pl.B) * 0.5 + Dir * ((Pl.B - Pl.A).Length() * 0.5);
			const double Reach = TorqueSim::Clamp(Pl.W * 0.4, 0.035, 0.08);
			std::vector<V3> Path, Ups;
			for (int I = 0; I < 6; ++I)
			{
				Path.push_back(Start + Dir * (Reach * I / 5.0));
				Ups.push_back(UpOf(Pl));
			}
			GuardTip(Out, "front_mudguard", Section, Path, Ups, Pl.W, "steer");
		};
		PlankTip(Blades.back(), true);
		PlankTip(Blades.front(), false);
		// Held up off the bottom yoke on a pair of stays.
		for (double Sd : {-1.0, 1.0})
		{
			const V3 A = Base + V3(0.05 * Sd, 0.0, 0.0), B = Yoke + H + V3(0.05 * Sd, -0.02, 0.0);
			CodeShapes::Emit(Out, "front_mudguard", "guard/" + Section + ".glb", "steer", "", CodeShapes::Tube(A, B, 0.008), CodeShapes::TubeAt(A, B), "black_metal");
		}
		return;
	}
	double Gap = 0.03, A0 = -0.5, A1 = 0.75;
	if (Style == "sport") { Gap = 0.02; A0 = -0.35; A1 = 0.65; W -= 0.01; }
	else if (Style == "cafe") { Gap = 0.035; A0 = -0.75; A1 = 0.95; W -= 0.02; }
	else if (Style == "bobbed") { Gap = 0.03; A0 = -0.3; A1 = 0.45; }
	else if (Style == "classic") { Gap = 0.045; A0 = -1.0; A1 = 1.3; W += 0.03; }
	const Json* Look = P->Get("look");
	Gap = PartNum(Look, "gap", Gap);
	A0 = PartNum(Look, "a0", A0);
	A1 = PartNum(Look, "a1", A1);
	W += PartNum(Look, "wider", 0.0);
	const V3 Axle = ToGodot(L.FrontAxle);
	const double Radius = R + Gap;
	GuardArc(Out, "front_mudguard", Section, Axle, Radius, W, A0, A1, Style == "classic" ? 10 : 6, "front_hub");
	// The code-drawn pieces on the hub: a rib along the crown, accent stripes, the brackets to the
	// fork legs and a valanced or blade guard's wire stays.
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "front_mudguard", "guard/" + Section + ".glb", "front_hub", Named(Name), M, GAffine::At(Axle) * Xf, Tag);
	};
	auto Arc = [&](double X, double Out_) {
		std::vector<V3> Pts;
		for (int I = 0; I < 9; ++I)
		{
			const double A = TorqueSim::Lerp(A0, A1, I / 8.0);
			Pts.push_back(V3(X, 0.0, 0.0) + V3(0.0, std::cos(A), std::sin(A)) * Out_);
		}
		return Pts;
	};
	const bool Ribbed = Look && Look->Flag("rib");
	if (Ribbed) Code("GuardRib", CodeShapes::Sweep(Arc(0.0, Radius + 0.008), 0.006, 12), GAffine(), "chrome");
	if (Look && Look->Get("accent"))
		for (double X : {-0.03, 0.03}) Code("", CodeShapes::Sweep(Arc(X, Radius + 0.007), 0.004, 12), GAffine(), "paint_2");
	for (double Sd : {-1.0, 1.0}) Code("", CodeShapes::Box(V3(0.03, 0.02, 0.05)), GAffine::At(V3(0.09 * Sd, Radius * 0.92, 0.0)), "black_metal");
	if (Style == "classic" || Style == "cafe" || Style == "bobbed")
	{
		const std::vector<double> Ends = Style != "bobbed" ? std::vector<double>{A0 + 0.2, A1 - 0.2} : std::vector<double>{A1 - 0.1};
		for (double Sd : {-1.0, 1.0})
		{
			const double X = (W * 0.5 + 0.01) * Sd;
			for (double A : Ends)
			{
				const V3 From(X, 0.0, 0.0), To = From + V3(0.0, std::cos(A), std::sin(A)) * Radius;
				Code("", CodeShapes::Tube(From, To, 0.005), CodeShapes::TubeAt(From, To), "chrome");
			}
		}
	}
}

std::vector<BikeParts::Intake> BikeParts::Intakes(const PartsMap& Parts, const Layout& L) const
{
	std::vector<Intake> Points;
	// (A V-twin breathes out of the right-hand side of the vee.)
	if (PartStr(Lay.Part(PartIn(Parts, "engine")), "layout", "parallel") == "vtwin")
		Points.push_back({ToGodot(L.Crank) + V3(0.04, 0.3 * L.Hard.Scale, 0.0), V3(1.0, 0.05, 0.0).Normalized()});
	else
		for (const Cylinder& C : Cylinders(Parts, L)) Points.push_back({C.Base + C.Up * 0.12 + V3(0.0, 0.0, 0.09), V3(0.0, 0.15, 1.0).Normalized()});
	return Points;
}

// BikeModel._obb_push: how far, and which way, box A has to move to come out of box B (the
// separating axes that could part them), zero when they don't overlap.
V3 BikeParts::ObbPush(const Obb& A, const Obb& B)
{
	std::vector<V3> Axes;
	for (int I = 0; I < 3; ++I)
	{
		Axes.push_back(A.H[I].Normalized());
		Axes.push_back(B.H[I].Normalized());
	}
	for (int I = 0; I < 3; ++I)
		for (int J = 0; J < 3; ++J)
		{
			const V3 N = A.H[I].Cross(B.H[J]);
			if (N.Length() > 1e-6) Axes.push_back(N.Normalized());
		}
	double Least = 1e18;
	V3 Way;
	const V3 Gap = A.C - B.C;
	for (const V3& N : Axes)
	{
		double Reach = 0.0;
		for (int I = 0; I < 3; ++I) Reach += std::fabs(A.H[I].Dot(N)) + std::fabs(B.H[I].Dot(N));
		const double Overlap = Reach - std::fabs(Gap.Dot(N));
		if (Overlap <= 0.0) return V3();
		if (Overlap < Least)
		{
			Least = Overlap;
			Way = N * (Gap.Dot(N) >= 0.0 ? Overlap : -Overlap);
		}
	}
	return Way;
}

void BikeParts::TailUnit(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "tail"));
	if (!P) return;
	const Json* Look = P->Get("look");
	const std::string Style = P->Str("style", "classic");
	const BuildStats& St = L.Stats;
	const V3 Tail = ToGodot(L.Tail);
	const V3 Axle(0.0, 0.0, St.Wheelbase * 0.5);
	const double R = St.RearRadius, W = St.RearTyreWidth + 0.05;
	V3 Lamp = Tail;
	if (const Json* At = P->Get("light_at"); At && At->A.size() == 2) Lamp = Tail + V3(0.0, At->A[0].N, At->A[1].N);
	else Lamp = Tail + V3(0.0, 0.0, 0.25);
	const std::string Section = Style == "classic" ? "valanced" : Style == "enduro" ? "plastic" : "steel";
	const std::string Guard = "guard/" + Section + ".glb";
	constexpr double GuardGap = 0.015;
	constexpr int TipRows = 6;
	auto TipLength = [](double Width) { return TorqueSim::Clamp(Width * 0.4, 0.035, 0.08); };
	// (_rear_guard_r: the guard stands off the tyre at full bump, so further out over the top.)
	auto GuardR = [&](double A, double Radius) {
		const double T = St.RearTravel, Need = St.RearRadius + GuardGap, Sa = T * std::sin(A);
		return std::max(Radius, T * std::cos(A) + std::sqrt(std::max(Need * Need - Sa * Sa, 0.0)));
	};
	auto GuardAt = [&](double A, double Radius) { return Axle + V3(0.0, std::cos(A), std::sin(A)) * GuardR(A, Radius); };
	auto InSeat = [&](const V3& At) { return At.y > Tail.y - 0.02 && At.z < Tail.z + 0.04; };
	// The rear mudguard's pieces as boxes (BikeModel._rear_guard, as _obb takes them: the code's
	// flat plates, which a redrawn piece keeps as its box), for the number plate to keep clear of.
	std::vector<Obb> GuardBoxes;
	auto BoxOf = [](const V3& C, const GAffine& B, const V3& Size, double Grow) {
		return Obb{C, {B.X * (Size.x * 0.5 + Grow), B.Y * (Size.y * 0.5 + Grow), B.Z * (Size.z * 0.5 + Grow)}};
	};
	// (_guard_tip's box: its rows' points across, top and bottom, in its own frame.)
	auto TipBox = [](const std::vector<V3>& Path, const std::vector<V3>& Ups, double Width, double Thick) {
		const size_t Rows = Path.size(), Mid = Rows / 2;
		std::vector<V3> Normals;
		for (size_t I = 0; I < Rows; ++I)
		{
			const V3 D = (Path[std::min(I + 1, Rows - 1)] - Path[I > 0 ? I - 1 : 0]).Normalized();
			Normals.push_back((Ups[I] - D * Ups[I].Dot(D)).Normalized());
		}
		const V3 Along = (Path[Rows - 1] - Path[0]).Normalized();
		const V3 Up = (Normals[Mid] - Along * Normals[Mid].Dot(Along)).Normalized();
		const V3 Ax = Up.Cross(Along).Normalized();
		V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
		for (size_t I = 0; I < Rows; ++I)
		{
			const double T = static_cast<double>(I) / static_cast<double>(Rows - 1);
			const double Half = Width * 0.5 * std::sqrt(std::max(1.0 - T * T, 0.0));
			for (int J = 0; J <= 6; ++J)
				for (double K : {0.5, -0.5})
				{
					const V3 Off = Path[I] + V3(-Half + 2.0 * Half * J / 6.0, 0.0, 0.0) + Normals[I] * (Thick * K) - Path[Mid];
					const V3 Lc(Off.Dot(Ax), Off.Dot(Up), Off.Dot(Along));
					Lo = V3(std::min(Lo.x, Lc.x), std::min(Lo.y, Lc.y), std::min(Lo.z, Lc.z));
					Hi = V3(std::max(Hi.x, Lc.x), std::max(Hi.y, Lc.y), std::max(Hi.z, Lc.z));
				}
		}
		const V3 Cl = (Lo + Hi) * 0.5, Hs = (Hi - Lo) * 0.5;
		return Obb{Path[Mid] + Ax * Cl.x + Up * Cl.y + Along * Cl.z, {Ax * Hs.x, Up * Hs.y, Along * Hs.z}};
	};
	// (_rear_arc: plates round the wheel, those that would run up into the seat left out, both
	// ends rounded off unless that would.)
	auto RearArc = [&](double Radius, double Width, double A0, double A1, int Segs) {
		const double Step = (A1 - A0) / Segs;
		double First = 1e9, Last = -1e9;
		bool Any = false;
		for (int I = 0; I < Segs; ++I)
		{
			const double A = A0 + Step * (I + 0.5);
			if (GuardR(A, Radius) > Radius + 0.002 && InSeat(GuardAt(A, Radius))) continue;
			std::vector<V3> Pts, Ups;
			for (int K = 0; K < 3; ++K)
			{
				Pts.push_back(GuardAt(A + Step * (K - 1) * 0.5, Radius));
				Ups.push_back((Pts.back() - Axle).Normalized());
			}
			SectionAlong(Out, "tail", Guard, Pts, Ups, Width, false, "");
			const V3 D = GuardAt(A + Step * 0.5, Radius) - GuardAt(A - Step * 0.5, Radius);
			GuardBoxes.push_back(BoxOf(GuardAt(A, Radius), GAffine::Rotation(GodotRight, std::atan2(-D.y, D.z)), V3(Width, 0.012, D.Length() * 1.1), 0.0));
			First = std::min(First, A - Step * 0.5);
			Last = std::max(Last, A + Step * 0.5);
			Any = true;
		}
		if (!Any) return;
		const double Reach = TipLength(Width) / Radius;
		for (const auto& [End, Way] : {std::make_pair(First, -1.0), std::make_pair(Last, 1.0)})
		{
			std::vector<V3> Path, Ups;
			for (int I = 0; I < TipRows; ++I)
			{
				Path.push_back(GuardAt(End + Way * Reach * I / (TipRows - 1.0), Radius));
				Ups.push_back((Path.back() - Axle).Normalized());
			}
			if (InSeat(Path.back())) continue;
			GuardTip(Out, "tail", Section, Path, Ups, Width, "");
			GuardBoxes.push_back(TipBox(Path, Ups, Width, 0.012));
		}
	};
	// A flat piece of the guard (a tray or a blade, as _box or _plank makes it: centre, basis and
	// size), and how deep the tyre at full bump runs into it (_tyre_depth on its _obb).
	struct Piece { V3 C; GAffine B; V3 Size; bool Valid = true; };
	const Json* Tyre = Lay.Part(PartIn(Parts, "rear_tyre"));
	const std::string TyreStyle = PartStr(Tyre, "style", "road");
	auto TyreDepth = [&](const Piece& Pc) {
		const V3 BumpAxle(0.0, St.RearTravel, St.Wheelbase * 0.5);
		const double Half = St.RearTyreWidth * 0.5;
		const double Bulge = St.RearRimRadius + (R - St.RearRimRadius) * 0.4;
		const double Pw = TyreStyle == "slick" || TyreStyle == "road" ? 2.0 : 2.8;
		const V3 Ax = Pc.B.X * (Pc.Size.x * 0.5), Ay = Pc.B.Y * (Pc.Size.y * 0.5), Az = Pc.B.Z * (Pc.Size.z * 0.5);
		double Worst = -1e18;
		for (int I = 0; I < 5; ++I)
			for (int J = 0; J < 5; ++J)
				for (double K : {-1.0, 1.0})
				{
					const V3 At = Pc.C + Ax * (I * 0.5 - 1.0) + Ay * K + Az * (J * 0.5 - 1.0);
					const double X = std::fabs(At.x) / Half;
					if (X >= 1.0) continue;
					const double Crown = Bulge + (R - Bulge) * std::pow(1.0 - std::pow(X, Pw), 1.0 / Pw);
					const double Dy = At.y - BumpAxle.y, Dz = At.z - BumpAxle.z;
					Worst = std::max(Worst, Crown - std::sqrt(Dy * Dy + Dz * Dz));
				}
		return Worst;
	};
	// (_lift_clear_of_tyre: up until the tyre at full bump is clear; one that ends up in the seat
	// is left out.)
	auto LiftClear = [&](Piece& Pc) {
		const double Start = Pc.C.y;
		for (int I = 0; I < 40; ++I)
		{
			const double D = TyreDepth(Pc);
			if (D <= 0.0) break;
			Pc.C.y += D + 0.003;
		}
		if (Pc.C.y > Start + 0.002 && InSeat(Pc.C + V3(0.0, -0.02, 0.0))) Pc.Valid = false;
	};
	// (_plank: a blade from A to B, turned about X to lie along it.)
	auto PlankOf = [](const V3& A, const V3& B, double Width, double Thick) {
		const V3 D = B - A;
		return Piece{(A + B) * 0.5, GAffine::Rotation(GodotRight, std::atan2(-D.y, D.z)), V3(Width, Thick, D.Length())};
	};
	if (Style == "cafe") RearArc(R + 0.03, W, -0.25, 0.75, 5);
	else if (Style == "loop") RearArc(R + 0.04, W, -0.3, 0.8, 5);
	else if (Style == "bobber") RearArc(R + 0.035, W, -0.55, 1.05, 6);
	else if (Style == "tidy")
	{
		Piece Tray{Tail + V3(0.0, -0.05, 0.1), GAffine::Rotation(GodotRight, -0.15), V3(0.18, 0.025, 0.28)};
		LiftClear(Tray);
		if (Tray.Valid && Put(Out, "tail", "tail/tidy_tray.glb", AtWith(Tray.B, Tray.C), ""))
		{
			// (Its Blender pieces stand in the guard for it, each its own glb box placed with it.)
			PartMesh Glb;
			if (Meshes && Meshes("tail/tidy_tray.glb", Glb))
				for (const PartSurface& Sf : Glb.Surfaces)
				{
					V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
					for (const V3& V : Sf.Pos)
					{
						const V3 G = ToGodot(V);
						Lo = V3(std::min(Lo.x, G.x), std::min(Lo.y, G.y), std::min(Lo.z, G.z));
						Hi = V3(std::max(Hi.x, G.x), std::max(Hi.y, G.y), std::max(Hi.z, G.z));
					}
					if (Sf.Pos.empty()) continue;
					GuardBoxes.push_back(BoxOf(Tray.C + Tray.B.Apply((Lo + Hi) * 0.5) - Tray.B.O, Tray.B, Hi - Lo, 0.0));
				}
		}
		Put(Out, "tail", "tail/lamp_bracket.glb", GAffine::At(Lamp - V3(0.0, 0.0, 0.012)), "");
	}
	else if (Style == "enduro")
	{
		const V3 A = Tail + V3(0.0, -0.05, -0.12);
		const V3 C = Tail + V3(0.0, 0.06, 0.34);
		const V3 B = MixV(A, C, 0.4);
		Piece Planks[2] = {PlankOf(A, B, W + 0.02, 0.012), PlankOf(B, C, W, 0.012)};
		const double Was[2] = {Planks[0].C.y, Planks[1].C.y};
		for (Piece& Pl : Planks) LiftClear(Pl);
		if (Planks[0].Valid && Planks[1].Valid)
		{
			const double Up = std::max(Planks[0].C.y - Was[0], Planks[1].C.y - Was[1]);
			for (int I = 0; I < 2; ++I) Planks[I].C.y = Was[I] + Up;
			Lamp = MixV(C, B, 0.3) + V3(0.0, Up - 0.035, 0.0);
		}
		// (_plank_tip at the far end, then each blade bent from the section: _guard_blade.)
		if (Planks[1].Valid)
		{
			const GAffine& Bs = Planks[1].B;
			const V3 Dir = Bs.Z.Normalized();
			const V3 Start = Planks[1].C + Dir * (Planks[1].Size.z * 0.5);
			const double Reach = TipLength(Planks[1].Size.x);
			std::vector<V3> Path, Ups;
			for (int I = 0; I < TipRows; ++I)
			{
				Path.push_back(Start + Dir * (Reach * I / (TipRows - 1.0)));
				Ups.push_back(Bs.Y.Normalized());
			}
			GuardTip(Out, "tail", Section, Path, Ups, Planks[1].Size.x, "");
			GuardBoxes.push_back(TipBox(Path, Ups, Planks[1].Size.x, Planks[1].Size.y));
		}
		for (const Piece& Pl : Planks)
		{
			if (!Pl.Valid) continue;
			std::vector<V3> Pts, Ups;
			for (int K = 0; K < 3; ++K)
			{
				Pts.push_back(Pl.C + Pl.B.Z * (Pl.Size.z * (K - 1) * 0.5));
				Ups.push_back(Pl.B.Y.Normalized());
			}
			SectionAlong(Out, "tail", Guard, Pts, Ups, Pl.Size.x, false, "");
			GuardBoxes.push_back(BoxOf(Pl.C, Pl.B, Pl.Size, 0.0));
		}
	}
	else
		RearArc(R + 0.05, W + 0.02, -0.45, 1.75, 10);

	// The code-drawn pieces round it (BikeModel._draw_tail): struts, brackets and the number plate.
	CodeShapes::GodotNames Named;
	const std::string Stand = "tail_light/round.glb";
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "tail", Stand, "", Named(Name), M, Xf, Tag);
	};
	auto Tube = [&](const V3& A, const V3& B, double Rad, const std::string& Tag) { Code("", CodeShapes::Tube(A, B, Rad), CodeShapes::TubeAt(A, B), Tag); };
	// On a lofted cowl (_tail_cowl): a tracker's oval number board, a solo cowl's back stop, the
	// accent's stripe along the top.
	if (!Body.CowlKind.empty() && Body.CowlKind != "sport" && Body.CowlKind != "racing")
	{
		const double Ln = PartNum(Look, "length", 1.0);
		const V3 T = Body.CowlT;
		if (Body.CowlKind == "tracker")
			Code("", CodeShapes::Cylinder(0.12, 0.012),
				AtWith(GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5 - 0.15) * GAffine::Scale(V3(1.15, 1.0, 0.75)), T + V3(0.0, 0.07, 0.31 * Ln)), "paint_2");
		else if (Body.CowlKind == "solo")
			Code("", CodeShapes::Box(V3(0.18, 0.08, 0.04)), GAffine::At(T + V3(0.0, 0.1, 0.2 * Ln)), "paint");
		if (Look && Look->Get("accent"))
		{
			std::vector<V3> Stripe;
			for (const LoftSection& Sec : Body.CowlSecs) Stripe.push_back(Sec.C + V3(0.0, Sec.Up + 0.002, 0.0));
			Code("", CodeShapes::Sweep(Stripe, 0.012, 12), GAffine(), "paint_2");
		}
	}
	if (Look && Look->Flag("struts"))
		for (double Sd : {-1.0, 1.0})
		{
			const double X = (W * 0.5 + 0.015) * Sd;
			Tube(Axle + V3(X, 0.0, 0.0), Axle + V3(X, R * 0.78, -0.12), 0.011, "chrome");
			Tube(Axle + V3(X, 0.0, 0.0), Axle + V3(X, R * 0.6, 0.3), 0.009, "chrome");
		}
	struct Plate { V3 At; GAffine B; V3 Size; };
	std::vector<Plate> Plates;
	auto NumberPlate = [&](const V3& At, double Sx, double Sy, double Tilt) { Plates.push_back({At, GAffine::Rotation(GodotRight, Tilt), V3(Sx, Sy, 0.005)}); };
	if (Style == "cafe")
	{
		Code("", CodeShapes::Box(V3(0.24, 0.12, 0.3)), AtWith(GAffine::Rotation(GodotRight, -0.08), Tail + V3(0.0, 0.0, 0.14)), "paint");
		Code("", CodeShapes::Box(V3(0.2, 0.07, 0.24)), AtWith(GAffine::Rotation(GodotRight, 0.22), Tail + V3(0.0, 0.08, 0.1)), "paint");
		Tube(Tail + V3(0.0, -0.05, 0.24), Tail + V3(0.0, -0.15, 0.31), 0.008, "black_metal");
		NumberPlate(Tail + V3(0.0, -0.2, 0.32), 0.18, 0.12, -0.2);
	}
	else if (Style == "loop")
	{
		for (double Sd : {-1.0, 1.0})
		{
			Tube(Tail + V3(0.07 * Sd, 0.0, -0.08), Tail + V3(0.07 * Sd, 0.01, 0.2), 0.012, "paint");
			Tube(Tail + V3(0.07 * Sd, 0.01, 0.2), Tail + V3(0.04 * Sd, 0.04, 0.25), 0.012, "paint");
		}
		Tube(Tail + V3(-0.04, 0.04, 0.25), Tail + V3(0.04, 0.04, 0.25), 0.012, "paint");
		Tube(Tail + V3(0.0, 0.01, 0.2), Lamp, 0.008, "black_metal");
		NumberPlate(Lamp + V3(0.0, -0.09, 0.0), 0.16, 0.11, -0.1);
	}
	else if (Style == "tidy")
	{
		Tube(Tail + V3(0.0, -0.06, 0.2), Tail + V3(0.0, -0.15, 0.33), 0.01, "black_metal");
		NumberPlate(Tail + V3(0.0, -0.2, 0.34), 0.17, 0.11, -0.35);
	}
	else if (Style == "bobber")
	{
		const V3 PlateAt = Axle + V3(-(W * 0.5 + 0.06), 0.14, 0.04);
		Tube(Axle + V3(-(W * 0.5 + 0.02), 0.04, 0.0), PlateAt, 0.008, "black_metal");
		Plates.push_back({PlateAt, GAffine::Rotation(GodotUp, TorqueSim::Pi * 0.5), V3(0.17, 0.12, 0.005)});
	}
	else if (Style == "enduro")
		NumberPlate(Lamp + V3(0.0, -0.07, -0.01), 0.13, 0.08, -0.3);
	else
	{
		// The lamp on a bracket off the guard, the plate below it.
		const double A = std::atan2(Lamp.z - Axle.z, Lamp.y - Axle.y);
		Tube(Lamp, GuardAt(A, R + 0.05), 0.01, "paint");
		const double Below = A + 0.4;
		NumberPlate(GuardAt(Below, R + 0.05) + V3(0.0, std::cos(Below), std::sin(Below)) * 0.03, 0.2, 0.13, -0.25);
	}
	// (_clear_plates: each plate moved the shortest way off the guard, PLATE_GAP to spare, out
	// through its face either way, out from the wheel, back or down, on a bracket from where it was.)
	constexpr double PlateGap = 0.004;
	for (Plate& Pl : Plates)
	{
		auto Clear = [&](const V3& At) {
			const Obb A = BoxOf(At, Pl.B, Pl.Size, PlateGap);
			for (const Obb& G : GuardBoxes)
				if (ObbPush(A, G).Length() > 0.0) return false;
			return true;
		};
		const V3 Start = Pl.At;
		const V3 Face = Pl.B.Z.Normalized();
		V3 Best;
		double BestD = 1e18;
		for (const V3& Way : {Face, Face * -1.0, V3(0.0, Start.y - Axle.y, Start.z - Axle.z).Normalized(), V3(0.0, 0.0, 1.0), V3(0.0, -1.0, 0.0)})
			for (int I = 0; I < 16; ++I)
			{
				double D = I * 0.01;
				if (D >= BestD) break;
				if (!Clear(Start + Way * D)) continue;
				while (D > 0.0)
				{
					if (!Clear(Start + Way * (D - 0.002))) break;
					D -= 0.002;
				}
				Best = Way * D;
				BestD = D;
				break;
			}
		Pl.At = Start + Best;
		Code("NumberPlate", CodeShapes::Box(Pl.Size), AtWith(Pl.B, Pl.At), "decal");
		if (Best.Length() > 0.01) Tube(Start, Pl.At, 0.006, "black_metal");
	}
	// The rear light (_blender_tail_light), at the lamp.
	static const char* Lamps[] = {"tombstone", "cateye", "twin", "halo", "lucas", "strip", "bullet", "led"};
	const std::string Kind = PartStr(Look, "lamp", P->Str("light", "round"));
	const bool Known = std::find_if(std::begin(Lamps), std::end(Lamps), [&](const char* K) { return Kind == K; }) != std::end(Lamps);
	Put(Out, "tail", "tail_light/" + (Known ? Kind : std::string("round")) + ".glb", GAffine::At(Lamp), "");
}

void BikeParts::Stands(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	// (Drawn as Godot draws them with no stand_pose: folded, as on the ride and in the shed.)
	const std::string Tube = "frame/tube/round.glb";
	const V3 Pv = ToGodot(L.Pivot);
	if (const Json* P = Lay.Part(PartIn(Parts, "side_stand")))
	{
		const V3 Pivot(-0.12, -0.04, Pv.z - 0.16);
		const V3 Foot = Pivot + V3(-0.03, -0.04, P->Num("length", 0.3) * 0.95);
		Sweep(Out, "side_stand", Tube, {Pivot, Foot}, {0.012}, "");
		if (Put(Out, "side_stand", "stand/foot.glb", GAffine::At(Foot), ""))
			Put(Out, "side_stand", "stand/pivot.glb", GAffine::At(Pivot), "");
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "centre_stand")))
	{
		const double Z = Pv.z - 0.08;
		const double Reach = L.Stats.RearRadius - 0.06 + P->Num("lift", 0.04);
		for (double Side : {-1.0, 1.0})
		{
			const V3 Top(0.1 * Side, -0.06, Z);
			const V3 Foot = Top + V3(0.0, -0.08, Reach * 0.9);
			Sweep(Out, "centre_stand", Tube, {Top, Foot}, {0.013}, "");
			Put(Out, "centre_stand", "stand/foot.glb", AtWith(GAffine::Scale(V3(1.0, 1.0, 0.06 / 0.07)), Foot), "");
		}
		const double BarZ = Z + Reach * 0.9, BarY = -0.14;
		Sweep(Out, "centre_stand", Tube, {V3(-0.1, BarY + 0.04, BarZ), V3(0.1, BarY + 0.04, BarZ)}, {0.01}, "");
		Sweep(Out, "centre_stand", Tube, {V3(-0.1, BarY + 0.04, BarZ), V3(-0.2, BarY + 0.08, BarZ + 0.05)}, {0.01}, "");
	}
}

void BikeParts::Rearsets(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "rearsets"));
	if (!P) return;
	const std::string Style = P->Str("style", "road");
	const std::string Tube = "frame/tube/round.glb";
	const V3 Pg = ToGodot(L.Peg), Pv = ToGodot(L.Pivot);
	constexpr double HangerT = 0.012;   // (BlenderParts.HANGER_T)
	auto TubeAB = [&](const V3& A, const V3& B, double R) { Sweep(Out, "rearsets", Tube, {A, B}, {R}, ""); };
	for (double Side : {-1.0, 1.0})
	{
		const V3 Peg(Pg.x * Side, Pg.y, Pg.z);
		const V3 Root(0.1 * Side, Pv.y - 0.02, Pv.z - 0.03);
		const GAffine Mirror = GAffine::Scale(V3(Side, 1.0, 1.0));
		auto Plate = [&](const V3& At, const V3& Size) {
			Put(Out, "rearsets", "controls/hanger.glb", AtWith(GAffine::Scale(V3(Side * Size.x / HangerT, Size.y, Size.z)), At), "");
		};
		if (Style == "forward")
		{
			TubeAB(V3(0.08 * Side, Pg.y + 0.02, Pg.z + 0.08), Peg, 0.012);
			Put(Out, "rearsets", "frame/footpeg.glb", AtWith(Mirror * GAffine::Scale(V3(0.9, 1.45, 1.45)), Peg + V3(0.005 * Side, 0.0, 0.0)), "");
			TubeAB(Peg + V3(-0.02 * Side, 0.0, -0.02), Peg + V3(-0.02 * Side, 0.08, -0.08), 0.008);
		}
		else if (Style == "boards")
		{
			TubeAB(V3(0.08 * Side, Pg.y + 0.01, Pg.z), V3(0.14 * Side, Pg.y, Pg.z), 0.01);
			Put(Out, "rearsets", "controls/floorboard.glb", AtWith(Mirror, V3(0.2 * Side, Pg.y, Pg.z)), "");
		}
		else if (Style == "rearset")
		{
			Plate((Peg + Root) * 0.5 + V3(0.015 * Side, 0.0, 0.0), V3(0.01, std::fabs(Peg.y - Root.y) + 0.07, std::fabs(Peg.z - Root.z) + 0.07));
			Put(Out, "rearsets", "controls/peg_alloy.glb", AtWith(Mirror, Peg), "");
			TubeAB(Peg + V3(0.0, -0.01, 0.0), Peg + V3(-0.005 * Side, -0.02, -0.13), 0.006);
		}
		else if (Style == "cleat")
		{
			Plate((Peg + Root) * 0.5, V3(0.012, std::fabs(Peg.y - Root.y) + 0.06, 0.06));
			Put(Out, "rearsets", "controls/cleat.glb", AtWith(Mirror, Peg), "");
			TubeAB(Peg + V3(-0.03 * Side, -0.01, 0.0), Peg + V3(-0.03 * Side, -0.02, -0.14), 0.007);
		}
		else
		{
			if (std::fabs(Peg.y - Root.y) > 0.04 || std::fabs(Peg.z - Root.z) > 0.06)
				Plate((Peg + Root) * 0.5, V3(0.012, std::fabs(Peg.y - Root.y) + 0.06, 0.06));
			Put(Out, "rearsets", "frame/footpeg.glb", AtWith(Mirror, Peg + V3(0.01 * Side, 0.0, 0.0)), "");
			TubeAB(Peg + V3(-0.03 * Side, 0.0, 0.0), Peg + V3(-0.03 * Side, -0.03, -0.15), 0.007);
		}
	}
}

void BikeParts::HornAndEcu(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	if (const Json* P = Lay.Part(PartIn(Parts, "horn")))
	{
		const V3 At = ToGodot(L.HeadBottom) + V3(0.0, -0.1, 0.07);
		const std::string Style = P->Str("style", "disc");
		auto Horn = [&](const std::string& Glb, const GAffine& Xf) { Put(Out, "horn", "horn/" + Glb + ".glb", Xf, ""); };
		if (Style == "snail")
			for (double X : {-0.045, 0.045}) Horn("snail", GAffine::At(At + V3(X, 0.0, 0.0)));
		else if (Style == "air")
		{
			const Json* Tones = P->Get("tones");
			const int N = Tones ? static_cast<int>(Tones->A.size()) : 2;
			for (int I = 0; I < N; ++I)
			{
				const double Len = 0.12 + 0.03 * I;
				Horn("trumpet", AtWith(GAffine::Scale(V3(1.0, 1.0, Len)), At + V3((I - (N - 1) * 0.5) * 0.04, 0.0, -0.1 * Len)));
			}
			Horn("compressor", GAffine::At(At + V3(0.0, -0.03, 0.04)));
		}
		else if (Style == "melody")
			for (int I = 0; I < 5; ++I)
			{
				const double Len = 0.08 + 0.02 * I;
				Horn("trumpet", AtWith(GAffine::Scale(V3(0.022 / 0.03, 0.022 / 0.03, Len)), At + V3((I - 2) * 0.026, 0.0, -0.03 + 0.2 * Len)));
			}
		else if (Style == "klaxon" || Style == "bulb") Horn(Style, GAffine::At(At));
		else Horn("disc", GAffine::At(At));
	}
	if (Lay.Part(PartIn(Parts, "ecu")))
	{
		const V3 SeatFront = ToGodot(L.SeatFront), Tail = ToGodot(L.Tail);
		const V3 At(0.12, L.Stats.SeatHeight - L.Stats.RearRadius - 0.17, TorqueSim::Lerp(SeatFront.z, Tail.z, 0.3));
		Put(Out, "ecu", "ecu/box.glb", GAffine::At(At), "");
		// Its loom, forward and in to the frame.
		const V3 A = At + V3(0.0, 0.04, -0.06), B = At + V3(-0.08, 0.1, -0.16);
		CodeShapes::Emit(Out, "ecu", "ecu/box.glb", "", "", CodeShapes::Tube(A, B, 0.006), CodeShapes::TubeAt(A, B), "rubber");
	}
}

void BikeParts::SidePanels(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "side_panels"));
	if (!P) return;
	const std::string Style = P->Str("style", "triangle");
	const std::string Path = "side_panels/" + Style + ".glb";
	const double PivotY = ToGodot(L.Pivot).y, PivotZ = ToGodot(L.Pivot).z;
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	const V3 C(0.0, TorqueSim::Lerp(PivotY, SeatY, 0.55), PivotZ + 0.07);
	const double H = TorqueSim::Clamp((SeatY - PivotY) * 0.62, 0.1, 0.24);
	const double K = H / 0.2;
	for (double Side : {-1.0, 1.0})
		Put(Out, "side_panels", Path, AtWith(GAffine::Scale(V3(Side, K, Style == "chrome" || Style == "oval" ? K : 1.0)), C + V3(0.115 * Side, 0.0, 0.0)), "");
}

void BikeParts::Guards(const PartsMap& Parts, const Layout& L, const Bars& Bar, std::vector<FittedPart>& Out) const
{
	const std::string Tube = "frame/tube/round.glb";
	const V3 H = ToGodot(L.Head), Hb = ToGodot(L.HeadBottom), E = ToGodot(L.Crank);
	const V3 Pv = ToGodot(L.Pivot), Pg = ToGodot(L.Peg), SeatFront = ToGodot(L.SeatFront), Tail = ToGodot(L.Tail);
	const double S = L.Hard.Scale;
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	auto Line = [&](const std::string& Slot, const std::vector<V3>& Pts, double R, bool Steer = false) {
		std::vector<V3> Route = Pts;
		if (Steer) for (V3& P : Route) P += H;
		Sweep(Out, Slot, Tube, Route, {R}, Steer ? "steer" : "");
	};
	if (const Json* P = Lay.Part(PartIn(Parts, "guards")))
	{
		const std::string Style = P->Str("style", "bars");
		const bool Shiny = Style == "bars" || Style == "wrap";
		double OutX = L.Hard.CaseHalfW + 0.07;
		if (PartStr(Lay.Part(PartIn(Parts, "engine")), "layout", "parallel") == "boxer") OutX = std::max(OutX, 0.36 * S + 0.06);
		const double Bottom = E.y - 0.13 * S, Nose = E.z - 0.17 * S;
		if (Style == "bars" || Style == "cage")
		{
			for (double Sd : {-1.0, 1.0})
			{
				Line("guards", {Hb + V3(0.05 * Sd, -0.06, 0.04), V3(OutX * 0.8 * Sd, E.y + 0.12 * S, Nose - 0.04), V3(OutX * Sd, E.y, Nose + 0.02),
					V3(OutX * 0.85 * Sd, Bottom + 0.02, Nose + 0.08), V3(0.08 * Sd, Bottom - 0.03, Nose + 0.14)}, Shiny ? 0.013 : 0.015);
				if (Style == "cage")
				{
					Line("guards", {Hb + V3(0.05 * Sd, 0.0, 0.03), H + V3(0.2 * Sd, -0.16, 0.22), SeatFront + V3(0.18 * Sd, -0.12, -0.12), SeatFront + V3(0.1 * Sd, -0.16, 0.0)}, 0.014);
					Line("guards", {V3(OutX * Sd, E.y, Nose + 0.02), H + V3(0.2 * Sd, -0.16, 0.22)}, 0.012);
				}
			}
			if (Style == "cage") Line("guards", {V3(-0.08, Bottom - 0.03, Nose + 0.14), V3(0.08, Bottom - 0.03, Nose + 0.14)}, 0.013);
		}
		else if (Style == "wrap")
		{
			for (double Sd : {-1.0, 1.0})
				Line("guards", {Hb + V3(0.05 * Sd, -0.05, 0.04), V3(OutX * 0.85 * Sd, E.y + 0.14 * S, Nose - 0.06), V3(OutX * Sd, E.y + 0.02, Nose - 0.03),
					V3(OutX * 0.95 * Sd, Bottom + 0.01, Nose + 0.06), V3(OutX * 0.8 * Sd, Bottom - 0.02, E.z + 0.08), V3(0.12 * Sd, Pg.y + 0.05, Pg.z - 0.08)}, 0.014);
			Line("guards", {V3(-OutX * 0.97, E.y + 0.06, Nose - 0.04), V3(0.0, E.y + 0.07, Nose - 0.07), V3(OutX * 0.97, E.y + 0.06, Nose - 0.04)}, 0.011);
		}
		else if (Style == "plate" || Style == "skid")
		{
			constexpr double PlateLen = 0.42;   // (GUARD_PLATE_LEN)
			const double Length = 0.34 * S + 0.08;
			Put(Out, "guards", "guards/" + Style + ".glb",
				AtWith(GAffine::Rotation(GodotRight, -0.05) * GAffine::Scale(V3(1.0, 1.0, Length / PlateLen)), V3(0.0, Bottom - 0.035, E.z + 0.02)), "");
		}
		else
			for (double Sd : {-1.0, 1.0})
				for (const V3& At : {V3(OutX * 0.8 * Sd, E.y + 0.1 * S, Nose + 0.06), V3(0.11 * Sd, Pv.y + 0.06, Pv.z - 0.04)})
					Put(Out, "guards", "guards/bung.glb", AtWith(GAffine::Rotation(GodotUp, Sd > 0.0 ? 0.0 : TorqueSim::Pi), At + V3(0.012 * Sd, 0.0, 0.0)), "");
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "number_boards")))
	{
		const std::string Style = P->Str("style", "mx");
		auto Board = [&](const std::string& Kind, const GAffine& Xf, bool Steer) {
			GAffine X = Xf;
			if (Steer) X.O += H;
			Put(Out, "number_boards", "number_board/" + Kind + ".glb", X, Steer ? "steer" : "");
		};
		const V3 SideAt(0.0, SeatY - 0.1, TorqueSim::Lerp(SeatFront.z, Tail.z, 0.62));
		if (Style == "oval")
			Board("oval", AtWith(GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5 + 0.15) * GAffine::Scale(V3(1.25, 1.0, 1.0)), V3(0.0, -0.02, -0.2)), true);
		else if (Style == "mx" || Style == "front")
			Board("plate", AtWith(GAffine::Rotation(GodotRight, 0.22), V3(0.0, -0.05, -0.2)), true);
		const V3 Back(0.0, 0.0, 1.0);
		if (Style == "mx" || Style == "tracker")
			for (double Sd : {-1.0, 1.0})
			{
				const V3 Spot = SideAt + V3(0.14 * Sd, 0.0, 0.0);
				if (Style == "tracker")
					Board("oval", AtWith(GAffine::Rotation(Back, TorqueSim::Pi * 0.5 * Sd) * GAffine::Scale(V3(1.2, 1.25, 1.68)), Spot), false);
				else
					Board("plate", AtWith(GAffine::Rotation(GodotRight, -0.12) * GAffine::Rotation(GodotUp, TorqueSim::Pi * 0.5) * GAffine::Scale(V3(0.27 / 0.22, 0.19 / 0.21, 1.0)), Spot), false);
			}
		else if (Style == "shrouds")
			for (double Sd : {-1.0, 1.0})
			{
				const GAffine Wing = GAffine::Rotation(GodotUp, -0.3 * Sd) * GAffine::Rotation(GodotRight, 0.35);
				Board("plate", AtWith(Wing * GAffine::Rotation(GodotUp, TorqueSim::Pi * 0.5) * GAffine::Scale(V3(0.24 / 0.22, 0.2 / 0.21, 1.25)), H + V3(0.16 * Sd, -0.2, 0.2)), false);
			}
		else if (Style == "oval")
			Board("oval", AtWith(GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5 - 0.25) * GAffine::Scale(V3(1.25, 1.0, 1.0)) * GAffine::Scale(V3(0.8, 1.0, 0.8)), Tail + V3(0.0, 0.06, 0.2)), false);
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "steering_damper")))
	{
		const std::string Style = P->Str("style", "linear");
		if (Style == "linear")
		{
			const V3 Back = Hb + V3(0.14, -0.025, 0.25), Front = Hb + V3(0.14, -0.02, 0.1);
			Put(Out, "steering_damper", "damper/linear.glb", AtWith(BasisUp(Front - Back) * GAffine::Scale(V3(1.0, (Front - Back).Length() / 0.15, 1.0)), Back), "");
			Line("steering_damper", {Front - H, Hb + V3(0.11, -0.02, -0.03) - H}, 0.006, true);
			CodeShapes::Emit(Out, "steering_damper", "damper/linear.glb", "steer", "", CodeShapes::Sphere(0.011), GAffine::At(Hb + V3(0.11, -0.02, -0.03)), "black_metal");   // (the rod's eye)
		}
		else if (Style == "rotary")
		{
			const V3 Ax = ToGodot(L.SteerAxis);
			Put(Out, "steering_damper", "damper/rotary.glb", AtWith(GAffine::Rotation(GodotRight, std::atan2(Ax.z, Ax.y)), V3(0.0, 0.04, 0.05) + H), "steer");
			Line("steering_damper", {H + V3(0.0, 0.02, 0.06), H + V3(0.0, 0.0, 0.16)}, 0.006);
		}
		else
			Put(Out, "steering_damper", "damper/friction.glb", GAffine::At(Hb + V3(0.0, -0.05, 0.02)), "");
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "lever_guards")))
	{
		std::vector<double> Sides = {1.0};
		if (P->Str("style", "brake") == "pair") Sides.push_back(-1.0);
		for (double Sd : Sides)
		{
			const V3 End = Bar.End(Sd);
			const V3 X = Bar.OutAt(Sd) * Sd;
			const V3 Z = X.Cross(GodotUp).Normalized();
			const GAffine B = BasisCols(X, Z.Cross(X).Normalized(), Z);
			auto At = [&](double Dx, double Dy, double Dz) { return End + B.Apply(V3(Dx * Sd, Dy, Dz)) - B.O; };
			Put(Out, "lever_guards", "guards/lever_boss.glb", AtWith(BasisUp(B.X * Sd), At(0.026, 0.0, 0.0) + H), "steer");
			Line("lever_guards", {At(0.03, -0.004, -0.012), At(0.042, -0.01, -0.05), At(0.024, -0.014, -0.082), At(-0.045, -0.014, -0.09)}, 0.0065, true);
		}
	}
}

void BikeParts::Luggage(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	const std::string Tube = "frame/tube/round.glb";
	const BuildStats& St = L.Stats;
	const V3 H = ToGodot(L.Head), Tail = ToGodot(L.Tail), Pv = ToGodot(L.Pivot), E = ToGodot(L.Crank);
	const double SeatY = St.SeatHeight - St.RearRadius, Zr = St.Wheelbase * 0.5;
	const double W = St.RearTyreWidth * 0.5 + 0.12;
	auto TubeAB = [&](const std::string& Slot, const V3& A, const V3& B, double R) { Sweep(Out, Slot, Tube, {A, B}, {R}, ""); };
	// (_hardware: a unit-sized fitting stretched to Size, turned so its thin way is its own Y.)
	auto Hardware = [&](const std::string& Slot, const std::string& Path, const V3& At, const V3& Size) {
		GAffine B;
		V3 Sz = Size;
		if (Size.x < Size.y && Size.x < Size.z) { B = GAffine::Rotation(V3(0.0, 0.0, 1.0), TorqueSim::Pi * 0.5); Sz = V3(Size.y, Size.x, Size.z); }
		else if (Size.z < Size.y) { B = GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5); Sz = V3(Size.x, Size.z, Size.y); }
		Put(Out, Slot, Path, AtWith(B * GAffine::Scale(Sz), At), "");
	};
	// (Godot numbers the names of the code's meshes on the bike itself across the panniers and the top box.)
	CodeShapes::GodotNames OnBike;
	// (_alu_case: the body and lid, k from its height; a pannier's faces out to its side.)
	auto AluCase = [&](const std::string& Slot, const V3& C, const V3& Size, const V3& Face) {
		const bool Side = std::fabs(Face.x) > 0.5;
		const std::string Glb = Side ? "alu_pannier" : "alu_top";
		const double K = Size.y / (Side ? 0.34 : 0.26);
		const GAffine Xf = AtWith(GAffine::Scale(V3(K * (Face.x < -0.5 ? -1.0 : 1.0), K, K)), C);
		if (!Put(Out, Slot, "luggage/" + Glb + ".glb", Xf, "")) return;
		Put(Out, Slot, "luggage/" + Glb + "_lid.glb", Xf, "");
		// (_alu_latches: two latches over the lid's seam on the face, and a reflector below them.)
		const double Hh = Size.y, BodyH = Hh * 0.66, Bottom = C.y - Hh * 0.5;
		const double Hx = Size.x * 0.5 + 0.002, Hz = Size.z * 0.5 + 0.002;
		const V3 Fo(Face.x * Hx, 0.0, Face.z * Hz);
		const V3 Across(std::fabs(Face.z), 0.0, std::fabs(Face.x)), FaceAbs(std::fabs(Face.x), std::fabs(Face.y), std::fabs(Face.z));
		const double Span = Side ? Size.z : Size.x;
		auto Mul = [](const V3& A, const V3& B) { return V3(A.x * B.x, A.y * B.y, A.z * B.z); };
		const std::string Stand = "luggage/" + Glb + ".glb";
		for (double Sg : {-1.0, 1.0})
		{
			const V3 At = V3(C.x, Bottom + BodyH + 0.004, C.z) + Fo + Across * (Sg * Span * 0.3);
			CodeShapes::Emit(Out, Slot, Stand, "", OnBike("Latch"), CodeShapes::Box(Mul(V3(0.04, 0.07, 0.04), Across + V3(0.0, 1.0, 0.0)) + FaceAbs * 0.012), GAffine::At(At + Face * 0.007), "black_metal");
			CodeShapes::Emit(Out, Slot, Stand, "", "", CodeShapes::Box(Mul(V3(0.025, 0.04, 0.025), Across + V3(0.0, 1.0, 0.0)) + FaceAbs * 0.006), GAffine::At(At + Face * 0.016 + V3(0.0, -0.012, 0.0)), "brushed_steel");
		}
		CodeShapes::Emit(Out, Slot, Stand, "", "", CodeShapes::Box(Across * (Span * 0.4) + V3(0.0, 0.022, 0.0) + FaceAbs * 0.005),
			GAffine::At(V3(C.x, Bottom + BodyH * 0.72, C.z) + Fo + Face * 0.003), "lens_red");
	};
	if (const Json* P = Lay.Part(PartIn(Parts, "panniers")))
	{
		const Json* Look = P->Get("look");
		const double K = std::pow(P->Num("litres", 40.0) / 60.0, 0.33);
		const double Top = SeatY - 0.04;
		const std::string Shape = PartStr(Look, "shape", P->Str("style", "hard"));
		const std::string Drawn = Shape == "hard" && !(Look && Look->Get("shape")) ? "sport_case" : Shape;
		for (double Sd : {-1.0, 1.0})
		{
			const double Depth = 0.15 * K;
			const V3 C(Sd * (W + Depth * 0.5), Top - 0.17 * K, Zr - 0.08);
			const GAffine Xf = AtWith(GAffine::Scale(V3(Sd * K, K, K)), C);
			if (Drawn == "saddlebag" || Drawn == "leather") Put(Out, "panniers", "luggage/saddlebag.glb", Xf, "");
			else if (Drawn == "alloy") AluCase("panniers", C, V3(0.15, 0.34, 0.46) * K, V3(Sd, 0.0, 0.0));
			else if (Drawn == "roll" || Drawn == "soft")
			{
				// (_roll_pannier and _throw_over: the bag's glb, and its buckles.)
				const double Hb = 0.3 * K, Y0 = C.y - Hb * 0.5, OuterX = C.x + Sd * (Depth * 0.5 + 0.004);
				const V3 Rail(Sd * W, Top, C.z);
				const std::string Stand = Drawn == "roll" ? "luggage/roll_pannier.glb" : "luggage/throw_over.glb";
				auto BagBox = [&](const V3& At, const V3& Size, const std::string& Tag) { CodeShapes::Emit(Out, "panniers", Stand, "", "", CodeShapes::Box(Size), GAffine::At(At), Tag); };
				// (_strip: a flat strap from A to B, its face as near up as that leaves.)
				auto Strap = [&](const V3& A, const V3& B, double Width, double Thick, const std::string& Tag) {
					const V3 Z = (B - A).Normalized();
					V3 X = V3(0.0, 1.0, 0.0).Cross(Z);
					X = X.Length() < 0.01 ? GodotRight : X.Normalized();
					CodeShapes::Emit(Out, "panniers", Stand, "", "", CodeShapes::Box(V3(Width, Thick, (B - A).Length())), BasisCols(X, Z.Cross(X), Z, (A + B) * 0.5), Tag);
				};
				if (Drawn == "roll")
				{
					const double HalfL = 0.21 * K;
					const double RollY = Y0 + Hb + 0.022;
					Put(Out, "panniers", "luggage/roll_pannier.glb", Xf, "");
					for (double Z : {-1.0, 1.0})
					{
						const double Sz = C.z + Z * HalfL * 0.6;
						BagBox(V3(C.x + Sd * Depth * 0.32, RollY + 0.028, Sz), V3(Depth * 0.4, 0.006, 0.024), "fabric");
						BagBox(V3(OuterX, Y0 + Hb * 0.62, Sz), V3(0.006, Hb * 0.62, 0.024), "fabric");
						Hardware("panniers", "luggage/buckle.glb", V3(OuterX + Sd * 0.006, Y0 + Hb * 0.42, Sz), V3(0.014, 0.04, 0.034));
						Strap(V3(C.x - Sd * Depth * 0.3, Y0 + Hb * 0.92, Sz), Rail + V3(0.0, 0.0, Z * 0.08), 0.022, 0.004, "fabric");   // (up to the rack rail)
					}
					// The compression strap round it, its buckle, and a reflective patch.
					CodeShapes::Emit(Out, "panniers", Stand, "", "", CodeShapes::Loft({{V3(C.x, Y0 + Hb * 0.25, C.z), Depth * 0.5 + 0.004, HalfL + 0.004, HalfL + 0.004, 2.8},
						{V3(C.x, Y0 + Hb * 0.25 + 0.026, C.z), Depth * 0.5 + 0.004, HalfL + 0.004, HalfL + 0.004, 2.8}}), GAffine(), "fabric");
					Hardware("panniers", "luggage/buckle.glb", V3(OuterX + Sd * 0.006, Y0 + Hb * 0.25 + 0.013, C.z), V3(0.014, 0.034, 0.04));
					BagBox(V3(OuterX, Y0 + Hb * 0.72, C.z), V3(0.004, Hb * 0.16, HalfL * 0.5), "brushed_steel");
				}
				else
				{
					const double HalfL = 0.2 * K;
					Put(Out, "panniers", "luggage/throw_over.glb", Xf, "");
					// The flap over its top and down its outside, the leather straps, and the yoke over the rack.
					BagBox(V3(C.x, Y0 + Hb + 0.004, C.z), V3(Depth * 0.9, 0.008, HalfL * 2.0), "paint_2");
					BagBox(V3(OuterX, Y0 + Hb * 0.8, C.z), V3(0.008, Hb * 0.4, HalfL * 2.0), "paint_2");
					for (double Z : {-1.0, 1.0})
					{
						BagBox(V3(OuterX + Sd * 0.005, Y0 + Hb * 0.62, C.z + Z * HalfL * 0.55), V3(0.006, Hb * 0.34, 0.022), "leather");
						Hardware("panniers", "luggage/brass_buckle.glb", V3(OuterX + Sd * 0.009, Y0 + Hb * 0.5, C.z + Z * HalfL * 0.55), V3(0.006, 0.026, 0.03));
					}
					Strap(V3(C.x - Sd * Depth * 0.4, Y0 + Hb, C.z), V3(0.0, Rail.y + 0.012, C.z), HalfL * 1.4, 0.006, "paint_2");
				}
			}
			else if (Put(Out, "panniers", "luggage/" + std::string(Drawn == "sport_case" ? "sport_case" : "hard_case") + ".glb", Xf, "") && (Drawn == "hard_top" || Drawn == "hard"))
				CodeShapes::Emit(Out, "panniers", "luggage/hard_case.glb", "", "", CodeShapes::Box(V3(0.006, 0.012, 0.42 * K)),
					GAffine::At(C + V3(Sd * (0.15 * K * 0.5 + 0.002), 0.06 * K, 0.0)), "paint_2");   // (its trim strip)
			TubeAB("panniers", V3(Sd * W, Top, Pv.z + 0.1), V3(Sd * W, Top, Zr + 0.16), 0.008);
			TubeAB("panniers", V3(Sd * W, Top, Zr + 0.16), V3(Sd * W, Top - 0.3 * K, Zr + 0.12), 0.008);
		}
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "top_box")))
	{
		const std::string Style = P->Str("style", "small");
		const double K = std::pow(P->Num("litres", 35.0) / 35.0, 0.33);
		const V3 Size = (Style == "roll" ? V3(0.56, 0.24, 0.27) : V3(0.36, 0.26, 0.34)) * K;
		const V3 Base(0.0, SeatY + 0.04, Tail.z + 0.03 + Size.z * 0.5);
		// (_top_rack: rails back off the subframe, stays, a cross bar; its plate is code-drawn.)
		const double Back = Base.z + Size.z * 0.42, RailY = Base.y - 0.012;
		for (double X : {-1.0, 1.0})
		{
			Sweep(Out, "top_box", Tube, {Tail + V3(0.08 * X, -0.02, -0.12), Tail + V3(0.1 * X, RailY - Tail.y, 0.02), V3(0.1 * X, RailY, Back)}, {0.009}, "");
			TubeAB("top_box", V3(0.1 * X, RailY, Base.z - Size.z * 0.1), Tail + V3(0.085 * X, -0.13, 0.04), 0.008);
		}
		Sweep(Out, "top_box", Tube, {V3(-0.1, RailY, Back), V3(0.0, RailY, Back + 0.025), V3(0.1, RailY, Back)}, {0.009}, "");
		TubeAB("top_box", V3(-0.1, RailY, Base.z - Size.z * 0.3), V3(0.1, RailY, Base.z - Size.z * 0.3), 0.008);
		// The mounting plate the box clicks onto.
		CodeShapes::Emit(Out, "top_box", "luggage/top_case.glb", "", "", CodeShapes::Box(V3(0.24, 0.014, std::min(Size.z * 0.86, 0.32))), GAffine::At(Base), "plastic");
		CodeShapes::Emit(Out, "top_box", "luggage/top_case.glb", "", "", CodeShapes::Box(V3(0.05, 0.018, std::min(Size.z * 0.7, 0.26))), GAffine::At(Base + V3(0.0, -0.004, 0.0)), "plastic");
		if (Style == "crate") Put(Out, "top_box", "luggage/crate.glb", AtWith(GAffine::Scale(V3(1.0, 1.0, 1.0) * (Size.y / 0.26)), Base + V3(0.0, 0.012, 0.0)), "");
		else if (Style == "trunk")
		{
			const GAffine Xf = AtWith(GAffine::Scale(V3(1.0, 1.0, 1.0) * (Size.y / 0.26)), Base + V3(0.0, 0.012, 0.0));
			if (Put(Out, "top_box", "luggage/trunk.glb", Xf, "")) Put(Out, "top_box", "luggage/trunk_lid.glb", Xf, "");
		}
		else if (Style == "alloy") AluCase("top_box", Base + V3(0.0, 0.016 + Size.y * 0.5, 0.0), Size, V3(0.0, 0.0, 1.0));
		else if (Style == "roll")
		{
			// (_roll_bag: the bag across the rack, its buckles and the cargo straps' hooks.)
			const double R = Size.y * 0.5, Half = Size.x * 0.5 - 0.06;
			const V3 C = Base + V3(0.0, R * 0.92 + 0.008, 0.0);
			Put(Out, "top_box", "luggage/roll_bag.glb", AtWith(GAffine::Scale(V3(1.0, 1.0, 1.0) * (Size.y / 0.24)), C), "");
			for (double X : {-1.0, 1.0})
			{
				for (double Y : {-1.0, 1.0}) Hardware("top_box", "luggage/buckle.glb", C + V3(X * (Half + 0.075), Y * (R * 0.75 + 0.012), 0.0), V3(0.034, 0.022, 0.034));
				Hardware("top_box", "luggage/buckle.glb", C + V3(X * (Half - 0.07), R + 0.006, 0.0), V3(0.035, 0.012, 0.03));
			}
			for (double X : {-1.0, 1.0})
			{
				const double Bx = X * Half * 0.5;
				Hardware("top_box", "luggage/buckle.glb", C + V3(Bx, R + 0.008, 0.0), V3(0.034, 0.014, 0.05));
				for (double Z : {-1.0, 1.0})
				{
					const V3 Foot(0.1 * (Bx > 0.0 ? 1.0 : -1.0), Base.y - 0.012, C.z + Z * (Size.z * 0.5 + 0.015));
					Hardware("top_box", "luggage/strap_hook.glb", Foot + V3(0.0, 0.008, 0.0), V3(0.012, 0.025, 0.02));
				}
			}
			// Its carry handle, swept from the tube section as the rack is.
			Sweep(Out, "top_box", Tube, {C + V3(-0.06, R * 0.98, 0.0), C + V3(-0.04, R + 0.03, 0.0), C + V3(0.04, R + 0.03, 0.0), C + V3(0.06, R * 0.98, 0.0)}, {0.008}, "");
		}
		else
		{
			// (_plastic_case, with a pillion backrest on the large one.)
			const GAffine Xf = AtWith(GAffine::Scale(V3(1.0, 1.0, 1.0) * (Size.y / 0.26)), Base + V3(0.0, 0.016, 0.0));
			if (Put(Out, "top_box", "luggage/top_case.glb", Xf, ""))
			{
				// (_case_reflector: the red reflector across the back of the tub, the lock over the seam.)
				const V3 At = Base + V3(0.0, 0.016, 0.0);
				const double Hw = Size.x * 0.5, Bz = Size.z * 0.5 * 1.03, Hh = Size.y, Seam = 0.5 * Hh;
				CodeShapes::Emit(Out, "top_box", "luggage/top_case.glb", "", "", CodeShapes::Box(V3(Hw * 1.3, 0.035 * Hh / 0.26, 0.008)), GAffine::At(At + V3(0.0, Seam - 0.09 * Hh, Bz + 0.002)), "lens_red");
				CodeShapes::Emit(Out, "top_box", "luggage/top_case.glb", "", "", CodeShapes::Cylinder(0.009, 0.01, -1.0, 12),
					AtWith(GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5), At + V3(0.0, Seam + 0.012 * Hh, Bz + 0.014)), "chrome");
				if (Style == "large") Put(Out, "top_box", "luggage/backrest.glb", Xf, "");
			}
		}
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "fog_lights")))
	{
		const std::string Style = P->Str("style", "round");
		const V3 Axis = ToGodot(L.SteerAxis);
		if (Style == "bar") Put(Out, "fog_lights", "fog/bar.glb", GAffine::At(Axis * -0.2 + V3(0.0, 0.0, -0.1) + H), "steer");
		else
			for (double X : {-1.0, 1.0})
				Put(Out, "fog_lights", "fog/" + std::string(Style == "cube" || Style == "rally" ? Style : "round") + ".glb",
					GAffine::At(Axis * -0.34 + V3(0.15 * X, 0.0, -0.06) + H), "steer");
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "aux_tank")))
	{
		const double K = std::pow(P->Num("capacity", 8.0) / 8.0, 0.33);
		const GAffine Sk = GAffine::Scale(V3(K, K, K));
		const std::string Style = P->Str("style", "can");
		// (KitModel._aux_tank: its brackets to the rails, and fuel lines, as the code draws them.)
		auto Kit = [&](const std::string& Glb, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) { CodeShapes::Emit(Out, "aux_tank", Glb, "", "", M, Xf, Tag); };
		auto KitTube = [&](const std::string& Glb, const V3& A, const V3& B, double R, const std::string& Tag) { Kit(Glb, CodeShapes::Tube(A, B, R), CodeShapes::TubeAt(A, B), Tag); };
		if (Style == "can")
		{
			const V3 C(-(W + 0.06), SeatY - 0.24, Zr - 0.36);
			const V3 Size = V3(0.075, 0.26, 0.2) * K;
			Put(Out, "aux_tank", "kit/fuel_can.glb", AtWith(Sk, C), "");
			KitTube("kit/fuel_can.glb", C + V3(0.03, Size.y * 0.3, 0.0), V3(-W, SeatY - 0.04, C.z), 0.007, "brushed_steel");
			KitTube("kit/fuel_can.glb", C + V3(0.03, -Size.y * 0.45, 0.0), V3(-W + 0.02, C.y - Size.y * 0.45, C.z + 0.1), 0.007, "brushed_steel");
		}
		else if (Style == "cell")
		{
			const V3 A(-(W + 0.05), SeatY - 0.09, Zr - 0.5), B(-(W + 0.05), SeatY - 0.09, Zr - 0.02);
			Put(Out, "aux_tank", "kit/fuel_cell.glb", AtWith(GAffine::Scale(V3(K, K, 1.0)), (A + B) * 0.5), "");
			for (double U : {0.2, 0.8})
			{
				const V3 At = A + (B - A) * U;
				KitTube("kit/fuel_cell.glb", At + V3(0.04, 0.0, 0.0), V3(-W, SeatY - 0.04, At.z), 0.006, "brushed_steel");
			}
			// Its fuel line forward to the tank (tank_top_at a little behind the tank's nose).
			double TopY = SeatY + 0.1;
			if (!Body.TankSecs.empty())
			{
				const double Z = Body.TankSecs.front().C.z + 0.3;
				const LoftSection* Best = &Body.TankSecs.front();
				for (const LoftSection& Sec : Body.TankSecs)
					if (std::fabs(Sec.C.z - Z) < std::fabs(Best->C.z - Z)) Best = &Sec;
				TopY = Best->C.y + Best->Up;
			}
			KitTube("kit/fuel_cell.glb", A + (B - A) * 0.05 + V3(0.03, -0.04, 0.0), V3(-0.08, TopY - 0.12, Pv.z), 0.005, "rubber");
		}
		else if (Style == "rear") Put(Out, "aux_tank", "kit/rear_tank.glb", AtWith(Sk, V3(0.0, SeatY - 0.06, Tail.z - 0.02)), "");
		else if (Style == "twin")
			for (double Sd : {-1.0, 1.0})
			{
				const V3 C(Sd * (L.Hard.CaseHalfW + 0.11), E.y + 0.06, E.z - 0.14);
				Put(Out, "aux_tank", "kit/side_tank.glb", AtWith(Sk, C), "");
				KitTube("kit/side_tank.glb", C + V3(-Sd * 0.04, -0.06, 0.08), E + V3(Sd * 0.06, 0.1, 0.0), 0.005, "rubber");
			}
		else
		{
			const V3 C(0.0, SeatY + 0.06, Tail.z - 0.08);
			Put(Out, "aux_tank", "kit/bladder.glb", AtWith(Sk, C), "");
			for (double X : {-0.08, 0.08})
			{
				// Its straps (_strip), down over the seat.
				const V3 A = C + V3(X, 0.055 * K, -0.06), B = C + V3(X, -0.06, -0.09);
				const V3 Z = (B - A).Normalized();
				V3 Xa = V3(0.0, 1.0, 0.0).Cross(Z);
				Xa = Xa.Length() < 0.01 ? GodotRight : Xa.Normalized();
				Kit("kit/bladder.glb", CodeShapes::Box(V3(0.02, 0.003, (B - A).Length())), BasisCols(Xa, Z.Cross(Xa), Z, (A + B) * 0.5), "fabric");
			}
		}
	}
	if (const Json* P = Lay.Part(PartIn(Parts, "toolbox")))
	{
		const std::string Style = P->Str("style", "box");
		if (Style == "roll") Put(Out, "toolbox", "kit/tool_roll.glb", GAffine::At(V3(0.0, -0.22, -0.1) + H), "steer");
		else if (Style == "tube")
		{
			const V3 A(-(W + 0.03), -0.03, Zr - 0.42), B(-(W + 0.03), -0.03, Zr - 0.08);
			Put(Out, "toolbox", "kit/tool_tube.glb", GAffine::At(A), "");
			for (double U : {0.25, 0.75})
			{
				const V3 At = A + (B - A) * U, To(-W + 0.02, At.y + 0.08, At.z);
				CodeShapes::Emit(Out, "toolbox", "kit/tool_tube.glb", "", "", CodeShapes::Tube(At + V3(0.04, 0.0, 0.0), To, 0.006), CodeShapes::TubeAt(At + V3(0.04, 0.0, 0.0), To), "brushed_steel");
			}
		}
		else if (Style == "bash") Put(Out, "toolbox", "kit/bash_box.glb", GAffine::At(V3(0.0, E.y - 0.22 * L.Hard.Scale - 0.04, E.z + 0.02)), "");
		else
		{
			const bool Big = Style == "case";
			const V3 Size = Big ? V3(0.1, 0.15, 0.26) : V3(0.08, 0.12, 0.22);
			const V3 C(-(W + Size.x * 0.5 + 0.01), -0.06, Zr - 0.36);
			if (Put(Out, "toolbox", "kit/toolbox.glb", AtWith(GAffine::Scale(V3(Size.x / 0.08, Size.y / 0.12, Size.z / 0.22)), C), ""))
			{
				// Its bracket up to the rail.
				const V3 A = C + V3(Size.x * 0.5, Size.y * 0.4, 0.0), B(-W + 0.02, SeatY - 0.1, C.z + 0.05);
				CodeShapes::Emit(Out, "toolbox", "kit/toolbox.glb", "", "", CodeShapes::Tube(A, B, 0.007), CodeShapes::TubeAt(A, B), "brushed_steel");
			}
		}
	}
}

namespace
{
// A loft section (ShapeKit.loft's): centre, half width, rise, drop and squareness.
struct ShellSec { V3 C; double W, Up, Down, N; };

// ShapeKit.loft's (and BlenderParts.loft_rows') rings of an open loft from A0 to A1, Godot frame.
std::vector<std::vector<V3>> ShellRows(const std::vector<ShellSec>& Secs, double A0, double A1)
{
	constexpr int Ring = 28;
	const double Span = A1 - A0;
	const int Steps = std::max(4, static_cast<int>(std::lround(Ring * Span / (2.0 * TorqueSim::Pi))));
	const int Count = static_cast<int>(Secs.size());
	std::vector<std::vector<V3>> Rows;
	for (int I = 0; I < Count; ++I)
	{
		const ShellSec& S = Secs[I];
		V3 T = Secs[std::min(I + 1, Count - 1)].C - Secs[std::max(I - 1, 0)].C;
		T.x = 0.0;
		T = T.Length() > 0.0001 ? T.Normalized() : V3(0.0, 0.0, 1.0);
		const V3 Up = T.Cross(V3(1.0, 0.0, 0.0)).Normalized();
		const double E = 2.0 / S.N;
		std::vector<V3> Pts;
		for (int J = 0; J <= Steps; ++J)
		{
			const double A = A0 + Span * J / Steps, Ca = std::cos(A), Sa = std::sin(A);
			const double X = (Ca > 0.0 ? 1.0 : Ca < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Ca), E) * S.W;
			const double Y = (Sa > 0.0 ? 1.0 : Sa < 0.0 ? -1.0 : 0.0) * std::pow(std::fabs(Sa), E) * (Sa > 0.0 ? S.Up : S.Down);
			Pts.push_back(S.C + V3(X, 0.0, 0.0) + Up * Y);
		}
		Rows.push_back(Pts);
	}
	return Rows;
}

// The loft's outer normals (ShapeKit.loft: across the ring crossed with along, turned out from
// the section's centre).
std::vector<std::vector<V3>> ShellLoftNormals(const std::vector<std::vector<V3>>& Rows, const std::vector<ShellSec>& Secs)
{
	const int Count = static_cast<int>(Rows.size()), Width = static_cast<int>(Rows[0].size());
	std::vector<std::vector<V3>> N(Count, std::vector<V3>(Width));
	for (int I = 0; I < Count; ++I)
		for (int J = 0; J < Width; ++J)
		{
			const V3 Across = Rows[I][std::min(J + 1, Width - 1)] - Rows[I][std::max(J - 1, 0)];
			const V3 Along = Rows[std::min(I + 1, Count - 1)][J] - Rows[std::max(I - 1, 0)][J];
			V3 Nm = Across.Cross(Along).Normalized();
			if (Nm.Dot(Rows[I][J] - Secs[I].C) < 0.0) Nm = Nm * -1.0;
			N[I][J] = Nm;
		}
	return N;
}

// BlenderParts._grid_normals: each grid point's normal from the faces round it, turned as Outer.
std::vector<std::vector<V3>> ShellGridNormals(const std::vector<std::vector<V3>>& Rows, const V3& Outer)
{
	const int Count = static_cast<int>(Rows.size()), Width = static_cast<int>(Rows[0].size());
	std::vector<std::vector<V3>> N(Count, std::vector<V3>(Width));
	for (int I = 0; I + 1 < Count; ++I)
		for (int J = 0; J + 1 < Width; ++J)
		{
			const V3 A = Rows[I][J];
			V3 Fn = (Rows[I][J + 1] - A).Cross(Rows[I + 1][J] - A);
			Fn += (Rows[I + 1][J + 1] - Rows[I + 1][J]).Cross(Rows[I + 1][J + 1] - Rows[I][J + 1]);
			N[I][J] += Fn; N[I][J + 1] += Fn; N[I + 1][J] += Fn; N[I + 1][J + 1] += Fn;
		}
	const double Turn = N[0][0].Dot(Outer) < 0.0 ? -1.0 : 1.0;
	for (auto& Row : N)
		for (V3& V : Row) V = (V * Turn).Normalized();
	return N;
}

// BlenderParts._grid_edges: the first row, the last, the first point across and the last, each
// as its points and the ones next in from them ({row, across}).
using ShellEdge = std::pair<std::vector<std::pair<int, int>>, std::vector<std::pair<int, int>>>;
std::vector<ShellEdge> ShellGridEdges(int Count, int Width)
{
	std::vector<ShellEdge> E(4);
	for (int J = 0; J < Width; ++J)
	{
		E[0].first.push_back({0, J}); E[0].second.push_back({1, J});
		E[1].first.push_back({Count - 1, J}); E[1].second.push_back({Count - 2, J});
	}
	for (int I = 0; I < Count; ++I)
	{
		E[2].first.push_back({I, 0}); E[2].second.push_back({I, 1});
		E[3].first.push_back({I, Width - 1}); E[3].second.push_back({I, Width - 2});
	}
	return E;
}
} // namespace

void BikeParts::BarFairing(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "fairing"));
	if (!P || P->Flag("frame_mount") || !Meshes) return;
	const Json* Look = P->Get("look");
	const std::string Style = P->Str("style", "half");
	const std::string Shape = PartStr(Look, "shape", Style);
	if (!(Style == "flyscreen" || Style == "bikini" || Shape == "batwing" || Shape == "cowl")) return;
	const V3 H = ToGodot(L.Head);
	const double Screen = P->Num("screen", 0.15);
	// (_surface: a carbon one's edges are carbon.)
	std::string Words = PartIn(Parts, "fairing") + " " + P->Str("name") + " " + P->Str("type") + " " + P->Str("material");
	for (char& Ch : Words) Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	const bool Carbon = PartStr(Look, "surface", Words.find("carbon") != std::string::npos && Words.find("white") == std::string::npos && Words.find("painted") == std::string::npos ? "carbon" : "") == "carbon";
	// One open shell: the code's loft set in by the edge section's thickness, the section run round
	// its four edges (BlenderParts.shell), and for a screen its fasteners (edge_spots).
	auto Shell = [&](const std::vector<ShellSec>& Secs, double A0, double A1, const std::string& Kind) {
		const std::string EdgePath = "fairing/edge/" + Kind + ".glb";
		PartMesh EdgeOurs;
		if (Secs.size() < 2 || !Meshes(EdgePath, EdgeOurs) || EdgeOurs.Surfaces.empty()) return;
		const PartSurface& Es = EdgeOurs.Surfaces[0];
		double Thick = 0.0;
		for (const V3& V : Es.Pos) Thick = std::max(Thick, -V.y);
		const std::vector<std::vector<V3>> Rows = ShellRows(Secs, A0, A1);
		const std::vector<std::vector<V3>> Ln = ShellLoftNormals(Rows, Secs);
		const int Count = static_cast<int>(Rows.size()), Width = static_cast<int>(Rows[0].size());
		auto Made = std::make_shared<PartMesh>();
		PartSurface S;
		S.Material = Es.Material;
		auto Add = [&](const V3& G, const V3& Gn, double U, double V) {
			S.Pos.push_back(FromGodot(G + H));
			S.Nrm.push_back(FromGodot(Gn));
			S.U.push_back(U);
			S.V.push_back(V);
			return static_cast<unsigned>(S.Pos.size() - 1);
		};
		// (Each triangle wound to face Out, as ShapeKit._tri_facing, for our meshes' way round.)
		auto Tri = [&](unsigned A, unsigned B, unsigned C, const V3& OutDir) {
			const V3 Face = (S.Pos[B] - S.Pos[A]).Cross(S.Pos[C] - S.Pos[A]);
			if (Face.Dot(FromGodot(OutDir)) >= 0.0) S.Idx.insert(S.Idx.end(), {A, B, C});
			else S.Idx.insert(S.Idx.end(), {A, C, B});
		};
		for (int Face = 0; Face < 2; ++Face)
		{
			const double Sg = Face == 0 ? 1.0 : -1.0;
			const unsigned Base = static_cast<unsigned>(S.Pos.size());
			for (int I = 0; I < Count; ++I)
				for (int J = 0; J < Width; ++J)
					Add(Rows[I][J] - (Face == 0 ? V3() : Ln[I][J] * Thick), Ln[I][J] * Sg, static_cast<double>(J) / std::max(Width - 1, 1), static_cast<double>(I) / std::max(Count - 1, 1));
			for (int I = 0; I + 1 < Count; ++I)
				for (int J = 0; J + 1 < Width; ++J)
				{
					const unsigned A = Base + I * Width + J, B = A + 1, C = A + Width, D = C + 1;
					Tri(A, C, B, (Ln[I][J] + Ln[I + 1][J] + Ln[I][J + 1]) * Sg);
					Tri(B, C, D, (Ln[I][J + 1] + Ln[I + 1][J] + Ln[I + 1][J + 1]) * Sg);
				}
		}
		// The edge section (Godot frame: X across, Y up out of the face, Z 0..1 along) laid along
		// each stretch of each edge, out from the shell (BlenderParts._along).
		const std::vector<std::vector<V3>> Gn = ShellGridNormals(Rows, Ln[0][0]);
		for (const ShellEdge& E : ShellGridEdges(Count, Width))
			for (size_t K = 0; K + 1 < E.first.size(); ++K)
			{
				V3 A = Rows[E.first[K].first][E.first[K].second], B = Rows[E.first[K + 1].first][E.first[K + 1].second];
				if ((B - A).Dot(B - A) < 1e-8) continue;
				V3 Na = Gn[E.first[K].first][E.first[K].second], Nb = Gn[E.first[K + 1].first][E.first[K + 1].second];
				const V3 Hint = (Rows[E.second[K].first][E.second[K].second] + Rows[E.second[K + 1].first][E.second[K + 1].second]) * 0.5;
				const V3 OutDir = (A + B) * 0.5 - Hint;
				if ((Na + Nb).Normalized().Cross(B - A).Dot(OutDir) < 0.0) { std::swap(A, B); std::swap(Na, Nb); }
				const V3 Along = (B - A).Normalized();
				const unsigned Base = static_cast<unsigned>(S.Pos.size());
				for (size_t Q = 0; Q < Es.Pos.size(); ++Q)
				{
					const V3 Sv = ToGodot(Es.Pos[Q]), Sn = ToGodot(Es.Nrm[Q]);
					const double F = TorqueSim::Clamp(Sv.z, 0.0, 1.0);
					V3 Up = MixV(Na, Nb, F);
					Up = (Up - Along * Up.Dot(Along)).Normalized();
					const V3 Across = Up.Cross(Along).Normalized();
					Add(MixV(A, B, F) + Across * Sv.x + Up * Sv.y, (Across * Sn.x + Up * Sn.y + Along * Sn.z).Normalized(), Es.U[Q], Es.V[Q]);
				}
				for (unsigned I : Es.Idx) S.Idx.push_back(Base + I);
			}
		Made->Surfaces.push_back(std::move(S));
		FittedPart F;
		F.Slot = "fairing";
		F.Glb = EdgePath;
		F.Node = "steer";
		F.Made = Made;
		Out.push_back(F);
		if (Kind != "screen") return;
		// A screen held on along its foot by screws into well nuts (edge_spots on its first row).
		const std::string Fastener = "fairing/fastener/button.glb";
		const std::vector<ShellEdge> Edges = ShellGridEdges(Count, Width);
		const ShellEdge& E = Edges[0];
		std::vector<double> Lengths = {0.0};
		for (size_t K = 1; K < E.first.size(); ++K)
			Lengths.push_back(Lengths.back() + (Rows[E.first[K].first][E.first[K].second] - Rows[E.first[K - 1].first][E.first[K - 1].second]).Length());
		constexpr double Spacing = 0.09, Inset = 0.012;
		const double Total = Lengths.back();
		if (Total < Spacing || !HasGlb(Fastener)) return;
		const int N = static_cast<int>(std::floor(Total / Spacing));
		const double Gap = (Total - Spacing * (N - 1)) * 0.5;
		FittedPart Fs;
		Fs.Slot = "fairing";
		Fs.Glb = Fastener;
		Fs.Node = "steer";
		size_t K = 1;
		for (int C = 0; C < N; ++C)
		{
			const double At = Gap + Spacing * C;
			while (K < E.first.size() - 1 && Lengths[K] < At) ++K;
			const double Ft = TorqueSim::Clamp((At - Lengths[K - 1]) / std::max(Lengths[K] - Lengths[K - 1], 0.0001), 0.0, 1.0);
			const V3 A = Rows[E.first[K - 1].first][E.first[K - 1].second], B = Rows[E.first[K].first][E.first[K].second];
			const V3 Pt = MixV(A, B, Ft);
			const V3 Hint = MixV(Rows[E.second[K - 1].first][E.second[K - 1].second], Rows[E.second[K].first][E.second[K].second], Ft);
			const V3 Up = MixV(Gn[E.first[K - 1].first][E.first[K - 1].second], Gn[E.first[K].first][E.first[K].second], Ft).Normalized();
			V3 Into = Hint - Pt;
			Into = Into - Up * Into.Dot(Up);
			if (Into.Length() < Inset * 1.5) continue;
			V3 Along = (B - A).Normalized();
			Along = (Along - Up * Along.Dot(Up)).Normalized();
			Fs.Copies.push_back(BasisCols(Up.Cross(Along), Up, Along, Pt + Into.Normalized() * Inset + H).Warp());
		}
		if (!Fs.Copies.empty()) Out.push_back(Fs);
	};
	const V3 Lamp(0.0, -0.08, -0.13);   // the headlight, in the steering's space
	const bool Big = Style == "bikini" || Shape == "batwing";
	double W = Shape == "batwing" ? 0.3 : Shape == "cowl" ? 0.11 : (Big ? 0.17 : 0.12);
	W *= PartNum(Look, "wide", 1.0);
	const double Tall = PartNum(Look, "tall", 1.0) * (Shape == "batwing" ? 1.25 : 1.0);
	const double Low = Shape == "batwing" ? 1.8 : (Big ? 1.3 : 0.7);
	std::vector<ShellSec> Secs;
	for (int K = 0; K < 4; ++K)
	{
		const double F = K / 3.0;
		Secs.push_back({Lamp + V3(0.0, 0.01 * F, TorqueSim::Lerp(-0.05, 0.1, F)), W * TorqueSim::Lerp(0.85, 1.05, F),
			TorqueSim::Lerp(0.1, 0.12, F) * (Big ? 1.1 : 1.0) * Tall, TorqueSim::Lerp(0.08, 0.1, F) * Low, PartNum(Look, "n", 2.6)});
	}
	Shell(Secs, -0.45, TorqueSim::Pi + 0.45, Carbon ? "carbon" : "moulded");
	if (Shape != "cowl" || Screen > 0.05)
	{
		// (_screen: a pane up from the shell's top, tilted back.)
		const ShellSec& Top = Secs[1];
		const V3 Base = Top.C + V3(0.0, Top.Up - 0.01, 0.0);
		const double Tilt = Shape == "batwing" ? 0.35 : 0.45, Half = W * 0.8;
		const V3 Dir(0.0, std::cos(Tilt), std::sin(Tilt));
		std::vector<ShellSec> Pane;
		for (int K = 0; K < 4; ++K)
		{
			const double F = K / 3.0;
			Pane.push_back({Base + Dir * (Screen * F) - V3(0.0, 0.035, 0.0), Half * TorqueSim::Lerp(1.0, 0.8, F), 0.04, 0.0, 2.0});
		}
		Shell(Pane, 0.25, TorqueSim::Pi - 0.25, "screen");
	}
}

void BikeParts::TankBag(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "tank_bag"));
	const std::string Path = "kit/tank_bag.glb";
	if (!P || !HasGlb(Path)) return;
	const double K = std::pow(P->Num("litres", 10.0) / 10.0, 0.33);
	const bool Tall = P->Str("style", "low") == "tall";
	const double Length = 0.26 * K, Height = (Tall ? 0.15 : 0.1) * K;
	// (BikeModel.tank_top_at: the nearest tank section's top and half width; _tank_front.)
	auto Secs = std::make_shared<std::vector<LoftSection>>(Body.TankSecs);
	const double SeatY = L.Stats.SeatHeight - L.Stats.RearRadius;
	const double Z0 = (Secs->empty() ? ToGodot(L.Head).z + 0.1 : Secs->front().C.z) + 0.04;
	auto TopAt = [Secs, SeatY](double Z) {
		if (Secs->empty()) return std::make_pair(SeatY + 0.1, 0.12);
		const LoftSection* Best = &Secs->front();
		for (const LoftSection& S : *Secs)
			if (std::fabs(S.C.z - Z) < std::fabs(Best->C.z - Z)) Best = &S;
		return std::make_pair(Best->C.y + Best->Up, Best->W);
	};
	auto Rise = [](double U) { return TorqueSim::Lerp(0.7, 1.0, std::sin(std::min(TorqueSim::Clamp(U, 0.0, 1.0) * 1.4, 1.0) * TorqueSim::Pi * 0.5)); };
	auto Mid = [=](double U) {
		const double Z = Z0 + Length * TorqueSim::Clamp(U, 0.0, 1.0);
		return V3(0.0, TopAt(Z).first + Height * 0.5 * Rise(U), Z);
	};
	FittedPart F;
	F.Slot = "tank_bag";
	F.Glb = Path;
	F.Warp = GodotWarpToOurs([=](const V3& G) {
		const double U = G.z / 0.26;
		const V3 C = Mid(U);
		const V3 T = Mid(U + 0.2) - Mid(U - 0.2);
		const V3 Up = T.Cross(GodotRight).Normalized();
		const double Sx = std::min(TopAt(C.z).second * 0.82, 0.13 * K) / 0.1, Sy = Height / 0.1;
		const double Own = 0.05 * Rise(U);
		return C + GodotRight * (G.x * Sx) + Up * ((G.y - Own) * Sy);
	});
	Out.push_back(F);
	// The code-drawn pieces on it (KitModel._tank_bag), on its loft sections: a map pocket, the
	// zip down each side, an accent stripe, the grab handle and its mount's straps or ring.
	struct BagSec { V3 C; double W, Up, Down; };
	std::vector<BagSec> Bs;
	const int N = 5;
	for (int I = 0; I <= N; ++I)
	{
		const double U = static_cast<double>(I) / N;
		const double Z = Z0 + Length * U;
		const auto Top = TopAt(Z);
		const double W = std::min(Top.second * 0.82, 0.13 * K) * TorqueSim::Lerp(0.8, 1.0, std::sin(U * TorqueSim::Pi));
		const double Hh = Height * TorqueSim::Lerp(0.7, 1.0, std::sin(std::min(U * 1.4, 1.0) * TorqueSim::Pi * 0.5));
		Bs.push_back({V3(0.0, Top.first + Hh * 0.5, Z), W, Hh * 0.5, Hh * 0.5});
	}
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "tank_bag", Path, "", Named(Name), M, Xf, Tag);
	};
	const BagSec& MidS = Bs[N / 2];
	if (P->Flag("map"))
	{
		const V3 At(0.0, MidS.C.y + MidS.Up + 0.003, Z0 + Length * 0.5);
		Code("", CodeShapes::Box(V3(MidS.W * 1.3, 0.003, Length * 0.6)), GAffine::At(At), "decal");
		Code("", CodeShapes::Box(V3(MidS.W * 1.4, 0.003, Length * 0.66)), GAffine::At(At + V3(0.0, 0.004, 0.0)), "glass");
		Code("", CodeShapes::Box(V3(0.008, 0.002, Length * 0.55)), GAffine::At(At + V3(0.0, 0.002, 0.0)), "decal");
		Code("", CodeShapes::Box(V3(MidS.W * 1.1, 0.002, 0.006)), GAffine::At(At + V3(0.02, 0.002, 0.02)), "decal");
	}
	for (double Sd : {-1.0, 1.0})
	{
		std::vector<V3> Line;
		for (const BagSec& Sec : Bs) Line.push_back(Sec.C + V3(Sec.W * 0.97 * Sd, Sec.Up * 0.3, 0.0));
		Code("", CodeShapes::Sweep(Line, 0.004, 12), GAffine(), "black_metal");
	}
	if (P->Get("accent"))
		for (double Sd : {-1.0, 1.0})
		{
			std::vector<V3> Line;
			for (const BagSec& Sec : Bs) Line.push_back(Sec.C + V3(Sec.W * 0.99 * Sd, -Sec.Down * 0.2, 0.0));
			Code("", CodeShapes::Sweep(Line, 0.006, 12), GAffine(), "paint_2");
		}
	const BagSec& Back = Bs[N];
	Code("", CodeShapes::Sweep({Back.C + V3(-0.04, Back.Up * 0.9, -0.02), Back.C + V3(-0.02, Back.Up + 0.02, -0.02), Back.C + V3(0.02, Back.Up + 0.02, -0.02),
		Back.C + V3(0.04, Back.Up * 0.9, -0.02)}, 0.006, 12), GAffine(), "black_metal");
	const std::string Mount = P->Str("mount", "");
	if (Mount == "strap")
		for (double Sd : {-1.0, 1.0})
			for (double U : {0.2, 0.8})
			{
				// Straps down the tank's sides, buckled underneath (_strip).
				const double Z = Z0 + Length * U;
				const auto T = TopAt(Z);
				const V3 A(T.second * 0.75 * Sd, T.first + 0.02, Z), B((T.second + 0.012) * Sd, T.first - 0.07, Z + 0.01);
				const V3 Dz = (B - A).Normalized();
				V3 Xa = V3(0.0, 1.0, 0.0).Cross(Dz);
				Xa = Xa.Length() < 0.01 ? GodotRight : Xa.Normalized();
				Code("", CodeShapes::Box(V3(0.02, 0.003, (B - A).Length())), BasisCols(Xa, Dz.Cross(Xa), Dz, (A + B) * 0.5), "black_metal");
			}
	else if (Mount == "ring")
		Code("TankRing", CodeShapes::Cylinder(0.07, 0.012, -1.0, 24), GAffine::At(V3(0.0, TopAt(Z0 + Length * 0.6).first + 0.004, Z0 + Length * 0.6)), "black_metal");
}

void BikeParts::Satnav(const PartsMap& Parts, const Layout& L, double DashLift, const Bars& Bar, const std::vector<V3>& DashFace, std::vector<FittedPart>& Out) const
{
	const Json* P = Lay.Part(PartIn(Parts, "satnav"));
	if (!P) return;
	const std::string Style = P->Str("style", "road");
	if (Style == "phone")
	{
		Phone(*P, L, Bar, DashFace, Out);
		return;
	}
	const Json* Size = P->Get("screen");
	const double Sw = Size && Size->A.size() == 2 ? Size->A[0].N : 0.1, Sh = Size && Size->A.size() == 2 ? Size->A[1].N : 0.065;
	V3 At(0.0, 0.22 + DashLift, -0.03);
	if (Style == "compact") At = V3(0.13, 0.2 + DashLift * 0.5, 0.02);
	else if (Style == "road") At = V3(0.1, 0.22 + DashLift, -0.02);
	else if (Style == "tablet") At = V3(0.0, 0.25 + DashLift, -0.08);
	V3 Mount(0.0, 0.12, 0.0);   // where its arm meets the bars or crossbar
	if (Style == "compact") Mount = V3(0.12, 0.12, 0.05);
	else if (Style == "road") Mount = V3(0.07, 0.13, 0.03);
	else if (Style == "tablet") Mount = V3(0.0, 0.16 + DashLift, -0.08);
	const V3 H = ToGodot(L.Head);
	const GAffine Tilt = GAffine::Rotation(GodotRight, 0.85);
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "satnav", "kit/satnav.glb", "steer", Name, M, GAffine::At(H) * Xf, Tag);
	};
	auto Arm = [&](const V3& A, const V3& B, double R) { Code("", CodeShapes::Tube(A, B, R), CodeShapes::TubeAt(A, B), "black_metal"); };
	if (Style == "adventure" || Style == "tablet")
	{
		// A crossbar over the clocks, on two stays from the top yoke.
		const double BarY = At.y - Sh * 0.5 - 0.03;
		Arm(V3(-0.1, BarY, At.z + 0.02), V3(0.1, BarY, At.z + 0.02), 0.009);
		for (double X : {-0.08, 0.08}) Arm(V3(X, 0.05, 0.03), V3(X, BarY, At.z + 0.02), 0.007);
		Mount = V3(0.0, BarY, At.z + 0.02);
	}
	// The arm from the mount up to the back of the unit, on a ball.
	Arm(Mount, At - Tilt.Y * 0.02, 0.008);
	Code("", CodeShapes::Sphere(0.014), GAffine::At(Mount), "black_metal");
	// (The unit is the Blender piece.)
	Put(Out, "satnav", "kit/satnav.glb", AtWith(Tilt * GAffine::Scale(V3((Sw + 0.022) / 0.122, 1.0, (Sh + 0.022) / 0.087)), At + H), "steer");
	if (P->Get("accent")) Code("", CodeShapes::Box(V3(Sw + 0.03, 0.012, Sh + 0.03)), AtWith(Tilt, At - Tilt.Y * 0.004), "paint_2");
	// A sun visor over the top edge on the bigger units.
	if (Style == "adventure" || Style == "tablet" || Style == "road")
		Code("", CodeShapes::Box(V3(Sw + 0.024, 0.004, 0.035)), AtWith(Tilt * GAffine::Rotation(GodotRight, -0.5), At + Tilt.Apply(V3(0.0, 0.03, -Sh * 0.5 - 0.008))), "paint");
	// The screen: a quad on the face (its map is drawn live).
	CodeShapes::GMesh Quad;
	for (double Y : {-1.0, 1.0})
		for (double X : {-1.0, 1.0}) Quad.Add(V3(X * Sw * 0.5, Y * Sh * 0.5, 0.0), V3(0.0, 0.0, 1.0), X * 0.5 + 0.5, 0.5 - Y * 0.5);
	Quad.Quad(0, 1, 2, 3);
	Code("SatnavScreen", Quad, AtWith(Tilt * GAffine::Rotation(GodotRight, -TorqueSim::Pi * 0.5), At + Tilt.Y * 0.0135), "glass");
}

void BikeParts::Phone(const Json& P, const Layout& L, const Bars& Bar, const std::vector<V3>& DashFace, std::vector<FittedPart>& Out) const
{
	const Json* Size = P.Get("screen");
	const double Sw = Size && Size->A.size() == 2 ? Size->A[0].N : 0.12, Sh = Size && Size->A.size() == 2 ? Size->A[1].N : 0.075;
	const V3 Half(Sw * 0.5 + 0.008, Sh * 0.5 + 0.006, 0.0045);   // the phone, half of each way
	const V3 Keep = Half + V3(0.012, 0.006, 0.012);              // with its cradle round it
	const V3 H = ToGodot(L.Head);
	const V3 Eye = EyeAt(L) - H;
	// (bar_point, bar_length and _back_of on the bar's line, the right half, steering space.)
	const std::vector<V3>& Line = Bar.Line;
	auto BarPoint = [&](double Back, V3& Dir) {
		if (Line.size() < 2)
		{
			Dir = GodotRight;
			return ToGodot(L.GripR) - H + V3(-0.15, 0.0, 0.0);
		}
		double Left = std::max(Back, 0.0);
		for (size_t I = Line.size() - 1; I > 0; --I)
		{
			const V3 A = Line[I - 1], B = Line[I];
			const double D = (B - A).Length();
			if (Left <= D || I == 1)
			{
				Dir = (B - A).Normalized();
				return MixV(B, A, TorqueSim::Clamp(Left / std::max(D, 0.0001), 0.0, 1.0));
			}
			Left -= D;
		}
		Dir = (Line[1] - Line[0]).Normalized();
		return Line[0];
	};
	double BarLen = 0.0;
	for (size_t I = 1; I < Line.size(); ++I) BarLen += (Line[I] - Line[I - 1]).Length();
	auto BackOf = [&](const V3& Pt) {
		double Back = 0.0, Best = 1e18, At = 0.0;
		for (size_t I = Line.size() - 1; I > 0 && Line.size() >= 2; --I)
		{
			const V3 A = Line[I], B = Line[I - 1], Ab = B - A;
			const double T = TorqueSim::Clamp((Pt - A).Dot(Ab) / std::max(Ab.Dot(Ab), 1e-12), 0.0, 1.0);
			const V3 C = A + Ab * T;
			if ((C - Pt).Length() < Best)
			{
				Best = (C - Pt).Length();
				At = Back + (C - A).Length();
			}
			Back += Ab.Length();
		}
		return At;
	};
	// (_facing: X along the phone to the rider's right, Y up its face, Z out of the glass at the eye.)
	auto Facing = [&](const V3& At) {
		const V3 N = (Eye - At).Normalized();
		const V3 R = (GodotRight - N * N.x).Normalized();
		return BasisCols(R, N.Cross(R), N);
	};
	// (_near_bars: the parts round the bars it has to keep clear of, steering space, each mesh
	// whose box reaches into the room round the bars.)
	std::vector<SegTri> Others;
	const std::vector<SegTri> BarTris = BarTriangles(Out, Bar, H);
	if (Meshes)
	{
		static const std::set<std::string> Clear = {"mirrors", "front_master", "dashboard", "fairing", "headlight", "fog_lights",
			"indicators", "steering_damper", "tank_bag"};
		for (const FittedPart& F : Out)
		{
			if (!Clear.count(F.Slot)) continue;
			PartMesh Glb;
			if (!Meshes(F.Glb, Glb)) continue;
			for (const PartSurface& S : Draw(Glb, F).Surfaces)
			{
				V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
				for (const V3& V : S.Pos)
				{
					const V3 G = ToGodot(V) - H;
					Lo = V3(std::min(Lo.x, G.x), std::min(Lo.y, G.y), std::min(Lo.z, G.z));
					Hi = V3(std::max(Hi.x, G.x), std::max(Hi.y, G.y), std::max(Hi.z, G.z));
				}
				if (!(Lo.x < 0.6 && Hi.x > -0.6 && Lo.y < 0.6 && Hi.y > -0.3 && Lo.z < 0.4 && Hi.z > -0.5)) continue;
				for (size_t I = 0; I + 2 < S.Idx.size(); I += 3)
					Others.push_back({ToGodot(S.Pos[S.Idx[I]]) - H, ToGodot(S.Pos[S.Idx[I + 1]]) - H, ToGodot(S.Pos[S.Idx[I + 2]]) - H});
			}
		}
	}
	auto Cut = [&](const V3& A, const V3& B) { return SegmentHits(BarTris, A, B) || SegmentHits(Others, A, B); };
	// (_seg_box: whether A to B passes through the box at O turned Bs, Hf half its size each way.)
	auto SegBox = [](const V3& A, const V3& B, const V3& O, const GAffine& Bs, const V3& Hf) {
		const V3 La((A - O).Dot(Bs.X), (A - O).Dot(Bs.Y), (A - O).Dot(Bs.Z));
		const V3 D((B - A).Dot(Bs.X), (B - A).Dot(Bs.Y), (B - A).Dot(Bs.Z));
		double T0 = 0.0, T1 = 1.0;
		const double Las[3] = {La.x, La.y, La.z}, Ds[3] = {D.x, D.y, D.z}, Hs[3] = {Hf.x, Hf.y, Hf.z};
		for (int K = 0; K < 3; ++K)
		{
			if (std::fabs(Ds[K]) < 0.000001)
			{
				if (std::fabs(Las[K]) > Hs[K]) return false;
				continue;
			}
			const double U = (-Hs[K] - Las[K]) / Ds[K], V = (Hs[K] - Las[K]) / Ds[K];
			T0 = std::max(T0, std::min(U, V));
			T1 = std::min(T1, std::max(U, V));
			if (T0 > T1) return false;
		}
		return true;
	};
	auto Lerp = [](double A, double B, double F) { return A + (B - A) * F; };
	// (_phone_fit: 0 a clean fit; clocks hidden count 3, things run into 2, sight lines cut 1.)
	auto Score = [&](const V3& O, const GAffine& Bs, const V3& Clamp) {
		int Hidden = 0, Hits = 0, Blocked = 0;
		for (const V3& Q : DashFace)
			if (SegBox(Q, Eye, O, Bs, Keep + V3(0.004, 0.004, 0.004))) ++Hidden;
		std::vector<std::pair<V3, V3>> Segs;
		for (double Z : {-Keep.z, Keep.z})
		{
			for (int K = 0; K < 6; ++K)
			{
				const double Y = Lerp(-Keep.y, Keep.y, K / 5.0);
				Segs.push_back({V3(-Keep.x, Y, Z), V3(Keep.x, Y, Z)});
			}
			for (int K = 0; K < 10; ++K)
			{
				const double X = Lerp(-Keep.x, Keep.x, K / 9.0);
				Segs.push_back({V3(X, -Keep.y, Z), V3(X, Keep.y, Z)});
			}
		}
		for (int I = 0; I < 4; ++I)
			for (int J = 0; J < 3; ++J)
				Segs.push_back({V3(Lerp(-Keep.x, Keep.x, I / 3.0), Lerp(-Keep.y, Keep.y, J / 2.0), -Keep.z),
					V3(Lerp(-Keep.x, Keep.x, I / 3.0), Lerp(-Keep.y, Keep.y, J / 2.0), Keep.z)});
		for (const auto& [Sa, Sb] : Segs)
			if (Cut(O + Bs.Apply(Sa), O + Bs.Apply(Sb))) ++Hits;
		const V3 Ball = O + Bs.Apply(V3(0.0, 0.0, -Keep.z - 0.006));
		const V3 Off = Clamp + (Ball - Clamp).Normalized() * 0.03;
		if (SegmentHits(Others, Off, Ball)) ++Hits;
		for (const V3& C : {V3(), V3(-1, -1, 0), V3(1, -1, 0), V3(-1, 1, 0), V3(1, 1, 0)})
		{
			const V3 Corner = Keep - V3(0.02, 0.012, 0.0);
			const V3 Pt = O + Bs.Apply(V3(C.x * Corner.x, C.y * Corner.y, Keep.z + 0.002));
			const V3 From = Pt + (Eye - Pt).Normalized() * 0.004;
			if (Cut(From, Eye)) ++Blocked;
		}
		return Hidden * 3 + Hits * 2 + Blocked;
	};
	// (_phone_place: the places tried, a clamp on the bar and the phone's middle, best first found.)
	std::vector<std::pair<V3, V3>> Spots;
	for (double Back : {0.19, 0.22, 0.16, 0.25, 0.28})
	{
		V3 Dir;
		const V3 C = BarPoint(Back, Dir);
		if (C.x < 0.05) continue;
		for (double Fwd : {-0.035, -0.07})
			for (double Up : {0.07, 0.09, 0.05, 0.11})
				for (double Dx : {0.0, -0.03, 0.03}) Spots.push_back({C, C + V3(Dx, Up, Fwd)});
	}
	{
		V3 Dir;
		const V3 Mid = BarPoint(std::max(BarLen - 0.05, 0.0), Dir);
		for (double Up : {0.12, 0.15, 0.18, 0.21})
			for (double Fwd : {-0.08, -0.12, -0.04}) Spots.push_back({Mid, V3(0.0, Mid.y + Up, Mid.z + Fwd)});
	}
	int Best = -1;
	V3 At, Clamp;
	for (const auto& [C, O] : Spots)
	{
		const int Sc = Score(O, Facing(O), C);
		if (Best < 0 || Sc < Best)
		{
			Best = Sc;
			At = O;
			Clamp = C;
		}
		if (Sc == 0) break;
	}
	if (Best < 0) return;
	const GAffine Bs = Facing(At);
	const GAffine Cradle = AtWith(Bs, At + H);   // in the bike's space
	CodeShapes::GodotNames Named;
	auto Code = [&](const std::string& Name, const CodeShapes::GMesh& M, const GAffine& Xf, const std::string& Tag) {
		CodeShapes::Emit(Out, "satnav", "kit/phone.glb", "steer", Named(Name), M, Xf, Tag);
	};
	auto InCradle = [&](const std::string& Name, const V3& C, const V3& S, const std::string& Tag) { Code(Name, CodeShapes::Box(S), Cradle * GAffine::At(C), Tag); };
	Put(Out, "satnav", "kit/phone.glb", Cradle * GAffine::Scale(V3(Half.x / 0.068, Half.y / 0.0435, 1.0)), "steer");
	// The buttons along the top edge; the back plate, the jaws gripping each end with a lip over the
	// glass, and the hooks under it.
	InCradle("", V3(Half.x * 0.3, Half.y + 0.0008, 0.0), V3(0.018, 0.0016, 0.003), "black_metal");
	InCradle("CradlePlate", V3(0.0, 0.0, -Half.z - 0.004), V3(Half.x * 1.2, Half.y * 1.4, 0.006), "paint");
	for (double Side : {-1.0, 1.0})
	{
		InCradle("CradleJaw", V3(Side * (Half.x + 0.004), 0.0, -0.002), V3(0.008, Half.y * 1.2, Half.z * 2.0 + 0.01), "paint");
		InCradle("", V3(Side * (Half.x - 0.0015), 0.0, Half.z + 0.0012), V3(0.005, Half.y * 0.9, 0.0024), "rubber");
		InCradle("CradleHook", V3(Side * Half.x * 0.45, -Half.y - 0.004, -0.002), V3(0.016, 0.008, Half.z * 2.0 + 0.008), "paint");
		InCradle("", V3(Side * Half.x * 0.45, -Half.y - 0.002, Half.z + 0.0012), V3(0.016, 0.004, 0.0024), "rubber");
	}
	// The ball joint on the back of the plate, and its collar.
	const V3 BallLocal(0.0, 0.0, -Half.z - 0.016);
	Code("", CodeShapes::Sphere(0.011), Cradle * GAffine::At(BallLocal), "polished_alloy");
	Code("", CodeShapes::Cylinder(0.009, 0.008), Cradle * GAffine::At(V3(0.0, 0.0, -Half.z - 0.009)) * GAffine::Rotation(GodotRight, TorqueSim::Pi * 0.5), "paint");
	// The arm down to the clamp round the bar, with a thumb wheel to lock it (steering space, + H).
	const V3 Ball = At + Bs.Apply(BallLocal);
	V3 BarDir;
	BarPoint(BackOf(Clamp), BarDir);
	V3 OutDir = Ball - Clamp;
	OutDir = (OutDir - BarDir * OutDir.Dot(BarDir)).Normalized();
	const V3 ClampTop = Clamp + OutDir * 0.02;
	auto UpTo = [](const V3& Dir) {
		// (_tube's turn from up to Dir: straight down is half a turn about X.)
		if (GodotUp.Cross(Dir).Length() <= 0.0001 && Dir.y < 0.0) return GAffine::Rotation(GodotRight, TorqueSim::Pi);
		return BasisUp(Dir);
	};
	const V3 ArmDir = (Ball - ClampTop).Normalized();
	Code("PhoneArm", CodeShapes::Cylinder(0.006, std::max((Ball - ClampTop).Length(), 0.001), -1.0, 12), AtWith(UpTo(ArmDir), (ClampTop + Ball) * 0.5 + H), "polished_alloy");
	Code("", CodeShapes::Cylinder(0.013, 0.007), AtWith(BasisUp(ArmDir), MixV(ClampTop, Ball, 0.45) + H), "paint");
	Code("PhoneClamp", CodeShapes::Cylinder(0.019, 0.03), AtWith(BasisUp(BarDir), Clamp + H), "paint");
	Code("", CodeShapes::Cylinder(0.006, 0.014), AtWith(BasisUp(OutDir * -1.0), Clamp - OutDir * 0.02 + H), "polished_alloy");
	// The screen: a quad on the glass, looking out of it at the rider (its map is drawn live).
	CodeShapes::GMesh Quad;
	for (double Y : {-1.0, 1.0})
		for (double X : {-1.0, 1.0}) Quad.Add(V3(X * Sw * 0.5, Y * Sh * 0.5, 0.0), V3(0.0, 0.0, 1.0), X * 0.5 + 0.5, 0.5 - Y * 0.5);
	Quad.Quad(0, 1, 2, 3);
	Code("SatnavScreen", Quad, Cradle * GAffine::At(V3(0.0, 0.0, Half.z + 0.0005)), "glass");
}

bool BikeParts::DrawnBox(const std::vector<FittedPart>& Out, const std::vector<std::string>& Slots, V3& Lo, V3& Hi) const
{
	if (!Meshes) return false;
	auto In = [&](const std::string& S) { return std::find(Slots.begin(), Slots.end(), S) != Slots.end(); };
	bool Any = false;
	for (const FittedPart& F : Out)
	{
		PartMesh Glb;
		if (!Meshes(F.Glb, Glb)) continue;
		for (const PartSurface& S : Draw(Glb, F).Surfaces)
		{
			const size_t Cut = S.Material.find("__");
			if (!In(Cut != std::string::npos ? S.Material.substr(Cut + 2) : F.Slot)) continue;
			for (const V3& P : S.Pos)
			{
				const V3 G = ToGodot(P);
				if (!Any) { Lo = Hi = G; Any = true; continue; }
				Lo = V3(std::min(Lo.x, G.x), std::min(Lo.y, G.y), std::min(Lo.z, G.z));
				Hi = V3(std::max(Hi.x, G.x), std::max(Hi.y, G.y), std::max(Hi.z, G.z));
			}
		}
	}
	return Any;
}

std::map<std::string, V3> BikeParts::Fairing(const PartsMap& Parts, const Layout& L, double FrameHalfW, std::vector<FittedPart>& Out) const
{
	// (BikeModel.fairing_set: which set a frame-mounted fairing is drawn as; one on the bars is
	// code-drawn, so not here.)
	std::map<std::string, V3> Mounts;
	const Json* P = Lay.Part(PartIn(Parts, "fairing"));
	if (!P || !P->Flag("frame_mount")) return Mounts;
	const Json* Look = P->Get("look");
	const std::string Shape = PartStr(Look, "shape", P->Str("style", ""));
	std::string Set;
	if (Shape == "race") Set = Look && Look->Get("lamps") && !Look->Flag("lamps") ? "race" : "sport";
	else if (Shape == "seeley" || Shape == "adventure" || Shape == "touring" || Shape == "half") Set = Shape;
	else if (Shape.rfind("naked_", 0) == 0) Set = Shape;   // beenster's naked fairings (Data/Parts/Redrawn/fairing/set)
	const std::string Path = "fairing/set/" + Set + ".glb";
	if (Set.empty() || !HasGlb(Path)) return Mounts;
	// The set's base mesh's bounds (its keys at 0), in the glb's Godot frame.
	V3 BoxLo(-0.2, -0.3, -0.3), BoxHi(0.2, 0.1, 0.5);
	PartMesh Base;
	if (Meshes && Meshes(Path, Base))
	{
		bool First = true;
		for (const PartSurface& S : Base.Surfaces)
			for (const V3& V : S.Pos)
			{
				const V3 G = ToGodot(V);
				if (First) { BoxLo = BoxHi = G; First = false; continue; }
				BoxLo = V3(std::min(BoxLo.x, G.x), std::min(BoxLo.y, G.y), std::min(BoxLo.z, G.z));
				BoxHi = V3(std::max(BoxHi.x, G.x), std::max(BoxHi.y, G.y), std::max(BoxHi.z, G.z));
			}
		// (Godot's mesh bounds take in each shape key at its full weight too.)
		for (const ShapeKey& K : Base.Keys)
			for (const ShapeKey::Moves& Mv : K.Surfaces)
				for (size_t I = 0; I < Mv.Vert.size() && Mv.Surface < Base.Surfaces.size(); ++I)
				{
					const V3 G = ToGodot(Base.Surfaces[Mv.Surface].Pos[Mv.Vert[I]] + Mv.DPos[I]);
					BoxLo = V3(std::min(BoxLo.x, G.x), std::min(BoxLo.y, G.y), std::min(BoxLo.z, G.z));
					BoxHi = V3(std::max(BoxHi.x, G.x), std::max(BoxHi.y, G.y), std::max(BoxHi.z, G.z));
				}
	}
	// (RigBodywork.fairing_fit: a full set wide enough round the engine as drawn and outside the
	// frame, its belly down to the style's ground line, its sides back to the seat's nose.)
	constexpr double Wide = 0.15, Long = 0.15, Drop = 0.08, Room = 0.035, BellyOver = 0.02;
	const V3 Stay = ToGodot(L.StayHead);
	FittedPart F;
	F.Slot = "fairing";
	F.Glb = Path;
	if (Set == "sport" || Set == "race")
	{
		const V3 A = ToGodot(L.EngineMin), B = ToGodot(L.EngineMax);
		V3 ELo(std::min(A.x, B.x), std::min(A.y, B.y), std::min(A.z, B.z)), EHi(std::max(A.x, B.x), std::max(A.y, B.y), std::max(A.z, B.z));
		V3 DLo, DHi;
		if (DrawnBox(Out, {"engine", "head"}, DLo, DHi))
		{
			ELo = V3(std::min(ELo.x, DLo.x), std::min(ELo.y, DLo.y), std::min(ELo.z, DLo.z));
			EHi = V3(std::max(EHi.x, DHi.x), std::max(EHi.y, DHi.y), std::max(EHi.z, DHi.z));
		}
		const double Need = std::max(std::max(std::fabs(ELo.x), std::fabs(EHi.x)), FrameHalfW) + Room;
		F.Weights["width"] = TorqueSim::Clamp((Need / std::max(BoxHi.x, 0.05) - 1.0) / Wide, 0.0, 2.5);
		F.Weights["drop"] = TorqueSim::Clamp((Stay.y + BoxLo.y - L.BellyY - BellyOver) / Drop, -1.0, 2.0);
		if (BoxHi.z > 0.1)
		{
			const double Reach = std::max(ToGodot(L.SeatFront).z, EHi.z + Room) - Stay.z;
			F.Weights["length"] = TorqueSim::Clamp((Reach / BoxHi.z - 1.0) / Long, -1.0, 1.5);
		}
	}
	const V3 Shift = L.StayHead;
	F.Warp = [Shift](const V3& Q) { return Q + Shift; };
	Out.push_back(F);
	const double Kx = 1.0 + Wide * (F.Weights.count("width") ? F.Weights.at("width") : 0.0);
	for (const char* Name : {"mirror_r", "mirror_l", "lamp"})
	{
		V3 At;
		if (Mount(Path, Name, At)) Mounts[Name] = Stay + V3(At.x * Kx, At.y, At.z);
	}
	return Mounts;
}

std::vector<FittedPart> BikeParts::Fit(const PartsMap& Parts, const Layout& L) const
{
	const Bodywork Body = BikeBody(Lay).Fit(Parts, L);
	std::vector<FittedPart> Out = Body.Parts;
	double CodeHalfW = 0.0;
	Frame(Parts, L, Out, &CodeHalfW);
	Engine(Parts, L, Out);
	Ancillaries(Parts, L, Body, Out);
	TankTrim(Parts, L, Body, Out);
	SaddleTrim(Parts, L, Body, Out);
	Exhaust(Parts, L, Out);
	Swingarm(Parts, L, Out);
	Fork(Parts, L, Out);
	const Bars Bar = Handlebars(Parts, L, Out);
	std::vector<V3> DashFace;
	const double DashLift = Dashboard(Parts, L, Bar, Out, &DashFace);
	Headlight(Parts, Out, ToGodot(L.Head));
	// (BikeModel: the fairing after the tail, the mirrors after it, on its mounts.)
	V3 FrameLo, FrameHi;
	const double FrameHalfW = std::max(CodeHalfW, DrawnBox(Out, {"frame"}, FrameLo, FrameHi) ? (FrameHi.x - FrameLo.x) * 0.5 : 0.0);
	const std::map<std::string, V3> FairingMounts = Fairing(Parts, L, FrameHalfW, Out);
	BarFairing(Parts, L, Out);
	Mirrors(Parts, L, Bar, FairingMounts, Out);
	Indicators(Parts, L, Out);
	FrontMudguard(Parts, L, Out);
	Wheels(Parts, L, Out);
	TailUnit(Parts, L, Body, Out);
	Stands(Parts, L, Out);
	Rearsets(Parts, L, Out);
	HornAndEcu(Parts, L, Out);
	SidePanels(Parts, L, Out);
	Guards(Parts, L, Bar, Out);
	Luggage(Parts, L, Body, Out);
	TankBag(Parts, L, Body, Out);
	Satnav(Parts, L, DashLift, Bar, DashFace, Out);
	RearMaster(L, Out);
	FinalDrive(Parts, L, Out);
	Shocks(Parts, L, Out);
	// (BikeModel._full_fairing_fitted: a full race fairing, or an adventure one, carries its own
	// lamps, so the plain headlight is tucked away inside it. So does each of beenster's naked
	// fairings, whose lamps are drawn into the fairing.)
	if (const Json* F = Lay.Part(PartIn(Parts, "fairing")))
	{
		const std::string Shape = PartStr(F->Get("look"), "shape", F->Str("style", ""));
		if ((F->Str("style") == "race" && Shape != "seeley") || Shape == "adventure" || (F->Flag("frame_mount") && Shape.rfind("naked_", 0) == 0))
			for (FittedPart& P : Out)
				if (P.Slot == "headlight") P.Hidden = true;
	}
	CoverUp(Parts, L, Out);
	return Out;
}

void BikeParts::CoverUp(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const
{
	const Json* F = Lay.Part(PartIn(Parts, "fairing"));
	if (!F || !F->Flag("frame_mount") || !Meshes) return;
	const Json* Look = F->Get("look");
	const std::string Shape = PartStr(Look, "shape", F->Str("style", ""));
	const bool Full = F->Str("style") == "race" && Shape != "seeley";   // (the code's _full_fairing, which sets _covered)
	const bool Touring = Shape == "touring";                              // (CAPPED_SETS)
	if (!Full && !Touring) return;
	struct Piece { FittedPart* Part; std::string Slot, Tag; V3 Lo, Hi; };
	std::vector<Piece> Pieces;
	std::vector<SegTri> Skin;
	V3 FLo(1e9, 1e9, 1e9), FHi(-1e9, -1e9, -1e9);
	double TopY = -1e18;
	const double HeadZ = ToGodot(L.Head).z;
	// (A mesh's bounds as Godot's _model_aabb takes them: a glb piece put on whole is its own box,
	// with each shape key at full weight, carried through the placing and boxed again; one baked
	// into the bike's space (bent, morphed, laid in copies, made here) is its points' box.)
	auto Affine = [](const PartMesh::Warp& W) {
		const V3 O = W(V3()), A = W(V3(1, 0, 0)), B = W(V3(0, 1, 0)), C = W(V3(0, 0, 1)), D = W(V3(0.3, -0.7, 0.5));
		return (O + (A - O) * 0.3 + (B - O) * -0.7 + (C - O) * 0.5 - D).Length() < 1e-9;
	};
	for (FittedPart& P : Out)
	{
		PartMesh Glb;
		if (!Meshes(P.Glb, Glb)) continue;
		const PartMesh Drawn = Draw(Glb, P);
		const bool Whole = !P.Made && P.Copies.empty() && P.Weights.empty() && P.Warp && Affine(P.Warp) && Drawn.Surfaces.size() == Glb.Surfaces.size();
		for (size_t Si = 0; Si < Drawn.Surfaces.size(); ++Si)
		{
			const PartSurface& S = Drawn.Surfaces[Si];
			const size_t Cut = S.Material.find("__");
			Piece Pc{&P, Cut != std::string::npos ? S.Material.substr(Cut + 2) : P.Slot, S.Material, V3(1e9, 1e9, 1e9), V3(-1e9, -1e9, -1e9)};
			auto Grow = [&](const V3& G) {
				Pc.Lo = V3(std::min(Pc.Lo.x, G.x), std::min(Pc.Lo.y, G.y), std::min(Pc.Lo.z, G.z));
				Pc.Hi = V3(std::max(Pc.Hi.x, G.x), std::max(Pc.Hi.y, G.y), std::max(Pc.Hi.z, G.z));
			};
			if (Whole)
			{
				V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
				auto Local = [&](const V3& V) { Lo = V3(std::min(Lo.x, V.x), std::min(Lo.y, V.y), std::min(Lo.z, V.z)); Hi = V3(std::max(Hi.x, V.x), std::max(Hi.y, V.y), std::max(Hi.z, V.z)); };
				for (const V3& V : Glb.Surfaces[Si].Pos) Local(V);
				for (const ShapeKey& K : Glb.Keys)
					for (const ShapeKey::Moves& Mv : K.Surfaces)
						if (Mv.Surface == Si)
							for (size_t I = 0; I < Mv.Vert.size(); ++I) Local(Glb.Surfaces[Si].Pos[Mv.Vert[I]] + Mv.DPos[I]);
				for (int C = 0; C < 8; ++C) Grow(ToGodot(P.Warp(V3(C & 1 ? Hi.x : Lo.x, C & 2 ? Hi.y : Lo.y, C & 4 ? Hi.z : Lo.z))));
			}
			for (const V3& V : S.Pos)
			{
				const V3 G = ToGodot(V);
				if (!Whole) Grow(G);
				// (Up to the side panels' top edge behind the head stock: the shell's highest point there.)
				if (Pc.Slot == "fairing" && S.Material == "paint" && G.z > HeadZ + 0.1 && std::fabs(G.x) > 0.1) TopY = std::max(TopY, G.y);
			}
			if (Pc.Slot == "fairing")
			{
				FLo = V3(std::min(FLo.x, Pc.Lo.x), std::min(FLo.y, Pc.Lo.y), std::min(FLo.z, Pc.Lo.z));
				FHi = V3(std::max(FHi.x, Pc.Hi.x), std::max(FHi.y, Pc.Hi.y), std::max(FHi.z, Pc.Hi.z));
				for (size_t I = 0; I + 2 < S.Idx.size(); I += 3)
					Skin.push_back({ToGodot(S.Pos[S.Idx[I]]), ToGodot(S.Pos[S.Idx[I + 1]]), ToGodot(S.Pos[S.Idx[I + 2]])});
			}
			Pieces.push_back(Pc);
		}
	}
	if (Skin.empty()) return;
	// (_covered: from the set's nose back past the code fairing's end (round the engine and gearbox
	// as drawn), from its belly up to the panels' top edge, every x.)
	bool Covers = false;
	V3 CLo, CHi;
	if (Full)
	{
		const V3 Crank = ToGodot(L.Crank), Pivot = ToGodot(L.Pivot);
		double BodyEnd = Crank.z;
		for (const Piece& Pc : Pieces)
			if (Pc.Slot == "engine" || Pc.Slot == "gearbox") BodyEnd = std::max(BodyEnd, Pc.Hi.z);
		const double Z1 = TorqueSim::Clamp(BodyEnd + 0.03, Crank.z + (Pivot.z - Crank.z) * 0.6, Pivot.z - 0.02);
		if (TopY < -1e17) TopY = FHi.y;
		CLo = V3(-1.0, FLo.y, FLo.z - 0.02);
		CHi = V3(1.0, TopY, std::max(Z1, FHi.z));
		Covers = true;
		// (_hide_covered: each mesh of the engine bay's slots wholly inside, side on, with a little
		// give at the ends; the airbox lives under the tank, reached past the panel's flank.)
		static const std::set<std::string> CoveredSlots = {"engine", "head", "gearbox", "cooling", "fuel", "spark_plugs", "oil_filter", "ecu", "horn", "exhaust"};
		for (const Piece& Pc : Pieces)
			if (CoveredSlots.count(Pc.Slot) && Pc.Lo.y >= CLo.y - 0.02 && Pc.Hi.y <= CHi.y + 0.02 && Pc.Lo.z >= CLo.z - 0.05 && Pc.Hi.z <= CHi.z + 0.04)
				Pc.Part->HiddenTags.push_back(Pc.Tag);
	}
	auto IsHidden = [](const Piece& Pc) { return std::find(Pc.Part->HiddenTags.begin(), Pc.Part->HiddenTags.end(), Pc.Tag) != Pc.Part->HiddenTags.end(); };
	// (_service_caps: the oil filter under a full fairing, the air filter under a touring set.)
	std::vector<FittedPart> Caps;
	for (const std::string& Slot : {std::string("air_filter"), std::string("oil_filter")})
	{
		if (Touring != (Slot == "air_filter")) continue;
		std::vector<const Piece*> Mine;
		V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
		bool Behind = Touring;
		for (const Piece& Pc : Pieces)
			if (Pc.Slot == Slot)
			{
				Mine.push_back(&Pc);
				Lo = V3(std::min(Lo.x, Pc.Lo.x), std::min(Lo.y, Pc.Lo.y), std::min(Lo.z, Pc.Lo.z));
				Hi = V3(std::max(Hi.x, Pc.Hi.x), std::max(Hi.y, Pc.Hi.y), std::max(Hi.z, Pc.Hi.z));
				Behind = Behind || IsHidden(Pc);
			}
		if (Covers) Behind = Behind || (Hi.y > CLo.y && Lo.y < CHi.y && Hi.z > CLo.z && Lo.z < CHi.z);
		if (Mine.empty() || !Behind) continue;
		const V3 Mid = (Lo + Hi) * 0.5;
		// The panel straight out to the right of it, or as near that as the panel goes.
		V3 At, Normal(1.0, 0.0, 0.0);
		bool Found = false;
		for (const std::pair<double, double>& D : {std::pair<double, double>(0.0, 0.0), {0.0, 0.05}, {0.0, -0.05}, {-0.05, 0.0}, {0.05, 0.0},
				{-0.05, 0.08}, {-0.05, -0.08}, {0.0, 0.12}, {0.0, -0.12}, {-0.1, 0.0}})
		{
			const double Y = Mid.y + D.first, Z = Mid.z + D.second;
			double Best = -1e18;
			const V3 From(1.0, Y, Z), Dir(-1.0, 0.0, 0.0);
			for (const SegTri& T : Skin)
			{
				// (Geometry3D.segment_intersects_triangle, for where it crosses.)
				const V3 E1 = T.B - T.A, E2 = T.C - T.A, Pv = Dir.Cross(E2);
				const double Det = E1.Dot(Pv);
				if (std::fabs(Det) < 0.00001) continue;
				const double Inv = 1.0 / Det;
				const V3 Tv = From - T.A;
				const double U = Tv.Dot(Pv) * Inv;
				if (U < 0.0 || U > 1.0) continue;
				const V3 Q = Tv.Cross(E1);
				const double V = Dir.Dot(Q) * Inv;
				if (V < 0.0 || U + V > 1.0) continue;
				const double Tt = E2.Dot(Q) * Inv;
				if (Tt > 0.00001 && Tt <= 1.0 && 1.0 - Tt > Best)
				{
					Best = 1.0 - Tt;
					Normal = (T.C - T.A).Cross(T.B - T.A).Normalized();
				}
			}
			if (Best > -1e17)
			{
				At = V3(Best, Y, Z);
				Found = true;
				break;
			}
		}
		if (!Found || (Slot == "air_filter" && Mid.x > At.x))
		{
			// No panel over it after all, or it stands out past the panel: it shows as it is.
			for (const Piece* Pc : Mine)
			{
				std::vector<std::string>& Tags = Pc->Part->HiddenTags;
				Tags.erase(std::remove(Tags.begin(), Tags.end(), Pc->Tag), Tags.end());
			}
			continue;
		}
		for (const Piece* Pc : Mine)
			if (!IsHidden(*Pc)) Pc->Part->HiddenTags.push_back(Pc->Tag);
		if (Normal.x < 0.0) Normal = Normal * -1.0;
		const GAffine Up = BasisUp(Normal);
		const std::string Stand = Slot == "air_filter" ? "filter/airbox.glb" : "filter/oil.glb";
		CodeShapes::Emit(Caps, Slot, Stand, "", "ServiceCap", CodeShapes::Cylinder(0.045, 0.012), AtWith(Up, At + Normal * 0.006), "paint");
		CodeShapes::Emit(Caps, Slot, Stand, "", "", CodeShapes::Cylinder(0.052, 0.006), AtWith(Up, At + Normal * 0.002), "black_metal");
		CodeShapes::Emit(Caps, Slot, Stand, "", "", CodeShapes::Box(V3(0.06, 0.008, 0.012)), AtWith(Up, At + Normal * 0.014), "paint");
	}
	// (Added once Pieces' pointers into Out are done with.)
	Out.insert(Out.end(), Caps.begin(), Caps.end());
}
} // namespace TorqueFit
