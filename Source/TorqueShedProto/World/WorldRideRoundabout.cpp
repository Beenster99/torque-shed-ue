// [OSM stage 5B] Roundabouts as rings: the region's roads the map marks as a roundabout (RoadExtra's
// Roundabout flag, from the roadx bake) joined into closed rings, each with its middle and radius, and
// whether its mapped line keeps close enough to a circle to be drawn as one (RingRoundShare of its
// radius, as the Godot game's road_net.gd decides). A ring that doesn't close, or is too big to be a
// roundabout (a one-way town loop), is left as the map draws it.
#include "WorldRide.h"
#include "WorldRoadLine.h"
#include "WorldRideRoadX.h"
#include "WorldRideSigns.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace TorqueWorld
{
void RidePlan::IndexRings()
{
	RoadRings.clear();
	RingOfNode.clear();
	RingOfEdge.clear();
	RingCells.clear();
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
		// [roundabouts] (one width all round, the widest its edges have, and at least two lanes' worth on a
		// big ring: its edges are often mapped as different classes, and each was its own width, a step in
		// the tarmac where they met; never so wide the island goes under 40% of the ring)
		for (int Re : R.Edges) R.Half = std::fmax(R.Half, RoadX::Of(G, Re, RoadLine::WidthOf(G.ClassOf(Re))).Half);
		if (R.Radius >= 20.0) R.Half = std::fmax(R.Half, 4.0);
		const double HalfUnclamped = R.Half;
		R.Half = std::fmin(R.Half, std::fmax(2.5, 0.6 * R.Radius));
		R.Reach = R.Radius;
		// (a small one, a compact roundabout, is left as the map draws it: on a tight arc held exactly, its
		// roads' shoulders gather over its island)
		R.Circle = R.Radius >= RingRoundLeast && R.Stray <= RingRoundShare * R.Radius + 1.0;
		R.Mini = R.Radius <= RingMiniRadius;
		if (!R.Circle && R.Radius >= RingRoundLeast)
		{
			// [roundabouts round 2] not round: its own shape, if it has one
			RoadRing Try = R;
			Try.Half = HalfUnclamped;
			if (ShapeRing(Try)) R = std::move(Try);
		}
		// [roundabouts round 2] (a road through the middle, over or under it: an interchange's ring, not an
		// island to grass over; left as the map draws it)
		if (R.Circle)
		{
			for (size_t E = 0; E < G.Edges.size() && R.Circle; ++E)
			{
				const RoadEdge& Re = G.Edges[E];
				if (OnRing[E] || Re.Line.size() < 2) continue;
				if (std::find(R.Nodes.begin(), R.Nodes.end(), Re.A) != R.Nodes.end() || std::find(R.Nodes.begin(), R.Nodes.end(), Re.B) != R.Nodes.end()) continue;
				const std::string& Class = G.ClassOf(int(E));
				if (Class == "path" || Class == "footway" || Class == "cycleway" || Class == "bridleway" || Class == "steps" || Class == "track") continue;
				// (along each of its stretches, every few metres: a straight road may have no point near)
				bool Through = false;
				for (size_t I = 1; I < Re.Line.size() && !Through; ++I)
				{
					const P2 P0 = Re.Line[I - 1], P1 = Re.Line[I];
					const double Len = P0.DistanceTo(P1);
					const P2 D = P1 - P0;
					// (the nearest it comes to the middle: none of it near, nothing to check)
					const double T0 = Len > 1e-9 ? std::clamp(((R.Mid.X - P0.X) * D.X + (R.Mid.Z - P0.Z) * D.Z) / (Len * Len), 0.0, 1.0) : 0.0;
					if ((P0 + D * T0).DistanceTo(R.Mid) >= R.Reach) continue;
					const int Bits = std::max(1, int(std::ceil(Len / 4.0)));
					for (int K = 0; K <= Bits && !Through; ++K)
					{
						const P2 P = P0 + D * (double(K) / Bits);
						Through = P.DistanceTo(R.Mid) < R.Reach && R.OffOf(P) < -R.Half - 1.0;
					}
				}
				if (Through) { R.Circle = false; R.Shaped = false; R.Prof.clear(); R.Reach = R.Radius; }
			}
		}
		const int Index = int(RoadRings.size());
		for (int Nd : R.Nodes) RingOfNode[Nd] = Index;
		if (R.Circle)
		{
			for (int Re : R.Edges) { RoundEdge[size_t(Re)] = 1; RingOfEdge[Re] = Index; }
			const double Reach = R.Reach + R.Half + 40.0;
			for (long long Rz = static_cast<long long>(std::floor((R.Mid.Z - Reach) / 64.0)); Rz <= static_cast<long long>(std::floor((R.Mid.Z + Reach) / 64.0)); ++Rz)
				for (long long Rx = static_cast<long long>(std::floor((R.Mid.X - Reach) / 64.0)); Rx <= static_cast<long long>(std::floor((R.Mid.X + Reach) / 64.0)); ++Rx)
					RingCells[Rz * 4000000LL + Rx].push_back(Index);
		}
		(void)Perimeter;
		RoadRings.push_back(std::move(R));
	}
}

// [roundabouts round 2] A ring's shape. A circle (Prof empty) is Radius all round; a shaped ring has
// its middle line's distance by bearing, read off between bins.
double RidePlan::RoadRing::RAt(double Angle) const
{
	if (Prof.empty()) return Radius;
	double U = Angle / 6.283185307 * double(RingProfBins);
	U -= std::floor(U / double(RingProfBins)) * double(RingProfBins);
	const int I = std::min(RingProfBins - 1, int(U));
	const double F = U - double(I);
	return Prof[size_t(I)] * (1.0 - F) + Prof[size_t((I + 1) % RingProfBins)] * F;
}

