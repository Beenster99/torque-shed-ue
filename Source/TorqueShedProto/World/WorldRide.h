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
#include <functional>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>

namespace TorqueWorld
{
namespace Ferries { struct Layout; }   // WorldFerries.h

// (beenster, build 147: walls "very low def", their top stones smeared. The game draws these UVs at half
// precision, a quarter metre apart at 300 m up a fell and half a metre 600 m along a square: each face's
// metres of UV start again within UvRepeat of nought. 18 m is a whole number of every scan's repeat on
// stone, masonry, moss and brick, so the faces still meet but where a face crosses a multiple of it.)
constexpr double UvRepeat = 18.0;
inline double UvShift(double At) { return std::floor(At / UvRepeat) * UvRepeat; }

struct RoadPoint
{
	P2 At;
	double Along = 0.0;    // metres from the start
	double Height = 0.0;   // the road's own
	double Half = 2.3;     // half the carriageway's width here
	bool Bridge = false;   // on a bridge: the road is a deck over the land as it lies
	bool Tunnel = false;   // in a tunnel: the road runs in a tube with the hill laid back over it
	double Bank = 0.0;     // how it is tilted across (RoadLine::CrossHeight), + the left side the higher
	int Lanes = 0;         // [OSM stage 5] mapped lanes across the whole road (0: the rule's; RoadX)
	bool OneWay = false;   // [OSM stage 5] no centre line
	bool Dual = false;     // [OSM stage 5C] one half of a dual carriageway: a central reservation on its offside
	unsigned char Banked = 0;   // [OSM stage 5E] the map's embankment (1) or cutting (2) under it: a steeper, cleaner bank
	unsigned char Park = 0;     // [OSM stage 5D] the map's parking as the road runs: left in bits 0-1, right 2-3 (RoadX::Across)
	double Shoulder = 0.0; // [OSM stage 5C] a motorway's hard shoulder on its nearside (m, within Half), 0 none
	signed char Near = 0;  // [OSM stage 5C] its nearside on a one-way road: 1 its left as its points run, -1 its right
};

// [OSM stage 2] One prop on a square of ground (World/WorldRideProps.cpp), for the game to draw with
// a model from the prop kit (Data/RideUK/prop_kit.json; the RideUK level's kit actor tagged RideProps):
// its kind (RidePlan::EProp), which of the kind's models (Variant, by the map's id), where its foot is
// (about the square's origin, like the plants), which way its front (the model's +x) faces (radians:
// along (cos, sin) with x east and z south), its scale, the map's own id, and whether the map put it
// there (Mapped) or a rule did (a street lamp through a village).
struct RideProp
{
	uint16_t Kind = 0;
	uint8_t Variant = 0;
	bool Mapped = true;
	float X = 0.0f, Y = 0.0f, Z = 0.0f, Yaw = 0.0f, Scale = 1.0f;
	uint32_t Id = 0;
	// [fps plan step 2] A facade kit model's three colours (linear RGB: the joinery's paint, the trim, the
	// third: a door's paint or a window's sill), for its material's per-instance data; Facade: has them,
	// and is placed at its model's own size (Variant is the size, not a pick).
	bool Facade = false;
	float Tint[9] = {};
	// [build 154] A model laid along a line (Buttertubs' wall lengths): tipped up its slope by Pitch (radians,
	// its far end up) and drawn Stretch times its own length along it.
	float Pitch = 0.0f, Stretch = 1.0f;
	// [Nov 20] And drawn Across times its own width across that line (a bridge's walls: the field walls thinned)
	float Across = 1.0f;
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

// A farm animal in a field (Ride UK stage 6), in its square's own origin: its kind (RidePlan::
// AnimalName), where its feet are, which way it faces (radians: its front along (cos, sin) with x east
// and z south), its size, and which of its kind's models (0..1: a herd shares one, so one coat).
struct RideAnimal
{
	uint16_t Kind = 0;
	float X = 0.0f, Y = 0.0f, Z = 0.0f, Yaw = 0.0f, Scale = 1.0f, Pick = 0.0f;
};

// What a place sounds like (Ride UK stage 6, RidePlan::SoundsAt), each 0 (none heard) to 1 (all round
// you): the sea's waves, a river or a beck, a town's hum, and sheep and cattle in the fields near by.
struct RidePlace
{
	double Sea = 0.0, Water = 0.0, Town = 0.0, Sheep = 0.0, Cattle = 0.0;
};

// A mesh in the ride's frame: positions about (OriginX, 0, OriginZ), so a float holds them finely.
struct RideMesh
{
	double OriginX = 0.0, OriginZ = 0.0;
	std::vector<float> Pos, Nrm;   // x, y, z each
	std::vector<float> Uv;         // u, v each: the ground's metres across the world over 8; what stands on it, metres of its surface
	std::vector<float> Col;        // r, g, b each (linear), or empty for a mesh of one colour
	std::vector<float> Alpha;      // a each, or empty for 1 everywhere
	// [real water] A second set of UVs (UV1), u, v each, or empty for 0 everywhere. Water: u its depth under
	// the water at the point (m, less than 0 where the point lies under the bank), v a river's flow (m/s) or
	// the sea's tidal range there (m, high to low water halved). Land: ShoreZero less its height over the nearest
	// water's level (0: no water near, read as high above any), v the shore's kind (EShore) times 10 plus that
	// sea's tidal half range.
	std::vector<float> Uv1;
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
	// [OSM stage 3] Pitched and flat roofs (M_RideRoof: the vertex colour the roof's colour, its alpha
	// the roofing, 0 slate, 0.25 stone slates, 0.5 clay tiles, 0.75 thatch, 1 sheet or felt with no
	// courses; UV0 metres along the eaves and up the slope) and brick walls (M_RideBrick: the vertex
	// colour the brick's, its alpha dirt at the foot; UV0 metres along the wall and down it). Null for none.
	std::shared_ptr<RideMesh> Roofing, Brickwork;
	// With a square of ground: its water (Ride UK stage 5: the sea at its level, lakes and wide rivers
	// as the land's cover has them, the map's rivers, streams and canals as ribbons), coloured, never
	// collided with (M_RideWater). Null for none.
	std::shared_ptr<RideMesh> Water;
	// What lights up after dark (Ride UK stage 7: M_RideNight, glowing as MPC_Weather's Night rises):
	// lit windows and street lamps' heads (alpha 0: dark glass by day) with what stands on a square, cat's
	// eyes with a piece of road (alpha 1: pale studs by day, catching the headlight near by at night).
	// The vertex colour is the light's, brighter for more. Null for none.
	std::shared_ptr<RideMesh> Night;
	RideMesh& NightOf()
	{
		if (!Night)
		{
			Night = std::make_shared<RideMesh>();
			Night->OriginX = OriginX;
			Night->OriginZ = OriginZ;
		}
		return *Night;
	}
	// [OSM stage 5D] Traffic lights' and beacons' lamps (M_RideSignal, lit by MPC_Weather's Signals, the
	// shared clock's seconds, so every rider sees the same colour): the vertex colour the lamp's, UV0 x the
	// seconds its cycle starts at and y which lamp (SignalRed, SignalAmber, SignalGreen, SignalBeacon).
	std::shared_ptr<RideMesh> Signals;
	RideMesh& SignalsOf()
	{
		if (!Signals)
		{
			Signals = std::make_shared<RideMesh>();
			Signals->OriginX = OriginX;
			Signals->OriginZ = OriginZ;
		}
		return *Signals;
	}
	// [OSM stage 6A] Road signs' faces and their lettering (M_RideSign: the vertex colour the paint's;
	// it shines back the headlight after dark, as real signs' reflective sheeting does). Null for none.
	std::shared_ptr<RideMesh> Signs;
	RideMesh& SignsOf()
	{
		if (!Signs)
		{
			Signs = std::make_shared<RideMesh>();
			Signs->OriginX = OriginX;
			Signs->OriginZ = OriginZ;
		}
		return *Signs;
	}
	// Words on signs (a village's name, a speed limit): where (in the mesh's own origin, the middle of
	// the words), which way they face (x, z), how tall a letter is (m), the words, and their colour.
	struct Label
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f, Nx = 0.0f, Nz = 1.0f, Size = 0.2f;
		float R = 0.0f, G = 0.0f, B = 0.0f;
		std::string Text;
	};
	std::vector<Label> Labels;
	bool Any() const { return Vertices() > 0 || (Stone && Stone->Vertices() > 0) || (Masonry && Masonry->Vertices() > 0) || (Render && Render->Vertices() > 0) || (Roofing && Roofing->Vertices() > 0) || (Brickwork && Brickwork->Vertices() > 0) || (Night && Night->Vertices() > 0)
		|| (Signals && Signals->Vertices() > 0) || (Signs && Signs->Vertices() > 0); }
	// With a square of ground: its grass and trees (RidePlan::PlantsTile), empty for none.
	std::vector<RidePlant> Plants;
	// With a square of ground: the sheep and cattle in its fields (RidePlan::AnimalsTile).
	std::vector<RideAnimal> Animals;
	// [OSM stage 2] With a square of ground: its props (RidePlan::PropsTile), the kinds the kit has models for.
	std::vector<RideProp> Props;
	// With a piece of road: its lamps' glowing faces (in a tunnel), their colour the light's, or null;
	// and where a real light goes for them, x y z each (in the piece's own origin), a few to a tunnel.
	// With what stands on a fine square: its street lamps' lights, the same way (lit only after dark).
	std::shared_ptr<RideMesh> Glow;
	std::vector<float> Lights;
	// [road edges, Nov 18] With a piece of road: the grass of its shoulders in the country, drawn as the land
	// is (its material, the land's colours as the squares draw them, its UVs the world's metres over 8), so
	// the field runs to the tarmac's worn edge with no strip of another grass between (beenster: "remove
	// this edge and blend the topology of the countryside to the edge of the road"). Null for none.
	std::shared_ptr<RideMesh> Turf;
};

// [OSM stage 2] Plain shapes for what stands by the road and on the skyline, made in code into a
// RideMesh (WorldRideSkyline.cpp): a point, a colour (linear), a quad (seen from both sides, or the side
// its winding gives), a thin bar between two points, an upright round thing (tapered, Sides round, its
// top a cone Cap high or flat), and an outline's inside and area.
namespace RideShapes
{
struct V3
{
	double X = 0.0, Y = 0.0, Z = 0.0;
};
struct Paint
{
	float R = 0.0f, G = 0.0f, B = 0.0f;
};
V3 Pt(double X, double Y, double Z);
void Vert(RideMesh& Out, const V3& Q, const V3& N, const Paint& C, double U, double V);
void SkyQuad(RideMesh& Out, const V3& Q0, const V3& Q1, const V3& Q2, const V3& Q3, const Paint& C, bool Both);
void Strut(RideMesh& Out, const V3& A, const V3& B, double W, const Paint& C);
void Column(RideMesh& Out, const V3& Base, double R0, double R1, double H, int Sides, const Paint& C, double Cap = 0.0);
bool InRing(const std::vector<P2>& Ring, double X, double Z);
double RingArea(const std::vector<P2>& Ring, P2& Mid);   // and its middle
} // namespace RideShapes

// [stage 8] A stretch of a ride where the map's data is thin: where it starts, and why.
struct ThinSpot
{
	double Along = 0.0;
	Geo At;
	std::string Why;
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
	std::vector<ThinSpot> Thin;     // [stage 8] where the map's data is thin, for a warning on the map
	// [OSM stage 1] Where it starts ("North Yorkshire, England") and the stops on the way, for the
	// planner: "2 pubs, 3 cafes, fuel, Middleham Castle" (the castles by name).
	std::string Place, StopsLine;
	double Litres(double PerHundredKm) const { return Metres / 100000.0 * PerHundredKm; }
};

// [places stage 5] The site notes (Data/RideUK/landmarks_uk.json "sites"): the famous places whose facts
// the map doesn't hold (how many stones, how tall, a spire or not), each where it is (lat, lon), the word
// its name holds that finds it among the places within Km, and what to draw. Add puts one in that the
// places file hasn't got. Read once (SetSiteTable, thread safe), kept for every ride after.
struct SiteNote
{
	std::string Name, Match, Kind, Note;
	double Lat = 0.0, Lon = 0.0, Km = 1.0;
	double Radius = 0.0, Tall = 0.0;
	int Stones = 0, Banks = 0, Spire = -1, Banded = -1;
	bool Lintels = false, Fallen = false, Unlit = false, Add = false;
	// [places stage 6] Its own model (Data/RideUK/Sites/<Model>.glb), "" none; turned Turn degrees
	// (clockwise seen from above) from the model's own north (-z); stood exactly at (Lat, Lon), not where
	// the map has the place.
	std::string Model;
	double Turn = 0.0;
};
bool SetSiteTable(const std::string& Text);
std::vector<SiteNote> SiteTable();

// [places stage 6] A famous place's own model (Data/RideUK/Sites/<id>.glb, made by
// Tools/make_ride_sites.py), drawn by PlacesTile in place of its kind's recipe. The glb's frame is the
// ride's: metres, x east, y up, z south, the place's middle at (0, 0, 0) and its ground y = 0, everything
// going some way below it. Each node (one mesh) is a part, drawn with the material its primitive's
// material is named for: "stone" and "masonry" (the scanned stone, its vertex colour the masks as
// RideMesh's Stone), "render" (limewash and paint: colour, alpha dirt), "roof" (colour, alpha roofing),
// "brick" (colour, alpha dirt), "glow" (lit after dark) and anything else coloured. A part whose node
// name holds "#drop" stands on the ground under its own middle (a stone of a circle on a slope), the rest
// with the whole, on the lowest ground under it; "#far" parts are drawn from afar as well.
struct SiteModel
{
	enum ELayer { Painted, Stone, Masonry, Render, Roofing, Brickwork, Glow, Layers };
	struct Part
	{
		int Layer = Painted;
		bool Drop = false, Far = false;
		double MidX = 0.0, MidZ = 0.0, Foot = 0.0;   // its middle across, and its lowest point
		double X0 = 0.0, X1 = 0.0, Z0 = 0.0, Z1 = 0.0;   // its box across
		std::vector<float> Pos, Nrm, Uv, Col, Alpha;  // as RideMesh's (Col r, g, b; Alpha a)
		std::vector<uint32_t> Idx;                    // wound as RideMesh's (the glb's turned round)
	};
	std::string Id;
	std::vector<Part> Parts;
	double Reach = 0.0;   // the farthest any of it is from the middle, across (m)
	double Tall = 0.0;    // its top above y = 0
	size_t Triangles() const;
};
bool ReadSiteModel(const std::string& Bytes, SiteModel& Out, std::string* Why = nullptr);
// The models read (thread safe, read once at start), and the one called Id, null for none.
void SetSiteModels(std::vector<std::shared_ptr<const SiteModel>> Models);
// A part put in Out: its middle (0, 0, 0) at (X, Y, Z), turned Turn (radians, clockwise seen from above).
void PutSitePart(RideMesh& Out, const SiteModel::Part& P, double X, double Y, double Z, double Turn);
std::shared_ptr<const SiteModel> SiteModelOf(const std::string& Id);

struct RideDetour;
struct CircuitIndex;   // [circuits] (WorldRideCircuits.h)

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
	static constexpr double RoamPad = 0.1;      // [ride anywhere] the cells within this (degrees) of the way are read: room to roam
	static constexpr int MaxCells = 64;         // the most cells one ride reads (RealRegion.MAX_AREA_CELLS)
	// [ride anywhere 3] As the rider nears the edge of what was read: the same ride over more of the
	// world, the cells within RoamPad of (X, Z) not read yet added after the old ones (so every road
	// and junction keeps its number, and the ride's road and every road laid stay as they were), its
	// network round (X, Z) laid. Blocks (a worker's job). Null, with why, when there is nothing to add.
	static std::shared_ptr<RidePlan> Grow(CellStore& Store, HeightStore& Heights, const RidePlan& Old, double X, double Z, std::string* Why = nullptr);
	// The cells within Pad (degrees) of (X, Z) not read yet (whether baked or not).
	std::vector<CellId> CellsToGrow(double X, double Z, double Pad) const;
	static constexpr double GrowPad = 0.05;     // a cell this near the rider (degrees: 3 to 5 km) and not read: grow
	static constexpr int MaxRoamCells = 144;    // the most a grown ride holds
	int Grown = 0;                              // how many times this ride has grown
	std::vector<int> RedrawRoads;               // after growing: roads (as RoadOf) whose ends now meet new ones
	std::vector<Rect> GrewFrom;                 // after growing: the old area's cells
	static constexpr double GrowLayM = 3100.0;  // a grown ride's network laid this far round the rider (the game's road reach and more)
	// After growing: whether all of the box (X0, Z0)-(X1, Z1) was in the old area's cells.
	bool WasRead(double X0, double Z0, double X1, double Z1) const;
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
	std::vector<RideStep> MadeSteps;    // the way, run-out and all, on the area's roads
	RouteRequest Request;               // the journey as asked for
	std::vector<int> PinNodes;          // each pin's junction
	double RunOutFrom = 0.0;            // how far along the map's roads the last pin is (the run-out is after it)
	double FinishM = 0.0;               // and how far along the road as laid
	// [ferries stage 5] A journey whose way takes a ferry: the ride's road runs to the boarding slipway's road (its
	// finish there, named for the ferry), and from where it lands the sat nav finds the way on to the pins still to
	// come (FerryOnward). Null without one.
	struct FerryLink
	{
		std::string Id, Name;          // the crossing's (Data/RideUK/ferries.json)
		int BoardEnd = 0;              // the end it's boarded at (0 the list's a)
		P2 Board, Land;                // the junctions nearest its slipways
		std::vector<P2> Pins;          // the junctions of the pins still to come from where it lands, the finish last
		std::vector<std::string> PinNames;
		double OnM = 0.0;              // the way on by road from where it lands (as planned)
	};
	std::shared_ptr<const FerryLink> Ferry;
	// [ferries, build 163 robot] Each listed crossing in the area laid with the plan (World/WorldRideFerry.cpp), so the
	// land is cut down along its slipways' approaches (Corran's ran up to a metre under the hillside, its front wheel
	// on nothing) before any of the land's squares are made; ATorqueFerry stands up the same layout. A crossing whose
	// ground wasn't in yet is laid by ATorqueFerry itself, uncut.
	struct FerryLaid
	{
		int Listed = -1;
		std::shared_ptr<const Ferries::Layout> Lay;
		double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;   // the land it cuts (m)
	};
	std::vector<FerryLaid> FerryLays;
	void IndexFerries();
	std::shared_ptr<const Ferries::Layout> FerryLayOf(int Listed) const;
	double FerryCut(double X, double Z, double Lie) const;
	bool OnFerryWay(double X, double Z, double Margin) const;   // (on a slipway or its approach, or within Margin: no plants)
	static constexpr double FerryCutBelow = 0.12, FerryCutVerge = 2.5, FerryCutBank = 1.5;

