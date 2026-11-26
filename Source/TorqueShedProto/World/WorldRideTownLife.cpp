// [roads stage 7] Town life (the OSM plan's stage 7): first, the petrol stations (7C). At each of the
// map's fuel stops near enough a road (not a motorway's: its services are stage 8's), a forecourt you can
// ride into: tarmac from the road's edge back (drawn with the road, as a place's pull-off is, so the kerb
// and pavement stop at its mouth), a canopy on its posts over two or three pump islands, a kiosk behind
// where the map has no building for one, and a tall price board by the road. No maker's name, logo or
// colours anywhere: each canopy's band is one plain colour picked by hash. Stopped by a pump, the tank
// fills (ForecourtAt; TorqueRideWorld's dash).
#include "WorldRide.h"
#include "WorldRideForecourtKit.h"
#include "WorldRideRoadX.h"
#include "WorldRideSceneBits.h"
#include "WorldRoadLine.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>

namespace TorqueWorld
{
namespace
{
using RideSceneBits::SceneCell;
using RideSceneBits::SceneKey;
using RideShapes::Paint;
using RideShapes::Pt;
using RideShapes::V3;

constexpr double CourtCell = 64.0;     // ForecourtGrid's squares
constexpr double BitCell = 128.0;      // the roads' bits, filed for finding the nearest
constexpr double EntryLane = 5.0;      // from the road's edge to the canopy's front
constexpr double CanopyDeep = 9.0;     // the canopy, out from the road
constexpr double IslandGap = 6.5;      // between pump islands' middles, along the road
constexpr double IslandLong = 4.4;
constexpr double KioskDeep = 6.5;
constexpr double KioskNeed = EntryLane + CanopyDeep + KioskDeep + 1.0;   // the forecourt's depth for a kiosk behind the canopy
// [garage] The kiosk's row behind the canopy, the garage at one end of it (KioskRow long at the most);
// or, too shallow for that row, the garage beyond the canopy's end: GarageEnd more forecourt at each end
// (the forecourt stays laid either side of its middle), and GarageEndNeed deep for it.
constexpr double KioskRow = 15.5;
constexpr double GarageEnd = 9.5;
constexpr double GarageEndNeed = EntryLane + 0.5 + 6.5 + 1.0;

long long CourtKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / CourtCell)) * 4000037LL + static_cast<long long>(std::floor(Z / CourtCell));
}

double SegDistance(const P2& P, const P2& A, const P2& B, double* T = nullptr)
{
	const P2 D = B - A;
	const double L2 = D.X * D.X + D.Z * D.Z;
	double U = L2 > 1e-12 ? ((P.X - A.X) * D.X + (P.Z - A.Z) * D.Z) / L2 : 0.0;
	U = std::clamp(U, 0.0, 1.0);
	if (T != nullptr) *T = U;
	return P.DistanceTo(A + D * U);
}

bool Crosses(const P2& A, const P2& B, const P2& C, const P2& D)
{
	const auto Side = [](const P2& P, const P2& Q, const P2& R) { return (Q.X - P.X) * (R.Z - P.Z) - (Q.Z - P.Z) * (R.X - P.X); };
	const double D1 = Side(C, D, A), D2 = Side(C, D, B), D3 = Side(A, B, C), D4 = Side(A, B, D);
	return ((D1 > 0.0) != (D2 > 0.0)) && ((D3 > 0.0) != (D4 > 0.0));
}

// The same number from 0 to 1 for the same seed and salt.
double Dice(uint32_t Seed, uint32_t Salt)
{
	uint32_t V = Seed * 2654435761u ^ (Salt + 0x9e3779b9u) * 2246822519u;
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
}

// The roads a forecourt may open off: not motorways, tracks, paths or private drives.
bool CourtRoad(const std::string& Class)
{
	return Class == "trunk" || Class == "trunk_link" || Class == "primary" || Class == "primary_link" || Class == "secondary" || Class == "secondary_link" || Class == "tertiary"
		|| Class == "tertiary_link" || Class == "unclassified" || Class == "residential" || Class == "road" || Class == "unknown";
}

// The forecourt's own frame: U along the road, V out from its edge (0 at the edge), Y up.
struct CourtFrame
{
	P2 Edge, U, V;
	V3 At(double Along, double Out, double Y) const { const P2 P = Edge + U * Along + V * Out; return Pt(P.X, Y, P.Z); }
	P2 Flat(double Along, double Out) const { return Edge + U * Along + V * Out; }
};

CourtFrame FrameOf(const RidePlan::Forecourt& F)
{
	CourtFrame C;
	C.Edge = F.RoadMid + F.Out * F.Half;
	C.U = F.RoadDir;
	C.V = F.Out;
	return C;
}

// A face Q0..Q3 seen from outside (the side away from Inside), flat shaded.
void Face(RideMesh& Out, const V3& Q0, const V3& Q1, const V3& Q2, const V3& Q3, const V3& Inside, const Paint& C)
{
	const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
	const double Nx = Ay * Bz - Az * By, Ny = Az * Bx - Ax * Bz, Nz = Ax * By - Ay * Bx;
	const double Mx = (Q0.X + Q1.X + Q2.X + Q3.X) * 0.25 - Inside.X, My = (Q0.Y + Q1.Y + Q2.Y + Q3.Y) * 0.25 - Inside.Y, Mz = (Q0.Z + Q1.Z + Q2.Z + Q3.Z) * 0.25 - Inside.Z;
	// ([Nov 18] SkyQuad shows the side its cross product points away from: so the order whose cross
	// product points in. These faces were shown from inside and lit from outside: near black)
	if (Nx * Mx + Ny * My + Nz * Mz >= 0.0) RideShapes::SkyQuad(Out, Q3, Q2, Q1, Q0, C, false);
	else RideShapes::SkyQuad(Out, Q0, Q1, Q2, Q3, C, false);
}

// A box in the forecourt's frame: from (A0, V0) to (A1, V1) along and out, Y0 to Y1 up; Top/Bottom its lids.
void Box(RideMesh& Out, const CourtFrame& F, double A0, double A1, double V0, double V1, double Y0, double Y1, const Paint& C, bool Top = true, bool Bottom = false)
{
	const V3 B[4] = {F.At(A0, V0, Y0), F.At(A1, V0, Y0), F.At(A1, V1, Y0), F.At(A0, V1, Y0)};
	const V3 T[4] = {F.At(A0, V0, Y1), F.At(A1, V0, Y1), F.At(A1, V1, Y1), F.At(A0, V1, Y1)};
	const V3 In = F.At((A0 + A1) * 0.5, (V0 + V1) * 0.5, (Y0 + Y1) * 0.5);
	for (int K = 0; K < 4; ++K) Face(Out, B[K], B[(K + 1) % 4], T[(K + 1) % 4], T[K], In, C);
	if (Top) Face(Out, T[0], T[1], T[2], T[3], In, C);
	if (Bottom) Face(Out, B[0], B[1], B[2], B[3], In, C);
}

// The night's light on new vertices (AlphaFrom in WorldRidePlaces: alpha 0 on the glow).
void Unlit(RideMesh& M, size_t From)
{
	if (M.Alpha.empty()) M.Alpha.assign(From, 1.0f);
	M.Alpha.resize(From);
	M.Alpha.resize(M.Vertices(), 0.0f);
}
} // namespace

