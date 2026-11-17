// The look of a part of Britain (Ride UK stage 5): what its field walls, houses and roofs are made of,
// so a ride in the Cotswolds is honey stone, one in Cornwall has hedge banks and white cottages, one
// in the Lakes grey-green slate and whitewashed farms, the Midlands red brick. Chosen by where a place
// is (a coarse map of the country's building traditions), the same on every rider's machine.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct Tint
{
	float R = 1.0f, G = 1.0f, B = 1.0f;
};

struct LocalLook
{
	const char* Name = "dales";
	// Field boundaries where the map has a wall line: a dry stone wall, a hedge bank (earth faced with
	// stone or turf, a hedge on top: the South West's), or a slate or stone fence-like dyke.
	enum EBoundary { Wall, HedgeBank };
	EBoundary Boundary = Wall;
	Tint WallStone;            // the scanned stone of the walls, tinted (1 the Dales' limestone)
	Tint HouseStone;           // and of stone houses
	// The share of houses (not barns) of each walling, the rest stone; and their colours (linear).
	float RenderShare = 0.1f, BrickShare = 0.0f;
	Tint RenderColour{0.55f, 0.53f, 0.47f};
	Tint BrickColour{0.26f, 0.095f, 0.06f};
	// Roofs: the share of each covering, the rest slate.
	float StoneSlateShare = 0.45f, TileShare = 0.0f, ThatchShare = 0.0f;
	Tint TileColour{0.30f, 0.11f, 0.06f};
	// [OSM stage 3] Building habits: the houses' roof pitch (degrees), how far the roof runs past the
	// walls (m), the windows (sashes, cottage casements or stone mullions), whether openings have stone
	// sills and lintels, where the chimneys stand (a stack at each gable end, or on the ridge), and the
	// door colours the place paints (linear).
	float Pitch = 40.0f, Overhang = 0.3f;
	enum EWindow { Sash, Casement, Mullion };
	EWindow Window = Sash;
	bool StoneSurrounds = true;
	enum EChimney { GableEnds, OnRidge };
	EChimney Chimney = GableEnds;
	Tint Doors[4] = {{0.02f, 0.06f, 0.03f}, {0.012f, 0.012f, 0.012f}, {0.12f, 0.015f, 0.015f}, {0.01f, 0.02f, 0.07f}};
};

// The look of the place at a latitude and longitude.
const LocalLook& LookAt(double Lat, double Lon);

// [towns stage 1] A town's own look, from Data/RideUK/town_looks.json (the famous places whose look
// is well known and whose map tags are thin): where it is, how far it reaches, and its walls and
// roofs. Within Km of it a building takes this look over its region's, and over the look counted
// from its neighbours' tags.
struct TownEntry
{
	std::string Name;
	double Lat = 0.0, Lon = 0.0, Km = 1.5;
	// The shares of walls and roofs ("" walls the region's), and their colours by name.
	bool HasWalls = false, HasRoofs = false;
	float Brick = 0.0f, Stone = 0.0f, Render = 0.0f;
	float Slate = 0.0f, Tile = 0.0f, StoneSlate = 0.0f, Thatch = 0.0f;
	std::string BrickName, StoneName, TileName;
};
// Reads the table (JSON text); false if it isn't one. Kept for every ride after (thread safe).
bool SetTownTable(const std::string& Text);
std::vector<TownEntry> TownTable();
// A brick colour by name (red, orange, brown, yellow, cream, blue), a stone's tint (bath, honey,
// sandstone, gritstone, sooty, granite, limestone, red_sandstone, flint, slate) or a roof tile's
// (red, orange, brown, blue); false for a name it doesn't know.
bool NamedBrick(const std::string& Name, Tint& Out);
bool NamedStone(const std::string& Name, Tint& Out);
bool NamedTile(const std::string& Name, Tint& Out);
// The region's look made a town's: these shares and colours over it.
LocalLook TownLook(const LocalLook& Region, const TownEntry& Town);
// [towns stage 4] House types: what a house is (a cottage or a village row out of town; in a town a
// detached house, a semi, a terrace or a tenement) and when it was built, from Data/RideUK/
// house_types.json. Each kind lists its periods with how common each is and how that period built:
// its windows ("place" the place's own, else sash, casement, mullion or modern), how many have bay
// windows (and of those how many run up two storeys, how many are square, not canted), how many
// of its squarish houses are hipped (-1 leave it to the place), its door styles and hoods. A whole row
// or street takes one period, so a terrace matches and only its doors' colours vary.
struct HousePeriod
{
	std::string Name;
	double Weight = 1.0;
	int Window = -1;                 // -1 the place's, else LocalLook::EWindow, or 3 modern (casements, no bars)
	double Bay = 0.0, BayTwo = 0.0, BaySquare = 0.0;
	double Hipped = -1.0;
	double Hood = 0.35;
	std::vector<int> Doors;          // door styles (0 six-panel, 1 half-glazed Victorian, 2 ledged, 3 modern); empty any
};
struct HouseTypes
{
	enum EKind { Cottage, VillageRow, Detached, Semi, Terrace, Tenement, Kinds };
	std::vector<HousePeriod> Periods[Kinds];
	// The period for a pick in [0, 1), by weight; null if the kind has none.
	const HousePeriod* Pick(int Kind, double Chance) const;
};
// Reads the table (JSON text); false if it isn't one. Kept for every ride after (thread safe); until
// then, and for a kind it leaves out, the built-in table (the file's own figures).
bool SetHouseTypes(const std::string& Text);
std::shared_ptr<const HouseTypes> HouseTypesNow();

// [stage 8] The world's default look, away from Britain: rendered walls, tiled roofs, grey stone.
const LocalLook& WorldLook();
} // namespace TorqueWorld
