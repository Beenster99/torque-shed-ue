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
	int Layer = 0;                   // [build 147] how it is drawn: 0 relief, 1 road map, 2 terrain, 3 satellite (ELayer)
	int W = 1024, H = 1024;          // the picture's pixels
	static constexpr double MinZoom = 4.5, MaxZoom = 13.0;

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

enum ELayer { Relief = 0, RoadMap = 1, TerrainMap = 2, Satellite = 3, LayerCount = 4 };
const char* LayerName(int Layer);

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
// tile is on disk yet the land is a dark grey. Roads (null: none) are drawn on the road map. Returns
// the share of pixels it had ground for.
double DrawRelief(HeightStore& Heights, const MapView& View, std::vector<uint8_t>& Bgra, MapRoads* Roads = nullptr, MapImagery* Imagery = nullptr);
} // namespace Route
} // namespace TorqueWorld
