#include "WorldInflate.h"

namespace TorqueWorld
{
bool PrefixCode::Build(const uint8_t* Lengths, int Symbols, bool ZeroBitSingle)
{
	for (uint16_t& C : Counts) C = 0;
	Single = -1;
	int Used = 0, Last = -1;
	for (int S = 0; S < Symbols; ++S)
	{
		if (Lengths[S] > 15) return false;
		++Counts[Lengths[S]];
		if (Lengths[S] != 0) { ++Used; Last = S; }
	}
	Counts[0] = 0;
	Sorted.assign(size_t(Used), 0);
	Fast.assign(size_t(1) << FastBits, 0);
	if (Used == 0) return ZeroBitSingle ? false : true;   // (deflate allows a block with no distance codes)
	if (Used == 1 && ZeroBitSingle)
	{
		Single = Last;
		return true;
	}
	int Left = 1;
	for (int L = 1; L <= 15; ++L)
	{
		Left = (Left << 1) - int(Counts[L]);
		if (Left < 0) return false;   // more codes than the lengths allow
	}
	if (Left > 0 && ZeroBitSingle) return false;   // (WebP's codes must be complete; deflate lets one short code be)
	uint16_t Offs[16];
	Offs[1] = 0;
	for (int L = 1; L < 15; ++L) Offs[L + 1] = uint16_t(Offs[L] + Counts[L]);
	for (int S = 0; S < Symbols; ++S)
		if (Lengths[S] != 0) Sorted[Offs[Lengths[S]]++] = uint16_t(S);
	// The quick table: every FastBits bits that start with a short code give its symbol straight off.
	uint32_t Code = 0;
	size_t Index = 0;
	for (int L = 1; L <= FastBits; ++L)
	{
		for (int K = 0; K < int(Counts[L]); ++K, ++Code, ++Index)
		{
			uint32_t Rev = 0;
			for (int B = 0; B < L; ++B) Rev |= ((Code >> B) & 1u) << (L - 1 - B);
			const uint32_t Entry = (uint32_t(Sorted[Index]) << 4) | uint32_t(L);
			for (uint32_t At = Rev; At < (uint32_t(1) << FastBits); At += uint32_t(1) << L) Fast[At] = Entry;
		}
		Code <<= 1;
	}
	return true;
}

int PrefixCode::Slow(BitReader& Bits) const
{
	int Code = 0, First = 0, Index = 0;
	for (int L = 1; L <= 15; ++L)
	{
		Code |= int(Bits.Read(1));
		const int Count = int(Counts[L]);
		if (Code - Count < First) return int(Sorted[size_t(Index + (Code - First))]);
		Index += Count;
		First += Count;
		First <<= 1;
		Code <<= 1;
		if (Bits.Overrun()) return -1;
	}
	return -1;
}

namespace
{
const uint16_t LenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t DistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t ClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

bool Codes(BitReader& Bits, const PrefixCode& Lit, const PrefixCode& Dist, std::string& Out, size_t Start, size_t MaxOut)
{
	for (;;)
	{
		const int Sym = Lit.Decode(Bits);
		if (Sym < 0 || Bits.Overrun()) return false;
		if (Sym < 256)
		{
			if (Out.size() - Start >= MaxOut) return false;
			Out.push_back(char(Sym));
			continue;
		}
		if (Sym == 256) return true;
		if (Sym > 285) return false;
		const size_t Len = size_t(LenBase[Sym - 257]) + Bits.Read(LenExtra[Sym - 257]);
		const int Ds = Dist.Decode(Bits);
		if (Ds < 0 || Ds > 29) return false;
		const size_t Back = size_t(DistBase[Ds]) + Bits.Read(DistExtra[Ds]);
		if (Back > Out.size() - Start || Out.size() - Start + Len > MaxOut) return false;
		size_t From = Out.size() - Back;
		for (size_t K = 0; K < Len; ++K) Out.push_back(Out[From++]);
	}
}
} // namespace

bool Inflate(const uint8_t* In, size_t InSize, std::string& Out, size_t MaxOut, size_t* Used)
{
	BitReader Bits(In, InSize);
	const size_t Start = Out.size();
	for (;;)
	{
		const bool Final = Bits.Read(1) != 0;
		const uint32_t Type = Bits.Read(2);
		if (Bits.Overrun()) return false;
		if (Type == 0)
		{
			const size_t At = Bits.AlignToByte();
			if (At + 4 > InSize) return false;
			const size_t Len = size_t(In[At]) | (size_t(In[At + 1]) << 8);
			const size_t NLen = size_t(In[At + 2]) | (size_t(In[At + 3]) << 8);
			if ((Len ^ 0xffff) != NLen || At + 4 + Len > InSize || Out.size() - Start + Len > MaxOut) return false;
			Out.append(reinterpret_cast<const char*>(In) + At + 4, Len);
			Bits.SeekByte(At + 4 + Len);
		}
		else if (Type == 1)
		{
			uint8_t L[288], D[30];
			for (int I = 0; I < 288; ++I) L[I] = uint8_t(I < 144 ? 8 : (I < 256 ? 9 : (I < 280 ? 7 : 8)));
			for (int I = 0; I < 30; ++I) D[I] = 5;
			PrefixCode Lit, Dist;
			if (!Lit.Build(L, 288, false) || !Dist.Build(D, 30, false)) return false;
			if (!Codes(Bits, Lit, Dist, Out, Start, MaxOut)) return false;
		}
		else if (Type == 2)
		{
			const int NLit = int(Bits.Read(5)) + 257, NDist = int(Bits.Read(5)) + 1, NCode = int(Bits.Read(4)) + 4;
			if (NLit > 286 || NDist > 30) return false;
			uint8_t Cl[19] = {};
			for (int I = 0; I < NCode; ++I) Cl[ClOrder[I]] = uint8_t(Bits.Read(3));
			PrefixCode LenCode;
			if (!LenCode.Build(Cl, 19, false)) return false;
			uint8_t Lens[286 + 30] = {};
			int At = 0;
			while (At < NLit + NDist)
			{
				const int Sym = LenCode.Decode(Bits);
				if (Sym < 0 || Bits.Overrun()) return false;
				if (Sym < 16) { Lens[At++] = uint8_t(Sym); continue; }
				int Repeat = 0;
				uint8_t Value = 0;
				if (Sym == 16)
				{
					if (At == 0) return false;
					Value = Lens[At - 1];
					Repeat = 3 + int(Bits.Read(2));
				}
				else if (Sym == 17) Repeat = 3 + int(Bits.Read(3));
				else Repeat = 11 + int(Bits.Read(7));
				if (At + Repeat > NLit + NDist) return false;
				while (Repeat-- > 0) Lens[At++] = Value;
			}
			if (Lens[256] == 0) return false;
			PrefixCode Lit, Dist;
			if (!Lit.Build(Lens, NLit, false) || !Dist.Build(Lens + NLit, NDist, false)) return false;
			if (!Codes(Bits, Lit, Dist, Out, Start, MaxOut)) return false;
		}
		else return false;
		if (Final) break;
	}
	if (Used != nullptr) *Used = Bits.AlignToByte();
	return !Bits.Overrun();
}

bool ZlibInflate(const uint8_t* In, size_t InSize, std::string& Out, size_t MaxOut)
{
	if (InSize < 6 || (In[0] & 15) != 8 || ((uint32_t(In[0]) << 8) | In[1]) % 31 != 0 || (In[1] & 0x20) != 0) return false;
	const size_t Start = Out.size();
	size_t Used = 0;
	if (!Inflate(In + 2, InSize - 2, Out, MaxOut, &Used) || 2 + Used + 4 > InSize) return false;
	uint32_t A = 1, B = 0;
	for (size_t I = Start; I < Out.size();)
	{
		const size_t End = I + 5552 < Out.size() ? I + 5552 : Out.size();
		for (; I < End; ++I) { A += uint8_t(Out[I]); B += A; }
		A %= 65521;
		B %= 65521;
	}
	const uint8_t* T = In + 2 + Used;
	return ((uint32_t(T[0]) << 24) | (uint32_t(T[1]) << 16) | (uint32_t(T[2]) << 8) | uint32_t(T[3])) == ((B << 16) | A);
}
} // namespace TorqueWorld
