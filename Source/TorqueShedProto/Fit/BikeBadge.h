// The tank's badge drawn (the Godot game's BikeBadge, bike_badge.gd): the logos' pictures, where
// the logo and the brand name go on the tank, and the thin skin cut from the tank's own surface
// that each is stamped on, both sides. Plain C++ (SimTest's badge_test checks it against the Godot
// game); the badge itself (what's picked) is Shop::Badge (Fit/BikeShop.h), and the game draws it
// with Shed/TorqueBikeBadge (the lettering by the engine's fonts).
//
// Its frame is Godot's bike model's: x across the bike (the right the larger), y up, z along it
// (the nose the smaller), metres.
#pragma once

#include "BikeShop.h"
#include "SimMath.h"

#include <string>
#include <vector>

namespace TorqueFit
{
namespace BadgeArt
{
using TorqueSim::V3;

// BikeBadge.SIZE: the logo's picture, in pixels (drawn at twice that and shrunk, for smooth edges);
// TEXT_PX the brand name's letters; BORDER the clear border round them (a share of their height);
// LIFT how far a decal sits off the tank's paint (m).
constexpr int LogoPx = 256;
constexpr int TextPx = 96;
constexpr double Border = 0.15;
constexpr double Lift = 0.0015;

// A picture's coverage, a byte a pixel (rows top down): 255 the picture, 0 clear.
struct Mask
{
	int W = 0, H = 0;
	std::vector<unsigned char> A;
	unsigned char At(int X, int Y) const { return X >= 0 && Y >= 0 && X < W && Y < H ? A[(size_t)Y * (size_t)W + (size_t)X] : 0; }
};
struct P2
{
	double X = 0.0, Y = 0.0;
};
using Ring = std::vector<P2>;

// BikeBadge._fill: the inside of Polys (pixels, y down) set to Value, a row at a time, the spans
// between the crossings of the row by all their edges (so an inner loop stays out).
void Fill(Mask& M, const std::vector<Ring>& Polys, unsigned char Value);
// Half the size, each pixel the mean of the four it covers.
Mask Shrink(const Mask& M);

// BikeBadge._shapes: logo Id as polygons in a -1..1 square (y down), drawn in order, each adding
// to (On) or cutting out of what's under it. Empty for a logo it doesn't know.
struct Shape
{
	bool On = true;
	Ring Pts;
};
std::vector<Shape> LogoShapes(const std::string& Id);
// BikeBadge.texture: logo Id's picture, LogoPx square (empty for one it doesn't know).
Mask LogoMask(const std::string& Id);

// BikeBadge.FONTS: lettering Id's file (in Data/Fonts/Badge; the plain one's too) and whether it's
// set in capitals whatever's typed. The plain one for an Id it doesn't know.
struct Lettering
{
	std::string Id, File;
	bool Caps = true;
};
const Lettering& FontOf(const std::string& Id);
// BikeBadge.brand_text: the brand as typed, or in capitals for lettering that's set in them.
std::string BrandText(const Shop::Badge& B);

// Where each picture goes on the tank (BikeBadge.stamp): Box the tank's bounds; BrandAspect the
// brand's picture's width over its height (with its border). Each picture's place is a rectangle
// in (z, y): X, Y its corner with the smaller z and y.
struct Box3
{
	V3 Min, Max;
};
struct Pic
{
	bool bLogo = true;
	double X = 0.0, Y = 0.0, W = 0.0, H = 0.0;
};
std::vector<Pic> Place(const Shop::Badge& B, const Box3& Box, double BrandAspect);
// The tank's bounds from its triangles (three points each).
Box3 BoundsOf(const std::vector<V3>& Faces);

// BikeBadge._decal_mesh: the skin for picture R on side Side (+1 the right): every triangle of
// Faces (three points each) on that side of MidX facing out that way that reaches into R and is
// the outermost there, lifted Lift off along its smoothed normal, the picture laid flat across R
// as seen from that side (U left to right, V top down). Each triangle (a, b, c) has
// (b - a) x (c - a) pointing out of the tank. False if no surface reaches it.
struct Skin
{
	std::vector<V3> Pos, Nrm;
	std::vector<double> U, V;
	std::vector<unsigned> Idx;
};
bool DecalSkin(const std::vector<V3>& Faces, double Side, double MidX, const Pic& R, Skin& Out);
} // namespace BadgeArt
} // namespace TorqueFit
