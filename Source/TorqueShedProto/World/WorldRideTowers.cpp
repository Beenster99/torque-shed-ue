// Towns stage 2: the skyline already in the data. The baker keeps every large or tall building (the
// "towers": Leeds' office blocks, York Minster's nave and towers as parts, retail sheds, schools,
// hospitals, stadiums) as its true outline with a height, a class and sometimes a look; here each is
// pushed up to its height, its walls by what it is (glass and spandrels on an office tower, brick or
// panel with windows a floor on flats and schools, metal cladding on a shed, stone and tall pointed
// windows on a church), a flat roof behind a parapet or, on a church's plain oblong, a pitched one.
// The tall ones (40 m and more) stand far off too, plain. A box building under one is not drawn.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace TorqueWorld
{
namespace RideTowerBits
{
using RideSceneBits::P3;
using RideSceneBits::At3;
using RideSceneBits::Rgb;
using RideSceneBits::Shade;
using RideSceneBits::Chance;
using RideSceneBits::Quad;
using RideSceneBits::BuildingTagColour;
constexpr double TowerCell = 256.0;

long long TowerKey(int Cx, int Cz)
{
	return static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
}

// Douglas-Peucker on an open run of points: keeps the ends and every point more than Tol off the line.
void Keep(const std::vector<P2>& Pts, size_t A, size_t B, double Tol, std::vector<char>& Kept)
{
	if (B <= A + 1) return;
	const P2 U = Pts[B] - Pts[A];
	const double L = std::hypot(U.X, U.Z);
	double Worst = -1.0;
	size_t At = A;
	for (size_t I = A + 1; I < B; ++I)
	{
		const P2 V = Pts[I] - Pts[A];
		const double D = L > 1e-9 ? std::fabs(U.X * V.Z - U.Z * V.X) / L : std::hypot(V.X, V.Z);
		if (D > Worst) { Worst = D; At = I; }
	}
	if (Worst <= Tol) return;
	Kept[At] = 1;
	Keep(Pts, A, At, Tol, Kept);
	Keep(Pts, At, B, Tol, Kept);
}

// A ring (closed or not) simplified to Most corners or fewer, by a growing tolerance (up to 6 m).
std::vector<P2> Simplify(std::vector<P2> Ring, size_t Most)
{
	if (Ring.size() >= 2 && Ring.front().DistanceTo(Ring.back()) < 0.01) Ring.pop_back();
	if (Ring.size() <= Most) return Ring;
	// (split at the point farthest from the first, so both halves are open runs)
	size_t Far = 0;
	double FarD = -1.0;
	for (size_t I = 0; I < Ring.size(); ++I)
		if (Ring[I].DistanceTo(Ring[0]) > FarD) { FarD = Ring[I].DistanceTo(Ring[0]); Far = I; }
	std::vector<P2> Loop = Ring;
	Loop.push_back(Ring[0]);
	std::vector<P2> Out;
	for (double Tol = 0.5; Tol <= 6.0; Tol *= 1.5)
	{
		std::vector<char> Kept(Loop.size(), 0);
		Kept[0] = Kept[Far] = Kept[Loop.size() - 1] = 1;
		Keep(Loop, 0, Far, Tol, Kept);
		Keep(Loop, Far, Loop.size() - 1, Tol, Kept);
		Out.clear();
		for (size_t I = 0; I + 1 < Loop.size(); ++I)
			if (Kept[I]) Out.push_back(Loop[I]);
		if (Out.size() <= Most) break;
	}
	return Out;
}

double Signed(const std::vector<P2>& Ring)
{
	double A = 0.0;
	for (size_t I = 0; I < Ring.size(); ++I)
	{
		const P2& P = Ring[I];
		const P2& Q = Ring[(I + 1) % Ring.size()];
		A += P.X * Q.Z - Q.X * P.Z;
	}
	return A * 0.5;
}

// Ear clipping: the ring's triangles, as index triples (any winding; Quad turns each face up).
std::vector<int> Triangles(const std::vector<P2>& Ring)
{
	std::vector<int> Out, Left;
	const int N = int(Ring.size());
	if (N < 3) return Out;
	for (int I = 0; I < N; ++I) Left.push_back(I);
	const double Turn = Signed(Ring) > 0.0 ? 1.0 : -1.0;
	const auto Cross = [&Ring](int A, int B, int C)
	{
		const P2 U = Ring[size_t(B)] - Ring[size_t(A)], V = Ring[size_t(C)] - Ring[size_t(A)];
		return U.X * V.Z - U.Z * V.X;
	};
	int Guard = 0;
	while (Left.size() > 3 && Guard++ < N * N)
	{
		bool Cut = false;
		for (size_t K = 0; K < Left.size(); ++K)
		{
			const int A = Left[(K + Left.size() - 1) % Left.size()], B = Left[K], C = Left[(K + 1) % Left.size()];
			if (Cross(A, B, C) * Turn <= 1e-9) continue;   // (a reflex corner: not an ear)
			bool Clear = true;
			for (int P : Left)
			{
				if (P == A || P == B || P == C) continue;
				if (Cross(A, B, P) * Turn > 0.0 && Cross(B, C, P) * Turn > 0.0 && Cross(C, A, P) * Turn > 0.0) { Clear = false; break; }
			}
			if (!Clear) continue;
			Out.insert(Out.end(), {A, B, C});
			Left.erase(Left.begin() + std::ptrdiff_t(K));
			Cut = true;
			break;
		}
		if (!Cut) break;   // (a ring that crosses itself: what is left goes as a fan)
	}
	for (size_t K = 1; K + 1 < Left.size(); ++K) Out.insert(Out.end(), {Left[0], Left[K], Left[K + 1]});
	return Out;
}

bool Among(const std::string& W, std::initializer_list<const char*> List)
{
	for (const char* S : List)
		if (W == S) return true;
	return false;
}
} // namespace RideTowerBits

void RidePlan::IndexTowers()
{
	using namespace RideTowerBits;
	TowerShapes.clear();
	TowersGuessed = TowersOutlines = 0;
	if (!Scene) return;
	const Region& G = Area.Roads;
	for (const FeatureArea& A : G.Scenery.Areas)
	{
		if ((A.Kind != "tower" && A.Kind != "skyscraper") || A.Ring.size() < 3) continue;
		TowerShape T;
		T.Ring = Simplify(A.Ring, 16);
		if (T.Ring.size() < 3) continue;
		P2 Mid;
		T.Floor = std::fabs(RideShapes::RingArea(T.Ring, Mid));
		T.Mid = Mid;
		if (T.Floor < 20.0) continue;
		T.Name = A.Name;
		T.Id = A.Id;
		T.Group = A.Group;
		const auto Word = [&A](size_t K) { return K < A.Look.size() ? A.Look[K] : std::string(); };
		T.Class = Word(0);
		T.RoofShape = Word(1);
		T.RoofColour = Word(2);
		T.RoofMaterial = Word(3);
		T.WallMaterial = Word(4);
		T.WallColour = Word(5);
		T.Base = std::fmax(0.0, A.Elevation);
		T.Top = A.Height;
		if (!(T.Top > T.Base + 0.5))   // (none mapped: NaN or nothing)
		{
			// (no height mapped: one from what it is and how big, until stage 3 measures them)
			const std::string& C = T.Class;
			const double Big = std::sqrt(T.Floor);
			T.Top = Among(C, {"industrial", "warehouse", "retail", "supermarket", "manufacture", "farm_auxiliary", "agricultural", "transportation", "garage"}) ? std::fmin(12.0, 6.0 + Big * 0.04)
				: Among(C, {"school", "college"}) ? 9.0 : Among(C, {"university", "hospital"}) ? 14.0 : Among(C, {"apartments", "residential", "dormitory", "hotel"}) ? 12.0
				: Among(C, {"office", "commercial", "civic", "public"}) ? 13.0 : Among(C, {"church", "chapel"}) ? 11.0 : C == "cathedral" ? 20.0 : C == "parking" ? 10.0
				: Among(C, {"stadium", "sports_centre", "grandstand"}) ? 14.0 : 8.0;
			T.Top += T.Base;
			T.Guessed = true;
			++TowersGuessed;
		}
		TowerShapes.push_back(std::move(T));
	}
	// (a building drawn as its parts: its outline is not drawn, only kept for the box buildings under it)
	std::set<std::string> HasParts;
	for (const TowerShape& T : TowerShapes)
		if (!T.Group.empty() && T.Group != T.Id) HasParts.insert(T.Group);
	for (TowerShape& T : TowerShapes)
		if (HasParts.count(T.Id) != 0)
		{
			T.Outline = true;
			++TowersOutlines;
		}
	// (each building on its lowest ground, all its parts on the same)
	std::unordered_map<std::string, double> Lowest;
	for (TowerShape& T : TowerShapes)
	{
		double Low = 1e9;
		for (const P2& P : T.Ring)
		{
			const double Y = Ground(P.X, P.Z);
			if (!std::isnan(Y)) Low = std::fmin(Low, Y);
		}
		T.Ground = Low;
		const std::string Key = T.Group.empty() ? T.Id : T.Group;
		auto It = Lowest.find(Key);
		if (It == Lowest.end()) Lowest[Key] = Low;
		else It->second = std::fmin(It->second, Low);
	}
	for (size_t I = 0; I < TowerShapes.size(); ++I)
	{
		TowerShape& T = TowerShapes[I];
		T.Ground = Lowest[T.Group.empty() ? T.Id : T.Group];
		if (T.Ground > 1e8) continue;
		double X0 = 1e18, Z0 = 1e18, X1 = -1e18, Z1 = -1e18;
		for (const P2& P : T.Ring) { X0 = std::fmin(X0, P.X); Z0 = std::fmin(Z0, P.Z); X1 = std::fmax(X1, P.X); Z1 = std::fmax(Z1, P.Z); }
		for (int Cz = int(std::floor(Z0 / TowerCell)); Cz <= int(std::floor(Z1 / TowerCell)); ++Cz)
			for (int Cx = int(std::floor(X0 / TowerCell)); Cx <= int(std::floor(X1 / TowerCell)); ++Cx)
				Scene->Towers[TowerKey(Cx, Cz)].push_back(int(I));
	}
}

bool RidePlan::UnderTower(double X, double Z) const
{
	using namespace RideTowerBits;
	if (!Scene) return false;
	const auto It = Scene->Towers.find(TowerKey(int(std::floor(X / TowerCell)), int(std::floor(Z / TowerCell))));
	if (It == Scene->Towers.end()) return false;
	for (int I : It->second)
	{
		const TowerShape& T = TowerShapes[size_t(I)];
		if (T.Base < 2.0 && RideShapes::InRing(T.Ring, X, Z)) return true;
	}
	return false;
}

void RidePlan::TowersTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	using namespace RideTowerBits;
	if (!Scene || TowerShapes.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	// (far off only the tall ones: 40 m and over, then 80 m and over)
	const double Tallest = Detail >= 1 ? 0.0 : Detail == 0 ? 40.0 : 80.0;
	std::set<int> Done;
	for (int Cz = int(std::floor(Z0 / TowerCell)); Cz <= int(std::floor((Z0 + Size - 0.001) / TowerCell)); ++Cz)
		for (int Cx = int(std::floor(X0 / TowerCell)); Cx <= int(std::floor((X0 + Size - 0.001) / TowerCell)); ++Cx)
		{
			const auto It = Scene->Towers.find(TowerKey(Cx, Cz));
			if (It == Scene->Towers.end()) continue;
			for (int I : It->second)
			{
				const TowerShape& T = TowerShapes[size_t(I)];
				if (T.Outline || !Inside(T.Mid) || T.Top < Tallest || !Done.insert(I).second) continue;
				const double Foot = T.Ground + T.Base - (T.Base < 0.5 ? 1.0 : 0.0), Top = T.Ground + T.Top;
				if (Top - Foot < 1.5) continue;
				const std::string& C = T.Class;
				const LocalLook& Look = LookHere(T.Mid.X, T.Mid.Z);
				const bool Worship = Among(C, {"church", "cathedral", "chapel", "mosque", "temple", "synagogue", "religious"});
				const bool Shed = Among(C, {"industrial", "warehouse", "retail", "supermarket", "manufacture", "farm_auxiliary", "agricultural", "transportation", "garage", "sports_centre", "stadium", "grandstand"});
				const bool Parking = C == "parking";
				const bool Glassy = T.WallMaterial == "glass" || (!Worship && !Shed && !Parking && T.Top >= 40.0 && !Among(C, {"apartments", "residential", "dormitory"}) && Chance(T.Mid.X, T.Mid.Z, 81) < 0.7)
					|| (Among(C, {"office", "commercial"}) && T.Top >= 25.0);
				const double Salt = Chance(T.Mid.X, T.Mid.Z, 80);
				// Its walls' colour: the map's, else by what it is.
				Rgb Wall;
				static const Rgb StoneWall = {0.30f, 0.28f, 0.245f}, Panel = {0.36f, 0.35f, 0.33f}, GlassWall = {0.045f, 0.055f, 0.065f};
				const Rgb Clad[3] = {{0.30f, 0.31f, 0.30f}, {0.20f, 0.24f, 0.27f}, {0.38f, 0.35f, 0.29f}};
				const Rgb Bricks = {Look.BrickColour.R, Look.BrickColour.G, Look.BrickColour.B};
				const Rgb Stone = {StoneWall.R * Look.HouseStone.R, StoneWall.G * Look.HouseStone.G, StoneWall.B * Look.HouseStone.B};
				const bool Brick = T.WallMaterial == "brick" || (T.WallMaterial.empty() && !Glassy && !Shed && !Worship && !Parking && Salt < 0.55);
				if (BuildingTagColour(T.WallColour, Wall)) {}
				else if (Glassy) Wall = GlassWall;
				else if (Worship || T.WallMaterial == "stone" || T.WallMaterial == "sandstone" || T.WallMaterial == "limestone") Wall = Stone;
				else if (Shed || T.WallMaterial == "metal") Wall = Clad[int(Salt * 2.999)];
				else if (Brick) Wall = Bricks;
				else Wall = Panel;
				Wall = Shade(Wall, 0.9 + 0.2 * Chance(T.Mid.X, T.Mid.Z, 82));
				const double Turn = Signed(T.Ring) > 0.0 ? 1.0 : -1.0;
				// (a pitched roof: a church's or a mapped gable's, on a plain oblong only)
				const bool Oblong = T.Ring.size() == 4;
				const bool Pitched = Oblong && Detail >= 0 && T.RoofShape != "flat" && (Worship || T.RoofShape == "gabled" || T.RoofShape == "hipped" || T.RoofShape == "pitched");
				double Long = 0.0;
				int LongSide = 0;
				for (int K = 0; K < int(T.Ring.size()); ++K)
				{
					const double L = T.Ring[size_t(K)].DistanceTo(T.Ring[(size_t(K) + 1) % T.Ring.size()]);
					if (L > Long) { Long = L; LongSide = K; }
				}
				const double Short = Oblong ? T.Ring[size_t(LongSide + 1) % 4].DistanceTo(T.Ring[size_t(LongSide + 2) % 4]) : 0.0;
				const double Rise = Pitched ? std::fmin(Short * 0.5 * (Worship ? 1.0 : 0.7), std::fmax(0.0, (Top - Foot) * 0.45)) : 0.0;
				const double Eaves = Top - Rise;
				const double Floor = Glassy || Among(C, {"office", "commercial"}) ? 3.8 : 3.1;
				const int Floors = std::max(1, int((Eaves - (T.Ground + T.Base) - 0.5) / Floor));
				const bool Fine = Detail >= 2;
				int Glass = 0;
				for (size_t K = 0; K < T.Ring.size(); ++K)
				{
					const P2 A = T.Ring[K], B = T.Ring[(K + 1) % T.Ring.size()];
					const double L = A.DistanceTo(B);
					if (L < 0.05) continue;
					const P2 U = (B - A) * (1.0 / L);
					const P2 Nout = P2(U.Z, -U.X) * Turn;   // (outward)
					const P3 Away = At3((A.X + B.X) * 0.5 + Nout.X * 50.0, (Foot + Eaves) * 0.5, (A.Z + B.Z) * 0.5 + Nout.Z * 50.0);
					const double By = 0.82 + 0.18 * std::fabs(Nout.X * 0.6 + Nout.Z * 0.8);
					Quad(Out, At3(A.X, Foot, A.Z), At3(B.X, Foot, B.Z), At3(B.X, Eaves, B.Z), At3(A.X, Eaves, A.Z), Away, Shade(Wall, By));
					// (a church's gable ends up to its ridge)
					if (Pitched && (int(K) % 2) != (LongSide % 2))
					{
						const P2 M = (A + B) * 0.5;
						Quad(Out, At3(A.X, Eaves, A.Z), At3(B.X, Eaves, B.Z), At3(M.X, Top, M.Z), At3(A.X, Eaves, A.Z), Away, Shade(Wall, By));
					}
					if (!Fine || L < 2.5) continue;
					// The openings on this wall, a little proud of it.
					const auto Pane = [&](double U0, double U1, double Lo, double Hi, const Rgb& Col, double Proud)
					{
						const P2 Pa = A + U * U0 + Nout * Proud, Pb = A + U * U1 + Nout * Proud;
						Quad(Out, At3(Pa.X, Lo, Pa.Z), At3(Pb.X, Lo, Pb.Z), At3(Pb.X, Hi, Pb.Z), At3(Pa.X, Hi, Pa.Z), Away, Col);
					};
					const auto Lit = [&](double U0, double U1, double Lo, double Hi, double Share, int Salt2)
					{
						const P2 Pm = A + U * ((U0 + U1) * 0.5);
						if (Chance(Pm.X, Pm.Z + Lo, Salt2) >= Share) return;
						const P2 Pa = A + U * U0 + Nout * 0.08, Pb = A + U * U1 + Nout * 0.08;
						static const Rgb Warm = {1.0f, 0.78f, 0.5f};
						Quad(Out.NightOf(), At3(Pa.X, Lo, Pa.Z), At3(Pb.X, Lo, Pb.Z), At3(Pb.X, Hi, Pb.Z), At3(Pa.X, Hi, Pa.Z), Away, Warm, false, 0.0f);
					};
					static const Rgb Pane1 = {0.03f, 0.035f, 0.04f}, Spandrel = {0.22f, 0.23f, 0.24f};
					const double Ground0 = T.Ground + T.Base;
					if (Glassy)
					{
						// A curtain wall: a lighter spandrel band at each floor, the glass between.
						for (int Fl = 1; Fl <= Floors && Glass < 4000; ++Fl, ++Glass)
						{
							const double Y = Ground0 + Fl * Floor;
							if (Y > Eaves - 0.3) break;
							Pane(0.0, L, Y - 0.45, Y + 0.35, Spandrel, 0.03);
							Lit(0.0, L, Y - 3.2, Y - 0.6, 0.25, 83);
						}
					}
					else if (Worship)
					{
						// Tall pointed windows down its sides.
						for (double Uw = 2.0; Uw < L - 1.5; Uw += 4.0)
						{
							const double Lo = Ground0 + 1.5, Hi = std::fmin(Eaves - 1.0, Ground0 + 8.0);
							if (Hi - Lo < 1.5) break;
							Pane(Uw - 0.5, Uw + 0.5, Lo, Hi, Pane1, 0.04);
							const P2 Pa = A + U * (Uw - 0.5) + Nout * 0.04, Pb = A + U * (Uw + 0.5) + Nout * 0.04, Pm = A + U * Uw + Nout * 0.04;
							Quad(Out, At3(Pa.X, Hi, Pa.Z), At3(Pb.X, Hi, Pb.Z), At3(Pm.X, Hi + 0.7, Pm.Z), At3(Pa.X, Hi, Pa.Z), Away, Pane1);
						}
					}
					else if (Shed)
					{
						// Cladding: a roller door on its longest wall, a band of rooflights' glow along the top.
						if (int(K) == LongSide && Eaves - Ground0 > 4.0)
							for (double Ud = L * 0.25; Ud < L - 3.0; Ud += std::fmax(12.0, L * 0.5))
								Pane(Ud - 2.0, Ud + 2.0, Ground0, Ground0 + 4.0, Rgb{0.2f, 0.21f, 0.22f}, 0.03);
						Pane(0.0, L, Eaves - 0.5, Eaves - 0.2, Shade(Wall, 0.7), 0.02);
					}
					else if (Parking)
					{
						// A car park: open decks, dark between their edges.
						for (int Fl = 0; Fl < Floors; ++Fl)
						{
							const double Y = Ground0 + Fl * 3.0;
							if (Y + 2.9 > Eaves) break;
							Pane(0.0, L, Y + 1.1, Y + 2.9, Rgb{0.02f, 0.02f, 0.02f}, 0.02);
						}
					}
					else
					{
						// Flats, schools, hospitals, offices in brick or panel: a window every 2.6 m on every floor
						// (as many as a building can have before it is a band instead).
						const int Across = std::max(1, int(L / 2.6));
						const bool Bands = Across * Floors > 900;
						for (int Fl = 0; Fl < Floors; ++Fl)
						{
							const double Lo = Ground0 + Fl * Floor + 0.9, Hi = Lo + Floor * 0.5;
							if (Hi > Eaves - 0.2) break;
							if (Bands) { Pane(0.3, L - 0.3, Lo, Hi, Pane1, 0.03); continue; }
							for (int W = 0; W < Across; ++W)
							{
								const double Uc = (W + 0.5) * L / Across;
								Pane(Uc - 0.6, Uc + 0.6, Lo, Hi, Pane1, 0.03);
								Lit(Uc - 0.55, Uc + 0.55, Lo + 0.05, Hi - 0.05, Among(C, {"apartments", "residential", "dormitory", "hotel"}) ? 0.45 : 0.2, 84);
							}
						}
					}
				}
				// The roof: pitched on a church's oblong, else flat (a parapet's coping round it near).
				static const Rgb Felt = {0.07f, 0.07f, 0.075f}, Leaden = {0.18f, 0.19f, 0.2f};
				Rgb RoofCol = Pitched ? (Worship ? Rgb{0.085f, 0.085f, 0.095f} : Rgb{Look.TileColour.R, Look.TileColour.G, Look.TileColour.B}) : Shed ? Shade(Wall, 0.85) : Glassy ? Leaden : Felt;
				BuildingTagColour(T.RoofColour, RoofCol);
				const P3 Sky = At3(T.Mid.X, 1e6, T.Mid.Z);
				if (Pitched)
				{
					const size_t S0 = size_t(LongSide), S1 = (S0 + 1) % 4, S2 = (S0 + 2) % 4, S3 = (S0 + 3) % 4;
					const P2 Ra = (T.Ring[S0] + T.Ring[S3]) * 0.5, Rb = (T.Ring[S1] + T.Ring[S2]) * 0.5;
					Quad(Out, At3(T.Ring[S0].X, Eaves, T.Ring[S0].Z), At3(T.Ring[S1].X, Eaves, T.Ring[S1].Z), At3(Rb.X, Top, Rb.Z), At3(Ra.X, Top, Ra.Z), Sky, RoofCol);
					Quad(Out, At3(T.Ring[S2].X, Eaves, T.Ring[S2].Z), At3(T.Ring[S3].X, Eaves, T.Ring[S3].Z), At3(Ra.X, Top, Ra.Z), At3(Rb.X, Top, Rb.Z), Sky, Shade(RoofCol, 0.88));
				}
				else
				{
					const std::vector<int> Tris = Triangles(T.Ring);
					for (size_t K = 0; K + 2 < Tris.size(); K += 3)
					{
						const P2& Pa = T.Ring[size_t(Tris[K])];
						const P2& Pb = T.Ring[size_t(Tris[K + 1])];
						const P2& Pc = T.Ring[size_t(Tris[K + 2])];
						Quad(Out, At3(Pa.X, Top, Pa.Z), At3(Pb.X, Top, Pb.Z), At3(Pc.X, Top, Pc.Z), At3(Pa.X, Top, Pa.Z), Sky, RoofCol);
					}
					if (Fine && !Shed)
						for (size_t K = 0; K < T.Ring.size(); ++K)
						{
							const P2 A = T.Ring[K], B = T.Ring[(K + 1) % T.Ring.size()];
							const double L = A.DistanceTo(B);
							if (L < 0.5) continue;
							const P2 Mid = (A + B) * 0.5;
							RideSceneBits::Box(Out, Mid.X, Mid.Z, (B - A) * (1.0 / L), L * 0.5, 0.15, Top - 0.05, Top + 0.8, Shade(Wall, 0.8));
						}
				}
			}
		}
}
} // namespace TorqueWorld
