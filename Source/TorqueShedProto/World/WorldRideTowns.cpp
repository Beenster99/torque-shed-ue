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
	AtlasGrid.clear();
	TownsCounted = TownsTabled = AtlasSquares = 0;
	ShopPlaces.clear();
	ShopGrid.clear();
	StreetPieces.clear();
	StreetGrid.clear();
	StreetsHere.clear();
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
		LocalLook L = TownLook(AreaLook(Here.Lat, Here.Lon), Counted);
		L.From = LocalLook::FromTags;
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
				TownLooks.push_back(TownLook(AreaLook(Here.Lat, Here.Lon), T));
				TownSources.push_back(T.Name);
			}
			auto Old = TownGrid.find(Sq.first);
			if (Old != TownGrid.end() && TownSources[size_t(Old->second)].rfind("tags", 0) == 0) --TownsCounted;
			TownGrid[Sq.first] = Index;
			++Hit;
		}
		if (Hit > 0) ++TownsTabled;
	}
	// [towns stage 6] The shopping streets: retail areas and pedestrian streets, indexed by 128 m square.
	{
		const auto Index = [this](const ShopPlace& Pl)
		{
			const int I = int(ShopPlaces.size());
			ShopPlaces.push_back(Pl);
			for (int Cz = int(std::floor((Pl.Z0 - 60.0) / 128.0)); Cz <= int(std::floor((Pl.Z1 + 60.0) / 128.0)); ++Cz)
				for (int Cx = int(std::floor((Pl.X0 - 60.0) / 128.0)); Cx <= int(std::floor((Pl.X1 + 60.0) / 128.0)); ++Cx)
					ShopGrid[TownKey(Cx, Cz)].push_back(I);
		};
		for (const GroundShape& A : G.Ground.Areas)
		{
			if (A.Kind != "retail" || A.Pts.size() < 3) continue;
			ShopPlace Pl;
			Pl.X0 = Pl.X1 = A.Pts[0].X;
			Pl.Z0 = Pl.Z1 = A.Pts[0].Z;
			for (const P2& P : A.Pts) { Pl.X0 = std::min(Pl.X0, P.X); Pl.X1 = std::max(Pl.X1, P.X); Pl.Z0 = std::min(Pl.Z0, P.Z); Pl.Z1 = std::max(Pl.Z1, P.Z); }
			// (not a retail park's sheds out of town: an area of a few hectares at most)
			if ((Pl.X1 - Pl.X0) * (Pl.Z1 - Pl.Z0) > 400000.0) continue;
			Pl.Ring = &A.Pts;
			Index(Pl);
		}
		for (const GroundShape& Pth : G.Ground.Paths)
		{
			if (Pth.Kind != "pedestrian") continue;
			for (size_t I = 1; I < Pth.Pts.size(); ++I)
			{
				ShopPlace Pl;
				Pl.A = Pth.Pts[I - 1];
				Pl.B = Pth.Pts[I];
				Pl.X0 = std::min(Pl.A.X, Pl.B.X); Pl.X1 = std::max(Pl.A.X, Pl.B.X);
				Pl.Z0 = std::min(Pl.A.Z, Pl.B.Z); Pl.Z1 = std::max(Pl.A.Z, Pl.B.Z);
				Index(Pl);
			}
		}
	}
	// [towns stage 8] The famous streets in the region (a built square under one of its points): each its
	// own look over its town's (or its area's), its line in pieces by 128 m square, and where it has
	// shops, its line a shopping street.
	for (const StreetEntry& S : StreetTable())
	{
		std::vector<std::vector<P2>> Lines;
		bool Here = false;
		for (const auto& Ll : S.Lines)
		{
			Lines.emplace_back();
			for (const auto& Pt : Ll)
			{
				Lines.back().push_back(F.FromGeo(Pt.first, Pt.second));
				Here = Here || Built.count(TownKey(CellOf(Lines.back().back().X), CellOf(Lines.back().back().Z))) != 0;
			}
		}
		if (!Here) continue;
		const P2 First = Lines[0][0];
		const LocalLook* Town = TownLookAt(First.X, First.Z);
		const Geo FirstGeo = F.ToGeo(First.X, First.Z);
		const int Look = int(TownLooks.size());
		TownLooks.push_back(StreetLook(Town ? *Town : AreaLook(FirstGeo.Lat, FirstGeo.Lon), S));
		TownSources.push_back("street " + S.Name);
		StreetsHere.push_back(S.Name);
		for (const std::vector<P2>& Line : Lines)
			for (size_t I = 1; I < Line.size(); ++I)
			{
				StreetPiece Pc;
				Pc.A = Line[I - 1];
				Pc.B = Line[I];
				Pc.Look = Look;
				Pc.Reach = S.Reach;
				Pc.Setts = S.Setts;
				Pc.Lamps = S.Lamps;
				const int Index = int(StreetPieces.size());
				StreetPieces.push_back(Pc);
				const double Pad = S.Reach + 40.0;
				for (int Cz = int(std::floor((std::min(Pc.A.Z, Pc.B.Z) - Pad) / 128.0)); Cz <= int(std::floor((std::max(Pc.A.Z, Pc.B.Z) + Pad) / 128.0)); ++Cz)
					for (int Cx = int(std::floor((std::min(Pc.A.X, Pc.B.X) - Pad) / 128.0)); Cx <= int(std::floor((std::max(Pc.A.X, Pc.B.X) + Pad) / 128.0)); ++Cx)
						StreetGrid[TownKey(Cx, Cz)].push_back(Index);
				if (S.Shops)
				{
					ShopPlace Pl;
					Pl.A = Pc.A;
					Pl.B = Pc.B;
					Pl.X0 = std::min(Pl.A.X, Pl.B.X); Pl.X1 = std::max(Pl.A.X, Pl.B.X);
					Pl.Z0 = std::min(Pl.A.Z, Pl.B.Z); Pl.Z1 = std::max(Pl.A.Z, Pl.B.Z);
					const int Shop = int(ShopPlaces.size());
					ShopPlaces.push_back(Pl);
					for (int Cz = int(std::floor((Pl.Z0 - 60.0) / 128.0)); Cz <= int(std::floor((Pl.Z1 + 60.0) / 128.0)); ++Cz)
						for (int Cx = int(std::floor((Pl.X0 - 60.0) / 128.0)); Cx <= int(std::floor((Pl.X1 + 60.0) / 128.0)); ++Cx)
							ShopGrid[TownKey(Cx, Cz)].push_back(Shop);
				}
			}
	}
	// [towns stage 5] The atlas's areas on the squares no town holds: one look per area met.
	std::unordered_map<std::string, int> AreaIndex;
	for (const auto& Sq : Built)
	{
		if (TownGrid.count(Sq.first) != 0) continue;
		const Geo Here = F.ToGeo((Sq.second.first + 0.5) * TownCell, (Sq.second.second + 0.5) * TownCell);
		std::string Name;
		const LocalLook L = AreaLook(Here.Lat, Here.Lon, &Name);
		if (Name.empty()) continue;
		auto Known = AreaIndex.find(Name);
		if (Known == AreaIndex.end())
		{
			Known = AreaIndex.emplace(Name, int(TownLooks.size())).first;
			TownLooks.push_back(L);
			TownSources.push_back("atlas " + Name);
		}
		AtlasGrid[Sq.first] = Known->second;
		++AtlasSquares;
	}
}

