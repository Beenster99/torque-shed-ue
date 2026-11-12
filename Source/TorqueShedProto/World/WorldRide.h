// A ride on the real world's bare ground (Ride UK stage 2): the way by road between two places,
// its road given a smooth height of its own, the ground with the road's bench cut into it, and both
// made into meshes a piece at a time, so the game can lay out only what is round the riders.
//
// Everything is in the area's frame, as the bike's sim wants it: metres, x east, y up, z south.
// Plain C++ and the same on every machine: two riders who plan the same ride get the same road to
// the last bit, since it is made from nothing but the cells and the height tiles.
//
// The road is made as the Godot game's route planner makes one (stage 3; WorldRoadLine.h): the quick
// way by road (main roads cost less a metre), its line with every mapped corner rounded and faired
// by the road's class so no bend is tighter than a bike can take, as wide as its class, its height
// the ground's along its middle, smoothed and no steeper than 15%. It has sloping shoulders that
// run down into the ground, edge lines on the wider roads and a dashed centre line where there are
// two lanes.
//
// The other roads near the ride are there too (Sides): every road within a few hundred metres of
// it, each from junction to junction, smoothed and as wide as its class, its height the ground's
// but the ride's own where it meets the ride's road, so a side road runs off the tarmac at a
// junction. Each mouth has its corners rounded with tarmac (about 6 m, more on a sharp corner), the
// ride's road's edge line broken across it, and where the side road is the lesser the UK's give-way
// marking across its near lane: two broken lines. (The Godot game's road_net.gd shapes junctions
// with far more care; this is the port's own plain kind. Where two side roads meet is still plain.)
//
// Where the ride's road crosses one of the map's bridges (the region's spans, matched as the Godot
// game matches them) it runs straight from one end to the other as a deck with parapets, and the
// land under it is left as it lies. The other roads' bridges are decks too, and where one crosses
// over or under the ride's road each keeps its own height.
//
// Through a mapped tunnel the road is levelled the same way and runs in a tube as the Godot game's
// does (tiled kerbs, concrete walls, a sooty arch: AdventureTunnels), with a headwall at each mouth.
// The land's own mesh has the road's cutting through the hill as anywhere else; the road's piece
// lays the hill back over it as a lid (the ground as it would lie with no tunnel there), so the
// land's squares never have to stand upright at a mouth. No lamps yet: it is dark but for the bike's.
//
// The roads are crowned on the straight and banked in the bends by the UK's design rule
// (RoadLine::BankOf: the Godot game's are flat across, so this is the UE port's own), the ground's
// bench, the shoulders and a bridge's parapets following the tilted surface.
#pragma once

