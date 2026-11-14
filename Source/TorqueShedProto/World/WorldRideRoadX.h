// [OSM stage 5] The road itself: the rules that turn what OpenStreetMap says about a road edge
// (Region::ExtraOf, from a cell's roadx file) into how wide the ride's road is and how it is painted.
// Mapped first, rule second, never both: where the map says nothing the class's rule stands.
#pragma once

#include "WorldRegion.h"

namespace TorqueWorld
{
namespace RoadX
{
// What one edge's road is like across: half its carriageway's width (m), its lanes (0: the rule's) and
// whether it runs one way.
struct Across
{
	double Half = 2.3;
	int Lanes = 0;
	bool OneWay = false;
	bool Dual = false;   // [5C] a dual carriageway's half (the map names its other half)
	double Shoulder = 0.0;   // [5C] a motorway's hard shoulder on its nearside (m wide; in Half), 0 none
};
// [5C] A motorway's hard shoulder: the carriageway this much wider on its nearside, its edge line there.
constexpr double HardShoulder = 3.3;
// RuleWidth is the class's width (RoadLine::WidthOf). A recorded width is kept within a sensible range
// for the class; a road mapped as one lane both ways is single track (SingleTrack m wide, no centre
// line); mapped lanes otherwise give the width at a lane each (LaneWidth).
Across Of(const Region& G, int Edge, double RuleWidth);
constexpr double SingleTrack = 3.3;
double LaneWidth(const std::string& Class);
// Britain's centre line (data/road_marks.json "uk": 4 m dashes, 5 m gaps), and lane lines the same.
constexpr double DashLength = 4.0, DashCycle = 9.0;
// Whether a road has a centre line: mapped two-way roads of two lanes or more, single track never; the
// rule (a road 5 m wide or more both ways) where the map says nothing.
bool CentreLine(int Lanes, bool OneWay, double Half);
// The lane lines across a road (offsets from its middle, m, + to the left), not counting the centre
// line: between a one-way road's lanes, and within each half of a two-way road of four or more.
std::vector<double> LaneLines(int Lanes, bool OneWay, double Half);
} // namespace RoadX
} // namespace TorqueWorld
