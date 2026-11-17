// [roads stage 7] Town life (the OSM plan's stage 7): first, the petrol stations (7C). At each of the
// map's fuel stops near enough a road (not a motorway's: its services are stage 8's), a forecourt you can
// ride into: tarmac from the road's edge back (drawn with the road, as a place's pull-off is, so the kerb
// and pavement stop at its mouth), a canopy on its posts over two or three pump islands, a kiosk behind
// where the map has no building for one, and a tall price board by the road. No maker's name, logo or
// colours anywhere: each canopy's band is one plain colour picked by hash. Stopped by a pump, the tank
// fills (ForecourtAt; TorqueRideWorld's dash).
#include "WorldRide.h"
#include "WorldRideRoadX.h"
#include "WorldRideSceneBits.h"
#include "WorldRoadLine.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cmath>
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
	if (Nx * Mx + Ny * My + Nz * Mz >= 0.0) RideShapes::SkyQuad(Out, Q0, Q1, Q2, Q3, C, false);
	else RideShapes::SkyQuad(Out, Q3, Q2, Q1, Q0, C, false);
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

void RidePlan::IndexForecourts()
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
		const double Long = CanopyLong + 12.0;   // (6 m either end: the way in and the way out)

		// Along the road from the foot, either way up to 25 m, as deep as it will go (ForecourtDepth down to
		// ForecourtLeast): clear of buildings, walls, water and other roads, off bridges, level enough.
		double BestScore = 1e18, BestAlong = 0.0, BestDepth = 0.0;
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
		F.Kiosk = BestDepth >= EntryLane + CanopyDeep + KioskDeep + 1.0;
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

void RidePlan::ForecourtsTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (Detail < 0 || Forecourts.empty()) return;
	// (the canopies' bands: plain colours, none a maker's pair)
	static const Paint Bands[] = {{0.62f, 0.62f, 0.60f}, {0.05f, 0.16f, 0.09f}, {0.04f, 0.07f, 0.20f}, {0.30f, 0.04f, 0.05f}, {0.16f, 0.17f, 0.18f}, {0.70f, 0.70f, 0.68f}};
	const Paint White = {0.72f, 0.72f, 0.70f}, Post = {0.55f, 0.56f, 0.56f}, Concrete = {0.34f, 0.33f, 0.31f};
	const Paint PumpBody = {0.58f, 0.59f, 0.60f}, Screen = {0.02f, 0.025f, 0.03f}, Glass = {0.03f, 0.04f, 0.05f}, Wall = {0.55f, 0.54f, 0.50f}, Frame = {0.10f, 0.10f, 0.11f};
	const Paint Hose = {0.03f, 0.03f, 0.03f}, Kerb = {0.75f, 0.62f, 0.10f};
	const Paint Light = {2.6f, 2.6f, 2.4f}, Shop = {2.2f, 1.9f, 1.5f}, Digits = {2.4f, 1.3f, 0.2f};
	const auto Cx0 = static_cast<long long>(std::floor((X0 - 40.0) / CourtCell)), Cx1 = static_cast<long long>(std::floor((X0 + Size + 40.0) / CourtCell));
	const auto Cz0 = static_cast<long long>(std::floor((Z0 - 40.0) / CourtCell)), Cz1 = static_cast<long long>(std::floor((Z0 + Size + 40.0) / CourtCell));
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
						const double KV0 = V1 + 1.5, KV1 = KV0 + KioskDeep, KL = std::fmin(F.CanopyLong, 12.0);
						const double Kf = std::fmin(Floor(-KL * 0.5, KV0), Floor(KL * 0.5, KV0)) - 0.05;
						Box(Out, C, -KL * 0.5, KL * 0.5, KV0, KV1, Kf - 0.4, Kf + 3.4, Wall);
						Box(Out, C, -KL * 0.5 - 0.1, KL * 0.5 + 0.1, KV0 - 0.1, KV1 + 0.1, Kf + 3.4, Kf + 4.0, Band);
						// (its glass front, facing the road, the door in the middle)
						const double Gv = KV0 - 0.02;
						Face(Out, C.At(-KL * 0.5 + 0.6, Gv, Kf + 0.4), C.At(KL * 0.5 - 0.6, Gv, Kf + 0.4), C.At(KL * 0.5 - 0.6, Gv, Kf + 2.9), C.At(-KL * 0.5 + 0.6, Gv, Kf + 2.9),
							C.At(0.0, KV0 + 1.0, Kf + 1.5), Glass);
						for (double Mu = -KL * 0.5 + 0.6; Mu <= KL * 0.5 - 0.5; Mu += (KL - 1.2) / 5.0)
							Box(Out, C, Mu - 0.05, Mu + 0.05, Gv - 0.05, Gv, Kf + 0.4, Kf + 2.9, Frame, false, false);
						RideMesh& Night = Out.NightOf();
						const size_t From = Night.Vertices();
						Face(Night, C.At(-KL * 0.5 + 0.7, Gv - 0.03, Kf + 0.5), C.At(KL * 0.5 - 0.7, Gv - 0.03, Kf + 0.5), C.At(KL * 0.5 - 0.7, Gv - 0.03, Kf + 2.8),
							C.At(-KL * 0.5 + 0.7, Gv - 0.03, Kf + 2.8), C.At(0.0, KV0 + 1.0, Kf + 1.5), Shop);
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
}
} // namespace TorqueWorld