double RidePlan::RoadRing::RSlope(double Angle) const
{
	if (Prof.empty()) return 0.0;
	const double H = 6.283185307 / double(RingProfBins);
	return (RAt(Angle + H) - RAt(Angle - H)) / (2.0 * H);
}

P2 RidePlan::RoadRing::At(double Angle, double Off) const
{
	const double C = std::cos(Angle), S = std::sin(Angle), Rr = RAt(Angle);
	if (Prof.empty()) return P2(Mid.X + (Rr + Off) * C, Mid.Z + (Rr + Off) * S);
	// (square to the line r(a): out along the bearing less the way it turns, r u - r' v)
	const double Rp = RSlope(Angle);
	P2 N(Rr * C + Rp * S, Rr * S - Rp * C);
	const double L = N.Length();
	N = L > 1e-9 ? N * (1.0 / L) : P2(C, S);
	return P2(Mid.X + Rr * C, Mid.Z + Rr * S) + N * Off;
}

double RidePlan::RoadRing::OffOf(const P2& P) const
{
	const double D = P.DistanceTo(Mid), A = std::atan2(P.Z - Mid.Z, P.X - Mid.X), Rr = RAt(A);
	if (Prof.empty()) return D - Rr;
	const double Rp = RSlope(A);
	return (D - Rr) * Rr / std::sqrt(Rr * Rr + Rp * Rp);
}

double RidePlan::RoadRing::Least() const
{
	if (Prof.empty()) return Radius;
	return *std::min_element(Prof.begin(), Prof.end());
}

