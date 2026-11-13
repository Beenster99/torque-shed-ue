// Towns stage 1: a look for each town. Where the map's mappers have tagged enough of a place's
// buildings with what their walls and roofs are made of (York: brick under clay tiles; Leeds: brick
// under slate), the place's houses follow that mix, counted over the 3 km round each kilometre
// square; and the famous places whose look is well known and whose tags are thin (Bath stone, Leeds'
// sooty stone, Edinburgh's sandstone...) take theirs from Data/RideUK/town_looks.json. Anywhere else
// keeps its region's look (WorldRideStyle). A building's own tags still win (WorldRideBuildings).
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace TorqueWorld
{
namespace RideTownBits
{
long long TownKey(int Cx, int Cz)
{
	return static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
}

// What the tagged buildings round a square say: how many name a wall material, and of what; how
// many a roof's, and of what; the colours of the brick ones (linear).
struct TagCount
{
	int Walls = 0, Brick = 0, Stone = 0, Render = 0;
	int Roofs = 0, Slate = 0, Tile = 0, Flags = 0, Thatch = 0;
	int Coloured = 0;
	double R = 0.0, G = 0.0, B = 0.0;
	void Add(const TagCount& O)
	{
		Walls += O.Walls; Brick += O.Brick; Stone += O.Stone; Render += O.Render;
		Roofs += O.Roofs; Slate += O.Slate; Tile += O.Tile; Flags += O.Flags; Thatch += O.Thatch;
		Coloured += O.Coloured; R += O.R; G += O.G; B += O.B;
	}
};
} // namespace RideTownBits

void RidePlan::IndexTowns()
{
	using namespace RideTownBits;
	TownGrid.clear();
	TownLooks.clear();
	TownSources.clear();
	TownsCounted = TownsTabled = 0;
	if (!Britain()) return;
	const Region& G = Area.Roads;
	const Frame F(G.FrameLat, G.FrameLon);
	const auto CellOf = [](double V) { return int(std::floor(V / TownCell)); };
	// (every kilometre square with buildings in it)
	std::unordered_map<long long, std::pair<int, int>> Built;
	for (const Building& B : G.Buildings) Built[TownKey(CellOf(B.X), CellOf(B.Z))] = {CellOf(B.X), CellOf(B.Z)};
	std::unordered_map<long long, TagCount> Tags;
	for (const BuildingLook& B : G.Scenery.Buildings)
	{
		TagCount& T = Tags[TownKey(CellOf(B.At.X), CellOf(B.At.Z))];
		const std::string& Wall = B.Words[4];
		const std::string& Roof = B.Words[3];
		if (!Wall.empty())
		{
			const bool IsBrick = Wall == "brick";
			const bool IsStone = Wall == "stone" || Wall == "sandstone" || Wall == "limestone" || Wall == "granite" || Wall == "flint";
			const bool IsRender = Wall == "plaster" || Wall == "render" || Wall == "rendered" || Wall == "concrete" || Wall == "pebbledash";
			if (IsBrick || IsStone || IsRender)
			{
				++T.Walls;
				T.Brick += IsBrick ? 1 : 0;
				T.Stone += IsStone ? 1 : 0;
				T.Render += IsRender ? 1 : 0;
				RideSceneBits::Rgb C;
				// (only a brick's colour: red to brown, not a grey or a white the paint or the render is)
				if (IsBrick && RideSceneBits::BuildingTagColour(B.Words[5], C) && C.R > C.G * 1.3f)
				{
					++T.Coloured;
					T.R += C.R; T.G += C.G; T.B += C.B;
				}
			}
		}
		if (!Roof.empty())
		{
			const bool IsSlate = Roof == "slate", IsTile = Roof == "roof_tiles" || Roof == "tiles" || Roof == "clay", IsFlags = Roof == "stone", IsThatch = Roof == "thatch";
			if (IsSlate || IsTile || IsFlags || IsThatch)
			{
				++T.Roofs;
				T.Slate += IsSlate ? 1 : 0;
				T.Tile += IsTile ? 1 : 0;
				T.Flags += IsFlags ? 1 : 0;
				T.Thatch += IsThatch ? 1 : 0;
			}
		}
	}
	// The counted looks: each built square with 50 or more tagged walls or roofs in the 3 km round it
	// (fewer is a guess from too few: the region's look stands).
	constexpr int Enough = 50;
	for (const auto& Sq : Built)
	{
		TagCount Sum;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Tags.find(TownKey(Sq.second.first + Dx, Sq.second.second + Dz));
				if (It != Tags.end()) Sum.Add(It->second);
			}
		if (Sum.Walls < Enough && Sum.Roofs < Enough) continue;
		const double Cx = (Sq.second.first + 0.5) * TownCell, Cz = (Sq.second.second + 0.5) * TownCell;
		const Geo Here = F.ToGeo(Cx, Cz);
		TownEntry Counted;
		Counted.Name = "tags";
		if (Sum.Walls >= Enough)
		{
			Counted.HasWalls = true;
			Counted.Brick = float(Sum.Brick);
			Counted.Stone = float(Sum.Stone);
			Counted.Render = float(Sum.Render);
		}
		if (Sum.Roofs >= Enough)
		{
			Counted.HasRoofs = true;
			Counted.Slate = float(Sum.Slate);
			Counted.Tile = float(Sum.Tile);
			Counted.StoneSlate = float(Sum.Flags);
			Counted.Thatch = float(Sum.Thatch);
		}
		LocalLook L = TownLook(LookAt(Here.Lat, Here.Lon), Counted);
		if (Sum.Coloured >= 10)
		{
			// (the brick colour the mappers saw, already linear and toned to brick; kept within a real
			// brick's brightness, so a few "black" or "white" tags don't make a town of them)
			const double R = Sum.R / Sum.Coloured, Gc = Sum.G / Sum.Coloured, Bc = Sum.B / Sum.Coloured;
			const double Lum = 0.3 * R + 0.59 * Gc + 0.11 * Bc;
			const double By = Lum > 1e-6 ? std::clamp(Lum, 0.07, 0.3) / Lum : 1.0;
			L.BrickColour = {float(R * By), float(Gc * By), float(Bc * By)};
		}
		TownGrid[Sq.first] = int(TownLooks.size());
		TownLooks.push_back(L);
		TownSources.push_back("tags " + std::to_string(std::max(Sum.Walls, Sum.Roofs)));
		++TownsCounted;
	}
	// The table's towns, over the counted looks: every built square whose middle is within its reach.
	for (const TownEntry& T : TownTable())
	{
		const P2 At = F.FromGeo(T.Lat, T.Lon);
		const double Reach = T.Km * 1000.0;
		int Hit = 0, Index = -1;
		for (const auto& Sq : Built)
		{
			const double Cx = (Sq.second.first + 0.5) * TownCell, Cz = (Sq.second.second + 0.5) * TownCell;
			if (std::hypot(Cx - At.X, Cz - At.Z) > Reach) continue;
			if (Index < 0)
			{
				const Geo Here = F.ToGeo(At.X, At.Z);
				Index = int(TownLooks.size());
				TownLooks.push_back(TownLook(LookAt(Here.Lat, Here.Lon), T));
				TownSources.push_back(T.Name);
			}
			auto Old = TownGrid.find(Sq.first);
			if (Old != TownGrid.end() && TownSources[size_t(Old->second)].rfind("tags", 0) == 0) --TownsCounted;
			TownGrid[Sq.first] = Index;
			++Hit;
		}
		if (Hit > 0) ++TownsTabled;
	}
}

const LocalLook* RidePlan::TownLookAt(double X, double Z) const
{
	if (TownGrid.empty()) return nullptr;
	const auto It = TownGrid.find(RideTownBits::TownKey(int(std::floor(X / TownCell)), int(std::floor(Z / TownCell))));
	return It == TownGrid.end() ? nullptr : &TownLooks[size_t(It->second)];
}
} // namespace TorqueWorld
