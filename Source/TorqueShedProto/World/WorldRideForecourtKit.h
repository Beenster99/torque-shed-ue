// [forecourts] Ride UK's petrol stations from the forecourt kit (everyday kits stage 5; beenster Nov 26: "These
// look great"): our own canopies, pump islands, kiosk bays, price poles, air and water machines, jet washes and
// forecourt clutter, modelled by script in Blender (Tools/blender_forecourt_kit.py --export:
// Data/RideUK/forecourt_kit.bin, the pieces, and forecourt_kit.json, the made-up liveries). World/WorldRideTownLife.cpp
// lays them where it already places each forecourt's parts, in place of its plain boxes; placing, filling up and the
// garage unit are unchanged. Every part carries the furthest level of detail it stays in (made by hand in Blender,
// never reduced by Unreal) and may take the station's livery or light up after dark only.
// -TorqueForecourts=0 (WORLD_RIDE_FORECOURTS=0 in the tools): the kit isn't read, the plain boxes stay.
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

struct ForecourtPiece
{
	// A part: its section (0 plain, 2 roofing, 3 brick, 4 render), colour, alpha code (the roofing, or the wall's
	// dirt), the furthest level of detail it stays in (0 near only, 2 always) and its slot (0 its own colour, 1 the
	// livery's main colour, 2 its accent, 10 soffit light, 11 shop glow, 12 price digits, 13 lit sign: night only).
	struct Part { uint8_t Section = 0, R = 0, G = 0, B = 0, Code = 255, Lod = 2, Slot = 0; };
	std::vector<Part> Parts;
	std::vector<int16_t> Pos;      // x along the road, y out from its edge, z up, cm
	std::vector<int8_t> Nrm;       // over 127
	std::vector<uint8_t> PartOf;
	std::vector<uint16_t> Idx;
	size_t Vertices() const { return PartOf.size(); }
};

struct ForecourtKit
{
	struct Livery { float Main[3] = {0.5f, 0.5f, 0.5f}, Accent[3] = {0.7f, 0.7f, 0.7f}; };
	std::vector<ForecourtPiece> Pieces;
	std::map<std::string, int> Named;
	std::vector<Livery> Liveries;
	double BrickShare = 0.3;       // the share of stations with the older brick kiosk
	double KioskBay = 2.5;
	int Piece(const std::string& Name) const { auto It = Named.find(Name); return It == Named.end() ? -1 : It->second; }
};

// Reads the kit (JSON and forecourt_kit.bin); false if either isn't one. Kept for every ride after (thread safe).
bool SetForecourtKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const ForecourtKit> ForecourtKitNow();
// [everyday kits stage 5b] Whether the lay-bys and roadside cafes are made (IndexTownLife: the lay-bys' tarmac, burger
// vans, signs, bins and tables; RoadsideProps lays them): on unless -TorqueRoadside=0 / WORLD_RIDE_ROADSIDE=0, set
// before the ride is planned.
void SetRoadsideOn(bool On);
bool RoadsideOn();

// Where a piece goes in a forecourt's frame: its point (Ou along, Ov out) and its plan turned and stretched
// (along = Ou + Xu * x + Yu * y, out = Ov + Xv * x + Yv * y); its z above 0 from Base up, at 0 and below from
// the ground under that point (so feet follow a slope).
struct ForecourtPlace
{
	double Ou = 0.0, Ov = 0.0, Xu = 1.0, Yu = 0.0, Xv = 0.0, Yv = 1.0, Base = 0.0;
};

// The forecourt's frame in the world: its road edge's point, its way along the road and its way out (unit).
struct ForecourtFrame
{
	double Ex = 0.0, Ez = 0.0, Ux = 1.0, Uz = 0.0, Vx = 0.0, Vz = 1.0;
};

// Lays piece P into Out (and its roofing, brick, render and night meshes): Need the least level a part must
// reach to be drawn (0 near: all), Ground(along, out) the ground's height. Returns the triangles laid.
size_t LayForecourtPiece(const ForecourtPiece& P, const ForecourtFrame& F, const ForecourtPlace& At, const std::function<double(double, double)>& Ground,
	const ForecourtKit::Livery& L, int Need, RideMesh& Out);
} // namespace TorqueWorld