// A ring that strays too far from a circle but is still a roundabout's shape: going once round its
// middle without its bearing ever turning back (an oval, a rounded triangle or square, not a loop that
// folds round a corner). Its line by bearing, smoothed (the mapped corners rounded, as the kerbs are),
// held only if its island keeps RingIslandLeast across at its narrowest and neither edge folds over
// itself where it bends tightest.
bool RidePlan::ShapeRing(RoadRing& R) const
{
	const size_t N = R.Line.size();
	if (N < 6) return false;
	const auto Bearing = [&R](const P2& P) { return std::atan2(P.Z - R.Mid.Z, P.X - R.Mid.X); };
	double Turn = 0.0;
	std::vector<double> Steps(N);
	for (size_t I = 0; I < N; ++I)
	{
		Steps[I] = std::remainder(Bearing(R.Line[(I + 1) % N]) - Bearing(R.Line[I]), 6.283185307);
		Turn += Steps[I];
	}
	if (std::fabs(std::fabs(Turn) - 6.283185307) > 0.05) return false;
	for (double D : Steps)
		if (D * Turn < -0.01) return false;   // (it turns back on itself: not star-shaped about its middle)
	// Its distance by bearing: where a ray from the middle crosses its line.
	std::vector<double> Prof(static_cast<size_t>(RingProfBins), 0.0);
	for (int B = 0; B < RingProfBins; ++B)
	{
		const double A = 6.283185307 * B / RingProfBins;
		const P2 U(std::cos(A), std::sin(A));
		double Best = -1.0;
		for (size_t I = 0; I < N; ++I)
		{
			const P2 P = R.Line[I] - R.Mid, D = R.Line[(I + 1) % N] - R.Line[I];
			const double Den = U.X * D.Z - U.Z * D.X;
			if (std::fabs(Den) < 1e-12) continue;
			const double Sr = (P.X * D.Z - P.Z * D.X) / Den, T = (P.X * U.Z - P.Z * U.X) / Den;
			if (T >= -1e-9 && T <= 1.0 + 1e-9 && Sr > 0.0) Best = std::fmax(Best, Sr);
		}
		if (Best <= 0.0) return false;
		Prof[size_t(B)] = Best;
	}
	// (smoothed round: the map's corners are points on a few metres of kerb)
	for (int Pass = 0; Pass < 4; ++Pass)
	{
		std::vector<double> Next(Prof.size());
		for (size_t B = 0; B < Prof.size(); ++B)
			Next[B] = 0.25 * Prof[(B + Prof.size() - 1) % Prof.size()] + 0.5 * Prof[B] + 0.25 * Prof[(B + 1) % Prof.size()];
		Prof.swap(Next);
	}
	RoadRing T = R;
	T.Prof = Prof;
	T.Reach = *std::max_element(Prof.begin(), Prof.end());
	if (T.Reach > RingMaxRadius) return false;
	const double Least = T.Least();
	T.Half = std::fmin(R.Half, std::fmax(2.5, 0.6 * Least));
	if (Least - T.Half < RingIslandLeast * 0.5 || T.Reach > 3.5 * Least) return false;
	// (neither edge folds: round each, the points' bearings keep turning the one way)
	for (const double Off : {-T.Half - 0.6, T.Half + 0.6})
	{
		double Prev = Bearing(T.At(0.0, Off));
		for (int B = 1; B <= RingProfBins; ++B)
		{
			const double Now = Bearing(T.At(6.283185307 * B / RingProfBins, Off));
			if (std::remainder(Now - Prev, 6.283185307) <= 0.0) return false;
			Prev = Now;
		}
	}
	T.Shaped = true;
	T.Circle = true;
	T.Stray = 0.0;
	R = std::move(T);
	return true;
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
		// ([roundabouts round 2] on its own shape, if it has one: by bearing, its middle line)
		const auto OnCircle = [&R](double A) { return R.Prof.empty() ? P2(R.Mid.X + R.Radius * std::cos(A), R.Mid.Z + R.Radius * std::sin(A)) : P2(R.Mid.X + R.RAt(A) * std::cos(A), R.Mid.Z + R.RAt(A) * std::sin(A)); };
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
			const int Bits = std::max(1, int(std::ceil(std::fabs(Sweep) * R.Reach / RingStep)));
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

// A roundabout's island, near the rider (Detail): its rim the ring's inner edge (a true circle, the ring
// one width all round), at the road's height there. [roundabouts] (beenster on 154: "a lot of poor implementations of roundabouts") Round the
// rim a kerb (RingKerb high, its face a 45 degree slope: seen more than hit, as the town kerbs); on a
// small island an overrun apron of paving inside it and a second kerb; then grass in the road material's
// verge grass (the land's grass scan, not a flat colour), rising a little to the middle and never under
// the ground, so the land never shows through it and a sloping site rises into a mound. Then the ring's
// markings (RingPaint).
void RidePlan::RingsTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (Detail < 1 || RoadRings.empty()) return;
	static constexpr float KerbStone[3] = {0.16f, 0.16f, 0.15f}, Paving[3] = {0.12f, 0.118f, 0.11f}, IslandGrass[3] = {0.10f, 0.16f, 0.03f};
	for (const RoadRing& R : RoadRings)
	{
		if (!R.Circle || R.Mid.X < X0 || R.Mid.X >= X0 + Size || R.Mid.Z < Z0 || R.Mid.Z >= Z0 + Size) continue;
		const double Most = R.Least() - R.Half - 0.1;
		if (Most >= 1.0)
		{
			// ([roundabouts round 2] the rim's points by bearing on the ring's own shape, a circle or not)
			const int Bearings = std::clamp(int(std::ceil(6.283185307 * (R.Reach - R.Half - 0.1) / 1.2)), 24, 160);
			const double RimOff = -R.Half - 0.1;
			const auto RimAt = [&R, RimOff, Bearings](int K, double In) { return R.At(6.283185307 * K / Bearings, RimOff - In); };
			std::vector<double> Rim(static_cast<size_t>(Bearings), 0.0), RimY(static_cast<size_t>(Bearings), 0.0);
			NearOpts Opts;
			Opts.NoBores = true;
			bool Any = false;
			for (int K = 0; K < Bearings; ++K)
			{
				// ([roundabouts] the ring is one width all round now: its inner edge is the rim; the roads onto
				// it that reach in past that end under the island)
				const P2 RimP = RimAt(K, 0.0), Tar = RimAt(K, -0.6);
				double Y = std::nan("");
				// (the road's surface at the rim)
				NearHit Edge;
				if (NearestOf(Tar.X, Tar.Z, Opts, Edge))
				{
					Y = Edge.Height + Edge.Cross;
					if (Edge.Which > 0) Y = JoinedHeight(Edge.Which, Tar, Y);
				}
				if (std::isnan(Y)) Y = Ground(RimP.X, RimP.Z);
				Rim[size_t(K)] = std::fmax(0.5, RimP.DistanceTo(R.Mid));
				RimY[size_t(K)] = Y;
				Any = Any || !std::isnan(Y);
			}
			if (Any)
			{
				for (int K = 0; K < Bearings; ++K)
					if (std::isnan(RimY[size_t(K)]))
						for (int J = 1; J < Bearings; ++J)
							if (!std::isnan(RimY[size_t((K + J) % Bearings)])) { RimY[size_t(K)] = RimY[size_t((K + J) % Bearings)]; break; }
				double Least = 1e18;
				for (double Rr : Rim) Least = std::fmin(Least, Rr);
				// The profile in from the rim: (how far in, how far up, its look), each a ring of points.
				struct Band
				{
					double In, Up;
					const float* Look;
				};
				std::vector<Band> Bands;
				Bands.push_back({0.0, 0.005, KerbStone});
				Bands.push_back({RingKerb, RingKerb, KerbStone});
				Bands.push_back({RingKerb + 0.15, RingKerb, KerbStone});
				double Inside = RingKerb + 0.15, Lift = RingKerb;
				double ApronIn = 0.0, ApronWide = 0.0;   // [roundabouts round 2] (the apron's, if it has one)
				if (2.0 * Least < RingApronUnder)
				{
					// (an overrun apron: paving a lorry's back wheels can cross, then a taller kerb)
					const double Apron = std::fmin(RingApron, std::fmax(0.0, Least - Inside - 1.2));
					if (Apron > 0.4)
					{
						ApronIn = Inside;
						ApronWide = Apron;
						Bands.push_back({Inside, RingKerb, Paving});
						Bands.push_back({Inside + Apron, RingKerb + 0.03, Paving});
						Bands.push_back({Inside + Apron, RingKerb + 0.03, KerbStone});
						Bands.push_back({Inside + Apron + 0.1, RingKerb * 2.0, KerbStone});
						Bands.push_back({Inside + Apron + 0.25, RingKerb * 2.0, KerbStone});
						Inside += Apron + 0.25;
						Lift = RingKerb * 2.0;
					}
				}
				Bands.push_back({Inside, Lift, IslandGrass});
				// (the grass in to the middle: a gentle dome, never under the ground)
				const double Dome = std::fmin(0.5, 0.03 * Least);
				// (a ring of points every 2.5 m or so in to the middle, so the land between them can't rise through it)
				const int Rounds = std::clamp(int(std::ceil((Least - Inside) / 2.5)), 3, 16);
				const uint32_t Base = uint32_t(Out.Vertices());
				const auto Put = [&](double X, double Y, double Z, const float* Look)
				{
					RideShapes::Vert(Out, RideShapes::Pt(X, Y, Z), RideShapes::Pt(0.0, 1.0, 0.0), RideShapes::Paint{Look[0], Look[1], Look[2]}, (X - R.Mid.X) / 4.0, (Z - R.Mid.Z) / 4.0);
				};
				// (each band's ring: a ring of points per bearing; a band whose look changes is a fresh ring, so colours don't smear)
				std::vector<uint32_t> RingStart;
				std::vector<char> Joins;   // (whether a ring joins the one before it with quads)
				for (size_t Bd = 0; Bd < Bands.size(); ++Bd)
				{
					RingStart.push_back(uint32_t(Out.Vertices()));
					Joins.push_back(Bd > 0 && Bands[Bd].Look == Bands[Bd - 1].Look ? 1 : 0);
					for (int K = 0; K < Bearings; ++K)
					{
						const P2 Pt = RimAt(K, Bands[Bd].In);
						Put(Pt.X, RimY[size_t(K)] + Bands[Bd].Up, Pt.Z, Bands[Bd].Look);
					}
				}
				for (int Ring = 1; Ring <= Rounds; ++Ring)
				{
					const double Share = 1.0 - double(Ring) / Rounds;   // (1 the grass's edge, 0 the middle)
					RingStart.push_back(uint32_t(Out.Vertices()));
					Joins.push_back(1);
					for (int K = 0; K < (Ring == Rounds ? 1 : Bearings); ++K)
					{
						// (in from the grass's edge towards the middle)
						const P2 Pt = R.Mid + (RimAt(K, Inside) - R.Mid) * Share;
						const double X = Pt.X, Z = Pt.Z;
						double Mean = 0.0;
						for (double Ry : RimY) Mean += Ry;
						Mean /= double(Bearings);
						// (over the middle, the rim's mean height; out at the edge, the rim's own here)
						double Y = (Ring == Rounds ? Mean : RimY[size_t(K)] * Share + Mean * (1.0 - Share)) + Lift + Dome * (1.0 - Share * Share);
						const double Gy = Ground(X, Z);
						if (!std::isnan(Gy)) Y = std::fmax(Y, Gy + 0.12);
						Put(X, Y, Z, IslandGrass);
					}
				}
				(void)Base;
				const auto Up = [&Out](uint32_t A, uint32_t B, uint32_t C)
				{
					const float* P = Out.Pos.data();
					const double Ax = P[A * 3], Az = P[A * 3 + 2], Bx = P[B * 3], Bz = P[B * 3 + 2], Cx2 = P[C * 3], Cz2 = P[C * 3 + 2];
					const double Ny = (Bz - Az) * (Cx2 - Ax) - (Bx - Ax) * (Cz2 - Az);
					if (std::fabs(Ny) < 1e-12) return;
					if (Ny >= 0.0) Out.Idx.insert(Out.Idx.end(), {A, B, C});
					else Out.Idx.insert(Out.Idx.end(), {A, C, B});
				};
				// (the kerb's face is steep: made to face out and up, by which way it climbs)
				const auto Face = [&Out](uint32_t A, uint32_t B, uint32_t C, const P2& Mid2)
				{
					const float* P = Out.Pos.data();
					const double Ux = P[B * 3] - P[A * 3], Uy = P[B * 3 + 1] - P[A * 3 + 1], Uz = P[B * 3 + 2] - P[A * 3 + 2];
					const double Vx = P[C * 3] - P[A * 3], Vy = P[C * 3 + 1] - P[A * 3 + 1], Vz = P[C * 3 + 2] - P[A * 3 + 2];
					const double Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
					const double Ox = P[A * 3] + Out.OriginX - Mid2.X, Oz = P[A * 3 + 2] + Out.OriginZ - Mid2.Z;
					// (Unreal's front face, as the plan winds them: anticlockwise seen from the front, so the
					// normal by this cross product, x east, y up, z south, points out of its back: flip it)
					if (-(Nx * Ox + Nz * Oz) + -Ny * 0.2 >= 0.0) Out.Idx.insert(Out.Idx.end(), {A, B, C});
					else Out.Idx.insert(Out.Idx.end(), {A, C, B});
				};
				// [roundabouts round 2] (beenster's photo: "Some roundabouts have chevron tiling") Over the apron's
				// setts, chevrons of dark blocks pointing the way round (clockwise in Britain), one every 1.4 m.
				if (ApronWide > 0.4)
				{
					static constexpr float Dark[3] = {0.022f, 0.022f, 0.025f};
					const double Way = KeepLeft() ? 1.0 : -1.0;   // (+: bearing growing, clockwise seen from above)
					const double Mid2 = ApronIn + ApronWide * 0.5, Arm = ApronWide * 0.5 - 0.12, Thick = 0.22;
					const double Rr = std::fmax(1.0, R.Least() - R.Half - 0.1 - Mid2);
					const int Count = std::max(6, int(std::floor(6.283185307 * Rr / 1.4)));
					const auto YAt = [&](double A)
					{
						double U = A / 6.283185307 * Bearings;
						U -= std::floor(U / Bearings) * Bearings;
						const int K0 = std::min(Bearings - 1, int(U));
						const double F = U - K0;
						return RimY[size_t(K0)] * (1.0 - F) + RimY[size_t((K0 + 1) % Bearings)] * F + RingKerb + 0.02;
					};
					const auto Pt = [&](double A, double In) { const P2 P = R.At(A, RimOff - In); return std::make_pair(P, YAt(A)); };
					for (int C = 0; C < Count; ++C)
					{
						const double A0 = 6.283185307 * C / Count;
						const double Tip = 0.55 / Rr, Th = Thick / Rr;
						// (two strips from each edge of the apron back from its tip on the middle)
						for (const double Side : {-1.0, 1.0})
						{
							const auto P0 = Pt(A0, Mid2 + Side * Arm), P1 = Pt(A0 + Way * Th, Mid2 + Side * Arm);
							const auto P2_ = Pt(A0 + Way * (Tip + Th), Mid2), P3_ = Pt(A0 + Way * Tip, Mid2);
							const uint32_t V = uint32_t(Out.Vertices());
							for (const auto* Q : {&P0, &P1, &P2_, &P3_})
								RideShapes::Vert(Out, RideShapes::Pt(Q->first.X, Q->second, Q->first.Z), RideShapes::Pt(0.0, 1.0, 0.0), RideShapes::Paint{Dark[0], Dark[1], Dark[2]}, (Q->first.X - R.Mid.X) / 4.0, (Q->first.Z - R.Mid.Z) / 4.0);
							Up(V, V + 1, V + 2);
							Up(V, V + 2, V + 3);
						}
					}
				}
				for (size_t Rg = 1; Rg < RingStart.size(); ++Rg)
				{
					if (!Joins[Rg]) continue;
					const uint32_t In0 = RingStart[Rg - 1], In1 = RingStart[Rg];
					const bool Last = Rg + 1 == RingStart.size();
					if (Last)
					{
						for (int K = 0; K < Bearings; ++K) Up(In0 + uint32_t(K), In0 + uint32_t((K + 1) % Bearings), In1);
						continue;
					}
					const bool Steep = Rg < Bands.size() && Bands[Rg].Up - Bands[Rg - 1].Up > 0.05;
					for (int K = 0; K < Bearings; ++K)
					{
						const uint32_t A = In0 + uint32_t(K), B = In0 + uint32_t((K + 1) % Bearings);
						const uint32_t C = In1 + uint32_t(K), D = In1 + uint32_t((K + 1) % Bearings);
						if (Steep) { Face(A, B, D, R.Mid); Face(A, D, C, R.Mid); }
						else { Up(A, B, D); Up(A, D, C); }
					}
				}
			}
		}
		RingPaint(R, Out);
	}
}

