// [car parks] The map's car parks (beenster, Nov 18: "start to model car parks"). Each one of the ground file's
// car_park areas becomes:
//  - its surface laid over the ground in M_RideRoad's colours (tarmac, setts or gravel), a few cm up, with its
//    bays' lines painted INTO it: the surface is cut where each line runs and that piece drawn in the paint's
//    colour, flush, as the roads' own lines are (performance first: no strips laid over it, nothing drawn twice);
//  - its bays in rows along its aisles: a service road through it sets the aisles (its line an aisle's middle),
//    else its longest side does; each bay clear of roads, buildings, water and the edge; the two by the way in
//    are blue badge bays, lined yellow;
//  - low concrete kerbs round its edge where no road runs by (none on gravel);
//  - a blue P sign and a pay machine at its way in, and lamps between its back-to-back rows when it is big;
//  - parked cars in about a third of its bays (the Fab cars, PropParkedCar), solid to the bike: the game
//    asks ParkedCarsNear for the ones round the rider and hands them to the sim as contact boxes.
// Only on the finest squares (as the paths): farther off the land's own car park tint stands for it.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideSigns.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace TorqueWorld
{
struct RidePlan::CarParkMemo
{
	std::mutex Lock;
	std::unordered_map<int, std::shared_ptr<const std::vector<RidePlan::ParkBay>>> Bays;
};

namespace
{
using RideSceneBits::SceneCell;
using RideSceneBits::SceneKey;
using RideShapes::Paint;
using RideShapes::Pt;
using RideShapes::V3;

constexpr double ParkCell = 64.0;      // CarParkGrid's squares
constexpr double ParkMostAcross = 400.0;
constexpr double ParkLeast = 60.0;     // m2: smaller is a mapping slip or a lay-by
constexpr double LineWide = 0.1;       // the bays' lines
constexpr double SlabStep = 2.0;       // the surface's points at most this far apart (it follows the ground)
constexpr double KerbHigh = 0.12, KerbWide = 0.15;
constexpr double CarLong = 4.3, CarWide = 1.8, CarHigh = 1.45;
constexpr double CarShare = 0.35;      // of the bays with a car in
// [build 157 feedback] (Sandgate, Newcastle: an embankment mapped inside a car park drew bays and cars down the
// bank) Ground steeper than this (rise over run, across 4 m: a bank, not a sloping lay-by) is not car park: the land's own grass shows there,
// no bays, no kerbs, no tarmac under the wheels; a bay whose corners differ more than BayRiseMost is left out.
constexpr double ParkSteepest = 1.0 / 6.0;
constexpr double BayRiseMost = 0.4;
constexpr int LampEvery = 10;          // bays between lamps along a back line
constexpr int BaysMost = 3000;

// M_RideRoad's colours (WorldRide.cpp's RoadTile): its tarmac key, white paint; the yellow of no-parking lines
// for the badge bays; setts and gravel as the roads and paths draw them; the kerbs' concrete.
const Paint Tarmac = {0.022f, 0.022f, 0.025f}, White = {0.83f, 0.83f, 0.75f}, Yellow = {0.78f, 0.52f, 0.03f};
const Paint Setts = {0.2f, 0.19f, 0.17f}, Gravel = {0.095f, 0.088f, 0.07f}, Concrete = {0.21f, 0.21f, 0.2f};
const Paint Steel = {0.35f, 0.36f, 0.37f}, MachineBody = {0.1f, 0.11f, 0.12f}, MachineTop = {0.62f, 0.47f, 0.05f}, Screen = {0.02f, 0.025f, 0.03f};
// Cars drawn in code where the game has no car models (the desk test's pictures): plain paints, glass dark.
const Paint CarPaints[] = {{0.55f, 0.56f, 0.57f}, {0.012f, 0.03f, 0.09f}, {0.8f, 0.8f, 0.78f}, {0.2f, 0.012f, 0.012f}, {0.08f, 0.08f, 0.085f}, {0.18f, 0.3f, 0.45f}};
const Paint CarGlass = {0.02f, 0.025f, 0.03f}, Tyre = {0.02f, 0.02f, 0.02f};

long long ParkKey(long long Cx, long long Cz) { return Cx * 4000037LL + Cz; }

double Dot(const P2& A, const P2& B) { return A.X * B.X + A.Z * B.Z; }

double Dice(uint32_t Seed, uint32_t Salt)
{
	uint32_t H = Seed * 2654435761u ^ (Salt + 0x9e3779b9u) * 2246822519u;
	H ^= H >> 15; H *= 2246822519u; H ^= H >> 13; H *= 3266489917u; H ^= H >> 16;
	return double(H & 0xffffff) / double(0x1000000);
}

double SegDist(const P2& P, const P2& A, const P2& B)
{
	const P2 D = B - A;
	const double L2 = D.X * D.X + D.Z * D.Z;
	const double T = L2 > 1e-12 ? std::clamp(((P.X - A.X) * D.X + (P.Z - A.Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
	return P.DistanceTo(A + D * T);
}

// Where segments AB and CD cross (false if they don't).
bool CrossAt(const P2& A, const P2& B, const P2& C, const P2& D, P2& At)
{
	const double Rx = B.X - A.X, Rz = B.Z - A.Z, Sx = D.X - C.X, Sz = D.Z - C.Z;
	const double Den = Rx * Sz - Rz * Sx;
	if (std::fabs(Den) < 1e-12) return false;
	const double T = ((C.X - A.X) * Sz - (C.Z - A.Z) * Sx) / Den, Q = ((C.X - A.X) * Rz - (C.Z - A.Z) * Rx) / Den;
	if (T < 0.0 || T > 1.0 || Q < 0.0 || Q > 1.0) return false;
	At = A + (B - A) * T;
	return true;
}

// The roads a car park is driven into from or round: anything a car takes (no paths, no tracks).
bool CarRoad(const std::string& Class)
{
	return Class != "footway" && Class != "path" && Class != "cycleway" && Class != "bridleway" && Class != "steps" && Class != "pedestrian" && Class != "track"
		&& Class != "motorway" && Class != "motorway_link";
}

// The ring clipped to the rectangle [U0, U1] x [V0, V1] (all in the car park's own u, v), Sutherland-Hodgman.
std::vector<P2> ClipToCell(const std::vector<P2>& Ring, double U0, double U1, double V0, double V1)
{
	std::vector<P2> Poly = Ring, Next;
	const auto Edge = [&](int Which, double Lim)
	{
		Next.clear();
		const auto In = [&](const P2& P) { return Which == 0 ? P.X >= Lim : Which == 1 ? P.X <= Lim : Which == 2 ? P.Z >= Lim : P.Z <= Lim; };
		const auto Cut = [&](const P2& A, const P2& B)
		{
			const double T = Which < 2 ? (Lim - A.X) / (B.X - A.X) : (Lim - A.Z) / (B.Z - A.Z);
			return A + (B - A) * T;
		};
		for (size_t I = 0; I < Poly.size(); ++I)
		{
			const P2& A = Poly[I];
			const P2& B = Poly[(I + 1) % Poly.size()];
			const bool Ia = In(A), Ib = In(B);
			if (Ia) Next.push_back(A);
			if (Ia != Ib) Next.push_back(Cut(A, B));
		}
		Poly.swap(Next);
	};
	Edge(0, U0);
	if (Poly.size() >= 3) Edge(1, U1);
	if (Poly.size() >= 3) Edge(2, V0);
	if (Poly.size() >= 3) Edge(3, V1);
	return Poly;
}

double Cross2(const P2& O, const P2& A, const P2& B) { return (A.X - O.X) * (B.Z - O.Z) - (A.Z - O.Z) * (B.X - O.X); }

// A simple polygon's triangles by ear clipping (indices into it), whatever its winding; tolerant of the
// zero-width bridges clipping a concave ring leaves.
void Ears(const std::vector<P2>& Poly, std::vector<int>& Tris)
{
	Tris.clear();
	const int N = int(Poly.size());
	if (N < 3) return;
	double Twice = 0.0;
	for (int I = 0; I < N; ++I) Twice += Cross2(P2(), Poly[size_t(I)], Poly[size_t((I + 1) % N)]);
	const double Sign = Twice >= 0.0 ? 1.0 : -1.0;
	std::vector<int> Left(static_cast<size_t>(N));
	for (int I = 0; I < N; ++I) Left[size_t(I)] = I;
	int Guard = N * N + 8;
	while (Left.size() > 3 && Guard-- > 0)
	{
		bool Cut = false;
		const int M = int(Left.size());
		for (int K = 0; K < M && !Cut; ++K)
		{
			const int Ia = Left[size_t((K + M - 1) % M)], Ib = Left[size_t(K)], Ic = Left[size_t((K + 1) % M)];
			const P2 &A = Poly[size_t(Ia)], &B = Poly[size_t(Ib)], &C = Poly[size_t(Ic)];
			const double Turn = Cross2(A, B, C) * Sign;
			if (Turn < -1e-9) continue;   // (a reflex corner)
			bool Empty = true;
			if (Turn > 1e-9)
				for (int J : Left)
				{
					if (J == Ia || J == Ib || J == Ic) continue;
					const P2& P = Poly[size_t(J)];
					if (Cross2(A, B, P) * Sign > 1e-9 && Cross2(B, C, P) * Sign > 1e-9 && Cross2(C, A, P) * Sign > 1e-9) { Empty = false; break; }
				}
			if (!Empty) continue;
			if (Turn > 1e-9) Tris.insert(Tris.end(), {Ia, Ib, Ic});
			Left.erase(Left.begin() + K);
			Cut = true;
		}
		if (!Cut) break;
	}
	if (Left.size() == 3 && Cross2(Poly[size_t(Left[0])], Poly[size_t(Left[1])], Poly[size_t(Left[2])]) * Sign > 1e-9) Tris.insert(Tris.end(), {Left[0], Left[1], Left[2]});
}

// A level box: its middle (X, Z), along a unit direction Ux, Uz, HalfU along it and HalfV across, Y0 to Y1.
void LevelBox(RideMesh& Out, double X, double Z, const P2& Ux, double HalfU, double HalfV, double Y0, double Y1, const Paint& C, bool Top = true)
{
	const P2 Vx(-Ux.Z, Ux.X), Mid(X, Z);
	const P2 Q[4] = {Mid - Ux * HalfU - Vx * HalfV, Mid + Ux * HalfU - Vx * HalfV, Mid + Ux * HalfU + Vx * HalfV, Mid - Ux * HalfU + Vx * HalfV};
	for (int K = 0; K < 4; ++K)
	{
		const P2 &A = Q[K], &B = Q[(K + 1) % 4];
		// (each side facing out: Q runs anticlockwise seen from above with x right and z down the page, so
		// SkyQuad's winding A B top-B top-A faces away from the middle)
		const V3 P0 = Pt(A.X, Y0, A.Z), P1 = Pt(B.X, Y0, B.Z), P2v = Pt(B.X, Y1, B.Z), P3 = Pt(A.X, Y1, A.Z);
		const double Nx = -(B.Z - A.Z), Nz = B.X - A.X;   // (one of its level normals)
		const P2 Side = (A + B) * 0.5 - Mid;
		if (Nx * Side.X + Nz * Side.Z > 0.0) RideShapes::SkyQuad(Out, P3, P2v, P1, P0, C, false);
		else RideShapes::SkyQuad(Out, P0, P1, P2v, P3, C, false);
	}
	if (Top)
	{
		const V3 T0 = Pt(Q[0].X, Y1, Q[0].Z), T1 = Pt(Q[1].X, Y1, Q[1].Z), T2 = Pt(Q[2].X, Y1, Q[2].Z), T3 = Pt(Q[3].X, Y1, Q[3].Z);
		// (facing up: SkyQuad shows the side (Q2 - Q0) x (Q3 - Q1) points to)
		const double Ny = (T2.Z - T0.Z) * (T3.X - T1.X) - (T2.X - T0.X) * (T3.Z - T1.Z);
		if (Ny > 0.0) RideShapes::SkyQuad(Out, T0, T1, T2, T3, C, false);
		else RideShapes::SkyQuad(Out, T3, T2, T1, T0, C, false);
	}
}
// The buildings standing over a box of ground as the squares draw them (BuildingsTile): where the world has
// the cells' outlines, those cut into boxes; elsewhere the cells' own boxes (an outline's own box can be far
// bigger than the building: an L-shaped one's, round its whole outline).
} // namespace

void RidePlan::CarParkStands(double X0, double Z0, double X1, double Z1, std::vector<Building>& Out) const
{
	const RidePlan& P = *this;
	Out.clear();
	if (!P.Scene) return;
	const Region& G = P.Area.Roads;
	std::vector<std::shared_ptr<const std::vector<RidePlan::OutlinePiece>>> Held;
	std::vector<const RidePlan::OutlinePiece*> Pieces;
	std::vector<CellId> Covered;
	P.OutlinesIn(X0 - 40.0, Z0 - 40.0, std::fmax(X1 - X0, Z1 - Z0) + 80.0, Held, Pieces, Covered);
	const auto Near = [&](const Building& B) { const double R = std::hypot(B.W, B.D) * 0.5; return B.X + R >= X0 && B.X - R <= X1 && B.Z + R >= Z0 && B.Z - R <= Z1; };
	for (const RidePlan::OutlinePiece* Piece : Pieces) if (Near(Piece->B)) Out.push_back(Piece->B);
	const Frame Fr = G.FrameOf();
	for (long long Cz = static_cast<long long>(std::floor((Z0 - 40.0) / SceneCell)); Cz <= static_cast<long long>(std::floor((Z1 + 40.0) / SceneCell)); ++Cz)
		for (long long Cx = static_cast<long long>(std::floor((X0 - 40.0) / SceneCell)); Cx <= static_cast<long long>(std::floor((X1 + 40.0) / SceneCell)); ++Cx)
		{
			const auto It = P.Scene->Buildings.find(Cx * 4000037LL + Cz);
			if (It == P.Scene->Buildings.end()) continue;
			for (int I : It->second)
			{
				const Building& B = G.Buildings[size_t(I)];
				if (!Near(B)) continue;
				if (!Covered.empty())
				{
					const Geo At = Fr.ToGeo(B.X, B.Z);
					if (std::find(Covered.begin(), Covered.end(), CellOf(At.Lat, At.Lon)) != Covered.end()) continue;
				}
				Out.push_back(B);
			}
		}
}

namespace
{
bool InStand(const std::vector<Building>& Stands, const P2& P, double Spare)
{
	for (const Building& H : Stands)
	{
		const double Cs = std::cos(H.Yaw), Sn = std::sin(H.Yaw), Ex = P.X - H.X, Ez = P.Z - H.Z;
		if (std::fabs(Ex * Cs + Ez * Sn) < H.W * 0.5 + Spare && std::fabs(-Ex * Sn + Ez * Cs) < H.D * 0.5 + Spare) return true;
	}
	return false;
}
} // namespace

void RidePlan::IndexCarParks()
{
	CarParks.clear();
	CarParkGrid.clear();
	ParkMemo = std::make_shared<CarParkMemo>();
	ResetDriveCars();   // [driveways]
	for (int& Count : CarParkCount) Count = 0;
	const Region& G = Area.Roads;
	// The roads cars take, by 64 m square, bit by bit: service roads apart (a car park's aisles).
	struct Bit { P2 A, B; bool Service; };
	std::unordered_map<long long, std::vector<Bit>> Bits;
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const std::string& Class = G.ClassOf(int(E));
		if (!CarRoad(Class)) continue;
		const std::vector<P2>& L = G.Edges[E].Line;
		for (size_t I = 0; I + 1 < L.size(); ++I)
		{
			const P2 Mid = (L[I] + L[I + 1]) * 0.5;
			Bits[ParkKey(static_cast<long long>(std::floor(Mid.X / ParkCell)), static_cast<long long>(std::floor(Mid.Z / ParkCell)))].push_back({L[I], L[I + 1], Class == "service"});
		}
	}
	for (const GroundShape& A : G.Ground.Areas)
	{
		if (A.Kind != "car_park" || A.Pts.size() < 3) continue;
		const std::string& W = A.Word;
		CarPark C;
		if (W == "grass" || W == "street_side" || W == "layby" || W == "multi-storey" || W == "underground" || W == "rooftop") continue;
		C.Surface = W == "gravel" || W == "fine_gravel" || W == "compacted" || W == "unpaved" || W == "ground" || W == "dirt" || W == "mud" || W == "pebblestone" ? ParkGravel
			: W == "sett" || W == "cobblestone" || W == "paving_stones" || W == "unhewn_cobblestone" ? ParkSetts : ParkTarmac;
		C.Ring = A.Pts;
		if (C.Ring.size() > 3 && C.Ring.front().DistanceTo(C.Ring.back()) < 0.05) C.Ring.pop_back();
		C.MinX = C.MaxX = C.Ring[0].X;
		C.MinZ = C.MaxZ = C.Ring[0].Z;
		for (const P2& P : C.Ring)
		{
			C.MinX = std::fmin(C.MinX, P.X); C.MaxX = std::fmax(C.MaxX, P.X);
			C.MinZ = std::fmin(C.MinZ, P.Z); C.MaxZ = std::fmax(C.MaxZ, P.Z);
		}
		if (C.MaxX - C.MinX > ParkMostAcross || C.MaxZ - C.MinZ > ParkMostAcross) continue;
		P2 Middle;
		C.Area = RideShapes::RingArea(C.Ring, Middle);
		if (C.Area < ParkLeast) continue;
		C.Seed = static_cast<uint32_t>(std::lround(C.Ring[0].X * 10.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(C.Ring[0].Z * 10.0)) * 19349663u;
		// The service roads in it, and the roads round it.
		std::vector<const Bit*> Near;
		for (long long Cz = static_cast<long long>(std::floor((C.MinZ - 30.0) / ParkCell)); Cz <= static_cast<long long>(std::floor((C.MaxZ + 30.0) / ParkCell)); ++Cz)
			for (long long Cx = static_cast<long long>(std::floor((C.MinX - 30.0) / ParkCell)); Cx <= static_cast<long long>(std::floor((C.MaxX + 30.0) / ParkCell)); ++Cx)
			{
				const auto It = Bits.find(ParkKey(Cx, Cz));
				if (It == Bits.end()) continue;
				for (const Bit& B : It->second) Near.push_back(&B);
			}
		// Its aisles' way: the service roads in it, weighed by length in 5 degree steps (an aisle mapped as many
		// short bits counts whole); else its longest side.
		double Weight[36] = {};
		double Best = 0.0;
		int BestStep = -1;
		for (const Bit* B : Near)
		{
			if (!B->Service || !RideShapes::InRing(C.Ring, (B->A.X + B->B.X) * 0.5, (B->A.Z + B->B.Z) * 0.5)) continue;
			const double L = B->A.DistanceTo(B->B);
			if (L < 0.5) continue;
			double Ang = std::atan2(B->B.Z - B->A.Z, B->B.X - B->A.X);
			if (Ang < 0.0) Ang += 3.14159265358979;
			const int Step = std::clamp(int(Ang / 3.14159265358979 * 36.0), 0, 35);
			Weight[Step] += L;
		}
		for (int K = 0; K < 36; ++K)
		{
			const double Sum = Weight[K] + 0.5 * (Weight[(K + 1) % 36] + Weight[(K + 35) % 36]);
			if (Sum > Best) { Best = Sum; BestStep = K; }
		}
		const Bit* Aisle = nullptr;
		if (Best >= 10.0)
		{
			// (the longest bit that way sets both the way and an aisle's middle)
			double Longest = 0.0;
			for (const Bit* B : Near)
			{
				if (!B->Service || !RideShapes::InRing(C.Ring, (B->A.X + B->B.X) * 0.5, (B->A.Z + B->B.Z) * 0.5)) continue;
				double Ang = std::atan2(B->B.Z - B->A.Z, B->B.X - B->A.X);
				if (Ang < 0.0) Ang += 3.14159265358979;
				const int Step = std::clamp(int(Ang / 3.14159265358979 * 36.0), 0, 35);
				if (std::abs(Step - BestStep) > 1 && std::abs(Step - BestStep) < 35) continue;
				const double L = B->A.DistanceTo(B->B);
				if (L > Longest) { Longest = L; Aisle = B; }
			}
		}
		if (Aisle != nullptr)
		{
			const double L = Aisle->A.DistanceTo(Aisle->B);
			C.U = (Aisle->B - Aisle->A) * (1.0 / L);
		}
		else
		{
			double Longest = 0.0;
			for (size_t I = 0; I < C.Ring.size(); ++I)
			{
				const P2 &P = C.Ring[I], &Q = C.Ring[(I + 1) % C.Ring.size()];
				const double L = P.DistanceTo(Q);
				if (L > Longest) { Longest = L; C.U = (Q - P) * (1.0 / L); }
			}
		}
		C.V = P2(-C.U.Z, C.U.X);
		double Umin = 1e18, Vmin = 1e18;
		for (const P2& P : C.Ring) { Umin = std::fmin(Umin, Dot(P, C.U)); Vmin = std::fmin(Vmin, Dot(P, C.V)); }
		C.PhaseU = Umin + 0.6;
		C.PhaseV = Aisle != nullptr ? Dot((Aisle->A + Aisle->B) * 0.5, C.V) : Vmin + 0.8 + ParkBayLong + ParkAisle * 0.5;
		// Its way in: where a service road crosses its edge (the one nearest a public road), else the point of its
		// edge nearest a road outside it.
		double EntryScore = 1e18;
		for (const Bit* B : Near)
		{
			if (!B->Service) continue;
			for (size_t I = 0; I < C.Ring.size(); ++I)
			{
				P2 At;
				if (!CrossAt(B->A, B->B, C.Ring[I], C.Ring[(I + 1) % C.Ring.size()], At)) continue;
				double Public = 60.0;
				for (const Bit* O : Near) if (!O->Service) Public = std::fmin(Public, SegDist(At, O->A, O->B));
				if (Public >= EntryScore) continue;
				EntryScore = Public;
				C.Entry = At;
				const P2 Run = B->B - B->A;
				const P2 Dir = Run * (1.0 / std::fmax(Run.Length(), 1e-6));
				C.EntryIn = RideShapes::InRing(C.Ring, At.X + Dir.X * 2.0, At.Z + Dir.Z * 2.0) ? Dir : Dir * -1.0;
				C.HasEntry = true;
			}
		}
		if (!C.HasEntry)
		{
			double Nearest = 25.0;
			for (const Bit* O : Near)
				for (size_t I = 0; I < C.Ring.size(); ++I)
				{
					const P2 &P = C.Ring[I], &Q = C.Ring[(I + 1) % C.Ring.size()];
					const double L = P.DistanceTo(Q);
					for (double T = 0.5; T < L; T += 2.0)
					{
						const P2 At = P + (Q - P) * (T / L);
						const double D = SegDist(At, O->A, O->B);
						if (D >= Nearest) continue;
						Nearest = D;
						C.Entry = At;
						const P2 In = Middle - At;
						C.EntryIn = In * (1.0 / std::fmax(In.Length(), 1e-6));
						C.HasEntry = true;
					}
				}
		}
		// [build 157 feedback] Its level: a plane fitted to the land in it (twice, the second time without the
		// points more than ParkCutMost off the first: a bank mapped inside it stays a bank), its fall no more than
		// ParkFallMost. None where the ground isn't fetched yet.
		if (Land)
		{
			const double Step = std::fmax(LandGrid, std::sqrt(C.Area / 400.0));
			std::vector<std::array<double, 3>> Pts;
			for (double Z = C.MinZ + Step * 0.5; Z < C.MaxZ; Z += Step)
				for (double X = C.MinX + Step * 0.5; X < C.MaxX; X += Step)
				{
					if (!RideShapes::InRing(C.Ring, X, Z)) continue;
					const double Y = Land->Height(X, Z);
					if (!std::isnan(Y)) Pts.push_back({X - Middle.X, Z - Middle.Z, Y});
				}
			double Y0 = std::nan(""), Gx = 0.0, Gz = 0.0;
			for (int Pass = 0; Pass < 2 && Pts.size() >= 3; ++Pass)
			{
				double N = 0, Sx = 0, Sz = 0, Sy = 0, Sxx = 0, Szz = 0, Sxz = 0, Sxy = 0, Szy = 0;
				for (const auto& Q : Pts)
				{
					if (Pass == 1 && std::fabs(Q[2] - (Y0 + Gx * Q[0] + Gz * Q[1])) > ParkCutMost) continue;
					N += 1; Sx += Q[0]; Sz += Q[1]; Sy += Q[2]; Sxx += Q[0] * Q[0]; Szz += Q[1] * Q[1]; Sxz += Q[0] * Q[1]; Sxy += Q[0] * Q[2]; Szy += Q[1] * Q[2];
				}
				if (N < 3) break;
				const double Mx = Sx / N, Mz = Sz / N, My = Sy / N;
				const double Cxx = Sxx / N - Mx * Mx, Czz = Szz / N - Mz * Mz, Cxz = Sxz / N - Mx * Mz, Cxy = Sxy / N - Mx * My, Czy = Szy / N - Mz * My;
				const double Det = Cxx * Czz - Cxz * Cxz;
				Gx = Gz = 0.0;
				if (Det > 1e-6) { Gx = (Cxy * Czz - Czy * Cxz) / Det; Gz = (Czy * Cxx - Cxy * Cxz) / Det; }
				const double Fall = std::hypot(Gx, Gz);
				if (Fall > ParkFallMost) { Gx *= ParkFallMost / Fall; Gz *= ParkFallMost / Fall; }
				Y0 = My - Gx * Mx - Gz * Mz;
			}
			if (!std::isnan(Y0)) { C.LevelAt = Middle; C.LevelY = Y0; C.LevelGx = Gx; C.LevelGz = Gz; }
		}
		const int Index = int(CarParks.size());
		// (filed with ParkLevelReach round it: the land eases back to its own lie out there)
		for (long long Cz = static_cast<long long>(std::floor((C.MinZ - ParkLevelReach) / ParkCell)); Cz <= static_cast<long long>(std::floor((C.MaxZ + ParkLevelReach) / ParkCell)); ++Cz)
			for (long long Cx = static_cast<long long>(std::floor((C.MinX - ParkLevelReach) / ParkCell)); Cx <= static_cast<long long>(std::floor((C.MaxX + ParkLevelReach) / ParkCell)); ++Cx)
				CarParkGrid[ParkKey(Cx, Cz)].push_back(Index);
		++CarParkCount[C.Surface];
		CarParks.push_back(std::move(C));
	}
}

void RidePlan::CarParkBays(int Which, std::vector<ParkBay>& Out) const
{
	Out.clear();
	if (Which < 0 || size_t(Which) >= CarParks.size()) return;
	if (ParkMemo)
	{
		std::lock_guard<std::mutex> Hold(ParkMemo->Lock);
		const auto It = ParkMemo->Bays.find(Which);
		if (It != ParkMemo->Bays.end()) { Out = *It->second; return; }
	}
	LayOutBays(Which, Out);
	if (ParkMemo)
	{
		std::lock_guard<std::mutex> Hold(ParkMemo->Lock);
		ParkMemo->Bays.emplace(Which, std::make_shared<const std::vector<ParkBay>>(Out));
	}
}

void RidePlan::LayOutBays(int Which, std::vector<ParkBay>& Out) const
{
	const CarPark& C = CarParks[size_t(Which)];
	if (C.Surface == ParkGravel) return;   // (no lines on gravel: park where you like)
	{
		Rect Around;
		Around.X0 = C.MinX - 30.0; Around.Z0 = C.MinZ - 30.0; Around.X1 = C.MaxX + 30.0; Around.Z1 = C.MaxZ + 30.0;
		LayBlocks(Around);
	}
	std::vector<Building> Stands;
	CarParkStands(C.MinX, C.MinZ, C.MaxX, C.MaxZ, Stands);
	const auto InBuilding = [&Stands](const P2& P, double Spare) { return InStand(Stands, P, Spare); };
	const auto EdgeDist = [&C](const P2& P)
	{
		double D = 1e18;
		for (size_t I = 0; I < C.Ring.size(); ++I) D = std::fmin(D, SegDist(P, C.Ring[I], C.Ring[(I + 1) % C.Ring.size()]));
		return D;
	};
	const auto Clear = [&](const P2& P)
	{
		if (!RideShapes::InRing(C.Ring, P.X, P.Z) || EdgeDist(P) < 0.3 || InBuilding(P, 0.4)) return false;
		if (Land && (Land->Cover(P.X, P.Z) == Terrain::CoverWater || Land->SeaAt(P.X, P.Z))) return false;
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + 0.3;
	};
	double Umin = 1e18, Umax = -1e18, Vmin = 1e18, Vmax = -1e18;
	for (const P2& P : C.Ring)
	{
		Umin = std::fmin(Umin, Dot(P, C.U)); Umax = std::fmax(Umax, Dot(P, C.U));
		Vmin = std::fmin(Vmin, Dot(P, C.V)); Vmax = std::fmax(Vmax, Dot(P, C.V));
	}
	std::unordered_map<long long, double> Heights;
	const double Period = 2.0 * ParkBayLong + ParkAisle;
	const int K0 = int(std::floor((Vmin - C.PhaseV) / Period)) - 1, K1 = int(std::ceil((Vmax - C.PhaseV) / Period)) + 1;
	const int I0 = int(std::floor((Umin - C.PhaseU) / ParkBayWide)), I1 = int(std::ceil((Umax - C.PhaseU) / ParkBayWide));
	for (int K = K0; K <= K1 && int(Out.size()) < BaysMost; ++K)
	{
		const double Va = C.PhaseV + K * Period;
		for (int Up = 0; Up < 2; ++Up)
		{
			const double Side = Up ? 1.0 : -1.0;
			const double Vf = Va + Side * ParkAisle * 0.5, Vb = Vf + Side * ParkBayLong;   // (its front, at the aisle, and back)
			for (int I = I0; I <= I1 && int(Out.size()) < BaysMost; ++I)
			{
				const double U0 = C.PhaseU + I * ParkBayWide, U1 = U0 + ParkBayWide;
				const auto At = [&C](double U, double V) { return C.U * U + C.V * V; };
				const P2 Corners[5] = {At(U0, Vf), At(U1, Vf), At(U1, Vb), At(U0, Vb), At((U0 + U1) * 0.5, (Vf + Vb) * 0.5)};
				bool Ok2 = true;
				for (const P2& P : Corners) Ok2 = Ok2 && Clear(P);
				if (!Ok2) continue;
				{
					// (level enough to park on: its corners within BayRiseMost, its middle not on a bank)
					double Lo = 1e18, Hi = -1e18;
					for (int Q = 0; Q < 4; ++Q)
					{
						const double Y = LandDrawnAt(Corners[Q].X, Corners[Q].Z, &Heights);
						if (!std::isnan(Y)) { Lo = std::fmin(Lo, Y); Hi = std::fmax(Hi, Y); }
					}
					if (Hi - Lo > BayRiseMost || CarParkSteep(Corners[4].X, Corners[4].Z, &Heights)) continue;
				}
				ParkBay B;
				B.Mid = Corners[4];
				B.Along = C.U;
				B.In = C.V * Side;
				B.Row = K * 2 + Up;
				B.Col = I;
				Out.push_back(B);
			}
		}
	}
	// (the two bays nearest the way in for blue badges; a car in about a third of the rest)
	if (C.HasEntry && Out.size() >= 6)
	{
		std::vector<std::pair<double, size_t>> ByEntry;
		for (size_t I = 0; I < Out.size(); ++I) ByEntry.push_back({Out[I].Mid.DistanceTo(C.Entry), I});
		std::partial_sort(ByEntry.begin(), ByEntry.begin() + 2, ByEntry.end());
		Out[ByEntry[0].second].Badge = Out[ByEntry[1].second].Badge = true;
	}
	// (a big one fuller near its way in, as people park; never more than ParkedCarsMost)
	const double Share = std::fmin(CarShare, double(ParkedCarsMost) / double(std::max<size_t>(Out.size(), 1)));
	for (ParkBay& B : Out)
	{
		const uint32_t Salt = static_cast<uint32_t>(B.Row * 7919 + B.Col * 104729 + 17);
		const double Near = C.HasEntry ? std::clamp(1.4 - B.Mid.DistanceTo(C.Entry) / 120.0, 0.6, 1.4) : 1.0;
		B.Car = ParkedCars && !B.Badge && Dice(C.Seed, Salt) < Share * Near;
		B.Pick = static_cast<uint8_t>(Dice(C.Seed, Salt + 1u) * 255.0);
	}
}

double RidePlan::CarParkLevel(double X, double Z, double Lie) const
{
	const auto It = CarParkGrid.find(ParkKey(static_cast<long long>(std::floor(X / ParkCell)), static_cast<long long>(std::floor(Z / ParkCell))));
	if (It == CarParkGrid.end()) return Lie;
	double Most = 0.0, Shift = 0.0;
	for (int Which : It->second)
	{
		const CarPark& C = CarParks[size_t(Which)];
		if (std::isnan(C.LevelY) || X < C.MinX - ParkLevelReach || X > C.MaxX + ParkLevelReach || Z < C.MinZ - ParkLevelReach || Z > C.MaxZ + ParkLevelReach) continue;
		const double Off = std::clamp(Lie - (C.LevelY + C.LevelGx * (X - C.LevelAt.X) + C.LevelGz * (Z - C.LevelAt.Z)), -ParkCutMost, ParkCutMost);
		double W = 1.0;
		if (!RideShapes::InRing(C.Ring, X, Z))
		{
			double Out = 1e18;
			for (size_t I = 0; I < C.Ring.size(); ++I) Out = std::fmin(Out, SegDist(P2(X, Z), C.Ring[I], C.Ring[(I + 1) % C.Ring.size()]));
			const double T = std::clamp(1.0 - Out / std::fmax(1.0, std::fmin(ParkLevelReach, 1.5 * std::fabs(Off))), 0.0, 1.0);
			W = T * T * (3.0 - 2.0 * T);
		}
		if (W > Most) { Most = W; Shift = Off * W; }
	}
	return Lie - Shift;
}

bool RidePlan::CarParkSteep(double X, double Z, std::unordered_map<long long, double>* Cache) const
{
	const double Hx0 = LandDrawnAt(X - 2.0, Z, Cache), Hx1 = LandDrawnAt(X + 2.0, Z, Cache), Hz0 = LandDrawnAt(X, Z - 2.0, Cache), Hz1 = LandDrawnAt(X, Z + 2.0, Cache);
	if (std::isnan(Hx0) || std::isnan(Hx1) || std::isnan(Hz0) || std::isnan(Hz1)) return false;
	return std::hypot(Hx1 - Hx0, Hz1 - Hz0) > ParkSteepest * 4.0;
}

bool RidePlan::InCarPark(double X, double Z) const
{
	const int Surface = CarParkSurfaceAt(X, Z);
	return Surface >= 0 && Surface != ParkGravel;
}

int RidePlan::CarParkSurfaceAt(double X, double Z) const
{
	const auto It = CarParkGrid.find(ParkKey(static_cast<long long>(std::floor(X / ParkCell)), static_cast<long long>(std::floor(Z / ParkCell))));
	if (It == CarParkGrid.end()) return -1;
	for (int Which : It->second)
	{
		const CarPark& C = CarParks[size_t(Which)];
		if (X >= C.MinX && X <= C.MaxX && Z >= C.MinZ && Z <= C.MaxZ && RideShapes::InRing(C.Ring, X, Z)) return CarParkSteep(X, Z) ? -1 : C.Surface;
	}
	return -1;
}

void RidePlan::ParkedCarsNear(double X, double Z, double Reach, std::vector<ParkedCarBox>& Out) const
{
	Out.clear();
	std::unordered_set<int> Seen;
	std::vector<ParkBay> Bays;
	for (long long Cz = static_cast<long long>(std::floor((Z - Reach) / ParkCell)); Cz <= static_cast<long long>(std::floor((Z + Reach) / ParkCell)); ++Cz)
		for (long long Cx = static_cast<long long>(std::floor((X - Reach) / ParkCell)); Cx <= static_cast<long long>(std::floor((X + Reach) / ParkCell)); ++Cx)
		{
			const auto It = CarParkGrid.find(ParkKey(Cx, Cz));
			if (It == CarParkGrid.end()) continue;
			for (int Which : It->second)
			{
				if (!Seen.insert(Which).second) continue;
				CarParkBays(Which, Bays);
				for (const ParkBay& B : Bays)
				{
					if (!B.Car || std::hypot(B.Mid.X - X, B.Mid.Z - Z) > Reach) continue;
					const double Y = LandDrawnAt(B.Mid.X, B.Mid.Z);
					if (std::isnan(Y)) continue;
					ParkedCarBox Box;
					Box.X = B.Mid.X;
					Box.Z = B.Mid.Z;
					Box.Ux = B.In.X;
					Box.Uz = B.In.Z;
					Box.HalfLong = CarLong * 0.5;
					Box.HalfWide = CarWide * 0.5;
					Box.Y0 = Y + ParkLift;
					Box.Y1 = Y + ParkLift + CarHigh;
					Out.push_back(Box);
				}
			}
		}
	DriveCarsNear(X, Z, Reach, Out);   // [driveways] (and the cars on the houses' drives)
}

void RidePlan::CarParksTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (CarParks.empty() || Detail < 2) return;
	const auto InTile = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	std::unordered_set<int> Here;
	for (long long Cz = static_cast<long long>(std::floor(Z0 / ParkCell)); Cz <= static_cast<long long>(std::floor((Z0 + Size - 0.001) / ParkCell)); ++Cz)
		for (long long Cx = static_cast<long long>(std::floor(X0 / ParkCell)); Cx <= static_cast<long long>(std::floor((X0 + Size - 0.001) / ParkCell)); ++Cx)
		{
			const auto It = CarParkGrid.find(ParkKey(Cx, Cz));
			if (It != CarParkGrid.end()) Here.insert(It->second.begin(), It->second.end());
		}
	if (Here.empty()) return;
	std::vector<int> Order(Here.begin(), Here.end());
	std::sort(Order.begin(), Order.end());
	std::unordered_map<long long, double> Heights;
	const auto Floor = [&](double X, double Z) { const double Y = LandDrawnAt(X, Z, &Heights); return std::isnan(Y) ? Y : Y + ParkLift; };
	const auto Up = [&](double X, double Z)
	{
		const double Hx0 = LandDrawnAt(X - 1.0, Z, &Heights), Hx1 = LandDrawnAt(X + 1.0, Z, &Heights), Hz0 = LandDrawnAt(X, Z - 1.0, &Heights), Hz1 = LandDrawnAt(X, Z + 1.0, &Heights);
		if (std::isnan(Hx0) || std::isnan(Hx1) || std::isnan(Hz0) || std::isnan(Hz1)) return Pt(0.0, 1.0, 0.0);
		const double Sx = (Hx1 - Hx0) * 0.5, Sz = (Hz1 - Hz0) * 0.5, L = std::sqrt(Sx * Sx + 1.0 + Sz * Sz);
		return Pt(-Sx / L, 1.0 / L, -Sz / L);
	};
	std::vector<Building> Stands;
	const auto InBuilding = [&Stands](const P2& P) { return InStand(Stands, P, 0.0); };
	const auto OnRoad = [&](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return NearestOf(P.X, P.Z, Opts, Hit) && Hit.Dist <= Hit.Half + Spare;
	};
	// A triangle of the surface, facing up (as LandTile's: seen from above with x right and z down the page,
	// wound clockwise), each point at the drawn ground plus the lift.
	// (each car park's surface gathered first as triangles, then laid: see Lay)
	struct Flat { P2 A, B, C; const Paint* Colour; };
	std::vector<Flat> Pending;
	const auto Tri = [&Pending](const P2& A, const P2& B, const P2& Cc, const Paint& Colour)
	{
		if (Cross2(A, B, Cc) > 0.0) Pending.push_back({A, Cc, B, &Colour});
		else Pending.push_back({A, B, Cc, &Colour});
	};
	// The gathered triangles laid on the drawn ground, facing up (as LandTile's: seen from above with x right and
	// z down the page, wound clockwise). The ground is drawn in 4 m triangles of its own (LandDrawnAt), so where
	// it bends under one of these (a bank's top) its corner would poke through: each triangle is lifted at its
	// points by as much as any of the ground's corners under it stands over it, and a point shared by triangles
	// takes the most any of them wants, so the surface stays whole (no cracks).
	std::unordered_map<long long, double> Raise;
	const auto PointKey = [](const P2& P) { return std::llround(P.X * 200.0) * 1000000007LL + std::llround(P.Z * 200.0); };
	const auto Lay = [&]()
	{
		Raise.clear();
		for (const Flat& F : Pending)
		{
			const double Ya = Floor(F.A.X, F.A.Z), Yb = Floor(F.B.X, F.B.Z), Yc = Floor(F.C.X, F.C.Z);
			if (std::isnan(Ya) || std::isnan(Yb) || std::isnan(Yc)) continue;
			const double Den = Cross2(F.A, F.B, F.C);
			if (std::fabs(Den) < 1e-9) continue;
			double Most = 0.0;
			const double Gx0 = std::ceil(std::fmin(F.A.X, std::fmin(F.B.X, F.C.X)) / LandGrid), Gx1 = std::floor(std::fmax(F.A.X, std::fmax(F.B.X, F.C.X)) / LandGrid);
			const double Gz0 = std::ceil(std::fmin(F.A.Z, std::fmin(F.B.Z, F.C.Z)) / LandGrid), Gz1 = std::floor(std::fmax(F.A.Z, std::fmax(F.B.Z, F.C.Z)) / LandGrid);
			for (double Gz = Gz0; Gz <= Gz1; Gz += 1.0)
				for (double Gx = Gx0; Gx <= Gx1; Gx += 1.0)
				{
					const P2 G(Gx * LandGrid, Gz * LandGrid);
					const double Wa = Cross2(G, F.B, F.C) / Den, Wb = Cross2(G, F.C, F.A) / Den, Wc = 1.0 - Wa - Wb;
					if (Wa < -1e-6 || Wb < -1e-6 || Wc < -1e-6) continue;
					const double Land0 = LandDrawnAt(G.X, G.Z, &Heights);
					if (!std::isnan(Land0)) Most = std::fmax(Most, Land0 + ParkLift - (Wa * Ya + Wb * Yb + Wc * Yc));
				}
			if (Most <= 0.0) continue;
			Most = std::fmin(Most, 0.4);
			for (const P2* P : {&F.A, &F.B, &F.C})
			{
				double& R = Raise[PointKey(*P)];
				R = std::fmax(R, Most);
			}
		}
		for (const Flat& F : Pending)
		{
			const uint32_t First = uint32_t(Out.Vertices());
			for (const P2* P : {&F.A, &F.B, &F.C})
			{
				const double Y = Floor(P->X, P->Z);
				const auto Lift = Raise.find(PointKey(*P));
				RideShapes::Vert(Out, Pt(P->X, (std::isnan(Y) ? 0.0 : Y) + (Lift != Raise.end() ? Lift->second : 0.0), P->Z), Up(P->X, P->Z), *F.Colour, P->X / 8.0, P->Z / 8.0);
			}
			Out.Idx.insert(Out.Idx.end(), {First, First + 1, First + 2});
		}
		Pending.clear();
	};
	std::vector<ParkBay> Bays;
	std::vector<int> Tris;
	for (int Which : Order)
	{
		const CarPark& C = CarParks[size_t(Which)];
		if (C.MaxX < X0 || C.MinX >= X0 + Size || C.MaxZ < Z0 || C.MinZ >= Z0 + Size) continue;
		CarParkBays(Which, Bays);
		CarParkStands(C.MinX, C.MinZ, C.MaxX, C.MaxZ, Stands);
		const Paint& Ground0 = C.Surface == ParkGravel ? Gravel : C.Surface == ParkSetts ? Setts : Tarmac;
		const auto World = [&C](double U, double V) { return C.U * U + C.V * V; };
		// The lines, as rectangles in its own u, v: each bay's two sides and its back, yellow round a badge bay.
		struct Line { double U0, U1, V0, V1; bool Yellow; };
		std::vector<Line> Lines;
		std::unordered_map<long long, size_t> SideAt;   // (a side shared by two bays drawn once: yellow wins)
		for (const ParkBay& B : Bays)
		{
			const double Um = Dot(B.Mid, C.U), Vm = Dot(B.Mid, C.V), Side = Dot(B.In, C.V);
			const double Vf = Vm - Side * ParkBayLong * 0.5, Vb = Vm + Side * ParkBayLong * 0.5;
			for (int E = 0; E < 2; ++E)
			{
				const int Col = B.Col + E;
				const long long Key = static_cast<long long>(B.Row) * 1000003LL + Col;
				const double Us = Um + (E ? 0.5 : -0.5) * ParkBayWide;
				const auto Found = SideAt.find(Key);
				if (Found != SideAt.end()) { Lines[Found->second].Yellow = Lines[Found->second].Yellow || B.Badge; continue; }
				SideAt[Key] = Lines.size();
				Lines.push_back({Us - LineWide * 0.5, Us + LineWide * 0.5, std::fmin(Vf, Vb), std::fmax(Vf, Vb), B.Badge});
			}
			Lines.push_back({Um - ParkBayWide * 0.5, Um + ParkBayWide * 0.5, Vb - LineWide * 0.5, Vb + LineWide * 0.5, B.Badge});
		}
		// The surface: a grid in u, v split at every line's edges and at most SlabStep between, each cell whole
		// where it lies inside the edge and clipped to it where the edge runs through; a cell is drawn in the
		// paint's colour where a line covers it, else the surface's. Each cell by the tile its middle is in.
		std::vector<P2> RingUv;
		double Umin = 1e18, Umax = -1e18, Vmin = 1e18, Vmax = -1e18;
		for (const P2& P : C.Ring)
		{
			RingUv.push_back(P2(Dot(P, C.U), Dot(P, C.V)));
			Umin = std::fmin(Umin, RingUv.back().X); Umax = std::fmax(Umax, RingUv.back().X);
			Vmin = std::fmin(Vmin, RingUv.back().Z); Vmax = std::fmax(Vmax, RingUv.back().Z);
		}
		std::vector<double> Us = {Umin, Umax}, Vs = {Vmin, Vmax};
		for (const Line& L : Lines) { Us.push_back(L.U0); Us.push_back(L.U1); Vs.push_back(L.V0); Vs.push_back(L.V1); }
		const auto Even = [](std::vector<double>& Cuts)
		{
			std::sort(Cuts.begin(), Cuts.end());
			Cuts.erase(std::unique(Cuts.begin(), Cuts.end(), [](double P, double Q) { return std::fabs(P - Q) < 0.01; }), Cuts.end());
			std::vector<double> Fine;
			for (size_t I = 0; I + 1 < Cuts.size(); ++I)
			{
				Fine.push_back(Cuts[I]);
				const int Bits = int(std::ceil((Cuts[I + 1] - Cuts[I]) / SlabStep));
				for (int K = 1; K < Bits; ++K) Fine.push_back(Cuts[I] + (Cuts[I + 1] - Cuts[I]) * K / Bits);
			}
			Fine.push_back(Cuts.back());
			Cuts.swap(Fine);
		};
		Even(Us);
		Even(Vs);
		// (the lines filed by u for the cells to find theirs)
		const auto Covered = [&Lines](double U, double V, bool& IsYellow)
		{
			for (const Line& L : Lines)
				if (U > L.U0 && U < L.U1 && V > L.V0 && V < L.V1) { IsYellow = L.Yellow; return true; }
			return false;
		};
		// (a bay's lines filed by its row and column for a quicker look-up: most cells are far from any)
		for (size_t J = 0; J + 1 < Vs.size(); ++J)
			for (size_t I = 0; I + 1 < Us.size(); ++I)
			{
				const double Ua = Us[I], Ub = Us[I + 1], Va = Vs[J], Vb = Vs[J + 1];
				const double Uc = (Ua + Ub) * 0.5, Vc = (Va + Vb) * 0.5;
				const P2 Mid = World(Uc, Vc);
				if (!InTile(Mid)) continue;
				const P2 Q[4] = {World(Ua, Va), World(Ub, Va), World(Ub, Vb), World(Ua, Vb)};
				int Inside = 0;
				for (const P2& P : Q) Inside += RideShapes::InRing(C.Ring, P.X, P.Z) ? 1 : 0;
				std::vector<P2> Piece;
				if (Inside == 4 && RideShapes::InRing(C.Ring, Mid.X, Mid.Z)) Piece.assign(Q, Q + 4);
				else
				{
					const std::vector<P2> Cut = ClipToCell(RingUv, Ua, Ub, Va, Vb);
					if (Cut.size() < 3) continue;
					for (const P2& P : Cut) Piece.push_back(World(P.X, P.Z));
				}
				if (OnRoad(Mid, 0.2) || InBuilding(Mid) || CarParkSteep(Mid.X, Mid.Z, &Heights)) continue;
				bool IsYellow = false;
				const bool Painted = C.Surface != ParkGravel && Covered(Uc, Vc, IsYellow);
				const Paint& Colour = Painted ? (IsYellow ? Yellow : White) : Ground0;
				if (Piece.size() == 4 && Inside == 4)
				{
					Tri(Piece[0], Piece[1], Piece[2], Colour);
					Tri(Piece[0], Piece[2], Piece[3], Colour);
				}
				else
				{
					Ears(Piece, Tris);
					for (size_t T = 0; T + 2 < Tris.size(); T += 3) Tri(Piece[size_t(Tris[T])], Piece[size_t(Tris[T + 1])], Piece[size_t(Tris[T + 2])], Colour);
				}
			}
		Lay();
		// The kerbs: round its edge, a little inside it, where no road runs by and not at its way in.
		if (C.Surface != ParkGravel)
			for (size_t I = 0; I < C.Ring.size(); ++I)
			{
				const P2 &A = C.Ring[I], &B = C.Ring[(I + 1) % C.Ring.size()];
				const double L = A.DistanceTo(B);
				if (L < 0.5) continue;
				const P2 Dir = (B - A) * (1.0 / L);
				P2 In(-Dir.Z, Dir.X);
				const P2 Mid0 = (A + B) * 0.5;
				if (!RideShapes::InRing(C.Ring, Mid0.X + In.X * 0.5, Mid0.Z + In.Z * 0.5)) In = In * -1.0;
				const int Bits = std::max(1, int(std::ceil(L / 2.0)));
				for (int K = 0; K < Bits; ++K)
				{
					const P2 Pa = A + Dir * (L * K / Bits), Pb = A + Dir * (L * (K + 1) / Bits), Pm = (Pa + Pb) * 0.5;
					if (!InTile(Pm) || (C.HasEntry && Pm.DistanceTo(C.Entry) < 5.0) || OnRoad(Pm + In * 0.1, 2.0) || InBuilding(Pm + In * 0.3)
						|| CarParkSteep(Pm.X + In.X * 1.5, Pm.Z + In.Z * 1.5, &Heights)) continue;
					const P2 Qa = Pa + In * KerbWide, Qb = Pb + In * KerbWide;
					const double Ya = Floor(Pa.X, Pa.Z), Yb = Floor(Pb.X, Pb.Z);
					if (std::isnan(Ya) || std::isnan(Yb)) continue;
					const V3 Out0 = Pt(Pa.X, Ya - 0.15, Pa.Z), Out1 = Pt(Pb.X, Yb - 0.15, Pb.Z), Top0 = Pt(Pa.X, Ya + KerbHigh, Pa.Z), Top1 = Pt(Pb.X, Yb + KerbHigh, Pb.Z);
					const V3 InTop0 = Pt(Qa.X, Ya + KerbHigh, Qa.Z), InTop1 = Pt(Qb.X, Yb + KerbHigh, Qb.Z), InFoot0 = Pt(Qa.X, Ya - 0.02, Qa.Z), InFoot1 = Pt(Qb.X, Yb - 0.02, Qb.Z);
					const P2 Away = In * -1.0;
					const auto Side = [&](const V3& P0, const V3& P1, const V3& P2v, const V3& P3, const P2& Facing)
					{
						const double Ax = P2v.X - P0.X, Ay = P2v.Y - P0.Y, Az = P2v.Z - P0.Z, Bx = P3.X - P1.X, By = P3.Y - P1.Y, Bz = P3.Z - P1.Z;
						const double Nx = Ay * Bz - Az * By, Nz = Ax * By - Ay * Bx;
						if (Nx * Facing.X + Nz * Facing.Z >= 0.0) RideShapes::SkyQuad(Out, P0, P1, P2v, P3, Concrete, false);
						else RideShapes::SkyQuad(Out, P3, P2v, P1, P0, Concrete, false);
					};
					Side(Out0, Out1, Top1, Top0, Away);       // (its outer face)
					Side(InFoot0, InFoot1, InTop1, InTop0, In);   // (its inner face, over the surface)
					{
						// (its top, facing up)
						const double Ny = (InTop1.Z - Top0.Z) * (InTop0.X - Top1.X) - (InTop1.X - Top0.X) * (InTop0.Z - Top1.Z);
						if (Ny > 0.0) RideShapes::SkyQuad(Out, Top0, Top1, InTop1, InTop0, Concrete, false);
						else RideShapes::SkyQuad(Out, InTop0, InTop1, Top1, Top0, Concrete, false);
					}
				}
			}
		// The P sign and the pay machine at its way in, either side of it.
		if (C.HasEntry && InTile(C.Entry))
		{
			const P2 Across(-C.EntryIn.Z, C.EntryIn.X);
			// (beside the way in, past its road's edge)
			double Wide = 3.0;
			{
				NearOpts Opts;
				NearHit Hit;
				if (NearestOf(C.Entry.X, C.Entry.Z, Opts, Hit) && Hit.Dist < 4.0) Wide = Hit.Half;
			}
			// (the sign on whichever side is clear first, a little way in if it must; the machine across from it)
			P2 SignAt = C.Entry;
			double SignSide = 0.0, Ys = std::nan("");
			for (const double In : {0.6, 3.0, 6.0})
			{
				for (const double Sd : {1.0, -1.0})
				{
					const P2 Q = C.Entry + Across * (Sd * (Wide + 1.0)) + C.EntryIn * In;
					const double Yq = Floor(Q.X, Q.Z);
					if (std::isnan(Yq) || OnRoad(Q, 0.3) || InBuilding(Q)) continue;
					SignAt = Q; SignSide = Sd; Ys = Yq;
					break;
				}
				if (SignSide != 0.0) break;
			}
			const double Opposite = SignSide != 0.0 ? -SignSide : 1.0;
			P2 PayAt = C.Entry + Across * (Opposite * (Wide + 1.4)) + C.EntryIn * 2.5;
			for (const P2& Q : {PayAt, C.Entry + Across * (Opposite * (Wide + 1.4)) + C.EntryIn * 6.0, C.Entry - Across * (Opposite * (Wide + 1.4)) + C.EntryIn * 5.0})
				if (RideShapes::InRing(C.Ring, Q.X, Q.Z) && !OnRoad(Q, 0.3) && !InBuilding(Q)) { PayAt = Q; break; }
			if (SignSide != 0.0)
			{
				RideShapes::Column(Out, Pt(SignAt.X, Ys - 0.3, SignAt.Z), 0.04, 0.04, 2.75, 6, Steel);
				SignPlate::Frame F;
				const P2 Face = C.EntryIn * -1.0;   // (to those coming in)
				F.X = SignAt.X + Face.X * 0.06; F.Z = SignAt.Z + Face.Z * 0.06; F.Y = Ys + 2.05; F.Fx = Face.X; F.Fz = Face.Z;
				static const float Blue[3] = {0.012f, 0.09f, 0.36f}, Ink[3] = {0.83f, 0.83f, 0.8f}, Back[3] = {0.3f, 0.31f, 0.32f};
				SignPlate::Plate(Out.SignsOf(), Out, F, SignPlate::Rect(0.6, 0.6, 0.04), Blue, 0.025, Ink, Back);
				SignText::Set(Out.SignsOf(), F.X + Face.X * 0.005, F.Y, F.Z + Face.Z * 0.005, Face.X, Face.Z, 0.4, "P", Ink);
			}
			const double Yp = Floor(PayAt.X, PayAt.Z);
			if (!Bays.empty() && !std::isnan(Yp) && !OnRoad(PayAt, 0.3) && !InBuilding(PayAt) && RideShapes::InRing(C.Ring, PayAt.X, PayAt.Z))
			{
				const P2 Facing = Across * -1.0;   // (its screen to the aisle)
				LevelBox(Out, PayAt.X, PayAt.Z, Facing, 0.17, 0.23, Yp - 0.1, Yp + 0.15, Concrete);
				LevelBox(Out, PayAt.X, PayAt.Z, Facing, 0.15, 0.2, Yp + 0.15, Yp + 1.45, MachineBody);
				LevelBox(Out, PayAt.X, PayAt.Z, Facing, 0.17, 0.22, Yp + 1.45, Yp + 1.62, MachineTop);
				const P2 Sc = PayAt + Facing * 0.155, Lat(-Facing.Z, Facing.X);
				RideShapes::SkyQuad(Out, Pt(Sc.X - Lat.X * 0.12, Yp + 1.0, Sc.Z - Lat.Z * 0.12), Pt(Sc.X + Lat.X * 0.12, Yp + 1.0, Sc.Z + Lat.Z * 0.12),
					Pt(Sc.X + Lat.X * 0.12, Yp + 1.3, Sc.Z + Lat.Z * 0.12), Pt(Sc.X - Lat.X * 0.12, Yp + 1.3, Sc.Z - Lat.Z * 0.12), Screen, true);
			}
		}
		// The lamps of a big one: on the back lines between back-to-back rows, every LampEvery bays, where both
		// rows have a bay each side of the spot (no light of their own: a lit face at night only, for the frame rate).
		if (Bays.size() >= 30)
		{
			std::unordered_set<long long> Have;
			for (const ParkBay& B : Bays) Have.insert(static_cast<long long>(B.Row) * 1000003LL + B.Col);
			const auto Has = [&Have](int Row, int Col) { return Have.count(static_cast<long long>(Row) * 1000003LL + Col) > 0; };
			const double Period = 2.0 * ParkBayLong + ParkAisle;
			for (const ParkBay& B : Bays)
			{
				// (the back line above aisle k (row 2k + 1) is the one below aisle k + 1 (row 2k + 2))
				if ((B.Row & 1) == 0 || (B.Col % LampEvery) != 0) continue;
				if (!Has(B.Row + 1, B.Col) || !Has(B.Row, B.Col - 1) || !Has(B.Row + 1, B.Col - 1)) continue;
				const int K = (B.Row - 1) / 2;
				const double U = C.PhaseU + B.Col * ParkBayWide, V = C.PhaseV + K * Period + ParkAisle * 0.5 + ParkBayLong;
				const P2 At = World(U, V);
				if (!InTile(At)) continue;
				const double Y = Floor(At.X, At.Z);
				if (std::isnan(Y)) continue;
				const bool Model = PropModelled[PropStreetLamp];
				const P2 Arm = C.V * (Dice(C.Seed, static_cast<uint32_t>(B.Col)) < 0.5 ? 1.0 : -1.0);
				if (Model)
				{
					RideProp P;
					P.Kind = PropStreetLamp;
					P.Mapped = false;
					P.Id = C.Seed ^ static_cast<uint32_t>(B.Row * 7919 + B.Col);
					P.Variant = static_cast<uint8_t>((P.Id >> 8) & 0xff);
					P.X = float(At.X - Out.OriginX);
					P.Z = float(At.Z - Out.OriginZ);
					P.Y = float(Y);
					P.Yaw = float(std::atan2(Arm.Z, Arm.X));
					Out.Props.push_back(P);
				}
				else
				{
					RideShapes::Column(Out, Pt(At.X, Y - 0.3, At.Z), 0.085, 0.05, 6.3, 8, Steel);
					LevelBox(Out, At.X + Arm.X * 0.9, At.Z + Arm.Z * 0.9, Arm, 0.5, 0.12, Y + 5.75, Y + 5.9, Steel);
				}
				const P2 Head = At + Arm * 1.1, Lat(-Arm.Z, Arm.X);
				static const Paint LampLight = {1.0f, 0.88f, 0.66f};
				RideMesh& Night = Out.NightOf();
				const size_t From = Night.Vertices();
				RideShapes::SkyQuad(Night, Pt(Head.X - Arm.X * 0.18 - Lat.X * 0.07, Y + 5.735, Head.Z - Arm.Z * 0.18 - Lat.Z * 0.07),
					Pt(Head.X + Arm.X * 0.18 - Lat.X * 0.07, Y + 5.735, Head.Z + Arm.Z * 0.18 - Lat.Z * 0.07), Pt(Head.X + Arm.X * 0.18 + Lat.X * 0.07, Y + 5.735, Head.Z + Arm.Z * 0.18 + Lat.Z * 0.07),
					Pt(Head.X - Arm.X * 0.18 + Lat.X * 0.07, Y + 5.735, Head.Z - Arm.Z * 0.18 + Lat.Z * 0.07), LampLight, true);
				if (Night.Alpha.empty()) Night.Alpha.assign(From, 1.0f);
				Night.Alpha.resize(From);
				Night.Alpha.resize(Night.Vertices(), 0.0f);
			}
		}
		// The parked cars: the kit's models where the game has them (as modelled, nose in or reversed in), else a
		// plain shape of one.
		for (const ParkBay& B : Bays)
		{
			if (!B.Car || !InTile(B.Mid)) continue;
			const double Y = Floor(B.Mid.X, B.Mid.Z);
			if (std::isnan(Y)) continue;
			const bool NoseIn = (B.Pick & 1) == 0 || (B.Pick % 7) < 4;
			const P2 Nose = NoseIn ? B.In : B.In * -1.0;
			// (not quite square in the bay, not quite in its middle)
			const double Skew = (double((B.Pick * 37u) % 255u) / 255.0 - 0.5) * 0.06, Slide = (double((B.Pick * 91u) % 255u) / 255.0 - 0.5) * 0.3;
			const P2 Dir = P2(Nose.X * std::cos(Skew) - Nose.Z * std::sin(Skew), Nose.X * std::sin(Skew) + Nose.Z * std::cos(Skew));
			const P2 Mid = B.Mid + B.Along * Slide;
			if (PropModelled[PropParkedCar])
			{
				RideProp P;
				P.Kind = PropParkedCar;
				P.Mapped = false;
				P.Id = C.Seed ^ static_cast<uint32_t>(B.Row * 7919 + B.Col * 31);
				P.Variant = B.Pick;
				P.X = float(Mid.X - Out.OriginX);
				P.Z = float(Mid.Z - Out.OriginZ);
				P.Y = float(Y);
				P.Yaw = float(std::atan2(Dir.Z, Dir.X));
				Out.Props.push_back(P);
				continue;
			}
			const Paint& Body = CarPaints[B.Pick % (sizeof(CarPaints) / sizeof(CarPaints[0]))];
			for (double Ax : {-1.0, 1.0})
				for (double Sd : {-1.0, 1.0})
				{
					const P2 W = Mid + Dir * (Ax * 1.35) + P2(-Dir.Z, Dir.X) * (Sd * 0.72);
					LevelBox(Out, W.X, W.Z, Dir, 0.31, 0.1, Y, Y + 0.6, Tyre);
				}
			LevelBox(Out, Mid.X, Mid.Z, Dir, CarLong * 0.5, CarWide * 0.5, Y + 0.25, Y + 0.85, Body);
			const P2 Cab = Mid - Dir * 0.25;
			LevelBox(Out, Cab.X, Cab.Z, Dir, 1.15, CarWide * 0.5 - 0.08, Y + 0.85, Y + 1.35, CarGlass, false);
			LevelBox(Out, Cab.X, Cab.Z, Dir, 1.0, CarWide * 0.5 - 0.1, Y + 1.35, Y + CarHigh, Body);
		}
	}
}
} // namespace TorqueWorld