void RidePlan::IndexTownLife()
{
	Forecourts.clear();
	ForecourtGrid.clear();
	FuelSeen = 0;
	if (!Scene) return;
	const Region& G = Area.Roads;

	// The roads' bits by square, with each road's half width and whether a forecourt may open off it.
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Bits;
	std::vector<double> Halves(G.Edges.size(), 0.0);
	std::vector<unsigned char> CanOpen(G.Edges.size(), 0), Motorway(G.Edges.size(), 0);
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& R = G.Edges[E];
		const std::string& Class = G.ClassOf(int(E));
		Halves[E] = RoadX::Of(G, int(E), RoadLine::WidthOf(Class)).Half;
		const RoadExtra* X = G.ExtraOf(int(E));
		CanOpen[E] = CourtRoad(Class) && !(X != nullptr && X->Has(RoadExtra::Private)) ? 1 : 0;
		Motorway[E] = Class == "motorway" || Class == "motorway_link" ? 1 : 0;
		for (size_t I = 0; I + 1 < R.Line.size(); ++I)
		{
			const P2& A = R.Line[I];
			const P2& B = R.Line[I + 1];
			const int X0 = int(std::floor(std::fmin(A.X, B.X) / BitCell)), X1 = int(std::floor(std::fmax(A.X, B.X) / BitCell));
			const int Z0 = int(std::floor(std::fmin(A.Z, B.Z) / BitCell)), Z1 = int(std::floor(std::fmax(A.Z, B.Z) / BitCell));
			for (int Cz = Z0; Cz <= Z1; ++Cz)
				for (int Cx = X0; Cx <= X1; ++Cx) Bits[static_cast<long long>(Cx) * 4000037LL + Cz].push_back({int(E), int(I)});
		}
	}
	struct Foot
	{
		int Edge = -1, Bit = 0;
		double Dist = 1e18, T = 0.0;
		P2 At;
	};
	// The nearest bit of road to P within Reach (only those a forecourt may open off if Open), but NotEdge.
	const auto NearestRoad = [&](const P2& P, double Reach, bool Open, int NotEdge = -1)
	{
		Foot Best;
		const int Cx0 = int(std::floor(P.X / BitCell)), Cz0 = int(std::floor(P.Z / BitCell));
		const int Rings = int(std::ceil(Reach / BitCell));
		for (int Ring = 0; Ring <= Rings; ++Ring)
		{
			if (Best.Edge >= 0 && Best.Dist < (Ring - 1) * BitCell) break;
			for (int Dz = -Ring; Dz <= Ring; ++Dz)
				for (int Dx = -Ring; Dx <= Ring; ++Dx)
				{
					if (std::max(std::abs(Dx), std::abs(Dz)) != Ring) continue;
					const auto It = Bits.find(static_cast<long long>(Cx0 + Dx) * 4000037LL + (Cz0 + Dz));
					if (It == Bits.end()) continue;
					for (const std::pair<int, int>& B : It->second)
					{
						if ((Open && !CanOpen[size_t(B.first)]) || B.first == NotEdge) continue;
						const std::vector<P2>& L = G.Edges[size_t(B.first)].Line;
						double T = 0.0;
						const double D = SegDistance(P, L[size_t(B.second)], L[size_t(B.second) + 1], &T);
						if (D < Best.Dist)
						{
							Best.Dist = D;
							Best.Edge = B.first;
							Best.Bit = B.second;
							Best.T = T;
							Best.At = L[size_t(B.second)] + (L[size_t(B.second) + 1] - L[size_t(B.second)]) * T;
						}
					}
				}
		}
		if (Best.Dist > Reach) Best.Edge = -1;
		return Best;
	};
	// Whether P is in (or within Spare of) one of the map's buildings, but the station's own (Own: the low,
	// small ones by the stop, its canopy or kiosk as the map has them, which the forecourt's stand in for).
	P2 Own(1e18, 1e18);
	const auto InBuilding = [&](const P2& P, double Spare)
	{
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
				{
					const Building& H = G.Buildings[size_t(B)];
					if (H.Height <= 6.5 && H.W * H.D <= 450.0 && std::hypot(H.X - Own.X, H.Z - Own.Z) < 18.0) continue;
					const double C = std::cos(H.Yaw), S = std::sin(H.Yaw), Ex = P.X - H.X, Ez = P.Z - H.Z;
					if (std::fabs(Ex * C + Ez * S) < H.W * 0.5 + Spare && std::fabs(-Ex * S + Ez * C) < H.D * 0.5 + Spare) return true;
				}
			}
		return false;
	};
	// Whether a mapped wall, fence or hedge crosses from A to B.
	const auto Walled = [&](const P2& A, const P2& B)
	{
		const P2 Mid = (A + B) * 0.5;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Lines.find(SceneKey(Mid.X + Dx * SceneCell, Mid.Z + Dz * SceneCell));
				if (It == Scene->Lines.end()) continue;
				for (int L : It->second)
				{
					const FeatureLine& Fl = G.Scenery.Lines[size_t(L)];
					if (Fl.Kind != "wall" && Fl.Kind != "fence" && Fl.Kind != "hedge" && Fl.Kind != "retaining_wall" && Fl.Kind != "city_wall") continue;
					for (size_t I = 0; I + 1 < Fl.Line.size(); ++I)
						if (Crosses(A, B, Fl.Line[I], Fl.Line[I + 1])) return true;
				}
			}
		return false;
	};
	const auto OnSpan = [&](const P2& P)
	{
		for (const Span& S : G.Spans)
			if (SegDistance(P, S.A, S.B) < 30.0) return true;
		return false;
	};
	const auto Wet = [this](const P2& P) { return Land && (Land->Cover(P.X, P.Z) == Terrain::CoverWater || Land->SeaAt(P.X, P.Z)); };

	for (const TorqueFit::Json& Poi : G.Pois)
	{
		if (Poi.Str("kind") != "fuel") continue;
		++FuelSeen;
		const P2 P(Poi.Num("x"), Poi.Num("z"));
		Own = P;
		// (one place mapped twice, or two brands side by side: one forecourt)
		bool Near = false;
		{
			const auto Cx = static_cast<long long>(std::floor(P.X / CourtCell)), Cz = static_cast<long long>(std::floor(P.Z / CourtCell));
			for (long long Dz = -2; Dz <= 2 && !Near; ++Dz)
				for (long long Dx = -2; Dx <= 2 && !Near; ++Dx)
				{
					const auto It = ForecourtGrid.find((Cx + Dx) * 4000037LL + (Cz + Dz));
					if (It == ForecourtGrid.end()) continue;
					for (int I : It->second) Near = Near || Forecourts[size_t(I)].Canopy.DistanceTo(P) < 80.0;
				}
		}
		if (Near) continue;
		// (a motorway's own: the services, stage 8's)
		const Foot Any = NearestRoad(P, 70.0, false);
		if (Any.Edge < 0 || Motorway[size_t(Any.Edge)]) continue;
		const Foot Foot0 = NearestRoad(P, 70.0, true);
		if (Foot0.Edge < 0) continue;
		const RoadEdge& R = G.Edges[size_t(Foot0.Edge)];
		const double Half = Halves[size_t(Foot0.Edge)];
		std::vector<double> Alongs(R.Line.size(), 0.0);
		for (size_t I = 1; I < R.Line.size(); ++I) Alongs[I] = Alongs[I - 1] + R.Line[I].DistanceTo(R.Line[I - 1]);
		const double Along0 = Alongs[size_t(Foot0.Bit)] + Foot0.T * (Alongs[size_t(Foot0.Bit) + 1] - Alongs[size_t(Foot0.Bit)]);
		const auto PointAt = [&](double A, P2& At, P2& D)
		{
			A = std::clamp(A, 0.0, Alongs.back());
			size_t I = 0;
			while (I + 2 < Alongs.size() && Alongs[I + 1] < A) ++I;
			const double Seg = std::fmax(1e-6, Alongs[I + 1] - Alongs[I]);
			const double U = std::clamp((A - Alongs[I]) / Seg, 0.0, 1.0);
			At = R.Line[I] + (R.Line[I + 1] - R.Line[I]) * U;
			D = (R.Line[I + 1] - R.Line[I]) * (1.0 / Seg);
		};
		P2 Mid0, Dir0;
		PointAt(Along0, Mid0, Dir0);
		const P2 Left0(Dir0.Z, -Dir0.X);
		const double SideSign = ((P.X - Mid0.X) * Left0.X + (P.Z - Mid0.Z) * Left0.Z) >= 0.0 ? 1.0 : -1.0;
		uint32_t Seed = 2166136261u;
		for (char C : Poi.Str("name")) Seed = (Seed ^ static_cast<unsigned char>(C)) * 16777619u;
		Seed ^= static_cast<uint32_t>(std::lround(P.X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(P.Z * 3.0)) * 19349663u;
		if (Seed == 0) Seed = 1;
		const int Islands = Dice(Seed, 1) < 0.35 ? 3 : 2;
		const double CanopyLong = Islands * IslandGap + 1.5;

		// Along the road from the foot, either way up to 25 m, as deep as it will go (ForecourtDepth down to
		// ForecourtLeast): clear of buildings, walls, water and other roads, off bridges, level enough.
		// ([garage] Long: the forecourt's length tried, the longer one for a garage beyond the canopy's end)
		double BestScore = 1e18, BestAlong = 0.0, BestDepth = 0.0;
		const auto Search = [&](double Long)
		{
			BestScore = 1e18;
			for (double Shift = 0.0; Shift <= 25.0; Shift += 5.0)
				for (double Way : {1.0, -1.0})
				{
					if (Shift == 0.0 && Way < 0.0) continue;
					const double A = Along0 + Way * Shift;
					if (A < 0.0 || A > Alongs.back()) continue;
					P2 Mid, D;
					PointAt(A, Mid, D);
					if (OnSpan(Mid)) continue;
					const P2 Out = P2(D.Z, -D.X) * SideSign;
					double Depth = ForecourtDepth;
					bool Bad = false;
					for (double U = -Long * 0.5 + 1.0; U <= Long * 0.5 - 1.0 && !Bad; U += 3.0)
					{
						// (laid straight along the road's way at its middle: the road must run straight with it,
						// on to the next bit of road where this one ends)
						const P2 Q = Mid + D * U;
						const P2& Oq = Out;
						const Foot Along1 = NearestRoad(Q, 3.0, false);
						if (Along1.Edge < 0 || Along1.Dist > 1.2) { Bad = true; break; }
						// (how far out before a building or a wall; another road's tarmac anywhere in it is no good)
						for (double V = 1.0; V <= Depth + 0.01; V += 2.0)
						{
							const P2 S = Q + Oq * (Half + V);
							if (InBuilding(S, 0.8) || Walled(Q + Oq * (Half + 0.3), S)) { Depth = std::fmin(Depth, V - 1.5); break; }
							if (Wet(S)) { Depth = std::fmin(Depth, V - 1.5); break; }
							const Foot Other = NearestRoad(S, 20.0, false, Foot0.Edge);
							if (Other.Edge >= 0 && Other.Dist < Halves[size_t(Other.Edge)] + 1.0) { Depth = std::fmin(Depth, V - 1.5); break; }
						}
						if (Depth < ForecourtLeast) Bad = true;
					}
					if (Bad) continue;
					// (the ground's fall across it and along it: 1 in 10 the most)
					const double Y0 = Ground(Mid.X + Out.X * (Half + 1.0), Mid.Z + Out.Z * (Half + 1.0));
					const double Y1 = Ground(Mid.X + Out.X * (Half + Depth), Mid.Z + Out.Z * (Half + Depth));
					const P2 Ea = Mid + D * (-Long * 0.5 + 2.0), Eb = Mid + D * (Long * 0.5 - 2.0);
					const double Ya = Ground(Ea.X + Out.X * (Half + Depth * 0.5), Ea.Z + Out.Z * (Half + Depth * 0.5));
					const double Yb = Ground(Eb.X + Out.X * (Half + Depth * 0.5), Eb.Z + Out.Z * (Half + Depth * 0.5));
					if (std::isnan(Y0) || std::isnan(Y1) || std::isnan(Ya) || std::isnan(Yb)) continue;
					const double Fall = std::fmax(std::fabs(Y1 - Y0) / Depth, std::fabs(Yb - Ya) / (Long - 4.0));
					if (Fall > 0.1) continue;
					const double Score = Fall * 60.0 + Shift * 0.1 + (ForecourtDepth - Depth) * 0.5;
					if (Score < BestScore) { BestScore = Score; BestAlong = A; BestDepth = Depth; }
				}
		};
		double Long = CanopyLong + 12.0;   // (6 m either end: the way in and the way out)
		Search(Long);
		// [garage] Too shallow for the kiosk row (where the garage goes beside the kiosk): tried again
		// longer, the garage beyond the canopy's end; failing that, as it was, with no garage.
		bool EndGarage = false;
		if (BestScore < 1e17 && BestDepth < KioskNeed)
		{
			const double Was = BestScore, WasAlong = BestAlong, WasDepth = BestDepth;
			Search(Long + 2.0 * GarageEnd);
			if (BestScore < 1e17 && BestDepth >= GarageEndNeed) { EndGarage = true; Long += 2.0 * GarageEnd; }
			else { BestScore = Was; BestAlong = WasAlong; BestDepth = WasDepth; }
		}
		if (BestScore >= 1e17) continue;
		Forecourt F;
		F.Half = Half;
		F.Depth = BestDepth;
		F.Length = Long;
		F.CanopyLong = CanopyLong;
		F.Islands = Islands;
		F.Seed = Seed;
		F.Name = Poi.Str("name");
		P2 Mid, D;
		PointAt(BestAlong, Mid, D);
		F.RoadMid = Mid;
		F.RoadDir = D;
		F.Out = P2(D.Z, -D.X) * SideSign;
		// (the kiosk where there's room behind the canopy: the map's own, if it has one there, is hidden under the forecourt)
		F.Kiosk = BestDepth >= KioskNeed;
		// [garage] In the kiosk's row (the kiosk shortened to share it), at the end away from the price
		// board; else beyond the canopy's end there, its front back from the lane.
		if (F.Kiosk)
		{
			const double Row = std::fmin(CanopyLong, KioskRow);
			F.Garage = true;
			F.GarageU = -Row * 0.5 + RidePlan::GarageWide * 0.5;
			F.GarageV = EntryLane + CanopyDeep + 1.5;
		}
		else if (EndGarage)
		{
			F.Garage = true;
			F.GarageU = -CanopyLong * 0.5 - 1.5 - RidePlan::GarageWide * 0.5;
			F.GarageV = EntryLane + 0.5;
		}
		// (its outline, along the road's edge, the ends tapering in as a lay-by's: the field it makes)
		const double Taper = 5.0;
		std::vector<P2> Inner, Outer;
		for (int K = 0; K <= 12; ++K)
		{
			const double U = double(K) / 12.0, A = BestAlong + (U - 0.5) * Long;
			const P2 Q = Mid + D * (A - BestAlong);
			const P2& Oq = F.Out;
			const double FromEnd = std::fmin(U, 1.0 - U) * Long;
			const double Deep = BestDepth * std::clamp(FromEnd / Taper, 0.15, 1.0);
			Inner.push_back(Q + Oq * (Half - 0.1));
			Outer.push_back(Q + Oq * (Half + Deep));
		}
		F.Ring = Inner;
		for (auto It = Outer.rbegin(); It != Outer.rend(); ++It) F.Ring.push_back(*It);
		F.Canopy = F.RoadMid + F.Out * (Half + EntryLane + CanopyDeep * 0.5);
		const double Gy = Ground(F.Canopy.X, F.Canopy.Z);
		F.Base = std::isnan(Gy) ? 0.0 : Gy;
		// (a wall or hedge the map has along the road's edge there, or across the forecourt: a gap in it the
		// forecourt's length, as a field gate's, so the way in is open)
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Lines.find(SceneKey(F.Canopy.X + Dx * SceneCell, F.Canopy.Z + Dz * SceneCell));
				if (It == Scene->Lines.end()) continue;
				for (int L : It->second)
				{
					const FeatureLine& Fl = G.Scenery.Lines[size_t(L)];
					if (Fl.Kind != "wall" && Fl.Kind != "fence" && Fl.Kind != "hedge" && Fl.Kind != "retaining_wall") continue;
					for (size_t K = 0; K + 1 < Fl.Line.size(); ++K)
					{
						const double Run = Fl.Line[K].DistanceTo(Fl.Line[K + 1]);
						const int Steps = std::max(1, int(std::ceil(Run / 1.5)));
						for (int St = 0; St <= Steps; ++St)
						{
							const P2 Q = Fl.Line[K] + (Fl.Line[K + 1] - Fl.Line[K]) * (double(St) / Steps);
							const P2 Rel = Q - Mid;
							const double U = Rel.X * D.X + Rel.Z * D.Z, V = Rel.X * F.Out.X + Rel.Z * F.Out.Z - Half;
							if (std::fabs(U) > Long * 0.5 || V < -1.5 || V > BestDepth) continue;
							Scene->Gaps[SceneKey(Q.X, Q.Z)].push_back(Q);
							Scene->GapWidths[SceneKey(Q.X, Q.Z)].push_back(3.2);
						}
					}
				}
			}
		ForecourtGrid[CourtKey(F.Canopy.X, F.Canopy.Z)].push_back(int(Forecourts.size()));
		Forecourts.push_back(std::move(F));
	}

	// [everyday kits stage 5b] The lay-bys and roadside cafes (RidePlan::RoadLaybys, RoadsideThings: World/WorldRideRoadside.cpp
	// lays their things), from the kits bake's things, made as the forecourts are.
	RoadLaybys.clear();
	LaybyGrid.clear();
	RoadsideThings.clear();
	RoadsideGrid.clear();
	LaybysSeen = RoadCafes = 0;
	if (RoadsideOn())
	{
		Own = P2(1e18, 1e18);
		const auto AddRoadside = [this](int Kind, const P2& At, const P2& Front, int Variant, uint32_t Id)
		{
			RoadsideThing T;
			T.Kind = Kind;
			T.At = At;
			T.Front = Front;
			T.Variant = Variant;
			T.Id = Id;
			RoadsideGrid[CourtKey(At.X, At.Z)].push_back(int(RoadsideThings.size()));
			RoadsideThings.push_back(T);
		};
		const auto NearLayby = [this](const P2& P, double Within)
		{
			for (const Layby& L : RoadLaybys)
				if (L.RoadMid.DistanceTo(P) < Within) return true;
			return false;
		};
		// (somewhere a thing may stand: no building, water or road's tarmac, the ground there)
		const auto Clear = [&](const P2& Q, double Spare)
		{
			if (InBuilding(Q, Spare) || Wet(Q) || OnForecourt(Q.X, Q.Z) || std::isnan(Ground(Q.X, Q.Z))) return false;
			const Foot R0 = NearestRoad(Q, 20.0, false);
			return R0.Edge < 0 || R0.Dist > Halves[size_t(R0.Edge)] + Spare + 0.4;
		};
		const auto ClassIs = [&G](int Edge, std::initializer_list<const char*> Classes)
		{
			const std::string& C = G.ClassOf(Edge);
			for (const char* K : Classes)
				if (C == K) return true;
			return false;
		};
		const auto MiddleOf = [](const GroundShape& T)
		{
			P2 C(0.0, 0.0);
			for (const P2& P : T.Pts) C = C + P;
			return C * (1.0 / double(T.Pts.size()));
		};
		for (const GroundShape& T : G.KitThings)
		{
			if (T.Kind != "layby" || T.Word != "layby" || T.Pts.empty()) continue;
			++LaybysSeen;
			const P2 P = MiddleOf(T);
			if (NearLayby(P, 50.0) || OnForecourt(P.X, P.Z) || OnPullOff(P.X, P.Z)) continue;
			const Foot Any = NearestRoad(P, 40.0, false);
			if (Any.Edge < 0 || Motorway[size_t(Any.Edge)]) continue;
			const Foot Foot0 = NearestRoad(P, 40.0, true);
			if (Foot0.Edge < 0 || ClassIs(Foot0.Edge, {"residential", "road", "unknown"})) continue;
			const RoadEdge& R = G.Edges[size_t(Foot0.Edge)];
			const double Half = Halves[size_t(Foot0.Edge)];
			std::vector<double> Alongs(R.Line.size(), 0.0);
			for (size_t I = 1; I < R.Line.size(); ++I) Alongs[I] = Alongs[I - 1] + R.Line[I].DistanceTo(R.Line[I - 1]);
			if (Alongs.back() < LaybyLong + 10.0) continue;
			const double Along0 = Alongs[size_t(Foot0.Bit)] + Foot0.T * (Alongs[size_t(Foot0.Bit) + 1] - Alongs[size_t(Foot0.Bit)]);
			const auto PointAt = [&](double A, P2& At, P2& D)
			{
				A = std::clamp(A, 0.0, Alongs.back());
				size_t I = 0;
				while (I + 2 < Alongs.size() && Alongs[I + 1] < A) ++I;
				const double Seg = std::fmax(1e-6, Alongs[I + 1] - Alongs[I]);
				const double U = std::clamp((A - Alongs[I]) / Seg, 0.0, 1.0);
				At = R.Line[I] + (R.Line[I + 1] - R.Line[I]) * U;
				D = (R.Line[I + 1] - R.Line[I]) * (1.0 / Seg);
			};
			P2 Mid0, Dir0;
			PointAt(Along0, Mid0, Dir0);
			const P2 Left0(Dir0.Z, -Dir0.X);
			const double SideSign = ((P.X - Mid0.X) * Left0.X + (P.Z - Mid0.Z) * Left0.Z) >= 0.0 ? 1.0 : -1.0;
			// (along the road from the foot, either way up to 15 m, as deep as it will go: as a forecourt's, but its
			// ends 5 m clear of the road's own ends, its junctions)
			double BestScore = 1e18, BestAlong = 0.0, BestDepth = 0.0;
			for (double Shift = 0.0; Shift <= 15.0; Shift += 5.0)
				for (double Way : {1.0, -1.0})
				{
					if (Shift == 0.0 && Way < 0.0) continue;
					const double A = Along0 + Way * Shift;
					if (A < LaybyLong * 0.5 + 5.0 || A > Alongs.back() - LaybyLong * 0.5 - 5.0) continue;
					P2 Mid, D;
					PointAt(A, Mid, D);
					if (OnSpan(Mid)) continue;
					const P2 Out = P2(D.Z, -D.X) * SideSign;
					double Depth = LaybyDeep;
					bool Bad = false;
					for (double U = -LaybyLong * 0.5 + 1.0; U <= LaybyLong * 0.5 - 1.0 && !Bad; U += 3.0)
					{
						const P2 Q = Mid + D * U;
						const Foot Along1 = NearestRoad(Q, 3.0, false);
						if (Along1.Edge < 0 || Along1.Dist > 0.8) { Bad = true; break; }
						for (double V = 0.5; V <= Depth + 0.01; V += 0.9)
						{
							const P2 S = Q + Out * (Half + V);
							if (InBuilding(S, 0.5) || Walled(Q + Out * (Half + 0.2), S) || Wet(S)) { Depth = std::fmin(Depth, V - 0.6); break; }
							const Foot Other = NearestRoad(S, 20.0, false, Foot0.Edge);
							if (Other.Edge >= 0 && Other.Dist < Halves[size_t(Other.Edge)] + 1.0) { Depth = std::fmin(Depth, V - 0.6); break; }
						}
						if (Depth < LaybyLeast) Bad = true;
					}
					if (Bad) continue;
					const double Y0 = Ground(Mid.X + Out.X * (Half + 0.5), Mid.Z + Out.Z * (Half + 0.5));
					const double Y1 = Ground(Mid.X + Out.X * (Half + Depth), Mid.Z + Out.Z * (Half + Depth));
					const P2 Ea = Mid + D * (-LaybyLong * 0.5 + 3.0), Eb = Mid + D * (LaybyLong * 0.5 - 3.0);
					const double Ya = Ground(Ea.X + Out.X * (Half + Depth * 0.5), Ea.Z + Out.Z * (Half + Depth * 0.5));
					const double Yb = Ground(Eb.X + Out.X * (Half + Depth * 0.5), Eb.Z + Out.Z * (Half + Depth * 0.5));
					if (std::isnan(Y0) || std::isnan(Y1) || std::isnan(Ya) || std::isnan(Yb)) continue;
					const double Fall = std::fmax(std::fabs(Y1 - Y0) / Depth, std::fabs(Yb - Ya) / (LaybyLong - 6.0));
					if (Fall > 0.12) continue;
					const double Score = Fall * 60.0 + Shift * 0.1 + (LaybyDeep - Depth);
					if (Score < BestScore) { BestScore = Score; BestAlong = A; BestDepth = Depth; }
				}
			if (BestScore >= 1e17) continue;
			Layby L;
			L.Half = Half;
			L.Depth = BestDepth;
			L.Seed = static_cast<uint32_t>(std::lround(P.X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(P.Z * 3.0)) * 19349663u;
			if (L.Seed == 0) L.Seed = 1;
			P2 Mid, D;
			PointAt(BestAlong, Mid, D);
			L.RoadMid = Mid;
			L.RoadDir = D;
			L.Out = P2(D.Z, -D.X) * SideSign;
			// (its outline: the road's edge, its ends tapering in over 8 m)
			const double Taper = 8.0;
			std::vector<P2> Inner, Outer;
			for (int K = 0; K <= 12; ++K)
			{
				const double U = double(K) / 12.0, A = (U - 0.5) * LaybyLong;
				const P2 Q = Mid + D * A;
				const double FromEnd = std::fmin(U, 1.0 - U) * LaybyLong;
				const double Deep = BestDepth * std::clamp(FromEnd / Taper, 0.12, 1.0);
				Inner.push_back(Q + L.Out * (Half - 0.1));
				Outer.push_back(Q + L.Out * (Half + Deep));
			}
			L.Ring = Inner;
			for (auto It = Outer.rbegin(); It != Outer.rend(); ++It) L.Ring.push_back(*It);
			// (its things: traffic on its side comes along Way, the left its side)
			const P2 Way(-L.Out.Z, L.Out.X);
			const P2 Back = L.Out * -1.0;
			const P2 Bin = Mid + Way * (LaybyLong * 0.5 - Taper - 1.0) + L.Out * (Half + BestDepth + 0.6);
			if (Clear(Bin, 0.2)) AddRoadside(PropLitterBin, Bin, Back, int(Dice(L.Seed, 21) * 2.0), L.Seed * 7u + 1u);
			const P2 Sign = Mid - Way * (LaybyLong * 0.5 + 4.0) + L.Out * (Half + 1.0);
			if (Clear(Sign, 0.2)) AddRoadside(PropRoadLaybySign, Sign, Way * -1.0, 0, L.Seed * 7u + 2u);
			const bool ARoad = ClassIs(Foot0.Edge, {"trunk", "primary", "trunk_link", "primary_link"});
			if (ARoad && Dice(L.Seed, 22) < 0.3)
			{
				// (a catering trailer on the verge behind its middle, its hatch to the lay-by, a flag at its end)
				const P2 Van = Mid + Way * 3.0 + L.Out * (Half + BestDepth + 1.4);
				bool Room = true;
				for (double U = -2.6; U <= 2.6 && Room; U += 1.3)
					Room = Clear(Van + Way * U, 0.3) && Clear(Van + Way * U + L.Out * 1.0, 0.3) && !Walled(Mid + Way * (3.0 + U) + L.Out * (Half + BestDepth - 0.2), Van + Way * U + L.Out * 1.2);
				if (Room)
				{
					AddRoadside(PropRoadBurgerVan, Van, Back, int(Dice(L.Seed, 23) * 2.0), L.Seed * 7u + 3u);
					const P2 Flag = Mid - Way * 2.0 + L.Out * (Half + BestDepth + 0.5);
					if (Clear(Flag, 0.1)) AddRoadside(PropRoadFlag, Flag, Way, 0, L.Seed * 7u + 4u);
				}
			}
			else if (Dice(L.Seed, 24) < 0.35)
			{
				const P2 Table = Mid - Way * 4.0 + L.Out * (Half + BestDepth + 2.2);
				if (Clear(Table, 1.0) && Clear(Table + L.Out * 1.0, 0.5) && !Walled(Mid - Way * 4.0 + L.Out * (Half + BestDepth - 0.2), Table + L.Out * 1.2))
					AddRoadside(PropPicnicTable, Table, Way, 0, L.Seed * 7u + 5u);
			}
			L.Tarmac = ClassIs(Foot0.Edge, {"trunk", "primary", "secondary", "trunk_link", "primary_link", "secondary_link"});
			LaybyGrid[CourtKey(Mid.X, Mid.Z)].push_back(int(RoadLaybys.size()));
			RoadLaybys.push_back(std::move(L));
		}
		// (the roadside cafes: out of town, by an A or B road; their boards 60 m before them each way on the left verge)
		for (const GroundShape& T : G.KitThings)
		{
			if ((T.Kind != "cafe" && T.Kind != "fast_food") || T.Pts.empty()) continue;
			const P2 P = MiddleOf(T);
			const Foot Near = NearestRoad(P, 60.0, false);
			if (Near.Edge < 0 || Motorway[size_t(Near.Edge)] || !ClassIs(Near.Edge, {"trunk", "primary", "secondary", "trunk_link", "primary_link", "secondary_link"})) continue;
			int Houses = 0;
			for (int Dz = -2; Dz <= 2; ++Dz)
				for (int Dx = -2; Dx <= 2; ++Dx)
				{
					const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
					if (It == Scene->Buildings.end()) continue;
					for (int B : It->second)
						if (std::hypot(G.Buildings[size_t(B)].X - P.X, G.Buildings[size_t(B)].Z - P.Z) < 100.0) ++Houses;
				}
			if (Houses > 12) continue;
			const RoadEdge& R = G.Edges[size_t(Near.Edge)];
			const P2 D0 = R.Line[size_t(Near.Bit) + 1] - R.Line[size_t(Near.Bit)];
			const double Dl = D0.Length();
			if (Dl < 1e-6) continue;
			const P2 D = D0 * (1.0 / Dl);
			const uint32_t Seed = static_cast<uint32_t>(std::lround(P.X * 3.0)) * 83492791u ^ static_cast<uint32_t>(std::lround(P.Z * 3.0)) * 19349663u;
			const double Half = Halves[size_t(Near.Edge)];
			bool Any = false;
			for (double S : {1.0, -1.0})
			{
				// (coming along Come: its left verge; the cafe to the left or right of it, the board's arrow that way)
				const P2 Come = D * S, Left(Come.Z, -Come.X);
				const P2 From = Near.At - Come * 60.0;
				const Foot There = NearestRoad(From, 6.0, false);
				if (There.Edge != Near.Edge) continue;
				const P2 Board = There.At + Left * (Half + 1.6);
				if (!Clear(Board, 0.4) || Walled(There.At + Left * (Half + 0.2), Board)) continue;
				const bool ToLeft = (P.X - Near.At.X) * Left.X + (P.Z - Near.At.Z) * Left.Z > 0.0;
				// (facing the traffic coming, turned a little to the road)
				P2 Face = Come * -1.0 - Left * 0.35;
				Face = Face * (1.0 / Face.Length());
				AddRoadside(PropRoadCafeSign, Board, Face, (ToLeft ? 2 : 0) + int(Dice(Seed, 31) * 2.0), Seed * 5u + (S > 0.0 ? 1u : 2u));
				Any = true;
			}
			if (T.Word == "van" && Near.Dist > Half + 2.5 && Clear(P, 0.5))
			{
				const P2 To = Near.At - P;
				AddRoadside(PropRoadBurgerVan, P, To * (1.0 / std::fmax(1e-6, To.Length())), int(Dice(Seed, 32) * 2.0), Seed * 5u + 3u);
			}
			RoadCafes += Any ? 1 : 0;
		}
	}

	// [everyday kits stage 8] The fast road set's things beside and over the motorways and dual carriageways (laid by
	// RoadsideProps with the lay-bys' things): the kits bake's sign gantries (lines across a carriageway), speed cameras
	// and emergency phones where the map has them, and a marker post every 100 m along a motorway's or a dual trunk
	// road's nearside verge.
	FastGantries = FastCameras = FastPhones = FastMarkers = 0;
	{
		const auto AddFast = [this](int Kind, const P2& At, const P2& Front, int Variant, uint32_t Id, double Across, double Y)
		{
			RoadsideThing T;
			T.Kind = Kind;
			T.At = At;
			T.Front = Front;
			T.Variant = Variant;
			T.Id = Id;
			T.Across = float(Across);
			T.Y = Y;
			RoadsideGrid[CourtKey(At.X, At.Z)].push_back(int(RoadsideThings.size()));
			RoadsideThings.push_back(T);
		};
		const auto Fast = [&](int Edge)
		{
			const std::string& C = G.ClassOf(Edge);
			return Motorway[size_t(Edge)] != 0 || C == "trunk" || C == "primary" || C == "trunk_link";
		};
		// (a road's way at a foot, and the way the traffic beside it on the side Side (+1 its left as its line runs)
		// goes: a one-way road's own way; on a two-way road the left side's along its line, Britain driving on the left)
		const auto WayAt = [&](const Foot& F)
		{
			const std::vector<P2>& L = G.Edges[size_t(F.Edge)].Line;
			P2 D = L[size_t(F.Bit) + 1] - L[size_t(F.Bit)];
			return D * (1.0 / std::fmax(1e-6, D.Length()));
		};
		const auto Traffic = [&](const Foot& F, double Side)
		{
			const int One = G.Edges[size_t(F.Edge)].Oneway;
			const P2 D = WayAt(F);
			return One != 0 ? D * double(One) : D * Side;
		};
		const auto Standing = [&](const P2& Q, double Spare)
		{
			if (InBuilding(Q, Spare) || Wet(Q) || OnSpan(Q) || OnForecourt(Q.X, Q.Z) || std::isnan(Ground(Q.X, Q.Z))) return false;
			const Foot R0 = NearestRoad(Q, 20.0, false);
			return R0.Edge < 0 || R0.Dist > Halves[size_t(R0.Edge)] + Spare;
		};
		uint32_t Seq = 1;
		for (const GroundShape& T : G.KitThings)
		{
			if (T.Pts.empty()) continue;
			++Seq;
			if (T.Kind == "gantry" && T.Pts.size() >= 2)
			{
				// (where the map has it, square across the carriageway under it (the map's line is often only part of
				// the way across), its legs 1.5 m outside its edges; its front to the traffic coming)
				const P2 A0 = T.Pts.front(), B0 = T.Pts.back(), Mid0 = (A0 + B0) * 0.5;
				if (A0.DistanceTo(B0) > 60.0 || OnSpan(Mid0)) continue;
				const Foot F = NearestRoad(Mid0, 15.0, false);
				if (F.Edge < 0 || !Fast(F.Edge) || OnSpan(F.At)) continue;
				const P2 Way = Traffic(F, 1.0);
				const P2 Front = Way * -1.0;
				const P2 Across(Way.Z, -Way.X);
				const double Span = Halves[size_t(F.Edge)] * 2.0 + 3.0;
				const P2 A = F.At + Across * (Span * 0.5), B = F.At - Across * (Span * 0.5);
				static constexpr double Spans[3] = {14.0, 20.0, 27.0};
				int V = 0;
				for (int K = 1; K < 3; ++K) if (std::fabs(Spans[K] - Span) < std::fabs(Spans[V] - Span)) V = K;
				const double Ya = Ground(A.X, A.Z), Yb = Ground(B.X, B.Z), Ym = Ground(F.At.X, F.At.Z);
				if (std::isnan(Ya) || std::isnan(Yb) || std::isnan(Ym)) continue;
				AddFast(PropFastGantry, F.At, Front, V, Seq * 11u + 1u, std::clamp(Span / Spans[V], 0.6, 1.5), std::fmin(Ym, std::fmin(Ya, Yb)));
				++FastGantries;
			}
			else if ((T.Kind == "speed_camera" && T.Word != "traffic_signals") || T.Kind == "emergency_phone")
			{
				// (on the verge beside the road it watches or serves, on the side the map has it (a point in the road: the
				// left as the traffic goes), the camera's lens to the traffic coming, the phone's door to the road)
				const bool Phone = T.Kind == "emergency_phone";
				const P2 P = T.Pts.front();
				const Foot F = NearestRoad(P, Phone ? 30.0 : 25.0, false);
				if (F.Edge < 0 || (Phone && !Fast(F.Edge))) continue;
				const P2 D = WayAt(F);
				const P2 Left(D.Z, -D.X);
				const double Half = Halves[size_t(F.Edge)];
				const double Off = (P.X - F.At.X) * Left.X + (P.Z - F.At.Z) * Left.Z;
				double Side = Off >= 0.0 ? 1.0 : -1.0;
				if (F.Dist < Half) Side = G.Edges[size_t(F.Edge)].Oneway < 0 ? -1.0 : 1.0;
				const double Out = std::clamp(F.Dist, Half + (Phone ? 2.2 : 1.6), Half + 5.0);
				const P2 At = F.At + Left * (Side * Out);
				if (!Standing(At, 0.6)) continue;
				const P2 Front = Phone ? Left * -Side : Traffic(F, Side) * -1.0;
				AddFast(Phone ? PropFastPhone : PropFastCamera, At, Front, 0, Seq * 13u + 5u, 1.0, std::numeric_limits<double>::quiet_NaN());
				++(Phone ? FastPhones : FastCameras);
			}
		}
		// (the marker posts: every 100 m along each one-way motorway or dual trunk road, 1.2 m out from its nearside edge,
		// none in its first or last 40 m (its junctions), on a bridge, or where something else stands)
		for (size_t E = 0; E < G.Edges.size(); ++E)
		{
			const RoadEdge& R = G.Edges[E];
			if (R.Oneway == 0 || R.Line.size() < 2) continue;
			const std::string& C = G.ClassOf(int(E));
			if (!(C == "motorway" || C == "trunk")) continue;
			double Along = 0.0;
			const double Total = R.Length > 0.0 ? R.Length : 0.0;
			double Next = 100.0 - std::fmod(std::fabs(R.Line.front().X * 0.37 + R.Line.front().Z * 0.61), 100.0);
			for (size_t I = 0; I + 1 < R.Line.size(); ++I)
			{
				const P2 A = R.Line[I], B = R.Line[I + 1];
				const double L = A.DistanceTo(B);
				while (L > 1e-6 && Next <= Along + L)
				{
					const double U = (Next - Along) / L;
					Next += 100.0;
					const double At0 = Along + U * L;
					if (At0 < 40.0 || (Total > 0.0 && At0 > Total - 40.0)) continue;
					const P2 Way = (B - A) * (double(R.Oneway) / L);
					const P2 Near(Way.Z, -Way.X);   // (the traffic's left: its nearside)
					const P2 Q = A + (B - A) * U + Near * (Halves[E] + 1.2);
					if (!Standing(Q, 0.9)) continue;
					AddFast(PropFastMarker, Q, Way * -1.0, 0, uint32_t(E) * 977u + uint32_t(Next), 1.0, std::numeric_limits<double>::quiet_NaN());
					++FastMarkers;
				}
				Along += L;
			}
		}
	}

	// The things. (a thing's square, and how it is filed)
	TownThings.clear();
	ThingGrid.clear();
	for (int& C : ThingCount) C = 0;
	CampsSeen = CampsMade = 0;
	const auto AddThing = [this](TownThing T)
	{
		ThingGrid[CourtKey(T.At.X, T.At.Z)].push_back(int(TownThings.size()));
		++ThingCount[T.Kind];
		TownThings.push_back(T);
	};
	// (open ground for a pitch: no building, road, forecourt, wall or water within reach, level enough)
	const auto Open = [&](const P2& Q, double Reach)
	{
		if (InBuilding(Q, Reach) || Wet(Q) || OnForecourt(Q.X, Q.Z)) return false;
		const Foot R0 = NearestRoad(Q, 30.0, false);
		if (R0.Edge >= 0 && R0.Dist < Halves[size_t(R0.Edge)] + Reach + 2.0) return false;
		if (Walled(Q + P2(-Reach, 0.0), Q + P2(Reach, 0.0)) || Walled(Q + P2(0.0, -Reach), Q + P2(0.0, Reach))) return false;
		const double Ya = Ground(Q.X - Reach, Q.Z), Yb = Ground(Q.X + Reach, Q.Z), Yc = Ground(Q.X, Q.Z - Reach), Yd = Ground(Q.X, Q.Z + Reach);
		if (std::isnan(Ya) || std::isnan(Yb) || std::isnan(Yc) || std::isnan(Yd)) return false;
		return std::fmax(std::fabs(Yb - Ya), std::fabs(Yd - Yc)) / (2.0 * Reach) < 0.12;
	};
	// The campsites: their pitches round the map's point, within 45 m, at least 9 m apart; touring caravans
	// and tents (a site's mix by hash), lined up roughly the one way, as a field of pitches is.
	std::vector<P2> Camps;
	for (const TorqueFit::Json& Poi : G.Pois)
	{
		if (Poi.Str("kind") != "campsite") continue;
		++CampsSeen;
		const P2 P(Poi.Num("x"), Poi.Num("z"));
		bool Twice = false;
		for (const P2& C : Camps) Twice = Twice || C.DistanceTo(P) < 120.0;
		if (Twice) continue;
		Camps.push_back(P);
		uint32_t Seed = 2166136261u ^ static_cast<uint32_t>(std::lround(P.X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(P.Z * 3.0)) * 19349663u;
		if (Seed == 0) Seed = 1;
		// [caravans] A holiday park (its name says so, and not a touring or camping one): rows of statics and
		// lodges, side by side 10 m apart, 15 m along, a gap here and there, out to 70 m round its point.
		std::string Lower = Poi.Str("name");
		for (char& Ch : Lower) Ch = char(std::tolower(static_cast<unsigned char>(Ch)));
		const auto Says = [&Lower](const char* W) { return Lower.find(W) != std::string::npos; };
		const bool Holiday = (Says("caravan park") || Says("holiday park") || Says("holiday village") || Says("holiday home") || Says("lodge") || Says("leisure park"))
			&& !Says("camping") && !Says("campsite") && !Says("club") && !Says("touring") && !Says("motorhome");
		if (Holiday)
		{
			const double ParkYaw = Dice(Seed, 2) * 3.14159265;
			const P2 U(std::cos(ParkYaw), std::sin(ParkYaw)), V(-U.Z, U.X);
			const double Ou = Dice(Seed, 3) * 15.0, Ov = Dice(Seed, 4) * 10.0;
			int Made = 0;
			for (int Iv = -7; Iv <= 7; ++Iv)
				for (int Iu = -5; Iu <= 5 && Made < 48; ++Iu)
				{
					const double Du = Iu * 15.0 + Ou - 7.5, Dv = Iv * 10.0 + Ov - 5.0;
					if (Du * Du + Dv * Dv > 70.0 * 70.0) continue;
					const uint32_t Salt = 1000u + uint32_t((Iv + 8) * 16 + Iu + 8);
					if (Dice(Seed, Salt) < 0.18) continue;   // (an empty plot, a path, a play area)
					const P2 Q = P + U * Du + V * Dv;
					if (!Open(Q, 6.0)) continue;
					TownThing T;
					T.Kind = ThingStatic;
					T.At = Q;
					T.Yaw = ParkYaw + (Dice(Seed, Salt + 400u) - 0.5) * 0.08 + (Dice(Seed, Salt + 800u) < 0.5 ? 3.14159265 : 0.0);
					T.Base = Ground(Q.X, Q.Z);
					T.Seed = Seed * 31u + Salt;
					AddThing(T);
					++Made;
				}
			if (Made > 0) { ++CampsMade; continue; }
		}
		const double Caravans = Dice(Seed, 1);   // (the share of caravans to tents)
		const double SiteYaw = Dice(Seed, 2) * 3.14159265;
		const int Want = 6 + int(Dice(Seed, 3) * 10.0);
		const int Statics = Dice(Seed, 4) < 0.3 ? 1 + int(Dice(Seed, 5) * 3.0) : 0;
		std::vector<P2> Taken;
		int Made = 0;
		for (int Try = 0; Try < Want * 6 && Made < Want + Statics; ++Try)
		{
			const double A = Dice(Seed, 100u + uint32_t(Try)) * 6.2831853, Rr = 45.0 * std::sqrt(Dice(Seed, 300u + uint32_t(Try)));
			const P2 Q = P + P2(std::cos(A) * Rr, std::sin(A) * Rr);
			bool Close = false;
			for (const P2& T : Taken) Close = Close || T.DistanceTo(Q) < 9.0;
			const bool Static = Made < Statics;
			if (Close || !Open(Q, Static ? 6.0 : 4.0)) continue;
			TownThing T;
			T.Kind = Static ? ThingStatic : Dice(Seed, 500u + uint32_t(Try)) < Caravans ? ThingCaravan : ThingTent;
			T.At = Q;
			T.Yaw = SiteYaw + (Dice(Seed, 700u + uint32_t(Try)) - 0.5) * 0.5;
			T.Base = Ground(Q.X, Q.Z);
			T.Seed = Seed * 31u + uint32_t(Try);
			AddThing(T);
			Taken.push_back(Q);
			++Made;
		}
		if (Made > 0) ++CampsMade;
	}
	// The fountains (a point, or an outline: its middle and size) and the chargers, where the map has them.
	for (const FeaturePoint& Fp : G.Scenery.Points)
	{
		if (Fp.Kind != "fountain" && Fp.Kind != "charging_station") continue;
		if (InBuilding(Fp.At, -0.5)) continue;
		bool Outlined = false;   // (a fountain the map also has the outline of: drawn from that)
		for (const FeatureArea& Fa : G.Scenery.Areas)
			Outlined = Outlined || (Fp.Kind == "fountain" && Fa.Kind == "fountain" && Fa.Ring.size() >= 3 && Fa.Ring[0].DistanceTo(Fp.At) < 60.0 && RideShapes::InRing(Fa.Ring, Fp.At.X, Fp.At.Z));
		if (Outlined) continue;
		const double Gy = Ground(Fp.At.X, Fp.At.Z);
		if (std::isnan(Gy)) continue;
		TownThing T;
		T.Kind = Fp.Kind == "fountain" ? ThingFountain : ThingCharger;
		T.At = Fp.At;
		T.Base = Gy;
		T.Size = 1.6;
		T.Seed = static_cast<uint32_t>(std::lround(Fp.At.X * 7.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Fp.At.Z * 7.0)) * 19349663u;
		// (a charger faces the nearest road)
		const Foot R0 = NearestRoad(Fp.At, 40.0, false);
		if (R0.Edge >= 0)
		{
			const P2 To = R0.At - Fp.At;
			T.Yaw = std::atan2(To.Z, To.X);
		}
		AddThing(T);
	}
	for (const FeatureArea& Fa : G.Scenery.Areas)
	{
		if (Fa.Kind != "fountain" || Fa.Ring.size() < 3) continue;
		P2 Mid;
		const double Area2 = std::fabs(RideShapes::RingArea(Fa.Ring, Mid));
		const double Gy = Ground(Mid.X, Mid.Z);
		if (std::isnan(Gy) || Area2 > 2500.0) continue;
		TownThing T;
		T.Kind = ThingFountain;
		T.At = Mid;
		T.Base = Gy;
		T.Size = std::clamp(std::sqrt(Area2 / 3.14159265), 1.2, 14.0);
		T.Seed = static_cast<uint32_t>(std::lround(Mid.X * 7.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Mid.Z * 7.0)) * 19349663u;
		AddThing(T);
	}

	// [roads stage 8] The runways, taxiways and piers, and the aprons.
	AirLines.clear();
	AirGrid.clear();
	Aprons.clear();
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& Fl = G.Scenery.Lines[L];
		const bool Runway = Fl.Kind == "runway", Taxi = Fl.Kind == "taxiway", Pier = Fl.Kind == "pier";
		if ((!Runway && !Taxi && !Pier) || Fl.Line.size() < 2) continue;
		AirLine A;
		A.Line = int(L);
		for (size_t K = 1; K < Fl.Line.size(); ++K) A.Length += Fl.Line[K].DistanceTo(Fl.Line[K - 1]);
		if (A.Length < (Pier ? 15.0 : 100.0) || A.Length > 5000.0) continue;
		const double W = std::isnan(Fl.Width) ? 0.0 : Fl.Width;
		if (Runway)
		{
			// (a long or wide one is a hard runway; a short narrow one a farm or club's grass strip)
			A.Kind = A.Length >= 1000.0 || W >= 30.0 ? 0 : 1;
			A.Width = W > 8.0 ? std::fmin(W, 60.0) : A.Kind == 0 ? 45.0 : 25.0;
		}
		else if (Taxi)
		{
			A.Kind = 2;
			A.Width = W > 5.0 ? std::fmin(W, 30.0) : 15.0;
		}
		else
		{
			A.Kind = 3;
			A.Width = W > 1.0 ? std::fmin(W, 20.0) : 3.0;
			const double Ya = Ground(Fl.Line.front().X, Fl.Line.front().Z), Yb = Ground(Fl.Line.back().X, Fl.Line.back().Z);
			if (std::isnan(Ya) && std::isnan(Yb)) continue;
			A.Deck = std::fmax(std::isnan(Ya) ? -1e9 : Ya, std::isnan(Yb) ? -1e9 : Yb) + 0.8;
		}
		const int Index = int(AirLines.size());
		for (size_t K = 0; K + 1 < Fl.Line.size(); ++K)
		{
			const P2& Pa = Fl.Line[K];
			const P2& Pb = Fl.Line[K + 1];
			const int Steps = std::max(1, int(Pa.DistanceTo(Pb) / 32.0));
			for (int St = 0; St <= Steps; ++St)
			{
				const P2 Q = Pa + (Pb - Pa) * (double(St) / Steps);
				std::vector<int>& Row = AirGrid[CourtKey(Q.X, Q.Z)];
				if (Row.empty() || Row.back() != Index) Row.push_back(Index);
			}
		}
		AirLines.push_back(A);
	}
	for (const FeatureArea& Fa : G.Scenery.Areas)
		if (Fa.Kind == "apron" && Fa.Ring.size() >= 3) Aprons.push_back(Fa.Ring);
}

