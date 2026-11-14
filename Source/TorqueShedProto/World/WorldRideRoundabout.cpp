// [OSM stage 5B] Roundabouts as rings: the region's roads the map marks as a roundabout (RoadExtra's
// Roundabout flag, from the roadx bake) joined into closed rings, each with its middle and radius, and
// whether its mapped line keeps close enough to a circle to be drawn as one (RingRoundShare of its
// radius, as the Godot game's road_net.gd decides). A ring that doesn't close, or is too big to be a
// roundabout (a one-way town loop), is left as the map draws it.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace TorqueWorld
{
void RidePlan::IndexRings()
{
	RoadRings.clear();
	RingOfNode.clear();
	const Region& G = Area.Roads;
	RoundEdge.assign(G.Edges.size(), 0);
	// The ring edges at each node.
	std::unordered_map<int, std::vector<int>> At;
	std::vector<unsigned char> OnRing(G.Edges.size(), 0);
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadExtra* X = G.ExtraOf(int(E));
		if (X == nullptr || !X->Has(RoadExtra::Roundabout) || G.Edges[E].Line.size() < 2) continue;
		OnRing[E] = 1;
		At[G.Edges[E].A].push_back(int(E));
		At[G.Edges[E].B].push_back(int(E));
	}
	std::vector<unsigned char> Used(G.Edges.size(), 0);
	for (size_t First = 0; First < G.Edges.size(); ++First)
	{
		if (!OnRing[First] || Used[First]) continue;
		// Walk on from edge to edge round the ring until back at the first node.
		RoadRing R;
		const int Start = G.Edges[First].A;
		int Node = Start, Edge = int(First);
		bool Closed = false;
		for (int Guard = 0; Guard < 64; ++Guard)
		{
			const RoadEdge& Re = G.Edges[size_t(Edge)];
			Used[size_t(Edge)] = 1;
			R.Edges.push_back(Edge);
			R.Nodes.push_back(Node);
			const bool Forward = Re.A == Node;
			// (its points round, without the last: the next edge's first)
			if (Forward)
				for (size_t I = 0; I + 1 < Re.Line.size(); ++I) R.Line.push_back(Re.Line[I]);
			else
				for (size_t I = Re.Line.size() - 1; I > 0; --I) R.Line.push_back(Re.Line[I]);
			Node = Forward ? Re.B : Re.A;
			if (Node == Start) { Closed = true; break; }
			int Next = -1;
			for (int Ne : At[Node])
				if (Ne != Edge && !Used[size_t(Ne)]) { Next = Ne; break; }
			if (Next < 0) break;
			Edge = Next;
		}
		if (!Closed || R.Line.size() < 4) continue;
		// Its middle (the area's), its radius (the mean distance), and how far its line strays from that circle.
		double A2 = 0.0, Cx = 0.0, Cz = 0.0, Perimeter = 0.0;
		const size_t N = R.Line.size();
		for (size_t I = 0; I < N; ++I)
		{
			const P2& P = R.Line[I];
			const P2& Q = R.Line[(I + 1) % N];
			const double Cross = P.X * Q.Z - Q.X * P.Z;
			A2 += Cross;
			Cx += (P.X + Q.X) * Cross;
			Cz += (P.Z + Q.Z) * Cross;
			Perimeter += P.DistanceTo(Q);
		}
		if (std::fabs(A2) < 1e-6) continue;
		R.Mid = P2(Cx / (3.0 * A2), Cz / (3.0 * A2));
		R.Clockwise = A2 > 0.0;   // (x east, z south: a positive area runs clockwise seen from above)
		double Sum = 0.0;
		for (const P2& P : R.Line) Sum += P.DistanceTo(R.Mid);
		R.Radius = Sum / double(N);
		for (const P2& P : R.Line) R.Stray = std::fmax(R.Stray, std::fabs(P.DistanceTo(R.Mid) - R.Radius));
		if (R.Radius > RingMaxRadius) continue;   // (a one-way loop round a town block, not a roundabout)
		R.Circle = R.Stray <= RingRoundShare * R.Radius + 1.0;
		R.Mini = R.Radius <= RingMiniRadius;
		const int Index = int(RoadRings.size());
		for (int Nd : R.Nodes) RingOfNode[Nd] = Index;
		if (R.Circle)
			for (int Re : R.Edges) RoundEdge[size_t(Re)] = 1;
		(void)Perimeter;
		RoadRings.push_back(std::move(R));
	}
}

