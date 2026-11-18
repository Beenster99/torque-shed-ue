#include "WorldRideStyle.h"

#include "FitJson.h"

#include <algorithm>
#include <cmath>
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
	// [towns stage 5] The share of houses painted in colours by the sea (St Ives, Tenby, Tobermory, Portrush).
	L.Paint = N == "wales" ? 0.4f : N == "cornwall" || N == "highlands" ? 0.35f : N == "devon" || N == "ulster" ? 0.3f : N == "lowlands" ? 0.25f
		: N == "lowland" || N == "fens" ? 0.2f : N == "moors" || N == "northumberland" || N == "lakes" ? 0.1f : 0.0f;
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
std::vector<StreetEntry>& Streets()
{
	static std::vector<StreetEntry> List;
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
	// [towns stage 5] (the atlas's: a Peterborough Fletton, pink-buff; a Suffolk white; a hard
	// Accrington red; a plum-purple London stock; a soft Georgian rubbed red)
	else if (Name == "fletton") Out = {0.30f, 0.15f, 0.095f};
	else if (Name == "white") Out = {0.52f, 0.47f, 0.38f};
	else if (Name == "accrington") Out = {0.22f, 0.06f, 0.04f};
	else if (Name == "plum") Out = {0.15f, 0.07f, 0.06f};
	else if (Name == "gault") Out = {0.50f, 0.44f, 0.32f};
	else if (Name == "stock") Out = {0.40f, 0.31f, 0.18f};
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
	// [towns stage 5] (the atlas's: Northamptonshire and Oxfordshire ironstone, rust-brown; Portland's
	// white; the Magnesian limestone's cream; Kentish ragstone, grey-green; Norfolk carstone, gingerbread;
	// chalk clunch; Purbeck's grey; Ham stone, golden-orange; whinstone, dark; a buff sandstone)
	else if (Name == "ironstone") Out = {1.22f, 0.80f, 0.46f};
	else if (Name == "portland") Out = {1.40f, 1.36f, 1.24f};
	else if (Name == "magnesian") Out = {1.30f, 1.20f, 0.98f};
	else if (Name == "ragstone") Out = {0.98f, 0.96f, 0.84f};
	else if (Name == "carstone") Out = {0.86f, 0.52f, 0.30f};
	else if (Name == "clunch") Out = {1.45f, 1.42f, 1.32f};
	else if (Name == "purbeck") Out = {1.05f, 1.03f, 0.94f};
	else if (Name == "ham") Out = {1.32f, 0.96f, 0.56f};
	else if (Name == "whinstone") Out = {0.60f, 0.60f, 0.60f};
	else if (Name == "buff") Out = {1.22f, 1.10f, 0.90f};
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

namespace
{
std::mutex HouseTypeLock;
std::shared_ptr<const HouseTypes>& HouseTypeTable()
{
	static std::shared_ptr<const HouseTypes> Table;
	return Table;
}

bool ReadHouseTypes(const std::string& Text, HouseTypes& Out)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return false;
	static const char* const KindNames[HouseTypes::Kinds] = {"cottage", "village_row", "detached", "semi", "terrace", "tenement"};
	for (int K = 0; K < HouseTypes::Kinds; ++K)
	{
		const TorqueFit::Json* Rows = Root.Get(KindNames[K]);
		if (Rows == nullptr || !Rows->IsArray()) continue;
		std::vector<HousePeriod> List;
		for (const TorqueFit::Json& J : Rows->A)
		{
			HousePeriod P;
			P.Name = J.Str("period");
			P.Weight = std::max(0.0, J.Num("weight", 1.0));
			const std::string Win = J.Str("window");
			P.Window = Win == "sash" ? int(LocalLook::Sash) : Win == "casement" ? int(LocalLook::Casement) : Win == "mullion" ? int(LocalLook::Mullion) : Win == "modern" ? 3 : -1;
			P.Bay = std::clamp(J.Num("bay", 0.0), 0.0, 1.0);
			P.BayTwo = std::clamp(J.Num("bay_two", 0.0), 0.0, 1.0);
			P.BaySquare = std::clamp(J.Num("bay_square", 0.0), 0.0, 1.0);
			P.Hipped = J.Num("hipped", -1.0);
			P.Hood = std::clamp(J.Num("hood", 0.35), 0.0, 1.0);
			if (const TorqueFit::Json* D = J.Get("doors"))
				if (D->IsArray())
					for (const TorqueFit::Json& Dn : D->A)
					{
						const std::string& N = Dn.S;
						const int Style = N == "panel6" ? 0 : N == "victorian" ? 1 : N == "ledged" ? 2 : N == "modern" ? 3 : -1;
						if (Style >= 0) P.Doors.push_back(Style);
					}
			List.push_back(std::move(P));
		}
		Out.Periods[K] = std::move(List);
	}
	return true;
}

// (the built-in table: Data/RideUK/house_types.json's own figures, for a ride before it is read or without it)
const char* const BuiltInHouseTypes = R"({
 "cottage": [{"period": "old", "weight": 1, "window": "place", "bay": 0.03, "hood": 0.3}],
 "village_row": [{"period": "victorian", "weight": 0.8, "window": "place", "bay": 0.2, "bay_two": 0.1, "hood": 0.3, "doors": ["panel6", "victorian", "ledged"]},
  {"period": "postwar", "weight": 0.2, "window": "modern", "hood": 0.15, "doors": ["modern", "panel6"]}],
 "detached": [{"period": "victorian", "weight": 0.3, "window": "sash", "bay": 0.45, "bay_two": 0.35, "bay_square": 0.2, "hood": 0.3, "doors": ["panel6", "victorian"]},
  {"period": "interwar", "weight": 0.3, "window": "casement", "bay": 0.6, "bay_two": 0.7, "bay_square": 0.35, "hipped": 0.6, "hood": 0.25, "doors": ["victorian", "ledged"]},
  {"period": "postwar", "weight": 0.4, "window": "modern", "bay": 0.08, "bay_square": 1, "hipped": 0.3, "hood": 0.1, "doors": ["modern", "panel6"]}],
 "semi": [{"period": "victorian", "weight": 0.2, "window": "sash", "bay": 0.6, "bay_two": 0.4, "bay_square": 0.1, "hood": 0.3, "doors": ["panel6", "victorian"]},
  {"period": "interwar", "weight": 0.5, "window": "casement", "bay": 0.85, "bay_two": 0.8, "bay_square": 0.25, "hipped": 0.75, "hood": 0.3, "doors": ["victorian", "ledged"]},
  {"period": "postwar", "weight": 0.3, "window": "modern", "bay": 0.05, "bay_square": 1, "hipped": 0.25, "hood": 0.1, "doors": ["modern", "panel6"]}],
 "terrace": [{"period": "victorian", "weight": 0.72, "window": "sash", "bay": 0.5, "bay_two": 0.3, "bay_square": 0.1, "hood": 0.15, "doors": ["victorian", "panel6"]},
  {"period": "georgian", "weight": 0.08, "window": "sash", "bay": 0, "hood": 0.5, "doors": ["panel6"]},
  {"period": "interwar", "weight": 0.08, "window": "casement", "bay": 0.4, "bay_two": 0.6, "bay_square": 0.4, "hood": 0.3, "doors": ["victorian", "ledged"]},
  {"period": "modern", "weight": 0.12, "window": "modern", "bay": 0, "hood": 0.1, "doors": ["modern"]}],
 "tenement": [{"period": "victorian", "weight": 1, "window": "sash", "bay": 0, "hood": 0, "doors": ["panel6", "victorian"]}]
})";
} // namespace

