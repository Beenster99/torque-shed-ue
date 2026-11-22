// [planner map] The sat nav's map as the route planner's (beenster, Nov 21: "can we have the satnav map as
// the planner map?"): its land, water, woods, parks, buildings and roads in the colours of a phone's map
// app, drawn for the planner's view from the world's cells (each read once, as a ride's area is, and kept),
// on the relief's sea. Zoomed out past From, the planner draws its road map instead.
//
// Plain C++ (SimTest/world_ride draws it); safe from any thread.
#pragma once

#include "Dash/TftMap.h"
#include "World/WorldGeo.h"
#include "World/WorldRouteMap.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace TorqueWorld { class CellStore; }

namespace TorqueTft
{
class PlannerMap
{
public:
	// From this zoom (about 45 m a pixel), when the view covers no more than MostCells.
	static constexpr double From = 11.0;
	static constexpr int MostCells = 12, Memory = 20;
	static std::vector<TorqueWorld::CellId> CellsFor(const TorqueWorld::Route::MapView& View);
	// Up to Load of the view's cells not held yet read (from disk, else fetched), the middle's first, until
	// Stop says the view has moved on. Returns how many of its cells with roads aren't held; Loaded, how many
	// it read. Blocks: a worker's job.
	int Know(TorqueWorld::CellStore& Store, const TorqueWorld::Route::MapView& View, int Load, const std::function<bool()>& Stop = nullptr, int* Loaded = nullptr);
	// The view drawn over Bgra (W x H, B G R A, as DrawRelief made it for the road map, unroaded: its sea is
	// kept, its land painted over). False, and Bgra left alone, unless every cell of the view with roads is held.
	bool Draw(const TorqueWorld::Route::MapView& View, std::vector<uint8_t>& Bgra, bool Night = false);

private:
	struct Cell
	{
		MapData Data;            // in the cell's own frame
		double Lat0 = 0.0, Lon0 = 0.0;
	};
	std::mutex Lock;
	std::map<TorqueWorld::CellId, std::pair<std::shared_ptr<const Cell>, long long>> Mem;
	long long Tick = 0;
	static std::shared_ptr<const Cell> Read(TorqueWorld::CellStore& Store, TorqueWorld::CellId C, const std::string& Dir);
};

// A Paint's triangles (not its text) laid over a picture (W x H, B G R A), their edges smoothed.
void Rasterize(const Paint& P, std::vector<uint8_t>& Bgra, int W, int H);
} // namespace TorqueTft
