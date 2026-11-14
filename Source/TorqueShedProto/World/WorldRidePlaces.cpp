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
