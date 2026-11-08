#include "BikeParts.h"

#include <cmath>

namespace TorqueFit
{
namespace
{
std::string PartIn(const PartsMap& P, const std::string& Slot)
{
	auto It = P.find(Slot);
	return It == P.end() || It->second.empty() ? std::string("none") : It->second;
}

const V3 GodotUp(0.0, 1.0, 0.0);

// BlenderParts' reference spacing and per-cylinder bores (blender_parts.gd).
constexpr double TopEndSpacing = 0.12;
double PerCylinderBore(const std::string& Layout)
{
	if (Layout == "2stroke") return 0.045;
	if (Layout == "vtwin" || Layout == "boxer") return 0.05;
	return 0.0;
}

double AngleTo(const V3& A, const V3& B)
{
	return std::atan2(A.Cross(B).Length(), A.Dot(B));
}

double PartNum(const Json* P, const char* Key, double Default)
{
	return P ? P->Num(Key, Default) : Default;
}

std::string PartStr(const Json* P, const char* Key, const std::string& Default)
{
	return P ? P->Str(Key, Default) : Default;
}

// Godot's Basis(x, y, z) from its columns, and a transform at an origin.
GAffine BasisCols(const V3& X, const V3& Y, const V3& Z, const V3& O = V3())
{
	GAffine A;
	A.X = X;
	A.Y = Y;
	A.Z = Z;
	A.O = O;
	return A;
}

GAffine AtWith(const GAffine& Basis, const V3& Origin)
{
	GAffine A = Basis;
	A.O = Origin;
	return A;
}

// BikeModel._basis_up: turned from straight up to `Up`.
GAffine BasisUp(const V3& Up)
{
	const V3 Axis = GodotUp.Cross(Up);
	if (Axis.Length() < 0.0001) return GAffine();
	return GAffine::Rotation(Axis.Normalized(), AngleTo(GodotUp, Up));
}

const V3 GodotRight(1.0, 0.0, 0.0);

PartMesh::Warp GodotWarpToOurs(std::function<V3(const V3&)> GodotWarp)
{
	return [GodotWarp](const V3& Ours) { return FromGodot(GodotWarp(ToGodot(Ours))); };
}
constexpr double ChainPitch = 0.015875;   // m: a 5/8" chain (BikeModel.CHAIN_PITCH)
constexpr double ChainRun = 8.0 * ChainPitch;
constexpr double DrumArm = 0.076;
constexpr double DrumShoeR = 0.1;
constexpr double BoxArmReach = 0.55;
constexpr int TyreSections = 32;
constexpr double TyreBulge = 0.434;

// BikeModel.rim_width_of, tread_pattern and sprocket_radius.
double RimWidthOf(const Json* Wheel) { return PartNum(Wheel, "rim_width", 3.5) * 0.0254; }
std::string TreadPattern(const Json* Tyre)
{
	const std::string Style = PartStr(Tyre, "style", "road");
	if (Style == "slick") return "";
	return PartStr(Tyre, "tread", Style == "offroad" ? "knobs" : "sport");
}
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
	if (Own)
	{
		if (Kind == "single_gull") return;
		if (Kind == "box_section" || Kind == "banana" || Kind == "braced_mx" || Kind == "hardtail")
		{
			Bridge("swingarm");
			return;
		}
	}
	if (Kind == "single") return;
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

std::vector<FittedPart> BikeParts::Fit(const PartsMap& Parts, const Layout& L) const
{
	std::vector<FittedPart> Out = BikeBody(Lay).Fit(Parts, L).Parts;
	Engine(Parts, L, Out);
	Swingarm(Parts, L, Out);
	Fork(Parts, L, Out);
	Wheels(Parts, L, Out);
	FinalDrive(Parts, L, Out);
	Shocks(Parts, L, Out);
	return Out;
}
} // namespace TorqueFit