const LocalLook* RidePlan::TownLookAt(double X, double Z) const
{
	if (TownGrid.empty()) return nullptr;
	const auto It = TownGrid.find(RideTownBits::TownKey(int(std::floor(X / TownCell)), int(std::floor(Z / TownCell))));
	return It == TownGrid.end() ? nullptr : &TownLooks[size_t(It->second)];
}

bool RidePlan::ShopStreetNear(double X, double Z, double Reach) const
{
	const auto It = ShopGrid.find(RideTownBits::TownKey(int(std::floor(X / 128.0)), int(std::floor(Z / 128.0))));
	if (It == ShopGrid.end()) return false;
	for (int I : It->second)
	{
		const ShopPlace& Pl = ShopPlaces[size_t(I)];
		if (X < Pl.X0 - Reach || X > Pl.X1 + Reach || Z < Pl.Z0 - Reach || Z > Pl.Z1 + Reach) continue;
		if (Pl.Ring != nullptr)
		{
			if (RideShapes::InRing(*Pl.Ring, X, Z)) return true;
			const std::vector<P2>& R = *Pl.Ring;
			for (size_t K = 0; K < R.size(); ++K)
			{
				const P2& P = R[K];
				const P2& Q = R[(K + 1) % R.size()];
				const double Dx = Q.X - P.X, Dz = Q.Z - P.Z, L2 = Dx * Dx + Dz * Dz;
				const double T = L2 > 1e-9 ? std::clamp(((X - P.X) * Dx + (Z - P.Z) * Dz) / L2, 0.0, 1.0) : 0.0;
				if (std::hypot(P.X + Dx * T - X, P.Z + Dz * T - Z) <= Reach) return true;
			}
			continue;
		}
		const double Dx = Pl.B.X - Pl.A.X, Dz = Pl.B.Z - Pl.A.Z, L2 = Dx * Dx + Dz * Dz;
		const double T = L2 > 1e-9 ? std::clamp(((X - Pl.A.X) * Dx + (Z - Pl.A.Z) * Dz) / L2, 0.0, 1.0) : 0.0;
		if (std::hypot(Pl.A.X + Dx * T - X, Pl.A.Z + Dz * T - Z) <= Reach) return true;
	}
	return false;
}