#include "WorldLoad.h"
#include "WorldRideStyle.h"
#include "WorldRoute.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TorqueWorld
{
struct RoadPoint
{
	P2 At;
	double Along = 0.0;    // metres from the start
	double Height = 0.0;   // the road's own
	double Half = 2.3;     // half the carriageway's width here
	bool Bridge = false;   // on a bridge: the road is a deck over the land as it lies
	bool Tunnel = false;   // in a tunnel: the road runs in a tube with the hill laid back over it
	double Bank = 0.0;     // how it is tilted across (RoadLine::CrossHeight), + the left side the higher
};

// One plant or tree on a square of ground (WorldRidePlants.cpp), for the game to draw with the
// hand-built ride's own meshes: Kind is RidePlan::PlantName's, the place is about the square's origin
// like its mesh's points, Normal the ground's tilt (x, z; 0, 0 upright), Height a tree's in metres
// (0 for the grass, which keeps Scale), Pick which of the kind's models (0..1), Far a tree far enough
// off for the far models.
struct RidePlant
{
	uint16_t Kind = 0;
	bool Far = false;
	float X = 0.0f, Y = 0.0f, Z = 0.0f;
	float Yaw = 0.0f, Scale = 1.0f, Nx = 0.0f, Nz = 0.0f, Height = 0.0f, Pick = 0.0f;
};

// A mesh in the ride's frame: positions about (OriginX, 0, OriginZ), so a float holds them finely.
struct RideMesh
{
	double OriginX = 0.0, OriginZ = 0.0;
	std::vector<float> Pos, Nrm;   // x, y, z each
	std::vector<float> Uv;         // u, v each: the ground's metres across the world over 8; what stands on it, metres of its surface
	std::vector<float> Col;        // r, g, b each (linear), or empty for a mesh of one colour
	std::vector<float> Alpha;      // a each, or empty for 1 everywhere
	std::vector<uint32_t> Idx;     // triangles, wound to face up seen from above with x right and z towards you
	size_t Vertices() const { return Pos.size() / 3; }
	// With a square of ground: what stands on it (walls, buildings, trees), coloured, or null for nothing.
	std::shared_ptr<RideMesh> Extra;
	// With what stands on a square: the parts drawn in the hand-built ride's scanned stone, their
	// vertex colours its masks, not colours (R brightness, 0.5 the scan as it is; G which scan, 0
	// lichened limestone, 1 bare; B lichen; Alpha moss) and their UVs metres of surface. Stone is the
	// dry stone walls (M_Stone), Masonry the stone houses and barns (M_Masonry). Null for none.
	std::shared_ptr<RideMesh> Stone, Masonry;
	// The local stone's colour (WorldRideStyle's LocalLook), for the scanned stone and masonry to be
	// tinted by; and rendered and limewashed walls (M_RideRender: the vertex colour the wash, its alpha
	// dirt at the foot), null for none.
	Tint StoneTint, MasonryTint;
	std::shared_ptr<RideMesh> Render;
	// Words on signs (a village's name, a speed limit): where (in the mesh's own origin, the middle of
	// the words), which way they face (x, z), how tall a letter is (m), the words, and their colour.
	struct Label
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f, Nx = 0.0f, Nz = 1.0f, Size = 0.2f;
		float R = 0.0f, G = 0.0f, B = 0.0f;
		std::string Text;
	};
	std::vector<Label> Labels;
	bool Any() const { return Vertices() > 0 || (Stone && Stone->Vertices() > 0) || (Masonry && Masonry->Vertices() > 0) || (Render && Render->Vertices() > 0); }
	// With a square of ground: its grass and trees (RidePlan::PlantsTile), empty for none.
	std::vector<RidePlant> Plants;
	// With a piece of road: its lamps' glowing faces (in a tunnel), their colour the light's, or null;
	// and where a real light goes for them, x y z each (in the piece's own origin), a few to a tunnel.
	std::shared_ptr<RideMesh> Glow;
	std::vector<float> Lights;
};

// What a planned ride is like, for the Route planner to show before it is ridden.
struct RideSummary
{
	double Metres = 0.0;            // to the finish (the last pin)
	double Climb = 0.0, Descent = 0.0;
	double Highest = 0.0, Lowest = 0.0;
	double Twist = 0.0;             // degrees of turning a km (Route::Twistiness)
	double Minutes = 0.0;           // at a steady, legal pace: bends taken at a comfortable lean
	std::vector<Geo> Line;          // the road, a point every 50 m or so, for the map
	double Litres(double PerHundredKm) const { return Metres / 100000.0 * PerHundredKm; }
};

class RidePlan
{
public:
	// Plans the ride: cells fetched and stitched, the way found, the ground fetched, the road laid.
	// Blocks (a worker's job); Say hears each step. Never null: Ok says whether it can be ridden.
	static std::shared_ptr<RidePlan> Make(CellStore& Store, HeightStore& Heights, double LatA, double LonA, double LatB, double LonB,
		const std::function<void(const std::string&)>& Say = nullptr);
	// The same through every pin of a journey, in order, by the roads its style likes (Route::EdgeCosts);
	// the quick way A to B above is the journey of two pins in the "main" style.
	static std::shared_ptr<RidePlan> Make(CellStore& Store, HeightStore& Heights, const RouteRequest& Want,
		const std::function<void(const std::string&)>& Say = nullptr);
	// The cells a journey's roads are read from (Wide: a band of whole cells, for a way that wanders).
	static std::vector<CellId> CellsFor(const RouteRequest& Want, bool Wide);
	static constexpr double CellDeg = 0.25;     // a cell of the world's store, in degrees
	static constexpr int MaxCells = 64;         // the most cells one ride reads (RealRegion.MAX_AREA_CELLS)
	static constexpr double SnapM = 2000.0;     // a pin is taken to the junction nearest it within this
	static constexpr double RunOutM = 600.0;    // the road goes on this far past the last pin
	static constexpr double SpurM = 300.0;      // an out-and-back this long or shorter is left out
	// A step of the way: from a junction along an edge to the next.
	struct RideStep
	{
		int From = 0, Edge = 0, To = 0;
	};
	static std::vector<RideStep> StripSpurs(const Region& G, const std::vector<RideStep>& Steps);
	static std::vector<RideStep> RunOut(const Region& G, const std::vector<RideStep>& Steps, const std::vector<double>& EdgeCost);
	RouteRequest Request;               // the journey as asked for
	std::vector<int> PinNodes;          // each pin's junction
	double RunOutFrom = 0.0;            // how far along the map's roads the last pin is (the run-out is after it)
	double FinishM = 0.0;               // and how far along the road as laid

