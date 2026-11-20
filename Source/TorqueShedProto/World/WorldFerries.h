// [ferries] The UK's vehicle ferries in Ride UK (beenster, Nov 19 2026: "The ferries should be short in realtime and we
// have to negotiate riding our bikes on and off the ferry, we should be able to walk about on the ferry. Each ferry
// should feel small (for a couple of cars and a couple of bikes)"). Plain C++ (SimTest/world_test.cpp), no Unreal:
//
//   ReadList      Data/RideUK/ferries.json: the crossings the game sails (the map has no way to tell a car ferry
//                 from a foot ferry, so the list says which), each by its route's id in the world's feature cells
//                 (kind "ferry", Overture's water segments) and its two ends.
//   InArea        the listed crossings whose routes are in a ride's area: the route's pieces (cut at the cells'
//                 edges) joined end to end, found by id, else by its ends.
//   Lay           a crossing's two slipways on the real shore: where each end's slipway runs down into the water
//                 (along the route), the approach from the map's nearest road to its head when the road stops
//                 short of the water (Corran's Nether Lochaber side), where the boat's ramp rests on it, and where
//                 the boat lies tied up there; the water's level (the sea's 0, or a loch's).
//   Sail          where the boat is part way over: a smooth curve from one berth to the other, its bow always
//                 towards end 1 (a double-ended boat never turns round: it sails one way bow first, back stern first).
//
// The boat is World/TorqueFerry.cpp's (its model Tools/props/make_ferry.py's ferry_small): the numbers it needs to
// tie up (its deck's height over the water, its half length, its ramp) are here, the same as the model's.
#pragma once

#include "WorldGeo.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace TorqueWorld
{
class RidePlan;
namespace Ferries
{
// The boat (ferry_small) and its ramps (ferry_ramp), as Tools/props/make_ferry.py draws them (m).
static constexpr double DeckY = 1.0;          // the car deck's top over the waterline
static constexpr double HalfDeck = 12.6;      // its ends, either side of the middle (the ramps hinge there)
static constexpr double RampLen = 4.6;        // a ramp from its hinge to its toe
static constexpr double RampHalfW = 3.3;
// A slipway: its width, how far up the land its head is from the water's edge, how far past the boat's ramp's
// toe it runs on down under the water, and how steep it may be (rise over run).
static constexpr double SlipHalfW = 3.4, SlipHeadIn = 7.0, SlipPastToe = 4.0, SlipSteepest = 1.0 / 6.0, SlipGentlest = 1.0 / 12.0;
static constexpr double ToeOverWater = 0.15;  // the ramp's toe rests on the slipway this far over the water
static constexpr double ApproachHalfW = 2.9;  // the approach road laid from the map's road to a slipway's head
static constexpr double RoadReach = 220.0;    // a slipway's head finds the map's road within this (m)
static constexpr double ShoreBack = 160.0, ShoreOut = 90.0;   // the water's edge looked for this far back and out along the route
static constexpr double LongFrom = 3000.0;    // a route this long or longer is a long one (stage 7: the open sea held short)

enum class EKind { Open, Chain, Turntable };
const char* KindName(EKind K);

struct Listed
{
	std::string Id, Name, MapName, Country;
	double LatA = 0.0, LonA = 0.0, LatB = 0.0, LonB = 0.0;
	std::string Places[2];
	EKind Kind = EKind::Open;
	double Metres = 0.0;
};
bool ReadList(const std::string& Text, std::vector<Listed>& Out);

struct Crossing
{
	int Listed = -1;             // its row in the list
	std::string Id, Name;
	EKind Kind = EKind::Open;
	std::vector<P2> Line;        // end 0 to end 1, in the ride's frame
	double Length = 0.0;
	bool Long = false;
	bool FromMap = false;        // its line from the map's route (else straight between the listed ends)
};
// The listed crossings with both ends inside Box (the ride's frame); Lines are the area's feature lines (kind "ferry"
// among them), F the ride's frame.
struct FerryLine
{
	std::string Id;
	std::vector<P2> Line;
};
std::vector<Crossing> InArea(const std::vector<Listed>& List, const std::vector<FerryLine>& Lines, const Frame& F, const Rect& Box);
// The same from a ride's plan (its area's feature lines and its box).
std::vector<Crossing> InPlan(const RidePlan& Plan, const std::vector<Listed>& List);

// What Lay needs of the world: the ground's height (NaN where it isn't known yet), whether a point is the sea, and
// the map's road best for an approach to a point within Reach whose height is WantY (its middle's nearest point unless
// that is much higher or lower, the road's surface there and its half width).
struct WorldAsk
{
	std::function<double(double X, double Z)> Height;
	std::function<bool(double X, double Z)> Sea;
	std::function<bool(const P2& At, double Reach, double WantY, P2& Foot, double& Y, double& Half)> Road;
};
WorldAsk AskOf(const RidePlan& Plan);

struct End
{
	P2 Axis;                     // the slipway's way down into the water (unit), the route's way out from this end
	P2 Shore;                    // where the slipway meets the water's edge
	P2 Head, Tip, Foot;          // its top on the land, where the boat's ramp's toe rests, its bottom under the water
	double HeadY = 0.0, TipY = 0.0, FootY = 0.0;
	double Grade = 0.0;          // its fall (rise over run)
	// The approach from the map's road to the head (its middle line, a point every 2 m or so: x, y, z), empty when the
	// road comes to the head itself; and the road where it leaves.
	std::vector<std::array<double, 3>> Approach;
	P2 Road;
	double RoadY = 0.0;
	P2 DeckEnd;                  // the boat's deck's end over the water, the ramp's hinge, while tied up here
	P2 Berth;                    // the boat's middle while tied up here
	double RampDown = 0.0;       // how far down from level its ramp lies on the slipway (radians)
};
struct Layout
{
	bool Ok = false;
	std::string Why;
	double Water = 0.0;          // the water's level (m: the sea's 0)
	bool Sea = false;
	End Ends[2];
	double PathLength = 0.0;     // berth to berth along Sail's curve
	double Seconds = 0.0;        // a sailing, tied up to tied up
};
bool Lay(const Crossing& C, const WorldAsk& Ask, Layout& Out);

// How long a sailing takes (s): 1 to 3 minutes whatever the route's length (Short: about a minute for Corran).
double SailSeconds(double PathM, bool Long);
// How far over the boat is (0..1 of the way) at T (0..1 of the sailing's time): it speeds up off the berth, runs
// steady, and slows in to the other.
double Progress(double T);
// Where the boat is at S (0 at end 0's berth, 1 at end 1's): its middle and its bow's way (unit; the bow always
// faces towards end 1). Long routes run out from end 0 and in to end 1 with the open sea between passed in a hold
// (HoldFrom..HoldTo of S): the boat lies at the end of the outward leg until HoldTo, then at the start of the inward.
static constexpr double HoldFrom = 0.46, HoldTo = 0.54, LongLeg = 450.0;
void Sail(const Layout& L, bool Long, double S, P2& Pos, P2& Bow);
} // namespace Ferries
} // namespace TorqueWorld
