// [OSM stage 6A] The sign system's lettering: any words set as flat triangles from the baked alphabet
// (Data/RideUK/sign_glyphs.json, made by Tools/make_sign_glyphs.py from Noto Sans Bold), so the words on
// signs are part of the square's mesh (RideMesh::Signs) rather than a text component each. Loaded once
// (SetSignFont) before any ride is planned; safe to read from any thread after.
#pragma once

#include "WorldRide.h"
#include "WorldRoute.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
struct SignGlyph
{
	float Width = 0.0f;           // its advance (em)
	std::vector<float> Points;    // x, y each (em: x from its start, y up from the baseline)
	std::vector<uint32_t> Index;  // triangles
};
struct SignFont
{
	float Cap = 0.714f, XHeight = 0.536f, Space = 0.26f;   // a capital's and a small letter's height, a space's width (em)
	std::unordered_map<uint32_t, SignGlyph> Glyphs;        // by code point
};

// Reads the baked alphabet (its JSON): false if it isn't one, and the one before stays.
bool SetSignFont(const std::string& Text);
std::shared_ptr<const SignFont> SignFontNow();   // null until one is set
// [OSM stage 6C] The places direction signs can name (Data/World/uk_places.tsv, as the Route planner's
// gazetteer reads it), set once like the font: false if none read.
bool SetSignPlaces(const std::string& Text);
std::shared_ptr<const Route::Gazetteer> SignPlacesNow();   // null until set

namespace SignText
{
// A capital's height as a share of a text component's world size (RideMesh::Label's Size), so words
// moved from labels to triangles stand as tall as they did.
constexpr double CapOfEm = 0.7;
// Each code point of UTF-8 text (a malformed byte as U+FFFD).
std::vector<uint32_t> CodePoints(const std::string& Utf8);
// How wide the words are set with capitals CapHigh m tall (Wide, m); false (Wide 0) if there is no font or it
// lacks one of their letters.
bool Width(const std::string& Utf8, double CapHigh, double& Wide);
// The words laid flat in the upright plane through At facing Face (a level unit direction, towards the
// reader), capitals CapHigh m tall with their middle at At.Y; Align -1 starts them at At, 0 centres them
// on it, 1 ends them there. Into Into (its own origin) in Colour, facing Face. False, and nothing drawn, if
// there is no font or it lacks a letter (the caller falls back to a RideMesh::Label).
bool Set(RideMesh& Into, double X, double Y, double Z, double FaceX, double FaceZ, double CapHigh, const std::string& Utf8, const float Colour[3], int Align = 0);
} // namespace SignText

