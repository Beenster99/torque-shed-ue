// The world's cells the game doesn't have yet (Ride UK stage 1): a port of the Godot game's
// scripts/cell_store.gd and region_store.gd. The world is baked and served as 0.25 degree cells by
// Beenster99/torque-shed-world, whose index.json names each cell's hash, size and download (a zip),
// or that it has no roads, and the hash of each cell's streams, trees, features and place profile
// (small files of their own beside the index). A fetched cell is checked against its hash and kept
// under Dir, where WorldRegion finds it; the index is kept there too, so a crew offline still has
// what it fetched before.
//
// This is plain C++ and never touches the network itself: the game gives it a Fetcher (a blocking
// GET, run on a worker), and the tests give it one that reads their own files. Safe from any thread.
#pragma once

#include "FitJson.h"
#include "WorldGeo.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
struct CellEntry
{
	bool Empty = false;   // no roads: the sea
	std::string Hash, Url;
	long long Bytes = 0;
};

// The store's index as the game reads it.
struct WorldIndex
{
	int Format = 0;
	std::string Release;
	std::unordered_map<std::string, CellEntry> Cells;   // by "ROW_COL"
	// The hash (the first 12 digits of its text's SHA-1) of each cell's own files, and each 5 degree block's sketch.
	std::unordered_map<std::string, std::string> Streams, Trees, Features, Profiles, Sketches;
	// [OSM stage 4] Each cell's ground (water shapes, land use, parks, car parks, single trees) and paths.
	std::unordered_map<std::string, std::string> Grounds, Paths;
	TorqueFit::Json Areas;   // named areas made of cells: {name: {title, frame, south, west, north, east}}

	bool Parse(const std::string& Text);
	const CellEntry* Cell(CellId C) const;
	bool Empty() const { return Cells.empty(); }
};

class CellStore
{
public:
	static constexpr const char* DefaultIndexUrl = "https://raw.githubusercontent.com/Beenster99/torque-shed-world/main/index.json";
	static constexpr const char* UserAgent = "TorqueShedUE (+https://github.com/Beenster99/torque-shed-ue)";
	// GETs a URL, following redirects: the HTTP code (0 for no answer) and the body. Blocks.
	using Fetcher = std::function<int(const std::string& Url, std::string& Body)>;
	// A cell's own files beside the index.
	enum class EKind { Streams, Trees, Features, Profiles, Sketches, Grounds, Paths };
	enum class EState { Here, Baked, Sea, Unknown };

	// Where fetched cells (and the index) are kept, and other folders cells are found in (the game's own).
	std::string Dir;
	std::vector<std::string> MoreRoots;
	std::string IndexUrl = DefaultIndexUrl;
	Fetcher Get;
	// No network: what isn't on disk stays missing.
	bool Offline = false;
	// At most this many fetched cells are kept on disk (each about 2.5 MB with its streams and trees):
	// past it, those used longest ago go, never one used this run.
	int Keep = 400;

	// Every folder a cell may be in: Dir, then MoreRoots.
	std::vector<std::string> CellRoots() const;
	// The index: fetched (at most once a quarter of an hour) and kept on disk; the copy on disk when
	// the network can't be had; empty if there is neither. Blocks while it fetches.
	std::shared_ptr<const WorldIndex> Index(bool Refresh = false);
	// The index as this game has it now, without asking the network.
	std::shared_ptr<const WorldIndex> IndexNow();
	void ForgetIndex();

	// What the game knows of a cell: here (it has it), baked (the index has it, to fetch), sea (no
	// roads), or unknown (nobody has baked it yet).
	EState StateOf(CellId C, const WorldIndex& Idx) const;
	// Those of Cells the index names with roads but not yet with a place profile: baked minutes ago,
	// their profiles still being made.
	std::vector<CellId> Finishing(const std::vector<CellId>& Cells, const WorldIndex& Idx) const;
	// Fetches those of Cells the game hasn't got and the index has baked, one by one, each checked
	// against its hash. Returns "" or why not. Blocks: a worker's job.
	std::string FetchCells(const std::vector<CellId>& Cells, const std::function<void(int Done, int Total)>& Progress = nullptr);
	// Fetches again those of Cells kept under Dir whose files aren't the ones the index names (baked again since).
	std::string RefetchStale(const std::vector<CellId>& Cells, const WorldIndex& Idx);
	// One of a cell's own files ("ROW_COL"; a sketch's block "R_C"), from the copy kept on disk if it
	// is the one the index names, else fetched (and kept). False when the index has none for it, or
	// it can't be had. Blocks while it fetches.
	bool Kept(EKind Kind, const std::string& Name, const WorldIndex& Idx, TorqueFit::Json& Out);

	// Notes cells used now (fetched, or stitched into a region): kept the longest, never pruned this run.
	void Touch(const std::vector<CellId>& Cells);
	// Deletes the kept cells past Keep, those used longest ago first. Returns how many went.
	int Prune();

	// The hash of a cell's folder, as the baker makes it: SHA-256 over every file but region.json and
	// features.json, folders and files in name order, the first 16 hex digits.
	static std::string HashOfDir(const std::string& Path);
	// Puts the cell in a zip's bytes (its folder's files at the zip's top level) in place as Root/Id,
	// if its hash is Expect ("" to take it as it is). Returns "" or why not.
	static std::string InstallZip(const std::string& Zip, const std::string& Id, const std::string& Expect, const std::string& Root);
	// The first 12 digits of a text's SHA-1 without its last newline: how the index names small files.
	static std::string TextHash(const std::string& Text);

private:
	std::string FetchOne(CellId C, const CellEntry& E);
	std::shared_ptr<const WorldIndex> SavedIndex() const;
	TorqueFit::Json Used() const;
	std::string BaseUrl() const;

	std::mutex Lock;
	std::mutex Fetching;   // one fetch at a time (the planner's view and a journey's plan may both want a cell)
	std::shared_ptr<const WorldIndex> Have;
	std::chrono::steady_clock::time_point HaveAt;
	std::set<std::string> UsedNow;
};
} // namespace TorqueWorld
