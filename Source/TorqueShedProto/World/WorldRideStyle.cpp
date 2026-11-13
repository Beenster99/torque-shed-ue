#include "WorldRideStyle.h"

#include "FitJson.h"

#include <algorithm>
#include <mutex>
#include <string>

namespace TorqueWorld
{
namespace
{
LocalLook Make(const char* Name, LocalLook::EBoundary Boundary, Tint Wall, Tint House, float Render, float Brick, float StoneSlate, float TileShare, float Thatch,
	Tint RenderColour = {0.55f, 0.53f, 0.47f}, Tint BrickColour = {0.26f, 0.095f, 0.06f}, Tint TileColour = {0.30f, 0.11f, 0.06f})
{
	LocalLook L;
	L.Name = Name;
	L.Boundary = Boundary;
	L.WallStone = Wall;
	L.HouseStone = House;
	L.RenderShare = Render;
	L.BrickShare = Brick;
	L.StoneSlateShare = StoneSlate;
	L.TileShare = TileShare;
	L.ThatchShare = Thatch;
	L.RenderColour = RenderColour;
	L.BrickColour = BrickColour;
	L.TileColour = TileColour;
	// [OSM stage 3] Its building habits, by the place.
	const std::string N = Name;
	const auto Set = [&L](float Pitch, float Overhang, LocalLook::EWindow Window, bool Stone, LocalLook::EChimney Chimney)
	{
		L.Pitch = Pitch;
		L.Overhang = Overhang;
		L.Window = Window;
		L.StoneSurrounds = Stone;
		L.Chimney = Chimney;
	};
	using B = LocalLook;
	if (N == "dales" || N == "peak") Set(32.0f, 0.08f, B::Mullion, true, B::GableEnds);         // (stone slates are heavy: a low pitch, flush verges)
	else if (N == "cotswolds") Set(50.0f, 0.1f, B::Mullion, true, B::GableEnds);
	else if (N == "lakes") Set(35.0f, 0.15f, B::Casement, true, B::GableEnds);
	else if (N == "moors" || N == "northumberland") Set(38.0f, 0.15f, B::Sash, true, B::GableEnds);
	else if (N == "cornwall") Set(35.0f, 0.2f, B::Casement, true, B::GableEnds);
	else if (N == "devon") Set(50.0f, 0.5f, B::Casement, false, B::GableEnds);                  // (thatch: steep, deep eaves)
	else if (N == "wales" || N == "highlands" || N == "ulster" || N == "lowlands") Set(42.0f, 0.2f, B::Sash, false, B::GableEnds);
	else if (N == "fens" || N == "lowland") Set(45.0f, 0.3f, B::Sash, false, B::OnRidge);
	else Set(30.0f, 0.5f, B::Casement, false, B::OnRidge);                                     // (the world's: low tiled roofs, wide eaves)
	if (N == "cotswolds") { L.Doors[0] = {0.12f, 0.14f, 0.09f}; L.Doors[1] = {0.5f, 0.47f, 0.38f}; L.Doors[2] = {0.09f, 0.05f, 0.025f}; L.Doors[3] = {0.05f, 0.08f, 0.12f}; }
	else if (N == "cornwall" || N == "devon") { L.Doors[0] = {0.02f, 0.06f, 0.15f}; L.Doors[1] = {0.6f, 0.6f, 0.58f}; L.Doors[3] = {0.03f, 0.12f, 0.1f}; }
	return L;
}

// A box of the map (south, west, north, east) and the look inside it; the first that holds a place wins.
struct Area
{
	double S, W, N, E;
	LocalLook Look;
};

const Tint One{1.0f, 1.0f, 1.0f};
const Tint Whitewash{0.70f, 0.69f, 0.64f}, Harl{0.62f, 0.61f, 0.57f}, Cream{0.66f, 0.60f, 0.46f};
const Tint Honey{1.30f, 1.06f, 0.66f}, Sandstone{1.12f, 0.98f, 0.82f}, Gritstone{0.80f, 0.76f, 0.68f}, Slate{0.66f, 0.70f, 0.70f}, Granite{0.92f, 0.90f, 0.88f};
const Tint Pantile{0.38f, 0.15f, 0.06f}, BuffBrick{0.42f, 0.30f, 0.18f}, RedBrick{0.26f, 0.095f, 0.06f};

const Area* Areas(int& Count)
{
	using B = LocalLook;
	static const Area List[] = {
		// The Cotswolds: honey limestone, walls and houses and roofs.
		{51.55, -2.35, 52.15, -1.45, Make("cotswolds", B::Wall, Honey, Honey, 0.04f, 0.04f, 0.75f, 0.05f, 0.04f)},
		// The Lake District: grey-green slate walls, whitewashed farms, slate roofs.
		{54.15, -3.55, 54.75, -2.70, Make("lakes", B::Wall, Slate, Slate, 0.45f, 0.0f, 0.05f, 0.0f, 0.0f, Whitewash)},
		// The Peak District: dark gritstone.
		{52.95, -2.15, 53.55, -1.45, Make("peak", B::Wall, Gritstone, Gritstone, 0.05f, 0.08f, 0.55f, 0.0f, 0.0f)},
		// The North York Moors and the coast below them: sandstone, red pantiles.
		{54.10, -1.35, 54.65, -0.35, Make("moors", B::Wall, Sandstone, Sandstone, 0.05f, 0.08f, 0.10f, 0.55f, 0.0f, Whitewash, RedBrick, Pantile)},
		// The Yorkshire Dales and the Pennines: limestone and gritstone, stone slate roofs.
		{53.55, -2.75, 54.75, -1.55, Make("dales", B::Wall, One, One, 0.10f, 0.02f, 0.45f, 0.0f, 0.0f)},
		// Cornwall: hedge banks, granite and white-painted cottages, slate.
		{49.80, -6.50, 50.95, -4.20, Make("cornwall", B::HedgeBank, Granite, Granite, 0.50f, 0.0f, 0.0f, 0.0f, 0.04f, Whitewash)},
		// Devon and west Somerset: hedge banks, cream cob and thatch.
		{50.15, -4.60, 51.25, -2.85, Make("devon", B::HedgeBank, Sandstone, Sandstone, 0.55f, 0.05f, 0.0f, 0.10f, 0.25f, Cream)},
		// Wales: grey stone and slate, white and pastel render.
		{51.35, -5.40, 53.45, -2.95, Make("wales", B::Wall, Slate, Slate, 0.45f, 0.05f, 0.0f, 0.0f, 0.0f, Whitewash)},
		// The Highlands and islands: dykes and fences, harled white houses, grey slate.
		{56.15, -8.70, 61.00, -1.60, Make("highlands", B::Wall, Granite, Granite, 0.60f, 0.0f, 0.0f, 0.0f, 0.0f, Harl)},
		{55.60, -8.70, 56.15, -4.60, Make("highlands", B::Wall, Granite, Granite, 0.60f, 0.0f, 0.0f, 0.0f, 0.0f, Harl)},
		// Northern Ireland: rendered and painted, slate.
		{54.00, -8.20, 55.40, -5.40, Make("ulster", B::Wall, Granite, Granite, 0.70f, 0.05f, 0.0f, 0.0f, 0.0f, Whitewash)},
		// Northumberland and Durham: buff sandstone, slate.
		{54.40, -2.60, 55.80, -1.20, Make("northumberland", B::Wall, Sandstone, Sandstone, 0.08f, 0.10f, 0.0f, 0.10f, 0.0f)},
		// Southern Scotland: whinstone and red sandstone, harl, slate.
		{54.60, -5.40, 56.15, -1.60, Make("lowlands", B::Wall, Gritstone, Sandstone, 0.40f, 0.02f, 0.0f, 0.0f, 0.0f, Harl)},
		// The Fens and East Anglia: buff and red brick, pantiles.
		{51.90, -0.45, 53.20, 1.80, Make("fens", B::Wall, Sandstone, Sandstone, 0.30f, 0.60f, 0.0f, 0.70f, 0.08f, Cream, BuffBrick, Pantile)},
	};
	Count = int(sizeof(List) / sizeof(List[0]));
	return List;
}
} // namespace

const LocalLook& LookAt(double Lat, double Lon)
{
	int Count = 0;
	const Area* List = Areas(Count);
	for (int I = 0; I < Count; ++I)
		if (Lat >= List[I].S && Lat < List[I].N && Lon >= List[I].W && Lon < List[I].E) return List[I].Look;
	// The rest of England: red brick and clay tiles, a little render and stone; hedges where the map
	// has walls is the map's to say, so its walls stay walls, of a warmer stone.
	static const LocalLook Lowland = Make("lowland", LocalLook::Wall, Sandstone, Sandstone, 0.20f, 0.65f, 0.0f, 0.75f, 0.03f, Cream, RedBrick);
	return Lowland;
}

namespace
{
std::mutex TownLock;
std::vector<TownEntry>& Towns()
{
	static std::vector<TownEntry> List;
	return List;
}
} // namespace

bool NamedBrick(const std::string& Name, Tint& Out)
{
	// (linear: a common red, a warm orange-red, a dark brown, a London yellow stock, a cream gault,
	// a Staffordshire blue)
	if (Name == "red") Out = {0.26f, 0.095f, 0.06f};
	else if (Name == "orange") Out = {0.36f, 0.15f, 0.07f};
	else if (Name == "brown") Out = {0.17f, 0.085f, 0.055f};
	else if (Name == "yellow") Out = {0.40f, 0.31f, 0.18f};
	else if (Name == "cream") Out = {0.50f, 0.44f, 0.32f};
	else if (Name == "blue") Out = {0.065f, 0.06f, 0.07f};
	else return false;
	return true;
}

bool NamedStone(const std::string& Name, Tint& Out)
{
	// (tints on the scanned limestone: 1 the Dales' own grey)
	if (Name == "bath") Out = {1.38f, 1.22f, 0.92f};
	else if (Name == "honey") Out = {1.30f, 1.06f, 0.66f};
	else if (Name == "sandstone") Out = {1.12f, 0.98f, 0.82f};
	else if (Name == "red_sandstone") Out = {1.15f, 0.72f, 0.58f};
	else if (Name == "gritstone") Out = {0.80f, 0.76f, 0.68f};
	else if (Name == "sooty") Out = {0.50f, 0.47f, 0.43f};   // (old northern stone, blackened by a century of coal smoke)
	else if (Name == "granite") Out = {0.92f, 0.90f, 0.88f};
	else if (Name == "limestone") Out = {1.0f, 1.0f, 1.0f};
	else if (Name == "flint") Out = {0.78f, 0.78f, 0.80f};
	else if (Name == "slate") Out = {0.66f, 0.70f, 0.70f};
	else return false;
	return true;
}

bool NamedTile(const std::string& Name, Tint& Out)
{
	if (Name == "red") Out = {0.30f, 0.11f, 0.06f};
	else if (Name == "orange") Out = {0.38f, 0.15f, 0.06f};
	else if (Name == "brown") Out = {0.16f, 0.08f, 0.05f};
	else if (Name == "blue") Out = {0.08f, 0.08f, 0.09f};
	else return false;
	return true;
}

bool SetTownTable(const std::string& Text)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const TorqueFit::Json* Rows = Root.Get("towns");
	if (Rows == nullptr || !Rows->IsArray()) return false;
	std::vector<TownEntry> List;
	for (const TorqueFit::Json& J : Rows->A)
	{
		TownEntry T;
		T.Name = J.Str("name");
		const TorqueFit::Json* At = J.Get("at");
		if (At == nullptr || !At->IsArray() || At->A.size() < 2) continue;
		T.Lat = At->A[0].N;
		T.Lon = At->A[1].N;
		T.Km = J.Num("km", 1.5);
		if (const TorqueFit::Json* W = J.Get("walls"))
		{
			T.HasWalls = true;
			T.Brick = float(W->Num("brick"));
			T.Stone = float(W->Num("stone"));
			T.Render = float(W->Num("render"));
		}
		if (const TorqueFit::Json* R = J.Get("roofs"))
		{
			T.HasRoofs = true;
			T.Slate = float(R->Num("slate"));
			T.Tile = float(R->Num("tile"));
			T.StoneSlate = float(R->Num("stone"));
			T.Thatch = float(R->Num("thatch"));
		}
		T.BrickName = J.Str("brick");
		T.StoneName = J.Str("stone");
		T.TileName = J.Str("tile");
		List.push_back(std::move(T));
	}
	std::lock_guard<std::mutex> Hold(TownLock);
	Towns() = std::move(List);
	return true;
}

