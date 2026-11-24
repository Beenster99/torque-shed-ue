// [eaves, Nov 24] A house's roof edges as they are built (beenster, Nov 24: "Can we add drainage and
// guttering for houses: fascia, soffits, cornice, bargeboards. they create an overhang which creates a
// shadow and brings the house to life"). Drawn by BuildingsTile with each building (WorldRideEaves.cpp):
//  - Boxed eaves: a fascia board at the rafters' ends, a soffit under it back to the wall, the box's ends
//    closed; up a gable's verge a bargeboard with its own soffit. uPVC white or brown, or painted timber.
//  - Stone eaves (the Dales, the Lakes, the Cotswolds' old stone houses): no box, a dressed stone eaves
//    course and a black cast-iron gutter on brackets under a flush verge.
//  - A moulded cornice (Georgian and Victorian town houses, a civic building or a block's flat roof).
//  - Gutters along every eaves and downpipes to the ground (swan neck, wall pipe), one an end of a house,
//    one a pair of bays along a terrace; none down a shop's or a pub's front.
// All plain quads in the building's own merged mesh (no new draws): about 60-90 triangles a house.
// -TorqueEaves=0 leaves it all out, 1 the trim without gutters and downpipes (the frame rate A/B).
#pragma once

#include "WorldRideSceneBits.h"


namespace TorqueWorld
{
namespace RideEaves
{
enum class EStyle : unsigned char { None, Boxed, StoneCourse, Cornice, GutterOnly, FeltTrim };

struct House
{
	// (its frame, as BuildingsTile's Local: along its length, across it, up from Base)
	double X = 0.0, Z = 0.0, Cs = 1.0, Sn = 0.0, Base = 0.0;
	bool LongX = true;
	double HalfLong = 0.0, HalfShort = 0.0;
	double Eaves = 0.0, Rise = 0.0, Over = 0.0, OverPlus = 0.0, OverMinus = 0.0, Drop = 0.0;
	int Shape = 0;                     // (0 gabled, 1 hipped, 2 flat, 3 skillion: BuildingsTile's ERoof)
	bool PartyPlus = false, PartyMinus = false;
	EStyle Style = EStyle::None;
	double HeadTop = 0.0;              // (the highest window head or shop cornice on its long walls: the soffit stays over it)
	RideSceneBits::Rgb Fascia, Soffit, Gutter, Pipe, Trim, Verge;
	bool Gutters = true, Pipes = true;
	bool Course = true;                // (a stone eaves course under a flush eaves; a whitewashed cottage's has none)
	double FrontSide = 1.0;            // (the long side its door is on)
	bool NoFrontPipe = false;          // (a shop front or a pub's board on its front: no pipe down it)
	bool DoorPlus = false;             // (a row house's door at its + end: its pipe at the other)
	int Bays = 1;
	double CornerUp[2][2] = {{0.0, 0.0}, {0.0, 0.0}};   // (the ground over Base by its corners: [along -,+][across -,+]; the pipes' feet)
};

// The heights (up from Base) its eaves trim reaches down to on its long walls: what a board on the wall
// (a pub's name) must stay under. Eaves itself when it has none.
double TrimUnder(const House& H);

// Draws its trim, gutters and downpipes into Out. Gutters and Pipes as asked (and by EavesLevel: 0 none
// of it, 1 no gutters or pipes, 2 all).
void Draw(const House& H, int EavesLevel, RideMesh& Out);

// [eaves] What BuildingsTile should draw on a building: its style and colours, by its region's look, its
// period, its walling and what it is. Seeds: Row (a row's or a street's, so they match), Own (its own).
struct Pick
{
	EStyle Style = EStyle::None;
	RideSceneBits::Rgb Fascia, Soffit, Gutter, Pipe;
	double MinOver = 0.0;   // (a boxed eaves' overhang at least this: a modern house's 30 cm)
	bool Gutters = true;
	bool Course = true;
};
Pick Choose(bool Pitched, bool Flat, bool Thatch, bool Sheet, bool Ruin, bool Glass, bool Big, bool Works, bool IsBarn, bool IsShed, bool Garage,
	bool IsHouse, bool IsShop, bool IsBlock, bool IsCivic, bool InTown, bool StoneWall, bool Rendered, double LookOver, int PeriodKind,
	const RideSceneBits::Rgb& Joinery, double RollRow, double RollOwn);
} // namespace RideEaves
} // namespace TorqueWorld
