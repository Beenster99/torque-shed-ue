#include "WorldCache.h"

#include "WorldFiles.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace TorqueWorld
{
namespace Cache
{
Use Measure(const std::string& CellsDir, const std::string& HeightsDir)
{
	Use U;
	for (const Files::FileFound& F : Files::Walk(CellsDir)) U.CellBytes += F.Bytes;
	for (const std::string& Sub : Files::List(CellsDir, true))
		if (!Sub.empty() && Sub[0] == 'c') ++U.Cells;
	for (const Files::FileFound& F : Files::Walk(HeightsDir))
	{
		U.HeightBytes += F.Bytes;
		++U.Tiles;
	}
	return U;
}

int CellsToKeep(long long LimitBytes)
{
	return int(std::max<long long>(24, LimitBytes * 2 / 5 / CellBytes));
}

long long PruneHeights(const std::string& HeightsDir, long long LimitBytes)
{
	std::vector<Files::FileFound> Found = Files::Walk(HeightsDir);
	long long Kept = 0;
	for (const Files::FileFound& F : Found) Kept += F.Bytes;
	const long long Allowed = LimitBytes * 3 / 5;
	if (Kept <= Allowed) return 0;
	std::sort(Found.begin(), Found.end(), [](const Files::FileFound& A, const Files::FileFound& B) { return A.Modified < B.Modified; });
	long long Freed = 0;
	for (const Files::FileFound& F : Found)
	{
		if (Kept - Freed <= Allowed) break;
		if (Files::Remove(F.Path)) Freed += F.Bytes;
	}
	return Freed;
}

void Clear(const std::string& CellsDir, const std::string& HeightsDir)
{
	Files::RemoveTree(CellsDir);
	Files::RemoveTree(HeightsDir);
}

std::string Words(long long Bytes)
{
	char Buf[32];
	if (Bytes >= 1000000000LL) std::snprintf(Buf, sizeof(Buf), "%.1f GB", double(Bytes) / 1e9);
	else std::snprintf(Buf, sizeof(Buf), "%.0f MB", double(Bytes) / 1e6);
	return Buf;
}
} // namespace Cache
} // namespace TorqueWorld
