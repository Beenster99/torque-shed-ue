// [warehouses] Ride UK's industrial estates from the warehouse kit (beenster Nov 25-26: "this looks great"):
// eight original designs of UK sheds modelled by script in Blender (Tools/blender_warehouse_kit.py) and cut
// into pieces (Tools/blender_warehouse_kit_export.py: Data/RideUK/warehouse_kit.bin, the pieces, and
// warehouse_kit.json, the designs and what each suits). A big shed the map draws as an oblong outline gets
// the design that suits its class and size, its walls laid bay by bay from the design's patterns and every
// piece stretched to the outline, so one design fits any size of building. Built into the square's own
// meshes (sheet walls and roofs in M_RideRoof, brick in M_RideBrick, concrete in M_RideRender), so it takes
// the game's materials. The same shed for every rider (all from the map and a seed).
// [stage 3, Nov 26: everyday kits plan] Ten designs now (a supermarket and a terrace of shop units for the
// retail parks), every sign board given a made-up name, and the yards dressed round them (DressYard).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct RideMesh;

struct WarehousePiece
{
	// A part: its section (0 plain, 2 roofing, 3 brick, 4 render: ChurchKit::ESection), colour, code (the
	// roofing, or the wall's dirt) and the furthest level of detail it stays in (0 near only, 2 always).
	struct Part { uint8_t Section = 0, R = 0, G = 0, B = 0, Code = 255, Lod = 2; };
	std::vector<Part> Parts;
	std::vector<int16_t> Pos;      // x along its wall, y up, z out of the wall's face, cm
	std::vector<int8_t> Nrm;       // over 127
	std::vector<uint8_t> PartOf;
	std::vector<uint16_t> Idx;
	double MinX = 1e9, MaxX = -1e9;   // (how far along its x it reaches, m)
	size_t Vertices() const { return PartOf.size(); }
};

// [warehouses stage 3] A sign board's face on a piece, in its frame: its middle, the way it faces (x, z), its
// width and height (m), and the kind of made-up name it carries ("shop", "store", "firm", "trade", "estate").
struct WarehouseSign
{
	double X = 0.0, Y = 0.0, Z = 0.0, Fx = 0.0, Fz = 1.0, W = 1.0, H = 0.5;
	std::string Kind;
};

// [warehouses stage 3] A yard's or car park's prop (Tools/blender_estate_props.py): its piece, centred, x along
// it, z towards its front; L long, D deep, H high (m).
struct WarehouseProp
{
	std::string Name;
	int Piece = -1;
	double L = 1.0, D = 1.0, H = 1.0;
	std::vector<WarehouseSign> Signs;
};

struct WarehouseDesign
{
	std::string Name, Label, Roof;   // Roof: "curve", "pitch", "multi" or "flat"
	double L = 0.0, W = 0.0, H = 0.0, Top = 0.0, GableTop = 0.0, Bay = 7.5, GableBay = 6.0;
	int Spans = 1;
	std::string Front, Back, Right, Left;   // the walls' bay patterns
	bool Solar = false, DockFloor = false;
	float Skirt[3] = {0.4f, 0.4f, 0.38f};   // (the plinth's colour, run down to the ground on a slope)
	std::vector<std::string> Classes;
	double ShortLo = 0.0, ShortHi = 1e9, AreaLo = 0.0, AreaHi = 1e9, Ratio = 0.0, Weight = 1.0;
	std::map<std::string, int> Pieces;      // its pieces ("long:D", "roof:L", ...) in the kit's list
	std::map<std::string, std::vector<WarehouseSign>> Signs;   // [stage 3] the pieces' sign faces
	int Piece(const std::string& Key) const { auto It = Pieces.find(Key); return It == Pieces.end() ? -1 : It->second; }
	bool Retail() const;   // [stage 3] a shop's (S, M, U, G bays on its front): a car park before it, not a yard
};