const HousePeriod* HouseTypes::Pick(int Kind, double Chance) const
{
	if (Kind < 0 || Kind >= Kinds || Periods[Kind].empty()) return nullptr;
	double Sum = 0.0;
	for (const HousePeriod& P : Periods[Kind]) Sum += P.Weight;
	double At = std::clamp(Chance, 0.0, 0.999999) * Sum;
	for (const HousePeriod& P : Periods[Kind])
	{
		if (At < P.Weight) return &P;
		At -= P.Weight;
	}
	return &Periods[Kind].back();
}

bool SetHouseTypes(const std::string& Text)
{
	auto Table = std::make_shared<HouseTypes>();
	ReadHouseTypes(BuiltInHouseTypes, *Table);
	HouseTypes Read;
	if (!ReadHouseTypes(Text, Read)) return false;
	for (int K = 0; K < HouseTypes::Kinds; ++K)
		if (!Read.Periods[K].empty()) Table->Periods[K] = std::move(Read.Periods[K]);
	std::lock_guard<std::mutex> Hold(HouseTypeLock);
	HouseTypeTable() = std::move(Table);
	return true;
}

std::shared_ptr<const HouseTypes> HouseTypesNow()
{
	std::lock_guard<std::mutex> Hold(HouseTypeLock);
	if (!HouseTypeTable())
	{
		auto Table = std::make_shared<HouseTypes>();
		ReadHouseTypes(BuiltInHouseTypes, *Table);
		HouseTypeTable() = std::move(Table);
	}
	return HouseTypeTable();
}

