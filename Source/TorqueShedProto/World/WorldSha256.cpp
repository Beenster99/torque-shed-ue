#include "WorldSha256.h"

#include <cstring>

namespace TorqueWorld
{
namespace
{
const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
	0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
	0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
	0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
	0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t Rotr(uint32_t V, int N) { return (V >> N) | (V << (32 - N)); }

void Block(uint32_t H[8], const uint8_t* P)
{
	uint32_t W[64];
	for (int I = 0; I < 16; ++I)
		W[I] = (uint32_t(P[I * 4]) << 24) | (uint32_t(P[I * 4 + 1]) << 16) | (uint32_t(P[I * 4 + 2]) << 8) | uint32_t(P[I * 4 + 3]);
	for (int I = 16; I < 64; ++I)
	{
		const uint32_t S0 = Rotr(W[I - 15], 7) ^ Rotr(W[I - 15], 18) ^ (W[I - 15] >> 3);
		const uint32_t S1 = Rotr(W[I - 2], 17) ^ Rotr(W[I - 2], 19) ^ (W[I - 2] >> 10);
		W[I] = W[I - 16] + S0 + W[I - 7] + S1;
	}
	uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4], F = H[5], G = H[6], Hh = H[7];
	for (int I = 0; I < 64; ++I)
	{
		const uint32_t S1 = Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25);
		const uint32_t Ch = (E & F) ^ (~E & G);
		const uint32_t T1 = Hh + S1 + Ch + K[I] + W[I];
		const uint32_t S0 = Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22);
		const uint32_t Maj = (A & B) ^ (A & C) ^ (B & C);
		const uint32_t T2 = S0 + Maj;
		Hh = G; G = F; F = E; E = D + T1; D = C; C = B; B = A; A = T1 + T2;
	}
	H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E; H[5] += F; H[6] += G; H[7] += Hh;
}
} // namespace

Sha256::Sha256()
{
	static const uint32_t Start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::memcpy(H, Start, sizeof(H));
}

void Sha256::Update(const uint8_t* Data, size_t Size)
{
	Total += Size;
	size_t At = 0;
	if (Held > 0)
	{
		const size_t Take = Size < 64 - Held ? Size : 64 - Held;
		std::memcpy(Buf + Held, Data, Take);
		Held += Take;
		At = Take;
		if (Held < 64) return;
		Block(H, Buf);
		Held = 0;
	}
	for (; At + 64 <= Size; At += 64) Block(H, Data + At);
	if (At < Size)
	{
		Held = Size - At;
		std::memcpy(Buf, Data + At, Held);
	}
}

std::string Sha256::FinishHex()
{
	uint8_t Tail[128];
	const size_t Left = Held;
	if (Left > 0) std::memcpy(Tail, Buf, Left);
	Tail[Left] = 0x80;
	const size_t Whole = Left + 1 + 8 <= 64 ? 64 : 128;
	std::memset(Tail + Left + 1, 0, Whole - Left - 1);
	const uint64_t Bits = Total * 8;
	for (int I = 0; I < 8; ++I) Tail[Whole - 1 - size_t(I)] = uint8_t(Bits >> (8 * I));
	Block(H, Tail);
	if (Whole == 128) Block(H, Tail + 64);
	static const char* Hex = "0123456789abcdef";
	std::string Out;
	Out.reserve(64);
	for (int I = 0; I < 8; ++I)
		for (int S = 28; S >= 0; S -= 4) Out.push_back(Hex[(H[I] >> S) & 15]);
	return Out;
}

std::string Sha256Hex(const uint8_t* Data, size_t Size)
{
	Sha256 Ctx;
	Ctx.Update(Data, Size);
	return Ctx.FinishHex();
}

std::string Sha1Hex(const std::string& Bytes)
{
	uint32_t H[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	std::string Msg = Bytes;
	Msg.push_back(char(0x80));
	while (Msg.size() % 64 != 56) Msg.push_back(char(0));
	const uint64_t Bits = uint64_t(Bytes.size()) * 8;
	for (int I = 7; I >= 0; --I) Msg.push_back(char(uint8_t(Bits >> (8 * I))));
	for (size_t At = 0; At < Msg.size(); At += 64)
	{
		const uint8_t* P = reinterpret_cast<const uint8_t*>(Msg.data()) + At;
		uint32_t W[80];
		for (int I = 0; I < 16; ++I)
			W[I] = (uint32_t(P[I * 4]) << 24) | (uint32_t(P[I * 4 + 1]) << 16) | (uint32_t(P[I * 4 + 2]) << 8) | uint32_t(P[I * 4 + 3]);
		for (int I = 16; I < 80; ++I)
		{
			const uint32_t V = W[I - 3] ^ W[I - 8] ^ W[I - 14] ^ W[I - 16];
			W[I] = (V << 1) | (V >> 31);
		}
		uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4];
		for (int I = 0; I < 80; ++I)
		{
			const uint32_t F = I < 20 ? ((B & C) | (~B & D)) : (I < 40 ? (B ^ C ^ D) : (I < 60 ? ((B & C) | (B & D) | (C & D)) : (B ^ C ^ D)));
			const uint32_t Kk = I < 20 ? 0x5a827999u : (I < 40 ? 0x6ed9eba1u : (I < 60 ? 0x8f1bbcdcu : 0xca62c1d6u));
			const uint32_t T = ((A << 5) | (A >> 27)) + F + E + Kk + W[I];
			E = D; D = C; C = (B << 30) | (B >> 2); B = A; A = T;
		}
		H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E;
	}
	static const char* Hex = "0123456789abcdef";
	std::string Out;
	for (int I = 0; I < 5; ++I)
		for (int S = 28; S >= 0; S -= 4) Out.push_back(Hex[(H[I] >> S) & 15]);
	return Out;
}
} // namespace TorqueWorld