const RidePlan::StreetPiece* RidePlan::StreetAt(double X, double Z, double Extra) const
{
	if (StreetGrid.empty()) return nullptr;
	const auto It = StreetGrid.find(RideTownBits::TownKey(int(std::floor(X / 128.0)), int(std::floor(Z / 128.0))));
	if (It == StreetGrid.end()) return nullptr;
	const StreetPiece* Best = nullptr;
	double BestGap = 1e9;
	for (int I : It->second)
	{
		const StreetPiece& Pc = StreetPieces[size_t(I)];
		const double Dx = Pc.B.X - Pc.A.X, Dz = Pc.B.Z - Pc.A.Z, L2 = Dx * Dx + Dz * Dz;
		const double T = L2 > 1e-9 ? std::clamp(((X - Pc.A.X) * Dx + (Z - Pc.A.Z) * Dz) / L2, 0.0, 1.0) : 0.0;
		const double Gap = std::hypot(Pc.A.X + Dx * T - X, Pc.A.Z + Dz * T - Z) - Pc.Reach - Extra;
		if (Gap <= 0.0 && Gap < BestGap)
		{
			Best = &Pc;
			BestGap = Gap;
		}
	}
	return Best;
}

const LocalLook* RidePlan::StreetLookAt(double X, double Z) const
{
	const StreetPiece* Pc = StreetAt(X, Z);
	return Pc ? &TownLooks[size_t(Pc->Look)] : nullptr;
}

bool RidePlan::SettsStreet(const std::vector<P2>& Line) const
{
	// (most of the road's points within its reach of a street in setts: a road along it, not one across it)
	if (StreetPieces.empty() || Line.size() < 2) return false;
	// ([Nov 18] beenster, Haworth: "some are cobbles, some are not". A short bit of road at a junction off the
	// street, or across it, had most of its few points within the street's reach and went to setts, so the
	// road flicked between setts and tarmac. Now by length, and only where the road runs along the street, on it.)
	double In = 0.0, All = 0.0;
	for (size_t I = 1; I < Line.size(); ++I)
	{
		const P2 D = Line[I] - Line[I - 1];
		const double Len = D.Length();
		if (Len < 1e-6) continue;
		All += Len;
		const P2 Mid = (Line[I] + Line[I - 1]) * 0.5;
		const StreetPiece* Pc = StreetAt(Mid.X, Mid.Z);
		if (!Pc || !Pc->Setts) continue;
		const P2 S = Pc->B - Pc->A;
		const double Sl = S.Length();
		if (Sl < 1e-6 || std::fabs((D.X * S.X + D.Z * S.Z) / (Len * Sl)) < 0.8) continue;
		// (and on it, not a street beside it within the look's reach)
		const double T = std::clamp(((Mid.X - Pc->A.X) * S.X + (Mid.Z - Pc->A.Z) * S.Z) / (Sl * Sl), 0.0, 1.0);
		if ((Pc->A + S * T).DistanceTo(Mid) > 6.0) continue;
		In += Len;
	}
	return All >= 15.0 && In >= All * 0.6;
}

bool RidePlan::SettsSurface(const std::string& Surface)
{
	return Surface == "sett" || Surface == "cobblestone" || Surface == "unhewn_cobblestone" || Surface == "cobblestone:flattened";
}

bool RidePlan::HeritageLampAt(double X, double Z) const
{
	if (!PropModelled[PropLampHeritage]) return false;
	const StreetPiece* Pc = StreetAt(X, Z, 10.0);   // [towns stage 8]
	if (Pc) return Pc->Lamps;
	return TownLookAt(X, Z) != nullptr && ShopStreetNear(X, Z, 35.0);
}

const LocalLook* RidePlan::AtlasLookAt(double X, double Z) const
{
	if (AtlasGrid.empty()) return nullptr;
	const auto It = AtlasGrid.find(RideTownBits::TownKey(int(std::floor(X / TownCell)), int(std::floor(Z / TownCell))));
	return It == AtlasGrid.end() ? nullptr : &TownLooks[size_t(It->second)];
}
} // namespace TorqueWorld