// [OSM stage 6A] A sign's plates and symbols, flat in an upright plane facing the reader: shapes made as
// a convex outline round corner centres (each corner rounded by Round), so a border is the same outline
// drawn Round less and the fill inside it. All in metres about the plate's middle: u to the reader's
// right, v up. Each triangle wound to face the reader (Face, a level unit direction towards them).
namespace SignPlate
{
struct Frame
{
	double X = 0.0, Y = 0.0, Z = 0.0;   // the plate's middle
	double Fx = 0.0, Fz = 1.0;          // towards the reader
};
struct Shape
{
	std::vector<double> Corners;   // u, v each, anticlockwise (one for a roundel)
	double Round = 0.0;
	int Segments = 32;             // round a roundel
};
Shape Roundel(double Radius);
// A triangle Side across (as the sign's size is given: its sharp outline's side), its middle (the
// centroid) at 0; point up (a warning) or down (give way); corners rounded Side * 0.07.
Shape Triangle(double Side, bool PointUp = true);
Shape Rect(double W, double H, double Round);
// A rectangle with its end at Point (-1 the reader's left, +1 right) brought to a point (a direction
// sign's flag, a fingerpost's arm): W along its whole length to the tip, H high, the point Tip long.
Shape Flag(double W, double H, int Point, double Tip, double Round);
// The shape's outline Inset in from its edge (u, v each, anticlockwise).
std::vector<double> Outline(const Shape& S, double Inset);
// The shape filled (Inset in from its edge), Proud m out towards the reader.
void Fill(RideMesh& Into, const Frame& At, const Shape& S, double Inset, double Proud, const float Colour[3]);
// A band round its edge, from Inset to Inset + Wide in.
void Band(RideMesh& Into, const Frame& At, const Shape& S, double Inset, double Wide, double Proud, const float Colour[3]);
// The whole plate: its back (Back, facing away, Proud - 0.02), its border Rim m wide in Edge and its face
// in Ground inside it (Into). Rim 0 for none.
void Plate(RideMesh& Into, RideMesh& Back, const Frame& At, const Shape& S, const float Ground[3], double Rim, const float Edge[3], const float BackColour[3], double Proud = 0.0);
// A convex polygon (u, v each, either winding) and a line Wide m thick along points (u, v each).
void Poly(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Proud, const float Colour[3]);
void Stroke(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Wide, double Proud, const float Colour[3]);
// A stroke ended by an arrowhead at its last point, Head m long.
void Arrow(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Wide, double Head, double Proud, const float Colour[3]);

// The warning triangles' symbols (Britain's: the Traffic Signs Regulations' diagrams, drawn simply).
enum ESymbol
{
	SymBendLeft, SymBendRight, SymDoubleBendLeft, SymDoubleBendRight,   // 512, 513 (the first bend to the left or right)
	SymCrossroads, SymSideLeft, SymSideRight, SymTJunction,            // 504.1, 506.1, 505.1
	SymRoundabout, SymSignals, SymSteepDown, SymSteepUp,              // 510, 543, 523.1, 524.1 (Figure the gradient)
	SymHumps, SymNarrows, SymDanger,                                  // 557.1, 516, 562 (other danger: with a plate)
	// [junction priority 168] Not warnings but orders, drawn by Warning in the same room so they stand on the
	// same post: Give Way (602, the red-bordered triangle point down with GIVE WAY over its middle) and
	// Stop (601.1, the red octagon with STOP in white)
	SymGiveWay, SymStop,
	SymKinds
};
// A warning sign: the red-bordered white triangle Side across with its symbol in black (and Figure,
// "20%", on a hill's), Proud out from At; its back grey into Back. Its middle is At (the centroid).
void Warning(RideMesh& Into, RideMesh& Back, const Frame& At, double Side, int Symbol, const std::string& Figure);
// A rectangular plate of words: white, a black border and black capitals CapHigh m tall (or Ink on
// Ground), wide enough for them; false if a letter is missing (nothing drawn). Its size into W, H.
bool WordPlate(RideMesh& Into, RideMesh& Back, const Frame& At, double CapHigh, const std::string& Utf8, const float Ground[3], const float Ink[3], double& W, double& H);

// [OSM stage 6C] Direction signs, in a scheme: Britain's white with black words and border (local
// roads), green with white (primary routes), blue with white (motorways). CapHigh their capitals' height.
enum EScheme { SchemeLocal = 0, SchemePrimary = 1, SchemeMotorway = 2, SchemeBrown = 3 };   // (brown: tourist signs, white words)
// A map-type advance direction sign: the road ahead as a stem with an arrow at its top and each side
// road an arm off it with its arrow, the places each way at its end. Arms: "^Hawes;Leyburn|<Askrigg|>Reeth"
// ('^' ahead, '<' left, '>' right; places by ';'). Its size into W, H; drawn only when Into is given
// (false, nothing drawn, if a letter is missing or there are no places). [roundabouts round 2] An arm "o"
// first: the junction is a roundabout, a ring on the stem with the arms out of it.
bool AdvanceBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Arms, double& W, double& H);
// A route confirmation sign after a junction: the places ahead with their miles, a column each.
// Lines: "Hawes\t12|Leyburn\t28".
bool ConfirmBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Lines, double& W, double& H);
// A junction's flag: pointing ('<' left or '>' right first) with its place and miles: "<Askrigg\t3".
bool FlagBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Words, double& W, double& H);
// [OSM stage 6F] A motorway exit's countdown marker: blue, Bars white stripes slanting up to the left (3 at
// 300 yards, 2 at 200, 1 at 100), 0.9 by 1.2 m about At.
void Countdown(RideMesh& Into, RideMesh& Back, const Frame& At, int Bars);
// Miles as signs show them: halves under three ("½", "1½"), whole miles after; "" under a quarter.
std::string Miles(double Metres);
} // namespace SignPlate
} // namespace TorqueWorld
