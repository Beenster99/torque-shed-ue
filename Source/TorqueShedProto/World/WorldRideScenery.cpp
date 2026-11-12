// What stands on a ride's land (Ride UK stage 3: the look of the hand-built ride, made as you go):
// the ground's colour by what covers it, dry stone walls, hedges and fences on their mapped lines,
// buildings on their mapped footprints, and trees where the map's canopy says there are trees.
//
// All of it is plain meshes with their colours in their vertices, made from nothing but the cells,
// so every rider has the same walls and the same trees. The trees are stand-in shapes until the
// scanned ones are placed in their stead.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace
{
constexpr double SceneCell = 256.0;

long long SceneKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / SceneCell)) * 4000037LL + static_cast<long long>(std::floor(Z / SceneCell));
}

// A number from 0 to 1 that is always the same for the same place (and Salt).
double Chance(double X, double Z, int Salt)
{
	uint32_t H = static_cast<uint32_t>(static_cast<int32_t>(std::floor(X * 8.0))) * 73856093u ^ static_cast<uint32_t>(static_cast<int32_t>(std::floor(Z * 8.0))) * 19349663u
		^ static_cast<uint32_t>(Salt) * 83492791u;
	H ^= H >> 13;
	H *= 0x5bd1e995u;
	H ^= H >> 15;
	return double(H & 0xffffffu) / double(0x1000000);
}

struct P3
{
	double X = 0.0, Y = 0.0, Z = 0.0;
};

P3 At3(double X, double Y, double Z)
{
	P3 Q;
	Q.X = X; Q.Y = Y; Q.Z = Z;
	return Q;
}

struct Rgb
{
	float R = 0.0f, G = 0.0f, B = 0.0f;
};

Rgb Shade(const Rgb& C, double By)
{
	Rgb Out;
	Out.R = float(C.R * By); Out.G = float(C.G * By); Out.B = float(C.B * By);
	return Out;
}

