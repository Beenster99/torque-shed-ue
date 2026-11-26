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
	int Banked = 0;          // [5E] the map's embankment (1) or cutting (2) under it, 0 none
	// [5D] The map's parking along its kerbs as the edge runs (A to B), left and right: 0 not mapped,
	// 1 no parking (double yellow lines), 2 no stopping (double red), 3 marked bays.
	int ParkLeft = 0, ParkRight = 0;
};
// [5C] A motorway's hard shoulder: the carriageway this much wider on its nearside, its edge line there.
constexpr double HardShoulder = 3.3;
// RuleWidth is the class's width (RoadLine::WidthOf). A recorded width is kept within a sensible range
// for the class; a road mapped as one lane both ways is single track (SingleTrack m wide, no centre
// line); mapped lanes otherwise give the width at a lane each (LaneWidth).
Across Of(const Region& G, int Edge, double RuleWidth);
constexpr double SingleTrack = 3.3;
double LaneWidth(const std::string& Class);
// [road rulebook D1, D5, C6] Britain's broken lines by the speed limit (Traffic Signs Manual chapter 5, figures 2-1,
// 2-2 and 2-4): the centre line (diagrams 1008, 1008.1) 2 m marks with 4 m gaps where the limit is 40 mph or less,
// 3 m with 6 m above; lane lines (1005, 1005.1) 1 m with 5 m, 2 m with 7 m; the warning line before a junction or
// hazard (1004, 1004.1) 4 m with 2 m, 6 m with 3 m. A pattern's marks start at whole cycles of the road's own length
// (OnDash), so a line carried across a junction keeps step with the road beyond.
struct DashPattern
{
	double Mark = 2.0, Cycle = 6.0;
};
inline DashPattern CentreDash(int Mph) { return Mph > 40 ? DashPattern{3.0, 9.0} : DashPattern{2.0, 6.0}; }
inline DashPattern LaneDash(int Mph) { return Mph > 40 ? DashPattern{2.0, 9.0} : DashPattern{1.0, 6.0}; }
inline DashPattern WarningDash(int Mph) { return Mph > 40 ? DashPattern{6.0, 9.0} : DashPattern{4.0, 6.0}; }
inline bool OnDash(double Along, const DashPattern& P)
{
	const double F = Along - P.Cycle * static_cast<double>(static_cast<long long>(Along / P.Cycle) - (Along < 0.0 ? 1 : 0));
	return F < P.Mark;
}
// How far the warning line runs before a junction (TSM chapter 5 table 5-1: at least 5 marks at 30 mph, 7 at 40,
// 7 of the longer marks above).
inline double WarningReach(int Mph) { return Mph > 40 ? 7.0 * 9.0 : Mph > 30 ? 7.0 * 6.0 : 5.0 * 6.0; }
// The edge line (diagram 1012.1, TSM chapter 5 table 2-5): 100 mm wide, its outer side 225 mm in from the edge of
// the carriageway as seen (the grass comes in over the tarmac's own edge by up to EdgeWorn).
constexpr double EdgeLineHalfWidth = 0.05, EdgeLineIn = 0.225, EdgeWorn = 0.12;
// The centre line's half width: 100 mm, 150 mm on a two-lane road 10 m wide or more (TSM chapter 5, 2.2.2).
inline double CentreHalfWidth(double Half) { return Half >= 5.0 ? 0.075 : 0.05; }
// Whether a road has a centre line: mapped two-way roads of two lanes or more, single track never; and none under
// 5.5 m wide ([road rulebook D2], TSM chapter 5, 2.2.6).
bool CentreLine(int Lanes, bool OneWay, double Half);
// The lane lines across a road (offsets from its middle, m, + to the left), not counting the centre
// line: between a one-way road's lanes, and within each half of a two-way road of four or more.
std::vector<double> LaneLines(int Lanes, bool OneWay, double Half);
} // namespace RoadX
} // namespace TorqueWorld
