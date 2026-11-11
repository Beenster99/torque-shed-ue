// A region of the real world the crew can plan a ride across (Ride UK stage 1): a port of the
// Godot game's scripts/real_region.gd. Every road of an area as a graph, with the buildings, places
// to stop, landmarks and named places beside them, all in the region's own flat frame (metres from
// (FrameLat, FrameLon), +x east, +z south).
//
// The world store keeps the world as 0.25 degree cells, each a small region in a frame at its own
// middle. Stitch joins the cells an area covers into one region: the rider never meets a cell.
// Read-only once made, so one can be shared by every thread.
#pragma once

#include "FitJson.h"
#include "WorldGeo.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct RoadEdge
{
	int A = 0, B = 0;        // its two nodes
	int Class = 0;           // into Region::Classes
	int Surface = 0;         // into Region::Surfaces
	int Limit = 0;           // posted limit, km/h; 0 for none
	int Name = 0;            // into Region::Names
	int Oneway = 0;          // 0 both ways, 1 only from A to B, -1 only from B to A
	double Length = 0.0;     // metres along Line
	std::vector<P2> Line;    // from node A to node B, both ends included
};

struct Building
{
	double X = 0.0, Z = 0.0;   // its middle
	double W = 0.0, D = 0.0;   // across and deep (m)
	double Yaw = 0.0;          // radians
	double Height = 0.0;
	std::vector<double> More;  // the rest of its row as the baker wrote it (its kind and so on)
};

// A bridge or a tunnel, end to end.
struct Span
{
	int Kind = 0;   // 0 a bridge, 1 a tunnel
	P2 A, B;
};

// A river or stream the land cover's water doesn't hold, as a line near the roads.
struct Stream
{
	int Kind = 0;   // 0 a river, 1 a stream, 2 a canal
	std::vector<P2> Line;
};

// Things worth stopping for and what stands in the land (the baker's feature files: 141 kinds, from
// a post box to a viaduct). A number the map says nothing of is NaN.
struct FeaturePoint
{
	std::string Kind, Name, Id;
	P2 At;
	double Height = 0.0, Elevation = 0.0, Heading = 0.0;
};
struct FeatureLine
{
	std::string Kind, Name, Id;
	std::vector<P2> Line;
	double Height = 0.0, Width = 0.0;
	int Flags = 0;
};
struct FeatureArea
{
	std::string Kind, Name, Id;
	std::vector<P2> Ring;        // the outer ring
	double Height = 0.0, Elevation = 0.0;
	// A tower or skyscraper only: its building's id, and what the map says it looks like.
	std::string Group;
	std::vector<std::string> Look;
};
// What the map says a building looks like: its class, roof shape, roof colour, roof material, wall
// material and wall colour, "" where it says nothing.
struct BuildingLook
{
	P2 At;
	std::string Words[6];
};
struct Features
{
	std::vector<FeaturePoint> Points;
	std::vector<FeatureLine> Lines;
	std::vector<FeatureArea> Areas;
	std::vector<BuildingLook> Buildings;
};

class Region
{
public:
	// Cells of the grid the nodes are filed in (m), for finding the nearest.
	static constexpr double GridCell = 250.0;

	std::string Id, Title, Hash;
	std::string Country = "world";   // whose speed-limit rules: "uk", "jp" or "world"
	std::string Theme = "hills";
	bool Sea = false;
	double FrameLat = 0.0, FrameLon = 0.0;
	Rect Bounds;
	std::vector<std::string> Classes, Surfaces, Names, Credits;
	std::vector<P2> Nodes;
	std::vector<RoadEdge> Edges;
	std::vector<std::vector<int>> Adjacent;      // for each node the edges that meet there
	std::vector<std::array<int, 3>> Bans;        // turns the road rules prohibit: (edge arrived by, node, edge left by), sorted
	std::vector<Building> Buildings;
	std::vector<TorqueFit::Json> Pois, Landmarks, Places;   // {kind, name, x, z, ...} as the baker wrote them
	// For each node the connected piece of road it is in, and how many junctions each piece has.
	std::vector<int> Component, ComponentSize;
	// Its bridges and tunnels, rivers and streams, and scenery, where the store has them (AddSpans,
	// AddStreams, AddFeatures: WorldLoad fills them from each cell's own files).
	std::vector<Span> Spans;
	std::vector<Stream> Streams;
	Features Scenery;
	// The cells it was stitched from: their folders and each one's region.json (for cover and more).
	std::vector<std::pair<std::string, TorqueFit::Json>> CellMetas;