	bool Ok = false;
	std::string Why;
	std::string Title;                  // what the ride is called on the screen ("" for the roads' own names)
	LoadedArea Area;
	std::shared_ptr<Terrain> Land;      // the ride's own view of the ground
	std::vector<RoadPoint> Road;        // the ride's own road, a point every 4 m or closer
	// Where a side road's end runs out onto the ride's road: the junction's mouth.
	struct Mouth
	{
		bool Is = false;
		P2 At;                 // on the side road's middle, at the ride's road's edge
		P2 Away;               // the side road's heading there, off from the ride's road
		P2 Across, Along;      // the ride's road there: square off it towards the side road, and along it
		int MainIndex = -1;    // the ride's road's point there
		double MainHalf = 0.0;
		bool GiveWay = false;  // the side road is the lesser: give-way lines across it
		double From = 0.0;     // how far along the side road from its end At is: nearer the end it lies on the ride's road
		double Clear[2] = {0.0, 0.0};   // and how far along its left (as Away heads) and right edges are clear of the rounded corners
	};
	struct SideRoad
	{
		std::vector<RoadPoint> Pts;
		double Length = 0.0;
		Mouth Mouths[2];       // at its first point's end and its last's
	};
	std::vector<SideRoad> Sides;        // the other roads near it, each from junction to junction
	// A junction off the ride's road's sides: two or more road ends at one place (side roads meeting each
	// other, or the ride's road's own start or end), none of them a mouth. Its tarmac is a fan over
	// them all, with rounded corners between neighbouring roads and a round outside where a road bends.
	struct KnotArm
	{
		int Which = 0, End = 0;   // the road and which end of it
		P2 Away;                  // heading out along it from the junction
		double Half = 0.0;
	};
	struct Knot
	{
		P2 At;
		double Height = 0.0;
		std::vector<KnotArm> Arms;   // round the junction, anticlockwise seen from above
		int Owner = 0, OwnerEnd = 0; // drawn with this road's piece at this end
	};
	std::vector<Knot> Knots;
	// A knot's fan, its corners' shoulders, into Out (in Out's own origin).
	void KnotPiece(const Knot& K, RideMesh& Out) const;
	// Every road: 0 is the ride's own, then the sides.
	int RoadCount() const { return 1 + int(Sides.size()); }
	const std::vector<RoadPoint>& RoadOf(int Which) const { return Which <= 0 ? Road : Sides[size_t(Which) - 1].Pts; }
	static constexpr double SideReach = 300.0;  // roads this near the ride's (about) are drawn
	std::vector<std::string> Names;     // the roads taken, each once, in order
	double Length = 0.0;
	double LatA = 0.0, LonA = 0.0, LatB = 0.0, LonB = 0.0;
	int TilesFailed = 0;

	static constexpr double Bench = 4.0;        // level ground kept each side of the road
	static constexpr double Blend = 12.0;       // and the bank from there back to the land as it lies
	static constexpr double Sink = 0.1;         // how far under the road's surface the ground under it lies
	// The road's shoulder: nearly level out to ShoulderFlat (ShoulderDip down, still clear of the
	// ground), then steeply down into the ground by Shoulder. The grass meets it on the steep part, so
	// the line between them stays put though the land's mesh is only a point every 4 m.
	static constexpr double Shoulder = 1.3, ShoulderDrop = 0.4, ShoulderFlat = 0.8, ShoulderDip = 0.03;
	// Where a side road joins a greater one: how far under its surface it sinks, and how far out from
	// its edge it leans into it.
	static constexpr double JoinSink = 0.02, JoinLean = 4.0;
	// Past the edge on the side the road falls to, the ground goes on falling this far (then level):
	// were it level from the edge, the land's mesh, straight between its points, would stand over the
	// tarmac's edge on the low side of a banked bend.
	static constexpr double FallOn = 2.0;
	double WidestHalf = 2.3;                    // of the whole ride
	int Bridges = 0, Tunnels = 0;               // those the ride's road crosses
	int SideBridges = 0, SideTunnels = 0;       // and those on the roads beside it
	// A tunnel's tube: its walls this far out past the road's edge, this high, and the arch over them.
	static constexpr double TubeSide = 0.85, TubeWall = 4.2, TubeArch = 2.4;
	static constexpr int TubeArchSegs = 6;
	// A tunnel's lamps: a pair high on the arch every LampStep metres, a real light every LightStep.
	static constexpr double LampStep = 10.0, LightStep = 30.0;
	static constexpr double LidReach = 21.0;    // the hill's lid over a tunnel: this far out past the road's edge
	static constexpr double BridgeReach = 10.0; // a span's ends are matched to the road within this

