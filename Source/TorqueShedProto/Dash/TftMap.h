// The sat nav's map (beenster's plan, Nov 13 2026): a road map drawn by the game itself from what it
// knows of the land round a ride (its roads by class and name, buildings, water, woods, the places'
// names, fuel and cafes), in the colours of a phone's map app, light by day and dark by night. The
// ride's way is a bold line, the bike an arrow the map turns under (or north kept up).
//
// Plain C++ (SimTest/tft_test draws it): MapData is the land, filled once when a ride is planned and
// then only read; MapScene is one moment's view of it; DrawMap fills a Paint.
//
// Places are in the ride's own flat frame: metres, x east, z south.
#pragma once

#include "TftPaint.h"

#include <string>
#include <vector>

namespace TorqueTft
{
struct MapPt
{
	double X = 0.0, Z = 0.0;
	MapPt() = default;
	MapPt(double InX, double InZ) : X(InX), Z(InZ) {}
};

enum class RoadKind { Motorway, Main, Secondary, Minor, Track, Path, Rail, River, Stream, Count };
enum class AreaKind { Urban, Park, Wood, Water, Building, Count };
enum class LabelKind { Town, Village, Fuel, Cafe, Landmark, Count };

struct MapLine
{
	RoadKind Kind = RoadKind::Minor;
	std::string Name;
	std::vector<MapPt> Pts;
	double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;   // its box (Index fills it)
};
struct MapArea
{
	AreaKind Kind = AreaKind::Building;
	std::vector<MapPt> Tris;                           // three points a triangle
	double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;
};
struct MapLabel
{
	LabelKind Kind = LabelKind::Village;
	std::string Name;
	MapPt At;
};

class MapData
{
public:
	std::vector<MapLine> Lines;
	std::vector<MapArea> Areas;
	std::vector<MapLabel> Labels;

	void AddLine(RoadKind Kind, const std::string& Name, const std::vector<MapPt>& Pts);
	// An area from its outline (any simple shape: cut into triangles here).
	void AddArea(AreaKind Kind, const std::vector<MapPt>& Ring);
	// A building from its middle, size and turn (radians).
	void AddBox(AreaKind Kind, double X, double Z, double W, double D, double Yaw);
	void AddLabel(LabelKind Kind, const std::string& Name, double X, double Z);
	// Call once everything is in: files it all by square, for Near.
	void Index();
	// What has any part within Reach of a place (indices into Lines, Areas, Labels).
	void Near(double X, double Z, double Reach, std::vector<int>& OutLines, std::vector<int>& OutAreas, std::vector<int>& OutLabels) const;
	bool Empty() const { return Lines.empty() && Areas.empty(); }

private:
	static constexpr double Cell = 400.0;
	double GX0 = 0.0, GZ0 = 0.0;
	int GW = 0, GH = 0;
	std::vector<std::vector<int>> LineCells, AreaCells, LabelCells;
	int CellOf(double X, double Z) const;
};

// Another rider on the map (co-op).
struct MapRider
{
	MapPt At;
	Rgba Colour;
	std::string Name;
};

struct MapScene
{
	const MapData* Data = nullptr;
	std::vector<MapPt> Route;       // the ride's way, in order (a point every 20 m or so does)
	MapPt Bike;
	double Heading = 0.0;           // radians, 0 north, clockwise
	double Metres = 600.0;          // ground across the view's width
	bool NorthUp = false;
	bool Night = false;
	bool HasFinish = false;
	MapPt Finish;
	std::vector<MapRider> Riders;
	std::vector<MapPt> Waypoints;
};

// The map in a box of a Paint. BikeDown is how far down the box the bike sits (0.5 the middle; 0.68
// with the heading up, so more of the road ahead shows).
void DrawMap(Paint& P, float X, float Y, float W, float H, const MapScene& Scene, float BikeDown);
// Cuts a simple outline into triangles (ears clipped). False if it is no shape at all.
bool Triangulate(const std::vector<MapPt>& Ring, std::vector<MapPt>& OutTris);
// How much ground a view should span: tight in town and near a turn, wider at speed. Step is the
// rider's own zoom, -2 (closest) .. 2.
double ViewMetres(double SpeedMs, double TurnInMetres, int Step);
} // namespace TorqueTft