	bool Ok = false;
	std::string Why;
	std::string Title;                  // what the ride is called on the screen ("" for the roads' own names)
	LoadedArea Area;
	// [stage 8] Whose rules the ride's roads are under: Britain's own kit (its signs, phone and post
	// boxes, pub signs, cat's eyes, the looks of its regions) or the world's default; and which side
	// the traffic keeps to (the left in Britain and Japan).
	bool Britain() const { return Area.Roads.Country == "uk"; }
	bool KeepLeft() const { return Area.Roads.Country == "uk" || Area.Roads.Country == "jp"; }
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
		bool Ring = false;     // [OSM stage 5B] it is a roundabout's entry: one broken line, not two
		bool Flare = false;    // [roundabouts] at a roundabout drawn as a circle: its corners flared wide
		double From = 0.0;     // how far along the side road from its end At is: nearer the end it lies on the ride's road
		double Clear[2] = {0.0, 0.0};   // and how far along its left (as Away heads) and right edges are clear of the rounded corners
	};
	// [OSM stage 5] A mapped thing on a road (Region::RoadPoints), where it lies along that road: its
	// kind, how far along, how far to the left of the middle, and which way the traffic it governs runs
	// (+1 the road's own way, -1 against it, 0 both or not mapped).
	struct MappedMark
	{
		std::string Kind;
		double Along = 0.0, Off = 0.0;
		int Dir = 0;
		int Flags = 0;
		bool LinesDrawn = false;   // [square T] (its lines painted with the junction's, along the through road's edge: only its triangle here)
		double Slant = 0.0;   // [square T] (a line across: how far further along it lies per metre to the left, to lie along the road it meets)
	};
	struct SideRoad
	{
		std::vector<RoadPoint> Pts;
		double Length = 0.0;
		Mouth Mouths[2];       // at its first point's end and its last's
		bool Track = false, Gravel = false;   // [OSM stage 1] a farm track (no paint); gravel, not tarmac
		bool Setts = false;    // [towns stage 6] laid in setts or cobbles (the map's surface): no paint
		int Edge = -1;         // [ride anywhere] its edge in the area's roads
		std::vector<MappedMark> Mapped;   // [OSM stage 5] its lights, crossings, humps (MapPoints)
	};
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
	// [OSM stage 5B] Two of a knot's roads less than this far apart (radians, about 50 degrees) are a fork:
	// no rounded corner between them.
	static constexpr double OverlapStep = 0.3;   // (GroundOf: two roads' tarmac overlapping less than this apart in height)
	static constexpr double KnotFork = 0.87, KnotNoseMost = 40.0;   // (a fork's tarmac between them reaches this far out at most)
	// A knot's fan, its corners' shoulders, into Out (in Out's own origin).
	void KnotPiece(const Knot& K, RideMesh& Out) const;
	// [ride anywhere] Every road: 0 is the ride's own; every other road of the area (its network) is
	// 1 + its edge in Area.Roads, from junction to junction. Those near the ride's road (SideReach) are
	// laid with the plan (their mouths are on it): PlanSides. Every other one is laid a block of
	// BlockSize metres at a time (LayBlocks), on whichever thread first wants it, the same on every
	// machine whatever order they come in. SideOf is null for a road not laid (yet, or ever: a loop),
	// and RoadOf empty.
	int RoadCount() const { return 1 + int(Area.Roads.Edges.size()); }
	const SideRoad* SideOf(int Which) const;
	const std::vector<RoadPoint>& RoadOf(int Which) const;
	const std::vector<int>& PlanSides() const { return Planned; }
	static constexpr double FarFromRoad = 140.0;   // the livestock keep this far from any road (and 40 m more)
	static constexpr double SideReach = 300.0;  // roads this near the ride's (about) are laid with it
	static constexpr double BlockSize = 512.0;
	static constexpr double TileMargin = 1000.0;   // the fine ground fetched this far round a block
	// The roads with a point in the blocks a box touches, 0 (the ride's own) among them where its box
	// meets this one; with Lay those blocks are laid first (a worker's job), else only roads laid already.
	std::vector<int> RoadsNear(const Rect& Box, bool Lay) const;
	void LayBlocks(const Rect& Box) const;
	bool BlockLaid(double X, double Z) const;
	// [build 150] The same without waiting: false while a worker holds the blocks (laying one), so the game
	// thread never stalls on it; ask again next time.
	bool BlockLaidNow(double X, double Z) const;
	int BlocksLaid() const;
	// [ride anywhere] The ground wherever the riders roam: the fine tiles over a box and a tile round
	// it fetched (a block's are, before its roads are laid, so their heights are final), and round a
	// rider the middle and skyline tiles out to their reach. EnsureGround: how many couldn't be had;
	// GroundRound: how many came. Both block, on the network (a worker's job). The stores are the
	// plan's (Make's), kept for the ride.
	int EnsureGround(const Rect& Box) const;
	int GroundRound(double X, double Z) const;
	CellStore* CellsFrom = nullptr;
	HeightStore* HeightsFrom = nullptr;
	// [LiDAR stage 2] A cell's LiDAR heights file (Tools/lidar_heights.py --file: its outlines' surveyed eaves and
	// ridges by tile and row), false where there is none; asked once per cell, from a worker. Unset, no LiDAR.
	std::function<bool(const std::string& Cell, std::string& Text)> LidarFrom;
	// (a LiDAR file read: by tile, then row, the row's check (its first latitude mod 10000), eaves and ridge; how
	// many rows it kept, none unless it is for Release; and one row's heights, false where unmeasured or moved)
	using LidarTiles = std::unordered_map<std::string, std::unordered_map<int, std::array<float, 3>>>;
	static size_t ReadLidar(const std::string& Text, const std::string& Release, LidarTiles& Out);
	static bool LidarOfRow(const LidarTiles& Tiles, const std::string& Tile, int Row, long long FirstLat, float& Eaves, float& Ridge);
	std::shared_ptr<void> StoresAlive;   // (whatever holds them, kept as long as the plan is)
	// The junction (knot) a road's end is one of the arms of, once its block is laid; null for none.
	const Knot* KnotAt(int Which, int End) const;
	std::vector<Knot> KnotsLaid() const;   // every one so far (the tests)
	std::vector<std::string> Names;     // the roads taken, each once, in order
	double Length = 0.0;
	double LatA = 0.0, LonA = 0.0, LatB = 0.0, LonB = 0.0;
	int TilesFailed = 0;

	static constexpr double Bench = 4.0;        // level ground kept each side of the road
	static constexpr double Blend = 12.0;       // and the bank from there back to the land as it lies
	// [OSM stage 5E] (Blend at least; BankWiden m more for each metre of height it makes up, to BankMost;
	// the map's embankments and cuttings a 1 in BatterRun batter, BatterLeast m at least)
	static constexpr double BankWiden = 0.9, BankMost = 30.0, BatterRun = 2.0, BatterLeast = 4.0;
	static constexpr double NearestReach = 40.0;   // NearestOf finds roads within this of a point (m)
	static constexpr double Sink = 0.1;         // how far under the road's surface the ground under it lies
	static constexpr double FlushSink = 0.045;  // [build 154] and beside it, past the edge: just under the shoulder's flat
	// The road's shoulder: nearly level out to ShoulderFlat (ShoulderDip down, still clear of the
	// ground), then steeply down into the ground by Shoulder. The grass meets it on the steep part, so
	// the line between them stays put though the land's mesh is only a point every 4 m.
	static constexpr double Shoulder = 1.3, ShoulderDrop = 0.4, ShoulderFlat = 0.8, ShoulderDip = 0.03;
	static constexpr double MeetUnder = 0.015;  // [road edges, Nov 17] the shoulder's outer edge this far under the ground as drawn
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
	// [road edges, Nov 17] The ground's height as the nearest squares draw it: their 4 m grid of Ground's
	// heights, each cell's two triangles (Cache, if given, keeps the grid's heights met for the next call).
	static constexpr double LandGrid = 4.0;   // (ATorqueRideWorld's finest squares: 256 m, 64 cells)
	double LandDrawnAt(double X, double Z, std::unordered_map<long long, double>* Cache = nullptr) const;
	// [road edges, Nov 18] And its colour as they draw it: LandColour at the grid's points (as LandTile has
	// them), across each cell's two triangles (Cache keeps the points' colours met).
	void LandColourDrawnAt(double X, double Z, float* Out, std::unordered_map<long long, std::array<float, 3>>* Cache = nullptr) const;
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
		int Banked = 0;          // [OSM stage 5E] the nearer point's RoadPoint::Banked
		P2 Foot;                 // the point of the road's middle nearest
		double Along = 0.0;      // how far along its road that is (m)
		double Off = 0.0;        // how far to the left of the road's middle the point is (m; - the right)
		double Fall = 0.0;       // how steeply the road's surface falls outwards at its edge on the point's side (0 if it rises)
	};
	bool NearestOf(double X, double Z, const NearOpts& Opts, NearHit& Hit) const;
	// A side road's surface height Y at a point, settled onto a greater road it lies on or joins there.
	double JoinedHeight(int Which, const P2& At, double Y) const;
	// [build 150] The same for every point of one piece of road at a time: the bits of other roads (but
	// Which, none on a deck or in a bore) NearestOf would look at anywhere in a box and whose edge comes
	// within JoinLean of it, gathered once into a little grid (JoinSegs); then NearestIn finds what
	// NearestOf would have for any point in the box whenever that road's edge is within JoinLean of it
	// (York's road pieces spent most of their time in NearestOf).
	struct JoinSeg
	{
		int Which = 0;
		long long Cx = 0, Cz = 0;   // the NearCell square it is filed under
		P2 A, B;
		double HalfA = 0.0, HalfB = 0.0, HeightA = 0.0, HeightB = 0.0, BankA = 0.0, BankB = 0.0;
	};
	struct JoinSet
	{
		std::vector<JoinSeg> Segs;
		double X0 = 0.0, Z0 = 0.0, X1 = -1.0, Z1 = -1.0;   // the box
		int Nx = 0, Nz = 0;
		std::vector<std::vector<int>> Cells;               // JoinCell squares of the box: the bits that can matter there
		bool Holds(const P2& At) const { return At.X >= X0 && At.X <= X1 && At.Z >= Z0 && At.Z <= Z1; }
	};
	static constexpr double JoinCell = 8.0;
	struct JoinHit { int Which = -1; double Dist = 0.0, Half = 0.0, Height = 0.0, Cross = 0.0; };
	void JoinSegs(int Which, double X0, double Z0, double X1, double Z1, JoinSet& Out) const;
	// (false if no road's edge is within JoinLean, else NearestOf's best: MainOnly the ride's road alone)
	bool NearestIn(const JoinSet& Set, const P2& At, bool MainOnly, JoinHit& Hit) const;
	double JoinedHeightFrom(const JoinSet& Set, int Which, const P2& At, double Y) const;
	// The ground as Ground gives it, but with NoBores as if no road ran in a tunnel: the hill over one.
	double GroundOf(double X, double Z, double Coarse, bool NoBores, bool Trough = false) const;
	double GroundUncut(double X, double Z, double Coarse, bool NoBores, bool Trough) const;   // [ferries] (GroundOf before FerryCut)
	// [build 155 feedback] The land mesh's own heights at its points (LandTile, LandDrawnAt): Ground, with the
	// near squares' points by a watercourse under its water (BanksTile draws the banks over them).
	double LandGround(double X, double Z, double Step) const { return GroundOf(X, Z, Step, Step > 8.5, std::fabs(Step - LandGrid) < 0.1); }

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
	// [build 149] A hedge's piece from A to B as hedge models (Out's Plants, about its origin).
	void HedgePlants(const P2& A, const P2& B, double Ya, double Yb, double Down, double Size, RideMesh& Out) const;
	// The grass, rushes, heather and bracken on a square of ground (the finest squares: Coarse 4 m or
	// less) and its trees (squares of 8 m or less, the far models past 4 m), as Tools/make_foliage.py
	// lays Buttertubs' out but from the cells' cover, slope and canopy: carpets of grass near every
	// road, clumps among them, rushes where it is wet, heather on the moor, bracken on the drier
	// slopes, nothing on a road. Mesh is the square's own ground (Cells by Cells from LandTile), so a
	// plant stands on the surface drawn. The same every time for the same square.
	void PlantsTile(double X0, double Z0, double Size, int Cells, const RideMesh& Mesh, std::vector<RidePlant>& Out) const;
	static constexpr int PlantKinds = 39, FirstTree = 18, HedgeKind = 32, FirstGarden = 33;
	// [gardens] Kinds FirstGarden on are the villages' and towns' garden planting (WorldRideGardens.cpp):
	// "garden_rose", "garden_shrub", "garden_flowers", "garden_low", "garden_tree", "garden_hedge", drawn
	// as our own models (Tools/make_gardens.py: SM_Tree_garden_<kind>_<n>, [Nov 20]) or scanned ones, close
	// to the rider.
	// "carpet_green", ... "bracken" (the hand-built ride's foliage species), then the trees' kinds
	// "ash", "sycamore", "alder", "birch", "hawthorn", "spruce" (Buttertubs' models SM_Tree_<kind>_N,
	// SM_TreeFar_<kind>_N), "oak", "beech", "willow", "rowan", "pine", "hazel", "holly", "larch" ([Nov 20]
	// ours too, Tools/tree_models.py; the game draws one of the first six in their place until they are in:
	// PlantStandIn), "hedge" (the field hedges).
	// What a tree kind is drawn as when there are no models of it (itself for the first six).
	static int PlantStandIn(int Kind);
	static const char* PlantName(int Kind);
	// The livestock in the fields of a square of ground (Ride UK stage 6: WorldRideLife.cpp): flocks of
	// sheep on the pasture, and the moor in the uplands; herds of cattle, more in the lowlands, most of
	// them in the sheds from November to March; lambs at foot from March to June; Highland cattle in
	// the Highlands. None on a road, a railway, in a building, a wall, water or a wood. The same every
	// time for the same square and month.
	void AnimalsTile(double X0, double Z0, double Size, std::vector<RideAnimal>& Out) const;
	enum EAnimal { EweGraze, EweLook, EweLie, Lamb, CowGraze, CowStand, CowLie, Highland, AnimalKinds };
	// "ewe_graze", ... "highland": the models' names are SM_Animal_<name>_<n> (Tools/make_ride_animals.py).
	static const char* AnimalName(int Kind);
	// What's heard at (X, Z): the sea within 400 m, running water within 80 m, the buildings within
	// 150 m, the flocks and herds (AnimalsTile's) within 150 m. A few milliseconds: ask about once a second.
	RidePlace SoundsAt(double X, double Z) const;
	// The month the ride is in (1..12; its seasons: the lambs, the cattle out, the heather in flower).
	int Month = 6;
	// The ride's date set in Settings (its month, 1..12), or 0 for the month now: the next plan made takes it.
	static inline int SeasonMonth = 0;
	// Set before the ground is made: trees come as plants (PlantsTile) and not as stand-in shapes
	// (SceneryTile); how many of the grass's plants are kept (0..1).
	bool RealTrees = false;
	// [build 149] Set with RealTrees when the game has hedge models (plant kind "hedge"): the finest
	// squares' hedges are then those models laid along their lines (HedgePlants), not lumpy shapes.
	bool RealHedges = false;
	double GrassDensity = 1.0;
	// [gardens] Set when the game has the garden kinds' models and -TorqueRidePlanting=0 isn't given: the
	// finest squares' houses get garden planting (GardenPlants) and the towns' and villages' grass a few
	// shrubs and flowers (PlantsTile). GardenShare (0..1, -TorqueRidePlanting=0.5) thins it out.
	bool RealGardens = false;
	double GardenShare = 1.0;
	// [gardens] One house's garden as BuildingsTile builds it: its middle, its turn (Cs, Sn: as Local's),
	// its length along (LongX: x) and depth; which side its front is on (FrontSide +1/-1, across), how far
	// from 1 m out of the front to the road's edge (FrontGap); its door (along) and drive (DriveFrom..DriveTo
	// along, none if equal); which of its ends are party walls; Kind (HouseTypes::EKind); Seed its own.
	struct GardenHouse
	{
		double X = 0.0, Z = 0.0, Cs = 1.0, Sn = 0.0, HalfLong = 0.0, HalfShort = 0.0;
		bool LongX = true, PartyPlus = false, PartyMinus = false, FrontWall = false, InTown = false;
		double FrontSide = 1.0, FrontGap = 1e9, DoorU = 0.0, DriveFrom = 0.0, DriveTo = 0.0;
		int Kind = 0, Bays = 1;   // (a row drawn as one building: a door a bay, in pairs, as BuildingsTile draws them)
	};
	// [gardens] Footprints (each a Building, its own frame) that garden plants keep clear of: the square's
	// buildings and its neighbours' near its edges, by 16 m bucket (each in every bucket it reaches).
	struct GardenClear
	{
		std::unordered_map<long long, std::vector<const Building*>> Buckets;
		void Add(const Building& B);
		bool Blocked(double X, double Z, double Gap) const;   // (Gap at most 2 m)
	};
	void GardenPlants(const GardenHouse& House, const GardenClear& Clear, RideMesh& Out) const;
	bool SeenFromRoad(const GardenClear& Clear, double X, double Z, const P2& Foot) const;   // [gardens] (a clear line to the road)
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
	// Then a red phone box in the bigger villages, a pub's hanging sign by the road with its name, and a
	// level crossing's barrier and lights on each road approach to a railway in use.
	// (Limit30 is any limit's roundel, its number in Text: [OSM stage 1] 20, 30, 40, 50 mph, or km/h abroad)
	// [OSM stage 2] stiles, kissing gates, lift barriers, bollards, picnic tables and information boards.
	enum EFurnish { NameSign, Limit30, LimitNational, Chevron, StreetLamp, CattleGrid, PostBox, BusStop, PowerPole, FieldGate, Seat, Milestone, PhoneBox, PubSign, CrossingBarrier,
		Stile, KissingGate, LiftGate, Bollard, PicnicTable, InfoBoard, PlaceBoard,
		TrafficSignal, Beacon,   // [OSM stage 5D] a traffic light facing the traffic (Size its cycle's start, s), a zebra crossing's beacon
		// [OSM stage 6B] a warning triangle (Size its symbol, SignPlate::ESymbol; Big its side, m; Text a hill's
		// gradient, "20%", or the words of a plate under it), a street's name plate (Text; Big its capitals, m)
		WarnSign, StreetPlate,
		// [OSM stage 6C] a junction's advance direction sign (Text its arms, SignPlate::AdvanceBoard's; Size its
		// scheme, SignPlate::EScheme; Big its capitals, m), a route confirmation sign after it (Text its lines)
		DirAdvance, DirConfirm,
		// [OSM stage 6D] a brown sign's flag to a place worth seeing (Text as SignPlate::FlagBoard's), a
		// fingerpost (Text its arms: "hx,hz;Place\tmiles|..." each pointing along (hx, hz); Big its capitals)
		BrownFlag, Fingerpost,
		BoundarySign,   // [OSM stage 6D/6E] a county's or country's board where the road crosses its border (Text its lines, "|" between)
		Countdown,      // [OSM stage 6F] a motorway exit's countdown marker (Size its bars, 1 to 3)
		FurnishKinds };   // [places stage 2] a place's name board (Text; a marker post with none)
	struct Furnish
	{
		int Kind = 0;
		P2 At, Face;
		std::string Text;
		double Size = 0.0;   // a grid's width, a gate's length, a chevron bend's turn (+ left)
		double Height = 0.0; // the ground (or the road) under it
		uint32_t Id = 0;     // [OSM stage 2] a hash of the map's own id (0 for one placed by rule): its variant
		bool Mapped = true;  // the map put it there (not a rule)
		double Big = 0.0;    // [OSM stage 6] a sign's size (WarnSign, StreetPlate)
	};
	std::vector<Furnish> Furniture;
	int FurnitureCount(int Kind) const;
	// [ride anywhere 4] The furniture of the network's roads (not the ride's own: that is Furniture),
	// made the first time a square of them is drawn and kept: street lamps through the built-up
	// stretches of each road laid, chevrons round its sharp bends, and the map's own boxes, stops,
	// benches, lamps, bollards and cattle grids by roads that weren't laid when the ride was planned.
	// Those standing in (X0, Z0)-(+Size) into Out, kept alive by Hold. Safe on any thread.
	void NetworkFurniture(double X0, double Z0, double Size, std::vector<std::shared_ptr<const std::vector<Furnish>>>& Hold, std::vector<const Furnish*>& Out) const;
	std::vector<Furnish> RoadFurniture(int Which) const;    // (a laid road's lamps and chevrons)
	// [OSM stage 6C] The direction signs of the ride's road's junctions (World/WorldRideDirections.cpp), in
	// Britain: where a side road of tertiary or more (no greater than the ride's road) meets it, a map-type
	// sign before it each way naming the places each road out leads to first (the shortest way there starts
	// by it: the nearest town or village and a bigger place further on), and on a primary or secondary road
	// the places ahead with their miles after it. Positioned, not yet put (IndexFurniture puts them first).
	std::vector<Furnish> DirectionSigns() const;
	std::vector<Furnish> BlockFurniture(const Rect& In) const;   // (the map's furniture in a block left by IndexFurniture)
	std::vector<P2> MappedLamps;                            // the map's street lamps (IndexFurniture)
	std::vector<unsigned char> MappedLeft;                  // by Scenery.Points: 1 for one IndexFurniture had no road for
	// [OSM stage 2] The prop layer (World/WorldRideProps.cpp). The kinds drawn from real models, each
	// stage adding its own at the end (never reordered). A kind the kit has a model for comes as props
	// (PropsTile) and FurnitureTile leaves its code-made shape out; one with none keeps that shape. The
	// game says which it has (PropModelled) before any square is made.
	enum EProp { PropPostBox, PropSeat, PropBusStop, PropMilestone, PropPhoneBox, PropPowerPole, PropFieldGate, PropStreetLamp, PropStile, PropKissingGate, PropLiftGate,
		PropBollard, PropPicnicTable, PropInfoBoard,
		// [fps plan step 2] The houses' windows, their surrounds and doors (Tools/make_facade_kit.py, each kind's
		// models by size: FacadeSize), placed by BuildingsTile in place of the boxes it drew, coloured per
		// instance (RideProp::Tint: paint, trim, third)
		PropWinSash6, PropWinSash2, PropWinCasement, PropWinCasementTop, PropWinMullion, PropWinMullionLeaded,
		PropSurStone, PropSurBrick, PropSurPlain,
		PropDoorPanel6, PropDoorVictorian, PropDoorLedged, PropDoorLedgedLight, PropDoorModern, PropDoorDouble, PropDoorHood,
		// [towns stage 4] Bay windows (Variant: storeys less one), placed only where the game has their models
		PropBayCanted, PropBaySquare,
		// [build 154] Dry stone walls laid stone by stone (Tools/make_wall_kit.py): 2 m lengths and 1 m wall heads,
		// finished at the model's +x; Variant is the region's walling (WallStyle) times WallVariants plus a pick.
		// Placed on the finest squares where both are modelled.
		PropWallStone, PropWallEnd,
		// [towns stage 6] A town centre's heritage street lamp (cast iron, swan neck, lantern), in place of the
		// plain one where the street is a shopping street (ShopStreetNear) and it has its model.
		PropLampHeritage,
		// [car parks] A parked car in a car park's bay (Tools/ue_build_rideuk.py: the Fab cars of
		// Data/Buttertubs/car_swaps.json in Data/RideUK/prop_kit.json's colours, SM_Prop_parked_car_<n>, as modelled)
		PropParkedCar,
		// ([Nov 19] beenster: "a more realistic traffic light model") a UK three-aspect signal (Tools/blender_street_kit.py
		// traffic_signal); its lit faces stay FurnitureTile's, on the shared clock
		PropTrafficSignal,
		// [circuits] The race circuits' trackside kit (Tools/blender_circuit_kit.py), placed by IndexCircuits: armco,
		// tyre walls and the pit wall laid along their runs (Stretch), the rest facing the track
		PropArmco, PropTyreWall, PropPitWall, PropMarshalPost, PropBrakingBoard, PropStartGantry, PropGrandstand,
		// [caravans] The campsites' touring caravans and static caravans (Tools/blender_caravan_kit.py), in place of
		// TownLifeTile's boxes where they have models (TownLifeProps)
		PropTouringCaravan, PropStaticCaravan,
		PropKinds };
	static constexpr int WallStyles = 6, WallVariants = 4;
	// Which walling a region's dry stone walls are (Tools/make_wall_kit.py's WALL_STYLES): 0 the Dales'
	// limestone (Buttertubs' own), 1 Cotswold, 2 slate (the Lakes, Wales), 3 gritstone (the Peak, southern
	// Scotland), 4 granite (the Highlands, Ulster), 5 sandstone (the Moors, the North East, the rest).
	static int WallStyle(const LocalLook& Look);
	static constexpr int FirstFacade = PropWinSash6;
	static constexpr int FacadeCoreEnd = PropBayCanted;   // (the kinds the kit can't go without: it is off unless all have models)
	// [fps plan step 2] Whether the houses' windows and doors come as the facade kit's instances (the game
	// sets it when every facade kind has its models and -TorqueRideFacadeKit=0 isn't given), else drawn.
	bool FacadeKit = false;
	static const char* PropName(int Kind);   // "post_box", "seat", ... (the kit file's and the models' names)
	static int PropOf(int FurnishKind);      // the prop a kind of furniture is drawn as, -1 none (signs)
	bool PropModelled[PropKinds] = {};
	bool Modelled(int FurnishKind) const { const int P = PropOf(FurnishKind); return P >= 0 && PropModelled[P]; }
	void PropsTile(double X0, double Z0, double Size, std::vector<RideProp>& Out) const;
	// A row of Data/RideUK/prop_kit.json: the kind, the words in an asset's path that make it one of the
	// kind's models, how far off it shows (m), its height in life (m: the model is scaled to it), a turn
	// (degrees) and a drop (m) to put the model's front to +x and its foot on the ground, whether it casts
	// a shadow, and the credit its licence asks for.
	struct PropKitRow
	{
		std::string Kind;
		std::vector<std::string> Words;
		double Reach = 300.0, Height = 1.0, Turn = 0.0, Drop = 0.0;
		bool Shadow = false;
		std::string Credit;
	};
	static bool ReadPropKit(const std::string& Text, std::vector<PropKitRow>& Out);
	// [OSM stage 2 batch B] The power and the skyline (World/WorldRideSkyline.cpp): the wires between
	// telegraph poles (the map's minor lines, or a pole joined to its neighbours within 90 m) and
	// between pylons (its power lines), the pylons, masts, wind turbines, silos, water towers and tanks
	// (Talls), and substations and solar farms (Yards). Made once by IndexSkyline (after IndexFurniture:
	// it turns each pole's arm square to its wire, and puts up a pole where a line has none). A wire is
	// from A (Ya over the ground's level, the attachment's height) to B, sagging Sag in its middle, in
	// Strands across Spread; one too long, or lower than 5 m over a road, is left out and counted.
	struct Wire
	{
		P2 A, B;
		double Ya = 0.0, Yb = 0.0, Sag = 0.0, Spread = 0.0;
		int Strands = 2;
		bool High = false;   // a power line's (pylons'), or a telegraph pole's
	};
	// ([roads stage 8] the skyline's big shapes too: gasholders' frames, power stations' cooling towers, and
	// the floodlights' masts round sports grounds)
	enum ETall { TallMast, TallMonopole, TallTurbine, TallSilo, TallWaterTower, TallTank, TallPylon, TallKiosk, TallGasholder, TallCooling, TallFlood, TallKinds };
	struct TallThing
	{
		int Kind = 0;
		P2 At, Face;            // Face: a pylon's line, a turbine's rotor's way
		double Ground = 0.0, Height = 0.0, Radius = 0.0;
		uint32_t Id = 0;
	};
	struct Yard
	{
		bool Solar = false;     // a solar farm (or a substation)
		std::vector<P2> Ring;
		P2 Mid;
		double Radius = 0.0;    // from Mid to its farthest corner
		uint32_t Id = 0;
	};
	std::vector<Wire> Wires;
	std::vector<TallThing> Talls;
	std::vector<Yard> Yards;
	int WiresDropped = 0;
	void IndexSkyline();
	// What of it stands on a square: Detail 2 (the finest squares) all of it with the wires, 1 all but
	// the wires, 0 (to 10 km) the tall ones as plain far shapes, -1 (beyond) only the turbines.
	void SkylineTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// [OSM stage 2 batch C] Field areas (World/WorldRideFields.cpp): quarries (bare rock, worked faces
	// along their edges), graveyards and cemeteries (mown grass, headstones in loose rows facing east,
	// none in a building), allotments (plots of dug earth and green, a shed on some) and orchards (fruit
	// trees in rows: PlantsTile). Each colours the ground inside its outline (LandColour: FieldTint)
	// and stands its things on it (FieldsTile: Detail as SkylineTile's, the small things only at 2).
	// [OSM stage 2 batch D] and scree (grey stones, a scatter of them near) and reservoirs (flat water at
	// their level, drawn with the water: WaterTile).
	// [OSM stage 4] And the ground file's shapes (Region::Ground): water as true shapes (as reservoirs:
	// flat, at the lowest tenth of their shore), lawns where people live, hard standing in yards and car
	// parks, farmyards, building sites, rough brownfield, mown parks and striped pitches, sand and mud,
	// and woods, scrub, heath and wetland coloured to their mapped edges. Where shapes overlap the
	// smallest wins (a pitch in a park in a town).
	enum EField { FieldQuarry, FieldGraves, FieldAllotments, FieldOrchard, FieldScree, FieldReservoir,
		FieldLawn, FieldHard, FieldFarmyard, FieldSite, FieldBrownfield, FieldPark, FieldPitch, FieldSand, FieldMud, FieldCarPark,
		FieldWood, FieldScrub, FieldHeath, FieldWetland, FieldPullOff, FieldForecourt, FieldKinds };   // [places stage 2] a pull-off made for a place (gravel); [roads stage 7] a petrol station's forecourt (tarmac)
	struct FieldArea
	{
		int Kind = 0;
		std::vector<P2> Ring;
		double MinX = 0.0, MinZ = 0.0, MaxX = 0.0, MaxZ = 0.0;
		double Level = 0.0;   // a reservoir's water
		uint32_t Id = 0;
		double BoxArea = 0.0;   // [OSM stage 4] (the smaller of two that overlap is the one here)
		bool Flowing = false;   // a river's or canal's own outline (its water runs: WaterRuns), not a lake's
		int Yard = -1;          // [churches stage 5] a graveyard round a church: its Churchyards entry
	};
	std::vector<FieldArea> FieldAreas;
	// [churches stage 5] Each place of worship's churchyard (IndexChurchyards, World/WorldRideFields.cpp): the
	// map's graveyard round it, else (out of a town) one made round it as far as its neighbours and roads
	// allow; its wall's runs (where the map draws no wall, fence or hedge of its own), its gate (the yard's
	// edge nearest the road), the path from the gate to the church's walk round it, its yews. FieldsTile keeps
	// the graves off the path, the walk, the gate and the yews; ChurchyardsTile builds the rest.
	struct Churchyard
	{
		int Field = -1;                 // its ground (FieldAreas: a FieldGraves area)
		bool Made = false, Chapel = false, Lych = false;
		P2 Church;                      // the church's outline's middle (all its pieces)
		double Cs = 1.0, Sn = 0.0, HalfU = 0.0, HalfV = 0.0;   // its box: half sizes along its building's axes (Yaw)
		P2 Gate, Out;                   // the gate's middle on the yard's edge, and outward (unit)
		P2 PathEnd;                     // where the path from the gate meets the walk round the church (1.5 m out)
		std::vector<std::pair<P2, P2>> Walls;   // the wall's runs
		std::vector<std::pair<P2, int>> Yews;   // where, and which (0 an old spreading one, 1 an Irish yew)
		double MinX = 0.0, MinZ = 0.0, MaxX = 0.0, MaxZ = 0.0;
	};
	std::vector<Churchyard> Churchyards;
	void IndexChurchyards();   // (from IndexFields: made yards are field areas too)
	// How far (X, Z) is outside a churchyard's church box (negative inside).
	double OffChurch(const Churchyard& Y, double X, double Z) const;
	// The churchyards' walls, gates, paths and yews on a square (fine squares only), in the place's stone.
	// Doors: the kit churches built on it, each its middle, its door (a metre out) and the way it faces, and
	// its footprint as built: its middle, its length's way (unit) and half sizes along and across.
	struct ChurchDoor { P2 Middle, Door, Out, Box, Along; double HalfA = 0.0, HalfC = 0.0; };
	void ChurchyardsTile(double X0, double Z0, double Size, const LocalLook& Look, const std::vector<ChurchDoor>& Doors, RideMesh& Out, RideMesh& Washed,
		RideMesh& Houses, RideMesh& RoofOut, RideMesh& BrickOut) const;
	// [beenster, build 147: "flat water surfaces and well formed banks for rivers, streams, brooks, lakes and
	// the coastal shoreline"] (World/WorldRideWater.cpp) Each of the map's rivers, streams and canals as a run
	// of points every WaterStep m down it (turned to run downhill), each with its water's level (never rising
	// downstream, level across), its half width (a river's from the water the land's cover and the map's
	// outlines show either side of it) and its depth. Ground cuts its channel and banks round them (and
	// presses a lake's bed and the sea's under their water), WaterTile draws their water level across.
	struct WaterRun
	{
		int Kind = 0;   // as Stream's: 0 a river, 1 a stream, 2 a canal
		std::vector<P2> At;
		std::vector<float> Surface, Half, Depth;
		std::vector<float> Along;   // [real water stage 6] metres down it from its first point, kept under 4 km (its flow's ripples repeat every 80 m)
	};
	std::vector<WaterRun> WaterRuns;
	std::unordered_map<long long, std::vector<std::pair<int, int>>> WaterNear;   // (run, first point) by every WaterCell square within reach
	std::unordered_map<long long, char> StillCells;   // the FieldCell squares a lake's outline covers
	static constexpr double WaterStep = 3.0, WaterCell = 32.0, BankRun = 1.2, BankReach = 14.0;
	// [build 155 feedback] (the near land's points within BankTrough of its squares past a watercourse's edge
	// lie under its water, BanksTile's strips of bank reaching BankStrip squares out over them; a lake's and
	// the sea's shore eased over ShoreReach m round its outline)
	static constexpr double BankTrough = 0.9, BankStrip = 2.4, ShoreReach = 10.0;
	// [real water, build 168 Totscore] (a stream's water lies no higher than the land this far past its banks)
	static constexpr double BedReach = 5.0;
	void IndexWater();   // (after IndexFields)
	// The run nearest (X, Z) within its bank's reach: how far from its middle, its water's level, half
	// width and depth there, and its kind.
	bool NearWater(double X, double Z, double& Dist, double& Surface, double& Half, double& Depth, int* Kind = nullptr, bool* Beside = nullptr) const;   // (Beside: alongside it, not past either end)
	// A lake's (or reservoir's) water level at (X, Z) if it is in one's outline, else NaN.
	double StillLevel(double X, double Z) const;
	void IndexFields();
	int FieldAt(double X, double Z) const;   // the field area here (FieldAreas), -1 none
	void FieldsIn(double X0, double Z0, double Size, std::vector<int>& Out) const;
	bool FieldTint(double X, double Z, float* Rgb) const;   // the ground's colour in a field area, false outside one
	void FieldsTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// [OSM stage 2 batch D] Wild ground and water works (World/WorldRideWild.cpp): the map's cliffs as
	// rock faces along their lines (the low side to the right of the way the line runs, as the map draws
	// them), a cairn or a trig pillar on each mapped peak, dams as walls across their valleys and weirs
	// as steps across their rivers. Detail as SkylineTile's: the cliffs and dams far off too (they shape
	// the skyline), the peaks' markers and the weirs near.
	struct WildLine
	{
		int Kind = 0;   // 0 a cliff, 1 a dam, 2 a weir
		std::vector<P2> Line;
		uint32_t Id = 0;
	};
	struct Summit
	{
		P2 At;
		std::string Name;
		double Elevation = 0.0;
		bool Trig = false;   // a trig pillar (or a cairn)
	};
	std::vector<WildLine> WildLines;
	std::vector<Summit> Summits;
	void IndexWild();
	// [towns stage 1] The look of each town (World/WorldRideTowns.cpp): by the kilometre square, a look
	// counted from the tagged walls and roofs round it, or a town's from Data/RideUK/town_looks.json
	// (SetTownTable), over the region's. LookHere gives it; null where a square has none.
	static constexpr double TownCell = 1000.0;
	std::unordered_map<long long, int> TownGrid;
	std::vector<LocalLook> TownLooks;
	std::vector<std::string> TownSources;   // each look's: "tags N" (counted from N) or the table's town
	int TownsCounted = 0, TownsTabled = 0;  // squares counted; the table's towns met
	// [towns stage 5] Out of town, the materials atlas's area's look over the region's (in TownLooks,
	// by the kilometre square: AtlasLookAt; null where no area holds it); the squares it covers.
	std::unordered_map<long long, int> AtlasGrid;
	int AtlasSquares = 0;
	// [towns stage 6] The shopping streets (IndexTowns): the map's retail areas (a town centre's
	// shops, as the map draws them) and its pedestrian streets, each with its box; ShopStreetNear whether a
	// place is within Reach of either (a building there with its front to a street has a shop front; a
	// street lamp there is a heritage one).
	struct ShopPlace
	{
		double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;
		const std::vector<P2>* Ring = nullptr;   // (an area's; null for a street)
		P2 A, B;                                 // (a street's piece)
	};
	std::vector<ShopPlace> ShopPlaces;
	std::unordered_map<long long, std::vector<int>> ShopGrid;   // by 128 m square, each place whose box (and 60 m) covers it
	bool ShopStreetNear(double X, double Z, double Reach) const;
	bool HeritageLampAt(double X, double Z) const;
	// [towns stage 6] Whether the map's surface for a road is setts or cobbles (drawn in M_RideRoad's setts).
	static bool SettsSurface(const std::string& Surface);   // (a street lamp here is the heritage kind)
	// [towns stage 8] The famous streets' own looks (StreetTable): each piece of a street's line with its
	// look (in TownLooks: the street's over the town's), how far either side it holds, and whether the
	// street is laid in setts and lit by heritage lamps; by 128 m square. StreetLookAt gives a place's
	// street look (LookHere's first); SettsStreet whether a road's line runs along a street in setts.
	struct StreetPiece
	{
		P2 A, B;
		int Look = -1;
		double Reach = 30.0;
		bool Setts = false, Lamps = false;
	};
	std::vector<StreetPiece> StreetPieces;
	std::unordered_map<long long, std::vector<int>> StreetGrid;
	std::vector<std::string> StreetsHere;   // the table's streets in this ride's region
	const StreetPiece* StreetAt(double X, double Z, double Extra = 0.0) const;
	const LocalLook* StreetLookAt(double X, double Z) const;
	bool SettsStreet(const std::vector<P2>& Line) const;
	void IndexTowns();
	const LocalLook* TownLookAt(double X, double Z) const;
	const LocalLook* AtlasLookAt(double X, double Z) const;
	// [towns stage 2] The large and tall buildings at their true outlines and heights (World/
	// WorldRideTowers.cpp): the baker's "towers" (and a tall building's parts), simplified to 16
	// corners or fewer, each on its building's lowest ground.
	struct TowerShape
	{
		std::vector<P2> Ring;
		P2 Mid;
		double Floor = 0.0;                 // its area (m2)
		double Base = 0.0, Top = 0.0;       // above its ground (a part may start up in the air)
		double Ground = 0.0;
		std::string Name, Id, Group, Class, RoofShape, RoofColour, RoofMaterial, WallMaterial, WallColour;
		bool Guessed = false;               // no height mapped: one from its class and size
		bool Outline = false;               // drawn as its parts instead
	};
	std::vector<TowerShape> TowerShapes;
	int TowersGuessed = 0, TowersOutlines = 0;
	void IndexTowers();
	// Detail 2 near (windows, parapets, lit windows at night), 1 plain, 0 far (40 m and over), -1 (80 m and over).
	void TowersTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// Whether a point is inside a tower that stands on the ground (a box building there is not drawn).
	bool UnderTower(double X, double Z) const;
	// [towns stage 3] Every building of a cell with its true outline and height (the cell's buildings
	// zip, CellStore::BuildingsZip), where the world has them: each outline as one or more boxes (an L
	// as two, a long curve as a few), each box a Building as the cells' own with the map's words as its
	// look. Read a tile of a cell at a time and kept. Safe on any thread.
	struct OutlinePiece
	{
		Building B;
		BuildingLook Look;
		double Floors = 0.0;   // the map's floors (0: it doesn't say)
		// [LiDAR stage 2] Its outline's surveyed eaves and ridge above the ground (metres; 0: not measured), from
		// the cell's LiDAR file (Tools/lidar_heights.py): the drawer takes these before the map's height or floors.
		float LidarEaves = 0.0f, LidarRidge = 0.0f;
		// [towns stage 4] Its place in a row (MarkRows, once per tile read): which outline it came from and
		// whether it is that outline's main box; the sides of the main box another house's main box shares
		// (bit 1 its +x side, 2 its -x, 4 its +z, 8 its -z, x across W, z deep D), and on which of those it
		// draws the shared chimney stack; the row's size (1 alone, 2 a pair of semis, 3 or more a
		// terrace), its place along it, and one spot for the whole row (so the row is built alike).
		int Outline = -1;
		bool Main = false;
		unsigned char Party = 0, Stacks = 0;
		int RowSize = 1, RowPos = 0;
		P2 RowSeed;
	};
	// [towns stage 4] Finds the rows in a tile's pieces (see OutlinePiece).
	static void MarkRows(std::vector<OutlinePiece>& Pieces);
	// Those whose middles lie in (X0, Z0)-(+Size) into Out (kept alive by Hold); the cells under the
	// square that have outlines into Covered (their own boxes aren't drawn there).
	void OutlinesIn(double X0, double Z0, double Size, std::vector<std::shared_ptr<const std::vector<OutlinePiece>>>& Hold,
		std::vector<const OutlinePiece*>& Out, std::vector<CellId>& Covered) const;
	// An outline (the area's frame, metres) as the boxes it is drawn with; Kind the baker's (land_bake's
	// BUILDING_KINDS) from the map's class and its size.
	static std::vector<Building> PiecesOf(const std::vector<P2>& Ring, double Height, int Kind);
	static int KindOfClass(const std::string& Class, double Area);
	struct OutlineStore;
	static std::shared_ptr<OutlineStore> NewOutlineStore();
	std::shared_ptr<OutlineStore> Outlines = NewOutlineStore();
	std::vector<std::string> TakeOutlineNotes() const;   // [towns stage 3] each cell's outlines read since last asked
	void WildTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// [OSM stage 3] What a square's buildings came to (for the tests and the report).
	struct BuildingTally
	{
		int Drawn = 0, Terraces = 0, TerraceHouses = 0, Chimneys = 0, NearRoad = 0, Barns = 0, Ruins = 0, UnderTowers = 0;
		int Outlined = 0;   // [towns stage 3] boxes cut from true outlines
		int KitChurches = 0;   // [churches stage 3] places of worship built from the church kit
		int Hipped = 0, LeanTo = 0;   // [towns stage 3.4] houses' roofs
		int HousesByRoad = 0, LongSideToRoad = 0;   // houses within 25 m of a road; those with a long side towards it
		int HouseFloors[3] = {0, 0, 0}, HouseMapped[3] = {0, 0, 0};   // houses (not rows) of 1, 2, 3+ storeys; those from the map's height
		int HouseKinds[6] = {};   // [towns stage 4] houses by HouseTypes::EKind (cottage, village row, detached, semi, terrace, tenement)
		int BayWindows = 0;       // and the bay windows on them
		int DoorsDropped = 0, DoorSteps = 0;   // doors set lower than the floor to meet the ground before them; doors with steps down to it
		// [towns stage 5] Houses', shops' and flats' walling and roofs by where they came from: their own tags,
		// the street's, the town's look counted from tags, the town table's, the materials atlas's area, the
		// region's (and for roofs, the house's period); the stone ones sooted; the painted ones by the sea.
		int WallFrom[7] = {}, RoofFrom[7] = {};
		int Sooted = 0, Painted = 0;
		int ShopFronts = 0;   // [towns stage 6] shop units fronted
		int FrontWalls = 0;   // [towns stage 6] houses with a front garden wall
	};
	void BuildingsTile(double X0, double Z0, double Size, bool Fine, const LocalLook& Look, RideMesh& Out, RideMesh& Washed, RideMesh& Houses, RideMesh& RoofOut, RideMesh& BrickOut, BuildingTally* Tally = nullptr) const;
	// [build 141] The buildings on a far square as plain blocks under plain roofs, those of MinArea (m2) or
	// more, the biggest Most of them.
	void FarBuildingsTile(double X0, double Z0, double Size, double Coarse, double MinArea, RideMesh& Out, size_t Most = 2500) const;
	// Where the ride's road is built up (a village or a town round it), by its points.
	std::vector<unsigned char> BuiltUp;
	// What each of the region's buildings is, where the map says (0 nothing special, 1 a church: drawn
	// with its tower, 2 a pub, 3 a café: its shop front named as one).
	std::vector<unsigned char> BuildingRole;
	// The local look (Ride UK stage 5: WorldRideStyle) at a place of the ride's frame.
	const LocalLook& LookHere(double X, double Z) const;
	// The railways near the ride (Ride UK stage 5), the map's railway lines joined end to end and a
	// point every RailStep metres along each: where, the top of its ballast (eased along so it climbs
	// as a railway does, cut into the hills and banked over the dips, lifted over a road it bridges,
	// level with a road it crosses), and what each point is on: 0 the ground, 1 a bridge, 2 in a tunnel
	// (not drawn), 3 across a road (a level crossing: the rails alone, flush with the road). A disused
	// line keeps its cuttings and banks and its bridges but has no track.
	struct RailLine
	{
		std::vector<P2> At;
		std::vector<double> Top;
		std::vector<unsigned char> On;
		// (beenster, build 147: a house beside a line on a bank was buried in it) how far out from each
		// point its bank may spread before a house: past that, a retaining wall (m, at least RailBedHalf)
		std::vector<float> Room;
		bool Disused = false;
	};
	std::vector<RailLine> Rails;
	// [level crossings] Each level crossing of a railway in use (World/WorldLevelCrossing.h lays it out; the game's
	// ATorqueLevelCrossing stands it up near the rider and runs it; the trains warn it by Key): where the rails cross
	// the road (Rails[Line] points First..Last, On == 3), the middle of that, the road's way across and the line's
	// (unit), the road's half width, the map's road class there and the crossing's type (LevelX::EType). Key is a
	// hash of where it is, the same on every machine. Found by IndexFurniture.
	struct LevelCrossing
	{
		uint32_t Key = 0;
		int Line = 0, First = 0, Last = 0;
		P2 Mid, Road, Rail;
		double Half = 3.0;
		int Type = 0;
		std::string Class;
	};
	std::vector<LevelCrossing> LevelCrossings;
	// [level crossings] The game has the crossings' kit (Tools/props/make_level_crossing.py's models): the code-made
	// barrier and lights (CrossingBarrier) aren't drawn, ATorqueLevelCrossing stands the real ones up instead.
	bool CrossingKit = false;
	static constexpr double RailBedHalf = 5.0;
	static constexpr double RailStep = 4.0, RailGauge = 1.435;
	// A station's platform: along line Line from point First to Last, on the Side (+1 the left going
	// along, -1 the right), with its name board.
	struct Platform
	{
		int Line = 0, First = 0, Last = 0;
		double Side = 1.0;
		std::string Name;
		bool Halt = false;    // [roads stage 8] a halt: the platform and its board, no more
		bool Built = false;   // [roads stage 8] the map has a building behind it (the station's own): none drawn
	};
	std::vector<Platform> Platforms;
	// The nearest railway to (X, Z) within 40 m: how far, its ballast's top there, what it is on there
	// (as RailLine::On), and whether it is disused. False with none that near.
	bool NearRail(double X, double Z, double& Dist, double& Top, int& On, bool* Disused = nullptr, double* Room = nullptr) const;
	// What the road's surface is made of (Ride UK stage 7: WorldRideSurface.cpp), as the tyres feel it
	// and RoadTile draws it: patches of newer or older tarmac in the wheel tracks, the black tar banding
	// round them and along old seams (slick in the wet), grit down the middle of a single-track lane,
	// iron covers in a town's road, the steel joints at a bridge's ends. Each mark lies From..To along
	// road Which and Left..Right across it (m from its middle, + the left), Lift above or below it.
	// [OSM stage 5] MarkPaint a mapped line or crossing's paint (slick), MarkHump a hump, table or cushion
	// (its Peak and profile: RoadX), MarkBay a passing place's tarmac beyond the edge.
	enum ERoadMark { MarkPatch, MarkTar, MarkGrit, MarkCover, MarkJoint, MarkPaint, MarkHump, MarkBay };
	struct RoadMark
	{
		int Kind = MarkPatch;
		double From = 0.0, To = 0.0, Left = 0.0, Right = 0.0, Lift = 0.0;
		// ([junction priority 168] a line slanting across the road: Left and Right are at From, and move this
		// far to the left for each metre along; LeftAt and RightAt give them anywhere)
		double Skew = 0.0;
		bool EdgeLine = false;   // ([junction priority 168] a through road's edge line: none along a town street's kerbs)
		double LeftAt(double Along) const { return Left + Skew * (Along - From); }
		double RightAt(double Along) const { return Right + Skew * (Along - From); }
		float Colour[3] = {0.0f, 0.0f, 0.0f};
		// ([OSM stage 5] a hump's rise: Peak m at its top, over Start..Start + Length along; Flat the
		// share of it that is level on top, 0 a round hump, a table's most of it)
		double Peak = 0.0, Start = 0.0, Length = 0.0, Flat = 0.0;
		double RiseAt(double Along) const;
	};
	std::vector<MappedMark> MainMapped;
	// The region's mapped points on the edges OnRoad says are this road's, along its points.
	// (OnlyEdge >= 0: that edge's alone, OnRoad unread)
	void MapPoints(const std::vector<RoadPoint>& Pts, const std::vector<char>& OnRoad, std::vector<MappedMark>& Out, int OnlyEdge = -1) const;
	// The marks of a road's mapped points between From and To (RoadMarks adds them to its own).
	void MappedMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const;
	void RoadMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const;
	// How far over the road RoadTile lays a kind of mark (a hair, so it shows; the paint over them all).
	static double MarkUp(int Kind);
	// The surface under a wheel at (X, Z): Kind 0 tarmac, 1 slick (paint or tar banding: wet, they let
	// go), 2 grit, 3 steel (a cover, a joint, a level crossing's rails); Lift how far it stands proud of
	// the road (m, - sunk), eased in over a few centimetres. Tarmac and 0 off the road.
	struct RideSurface
	{
		int Kind = 0;
		double Lift = 0.0;
		double Drawn = 0.0;   // how far over the road the mark there is drawn (MarkUp): a ray hits that
	};
	enum ESurface { SurfTarmac, SurfSlick, SurfGrit, SurfSteel, SurfGravel };   // [OSM stage 1] gravel: a gravel road's own surface
	RideSurface SurfaceAt(double X, double Z) const;
	// The sat nav (Ride UK stage 7: WorldRideNav.cpp). The turns along the ride's road: at each junction
	// where it leaves the road ahead (or the road it is on bends away from the way on), how far along,
	// which way (-3 sharp left, -2 left, -1 bear left, 0 straight on, 1 bear right, 2 right, 3 sharp
	// right) and the road it is onto; the finish last (Turn Finish).
	struct RideTurn
	{
		double Along = 0.0;
		int Turn = 0;
		std::string Onto;
		int Exit = 0;   // [OSM stage 5B] at a roundabout, which exit (1 the first), 0 not a roundabout
	};
	static constexpr int Finish = 9;
	static constexpr double GiveWayWarn = 160.0;   // [junction priority 168] (the sat nav says a give-way or stop line this far ahead)
	std::vector<RideTurn> Turns;
	std::vector<double> RingGiveWays;   // [OSM stage 5B] how far along the ride's road it gives way onto a roundabout
	// [junction priority, Nov 19] (beenster: "Our routed road is THE road that everything else comes from.
	// This is wrong.") Who gives way at each junction of the map the ride passes (three or more roads, not a
	// roundabout), as on the real road and whichever way the ride goes: the through road is the pair of its
	// roads of the best class running on most nearly straight (within JunctionStraight, or the only two of
	// that class within JunctionBent), and every other road there gives way to it. Where the ride comes in
	// or goes out by one of those, its own road stops at the through road's edge: give-way lines across the
	// lane coming up to it, none of its own lines across the through road, and a junction's tight corner
	// where it turns (not a bend faired as the road's class would be).
	struct JunctionRule
	{
		int Node = -1, Came = -1, Onto = -1;
		int ThroughA = -1, ThroughB = -1;   // the through road's two roads there
		double RawAlong = 0.0;    // where it is along the map's line of the ride
		double Along = -1.0;      // and along the road as laid (PlaceJunctions)
		double MajorHalf = 0.0;   // half the through road's width
		double Turn = 0.0;        // the ride's turn there (radians, + to the left)
		bool CameMinor = false, OntoMinor = false;   // the ride comes in, goes out, by a road that gives way
	};
	static constexpr double JunctionStraight = 0.7, JunctionBent = 1.2, JunctionTurn = 0.6, JunctionCorner = 25.0, JunctionFair = 2.0;
	static constexpr double JunctionSquare = 15.0;   // (each road laid straight into a T the ride turns at, this far)
	std::vector<JunctionRule> Junctions;
	// (each road's end at a junction of the ride's: node * 4000037 + edge; 1 it gives way, 2 it is the through road)
	std::unordered_map<long long, unsigned char> ArmRank;
	unsigned char ArmRankOf(int Node, int Edge) const;
	std::vector<std::pair<double, double>> JunctionNoLines;   // (stretches of the ride's road across a through road: none of its lines)
	bool LinesStopAt(double Along) const;
	int JunctionGiveWayCount = 0;   // (give-way lines the rule painted on the ride's own road)
	// [junction priority 168] (beenster, build 167: "You are still creating a road from A to B (our road) which
	// has priority. Make sure it feels like we have to respect the law of the road more".) Where the ride
	// comes in or goes out by a road that gives way, the through road's own lines painted on across the
	// ride's tarmac (its centre line as Britain's junction warning line, 6 m marks and 3 m gaps, and at a T
	// its far edge line unbroken), so the through road visibly carries on and the ride's road meets it;
	// sorted by From. RoadMarks adds them to the ride's road's own.
	std::vector<RoadMark> JunctionPaint;
	int JunctionThroughCount = 0;   // (junctions whose through road was painted across)
	// (a through road's arm off the ride's road, by its edge * 2 + which end: its own lines run on to its
	// mouth, this far along it from that end, not stopping 12 m short as a side road's do)
	std::unordered_map<long long, double> ThroughEnds;
	// [junction priority, true T] (beenster, Nov 21: the main road whole through the junction, the ride's road
	// meeting it square.) At a T where the ride's road gives way, the through road's far edge as it runs from
	// its arm's mouth on across to the ride's road again: drawn in place of that mouth's rounded corner on the
	// far side, the tarmac out to it, and the ride's own kerb or verge between taken away. By the arm's edge
	// * 2 + which end: which corner of its mouth (Sigma, as MouthCorner's), the line from the ride's road's
	// edge (first) to the arm's (last), and how far along the ride's road its two ends lie.
	struct FarEdge
	{
		double Sigma = 1.0;
		double RideFrom = 0.0, RideTo = 0.0;
		std::vector<P2> Pts;
	};
	std::unordered_map<long long, FarEdge> FarEdges;
	// [square T] (beenster, Nov 22: "build the A road first respecting its direction, then build the b road
	// and DO NOT change the A road".) Where the ride turns at a T to or from a road that gives way, the
	// through road as one straight line through the junction, fitted to the map's line of both its arms: the
	// ride's road laid along it on its own side, and the through road's other arm (a side road), by its edge,
	// laid along it into the junction (Hub, the junction on that line; Dir, along that arm, away).
	struct ThroughStraight
	{
		P2 Hub, Dir;
	};
	std::unordered_map<int, ThroughStraight> ThroughLines;
	bool GiveDrawn[2] = {false, false};   // [square T] (PaintThrough's last: the give-way lines painted by it, coming in, going out)
	double GiveSlant[2] = {0.0, 0.0};   // [square T] (PaintThrough's last: the give-way line's Slant coming in, going out)
	int JunctionSignCount = 0;      // (Give Way and Stop signs stood by the ride's own lines: IndexFurniture)
	void PaintThrough(const JunctionRule& J, double& In, double& Out, double& GiveIn, double& GiveOut);
	void FindPriorities(int From, const std::vector<int>& Path);
	static bool NoJunctionPriority;   // (the tests' before-and-after: every junction as the ride's road had it)
	void PlaceJunctions(double RawLength);
	// Places to stop along it from the map: fuel, cafes, pubs, viewpoints, lay-bys, within StopReach of
	// the road; how far along, and which side (+ the left).
	// [OSM stage 1] inns, campsites and castles too.
	enum EStop { StopFuel, StopCafe, StopPub, StopView, StopLayby, StopInn, StopCamp, StopCastle, StopKinds };
	struct RideStop
	{
		double Along = 0.0, Off = 0.0;
		int Kind = StopCafe;
		std::string Name;
	};
	static constexpr double StopReach = 120.0;
	std::vector<RideStop> Stops;
	static const char* StopName(int Kind);   // "Fuel", "Cafe", "Pub", "Viewpoint", "Lay-by", "Inn", "Campsite", "Castle"
	// [places stage 2] Somewhere to arrive at each place worth riding to (Region::Sights: those with a
	// Wikipedia page, which the info panel suggests, and the named ones of the kinds a rider rides out
	// to; World/WorldRidePlaces.cpp): the map's car park for it within PlaceCarParkReach, else its lay-by
	// within PlaceLaybyReach, else a pull-off made beside the road nearest it (within PlaceReach: gravel,
	// PullOffLength by up to PullOffDepth, where the ground is level, out of the walls, off junctions,
	// bridges and other roads); in a built-up street just the board at the kerb. A name board stands
	// at each (furniture: PlaceBoard), facing the road. Places close together share one stop.
	enum EPlaceStop { PlaceMade, PlaceCarPark, PlaceLayby, PlaceStreet };
	struct PlaceStop
	{
		std::vector<int> Sights;   // into Area.Roads.Sights: the places it serves (the first named on its board)
		int How = PlaceMade;
		P2 At;                     // where to stop: the sat nav's arrival
		P2 Board, Face;            // its board, and the way the board faces (towards the road)
		int Edge = -1;             // the road it is beside (Area.Roads.Edges)
		double Depth = 0.0;        // a made pull-off's depth (m)
		P2 RoadMid, RoadDir, Out;  // a made pull-off's road middle beside its middle, the road's way there, and out towards it (level, unit)
		std::vector<P2> Ring;      // a made pull-off's outline (a field area: FieldPullOff), else empty
		P2 View;                   // [places stage 7] the spot with a good view of the place, its path's end (HasView)
		bool HasView = false;
	};
	std::vector<PlaceStop> PlaceStops;
	static constexpr double PlaceReach = 1500.0, PlaceCarParkReach = 400.0, PlaceLaybyReach = 300.0;
	static constexpr double PullOffLength = 22.0, PullOffDepth = 6.0;
	// What IndexPlaces found: places looked at, stops by EPlaceStop, places with no road near enough.
	int PlacesSeen = 0, PlacesFar = 0, PlaceStopCount[4] = {};
	int PlacePaths = 0, PlaceGates = 0;   // [places stage 7] paths made in to the places, kissing gates on them
	void NoteSites();     // [places stage 5] (the start of IndexPlaces: SiteNotes, SightNote)
	void IndexPlaces();
	// The place stop whose arrival is within Within m of (X, Z), nearest first; -1 none.
	int PlaceStopAt(double X, double Z, double Within = 30.0) const;
	// The name a stop's board shows ("Bolton Castle"), "" none.
	std::string PlaceStopName(int Stop) const;
	std::string PlaceStopNote(int Stop) const;   // [places stage 5] its site note's words, "" none
	// [places stage 7] What a visited list keeps a stop's place by: its Wikidata id, else its name and where
	// it is ("Castlerigg Stone Circle@54.603,-3.098"), "" none.
	std::string PlaceStopKey(int Stop) const;
	// The stop whose view spot (PlaceStop::View) is within Within m of (X, Z); -1 none.
	int PlaceViewAt(double X, double Z, double Within = 8.0) const;
	// Whether (X, Z) is on a pull-off made for a place (gravel under the tyres).
	bool OnPullOff(double X, double Z) const;
	std::unordered_map<long long, std::vector<int>> PlaceGrid;   // stops by the 64 m square their arrival is in
	// [roads stage 7] Petrol stations (OSM stage 7C: World/WorldRideTownLife.cpp): at each of the map's fuel
	// stops near enough a road, a tarmac forecourt from the road's edge (made as a pull-off is: where nothing
	// stands in the way and the ground is level enough), a canopy on its posts over two or three pump
	// islands, a kiosk behind where the map has no building for one, and a tall price board by the road. No
	// maker's name or colours: each canopy's band is one plain colour picked by hash. Stopped by a pump, the
	// tank fills (ForecourtAt). Not on motorways: their services are stage 8's.
	struct Forecourt
	{
		P2 RoadMid, RoadDir, Out;   // the road's middle beside its middle, the road's way there, out towards it (level, unit)
		double Half = 0.0;          // the road's half width there
		double Depth = 0.0, Length = 0.0;   // from the road's edge back, and along the road (m)
		std::vector<P2> Ring;       // its outline (a field area: FieldForecourt)
		P2 Canopy;                  // the canopy's middle
		double CanopyLong = 12.0;   // along the road (8 m deep)
		int Islands = 2;
		bool Kiosk = true;          // false: the map has a building there already
		double Base = 0.0;          // the ground's height under the canopy
		uint32_t Seed = 1;
		std::string Name;
		// [garage] The workshop unit you ride into to work on your bike (beenster, Nov 17: room for three
		// bikes, the spanner and a wall-mounted parts computer). Its door faces the road (-Out); GarageU is
		// its middle along the road from the road's edge's frame (as the canopy's), GarageV its front's
		// distance out from the road's edge. Beside the kiosk in the row behind the canopy where there's
		// one (the kiosk shortened to share it), else beyond the canopy's end, set back from the lane.
		bool Garage = false;
		double GarageU = 0.0, GarageV = 0.0;
	};
	// [garage] A forecourt's garage unit in the world: the middle of its floor, the way its door faces
	// (out of it, level, unit), the floor's height (just over the ground's highest under it), the
	// ground's 1.5 m out of its door (a ramp joins them), its forecourt.
	struct GarageSpot
	{
		P2 Middle, Door;
		double Base = 0.0, DoorGround = 0.0;
		int Court = -1;
	};
	static constexpr double GarageWide = 7.5, GarageDeep = 6.5;   // outside, along its front and front to back (m)
	bool GarageOf(int Court, GarageSpot& Out) const;
	// The garages with their middles within Reach m of (X, Z).
	void GaragesNear(double X, double Z, double Reach, std::vector<GarageSpot>& Out) const;
	std::vector<Forecourt> Forecourts;
	std::unordered_map<long long, std::vector<int>> ForecourtGrid;   // by the 64 m square each's canopy is in
	int FuelSeen = 0;                       // fuel stops looked at (Forecourts.size() of them made)
	static constexpr double ForecourtDepth = 24.0, ForecourtLeast = 13.0;
	// [roads stage 7] The smaller things of town and country life from the map (OSM stage 7F, 7G): each
	// campsite's pitches (touring caravans and tents, a static caravan or two on the bigger ones) round its
	// point on open level ground, fountains (a stone basin and its water, a pillar and bowl in the middle,
	// as big as the map's outline when it has one) and electric car chargers (a post each).
	enum EThing { ThingTent, ThingCaravan, ThingStatic, ThingFountain, ThingCharger, ThingKinds };
	struct TownThing
	{
		int Kind = ThingTent;
		P2 At;
		double Yaw = 0.0, Base = 0.0, Size = 0.0;   // which way it faces; the ground's height; a fountain's radius
		uint32_t Seed = 1;
	};
	std::vector<TownThing> TownThings;
	std::unordered_map<long long, std::vector<int>> ThingGrid;   // by the 64 m square each is in
	int ThingCount[ThingKinds] = {};
	int CampsSeen = 0, CampsMade = 0;
	// [roads stage 8] The airfields' runways and taxiways (tarmac with their paint, or a mown strip for a
	// short one no wider than a farm strip) and the piers (a deck on piles at its higher end's height, rails
	// either side), from the map's lines; aprons are tarmac fields (FieldForecourt's).
	struct AirLine
	{
		int Line = -1;          // into Area.Roads.Scenery.Lines
		int Kind = 0;           // 0 a hard runway, 1 a grass one, 2 a taxiway, 3 a pier
		double Width = 0.0, Deck = 0.0, Length = 0.0;   // a pier's deck height
	};
	std::vector<AirLine> AirLines;
	std::unordered_map<long long, std::vector<int>> AirGrid;   // by every 64 m square a bit of each passes
	std::vector<std::vector<P2>> Aprons;   // the airfields' aprons' outlines (fields)
	void IndexTownLife();   // the forecourts and the things
	// The forecourt with a pump within Within m of (X, Z), -1 none (the tank fills there).
	int ForecourtAt(double X, double Z, double Within = 4.0) const;
	bool OnForecourt(double X, double Z) const;
	void TownLifeTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// [car parks] The map's car parks (World/WorldRideCarParks.cpp): each one's tarmac laid over its ground with
	// its bays painted into it, flush (no strips over it), in rows along its aisles (a service road through it, or
	// its longest side); low kerbs round it where no road runs by; a blue P sign and a pay machine at its way in;
	// lamps over a big one; and parked cars in about a third of its bays, solid to the bike (ParkedCarsNear).
	// Gravel ones get no paint or kerbs; "grass", "street_side" and "layby" ones are left as the ground is.
	enum ECarParkSurface { ParkTarmac = 0, ParkSetts, ParkGravel };
	struct CarPark
	{
		std::vector<P2> Ring;
		double MinX = 0.0, MaxX = 0.0, MinZ = 0.0, MaxZ = 0.0, Area = 0.0;
		int Surface = ParkTarmac;
		P2 U = P2(1.0, 0.0), V = P2(0.0, 1.0);   // the rows of bays run along U; V across them
		double PhaseU = 0.0, PhaseV = 0.0;       // (along U, V from the origin) a bay's side; an aisle's middle
		P2 Entry, EntryIn;                       // its way in, and the way into it there
		bool HasEntry = false;
		uint32_t Seed = 0;
		// [build 157 feedback] Its level: the plane its ground is laid to (GroundOf, CarParkLevel), fitted to the
		// land in it and no steeper than ParkFallMost; LevelY at LevelAt, rising LevelGx, LevelGz a metre (nan: none).
		P2 LevelAt;
		double LevelY = std::nan(""), LevelGx = 0.0, LevelGz = 0.0;
	};
	struct ParkBay
	{
		P2 Mid, Along, In;   // its middle; along its row; from the aisle into it
		bool Car = false;    // a car parked in it
		uint8_t Pick = 0;    // which car
		bool Badge = false;  // a blue badge bay by the way in (its lines yellow, never a car)
		int Row = 0, Col = 0;   // which row (two to an aisle: 2k below it, 2k + 1 above) and which along it
	};
	static constexpr double ParkBayWide = 2.4, ParkBayLong = 4.8, ParkAisle = 6.0;
	std::vector<CarPark> CarParks;
	std::unordered_map<long long, std::vector<int>> CarParkGrid;   // by every 64 m square each one covers
	void IndexCarParks();
	// [circuits] Race circuits (WorldRideCircuits.cpp): whether a road is a circuit's track or pit lane; its
	// paint (red and white kerbs on its corners, the start line, the pit lane's lines) in place of a public
	// road's; the ground's colour on a gravel trap or paved run-off, and what the tyres feel there (2
	// gravel, 1 tarmac, -1 not a circuit's). IndexCircuits once the ride is laid out.
	bool CircuitRoad(int Which) const;
	void CircuitMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const;
	bool CircuitTint(double X, double Z, float* Rgb) const;
	int CircuitGroundAt(double X, double Z) const;
	void IndexCircuits();
	std::shared_ptr<const CircuitIndex> CircuitIx;
	// [circuits] The nearest circuit's start line within Within (m) of (X, Z): a place on its track Back metres
	// before the line, the heading it's raced at there (sim: 0 faces -z, + left), and its name. False with none.
	// [circuits] The circuits' trackside kit on a square of ground (added to PropsTile's).
	void CircuitProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const;
	// [caravans] The campsites' caravans in the square, as models (when the kit has them: PropModelled).
	void TownLifeProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const;
	void IndexTrackside(CircuitIndex& Ix) const;
	// [circuits stage 6] A circuit's famous corners' banks (CircuitNote::Corners) into its track's Banks (RoadLine::BankOf's).
	// [circuits, build 168] The ground under a circuit's track, smoothed: its height averaged over 30 m round (X, Z),
	// so the track laid on it (smoothed again along its length) has no bumps at all, and each junction agrees.
	// [circuits, build 168] The circuits' gravel traps drawn as sand (beenster on 167: "Make the gravel traps look
	// like sand"): each one's ground laid over in M_RideRoad's sand (its vertex colour the key CircuitSand), the
	// finest squares only (further off, the land's own tint), into a square's standing things.
	void CircuitSandTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	double CircuitGround(double X, double Z) const;
	void CircuitBanks(int Edge, const std::vector<P2>& Line, std::vector<double>& Banks) const;
	bool CircuitStartNear(double X, double Z, double Within, double Back, P2& At, double& Heading, std::string& Name) const;
	// A car park's bays (and its cars), the same wherever asked: clear of its roads (laid round it first),
	// its buildings, water and edge.
	void CarParkBays(int Which, std::vector<ParkBay>& Out) const;
	void LayOutBays(int Which, std::vector<ParkBay>& Out) const;   // (CarParkBays' work, before it is kept)
	void CarParkStands(double X0, double Z0, double X1, double Z1, std::vector<Building>& Out) const;   // (the buildings over it as drawn)
	void CarParksTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// The parked cars within Reach of (X, Z) as boxes for the bike to bump into: the middle, the way its nose
	// points (a level unit direction), half its length and width, its foot and roof heights.
	struct ParkedCarBox
	{
		double X = 0.0, Z = 0.0, Ux = 1.0, Uz = 0.0, HalfLong = 2.1, HalfWide = 0.9, Y0 = 0.0, Y1 = 1.5;
	};
	void ParkedCarsNear(double X, double Z, double Reach, std::vector<ParkedCarBox>& Out) const;
	bool InCarPark(double X, double Z) const;   // (in a tarmac or setts one: no path drawn over it)
	int CarParkSurfaceAt(double X, double Z) const;   // (ECarParkSurface of the one at (X, Z), -1 none or too steep)
	// [build 157 feedback] (beenster: "strange carpark", Sandgate: bays and cars draped down an embankment) The
	// land under and round a car park as built: within ParkCutMost of its level laid to it (cut or filled), the
	// rest of a bank kept as a bank; outside its edge easing back to the land 2 in 3. Lie: the land as it lies.
	double CarParkLevel(double X, double Z, double Lie) const;
	static constexpr double ParkFallMost = 1.0 / 15.0, ParkCutMost = 2.5, ParkLevelReach = 6.0;
	bool CarParkSteep(double X, double Z, std::unordered_map<long long, double>* Cache = nullptr) const;   // (a bank inside one: land, not car park)
	static constexpr double ParkLift = 0.05;          // its surface over the drawn ground (the wheels ride on it)
	int CarParkCount[3] = {};   // by surface, indexed (for the desk test)
	// Whether the bays have cars in (the game: when the kit has the car models and -TorqueRideParkedCars=0 isn't
	// given, the hub's frame rate A/B; the desk test draws plain shapes for them).
	bool ParkedCars = true;
	static constexpr int ParkedCarsMost = 150;   // in any one car park
	// (each car park's bays once laid out, kept: the squares and the bike's contacts ask again and again)
	struct CarParkMemo;
	std::shared_ptr<CarParkMemo> ParkMemo;
	// [places stage 3] The places drawn by their kind's recipe (World/WorldRidePlaces.cpp: PlacesTile):
	// stone circles and standing stones in the local stone, crosses and memorials on their steps,
	// obelisks, small churches where the map has no building for one (its tower at the west end),
	// towers, lighthouses (lit after dark), tower windmills with their sails, and chimneys. A church the
	// map has a building for gets its tower (BuildingRole 1); the building under a tower, lighthouse,
	// windmill or chimney is left out (UnderPlace) and the recipe drawn at its size. Seen from afar: the
	// tall ones. Detail as SkylineTile's.
	enum ERecipe { RecipeCircle, RecipeStone, RecipeCross, RecipeObelisk, RecipeMemorial, RecipeChurch, RecipeTower, RecipeLighthouse, RecipeWindmill, RecipeChimney,
		RecipeCastleRuin, RecipeKeep, RecipeHillfort, RecipeRuin, RecipeKinds };
	// [places stage 4] And the places that shape the land: a ruined castle's broken curtain walls round
	// its mapped outline with towers at its corners and its keep, a castle with no building for it as a
	// keep, a hillfort's grassed ramparts round its outline (or a ring), a ruin's low walls; and the dams
	// (WildTile) as masonry walls with a battered face and parapets, or long earth banks with a wave wall.
	struct PlaceRecipe
	{
		int Sight = -1, Kind = 0;
		P2 At;
		double Yaw = 0.0;      // the way it faces (radians from +x towards +z): its road, or a church's east
		double Radius = 0.0;   // its size where the map gives it (m), 0 none
		double Height = 0.0;   // the map's height, 0 none
		double Base = 0.0;     // the ground under its middle
		double Claim = 0.0;    // the buildings within this of its middle are its own (not drawn), 0 none
		uint32_t Seed = 1;
		// [places stage 5] What its site note says (Data/RideUK/landmarks_uk.json), else the recipe's own:
		int Count = 0;         // a circle's stones
		int Banks = 0;         // a hillfort's ramparts
		int Spire = -1, Banded = -1;   // a church's spire, a lighthouse's red bands (1 yes, 0 no, -1 the recipe's choice)
		bool Lintels = false, Fallen = false, Unlit = false;
		std::string Note;      // the note's words about it ("38 stones, 3,000 years old")
		// [places stage 6] Its own model, drawn instead of the recipe (null none), turned Turn (radians,
		// clockwise seen from above).
		std::shared_ptr<const SiteModel> Model;
		double Turn = 0.0;
	};
	// [OSM stage 5B] The roundabouts: the map's roundabout edges joined into closed rings (IndexRings).
	// [towns stage 7] Whether a model's own parts (not its windows or lights) stand over (X, Z), Spare m round.
	static bool ModelCovers(const PlaceRecipe& R, double X, double Z, double Spare);
	struct RoadRing
	{
		std::vector<int> Edges, Nodes;   // round it, each edge from the node before it
		std::vector<P2> Line;            // its mapped line round, closed (the first point not repeated)
		P2 Mid;
		double Radius = 0.0;             // the mean distance of its line from Mid (m)
		double Stray = 0.0;              // how far its line strays from that circle at most (m)
		bool Circle = false;             // near enough a circle to be drawn as one
		bool Mini = false;               // small enough to be a mini-roundabout
		bool Clockwise = false;          // its edges run clockwise seen from above
		double Half = 0.0;               // [roundabouts] the ring's half width, the same all round (drawn as a circle)
		// [roundabouts round 2] (beenster: "roundabouts aren't always round, large ones can be elongated
		// circles and other shapes") A ring that isn't near a circle but goes once round its middle without
		// doubling back (an oval, a rounded triangle or square) is drawn smoothed to its own shape: its
		// middle line's distance from Mid by bearing (Prof, RingProfBins bins round from east, empty for
		// a circle), Reach the farthest it gets. Circle is then set too: drawn as a smooth ring.
		std::vector<double> Prof;
		double Reach = 0.0;
		bool Shaped = false;
		// Its middle line's distance from Mid at a bearing (radians, atan2(z, x)), and how fast that changes.
		double RAt(double Angle) const;
		double RSlope(double Angle) const;
		// A point Off metres out (- in) from its middle line, square to it, at a bearing.
		P2 At(double Angle, double Off) const;
		// How far a point is out from its middle line (- in towards the island), square to it.
		double OffOf(const P2& P) const;
		// The least distance of its middle line from Mid.
		double Least() const;
	};
	static constexpr int RingProfBins = 180;
	static constexpr double RingIslandLeast = 6.0;   // [roundabouts round 2] a shaped ring's island at its narrowest (m)
	std::vector<RoadRing> RoadRings;
	std::unordered_map<int, int> RingOfNode;   // a node on a ring: which
	// ([roundabouts] RingRoundLeast was 12 m: the compact rings under it were left as the map drew them, a
	// knot of tarmac with no island; beenster on 154: "a lot of poor implementations of roundabouts")
	static constexpr double RingMaxRadius = 90.0, RingMiniRadius = 6.0, RingRoundShare = 0.12, RingRoundLeast = 8.0;
	// [roundabouts] The ring's markings and island: corners onto it rounded to RingFlare m (6 m elsewhere),
	// the island's kerb (RingKerb high, its face a 45 degree slope as the town kerbs), an overrun apron of
	// setts RingApron wide round an island under RingApronUnder across, the arms' painted splitters.
	static constexpr double RingFlare = 15.0, RingKerb = 0.125, RingApron = 1.6, RingApronUnder = 14.0;   // ([roundabouts round 2] under 9 m: none ever had one)
	// [roundabouts round 2] How far out from a ring's outer edge roads are checked for grass shoulders on tarmac (m).
	static constexpr double RingClearReach = 30.0;
	static constexpr double RingMouthClear = 10.0;   // [roundabouts round 4] no road shoulder or pavement this near a ring's edge but the ring's own (m)
	static constexpr double RingFlareLeast = 6.0;   // [roundabouts 156] the flare onto a small ring (m); RingFlare is a big one's
	static constexpr double RingGapFill = 8.0;
	static constexpr double RingArmLines = 25.0;   // [roundabouts 156] edge lines this far along each one-way road off and on to a ring (m)
	static constexpr double RingCornerClear = 18.0;   // [roundabouts 156] no junction corner kerb or verge this near a ring's edge (m): its arms' pavements stop short of the mouth
	static constexpr double RingBarrierClear = 40.0;   // [roundabouts 156] no central reservation barrier this near a ring's edge (m)   // [roundabouts round 4] near a ring, a gap to another road this narrow or less is tarmac (m)
	bool ShapeRing(RoadRing& R) const;
	// [roundabouts round 2] The roads onto a ring drawn as one: where each meets its outer edge, which way it
	// heads out, how wide, and which way its traffic may go (+1 out only, -1 in only, 0 both).
	struct RingArm
	{
		P2 At, Out;
		double Angle = 0.0, Half = 0.0;
		int Way = 0, Edge = -1, Node = -1;
		std::vector<P2> Line;   // (its middle out from the ring's edge)
	};
	std::vector<RingArm> RingArms(const RoadRing& R) const;
	// [roundabouts round 2] A ring's own furniture: a chevron board on its island facing each way on, and
	// on a bigger ring's ways on a roundabout warning triangle before it (NetworkFurniture's, once a ring).
	std::vector<Furnish> RingFurniture(int Ring) const;   // [roundabouts round 2] give a ring that isn't round its own smooth shape, if it has one
	std::unordered_map<int, int> RingOfEdge;   // [roundabouts] an edge on a ring drawn as a circle: which
	std::unordered_map<long long, std::vector<int>> RingCells;   // [roundabouts] the circle rings by 64 m square they reach
	double RingHalfOf(int Edge, double Half) const;   // [roundabouts] its half width: the ring's, if on one drawn as a circle
	int PaintedRingNear(const P2& At, double Pad) const;   // [roundabouts] the circle ring whose tarmac (and Pad) is at a point, -1 none
	void RingPaint(const RoadRing& R, RideMesh& Out) const;   // [roundabouts] its lines, give-ways and splitters
	void IndexRings();
	int RoundRings();   // the rings near a circle made true arcs (before any road is laid): how many
	static constexpr double RingStep = 2.0;
	int RingAt(int Node) const;   // the ring a node is on, -1 none
	void RingsTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;   // the islands
	bool OnRoundRing(int Edge) const;   // whether an edge is part of a ring drawn as a circle
	std::vector<unsigned char> RoundEdge;   // by edge: 1 on a ring drawn as a circle (IndexRings)
	// [OSM stage 5C] A dual carriageway's other half, across from a point of one half (DualAcross): on
	// Side (+1 the half's left as its points run, -1 its right), its road (Which), the grass between the
	// two halves' edges (Gap, m), and the other half's surface at its near edge (Far). The reservation
	// between them lies from one edge's height to the other's, MedianSink under, the barrier down its
	// middle where Gap is between MedianLeast and MedianMost.
	struct DualHit
	{
		int Side = 0, Which = -1;
		double Gap = 0.0, Near = 0.0, Far = 0.0;
		double FarAlong = 0.0;   // how far along its own road the other half is there
	};
	bool DualAcross(int Which, int Index, int OnlySide, DualHit& Out) const;
	static constexpr double EmbankBarrier = 3.0;
	// [OSM stage 5D] Town streets: a kerb KerbHigh high (its face a 45 degree slope) and a pavement out to
	// PaveWide from the road's edge, each side, in place of the verge (beenster Nov 14: one continuous kerb
	// and pavement each side of a built-up street, flat across the side roads' mouths; none in the country).
	// ([build 154] beenster: pavements "too wide": 1.8 m was; [build 155] "lower and 10% narrower": 0.125 m and 1.4 m were)
	static constexpr double KerbHigh = 0.09, PaveWide = 1.26;
	static constexpr double PaveBack = 1.2;   // ([build 155] "make the ground meet the other side of the pavement": its back a slope down to the ground as drawn, this wide; [156] drawn as the land, Turf)
	static constexpr double CornerBack = 0.8;   // ([156] round a corner, as it was: wider, its back folds on a tight one)
	// [OSM stage 5D] Traffic lights: a fixed cycle of SignalCycle seconds from the shared clock (the master
	// plan's open question: until it is answered, nothing happens to a rider who goes through on red).
	// The lamps' codes in RideMesh::Signals' UV0 y.
	enum ESignalLamp { SignalRed = 0, SignalAmber = 1, SignalGreen = 2, SignalBeacon = 3 };
	static constexpr double SignalCycle = 60.0;
	static bool StreetClass(const std::string& Class);   // a class that can be a town street
	std::vector<char> StreetsAlong(int Which) const;      // each point of a road: a town street's (1) or not   // (a fast road this far over the land beside it has a barrier on that side)
	static constexpr double MedianSink = 0.12, MedianLeast = 1.2, MedianMost = 20.0, MedianWidest = 40.0, MedianPad = 2.5;
	// The reservation's ground at a point Hit (NearestOf's, of a dual carriageway's half) finds, if it is
	// between the two halves; NaN otherwise.
	double MedianGround(const NearHit& Hit) const;
	static double MedianAcross(const DualHit& D, double Out);   // (Out m from the near half's edge)
	std::vector<PlaceRecipe> PlaceRecipes;
	std::vector<SiteNote> SiteNotes;   // [places stage 5] the notes for this ride's places (IndexPlaces)
	std::vector<int> SightNote;        // for each of Area.Roads.Sights its note in SiteNotes, -1 none
	std::unordered_map<long long, std::vector<int>> RecipeGrid;   // by the scenery's 256 m squares
	int RecipeCount[RecipeKinds] = {};
	int ModelCount = 0;   // [places stage 6] the places drawn by their own model
	std::vector<unsigned char> TowerTaken;   // the towns' tall buildings a recipe stands in for (TowersTile leaves them out)
	void IndexPlaceRecipes();   // (after IndexTowers)
	bool UnderPlace(double X, double Z) const;
	void PlacesTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const;
	// What the sat nav says at (X, Z): how far along the ride's road the rider is (near Was, the last
	// answer, where the road passes itself), the next turn and the distance to it, the next stop of
	// each kind ahead, and how far to the finish; OffRoute when the rider is more than 40 m off it.
	struct RideNav
	{
		double Along = 0.0, ToFinish = 0.0;
		bool OffRoute = false;
		int Turn = Finish;
		double ToTurn = 0.0;
		std::string Onto;
		int Exit = 0;   // [OSM stage 5B] the next turn's roundabout exit, 0 none
		// [junction priority 168] the next give-way or stop line across the ride's own lane: how far (-1 none
		// within GiveWayWarn), and whether it is a stop
		double ToGiveWay = -1.0;
		bool GiveWayStop = false;
		double ToStop[StopKinds] = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};   // -1: none ahead
		std::string StopCalled[StopKinds];
		// [OSM stage 1] The road's facts here: its limit as a sign shows it (mph in Britain, rounded to
		// 5; km/h elsewhere; 0 for none shown, as on a farm track), whether that is Britain's national
		// limit, and whether the map said it or it was assumed (built up 30, else 60 or 70); the road's
		// name as the map has it; and the place ("North Yorkshire, England").
		int Limit = 0;
		bool LimitMph = true, LimitNational = false, LimitAssumed = true;
		std::string Road, Place;
	};
	RideNav NavAt(double X, double Z, double Was = -1.0) const;
	// [ride anywhere] The way back when the rider has left the ride's road: from the junction ahead of
	// them on the road they are on (Heading: sim, 0 faces -z, + left) by the map's roads to the junction
	// of the ride's road ahead of Was that leaves least to ride to the finish, with its turns (the last
	// onto the ride's road there, Onto its name). Null when they are on no road, or none gets back.
	std::shared_ptr<const RideDetour> DetourFrom(double X, double Z, double Heading, double Was) const;
	// [ferries stage 5] Over the water: the way on from where the rider is (the junction ahead of them, as
	// DetourFrom) by the map's roads through the pins still to come to the finish, each pin an arrival; RejoinAlong
	// FinishM (nothing of the ride's road left). Null without a ferry, off any road, or no way on (WorldRideFerry.cpp).
	std::shared_ptr<const RideDetour> FerryOnward(double X, double Z, double Heading) const;
	// [waypoint] The way by a place the rider picked (the info panel's waypoint; beenster, build 146:
	// "add it as a waypoint and retain our final destination"): from where they are (on the ride's
	// road, from its next junction; on another road, from the junction ahead as DetourFrom) by the
	// map's roads to the junction nearest (ViaX, ViaZ), then on to the junction of the ride's road
	// ahead that leaves least to ride to the finish. Its turns as DetourFrom's, with an arrival
	// (Finish, onto ViaName) at the waypoint. Null with no junction within 3 km of it in the map
	// loaded so far, or no way there and back.
	std::shared_ptr<const RideDetour> DetourVia(double X, double Z, double Heading, double Was, double ViaX, double ViaZ, const std::string& ViaName) const;
	// The sat nav along a detour (as NavAt; OffRoute when more than 40 m off it).
	RideNav NavDetour(const RideDetour& D, double X, double Z) const;
	// The junctions of the ride's road, each with how far along it is and the edge it leaves by (-1 the last).
	struct PathNode
	{
		int Node = -1, Next = -1;
		double Along = 0.0;
	};
	std::vector<PathNode> PathNodes;
	void FindPathNodes(int From, const std::vector<int>& Path, double RawLength);   // ([OSM stage 6C] before the furniture; FindTurnsAndStops makes them again)
	// [OSM stage 1] The road's facts along the ride (World/WorldRideFacts.cpp), worked out when the ride
	// is planned. Facts: each stretch of the map's road the ride follows, From..To along it, with the
	// limit the map gives (km/h, 0 none), its name, class and whether it is gravel. Limits: the limit
	// all along it once settled (SettleLimits, after the built-up stretches are known): the map's where
	// it has one, else assumed (in Britain 30 mph built up, 70 on a motorway, else 60; abroad by class),
	// none on a track; a change kept only with 100 m or more each side.
	struct RoadFact
	{
		double From = 0.0, To = 0.0;
		int MappedKmh = 0;
		std::string Name, Class;
		bool Gravel = false;
		bool Setts = false;   // [towns stage 6]
	};
	std::vector<RoadFact> Facts;
	struct LimitStretch
	{
		double From = 0.0, To = 0.0;
		int Kmh = 0;
		bool Assumed = true;
	};
	std::vector<LimitStretch> Limits;
	void FindFacts(int From, const std::vector<int>& Path, double RawLength);
	void SettleLimits();
	const RoadFact* FactAt(double Along) const;
	const LimitStretch* LimitAt(double Along) const;
	// A limit as a sign here shows it (mph rounded to 5 in Britain, km/h elsewhere), and whether it is
	// Britain's national limit (60 or 70 mph).
	int ShownLimit(int Kmh, bool* National = nullptr) const;
	static int AssumedKmh(const std::string& Class, bool BuiltUp, bool Uk);
	// The county and country at a place of the ride's frame, from the cells' place profiles; "" if none.
	std::string PlaceAt(double X, double Z) const;
	void FindTurnsAndStops(int From, const std::vector<int>& Path, double RawLength);
	// [stage 8] Where the map is thin along the road (World/WorldRideNav.cpp): a town's ground with few
	// of its buildings drawn, and no tree heights at all.
	std::vector<ThinSpot> Thin;
	void FindThinData();
	// [stage 8] Britain's landmark bridges as hero pieces (World/WorldRideHero.cpp): where one of
	// Data/RideUK/landmark_bridges.json carries a road of the ride, its towers, cables, stays or arch
	// over the deck the road already has. Read once; each piece is a mesh of its own (coloured, about
	// the bridge's middle) and its name. Kinds: suspension, cable_stayed, arch.
	struct Landmark
	{
		std::string Name, Kind;
		Geo At;                  // about the middle of its main span
		double Span = 300.0;     // the main span (m), or each span between towers
		double Up = 60.0;        // how high its towers (or arch) stand over the deck (m)
		int Towers = 2;
		float Colour[3] = {0.32f, 0.33f, 0.33f};
	};
	static bool ReadLandmarks(const std::string& Text, std::vector<Landmark>& Out);
	struct HeroPiece
	{
		std::string Name;
		std::shared_ptr<RideMesh> Mesh;
	};
	void HeroBridges(const std::vector<Landmark>& Marks, std::vector<HeroPiece>& Out) const;
	// The ride to its finish, summed up. Cap is the fastest it is ridden anywhere (m/s; 70 mph); [OSM
	// stage 1] each stretch a little under its limit (Limits), as a rider on a road they do not know.
	RideSummary Summary(double Cap = 31.3) const;