struct WarehouseKit
{
	std::vector<WarehouseDesign> Designs;
	std::vector<WarehousePiece> Pieces;
	std::vector<WarehouseProp> Props;   // [stage 3]
	double StretchLo = 0.55, StretchHi = 1.8;
	bool Yards = true;                  // [stage 3] dress the yards and car parks
	int Prop(const std::string& Name) const;
	// The design for a building of the map's Class, Long by Short (m), Seed in [0, 1) its own; -1 if none suits.
	int Pick(const std::string& Class, double Long, double Short, double Seed) const;
};

// A pattern ("OO E D* R") filled out to N bays: the letter before a '*' repeated to fill.
std::string ExpandBays(const std::string& Pattern, int N);

// Reads the designs (JSON) and the pieces (warehouse_kit.bin); false if either isn't one. Kept for every
// ride after (thread safe); until then none (the drawn sheds stay).
bool SetWarehouseKit(const std::string& Json, const std::string& Bin, bool Yards = true);
std::shared_ptr<const WarehouseKit> WarehouseKitNow();
// [everyday kits stage 4] The same reading for another kit in this format (the farm kit); null if it isn't one.
std::shared_ptr<WarehouseKit> ReadWarehouseKit(const std::string& Json, const std::string& Bin, bool Yards = false);

// [round 175 fps: building the kit's triangles into each square's mesh cost the game thread about 10 ms a frame
// at Swanley] While on, BuildWarehouse and DressYard hand their pieces over as instances (RideMesh::KitParts)
// for the game to draw from each piece's own mesh (WarehousePieceMesh, made once), not laid into the square's
// meshes; the signs' names, the slope skirts and the aprons are still laid. Off (the default): all laid.
void SetWarehouseInstancing(bool On);
bool WarehouseInstancing();
// A piece of the kit as its own mesh (Out's origin its own), its close-up detail (Near) or the middle's: in Out
// and its sections, as BuildWarehouse would lay it. Returns its triangles.
size_t WarehousePieceMesh(const WarehouseKit& Kit, int Piece, bool Near, RideMesh& Out);

// One shed of design D on an oblong: Corner its front wall's left end (seen from outside) on the ground,
// Ux, Uz along that front wall (unit), Nx, Nz out of it (unit); Long by Short (m); its floor at Y, the ground's
// lowest at Low. Detail 2 everything, 1 the middle level, 0 and less the far shell. Into Out and its sections.
// Returns the triangles laid.
size_t BuildWarehouse(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, double Y, double Low, int Detail, RideMesh& Out);
// [everyday kits stage 4] The same, each section into its own mesh: Into[0] plain, [1] masonry, [2] roofing,
// [3] brick, [4] render (a building's meshes in BuildingsTile, as the church kit's).
size_t BuildWarehouse(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, double Y, double Low, int Detail, RideMesh* const Into[5]);

// [warehouses stage 3] A made-up name for a sign of the given kind ("shop", "store", "firm", "trade", "estate",
// "retailpark"), from two rolls in [0, 1). Common family names and everyday words: no real firm's name.
std::string WarehouseName(const std::string& Kind, double Roll, double Roll2);

// [warehouses stage 3] What stands round a shed of design D laid as BuildWarehouse laid it: lorry trailers at its
// docks, vans by its roller doors, skips, pallets, a container, a gas cage, bins, yard lamps, a palisade fence
// with a sliding gate and the estate's name board before an industrial shed's yard; trolley shelters before a
// supermarket. Clear says whether a point is free ground (no road, building, railway or water); Ground its
// height; RoadDist how far a point is from the nearest road. Only at Detail 1 and 2; each prop only while
// Budget (triangles) lasts. Returns the triangles laid.
size_t DressYard(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, int Detail, const std::function<bool(double, double)>& Clear, const std::function<double(double, double)>& Ground,
	const std::function<double(double, double)>& RoadDist, long long& Budget, RideMesh& Out);
} // namespace TorqueWorld
