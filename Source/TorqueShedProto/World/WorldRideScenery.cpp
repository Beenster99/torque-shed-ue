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

// A flat disc of a sign facing Face (level, unit), its middle at C.
void Disc(RideMesh& Out, const P3& C, const P2& Face, double Radius, const Rgb& Colour, int Segments = 20)
{
	const uint32_t V = uint32_t(Out.Vertices());
	const P2 U(Face.Z, -Face.X);
	for (int K = -1; K < Segments; ++K)
	{
		const double A = 6.283185307179586 * K / Segments;
		const double X = K < 0 ? C.X : C.X + U.X * std::cos(A) * Radius, Y = K < 0 ? C.Y : C.Y + std::sin(A) * Radius, Z = K < 0 ? C.Z : C.Z + U.Z * std::cos(A) * Radius;
		Out.Pos.push_back(float(X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z - Out.OriginZ));
		Out.Nrm.push_back(float(Face.X)); Out.Nrm.push_back(0.0f); Out.Nrm.push_back(float(Face.Z));
		Out.Uv.push_back(float(X - Out.OriginX)); Out.Uv.push_back(float(-Y));
		Out.Col.push_back(Colour.R); Out.Col.push_back(Colour.G); Out.Col.push_back(Colour.B);
		if (!Out.Alpha.empty()) Out.Alpha.push_back(0.0f);
	}
	// (wound as Quad's are, to face the way (B - A) x (C - A) points: U x up is Face)
	for (int K = 0; K < Segments; ++K)
	{
		const uint32_t A = V + 1 + uint32_t(K), B = V + 1 + uint32_t((K + 1) % Segments);
		Out.Idx.insert(Out.Idx.end(), {V, A, B});
	}
}

