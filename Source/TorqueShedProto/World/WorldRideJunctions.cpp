// [smooth junctions stage 2] The map tidied into whole junctions (beenster, Nov 24: "junctions and roundabouts remain
// messy ... this should be the focus"). The map draws one real junction as several points a few metres apart, a dual
// carriageway as two lines, a slip lane as a small triangle; a junction was built at every point, so one junction on
// the ground became a cluster of tiny overlapping ones. Here the points that are one junction are merged, a ring's
// points are one roundabout, a dual's two halves arriving count as one road, stubs are dropped, and each junction gets
// its kind from the catalogue (T, crossroads, staggered, fork, slip, five ways or more, roundabout, mini, a dual's gap)
// and its through road. Nothing is drawn from it yet: stage 3's builders lay each junction whole from this.
#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace TorqueWorld
{
namespace SmoothJunctionBits
{
// The heading out of a junction along an edge: its first 20 m (or all of it, if shorter).
P2 JunctionHeading(const Region& G, int Edge, int Node)
{
	const std::vector<P2> Line = G.LineFrom(Edge, Node);
	if (Line.size() < 2) return P2(1.0, 0.0);
	P2 To = Line.back();
	double Gone = 0.0;
	for (size_t I = 1; I < Line.size(); ++I)
	{
		Gone += Line[I].DistanceTo(Line[I - 1]);
		if (Gone >= 20.0) { To = Line[I]; break; }
	}
	const P2 D = To - Line.front();
	const double L = D.Length();
	return L > 1e-6 ? D * (1.0 / L) : P2(1.0, 0.0);
}

// The angle between two headings (radians, 0 to pi).
double Between(const P2& A, const P2& B)
{
	return std::acos(std::clamp(A.X * B.X + A.Z * B.Z, -1.0, 1.0));
}

// (union-find over the nodes)
int Root(std::vector<int>& Up, int N)
{
	while (Up[size_t(N)] != N) { Up[size_t(N)] = Up[size_t(Up[size_t(N)])]; N = Up[size_t(N)]; }
	return N;
}
void Join(std::vector<int>& Up, int A, int B)
{
	A = Root(Up, A); B = Root(Up, B);
	if (A != B) Up[size_t(std::max(A, B))] = std::min(A, B);
}
} // namespace SmoothJunctionBits

const char* RidePlan::JunctionKindName(EJunction Kind)
{
	switch (Kind)
	{
	case EJunction::Bend: return "bend";
	case EJunction::T: return "T";
	case EJunction::Cross: return "cross";
	case EJunction::Stagger: return "stagger";
	case EJunction::Fork: return "fork";
	case EJunction::Slip: return "slip";
	case EJunction::Multi: return "multi";
	case EJunction::Ring: return "ring";
	case EJunction::Mini: return "mini";
	case EJunction::DualGap: return "dualgap";
	default: return "none";
	}
}

void RidePlan::IndexJunctions()
{
	using namespace SmoothJunctionBits;
	const Region& G = Area.Roads;
	const int NodeCount = int(G.Nodes.size());
	JunctionPlaces.clear();
	JunctionOfNode.assign(size_t(NodeCount), -1);
	const auto Degree = [&G](int N) { return G.Adjacent[size_t(N)].size(); };
	const auto DualHalf = [&G](int E) { const RoadExtra* X = G.ExtraOf(E); return X != nullptr && !X->Half.empty(); };
	const auto IsLink = [&G](int E) { const std::string& C = G.ClassOf(E); return C.size() > 5 && C.compare(C.size() - 5, 5, "_link") == 0; };
	const auto Stub = [&](int E, int From)
	{
		const RoadEdge& Ed = G.Edges[size_t(E)];
		return Ed.Length < JunctionStub && Degree(G.OtherEnd(E, From)) == 1;
	};
	// (a dual's crossing: an edge, not a half itself, both of whose ends are on a dual's halves)
	const auto OnDual = [&](int N)
	{
		for (int E : G.Adjacent[size_t(N)]) if (DualHalf(E)) return true;
		return false;
	};
	// ([smooth junctions stage 7] a big ring the map draws that isn't round, a town's one-way loop (Bath, Plymouth: 40-150 m
	// across, up to a dozen roads off): its points junctions of their own, built whole as any T or cluster on one-way
	// roads; as one roundabout it was turned down (no kind), its old pieces stepped, half of them bad.
	// WHOLE_GYROLD: one roundabout, as before)
	static const bool GyrOld = std::getenv("WHOLE_GYROLD") != nullptr && std::getenv("WHOLE_GYROLD")[0] != '0';
	const auto RingJ = [this](int N)
	{
		const int R = RingAt(N);
		if (R < 0 || GyrOld) return R;
		const RoadRing& Rr = RoadRings[size_t(R)];
		return !Rr.Circle && !Rr.Mini && Rr.Radius >= WholeGyrLeast ? -1 : R;
	};
	std::vector<int> Up(static_cast<size_t>(NodeCount));
	std::iota(Up.begin(), Up.end(), 0);
	std::vector<char> Junction(size_t(NodeCount), 0);
	for (int N = 0; N < NodeCount; ++N)
	{
		size_t Real = 0;
		for (int E : G.Adjacent[size_t(N)]) Real += Stub(E, N) ? 0 : 1;
		Junction[size_t(N)] = Real >= 3 || RingJ(N) >= 0 ? 1 : 0;
	}
	// (a ring's points: one roundabout)
	for (const RoadRing& R : RoadRings)
		if (R.Nodes.empty() || RingJ(R.Nodes[0]) >= 0)
		for (size_t I = 1; I < R.Nodes.size(); ++I) Join(Up, R.Nodes[0], R.Nodes[I]);
	// (two junction points joined by a short edge: one junction; a slip's link or a dual's gap a little further)
	std::vector<char> Merged(G.Edges.size(), 0);
	for (int E = 0; E < int(G.Edges.size()); ++E)
	{
		const RoadEdge& Ed = G.Edges[size_t(E)];
		if (Ed.A == Ed.B || !Junction[size_t(Ed.A)] || !Junction[size_t(Ed.B)]) continue;
		if (RingJ(Ed.A) >= 0 && RingJ(Ed.B) >= 0) continue;   // (a ring's own: joined above; two rings apart stay apart)
		if (RingJ(Ed.A) >= 0 || RingJ(Ed.B) >= 0) continue;   // (a road onto a ring is an arm of it, however short)
		bool Merge = Ed.Length <= JunctionMerge;
		if (!Merge && Ed.Length <= JunctionGap && !DualHalf(E) && OnDual(Ed.A) && OnDual(Ed.B)) Merge = true;
		if (!Merge && Ed.Length <= JunctionSlip && IsLink(E)) Merge = true;
		if (Merge) { Join(Up, Ed.A, Ed.B); Merged[size_t(E)] = 1; }
	}
	// (a small triangle of short edges, a slip lane's island at a T: one junction)
	for (int N = 0; N < NodeCount; ++N)
	{
		if (!Junction[size_t(N)]) continue;
		for (int E1 : G.Adjacent[size_t(N)])
		{
			const int M = G.OtherEnd(E1, N);
			if (M <= N || !Junction[size_t(M)] || G.Edges[size_t(E1)].Length > JunctionSlip) continue;
			for (int E2 : G.Adjacent[size_t(M)])
			{
				const int K = G.OtherEnd(E2, M);
				if (K == N || !Junction[size_t(K)] || G.Edges[size_t(E2)].Length > JunctionSlip) continue;
				for (int E3 : G.Adjacent[size_t(K)])
					if (G.OtherEnd(E3, K) == N && G.Edges[size_t(E3)].Length <= JunctionSlip * 1.5 && RingJ(N) < 0 && RingJ(M) < 0 && RingJ(K) < 0)
					{
						Join(Up, N, M); Join(Up, M, K);
					}
			}
		}
	}
	// (the clusters, each kept only while its points lie within JunctionSpanMost of each other: a longer chain is
	// split back into its points, kinds of their own; a ring is kept whole however big)
	std::unordered_map<int, std::vector<int>> Groups;
	for (int N = 0; N < NodeCount; ++N)
		if (Junction[size_t(N)]) Groups[Root(Up, N)].push_back(N);
	std::vector<std::vector<int>> Sets;
	for (auto& Gr : Groups)
	{
		std::vector<int>& Ns = Gr.second;
		std::sort(Ns.begin(), Ns.end());
		double Span = 0.0;
		for (size_t I = 0; I < Ns.size(); ++I)
			for (size_t J = I + 1; J < Ns.size(); ++J) Span = std::fmax(Span, G.Nodes[size_t(Ns[I])].DistanceTo(G.Nodes[size_t(Ns[J])]));
		if (Span > JunctionSpanMost && RingJ(Ns[0]) < 0)
			for (int N : Ns) Sets.push_back({N});
		else Sets.push_back(Ns);
	}
	std::sort(Sets.begin(), Sets.end(), [](const std::vector<int>& A, const std::vector<int>& B) { return A[0] < B[0]; });
	for (const std::vector<int>& Ns : Sets)
	{
		JunctionPlace J;
		J.Nodes = Ns;
		const int Ring = RingJ(Ns[0]);
		J.Ring = Ring;
		const auto In = [&Ns](int N) { return std::binary_search(Ns.begin(), Ns.end(), N); };
		P2 Sum(0.0, 0.0);
		for (int N : Ns) Sum = Sum + G.Nodes[size_t(N)];
		J.At = Ring >= 0 ? RoadRings[size_t(Ring)].Mid : Sum * (1.0 / double(Ns.size()));
		for (size_t I = 0; I < Ns.size(); ++I)
			for (size_t K = I + 1; K < Ns.size(); ++K) J.Span = std::fmax(J.Span, G.Nodes[size_t(Ns[I])].DistanceTo(G.Nodes[size_t(Ns[K])]));
		// (its arms: the edges out of it, a stub left off; an edge both of whose ends are in it is inside)
		for (int N : Ns)
			for (int E : G.Adjacent[size_t(N)])
			{
				const RoadEdge& Ed = G.Edges[size_t(E)];
				const int Other = G.OtherEnd(E, N);
				if (In(Other) && Ed.A != Ed.B)
				{
					if (std::find(J.Inside.begin(), J.Inside.end(), E) == J.Inside.end()) J.Inside.push_back(E);
					continue;
				}
				if (Stub(E, N)) continue;
				JunctionArm A;
				A.Edge = E;
				A.Node = N;
				A.Away = JunctionHeading(G, E, N);
				A.Cost = RoadLine::MainCostOf(G.ClassOf(E));
				A.In = Ed.Oneway == 0 || (Ed.Oneway == 1 ? Ed.B == N : Ed.A == N);
				A.Out = Ed.Oneway == 0 || (Ed.Oneway == 1 ? Ed.A == N : Ed.B == N);
				const std::string& Class = G.ClassOf(E);
				A.Unpaved = Class == "track" || G.SurfaceOf(E) == "gravel" || SettsSurface(G.SurfaceOf(E));
				J.Arms.push_back(A);
			}
		std::sort(J.Arms.begin(), J.Arms.end(), [](const JunctionArm& A, const JunctionArm& B) { return std::atan2(A.Away.Z, A.Away.X) < std::atan2(B.Away.Z, B.Away.X); });
		// (a dual's two halves arriving side by side: one road)
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			const RoadExtra* Xa = G.ExtraOf(J.Arms[A].Edge);
			if (Xa == nullptr || Xa->Half.empty()) continue;
			J.Dual = true;
			for (size_t B = 0; B < J.Arms.size(); ++B)
			{
				if (B == A || J.Arms[B].Pair >= 0 || J.Arms[A].Pair >= 0) continue;
				const RoadExtra* Xb = G.ExtraOf(J.Arms[B].Edge);
				const bool Halves = (Xb != nullptr && (Xb->Half == G.Edges[size_t(J.Arms[A].Edge)].Gid || Xa->Half == G.Edges[size_t(J.Arms[B].Edge)].Gid))
					|| (Xb != nullptr && !Xb->Half.empty() && G.ClassOf(J.Arms[A].Edge) == G.ClassOf(J.Arms[B].Edge) && J.Arms[A].In != J.Arms[B].In && Between(J.Arms[A].Away, J.Arms[B].Away) < 0.35);
				if (Halves) { J.Arms[A].Pair = int(B); J.Arms[B].Pair = int(A); }
			}
		}
		J.Roads = 0;
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			J.Roads += J.Arms[A].Pair < 0 || J.Arms[A].Pair > int(A) ? 1 : 0;
			J.Unpaved = J.Unpaved || J.Arms[A].Unpaved;
		}
		for (const MappedPoint& Mp : G.RoadPoints)
			if (Mp.Kind == "traffic_signals" && Mp.At.DistanceTo(J.At) < 20.0 + J.Span * 0.5) J.Lights = true;
		bool MappedMini = false;
		for (const MappedPoint& Mp : G.RoadPoints)
			if (Mp.Kind == "mini_roundabout" && Mp.At.DistanceTo(J.At) < 12.0) MappedMini = true;
		// (the through road: of the roads (a dual's pair as one), the pair of the best class, then the straightest)
		std::vector<int> RoadArm;   // (an arm standing for each road)
		std::vector<P2> RoadHead;
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			if (J.Arms[A].Pair >= 0 && J.Arms[A].Pair < int(A)) continue;
			P2 Away = J.Arms[A].Away;
			if (J.Arms[A].Pair >= 0) { Away = Away + J.Arms[size_t(J.Arms[A].Pair)].Away; const double L = Away.Length(); if (L > 1e-6) Away = Away * (1.0 / L); }
			RoadArm.push_back(int(A));
			RoadHead.push_back(Away);
		}
		int Ta = -1, Tb = -1;
		double BestCost = 1e9, BestBend = 1e9, Tightest = WorldPi;
		for (size_t A = 0; A < RoadArm.size(); ++A)
			for (size_t B = A + 1; B < RoadArm.size(); ++B)
			{
				const double Gap = Between(RoadHead[A], RoadHead[B]);
				Tightest = std::fmin(Tightest, Gap);
				const double Bend = WorldPi - Gap;
				if (Bend > JunctionBent) continue;
				const double C = std::fmax(J.Arms[size_t(RoadArm[A])].Cost, J.Arms[size_t(RoadArm[B])].Cost);
				if (C < BestCost - 0.05 || (C < BestCost + 0.05 && Bend < BestBend)) { BestCost = C; BestBend = Bend; Ta = int(A); Tb = int(B); }
			}
		const auto MarkThrough = [&](int R)
		{
			JunctionArm& A = J.Arms[size_t(RoadArm[size_t(R)])];
			A.Through = true;
			if (A.Pair >= 0) J.Arms[size_t(A.Pair)].Through = true;
		};
		if (Ta >= 0) { MarkThrough(Ta); MarkThrough(Tb); }
		// (its kind)
		int StraightPairs = 0;
		std::vector<char> Used(RoadArm.size(), 0);
		for (size_t A = 0; A < RoadArm.size(); ++A)
			for (size_t B = A + 1; B < RoadArm.size(); ++B)
				if (!Used[A] && !Used[B] && WorldPi - Between(RoadHead[A], RoadHead[B]) < JunctionStraight) { Used[A] = Used[B] = 1; ++StraightPairs; }
		bool Link = false;
		for (const JunctionArm& A : J.Arms) Link = Link || IsLink(A.Edge);
		bool Gap = false;
		for (int E : J.Inside) Gap = Gap || (!DualHalf(E) && J.Dual);
		if (Ring >= 0) J.Kind = RoadRings[size_t(Ring)].Mini || MappedMini ? EJunction::Mini : EJunction::Ring;
		else if (MappedMini) J.Kind = EJunction::Mini;
		else if (J.Roads <= 2) J.Kind = EJunction::Bend;
		else if (J.Roads == 3 && Link && Tightest < KnotFork) J.Kind = EJunction::Slip;
		else if (Gap && J.Roads <= 4) J.Kind = EJunction::DualGap;
		else if (J.Roads == 3) J.Kind = Tightest < KnotFork ? EJunction::Fork : EJunction::T;
		else if (J.Roads == 4) J.Kind = StraightPairs >= 2 ? (Ns.size() >= 2 && J.Span > 6.0 ? EJunction::Stagger : EJunction::Cross) : Ns.size() >= 2 ? EJunction::Stagger : EJunction::Multi;
		else if (J.Roads <= 5) J.Kind = EJunction::Multi;
		else J.Kind = EJunction::None;
		const int Index = int(JunctionPlaces.size());
		for (int N : Ns) JunctionOfNode[size_t(N)] = Index;
		JunctionPlaces.push_back(std::move(J));
	}
}
} // namespace TorqueWorld