namespace
{
// An entry's walls and roofs, their colours by name, its soot and its paint.
void ReadLook(const TorqueFit::Json& J, TownEntry& T)
{
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
	T.Soot = float(std::clamp(J.Num("soot", -1.0), -1.0, 1.0));
	T.Paint = float(std::clamp(J.Num("paint", -1.0), -1.0, 1.0));
}

// A table of town entries (a town's look, or a materials atlas area) under the key List.
bool ReadEntries(const std::string& Text, const char* List, std::vector<TownEntry>& Out)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const TorqueFit::Json* Rows = Root.Get(List);
	if (Rows == nullptr || !Rows->IsArray()) return false;
	for (const TorqueFit::Json& J : Rows->A)
	{
		TownEntry T;
		T.Name = J.Str("name");
		const TorqueFit::Json* At = J.Get("at");
		if (At == nullptr || !At->IsArray() || At->A.size() < 2) continue;
		T.Lat = At->A[0].N;
		T.Lon = At->A[1].N;
		T.Km = J.Num("km", 1.5);
		ReadLook(J, T);
		Out.push_back(std::move(T));
	}
	return true;
}

std::mutex AtlasLock;
std::vector<TownEntry>& Atlas()
{
	static std::vector<TownEntry> List;
	return List;
}
} // namespace

// [towns stage 8] The famous streets under "streets": each one's line and reach, its look as a town's
// (ReadEntries' words), its colours by "#rrggbb" and its setts, shops and lamps.
static void ReadStreets(const std::string& Text, std::vector<StreetEntry>& Out)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return;
	const TorqueFit::Json* Rows = Root.Get("streets");
	if (Rows == nullptr || !Rows->IsArray()) return;
	for (const TorqueFit::Json& J : Rows->A)
	{
		StreetEntry S;
		S.Name = J.Str("name");
		if (const TorqueFit::Json* Lines = J.Get("lines"); Lines != nullptr && Lines->IsArray())
			for (const TorqueFit::Json& L : Lines->A)
			{
				std::vector<std::pair<double, double>> Line;
				if (L.IsArray())
					for (const TorqueFit::Json& P : L.A)
						if (P.IsArray() && P.A.size() >= 2) Line.emplace_back(P.A[0].N, P.A[1].N);
				if (Line.size() >= 2) S.Lines.push_back(std::move(Line));
			}
		if (S.Lines.empty()) continue;
		S.Reach = std::clamp(J.Num("reach", 30.0), 5.0, 200.0);
		ReadLook(J, S.Look);
		S.Look.Name = S.Name;
		S.Look.Lat = S.Lines[0][0].first;
		S.Look.Lon = S.Lines[0][0].second;
		S.HasRender = HexTint(J.Str("render"), S.Render);
		for (const char* Key : {"washes", "doors"})
			if (const TorqueFit::Json* Cs = J.Get(Key); Cs != nullptr && Cs->IsArray())
				for (const TorqueFit::Json& C : Cs->A)
				{
					Tint T;
					if (C.IsString() && HexTint(C.S, T)) (Key[0] == 'w' ? S.Washes : S.Doors).push_back(T);
				}
		S.Setts = J.Flag("setts");
		S.Shops = J.Flag("shops");
		S.Lamps = J.Flag("lamps");
		Out.push_back(std::move(S));
	}
}