// An upright box: its foot's middle at (X, Z), Along its length (unit), half Long by half Wide, from
// Lo to Hi over Base; its top too.
void Box(RideMesh& Out, double X, double Z, const P2& Along, double Long, double Wide, double Lo, double Hi, const Rgb& Colour)
{
	const P2 Across(Along.Z, -Along.X);
	const auto Corner = [&](double Su, double Sv, double Y) { return At3(X + Along.X * Su * Long + Across.X * Sv * Wide, Y, Z + Along.Z * Su * Long + Across.Z * Sv * Wide); };
	const P3 Mid = At3(X, (Lo + Hi) * 0.5, Z);
	const auto Away = [&Mid](const P3& A, const P3& C) { return At3(Mid.X + ((A.X + C.X) * 0.5 - Mid.X) * 50.0, Mid.Y, Mid.Z + ((A.Z + C.Z) * 0.5 - Mid.Z) * 50.0); };
	const double S[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
	for (int K = 0; K < 4; ++K)
	{
		const P3 A = Corner(S[K][0], S[K][1], Lo), B = Corner(S[K + 1][0], S[K + 1][1], Lo), C = Corner(S[K + 1][0], S[K + 1][1], Hi), D = Corner(S[K][0], S[K][1], Hi);
		Quad(Out, A, B, C, D, Away(A, C), Shade(Colour, K % 2 == 0 ? 1.0 : 0.85));
	}
	Quad(Out, Corner(-1, -1, Hi), Corner(1, -1, Hi), Corner(1, 1, Hi), Corner(-1, 1, Hi), At3(X, Hi + 100.0, Z), Shade(Colour, 1.1));
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

const LocalLook& RidePlan::LookHere(double X, double Z) const
{
	const Geo Here = Frame(Area.Roads.FrameLat, Area.Roads.FrameLon).ToGeo(X, Z);
	return LookAt(Here.Lat, Here.Lon);
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
	// The local look: the walls' and houses' stone, the houses' walling and roofs (Ride UK stage 5).
	const LocalLook& Look = LookHere(X0 + Size * 0.5, Z0 + Size * 0.5);
	Out.StoneTint = Look.WallStone;
	Out.MasonryTint = Look.HouseStone;
	const bool Banks = Look.Boundary == LocalLook::HedgeBank;
	std::shared_ptr<RideMesh> Washed = std::make_shared<RideMesh>();
	Washed->OriginX = X0;
	Washed->OriginZ = Z0;
	// (the dry stone walls and the stone houses go in meshes of their own, in the scanned stone)
	std::shared_ptr<RideMesh> Walls = std::make_shared<RideMesh>(), Houses = std::make_shared<RideMesh>();
	Walls->OriginX = Houses->OriginX = X0;
	Walls->OriginZ = Houses->OriginZ = Z0;
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));

	// Walls, hedges and fences: each mapped line, a bit at a time over the ground.
	static const Rgb Limestone = {0.235f, 0.225f, 0.205f}, HedgeGreen = {0.035f, 0.065f, 0.02f}, Post = {0.11f, 0.085f, 0.06f}, Turf = {0.07f, 0.095f, 0.035f};
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
				// (in the South West a mapped wall is a hedge bank: earth faced with turf, a hedge along its top)
				const bool Bank = Banks && !Fence;
				// (a dry stone wall: 1.3 m, wider at the foot; a hedge: taller and thick; a fence: posts and a rail)
				const double High = Hedge ? 1.7 : Fence ? 1.1 : (F.Kind == "retaining_wall" ? 1.0 : 1.3) * (0.92 + 0.16 * Chance(F.Line[0].X, F.Line[0].Z, 7));
				const double Foot = Hedge ? 0.7 : Fence ? 0.05 : 0.33, Top = Hedge ? 0.55 : Fence ? 0.05 : 0.22;
				const Rgb Colour = Shade(Hedge ? HedgeGreen : Fence ? Post : Limestone, 0.88 + 0.24 * Chance(F.Line[0].X, F.Line[0].Z, 3));
				// (a wall's stone: how bright, which scan, how lichened, as the hand-built ride's walls are)
				const bool IsWall = !Hedge && !Fence && !Bank;
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
						if (InGap(Pm.X, Pm.Z)) continue;   // (a field gate stands here)
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
						// A length of it: two faces leaning in from Foot to Top, Lo to Hi over the ground, and its top.
						const auto Stretch = [&](double FootW, double TopW, double Lo, double Hi, const Rgb& Col, bool Scanned)
						{
							for (double Sg : {1.0, -1.0})
							{
								const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 1.0, Pm.Z + Side.Z * Sg * 50.0);
								Quad(Into, At3(Pa.X + Side.X * Sg * FootW, Ga + Lo, Pa.Z + Side.Z * Sg * FootW), At3(Pb.X + Side.X * Sg * FootW, Gb + Lo, Pb.Z + Side.Z * Sg * FootW),
									At3(Pb.X + Side.X * Sg * TopW, Gb + Hi, Pb.Z + Side.Z * Sg * TopW), At3(Pa.X + Side.X * Sg * TopW, Ga + Hi, Pa.Z + Side.Z * Sg * TopW), Far,
									Scanned ? Mask : Shade(Col, Sg > 0.0 ? 1.0 : 0.9), false, Scanned ? 0.22f : -1.0f);
							}
							Quad(Into, At3(Pa.X + Side.X * TopW, Ga + Hi, Pa.Z + Side.Z * TopW), At3(Pb.X + Side.X * TopW, Gb + Hi, Pb.Z + Side.Z * TopW),
								At3(Pb.X - Side.X * TopW, Gb + Hi, Pb.Z - Side.Z * TopW), At3(Pa.X - Side.X * TopW, Ga + Hi, Pa.Z - Side.Z * TopW), Sky, Scanned ? Mask : Shade(Col, 1.12), false, Scanned ? 0.45f : -1.0f);
						};
						if (Bank)
						{
							// (a bank about 1.4 m, a metre wide at its top, and the hedge on it)
							const double BankHigh = 1.3 + 0.3 * Chance(F.Line[0].X, F.Line[0].Z, 21);
							Stretch(1.15, 0.55, -0.4, BankHigh, Shade(Turf, 0.85 + 0.3 * Chance(F.Line[0].X, F.Line[0].Z, 22)), false);
							if (Fine) Stretch(0.5, 0.35, BankHigh - 0.1, BankHigh + 0.9 + 0.5 * Chance(Pa.X, Pa.Z, 23), Shade(HedgeGreen, 0.85 + 0.3 * Chance(Pa.X, Pa.Z, 24)), false);
							continue;
						}
						Stretch(Foot, Top, -0.4, High, Colour, IsWall);
					}
				}
			}
		}

	// Buildings: each on its mapped footprint, stone under a pitched roof, its ridge along its length.
	static const Rgb Stone = {0.30f, 0.28f, 0.245f}, Slate = {0.085f, 0.085f, 0.095f}, StoneSlate = {0.17f, 0.155f, 0.13f}, Glass = {0.02f, 0.025f, 0.03f},
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
				// (the local walling: render, brick, else stone; a barn stays stone, or brick where brick is the rule)
				const bool Rendered = !Big && Pick < Look.RenderShare;
				const bool Brick = !Rendered && (Big ? Pick < Look.BrickShare * 0.8 : Pick < Look.RenderShare + Look.BrickShare);
				const Rgb Washes = {Look.RenderColour.R, Look.RenderColour.G, Look.RenderColour.B}, Bricks = {Look.BrickColour.R, Look.BrickColour.G, Look.BrickColour.B};
				const Rgb Wall = Shade(Rendered ? Washes : Brick ? Bricks : Stone, (Rendered ? 0.92 : 0.85) + (Rendered ? 0.14 : 0.3) * Chance(B.X, B.Z, 4));
				// (a stone house's walls in the scanned masonry: brightness, mostly the bare scan, a little lichen)
				Rgb Mask;
				Mask.R = float(0.48 + 0.14 * Chance(B.X, B.Z, 4));
				Mask.G = 0.8f;
				Mask.B = 0.3f;
				const auto WallQuad = [&](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, double By)
				{
					if (Rendered) Quad(*Washed, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By), false, 0.08f);
					else if (Brick) Quad(Out, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By));
					else Quad(*Houses, Q0, Q1, Q2, Q3, Toward, Mask, false, 0.06f);
				};
				// (the local roofs: thatch, clay tiles or pantiles, stone slates, else slate)
				const double RoofPick = Chance(B.X, B.Z, 6);
				const bool Thatch = !Big && RoofPick < Look.ThatchShare;
				const bool Tiled = !Thatch && RoofPick < Look.ThatchShare + Look.TileShare;
				const bool Flags = !Thatch && !Tiled && RoofPick < Look.ThatchShare + Look.TileShare + Look.StoneSlateShare;
				static const Rgb Straw = {0.27f, 0.21f, 0.11f};
				const Rgb Tiles = {Look.TileColour.R, Look.TileColour.G, Look.TileColour.B};
				const Rgb Roof = Shade(Thatch ? Straw : Tiled ? Tiles : Flags ? StoneSlate : Slate, 0.9 + 0.2 * Chance(B.X, B.Z, 5));
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
					const double Over = Thatch ? 0.55 : 0.3, Drop = Over * Rise / HalfShort;
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
	FurnitureTile(X0, Z0, Size, Fine, Out);
	if (Walls->Vertices() > 0) Out.Stone = Walls;
	if (Houses->Vertices() > 0) Out.Masonry = Houses;
	if (Washed->Vertices() > 0) Out.Render = Washed;
}

