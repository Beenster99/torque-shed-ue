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

std::vector<FittedPart> BikeParts::Fit(const PartsMap& Parts, const Layout& L) const
{
	std::vector<FittedPart> Out = BikeBody(Lay).Fit(Parts, L).Parts;
	Engine(Parts, L, Out);
	return Out;
}
} // namespace TorqueFit
