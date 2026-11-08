// The rest of a bike's Blender parts on any laid-out bike: where the Godot game's BikeModel puts
// each glb in its Rig built look (scripts/bike_model.gd, scripts/blender_parts.gd), slot by slot,
// from the layout's datums (Fit/BikeLayout). Bodywork that bends onto the bike's lines is
// Fit/BikeBody; this is the parts that go on whole, placed (and scaled) by a transform.
//
// Ported so far: the engine's crankcase and its top end. Each port keeps the Godot frame inside
// (as bike_model.gd) and hands back FittedParts in ours.
#pragma once

#include "BikeBody.h"

#include <functional>
#include <string>
#include <vector>

namespace TorqueFit
{
// A Godot Transform3D: columns X, Y, Z (they may be scaled or sheared) and the origin.
struct GAffine
{
	V3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, O;
	V3 Apply(const V3& P) const { return X * P.x + Y * P.y + Z * P.z + O; }
	GAffine operator*(const GAffine& B) const
	{
		GAffine R;
		R.X = X * B.X.x + Y * B.X.y + Z * B.X.z;
		R.Y = X * B.Y.x + Y * B.Y.y + Z * B.Y.z;
		R.Z = X * B.Z.x + Y * B.Z.y + Z * B.Z.z;
		R.O = Apply(B.O);
		return R;
	}
	static GAffine Scale(const V3& S) { GAffine A; A.X = V3(S.x, 0, 0); A.Y = V3(0, S.y, 0); A.Z = V3(0, 0, S.z); return A; }
	static GAffine At(const V3& Origin) { GAffine A; A.O = Origin; return A; }
	// Godot's Basis(axis, angle).
	static GAffine Rotation(const V3& Axis, double Angle);
	// The warp it makes for a FittedPart (a glb point in ours to the bike's, in ours).
	PartMesh::Warp Warp() const;
};

class BikeParts
{
public:
	// HasGlb: whether Data/Parts/Blender has a glb at that path ("engine/twin_750.glb"), as
	// BlenderParts.part(path) not being empty.
	BikeParts(const BikeLayout& InLayout, std::function<bool(const std::string&)> InHasGlb)
		: Lay(InLayout), HasGlb(std::move(InHasGlb)) {}

	// Every placed part of the bike (the bodywork's too), slot by slot.
	std::vector<FittedPart> Fit(const PartsMap& Parts, const Layout& L) const;

	// The engine's cylinders as BikeModel._place_cylinders lays them (Godot frame): base and "up".
	struct Cylinder { V3 Base, Up; };
	std::vector<Cylinder> Cylinders(const PartsMap& Parts, const Layout& L) const;

private:
	const BikeLayout& Lay;
	std::function<bool(const std::string&)> HasGlb;

	void Engine(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
};
} // namespace TorqueFit