	// The bare ground at (X, Z), the road's bench cut in; NaN where the world has none. Coarse is how
	// far apart (m) the asker's points are: a mesh coarser than 4 m has the bench wider and a little
	// deeper, so its straight edges between points never stand over the road. (The coarsest, far-off
	// meshes have no cutting over a tunnel: the hill as it lies.)
	double Ground(double X, double Z, double Coarse = 0.0) const;
	// The road's surface at (X, Z), crown and bank and all, if it is on a road, else NaN.
	double RoadHeight(double X, double Z) const;
	// The road nearest (X, Z), any of them, within 40 m of its middle (nearest by how far outside its
	// edge the point is): how far from its middle, its height, which point, its half width, which
	// road. MainOnly looks at the ride's own road alone. Cross is how far the road's surface is above
	// its middle's Height at the point (the edge's, past an edge). False if none.
	bool Nearest(double X, double Z, double& Dist, double& Height, int* Index = nullptr, double* Half = nullptr, int* Which = nullptr, bool MainOnly = false, bool* OnBridge = nullptr,
		double* Cross = nullptr) const;
	// The same with more say in it and more said back.
	struct NearOpts
	{
		bool MainOnly = false;   // the ride's own road alone
		bool NoDecks = false;    // not the bits of road on a bridge
		bool NoBores = false;    // nor those in a tunnel
		int NotRoad = -1;        // nor any of this road
		int OnlyRoad = -1;       // this road alone
	};
	struct NearHit
	{
		double Dist = 0.0, Height = 0.0, Half = 0.0, Cross = 0.0;
		int Index = -1, Which = 0;
		bool Deck = false, Bore = false;
		P2 Foot;                 // the point of the road's middle nearest
		double Fall = 0.0;       // how steeply the road's surface falls outwards at its edge on the point's side (0 if it rises)
	};
	bool NearestOf(double X, double Z, const NearOpts& Opts, NearHit& Hit) const;
	// A side road's surface height Y at a point, settled onto a greater road it lies on or joins there.
	double JoinedHeight(int Which, const P2& At, double Y) const;
	// The ground as Ground gives it, but with NoBores as if no road ran in a tunnel: the hill over one.
	double GroundOf(double X, double Z, double Coarse, bool NoBores) const;

	// A square of ground, Cells by Cells, from (X0, Z0), with a skirt hanging from its edge (so a finer
	// piece beside a coarser one shows no crack), coloured by what covers it. A square with points 8 m
	// apart or closer has what stands on it too (Out.Extra: SceneryTile).
	void LandTile(double X0, double Z0, double Size, int Cells, RideMesh& Out) const;
	// The ground's colour at a point (r, g, b, linear): what covers it there and Spread metres off each
	// way, bare stone where it is steep (Slope: its rise over its run).
	void LandColour(double X, double Z, double Spread, double Slope, float* Out) const;
	// What stands on a square of ground (WorldRideScenery.cpp): walls, hedges and fences on their
	// mapped lines, buildings on their footprints, stand-in trees under the mapped canopy. Coarse is how
	// far apart the square's points are: past 4 m things are made more plainly.
	void SceneryTile(double X0, double Z0, double Size, double Coarse, RideMesh& Out) const;
	// The grass, rushes, heather and bracken on a square of ground (the finest squares: Coarse 4 m or
	// less) and its trees (squares of 8 m or less, the far models past 4 m), as Tools/make_foliage.py
	// lays Buttertubs' out but from the cells' cover, slope and canopy: carpets of grass near every
	// road, clumps among them, rushes where it is wet, heather on the moor, bracken on the drier
	// slopes, nothing on a road. Mesh is the square's own ground (Cells by Cells from LandTile), so a
	// plant stands on the surface drawn. The same every time for the same square.
	void PlantsTile(double X0, double Z0, double Size, int Cells, const RideMesh& Mesh, std::vector<RidePlant>& Out) const;
	static constexpr int PlantKinds = 30, FirstTree = 18;
	// "carpet_green", ... "bracken" (the hand-built ride's foliage species), then the trees' kinds
	// "ash", "sycamore", "alder", "birch", "hawthorn", "spruce" (Buttertubs' models SM_Tree_<kind>_N,
	// SM_TreeFar_<kind>_N, or scanned ones), "oak", "beech", "willow", "rowan", "pine", "hazel" (scanned
	// ones only; the game draws one of the first six in their place until they are in: PlantStandIn).
	// What a tree kind is drawn as when there are no models of it (itself for the first six).
	static int PlantStandIn(int Kind);
	static const char* PlantName(int Kind);
	// Set before the ground is made: trees come as plants (PlantsTile) and not as stand-in shapes
	// (SceneryTile); how many of the grass's plants are kept (0..1).
	bool RealTrees = false;
	double GrassDensity = 1.0;
	// Road Which from point First to Last (both included): its surface, its shoulders and its paint.
	void RoadPiece(int Which, int First, int Last, RideMesh& Out) const;

