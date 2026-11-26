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
#include <unordered_map>
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
	std::string Gid;         // its Overture segment's short id ("g"), where the cell gave one: the key of its RoadExtra
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
// [OSM stage 4] The ground between the roads (a cell's ground file) and the paths beside them (its paths
// file): each a kind ("lake", "car_park", "footway"...), a word ("soccer" for a pitch, a tree's species, a
// path's surface; "" none), a name where kept, and its shape: an area's outer ring, a line, or one point.
struct GroundShape
{
	std::string Kind, Word, Name;
	std::vector<P2> Pts;
	double Width = 0.0;   // (m: a line's, where the map gives it; 0 none)
};
// [OSM stage 5] What OpenStreetMap says about a road edge close up (a cell's roadx file, by its "g"). 0 is
// "not mapped" throughout: the road builder's rules carry on there. Mapped first, rule second, never both.
struct RoadExtra
{
	enum EFlag
	{
		Roundabout = 1, Lit = 2, Unlit = 4, PaveLeft = 8, PaveRight = 16, PaveNone = 32, Embankment = 64, Cutting = 128,
		Private = 256, BusLane = 512, CycleLane = 1024, TurnLanes = 2048, Building = 4096, Oneway = 8192
	};
	enum EParking { ParkUnknown = 0, ParkNone, ParkBays, NoParking, NoStopping };
	int Lanes = 0, Forward = 0, Backward = 0;   // lane counts, as the segment runs
	double Width = 0.0;                          // recorded width (m)
	int Flags = 0;
	int ParkLeft = 0, ParkRight = 0;             // EParking, as the segment runs
	double MaxHeight = 0.0, MaxWidth = 0.0, MaxWeight = 0.0;   // (m, m, t: shown on the planner, never enforced)
	std::string Half;                            // the other half of a dual carriageway's g, or ""
	bool Has(EFlag F) const { return (Flags & F) != 0; }
};
// A thing on the road at a point: a light, a crossing, a hump. Edge and Along are where it snaps to the
// region's roads (SnapRoadPoints: the nearest edge within RoadPointSnap m; -1 none).
struct MappedPoint
{
	std::string Kind;          // roadx_bake.KINDS: traffic_signals, crossing_zebra, mini_roundabout, hump, ...
	P2 At;
	double Heading = -1.0;     // degrees from north, clockwise, of the traffic it governs; -1 unknown
	int Flags = 0;             // 1 an island, 2 both ways
	int Edge = -1;
	double Along = 0.0;        // metres along the edge from its node A
};

// [places stage 1] A place worth riding to (a cell's places file: tools/places_bake.py): what it is
// (Kind, places_bake.KINDS: stone_circle, church, castle_ruin, dam, ...), its name, its Wikidata id,
// its middle, its outline (a ring, or a line for a dam's crest; empty for a point), its height and
// material where mapped (0, ""), and what the map says more of its kind (Sub: a tower's type, a
// church's denomination, "ruins").
struct Sight
{
	std::string Kind, Name, Qid, Material, Sub;
	P2 At;
	double Lat = 0.0, Lon = 0.0;
	double Height = 0.0;
	std::vector<P2> Outline;
};

// [circuits] A race circuit's pieces as the circuit bake gave them (a cell's circuit file: the world
// repo's scripts/circuit_bake.py; WorldCircuits.cpp): a way of its track (Kind 0 track, 1 pit lane, 2 a
// kart track, 3 loose ground) with its mapped width (m, 0 none) and one-way (1 as drawn, -1 against, 0
// not said); an area round it (Kind 4 its outline, 5 gravel, 6 sand, 7 grass, 8 paved run-off, 9 a
// grandstand); and the circuit itself, its middle and its track's length (m). Circuit: the circuit's id.
struct CircuitWay
{
	std::string Id, Circuit, Name;
	int Kind = 0, Oneway = 0;
	double Width = 0.0;
	std::vector<P2> Line;
};
struct CircuitArea
{
	std::string Id, Circuit, Name;
	int Kind = 0;
	std::vector<P2> Ring;
};
struct CircuitInfo
{
	std::string Id, Name, Kind;
	P2 At;
	double Length = 0.0;
};

