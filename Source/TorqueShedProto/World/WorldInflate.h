// Reading squeezed data (Ride UK stage 1): the bit reader and prefix codes that both zlib's deflate
// (PNG tiles, the store's cell archives and cover) and WebP's lossless pictures (the height tiles)
// are built on, and deflate itself. Plain C++, no exceptions: bad data gives false, never a crash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TorqueWorld
{
// Bits from a run of bytes, lowest bit of each byte first. Past the end it reads zeros and says so.
class BitReader
{
public:
	BitReader(const uint8_t* InData, size_t InSize) : Data(InData), Size(InSize) {}
	uint32_t Peek(int N)   // N up to 32
	{
		if (Count < N) Fill();
		return uint32_t(Acc & ((uint64_t(1) << N) - 1));
	}
	void Skip(int N)
	{
		if (Count < N) { Fill(); if (Count < N) { Over = true; Acc = 0; Count = 0; return; } }
		Acc >>= N;
		Count -= N;
	}
	uint32_t Read(int N)
	{
		if (N == 0) return 0;
		const uint32_t V = Peek(N);
		Skip(N);
		return V;
	}
	// Drops the bits left in the byte being read, and gives where the next whole byte is.
	size_t AlignToByte()
	{
		Skip(Count & 7);
		return Pos - size_t(Count / 8);
	}
	void SeekByte(size_t At) { Pos = At < Size ? At : Size; Acc = 0; Count = 0; }
	bool Overrun() const { return Over; }

private:
	void Fill()
	{
		while (Count <= 56 && Pos < Size)
		{
			Acc |= uint64_t(Data[Pos++]) << Count;
			Count += 8;
		}
	}
	const uint8_t* Data;
	size_t Size;
	size_t Pos = 0;
	uint64_t Acc = 0;
	int Count = 0;
	bool Over = false;
};

// A canonical prefix (Huffman) code, as deflate and WebP both make them from a list of lengths.
class PrefixCode
{
public:
	// From each symbol's length in bits (0: unused, up to 15). With ZeroBitSingle, a code with one
	// symbol takes no bits at all (WebP's rule; in deflate it takes one). False for lengths that are
	// no code.
	bool Build(const uint8_t* Lengths, int Symbols, bool ZeroBitSingle);
	// The next symbol, or -1 on bad data.
	int Decode(BitReader& Bits) const
	{
		if (Single >= 0) return Single;
		const uint32_t E = Fast[Bits.Peek(FastBits)];
		if (E != 0)
		{
			Bits.Skip(int(E & 15));
			return int(E >> 4);
		}
		return Slow(Bits);
	}

private:
	static constexpr int FastBits = 9;
	int Slow(BitReader& Bits) const;
	uint16_t Counts[16] = {};
	std::vector<uint16_t> Sorted;       // the symbols in code order
	std::vector<uint32_t> Fast;         // by the next FastBits bits: symbol * 16 + length, 0 for a longer code
	int Single = -1;
};

// Raw deflate. Out is appended to; stops with false past MaxOut bytes or on bad data. Used: the
// bytes of In it read, when asked.
bool Inflate(const uint8_t* In, size_t InSize, std::string& Out, size_t MaxOut = size_t(1) << 30, size_t* Used = nullptr);
// A zlib stream (its two-byte header, deflate, an Adler-32 check).
bool ZlibInflate(const uint8_t* In, size_t InSize, std::string& Out, size_t MaxOut = size_t(1) << 30);
} // namespace TorqueWorld
