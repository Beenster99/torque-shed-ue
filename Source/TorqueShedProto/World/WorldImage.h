// Pictures the world store sends (Ride UK stage 1): the height tiles are WebP lossless (Mapterhorn)
// or PNG (AWS), and the game must read them itself, the same on every machine, so every rider's
// ground is the same to the last bit. Plain C++; bad data gives false, never a crash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TorqueWorld
{
struct Picture
{
	int Width = 0, Height = 0;
	std::vector<uint8_t> Rgb;   // Width x Height x 3, rows from the top
};

// A PNG of 8 bits a channel (grey, colour or palette, with or without alpha), not interlaced.
bool DecodePng(const uint8_t* Data, size_t Size, Picture& Out);
// A WebP whose picture is lossless (a "VP8L" chunk). Lossy WebP is not read: heights can't be lossy.
bool DecodeWebpLossless(const uint8_t* Data, size_t Size, Picture& Out);
// Either, told apart by the file's first bytes.
bool DecodePicture(const uint8_t* Data, size_t Size, Picture& Out);
} // namespace TorqueWorld
