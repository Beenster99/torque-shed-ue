#pragma once
// [bought bikes] A bought bike's drawing ships in the game folder scrambled (Data/BikesV2/<model>.tsb), not as a plain
// model file anyone could lift out of it: the model's licence lets it be used inside the game, not handed out
// (beenster's Multistrada, Nov 25). Tools/bought_bikes.py writes it; the game reads it back here. No UE in this file
// (the SimTest tools use it too).
#include <cstdint>
#include <string>

namespace TorqueFit
{
// "TSB1" then the glb, every byte xor'd with a xorshift32 stream. Same bytes in, same bytes out: Scramble undoes itself.
inline void BoughtXor(std::string& Bytes, size_t From)
{
	uint32_t X = 0x2545F491u;
	for (size_t I = From; I < Bytes.size(); ++I)
	{
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		Bytes[I] = static_cast<char>(static_cast<uint8_t>(Bytes[I]) ^ static_cast<uint8_t>(X & 0xFFu));
	}
}

// True and the glb's bytes in place if Bytes is a scrambled bought drawing; false (unchanged) if it isn't one.
inline bool UnscrambleBought(std::string& Bytes)
{
	if (Bytes.size() < 4 || Bytes.compare(0, 4, "TSB1") != 0) return false;
	BoughtXor(Bytes, 4);
	Bytes.erase(0, 4);
	return true;
}
} // namespace TorqueFit
