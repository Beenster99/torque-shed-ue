#include "BikeParts.h"
#include "BikePartsKit.h"

#include <algorithm>
#include <cmath>
#include <memory>

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
constexpr double TyreBulge = 0.434;

double SprocketRadius(int Teeth)
{
	return ChainPitch / (2.0 * std::sin(TorqueSim::Pi / std::max(static_cast<double>(Teeth), 6.0))) + 0.003;
}
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
			const double Step = 2.0 * TorqueSim::Pi / TyreSections;
			FittedPart P;
			P.Slot = Side + "_tyre";
			P.Glb = TyrePath;
			P.Node = Side + "_wheel";
			for (int K = 0; K < TyreSections; ++K)
				P.Copies.push_back(GodotWarpToOurs([=](const V3& V) {
					const double A = (K + V.z) * Step;
					const double R = Base + V.y * Span;
					double X = V.x * Half;
					if (V.y < TyreBulge && std::fabs(V.x) >= 0.9)
						X = (V.x < 0.0 ? -1.0 : 1.0) * TorqueSim::Lerp(Bead, Half, TorqueSim::Clamp((std::fabs(V.x) - 0.9) / 0.1, 0.0, 1.0));
					return Axle + V3(X, std::cos(A) * R, std::sin(A) * R);
				}));
			Out.push_back(P);
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
		// The brake arm on each cam (two on a twin leading shoe).
		std::vector<std::pair<double, double>> Cams = {{R * 0.6, R * 0.5}};
		if (Shoes != "sls") Cams.push_back({-R * 0.6, -R * 0.5});
		for (const auto& Cam : Cams)
		{
			const V3 At(X + 0.01, Cam.first, Cam.second * (Front ? 1.0 : -1.0));
			const V3 Tip = At + V3(0.0, 0.07, Front ? 0.03 : -0.03);
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
		}
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
	// (BikeModel._draw_swingarm, _draw_own_swingarm and _arm_bridge: the Blender pieces; the arms'
	// code-drawn shells and tubes aren't here yet.)
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
	// (_to_arm: a point of the bike's at rest as the swingarm sees it.)
	const V3 PivotG = ToGodot(L.Pivot), ToAxle = ToGodot(L.RearAxle) - PivotG;
	auto ToArm = [&](const V3& P) { return GAffine::Rotation(GodotRight, std::atan2(ToAxle.y, ToAxle.z)).Apply(P - PivotG); };
	const V3 SeatFrontG = ToGodot(L.SeatFront);
	if (Own)
	{
		if (Kind == "single_gull") return;
		if (Kind == "box_section")
		{
			for (double Side : {-1.0, 1.0})
				ArmTube("swingarm", V3(ArmX * Side, 0.0, ArmLen + 0.045), V3(ArmX * Side, 0.0, ArmLen + 0.075), 0.006);
			ArmTube("swingarm", V3(-ArmX, 0.0, ArmLen * 0.36), V3(ArmX, 0.0, ArmLen * 0.36), 0.016);
		}
		else if (Kind == "banana")
			ArmTube("swingarm", V3(-ArmX, 0.06, ArmLen * 0.4), V3(ArmX, 0.08, ArmLen * 0.4), 0.014);
		else if (Kind == "braced_mx")
			for (double Side : {-1.0, 1.0})
			{
				const double X = ArmX * Side;
				ArmBend("swingarm", {V3(X, -0.05, 0.06), V3(X, -0.1, ArmLen * 0.38), V3(X, -0.05, ArmLen * 0.78)}, 0.013);
				for (double F : {0.22, 0.42, 0.62})
				{
					const double Y = -0.05 - 0.05 * std::sin(TorqueSim::Clamp(F / 0.78, 0.0, 1.0) * TorqueSim::Pi);
					ArmTube("swingarm", V3(X, -0.03, ArmLen * F), V3(X, Y + 0.005, ArmLen * F), 0.007);
				}
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
	if (Kind == "single") return;
	if (Kind == "braced")
	{
		// The brace arching over the right-hand arm (the arms are code-drawn shells).
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
			Put(Out, "spark_plugs", "spark_plugs/coil.glb", GAffine::At(V3(0.0, Top - 0.11, Mid.z + 0.16)), "");
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
			Put(Out, "fuel", Glb, AtWith(BasisAlong(V3(0.0, 0.15, 1.0)), Mid + V3(0.0, 0.0, 0.03)), "");
		else
			for (const Intake& P : Points) Put(Out, "fuel", Glb, AtWith(BasisAlong(P.Dir), P.Port), "");
	}

	// The air filter (_draw_air_filter, _blender_airbox): pods or stacks, a cone, a V-twin's round
	// cleaner, or the airbox under the tank with its element's housing out to the right.
	{
		constexpr double AirFilterOut = 0.21;
		const std::string Style = PartStr(Lay.Part(PartIn(Parts, "air_filter")), "style", "airbox");
		if (Style == "pods" || Style == "stacks")
		{
			const bool Stacks = Style == "stacks";
			for (const Intake& P : Points)
			{
				const double Side = P.Port.x < -0.01 ? -1.0 : 1.0;
				const V3 Dir = (P.Dir + V3(Side * 0.9, 0.0, 0.0)).Normalized();
				const double HalfLen = Stacks ? 0.025 : 0.045;
				const double Reach = std::max(Stacks ? 0.11 : 0.13, (AirFilterOut - std::fabs(P.Port.x)) / std::fabs(Dir.x) - HalfLen);
				Put(Out, "air_filter", Stacks ? "filter/stack.glb" : "filter/pod.glb", AtWith(BasisUp(Dir), P.Port + Dir * Reach), "");
			}
		}
		else if (Style == "cone")
		{
			const V3 Dir = V3(0.75, 0.05, 0.66).Normalized();
			V3 At(std::max(AirFilterOut, Mid.x + 0.12) - Dir.x * 0.07, Mid.y + 0.02, Mid.z + 0.16);
			At.y = std::min(At.y, TankBaseY(At.z) - 0.07);
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
			Put(Out, "cooling", "radiator/oil_cooler.glb", GAffine::At(V3(0.0, E.y + 0.2, FrontZ + 0.035)), "");
		else if (Style == "radiator" || Style == "race")
		{
			const bool Big = Style == "race";
			const double W = Big ? 0.42 : 0.3, H = Big ? 0.34 : 0.28;
			Put(Out, "cooling", "radiator/core.glb", AtWith(GAffine::Rotation(GodotRight, -0.25) * GAffine::Scale(V3(W, H, 1.0)), V3(0.0, E.y + 0.25, FrontZ)), "");
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
			if ((Start - Lever).Length() > 0.02 && Qs && Qs->B)
			{
				const V3 Along = (Lever - Start).Normalized();
				Put(Out, "gearbox", "gearbox/quickshifter.glb", AtWith(BasisUp(Along), Start + Along * std::min(0.06, (Start - Lever).Length() * 0.4)), "");
			}
			if (EngLayout == "single" || EngLayout == "2stroke" || EngLayout == "twin")
				Put(Out, "gearbox", "gearbox/kickstart.glb", GAffine::At(E + V3(BlockW * 0.5 + 0.035, -0.04, 0.2)), "");
		}
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
		// (Its hose is code-drawn.)
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

void BikeParts::Dashboard(const PartsMap& Parts, const Layout& L, const Bars& Bar, std::vector<FittedPart>& Out) const
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
		struct Tri { V3 A, B, C; };
		std::vector<Tri> BarTris;
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
		auto Blocked = [&](const V3& From, const V3& To) {
			const V3 D = To - From;
			for (const Tri& T : BarTris)
			{
				// (Geometry3D.segment_intersects_triangle, both faces, its epsilons too: a triangle
				// small beside the segment counts as edge on and is missed, as in Godot.)
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
		};
		const V3 Eye = V3(0.0, L.Stats.SeatHeight - L.Stats.RearRadius + 0.88, ToGodot(L.Hip).z - 0.02) - H;
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
		return C + UpDir * (Bar.R + 0.012);
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
		Put(Out, "mirrors", Round ? "mirror/round.glb" : "mirror/rect.glb",
			AtWith(Aim(At, OnBars) * GAffine::Scale(V3(Sx, Round ? Sx : Sy, 1.0)), At + (OnBars ? H : V3())), OnBars ? "steer" : "");
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
				Put(Out, AtFront ? FrontSlot : BackSlot, Glb,
					AtWith(AtFront ? GAffine() : GAffine::Rotation(GodotUp, TorqueSim::Pi), Tip + (AtFront ? H : V3())), AtFront ? "steer" : "");
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
	GuardArc(Out, "front_mudguard", Section, ToGodot(L.FrontAxle), R + Gap, W, A0, A1, Style == "classic" ? 10 : 6, "front_hub");
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
	Exhaust(Parts, L, Out);
	Swingarm(Parts, L, Out);
	Fork(Parts, L, Out);
	const Bars Bar = Handlebars(Parts, L, Out);
	Dashboard(Parts, L, Bar, Out);
	Headlight(Parts, Out, ToGodot(L.Head));
	// (BikeModel: the fairing after the tail, the mirrors after it, on its mounts.)
	V3 FrameLo, FrameHi;
	const double FrameHalfW = std::max(CodeHalfW, DrawnBox(Out, {"frame"}, FrameLo, FrameHi) ? (FrameHi.x - FrameLo.x) * 0.5 : 0.0);
	const std::map<std::string, V3> FairingMounts = Fairing(Parts, L, FrameHalfW, Out);
	Mirrors(Parts, L, Bar, FairingMounts, Out);
	Indicators(Parts, L, Out);
	FrontMudguard(Parts, L, Out);
	Wheels(Parts, L, Out);
	RearMaster(L, Out);
	FinalDrive(Parts, L, Out);
	Shocks(Parts, L, Out);
	// (BikeModel._full_fairing_fitted: a full race fairing, or an adventure one, carries its own
	// lamps, so the plain headlight is tucked away inside it.)
	if (const Json* F = Lay.Part(PartIn(Parts, "fairing")))
	{
		const std::string Shape = PartStr(F->Get("look"), "shape", F->Str("style", ""));
		if ((F->Str("style") == "race" && Shape != "seeley") || Shape == "adventure")
			for (FittedPart& P : Out)
				if (P.Slot == "headlight") P.Hidden = true;
	}
	return Out;
}
} // namespace TorqueFit