	// The route as the bike reads it (Data/Buttertubs/route.json's fields): line_sim, length_m,
	// road_half_width_m, origin_geo, spawn, credits.
	std::string RouteJson() const;
	// The road's furniture (Ride UK stage 5), drawn with what stands on the ground: the village's name
	// and the 30 limit where the ride's road comes into a built-up place and the national limit where
	// it leaves, chevrons round its sharp bends, street lamps through villages and towns, and from the
	// map cattle grids across the road, telegraph poles, post boxes, bus stops, benches, milestones
	// and field gates in their walls. Face is the way a sign faces (towards the traffic that reads it).
	enum EFurnish { NameSign, Limit30, LimitNational, Chevron, StreetLamp, CattleGrid, PostBox, BusStop, PowerPole, FieldGate, Seat, Milestone, FurnishKinds };
	struct Furnish
	{
		int Kind = 0;
		P2 At, Face;
		std::string Text;
		double Size = 0.0;   // a grid's width, a gate's length, a chevron bend's turn (+ left)
		double Height = 0.0; // the ground (or the road) under it
	};
	std::vector<Furnish> Furniture;
	int FurnitureCount(int Kind) const;
	// Where the ride's road is built up (a village or a town round it), by its points.
	std::vector<unsigned char> BuiltUp;
	// The local look (Ride UK stage 5: WorldRideStyle) at a place of the ride's frame.
	const LocalLook& LookHere(double X, double Z) const;
	// The ride to its finish, summed up. Cap is the fastest it is ridden anywhere (m/s; 55 mph, a
	// little under the limit, as a rider on a road they do not know).
	RideSummary Summary(double Cap = 24.6) const;

private:
	void LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves, const std::vector<double>& Speeds);
	// The region's spans of Kind (0 a bridge, 1 a tunnel) the line passes along: first knot, last knot, ...
	std::vector<int> SpansAlong(const std::vector<P2>& Line, int Kind) const;
	// Evens H out across each bridge and tunnel the line passes along (straight from one end to the
	// other) and says what each knot is on: 0 the ground, 1 a bridge, 2 a tunnel. Counts them.
	void LevelSpans(const std::vector<P2>& Line, std::vector<double>& H, std::vector<char>& Kind, int& BridgeCount, int& TunnelCount) const;
	void LaySides(const std::vector<int>& Path);
	void LayKnots();
	// A mouth's corner on the side road's left (Sigma +1) or right (-1): the corner itself, the way
	// along the ride's road's edge from it, and how far along each edge the rounding starts.
	static void MouthCorner(const Mouth& M, const P2& MainAt, double Half, double Sigma, P2& Corner, P2& AlongMain, double& Reach);
	// By the ride's road's point: 1 a mouth or its corners on its left, 2 on its right (no edge line);
	// 4 the mouth itself on its left, 8 on its right (no shoulder).
	std::vector<unsigned char> MouthAt;
	// The scenery's lines and buildings by 256 m square (a line under each square one of its bits' middles is in).
	struct SceneIndex
	{
		std::unordered_map<long long, std::vector<int>> Lines, Buildings;
		std::unordered_map<long long, std::vector<int>> Furniture;   // into RidePlan::Furniture
		std::unordered_map<long long, std::vector<P2>> Gaps;         // gateways the walls leave open
	};
	void IndexFurniture();
	void FurnitureTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out) const;
	bool InGap(double X, double Z) const;
	std::shared_ptr<SceneIndex> Scene;
	void IndexScenery();
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Near;   // (road, point) by 16 m square
};
} // namespace TorqueWorld