int RidePlan::ForecourtAt(double X, double Z, double Within) const
{
	const P2 P(X, Z);
	const auto Cx = static_cast<long long>(std::floor(X / CourtCell)), Cz = static_cast<long long>(std::floor(Z / CourtCell));
	for (long long Dz = -1; Dz <= 1; ++Dz)
		for (long long Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = ForecourtGrid.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == ForecourtGrid.end()) continue;
			for (int I : It->second)
			{
				const Forecourt& F = Forecourts[size_t(I)];
				const CourtFrame C = FrameOf(F);
				for (int K = 0; K < F.Islands; ++K)
				{
					const double U = (K - (F.Islands - 1) * 0.5) * IslandGap;
					const P2 A = C.Flat(U - IslandLong * 0.5, EntryLane + CanopyDeep * 0.5), B = C.Flat(U + IslandLong * 0.5, EntryLane + CanopyDeep * 0.5);
					if (SegDistance(P, A, B) <= Within) return I;
				}
			}
		}
	return -1;
}

bool RidePlan::OnForecourt(double X, double Z) const
{
	const auto Cx = static_cast<long long>(std::floor(X / CourtCell)), Cz = static_cast<long long>(std::floor(Z / CourtCell));
	for (long long Dz = -1; Dz <= 1; ++Dz)
		for (long long Dx = -1; Dx <= 1; ++Dx)
		{
			const auto Found = ForecourtGrid.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (Found == ForecourtGrid.end()) continue;
			for (int I : Found->second)
				if (RideShapes::InRing(Forecourts[size_t(I)].Ring, X, Z)) return true;
		}
	return false;
}