// Four points as a quad that faces Toward (or both ways), flat shaded. Its UVs are metres of its own
// surface (across and up a wall, east and south on a flat top); Moss, 0 or more, is its vertex alpha.
void Quad(RideMesh& Out, const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, const Rgb& Colour, bool BothSides = false, float Moss = -1.0f)
{
	const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
	double Nx = Ay * Bz - Az * By, Ny = Az * Bx - Ax * Bz, Nz = Ax * By - Ay * Bx;
	const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	if (Len < 1e-9) return;
	const bool Flip = Nx * (Toward.X - Q0.X) + Ny * (Toward.Y - Q0.Y) + Nz * (Toward.Z - Q0.Z) < 0.0;
	const double Sign = (Flip ? -1.0 : 1.0) / Len;
	Nx *= Sign; Ny *= Sign; Nz *= Sign;
	const uint32_t V = uint32_t(Out.Vertices());
	const bool Flat = std::fabs(Ny) > 0.7;
	const double Hl = std::sqrt(Nx * Nx + Nz * Nz);
	const double Tx = Hl > 1e-6 ? -Nz / Hl : 1.0, Tz = Hl > 1e-6 ? Nx / Hl : 0.0;   // (along a wall's face, level)
	for (const P3* Q : {&Q0, &Q1, &Q2, &Q3})
	{
		Out.Pos.push_back(float(Q->X - Out.OriginX)); Out.Pos.push_back(float(Q->Y)); Out.Pos.push_back(float(Q->Z - Out.OriginZ));
		Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
		// (kept small: a float's metres of UV stay sharp near the square, not 100 km from the cell's corner)
		const double Lx = Q->X - Out.OriginX, Lz = Q->Z - Out.OriginZ;
		Out.Uv.push_back(float(Flat ? Lx : Lx * Tx + Lz * Tz)); Out.Uv.push_back(float(Flat ? Lz : -Q->Y));
		Out.Col.push_back(Colour.R); Out.Col.push_back(Colour.G); Out.Col.push_back(Colour.B);
		if (Moss >= 0.0f) Out.Alpha.push_back(Moss);
	}
	if (!Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
	if (Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
}

// The ground's colours (linear), by the baker's cover ids: grass, forest, crop, shrub, barren, urban,
// sand, wetland, water, rock. Pasture is the green of the Dales' valley floors and the moor (shrub)
// the dun of their tops, as the hand-built ride has them.
const Rgb CoverColour[10] = {
	{0.105f, 0.150f, 0.045f},   // grass: grazed pasture
	{0.040f, 0.060f, 0.025f},   // forest: the floor under the trees
	{0.150f, 0.160f, 0.055f},   // crop: meadow and ploughland
	{0.095f, 0.090f, 0.045f},   // shrub: moor grass and heather
	{0.160f, 0.150f, 0.130f},   // barren
	{0.120f, 0.125f, 0.095f},   // urban: yards and gardens
	{0.380f, 0.330f, 0.230f},   // sand
	{0.060f, 0.075f, 0.040f},   // wetland
	{0.030f, 0.045f, 0.050f},   // water
	{0.210f, 0.205f, 0.190f},   // rock
};
const Rgb NoCover = {0.088f, 0.105f, 0.04f};
const Rgb Scar = {0.2f, 0.195f, 0.18f};   // (limestone showing on the steepest ground)
} // namespace

void RidePlan::LandColour(double X, double Z, double Spread, double Slope, float* Out) const
{
	// What covers the ground here and a little way off each way, so one kind runs into the next.
	double R = 0.0, G = 0.0, B = 0.0;
	const double Offs[5][2] = {{0.0, 0.0}, {Spread, 0.0}, {-Spread, 0.0}, {0.0, Spread}, {0.0, -Spread}};
	for (const double* Off : Offs)
	{
		const int Id = Land ? Land->Cover(X + Off[0], Z + Off[1]) : -1;
		const Rgb& C = Id >= 0 && Id < 10 ? CoverColour[Id] : NoCover;
		const double W = Off[0] == 0.0 && Off[1] == 0.0 ? 2.0 : 1.0;
		R += C.R * W; G += C.G * W; B += C.B * W;
	}
	R /= 6.0; G /= 6.0; B /= 6.0;
	// Steep ground is thin turf, then bare stone.
	const double Bare = std::fmin(1.0, std::fmax(0.0, (Slope - 0.7) / 0.5));
	R += (Scar.R - R) * Bare; G += (Scar.G - G) * Bare; B += (Scar.B - B) * Bare;
	Out[0] = float(R); Out[1] = float(G); Out[2] = float(B);
}

void RidePlan::IndexScenery()
{
	Scene = std::make_shared<SceneIndex>();
	const Region& G = Area.Roads;
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& F = G.Scenery.Lines[L];
		const bool Wall = F.Kind == "wall" || F.Kind == "retaining_wall", Hedge = F.Kind == "hedge", Fence = F.Kind == "fence";
		if (!(Wall || Hedge || Fence) || F.Line.size() < 2) continue;
		long long Last = -1;
		bool Any = false;
		for (size_t I = 0; I + 1 < F.Line.size(); ++I)
		{
			const P2 Mid = (F.Line[I] + F.Line[I + 1]) * 0.5;
			const long long Key = SceneKey(Mid.X, Mid.Z);
			if (Any && Key == Last) continue;
			std::vector<int>& Row = Scene->Lines[Key];
			if (std::find(Row.begin(), Row.end(), int(L)) == Row.end()) Row.push_back(int(L));
			Last = Key;
			Any = true;
		}
	}
	for (size_t B = 0; B < G.Buildings.size(); ++B) Scene->Buildings[SceneKey(G.Buildings[B].X, G.Buildings[B].Z)].push_back(int(B));
}