// [roundabouts] A ring's markings, as Britain paints them, a hair over whatever tarmac is there: a solid
// edge line round the island; a broken lane line round the middle of a ring two lanes wide; the outer
// edge line, broken where each road meets the ring; at each road's way on, a single broken give-way line
// (600 mm marks, 300 mm gaps, 200 mm deep) along the ring's edge across its entry lanes; and between a
// two-way road's halves near the ring, a painted splitter: a teardrop outlined in white and hatched.
void RidePlan::RingPaint(const RoadRing& R, RideMesh& Out) const
{
	static constexpr float Paint[3] = {0.83f, 0.83f, 0.75f};
	// ([roundabouts round 2] on the ring's own shape: offsets square to its middle line, by bearing)
	const double Rin = R.Least() - R.Half;
	if (Rin < 0.5) return;
	NearOpts Opts;
	Opts.NoBores = true;
	// (the roads about the ring: the ring's own and those onto it, found by looking round it)
	std::vector<int> Roads;
	for (int K = 0; K < 48; ++K)
		for (const double Out2 : {-R.Half + 0.5, 0.0, R.Half - 0.5, R.Half + 2.0, R.Half + 6.0})
		{
			const P2 Look = R.At(6.283185307 * K / 48.0, Out2);
			NearHit Hit;
			if (NearestOf(Look.X, Look.Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.0 &&
				std::find(Roads.begin(), Roads.end(), Hit.Which) == Roads.end())
				Roads.push_back(Hit.Which);
		}
	// (the tarmac's surface at a point: the highest of those roads' that it is on or by, so the paint is
	// over every road's surface and the junctions' fans, a hair over theirs)
	const auto Top = [&](const P2& At)
	{
		double Best = -1e18;
		for (int W : Roads)
		{
			NearOpts Only = Opts;
			Only.OnlyRoad = W;
			NearHit Hit;
			if (!NearestOf(At.X, At.Z, Only, Hit) || Hit.Dist > Hit.Half + 1.5) continue;
			double Y = Hit.Height + Hit.Cross;
			if (W > 0) Y = JoinedHeight(W, At, Y);
			Best = std::fmax(Best, Y);
		}
		if (Best > -1e17) return Best + 0.025;
		const double Gy = Ground(At.X, At.Z);
		return (std::isnan(Gy) ? 0.0 : Gy) + 0.025;
	};
	const auto Quad = [&](const P2& A, const P2& B, const P2& C, const P2& D)
	{
		const uint32_t V = uint32_t(Out.Vertices());
		for (const P2* Q : {&A, &B, &C, &D})
			RideShapes::Vert(Out, RideShapes::Pt(Q->X, Top(*Q), Q->Z), RideShapes::Pt(0.0, 1.0, 0.0), RideShapes::Paint{Paint[0], Paint[1], Paint[2]}, (Q->X - R.Mid.X) / 4.0, (Q->Z - R.Mid.Z) / 4.0);
		const float* P = Out.Pos.data();
		const double Ny = double(P[(V + 1) * 3 + 2] - P[V * 3 + 2]) * double(P[(V + 2) * 3] - P[V * 3]) - double(P[(V + 1) * 3] - P[V * 3]) * double(P[(V + 2) * 3 + 2] - P[V * 3 + 2]);
		if (Ny >= 0.0) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
		else Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
	};
	const auto OnCircle = [&R](double Off, double Angle) { return R.At(Angle, Off); };
	// (an arc of line Width wide centred Off out from the ring's middle line, from bearing A0 to A1)
	const auto Arc = [&](double Off, double Width, double A0, double A1)
	{
		if (A1 <= A0) return;
		const int Bits = std::max(1, int(std::ceil((A1 - A0) * (R.Reach + Off) / 1.5)));
		for (int I = 0; I < Bits; ++I)
		{
			const double U0 = A0 + (A1 - A0) * I / Bits, U1 = A0 + (A1 - A0) * (I + 1) / Bits;
			Quad(OnCircle(Off - Width * 0.5, U0), OnCircle(Off - Width * 0.5, U1), OnCircle(Off + Width * 0.5, U1), OnCircle(Off + Width * 0.5, U0));
		}
	};
	const std::vector<RingArm> Arms = RingArms(R);
	// The island's edge line, solid.
	if (Rin > 2.5) Arc(-R.Half + 0.35, 0.1, 0.0, 6.283185307);
	// The lane line round the middle of a ring two lanes wide (4 m marks, 2 m gaps, on its own radius).
	if (R.Half >= 3.4 && R.Radius >= 14.0)
	{
		// (by the length along the line, its bearing changing faster where it runs nearer the middle)
		const int Fine = std::max(64, int(std::ceil(6.283185307 * R.Reach / 0.5)));
		double Run = 0.0;
		P2 Prev = R.At(0.0, 0.0);
		for (int I = 0; I < Fine; ++I)
		{
			const double U0 = 6.283185307 * I / Fine, U1 = 6.283185307 * (I + 1) / Fine;
			const P2 Next = R.At(U1, 0.0);
			const double Len = Next.DistanceTo(Prev);
			if (std::fmod(Run + 0.5 * Len, 6.0) < 4.0) Arc(0.0, 0.1, U0, U1);
			Run += Len;
			Prev = Next;
		}
	}
	// The outer edge line, broken at each road onto the ring (its width and its flared corners either side).
	if (R.Half >= 2.4)
	{
		std::vector<std::pair<double, double>> Gaps;
		for (const RingArm& A : Arms)
		{
			const double Wide = (A.Half + 2.0) / (R.RAt(A.Angle) + R.Half);
			Gaps.push_back({A.Angle - Wide, A.Angle + Wide});
		}
		const int Bits = std::max(16, int(std::ceil(6.283185307 * (R.Reach + R.Half) / 1.5)));
		for (int I = 0; I < Bits; ++I)
		{
			const double U0 = 6.283185307 * I / Bits, U1 = 6.283185307 * (I + 1) / Bits, Mid2 = 0.5 * (U0 + U1);
			bool InGap = false;
			for (const auto& Gp : Gaps)
			{
				double D = std::remainder(Mid2 - 0.5 * (Gp.first + Gp.second), 6.283185307);
				InGap = InGap || std::fabs(D) < 0.5 * (Gp.second - Gp.first);
			}
			if (!InGap) Arc(R.Half - 0.35, 0.1, U0, U1);
		}
	}
	// Each road's way on: its give-way line, and on a two-way road its splitter.
	const double KeepSide = KeepLeft() ? 1.0 : -1.0;
	for (const RingArm& A : Arms)
	{
		if (A.Way > 0) continue;   // (a way off only)
		// (driving in, its left: the way in, turned to its left as the plan's left is)
		const P2 In(-A.Out.X, -A.Out.Z), Left(In.Z, -In.X);
		const P2 Entry = Left * KeepSide;   // (the side its traffic comes in on)
		double Split = 0.0;
		const double Here = R.RAt(A.Angle);
		if (A.Way == 0 && A.Half >= 2.4 && Here >= 9.0)
		{
			// The splitter: from just outside the give-way line, a rounded nose then tapering away along
			// the road's middle (its middle as the map draws it, out from the ring).
			Split = std::fmin(1.1, 0.3 * A.Half);
			const double Long = std::clamp(0.7 * Here, 8.0, 18.0), Nose = 1.5, S0 = 0.6;
			const auto Wide = [&](double S)
			{
				const double U = S - S0;
				if (U <= 0.0) return 0.0;
				if (U < Nose) return Split * std::sqrt(U / Nose);
				return Split * std::fmax(0.0, 1.0 - (U - Nose) / (Long - Nose));
			};
			// (a point along its middle S out from the ring's edge, and the square to it there)
			const auto Along = [&A](double S, P2& Across)
			{
				double Run = 0.0;
				for (size_t I = 1; I < A.Line.size(); ++I)
				{
					const double L = A.Line[I].DistanceTo(A.Line[I - 1]);
					if (L < 1e-9) continue;
					const P2 D = (A.Line[I] - A.Line[I - 1]) * (1.0 / L);
					Across = P2(D.Z, -D.X);
					if (Run + L >= S || I + 1 == A.Line.size()) return A.Line[I - 1] + D * (S - Run);
					Run += L;
				}
				Across = P2(A.Out.Z, -A.Out.X);
				return A.At + A.Out * S;
			};
			const double Step = 0.75;
			for (double S = S0; S < S0 + Long - 1e-6; S += Step)
			{
				P2 C0, C1;
				const P2 M0 = Along(S, C0), M1 = Along(S + Step, C1);
				const double W0 = Wide(S), W1 = Wide(S + Step);
				for (double Sd : {1.0, -1.0})
					Quad(M0 + C0 * (Sd * W0), M1 + C1 * (Sd * W1), M1 + C1 * (Sd * (W1 - 0.1)), M0 + C0 * (Sd * (W0 - 0.1)));
			}
			// (its hatching: a stripe across every 1.5 m, slanting)
			for (double S = S0 + 0.9; S + 1.0 < S0 + Long - 1.0; S += 1.5)
			{
				P2 C0, C1;
				const P2 M0 = Along(S, C0), M1 = Along(S + 0.9, C1);
				const double W0 = Wide(S) - 0.1, W1 = Wide(S + 0.9) - 0.1;
				if (W0 < 0.15 || W1 < 0.15) continue;
				P2 C2, C3;
				const P2 M2 = Along(S + 0.15, C2), M3 = Along(S + 1.05, C3);
				Quad(M0 + C0 * W0, M2 + C2 * W0, M3 + C3 * -W1, M1 + C1 * -W1);
			}
		}
		// The give-way line along the ring's edge, across the way in: on a two-way road its entry half
		// (from the splitter, or the middle, to its kerb side), on a one-way road all of it.
		const double From = A.Way == 0 ? Split + 0.15 : -A.Half + 0.15, To = A.Half - 0.15;
		for (double U = From; U + 0.6 <= To + 1e-9; U += 0.9)
		{
			// (each mark: its two ends across the road, carried onto the circle 0.2 m deep outside the edge)
			const P2 E0 = A.At + Entry * U, E1 = A.At + Entry * (U + 0.6);
			const double B0 = std::atan2(E0.Z - R.Mid.Z, E0.X - R.Mid.X), B1 = std::atan2(E1.Z - R.Mid.Z, E1.X - R.Mid.X);
			Quad(OnCircle(R.Half + 0.05, B0), OnCircle(R.Half + 0.05, B1), OnCircle(R.Half + 0.25, B1), OnCircle(R.Half + 0.25, B0));
		}
	}
}

std::vector<RidePlan::RingArm> RidePlan::RingArms(const RoadRing& R) const
{
	// The roads onto the ring: where each meets its outer edge, which way it heads out, how wide, and
	// which way its traffic may go (+1 out only, -1 in only, 0 both).
	const Region& G = Area.Roads;
	std::vector<RingArm> Arms;
	for (int Nd : R.Nodes)
	{
		if (size_t(Nd) >= G.Adjacent.size()) continue;
		for (int Ae : G.Adjacent[size_t(Nd)])
		{
			if (RingOfEdge.count(Ae) != 0) continue;
			const RoadEdge& E = G.Edges[size_t(Ae)];
			if (E.Line.size() < 2) continue;
			const std::string& Class = G.ClassOf(Ae);
			if (Class == "track" || Class == "path" || Class == "footway" || Class == "cycleway" || Class == "bridleway" || Class == "steps") continue;
			std::vector<P2> Line = E.Line;
			if (E.A != Nd) std::reverse(Line.begin(), Line.end());
			// (from where it crosses the ring's outer edge)
			std::vector<P2> From;
			for (size_t I = 1; I < Line.size(); ++I)
			{
				const double D0 = R.OffOf(Line[I - 1]), D1 = R.OffOf(Line[I]);
				if (From.empty() && D0 <= R.Half && D1 > R.Half)
					From.push_back(Line[I - 1] + (Line[I] - Line[I - 1]) * ((R.Half - D0) / std::fmax(1e-6, D1 - D0)));
				if (!From.empty()) From.push_back(Line[I]);
			}
			if (From.size() < 2) continue;
			RingArm A;
			A.Edge = Ae;
			A.Node = Nd;
			A.At = From.front();
			const P2 D = From[1] - From[0];
			if (D.Length() < 1e-6) continue;
			A.Out = D * (1.0 / D.Length());
			A.Angle = std::atan2(A.At.Z - R.Mid.Z, A.At.X - R.Mid.X);
			A.Half = RoadX::Of(G, Ae, RoadLine::WidthOf(Class)).Half;
			const RoadExtra* X = G.ExtraOf(Ae);
			const bool OneWay = E.Oneway != 0 || (X != nullptr && X->Has(RoadExtra::Oneway));
			if (OneWay)
			{
				// (the edge's own way, A to B, unless mapped against: out of the ring if it runs from this node)
				const bool Forward = E.Oneway >= 0;
				A.Way = (Forward == (E.A == Nd)) ? 1 : -1;
			}
			A.Line = From;
			if (A.Half >= 1.4) Arms.push_back(A);
		}
	}
	return Arms;
}

// [roundabouts round 2] A ring's own furniture (beenster: "Some roundabouts have chevron tiling and
// boards"; "make sure that bigger roundabouts get signage"). On an island 4 m or more in from its kerb,
// a chevron board facing each way on, 1.6 m in from the kerb: black, white chevrons pointing the way the
// traffic goes round (left in Britain), wider on a wider road. And on a ring RingSignLeast m or more
// round, a roundabout warning triangle on each way on but the ride's own road (its own warnings are
// IndexFurniture's), on the traffic's side 45 to 70 m out, where it stands clear of every road.
std::vector<RidePlan::Furnish> RidePlan::RingFurniture(int Index) const
{
	std::vector<Furnish> Out;
	if (Index < 0 || size_t(Index) >= RoadRings.size()) return Out;
	const RoadRing& R = RoadRings[size_t(Index)];
	if (!R.Circle) return Out;
	const Region& G = Area.Roads;
	const double Island = R.Least() - R.Half;
	const double KeepSide = KeepLeft() ? 1.0 : -1.0;
	NearOpts Opts;
	Opts.NoBores = true;
	const auto OffAll = [this, &Opts](const P2& P, double Spare)
	{
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	for (const RingArm& A : RingArms(R))
	{
		if (A.Way > 0) continue;   // (a way off only)
		if (Island >= 4.0)
		{
			Furnish F;
			F.Kind = Chevron;
			F.Mapped = false;
			// (in past the apron's setts and its second kerb, on an island that has them)
			const double In = 2.0 * (Island - 0.1) < RingApronUnder ? RingKerb + 0.15 + RingApron + 0.25 + 0.7 : 1.6;
			F.At = R.At(A.Angle, -R.Half - In);
			const P2 Edge = R.At(A.Angle, -R.Half), Outer = R.At(A.Angle, -R.Half + 1.0);
			const P2 D = Outer - Edge;
			F.Face = D.Length() > 1e-9 ? D * (1.0 / D.Length()) : P2(std::cos(A.Angle), std::sin(A.Angle));
			F.Size = KeepSide;   // (+1: the chevrons point to the reader's left, the way round in Britain)
			F.Big = std::clamp(A.Half * 0.55, 1.2, 2.4);
			// (on the island, a kerb up from the ring's surface there)
			NearHit Hit;
			const P2 Tar = R.At(A.Angle, -R.Half + 0.6);
			double Y = std::nan("");
			if (NearestOf(Tar.X, Tar.Z, Opts, Hit))
			{
				Y = Hit.Height + Hit.Cross;
				if (Hit.Which > 0) Y = JoinedHeight(Hit.Which, Tar, Y);
				Y += RingKerb;
			}
			const double Gy = Ground(F.At.X, F.At.Z);
			F.Height = std::isnan(Y) ? Gy : std::isnan(Gy) ? Y : std::fmax(Y, Gy);
			if (!std::isnan(F.Height)) Out.push_back(F);
		}
		if (R.Reach < RingSignLeast || A.Line.size() < 2) continue;
		const std::string& Class = G.ClassOf(A.Edge);
		if (Class == "service" || Class == "living_street") continue;
		// (along the arm out from the ring's edge)
		double Run = 0.0;
		for (size_t I = 1; I < A.Line.size(); ++I)
		{
			const double L = A.Line[I].DistanceTo(A.Line[I - 1]);
			if (L < 1e-9) { continue; }
			const P2 D = (A.Line[I] - A.Line[I - 1]) * (1.0 / L);
			bool Done = false;
			for (double S = std::fmax(45.0, Run); S <= std::fmin(70.0, Run + L) && !Done; S += 5.0)
			{
				const P2 P = A.Line[I - 1] + D * (S - Run);
				// (coming in, the traffic heads -D; its left is the plan's left of -D)
				const P2 In(-D.X, -D.Z), Left(In.Z, -In.X);
				Furnish F;
				F.Kind = WarnSign;
				F.Mapped = false;
				F.Size = SignPlate::SymRoundabout;
				F.Big = A.Half >= 3.5 ? 0.9 : 0.75;
				F.At = P + Left * (KeepSide * (A.Half + 0.7 + F.Big * 0.5));
				F.Face = D;
				F.Height = Ground(F.At.X, F.At.Z);
				NearHit Ride;
				NearOpts Main;
				Main.MainOnly = true;
				if (NearestOf(P.X, P.Z, Main, Ride) && Ride.Dist < Ride.Half + 3.0) { Done = true; break; }   // (the ride's own road: its own)
				const P2 Across(F.Face.Z, -F.Face.X);
				if (std::isnan(F.Height) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (F.Big * 0.5), 0.5) || !OffAll(F.At - Across * (F.Big * 0.5), 0.5)) continue;
				Out.push_back(F);
				Done = true;
			}
			if (Done || Run + L > 70.0) break;
			Run += L;
		}
	}
	return Out;
}

double RidePlan::RingHalfOf(int Edge, double Half) const
{
	const auto It = RingOfEdge.find(Edge);
	return It == RingOfEdge.end() ? Half : RoadRings[size_t(It->second)].Half;
}

int RidePlan::PaintedRingNear(const P2& At, double Pad) const
{
	if (RingCells.empty()) return -1;
	const auto It = RingCells.find(static_cast<long long>(std::floor(At.Z / 64.0)) * 4000000LL + static_cast<long long>(std::floor(At.X / 64.0)));
	if (It == RingCells.end()) return -1;
	for (int I : It->second)
	{
		const RoadRing& R = RoadRings[size_t(I)];
		if (At.DistanceTo(R.Mid) <= R.Reach + R.Half + Pad && R.OffOf(At) <= R.Half + Pad) return I;
	}
	return -1;
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
