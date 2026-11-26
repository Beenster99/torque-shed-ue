// [warehouses] Ride UK's industrial estates from the warehouse kit (beenster Nov 25-26: "this looks great"):
// eight original designs of UK sheds modelled by script in Blender (Tools/blender_warehouse_kit.py) and cut
// into pieces (Tools/blender_warehouse_kit_export.py: Data/RideUK/warehouse_kit.bin, the pieces, and
// warehouse_kit.json, the designs and what each suits). A big shed the map draws as an oblong outline gets
// the design that suits its class and size, its walls laid bay by bay from the design's patterns and every
// piece stretched to the outline, so one design fits any size of building. Built into the square's own
// meshes (sheet walls and roofs in M_RideRoof, brick in M_RideBrick, concrete in M_RideRender), so it takes
// the game's materials. The same shed for every rider (all from the map and a seed).
#pragma once

#include <cstdint>
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
	size_t Vertices() const { return PartOf.size(); }
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
	int Piece(const std::string& Key) const { auto It = Pieces.find(Key); return It == Pieces.end() ? -1 : It->second; }
};

struct WarehouseKit
{
	std::vector<WarehouseDesign> Designs;
	std::vector<WarehousePiece> Pieces;
	double StretchLo = 0.55, StretchHi = 1.8;
	// The design for a building of the map's Class, Long by Short (m), Seed in [0, 1) its own; -1 if none suits.
	int Pick(const std::string& Class, double Long, double Short, double Seed) const;
};

// A pattern ("OO E D* R") filled out to N bays: the letter before a '*' repeated to fill.
std::string ExpandBays(const std::string& Pattern, int N);

// Reads the designs (JSON) and the pieces (warehouse_kit.bin); false if either isn't one. Kept for every
// ride after (thread safe); until then none (the drawn sheds stay).
bool SetWarehouseKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const WarehouseKit> WarehouseKitNow();
// [everyday kits stage 4] The same reading for another kit in this format (the farm kit); null if it isn't one.
std::shared_ptr<WarehouseKit> ReadWarehouseKit(const std::string& Json, const std::string& Bin);

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
} // namespace TorqueWorld
