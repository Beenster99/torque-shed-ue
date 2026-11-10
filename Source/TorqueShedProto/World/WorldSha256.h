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

// SHA-256 over several runs of bytes, one after another (a folder's files).
class Sha256
{
public:
	Sha256();
	void Update(const uint8_t* Data, size_t Size);
	void Update(const std::string& Bytes) { Update(reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size()); }
	std::string FinishHex();

private:
	uint32_t H[8];
	uint8_t Buf[64];
	size_t Held = 0;
	uint64_t Total = 0;
};

// SHA-1, as 40 lower-case hex digits: the store names a cell's streams, trees and features by the
// first 12 of their text's.
std::string Sha1Hex(const std::string& Bytes);
} // namespace TorqueWorld
