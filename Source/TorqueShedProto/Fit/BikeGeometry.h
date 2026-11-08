// Geometry mode's fit for one bike: a port of the Godot game's geometry mode (scripts/geometry_mode.gd,
// geo_gizmo.gd, BikeModel.apply_geometry and BikeRig.ADJUST), with finer control added on top.
//
// As in Torque Shed, a bike's fit is the rig's range handles (BikeLayout's LayoutAdjust: the tank's
// mounts, the pegs, the seat hoop; the layout re-solves round them) and, with Expert free move, an
// offset per slot: moved (metres), turned (Euler degrees, Godot's YXZ order) and sized along the
// part's own axes, about the part's middle, in the bike's own axes at rest (Godot frame: x right,
// y up, z back). Looks only: the physics never see it. The host sanitizes it and keeps it with the
// bike; it travels as text (ToText/FromText), so co-op shares it the same way as part fits.
//
// Godot's handles are kept as they are: W/E/R move, rotate and scale gizmos along the bike's axes,
// Ctrl snaps (1 cm, 5 degrees, 5%), undo, reset. Finer control, new here (marked "finer" below):
//   - fine snap (Ctrl+Shift: 1 mm, 0.5 degrees, 1%) and a slow drag (Shift: a tenth of the mouse),
//   - nudges by a step from keys or a pad (Nudged), and a value typed in (WithValue),
//   - moving along the part's own turned axes as well as the bike's (MovedAlong, the "Part" axes),
//   - the gaps from a moved part to the parts round it (Gaps), to fit it up close without a clash.
// Plain C++, deterministic: the same fit gives the same bike on every machine.
#pragma once

#include "BikeLayout.h"
#include "BikeParts.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
// One slot's offset (GeoGizmo's {p, r, s}), Godot frame.
struct GeoOffset
{
	V3 P;                  // metres
	V3 R;                  // Euler degrees, YXZ (Godot's Basis.from_euler)
	V3 S = V3(1, 1, 1);    // along the part's own (turned) axes
	bool IsIdentity() const;
	bool operator==(const GeoOffset& O) const;
	bool operator!=(const GeoOffset& O) const { return !(*this == O); }
};

// A bike's whole fit (Shed.bikes[id].geo): the rig's handles and each moved slot's offset.
struct BikeGeo
{
	LayoutAdjust Rig;
	std::map<std::string, GeoOffset> Slots;
	bool Empty() const;
	bool operator==(const BikeGeo& O) const;
	bool operator!=(const BikeGeo& O) const { return !(*this == O); }
	GeoOffset Of(const std::string& Slot) const;   // identity if it isn't moved
	void Set(const std::string& Slot, const GeoOffset& Off);   // an identity offset drops the slot
};

enum class GeoHandle { Move, Rotate, Scale };   // GeoGizmo.Mode
enum class GeoSnap { Off, Coarse, Fine };       // Coarse: Godot's Ctrl. Fine (finer): Ctrl+Shift.

