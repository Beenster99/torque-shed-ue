// The real world's grid and frames (Ride UK stage 1), plain C++ shared by the game and SimTest's
// world_test: a port of the Godot game's maths in scripts/real_region.gd, real_terrain.gd and
// height_tiles.gd, so both games read the same world store the same way.
//
// - A frame is the baker's flat one: metres from a point (Lat0, Lon0), +x east, +z south, a degree's
//   metres taken at Lat0 (tools/route_bake.py Frame). A journey has its own, at its first pin.
// - A cell is a 0.25 degree square of the world's grid, (row up from the south pole, column east
//   from 180 W), named "ROW_COL": the unit the world store keeps.
// - A height tile is a web-mercator map tile (zoom, x, y).
// Everything is in doubles: at zoom 14 a float is a pixel out.
#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace TorqueWorld
{
constexpr double WorldPi = 3.14159265358979323846;
constexpr double CellDeg = 0.25;

struct P2
{
	double X = 0.0, Z = 0.0;
	P2() = default;
	P2(double InX, double InZ) : X(InX), Z(InZ) {}
	P2 operator+(const P2& O) const { return {X + O.X, Z + O.Z}; }
	P2 operator-(const P2& O) const { return {X - O.X, Z - O.Z}; }
	P2 operator*(double S) const { return {X * S, Z * S}; }
	double Length() const { return std::sqrt(X * X + Z * Z); }
	double DistanceTo(const P2& O) const { return (*this - O).Length(); }
};

struct Rect
{
	double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;   // left, top (north), right, bottom
	bool Empty() const { return X1 <= X0 || Z1 <= Z0; }
	void Merge(const Rect& O);
};

struct Geo
{
	double Lat = 0.0, Lon = 0.0;
};

// Metres per degree of latitude and of longitude at a latitude (the WGS84 series).
struct MetresPerDeg
{
	double Lat = 111000.0, Lon = 111000.0;
};
MetresPerDeg MetresPerDegree(double LatDeg);

struct Frame
{
	double Lat0 = 0.0, Lon0 = 0.0;
	MetresPerDeg M;
	Frame() = default;
	Frame(double InLat0, double InLon0) : Lat0(InLat0), Lon0(InLon0), M(MetresPerDegree(InLat0)) {}
	P2 FromGeo(double Lat, double Lon) const { return {(Lon - Lon0) * M.Lon, -(Lat - Lat0) * M.Lat}; }
	Geo ToGeo(double X, double Z) const { return {Lat0 - Z / M.Lat, Lon0 + X / M.Lon}; }
};

struct CellId
{
	int Row = 0, Col = 0;
	bool operator==(const CellId& O) const { return Row == O.Row && Col == O.Col; }
	bool operator!=(const CellId& O) const { return !(*this == O); }
	bool operator<(const CellId& O) const { return Row != O.Row ? Row < O.Row : Col < O.Col; }
	std::string Name() const { return std::to_string(Row) + "_" + std::to_string(Col); }   // "ROW_COL"
	bool Valid() const { return Row >= 0 && Row < int(180.0 / CellDeg) && Col >= 0 && Col < int(360.0 / CellDeg); }
};
CellId CellOf(double Lat, double Lon);
Geo CellMiddle(CellId C);
// The cells a box of degrees touches.
std::vector<CellId> CellsInBox(double South, double West, double North, double East);
// "ROW_COL" read back; false for junk.
bool ParseCell(const std::string& Name, CellId& Out);

struct TileKey
{
	int Zoom = -1, X = -1, Y = -1;
	bool operator==(const TileKey& O) const { return Zoom == O.Zoom && X == O.X && Y == O.Y; }
	bool operator!=(const TileKey& O) const { return !(*this == O); }
	bool operator<(const TileKey& O) const { return Zoom != O.Zoom ? Zoom < O.Zoom : (X != O.X ? X < O.X : Y < O.Y); }
	bool Valid() const { return Zoom >= 0; }
	std::string Rel() const { return std::to_string(Zoom) + "/" + std::to_string(X) + "/" + std::to_string(Y); }
};

// The fractional position across (down) the world's grid of tiles at a zoom; the tile is its floor.
double MercatorX(double Lon, int Zoom);
double MercatorY(double Lat, int Zoom);
// The latitude of the top of (fractional) row Y at a zoom: MercatorY's inverse.
double LatOfRow(double Y, int Zoom);
// The smallest zoom whose pixels are no coarser than Spacing metres at a latitude.
int ZoomFor(double Lat, double Spacing, int Size = 512);

struct GeoBox
{
	double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
};
GeoBox TileBox(const TileKey& K);
// The cells a tile lies over.
std::vector<CellId> CellsUnder(const TileKey& K);
} // namespace TorqueWorld