bool RidePlan::GarageOf(int Court, GarageSpot& Out) const
{
	if (Court < 0 || size_t(Court) >= Forecourts.size() || !Forecourts[size_t(Court)].Garage) return false;
	const Forecourt& F = Forecourts[size_t(Court)];
	const CourtFrame C = FrameOf(F);
	Out.Court = Court;
	Out.Middle = C.Flat(F.GarageU, F.GarageV + GarageDeep * 0.5);
	Out.Door = F.Out * -1.0;
	// (its floor just over the ground's highest under it, the plinth going down to the rest; and the
	// ground just out of its door, for the ramp up to the floor)
	double High = -1e18;
	for (double U : {-0.5, 0.0, 0.5})
		for (double V : {0.0, 0.5, 1.0})
		{
			const P2 Q = C.Flat(F.GarageU + U * GarageWide, F.GarageV + V * GarageDeep);
			const double Gy = Ground(Q.X, Q.Z);
			if (!std::isnan(Gy)) High = std::fmax(High, Gy);
		}
	Out.Base = (High > -1e17 ? High : F.Base) + 0.03;
	const P2 Q = C.Flat(F.GarageU, F.GarageV - 1.5);
	const double Gd = Ground(Q.X, Q.Z);
	Out.DoorGround = std::isnan(Gd) ? Out.Base : Gd;
	return true;
}