namespace Geo
{
inline constexpr int Uniform = 3;               // the middle box in scale mode: every axis at once
inline constexpr double SnapMove = 0.01, SnapRot = 5.0, SnapScale = 0.05;
inline constexpr double FineMove = 0.001, FineRot = 0.5, FineScale = 0.01;   // (finer)
inline constexpr double SlowDrag = 0.1;         // (finer) Shift: the drag at a tenth of the mouse
inline constexpr double MaxMove = 1.5, MinScale = 0.1, MaxScale = 4.0;

// The slots a fit may move (PartsDB.SLOTS, in the catalogue's order).
const std::vector<std::string>& Slots();

// Basis.from_euler (YXZ, radians) and Basis.get_euler (YXZ) as Godot works them out.
GAffine FromEuler(const V3& Radians);
V3 EulerOf(const GAffine& B);
GAffine RotationOf(const GeoOffset& O);
// The whole offset as a transform in the bike's space (Godot frame) about Pivot, the part's middle.
GAffine OffsetXform(const GeoOffset& O, const V3& Pivot);

// Dragging a handle, as GeoGizmo does: a move arrow Amount metres along the bike's Axis (0 x, 1 y,
// 2 z), a ring Degrees about it on top of how the part's turned, a box Factor times the size along
// the part's own Axis (Uniform: all three). Only that axis changes; Snap snaps the result.
GeoOffset Moved(const GeoOffset& Start, int Axis, double Amount, GeoSnap Snap);
GeoOffset Rotated(const GeoOffset& Start, int Axis, double Degrees, GeoSnap Snap);
GeoOffset Scaled(const GeoOffset& Start, int Axis, double Factor, GeoSnap Snap);
// (finer) A move along a direction of its own (Godot frame, unit): the part's own turned axis
// Axis for the "Part" axes (PartAxis). Snap snaps the distance along it.
V3 PartAxis(const GeoOffset& O, int Axis);
GeoOffset MovedAlong(const GeoOffset& Start, const V3& Dir, double Amount, GeoSnap Snap);
// (finer) Steps nudges (each a snap step: fine or coarse) of a handle along or about Axis.
GeoOffset Nudged(const GeoOffset& Start, GeoHandle Handle, int Axis, int Steps, bool Fine, bool PartAxes = false);
// (finer) One number of the readout typed in: centimetres, degrees or percent, as it reads.
GeoOffset WithValue(const GeoOffset& Start, GeoHandle Handle, int Axis, double Value);

// A fit made safe to keep (GeoGizmo.sanitize): known slots only, moves and sizes in range, turns
// wrapped to -180..180, parts left as dropped, the rig's handles cleaned (BikeRig.clean_adjust).
BikeGeo Sanitize(const BikeGeo& In);

// The fit as it's kept and sent: JSON as Godot's dictionary ({"rig": {"tank_up": 12}, "tank":
// {"p": [x, y, z], "r": [...], "s": [...]}}), every number exact. FromText sanitizes; false if it
// isn't a fit at all (it's then empty).
std::string ToText(const BikeGeo& G);
bool FromText(const std::string& Text, BikeGeo& Out);

// The drag maths (GeoGizmo.line_param, plane_point, angle_about), any frame.
double LineParam(const V3& Origin, const V3& Dir, const V3& RayO, const V3& RayD);
bool PlanePoint(const V3& Center, const V3& Axis, const V3& RayO, const V3& RayD, V3& Hit);
double AngleAbout(const V3& A, const V3& B, const V3& Axis);

// Godot's readout: "Move    X  +1.0  Y ..." for the selected part's offset.
std::string Readout(const GeoOffset& O);
} // namespace Geo

// Drawing a bike with its fit. Each slot's middle (its pivot) is where its drawn meshes sit at
// rest, before any fit; a "tag__slot" surface counts for, and moves with, that slot (as
// BikeModel's slot meshes). Ours frame in and out, as BikeParts::Draw hands meshes back.
//
// A part riding on a node (FittedPart.Node: the steering, a hub, a spinning wheel) takes the
// offset at rest, so moved bars still steer: Bake moves its vertices at rest, which is right for
// everything but a spinning wheel's own pieces (rims, tyres, discs). Those should spin about the
// moved axle: put Xform(slot) on a component between the hub and the spin (BikeModel's
// _update_geometry carries it that way) rather than baking it.
class GeoPivots
{
public:
	void Add(const FittedPart& F, const PartMesh& Drawn);
	bool Has(const std::string& Slot) const { return Box.count(Slot) > 0; }
	V3 Pivot(const std::string& Slot) const;   // Godot frame
	// The slot's offset as a transform in ours (bike space at rest).
	GAffine Xform(const BikeGeo& G, const std::string& Slot) const;
	// A drawn part (Drawn: BikeParts::Draw of F) with the fit applied, surface by surface.
	void Bake(const BikeGeo& G, const FittedPart& F, PartMesh& Drawn) const;
	static std::string SlotOf(const FittedPart& F, const PartSurface& S);

private:
	struct Bounds { V3 Lo, Hi; };
	std::map<std::string, Bounds> Box;   // Godot frame
};

// (finer) How close a slot's drawn meshes come to each other slot's within Within: the nearest gap
// in metres, nearest first; 0 where they touch or pass into each other (as many parts already do
// at rest, as Godot draws them: a seat on the tank's tail). Meshes: what BikeParts::Draw handed
// back with the fit applied (GeoPivots::Bake), each with its part. About a tenth of a second on a
// whole bike: work it out when a drag's let go (as Godot refits the bike then), not every frame.
struct GeoGap
{
	std::string Slot;
	double Gap = 0.0;
};
std::vector<GeoGap> Gaps(const std::vector<std::pair<const FittedPart*, const PartMesh*>>& Meshes, const std::string& Slot, double Within = 0.03);
} // namespace TorqueFit
