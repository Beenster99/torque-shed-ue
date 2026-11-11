// A ride on the real world's bare ground (Ride UK stage 2): the way by road between two places,
// its road given a smooth height of its own, the ground with the road's bench cut into it, and both
// made into meshes a piece at a time, so the game can lay out only what is round the riders.
//
// Everything is in the area's frame, as the bike's sim wants it: metres, x east, y up, z south.
// Plain C++ and the same on every machine: two riders who plan the same ride get the same road to
// the last bit, since it is made from nothing but the cells and the height tiles.
//
// The road here is a first, plain one: a strip of one width along the route's line, its height the
// ground's along its middle, smoothed. Torque Shed's own road builder (cambers, junctions, widths by
// class, bridges) is stage 3's port.
#pragma once

#include "WorldLoad.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
struct RoadPoint
{
	P2 At;
	double Along = 0.0;    // metres from the start
	double Height = 0.0;   // the road's own
};

// A mesh in the ride's frame: positions about (OriginX, 0, OriginZ), so a float holds them finely.
struct RideMesh
{
	double OriginX = 0.0, OriginZ = 0.0;
	std::vector<float> Pos, Nrm;   // x, y, z each
	std::vector<float> Uv;         // u, v each: metres across the world over 8
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
	std::vector<RoadPoint> Road;        // every 4 m or closer
	std::vector<std::string> Names;     // the roads taken, each once, in order
	double Length = 0.0;
	double LatA = 0.0, LonA = 0.0, LatB = 0.0, LonB = 0.0;
	int TilesFailed = 0;

	static constexpr double HalfWidth = 2.75;   // the road's
	static constexpr double Bench = 4.0;        // level ground kept each side of it
	static constexpr double Blend = 12.0;       // and the bank from there back to the land as it lies
	static constexpr double Sink = 0.12;        // how far under the road's surface the ground under it lies

	// The bare ground at (X, Z), the road's bench cut in; NaN where the world has none.
	double Ground(double X, double Z) const;
	// The road's surface at (X, Z) if it is on the road, else NaN.
	double RoadHeight(double X, double Z) const;
	// The nearest point of the road's middle within 40 m: how far, its height, which point. False if none.
	bool Nearest(double X, double Z, double& Dist, double& Height, int* Index = nullptr) const;

	// A square of ground, Cells by Cells, from (X0, Z0), with a skirt hanging from its edge (so a finer
	// piece beside a coarser one shows no crack).
	void LandTile(double X0, double Z0, double Size, int Cells, RideMesh& Out) const;
	// The road's strip from point First to Last (both included).
	void RoadPiece(int First, int Last, RideMesh& Out) const;

	// The route as the bike reads it (Data/Buttertubs/route.json's fields): line_sim, length_m,
	// road_half_width_m, origin_geo, spawn, credits.
	std::string RouteJson() const;

private:
	void LayRoad(const std::vector<P2>& Line);
	std::unordered_map<long long, std::vector<int>> Near;   // road points by 16 m square
};
} // namespace TorqueWorld
