#include "WorldRideStyle.h"

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
} // namespace TorqueWorld