struct GroundData
{
	std::vector<GroundShape> Areas, Lines, Points, Paths;
	bool Empty() const { return Areas.empty() && Lines.empty() && Points.empty() && Paths.empty(); }
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
	// [towns stage 1] Whether a landmark's name is a landmark's: not a business's named after a place
	// ("Bradford Leeds Castle Hire"), nor one far off mapped by mistake ("Calangute Beach, Goa").
	static bool LandmarkName(const std::string& Name);
	// For each node the connected piece of road it is in, and how many junctions each piece has.
	std::vector<int> Component, ComponentSize;
	// Its bridges and tunnels, rivers and streams, and scenery, where the store has them (AddSpans,
	// AddStreams, AddFeatures: WorldLoad fills them from each cell's own files).
	std::vector<Span> Spans;
	std::vector<Stream> Streams;
	Features Scenery;
	// [OSM stage 4] Its ground and paths, where the store has them (AddGround, AddPaths).
	GroundData Ground;
	// [everyday kits stage 10] The kits bake's things (a cell's kits file, torque-shed-world scripts/kits_bake.py) the
	// game lays: the coast's for now (beach huts, sea walls, promenades, slipways), as GroundShapes (Kind, Word, its
	// point, line or ring).
	std::vector<GroundShape> KitThings;
	// [OSM stage 5] The road itself, where the store has it (AddRoadX): extras by edge id, points, barriers.
	std::unordered_map<std::string, RoadExtra> Extras;
	std::vector<MappedPoint> RoadPoints;
	std::vector<GroundShape> Barriers;   // (Kind guard_rail or concrete_barrier)
	// [places stage 1] The places worth riding to (AddSights), and finding one: by its Wikidata id first,
	// else the nearest within Within m of (X, Z) that is not a settlement; -1 none. ByQid says which.
	std::vector<Sight> Sights;
	int FindSight(const std::string& Qid, double X, double Z, double Within = 250.0, bool* ByQid = nullptr) const;
	// The extras of an edge, or null where the map says nothing more.
	const RoadExtra* ExtraOf(int Edge) const;
	// Snaps the road points to their nearest edges (after the cells are stitched and the roadx files read).
	void SnapRoadPoints();
	static constexpr double RoadPointSnap = 3.0;
	// [OSM stage 1] Each cell's place, from its place profile: its middle and "North Yorkshire, England".
	struct CellPlace
	{
		double Lat = 0.0, Lon = 0.0;
		std::string Words;
	};
	std::vector<CellPlace> CellPlaces;
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
	// [roundabouts] Every closed ring of roundabout edges made one-way the way traffic goes round it:
	// clockwise seen from above where traffic keeps left (Britain), anticlockwise elsewhere (beenster,
	// Nov 17: "in the UK we ALWAYS ride clockwise"). An edge the map left two-way was routed against
	// the flow. Returns how many edges it set.
	int RingsOneWay();
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
	// The same with what a metre of each edge costs (EdgeCost, one per edge): the Route planner's styles.
	std::vector<int> CheapestPath(int From, int To, const std::vector<double>* EdgeCost) const;
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
	// [OSM stage 4] A ground file and a paths file (a cell's) into the region's frame; another format is left out.
	void AddGround(const TorqueFit::Json& File);
	void AddKits(const TorqueFit::Json& File);   // [everyday kits stage 10]
	void AddPaths(const TorqueFit::Json& File);
	static constexpr int GroundFormat = 1, PathsFormat = 1;
	// [OSM stage 5] A roadx file (a cell's) into the region; another format is left out.
	void AddRoadX(const TorqueFit::Json& File);
	static constexpr int RoadXFormat = 1;
	// [places stage 1] A places file (a cell's) into the region; another format is left out.
	void AddSights(const TorqueFit::Json& File);
	// [circuits] A circuit file (a cell's) into the region, each way, area and circuit once (WorldCircuits.cpp);
	// then the tracks and pit lanes joined into the roads (once, after every cell's): edges of class "raceway"
	// and "pit_lane" from CircuitFirst on, each joined to the roads that reach it (or, where none does, by a
	// paddock road of class "service" to the nearest). Returns how many edges it added.
	void AddCircuits(const TorqueFit::Json& File);
	int JoinCircuits();
	static constexpr int CircuitsFormat = 1;
	std::vector<CircuitInfo> Circuits;
	std::vector<CircuitWay> CircuitWays;
	std::vector<CircuitArea> CircuitAreas;
	int CircuitFirst = -1;                  // the first circuit edge (they are the last), -1 none
	std::vector<int> CircuitOf;             // for each edge from CircuitFirst on: its circuit in Circuits, -1 a paddock road
	static constexpr double TrackWidth = 12.0, PitWidth = 8.0, KartWidth = 8.0;   // (m, where the map gives none)
	// The circuit (in Circuits) whose track or pit lane is nearest (X, Z) within Within m, or -1.
	int CircuitAt(double X, double Z, double Within) const;
	bool CircuitEdge(int Edge) const { return CircuitFirst >= 0 && Edge >= CircuitFirst && Edge < int(Edges.size()) && size_t(Edge - CircuitFirst) < CircuitOf.size() && CircuitOf[size_t(Edge - CircuitFirst)] >= 0; }
	static constexpr int SightsFormat = 1;
	// A tower this tall (m) is a skyscraper, seen from miles off.
	static constexpr double Skyscraper = 100.0;

private:
	void Finish();
	std::map<std::pair<int, int>, std::vector<int>> Grid;
};
} // namespace TorqueWorld
