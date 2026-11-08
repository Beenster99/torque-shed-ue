// The rest of a bike's Blender parts on any laid-out bike: where the Godot game's BikeModel puts
// each glb in its Rig built look (scripts/bike_model.gd, scripts/blender_parts.gd), slot by slot,
// from the layout's datums (Fit/BikeLayout). Bodywork that bends onto the bike's lines is
// Fit/BikeBody; this is the parts that go on whole, placed (and scaled) by a transform.
//
// Ported so far: the engine's crankcase and its top end, and its ancillaries; the exhaust; the running gear (rims and cush drive,
// tyres, discs and drums, calipers and pads, the fork, the sprockets and chain, the swingarm's
// Blender pieces and the shocks). Each port keeps the Godot frame inside (as bike_model.gd) and
// hands back FittedParts in ours. Still code-drawn in Godot and not here yet: frame and swingarm
// tubes and shells, brake hoses. The bars, grips, controls and cables are here. The exhaust's headers and cans are here (its heat
// shield is code-drawn). Swept and bent parts take their normals from the warp, which differs a
// little from Godot's (it turns the section's normals without the stretch).
#pragma once

#include "BikeBody.h"
#include "FitJson.h"

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
	// Index: Data/Parts/Blender/index.json's "parts" (which glbs there are, their mounts).
	BikeParts(const BikeLayout& InLayout, const Json& InIndex) : Lay(InLayout), Index(InIndex) {}

	// Where the glbs' meshes come from (Data/Parts/Blender/<path> loaded with its shape keys), for
	// the few placements that measure another part's mesh, as BikeModel's slot_bounds does (a
	// V-twin's air cleaner stands out past the tank's edge). Without it they use the loft's sections.
	using MeshSource = std::function<bool(const std::string& Glb, PartMesh& Out)>;
	void SetMeshSource(MeshSource In) { Meshes = std::move(In); }

	// The mesh a fitted part draws: its glb's (with its shape keys) deformed by its warp, or by
	// each of its copies' warps one after the other.
	static PartMesh Draw(const PartMesh& Glb, const FittedPart& P);

	// Every placed part of the bike (the bodywork's too), slot by slot.
	std::vector<FittedPart> Fit(const PartsMap& Parts, const Layout& L) const;

	// The engine's cylinders as BikeModel._place_cylinders lays them (Godot frame): base and "up".
	struct Cylinder { V3 Base, Up; };
	std::vector<Cylinder> Cylinders(const PartsMap& Parts, const Layout& L) const;
	// Where the intake meets each cylinder (BikeModel._intake_points, Godot frame).
	struct Intake { V3 Port, Dir; };
	std::vector<Intake> Intakes(const PartsMap& Parts, const Layout& L) const;

private:
	const BikeLayout& Lay;
	const Json& Index;
	MeshSource Meshes;

	// Whether Data/Parts/Blender has a glb at that path ("engine/twin_750.glb"), as
	// BlenderParts.part(path) not being empty.
	bool HasGlb(const std::string& Path) const;
	// A mount of the glb at Path (Godot frame), or false.
	bool Mount(const std::string& Path, const std::string& Name, V3& At) const;
	// Puts a glb on as BikeModel._blender_pieces does, if there is one: true when it went on.
	bool Put(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Path, const GAffine& Xf, const std::string& Node) const;

	void Engine(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void Wheels(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void Brake(const PartsMap& Parts, const Layout& L, bool Front, double RimR, double RimW, std::vector<FittedPart>& Out) const;
	void Fork(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void Swingarm(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void FinalDrive(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void Shocks(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	void Exhaust(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	// The engine's ancillaries: spark plugs and coil, carbs or throttle bodies, air filter, oil
	// filter, cooling and the gearbox's pieces (BikeModel._draw_spark_plugs to _blender_gearbox).
	void Ancillaries(const PartsMap& Parts, const Layout& L, const Bodywork& Body, std::vector<FittedPart>& Out) const;
	// The bars, their grips and the controls on them (the front master cylinder too), and the clutch and throttle cables
	// (BikeModel._draw_handlebars, _bent_bar, _draw_controls and _draw_cables).
	// The bar as drawn, in the steering's space (BikeModel's _bar_line, _bar_tip, _bar_out, _bar_r),
	// and a braced bar's pad (code-drawn), for what goes on or near it.
	struct Bars
	{
		std::vector<V3> Line;   // the middle out to the right end
		V3 Tip, Out, Pad;
		double R = 0.012;
		bool HasPad = false;
		V3 End(double Side) const { return V3(Tip.x * Side, Tip.y, Tip.z); }
		V3 OutAt(double Side) const { return V3(Out.x * Side, Out.y, Out.z); }
	};
	Bars Handlebars(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out) const;
	// The headlight's Blender-made body on the steering (H: the head, Godot frame).
	void Headlight(const PartsMap& Parts, std::vector<FittedPart>& Out, const V3& H) const;
	// The rear brake's master cylinder (BikeModel._draw_rear_master).
	void RearMaster(const Layout& L, std::vector<FittedPart>& Out) const;
	// The clocks (BikeModel._draw_dashboard), raised clear of the bars as _lift_clear_of_bars
	// raises them (that needs the mesh source).
	void Dashboard(const PartsMap& Parts, const Layout& L, const Bars& Bar, std::vector<FittedPart>& Out) const;
	// BlenderParts.sweep_section: copies of a Blender section (drawn at radius 1 along +Z, Godot
	// frame) laid end to end along ShapeKit.smooth's curve through Route, scaled by Radii (one, or
	// one per route point). False when there's no glb.
	bool Sweep(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Path, const std::vector<V3>& Route,
		const std::vector<double>& Radii, const std::string& Node) const;
	// BikeModel._can: a silencer of Shape from A to B, R its biggest radius (Godot frame).
	void Can(std::vector<FittedPart>& Out, const V3& A, const V3& B, const std::string& Shape, double R) const;
	// BikeModel's _swingarm node at rest (Godot frame), and its reach pivot to axle.
	GAffine SwingarmAt(const Layout& L) const;
	double ArmLength(const Layout& L) const;
	std::string ArmType(const PartsMap& Parts) const;
};
} // namespace TorqueFit