void RidePlan::SceneryTile(double X0, double Z0, double Size, double Coarse, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	const bool Fine = Coarse <= 4.5;
	const auto Inside = [X0, Z0, Size](double X, double Z) { return X >= X0 && X < X0 + Size && Z >= Z0 && Z < Z0 + Size; };
	// (clear of every road: a wall stops at the verge, and nothing stands on the tarmac)
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const P3 Sky = At3(X0, 1e6, Z0);
	// (the dry stone walls and the stone houses go in meshes of their own, in the scanned stone)
	std::shared_ptr<RideMesh> Walls = std::make_shared<RideMesh>(), Houses = std::make_shared<RideMesh>();
	Walls->OriginX = Houses->OriginX = X0;
	Walls->OriginZ = Houses->OriginZ = Z0;
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));

	// Walls, hedges and fences: each mapped line, a bit at a time over the ground.
	static const Rgb Limestone = {0.235f, 0.225f, 0.205f}, HedgeGreen = {0.035f, 0.065f, 0.02f}, Post = {0.11f, 0.085f, 0.06f};
	const double Step = Fine ? 4.0 : 12.0;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Lines.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Lines.end()) continue;
			for (int L : It->second)
			{
				const FeatureLine& F = G.Scenery.Lines[size_t(L)];
				const bool Hedge = F.Kind == "hedge", Fence = F.Kind == "fence";
				// (a dry stone wall: 1.3 m, wider at the foot; a hedge: taller and thick; a fence: posts and a rail)
				const double High = Hedge ? 1.7 : Fence ? 1.1 : (F.Kind == "retaining_wall" ? 1.0 : 1.3) * (0.92 + 0.16 * Chance(F.Line[0].X, F.Line[0].Z, 7));
				const double Foot = Hedge ? 0.7 : Fence ? 0.05 : 0.33, Top = Hedge ? 0.55 : Fence ? 0.05 : 0.22;
				const Rgb Colour = Shade(Hedge ? HedgeGreen : Fence ? Post : Limestone, 0.88 + 0.24 * Chance(F.Line[0].X, F.Line[0].Z, 3));
				// (a wall's stone: how bright, which scan, how lichened, as the hand-built ride's walls are)
				const bool IsWall = !Hedge && !Fence;
				Rgb Mask;
				Mask.R = float(0.42 + 0.16 * Chance(F.Line[0].X, F.Line[0].Z, 3));
				Mask.G = Chance(F.Line[0].X, F.Line[0].Z, 8) < 0.5 ? 0.15f : 0.85f;
				Mask.B = float(0.2 + 0.5 * Chance(F.Line[0].X, F.Line[0].Z, 9));
				RideMesh& Into = IsWall ? *Walls : Out;
				for (size_t I = 0; I + 1 < F.Line.size(); ++I)
				{
					const P2 A = F.Line[I], B = F.Line[I + 1];
					const P2 Mid = (A + B) * 0.5;
					// (each bit of a line is its middle's square's, and the finer square's if both would draw it)
					if (SceneKey(Mid.X, Mid.Z) != It->first || !Inside(Mid.X, Mid.Z)) continue;
					const double Len = A.DistanceTo(B);
					if (Len < 0.3) continue;
					const int Bits = std::max(1, int(std::ceil(Len / Step)));
					const P2 Dir = (B - A) * (1.0 / Len);
					const P2 Side(Dir.Z, -Dir.X);
					for (int K = 0; K < Bits; ++K)
					{
						const P2 Pa = A + (B - A) * (double(K) / Bits), Pb = A + (B - A) * (double(K + 1) / Bits);
						const P2 Pm = (Pa + Pb) * 0.5;
						const double Spare = Hedge ? 1.6 : 1.0;
						if (!OffRoad(Pm.X, Pm.Z, Spare) || !OffRoad(Pa.X, Pa.Z, Spare) || !OffRoad(Pb.X, Pb.Z, Spare)) continue;
						const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
						if (std::isnan(Ga) || std::isnan(Gb)) continue;
						if (Fence)
						{
							// A post at the bit's start and a rail to the next.
							const P2 Along = Dir * 0.05, Across = Side * 0.05;
							const P3 Out3 = At3(Pa.X + Side.X * 5.0, Ga + 0.5, Pa.Z + Side.Z * 5.0);
							Quad(Out, At3(Pa.X - Along.X, Ga - 0.2, Pa.Z - Along.Z), At3(Pa.X + Along.X, Ga - 0.2, Pa.Z + Along.Z), At3(Pa.X + Along.X, Ga + High, Pa.Z + Along.Z),
								At3(Pa.X - Along.X, Ga + High, Pa.Z - Along.Z), Out3, Colour, true);
							Quad(Out, At3(Pa.X - Across.X, Ga - 0.2, Pa.Z - Across.Z), At3(Pa.X + Across.X, Ga - 0.2, Pa.Z + Across.Z), At3(Pa.X + Across.X, Ga + High, Pa.Z + Across.Z),
								At3(Pa.X - Across.X, Ga + High, Pa.Z - Across.Z), Out3, Colour, true);
							if (Fine)
								Quad(Out, At3(Pa.X, Ga + High - 0.22, Pa.Z), At3(Pb.X, Gb + High - 0.22, Pb.Z), At3(Pb.X, Gb + High - 0.12, Pb.Z), At3(Pa.X, Ga + High - 0.12, Pa.Z), Out3, Colour, true);
							continue;
						}
						for (double Sg : {1.0, -1.0})
						{
							const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 1.0, Pm.Z + Side.Z * Sg * 50.0);
							Quad(Into, At3(Pa.X + Side.X * Sg * Foot, Ga - 0.4, Pa.Z + Side.Z * Sg * Foot), At3(Pb.X + Side.X * Sg * Foot, Gb - 0.4, Pb.Z + Side.Z * Sg * Foot),
								At3(Pb.X + Side.X * Sg * Top, Gb + High, Pb.Z + Side.Z * Sg * Top), At3(Pa.X + Side.X * Sg * Top, Ga + High, Pa.Z + Side.Z * Sg * Top), Far,
								IsWall ? Mask : Shade(Colour, Sg > 0.0 ? 1.0 : 0.9), false, IsWall ? 0.22f : -1.0f);
						}
						Quad(Into, At3(Pa.X + Side.X * Top, Ga + High, Pa.Z + Side.Z * Top), At3(Pb.X + Side.X * Top, Gb + High, Pb.Z + Side.Z * Top),
							At3(Pb.X - Side.X * Top, Gb + High, Pb.Z - Side.Z * Top), At3(Pa.X - Side.X * Top, Ga + High, Pa.Z - Side.Z * Top), Sky, IsWall ? Mask : Shade(Colour, 1.12), false, IsWall ? 0.45f : -1.0f);
					}
				}
			}
		}

	// Buildings: each on its mapped footprint, stone under a pitched roof, its ridge along its length.
	static const Rgb Stone = {0.30f, 0.28f, 0.245f}, Render = {0.55f, 0.53f, 0.47f}, Slate = {0.085f, 0.085f, 0.095f}, StoneSlate = {0.17f, 0.155f, 0.13f}, Glass = {0.02f, 0.025f, 0.03f},
		DoorWood = {0.07f, 0.045f, 0.03f};
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Buildings.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Buildings.end()) continue;
			for (int Index : It->second)
			{
				const Building& B = G.Buildings[size_t(Index)];
				if (!Inside(B.X, B.Z) || B.W < 2.0 || B.D < 2.0 || !OffRoad(B.X, B.Z, std::fmin(B.W, B.D) * 0.5)) continue;
				const double Base = Ground(B.X, B.Z);
				if (std::isnan(Base)) continue;
				// (local to the building: x across, z deep; the turn is the one the land's levelled pad uses)
				const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
				const bool LongX = B.W >= B.D;
				const double HalfLong = (LongX ? B.W : B.D) * 0.5, HalfShort = (LongX ? B.D : B.W) * 0.5;
				const auto Local = [&](double Along, double Across, double Y)
				{
					const double Lx = LongX ? Along : Across, Lz = LongX ? Across : Along;
					return At3(B.X + Lx * Cs + Lz * Sn, Base + Y, B.Z - Lx * Sn + Lz * Cs);
				};
				const bool Big = HalfShort > 6.0;   // (a barn or a shed: a low roof)
				const double Rise = Big ? HalfShort * 0.35 : std::fmin(HalfShort * 0.85, 3.6);
				const double Whole = B.Height > 2.0 ? B.Height : (Big ? 6.5 : 4.0 + 3.0 * Chance(B.X, B.Z, 1));
				const double Eaves = std::fmax(2.3, Whole - Rise);
				const double Pick = Chance(B.X, B.Z, 2);
				const bool Rendered = Pick < 0.14 && !Big;
				const Rgb Wall = Shade(Rendered ? Render : Stone, 0.85 + 0.3 * Chance(B.X, B.Z, 4));
				// (a stone house's walls in the scanned masonry: brightness, mostly the bare scan, a little lichen)
				Rgb Mask;
				Mask.R = float(0.48 + 0.14 * Chance(B.X, B.Z, 4));
				Mask.G = 0.8f;
				Mask.B = 0.3f;
				RideMesh& Walling = Rendered ? Out : *Houses;
				const auto WallQuad = [&](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, double By)
				{
					if (Rendered) Quad(Out, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By));
					else Quad(Walling, Q0, Q1, Q2, Q3, Toward, Mask, false, 0.06f);
				};
				const Rgb Roof = Shade(Pick > 0.55 ? StoneSlate : Slate, 0.9 + 0.2 * Chance(B.X, B.Z, 5));
				const P3 Centre = Local(0.0, 0.0, Eaves * 0.5);
				// (none of it over a road: the map's footprints and the road's own line don't always agree)
				bool Clear = true;
				for (double Su : {-1.0, 0.0, 1.0})
					for (double Sv : {-1.0, 1.0})
					{
						const P3 Corner = Local(Su * HalfLong, Sv * HalfShort, 0.0);
						Clear = Clear && OffRoad(Corner.X, Corner.Z, 0.6);
					}
				if (!Clear) continue;
				const auto Outside = [&Centre](const P3& A, const P3& C)
				{
					// (a point well outside the face whose opposite corners are A and C)
					return At3(Centre.X + ((A.X + C.X) * 0.5 - Centre.X) * 50.0, Centre.Y, Centre.Z + ((A.Z + C.Z) * 0.5 - Centre.Z) * 50.0);
				};
				const double Sunk = -1.0;
				for (double Sg : {1.0, -1.0})
				{
					// The long walls, and the ends with their gables.
					const P3 L0 = Local(-HalfLong, Sg * HalfShort, Sunk), L1 = Local(HalfLong, Sg * HalfShort, Sunk), L2 = Local(HalfLong, Sg * HalfShort, Eaves), L3 = Local(-HalfLong, Sg * HalfShort, Eaves);
					WallQuad(L0, L1, L2, L3, Outside(L0, L2), 1.0);
					const P3 E0 = Local(Sg * HalfLong, -HalfShort, Sunk), E1 = Local(Sg * HalfLong, HalfShort, Sunk), E2 = Local(Sg * HalfLong, HalfShort, Eaves), E3 = Local(Sg * HalfLong, -HalfShort, Eaves);
					WallQuad(E0, E1, E2, E3, Outside(E0, E2), 0.93);
					const P3 Peak = Local(Sg * HalfLong, 0.0, Eaves + Rise);
					WallQuad(E3, E2, Peak, E3, Outside(E0, E2), 0.93);
					// The roof's two slopes, a little over the walls.
					const double Over = 0.3, Drop = Over * Rise / HalfShort;
					Quad(Out, Local(-HalfLong - Over, Sg * (HalfShort + Over), Eaves - Drop), Local(HalfLong + Over, Sg * (HalfShort + Over), Eaves - Drop), Local(HalfLong + Over, 0.0, Eaves + Rise),
						Local(-HalfLong - Over, 0.0, Eaves + Rise), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
					if (!Fine || Big) continue;
					// Windows along the long walls, and a door in the one that faces +.
					const int Floors = Eaves > 4.8 ? 2 : 1, Along = std::max(1, int(HalfLong * 2.0 / 3.5));
					for (int Fl = 0; Fl < Floors; ++Fl)
						for (int Wn = 0; Wn < Along; ++Wn)
						{
							const double U = -HalfLong + (Wn + 0.5) * (HalfLong * 2.0 / Along), Y = 1.0 + Fl * 2.7;
							const bool Door = Sg > 0.0 && Fl == 0 && Wn == Along / 2;
							const double Wide = Door ? 0.5 : 0.55, Lo = Door ? 0.0 : Y, Hi = Door ? 2.05 : Y + 1.2, Proud = Sg * (HalfShort + 0.04);
							const P3 W0 = Local(U - Wide, Proud, Lo), W1 = Local(U + Wide, Proud, Lo), W2 = Local(U + Wide, Proud, Hi), W3 = Local(U - Wide, Proud, Hi);
							Quad(Out, W0, W1, W2, W3, Outside(L0, L2), Door ? DoorWood : Glass);
						}
				}
			}
		}

	// Trees, where the map's canopy says there are some: stand-in shapes on a scattered grid, as tall
	// as the canopy, broadleaved (round) or needleleaved (pointed).
	if (Land->HasTrees() && !RealTrees)
	{
		static const Rgb Bark = {0.06f, 0.045f, 0.03f}, Leaf = {0.045f, 0.085f, 0.025f}, Needle = {0.02f, 0.05f, 0.025f};
		const double Grid = Fine ? 9.0 : 22.0;
		const int Gx0 = int(std::ceil(X0 / Grid)), Gx1 = int(std::ceil((X0 + Size) / Grid)), Gz0 = int(std::ceil(Z0 / Grid)), Gz1 = int(std::ceil((Z0 + Size) / Grid));
		for (int Gz = Gz0; Gz < Gz1; ++Gz)
			for (int Gx = Gx0; Gx < Gx1; ++Gx)
			{
				const double Jx = Gx * Grid, Jz = Gz * Grid;
				const double X = Jx + (Chance(Jx, Jz, 11) - 0.5) * Grid * 0.8, Z = Jz + (Chance(Jx, Jz, 12) - 0.5) * Grid * 0.8;
				if (!Inside(X, Z)) continue;
				const int Trees = Land->TreesAt(X, Z);
				if (Trees <= 0) continue;
				const double Tall = double(Trees & 31) * 2.0, Share = double((Trees >> 5) & 7) / 8.0;
				if (Tall < 3.0 || Chance(Jx, Jz, 13) > Share * 1.2 || !OffRoad(X, Z, 3.0)) continue;
				const double Base = Ground(X, Z);
				if (std::isnan(Base)) continue;
				const bool Pointed = ((Trees >> 8) & 3) == 2 || (((Trees >> 8) & 3) == 3 && Chance(Jx, Jz, 14) < 0.5);
				const double High = Tall * (0.8 + 0.35 * Chance(Jx, Jz, 15)) * (Fine ? 1.0 : 1.05);
				const double Wide = (Pointed ? High * 0.19 : High * 0.33) * (Fine ? 1.0 : 1.5), Trunk = High * (Pointed ? 0.18 : 0.3);
				const Rgb Green = Shade(Pointed ? Needle : Leaf, 0.75 + 0.5 * Chance(Jx, Jz, 16));
				const double Turn = Chance(Jx, Jz, 17) * 1.5;
				const int Faces = Fine ? 6 : 4;
				const auto Ring = [&](int K, double Radius, double Y) { const double A = Turn + 6.283185307179586 * K / Faces; return At3(X + std::cos(A) * Radius, Base + Y, Z + std::sin(A) * Radius); };
				const P3 Axis = At3(X, Base + High * 0.6, Z);
				const auto Away = [&Axis](const P3& A, const P3& C) { return At3(Axis.X + ((A.X + C.X) * 0.5 - Axis.X) * 50.0, (A.Y + C.Y) * 0.5, Axis.Z + ((A.Z + C.Z) * 0.5 - Axis.Z) * 50.0); };
				for (int K = 0; K < Faces; ++K)
				{
					if (Fine)
					{
						const P3 T0 = Ring(K, 0.035 * High, -0.3), T1 = Ring(K + 1, 0.035 * High, -0.3), T2 = Ring(K + 1, 0.025 * High, Trunk + 0.3), T3 = Ring(K, 0.025 * High, Trunk + 0.3);
						Quad(Out, T0, T1, T2, T3, Away(T0, T2), Bark);
					}
					const double Lit = 0.8 + 0.4 * (0.5 + 0.5 * std::cos(Turn + 6.283185307179586 * (K + 0.5) / Faces - 0.8));
					if (Pointed)
					{
						const P3 C0 = Ring(K, Wide, Trunk), C1 = Ring(K + 1, Wide, Trunk), Tip = At3(X, Base + High, Z);
						Quad(Out, C0, C1, Tip, C0, Away(C0, C1), Shade(Green, Lit));
					}
					else
					{
						// (a crown in three rings: narrow where it leaves the trunk, widest a little over half way, closed at the top)
						const double Mid = Trunk + (High - Trunk) * 0.45;
						const P3 A0 = Ring(K, Wide * 0.45, Trunk), A1 = Ring(K + 1, Wide * 0.45, Trunk), B0 = Ring(K, Wide, Mid), B1 = Ring(K + 1, Wide, Mid), Tip = At3(X, Base + High, Z);
						Quad(Out, A0, A1, B1, B0, Away(A0, B1), Shade(Green, Lit * 0.8));
						const P3 U0 = Ring(K, Wide * 0.62, Trunk + (High - Trunk) * 0.82), U1 = Ring(K + 1, Wide * 0.62, Trunk + (High - Trunk) * 0.82);
						Quad(Out, B0, B1, U1, U0, Away(B0, U1), Shade(Green, Lit));
						Quad(Out, U0, U1, Tip, U0, Sky, Shade(Green, Lit * 1.12));
					}
				}
			}
	}
	if (Walls->Vertices() > 0) Out.Stone = Walls;
	if (Houses->Vertices() > 0) Out.Masonry = Houses;
}
} // namespace TorqueWorld
