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
// The region id of an area of listed cells: "area:@ROW_COL,ROW_COL".
std::string CellsAreaId(const std::vector<CellId>& Cells);
} // namespace TorqueWorld