bool SetTownTable(const std::string& Text)
{
	std::vector<TownEntry> List;
	if (!ReadEntries(Text, "towns", List)) return false;
	std::vector<StreetEntry> StreetList;
	ReadStreets(Text, StreetList);
	std::lock_guard<std::mutex> Hold(TownLock);
	Towns() = std::move(List);
	Streets() = std::move(StreetList);
	return true;
}

std::vector<StreetEntry> StreetTable()
{
	std::lock_guard<std::mutex> Hold(TownLock);
	return Streets();
}

bool HexTint(const std::string& Hex, Tint& Out)
{
	if (Hex.size() != 7 || Hex[0] != '#') return false;
	float C[3];
	for (int K = 0; K < 3; ++K)
	{
		int V = 0;
		for (int D = 0; D < 2; ++D)
		{
			const char Ch = Hex[size_t(1 + K * 2 + D)];
			const int Digit = Ch >= '0' && Ch <= '9' ? Ch - '0' : Ch >= 'a' && Ch <= 'f' ? Ch - 'a' + 10 : Ch >= 'A' && Ch <= 'F' ? Ch - 'A' + 10 : -1;
			if (Digit < 0) return false;
			V = V * 16 + Digit;
		}
		const double S = V / 255.0;
		C[K] = float(S <= 0.04045 ? S / 12.92 : std::pow((S + 0.055) / 1.055, 2.4));
	}
	Out = {C[0], C[1], C[2]};
	return true;
}

LocalLook StreetLook(const LocalLook& Under, const StreetEntry& Street)
{
	LocalLook L = TownLook(Under, Street.Look);
	L.Street = true;
	L.Shops = Street.Shops;
	if (Street.HasRender) L.RenderColour = Street.Render;
	L.PaintAnywhere = Street.Look.Paint > 0.0f;
	L.WashCount = int(std::min<size_t>(Street.Washes.size(), 6));
	for (int K = 0; K < L.WashCount; ++K) L.Washes[K] = Street.Washes[size_t(K)];
	for (size_t K = 0; K < 4 && !Street.Doors.empty(); ++K) L.Doors[K] = Street.Doors[K % Street.Doors.size()];
	return L;
}

bool SetMaterialsAtlas(const std::string& Text)
{
	std::vector<TownEntry> List;
	if (!ReadEntries(Text, "areas", List)) return false;
	std::lock_guard<std::mutex> Hold(AtlasLock);
	Atlas() = std::move(List);
	return true;
}

std::vector<TownEntry> MaterialsAtlas()
{
	std::lock_guard<std::mutex> Hold(AtlasLock);
	return Atlas();
}

LocalLook AreaLook(double Lat, double Lon, std::string* Area)
{
	const LocalLook& Region = LookAt(Lat, Lon);
	// (the smallest area that holds the place: a town's own stone inside a county's)
	const TownEntry* Best = nullptr;
	double BestKm = 1e9;
	std::lock_guard<std::mutex> Hold(AtlasLock);
	for (const TownEntry& T : Atlas())
	{
		const double Dn = (Lat - T.Lat) * 111.2, De = (Lon - T.Lon) * 111.2 * std::cos(Lat * 0.0174533);
		if (Dn * Dn + De * De > T.Km * T.Km || T.Km >= BestKm) continue;
		Best = &T;
		BestKm = T.Km;
	}
	if (Best == nullptr) return Region;
	if (Area) *Area = Best->Name;
	LocalLook L = TownLook(Region, *Best);
	L.From = LocalLook::FromAtlas;
	return L;
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
	if (Town.Soot >= 0.0f) L.Soot = Town.Soot;
	if (Town.Paint >= 0.0f) L.Paint = Town.Paint;
	L.From = LocalLook::FromTable;
	return L;
}

const LocalLook& WorldLook()
{
	// (pale render on most houses, the rest grey stone; red-brown clay tiles; walls of field stone)
	static const LocalLook World = Make("world", LocalLook::Wall, Gritstone, Granite, 0.70f, 0.05f, 0.10f, 0.70f, 0.0f, Cream, RedBrick, Pantile);
	return World;
}
} // namespace TorqueWorld
