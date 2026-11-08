// The bodywork on any laid-out bike: the tank, the seat and the tail cowl fitted to the lines the
// layout solver hands over (Fit/BikeLayout), a port of the Godot game's RigBodywork
// (scripts/rig_bodywork.gd) and the parts of BikeModel._draw_tank, _draw_saddle and _tail_cowl that
// call it. Each Blender part (Data/Parts/Blender) carries shape keys; this works out each key's
// weight for the bike and the warp that bends the part onto its lines (PartMesh::Deformed draws it).
// Nothing here moves the layout: it only fits to it. Plain C++, deterministic (co-op).
#pragma once

#include "BikeLayout.h"
#include "PartMesh.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TorqueFit
{
// A Blender part fitted to a bike: which glb, its shape keys' weights and the warp from the glb's
// space to the bike's (ours). Colour: the part's own paint goes on the surfaces tagged ColourTag
// ("" for every surface the paint tags would take).
//
// Node: what the part moves with as the bike rides (BikeModel's nodes): "" the bike itself,
// "steer" (bars, fork tops), "front_hub" / "rear_hub" (go up and down with the suspension: fork
// sliders, calipers), "front_wheel" / "rear_wheel" (spin too: rims, tyres, discs) and "swingarm".
// The warp places it on the bike at rest. Copies: when set, the glb is drawn once with each of
// these warps instead (a tyre is its tread's section bent round the hoop 32 times).
struct FittedPart
{
	std::string Slot, Glb, ColourTag, Node;
	std::map<std::string, double> Weights;
	PartMesh::Warp Warp;
	std::vector<PartMesh::Warp> Copies;
	// Placed but not drawn, as Godot hides it (the plain headlight inside a full fairing).
	bool Hidden = false;
	// A mesh made here rather than a glb deformed (a bar fairing's shell, the code's loft given the
	// thickness and edges of the Glb section): BikeParts::Draw hands it back as it is.
	std::shared_ptr<const PartMesh> Made;
	// Made from a shape the Godot game draws in code (a single-sided arm's cast beam, an axle
	// block): Glb is only a stand-in from the same family, and Name is the Godot mesh's, if it has one.
	bool Code = false;
	std::string Name;
};

// A loft section (BikeModel's "secs"): centre, half-width, rise above and drop below it.
struct LoftSection
{
	V3 C;
	double W = 0.0, Up = 0.0, Down = 0.0;
};

struct Bodywork
{
	std::vector<FittedPart> Parts;
	std::vector<LoftSection> TankSecs, SeatSecs;
	LoftSection SeatEnd;
	double SeatZ0 = 0.0, SeatZ1 = 0.0, SeatSlope = 0.0;   // Godot frame (z back), as RigBodywork
	bool HasSeat = false;
	// The tank's base line, front and back (BikeModel._tank_a, _tank_b: its flanks' bottom edge,
	// what the airbox keeps under), Godot frame; equal when there's no tank.
	V3 TankA, TankB;
};

class BikeBody
{
public:
	explicit BikeBody(const BikeLayout& InLayout) : Lay(InLayout) {}
	Bodywork Fit(const PartsMap& Parts, const Layout& L) const;

private:
	const BikeLayout& Lay;
};
} // namespace TorqueFit
