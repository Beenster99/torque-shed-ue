// The layout solver: where any bike built from the catalogue's parts has its steering head,
// swingarm pivot, engine, tank, seat, tail, pegs and bars, and whether its engine and frame go
// together. A port of the Godot game's BikeRig.solve (scripts/bike_rig.gd, "the Rig built look")
// and the geometry BikeBuild works out from the parts (wheelbase, rake, fork length, seat height,
// wheel radii), so every frame and every style lays out as it does there, and a co-op host and
// guest given the same parts get the same bike (it reads nothing but the parts and the handles).
//
// Data: Data/Fitment/layout.json (Tools/import_layout_data.py: the styles' eight lines, the tank
// shapes, the slots, the 14 reference bikes) and Data/Parts/parts.json.
//
// Internally it keeps the Godot game's frame (metres, y up, -z forward, x right; origin mid
// wheelbase at the front axle's height) so it can be checked line by line against bike_rig.gd. Its
// results (Layout) are in ours: x forward, y up, z right (Fitment.h), ours = (-gz, gy, gx).
#pragma once

#include "FitJson.h"
#include "PartsDb.h"
#include "SimMath.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
using TorqueSim::V3;

// Slot -> part id ("none" for an empty slot), as the Godot game's builds.
using PartsMap = std::map<std::string, std::string>;

// The geometry BikeBuild works out from the parts (the catalogue's numbers, the same in every look,
// which the riding physics use too).
struct BuildStats
{
	double Wheelbase = 1.4, RakeDeg = 25.0, SeatHeight = 0.85, ForkLength = 0.75;
	double FrontRadius = 0.3, RearRadius = 0.31, FrontTravel = 0.12, RearTravel = 0.1;
	double FrontRimRadius = 0.22, RearRimRadius = 0.22, FrontTyreWidth = 0.12, RearTyreWidth = 0.16;
	double PivotY = 0.16, Arm = 0.6;   // the frame's geo
};

// Geometry mode's range handles (BikeRig.ADJUST): what a builder can move on a solved bike.
struct LayoutAdjust
{
	double TankUpMm = 0.0;    // 0..30: the tank's mounts raised
	double PegsMm = 0.0;      // -40..40: the pegs moved back along their plate
	double HoopUpMm = 0.0;    // -20..40: the seat hoop raised at the tail
	double HoopDeg = 0.0;     // -6..8: the seat hoop tipped up at the tail
	LayoutAdjust Clean() const;
};

// One handle's range, for geometry mode's sliders.
struct AdjustRange
{
	std::string Key, Name, Unit;
	double Min = 0.0, Max = 0.0, Step = 1.0;
};

// [bikes v2] A Bike Models V2 bike is a frame part with "v2" (Data/BikesV2/parts_v2.json): its model's
// name and its key points as BikesV2 draws them (mm: x forward from the rear axle, y to the rider's
// left, z up from the ground). This takes those, and the model's glb (metres: x forward, y up, z to
// the right, the same origin), into ours: the origin midway between the axles, both axles at y = 0
// (a V2 bike's wheels differ in size, so the drawing is tipped by that much, as the sim stands it).
struct V2Frame
{
	bool Valid = false;
	double MidX = 0.0, MidZ = 0.0, Cos = 1.0, Sin = 0.0;   // mm, and the tip
	const Json* Points = nullptr;                         // "points_mm"
	static V2Frame Of(const Json* FramePart);
	V3 Point(double X, double YLeft, double ZUp) const;
	V3 Dir(double X, double YLeft, double ZUp) const;
	// A named point of points_mm ("front_axle"); False if it has none.
	bool Named(const std::string& Key, V3& Out) const;
	V3 FromGlb(const V3& G) const { return Point(G.x * 1000.0, -G.z * 1000.0, G.y * 1000.0); }
	V3 DirFromGlb(const V3& G) const { return Dir(G.x, -G.z, G.y); }
};

// [common parts] A slot for the common bolt-on parts that the Godot game never had (Data/BikesV2/common_parts.json
// "new_slots"): BikeLayout::Load adds each, optional, its kind its own name, mounted on On.
struct CommonSlot
{
	const char* Slot;
	const char* On;
};
const std::vector<CommonSlot>& CommonSlots();

