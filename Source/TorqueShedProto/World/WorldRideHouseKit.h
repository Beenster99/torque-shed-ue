// [house kit] Ride UK's detailed detached houses (beenster Nov 26: "Can we build these with blender and use as
// some of the houses", four sheets of house packs as the look to aim for). Our own designs of British houses,
// modelled whole by script (Tools/blender_house_kit.py, the designs in Tools/house_kit_designs.py) and written
// as Data/RideUK/house_kit.bin (the pieces, in the warehouse kit's format) and house_kit.json (each design's
// footprint, its pieces, its parts' roles and its colourways). Three sets: estate (brick new-builds), villa
// (Edwardian and inter-war) and render (rendered, painted and weatherboarded).
//
// A detached house the map draws whose footprint one of the designs fits (within about a sixth each way) is
// given that design, fronting its road, in place of the drawn box: the set by when the street was built and
// the place's walls, the colourway by the place's brick, a street's houses mostly alike in set and colour, each
// its own design. Every part is tagged with how far off it stays (0 close only, 1 middle, 2 the far shell), and
// the game draws each design and colourway as one mesh with three levels of detail, shared by every house of
// it (instances), so a street of them costs little. The same houses for every rider (all from the map and a seed).
#pragma once

#include "WorldRideWarehouses.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct RideMesh;

struct HouseDesign
{
	std::string Name, Label, Set;          // Set: "estate", "villa" or "render"
	double L = 0.0, W = 0.0, H = 0.0;      // its footprint along the front by deep, and its height (m)
	int Floors = 2;
	std::vector<std::string> Classes, Regions;
	double Weight = 1.0;
	std::vector<int> Pieces;               // in the kit's list
	std::vector<std::string> Roles;        // each part's role, in the pieces' part order
	// Its colourways: per part, the colour (linear rgb, 0..255) each one gives it; the first the pieces' own.
	std::vector<std::vector<uint8_t>> Colours;
	bool Boards() const { return std::find(Classes.begin(), Classes.end(), "boards") != Classes.end(); }
};

struct HouseKit
{
	std::vector<WarehousePiece> Pieces;
	std::vector<HouseDesign> Designs;
	// A design of Set fitting a footprint Front wide (along its front) by Deep (m), from Seed in [0, 1): those
	// within Fit of its size each way (as a ratio's log), the nearer the likelier; -1 for none. Boards: the
	// weatherboarded ones may be chosen (the coast, Kent and Essex).
	int Pick(const std::string& Set, double Front, double Deep, double Seed, bool Boards, double Fit = 0.17) const;
};

// Reads the kit (house_kit.json and .bin); false if either isn't one. Kept for every ride after (thread safe).
bool SetHouseKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const HouseKit> HouseKitNow();

// While on (the game made each design's meshes: SetHouseInstancing), a house is handed over as an instance
// (RideMesh::KitParts, Kit 1) for the game to draw from its design's own mesh; off (the cloud checker), laid
// into the square's meshes at its finest.
void SetHouseInstancing(bool On);
bool HouseInstancing();

// The look of design D in colourway Cw at level of detail Lod (0 close, 1 middle, 2 far), about its own origin
// (its footprint's middle on the ground, x along its front, z out of it), in Out: plain parts into Out itself,
// masonry Out.Masonry, roofing Out.Roofing, brick Out.Brickwork, render Out.Render, glass Out.Night. Returns
// its triangles.
size_t HouseLookMesh(const HouseKit& Kit, int Design, int Cw, int Lod, RideMesh& Out);

// One house of design D, colourway Cw, its footprint's middle at (X, Z) on its floor Y, Ux, Uz along its front
// (unit, left to right seen from outside), Nx, Nz out of its front; stretched Sx along the front and Sz deep.
// As an instance while instancing is on, else laid into Into: [0] plain, [1] masonry, [2] roofing, [3] brick,
// [4] render, [5] the night's glass. Returns the triangles.
size_t PlaceHouse(const HouseKit& Kit, int Design, int Cw, double X, double Z, double Y, double Ux, double Uz, double Nx, double Nz,
	double Sx, double Sz, RideMesh* const Into[6]);
} // namespace TorqueWorld
