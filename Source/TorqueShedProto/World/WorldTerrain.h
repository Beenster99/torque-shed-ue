// The real world's ground (Ride UK stage 1): a port of the live heights in the Godot game's
// scripts/real_terrain.gd. Heights in metres above sea level at any point of a frame, read from
// the map tiles (HeightStore) at three zooms that stand for three levels: fine (about 3 m a pixel,
// near the road), middle (about 22 m, the hillsides) and ridge (about 90 m, the skyline). Height
// answers from the finest level that has the point, eased into the next where that level's tiles
// end so there is no step, with the ground levelled under each building.
//
// A ride's terrain is its own view (ForRoad): the tiles of each level it uses are those within a set
// reach of its road, fixed by the road alone, so every machine that plans the same road gets the
// same heights whatever else it has on disk. Safe to ask from worker threads.
#pragma once

#include "HeightTiles.h"
#include "WorldGeo.h"
#include "WorldRegion.h"

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
class Terrain
{
public:
	static constexpr int LevelCount = 3;
	// The spacing (m) each level stands in for, how far (m) from a ride's road each level's tiles
	// are used, and over how many of a tile's pixels (of 512) a level is eased into the next.
	static constexpr double LiveSpacing[LevelCount] = {4.0, 32.0, 128.0};
	static constexpr double LiveReach[LevelCount] = {1000.0, 6000.0, 18000.0};
	static constexpr double EasePx = 48.0;
	// The buildings whose ground is levelled, filed in PadCell squares; a pad reaches PadMargin past
	// the walls and feathers into the land over PadFeather.
	static constexpr double PadCell = 64.0;
	static constexpr double PadMargin = 2.5;
	static constexpr double PadFeather = 5.0;

	Terrain(HeightStore& InStore, double Lat0, double Lon0);

	const Frame& FrameOf() const { return F; }
	// The levels' zooms, finest first.
	int Zoom(int Level) const { return Zooms[Level]; }

	// The ground's height at frame (X, Z), or NaN where no level has it.
	double Height(double X, double Z) const;
	// One level's height there (and the coarser ones' where it has none), with no easing or pads.
	double HeightAtLevel(int Level, double X, double Z) const;
	// Whether the heights over a box are final at a level: every tile of it there is in.
	bool Ready(const Rect& Area, int Level = 0) const;
	// Whether (X, Z) is open sea.
	bool SeaAt(double X, double Z) const;
	// A tile's box in the frame.
	Rect KeyBox(const TileKey& Key) const;

	// The buildings whose ground the heights level.
	void SetBuildings(const std::vector<Building>& Rows);
	// The terrain of a ride along a road (points along it, a few tens of metres apart).
	std::shared_ptr<Terrain> ForRoad(const std::vector<P2>& Road) const;
	// Every tile this ride uses, finest first (what to fetch before it starts); empty for the map's
	// own view, which uses whatever is on disk.
	std::vector<TileKey> UsedKeys() const;
	bool IsRide() const { return !Uses.empty(); }

private:
	struct PadSet
	{
		std::vector<Building> Rows;
		std::map<std::pair<int, int>, std::vector<int>> Grid;
	};
	bool UsesKey(int Level, const TileKey& Key) const;
	bool HasGround(int Level, const TileKey& Key) const { return UsesKey(Level, Key) && !Store.IsNone(Key); }
	double LiveLevel(int Level, double Lat, double Lon, bool Ease) const;
	double Bilinear(const TileKey& Key, const HeightTile& T, double Fx, double Fy) const;
	double Pixel(const TileKey& Key, const HeightTile& T, int I, int J) const;
	double EdgeWeight(int Level, const TileKey& Key, double U, double V, int Size) const;
	double OnPads(double X, double Z, double H) const;
	double PadGround(int Index) const;

	HeightStore& Store;
	Frame F;
	int Zooms[LevelCount];
	std::vector<std::set<TileKey>> Uses;   // per level; empty: any tile on disk (the map's own view)
	std::shared_ptr<const PadSet> Pads;
	mutable std::mutex Lock;
	mutable std::unordered_map<int, double> PadLevel;   // building -> its ground, once worked out
};
} // namespace TorqueWorld