// How the engine and frame go together (BikeRig._fit).
struct FitReport
{
	std::string Tier;                // "bolts on", "fabricate" or "won't fit"
	std::vector<std::string> Work;   // what the shed has to make (fabricate)
	std::string Reason;
};

// One job of fabrication and what it takes (BikeRig.fab_job).
struct FabJob
{
	std::string What;
	double Hours = 0.0;
	int Cost = 0;
};

// A part in a slot, as it goes on a bike (BikeRig.part_fit).
struct PartTier
{
	std::string Tier, Reason;
	std::vector<FabJob> Work;
};

// The engine's hard points in its own space (BikeRig.hard_points), crank at the origin, ours.
struct EngineHard
{
	V3 Output, HeadFront, HeadRear, CaseFront, CaseRear, Sump, MountFU, MountFL, MountRU, MountRL, HeadSteady, AirboxTop;
	std::vector<V3> HeadTops, Ups;
	std::vector<std::pair<V3, V3>> Intakes, Exhausts;   // at, direction
	double CaseHalfW = 0.0, HeadHalfW = 0.0, TopY = 0.0, HeadK = 1.0, Scale = 1.0;
};

// A frame's own Rig built proportions (BikeRig.FRAME_RIG, with the frame's "rig" over them).
struct FrameRig
{
	double Gauge = 1.0, Twin = 0.0, Top = 0.0, TankLow = 0.0, Drop = 0.0, Deep = 1.0, Wide = 1.0, Loop = 0.06, Slope = 0.0;
	bool Split = false, Gusset = false, Steady = false;
};

// A bike laid out (BikeRig.solve's keys, in our frame).
struct Layout
{
	std::string Style, Family, Frame, MixedTail;
	BuildStats Stats;
	V3 FrontAxle, RearAxle, SteerAxis, Head, HeadBottom, Pivot;
	EngineHard Hard;
	V3 Crank, Output;
	std::vector<V3> Spine, TankFloor;
	V3 TankNose, TankRear;
	double TankTopMax = 0.0, TunnelHalfW = 0.0, SpineR = 0.0;
	FrameRig Frig;   // the frame's proportions ("frame_rig")
	V3 KneeL, KneeR, SeatFront, Hip, Tail, Peg, GripR, StayHead, ScreenBase;
	double TailTop = 0.0, RailHalfW = 0.075, BellyY = 0.0, RadiatorX = 0.0;
	// [rider pose] The bars moved back toward the rider and up ("bar_move_mm" [back, up], beenster's
	// marked upright backs), metres: they sit on risers with that pullback.
	double BarBackM = 0.0, BarUpM = 0.0;
	bool SeatOverlap = false, HasRadiator = false;
	V3 SparFrom, SparTo;
	V3 EngineMin, EngineMax;   // the engine's box
	double FrontClear = 0.0;
	FitReport Fit;
	double Ground() const { return -Stats.RearRadius; }
};

class BikeLayout
{
public:
	bool Load(const std::string& LayoutJsonText, const PartsDb& Db, std::string& Error);
	bool Loaded() const { return Db != nullptr; }

