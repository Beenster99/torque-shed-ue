// A ride on the real world's bare ground (Ride UK stage 2): the way by road between two places,
// its road given a smooth height of its own, the ground with the road's bench cut into it, and both
// made into meshes a piece at a time, so the game can lay out only what is round the riders.
//
// Everything is in the area's frame, as the bike's sim wants it: metres, x east, y up, z south.
// Plain C++ and the same on every machine: two riders who plan the same ride get the same road to
// the last bit, since it is made from nothing but the cells and the height tiles.
//
// The road is made as the Godot game's route planner makes one (stage 3; WorldRoadLine.h): the quick
// way by road (main roads cost less a metre), its line with every mapped corner rounded and faired
// by the road's class so no bend is tighter than a bike can take, as wide as its class, its height
// the ground's along its middle, smoothed and no steeper than 15%. It has sloping shoulders that
// run down into the ground, edge lines on the wider roads and a dashed centre line where there are
// two lanes.
//
// The other roads near the ride are there too (Sides): every road within a few hundred metres of
// it, each from junction to junction, smoothed and as wide as its class, its height the ground's
// but the ride's own where it meets the ride's road, so a side road runs off the tarmac at a
// junction. They are plain where they meet (no kerb radii or give-way lines yet).
//
// Where the ride's road crosses one of the map's bridges (the region's spans, matched as the Godot
// game matches them) it runs straight from one end to the other as a deck with parapets, and the
// land under it is left as it lies. A tunnel's road is levelled the same way but is still an open
// cutting. The road is flat across, as the Godot game's is: no camber.
#pragma once

#include "WorldLoad.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TorqueWorld
{
struct RoadPoint
{
	P2 At;
	double Along = 0.0;    // metres from the start
	double Height = 0.0;   // the road's own
	double Half = 2.3;     // half the carriageway's width here
	bool Bridge = false;   // on a bridge: the road is a deck over the land as it lies
};

// A mesh in the ride's frame: positions about (OriginX, 0, OriginZ), so a float holds them finely.
struct RideMesh
{
	double OriginX = 0.0, OriginZ = 0.0;
	std::vector<float> Pos, Nrm;   // x, y, z each
	std::vector<float> Uv;         // u, v each: metres across the world over 8
	std::vector<float> Col;        // r, g, b each (linear), or empty for a mesh of one colour
	std::vector<uint32_t> Idx;     // triangles, wound to face up seen from above with x right and z towards you
	size_t Vertices() const { return Pos.size() / 3; }
};

class RidePlan
{
public:
	// Plans the ride: cells fetched and stitched, the way found, the ground fetched, the road laid.
	// Blocks (a worker's job); Say hears each step. Never null: Ok says whether it can be ridden.
	static std::shared_ptr<RidePlan> Make(CellStore& Store, HeightStore& Heights, double LatA, double LonA, double LatB, double LonB,
		const std::function<void(const std::string&)>& Say = nullptr);

	bool Ok = false;
	std::string Why;
	std::string Title;                  // what the ride is called on the screen ("" for the roads' own names)
	LoadedArea Area;
	std::shared_ptr<Terrain> Land;      // the ride's own view of the ground
	std::vector<RoadPoint> Road;        // the ride's own road, a point every 4 m or closer
	struct SideRoad
	{
		std::vector<RoadPoint> Pts;
		double Length = 0.0;
	};
	std::vector<SideRoad> Sides;        // the other roads near it, each from junction to junction
	// Every road: 0 is the ride's own, then the sides.
	int RoadCount() const { return 1 + int(Sides.size()); }
	const std::vector<RoadPoint>& RoadOf(int Which) const { return Which <= 0 ? Road : Sides[size_t(Which) - 1].Pts; }
	static constexpr double SideReach = 300.0;  // roads this near the ride's (about) are drawn
	std::vector<std::string> Names;     // the roads taken, each once, in order
	double Length = 0.0;
	double LatA = 0.0, LonA = 0.0, LatB = 0.0, LonB = 0.0;
	int TilesFailed = 0;

	static constexpr double Bench = 4.0;        // level ground kept each side of the road
	static constexpr double Blend = 12.0;       // and the bank from there back to the land as it lies
	static constexpr double Sink = 0.05;        // how far under the road's surface the ground under it lies
	static constexpr double Shoulder = 1.3;     // the road's sloping edge: this far out,
	static constexpr double ShoulderDrop = 0.26;   // and this far down, into the ground
	double WidestHalf = 2.3;                    // of the whole ride
	int Bridges = 0, Tunnels = 0;               // those the ride's road crosses
	static constexpr double BridgeReach = 10.0; // a span's ends are matched to the road within this

	// The bare ground at (X, Z), the road's bench cut in; NaN where the world has none. Coarse is how
	// far apart (m) the asker's points are: a mesh coarser than 4 m has the bench wider and a little
	// deeper, so its straight edges between points never stand over the road.
	double Ground(double X, double Z, double Coarse = 0.0) const;
	// The road's surface at (X, Z) if it is on the road, else NaN.
	double RoadHeight(double X, double Z) const;
	// The road nearest (X, Z), any of them, within 40 m of its middle (nearest by how far outside its
	// edge the point is): how far from its middle, its height, which point, its half width, which
	// road. MainOnly looks at the ride's own road alone. False if none.
	bool Nearest(double X, double Z, double& Dist, double& Height, int* Index = nullptr, double* Half = nullptr, int* Which = nullptr, bool MainOnly = false, bool* OnBridge = nullptr) const;

	// A square of ground, Cells by Cells, from (X0, Z0), with a skirt hanging from its edge (so a finer
	// piece beside a coarser one shows no crack).
	void LandTile(double X0, double Z0, double Size, int Cells, RideMesh& Out) const;
	// Road Which from point First to Last (both included): its surface, its shoulders and its paint.
	void RoadPiece(int Which, int First, int Last, RideMesh& Out) const;

	// The route as the bike reads it (Data/Buttertubs/route.json's fields): line_sim, length_m,
	// road_half_width_m, origin_geo, spawn, credits.
	std::string RouteJson() const;

private:
	void LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves);
	// The region's spans of Kind (0 a bridge, 1 a tunnel) the line passes along: first knot, last knot, ...
	std::vector<int> SpansAlong(const std::vector<P2>& Line, int Kind) const;
	void LaySides(const std::vector<int>& Path);
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Near;   // (road, point) by 16 m square
};
} // namespace TorqueWorld
