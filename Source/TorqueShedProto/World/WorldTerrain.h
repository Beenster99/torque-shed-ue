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

#include "FitJson.h"

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
	// The cells under the ride's fine and middle tiles: those whose place profiles say where the sea is.
	std::vector<CellId> CellsUnderRide() const;

	// What covers the ground. The ids are the baker's: grass, forest, crop, shrub, barren, urban,
	// sand, wetland, water (rivers and lakes; the sea is in the heights), rock.
	static constexpr int CoverWater = 8;
	static const char* CoverName(int Id);
	// Reads cover from a cell's folder, wherever its box is (its region.json: frame, geo box, levels).
	void AddCoverCell(const std::string& CellDir, const TorqueFit::Json& Meta);
	// What covers the ground at (X, Z): the sample nearest, from the finest level that has it, or -1.
	int Cover(double X, double Z) const;
	// Takes in a cell's trees ({cell: "ROW_COL", rows, cols, trees, leaf}, the last two base64 of
	// zlib'd bytes); false for one that won't read.
	bool AddTrees(const TorqueFit::Json& Row);
	bool HasTrees() const;
	// The trees at (X, Z): the canopy's height in TreeHeightStep in its low 5 bits, how much of the
	// square is under trees in eighths in its next 3, and the leaf's id times 256 (0 unknown, 1
	// broadleaved, 2 needleleaved, 3 mixed); -1 where no cell's trees have the point.
	int TreesAt(double X, double Z) const;

private:
	struct CoverLevel
	{
		std::string Base, Folder;
		double Spacing = 16.0;
		int Cells = 128;
	};
	struct CoverCell
	{
		std::vector<CoverLevel> Levels;   // finest first
		double Lat = 0.0, Lon = 0.0;      // the cell's own frame
		MetresPerDeg M;
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
	};
	struct TreeCell
	{
		double South = 0.0, West = 0.0;
		int Rows = 0, Cols = 0;
		std::string Trees, Leaf;   // Rows x Cols bytes, from the north-west corner
	};
	// What every ride over the same land shares: the cells' cover and trees.
	struct Ground
	{
		std::vector<CoverCell> CoverCells;
		std::vector<TreeCell> TreeCells;
		std::mutex Lock;
		std::map<std::string, std::shared_ptr<const std::string>> CoverTiles;   // by path; an empty one where there is no file
	};
	std::shared_ptr<const std::string> CoverTile(const CoverLevel& Lv, int Tx, int Tz) const;
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
	std::shared_ptr<Ground> Land;
	mutable std::mutex Lock;
	mutable std::unordered_map<int, double> PadLevel;   // building -> its ground, once worked out
};
} // namespace TorqueWorld