	const std::vector<std::string>& Styles() const { return StyleList; }
	const std::vector<std::string>& Slots() const { return SlotList; }
	std::string StyleName(const std::string& Style) const;
	std::string KindOfSlot(const std::string& Slot) const;
	std::string KindOfPart(const std::string& Id) const;
	// The styles a part belongs to (PartsDB.styles_of).
	std::vector<std::string> StylesOf(const std::string& Id) const;
	const Json* Part(const std::string& Id) const;
	// [bikes v2] The frame's "v2" if the bike of Parts is a Bike Models V2 one, else nullptr.
	const Json* V2Of(const PartsMap& Parts) const;
	// [bikes v2] The old bikes are retired (beenster, 17 Nov): wherever the game hands out a bike it is a
	// V2 one. The V2 frames made for a style (the family's lead first), the styles that have any
	// (every style when the catalogue has no V2 bikes at all, as in the tests against the Godot game),
	// and a style's bike to start from: its reference bike on its lead V2 frame.
	std::vector<std::string> V2Frames(const std::string& Style) const;
	const std::vector<std::string>& ShopStyles() const { return ShopStyleList.empty() ? StyleList : ShopStyleList; }
	bool HasV2() const { return !ShopStyleList.empty(); }
	PartsMap ShopBuild(const std::string& Style) const;
	// [bare frame] Every V2 bike's own parts are shop parts ("v2part" in Data/BikesV2/parts_v2.json): a frame's
	// own, slot -> id (its "v2" "build"), and Parts with a new frame's own in place of other V2 parts (and in the
	// slots that were empty); what the player chose that isn't a V2 model's part (common, service parts) stays.
	PartsMap V2Build(const std::string& FrameId) const;
	PartsMap V2Dress(const PartsMap& Parts) const;
	// Why a V2 model's part won't go in Slot on a bike of Parts (empty: it fits, or isn't a V2 model's part). The
	// family's mounting standard (docs/mounting_standard.md): its family's frames; an engine and what bolts to it
	// only a frame built round its layout; a shock only the same rear end; handlebars only their own model. On a
	// loose engine (Parts has its engine and no frame) its parts go by the engine's layout.
	std::string V2FitReason(const PartsMap& Parts, const std::string& Slot, const std::string& Id) const;
	// [engine parts] What the engine part needs fitted with it: an open intake drawn on one fuel system ("with_fuel"),
	// a carb that needs the airbox off ("open_air"). Empty when it can go on now. (The shop lists them all the same.)
	std::string V2NeedsReason(const PartsMap& Parts, const std::string& Slot, const std::string& Id) const;
	// The parts catalogue it was loaded on (its kinds and ids in parts.json's order).
	const PartsDb* Catalogue() const { return Db; }
	// layout.json as loaded (tank_shapes, seat_shapes, tail_cowls...).
	const Json& Tables() const { return Data; }
	bool MayBeEmpty(const std::string& Slot) const;
	// The style's reference bike (Contracts.style_build), every slot filled or "none".
	PartsMap ReferenceBuild(const std::string& Style) const;
	// What style a set of parts adds up to (PartsDB.detect_style): the best and how much agrees.
	std::string DetectStyle(const PartsMap& Parts, double* Coherence = nullptr) const;
	// The style whose lines a bike is laid out to (BikeRig.style_for).
	std::string StyleFor(const PartsMap& Parts) const;
	// A number from a style's eight lines (data/bike_lines.json): Line "4_seat", Key "rider_u".
	double LineNumber(const std::string& Style, const std::string& Line, const std::string& Key, double Default) const;

	BuildStats Stats(const PartsMap& Parts) const;
	EngineHard HardPoints(const std::string& EngineId) const;
	Layout Solve(const PartsMap& Parts, const LayoutAdjust& Adjust = LayoutAdjust()) const;

	// How part Id goes in Slot on a bike of Parts ("none" to take it off), counting fabrication
	// already done (Done, the jobs' "what").
	PartTier PartFit(const PartsMap& Parts, const std::string& Slot, const std::string& Id,
		const std::vector<std::string>& Done = std::vector<std::string>()) const;
	static FabJob FabJobFor(const std::string& What);
	static std::vector<AdjustRange> AdjustRanges();

	// Whether the bodywork is fitted to the rig (Godot's RigBodywork.on): then a tank the heads
	// push up is shaped to its line and needs no raised mounts made.
	bool RigBodywork = true;

private:
	const PartsDb* Db = nullptr;
	Json Data;
	std::map<std::string, std::string> PartKind, SlotKind;
	std::vector<std::string> StyleList, SlotList, ShopStyleList;

	const Json* Lines(const std::string& Style) const;
	std::string Brief(const PartsMap& Parts, Json& LinesOut) const;
	FitReport FitOf(const Layout& L, const Json* Engine, double FrontClearWant, const std::vector<std::pair<std::string, bool>>& Notes) const;
};

// Godot frame <-> ours.
inline V3 FromGodot(const V3& G) { return V3(-G.z, G.y, G.x); }
inline V3 ToGodot(const V3& O) { return V3(O.z, O.y, -O.x); }
} // namespace TorqueFit
