// [places stage 2] Somewhere to arrive at each place worth riding to (the places plan): the map's car
// park for it, else its lay-by, else a pull-off made beside the road nearest it, with a name board,
// so a rider who rides out to a place the info panel suggests has somewhere to stop and something
// that says they are there. In a built-up street the board stands at the kerb. The places themselves
// are drawn by kind from stage 3 on (World/WorldRidePlaces.cpp grows with them).
#include "WorldRide.h"
#include "WorldRideRoadX.h"
#include "WorldRideSceneBits.h"
#include "WorldRoadLine.h"

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

constexpr double PlaceCell = 64.0;     // PlaceGrid's squares
constexpr double EdgeCell = 128.0;     // the roads' bits, filed for finding the nearest
constexpr double ShareWithin = 120.0;  // places whose stops would be this close share one

long long CellKey(double X, double Z, double Cell)
{
	return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell));
}

// The kinds a rider rides out to even with no Wikipedia page, when named.
bool Worth(const std::string& Kind)
{
	static const char* const Kinds[] = {"stone_circle", "standing_stone", "castle", "castle_ruin", "abbey", "ruin", "hillfort", "ancient_site", "dam", "lighthouse", "windmill",
		"watermill", "waterfall", "cave", "monument", "tower", "viewpoint", "peak", "industrial_heritage", "battlefield"};
	for (const char* K : Kinds)
		if (Kind == K) return true;
	return false;
}

// The roads a pull-off may be beside: not motorways (no stopping), nor tracks, paths and private drives.
bool Stoppable(const std::string& Class)
{
	return Class == "trunk" || Class == "primary" || Class == "secondary" || Class == "tertiary" || Class == "unclassified" || Class == "residential" || Class == "living_street"
		|| Class == "road" || Class == "unknown";
}

double SegDist(const P2& P, const P2& A, const P2& B, double* T = nullptr)
{
	const P2 D = B - A;
	const double L2 = D.X * D.X + D.Z * D.Z;
	double U = L2 > 1e-12 ? ((P.X - A.X) * D.X + (P.Z - A.Z) * D.Z) / L2 : 0.0;
	U = std::clamp(U, 0.0, 1.0);
	if (T != nullptr) *T = U;
	return P.DistanceTo(A + D * U);
}

bool SegsCross(const P2& A, const P2& B, const P2& C, const P2& D)
{
	const auto Side = [](const P2& P, const P2& Q, const P2& R) { return (Q.X - P.X) * (R.Z - P.Z) - (Q.Z - P.Z) * (R.X - P.X); };
	const double D1 = Side(C, D, A), D2 = Side(C, D, B), D3 = Side(A, B, C), D4 = Side(A, B, D);
	return ((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0));
}

bool InRing(const std::vector<P2>& Ring, double X, double Z)
{
	bool In = false;
	for (size_t I = 0, J = Ring.size() - 1; I < Ring.size(); J = I++)
	{
		const P2& A = Ring[I];
		const P2& B = Ring[J];
		if ((A.Z > Z) != (B.Z > Z) && X < (B.X - A.X) * (Z - A.Z) / (B.Z - A.Z) + A.X) In = !In;
	}
	return In;
}
} // namespace

