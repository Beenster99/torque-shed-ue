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
	// The season (Ride UK stage 6): the heather in flower on the moor in August, the bracken and the
	// pasture dull and straw-coloured over the winter, the crops ripening gold, then the stubble and
	// the plough.
	const double Bloom = Month == 8 ? 1.0 : Month == 9 ? 0.5 : Month == 7 ? 0.25 : 0.0;
	const double Dead = Month >= 11 || Month <= 2 ? 1.0 : Month == 3 || Month == 10 ? 0.5 : 0.0;
	const double Ripe = Month == 7 || Month == 8 ? 1.0 : Month == 6 ? 0.4 : 0.0, Stubble = Month >= 9 || Month <= 2 ? 1.0 : 0.0;
	const bool Patch = Chance(std::floor(X / 24.0), std::floor(Z / 24.0), 91) < 0.6;
	const auto Toward = [](Rgb C, const Rgb& To, double By)
	{
		C.R = float(C.R + (To.R - C.R) * By); C.G = float(C.G + (To.G - C.G) * By); C.B = float(C.B + (To.B - C.B) * By);
		return C;
	};
	static const Rgb Heather = {0.13f, 0.045f, 0.11f}, Bracken = {0.15f, 0.075f, 0.03f}, Straw = {0.125f, 0.12f, 0.06f}, Gold = {0.26f, 0.2f, 0.07f}, Plough = {0.11f, 0.08f, 0.05f};
	for (const double* Off : Offs)
	{
		const int Id = Land ? Land->Cover(X + Off[0], Z + Off[1]) : -1;
		Rgb C = Id >= 0 && Id < 10 ? CoverColour[Id] : NoCover;
		if (Id == 3) C = Toward(Toward(C, Heather, Patch ? 0.5 * Bloom : 0.0), Bracken, (Patch ? 0.15 : 0.4) * Dead);
		else if (Id == 0) C = Toward(C, Straw, 0.3 * Dead);
		else if (Id == 2) C = Toward(Toward(C, Gold, 0.5 * Ripe), Plough, 0.45 * Stubble);
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
	for (size_t S = 0; S < G.Streams.size(); ++S)
		for (size_t I = 0; I + 1 < G.Streams[S].Line.size(); ++I)
		{
			const P2 Mid = (G.Streams[S].Line[I] + G.Streams[S].Line[I + 1]) * 0.5;
			std::vector<int>& Row = Scene->Streams[SceneKey(Mid.X, Mid.Z)];
			if (std::find(Row.begin(), Row.end(), int(S)) == Row.end()) Row.push_back(int(S));
		}
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
	// (how far from a railway's middle: a railway drawn, or a disused one's bed)
	const auto RailClear = [this](double X, double Z)
	{
		double Dist = 0.0, Top = 0.0;
		int On = 0;
		return NearRail(X, Z, Dist, Top, On) && On != 2 ? Dist : 1e9;
	};
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
						if (RailClear(Pm.X, Pm.Z) < 2.6) continue;   // (nor across a railway)
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
				if (!Inside(B.X, B.Z) || B.W < 2.0 || B.D < 2.0 || !OffRoad(B.X, B.Z, std::fmin(B.W, B.D) * 0.5) || RailClear(B.X, B.Z) < std::fmin(B.W, B.D) * 0.5 + 2.0) continue;
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
				if (size_t(Index) < BuildingRole.size() && BuildingRole[size_t(Index)] == 1)
				{
					// A church: its tower at one end (the other if that one is over a road), square and a
					// little narrower than the nave, its belfry's openings near the top, battlemented, or in
					// the lowlands now and then a spire.
					const double TowerHalf = std::fmin(HalfShort * 0.8, 3.2);
					const double TowerTop = Eaves + Rise + 5.0 + 3.0 * Chance(B.X, B.Z, 53);
					double End = -1.0;
					for (double Try : {-1.0, 1.0})
					{
						bool Free = true;
						for (double Su : {0.0, 2.0})
							for (double Sv : {-1.0, 1.0})
							{
								const P3 Corner = Local(Try * (HalfLong + TowerHalf * Su - 0.2), Sv * TowerHalf, 0.0);
								Free = Free && OffRoad(Corner.X, Corner.Z, 0.8);
							}
						if (Free) { End = Try; break; }
						End = 0.0;
					}
					if (End != 0.0)
					{
						const double Mid = End * (HalfLong + TowerHalf - 0.2);
						const P3 Axis = Local(Mid, 0.0, TowerTop * 0.5);
						const auto Away = [&Axis](const P3& A, const P3& C) { return At3(Axis.X + ((A.X + C.X) * 0.5 - Axis.X) * 50.0, Axis.Y, Axis.Z + ((A.Z + C.Z) * 0.5 - Axis.Z) * 50.0); };
						const double S4[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
						const auto At4 = [&](int K, double Y) { return Local(Mid + S4[K][0] * TowerHalf, S4[K][1] * TowerHalf, Y); };
						for (int K = 0; K < 4; ++K)
						{
							const P3 Q0 = At4(K, Sunk), Q1 = At4(K + 1, Sunk), Q2 = At4(K + 1, TowerTop), Q3 = At4(K, TowerTop);
							WallQuad(Q0, Q1, Q2, Q3, Away(Q0, Q2), K % 2 == 0 ? 1.0 : 0.93);
							if (Fine)
							{
								// (the belfry's opening, a dark louvre just proud of the face)
								const P3 Pa = At4(K, 0.0), Pb = At4(K + 1, 0.0);
								const double Fx = (Pa.X + Pb.X) * 0.5, Fz = (Pa.Z + Pb.Z) * 0.5, Ux = (Pb.X - Pa.X) / (TowerHalf * 2.0), Uz = (Pb.Z - Pa.Z) / (TowerHalf * 2.0);
								const P3 Out3 = Away(Q0, Q2);
								const double Nx = Out3.X - Axis.X, Nz = Out3.Z - Axis.Z, Nl = std::hypot(Nx, Nz);
								const double Px = Fx + Nx / Nl * 0.04, Pz = Fz + Nz / Nl * 0.04;
								Quad(Out, At3(Px - Ux * 0.45, TowerTop - 3.0, Pz - Uz * 0.45), At3(Px + Ux * 0.45, TowerTop - 3.0, Pz + Uz * 0.45), At3(Px + Ux * 0.45, TowerTop - 1.3, Pz + Uz * 0.45),
									At3(Px - Ux * 0.45, TowerTop - 1.3, Pz - Uz * 0.45), Out3, Glass);
							}
						}
						const bool Spire = Look.Boundary == LocalLook::HedgeBank || (Look.BrickShare > 0.3 && Chance(B.X, B.Z, 54) < 0.4);
						if (Spire)
						{
							const P3 Tip = Local(Mid, 0.0, TowerTop + TowerHalf * 3.5);
							for (int K = 0; K < 4; ++K)
							{
								const P3 Q0 = At4(K, TowerTop), Q1 = At4(K + 1, TowerTop);
								Quad(Out, Q0, Q1, Tip, Q0, Away(Q0, Q1), Shade(Slate, K % 2 == 0 ? 1.0 : 0.85));
							}
						}
						else
						{
							// (a flat lead roof and a battlemented parapet: merlons along each side)
							Quad(Out, At4(0, TowerTop), At4(1, TowerTop), At4(2, TowerTop), At4(3, TowerTop), Sky, Shade(Slate, 1.3));
							if (Fine)
								for (int K = 0; K < 4; ++K)
									for (int M = 0; M < 3; ++M)
									{
										const double T = (M + 0.5) / 3.0;
										const P3 Pa = At4(K, TowerTop), Pb = At4(K + 1, TowerTop);
										const double Mx = Pa.X + (Pb.X - Pa.X) * T, Mz = Pa.Z + (Pb.Z - Pa.Z) * T;
										const double Lx = Pb.X - Pa.X, Lz = Pb.Z - Pa.Z, Ll = std::hypot(Lx, Lz);
										Box(Out, Mx, Mz, P2(Lx / Ll, Lz / Ll), 0.35, 0.2, TowerTop - 0.05, TowerTop + 0.7, Shade(Stone, 0.9));
									}
						}
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
				if (Tall < 3.0 || Chance(Jx, Jz, 13) > Share * 1.2 || !OffRoad(X, Z, 3.0) || RailClear(X, Z) < 5.0) continue;
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
	RailTile(X0, Z0, Size, Fine, Out, *Walls);
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
	// A red phone box half way through each built-up stretch of 250 m or more (most of them), on the left.
	for (size_t I = 0; I < Road.size(); ++I)
	{
		if (!BuiltUp[I] || (I > 0 && BuiltUp[I - 1])) continue;
		size_t J = I;
		while (J + 1 < Road.size() && BuiltUp[J + 1]) ++J;
		const size_t K = PointAt((Road[I].Along + Road[J].Along) * 0.5 + 20.0);
		if (Road[J].Along - Road[I].Along >= 250.0 && Chance(Road[K].At.X, Road[K].At.Z, 51) < 0.75)
		{
			Furnish F;
			F.Kind = PhoneBox;
			F.At = Verge(K, 1.0, 1.5);
			const P2 D = Dir(K);
			F.Face = P2(-D.Z, D.X);   // (its door to the road)
			F.Height = Ground(F.At.X, F.At.Z);
			if (OffAll(F.At, 0.9)) Put(F);
		}
		I = J;
	}
	// The pubs (the map's pubs and inns that are surely one) with their signs out by the road, and the
	// churches: the building nearest each, given its tower.
	BuildingRole.assign(G.Buildings.size(), 0);
	const auto NearestBuilding = [this, &G](double X, double Z, double Within)
	{
		int Best = -1;
		double BestD = Within;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(X + Dx * SceneCell, Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
				{
					const double D = std::hypot(G.Buildings[size_t(B)].X - X, G.Buildings[size_t(B)].Z - Z);
					if (D < BestD) { BestD = D; Best = B; }
				}
			}
		return Best;
	};
	for (const TorqueFit::Json& Poi : G.Pois)
	{
		const std::string What = Poi.Str("kind");
		if (What != "pub" && !(What == "inn" && Poi.Num("conf", 1.0) >= 0.75)) continue;
		const std::string Called = Poi.Str("name");
		const P2 At(Poi.Num("x"), Poi.Num("z"));
		const int Home = NearestBuilding(At.X, At.Z, 30.0);
		if (Home >= 0) BuildingRole[size_t(Home)] = 2;
		if (Called.empty()) continue;
		NearOpts Opts;
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist > Hit.Half + 40.0 || Hit.Deck || Hit.Bore) continue;
		const P2 Off = At - Hit.Foot;
		const double Len = Off.Length();
		if (Len < 1e-3) continue;
		const P2 Toward = Off * (1.0 / Len);
		Furnish F;
		F.Kind = PubSign;
		F.At = Hit.Foot + Toward * (Hit.Half + 1.3);
		F.Face = P2(Toward.Z, -Toward.X);   // (the board hangs along the road, read from either way)
		F.Text = Called;
		F.Height = Ground(F.At.X, F.At.Z);
		if (OffAll(F.At, 0.9)) Put(F);
	}
	for (const TorqueFit::Json& Mark : G.Landmarks)
	{
		if (Mark.Str("kind") != "worship") continue;
		const int Home = NearestBuilding(Mark.Num("x"), Mark.Num("z"), 40.0);
		if (Home >= 0 && G.Buildings[size_t(Home)].W * G.Buildings[size_t(Home)].D >= 60.0) BuildingRole[size_t(Home)] = 1;
	}
	// Level crossings: on each road's approach to a railway in use, a barrier (up: no trains run) and
	// its lights, on the traffic's left.
	for (const RailLine& Run : Rails)
	{
		if (Run.Disused) continue;
		for (size_t I = 0; I < Run.At.size(); ++I)
		{
			if (Run.On[I] != 3 || (I > 0 && Run.On[I - 1] == 3)) continue;
			size_t J = I;
			while (J + 1 < Run.At.size() && Run.On[J + 1] == 3) ++J;
			// (the rail's points just off the road each side, and the way across the line there)
			for (int End = 0; End < 2; ++End)
			{
				const size_t K = End == 0 ? (I > 0 ? I - 1 : I) : std::min(Run.At.size() - 1, J + 1);
				const size_t A = K > 0 ? K - 1 : K, B = std::min(Run.At.size() - 1, K + 1);
				const P2 D = Run.At[B] - Run.At[A];
				const double Len = D.Length();
				if (Len < 1e-6) continue;
				const P2 Left(D.Z / Len, -D.X / Len);
				const double Sg = End == 0 ? 1.0 : -1.0;
				Furnish F;
				F.Kind = CrossingBarrier;
				F.At = Run.At[K] + Left * (Sg * 3.2);
				F.Face = Left * Sg;   // (towards the traffic coming to the line on this side)
				F.Height = Ground(F.At.X, F.At.Z);
				if (OffAll(F.At, 0.4)) Put(F);
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
				case PhoneBox:
				{
					// A red box with a domed top, its glazed sides, and TELEPHONE over the door.
					Box(Out, F.At.X, F.At.Z, Face, 0.45, 0.45, Y - 0.1, Y + 2.3, Red);
					Box(Out, F.At.X, F.At.Z, Face, 0.5, 0.5, Y + 2.3, Y + 2.45, Shade(Red, 0.85));
					Box(Out, F.At.X, F.At.Z, Face, 0.36, 0.36, Y + 2.45, Y + 2.55, Shade(Red, 0.75));
					for (int Way = 0; Way < 4; ++Way)
					{
						const P2 N = Way == 0 ? Face : Way == 1 ? Side : Way == 2 ? Face * -1.0 : Side * -1.0;
						Board(At3(F.At.X, Y + 1.3, F.At.Z), N, 0.62, 1.4, Shade(Grey, 0.35), 0.455);
						Board(At3(F.At.X, Y + 2.17, F.At.Z), N, 0.7, 0.13, White, 0.505);
					}
					Label(At3(F.At.X + Face.X * 0.52, Y + 2.17, F.At.Z + Face.Z * 0.52), Face, 0.09, "TELEPHONE", Black);
					break;
				}
				case PubSign:
				{
					// A post, an arm out over the verge, and the board hanging from it with the pub's name both sides.
					Box(Out, F.At.X, F.At.Z, Face, 0.08, 0.08, Y - 0.3, Y + 3.4, Wood);
					// (Side is towards the road: the arm reaches out over the verge; the board painted one of a pub's colours)
					const P2 Arm = F.At + Side * 0.6;
					Box(Out, (F.At.X + Arm.X) * 0.5, (F.At.Z + Arm.Z) * 0.5, Side, 0.65, 0.04, Y + 3.2, Y + 3.3, Wood);
					static const Rgb Paints[4] = {{0.01f, 0.06f, 0.025f}, {0.12f, 0.012f, 0.015f}, {0.008f, 0.015f, 0.07f}, {0.012f, 0.012f, 0.012f}};
					const Rgb& Paint = Paints[std::min(3, int(Chance(F.At.X, F.At.Z, 52) * 4.0))];
					const double W = std::fmax(0.8, std::fmin(1.3, 0.25 + 0.05 * double(F.Text.size())));
					const P3 Mid = At3(Arm.X, Y + 2.55, Arm.Z);
					Board(Mid, Face, W, 0.9, Shade(Wood, 1.4), 0.02);
					Board(Mid, Face * -1.0, W, 0.9, Shade(Wood, 1.4), 0.02);
					Board(Mid, Face, W - 0.1, 0.8, Paint, 0.03);
					Board(Mid, Face * -1.0, W - 0.1, 0.8, Paint, 0.03);
					for (int Back = 0; Back < 2; ++Back)
					{
						const P2 N = Back ? Face * -1.0 : Face;
						Label(At3(Mid.X + N.X * 0.045, Mid.Y, Mid.Z + N.Z * 0.045), N, std::fmin(0.12, 0.9 * (W - 0.2) / std::fmax(1.0, double(F.Text.size()) * 0.55)), F.Text, Shade(White, 1.1));
					}
					break;
				}
				case CrossingBarrier:
				{
					// The post with its two red lights in a black board facing the traffic, and the red and
					// white barrier arm raised.
					Box(Out, F.At.X, F.At.Z, Face, 0.08, 0.08, Y - 0.3, Y + 2.6, White);
					const P3 Lights = At3(F.At.X, Y + 2.2, F.At.Z);
					Board(Lights, Face, 1.0, 0.45, Black, 0.09);
					for (double Su : {-0.3, 0.3})
						Disc(Out, At3(Lights.X + Side.X * Su + Face.X * 0.1, Lights.Y, Lights.Z + Side.Z * Su + Face.Z * 0.1), Face, 0.11, Shade(Red, 1.3), 12);
					Box(Out, F.At.X + Side.X * 0.3, F.At.Z + Side.Z * 0.3, Face, 0.18, 0.18, Y - 0.1, Y + 1.1, Shade(Grey, 0.6));
					for (int K = 0; K < 6; ++K)
						Box(Out, F.At.X + Side.X * 0.3, F.At.Z + Side.Z * 0.3, Face, 0.05, 0.05, Y + 1.1 + K * 0.6, Y + 1.7 + K * 0.6, K % 2 == 0 ? Red : White);
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

// --- Water (stage 5) ----------------------------------------------------------------------------------

namespace
{
const Rgb SeaWater = {0.022f, 0.048f, 0.062f}, LakeWater = {0.026f, 0.040f, 0.044f}, RiverWater = {0.034f, 0.044f, 0.038f};
}

void RidePlan::WaterTile(double X0, double Z0, double Size, int Cells, const RideMesh& Sheet, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	if (!Land || Cells < 1) return;
	const int N = Cells + 1;
	const double Step = Size / Cells;
	if (Sheet.Vertices() < size_t(N) * N) return;
	// Each point of the square: 0 dry, 1 a lake or a wide river (the land's cover), 2 the sea.
	std::vector<unsigned char> Wet(size_t(N) * N, 0);
	bool AnyWet = false;
	for (int J = 0; J < N; ++J)
		for (int I = 0; I < N; ++I)
		{
			const double X = X0 + I * Step, Z = Z0 + J * Step;
			unsigned char W = Land->SeaAt(X, Z) ? 2 : Land->Cover(X, Z) == Terrain::CoverWater ? 1 : 0;
			// (never on a road: a causeway or a road along a lake's edge stays dry)
			if (W != 0)
			{
				NearOpts Opts;
				Opts.NoDecks = true;
				NearHit Hit;
				if (NearestOf(X, Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.0) W = 0;
			}
			Wet[size_t(J) * N + I] = W;
			AnyWet = AnyWet || W != 0;
		}
	const auto Put = [&Out](double X, double Y, double Z, const Rgb& C)
	{
		Out.Pos.push_back(float(X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z - Out.OriginZ));
		Out.Nrm.push_back(0.0f); Out.Nrm.push_back(1.0f); Out.Nrm.push_back(0.0f);
		Out.Uv.push_back(float(X / 8.0)); Out.Uv.push_back(float(Z / 8.0));
		Out.Col.push_back(C.R); Out.Col.push_back(C.G); Out.Col.push_back(C.B);
	};
	if (AnyWet)
	{
		// A quad of water over every cell with a wet corner: a wet corner at its own ground and a little
		// over (the sea at its level), a dry one no higher than the lowest wet one, so the water runs
		// under the bank and the land itself draws the shore.
		for (int J = 0; J < Cells; ++J)
			for (int I = 0; I < Cells; ++I)
			{
				const int Ci[4] = {I, I + 1, I, I + 1}, Cj[4] = {J, J, J + 1, J + 1};
				double Level[4], Lowest = 1e9;
				bool Sea = false, Any = false;
				for (int K = 0; K < 4; ++K)
				{
					const size_t V = size_t(Cj[K]) * N + Ci[K];
					Level[K] = std::nan("");
					if (Wet[V] == 0) continue;
					Any = true;
					Sea = Sea || Wet[V] == 2;
					Level[K] = Wet[V] == 2 ? 0.0 : double(Sheet.Pos[V * 3 + 1]) + 0.12;
					Lowest = std::fmin(Lowest, Level[K]);
				}
				if (!Any) continue;
				const uint32_t Base = uint32_t(Out.Vertices());
				for (int K = 0; K < 4; ++K)
				{
					// (a dry corner a little under its own ground, so on flat land the shore runs between
					// the points, not round the squares)
					const double Y = std::isnan(Level[K]) ? std::fmin(Lowest, double(Sheet.Pos[(size_t(Cj[K]) * N + Ci[K]) * 3 + 1]) - 0.25) : Level[K];
					Put(X0 + Ci[K] * Step, Y, Z0 + Cj[K] * Step, Sea ? SeaWater : LakeWater);
				}
				// (A B across the top, C D below: as LandTile's own quads, facing up)
				Out.Idx.insert(Out.Idx.end(), {Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
			}
	}
	// The map's rivers, streams and canals the cover doesn't hold: a ribbon along each, on the finer squares.
	if (Step > 8.5 || !Scene || Scene->Streams.empty()) return;
	const Region& G = Area.Roads;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = Scene->Streams.find(Key);
			if (It == Scene->Streams.end()) continue;
			for (int S : It->second)
			{
				const Stream& Flow = G.Streams[size_t(S)];
				const double Wide = Flow.Kind == 0 ? 7.0 : Flow.Kind == 2 ? 6.0 : 2.2;
				for (size_t I = 0; I + 1 < Flow.Line.size(); ++I)
				{
					const P2 A = Flow.Line[I], B = Flow.Line[I + 1];
					const P2 Mid = (A + B) * 0.5;
					if (SceneKey(Mid.X, Mid.Z) != Key || !Inside(Mid)) continue;
					const double Len = A.DistanceTo(B);
					if (Len < 0.2) continue;
					const P2 Dir = (B - A) * (1.0 / Len), Left(Dir.Z, -Dir.X);
					const int Bits = std::max(1, int(std::ceil(Len / 3.0)));
					// (each bit a little longer than its share, so the bends close)
					for (int K = 0; K < Bits; ++K)
					{
						const P2 Pa = A + (B - A) * (double(K) / Bits) - Dir * 0.3, Pb = A + (B - A) * (double(K + 1) / Bits) + Dir * 0.3;
						const P2 Pm = (Pa + Pb) * 0.5;
						if (Land->Cover(Pm.X, Pm.Z) == Terrain::CoverWater || Land->SeaAt(Pm.X, Pm.Z)) continue;
						NearOpts Opts;
						NearHit Hit;
						if (NearestOf(Pm.X, Pm.Z, Opts, Hit) && Hit.Dist <= Hit.Half + Wide * 0.5 + 1.5) continue;   // (under the road: a culvert)
						double Lie = 1e9;
						for (const P2& P : {Pa, Pb, Pa + Left * (Wide * 0.5), Pa - Left * (Wide * 0.5), Pb + Left * (Wide * 0.5), Pb - Left * (Wide * 0.5)})
						{
							const double Y = Ground(P.X, P.Z, Step);
							if (!std::isnan(Y)) Lie = std::fmin(Lie, Y);
						}
						if (Lie > 1e8) continue;
						const double Y = Flow.Kind == 2 ? Lie + 0.2 : Lie + 0.12;
						const uint32_t Base = uint32_t(Out.Vertices());
						const Rgb C = Flow.Kind == 2 ? LakeWater : RiverWater;
						Put(Pa.X + Left.X * Wide * 0.5, Y, Pa.Z + Left.Z * Wide * 0.5, C);
						Put(Pa.X - Left.X * Wide * 0.5, Y, Pa.Z - Left.Z * Wide * 0.5, C);
						Put(Pb.X + Left.X * Wide * 0.5, Y, Pb.Z + Left.Z * Wide * 0.5, C);
						Put(Pb.X - Left.X * Wide * 0.5, Y, Pb.Z - Left.Z * Wide * 0.5, C);
						// (Left is the stream's left going down it, so these face up whichever way it runs)
						Out.Idx.insert(Out.Idx.end(), {Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
					}
				}
			}
		}
}

// --- Railways (stage 5) -------------------------------------------------------------------------------

namespace
{
// How far P is from the segment A B, and how far along it (0..1) the nearest point is.
double ToSegment(const P2& P, const P2& A, const P2& B, double& T)
{
	const P2 D = B - A;
	const double L2 = D.X * D.X + D.Z * D.Z;
	T = L2 > 1e-12 ? std::clamp(((P.X - A.X) * D.X + (P.Z - A.Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
	return P.DistanceTo(A + D * T);
}

long long RailNearKey(double X, double Z, double Cell)
{
	return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell));
}
} // namespace

bool RidePlan::NearRail(double X, double Z, double& Dist, double& Top, int& On, bool* Disused) const
{
	if (!Scene || Scene->RailNearBy.empty()) return false;
	const auto It = Scene->RailNearBy.find(RailNearKey(X, Z, RailCell));
	if (It == Scene->RailNearBy.end()) return false;
	const P2 Here(X, Z);
	double Best = 40.0;
	bool Found = false;
	for (const std::pair<int, int>& Bit : It->second)
	{
		const RailLine& Run = Rails[size_t(Bit.first)];
		const size_t I = size_t(Bit.second);
		double T = 0.0;
		const double D = ToSegment(Here, Run.At[I], Run.At[I + 1], T);
		if (D >= Best) continue;
		Best = D;
		Found = true;
		Dist = D;
		Top = Run.Top[I] + (Run.Top[I + 1] - Run.Top[I]) * T;
		On = Run.On[T < 0.5 ? I : I + 1];
		if (Disused) *Disused = Run.Disused;
	}
	return Found;
}

void RidePlan::IndexRails()
{
	Rails.clear();
	Platforms.clear();
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	// The map's pieces of railway (not those still being built).
	std::vector<int> Pieces;
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& F = G.Scenery.Lines[L];
		const bool Rail = F.Kind == "railway" || F.Kind == "narrow_gauge" || F.Kind == "preserved" || F.Kind == "light_rail";
		if (Rail && F.Line.size() >= 2 && (F.Flags & 8) == 0) Pieces.push_back(int(L));
	}
	const auto OnOf = [](int Flags) { return static_cast<unsigned char>((Flags & 1) ? 1 : (Flags & 2) ? 2 : 0); };
	std::vector<char> Used(Pieces.size(), 0);
	for (size_t Seed = 0; Seed < Pieces.size(); ++Seed)
	{
		if (Used[Seed]) continue;
		Used[Seed] = 1;
		const FeatureLine& Start = G.Scenery.Lines[size_t(Pieces[Seed])];
		const bool Gone = (Start.Flags & 4) != 0;
		// Joined end to end with the pieces that meet it (a junction: the first found carries on), each
		// point with what the stretch after it is on.
		std::vector<P2> Pts(Start.Line.begin(), Start.Line.end());
		std::vector<unsigned char> After(Pts.size(), OnOf(Start.Flags));
		for (int End = 0; End < 2; ++End)
			for (bool Grew = true; Grew;)
			{
				Grew = false;
				const P2 Tip = End == 0 ? Pts.back() : Pts.front();
				for (size_t K = 0; K < Pieces.size() && !Grew; ++K)
				{
					if (Used[K]) continue;
					const FeatureLine& F = G.Scenery.Lines[size_t(Pieces[K])];
					if (((F.Flags & 4) != 0) != Gone) continue;
					const bool AtStart = F.Line.front().DistanceTo(Tip) < 0.5, AtEnd = F.Line.back().DistanceTo(Tip) < 0.5;
					if (!AtStart && !AtEnd) continue;
					std::vector<P2> More(F.Line.begin(), F.Line.end());
					if (End == 0 ? !AtStart : !AtEnd) std::reverse(More.begin(), More.end());
					const unsigned char On = OnOf(F.Flags);
					if (End == 0)
					{
						After.back() = On;
						Pts.insert(Pts.end(), More.begin() + 1, More.end());
						After.insert(After.end(), More.size() - 1, On);
					}
					else
					{
						Pts.insert(Pts.begin(), More.begin(), More.end() - 1);
						After.insert(After.begin(), More.size() - 1, On);
					}
					Used[K] = 1;
					Grew = true;
				}
			}
		// A point every RailStep metres along it.
		std::vector<P2> At;
		std::vector<unsigned char> On;
		double Carry = 0.0;
		for (size_t I = 0; I + 1 < Pts.size(); ++I)
		{
			const double Len = Pts[I].DistanceTo(Pts[I + 1]);
			if (Len < 1e-6) continue;
			double S = Carry;
			for (; S < Len; S += RailStep)
			{
				At.push_back(Pts[I] + (Pts[I + 1] - Pts[I]) * (S / Len));
				On.push_back(After[I]);
			}
			Carry = S - Len;
		}
		if (At.empty() || At.back().DistanceTo(Pts.back()) > 0.5)
		{
			At.push_back(Pts.back());
			On.push_back(After[Pts.size() >= 2 ? Pts.size() - 2 : 0]);
		}
		// The ground under it, and the stretches where the ride's land has it (each a line of its own).
		std::vector<double> Lie(At.size());
		for (size_t I = 0; I < At.size(); ++I) Lie[I] = Land->Height(At[I].X, At[I].Z);
		for (size_t From = 0; From < At.size();)
		{
			if (std::isnan(Lie[From])) { ++From; continue; }
			size_t To = From;
			while (To + 1 < At.size() && !std::isnan(Lie[To + 1])) ++To;
			const size_t Count = To - From + 1;
			if (Count >= 3)
			{
				RailLine Run;
				Run.Disused = Gone;
				Run.At.assign(At.begin() + long(From), At.begin() + long(To) + 1);
				Run.On.assign(On.begin() + long(From), On.begin() + long(To) + 1);
				std::vector<double> Y(Lie.begin() + long(From), Lie.begin() + long(To) + 1);
				// Bridges and tunnels straight from one end to the other.
				const auto Spans = [&Run, &Y]()
				{
					for (size_t A = 0; A < Y.size();)
					{
						if (Run.On[A] != 1 && Run.On[A] != 2) { ++A; continue; }
						size_t B = A;
						while (B + 1 < Y.size() && (Run.On[B + 1] == 1 || Run.On[B + 1] == 2)) ++B;
						const size_t Lo = A > 0 ? A - 1 : A, Hi = B + 1 < Y.size() ? B + 1 : B;
						if (Hi > Lo)
							for (size_t K = Lo + 1; K < Hi; ++K) Y[K] = Y[Lo] + (Y[Hi] - Y[Lo]) * double(K - Lo) / double(Hi - Lo);
						A = B + 1;
					}
				};
				Spans();
				// Eased along (a railway climbs gently: it cuts through the rises and banks over the dips),
				// then no steeper anywhere than 1 in 40: halfway between the highest such line under the
				// eased one and the lowest over it.
				for (int Pass = 0; Pass < 3; ++Pass)
				{
					std::vector<double> Sum(Y.size() + 1, 0.0);
					for (size_t K = 0; K < Y.size(); ++K) Sum[K + 1] = Sum[K] + Y[K];
					std::vector<double> Eased(Y.size());
					for (size_t K = 0; K < Y.size(); ++K)
					{
						const size_t A = K > 12 ? K - 12 : 0, B = std::min(Y.size() - 1, K + 12);
						Eased[K] = (Sum[B + 1] - Sum[A]) / double(B - A + 1);
					}
					Y.swap(Eased);
				}
				const double Grade = RailStep / 40.0;
				{
					std::vector<double> Under = Y, Over = Y;
					for (size_t K = 1; K < Y.size(); ++K) { Under[K] = std::fmin(Under[K], Under[K - 1] + Grade); Over[K] = std::fmax(Over[K], Over[K - 1] - Grade); }
					for (size_t K = Y.size() - 1; K-- > 0;) { Under[K] = std::fmin(Under[K], Under[K + 1] + Grade); Over[K] = std::fmax(Over[K], Over[K + 1] - Grade); }
					for (size_t K = 0; K < Y.size(); ++K) Y[K] = (Under[K] + Over[K]) * 0.5;
				}
				Spans();
				// The roads it meets: level with one it crosses, over one it bridges, under one that bridges it.
				std::vector<double> Floor(Y.size(), -1e9), Ceiling(Y.size(), 1e9);
				for (size_t K = 0; K < Y.size(); ++K)
				{
					NearOpts Opts;
					NearHit Hit;
					if (Run.On[K] == 2 || !NearestOf(Run.At[K].X, Run.At[K].Z, Opts, Hit) || Hit.Dist > Hit.Half + 2.0 || Hit.Bore) continue;
					const double Surface = Hit.Height + Hit.Cross;
					if (Hit.Deck && Run.On[K] != 1) Ceiling[K] = Surface - 6.5;
					else if (Run.On[K] == 1) Floor[K] = Surface + 6.0;
					else if (!Hit.Deck && Hit.Dist <= Hit.Half + 0.6)
					{
						Floor[K] = Ceiling[K] = Surface - 0.27;   // (the rails' tops flush with the road)
						Run.On[K] = 3;
					}
				}
				// (a railway's ramp to and from them: 1 in 40 too)
				const double Ramp = Grade;
				for (size_t K = 1; K < Y.size(); ++K) { Floor[K] = std::fmax(Floor[K], Floor[K - 1] - Ramp); Ceiling[K] = std::fmin(Ceiling[K], Ceiling[K - 1] + Ramp); }
				for (size_t K = Y.size() - 1; K-- > 0;) { Floor[K] = std::fmax(Floor[K], Floor[K + 1] - Ramp); Ceiling[K] = std::fmin(Ceiling[K], Ceiling[K + 1] + Ramp); }
				for (size_t K = 0; K < Y.size(); ++K) Y[K] = std::fmax(std::fmin(Y[K], Ceiling[K]), Floor[K]);
				Run.Top = Y;
				Rails.push_back(std::move(Run));
			}
			From = To + 1;
		}
	}
	// Filed by square: each bit by its middle's 256 m square, and by every RailCell square within 40 m.
	for (size_t L = 0; L < Rails.size(); ++L)
	{
		const RailLine& Run = Rails[L];
		for (size_t I = 0; I + 1 < Run.At.size(); ++I)
		{
			const P2 A = Run.At[I], B = Run.At[I + 1];
			const P2 Mid = (A + B) * 0.5;
			Scene->RailBits[SceneKey(Mid.X, Mid.Z)].emplace_back(int(L), int(I));
			const int X0 = int(std::floor((std::fmin(A.X, B.X) - 40.0) / RailCell)), X1 = int(std::floor((std::fmax(A.X, B.X) + 40.0) / RailCell));
			const int Z0 = int(std::floor((std::fmin(A.Z, B.Z) - 40.0) / RailCell)), Z1 = int(std::floor((std::fmax(A.Z, B.Z) + 40.0) / RailCell));
			for (int Cz = Z0; Cz <= Z1; ++Cz)
				for (int Cx = X0; Cx <= X1; ++Cx)
					Scene->RailNearBy[static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz)].emplace_back(int(L), int(I));
		}
	}
	// The stations' platforms, beside the line nearest each station on the map.
	for (const FeaturePoint& P : G.Scenery.Points)
	{
		const bool Halt = P.Kind == "railway_halt";
		if (!Halt && P.Kind != "railway_station") continue;
		const auto It = Scene->RailNearBy.find(RailNearKey(P.At.X, P.At.Z, RailCell));
		if (It == Scene->RailNearBy.end()) continue;
		double Best = 40.0;
		int BestLine = -1, BestAt = 0;
		for (const std::pair<int, int>& Bit : It->second)
		{
			const RailLine& Run = Rails[size_t(Bit.first)];
			double T = 0.0;
			const double D = ToSegment(P.At, Run.At[size_t(Bit.second)], Run.At[size_t(Bit.second) + 1], T);
			if (D < Best) { Best = D; BestLine = Bit.first; BestAt = Bit.second + (T < 0.5 ? 0 : 1); }
		}
		if (BestLine < 0) continue;
		const RailLine& Run = Rails[size_t(BestLine)];
		if (Run.On[size_t(BestAt)] != 0) continue;
		Platform Made;
		Made.Line = BestLine;
		const int Reach = Halt ? 5 : 10;
		Made.First = BestAt;
		Made.Last = BestAt;
		while (Made.First > 0 && BestAt - Made.First < Reach && Run.On[size_t(Made.First - 1)] == 0) --Made.First;
		while (Made.Last + 1 < int(Run.At.size()) && Made.Last - BestAt < Reach && Run.On[size_t(Made.Last + 1)] == 0) ++Made.Last;
		if (Made.Last - Made.First < 3) continue;
		const size_t A = size_t(std::max(0, BestAt - 1)), B = std::min(Run.At.size() - 1, size_t(BestAt) + 1);
		const P2 Dir = Run.At[B] - Run.At[A];
		const P2 Off = P.At - Run.At[size_t(BestAt)];
		Made.Side = Dir.Z * Off.X - Dir.X * Off.Z >= 0.0 ? 1.0 : -1.0;   // (left of the way along: (Dir.Z, -Dir.X))
		Made.Name = P.Name;
		Platforms.push_back(Made);
	}
}

void RidePlan::RailTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out, RideMesh& Stone) const
{
	if (!Scene || Scene->RailBits.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const P3 Sky = At3(X0, 1e6, Z0);
	static const Rgb Ballast = {0.125f, 0.118f, 0.108f}, Trackbed = {0.07f, 0.075f, 0.04f}, Sleeper = {0.19f, 0.185f, 0.175f}, RailHead = {0.42f, 0.40f, 0.38f},
		RailWeb = {0.10f, 0.065f, 0.04f}, Girder = {0.06f, 0.065f, 0.07f}, Asphalt = {0.085f, 0.085f, 0.09f}, Coping = {0.32f, 0.31f, 0.29f},
		Warning = {0.55f, 0.42f, 0.02f}, Board = {0.008f, 0.04f, 0.22f}, PostGrey = {0.15f, 0.15f, 0.15f};
	// (the viaducts' stone, as the walls' masks: a little lichened, mostly the bare scan)
	Rgb Mask;
	Mask.R = 0.46f;
	Mask.G = 0.7f;
	Mask.B = 0.35f;
	// The way left (unit) across the line at a point, from its neighbours (so bends close).
	const auto LeftAt = [](const RailLine& Run, size_t I)
	{
		const size_t A = I > 0 ? I - 1 : I, B = I + 1 < Run.At.size() ? I + 1 : I;
		const P2 D = Run.At[B] - Run.At[A];
		const double Len = D.Length();
		return Len > 1e-9 ? P2(D.Z / Len, -D.X / Len) : P2(1.0, 0.0);
	};
	// A strip of the line from point I to I + 1, Off0 to Off1 across (+ left), Lift0 at the first
	// offset and Lift1 at the second over its top there.
	const auto Strip = [&](RideMesh& Into, const RailLine& Run, size_t I, double Off0, double Lift0, double Off1, double Lift1, const P3& Toward, const Rgb& C, float Moss)
	{
		const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
		const P2 A = Run.At[I], B = Run.At[I + 1];
		const double Ya = Run.Top[I], Yb = Run.Top[I + 1];
		Quad(Into, At3(A.X + La.X * Off0, Ya + Lift0, A.Z + La.Z * Off0), At3(B.X + Lb.X * Off0, Yb + Lift0, B.Z + Lb.Z * Off0), At3(B.X + Lb.X * Off1, Yb + Lift1, B.Z + Lb.Z * Off1),
			At3(A.X + La.X * Off1, Ya + Lift1, A.Z + La.Z * Off1), Toward, C, false, Moss);
	};
	// A point out to one side of the line at I (for a face to face it).
	const auto Beside = [&](const RailLine& Run, size_t I, double Off, double Lift)
	{
		const P2 L = LeftAt(Run, I);
		return At3(Run.At[I].X + L.X * Off, Run.Top[I] + Lift, Run.At[I].Z + L.Z * Off);
	};
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = Scene->RailBits.find(Key);
			if (It == Scene->RailBits.end()) continue;
			for (const std::pair<int, int>& Bit : It->second)
			{
				const RailLine& Run = Rails[size_t(Bit.first)];
				const size_t I = size_t(Bit.second);
				const P2 A = Run.At[I], B = Run.At[I + 1];
				if (!Inside((A + B) * 0.5) || A.DistanceTo(B) < 0.05) continue;
				const unsigned char Oa = Run.On[I], Ob = Run.On[I + 1];
				if (Oa == 2 || Ob == 2) continue;   // (in a tunnel)
				const bool Bridge = Oa == 1 || Ob == 1, Crossing = Oa == 3 || Ob == 3;
				const double Ya = Run.Top[I];
				if (Bridge) [&]()
				{
					// The deck, 4.6 m wide and 1.2 m deep, its parapets stone.
					Strip(Out, Run, I, -2.3, 0.0, 2.3, 0.0, Sky, Run.Disused ? Trackbed : Ballast, -1.0f);
					for (double Sg : {1.0, -1.0})
					{
						const P3 Away = Beside(Run, I, Sg * 50.0, 0.0), Inward = Beside(Run, I, -Sg * 50.0, 0.5);
						Strip(Stone, Run, I, Sg * 2.75, -1.2, Sg * 2.75, 1.0, Away, Mask, 0.3f);
						Strip(Stone, Run, I, Sg * 2.3, 0.0, Sg * 2.3, 1.0, Inward, Mask, 0.3f);
						Strip(Stone, Run, I, Sg * 2.3, 1.0, Sg * 2.75, 1.0, Sky, Mask, 0.5f);
					}
					// What holds it up: where it is high, stone piers every 16 m (arches between them on a
					// viaduct), and a stone abutment at each end.
					const double Under = Ground(A.X, A.Z);
					if (std::isnan(Under)) return;
					const double High = Ya - 1.2 - Under;
					size_t First = I;
					while (First > 0 && Run.On[First - 1] == 1) --First;
					const bool Viaduct = High > 9.0;
					const double Span = 4.0 * RailStep, Radius = Span * 0.5 - 1.0, Spring = Ya - 1.2 - 0.7 - Radius;
					const auto ArchAt = [&](double S)
					{
						// (the arch's underside S metres from a pier's middle: the pier's top within a metre of it)
						const double U = S - Span * 0.5;
						return S < 1.0 || S > Span - 1.0 ? Spring : Spring + std::sqrt(std::fmax(0.0, Radius * Radius - U * U));
					};
					const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
					if (Viaduct)
					{
						// (four slices of the arch per bit: each a spandrel face each side down to the arch, and its soffit)
						const double S0 = double((I - First) % 4) * RailStep;
						for (int Slice = 0; Slice < 4; ++Slice)
						{
							const double T0 = Slice / 4.0, T1 = (Slice + 1) / 4.0;
							const double Sa = S0 + T0 * RailStep, Sb = S0 + T1 * RailStep;
							const P2 Pa = A + (B - A) * T0, Pb = A + (B - A) * T1;
							const P2 Lx = La + (Lb - La) * ((T0 + T1) * 0.5);
							const double Da = Ya + (Run.Top[I + 1] - Ya) * T0 - 1.2, Db = Ya + (Run.Top[I + 1] - Ya) * T1 - 1.2;
							for (double Sg : {1.0, -1.0})
							{
								const P3 Away = At3(Pa.X + Lx.X * Sg * 50.0, Da, Pa.Z + Lx.Z * Sg * 50.0);
								Quad(Stone, At3(Pa.X + Lx.X * Sg * 2.75, ArchAt(Sa), Pa.Z + Lx.Z * Sg * 2.75), At3(Pb.X + Lx.X * Sg * 2.75, ArchAt(Sb), Pb.Z + Lx.Z * Sg * 2.75),
									At3(Pb.X + Lx.X * Sg * 2.75, Db, Pb.Z + Lx.Z * Sg * 2.75), At3(Pa.X + Lx.X * Sg * 2.75, Da, Pa.Z + Lx.Z * Sg * 2.75), Away, Mask, false, 0.35f);
							}
							Quad(Stone, At3(Pa.X + Lx.X * 2.75, ArchAt(Sa), Pa.Z + Lx.Z * 2.75), At3(Pb.X + Lx.X * 2.75, ArchAt(Sb), Pb.Z + Lx.Z * 2.75),
								At3(Pb.X - Lx.X * 2.75, ArchAt(Sb), Pb.Z - Lx.Z * 2.75), At3(Pa.X - Lx.X * 2.75, ArchAt(Sa), Pa.Z - Lx.Z * 2.75), At3(Pa.X, -1e6, Pa.Z), Shade(Mask, 0.8), false, 0.5f);
						}
					}
					else if (High > 0.5)
					{
						// (a plain deck's sides and underside)
						for (double Sg : {1.0, -1.0}) Strip(Out, Run, I, Sg * 2.8, -1.25, Sg * 2.8, -0.2, Beside(Run, I, Sg * 50.0, 0.0), Girder, -1.0f);
						Strip(Out, Run, I, -2.8, -1.25, 2.8, -1.25, Beside(Run, I, 0.0, -100.0), Girder, -1.0f);
					}
					// A pier (every 16 m from the bridge's start, not on a road) or an abutment (where it meets the ground).
					const bool Abutment = Oa != 1 || Ob != 1;
					const bool Pier = Oa == 1 && (I - First) % 4 == 0 && High > 2.5;
					if (!Abutment && !Pier) return;
					NearOpts Opts;
					NearHit Hit;
					if (!Abutment && NearestOf(A.X, A.Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.5) return;
					const P2 Along = (B - A) * (1.0 / A.DistanceTo(B));
					const double Thick = Abutment ? 0.9 : 1.0, Wide = Abutment ? 3.6 : 2.6;
					const double TopY = Abutment ? Ya - 0.3 : Viaduct ? Spring + 0.01 : Ya - 1.2;
					const P2 C = Oa != 1 ? B : A;   // (an abutment where the deck meets the ground)
					const double Foot = Abutment ? Ground(C.X, C.Z) : Under;
					if (std::isnan(Foot)) return;
					const P3 Mid3 = At3(C.X, (Foot + TopY) * 0.5, C.Z);
					const auto Corner = [&](double Su, double Sv, double Y) { return At3(C.X + Along.X * Su * Thick + La.X * Sv * Wide, Y, C.Z + Along.Z * Su * Thick + La.Z * Sv * Wide); };
					const double S4[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
					for (int K = 0; K < 4; ++K)
					{
						const P3 Q0 = Corner(S4[K][0], S4[K][1], Foot - 1.0), Q1 = Corner(S4[K + 1][0], S4[K + 1][1], Foot - 1.0), Q2 = Corner(S4[K + 1][0], S4[K + 1][1], TopY),
							Q3 = Corner(S4[K][0], S4[K][1], TopY);
						const P3 Away = At3(Mid3.X + ((Q0.X + Q2.X) * 0.5 - Mid3.X) * 50.0, Mid3.Y, Mid3.Z + ((Q0.Z + Q2.Z) * 0.5 - Mid3.Z) * 50.0);
						Quad(Stone, Q0, Q1, Q2, Q3, Away, Mask, false, 0.3f);
					}
				}();
				if (Run.Disused) continue;   // (an old line: its cuttings and banks in the land, no track)
				if (!Crossing && !Bridge)
				{
					// The ballast: its top 3.2 m wide, its shoulders down to the cess.
					Strip(Out, Run, I, -1.6, 0.0, 1.6, 0.0, Sky, Shade(Ballast, 0.9 + 0.2 * Chance(A.X, A.Z, 41)), -1.0f);
					Strip(Out, Run, I, 1.6, 0.0, 2.25, -0.47, Sky, Shade(Ballast, 0.85), -1.0f);
					Strip(Out, Run, I, -2.25, -0.47, -1.6, 0.0, Sky, Shade(Ballast, 0.85), -1.0f);
				}
				if (!Fine) continue;
				const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
				const double Yb = Run.Top[I + 1];
				const double Len = A.DistanceTo(B);
				if (!Crossing)
				{
					// Concrete sleepers every 0.65 m.
					const int Count = std::max(1, int(std::lround(Len / 0.65)));
					const P2 Along = (B - A) * (1.0 / Len);
					for (int K = 0; K < Count; ++K)
					{
						const double T = (K + 0.5) / Count;
						const P2 P = A + (B - A) * T, L = La + (Lb - La) * T;
						const double Y = Ya + (Yb - Ya) * T;
						const P2 F = Along * 0.13;
						const Rgb C = Shade(Sleeper, 0.9 + 0.2 * Chance(P.X, P.Z, 42));
						const auto At2 = [&](double Across, double Fore, double Lift) { return At3(P.X + L.X * Across + F.X * Fore, Y + Lift, P.Z + L.Z * Across + F.Z * Fore); };
						Quad(Out, At2(-1.3, -1.0, 0.12), At2(1.3, -1.0, 0.12), At2(1.3, 1.0, 0.12), At2(-1.3, 1.0, 0.12), Sky, C);
						Quad(Out, At2(-1.3, 1.0, -0.02), At2(1.3, 1.0, -0.02), At2(1.3, 1.0, 0.12), At2(-1.3, 1.0, 0.12), At2(0.0, 30.0, 0.05), Shade(C, 0.8));
						Quad(Out, At2(-1.3, -1.0, -0.02), At2(1.3, -1.0, -0.02), At2(1.3, -1.0, 0.12), At2(-1.3, -1.0, 0.12), At2(0.0, -30.0, 0.05), Shade(C, 0.8));
					}
				}
				// The rails: a head and two sides each.
				for (double Sg : {1.0, -1.0})
				{
					const double Mid = Sg * (RailGauge * 0.5 + 0.035);
					Strip(Out, Run, I, Mid - 0.035, 0.27, Mid + 0.035, 0.27, Sky, RailHead, -1.0f);
					Strip(Out, Run, I, Mid + 0.035, 0.12, Mid + 0.035, 0.27, Beside(Run, I, Mid + 30.0, 0.2), RailWeb, -1.0f);
					Strip(Out, Run, I, Mid - 0.035, 0.12, Mid - 0.035, 0.27, Beside(Run, I, Mid - 30.0, 0.2), RailWeb, -1.0f);
				}
			}
		}
	// The stations: a platform beside the line, its coping and yellow line, and its name board.
	for (const Platform& Plat : Platforms)
	{
		const RailLine& Run = Rails[size_t(Plat.Line)];
		const int Middle = (Plat.First + Plat.Last) / 2;
		if (!Inside(Run.At[size_t(Middle)])) continue;
		const double S = Plat.Side, Edge = 1.6, Back = Edge + 4.0, Lift = 1.18;
		for (int K = Plat.First; K < Plat.Last; ++K)
		{
			const size_t I = size_t(K);
			Strip(Out, Run, I, S * Edge, -0.4, S * Edge, Lift, Beside(Run, I, 0.0, 0.5), Shade(Coping, 0.7), -1.0f);
			Strip(Out, Run, I, S * Edge, Lift, S * (Edge + 0.6), Lift, Sky, Coping, -1.0f);
			Strip(Out, Run, I, S * (Edge + 0.6), Lift, S * Back, Lift, Sky, Asphalt, -1.0f);
			Strip(Out, Run, I, S * (Edge + 0.68), Lift + 0.006, S * (Edge + 0.78), Lift + 0.006, Sky, Warning, -1.0f);
			Strip(Out, Run, I, S * Back, -1.5, S * Back, Lift, Beside(Run, I, S * 50.0, 0.0), Shade(Coping, 0.6), -1.0f);
		}
		// (the platform's two ends)
		for (int End : {Plat.First, Plat.Last})
		{
			const size_t I = size_t(End);
			const P3 Q0 = Beside(Run, I, S * Edge, -0.4), Q1 = Beside(Run, I, S * Back, -1.5), Q2 = Beside(Run, I, S * Back, Lift), Q3 = Beside(Run, I, S * Edge, Lift);
			const size_t Other = End == Plat.First ? I + 1 : I - 1;
			const P3 Out3 = At3(Run.At[I].X * 2.0 - Run.At[Other].X, Run.Top[I] + Lift, Run.At[I].Z * 2.0 - Run.At[Other].Z);
			Quad(Out, Q0, Q1, Q2, Q3, Out3, Shade(Coping, 0.65));
		}
		if (Plat.Name.empty()) continue;
		// The name board: white words on blue, on two posts at the back of the platform, facing the line.
		const size_t I = size_t(Middle);
		const P2 L = LeftAt(Run, I);
		const P2 Face(-L.X * S, -L.Z * S);
		const P2 Along(L.Z, -L.X);   // (along the line, against the way it runs)
		const double Wide = 0.8 + 0.19 * double(Plat.Name.size()), Base = Run.Top[I] + Lift;
		const P2 C = Run.At[I] + L * (S * (Back - 0.6));
		for (double Sg : {1.0, -1.0}) Box(Out, C.X + Along.X * Sg * (Wide * 0.5 - 0.1), C.Z + Along.Z * Sg * (Wide * 0.5 - 0.1), Along, 0.04, 0.04, Base - 0.1, Base + 2.4, PostGrey);
		const P2 Half = Along * (Wide * 0.5);
		const P2 Proud = Face * 0.05;
		const double Lo = Base + 1.75, Hi = Base + 2.35;
		Quad(Out, At3(C.X - Half.X + Proud.X, Lo, C.Z - Half.Z + Proud.Z), At3(C.X + Half.X + Proud.X, Lo, C.Z + Half.Z + Proud.Z), At3(C.X + Half.X + Proud.X, Hi, C.Z + Half.Z + Proud.Z),
			At3(C.X - Half.X + Proud.X, Hi, C.Z - Half.Z + Proud.Z), At3(C.X + Face.X * 50.0, (Lo + Hi) * 0.5, C.Z + Face.Z * 50.0), Board);
		Quad(Out, At3(C.X - Half.X, Lo, C.Z - Half.Z), At3(C.X + Half.X, Lo, C.Z + Half.Z), At3(C.X + Half.X, Hi, C.Z + Half.Z), At3(C.X - Half.X, Hi, C.Z - Half.Z),
			At3(C.X - Face.X * 50.0, (Lo + Hi) * 0.5, C.Z - Face.Z * 50.0), PostGrey);
		RideMesh::Label Words;
		Words.X = float(C.X + Face.X * 0.07 - Out.OriginX);
		Words.Y = float((Lo + Hi) * 0.5);
		Words.Z = float(C.Z + Face.Z * 0.07 - Out.OriginZ);
		Words.Nx = float(Face.X);
		Words.Nz = float(Face.Z);
		Words.Size = 0.3f;
		Words.R = Words.G = Words.B = 0.9f;
		Words.Text = Plat.Name;
		Out.Labels.push_back(Words);
	}
}
} // namespace TorqueWorld
