// [level crossings] How each of Britain's level crossings is laid out (beenster, Nov 20 2026: "Please build a 3D
// blender model for these level crossing signs, which flash when there is an imminent train"; the trains plan's
// stage 4). Plain C++ (SimTest's lx_test dumps every type's layout for Tools/props/render_lx_sheet.py, so the
// cloud's preview renders are the game's own layout).
//
// The kit is Tools/props/make_level_crossing.py's (Data/Props/Blender/lx_*.glb); World/TorqueLevelCrossing.cpp
// stands a crossing up from this list near the rider and runs its lights, barriers and gates.
//
// The types, as the road decides (TypeFor):
//   Passive      an open crossing with no lights: a saltire, Give Way and the steam locomotive plate (lanes, tracks)
//   Lights       an open crossing with lights (AOCL): the saltire over each light unit, the DANGER board
//   HalfBarrier  automatic half barriers (AHB): a boom across each approach's own lane, picket fencing
//   FullBarrier  full barriers (MCB): booms with hanging skirts across the whole road both sides, palisade fencing,
//                a floodlight column
//   Gated        gates the crossing swings shut across the road, a miniature stop light, STOP on the road
#pragma once

#include "WorldGeo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TorqueWorld
{
namespace LevelX
{
enum EType { Passive = 0, Lights, HalfBarrier, FullBarrier, Gated, Types };
const char* TypeName(int Type);

// What a piece is to the game: Fixed stands still; Unit is a road light unit (its three lit faces lx_lit_*, in its
// own frame); Boom a barrier boom (on its machine's pivot, Length long, Skirted with a hanging skirt, Offside one
// across the far lane that the traffic leaving meets); Gate a gate leaf (on its hinge: Front is the way it lies
// open, Shut the way it lies across the road); Msl the gated crossing's miniature stop light (its lit faces);
// Fence a run of fence panels (one instance each); Contact the blue plate (its row in the printed atlas: Row);
// Paint the road's paint (a decal Length along the road by Width across, its image's top towards the railway; Row
// its row in lx_road_paint.png).
enum ERole { Fixed = 0, Unit, Boom, Gate, Msl, Fence, Contact, Paint };

struct Piece
{
	std::string Mesh;   // the kit's id (lx_post, ...)
	int Role = Fixed;
	int Approach = 0;   // which side of the railway (0 the side the road's direction comes from, 1 the other)
	P2 At;              // where (the ride's frame, m)
	double Up = 0.0;    // how far up from the ground there (a light unit's middle up its post, ...)
	bool OnRoad = false;// stands on the road's surface (the paint) rather than the ground
	P2 Front;           // which way its front (the model's +z) faces
	P2 Shut;            // a gate's way across the road when shut
	double Length = 0.0, Width = 0.0;
	bool Skirted = false, Offside = false;
	int Row = 0;
};

// Where a crossing is: the middle of the road's crossing of the line, the road's way across (unit), the railway's
// way (unit), the road's half width there.
struct Spot
{
	P2 Mid, Road, Rail;
	double Half = 3.0;
	uint32_t Key = 0;
};

// The road's distance from the middle to where each approach's barrier, stop line and lights stand: CLEAR metres
// square from the nearest rail's middle at the road's near edge, more as the crossing is skewed.
constexpr double Clear = 4.0;
double StandOff(const Spot& S);
// The type for a road of this class (the map's highway=, as RoadGraph's Classes) and half width.
int TypeFor(const std::string& RoadClass, double Half, uint32_t Key);
// Every piece of a crossing of this type at S.
std::vector<Piece> Layout(int Type, const Spot& S);
// A boom's or gate's length for its piece, and the kit's nearest model to it (lx_boom_3 .. lx_boom_9, lx_gate_25 ..
// lx_gate_45), which the game stretches to the exact length.
std::string BoomMesh(double Length);
std::string GateMesh(double Length);
double MeshLength(const std::string& Mesh);
// A road's light unit's lamps relative to its origin (m: across to the right as the traffic sees it, up), as the
// kit's LAMPS; the fence panel's length.
constexpr double PanelLength = 2.0;
// A boom lamp's places along a boom of this length (from its pivot), as the kit's boom_lamps.
void BoomLamps(double Length, double Out[3]);
} // namespace LevelX
} // namespace TorqueWorld