	// A road graph from the rows of a roads.json ({nodes, edges, bans, classes, surfaces, names}).
	static bool FromRows(const TorqueFit::Json& Rows, Region& Out);
	// One region of the cells in folders Dirs, in a frame at (Lat0, Lon0). A road that runs from one
	// cell into the next is in both, with the same ids for its ends and its segment, and is joined up
	// by them; a building, place or stop both cells have is kept once. False when no cell is readable.
	static bool Stitch(const std::string& InId, const std::string& InTitle, const std::vector<std::string>& Dirs, double Lat0, double Lon0, Region& Out,
		std::string* Error = nullptr);
	// The named area ("swaledale") from the areas.json beside the cells in any of CellRoots.
	static bool NamedArea(const std::vector<std::string>& CellRoots, const std::string& Name, Region& Out, std::string* Error = nullptr);
	// The folder of a cell in the first of CellRoots that has it, or "".
	static std::string CellDir(const std::vector<std::string>& CellRoots, CellId Cell);

	int OtherEnd(int Edge, int Node) const { return Edges[size_t(Edge)].A == Node ? Edges[size_t(Edge)].B : Edges[size_t(Edge)].A; }
	// Whether an edge may be driven starting from a node (a one-way road only from its open end).
	bool Passable(int Edge, int Node) const;
	bool TurnBanned(int Came, int Node, int Onto) const;
	// The line of an edge as it runs from one of its ends.
	std::vector<P2> LineFrom(int Edge, int Node) const;
	const std::string& ClassOf(int Edge) const { return Classes[size_t(Edges[size_t(Edge)].Class)]; }
	const std::string& SurfaceOf(int Edge) const { return Surfaces[size_t(Edges[size_t(Edge)].Surface)]; }
	// The road's name: its own, or its class's ("Main road").
	std::string NameOf(int Edge) const;
	// The node nearest (X, Z) within Within metres, or -1.
	int NearestNode(double X, double Z, double Within = 500.0) const;
	// The junction nearest (X, Z) that is part of a road network of at least MinNodes junctions (not
	// a stray bit of road), else just the nearest; -1 when there is none.
	int NearestConnected(double X, double Z, double Within = 500.0, int MinNodes = 200) const;
	Frame FrameOf() const { return Frame(FrameLat, FrameLon); }
	// The shortest way by road from one junction to another, as the edges to take in order (one-way
	// streets and banned turns kept to); empty when there is none. A plain shortest path: the crew's
	// route planner (which weighs the kind of road) comes with the Route TV.
	// With ClassCost (what a metre of each of Classes costs), the cheapest way instead: the quick way
	// keeps to main roads (RoadLine::MainCostOf).
	std::vector<int> ShortestPath(int From, int To, const std::vector<double>* ClassCost = nullptr) const;
	// The line of a path from its first junction, every point once.
	std::vector<P2> PathLine(int From, const std::vector<int>& Path) const;

	// Spans [[kind, lat0, lon0, lat1, lon1] in 1e-5 degrees] into the region's frame.
	void AddSpans(const TorqueFit::Json& Rows);
	// Streams [[kind, lat, lon, dlat, dlon, ...] in 1e-5 degrees] into the region's frame.
	void AddStreams(const TorqueFit::Json& Rows);
	// A features file (a cell's) into the region's frame. One of another format is left out.
	void AddFeatures(const TorqueFit::Json& File);
	// The features files this game reads.
	static constexpr int FeaturesFormat = 1;
	// A tower this tall (m) is a skyscraper, seen from miles off.
	static constexpr double Skyscraper = 100.0;

private:
	void Finish();
	std::map<std::pair<int, int>, std::vector<int>> Grid;
};
} // namespace TorqueWorld