void RidePlan::GaragesNear(double X, double Z, double Reach, std::vector<GarageSpot>& Out) const
{
	Out.clear();
	const auto Cx0 = static_cast<long long>(std::floor((X - Reach - 40.0) / CourtCell)), Cx1 = static_cast<long long>(std::floor((X + Reach + 40.0) / CourtCell));
	const auto Cz0 = static_cast<long long>(std::floor((Z - Reach - 40.0) / CourtCell)), Cz1 = static_cast<long long>(std::floor((Z + Reach + 40.0) / CourtCell));
	for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
		for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = ForecourtGrid.find(Cx * 4000037LL + Cz);
			if (It == ForecourtGrid.end()) continue;
			for (int I : It->second)
			{
				GarageSpot G;
				if (GarageOf(I, G) && std::hypot(G.Middle.X - X, G.Middle.Z - Z) <= Reach) Out.push_back(G);
			}
		}
}

namespace
{
// [forecourts] One forecourt from the kit's pieces, laid where the boxes below go: the canopy over its islands,
// a pump island at each, the kiosk's bays in its row behind (the garage unit at one end stays the game's own),
// the price pole by the way in, and the rules plate, air and water machine, clutter and jet wash where there's
// room. Floor(along, out) is the forecourt's surface (the ground and 5 cm).
void DrawKitForecourt(const ForecourtKit& Kit, const RidePlan::Forecourt& F, const P2& Edge, const P2& Ua, const P2& Va,
	const std::function<double(double, double)>& Floor, int Detail, RideMesh& Out)
{
	const int Need = Detail >= 2 ? 0 : Detail == 1 ? 1 : 2;
	ForecourtFrame Fr;
	Fr.Ex = Edge.X; Fr.Ez = Edge.Z; Fr.Ux = Ua.X; Fr.Uz = Ua.Z; Fr.Vx = Va.X; Fr.Vz = Va.Z;
	const ForecourtKit::Livery& Lv = Kit.Liveries[size_t(Dice(F.Seed, 2) * double(Kit.Liveries.size())) % Kit.Liveries.size()];
	const std::function<double(double, double)> Ground = [&Floor](double U, double V) { return Floor(U, V) - 0.05; };
	const auto Lay = [&](const std::string& Name, double Ou, double Ov, double Base, double Xu = 1.0, double Yu = 0.0, double Xv = 0.0, double Yv = 1.0)
	{
		const int Pi = Kit.Piece(Name);
		if (Pi < 0) return;
		ForecourtPlace At;
		At.Ou = Ou; At.Ov = Ov; At.Xu = Xu; At.Yu = Yu; At.Xv = Xv; At.Yv = Yv; At.Base = Base;
		LayForecourtPiece(Kit.Pieces[size_t(Pi)], Fr, At, Ground, Lv, Need, Out);
	};
	const double A0 = -F.CanopyLong * 0.5, A1 = F.CanopyLong * 0.5, V0 = EntryLane, V1 = EntryLane + CanopyDeep, Vm = (V0 + V1) * 0.5;
	Lay(F.Islands >= 3 ? "canopy_3" : "canopy_2", 0.0, 0.0, F.Base);
	for (int K = 0; K < F.Islands; ++K)
	{
		const double U = (K - (F.Islands - 1) * 0.5) * IslandGap;
		Lay("island", U, Vm, Floor(U, Vm) - 0.05);
	}
	// (the price pole by the road at the way in, its faces to the traffic either way)
	Lay("price_pole", A1 + 4.0, 1.6, Floor(A1 + 4.0, 1.6) - 0.05, 0.0, -1.0, 1.0, 0.0);
	Lay("rules", A0 + 1.2, V1 - 2.25, Floor(A0 + 1.2, V1 - 2.25) - 0.05);
	if (F.Kiosk)
	{
		const bool Brick = Dice(F.Seed, 7) < Kit.BrickShare;
		const std::string Style = Brick ? "kiosk_brick_" : "kiosk_glass_";
		const double KV0 = V1 + 1.5;
		const double Row = std::fmin(F.CanopyLong, KioskRow);
		const double KU0 = F.Garage ? F.GarageU + RidePlan::GarageWide * 0.5 + 0.2 : -std::fmin(F.CanopyLong, 12.0) * 0.5;
		const double KU1 = F.Garage ? Row * 0.5 : std::fmin(F.CanopyLong, 12.0) * 0.5;
		const double KL = KU1 - KU0;
		const double Kf = std::fmin(Floor(KU0, KV0), Floor(KU1, KV0)) - 0.05;
		const int N = std::max(1, int(std::lround((KL - 0.6) / Kit.KioskBay)));
		const double Bw = (KL - 0.6) / N;
		for (int I = 0; I < N; ++I)
		{
			const char* Kind = I == N / 2 ? "door" : (N >= 3 && I == N - 1) ? "atm" : "glass";
			Lay(Style + Kind, KU0 + 0.3 + I * Bw, KV0, Kf, Bw / Kit.KioskBay);
		}
		Lay(Style + "end", KU0 + 0.3, KV0, Kf, -1.0);
		Lay(Style + "end", KU1 - 0.3, KV0, Kf);
		// (in front of the shop: the gas cage, ice chest, logs and coal, A-board, planters and bin, squeezed to its length)
		const double Squeeze = std::fmin(0.9, (KL - 0.6) / 9.7);
		if (Squeeze >= 0.55) Lay("clutter", KU0 + 0.5, KV0 - 1.1, Floor(KU0 + 0.5, KV0 - 1.1) - 0.05, Squeeze);
		Lay("air_water", A1 + 2.5, V1 + 1.0, Floor(A1 + 2.5, V1 + 1.0) - 0.05);
	}
	// (a jet wash past the canopy's far end, only where the forecourt runs on far enough and deep enough)
	if (F.Length * 0.5 - A1 >= 11.0 && F.Depth >= V1 + 9.0 && !(F.Garage && F.GarageU > A1))
		Lay("jet_wash", A1 + 2.0, V1 + 3.0, Floor(A1 + 6.0, V1 + 5.75) - 0.05);
}
} // namespace

