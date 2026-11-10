// Reading a zip (Ride UK stage 1): the world store sends each cell as a zip of its folder. Plain
// C++; only what those zips use (files stored or deflated, no encryption, no zip64).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TorqueWorld
{
struct ZipEntry
{
	std::string Name;      // with '/' between folders; a folder's own entry ends in '/'
	uint32_t Method = 0;   // 0 stored, 8 deflated
	uint32_t Packed = 0, Size = 0, Crc = 0, Offset = 0;
};

// The zip's list of files; false when the bytes are no zip.
bool ZipList(const std::string& Zip, std::vector<ZipEntry>& Out);
// One file's bytes, checked against its CRC; false when it can't be read.
bool ZipRead(const std::string& Zip, const ZipEntry& Entry, std::string& Out);
uint32_t Crc32(const uint8_t* Data, size_t Size);
} // namespace TorqueWorld
