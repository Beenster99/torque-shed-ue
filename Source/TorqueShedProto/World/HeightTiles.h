// The world's ground heights as web-mercator map tiles (Ride UK stage 1): a port of the Godot
// game's scripts/height_tiles.gd. Terrarium tiles (height = R * 256 + G + B / 256 - 32768 metres)
// from Mapterhorn (512 px lossless WebP), else AWS's Terrain Tiles (256 px PNG) at zoom AwsTop and
// coarser only: finer than that, where Mapterhorn has no tile there is none (a ".none" file says
// so) and the coarser zoom is the ground (WorldTerrain eases into it).
//
// Tiles are kept on disk under Disk (and read from any folder in Dirs: the game's own, the tests'
// fixtures) and decoded into memory the first time they are sampled. This class never touches the
// network: the game fetches a tile's bytes and hands them to Store. Safe from any thread.
#pragma once

#include "WorldGeo.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct HeightTile
{
	int Size = 512;
	std::vector<uint8_t> Rgb;   // Size x Size x 3
	std::vector<uint8_t> Sea;   // Size x Size smoothed sea mask (0 to 255); empty when the tile has no sea

	// The ground height (m) at pixel (I, J), the sea given its floor.
	double At(int I, int J) const;
	// Whether pixel (I, J) is open sea.
	bool IsSea(int I, int J) const;
};

class HeightStore
{
public:
	// The finest zoom AWS's tiles are used at (finer, they are the 30 m survey drawn larger, with
	// flat rectangles standing out of the hills).
	static constexpr int AwsTop = 12;
	// A tile Mapterhorn had none of is asked for again once its .none file is this old (days).
	static constexpr int NoneDays = 30;
	// Decoded tiles kept in memory (a 512 px tile is 768 KB).
	static constexpr int Memory = 160;
	// Ground at or under this (m) is sea; a tile with fewer sea pixels than SeaMin (of 512 x 512)
	// has a pond, not sea; the sea's floor slopes from the shore down to SeaDepth.
	static constexpr double SeaAbs = 0.4;
	static constexpr int SeaMin = 400;
	static constexpr double SeaDepth = 35.0;
	// How far under the pixels round it (m) a pixel is a pit in the source, not the ground.
	static constexpr double SpikeM = 40.0;
	static constexpr int SeaCells = 16;

	// Where fetched tiles are kept, and the folders searched after it.
	std::string Disk;
	std::vector<std::string> Dirs;

	// A decoded tile, read from disk the first time; null when there is none on disk.
	std::shared_ptr<const HeightTile> Tile(const TileKey& Key);
	// Whether the tile is on disk or in memory, or known to be none (without decoding it).
	bool Has(const TileKey& Key);
	// Whether there is known to be no such tile: Has is true of it, but Tile is null.
	bool IsNone(const TileKey& Key);
	// Notes on disk that Mapterhorn has no such tile.
	void MarkNone(const TileKey& Key);
	// Keeps a fetched tile's bytes on disk (FromAws: a PNG); false when they are no picture of a tile.
	bool Store(const TileKey& Key, const std::string& Bytes, bool FromAws);
	// A tile from its file's bytes; null when they are no good. With Despike the source's pits are
	// filled. Key places it, for which cells have sea (null: unplaced, sea wherever the ground is).
	std::shared_ptr<HeightTile> Decode(const std::string& Bytes, const TileKey* Key, bool Despike) const;

	// GETs a URL: the HTTP code (0 for no answer) and the body. Blocks; called from several threads at once.
	using Fetcher = std::function<int(const std::string& Url, std::string& Body)>;
	// Fetches those of Keys that aren't on disk yet, AtOnce at a time, and waits for them: Mapterhorn's,
	// else AWS's at AwsTop and coarser; finer, a tile Mapterhorn has none of is marked none. Returns how
	// many it couldn't get. Blocks: a worker's job. Progress(done, total) is called as they come.
	int Fetch(const std::vector<TileKey>& Keys, const Fetcher& Get, int AtOnce = 6, const std::function<void(int, int)>& Progress = nullptr);

	// Says whether a cell has sea, from its place profile. Tiles over it read before are read again.
	void KnowSea(CellId Cell, bool HasSea);
	// 1 yes, 0 no, -1 not known yet (taken as yes).
	int SeaIn(CellId Cell) const;

	// The tiles at a zoom within Reach metres of any of Points (frame x, z).
	static std::vector<TileKey> KeysNear(const std::vector<P2>& Points, double Reach, int Zoom, const Frame& F);
	// Where a tile is fetched from.
	static std::string Url(const TileKey& Key, bool FromAws);
	// Forgets every decoded tile, what was missing and which cells have sea.
	void Reset();

	static void FillPits(HeightTile& T);

private:
	std::string PathOf(const TileKey& Key, bool* InDisk) const;
	std::string NonePath(const TileKey& Key, bool* InDisk) const;
	std::vector<uint8_t> SeaMask(const HeightTile& T, const TileKey* Key) const;
	std::vector<uint8_t> SeaAllowed(int Size, const TileKey& Key) const;

	struct Held
	{
		std::shared_ptr<const HeightTile> Tile;
		long long Used = 0;
	};
	mutable std::mutex Lock;
	std::map<TileKey, Held> Mem;
	std::set<TileKey> Missing;
	std::map<TileKey, bool> None;
	std::map<CellId, bool> SeaCellsKnown;
	long long Tick = 0;
};
} // namespace TorqueWorld