// Each ring near enough a circle drawn as one: its nodes moved onto the circle, each of its edges a
// true arc with a point every RingStep metres, and every other road's end at a moved node moved with
// it. Run once on the region before anything lays a road (LayOut); running it again changes nothing.
int RidePlan::RoundRings()
{
	Region& G = Area.Roads;
	int Rounded = 0;
	for (const RoadRing& R : RoadRings)
	{
		if (!R.Circle || R.Radius < 3.0) continue;
		const auto Angle = [&R](const P2& P) { return std::atan2(P.Z - R.Mid.Z, P.X - R.Mid.X); };
		const auto OnCircle = [&R](double A) { return P2(R.Mid.X + R.Radius * std::cos(A), R.Mid.Z + R.Radius * std::sin(A)); };
		// (the way round: + is clockwise seen from above, x east and z south, as atan2 grows)
		const double Way = R.Clockwise ? 1.0 : -1.0;
		std::unordered_map<int, P2> Moved;
		for (int Nd : R.Nodes) Moved[Nd] = OnCircle(Angle(G.Nodes[size_t(Nd)]));
		for (size_t K = 0; K < R.Edges.size(); ++K)
		{
			RoadEdge& E = G.Edges[size_t(R.Edges[K])];
			const int From = R.Nodes[K], To = R.Nodes[(K + 1) % R.Nodes.size()];
			const double A0 = Angle(Moved[From]);
			double Sweep = Angle(Moved[To]) - A0;
			// (the sweep the way the ring runs, 0 to a whole turn)
			while (Sweep * Way <= 0.0) Sweep += Way * 6.283185307;
			while (Sweep * Way > 6.283185307) Sweep -= Way * 6.283185307;
			const int Bits = std::max(1, int(std::ceil(std::fabs(Sweep) * R.Radius / RingStep)));
			std::vector<P2> Arc;
			for (int I = 0; I <= Bits; ++I) Arc.push_back(OnCircle(A0 + Sweep * double(I) / Bits));
			Arc.front() = Moved[From];
			Arc.back() = Moved[To];
			if (E.A != From) std::reverse(Arc.begin(), Arc.end());
			E.Line = Arc;
			double Run = 0.0;
			for (size_t I = 1; I < Arc.size(); ++I) Run += Arc[I].DistanceTo(Arc[I - 1]);
			E.Length = Run;
		}
		// The nodes, and the other roads' ends at them.
		for (const auto& Kv : Moved)
		{
			G.Nodes[size_t(Kv.first)] = Kv.second;
			if (size_t(Kv.first) >= G.Adjacent.size()) continue;
			for (int Ae : G.Adjacent[size_t(Kv.first)])
			{
				RoadEdge& E = G.Edges[size_t(Ae)];
				if (E.Line.empty()) continue;
				if (E.A == Kv.first) E.Line.front() = Kv.second;
				if (E.B == Kv.first) E.Line.back() = Kv.second;
			}
		}
		++Rounded;
	}
	return Rounded;
}

bool RidePlan::OnRoundRing(int Edge) const
{
	return Edge >= 0 && size_t(Edge) < RoundEdge.size() && RoundEdge[size_t(Edge)] != 0;
}

int RidePlan::RingAt(int Node) const
{
	const auto It = RingOfNode.find(Node);
	return It == RingOfNode.end() ? -1 : It->second;
}
} // namespace TorqueWorld
