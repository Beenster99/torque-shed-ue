#include "WorldTerrain.h"

#include <algorithm>

namespace TorqueWorld
{
namespace
{
const double NaN = std::numeric_limits<double>::quiet_NaN();
inline int FloorInt(double V) { return int(std::floor(V)); }
inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }
inline double Smoothstep(double From, double To, double V)
{
	const double S = std::min(1.0, std::max(0.0, (V - From) / (To - From)));
	return S * S * (3.0 - 2.0 * S);
}
inline int PosMod(int A, int N) { const int R = A % N; return R < 0 ? R + N : R; }
inline int Clamp(int V, int Lo, int Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
} // namespace


Terrain::Terrain(HeightStore& InStore, double Lat0, double Lon0) : Store(InStore), F(Lat0, Lon0)
{
	for (int L = 0; L < LevelCount; ++L) Zooms[L] = ZoomFor(Lat0, LiveSpacing[L]);
}

double Terrain::Height(double X, double Z) const
{
	const double H = LiveLevel(0, F.Lat0 - Z / F.M.Lat, F.Lon0 + X / F.M.Lon, true);
	if (std::isnan(H)) return NaN;
	return Pads != nullptr && !Pads->Grid.empty() ? OnPads(X, Z, H) : H;
}

double Terrain::HeightAtLevel(int Level, double X, double Z) const
{
	return Level >= 0 && Level < LevelCount ? LiveLevel(Level, F.Lat0 - Z / F.M.Lat, F.Lon0 + X / F.M.Lon, false) : NaN;
}

bool Terrain::UsesKey(int Level, const TileKey& Key) const
{
	if (Uses.empty()) return Store.Has(Key);
	return Uses[size_t(Level)].count(Key) != 0;
}

bool Terrain::Ready(const Rect& Area, int Level) const
{
	if (Level < 0 || Level >= LevelCount) return false;
	const int Zm = Zooms[Level];
	const Geo A = F.ToGeo(Area.X0, Area.Z0), B = F.ToGeo(Area.X1, Area.Z1);
	for (int Ty = FloorInt(MercatorY(A.Lat, Zm)); Ty <= FloorInt(MercatorY(B.Lat, Zm)); ++Ty)
		for (int Tx = FloorInt(MercatorX(A.Lon, Zm)); Tx <= FloorInt(MercatorX(B.Lon, Zm)); ++Tx)
		{
			const TileKey Key{Zm, Tx, Ty};
			if (!UsesKey(Level, Key) || !Store.Has(Key)) return false;
		}
	return true;
}

bool Terrain::SeaAt(double X, double Z) const
{
	const Geo G = F.ToGeo(X, Z);
	for (int L = 0; L < LevelCount; ++L)
	{
		const int Zm = Zooms[L];
		const double Mx = MercatorX(G.Lon, Zm), My = MercatorY(G.Lat, Zm);
		const TileKey Key{Zm, FloorInt(Mx), FloorInt(My)};
		const std::shared_ptr<const HeightTile> T = UsesKey(L, Key) ? Store.Tile(Key) : nullptr;
		if (T != nullptr) return T->IsSea(Clamp(int((Mx - Key.X) * T->Size), 0, T->Size - 1), Clamp(int((My - Key.Y) * T->Size), 0, T->Size - 1));
	}
	return false;
}

Rect Terrain::KeyBox(const TileKey& Key) const
{
	const GeoBox B = TileBox(Key);
	const P2 Nw = F.FromGeo(B.North, B.West), Se = F.FromGeo(B.South, B.East);
	return Rect{Nw.X, Nw.Z, Se.X, Se.Z};
}

void Terrain::SetBuildings(const std::vector<Building>& Rows)
{
	std::shared_ptr<PadSet> Set = std::make_shared<PadSet>();
	Set->Rows = Rows;
	for (size_t I = 0; I < Rows.size(); ++I)
	{
		const Building& B = Rows[I];
		const double R = double(float(std::sqrt(B.W * B.W + B.D * B.D))) * 0.5 + PadMargin + PadFeather;
		for (int Gz = FloorInt((B.Z - R) / PadCell); Gz <= FloorInt((B.Z + R) / PadCell); ++Gz)
			for (int Gx = FloorInt((B.X - R) / PadCell); Gx <= FloorInt((B.X + R) / PadCell); ++Gx) Set->Grid[{Gx, Gz}].push_back(int(I));
	}
	std::lock_guard<std::mutex> G(Lock);
	Pads = Set;
	PadLevel.clear();
}

std::shared_ptr<Terrain> Terrain::ForRoad(const std::vector<P2>& Road) const
{
	std::shared_ptr<Terrain> T = std::make_shared<Terrain>(Store, F.Lat0, F.Lon0);
	T->Pads = Pads;   // (each ride works out its own pads' ground: it depends on the tiles the ride uses)
	for (int L = 0; L < LevelCount; ++L)
	{
		const std::vector<TileKey> Keys = HeightStore::KeysNear(Road, LiveReach[L], Zooms[L], F);
		T->Uses.emplace_back(Keys.begin(), Keys.end());
	}
	return T;
}

std::vector<TileKey> Terrain::UsedKeys() const
{
	std::vector<TileKey> Out;
	for (const std::set<TileKey>& S : Uses) Out.insert(Out.end(), S.begin(), S.end());
	return Out;
}

// A level's height at (Lat, Lon), eased into the next level's where its tiles end (when Ease), else the next's.
double Terrain::LiveLevel(int Level, double Lat, double Lon, bool Ease) const
{
	if (Level >= LevelCount) return NaN;
	const int Zm = Zooms[Level];
	const double Mx = MercatorX(Lon, Zm), My = MercatorY(Lat, Zm);
	const TileKey Key{Zm, FloorInt(Mx), FloorInt(My)};
	const std::shared_ptr<const HeightTile> T = UsesKey(Level, Key) ? Store.Tile(Key) : nullptr;
	if (T == nullptr) return LiveLevel(Level + 1, Lat, Lon, Ease);
	const double U = (Mx - Key.X) * T->Size, V = (My - Key.Y) * T->Size;
	const double H = Bilinear(Key, *T, U - 0.5, V - 0.5);
	if (!Ease || Level + 1 >= LevelCount) return H;
	const double W = EdgeWeight(Level, Key, U, V, T->Size);
	if (W <= 0.0) return H;
	const double Below = LiveLevel(Level + 1, Lat, Lon, true);
	return std::isnan(Below) ? H : Lerp(H, Below, W);
}

// Bilinear between the pixels round (Fx, Fy), reaching into the next tile past the edge (or holding
// the edge pixel where there is none).
double Terrain::Bilinear(const TileKey& Key, const HeightTile& T, double Fx, double Fy) const
{
	const int Ix = FloorInt(Fx), Iy = FloorInt(Fy);
	const double Tx = Fx - Ix, Ty = Fy - Iy;
	if (Ix >= 0 && Iy >= 0 && Ix + 1 < T.Size && Iy + 1 < T.Size)
		return Lerp(Lerp(T.At(Ix, Iy), T.At(Ix + 1, Iy), Tx), Lerp(T.At(Ix, Iy + 1), T.At(Ix + 1, Iy + 1), Tx), Ty);
	return Lerp(Lerp(Pixel(Key, T, Ix, Iy), Pixel(Key, T, Ix + 1, Iy), Tx), Lerp(Pixel(Key, T, Ix, Iy + 1), Pixel(Key, T, Ix + 1, Iy + 1), Tx), Ty);
}

double Terrain::Pixel(const TileKey& Key, const HeightTile& T, int I, int J) const
{
	const int Dx = FloorInt(double(I) / T.Size), Dy = FloorInt(double(J) / T.Size);
	if (Dx == 0 && Dy == 0) return T.At(I, J);
	const std::shared_ptr<const HeightTile> Other = Store.Tile(TileKey{Key.Zoom, Key.X + Dx, Key.Y + Dy});
	if (Other == nullptr || Other->Size != T.Size) return T.At(Clamp(I, 0, T.Size - 1), Clamp(J, 0, T.Size - 1));
	return Other->At(PosMod(I, T.Size), PosMod(J, T.Size));
}

// How much of the next level a sample at pixel (U, V) of a tile takes: 0 inside the ride's tiles,
// rising to 1 at an edge (or a corner) with no tile of the ride's beyond it, or one there is none
// of, so a fine tile next to a gap meets the coarser level there with no step.
double Terrain::EdgeWeight(int Level, const TileKey& Key, double U, double V, int Size) const
{
	const double Fe = EasePx * Size / 512.0;
	const bool NearW = U < Fe, NearE = Size - U < Fe, NearN = V < Fe, NearS = Size - V < Fe;
	if (!(NearW || NearE || NearN || NearS)) return 0.0;
	double W = 0.0;
	if (NearW && !HasGround(Level, TileKey{Key.Zoom, Key.X - 1, Key.Y})) W = std::max(W, 1.0 - Smoothstep(0.0, Fe, U));
	if (NearE && !HasGround(Level, TileKey{Key.Zoom, Key.X + 1, Key.Y})) W = std::max(W, 1.0 - Smoothstep(0.0, Fe, Size - U));
	if (NearN && !HasGround(Level, TileKey{Key.Zoom, Key.X, Key.Y - 1})) W = std::max(W, 1.0 - Smoothstep(0.0, Fe, V));
	if (NearS && !HasGround(Level, TileKey{Key.Zoom, Key.X, Key.Y + 1})) W = std::max(W, 1.0 - Smoothstep(0.0, Fe, Size - V));
	for (int C = 0; C < 4; ++C)
	{
		const int Dx = (C & 1) ? 1 : -1, Dy = (C & 2) ? 1 : -1;
		const double Cu = Dx < 0 ? U : Size - U, Cv = Dy < 0 ? V : Size - V;
		if (Cu < Fe && Cv < Fe && !HasGround(Level, TileKey{Key.Zoom, Key.X + Dx, Key.Y + Dy})) W = std::max(W, 1.0 - Smoothstep(0.0, Fe, std::max(Cu, Cv)));
	}
	return W;
}

// H at (X, Z) levelled under any building there, feathered into the land round it.
double Terrain::OnPads(double X, double Z, double H) const
{
	auto It = Pads->Grid.find({FloorInt(X / PadCell), FloorInt(Z / PadCell)});
	if (It == Pads->Grid.end()) return H;
	for (int I : It->second)
	{
		const Building& B = Pads->Rows[size_t(I)];
		const double C = std::cos(B.Yaw), S = std::sin(B.Yaw);
		const double Px = X - B.X, Pz = Z - B.Z;
		const double Lx = Px * C - Pz * S, Lz = Px * S + Pz * C;
		const double Ex = std::max(std::fabs(Lx) - B.W * 0.5 - PadMargin, 0.0), Ez = std::max(std::fabs(Lz) - B.D * 0.5 - PadMargin, 0.0);
		const double Dist = double(float(std::sqrt(double(float(Ex)) * double(float(Ex)) + double(float(Ez)) * double(float(Ez)))));
		if (Dist >= PadFeather) continue;
		const double Level = PadGround(I);
		if (std::isnan(Level)) continue;
		const double T = 1.0 - Dist / PadFeather;
		H += (Level - H) * T * T * (3.0 - 2.0 * T);
	}
	return H;
}

// A building's ground: the mean of the bare land over its footprint (and margin), every 4 m, worked out once.
double Terrain::PadGround(int Index) const
{
	{
		std::lock_guard<std::mutex> G(Lock);
		auto It = PadLevel.find(Index);
		if (It != PadLevel.end()) return It->second;
	}
	const Building& B = Pads->Rows[size_t(Index)];
	const double C = std::cos(B.Yaw), S = std::sin(B.Yaw);
	const double Hw = B.W * 0.5 + PadMargin, Hd = B.D * 0.5 + PadMargin;
	double Total = 0.0;
	int N = 0;
	for (double Lx = -Hw; Lx <= Hw; Lx += 4.0)
		for (double Lz = -Hd; Lz <= Hd; Lz += 4.0)
		{
			// (local to the building, back to the frame: the inverse of OnPads' turn)
			const double H = LiveLevel(0, F.Lat0 - (B.Z - Lx * S + Lz * C) / F.M.Lat, F.Lon0 + (B.X + Lx * C + Lz * S) / F.M.Lon, true);
			if (!std::isnan(H))
			{
				Total += H;
				++N;
			}
		}
	const double Level = N > 0 ? Total / N : NaN;
	std::lock_guard<std::mutex> G(Lock);
	PadLevel[Index] = Level;   // (two threads may both work it out: they get the same answer)
	return Level;
}
} // namespace TorqueWorld