private:
	// The rest of Make once the way (MadeSteps) is found on the area read: the road smoothed and laid,
	// its ground fetched, the network round StartAt (the road's start when null) laid, and all indexed.
	static std::shared_ptr<RidePlan> LayOut(const std::shared_ptr<RidePlan>& R, CellStore& Store, HeightStore& Heights,
		const std::function<void(const std::string&)>& Said, const P2* StartAt);
	// ([OSM stage 5] Lanes and OneWays, a knot each where given: the map's lanes, RoadX; OneWays 1 one way, 2 more a dual carriageway's half)
	void LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves, const std::vector<double>& Speeds, const std::vector<int>* Lanes = nullptr,
		const std::vector<char>* OneWays = nullptr, const std::vector<double>* Shoulders = nullptr);
	// [OSM stage 5C] A line moved square to itself by By metres (+ to its left as it runs), eased in from
	// nothing over the first EaseIn metres and out over the last EaseOut; or each point by Each[I] * Scale.
	static void ShiftLine(std::vector<P2>& Line, double By, double EaseIn, double EaseOut);
	static void ShiftLine(std::vector<P2>& Line, const std::vector<double>& Each, double Scale);
	// The region's spans of Kind (0 a bridge, 1 a tunnel) the line passes along: first knot, last knot, ...
	std::vector<int> SpansAlong(const std::vector<P2>& Line, int Kind) const;
	// Evens H out across each bridge and tunnel the line passes along (straight from one end to the
	// other) and says what each knot is on: 0 the ground, 1 a bridge, 2 a tunnel. Counts them.
	void LevelSpans(const std::vector<P2>& Line, std::vector<double>& H, std::vector<char>& Kind, int& BridgeCount, int& TunnelCount) const;
	void LaySides(const std::vector<int>& Path);
	// [ride anywhere] One road of the network laid (edge E): false for none. Marks: the ride's road's
	// points its mouths lie beside (for MouthAt), with their bits.
	bool LaySide(int E, SideRoad& Side, std::vector<std::pair<int, unsigned char>>& Marks, int& NBridges, int& NTunnels) const;
	const SideRoad* EnsureSide(int E, std::vector<std::pair<int, unsigned char>>* Marks = nullptr) const;
	void LayBlock(long long Key) const;
	void KnotsOfBlock(long long Key) const;
	struct Network;
	std::shared_ptr<Network> Net;
	std::vector<int> Planned;
	Rect RoadBox;
	// A mouth's corner on the side road's left (Sigma +1) or right (-1): the corner itself, the way
	// along the ride's road's edge from it, and how far along each edge the rounding starts.
	static void MouthCorner(const Mouth& M, const P2& MainAt, double Half, double Sigma, P2& Corner, P2& AlongMain, double& Reach);
	// [true T] (beenster, Nov 21: "messy junction".) Whether a point lies on the tarmac of a road laid off
	// the ride's road near MainAlong, other than Which (Margin m past its edge counted in); and a mouth's
	// corner drawn tighter (Reach smaller) where its curve would cross a neighbouring road's tarmac, false
	// where even a tight one would (that corner then left open: tarmac, no kerb or verge across the other road).
	bool OnOtherRoad(const P2& P, int Which, double MainAlong, double Margin) const;
	bool FitCorner(const Mouth& M, int Which, const P2& Corner, const P2& AlongMain, double& Reach) const;
	// By the ride's road's point: 1 a mouth or its corners on its left, 2 on its right (no edge line);
	// 4 the mouth itself on its left, 8 on its right (no shoulder).
	std::vector<unsigned char> MouthAt;
	// [OSM stage 5D] The same for the mouths of roads that can be town streets (not tracks, drives or
	// service roads): where double yellow lines start.
	std::vector<unsigned char> StreetMouthAt;
	// The scenery's lines and buildings by 256 m square (a line under each square one of its bits' middles is in).
	struct SceneIndex
	{
		std::unordered_map<long long, std::vector<int>> Lines, Buildings;
		std::unordered_map<long long, std::vector<int>> Looks;       // into Region::Scenery.Buildings (the map's tags)
		std::unordered_map<long long, std::vector<int>> Furniture;   // into RidePlan::Furniture
		std::unordered_map<long long, std::vector<P2>> Gaps;         // gateways the walls leave open
		std::unordered_map<long long, std::vector<double>> GapWidths;   // [OSM stage 2] each one's width, as Gaps
		std::unordered_map<long long, std::vector<int>> Wires, Talls, Yards;   // [OSM stage 2 batch B] by the square their middle is in
		std::unordered_map<long long, std::vector<int>> Fields;   // [OSM stage 2 batch C] by every 64 m square each field area's box covers
		std::unordered_map<long long, std::vector<std::pair<int, int>>> Wild;   // [OSM stage 2 batch D] (line, first point) by the square its middle is in
		std::unordered_map<long long, std::vector<int>> Summits;
		std::unordered_map<long long, std::vector<int>> Towers;   // [towns stage 2] by every 256 m square each tower's box covers
		std::unordered_map<long long, std::vector<int>> Streams;     // into Region::Streams
		std::unordered_map<long long, std::vector<int>> Paths;       // [OSM stage 4] into Region::Ground.Paths, by the squares their bits' middles are in
		// The railways' bits (line, first point) by the 256 m square their middle is in, and by every
		// RailCell square within 40 m of them (for NearRail).
		std::unordered_map<long long, std::vector<std::pair<int, int>>> RailBits, RailNearBy;
	};
	static constexpr double RailCell = 32.0;
	void IndexRails();
	void RailTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out, RideMesh& Stone) const;
	// The water on a square of ground whose own mesh (LandTile's, its points Cells + 1 a side) is Sheet.
	// [real water stage 2] Shore (if given): two figures per point of the land's sheet, its UV1 (RideMesh::Uv1):
	// ShoreZero less how high it stands over the water near it, and its shore's kind times 10 plus the tide's half range.
	void WaterTile(double X0, double Z0, double Size, int Cells, const RideMesh& Sheet, RideMesh& Out, std::vector<float>* Shore = nullptr) const;
	// [real water] The land's UV1 u is ShoreZero less its height over the water near it (0, no water near: 64 m over it).
	static constexpr double ShoreZero = 64.0;
	enum EShore { ShoreNone = 0, ShoreBank = 1, ShoreSand = 2, ShoreShingle = 3, ShoreRock = 4, ShoreMud = 5 };
	// [real water stage 9] The tide's half range (m) for the sea at (X, Z): Tide::HalfRange, eased to none round a
	// ferry's berths (its boat and slipway are laid for the sea's mean level).
	double TideHalfAt(double X, double Z) const;
	// [build 155 feedback] A near square's watercourses' banks and channels, in the land's own colours, added
	// to its land mesh Out: each side from under the water's edge out BankStrip squares, finely, over the
	// near land's points that lie under the water (GroundOf), to meet the land as drawn at its outer edge.
	void BanksTile(double X0, double Z0, double Size, RideMesh& Out) const;
	// [build 155 feedback] The ground as the near squares draw it: the land mesh's (LandDrawnAt), or by a
	// watercourse its banks' (BanksTile): what a path laid on the ground lies on.
	double LandSurfaceAt(double X, double Z, std::unordered_map<long long, double>* Cache = nullptr) const;
	// [OSM stage 4] The map's footpaths, bridleways, cycleways, pedestrian streets and steps beside the roads
	// (Region::Ground.Paths): a ribbon on the ground along each, its colour its surface's, never on a road.
	void PathsTile(double X0, double Z0, double Size, double Step, RideMesh& Out) const;
	void IndexFurniture();
	void FindLevelCrossing(int Line, int First, int Last);   // [level crossings] (one of LevelCrossings, from IndexFurniture)
	void FurnitureTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out) const;
	bool InGap(double X, double Z) const;
	// [OSM stage 2] A bit of a wall, hedge or fence from A to B less the gateways in it: its pieces
	// (Out, each from and to), 0, 1 or 2 of them.
	int CutGaps(const P2& A, const P2& B, P2 Out[2][2]) const;
	std::shared_ptr<SceneIndex> Scene;
	void IndexScenery();
	bool BarriersLined = false;   // [OSM stage 5C] the map's crash barriers added to the scenery's lines
};
// [ride anywhere] The sat nav's way back to the ride's road (RidePlan::DetourFrom).
struct RideDetour
{
	std::vector<P2> Line;                    // from the rider, by the roads, to where it rejoins
	std::vector<double> Along;               // how far along Line each point is
	std::vector<RidePlan::RideTurn> Turns;   // along Line; the last where it rejoins
	double Length = 0.0;
	double RejoinAlong = 0.0;                // how far along the ride's road it rejoins
	// [waypoint] A way by a waypoint (RidePlan::DetourVia): how far along Line it is reached (-1 none), and where.
	double ViaAlong = -1.0;
	P2 Via;
};
} // namespace TorqueWorld
