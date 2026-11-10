// SHA-256 (Ride UK stage 1): the world store names what it holds by hash (a stitched area's road
// hash, a downloaded cell's check), the same ones the Godot game works out. Plain C++.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace TorqueWorld
{
// The hash of the bytes, as 64 lower-case hex digits.
std::string Sha256Hex(const uint8_t* Data, size_t Size);
inline std::string Sha256Hex(const std::string& Bytes) { return Sha256Hex(reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size()); }
} // namespace TorqueWorld
