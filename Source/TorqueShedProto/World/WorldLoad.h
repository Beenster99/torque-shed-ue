// Putting a part of the world together to ride (Ride UK stage 1): the cells an area covers fetched
// if the game hasn't them, stitched into one road graph, with their bridges and tunnels, streams,
// scenery, cover and trees, and the ground over them. What the Godot game's RealRegion._load does
// for an area of cells. Blocks while it fetches: a worker's job.
#pragma once

#include "CellStore.h"
#include "HeightTiles.h"
#include "WorldRegion.h"
#include "WorldTerrain.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct LoadedArea
{
	Region Roads;
	std::shared_ptr<Terrain> Land;      // the map's own view; a ride takes Land->ForRoad(its road)
	std::string FeaturesHash;           // of the scenery read ("" for none): never part of the roads' hash
	std::vector<CellId> Cells;          // those it was made of
};

// The area of Cells in a frame at (Lat0, Lon0). False, with why, when none of it can be had.
bool LoadArea(CellStore& Store, HeightStore& Heights, const std::vector<CellId>& Cells, double Lat0, double Lon0, const std::string& Title, LoadedArea& Out,
	std::string* Why = nullptr);
// The same, with its frame at the middle of the first cell (a journey's own).
bool LoadCells(CellStore& Store, HeightStore& Heights, const std::vector<CellId>& Cells, LoadedArea& Out, std::string* Why = nullptr);
// Fetches the ground a ride needs before it starts: first the place profiles of the cells under its
// fine and middle tiles (so the sea is known before a tile is read), then every tile it uses that
// isn't on disk. Returns how many tiles it couldn't get. Blocks: a worker's job.
int FetchGround(CellStore& Store, HeightStore& Heights, const Terrain& Ride, const std::function<void(int, int)>& Progress = nullptr);
// A road's points no further apart than Step metres (a ride's road, for Terrain::ForRoad), with how
// far along the road each is.
std::vector<P2> Resample(const std::vector<P2>& Line, double Step, std::vector<double>* Along = nullptr);
// A ride checked from end to end without a bike (the stage 1 gate): the cells between two places
// fetched and stitched, the shortest way by road between them, its ground fetched, and the road's
// height read all the way along.
struct RideCheck
{
	bool Ok = false;
	std::string Why;                    // when not Ok
	int Cells = 0, Junctions = 0, Roads = 0;
	int From = -1, To = -1;             // the junctions nearest each end
	double Km = 0.0, CrowKm = 0.0;
	int Points = 0, WithGround = 0;     // along the road every 30 m, and those with ground under them
	double Low = 0.0, High = 0.0;       // the road's ground, metres above the sea
	double Climb = 0.0;                 // every rise along it added up, metres
	double Steepest = 0.0;              // the steepest 30 m of it, as a fraction (0.25: 1 in 4)
	int Tiles = 0, TilesFailed = 0;
	std::vector<std::string> Names;     // the roads taken, each once, in order
	std::vector<std::string> Credits;
	std::string Dump() const;           // as JSON
};
// Blocks (a worker's job); Say hears each step as it starts. A place may be up to 2 km from a road.
RideCheck CheckRide(CellStore& Store, HeightStore& Heights, double LatA, double LonA, double LatB, double LonB,
	const std::function<void(const std::string&)>& Say = nullptr);
// The region id of an area of listed cells: "area:@ROW_COL,ROW_COL".
std::string CellsAreaId(const std::vector<CellId>& Cells);
} // namespace TorqueWorld
