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
	V3 KneeL, KneeR, SeatFront, Hip, Tail, Peg, GripR, StayHead, ScreenBase;
	double TailTop = 0.0, RailHalfW = 0.075, BellyY = 0.0, RadiatorX = 0.0;
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
	const Json* Part(const std::string& Id) const;
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
	std::vector<std::string> StyleList, SlotList;

	const Json* Lines(const std::string& Style) const;
	std::vector<std::string> StylesOf(const std::string& Id) const;
	std::string Brief(const PartsMap& Parts, Json& LinesOut) const;
	FitReport FitOf(const Layout& L, const Json* Engine, double FrontClearWant, const std::vector<std::pair<std::string, bool>>& Notes) const;
};

// Godot frame <-> ours.
inline V3 FromGodot(const V3& G) { return V3(-G.z, G.y, G.x); }
inline V3 ToGodot(const V3& O) { return V3(O.z, O.y, -O.x); }
} // namespace TorqueFit
