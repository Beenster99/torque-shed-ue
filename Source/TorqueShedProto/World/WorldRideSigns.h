// [OSM stage 6A] The sign system's lettering: any words set as flat triangles from the baked alphabet
// (Data/RideUK/sign_glyphs.json, made by Tools/make_sign_glyphs.py from Noto Sans Bold), so the words on
// signs are part of the square's mesh (RideMesh::Signs) rather than a text component each. Loaded once
// (SetSignFont) before any ride is planned; safe to read from any thread after.
#pragma once

#include "WorldRide.h"

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
} // namespace TorqueWorld
