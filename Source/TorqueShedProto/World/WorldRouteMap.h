// The Route planner's map (Ride UK stage 4), in plain C++ so SimTest can try it: a web-mercator view
// of the world at any zoom, and the land under it drawn as a relief map (hills shaded from the
// north-west, tinted by height, the sea blue) from the same height tiles the rides are built from.
#pragma once

#include "WorldGeo.h"

#include <cstdint>
#include <vector>

namespace TorqueWorld
{
class HeightStore;

namespace Route
{
struct MapView
{
	double Lat = 54.6, Lon = -3.4;   // the middle
	double Zoom = 5.6;               // as web maps have it: the world is 256 x 2^Zoom pixels across
	int W = 1024, H = 1024;          // the picture's pixels
	static constexpr double MinZoom = 4.5, MaxZoom = 13.0;

	// Where a place is on the picture (pixels from its top left), and the place at a pixel.
	P2 PixelOf(double Lat, double Lon) const;
	Geo PlaceAt(double X, double Y) const;
	// Ground metres a pixel at the middle.
	double MetresPerPixel() const;
	// The view moved by pixels, and zoomed by Steps (2x a step) about a pixel, which stays put.
	MapView Panned(double DX, double DY) const;
	MapView Zoomed(double Steps, double AtX, double AtY) const;
	// The zoom of height tile (512 px) that matches the picture's pixels, and the tiles it needs.
	int TileZoom() const;
	std::vector<TileKey> Tiles() const;
};

// The relief under a view, W x H pixels, B G R A each (as a texture takes them). Where no tile is on
// disk yet the land is a dark grey. Returns the share of pixels it had ground for.
double DrawRelief(HeightStore& Heights, const MapView& View, std::vector<uint8_t>& Bgra);
} // namespace Route
} // namespace TorqueWorld