void RidePlan::TownLifeTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (Detail < 0 || (Forecourts.empty() && TownThings.empty() && AirLines.empty())) return;
	// (the canopies' bands: plain colours, none a maker's pair)
	static const Paint Bands[] = {{0.62f, 0.62f, 0.60f}, {0.05f, 0.16f, 0.09f}, {0.04f, 0.07f, 0.20f}, {0.30f, 0.04f, 0.05f}, {0.16f, 0.17f, 0.18f}, {0.70f, 0.70f, 0.68f}};
	const Paint White = {0.72f, 0.72f, 0.70f}, Post = {0.55f, 0.56f, 0.56f}, Concrete = {0.34f, 0.33f, 0.31f};
	const Paint PumpBody = {0.58f, 0.59f, 0.60f}, Screen = {0.02f, 0.025f, 0.03f}, Glass = {0.03f, 0.04f, 0.05f}, Wall = {0.55f, 0.54f, 0.50f}, Frame = {0.10f, 0.10f, 0.11f};
	const Paint Hose = {0.03f, 0.03f, 0.03f}, Kerb = {0.75f, 0.62f, 0.10f};
	const Paint Light = {2.6f, 2.6f, 2.4f}, Shop = {2.2f, 1.9f, 1.5f}, Digits = {2.4f, 1.3f, 0.2f};
	const auto Cx0 = static_cast<long long>(std::floor((X0 - 40.0) / CourtCell)), Cx1 = static_cast<long long>(std::floor((X0 + Size + 40.0) / CourtCell));
	const auto Cz0 = static_cast<long long>(std::floor((Z0 - 40.0) / CourtCell)), Cz1 = static_cast<long long>(std::floor((Z0 + Size + 40.0) / CourtCell));
	// [forecourts] The forecourt kit's pieces in place of the boxes below, when it's read (-TorqueForecourts=0: not).
	const std::shared_ptr<const ForecourtKit> Kit = ForecourtKitNow();
	for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
		for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = ForecourtGrid.find(Cx * 4000037LL + Cz);
			if (It == ForecourtGrid.end()) continue;
			for (int I : It->second)
			{
				const Forecourt& F = Forecourts[size_t(I)];
				if (F.Canopy.X < X0 || F.Canopy.X >= X0 + Size || F.Canopy.Z < Z0 || F.Canopy.Z >= Z0 + Size) continue;
				const CourtFrame C = FrameOf(F);
				const Paint Band = Bands[size_t(Dice(F.Seed, 2) * 5.999)];
				// (the forecourt's height at a point: the ground, a little over it)
				const auto Floor = [this, &C](double U, double V)
				{
					const P2 Q = C.Flat(U, V);
					const double Gy = Ground(Q.X, Q.Z);
					return (std::isnan(Gy) ? 0.0 : Gy) + 0.05;
				};
				if (Kit)
				{
					DrawKitForecourt(*Kit, F, C.Edge, C.U, C.V, Floor, Detail, Out);
					continue;
				}
				const double Roof = F.Base + 5.0;   // (the soffit: room for a lorry)
				const double A0 = -F.CanopyLong * 0.5, A1 = F.CanopyLong * 0.5, V0 = EntryLane, V1 = EntryLane + CanopyDeep;
				// The canopy: its band all round, white lid, the soffit under it; its posts.
				Box(Out, C, A0, A1, V0, V1, Roof + 0.15, Roof + 1.0, Band, false, false);
				Face(Out, C.At(A0, V0, Roof + 1.0), C.At(A1, V0, Roof + 1.0), C.At(A1, V1, Roof + 1.0), C.At(A0, V1, Roof + 1.0), C.At(0.0, (V0 + V1) * 0.5, Roof), White);
				Box(Out, C, A0 + 0.05, A1 - 0.05, V0 + 0.05, V1 - 0.05, Roof, Roof + 0.15, White, false, true);
				{
					// (a thin white line along the band's top and foot, as most canopies have)
					Box(Out, C, A0 - 0.01, A1 + 0.01, V0 - 0.01, V1 + 0.01, Roof + 0.92, Roof + 1.0, White, false, false);
				}
				const int PostRows = F.CanopyLong > 16.0 ? 3 : 2;
				for (int Pr = 0; Pr < PostRows; ++Pr)
				{
					const double U = A0 + 1.2 + (F.CanopyLong - 2.4) * Pr / (PostRows - 1);
					for (double V : {V0 + 2.0, V1 - 2.0})
					{
						// (the posts in line with the islands' ends, out of the lanes)
						const double Pu = U, Gf = Floor(Pu, V);
						Box(Out, C, Pu - 0.18, Pu + 0.18, V - 0.18, V + 0.18, Gf - 0.3, Roof, Post, false, false);
					}
				}
				if (Detail >= 1)
				{
					// The pump islands: a raised concrete island, its ends painted, two pumps on each.
					for (int K = 0; K < F.Islands; ++K)
					{
						const double U = (K - (F.Islands - 1) * 0.5) * IslandGap;
						const double Vm = (V0 + V1) * 0.5, Gf = Floor(U, Vm);
						Box(Out, C, U - IslandLong * 0.5, U + IslandLong * 0.5, Vm - 0.6, Vm + 0.6, Gf - 0.1, Gf + 0.16, Concrete);
						for (double End : {-1.0, 1.0})
						{
							const double Ue = U + End * (IslandLong * 0.5 - 0.15);
							Box(Out, C, Ue - 0.12, Ue + 0.12, Vm - 0.12, Vm + 0.12, Gf + 0.16, Gf + 1.0, Kerb);   // (a bollard)
						}
						for (double Side : {-1.0, 1.0})
						{
							const double Up = U + Side * 1.05;
							Box(Out, C, Up - 0.42, Up + 0.42, Vm - 0.24, Vm + 0.24, Gf + 0.16, Gf + 1.7, PumpBody);
							Box(Out, C, Up - 0.44, Up + 0.44, Vm - 0.26, Vm + 0.26, Gf + 1.7, Gf + 2.0, Band);
							if (Detail >= 2)
							{
								// (a display each side, and the nozzles' holsters, hoses hanging)
								for (double Face2 : {-1.0, 1.0})
								{
									const double Vf = Vm + Face2 * 0.245;
									Face(Out, C.At(Up - 0.3, Vf, Gf + 1.2), C.At(Up + 0.3, Vf, Gf + 1.2), C.At(Up + 0.3, Vf, Gf + 1.55), C.At(Up - 0.3, Vf, Gf + 1.55),
										C.At(Up, Vm, Gf + 1.4), Screen);
									Box(Out, C, Up - 0.36, Up - 0.30, Vf - 0.03 * Face2, Vf + 0.08 * Face2, Gf + 0.5, Gf + 1.1, Hose);
									Box(Out, C, Up + 0.30, Up + 0.36, Vf - 0.03 * Face2, Vf + 0.08 * Face2, Gf + 0.5, Gf + 1.1, Hose);
								}
							}
						}
					}
					// The kiosk behind: rendered walls, glazed across the front, a flat roof with the band.
					if (F.Kiosk)
					{
						const double KV0 = V1 + 1.5, KV1 = KV0 + KioskDeep;
						// ([garage] sharing the row with the garage: from beside it to the row's end)
						const double Row = std::fmin(F.CanopyLong, KioskRow);
						const double KU0 = F.Garage ? F.GarageU + RidePlan::GarageWide * 0.5 + 0.2 : -std::fmin(F.CanopyLong, 12.0) * 0.5;
						const double KU1 = F.Garage ? Row * 0.5 : std::fmin(F.CanopyLong, 12.0) * 0.5;
						const double KL = KU1 - KU0, KM = (KU0 + KU1) * 0.5;   // (its length and middle)
						const double Kf = std::fmin(Floor(KU0, KV0), Floor(KU1, KV0)) - 0.05;
						Box(Out, C, KU0, KU1, KV0, KV1, Kf - 0.4, Kf + 3.4, Wall);
						Box(Out, C, KU0 - 0.1, KU1 + 0.1, KV0 - 0.1, KV1 + 0.1, Kf + 3.4, Kf + 4.0, Band);
						// (its glass front, facing the road, the door in the middle)
						const double Gv = KV0 - 0.02;
						Face(Out, C.At(KU0 + 0.6, Gv, Kf + 0.4), C.At(KU1 - 0.6, Gv, Kf + 0.4), C.At(KU1 - 0.6, Gv, Kf + 2.9), C.At(KU0 + 0.6, Gv, Kf + 2.9),
							C.At(KM, KV0 + 1.0, Kf + 1.5), Glass);
						for (double Mu = KU0 + 0.6; Mu <= KU1 - 0.5; Mu += (KL - 1.2) / 5.0)
							Box(Out, C, Mu - 0.05, Mu + 0.05, Gv - 0.05, Gv, Kf + 0.4, Kf + 2.9, Frame, false, false);
						RideMesh& Night = Out.NightOf();
						const size_t From = Night.Vertices();
						Face(Night, C.At(KU0 + 0.7, Gv - 0.03, Kf + 0.5), C.At(KU1 - 0.7, Gv - 0.03, Kf + 0.5), C.At(KU1 - 0.7, Gv - 0.03, Kf + 2.8),
							C.At(KU0 + 0.7, Gv - 0.03, Kf + 2.8), C.At(KM, KV0 + 1.0, Kf + 1.5), Shop);
						Unlit(Night, From);
					}
					// The price board by the road at the way in: a tall slab in the band's colour, three
					// dark panels (lit amber after dark), on a plinth.
					{
						const double Bu = A1 + 4.0, Bv = 1.6, Gf = Floor(Bu, Bv);
						Box(Out, C, Bu - 0.12, Bu + 0.12, Bv - 0.7, Bv + 0.7, Gf - 0.3, Gf + 0.4, Concrete);
						Box(Out, C, Bu - 0.17, Bu + 0.17, Bv - 0.65, Bv + 0.65, Gf + 0.4, Gf + 5.2, Band);
						RideMesh& Night = Out.NightOf();
						for (int Pn = 0; Pn < 3; ++Pn)
						{
							const double Y = Gf + 1.4 + Pn * 0.75;
							for (double Sd : {-1.0, 1.0})
							{
								const double Fu = Bu + Sd * 0.18;
								Face(Out, C.At(Fu, Bv - 0.5, Y), C.At(Fu, Bv + 0.5, Y), C.At(Fu, Bv + 0.5, Y + 0.55), C.At(Fu, Bv - 0.5, Y + 0.55), C.At(Bu, Bv, Y), Screen);
								const size_t From = Night.Vertices();
								const double Gu = Bu + Sd * 0.19;
								Face(Night, C.At(Gu, Bv - 0.05, Y + 0.12), C.At(Gu, Bv + 0.42, Y + 0.12), C.At(Gu, Bv + 0.42, Y + 0.43), C.At(Gu, Bv - 0.05, Y + 0.43), C.At(Bu, Bv, Y),
									Digits);
								Unlit(Night, From);
							}
						}
					}
				}
				// After dark the soffit's lights: a lit panel over each island and the lanes.
				{
					RideMesh& Night = Out.NightOf();
					const size_t From = Night.Vertices();
					for (int K = 0; K < F.Islands; ++K)
					{
						const double U = (K - (F.Islands - 1) * 0.5) * IslandGap;
						for (double Vl : {V0 + 1.8, (V0 + V1) * 0.5, V1 - 1.8})
							Face(Night, C.At(U - 1.4, Vl - 0.5, Roof - 0.02), C.At(U + 1.4, Vl - 0.5, Roof - 0.02), C.At(U + 1.4, Vl + 0.5, Roof - 0.02), C.At(U - 1.4, Vl + 0.5, Roof - 0.02),
								C.At(U, Vl, Roof + 1.0), Light);
					}
					Unlit(Night, From);
				}
			}
		}
	// [roads stage 8] The runways, taxiways and piers: each a run of pieces down its line, a piece drawn by
	// the square its middle is in.
	if (!AirLines.empty())
	{
		std::vector<int> Here;
		for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
			for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
			{
				const auto It = AirGrid.find(Cx * 4000037LL + Cz);
				if (It != AirGrid.end()) Here.insert(Here.end(), It->second.begin(), It->second.end());
			}
		std::sort(Here.begin(), Here.end());
		Here.erase(std::unique(Here.begin(), Here.end()), Here.end());
		const Paint Tarmac = {0.06f, 0.06f, 0.065f}, Taxi = {0.075f, 0.075f, 0.08f}, Mown = {0.13f, 0.20f, 0.06f}, Paint0 = {0.7f, 0.7f, 0.66f}, TaxiLine = {0.6f, 0.45f, 0.03f};
		const Paint Deck = {0.21f, 0.16f, 0.11f}, Iron = {0.10f, 0.11f, 0.11f}, Rail = {0.55f, 0.55f, 0.52f};
		const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
		for (int Ai : Here)
		{
			const AirLine& A = AirLines[size_t(Ai)];
			const std::vector<P2>& Line = Area.Roads.Scenery.Lines[size_t(A.Line)].Line;
			if (A.Kind == 3 && Detail < 1) continue;
			// (points down it every Step m, each with its way along)
			const double Step = A.Kind == 3 ? 3.0 : 5.0;
			std::vector<P2> At, Way;
			std::vector<double> Run;
			{
				double Done = 0.0;
				for (size_t K = 0; K + 1 < Line.size(); ++K)
				{
					const P2 D = Line[K + 1] - Line[K];
					const double L = D.Length();
					if (L < 1e-6) continue;
					const P2 U = D * (1.0 / L);
					for (double T = At.empty() ? 0.0 : std::fmod(Step - std::fmod(Done, Step), Step); T < L; T += Step)
					{
						At.push_back(Line[K] + U * T);
						Way.push_back(U);
						Run.push_back(Done + T);
					}
					Done += L;
				}
				At.push_back(Line.back());
				Way.push_back(Way.empty() ? P2(1.0, 0.0) : Way.back());
				Run.push_back(A.Length);
			}
			const double Half = A.Width * 0.5;
			const double Lift = A.Kind == 1 ? 0.06 : 0.1;
			// (a band of the piece from point I to I + 1: Off0 to Off1 across (+ left), Up over the ground or the deck)
			// (in columns no more than 3 m across, each corner on the ground, so the ground's own folds never show through)
			const auto Band = [&](RideMesh& Into, size_t I, double Off0, double Off1, double Up, const Paint& C)
			{
				const P2 La(-Way[I].Z, Way[I].X), Lb(-Way[I + 1].Z, Way[I + 1].X);
				const int Cols = std::max(1, int(std::ceil(std::fabs(Off1 - Off0) / 3.0)));
				const auto Y = [&](const P2& Q)
				{
					if (A.Kind == 3) return A.Deck + Up;
					const double Gy = Ground(Q.X, Q.Z);
					return (std::isnan(Gy) ? 0.0 : Gy) + Up;
				};
				for (int Cn = 0; Cn < Cols; ++Cn)
				{
					const double O0 = Off0 + (Off1 - Off0) * Cn / Cols, O1 = Off0 + (Off1 - Off0) * (Cn + 1) / Cols;
					const P2 Q[4] = {At[I] + La * O0, At[I + 1] + Lb * O0, At[I + 1] + Lb * O1, At[I] + La * O1};
					V3 V[4];
					for (int K = 0; K < 4; ++K) V[K] = Pt(Q[K].X, Y(Q[K]), Q[K].Z);
					Face(Into, V[0], V[1], V[2], V[3], Pt((V[0].X + V[2].X) * 0.5, (V[0].Y + V[2].Y) * 0.5 - 10.0, (V[0].Z + V[2].Z) * 0.5), C);
				}
			};
			for (size_t I = 0; I + 1 < At.size(); ++I)
			{
				if (!Inside((At[I] + At[I + 1]) * 0.5)) continue;
				if (A.Kind == 3)
				{
					// A pier: its deck, its rails either side, iron piles every other piece where the ground is below.
					Band(Out, I, -Half, Half, 0.0, Deck);
					Band(Out, I, -Half, Half, -0.25, Iron);
					for (double Sd : {-1.0, 1.0})
					{
						const P2 L(-Way[I].Z * Sd, Way[I].X * Sd);
						const P2 Pa = At[I] + L * (Half - 0.05), Pb = At[I + 1] + L * (Half - 0.05);
						RideShapes::Strut(Out, Pt(Pa.X, A.Deck + 1.0, Pa.Z), Pt(Pb.X, A.Deck + 1.0, Pb.Z), 0.06, Rail);
						RideShapes::Strut(Out, Pt(Pa.X, A.Deck, Pa.Z), Pt(Pa.X, A.Deck + 1.0, Pa.Z), 0.05, Rail);
						if (I % 2 == 0)
						{
							const double Gy = Ground(Pa.X, Pa.Z);
							if (!std::isnan(Gy) && Gy < A.Deck - 0.6) RideShapes::Column(Out, Pt(Pa.X, Gy - 0.5, Pa.Z), 0.18, 0.18, A.Deck - Gy + 0.25, 6, Iron);
						}
					}
					continue;
				}
				Band(Out, I, -Half, Half, Lift, A.Kind == 0 ? Tarmac : A.Kind == 1 ? Mown : Taxi);
				if (A.Kind == 1 || Detail < 1) continue;
				// Its paint: a hard runway's edge lines, dashed centre line and threshold bars at its ends; a
				// taxiway's yellow centre line.
				const double Up = Lift + 0.015;
				if (A.Kind == 2) { Band(Out, I, -0.15, 0.15, Up, TaxiLine); continue; }
				Band(Out, I, -Half + 0.6, -Half + 1.5, Up, Paint0);
				Band(Out, I, Half - 1.5, Half - 0.6, Up, Paint0);
				const double Mid = Run[I] + Step * 0.5;
				if (std::fmod(Mid, 50.0) < 30.0 && Mid > 60.0 && Mid < A.Length - 60.0) Band(Out, I, -0.45, 0.45, Up, Paint0);
				if (Mid < 40.0 || Mid > A.Length - 40.0)
					for (int Bar = 0; Bar < 8; ++Bar)
					{
						const double C = (Bar - 3.5) * (Half - 3.0) / 4.0;
						if (std::fabs(C) < 2.5) continue;
						Band(Out, I, C - 0.9, C + 0.9, Up, Paint0);
					}
			}
		}
	}

	// The things.
	if (Detail < 1 || TownThings.empty()) return;
	static const Paint TentCloth[] = {{0.10f, 0.20f, 0.08f}, {0.45f, 0.18f, 0.04f}, {0.06f, 0.12f, 0.30f}, {0.30f, 0.30f, 0.28f}, {0.40f, 0.08f, 0.06f}, {0.32f, 0.26f, 0.10f}};
	const Paint Van = {0.74f, 0.74f, 0.71f}, VanTrim = {0.40f, 0.40f, 0.40f}, Pane = {0.03f, 0.035f, 0.04f}, Tyre = {0.03f, 0.03f, 0.03f};
	const Paint Stone = {0.42f, 0.40f, 0.36f}, Pool = {0.04f, 0.07f, 0.09f}, PostGreen = {0.40f, 0.44f, 0.42f}, Face0 = {0.02f, 0.025f, 0.03f};
	static const Paint Statics[] = {{0.70f, 0.69f, 0.62f}, {0.62f, 0.66f, 0.60f}, {0.70f, 0.70f, 0.69f}};
	for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
		for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = ThingGrid.find(Cx * 4000037LL + Cz);
			if (It == ThingGrid.end()) continue;
			for (int I : It->second)
			{
				const TownThing& T = TownThings[size_t(I)];
				if (T.At.X < X0 || T.At.X >= X0 + Size || T.At.Z < Z0 || T.At.Z >= Z0 + Size) continue;
				CourtFrame C;
				C.Edge = T.At;
				C.U = P2(std::cos(T.Yaw), std::sin(T.Yaw));
				C.V = P2(-C.U.Z, C.U.X);
				const double Y = T.Base;
				switch (T.Kind)
				{
				case ThingTent:
				{
					// (a dome tent: a low six-sided tent, its door flap darker)
					const Paint Cloth = TentCloth[size_t(Dice(T.Seed, 1) * 5.999)];
					const double R = 1.1 + 0.6 * Dice(T.Seed, 2);
					RideShapes::Column(Out, Pt(T.At.X, Y - 0.05, T.At.Z), R, R * 0.35, R * 0.9, Detail >= 2 ? 8 : 6, Cloth, R * 0.25);
					break;
				}
				case ThingCaravan:
				{
					if (PropModelled[PropTouringCaravan]) break;   // ([caravans] the model's: TownLifeProps)
					// (a touring caravan on its wheels: a white box, a dark window band, the A-frame forward)
					const double L = 5.2 + 1.2 * Dice(T.Seed, 1), W = 1.15, Lo = Y + 0.35, Hi = Lo + 2.05;
					Box(Out, C, -L * 0.5, L * 0.5, -W, W, Lo, Hi, Van, false, true);
					Box(Out, C, -L * 0.5 + 0.1, L * 0.5 - 0.1, -W + 0.1, W - 0.1, Hi, Hi + 0.08, VanTrim);
					Box(Out, C, -L * 0.5 - 0.01, L * 0.5 + 0.01, -W - 0.01, W + 0.01, Lo + 1.15, Lo + 1.6, Pane, false, false);
					Box(Out, C, -L * 0.5 - 0.01, L * 0.5 + 0.01, -W - 0.01, W + 0.01, Lo, Lo + 0.25, VanTrim, false, false);
					Box(Out, C, -0.35, 0.35, -W - 0.05, -W + 0.15, Y - 0.05, Lo + 0.25, Tyre);
					Box(Out, C, -0.35, 0.35, W - 0.15, W + 0.05, Y - 0.05, Lo + 0.25, Tyre);
					RideShapes::Strut(Out, C.At(L * 0.5, -0.6, Lo + 0.1), C.At(L * 0.5 + 1.1, 0.0, Y + 0.45), 0.08, VanTrim);
					RideShapes::Strut(Out, C.At(L * 0.5, 0.6, Lo + 0.1), C.At(L * 0.5 + 1.1, 0.0, Y + 0.45), 0.08, VanTrim);
					break;
				}
				case ThingStatic:
				{
					if (PropModelled[PropStaticCaravan]) break;
					// (a static caravan: long and boxy, on blocks, windows round it, a low-pitched roof)
					const Paint Body = Statics[size_t(Dice(T.Seed, 1) * 2.999)];
					const double L = 10.0, W = 1.85, Lo = Y + 0.5, Hi = Lo + 2.4;
					Box(Out, C, -L * 0.5, L * 0.5, -W, W, Y - 0.2, Lo, VanTrim, false, false);
					Box(Out, C, -L * 0.5, L * 0.5, -W, W, Lo, Hi, Body);
					Face(Out, C.At(-L * 0.5, -W, Hi), C.At(L * 0.5, -W, Hi), C.At(L * 0.5, 0.0, Hi + 0.35), C.At(-L * 0.5, 0.0, Hi + 0.35), C.At(0.0, 0.0, Lo), VanTrim);
					Face(Out, C.At(-L * 0.5, W, Hi), C.At(L * 0.5, W, Hi), C.At(L * 0.5, 0.0, Hi + 0.35), C.At(-L * 0.5, 0.0, Hi + 0.35), C.At(0.0, 0.0, Lo), VanTrim);
					for (double Wu : {-3.2, -0.6, 2.6})
						Box(Out, C, Wu - 0.7, Wu + 0.7, -W - 0.01, W + 0.01, Lo + 0.9, Lo + 1.9, Pane, false, false);
					break;
				}
				case ThingFountain:
				{
					// (a stone basin, its water a little under the rim; on the bigger ones a pillar and a bowl)
					const double R = T.Size;
					const int Sides = R > 4.0 ? 24 : 14;
					RideShapes::Column(Out, Pt(T.At.X, Y - 0.2, T.At.Z), R, R, 0.75, Sides, Stone);
					RideShapes::Column(Out, Pt(T.At.X, Y + 0.5, T.At.Z), R - 0.3, R - 0.3, 0.02, Sides, Pool);
					const double Pillar = std::fmin(2.4, 0.9 + R * 0.3);
					RideShapes::Column(Out, Pt(T.At.X, Y + 0.5, T.At.Z), std::fmin(0.35, R * 0.2), std::fmin(0.25, R * 0.15), Pillar, 10, Stone);
					if (R >= 1.8)
					{
						const double Bowl = std::fmin(R * 0.45, 2.2);
						RideShapes::Column(Out, Pt(T.At.X, Y + 0.5 + Pillar, T.At.Z), Bowl * 0.4, Bowl, 0.35, Sides, Stone);
						RideShapes::Column(Out, Pt(T.At.X, Y + 0.5 + Pillar + 0.36, T.At.Z), Bowl - 0.12, Bowl - 0.12, 0.01, Sides, Pool);
						RideShapes::Column(Out, Pt(T.At.X, Y + 0.85 + Pillar, T.At.Z), 0.12, 0.05, 0.6, 8, Stone);
					}
					break;
				}
				case ThingCharger:
				{
					// (an electric car charger: a slim grey post, its screen facing the road, a light on it at night)
					Box(Out, C, -0.18, 0.18, -0.3, 0.3, Y - 0.2, Y + 1.65, PostGreen);
					Face(Out, C.At(0.19, -0.2, Y + 1.05), C.At(0.19, 0.2, Y + 1.05), C.At(0.19, 0.2, Y + 1.45), C.At(0.19, -0.2, Y + 1.45), C.At(0.0, 0.0, Y + 1.2), Face0);
					RideMesh& Night = Out.NightOf();
					const size_t From = Night.Vertices();
					Face(Night, C.At(0.2, -0.17, Y + 1.5), C.At(0.2, 0.17, Y + 1.5), C.At(0.2, 0.17, Y + 1.58), C.At(0.2, -0.17, Y + 1.58), C.At(0.0, 0.0, Y + 1.5), Paint{0.3f, 1.6f, 0.6f});
					Unlit(Night, From);
					break;
				}
				default: break;
				}
			}
		}
}
// [caravans] The campsites' caravans as models (Tools/blender_caravan_kit.py), length along their Yaw, the tourer's
// A-frame forward as the boxes had it; which model by the pitch's seed.
void RidePlan::TownLifeProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	if (TownThings.empty() || (!PropModelled[PropTouringCaravan] && !PropModelled[PropStaticCaravan])) return;
	const auto Cx0 = static_cast<long long>(std::floor(X0 / CourtCell)), Cx1 = static_cast<long long>(std::floor((X0 + Size) / CourtCell));
	const auto Cz0 = static_cast<long long>(std::floor(Z0 / CourtCell)), Cz1 = static_cast<long long>(std::floor((Z0 + Size) / CourtCell));
	for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
		for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = ThingGrid.find(Cx * 4000037LL + Cz);
			if (It == ThingGrid.end()) continue;
			for (int I : It->second)
			{
				const TownThing& T = TownThings[size_t(I)];
				if (T.At.X < X0 || T.At.X >= X0 + Size || T.At.Z < Z0 || T.At.Z >= Z0 + Size || std::isnan(T.Base)) continue;
				const int Kind = T.Kind == ThingCaravan ? PropTouringCaravan : T.Kind == ThingStatic ? PropStaticCaravan : -1;
				if (Kind < 0 || !PropModelled[Kind]) continue;
				RideProp P;
				P.Kind = uint16_t(Kind);
				P.Id = T.Seed;
				P.Variant = uint8_t(Dice(T.Seed, 9) * 255.0);
				P.X = float(T.At.X - X0);
				P.Z = float(T.At.Z - Z0);
				P.Y = float(T.Base);
				P.Yaw = float(T.Yaw);
				Out.push_back(P);
			}
		}
}
} // namespace TorqueWorld
