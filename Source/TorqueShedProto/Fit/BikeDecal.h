// Decals a player places on a bike's tank, fairing, panels and guards, and on a helmet (the decals
// plan, beenster 17 Nov 2026): what each one is, how a part's list of them is kept as text (saved
// with the part, sent to friends in co-op), and the thin skin each is drawn on, cut from the part's
// own triangles under it and lifted a hair off, so it wraps round the curves and stays put when the
// part is repainted. Plain C++: SimTest's decal_test checks it; the game draws it with
// Shed/TorqueDecals (the library's pictures, lettering, M_ShedDecal).
//
// The frame is the part's own, in cm: the middle of the box round its meshes as drawn on the bike,
// the bike's axes (x forward, y right, z up). A decal is laid at a point on the surface, facing out
// along the surface's normal there, turned and sized, and seen from outside: its picture's right is
// Up x (-Normal) (on the bike's right side, towards the nose), its picture's up the world's up laid
// flat on the surface (or forward, on a surface facing up or down).
#pragma once

#include "SimMath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TorqueFit
{
namespace Decals
{
using TorqueSim::V3;

constexpr int MaxPerPart = 8;       // a typed word is one decal however many letters it has
constexpr int MaxTextChars = 40;    // a typed word or name
constexpr double MinCm = 1.0, MaxCm = 120.0;
constexpr double Lift = 0.12;       // cm off the paint (the badges' 1.5 mm, a little less)
constexpr int MaxSkinTris = 20000;

enum class Kind : uint8_t
{
	Library,   // a built-in picture (Data/Decals/decals.json): Id
	Text,      // typed lettering: Text in face Id (decals.json "fonts")
	Image,     // the player's own picture: Id its name (Saved/Decals), sent once to friends
};

enum Flags : uint32_t
{
	Filled = 1,        // the fill drawn (else line only)
	UnderLacquer = 2,  // takes the part's own shine, as paintwork; else a sticker's satin
	Flip = 4,          // the picture mirrored left to right
	Outline = 8,       // lettering: an outline in colour 2
	Shadow = 16,       // lettering: a drop shadow in colour 2
	Pair = 32,         // drawn on the other side too, mirrored across the bike's middle
	Worn = 64,         // an old sticker: chipped in specks and at its edges (stage 7)
};

struct Decal
{
	Kind K = Kind::Library;
	std::string Id;            // library id, lettering face, or picture name
	std::string Text;          // the lettering typed
	V3 At;                     // where on the surface (part frame, cm)
	V3 Normal = V3(0.0, 1.0, 0.0);   // facing out there
	double Turn = 0.0;         // degrees, anticlockwise as seen
	double W = 20.0, H = 10.0; // its size on the part (cm)
	uint32_t Rgb1 = 0xC81A12, Rgb2 = 0x0A0A0A;   // fill, ink
	uint32_t Flag = Filled;
	double Arch = 0.0;         // lettering bent on an arch: 0 straight .. 1 a half circle
	bool Has(uint32_t F) const { return (Flag & F) != 0; }
};

// A part's decals as text and back: "decal1;" then each decal's fields split by '|', decals split
// by ';'. Parse skips what doesn't read (and keeps at most MaxPerPart); Clean makes a typed word safe.
std::string ToText(const std::vector<Decal>& L);
std::vector<Decal> Parse(const std::string& Text);
std::string Clean(const std::string& Typed);
// Text made safe from anyone (a friend's, a save): parsed and written again ("" when none read).
std::string Sanitize(const std::string& Text);

// The decal's frame on the surface (part frame, cm): its middle, its picture's right and up across
// the surface (turned by Turn), and out.
struct Frame
{
	V3 O, R, U, N;
};
Frame FrameOf(const Decal& D);
// Its twin across the bike's middle (y = 0): placed mirror-wise, its picture flipped, except
// lettering, which still reads left to right.
Decal Mirrored(const Decal& D);

// The skin for decal D on a part whose triangles are Tris (part frame, cm, three points each):
// every triangle reaching into its rectangle that faces out its way and is the outermost there,
// lifted Lift off along its smoothed normal, the picture laid across it (U left to right, V top
// down, 0 .. 1 inside the rectangle; the material leaves out what's outside it), bent onto an arch
// for lettering that has one. Each triangle (a, b, c) wound so (b - a) x (c - a) points out
// (Normal's way). False if no surface reaches it.
struct Skin
{
	std::vector<V3> Pos, Nrm;
	std::vector<double> U, V;
	std::vector<unsigned> Idx;
	size_t Tris() const { return Idx.size() / 3; }
};
bool MakeSkin(const std::vector<V3>& Tris, const Decal& D, Skin& Out);

// Where a picture's point (U, V) lands with an arch (Arch 0 .. 1) on a W x H decal: the inverse of
// MakeSkin's bend, for the editor's outline. (U, V) in 0 .. 1; returns the surface coordinates (cm,
// x right, y up from the middle).
void ArchPoint(double U, double V, double W, double H, double Arch, double& X, double& Y);

// The surface point and normal under a ray (part frame): the nearest triangle it crosses, its
// normal turned to face the ray. False if none.
bool RayHit(const std::vector<V3>& Tris, const V3& From, const V3& Dir, V3& Hit, V3& Normal);

// A saved set put on another part (stage 7, reusable sets): each decal moved onto this part's
// surface, looked for along its own normal from Reach cm outside its old place, then from the part's
// middle out its way; ones that find no surface are left out.
std::vector<Decal> Snapped(const std::vector<V3>& Tris, const std::vector<Decal>& L, double Reach = 40.0);
} // namespace Decals
} // namespace TorqueFit
