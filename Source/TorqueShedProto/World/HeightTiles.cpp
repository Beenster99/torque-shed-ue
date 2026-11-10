#include "HeightTiles.h"

#include "WorldFiles.h"
#include "WorldImage.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace TorqueWorld
{
namespace
{
inline int Raw24(const uint8_t* P) { return int(P[0]) * 65536 + int(P[1]) * 256 + int(P[2]); }
inline double Metres(const uint8_t* P) { return P[0] * 256.0 + P[1] + P[2] / 256.0 - 32768.0; }
} // namespace

double HeightTile::At(int I, int J) const
{
	const size_t P = size_t(J) * size_t(Size) + size_t(I);
	double H = Metres(&Rgb[P * 3]);
	if (H <= HeightStore::SeaAbs && !Sea.empty() && Sea[P] > 0)
		H = -(2.0 + (HeightStore::SeaDepth - 2.0) * std::min(1.0, std::max(0.0, Sea[P] / 255.0 * 1.6)));
	return H;
}

bool HeightTile::IsSea(int I, int J) const
{
	const size_t P = size_t(J) * size_t(Size) + size_t(I);
	if (Sea.empty() || Sea[P] == 0) return false;
	return Metres(&Rgb[P * 3]) <= HeightStore::SeaAbs;
}

std::string HeightStore::Url(const TileKey& Key, bool FromAws)
{
	return (FromAws ? "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/" : "https://tiles.mapterhorn.com/") + Key.Rel() + (FromAws ? ".png" : ".webp");
}

std::string HeightStore::PathOf(const TileKey& Key, bool* InDisk) const
{
	// (past AwsTop only Mapterhorn's: AWS's fetched there before are passed over)
	for (size_t R = 0; R <= Dirs.size(); ++R)
	{
		const std::string& Root = R == 0 ? Disk : Dirs[R - 1];
		if (Root.empty()) continue;
		for (int E = 0; E < (Key.Zoom <= AwsTop ? 2 : 1); ++E)
		{
			const std::string P = Files::Join(Root, Key.Rel() + (E == 0 ? ".webp" : ".png"));
			if (Files::Exists(P))
			{
				if (InDisk != nullptr) *InDisk = R == 0;
				return P;
			}
		}
	}
	return std::string();
}

std::string HeightStore::NonePath(const TileKey& Key, bool* InDisk) const
{
	if (Key.Zoom <= AwsTop) return std::string();
	for (size_t R = 0; R <= Dirs.size(); ++R)
	{
		const std::string& Root = R == 0 ? Disk : Dirs[R - 1];
		if (Root.empty()) continue;
		const std::string P = Files::Join(Root, Key.Rel() + ".none");
		if (Files::Exists(P))
		{
			if (InDisk != nullptr) *InDisk = R == 0;
			return P;
		}
	}
	return std::string();
}

std::shared_ptr<const HeightTile> HeightStore::Tile(const TileKey& Key)
{
	{
		std::lock_guard<std::mutex> G(Lock);
		auto It = Mem.find(Key);
		if (It != Mem.end())
		{
			It->second.Used = ++Tick;
			return It->second.Tile;
		}
		if (Missing.count(Key) != 0) return nullptr;
	}
	bool InDisk = false;
	const std::string Path = PathOf(Key, &InDisk);
	std::string Bytes;
	std::shared_ptr<const HeightTile> T;
	// (the game's own tiles, under Dirs, were looked over when they were chosen: only those fetched have pits filled)
	if (!Path.empty() && Files::Read(Path, Bytes) && !Bytes.empty()) T = Decode(Bytes, &Key, InDisk);
	std::lock_guard<std::mutex> G(Lock);
	if (T == nullptr)
	{
		Missing.insert(Key);
		return nullptr;
	}
	auto It = Mem.find(Key);
	if (It != Mem.end()) return It->second.Tile;   // (another thread got there first)
	Held H;
	H.Tile = T;
	H.Used = ++Tick;
	Mem[Key] = H;
	if (int(Mem.size()) > Memory)
	{
		auto Oldest = Mem.begin();
		for (auto M = Mem.begin(); M != Mem.end(); ++M)
			if (M->second.Used < Oldest->second.Used) Oldest = M;
		Mem.erase(Oldest);
	}
	return T;
}

bool HeightStore::Has(const TileKey& Key)
{
	bool Known = false, Gone = false;
	{
		std::lock_guard<std::mutex> G(Lock);
		Known = Mem.count(Key) != 0;
		Gone = Missing.count(Key) != 0;
	}
	if (Known || IsNone(Key)) return true;
	if (Gone) return false;
	if (!PathOf(Key, nullptr).empty()) return true;
	std::lock_guard<std::mutex> G(Lock);
	Missing.insert(Key);   // (until Store brings it)
	return false;
}

bool HeightStore::IsNone(const TileKey& Key)
{
	{
		std::lock_guard<std::mutex> G(Lock);
		auto It = None.find(Key);
		if (It != None.end()) return It->second;
	}
	bool InDisk = false;
	const std::string P = NonePath(Key, &InDisk);
	// (only those fetched go stale: one among the game's own or the tests' tiles stands)
	const bool Yes = !P.empty() && (!InDisk || double(std::time(nullptr)) - double(Files::ModifiedUnix(P)) < NoneDays * 86400.0);
	std::lock_guard<std::mutex> G(Lock);
	None[Key] = Yes;
	return Yes;
}

void HeightStore::MarkNone(const TileKey& Key)
{
	if (!Disk.empty()) Files::Write(Files::Join(Disk, Key.Rel() + ".none"), std::string());
	std::lock_guard<std::mutex> G(Lock);
	None[Key] = true;
	Missing.erase(Key);
}

bool HeightStore::Store(const TileKey& Key, const std::string& Bytes, bool FromAws)
{
	if (Disk.empty() || Decode(Bytes, nullptr, false) == nullptr) return false;
	if (!Files::Write(Files::Join(Disk, Key.Rel() + (FromAws ? ".png" : ".webp")), Bytes)) return false;
	std::lock_guard<std::mutex> G(Lock);
	Missing.erase(Key);
	return true;
}

std::shared_ptr<HeightTile> HeightStore::Decode(const std::string& Bytes, const TileKey* Key, bool Despike) const
{
	Picture Pic;
	if (!DecodePicture(reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size(), Pic) || Pic.Width != Pic.Height || Pic.Width < 2) return nullptr;
	std::shared_ptr<HeightTile> T = std::make_shared<HeightTile>();
	T->Size = Pic.Width;
	T->Rgb.swap(Pic.Rgb);
	if (Despike) FillPits(*T);
	T->Sea = SeaMask(*T, Key);
	return T;
}

// Fills the tile's pits: pixels more than SpikeM under the middle of the eight round them, which the
// sources have here and there (one in Rio drops 706 m below the street round it). Each is given that
// middle height. Real hollows and cuttings are wider than a pixel, so they stay.
void HeightStore::FillPits(HeightTile& T)
{
	const int N = T.Size;
	const int Deep = int(SpikeM * 256.0);
	std::vector<uint8_t>& Rgb = T.Rgb;
	std::vector<int> Found;
	// (a first look along each row only, cheap: a pit is well under one of the pixels beside it)
	for (int J = 1; J < N - 1; ++J)
	{
		const int Row = J * N;
		int Left = Raw24(&Rgb[size_t(Row) * 3]);
		int V = Raw24(&Rgb[size_t(Row + 1) * 3]);
		for (int I = 1; I < N - 1; ++I)
		{
			const int Right = Raw24(&Rgb[size_t(Row + I + 1) * 3]);
			if (V < Left - Deep || V < Right - Deep) Found.push_back(Row + I);
			Left = V;
			V = Right;
		}
	}
	if (Found.empty()) return;
	std::vector<int> Fixed(Found.size(), -1);
	const int Round[8] = {-N - 1, -N, -N + 1, -1, 1, N - 1, N, N + 1};
	for (size_t F = 0; F < Found.size(); ++F)
	{
		int Near[8];
		for (int K = 0; K < 8; ++K) Near[K] = Raw24(&Rgb[size_t(Found[F] + Round[K]) * 3]);
		std::sort(Near, Near + 8);
		const int Mid = (Near[3] + Near[4]) / 2;
		if (Raw24(&Rgb[size_t(Found[F]) * 3]) < Mid - Deep) Fixed[F] = Mid;
	}
	for (size_t F = 0; F < Found.size(); ++F)
	{
		if (Fixed[F] < 0) continue;
		uint8_t* P = &Rgb[size_t(Found[F]) * 3];
		P[0] = uint8_t(Fixed[F] >> 16);
		P[1] = uint8_t((Fixed[F] >> 8) & 255);
		P[2] = uint8_t(Fixed[F] & 255);
	}
}

namespace
{
// The Godot engine's own picture resizing (core/io/image.cpp, one channel of bytes), step for step:
// the sea's floor is shaped by it, so it is copied here for both games to give the sea the same bed.
void ScaleBilinear(const std::vector<uint8_t>& Src, int SrcSize, std::vector<uint8_t>& Dst, int DstSize)
{
	const uint32_t FracBits = 8, FracLen = 1u << FracBits, FracHalf = FracLen >> 1, FracMask = FracLen - 1;
	const uint32_t Sn = uint32_t(SrcSize), Dn = uint32_t(DstSize);
	Dst.assign(size_t(Dn) * size_t(Dn), 0);
	std::vector<uint32_t> Lo(Dn), Hi(Dn), Frac(Dn);
	for (uint32_t I = 0; I < Dn; ++I)
	{
		// (the middle of each new pixel, among the old ones' middles)
		const uint32_t Fp = uint32_t((I + 0.5) * Sn * FracLen / Dn);
		Lo[I] = Fp >= FracHalf ? (Fp - FracHalf) >> FracBits : 0;
		Hi[I] = std::min((Fp + FracHalf) >> FracBits, Sn - 1);
		const uint32_t Fr = Fp & FracMask;
		Frac[I] = Fr >= FracHalf ? Fr - FracHalf : Fr + FracHalf;
	}
	for (uint32_t I = 0; I < Dn; ++I)
		for (uint32_t J = 0; J < Dn; ++J)
		{
			const uint32_t P00 = uint32_t(Src[size_t(Lo[I]) * Sn + Lo[J]]) << FracBits, P10 = uint32_t(Src[size_t(Lo[I]) * Sn + Hi[J]]) << FracBits;
			const uint32_t P01 = uint32_t(Src[size_t(Hi[I]) * Sn + Lo[J]]) << FracBits, P11 = uint32_t(Src[size_t(Hi[I]) * Sn + Hi[J]]) << FracBits;
			// (unsigned on purpose, wrapping as the engine's does)
			const uint32_t Up = P00 + (((P10 - P00) * Frac[J]) >> FracBits);
			const uint32_t Down = P01 + (((P11 - P01) * Frac[J]) >> FracBits);
			const uint32_t Mix = Up + (((Down - Up) * Frac[I]) >> FracBits);
			Dst[size_t(I) * Dn + J] = uint8_t(Mix >> FracBits);
		}
}

double CubicKernel(double X)
{
	X = std::fabs(X);
	if (X <= 1.0) return (1.5 * X - 2.5) * X * X + 1.0;
	if (X < 2.0) return ((-0.5 * X + 2.5) * X - 4.0) * X + 2.0;
	return 0.0;
}

void ScaleCubic(const std::vector<uint8_t>& Src, int SrcSize, std::vector<uint8_t>& Dst, int DstSize)
{
	const double Fac = double(SrcSize) / DstSize;
	const int Max = SrcSize - 1;
	Dst.assign(size_t(DstSize) * size_t(DstSize), 0);
	// (the same along a row as down a column: each new pixel's four old ones and their weights, worked out once)
	std::vector<int> At(size_t(DstSize) * 4);
	std::vector<double> Wy(size_t(DstSize) * 4), Wx(size_t(DstSize) * 4);
	for (int I = 0; I < DstSize; ++I)
	{
		const double O = (I + 0.5) * Fac - 0.5;
		const int O1 = int(O);
		const double D = O - double(O1);
		for (int N = -1; N < 3; ++N)
		{
			At[size_t(I) * 4 + size_t(N + 1)] = std::min(Max, std::max(0, O1 + N));
			Wy[size_t(I) * 4 + size_t(N + 1)] = CubicKernel(D - double(N));
			Wx[size_t(I) * 4 + size_t(N + 1)] = CubicKernel(double(N) - D);
		}
	}
	for (int Y = 0; Y < DstSize; ++Y)
		for (int X = 0; X < DstSize; ++X)
		{
			double Colour = 0.0;
			for (size_t N = 0; N < 4; ++N)
			{
				const double K1 = Wy[size_t(Y) * 4 + N];
				const uint8_t* Row = &Src[size_t(At[size_t(Y) * 4 + N]) * size_t(SrcSize)];
				for (size_t M = 0; M < 4; ++M) Colour += Row[At[size_t(X) * 4 + M]] * (K1 * Wx[size_t(X) * 4 + M]);
			}
			const long V = std::lrintf(float(Colour));
			Dst[size_t(Y) * size_t(DstSize) + size_t(X)] = uint8_t(V < 0 ? 0 : (V > 255 ? 255 : V));
		}
}
} // namespace

// The tile's sea, smoothed so the floor slopes down from the shore: empty when it has none (or only
// a pond). Ground at or under SeaAbs is sea only in a cell that has sea (KnowSea); a pixel in one
// that hasn't is 0 (land), and one of the sea at least 1. The smoothing shrinks the mask to SeaCells
// a side and back.
std::vector<uint8_t> HeightStore::SeaMask(const HeightTile& T, const TileKey* Key) const
{
	const int S = T.Size;
	const size_t N = size_t(S) * size_t(S);
	const std::vector<uint8_t> Allowed = Key != nullptr && Key->Valid() ? SeaAllowed(S, *Key) : std::vector<uint8_t>();   // (empty: everywhere)
	if (Allowed.size() == 1) return std::vector<uint8_t>();   // (no cell under it has sea)
	std::vector<uint8_t> Mask(N, 0);
	long long Count = 0;
	const int Limit = int((SeaAbs + 32768.0) * 256.0);
	for (size_t P = 0; P < N; ++P)
		if (Raw24(&T.Rgb[P * 3]) <= Limit && (Allowed.empty() || Allowed[P] != 0))
		{
			Mask[P] = 255;
			++Count;
		}
	if (Count <= (long long)(SeaMin) * S * S / (512 * 512)) return std::vector<uint8_t>();
	std::vector<uint8_t> Small, Out;
	ScaleBilinear(Mask, S, Small, SeaCells);
	ScaleCubic(Small, SeaCells, Out, S);
	for (size_t P = 0; P < N; ++P)
	{
		if (Mask[P] != 0) Out[P] = std::max<uint8_t>(Out[P], 1);
		else if (!Allowed.empty() && Allowed[P] == 0) Out[P] = 0;   // (the smoothing reaches over the cell's edge: none of it on the land)
	}
	return Out;
}

// Which pixels of a tile lie in a cell that may have sea: empty when they all may, a single 0 when
// none may, else Size x Size of 0 or 1.
std::vector<uint8_t> HeightStore::SeaAllowed(int Size, const TileKey& Key) const
{
	std::map<CellId, bool> Known;
	{
		std::lock_guard<std::mutex> G(Lock);
		Known = SeaCellsKnown;
	}
	if (Known.empty()) return std::vector<uint8_t>();
	auto May = [&Known](int R, int C) {
		auto It = Known.find(CellId{R, C});
		return It == Known.end() ? true : It->second;
	};
	const double Scale = double(1 << Key.Zoom);
	std::vector<int> Rows(size_t(Size), 0), Cols(size_t(Size), 0);
	for (int J = 0; J < Size; ++J) Rows[size_t(J)] = int(std::floor((LatOfRow(Key.Y + (J + 0.5) / Size, Key.Zoom) + 90.0) / CellDeg));
	for (int I = 0; I < Size; ++I) Cols[size_t(I)] = int(std::floor(((Key.X + (I + 0.5) / Size) / Scale * 360.0) / CellDeg));
	bool AnyYes = false, AnyNo = false;
	for (int R = Rows[size_t(Size - 1)]; R <= Rows[0]; ++R)
		for (int C = Cols[0]; C <= Cols[size_t(Size - 1)]; ++C) (May(R, C) ? AnyYes : AnyNo) = true;
	if (!AnyNo) return std::vector<uint8_t>();
	if (!AnyYes) return std::vector<uint8_t>(1, 0);
	std::vector<uint8_t> Out(size_t(Size) * size_t(Size), 0);
	for (int J = 0; J < Size; ++J)
		for (int I = 0; I < Size; ++I) Out[size_t(J) * size_t(Size) + size_t(I)] = May(Rows[size_t(J)], Cols[size_t(I)]) ? 1 : 0;
	return Out;
}

void HeightStore::KnowSea(CellId Cell, bool HasSea)
{
	std::lock_guard<std::mutex> G(Lock);
	auto It = SeaCellsKnown.find(Cell);
	const bool Was = It != SeaCellsKnown.end();
	const bool WasSea = Was && It->second;
	SeaCellsKnown[Cell] = HasSea;
	if ((Was && WasSea == HasSea) || (!Was && HasSea)) return;   // (no tile read so far is any different)
	const double S = CellDeg;
	for (auto M = Mem.begin(); M != Mem.end();)
	{
		const GeoBox B = TileBox(M->first);
		if (B.North > Cell.Row * S - 90.0 && B.South < (Cell.Row + 1) * S - 90.0 && B.East > Cell.Col * S - 180.0 && B.West < (Cell.Col + 1) * S - 180.0)
			M = Mem.erase(M);
		else ++M;
	}
}

int HeightStore::SeaIn(CellId Cell) const
{
	std::lock_guard<std::mutex> G(Lock);
	auto It = SeaCellsKnown.find(Cell);
	return It == SeaCellsKnown.end() ? -1 : (It->second ? 1 : 0);
}

std::vector<TileKey> HeightStore::KeysNear(const std::vector<P2>& Points, double Reach, int Zoom, const Frame& F)
{
	std::set<TileKey> Found;
	for (const P2& P : Points)
	{
		const int X0 = int(std::floor(MercatorX(F.Lon0 + (P.X - Reach) / F.M.Lon, Zoom)));
		const int X1 = int(std::floor(MercatorX(F.Lon0 + (P.X + Reach) / F.M.Lon, Zoom)));
		const int Y0 = int(std::floor(MercatorY(F.Lat0 - (P.Z - Reach) / F.M.Lat, Zoom)));
		const int Y1 = int(std::floor(MercatorY(F.Lat0 - (P.Z + Reach) / F.M.Lat, Zoom)));
		for (int Ty = Y0; Ty <= Y1; ++Ty)
			for (int Tx = X0; Tx <= X1; ++Tx) Found.insert(TileKey{Zoom, Tx, Ty});
	}
	return std::vector<TileKey>(Found.begin(), Found.end());
}

void HeightStore::Reset()
{
	std::lock_guard<std::mutex> G(Lock);
	Mem.clear();
	Missing.clear();
	None.clear();
	SeaCellsKnown.clear();
}
} // namespace TorqueWorld
