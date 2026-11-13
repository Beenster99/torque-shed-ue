// Ride anywhere (the Godot game's network ride, v2): what the rest of the game reads of the world
// round the riders and of the way the sat nav is guiding them, while the roads and the land are
// streamed in round wherever the riders go.
//
// Both are published as whole, immutable snapshots behind a shared pointer, each with a Version that
// goes up whenever a new one is published, so a reader on any thread holds the one it has for as
// long as it likes and checks the Version to know when to take the new one:
//   RoamArea: the map's roads, buildings, water, places and stops loaded round the ride so far (a
//     Region, in the ride's own frame: metres, x east, z south, the same frame as the RidePlan's). It
//     grows as the riders roam (more cells stitched into the same frame); Box is what it covers.
//   RideGuide: the way the sat nav is taking the riders, a point every GuideStep metres from the
//     start (or where it last re-routed from) to the finish, with the pins still to come; a new one
//     each time it re-routes. OffRoute: the rider is off it; Rerouting: a new way is being found.
#pragma once

#include "WorldLoad.h"

#include <memory>
#include <vector>

namespace TorqueWorld
{
class RidePlan;
struct RideDetour;

struct RoamArea
{
	int Version = 0;
	std::shared_ptr<const LoadedArea> Area;   // Area->Roads: the Region (its FrameOf() is the ride's)
	Rect Box;                                  // the part of the frame it covers
};

struct RideGuide
{
	static constexpr double GuideStep = 20.0;
	int Version = 0;
	std::vector<P2> Line;        // start (or the last re-route) to the finish, a point every GuideStep m
	std::vector<P2> Waypoints;   // the pins still to come, the finish last
	P2 Finish;
	bool OffRoute = false;
	bool Rerouting = false;
};

// The area and guide of a ride as planned (Version 0 of each): the plan's own area, and its road.
std::shared_ptr<const RoamArea> RoamOfPlan(const std::shared_ptr<const RidePlan>& Plan, int Version = 0);
std::shared_ptr<const RideGuide> GuideOfPlan(const RidePlan& Plan, int Version = 0);
// The guide along a way back (RidePlan::DetourFrom): its line, then the ride's road on from where it rejoins.
std::shared_ptr<const RideGuide> GuideOfDetour(const RidePlan& Plan, const RideDetour& Back, int Version);
} // namespace TorqueWorld