std::vector<TownEntry> TownTable()
{
	std::lock_guard<std::mutex> Hold(TownLock);
	return Towns();
}

LocalLook TownLook(const LocalLook& Region, const TownEntry& Town)
{
	LocalLook L = Region;
	if (Town.HasWalls)
	{
		const float All = std::max(0.001f, Town.Brick + Town.Stone + Town.Render);
		L.BrickShare = Town.Brick / All;
		L.RenderShare = Town.Render / All;
	}
	if (Town.HasRoofs)
	{
		const float All = std::max(0.001f, Town.Slate + Town.Tile + Town.StoneSlate + Town.Thatch);
		L.TileShare = Town.Tile / All;
		L.StoneSlateShare = Town.StoneSlate / All;
		L.ThatchShare = Town.Thatch / All;
	}
	NamedBrick(Town.BrickName, L.BrickColour);
	if (NamedStone(Town.StoneName, L.HouseStone)) L.WallStone = L.HouseStone;
	NamedTile(Town.TileName, L.TileColour);
	return L;
}

const LocalLook& WorldLook()
{
	// (pale render on most houses, the rest grey stone; red-brown clay tiles; walls of field stone)
	static const LocalLook World = Make("world", LocalLook::Wall, Gritstone, Granite, 0.70f, 0.05f, 0.10f, 0.70f, 0.0f, Cream, RedBrick, Pantile);
	return World;
}
} // namespace TorqueWorld
