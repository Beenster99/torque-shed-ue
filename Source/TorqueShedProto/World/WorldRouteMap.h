// The Route planner's map (Ride UK stage 4), in plain C++ so SimTest can try it: a web-mercator view
// of the world at any zoom, and the land under it drawn as a relief map (hills shaded from the
// north-west, tinted by height, the sea blue) from the same height tiles the rides are built from.
#pragma once

#include "WorldGeo.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace TorqueWorld
{
class HeightStore;

namespace Route
{
struct MapView
{
	double Lat = 54.6, Lon = -3.4;   // the middle
	double Zoom = 5.6;               // as web maps have it: the world is 256 x 2^Zoom pixels across
	int Layer = 0;                   // [build 147] how it is drawn: 0 relief, 1 road map, 2 terrain, 3 satellite, 4 satellite with roads (ELayer)
	int W = 1024, H = 1024;          // the picture's pixels
	// [map detail] In as far as a street map goes (about 2 m a pixel), so every lane can be seen and pinned
	// (beenster, Nov 20: "more of a google maps experience where when we zoom in more details appear").
	static constexpr double MinZoom = 4.5, MaxZoom = 16.5;

	// Where a place is on the picture (pixels from its top left), and the place at a pixel.
	P2 PixelOf(double Lat, double Lon) const;
	Geo PlaceAt(double X, double Y) const;
	// Ground metres a pixel at the middle.
	double MetresPerPixel() const;
	// The view moved by pixels, and zoomed by Steps (2x a step) about a pixel, which stays put.
	MapView Panned(double DX, double DY) const;
	MapView Zoomed(double Steps, double AtX, double AtY) const;
	// The zoom of height tile (512 px) that matches the picture's pixels, and the tiles it needs.
	int TileZoom() const;
	std::vector<TileKey> Tiles() const;
};

// [planner map] SatNav: the sat nav's own map (Dash/TftPlannerMap), the road map's look zoomed out past it.
enum ELayer { Relief = 0, RoadMap = 1, TerrainMap = 2, Satellite = 3, Hybrid = 4, SatNav = 5, LayerCount = 6 };
const char* LayerName(int Layer);
// Whether a layer is drawn from the satellite's pictures; whether it draws roads.
inline bool PhotoLayer(int Layer) { return Layer == Satellite || Layer == Hybrid; }
inline bool RoadLayer(int Layer) { return Layer == RoadMap || Layer == Hybrid || Layer == SatNav; }

// [build 147] The main roads the map draws on its road map: each 5 degree block's sketch's motorways,
// trunk roads and A roads (sketch_bake.py), read once. Safe from any thread.
struct MapRoads
{
	struct Line
	{
		int Class = 2;   // 0 motorway, 1 trunk, 2 primary (an A road)
		std::vector<std::pair<double, double>> Pts;   // (lat, lon)
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
	};
	std::mutex Lock;
	std::vector<Line> Lines;
	std::set<std::string> Blocks;   // read (or found missing)
	// Whether Block's roads are still to read: true once, for the one who then reads them.
	bool Claim(const std::string& Block);
	void Add(std::vector<Line>&& More);
};

// [map detail] Every road of the world's cells (the same 0.25 degree cells the rides are built from: each
// one's roads.json), for the road map zoomed in: B roads, lanes and tracks come in as the map zooms, as a
// web map's do. Kept a cell at a time, the most recently drawn Memory of them. Safe from any thread.
struct MapStreets
{
	// What a road is drawn as, the biggest first.
	enum EKind { Motorway = 0, Trunk, Primary, Secondary, Tertiary, Minor, Track, KindCount };
	static int KindOf(const std::string& Class);
	// The zoom a kind is drawn from (Motorway to Primary: always).
	static double ShownFrom(int Kind);
	// Cells are read (and those not on disk fetched) for a view from this zoom, when it covers no more than
	// MostCells (about 1400 x 900 pixels at zoom 10.5).
	static constexpr double From = 10.5;
	static constexpr int MostCells = 32, Memory = 64;
	struct Line
	{
		int Kind = Minor;
		int Name = 0;   // into its cell's Names (0: none)
		std::vector<std::pair<float, float>> Pts;   // (lat, lon)
		float South = 0.0f, West = 0.0f, North = 0.0f, East = 0.0f;
	};
	struct Cell
	{
		std::vector<Line> Lines;
		std::vector<std::string> Names;
		// [map water] Its water (rivers' banks, lakes, docks: the cell's ground file) and its rivers, canals and
		// streams as lines (Kind 0 river, 1 stream, 2 canal), all (lat, lon) (beenster, Nov 21: "why is there
		// no River Thames in London").
		std::vector<std::vector<std::pair<float, float>>> Water;
		struct Flow
		{
			int Kind = 0;
			std::vector<std::pair<float, float>> Pts;
		};
		std::vector<Flow> Flows;
		static bool IsWater(const std::string& Kind, const std::string& Word);
	};
	// The roads' names, from this zoom: where each named road's longest stretch in view is, its middle,
	// at most Most, the biggest roads (then the longest stretches) first.
	static constexpr double NamesFrom = 13.5;
	struct Label
	{
		double Lat = 0.0, Lon = 0.0;
		std::string Name;
		int Kind = Minor;
	};
	std::vector<Label> Labels(const MapView& View, size_t Most = 120);
	// The cells a view draws streets from (empty when zoomed too far out), the middle's first.
	static std::vector<CellId> CellsFor(const MapView& View);
	// A cell's roads from its folder's roads.json and region.json (its frame); null if they won't read.
	static std::shared_ptr<Cell> Read(const std::string& Dir);
	bool Has(CellId C);
	void Put(CellId C, std::shared_ptr<const Cell> Got);
	// Those of Cells held (and so drawn), noted as just used.
	std::map<CellId, std::shared_ptr<const Cell>> Held(const std::vector<CellId>& Cells);

private:
	std::mutex Lock;
	std::map<CellId, std::pair<std::shared_ptr<const Cell>, long long>> Mem;
	long long Tick = 0;
};

// [build 147] The satellite layer's pictures: EOX's Sentinel-2 cloudless (s2maps.eu; CC BY-NC-SA 4.0:
// free for a game that isn't sold, beenster Nov 14), 256 px JPEG web-map tiles, kept on disk under Disk
// and decoded by Decode (the game's: plain C++ has no JPEG). Safe from any thread.
struct MapImagery
{
	static constexpr int Top = 14;   // the finest zoom used (about 10 m a pixel: the survey's own)
	static constexpr int Memory = 192;
	static const char* Credit();
	std::string Disk;
	std::function<bool(const std::string& Bytes, int& W, int& H, std::vector<uint8_t>& Rgb)> Decode;
	struct Tile
	{
		int Size = 256;
		std::vector<uint8_t> Rgb;
	};
	static std::string Url(const TileKey& Key);
	// The zoom of picture tile a view samples, and the tiles it needs.
	static int ZoomFor(const MapView& View);
	static std::vector<TileKey> Keys(const MapView& View);
	bool OnDisk(const TileKey& Key) const;
	// The decoded tile from memory or disk; null when it isn't on disk (or won't decode).
	std::shared_ptr<const Tile> Get(const TileKey& Key);
	// Fetches those of Keys not on disk, AtOnce at a time; how many it couldn't get. Blocks.
	int Fetch(const std::vector<TileKey>& Keys, const std::function<int(const std::string& Url, std::string& Body)>& Getter, int AtOnce = 6);

private:
	std::string PathOf(const TileKey& Key) const;
	std::mutex Lock;
	std::map<TileKey, std::pair<std::shared_ptr<const Tile>, long long>> Mem;
	long long Tick = 0;
};

// The map under a view in its layer, W x H pixels, B G R A each (as a texture takes them). Where no
// tile is on disk yet the land is a dark grey. Roads (null: none) are drawn on the road map, and zoomed in,
// Streets' (null: none) on the road map and over the satellite's pictures. Returns the share of pixels it
// had ground for.
// [planner map] The ferry crossings' dashed lines over a picture drawn (DrawRelief draws them on every layer).
void DrawFerryLines(const MapView& View, std::vector<uint8_t>& Bgra);
double DrawRelief(HeightStore& Heights, const MapView& View, std::vector<uint8_t>& Bgra, MapRoads* Roads = nullptr, MapImagery* Imagery = nullptr,
	MapStreets* Streets = nullptr);
} // namespace Route
} // namespace TorqueWorld