// --- The road's furniture (stage 5) -----------------------------------------------------------------

int RidePlan::FurnitureCount(int Kind) const
{
	int N = 0;
	for (const Furnish& F : Furniture) N += F.Kind == Kind ? 1 : 0;
	return N;
}

void RidePlan::IndexFurniture()
{
	Furniture.clear();
	BuiltUp.assign(Road.size(), 0);
	if (!Scene || Road.size() < 2) return;
	const Region& G = Area.Roads;
	const auto Put = [this](Furnish F)
	{
		if (std::isnan(F.Height)) return;
		Scene->Furniture[SceneKey(F.At.X, F.At.Z)].push_back(int(Furniture.size()));
		Furniture.push_back(std::move(F));
	};
	const auto Dir = [this](size_t I)
	{
		const size_t A = I > 0 ? I - 1 : 0, B = std::min(Road.size() - 1, I + 1);
		const P2 D = Road[B].At - Road[A].At;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const auto OffAll = [this](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	// Built up: six buildings or more within 70 m of the road, gaps under 120 m closed, stretches
	// under 150 m left out.
	{
		std::vector<unsigned char> Busy(Road.size(), 0);
		for (size_t I = 0; I < Road.size(); ++I)
		{
			const P2 P = Road[I].At;
			int Count = 0;
			for (int Dz = -1; Dz <= 1 && Count < 6; ++Dz)
				for (int Dx = -1; Dx <= 1 && Count < 6; ++Dx)
				{
					const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
					if (It == Scene->Buildings.end()) continue;
					for (int B : It->second)
						if (std::hypot(G.Buildings[size_t(B)].X - P.X, G.Buildings[size_t(B)].Z - P.Z) < 70.0 && ++Count >= 6) break;
				}
			Busy[I] = Count >= 6 ? 1 : 0;
		}
		const auto Runs = [](const std::vector<unsigned char>& Of, unsigned char Want)
		{
			std::vector<std::pair<size_t, size_t>> Out;
			for (size_t I = 0; I < Of.size();)
			{
				if (Of[I] != Want) { ++I; continue; }
				size_t J = I;
				while (J + 1 < Of.size() && Of[J + 1] == Want) ++J;
				Out.emplace_back(I, J);
				I = J + 1;
			}
			return Out;
		};
		for (const auto& Gap : Runs(Busy, 0))
			if (Gap.first > 0 && Gap.second + 1 < Busy.size() && Road[Gap.second].Along - Road[Gap.first].Along < 120.0)
				for (size_t I = Gap.first; I <= Gap.second; ++I) Busy[I] = 1;
		for (const auto& Run : Runs(Busy, 1))
			if (Road[Run.second].Along - Road[Run.first].Along >= 150.0)
				for (size_t I = Run.first; I <= Run.second; ++I) BuiltUp[I] = 1;
	}
	const auto PointAt = [this](double Along)
	{
		size_t I = 0;
		while (I + 1 < Road.size() && Road[I + 1].Along <= Along) ++I;
		return I;
	};
	const auto Verge = [&](size_t I, double Side, double Out)
	{
		// (Side +1 the left of the way the road runs, -1 its right; Out metres past the edge)
		const P2 D = Dir(I);
		const P2 L(D.Z, -D.X);
		return Road[I].At + L * (Side * (Road[I].Half + Out));
	};
	// Coming into each built-up stretch, each way: the place's name, then the 30 limit; leaving, the
	// national limit. In Britain they stand on the left of the traffic that reads them.
	for (size_t I = 0; I < Road.size(); ++I)
	{
		if (!BuiltUp[I] || (I > 0 && BuiltUp[I - 1])) continue;
		size_t J = I;
		while (J + 1 < Road.size() && BuiltUp[J + 1]) ++J;
		// (named by the nearest of the map's places to its middle)
		const P2 Mid = Road[(I + J) / 2].At;
		std::string Called;
		double Best = 2500.0;
		for (const TorqueFit::Json& Pl : G.Places)
		{
			const double D = std::hypot(Pl.Num("x") - Mid.X, Pl.Num("z") - Mid.Z);
			if (D < Best) { Best = D; Called = Pl.Str("name"); }
		}
		for (int Way = 0; Way < 2; ++Way)
		{
			// (forwards it starts at I, backwards at J; its left is the forward left, then the right)
			const double Side = Way == 0 ? 1.0 : -1.0;
			const double Start = Way == 0 ? Road[I].Along : Road[J].Along, Back = Way == 0 ? -1.0 : 1.0;
			const auto Sign = [&](int Kind, double Along, const std::string& Words)
			{
				if (Along < 0.0 || Along > Length) return;
				const size_t K = PointAt(Along);
				Furnish F;
				F.Kind = Kind;
				// (a board stands across the way, so its near edge, not its middle, is kept clear of the road)
				const double Wide = Kind == NameSign ? 0.5 + 0.17 * double(Words.size()) : 0.8;
				F.At = Verge(K, Side, 0.7 + Wide * 0.5);
				F.Face = Dir(K) * Back;   // (towards the traffic coming in)
				F.Text = Words;
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (OffAll(F.At, 0.6) && OffAll(F.At + Across * (Wide * 0.5), 0.3) && OffAll(F.At - Across * (Wide * 0.5), 0.3)) Put(F);
			};
			if (!Called.empty()) Sign(NameSign, Start + Back * 10.0, Called);
			Sign(Limit30, Start, "30");
			// (leaving at the other end, the way this traffic goes: its left there)
			const double End = Way == 0 ? Road[J].Along : Road[I].Along;
			Sign(LimitNational, End - Back * 5.0, "");
		}
		// Street lamps through it, on its left every 36 m.
		for (double Along = Road[I].Along + 12.0; Along < Road[J].Along - 6.0; Along += 36.0)
		{
			const size_t K = PointAt(Along);
			Furnish F;
			F.Kind = StreetLamp;
			F.At = Verge(K, 1.0, 0.9);
			const P2 D = Dir(K);
			F.Face = P2(-D.Z, D.X);   // (its arm reaches out over the road)
			F.Height = Ground(F.At.X, F.At.Z);
			if (OffAll(F.At, 0.5)) Put(F);
		}
		I = J;
	}
	// Chevrons round the sharp bends: under 40 m round, turning 45 degrees or more, on the outside.
	{
		const double Pi = 3.14159265358979323846;
		std::vector<double> Radius(Road.size(), 1e9), Turn(Road.size(), 0.0);
		for (size_t I = 0; I < Road.size(); ++I)
		{
			size_t A = I, B = I;
			while (A > 0 && Road[I].Along - Road[A].Along < 15.0) --A;
			while (B + 1 < Road.size() && Road[B].Along - Road[I].Along < 15.0) ++B;
			if (A == I || B == I) continue;
			const P2 U = Road[I].At - Road[A].At, V = Road[B].At - Road[I].At;
			double T = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
			while (T > Pi) T -= 2.0 * Pi;
			while (T < -Pi) T += 2.0 * Pi;
			Turn[I] = T;
			if (std::fabs(T) > 1e-4) Radius[I] = (Road[B].Along - Road[A].Along) / std::fabs(T);
		}
		for (size_t I = 0; I < Road.size(); ++I)
		{
			if (Radius[I] >= 40.0 || BuiltUp[I]) continue;
			size_t J = I, Apex = I;
			while (J + 1 < Road.size() && Radius[J + 1] < 40.0 && (Turn[J + 1] > 0.0) == (Turn[I] > 0.0))
			{
				++J;
				if (Radius[J] < Radius[Apex]) Apex = J;
			}
			double Whole = 0.0;
			for (size_t K = I; K <= J; ++K) Whole += Turn[K] * (K > I ? (Road[K].Along - Road[K - 1].Along) : 0.0) / 30.0;
			if (std::fabs(Whole) >= 45.0 * Pi / 180.0 || Radius[Apex] < 20.0)
				for (double Off : {-12.0, 0.0, 12.0})
				{
					const size_t K = PointAt(Road[Apex].Along + Off);
					// (a right-hand bend, Turn > 0, has its outside on the left)
					Furnish F;
					F.Kind = Chevron;
					F.At = Verge(K, Turn[Apex] > 0.0 ? 1.0 : -1.0, 1.6);
					F.Face = Dir(K) * -1.0;
					F.Size = Turn[Apex] > 0.0 ? -1.0 : 1.0;   // (+1: the arrows point left, as the forward rider reads them)
					F.Height = Ground(F.At.X, F.At.Z);
					if (OffAll(F.At, 0.8)) Put(F);
				}
			I = J;
		}
	}
	// What the map has: cattle grids on the road, and beside it poles, boxes, stops, benches, milestones and gates.
	for (const FeaturePoint& P : G.Scenery.Points)
	{
		int Kind = -1;
		if (P.Kind == "cattle_grid") Kind = CattleGrid;
		else if (P.Kind == "power_pole") Kind = PowerPole;
		else if (P.Kind == "post_box") Kind = PostBox;
		else if (P.Kind == "bus_stop") Kind = BusStop;
		else if (P.Kind == "bench") Kind = Seat;
		else if (P.Kind == "milestone") Kind = Milestone;
		else if (P.Kind == "gate") Kind = FieldGate;
		if (Kind < 0) continue;
		NearOpts Opts;
		NearHit Hit;
		const bool Any = NearestOf(P.At.X, P.At.Z, Opts, Hit);
		// (only near a road anyone sees: within 300 m of one)
		if (!Any && Kind != PowerPole) continue;
		Furnish F;
		F.Kind = Kind;
		F.At = P.At;
		F.Face = P2(std::cos(P.Heading), std::sin(P.Heading));
		if (std::isnan(F.Face.X) || std::isnan(F.Face.Z)) F.Face = P2(1.0, 0.0);
		if (Kind == CattleGrid)
		{
			// (across the road it lies on, the road's own way)
			if (!Any || Hit.Dist > Hit.Half) continue;
			const std::vector<RoadPoint>& On = RoadOf(Hit.Which);
			const size_t K = size_t(std::max(0, Hit.Index));
			if (K + 1 >= On.size()) continue;
			const P2 D = On[K + 1].At - On[K].At;
			const double L = D.Length();
			if (L < 1e-6) continue;
			F.At = Hit.Foot;
			F.Face = D * (1.0 / L);
			F.Size = Hit.Half * 2.0 + 0.6;
			F.Height = Hit.Height;
			Put(F);
			continue;
		}
		if (Any && Hit.Dist < Hit.Half + 0.5) continue;   // (a gate across a lane, a box on the tarmac: not here)
		if (Kind == FieldGate)
		{
			// In the wall, hedge or fence it stands in, along it, and a gap left in the wall for it.
			const auto It = Scene->Lines.find(SceneKey(P.At.X, P.At.Z));
			if (It == Scene->Lines.end()) continue;
			double BestD = 1.5;
			P2 Along;
			for (int L : It->second)
			{
				const FeatureLine& Line = G.Scenery.Lines[size_t(L)];
				for (size_t K = 0; K + 1 < Line.Line.size(); ++K)
				{
					const P2 A = Line.Line[K], B = Line.Line[K + 1], AB = B - A;
					const double Len2 = AB.X * AB.X + AB.Z * AB.Z;
					if (Len2 < 1e-6) continue;
					const double T = std::fmax(0.0, std::fmin(1.0, ((P.At.X - A.X) * AB.X + (P.At.Z - A.Z) * AB.Z) / Len2));
					const double D = (A + AB * T).DistanceTo(P.At);
					if (D < BestD) { BestD = D; Along = AB * (1.0 / std::sqrt(Len2)); }
				}
			}
			if (BestD >= 1.5) continue;
			F.Face = Along;
			F.Size = 3.6;
			Scene->Gaps[SceneKey(P.At.X, P.At.Z)].push_back(P.At);
		}
		F.Height = Ground(F.At.X, F.At.Z);
		Put(F);
	}
}

bool RidePlan::InGap(double X, double Z) const
{
	if (!Scene) return false;
	const auto It = Scene->Gaps.find(SceneKey(X, Z));
	if (It == Scene->Gaps.end()) return false;
	for (const P2& G : It->second)
		if (std::hypot(G.X - X, G.Z - Z) < 1.9) return true;
	return false;
}

void RidePlan::FurnitureTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out) const
{
	if (!Scene) return;
	static const Rgb White = {0.75f, 0.75f, 0.74f}, Black = {0.012f, 0.012f, 0.012f}, Red = {0.45f, 0.012f, 0.01f}, Grey = {0.16f, 0.16f, 0.165f}, Steel = {0.24f, 0.25f, 0.26f},
		Wood = {0.075f, 0.05f, 0.032f}, PoleWood = {0.05f, 0.04f, 0.03f}, Stone3 = {0.22f, 0.21f, 0.19f}, Lamp = {0.6f, 0.58f, 0.5f};
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	const auto Label = [&Out](const P3& At, const P2& Face, double Tall, const std::string& Words, const Rgb& Colour)
	{
		RideMesh::Label L;
		L.X = float(At.X - Out.OriginX);
		L.Y = float(At.Y);
		L.Z = float(At.Z - Out.OriginZ);
		L.Nx = float(Face.X);
		L.Nz = float(Face.Z);
		L.Size = float(Tall);
		L.R = Colour.R; L.G = Colour.G; L.B = Colour.B;
		L.Text = Words;
		Out.Labels.push_back(L);
	};
	// A flat board facing Face (its back grey), its middle Mid, W by H.
	const auto Board = [&Out](const P3& Mid, const P2& Face, double W, double H, const Rgb& Colour, double Proud)
	{
		const P2 U(Face.Z, -Face.X);
		const P3 C = At3(Mid.X + Face.X * Proud, Mid.Y, Mid.Z + Face.Z * Proud);
		const auto Pt = [&](double Su, double Sv) { return At3(C.X + U.X * Su * W * 0.5, C.Y + Sv * H * 0.5, C.Z + U.Z * Su * W * 0.5); };
		Quad(Out, Pt(-1, -1), Pt(1, -1), Pt(1, 1), Pt(-1, 1), At3(C.X + Face.X * 50.0, C.Y, C.Z + Face.Z * 50.0), Colour);
	};
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Furniture.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Furniture.end()) continue;
			for (int Index : It->second)
			{
				const Furnish& F = Furniture[size_t(Index)];
				if (F.At.X < X0 || F.At.X >= X0 + Size || F.At.Z < Z0 || F.At.Z >= Z0 + Size) continue;
				if (!Fine && F.Kind != PowerPole && F.Kind != StreetLamp) continue;
				const double Y = F.Height;
				const P2 Face = F.Face;
				const P2 Side(Face.Z, -Face.X);
				switch (F.Kind)
				{
				case NameSign:
				{
					const double W = 0.5 + 0.17 * double(F.Text.size()), H = 0.5;
					for (double Su : {-1.0, 1.0}) Box(Out, F.At.X + Side.X * Su * (W * 0.5 - 0.15), F.At.Z + Side.Z * Su * (W * 0.5 - 0.15), Face, 0.04, 0.04, Y - 0.3, Y + 1.95, Grey);
					const P3 Mid = At3(F.At.X, Y + 1.55, F.At.Z);
					Board(Mid, Face, W + 0.08, H + 0.08, Grey, -0.01);
					Board(Mid, Face * -1.0, W + 0.08, H + 0.08, Grey, 0.03);
					Board(Mid, Face, W + 0.04, H + 0.04, Black, 0.05);
					Board(Mid, Face, W, H, White, 0.06);
					Label(At3(Mid.X + Face.X * 0.08, Mid.Y, Mid.Z + Face.Z * 0.08), Face, 0.22, F.Text, Black);
					break;
				}
				case Limit30:
				case LimitNational:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.04, 0.04, Y - 0.3, Y + 1.9, Grey);
					const P3 Mid = At3(F.At.X, Y + 1.75, F.At.Z);
					Disc(Out, At3(Mid.X - Face.X * 0.05, Mid.Y, Mid.Z - Face.Z * 0.05), Face * -1.0, 0.38, Grey);
					if (F.Kind == Limit30)
					{
						Disc(Out, At3(Mid.X + Face.X * 0.05, Mid.Y, Mid.Z + Face.Z * 0.05), Face, 0.38, Red);
						Disc(Out, At3(Mid.X + Face.X * 0.06, Mid.Y, Mid.Z + Face.Z * 0.06), Face, 0.30, White);
						Label(At3(Mid.X + Face.X * 0.08, Mid.Y, Mid.Z + Face.Z * 0.08), Face, 0.3, F.Text, Black);
					}
					else
					{
						// (white, crossed by a black bar from top right to bottom left)
						Disc(Out, At3(Mid.X + Face.X * 0.05, Mid.Y, Mid.Z + Face.Z * 0.05), Face, 0.38, White);
						const P2 U(Face.Z, -Face.X);
						const double Px = Mid.X + Face.X * 0.065, Pz = Mid.Z + Face.Z * 0.065, R = 0.36, T = 0.05;
						const auto Pt = [&](double Su, double Sv) { return At3(Px + U.X * Su, Mid.Y + Sv, Pz + U.Z * Su); };
						const double C = R * 0.7071;
						Quad(Out, Pt(-C - T, -C + T), Pt(-C + T, -C - T), Pt(C + T, C - T), Pt(C - T, C + T), At3(Px + Face.X * 50.0, Mid.Y, Pz + Face.Z * 50.0), Black, true);
					}
					break;
				}
				case Chevron:
				{
					for (double Su : {-0.3, 0.3}) Box(Out, F.At.X + Side.X * Su, F.At.Z + Side.Z * Su, Face, 0.035, 0.035, Y - 0.3, Y + 1.7, Grey);
					const P3 Mid = At3(F.At.X, Y + 1.3, F.At.Z);
					// (each face its arrows the way its own reader's bend turns: forwards F.Size, back the other)
					for (int Back = 0; Back < 2; ++Back)
					{
						const P2 N = Back ? Face * -1.0 : Face;
						const double Point = Back ? -F.Size : F.Size;   // +1: the tip to the reader's left
						Board(Mid, N, 0.9, 0.7, Black, 0.03);
						const P2 U(N.Z, -N.X);   // (the reader's left, as they face it)
						const double Px = Mid.X + N.X * 0.04, Pz = Mid.Z + N.Z * 0.04;
						const auto Pt = [&](double Su, double Sv) { return At3(Px - U.X * Su * Point, Mid.Y + Sv, Pz - U.Z * Su * Point); };
						const P3 Toward = At3(Px + N.X * 50.0, Mid.Y, Pz + N.Z * 50.0);
						for (double Sv : {1.0, -1.0})
							Quad(Out, Pt(-0.2, 0.0), Pt(-0.06, 0.0), Pt(0.2, 0.26 * Sv), Pt(0.06, 0.26 * Sv), Toward, White, true);
					}
					break;
				}
				case StreetLamp:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.07, 0.07, Y - 0.3, Y + 6.0, Grey);
					if (!Fine) break;
					const double Ax = F.At.X + Face.X * 1.1, Az = F.At.Z + Face.Z * 1.1;
					Box(Out, (F.At.X + Ax) * 0.5, (F.At.Z + Az) * 0.5, Face, 0.6, 0.03, Y + 5.85, Y + 5.95, Grey);
					Box(Out, Ax, Az, Face, 0.25, 0.1, Y + 5.75, Y + 5.92, Grey);
					Board(At3(Ax, Y + 5.745, Az), P2(0.0, 1.0), 0.4, 0.15, Lamp, 0.0);
					break;
				}
				case CattleGrid:
				{
					// Steel bars across the road, a little proud of it, and a short rail each side.
					const P2 Across(Face.Z, -Face.X);
					const double Half = F.Size * 0.5;
					for (int K = -9; K <= 9; ++K)
					{
						const double Off = K * 0.13;
						const P2 C = F.At + Face * Off;
						const auto Top = [&](double Su)
						{
							const P2 Q = C + Across * (Su * Half);
							const double H = RoadHeight(Q.X, Q.Z);
							return std::isnan(H) ? Y : H;
						};
						const double Ya = Top(-1.0) + 0.012, Yb = Top(1.0) + 0.012;
						const P2 A = C + Across * (-Half), B = C + Across * Half, T = Face * 0.025;
						Quad(Out, At3(A.X - T.X, Ya, A.Z - T.Z), At3(B.X - T.X, Yb, B.Z - T.Z), At3(B.X + T.X, Yb, B.Z + T.Z), At3(A.X + T.X, Ya, A.Z + T.Z), At3(C.X, Y + 100.0, C.Z), Steel);
					}
					for (double Su : {-1.0, 1.0})
					{
						const P2 Q = F.At + Across * (Su * (Half + 0.3));
						const double G0 = Ground(Q.X, Q.Z);
						if (!std::isnan(G0)) Box(Out, Q.X, Q.Z, Face, 1.3, 0.05, G0 - 0.2, G0 + 0.9, Steel);
					}
					break;
				}
				case PowerPole:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.12, 0.12, Y - 0.5, Y + 8.5, PoleWood);
					Box(Out, F.At.X, F.At.Z, Side, 0.8, 0.05, Y + 7.9, Y + 8.05, PoleWood);
					break;
				}
				case PostBox:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.25, 0.25, Y - 0.1, Y + 1.45, Red);
					Box(Out, F.At.X, F.At.Z, Face, 0.28, 0.28, Y + 1.45, Y + 1.55, Shade(Red, 0.7));
					Board(At3(F.At.X, Y + 1.15, F.At.Z), Face, 0.3, 0.04, Black, 0.26);
					break;
				}
				case BusStop:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.04, 0.04, Y - 0.3, Y + 2.7, Grey);
					const P3 Mid = At3(F.At.X + Side.X * 0.3, Y + 2.45, F.At.Z + Side.Z * 0.3);
					for (int Back = 0; Back < 2; ++Back) Board(Mid, Back ? Face * -1.0 : Face, 0.5, 0.42, White, 0.01);
					for (int Back = 0; Back < 2; ++Back)
						Label(At3(Mid.X + (Back ? -Face.X : Face.X) * 0.03, Mid.Y, Mid.Z + (Back ? -Face.Z : Face.Z) * 0.03), Back ? Face * -1.0 : Face, 0.14, "BUS", Black);
					break;
				}
				case Seat:
				{
					Box(Out, F.At.X, F.At.Z, Side, 0.8, 0.22, Y + 0.42, Y + 0.47, Wood);
					Box(Out, F.At.X - Face.X * 0.2, F.At.Z - Face.Z * 0.2, Side, 0.8, 0.03, Y + 0.5, Y + 0.85, Wood);
					for (double Su : {-0.7, 0.7}) Box(Out, F.At.X + Side.X * Su, F.At.Z + Side.Z * Su, Side, 0.04, 0.2, Y - 0.1, Y + 0.42, Steel);
					break;
				}
				case Milestone:
				{
					Box(Out, F.At.X, F.At.Z, Face, 0.18, 0.12, Y - 0.2, Y + 0.75, Stone3);
					break;
				}
				case FieldGate:
				{
					// Five bars and a brace between two posts, in line with its wall, galvanised or wooden.
					const Rgb Bar = Chance(F.At.X, F.At.Z, 31) < 0.6 ? Steel : Shade(Wood, 1.6);
					const double Half = F.Size * 0.5;
					for (double Su : {-1.0, 1.0})
					{
						const P2 Q = F.At + Face * (Su * (Half + 0.12));
						Box(Out, Q.X, Q.Z, Face, 0.1, 0.1, Y - 0.3, Y + 1.35, Shade(Stone3, 1.1));
					}
					const P2 A = F.At + Face * -Half, B = F.At + Face * Half;
					for (int K = 0; K < 5; ++K)
					{
						const double H = 0.25 + K * 0.22;
						Quad(Out, At3(A.X, Y + H, A.Z), At3(B.X, Y + H, B.Z), At3(B.X, Y + H + 0.06, B.Z), At3(A.X, Y + H + 0.06, A.Z), At3(F.At.X + Side.X * 50.0, Y + 1.0, F.At.Z + Side.Z * 50.0), Bar, true);
					}
					Quad(Out, At3(A.X, Y + 0.25, A.Z), At3(A.X, Y + 0.31, A.Z), At3(B.X, Y + 1.19, B.Z), At3(B.X, Y + 1.13, B.Z), At3(F.At.X + Side.X * 50.0, Y + 1.0, F.At.Z + Side.Z * 50.0), Bar, true);
					break;
				}
				default: break;
				}
			}
		}
}
} // namespace TorqueWorld