void RidePlan::IndexPlaces()
{
	PlaceStops.clear();
	PlaceGrid.clear();
	PlacesSeen = PlacesFar = 0;
	for (int& C : PlaceStopCount) C = 0;
	const Region& G = Area.Roads;
	if (!Scene || G.Sights.empty()) return;

	// The roads' bits, filed by square (each bit in every square its box touches), with each road's half width.
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Bits;   // (edge, first point)
	std::vector<double> Halves(G.Edges.size(), 0.0);
	std::vector<unsigned char> CanStop(G.Edges.size(), 0);
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& R = G.Edges[E];
		const std::string& Class = G.ClassOf(int(E));
		Halves[E] = RoadX::Of(G, int(E), RoadLine::WidthOf(Class)).Half;
		const RoadExtra* X = G.ExtraOf(int(E));
		CanStop[E] = Stoppable(Class) && !(X != nullptr && X->Has(RoadExtra::Private)) ? 1 : 0;
		for (size_t I = 0; I + 1 < R.Line.size(); ++I)
		{
			const P2& A = R.Line[I];
			const P2& B = R.Line[I + 1];
			const int X0 = int(std::floor(std::fmin(A.X, B.X) / EdgeCell)), X1 = int(std::floor(std::fmax(A.X, B.X) / EdgeCell));
			const int Z0 = int(std::floor(std::fmin(A.Z, B.Z) / EdgeCell)), Z1 = int(std::floor(std::fmax(A.Z, B.Z) / EdgeCell));
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
	// The nearest bit of road to P within Reach (only the stoppable ones if Stop), searching out ring by ring.
	const auto NearestRoad = [&](const P2& P, double Reach, bool Stop, int NotEdge = -1)
	{
		Foot Best;
		const int Cx0 = int(std::floor(P.X / EdgeCell)), Cz0 = int(std::floor(P.Z / EdgeCell));
		const int Rings = int(std::ceil(Reach / EdgeCell));
		for (int Ring = 0; Ring <= Rings; ++Ring)
		{
			if (Best.Edge >= 0 && Best.Dist < (Ring - 1) * EdgeCell) break;
			for (int Dz = -Ring; Dz <= Ring; ++Dz)
				for (int Dx = -Ring; Dx <= Ring; ++Dx)
				{
					if (std::max(std::abs(Dx), std::abs(Dz)) != Ring) continue;
					const auto It = Bits.find(static_cast<long long>(Cx0 + Dx) * 4000037LL + (Cz0 + Dz));
					if (It == Bits.end()) continue;
					for (const std::pair<int, int>& B : It->second)
					{
						if ((Stop && !CanStop[size_t(B.first)]) || B.first == NotEdge) continue;
						const std::vector<P2>& L = G.Edges[size_t(B.first)].Line;
						double T = 0.0;
						const double D = SegDist(P, L[size_t(B.second)], L[size_t(B.second) + 1], &T);
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
	const auto BuildingsNear = [&](const P2& P, double Within, int Enough)
	{
		int Count = 0;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
				{
					const Building& H = G.Buildings[size_t(B)];
					// (from its middle, less half its size: near its walls)
					if (std::hypot(H.X - P.X, H.Z - P.Z) - 0.5 * std::fmax(H.W, H.D) < Within && ++Count >= Enough) return Count;
				}
			}
		return Count;
	};
	// Whether a mapped wall, hedge or fence crosses from A to B.
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
					const std::vector<P2>& Line = G.Scenery.Lines[size_t(L)].Line;
					for (size_t I = 0; I + 1 < Line.size(); ++I)
						if (SegsCross(A, B, Line[I], Line[I + 1])) return true;
				}
			}
		return false;
	};
	const auto OnSpan = [&](const P2& P)
	{
		for (const Span& S : G.Spans)
			if (SegDist(P, S.A, S.B) < 30.0) return true;
		return false;
	};
	const auto NearJunction = [&](int Edge, const P2& P)
	{
		const RoadEdge& R = G.Edges[size_t(Edge)];
		for (int N : {R.A, R.B})
			if (G.Adjacent[size_t(N)].size() > 2 && G.Nodes[size_t(N)].DistanceTo(P) < 30.0) return true;
		return false;
	};
	const auto AddStop = [this](PlaceStop S)
	{
		const int Index = int(PlaceStops.size());
		PlaceGrid[CellKey(S.At.X, S.At.Z, PlaceCell)].push_back(Index);
		++PlaceStopCount[S.How];
		PlaceStops.push_back(std::move(S));
		return Index;
	};
	// A stop already made within ShareWithin of P, -1 none.
	const auto SharedNear = [this](const P2& P)
	{
		const int Cx = int(std::floor(P.X / PlaceCell)), Cz = int(std::floor(P.Z / PlaceCell));
		int Best = -1;
		double BestD = ShareWithin;
		for (int Dz = -2; Dz <= 2; ++Dz)
			for (int Dx = -2; Dx <= 2; ++Dx)
			{
				const auto It = PlaceGrid.find(static_cast<long long>(Cx + Dx) * 4000037LL + (Cz + Dz));
				if (It == PlaceGrid.end()) continue;
				for (int I : It->second)
				{
					const double D = PlaceStops[size_t(I)].At.DistanceTo(P);
					if (D < BestD) { BestD = D; Best = I; }
				}
			}
		return Best;
	};

	// Where a pull-off may not go: graveyards, allotments, quarries, orchards, water and pitches.
	struct NoGo
	{
		const std::vector<P2>* Ring = nullptr;
		double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;
	};
	std::unordered_map<long long, std::vector<NoGo>> NoGos;   // by every SceneCell square its box covers
	const auto AddNoGo = [&NoGos](const std::vector<P2>& Ring)
	{
		if (Ring.size() < 3) return;
		NoGo N;
		N.Ring = &Ring;
		N.X0 = N.X1 = Ring[0].X;
		N.Z0 = N.Z1 = Ring[0].Z;
		for (const P2& P : Ring) { N.X0 = std::fmin(N.X0, P.X); N.X1 = std::fmax(N.X1, P.X); N.Z0 = std::fmin(N.Z0, P.Z); N.Z1 = std::fmax(N.Z1, P.Z); }
		if (N.X1 - N.X0 > 5000.0 || N.Z1 - N.Z0 > 5000.0) return;
		for (int Cz = int(std::floor(N.Z0 / SceneCell)); Cz <= int(std::floor(N.Z1 / SceneCell)); ++Cz)
			for (int Cx = int(std::floor(N.X0 / SceneCell)); Cx <= int(std::floor(N.X1 / SceneCell)); ++Cx) NoGos[static_cast<long long>(Cx) * 4000037LL + Cz].push_back(N);
	};
	for (const FeatureArea& A : G.Scenery.Areas)
		if (A.Kind == "cemetery" || A.Kind == "grave_yard" || A.Kind == "allotments" || A.Kind == "quarry" || A.Kind == "orchard" || A.Kind == "reservoir") AddNoGo(A.Ring);
	for (const GroundShape& A : G.Ground.Areas)
		if (A.Kind == "lake" || A.Kind == "pond" || A.Kind == "reservoir" || A.Kind == "river" || A.Kind == "basin" || A.Kind == "canal" || A.Kind == "pitch" || A.Kind == "cemetery"
			|| A.Kind == "grave_yard" || A.Kind == "allotments")
			AddNoGo(A.Pts);
	const auto Forbidden = [&NoGos](const P2& P)
	{
		const auto It = NoGos.find(SceneKey(P.X, P.Z));
		if (It == NoGos.end()) return false;
		for (const NoGo& N : It->second)
			if (P.X >= N.X0 && P.X <= N.X1 && P.Z >= N.Z0 && P.Z <= N.Z1 && InRing(*N.Ring, P.X, P.Z)) return true;
		return false;
	};

	// The map's car parks and lay-bys.
	struct Park
	{
		P2 Mid;
		const std::vector<P2>* Ring = nullptr;
	};
	std::vector<Park> Parks;
	for (const GroundShape& A : G.Ground.Areas)
	{
		if (A.Kind != "car_park" || A.Pts.size() < 3) continue;
		Park K;
		for (const P2& P : A.Pts) K.Mid = K.Mid + P;
		K.Mid = K.Mid * (1.0 / double(A.Pts.size()));
		K.Ring = &A.Pts;
		Parks.push_back(K);
	}
	std::vector<P2> Laybys;
	for (const TorqueFit::Json& Poi : G.Pois)
		if (Poi.Str("kind") == "layby") Laybys.push_back(P2(Poi.Num("x"), Poi.Num("z")));

	// The places, the best known first (so a shared stop's board names the one with a page).
	std::vector<int> Order;
	for (size_t I = 0; I < G.Sights.size(); ++I)
	{
		const Sight& S = G.Sights[I];
		if (S.Kind == "settlement") continue;
		if (S.Qid.empty() && !(Worth(S.Kind) && !S.Name.empty())) continue;
		Order.push_back(int(I));
	}
	std::stable_sort(Order.begin(), Order.end(), [&G](int A, int B) { return G.Sights[size_t(A)].Qid.empty() < G.Sights[size_t(B)].Qid.empty(); });

	for (int Index : Order)
	{
		const Sight& S = G.Sights[size_t(Index)];
		++PlacesSeen;
		// (a place with an outline: from its edge nearest the road, not its middle)
		const Foot Near = NearestRoad(S.At, PlaceReach, true);
		if (Near.Edge < 0) { ++PlacesFar; continue; }
		PlaceStop Stop;
		Stop.Sights.push_back(Index);
		Stop.Edge = Near.Edge;
		// 1. The map's car park for it.
		{
			int Best = -1;
			double BestD = PlaceCarParkReach;
			for (size_t K = 0; K < Parks.size(); ++K)
			{
				const double D = Parks[K].Mid.DistanceTo(S.At);
				if (D < BestD && NearestRoad(Parks[K].Mid, 80.0, false).Edge >= 0) { BestD = D; Best = int(K); }
			}
			if (Best >= 0)
			{
				const Park& K = Parks[size_t(Best)];
				Stop.How = PlaceCarPark;
				Stop.At = K.Mid;
				// (its board at the park's edge nearest the place, a little inside, facing into the park)
				P2 Edge = (*K.Ring)[0];
				for (const P2& P : *K.Ring)
					if (P.DistanceTo(S.At) < Edge.DistanceTo(S.At)) Edge = P;
				const P2 In = K.Mid - Edge;
				const double L = In.Length();
				Stop.Face = L > 1e-6 ? In * (1.0 / L) : P2(0.0, 1.0);
				Stop.Board = Edge + Stop.Face * std::fmin(2.0, L * 0.5);
			}
		}
		// 2. Else its lay-by.
		if (Stop.How != PlaceCarPark)
		{
			int Best = -1;
			double BestD = PlaceLaybyReach;
			for (size_t K = 0; K < Laybys.size(); ++K)
			{
				const double D = Laybys[K].DistanceTo(S.At);
				if (D < BestD) { BestD = D; Best = int(K); }
			}
			if (Best >= 0)
			{
				const Foot At = NearestRoad(Laybys[size_t(Best)], 60.0, false);
				if (At.Edge >= 0)
				{
					Stop.How = PlaceLayby;
					Stop.Edge = At.Edge;
					const P2 Out = Laybys[size_t(Best)] - At.At;
					const double L = Out.Length();
					const P2 Away = L > 0.5 ? Out * (1.0 / L) : P2(0.0, 1.0);
					const double Half = Halves[size_t(At.Edge)];
					Stop.At = At.At + Away * (Half + 2.5);
					Stop.Board = At.At + Away * (Half + 5.5);
					Stop.Face = Away * -1.0;
				}
			}
		}
		// 3. Else a pull-off made by the road nearest it (or, in a street, the board at the kerb).
		if (Stop.How != PlaceCarPark && Stop.How != PlaceLayby)
		{
			const RoadEdge& R = G.Edges[size_t(Near.Edge)];
			const double Half = Halves[size_t(Near.Edge)];
			// (the place's side of the road; a place on the road itself, its left)
			const P2 Dir0 = R.Line[size_t(Near.Bit) + 1] - R.Line[size_t(Near.Bit)];
			const double Dl = Dir0.Length();
			const P2 Dir = Dl > 1e-6 ? Dir0 * (1.0 / Dl) : P2(1.0, 0.0);
			const P2 Left(Dir.Z, -Dir.X);
			const P2 ToIt = S.At - Near.At;
			const double SideSign = (ToIt.X * Left.X + ToIt.Z * Left.Z) >= 0.0 ? 1.0 : -1.0;
			if (BuildingsNear(Near.At, 40.0, 6) >= 6)
			{
				Stop.How = PlaceStreet;
				const P2 Out = Left * SideSign;
				Stop.At = Near.At + Out * (Half + 0.5);
				Stop.Board = Near.At + Out * (Half + 1.6);
				Stop.Face = Out * -1.0;
			}
			else
			{
				// (along the road from the foot, either way, up to 80 m: the spot most level, with the
				// most room before a wall, off junctions, bridges and other roads, clear of buildings)
				double Along0 = 0.0;
				std::vector<double> Alongs(R.Line.size(), 0.0);
				for (size_t I = 1; I < R.Line.size(); ++I) Alongs[I] = Alongs[I - 1] + R.Line[I].DistanceTo(R.Line[I - 1]);
				Along0 = Alongs[size_t(Near.Bit)] + Near.T * (Alongs[size_t(Near.Bit) + 1] - Alongs[size_t(Near.Bit)]);
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
				double BestScore = 1e18, BestAlong = Along0, BestDepth = 0.0;
				for (double Shift = 0.0; Shift <= 80.0; Shift += 10.0)
					for (double Way : {1.0, -1.0})
					{
						if (Shift == 0.0 && Way < 0.0) continue;
						const double A = Along0 + Way * Shift;
						if (A < PullOffLength * 0.5 || A > Alongs.back() - PullOffLength * 0.5) continue;
						P2 Mid, D;
						PointAt(A, Mid, D);
						const P2 Out = P2(D.Z, -D.X) * SideSign;
						if (OnSpan(Mid) || NearJunction(Near.Edge, Mid)) continue;
						// (how deep it can go before a wall, another road or a building)
						double Depth = PullOffDepth;
						for (double Su : {-0.5, 0.0, 0.5})
						{
							P2 Q, Dq;
							PointAt(A + Su * PullOffLength, Q, Dq);
							const P2 Oq = P2(Dq.Z, -Dq.X) * SideSign;
							const P2 Inner = Q + Oq * (Half + 0.2);
							for (double Try = PullOffDepth; Try >= 3.0; Try -= 0.5)
							{
								if (!Walled(Inner, Inner + Oq * Try)) { Depth = std::fmin(Depth, Try); break; }
								if (Try <= 3.0) Depth = 0.0;
							}
							const Foot Other = NearestRoad(Inner + Oq * Depth, Halves[size_t(Near.Edge)] + PullOffDepth + 6.0, false, Near.Edge);
							if (Other.Edge >= 0 && Other.Dist < Halves[size_t(Other.Edge)] + 1.0) Depth = 0.0;
						}
						if (Depth < 3.0) continue;
						if (BuildingsNear(Mid + Out * (Half + Depth * 0.5), 10.0, 1) > 0) continue;
						{
							bool Bad = false;
							for (double Su : {-0.5, 0.0, 0.5})
								for (double Dv : {0.5, 1.0})
								{
									P2 Q, Dq;
									PointAt(A + Su * PullOffLength, Q, Dq);
									Bad = Bad || Forbidden(Q + P2(Dq.Z, -Dq.X) * (SideSign * (Half + Depth * Dv)));
								}
							if (Bad) continue;
						}
						// (the ground's fall across it and along it: 1 in 6 the most)
						const double Y0 = Ground(Mid.X + Out.X * (Half + 0.3), Mid.Z + Out.Z * (Half + 0.3));
						const double Y1 = Ground(Mid.X + Out.X * (Half + Depth), Mid.Z + Out.Z * (Half + Depth));
						P2 Ea, De, Eb;
						PointAt(A - PullOffLength * 0.5, Ea, De);
						PointAt(A + PullOffLength * 0.5, Eb, De);
						const double Ya = Ground(Ea.X + Out.X * (Half + Depth * 0.5), Ea.Z + Out.Z * (Half + Depth * 0.5));
						const double Yb = Ground(Eb.X + Out.X * (Half + Depth * 0.5), Eb.Z + Out.Z * (Half + Depth * 0.5));
						if (std::isnan(Y0) || std::isnan(Y1) || std::isnan(Ya) || std::isnan(Yb)) continue;
						const double Fall = std::fmax(std::fabs(Y1 - Y0) / Depth, std::fabs(Yb - Ya) / PullOffLength);
						if (Fall > 1.0 / 6.0) continue;
						const double Score = Fall * 100.0 + Shift * 0.05 + (PullOffDepth - Depth);
						if (Score < BestScore) { BestScore = Score; BestAlong = A; BestDepth = Depth; }
					}
				if (BestScore < 1e17)
				{
					Stop.How = PlaceMade;
					Stop.Depth = BestDepth;
					// (its outline: straight along the road's edge, its ends tapering in, as a lay-by's)
					const double Taper = 5.0;
					std::vector<P2> Inner, Outer;
					for (int K = 0; K <= 8; ++K)
					{
						const double U = double(K) / 8.0, A = BestAlong + (U - 0.5) * PullOffLength;
						P2 P, D;
						PointAt(A, P, D);
						const P2 Out = P2(D.Z, -D.X) * SideSign;
						const double FromEnd = std::fmin(U, 1.0 - U) * PullOffLength;
						const double Deep = BestDepth * std::clamp(FromEnd / Taper, 0.15, 1.0);
						Inner.push_back(P + Out * (Half - 0.1));
						Outer.push_back(P + Out * (Half + Deep));
					}
					Stop.Ring = Inner;
					for (auto It = Outer.rbegin(); It != Outer.rend(); ++It) Stop.Ring.push_back(*It);
					P2 Mid, D;
					PointAt(BestAlong, Mid, D);
					const P2 Out = P2(D.Z, -D.X) * SideSign;
					Stop.At = Mid + Out * (Half + BestDepth * 0.5);
					Stop.RoadMid = Mid;
					Stop.RoadDir = D;
					Stop.Out = Out;
					Stop.Board = Mid + Out * (Half + BestDepth + 0.6) + D * (PullOffLength * 0.25);
					Stop.Face = Out * -1.0;
				}
				else
				{
					// (nowhere fit for one: the board on the verge by the foot)
					Stop.How = PlaceStreet;
					const P2 Out = Left * SideSign;
					Stop.At = Near.At + Out * (Half + 0.5);
					Stop.Board = Near.At + Out * (Half + 1.8);
					Stop.Face = Out * -1.0;
				}
			}
		}
		// (a stop near one made already: shared, its board naming both)
		const int Shared = SharedNear(Stop.At);
		if (Shared >= 0)
		{
			PlaceStops[size_t(Shared)].Sights.push_back(Index);
			continue;
		}
		AddStop(std::move(Stop));
	}

	// Each stop's board: the first place's name, a plain marker post for a place with none.
	for (size_t I = 0; I < PlaceStops.size(); ++I)
	{
		const PlaceStop& S = PlaceStops[I];
		Furnish F;
		F.Kind = PlaceBoard;
		F.At = S.Board;
		F.Face = S.Face;
		F.Text = PlaceStopName(int(I));
		F.Height = Ground(S.Board.X, S.Board.Z);
		F.Id = uint32_t(I + 1);
		if (std::isnan(F.Height)) continue;
		Scene->Furniture[SceneKey(F.At.X, F.At.Z)].push_back(int(Furniture.size()));
		Furniture.push_back(std::move(F));
	}
}

int RidePlan::PlaceStopAt(double X, double Z, double Within) const
{
	const int Cx = int(std::floor(X / PlaceCell)), Cz = int(std::floor(Z / PlaceCell));
	const int Reach = int(std::ceil(Within / PlaceCell));
	int Best = -1;
	double BestD = Within;
	for (int Dz = -Reach; Dz <= Reach; ++Dz)
		for (int Dx = -Reach; Dx <= Reach; ++Dx)
		{
			const auto It = PlaceGrid.find(static_cast<long long>(Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == PlaceGrid.end()) continue;
			for (int I : It->second)
			{
				const double D = std::hypot(PlaceStops[size_t(I)].At.X - X, PlaceStops[size_t(I)].At.Z - Z);
				if (D < BestD) { BestD = D; Best = I; }
			}
		}
	return Best;
}

std::string RidePlan::PlaceStopName(int Stop) const
{
	if (Stop < 0 || Stop >= int(PlaceStops.size())) return std::string();
	for (int S : PlaceStops[size_t(Stop)].Sights)
		if (!Area.Roads.Sights[size_t(S)].Name.empty()) return Area.Roads.Sights[size_t(S)].Name;
	return std::string();
}

bool RidePlan::OnPullOff(double X, double Z) const
{
	const int I = PlaceStopAt(X, Z, PullOffLength);
	if (I < 0) return false;
	const PlaceStop& S = PlaceStops[size_t(I)];
	return S.How == PlaceMade && S.Ring.size() >= 3 && InRing(S.Ring, X, Z);
}
} // namespace TorqueWorld

// --- [places stage 3] The first recipes: each place drawn by its kind from the map ------------------------

namespace TorqueWorld
{
namespace
{
using RideShapes::Paint;
using RideShapes::Pt;
using RideShapes::V3;

uint32_t PlaceSeed(const Sight& S)
{
	uint32_t H = 2166136261u;
	for (char C : S.Qid.empty() ? S.Name : S.Qid) H = (H ^ static_cast<unsigned char>(C)) * 16777619u;
	H ^= static_cast<uint32_t>(std::lround(S.At.X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(S.At.Z * 3.0)) * 19349663u;
	return H == 0 ? 1u : H;
}

// The same number from 0 to 1 for the same seed and salt.
double Roll(uint32_t Seed, uint32_t Salt)
{
	uint32_t V = Seed * 2654435761u ^ (Salt + 0x9e3779b9u) * 2246822519u;
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
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

// A block standing on (X, Y, Z): Long along Way (level, unit) by Wide across, High tall; its top Taper
// times its foot, shifted by Lean (m, along and across) and tipped by TopTilt (m from one end to the other).
struct Frame2
{
	double X = 0.0, Z = 0.0, Ux = 1.0, Uz = 0.0;
	V3 At(double Along, double Across, double Y) const { return Pt(X + Ux * Along - Uz * Across, Y, Z + Uz * Along + Ux * Across); }
};
void Block(RideMesh& Out, const Frame2& F, double Y, double Long, double Wide, double High, const Paint& C, double Taper = 1.0, double LeanA = 0.0, double LeanB = 0.0,
	double TopTilt = 0.0, bool Top = true)
{
	const double L0 = Long * 0.5, W0 = Wide * 0.5, L1 = L0 * Taper, W1 = W0 * Taper;
	V3 B[4] = {F.At(-L0, -W0, Y), F.At(L0, -W0, Y), F.At(L0, W0, Y), F.At(-L0, W0, Y)};
	V3 T[4] = {F.At(-L1 + LeanA, -W1 + LeanB, Y + High - TopTilt * 0.5), F.At(L1 + LeanA, -W1 + LeanB, Y + High + TopTilt * 0.5),
		F.At(L1 + LeanA, W1 + LeanB, Y + High + TopTilt * 0.5), F.At(-L1 + LeanA, W1 + LeanB, Y + High - TopTilt * 0.5)};
	const V3 In = F.At(LeanA * 0.5, LeanB * 0.5, Y + High * 0.5);
	for (int K = 0; K < 4; ++K) Face(Out, B[K], B[(K + 1) % 4], T[(K + 1) % 4], T[K], In, C);
	if (Top) Face(Out, T[0], T[1], T[2], T[3], In, Paint{C.R * 1.06f, C.G * 1.06f, C.B * 1.06f});
}

// A four-sided point (a pyramid, a spire) on a square Side across at Y, Tall high.
void Point4(RideMesh& Out, const Frame2& F, double Y, double Side, double Tall, const Paint& C)
{
	const double H = Side * 0.5;
	const V3 Tip = F.At(0.0, 0.0, Y + Tall), In = F.At(0.0, 0.0, Y + Tall * 0.3);
	const V3 B[4] = {F.At(-H, -H, Y), F.At(H, -H, Y), F.At(H, H, Y), F.At(-H, H, Y)};
	for (int K = 0; K < 4; ++K) Face(Out, B[K], B[(K + 1) % 4], Tip, Tip, In, C);
}

// Battlements round a square tower's top: merlons along each side.
void Battlements(RideMesh& Out, const Frame2& F, double Y, double Side, const Paint& C)
{
	const double H = Side * 0.5;
	const int Each = std::max(2, int(Side / 1.1));
	for (int S = 0; S < 4; ++S)
		for (int K = 0; K < Each; K += 1)
		{
			if (K % 2 == 1) continue;
			const double U = -H + (K + 0.5) * Side / Each;
			Frame2 M = F;
			const double A = S == 0 ? U : S == 1 ? H - 0.2 : S == 2 ? -U : -H + 0.2, B = S == 0 ? -H + 0.2 : S == 1 ? U : S == 2 ? H - 0.2 : -U;
			const V3 P = F.At(A, B, 0.0);
			M.X = P.X; M.Z = P.Z;
			Block(Out, M, Y, Side / Each, 0.4, 0.7, C);
		}
}

// The new vertices of a mesh from From on given alpha A (the older ones kept; "1 everywhere" made real).
void AlphaFrom(RideMesh& M, size_t From, float A)
{
	if (M.Alpha.empty()) M.Alpha.assign(From, 1.0f);
	M.Alpha.resize(From);
	M.Alpha.resize(M.Vertices(), A);
}

RideMesh& Layer(RideMesh& Out, std::shared_ptr<RideMesh>& Field)
{
	if (!Field)
	{
		Field = std::make_shared<RideMesh>();
		Field->OriginX = Out.OriginX;
		Field->OriginZ = Out.OriginZ;
	}
	return *Field;
}
} // namespace

void RidePlan::IndexPlaceRecipes()
{
	PlaceRecipes.clear();
	RecipeGrid.clear();
	for (int& C : RecipeCount) C = 0;
	const Region& G = Area.Roads;
	if (!Scene || G.Sights.empty()) return;
	// (the region's buildings near a point: the one whose box holds it or whose middle is nearest, within Within)
	const auto NearestBuilding = [this, &G](const P2& P, double Within, double* Footprint = nullptr)
	{
		int Best = -1;
		double BestD = Within;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
				{
					const Building& H = G.Buildings[size_t(B)];
					const double D = std::hypot(H.X - P.X, H.Z - P.Z) - 0.5 * std::fmin(H.W, H.D);
					if (D < BestD) { BestD = D; Best = B; }
				}
			}
		if (Best >= 0 && Footprint != nullptr) *Footprint = G.Buildings[size_t(Best)].W * G.Buildings[size_t(Best)].D;
		return Best;
	};
	// (a tall building of the towns' (TowerShapes) standing on a point)
	const auto TowerAt = [this](const P2& P)
	{
		for (size_t I = 0; I < TowerShapes.size(); ++I)
			if (std::fabs(TowerShapes[I].Mid.X - P.X) < 80.0 && std::fabs(TowerShapes[I].Mid.Z - P.Z) < 80.0 && RideShapes::InRing(TowerShapes[I].Ring, P.X, P.Z)) return int(I);
		return -1;
	};
	TowerTaken.assign(TowerShapes.size(), 0);
	for (size_t I = 0; I < G.Sights.size(); ++I)
	{
		const Sight& S = G.Sights[I];
		const std::string& K = S.Kind;
		int Kind = -1;
		if (K == "stone_circle") Kind = RecipeCircle;
		else if (K == "standing_stone") Kind = RecipeStone;
		else if (K == "cross") Kind = RecipeCross;
		else if (K == "obelisk") Kind = RecipeObelisk;
		else if (K == "memorial" || K == "monument") Kind = S.Height >= 12.0 ? RecipeObelisk : RecipeMemorial;
		else if (K == "church" || K == "cathedral") Kind = RecipeChurch;
		else if (K == "tower") Kind = RecipeTower;
		else if (K == "lighthouse") Kind = RecipeLighthouse;
		else if (K == "windmill") Kind = RecipeWindmill;
		else if (K == "chimney") Kind = RecipeChimney;
		if (Kind < 0) continue;
		PlaceRecipe R;
		R.Sight = int(I);
		R.Kind = Kind;
		R.At = S.At;
		R.Seed = PlaceSeed(S);
		R.Height = S.Height;
		// (a place mapped as an outline: its middle and size from it)
		if (S.Outline.size() >= 3)
		{
			P2 Mid;
			const double A = std::fabs(RideShapes::RingArea(S.Outline, Mid));
			if (A > 1.0) { R.At = Mid; R.Radius = std::sqrt(A / 3.14159265); }
		}
		const double Y = Ground(R.At.X, R.At.Z);
		if (std::isnan(Y)) continue;
		// (facing the nearest road)
		{
			NearOpts Opts;
			NearHit Hit;
			if (NearestOf(R.At.X, R.At.Z, Opts, Hit) && Hit.Foot.DistanceTo(R.At) > 1.0)
			{
				const P2 D = Hit.Foot - R.At;
				R.Yaw = std::atan2(D.Z, D.X);
			}
			else R.Yaw = Roll(R.Seed, 1) * 6.283185307;
		}
		if (Kind == RecipeChurch)
		{
			// A church the map has a building for is drawn as that building, with its tower (BuildingRole 1);
			// one with none, by the recipe (nave east to west, its tower at the west end).
			double Foot = 0.0;
			const int B = NearestBuilding(R.At, 25.0, &Foot);
			if (B >= 0 && Foot >= 60.0)
			{
				if (size_t(B) < BuildingRole.size()) BuildingRole[size_t(B)] = 1;
				continue;
			}
			if (B >= 0) continue;   // (a small building there: a chapel, as it stands)
			if (TowerAt(R.At) >= 0) continue;
			R.Yaw = 0.0;
		}
		else
		{
			// The building the map has for a tower, lighthouse, windmill or chimney (small, under it) is
			// drawn by the recipe instead, at its size; the stones and memorials stand where they are.
			if (Kind == RecipeTower || Kind == RecipeLighthouse || Kind == RecipeWindmill || Kind == RecipeChimney)
			{
				double Foot = 0.0;
				const int B = NearestBuilding(R.At, 6.0, &Foot);
				if (B >= 0 && Foot <= 400.0)
				{
					const Building& H = G.Buildings[size_t(B)];
					R.At = P2(H.X, H.Z);
					R.Radius = std::fmax(R.Radius, 0.5 * std::sqrt(Foot));
					R.Claim = std::fmax(H.W, H.D) * 0.6 + 1.0;
				}
				const int T = TowerAt(R.At);
				if (T >= 0) TowerTaken[size_t(T)] = 1;
				R.Claim = std::fmax(R.Claim, 3.0);
			}
		}
		R.Base = Ground(R.At.X, R.At.Z);
		if (std::isnan(R.Base)) continue;
		++RecipeCount[Kind];
		RecipeGrid[SceneKey(R.At.X, R.At.Z)].push_back(int(PlaceRecipes.size()));
		PlaceRecipes.push_back(R);
	}
}

bool RidePlan::UnderPlace(double X, double Z) const
{
	for (int Dz = -1; Dz <= 1; ++Dz)
		for (int Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = RecipeGrid.find(SceneKey(X + Dx * SceneCell, Z + Dz * SceneCell));
			if (It == RecipeGrid.end()) continue;
			for (int I : It->second)
			{
				const PlaceRecipe& R = PlaceRecipes[size_t(I)];
				if (R.Claim > 0.0 && std::hypot(R.At.X - X, R.At.Z - Z) < R.Claim) return true;
			}
		}
	return false;
}

void RidePlan::PlacesTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (Detail < 0 || PlaceRecipes.empty()) return;
	const Region& G = Area.Roads;
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell));
	const int CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	// (stone masks for M_Stone and M_Masonry: brightness 0.5 the scan as it is, which scan, lichen)
	const auto StoneMask = [](double Bright, double Bare, double Lichen) { return Paint{float(Bright), float(Bare), float(Lichen)}; };
	const auto MasonryOf = [&Out, this, X0, Z0, Size]() -> RideMesh&
	{
		const bool Fresh = !Out.Masonry;
		RideMesh& M = Layer(Out, Out.Masonry);
		if (Fresh) Out.MasonryTint = LookHere(X0 + Size * 0.5, Z0 + Size * 0.5).HouseStone;
		return M;
	};
	const auto StoneOf = [&Out, this, X0, Z0, Size]() -> RideMesh&
	{
		const bool Fresh = !Out.Stone;
		RideMesh& M = Layer(Out, Out.Stone);
		if (Fresh) Out.StoneTint = LookHere(X0 + Size * 0.5, Z0 + Size * 0.5).WallStone;
		return M;
	};
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = RecipeGrid.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == RecipeGrid.end()) continue;
			for (int Index : It->second)
			{
				const PlaceRecipe& R = PlaceRecipes[size_t(Index)];
				if (R.At.X < X0 || R.At.X >= X0 + Size || R.At.Z < Z0 || R.At.Z >= Z0 + Size) continue;
				const Sight& S = G.Sights[size_t(R.Sight)];
				const uint32_t Seed = R.Seed;
				Frame2 F;
				F.X = R.At.X;
				F.Z = R.At.Z;
				F.Ux = std::cos(R.Yaw);
				F.Uz = std::sin(R.Yaw);
				const double Y = R.Base;
				switch (R.Kind)
				{
				case RecipeCircle:
				case RecipeStone:
				{
					// Standing stones: rough slabs of the local stone, leaning a little, some fallen. A
					// circle's stones round its ring (the map's, or 8 to 15 m across), an outlier or two.
					if (Detail < 1) break;
					RideMesh& M = StoneOf();
					const bool Circle = R.Kind == RecipeCircle;
					const double Radius = Circle ? (R.Radius > 3.0 ? std::fmin(R.Radius, 50.0) : 4.0 + 4.0 * Roll(Seed, 2)) : 0.0;
					const int Count = Circle ? std::clamp(int(Radius * 1.4 + 6.0 * Roll(Seed, 3)), 8, 40) : 1;
					const double Tall = Circle ? (Radius > 20.0 ? 1.0 : 1.4) : 1.8 + 1.6 * Roll(Seed, 4);
					for (int K = 0; K < Count; ++K)
					{
						const double A = 6.283185307 * (K + 0.35 * (Roll(Seed, 10 + K) - 0.5)) / Count;
						const double Rr = Radius * (1.0 + 0.06 * (Roll(Seed, 60 + K) - 0.5));
						Frame2 Sf;
						Sf.X = R.At.X + std::cos(A) * Rr;
						Sf.Z = R.At.Z + std::sin(A) * Rr;
						// (a circle's stones face along the ring; a lone one its own way)
						const double Turn = Circle ? A + 1.5708 + 0.4 * (Roll(Seed, 110 + K) - 0.5) : R.Yaw + Roll(Seed, 5) * 3.14159;
						Sf.Ux = std::cos(Turn);
						Sf.Uz = std::sin(Turn);
						const double Gy = Ground(Sf.X, Sf.Z);
						if (std::isnan(Gy)) continue;
						const double H = Tall * (0.45 + 0.9 * Roll(Seed, 160 + K)), W = (0.55 + 0.6 * Roll(Seed, 210 + K)) * (Circle ? 1.0 : 1.3), T = 0.3 + 0.3 * Roll(Seed, 260 + K);
						const Paint C = StoneMask(0.38 + 0.2 * Roll(Seed, 310 + K), Roll(Seed, 360 + K) < 0.3 ? 1.0 : 0.0, 0.3 + 0.5 * Roll(Seed, 410 + K));
						if (Circle && Roll(Seed, 460 + K) < 0.15)
						{
							// (fallen: lying in the grass)
							Block(M, Sf, Gy - 0.15, H, W, T + 0.1, C, 0.85);
							continue;
						}
						Block(M, Sf, Gy - 0.4, W, T, H + 0.4, C, 0.7 + 0.2 * Roll(Seed, 510 + K), 0.25 * (Roll(Seed, 560 + K) - 0.5) * H, 0.25 * (Roll(Seed, 610 + K) - 0.5) * H,
							0.4 * (Roll(Seed, 660 + K) - 0.5) * W);
					}
					break;
				}
				case RecipeCross:
				case RecipeMemorial:
				{
					// A cross or a memorial: two or three steps, a plinth, then a cross on a tapering shaft
					// (a market or war memorial cross), a short obelisk, or a column with an urn.
					if (Detail < 1) break;
					RideMesh& M = MasonryOf();
					const Paint C = StoneMask(0.5 + 0.12 * Roll(Seed, 2), 1.0, 0.25);
					const Paint Step = StoneMask(0.42, 1.0, 0.4);
					const int Steps = 2 + int(Roll(Seed, 3) * 2.0);
					double At = Y - 0.3, W = 2.6 + 1.2 * Roll(Seed, 4);
					for (int K = 0; K < Steps; ++K)
					{
						Block(M, F, At, W, W, 0.3 + (K == 0 ? 0.3 : 0.0), Step);
						At += 0.3 + (K == 0 ? 0.3 : 0.0);
						W -= 0.7;
					}
					const double Plinth = 1.1 + 0.6 * Roll(Seed, 5);
					Block(M, F, At, W * 0.75, W * 0.75, Plinth, C);
					At += Plinth;
					const int Style = R.Kind == RecipeCross ? 0 : int(Roll(Seed, 6) * 3.0);
					if (Style == 0)
					{
						const double Shaft = 2.4 + 1.4 * Roll(Seed, 7);
						Block(M, F, At, 0.38, 0.38, Shaft, C, 0.7);
						const double Arm = 0.45 + 0.15 * Roll(Seed, 8);
						Block(M, F, At + Shaft - 0.05, 0.24, 0.24, 0.7, C);
						Frame2 Bar = F;
						Block(M, Bar, At + Shaft + 0.1, Arm * 2.0, 0.22, 0.24, C);
					}
					else if (Style == 1) { Block(M, F, At, 0.8, 0.8, 3.0, C, 0.6); Point4(M, F, At + 3.0, 0.48, 0.4, C); }
					else
					{
						RideShapes::Column(M, Pt(R.At.X, At, R.At.Z), 0.35, 0.3, 2.6, Detail >= 2 ? 14 : 8, C);
						Block(M, F, At + 2.6, 0.8, 0.8, 0.25, C);
						RideShapes::Column(M, Pt(R.At.X, At + 2.85, R.At.Z), 0.22, 0.12, 0.5, 8, C, 0.15);
					}
					break;
				}
				case RecipeObelisk:
				{
					// An obelisk: a plinth and a tapering square shaft to a little pyramid; seen from afar
					// when tall.
					const double Tall = R.Height > 4.0 ? R.Height : 8.0 + 6.0 * Roll(Seed, 2);
					if (Detail < 1 && Tall < 15.0) break;
					RideMesh& M = MasonryOf();
					const Paint C = StoneMask(0.5, 1.0, 0.2);
					const double Base = std::fmax(1.2, Tall * 0.14);
					Block(M, F, Y - 0.3, Base * 1.8, Base * 1.8, 0.8, C);
					Block(M, F, Y + 0.5, Base * 1.3, Base * 1.3, Tall * 0.12, C);
					const double ShaftY = Y + 0.5 + Tall * 0.12, Shaft = Tall * 0.82;
					Block(M, F, ShaftY, Base, Base, Shaft, C, 0.65, 0.0, 0.0, 0.0, false);
					Point4(M, F, ShaftY + Shaft, Base * 0.65, Base * 0.5, C);
					break;
				}
				case RecipeChurch:
				{
					// A small church: a nave east to west under a slate roof, a chancel lower at its east
					// end, and a square tower at its west end, battlemented or under a spire. Not where the
					// world's building outlines have a building for it (drawn as that, with its tower).
					if (Detail < 1) break;
					{
						std::vector<std::shared_ptr<const std::vector<OutlinePiece>>> Hold;
						std::vector<const OutlinePiece*> Near;
						std::vector<CellId> Covered;
						OutlinesIn(R.At.X - 25.0, R.At.Z - 25.0, 50.0, Hold, Near, Covered);
						bool Built = false;
						for (const OutlinePiece* P : Near) Built = Built || (std::hypot(P->B.X - R.At.X, P->B.Z - R.At.Z) < 25.0 && P->B.W * P->B.D >= 40.0);
						if (Built) break;
					}
					RideMesh& M = MasonryOf();
					const Paint C = StoneMask(0.48, Roll(Seed, 2) < 0.5 ? 0.0 : 1.0, 0.35);
					const double Long = 14.0 + 6.0 * Roll(Seed, 3), Wide = 6.5 + 1.5 * Roll(Seed, 4), Wall = 5.5 + Roll(Seed, 5), TowerSide = 4.0 + 1.2 * Roll(Seed, 6);
					const double TowerTall = 12.0 + 6.0 * Roll(Seed, 7);
					const bool Spire = Roll(Seed, 8) < 0.3;
					Frame2 T = F;
					const V3 Tw = F.At(-Long * 0.5 - TowerSide * 0.5 + 0.3, 0.0, 0.0);
					T.X = Tw.X; T.Z = Tw.Z;
					Block(M, T, Y - 0.5, TowerSide, TowerSide, TowerTall + 0.5, C);
					if (Spire) Point4(M, T, Y + TowerTall, TowerSide * 0.9, TowerSide * 2.4, StoneMask(0.45, 1.0, 0.2));
					else Battlements(M, T, Y + TowerTall, TowerSide, C);
					Block(M, F, Y - 0.5, Long, Wide, Wall + 0.5, C, 1.0, 0.0, 0.0, 0.0, false);
					Frame2 Ch = F;
					const V3 Cm = F.At(Long * 0.5 + 2.5, 0.0, 0.0);
					Ch.X = Cm.X; Ch.Z = Cm.Z;
					Block(M, Ch, Y - 0.5, 5.0, Wide * 0.75, Wall - 0.5, C, 1.0, 0.0, 0.0, 0.0, false);
					// (the roofs: slate, pitched along the nave and the chancel, and the gable ends in stone)
					RideMesh& Roof = Layer(Out, Out.Roofing);
					const size_t From = Roof.Vertices();
					const Paint Slate = {0.085f, 0.085f, 0.095f};
					for (int Part = 0; Part < 2; ++Part)
					{
						const Frame2& P = Part == 0 ? F : Ch;
						const double L = Part == 0 ? Long : 5.0, Wd = (Part == 0 ? Wide : Wide * 0.75) + 0.5, Eave = Y + (Part == 0 ? Wall : Wall - 1.0), Ridge = Eave + Wd * 0.45;
						const V3 In = P.At(0.0, 0.0, Eave);
						for (double Side : {-1.0, 1.0})
							Face(Roof, P.At(-L * 0.5 - 0.2, Side * Wd * 0.5, Eave), P.At(L * 0.5 + 0.2, Side * Wd * 0.5, Eave), P.At(L * 0.5 + 0.2, 0.0, Ridge), P.At(-L * 0.5 - 0.2, 0.0, Ridge), In, Slate);
						for (double End : {-1.0, 1.0})
							Face(M, P.At(End * L * 0.5, -Wd * 0.5 + 0.25, Eave), P.At(End * L * 0.5, Wd * 0.5 - 0.25, Eave), P.At(End * L * 0.5, 0.0, Ridge - 0.1), P.At(End * L * 0.5, 0.0, Ridge - 0.1),
								P.At(0.0, 0.0, Eave), C);
					}
					AlphaFrom(Roof, From, 0.0f);
					break;
				}
				case RecipeTower:
				{
					// A tower (a folly, a clock or a lookout tower): square and battlemented, or round, in stone.
					const double Tall = R.Height > 4.0 ? R.Height : 12.0 + 8.0 * Roll(Seed, 2);
					if (Detail < 1 && Tall < 12.0) break;
					RideMesh& M = MasonryOf();
					const Paint C = StoneMask(0.47, 1.0, 0.3);
					const double Side = R.Radius > 1.5 ? std::clamp(R.Radius * 1.6, 3.0, 10.0) : 3.5 + 2.5 * Roll(Seed, 3);
					if (Roll(Seed, 4) < 0.35)
					{
						RideShapes::Column(M, Pt(R.At.X, Y - 0.5, R.At.Z), Side * 0.55, Side * 0.5, Tall + 0.5, Detail >= 2 ? 18 : 8, C);
						if (Detail >= 1)
							for (int K = 0; K < 8; ++K)
							{
								const double A = 6.283185307 * K / 8;
								Frame2 Mf = F;
								Mf.X = R.At.X + std::cos(A) * Side * 0.45;
								Mf.Z = R.At.Z + std::sin(A) * Side * 0.45;
								Mf.Ux = -std::sin(A); Mf.Uz = std::cos(A);
								Block(M, Mf, Y + Tall, 0.9, 0.35, 0.7, C);
							}
					}
					else
					{
						Block(M, F, Y - 0.5, Side, Side, Tall + 0.5, C);
						if (Detail >= 1) Battlements(M, F, Y + Tall, Side, C);
					}
					break;
				}
				case RecipeLighthouse:
				{
					// A lighthouse: a white tapering round tower (a red band on some), a gallery, a glazed
					// lantern lit after dark, and a dark dome.
					const double Tall = R.Height > 6.0 ? R.Height : 18.0 + 10.0 * Roll(Seed, 2);
					RideMesh& Wash = Layer(Out, Out.Render);
					const size_t From = Wash.Vertices();
					const Paint White = {0.62f, 0.61f, 0.57f}, Red = {0.30f, 0.03f, 0.025f};
					const double R0 = std::clamp(R.Radius > 1.5 ? R.Radius : Tall * 0.13, 2.0, 7.0), R1 = R0 * 0.7;
					const int Sides = Detail >= 2 ? 20 : Detail >= 1 ? 12 : 8;
					const bool Banded = Roll(Seed, 3) < 0.35;
					if (Banded)
					{
						const int Bands = 3;
						for (int K = 0; K < Bands * 2; ++K)
						{
							const double Ya = Y - 0.5 + (Tall + 0.5) * K / (Bands * 2), Yb = Y - 0.5 + (Tall + 0.5) * (K + 1) / (Bands * 2);
							const double Ra = R0 + (R1 - R0) * K / (Bands * 2), Rb = R0 + (R1 - R0) * (K + 1) / (Bands * 2);
							RideShapes::Column(Wash, Pt(R.At.X, Ya, R.At.Z), Ra, Rb, Yb - Ya, Sides, K % 2 == 0 ? White : Red);
						}
					}
					else RideShapes::Column(Wash, Pt(R.At.X, Y - 0.5, R.At.Z), R0, R1, Tall + 0.5, Sides, White);
					AlphaFrom(Wash, From, 0.25f);
					const Paint Iron = {0.02f, 0.022f, 0.024f};
					RideShapes::Column(Out, Pt(R.At.X, Y + Tall, R.At.Z), R1 + 0.6, R1 + 0.6, 0.25, Sides, Iron);   // the gallery
					if (Detail >= 1)
						for (int K = 0; K < Sides; ++K)
						{
							const double A = 6.283185307 * K / Sides;
							RideShapes::Strut(Out, Pt(R.At.X + std::cos(A) * (R1 + 0.55), Y + Tall + 0.25, R.At.Z + std::sin(A) * (R1 + 0.55)),
								Pt(R.At.X + std::cos(A) * (R1 + 0.55), Y + Tall + 1.25, R.At.Z + std::sin(A) * (R1 + 0.55)), 0.05, Iron);
						}
					const double Lr = R1 * 0.65;
					RideShapes::Column(Out, Pt(R.At.X, Y + Tall + 0.25, R.At.Z), Lr, Lr, 0.5, Sides, Iron);
					RideMesh& Night = Out.NightOf();
					const size_t NightFrom = Night.Vertices();
					RideShapes::Column(Night, Pt(R.At.X, Y + Tall + 0.75, R.At.Z), Lr * 0.95, Lr * 0.95, 2.0, Sides, Paint{3.0f, 2.6f, 1.8f});
					AlphaFrom(Night, NightFrom, 0.0f);
					RideShapes::Column(Out, Pt(R.At.X, Y + Tall + 2.75, R.At.Z), Lr, Lr * 0.3, 1.0, Sides, Iron, 0.5);
					break;
				}
				case RecipeWindmill:
				{
					// A tower mill: a tapering round tower of stone (or tarred, or white), a cap, and four
					// sails standing still.
					const double Tall = R.Height > 5.0 ? std::fmin(R.Height, 30.0) : 11.0 + 5.0 * Roll(Seed, 2);
					const int Sides = Detail >= 2 ? 16 : 8;
					const double R0 = std::clamp(R.Radius > 1.5 ? R.Radius : Tall * 0.32, 3.0, 7.0), R1 = R0 * 0.62;
					const double Look = Roll(Seed, 3);
					if (Look < 0.5)
					{
						RideMesh& M = MasonryOf();
						RideShapes::Column(M, Pt(R.At.X, Y - 0.5, R.At.Z), R0, R1, Tall + 0.5, Sides, StoneMask(0.45, 1.0, 0.3));
					}
					else
					{
						RideMesh& Wash = Layer(Out, Out.Render);
						const size_t From = Wash.Vertices();
						RideShapes::Column(Wash, Pt(R.At.X, Y - 0.5, R.At.Z), R0, R1, Tall + 0.5, Sides, Look < 0.8 ? Paint{0.03f, 0.028f, 0.026f} : Paint{0.6f, 0.59f, 0.55f});
						AlphaFrom(Wash, From, 0.3f);
					}
					const Paint Cap = {0.04f, 0.05f, 0.04f}, Sail = {0.32f, 0.30f, 0.26f};
					RideShapes::Column(Out, Pt(R.At.X, Y + Tall, R.At.Z), R1 + 0.3, R1 * 0.4, R1 * 1.1, Sides, Cap, 0.4);
					if (Detail < 1) break;
					// (the sails on the cap's side facing the wind's usual way, the south west)
					const double Face2 = 3.92699 + 0.4 * (Roll(Seed, 4) - 0.5);
					const double Fx = std::cos(Face2), Fz = std::sin(Face2);
					const V3 Hub = Pt(R.At.X + Fx * (R1 + 0.9), Y + Tall + R1 * 0.45, R.At.Z + Fz * (R1 + 0.9));
					const double SailLong = Tall * 0.75, Turn = Roll(Seed, 5) * 1.5708;
					const double Sx = -Fz, Sz = Fx;   // (across the sails' face)
					for (int K = 0; K < 4; ++K)
					{
						const double A = Turn + K * 1.5708, Ca = std::cos(A), Sa = std::sin(A);
						const V3 Tip = Pt(Hub.X + Sx * Ca * SailLong, Hub.Y + Sa * SailLong, Hub.Z + Sz * Ca * SailLong);
						RideShapes::Strut(Out, Hub, Tip, 0.25, Cap);
						// (the sail's frame: a lattice beside the whip, from a fifth of the way out)
						const double Bx = -Sx * Sa, By = Ca, Bz = -Sz * Sa;   // (across the arm, in the sails' plane)
						const auto At = [&](double U, double V) { return Pt(Hub.X + Sx * Ca * SailLong * U + Bx * V, Hub.Y + Sa * SailLong * U + By * V, Hub.Z + Sz * Ca * SailLong * U + Bz * V); };
						RideShapes::SkyQuad(Out, At(0.2, 0.15), At(1.0, 0.15), At(1.0, 1.9), At(0.2, 1.9), Sail, true);
					}
					break;
				}
				case RecipeChimney:
				{
					// A mill or mine chimney: a tall tapering round stack of brick or stone.
					const double Tall = R.Height > 8.0 ? R.Height : 25.0 + 15.0 * Roll(Seed, 2);
					RideMesh& M = MasonryOf();
					RideShapes::Column(M, Pt(R.At.X, Y - 0.5, R.At.Z), 1.8, 1.0, Tall + 0.5, Detail >= 1 ? 12 : 6, StoneMask(0.4, 1.0, 0.15));
					break;
				}
				default: break;
				}
				(void)S;
			}
		}
}
} // namespace TorqueWorld
