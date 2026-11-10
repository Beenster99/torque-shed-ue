#include "WorldImage.h"

#include "WorldInflate.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace TorqueWorld
{
namespace
{
uint32_t Be32(const uint8_t* P) { return (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) | (uint32_t(P[2]) << 8) | uint32_t(P[3]); }
uint32_t Le32(const uint8_t* P) { return uint32_t(P[0]) | (uint32_t(P[1]) << 8) | (uint32_t(P[2]) << 16) | (uint32_t(P[3]) << 24); }
const int MaxSide = 16384;
} // namespace

// ---------------------------------------------------------------------------- PNG

bool DecodePng(const uint8_t* Data, size_t Size, Picture& Out)
{
	static const uint8_t Sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	if (Size < 8 + 25 || std::memcmp(Data, Sig, 8) != 0) return false;
	int W = 0, H = 0, Type = -1;
	std::string Packed;
	uint8_t Palette[256 * 3] = {};
	for (size_t At = 8; At + 12 <= Size;)
	{
		const size_t Len = Be32(Data + At);
		const uint8_t* Tag = Data + At + 4;
		const uint8_t* Body = Data + At + 8;
		if (Len > Size - At - 12) return false;
		if (std::memcmp(Tag, "IHDR", 4) == 0)
		{
			if (Len < 13) return false;
			W = int(Be32(Body));
			H = int(Be32(Body + 4));
			Type = Body[9];
			if (Body[8] != 8 || Body[12] != 0 || W <= 0 || H <= 0 || W > MaxSide || H > MaxSide) return false;
		}
		else if (std::memcmp(Tag, "PLTE", 4) == 0) std::memcpy(Palette, Body, Len < sizeof(Palette) ? Len : sizeof(Palette));
		else if (std::memcmp(Tag, "IDAT", 4) == 0) Packed.append(reinterpret_cast<const char*>(Body), Len);
		else if (std::memcmp(Tag, "IEND", 4) == 0) break;
		At += 12 + Len;
	}
	const int Bpp = Type == 0 ? 1 : (Type == 2 ? 3 : (Type == 3 ? 1 : (Type == 4 ? 2 : (Type == 6 ? 4 : 0))));
	if (Bpp == 0 || Packed.empty()) return false;
	const size_t Row = size_t(W) * size_t(Bpp);
	std::string Raw;
	if (!ZlibInflate(reinterpret_cast<const uint8_t*>(Packed.data()), Packed.size(), Raw, (Row + 1) * size_t(H)) || Raw.size() != (Row + 1) * size_t(H))
		return false;
	// Each row was filtered against the one above and the pixel to its left: undone in place.
	std::vector<uint8_t> Pix(Row * size_t(H));
	for (int Y = 0; Y < H; ++Y)
	{
		const uint8_t* In = reinterpret_cast<const uint8_t*>(Raw.data()) + (Row + 1) * size_t(Y);
		uint8_t* Cur = &Pix[Row * size_t(Y)];
		const uint8_t* Up = Y > 0 ? Cur - Row : nullptr;
		const int Filter = In[0];
		if (Filter > 4) return false;
		for (size_t X = 0; X < Row; ++X)
		{
			const int A = X >= size_t(Bpp) ? Cur[X - size_t(Bpp)] : 0;
			const int B = Up != nullptr ? Up[X] : 0;
			const int C = (Up != nullptr && X >= size_t(Bpp)) ? Up[X - size_t(Bpp)] : 0;
			int Pred = 0;
			if (Filter == 1) Pred = A;
			else if (Filter == 2) Pred = B;
			else if (Filter == 3) Pred = (A + B) / 2;
			else if (Filter == 4)
			{
				const int P = A + B - C, Pa = std::abs(P - A), Pb = std::abs(P - B), Pc = std::abs(P - C);
				Pred = (Pa <= Pb && Pa <= Pc) ? A : (Pb <= Pc ? B : C);
			}
			Cur[X] = uint8_t(In[1 + X] + Pred);
		}
	}
	Out.Width = W;
	Out.Height = H;
	Out.Rgb.resize(size_t(W) * size_t(H) * 3);
	for (size_t P = 0; P < size_t(W) * size_t(H); ++P)
	{
		const uint8_t* S = &Pix[P * size_t(Bpp)];
		uint8_t* D = &Out.Rgb[P * 3];
		if (Type == 2 || Type == 6) { D[0] = S[0]; D[1] = S[1]; D[2] = S[2]; }
		else if (Type == 3) { D[0] = Palette[S[0] * 3]; D[1] = Palette[S[0] * 3 + 1]; D[2] = Palette[S[0] * 3 + 2]; }
		else { D[0] = D[1] = D[2] = S[0]; }
	}
	return true;
}

// ---------------------------------------------------------------------------- WebP lossless

namespace
{
// How far back a short distance code points: (rows up * 16) + (8 - columns across), nearest first.
const uint8_t PlaneCode[120] = {
	0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a, 0x26, 0x2a, 0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a,
	0x25, 0x2b, 0x48, 0x04, 0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b, 0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45, 0x4b, 0x34, 0x3c, 0x03,
	0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d, 0x44, 0x4c, 0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
	0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b, 0x32, 0x3e, 0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f,
	0x64, 0x6c, 0x42, 0x4e, 0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b, 0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e, 0x00, 0x74, 0x7c, 0x41,
	0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d, 0x51, 0x5f, 0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70};
const uint8_t LengthOrder[19] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

int Blocks(int Size, int Bits) { return (Size + (1 << Bits) - 1) >> Bits; }

struct Transform
{
	int Type = 0;       // 0 predictor, 1 colour, 2 subtract green, 3 palette
	int Bits = 0;
	int Width = 0;      // the picture's width as this transform sees it
	std::vector<uint32_t> Data;
};

struct Group
{
	PrefixCode Code[5];   // green (and lengths, and the cache), red, blue, alpha, distance
};

class Vp8l
{
public:
	Vp8l(const uint8_t* InData, size_t InSize) : Bits(InData, InSize) {}
	bool Run(Picture& Out);

private:
	bool ReadCode(int Alphabet, PrefixCode& Code);
	bool ReadPixels(int W, int H, bool Main, std::vector<uint32_t>& Out);
	uint32_t Prefixed(int Sym) 
	{
		if (Sym < 4) return uint32_t(Sym + 1);
		const int Extra = (Sym - 2) >> 1;
		const uint32_t Offset = uint32_t(2 + (Sym & 1)) << Extra;
		return Offset + Bits.Read(Extra) + 1;
	}
	BitReader Bits;
};

bool Vp8l::ReadCode(int Alphabet, PrefixCode& Code)
{
	std::vector<uint8_t> Lens(size_t(Alphabet), 0);
	if (Bits.Read(1) != 0)
	{
		// A simple code: one or two symbols.
		const int Count = int(Bits.Read(1)) + 1;
		const int First = int(Bits.Read(Bits.Read(1) != 0 ? 8 : 1));
		if (First >= Alphabet) return false;
		Lens[size_t(First)] = 1;
		if (Count == 2)
		{
			const int Second = int(Bits.Read(8));
			if (Second >= Alphabet) return false;
			Lens[size_t(Second)] = 1;
		}
	}
	else
	{
		uint8_t Cl[19] = {};
		const int NCode = int(Bits.Read(4)) + 4;
		if (NCode > 19) return false;
		for (int I = 0; I < NCode; ++I) Cl[LengthOrder[I]] = uint8_t(Bits.Read(3));
		PrefixCode LenCode;
		if (!LenCode.Build(Cl, 19, true)) return false;
		int MaxSymbols = Alphabet;
		if (Bits.Read(1) != 0)
		{
			const int LenBits = 2 + 2 * int(Bits.Read(3));
			MaxSymbols = 2 + int(Bits.Read(LenBits));
			if (MaxSymbols > Alphabet) return false;
		}
		int At = 0;
		uint8_t Prev = 8;
		while (At < Alphabet)
		{
			if (MaxSymbols-- == 0) break;
			const int Sym = LenCode.Decode(Bits);
			if (Sym < 0 || Bits.Overrun()) return false;
			if (Sym < 16)
			{
				Lens[size_t(At++)] = uint8_t(Sym);
				if (Sym != 0) Prev = uint8_t(Sym);
				continue;
			}
			const int Repeat = Sym == 16 ? 3 + int(Bits.Read(2)) : (Sym == 17 ? 3 + int(Bits.Read(3)) : 11 + int(Bits.Read(7)));
			if (At + Repeat > Alphabet) return false;
			const uint8_t Value = Sym == 16 ? Prev : uint8_t(0);
			for (int K = 0; K < Repeat; ++K) Lens[size_t(At++)] = Value;
		}
	}
	return !Bits.Overrun() && Code.Build(Lens.data(), Alphabet, true);
}

// One entropy-coded picture of ARGB pixels: the main one (which may have a map of which codes are
// used where) or a small one a transform or that map is kept in.
bool Vp8l::ReadPixels(int W, int H, bool Main, std::vector<uint32_t>& Out)
{
	int CacheBits = 0;
	if (Bits.Read(1) != 0)
	{
		CacheBits = int(Bits.Read(4));
		if (CacheBits < 1 || CacheBits > 11) return false;
	}
	int MetaBits = 0;
	std::vector<uint32_t> Meta;
	int Groups = 1;
	if (Main && Bits.Read(1) != 0)
	{
		MetaBits = int(Bits.Read(3)) + 2;
		if (!ReadPixels(Blocks(W, MetaBits), Blocks(H, MetaBits), false, Meta)) return false;
		for (uint32_t& M : Meta)
		{
			M = (M >> 8) & 0xffff;
			if (int(M) + 1 > Groups) Groups = int(M) + 1;
		}
	}
	const int CacheSize = CacheBits > 0 ? 1 << CacheBits : 0;
	std::vector<Group> Codes(static_cast<size_t>(Groups));
	for (Group& G : Codes)
	{
		static const int Alphabets[5] = {256 + 24, 256, 256, 256, 40};
		for (int K = 0; K < 5; ++K)
			if (!ReadCode(Alphabets[K] + (K == 0 ? CacheSize : 0), G.Code[K])) return false;
	}
	std::vector<uint32_t> Cache(size_t(CacheSize), 0);
	const size_t Total = size_t(W) * size_t(H);
	Out.assign(Total, 0);
	const int MetaW = MetaBits > 0 ? Blocks(W, MetaBits) : 0;
	size_t Pos = 0;
	size_t Cached = 0;   // (pixels before this are in the cache)
	const Group* G = &Codes[0];
	int X = 0, Y = 0;
	while (Pos < Total)
	{
		if (MetaBits > 0) G = &Codes[Meta[size_t(Y >> MetaBits) * size_t(MetaW) + size_t(X >> MetaBits)]];
		const int Sym = G->Code[0].Decode(Bits);
		if (Sym < 0 || Bits.Overrun()) return false;
		if (Sym < 256)
		{
			const int R = G->Code[1].Decode(Bits);
			const int B = G->Code[2].Decode(Bits);
			const int A = G->Code[3].Decode(Bits);
			if (R < 0 || B < 0 || A < 0) return false;
			Out[Pos++] = (uint32_t(A) << 24) | (uint32_t(R) << 16) | (uint32_t(Sym) << 8) | uint32_t(B);
			if (++X == W) { X = 0; ++Y; }
		}
		else if (Sym < 256 + 24)
		{
			const size_t Len = Prefixed(Sym - 256);
			const int Ds = G->Code[4].Decode(Bits);
			if (Ds < 0) return false;
			const uint32_t DistCode = Prefixed(Ds);
			size_t Dist = 0;
			if (DistCode > 120) Dist = DistCode - 120;
			else
			{
				const int Pc = PlaneCode[DistCode - 1];
				const int D = (Pc >> 4) * W + (8 - (Pc & 15));
				Dist = D >= 1 ? size_t(D) : 1;
			}
			if (Dist > Pos || Pos + Len > Total || Bits.Overrun()) return false;
			for (size_t K = 0; K < Len; ++K, ++Pos) Out[Pos] = Out[Pos - Dist];
			X += int(Len);
			while (X >= W) { X -= W; ++Y; }
		}
		else
		{
			if (Sym - 280 >= CacheSize) return false;
			for (; Cached < Pos; ++Cached) Cache[(0x1e35a7bdu * Out[Cached]) >> (32 - CacheBits)] = Out[Cached];
			Out[Pos++] = Cache[size_t(Sym - 280)];
			if (++X == W) { X = 0; ++Y; }
		}
		if (CacheSize > 0)
			for (; Cached < Pos; ++Cached) Cache[(0x1e35a7bdu * Out[Cached]) >> (32 - CacheBits)] = Out[Cached];
	}
	return !Bits.Overrun();
}

inline uint32_t AddPixels(uint32_t A, uint32_t B)
{
	const uint32_t Ag = (A & 0xff00ff00u) + (B & 0xff00ff00u);
	const uint32_t Rb = (A & 0x00ff00ffu) + (B & 0x00ff00ffu);
	return (Ag & 0xff00ff00u) | (Rb & 0x00ff00ffu);
}
inline uint32_t Avg2(uint32_t A, uint32_t B) { return (((A ^ B) & 0xfefefefeu) >> 1) + (A & B); }
inline int Chan(uint32_t P, int Shift) { return int((P >> Shift) & 0xff); }
inline uint32_t Clip255(int V) { return uint32_t(V < 0 ? 0 : (V > 255 ? 255 : V)); }

uint32_t Predict(int Mode, uint32_t L, uint32_t T, uint32_t Tl, uint32_t Tr)
{
	switch (Mode)
	{
	case 1: return L;
	case 2: return T;
	case 3: return Tr;
	case 4: return Tl;
	case 5: return Avg2(Avg2(L, Tr), T);
	case 6: return Avg2(L, Tl);
	case 7: return Avg2(L, T);
	case 8: return Avg2(Tl, T);
	case 9: return Avg2(T, Tr);
	case 10: return Avg2(Avg2(L, Tl), Avg2(T, Tr));
	case 11:
	{
		// Whichever of left and above is nearer to left + above - above-left.
		int ToL = 0, ToT = 0;
		for (int S = 0; S < 32; S += 8)
		{
			ToL += std::abs(Chan(T, S) - Chan(Tl, S));
			ToT += std::abs(Chan(L, S) - Chan(Tl, S));
		}
		return ToL < ToT ? L : T;
	}
	case 12:
	{
		uint32_t Out = 0;
		for (int S = 0; S < 32; S += 8) Out |= Clip255(Chan(L, S) + Chan(T, S) - Chan(Tl, S)) << S;
		return Out;
	}
	case 13:
	{
		const uint32_t Av = Avg2(L, T);
		uint32_t Out = 0;
		for (int S = 0; S < 32; S += 8) Out |= Clip255(Chan(Av, S) + (Chan(Av, S) - Chan(Tl, S)) / 2) << S;
		return Out;
	}
	default: return 0xff000000u;
	}
}

inline int Delta(int T, int C) { return (int(int8_t(T)) * int(int8_t(C))) >> 5; }

bool Vp8l::Run(Picture& Out)
{
	if (Bits.Read(8) != 0x2f) return false;
	const int W = int(Bits.Read(14)) + 1, H = int(Bits.Read(14)) + 1;
	Bits.Read(1);   // (whether alpha is used: the heights don't)
	if (Bits.Read(3) != 0 || Bits.Overrun()) return false;
	std::vector<Transform> Transforms;
	int CodedW = W;
	bool Seen[4] = {false, false, false, false};
	while (Bits.Read(1) != 0)
	{
		Transform T;
		T.Type = int(Bits.Read(2));
		if (Seen[T.Type] || Bits.Overrun()) return false;
		Seen[T.Type] = true;
		T.Width = CodedW;
		if (T.Type == 0 || T.Type == 1)
		{
			T.Bits = int(Bits.Read(3)) + 2;
			if (!ReadPixels(Blocks(CodedW, T.Bits), Blocks(H, T.Bits), false, T.Data)) return false;
		}
		else if (T.Type == 3)
		{
			const int Colours = int(Bits.Read(8)) + 1;
			T.Bits = Colours > 16 ? 0 : (Colours > 4 ? 1 : (Colours > 2 ? 2 : 3));
			if (!ReadPixels(Colours, 1, false, T.Data)) return false;
			for (size_t I = 1; I < T.Data.size(); ++I) T.Data[I] = AddPixels(T.Data[I], T.Data[I - 1]);
			T.Data.resize(256, 0);
			CodedW = Blocks(CodedW, T.Bits);
		}
		Transforms.push_back(std::move(T));
	}
	std::vector<uint32_t> Pix;
	if (!ReadPixels(CodedW, H, true, Pix)) return false;
	// The transforms undone, the last one made first.
	for (size_t Ti = Transforms.size(); Ti-- > 0;)
	{
		const Transform& T = Transforms[Ti];
		const int Tw = T.Width;
		if (T.Type == 2)
		{
			for (uint32_t& P : Pix)
			{
				const uint32_t G = (P >> 8) & 0xff;
				P = (P & 0xff00ff00u) | (((P & 0x00ff00ffu) + ((G << 16) | G)) & 0x00ff00ffu);
			}
		}
		else if (T.Type == 1)
		{
			const int Bw = Blocks(Tw, T.Bits);
			for (int Y = 0; Y < H; ++Y)
				for (int X = 0; X < Tw; ++X)
				{
					const uint32_t M = T.Data[size_t(Y >> T.Bits) * size_t(Bw) + size_t(X >> T.Bits)];
					uint32_t& P = Pix[size_t(Y) * size_t(Tw) + size_t(X)];
					const int G = Chan(P, 8);
					const int R = (Chan(P, 16) + Delta(Chan(M, 0), G)) & 0xff;
					const int B = (Chan(P, 0) + Delta(Chan(M, 8), G) + Delta(Chan(M, 16), R)) & 0xff;
					P = (P & 0xff00ff00u) | (uint32_t(R) << 16) | uint32_t(B);
				}
		}
		else if (T.Type == 0)
		{
			const int Bw = Blocks(Tw, T.Bits);
			for (int Y = 0; Y < H; ++Y)
				for (int X = 0; X < Tw; ++X)
				{
					const size_t At = size_t(Y) * size_t(Tw) + size_t(X);
					uint32_t Pred = 0xff000000u;
					if (Y == 0) Pred = X == 0 ? 0xff000000u : Pix[At - 1];
					else if (X == 0) Pred = Pix[At - size_t(Tw)];
					else
					{
						const int Mode = Chan(T.Data[size_t(Y >> T.Bits) * size_t(Bw) + size_t(X >> T.Bits)], 8) & 15;
						// (above-right of the row's last pixel is the row's first: the next in memory)
						Pred = Predict(Mode, Pix[At - 1], Pix[At - size_t(Tw)], Pix[At - size_t(Tw) - 1], Pix[At - size_t(Tw) + 1]);
					}
					Pix[At] = AddPixels(Pix[At], Pred);
				}
		}
		else
		{
			// A palette: each coded pixel's green holds one index, or several small ones packed.
			const int Packed = Blocks(Tw, T.Bits);
			std::vector<uint32_t> Wide(size_t(Tw) * size_t(H));
			const int Per = 1 << T.Bits, Each = 8 >> T.Bits;
			for (int Y = 0; Y < H; ++Y)
				for (int X = 0; X < Tw; ++X)
				{
					const uint32_t G = (Pix[size_t(Y) * size_t(Packed) + size_t(X >> T.Bits)] >> 8) & 0xff;
					const uint32_t Index = T.Bits == 0 ? G : ((G >> ((X & (Per - 1)) * Each)) & ((1u << Each) - 1));
					Wide[size_t(Y) * size_t(Tw) + size_t(X)] = T.Data[Index];
				}
			Pix.swap(Wide);
		}
	}
	if (Pix.size() != size_t(W) * size_t(H)) return false;
	Out.Width = W;
	Out.Height = H;
	Out.Rgb.resize(Pix.size() * 3);
	for (size_t P = 0; P < Pix.size(); ++P)
	{
		Out.Rgb[P * 3] = uint8_t(Pix[P] >> 16);
		Out.Rgb[P * 3 + 1] = uint8_t(Pix[P] >> 8);
		Out.Rgb[P * 3 + 2] = uint8_t(Pix[P]);
	}
	return true;
}
} // namespace

bool DecodeWebpLossless(const uint8_t* Data, size_t Size, Picture& Out)
{
	if (Size < 20 || std::memcmp(Data, "RIFF", 4) != 0 || std::memcmp(Data + 8, "WEBP", 4) != 0) return false;
	for (size_t At = 12; At + 8 <= Size;)
	{
		const size_t Len = Le32(Data + At + 4);
		if (Len > Size - At - 8) return false;
		if (std::memcmp(Data + At, "VP8L", 4) == 0)
		{
			Vp8l Reader(Data + At + 8, Len);
			return Reader.Run(Out);
		}
		At += 8 + Len + (Len & 1);
	}
	return false;
}

bool DecodePicture(const uint8_t* Data, size_t Size, Picture& Out)
{
	if (Size >= 12 && std::memcmp(Data + 8, "WEBP", 4) == 0) return DecodeWebpLossless(Data, Size, Out);
	return DecodePng(Data, Size, Out);
}
} // namespace TorqueWorld
