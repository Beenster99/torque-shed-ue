// The look of a part of Britain (Ride UK stage 5): what its field walls, houses and roofs are made of,
// so a ride in the Cotswolds is honey stone, one in Cornwall has hedge banks and white cottages, one
// in the Lakes grey-green slate and whitewashed farms, the Midlands red brick. Chosen by where a place
// is (a coarse map of the country's building traditions), the same on every rider's machine.
#pragma once

#include <string>

namespace TorqueWorld
{
struct Tint
{
	float R = 1.0f, G = 1.0f, B = 1.0f;
};

struct LocalLook
{
	const char* Name = "dales";
	// Field boundaries where the map has a wall line: a dry stone wall, a hedge bank (earth faced with
	// stone or turf, a hedge on top: the South West's), or a slate or stone fence-like dyke.
	enum EBoundary { Wall, HedgeBank };
	EBoundary Boundary = Wall;
	Tint WallStone;            // the scanned stone of the walls, tinted (1 the Dales' limestone)
	Tint HouseStone;           // and of stone houses
	// The share of houses (not barns) of each walling, the rest stone; and their colours (linear).
	float RenderShare = 0.1f, BrickShare = 0.0f;
	Tint RenderColour{0.55f, 0.53f, 0.47f};
	Tint BrickColour{0.26f, 0.095f, 0.06f};
	// Roofs: the share of each covering, the rest slate.
	float StoneSlateShare = 0.45f, TileShare = 0.0f, ThatchShare = 0.0f;
	Tint TileColour{0.30f, 0.11f, 0.06f};
};

// The look of the place at a latitude and longitude.
const LocalLook& LookAt(double Lat, double Lon);
// [stage 8] The world's default look, away from Britain: rendered walls, tiled roofs, grey stone.
const LocalLook& WorldLook();
} // namespace TorqueWorld
