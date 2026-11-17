// [OSM stage 5B] Roundabouts as rings: the region's roads the map marks as a roundabout (RoadExtra's
// Roundabout flag, from the roadx bake) joined into closed rings, each with its middle and radius, and
// whether its mapped line keeps close enough to a circle to be drawn as one (RingRoundShare of its
// radius, as the Godot game's road_net.gd decides). A ring that doesn't close, or is too big to be a
// roundabout (a one-way town loop), is left as the map draws it.
#include "WorldRide.h"
#include "WorldRoadLine.h"
#include "WorldRideRoadX.h"

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
		// (a small one, a compact roundabout, is left as the map draws it: on a tight arc held exactly, its
		// roads' shoulders gather over its island)
		R.Circle = R.Radius >= RingRoundLeast && R.Stray <= RingRoundShare * R.Radius + 1.0;
		R.Mini = R.Radius <= RingMiniRadius;
		const int Index = int(RoadRings.size());
		for (int Nd : R.Nodes) RingOfNode[Nd] = Index;
		// [roundabouts] (one width all round, the widest its edges have, and at least two lanes' worth on a
		// big ring: its edges are often mapped as different classes, and each was its own width, a step in
		// the tarmac where they met; never so wide the island goes under 40% of the ring)
		for (int Re : R.Edges) R.Half = std::fmax(R.Half, RoadX::Of(G, Re, RoadLine::WidthOf(G.ClassOf(Re))).Half);
		if (R.Radius >= 20.0) R.Half = std::fmax(R.Half, 4.0);
		R.Half = std::fmin(R.Half, std::fmax(2.5, 0.6 * R.Radius));
		if (R.Circle)
		{
			for (int Re : R.Edges) { RoundEdge[size_t(Re)] = 1; RingOfEdge[Re] = Index; }
			const double Reach = R.Radius + R.Half + 40.0;
			for (long long Rz = static_cast<long long>(std::floor((R.Mid.Z - Reach) / 64.0)); Rz <= static_cast<long long>(std::floor((R.Mid.Z + Reach) / 64.0)); ++Rz)
				for (long long Rx = static_cast<long long>(std::floor((R.Mid.X - Reach) / 64.0)); Rx <= static_cast<long long>(std::floor((R.Mid.X + Reach) / 64.0)); ++Rx)
					RingCells[Rz * 4000000LL + Rx].push_back(Index);
		}
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
		const double Most = R.Radius - R.Half - 0.1;
		if (Most >= 1.0)
		{
			const int Bearings = std::clamp(int(std::ceil(6.283185307 * Most / 1.2)), 24, 96);
			std::vector<double> Rim(static_cast<size_t>(Bearings), 0.0), RimY(static_cast<size_t>(Bearings), 0.0);
			NearOpts Opts;
			Opts.NoBores = true;
			bool Any = false;
			for (int K = 0; K < Bearings; ++K)
			{
				const double A = 6.283185307 * K / Bearings, Ca = std::cos(A), Sa = std::sin(A);
				// ([roundabouts] the ring is one width all round now: its inner edge is the rim, a true circle; the
				// roads onto it that reach in past that end under the island)
				const double Rr = Most;
				double Y = std::nan("");
				// (the road's surface at the rim)
				NearHit Edge;
				if (NearestOf(R.Mid.X + Ca * (Rr + 0.6), R.Mid.Z + Sa * (Rr + 0.6), Opts, Edge))
				{
					Y = Edge.Height + Edge.Cross;
					if (Edge.Which > 0) Y = JoinedHeight(Edge.Which, P2(R.Mid.X + Ca * (Rr + 0.6), R.Mid.Z + Sa * (Rr + 0.6)), Y);
				}
				if (std::isnan(Y)) Y = Ground(R.Mid.X + Ca * Rr, R.Mid.Z + Sa * Rr);
				Rim[size_t(K)] = std::fmax(0.5, Rr);
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
				if (2.0 * Least < RingApronUnder)
				{
					// (an overrun apron: paving a lorry's back wheels can cross, then a taller kerb)
					const double Apron = std::fmin(RingApron, std::fmax(0.0, Least - Inside - 1.2));
					if (Apron > 0.4)
					{
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
						const double A = 6.283185307 * K / Bearings;
						const double Rr = std::fmax(0.2, Rim[size_t(K)] - Bands[Bd].In);
						const double X = R.Mid.X + std::cos(A) * Rr, Z = R.Mid.Z + std::sin(A) * Rr;
						Put(X, RimY[size_t(K)] + Bands[Bd].Up, Z, Bands[Bd].Look);
					}
				}
				for (int Ring = 1; Ring <= Rounds; ++Ring)
				{
					const double Share = 1.0 - double(Ring) / Rounds;   // (1 the grass's edge, 0 the middle)
					RingStart.push_back(uint32_t(Out.Vertices()));
					Joins.push_back(1);
					for (int K = 0; K < (Ring == Rounds ? 1 : Bearings); ++K)
					{
						const double A = 6.283185307 * K / Bearings;
						const double Rr = std::fmax(0.0, Rim[size_t(K)] - Inside) * Share;
						const double X = R.Mid.X + std::cos(A) * Rr, Z = R.Mid.Z + std::sin(A) * Rr;
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
	const Region& G = Area.Roads;
	const double Rin = R.Radius - R.Half, Rout = R.Radius + R.Half;
	if (Rin < 0.5) return;
	NearOpts Opts;
	Opts.NoBores = true;
	// (the roads about the ring: the ring's own and those onto it, found by looking round it)
	std::vector<int> Roads;
	for (int K = 0; K < 48; ++K)
		for (const double Out2 : {Rin + 0.5, R.Radius, Rout - 0.5, Rout + 2.0, Rout + 6.0})
		{
			const double A = 6.283185307 * K / 48.0;
			NearHit Hit;
			if (NearestOf(R.Mid.X + Out2 * std::cos(A), R.Mid.Z + Out2 * std::sin(A), Opts, Hit) && Hit.Dist <= Hit.Half + 1.0 &&
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
	const auto OnCircle = [&R](double Radius, double Angle) { return P2(R.Mid.X + Radius * std::cos(Angle), R.Mid.Z + Radius * std::sin(Angle)); };
	// (an arc of line Width wide centred on radius Radius, from angle A0 to A1)
	const auto Arc = [&](double Radius, double Width, double A0, double A1)
	{
		if (A1 <= A0) return;
		const int Bits = std::max(1, int(std::ceil((A1 - A0) * Radius / 1.5)));
		for (int I = 0; I < Bits; ++I)
		{
			const double U0 = A0 + (A1 - A0) * I / Bits, U1 = A0 + (A1 - A0) * (I + 1) / Bits;
			Quad(OnCircle(Radius - Width * 0.5, U0), OnCircle(Radius - Width * 0.5, U1), OnCircle(Radius + Width * 0.5, U1), OnCircle(Radius + Width * 0.5, U0));
		}
	};
	// The roads onto the ring: where each meets its outer edge, which way it heads out, how wide, and
	// which way its traffic may go (+1 out only, -1 in only, 0 both).
	struct Arm
	{
		P2 At, Out;
		double Angle = 0.0, Half = 0.0;
		int Way = 0;
		std::vector<P2> Line;   // (its middle out from the ring's edge)
	};
	std::vector<Arm> Arms;
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
				const double D0 = Line[I - 1].DistanceTo(R.Mid), D1 = Line[I].DistanceTo(R.Mid);
				if (From.empty() && D0 <= Rout && D1 > Rout)
					From.push_back(Line[I - 1] + (Line[I] - Line[I - 1]) * ((Rout - D0) / std::fmax(1e-6, D1 - D0)));
				if (!From.empty()) From.push_back(Line[I]);
			}
			if (From.size() < 2) continue;
			Arm A;
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
	// The island's edge line, solid.
	if (Rin > 2.5) Arc(Rin + 0.35, 0.1, 0.0, 6.283185307);
	// The lane line round the middle of a ring two lanes wide (4 m marks, 2 m gaps, on its own radius).
	if (R.Half >= 3.4 && R.Radius >= 14.0)
	{
		const double Step = 6.0 / R.Radius;
		for (double A = 0.0; A + 4.0 / R.Radius <= 6.283185307 - 1e-6; A += Step) Arc(R.Radius, 0.1, A, A + 4.0 / R.Radius);
	}
	// The outer edge line, broken at each road onto the ring (its width and its flared corners either side).
	if (R.Half >= 2.4)
	{
		std::vector<std::pair<double, double>> Gaps;
		for (const Arm& A : Arms)
		{
			const double Wide = (A.Half + 2.0) / Rout;
			Gaps.push_back({A.Angle - Wide, A.Angle + Wide});
		}
		const int Bits = std::max(16, int(std::ceil(6.283185307 * Rout / 1.5)));
		for (int I = 0; I < Bits; ++I)
		{
			const double U0 = 6.283185307 * I / Bits, U1 = 6.283185307 * (I + 1) / Bits, Mid2 = 0.5 * (U0 + U1);
			bool InGap = false;
			for (const auto& Gp : Gaps)
			{
				double D = std::remainder(Mid2 - 0.5 * (Gp.first + Gp.second), 6.283185307);
				InGap = InGap || std::fabs(D) < 0.5 * (Gp.second - Gp.first);
			}
			if (!InGap) Arc(Rout - 0.35, 0.1, U0, U1);
		}
	}
	// Each road's way on: its give-way line, and on a two-way road its splitter.
	const double KeepSide = KeepLeft() ? 1.0 : -1.0;
	for (const Arm& A : Arms)
	{
		if (A.Way > 0) continue;   // (a way off only)
		// (driving in, its left: the way in, turned to its left as the plan's left is)
		const P2 In(-A.Out.X, -A.Out.Z), Left(In.Z, -In.X);
		const P2 Entry = Left * KeepSide;   // (the side its traffic comes in on)
		double Split = 0.0;
		if (A.Way == 0 && A.Half >= 2.4 && R.Radius >= 9.0)
		{
			// The splitter: from just outside the give-way line, a rounded nose then tapering away along
			// the road's middle (its middle as the map draws it, out from the ring).
			Split = std::fmin(1.1, 0.3 * A.Half);
			const double Long = std::clamp(0.7 * R.Radius, 8.0, 18.0), Nose = 1.5, S0 = 0.6;
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
			Quad(OnCircle(Rout + 0.05, B0), OnCircle(Rout + 0.05, B1), OnCircle(Rout + 0.25, B1), OnCircle(Rout + 0.25, B0));
		}
	}
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
		if (At.DistanceTo(R.Mid) <= R.Radius + R.Half + Pad) return I;
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
