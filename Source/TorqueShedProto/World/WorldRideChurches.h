// [churches stage 3] Ride UK's churches from the kit (plan "Churches that look like their place",
// approved by beenster Nov 18): forty churches and chapels modelled by script in Blender
// (Tools/blender_church_assemble.py --export: Data/RideUK/church_kit.bin, their triangles, and
// church_kit.json, the rules), one chosen for each place of worship's building by its region, its
// size, and whether the map calls it a chapel, stretched to fit and built into the square's own meshes
// so it takes the game's scanned looks: walls in the place's masonry, brick, render, roofs by their
// roofing, glass and timber plain. The same church for every rider (all from the map and a seed).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct RideMesh;

struct ChurchModel
{
	std::string Name, Label, Kind, Region;   // Kind: "church", "chapel" or "tin" (a tin tabernacle, chapel-like)
	double NaveL = 0.0, NaveW = 0.0;         // its nave's walls (m: along, across), the part fitted to the building
	double MinX = 0.0, MaxX = 0.0, MinZ = 0.0, MaxZ = 0.0, Top = 0.0;   // all of it about the nave's middle
	double Weight = 1.0;
	// A part's section (ChurchKit::ESection) and three bytes: a colour, or the masonry's masks (brightness,
	// which scan, lichen); and its alpha byte (the roofing, moss or dirt).
	struct Part { uint8_t Section = 0, A = 0, B = 0, C = 0, Code = 255; };
	std::vector<Part> Parts;
	std::vector<int16_t> Pos;      // x, y, z each, cm, the props' frame: x east (its altar end), y up, z south
	std::vector<int8_t> Nrm;       // x, y, z each, over 127
	std::vector<uint8_t> PartOf;   // each vertex's part
	std::vector<uint16_t> Idx;     // triangles, facing out by the right hand
	size_t Vertices() const { return PartOf.size(); }
};

struct ChurchKit
{
	enum ESection { Plain, Masonry, Roofing, Brickwork, Render };
	std::vector<ChurchModel> Models;
	std::vector<std::pair<std::string, std::vector<std::string>>> Looks;   // the kit's regions: the places' looks in each
	double TownShare = 0.5, StretchLo = 0.72, StretchHi = 1.35, Spread = 0.18, AreaLo = 55.0, AreaHi = 1500.0;
	std::vector<std::string> ChapelNames, SkipNames;
	// What a name says: 0 a church (or nothing), 1 a chapel, 2 no church (a mosque, a temple...: left as the map's building).
	int NameKind(const std::string& Name) const;
	// The model for a building Long by Short (m) where the look is LookName, in a town or not, a chapel or a
	// church; Seed and Seed2 in [0, 1) the building's own. -1 if none fits within the stretch; else Along and
	// Across, the stretch each way.
	// Whole: fitted by its whole footprint (towers, aisles, porch and all), else by its nave's walls (a
	// building the map cut into pieces, its nave the main one): whichever fits better.
	int Pick(const char* LookName, bool InTown, bool Chapel, double Long, double Short, double Seed, double Seed2, double& Along, double& Across, bool& Whole) const;
};

// Reads the rules (JSON) and the triangles (church_kit.bin); false if either isn't one. Kept for every ride
// after (thread safe); until then none (the old drawn churches stay).
bool SetChurchKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const ChurchKit> ChurchKitNow();

// Builds a model into the square's meshes: its nave's middle (Whole: its whole footprint's) at (X, Z) on the ground at Y, its +x towards
// (Dx, Dz) (a unit vector), stretched Along and Across and Up; the masonry sooted if asked, as the place's old
// stone is; on a slope, on a stone plinth down to Low (its ground's lowest). Out the plain colours, Washed
// render, Houses masonry, Roofs roofing, Bricks brick (as BuildingsTile's).
void BuildChurch(const ChurchModel& M, bool Whole, double X, double Y, double Z, double Low, double Dx, double Dz, double Along, double Across, double Up, bool Sooted,
	RideMesh& Out, RideMesh& Washed, RideMesh& Houses, RideMesh& Roofs, RideMesh& Bricks);
} // namespace TorqueWorld
