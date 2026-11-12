// What of the world the game keeps on disk (Ride UK stage 8): how much there is, keeping it under the
// player's limit, and clearing it. The cells (CellStore, about 2.5 MB each with their streams and
// trees) and the ground's height tiles (HeightStore) are kept under Saved/World so a road ridden
// before rides again offline; this is the one place that weighs them.
//
// Plain C++ (SimTest's world_test tries it): the game runs it on a worker, never mid-ride.
#pragma once

#include <string>

namespace TorqueWorld
{
namespace Cache
{
struct Use
{
	long long CellBytes = 0, HeightBytes = 0;
	int Cells = 0, Tiles = 0;
	long long Total() const { return CellBytes + HeightBytes; }
};
// How much is kept in the two folders.
Use Measure(const std::string& CellsDir, const std::string& HeightsDir);
// How many cells the store may keep in a limit of LimitBytes for everything: the cells have 40% of it.
int CellsToKeep(long long LimitBytes);
static constexpr long long CellBytes = 2500000;
// Deletes the height tiles fetched longest ago (and the notes of tiles that are none) until those kept
// come to no more than 60% of LimitBytes. Returns the bytes freed.
long long PruneHeights(const std::string& HeightsDir, long long LimitBytes);
// Everything fetched gone: both folders emptied (the next ride fetches what it needs again).
void Clear(const std::string& CellsDir, const std::string& HeightsDir);
// "1.2 GB", "340 MB".
std::string Words(long long Bytes);
} // namespace Cache
} // namespace TorqueWorld
