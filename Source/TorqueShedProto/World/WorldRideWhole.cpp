// [smooth junctions stage 3] A junction built whole (beenster, Nov 24: "Can you create a plan for how to get junctions and
// roundabouts really smooth and proper?"). Until now every road was its own strip and a junction was whatever was left
// where the strips overlapped: side roads sunk 2 cm under the road they met and leaning into it, a fan of tarmac 12 mm
// over them all, settled and patched, which gave the steps, the doubled tarmac, the holes and the spikes the checker
// counts. Here, for a junction of the tidied map (IndexJunctions) that the catalogue has a design for, every road into it
// stops at a mouth line a short way back, square across it, and the junction between the mouths is one piece of tarmac
// with one surface: on each mouth line exactly the road's own surface there, and between them the roads' surfaces
// blended (each road's crown and tilt carried on in, weighted by nearness to its mouth), rounded corners between the
// roads, a shoulder round each corner. Nothing overlaps, so nothing settles or is patched.
// Behind a switch (RidePlan::WholeJunctions, -TorqueJunctions=whole) until the plan's stage 7.
#include "WorldRide.h"
#include <functional>
#include "WorldRoadLine.h"
#include "WorldRideRoadX.h"

#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace TorqueWorld
{
bool RidePlan::WholeJunctions = false;

namespace
{
// (the cloud checker's reasons, printed when asked by name: never in the game's own build)
bool WhyOn(const char* Name)
{
#if defined(_MSC_VER)
	(void)Name;
	return false;
#else
	return std::getenv(Name) != nullptr;
#endif
}
// ([smooth junctions stage 5c] junctions on the ride's road built whole: the checker can turn it off to compare)
const bool WholeRideTurns = !WhyOn("WHOLE_NORIDE");
const bool LandOld = WhyOn("WHOLE_LANDOLD");
const bool RoomOld = WhyOn("WHOLE_ROOMOLD");   // (the checker can compare the arms' room as before)
const bool BothDown = WhyOn("WHOLE_BOTHDOWN");   // (the checker can turn both down as before)
const bool CornerOld = WhyOn("WHOLE_CORNEROLD");   // (the checker can turn down for room without trying tight corners)
const bool GapOld = WhyOn("WHOLE_GAPOLD");   // (the checker can turn down a dual's gaps as before)
const bool SlipOld = WhyOn("WHOLE_SLIPOLD");   // (the checker can turn down a slip's triangle as before)
const bool DualOld = WhyOn("WHOLE_DUALOLD");
const bool RingSpanOld = WhyOn("WHOLE_RINGSPANOLD");
const bool PaintOld = WhyOn("WHOLE_PAINTOLD");
const bool RideEndOld = WhyOn("WHOLE_RIDEENDOLD");   // (the checker can turn down a junction the ride starts or ends at as before)   // (the checker can paint off the piece as before)   // (the checker can turn down a ring for any bridge within 40 m of it as before)   // (the checker can turn down every junction on a dual's half as before)
} // namespace
const bool RidePlan::WholeFirmOff = WhyOn("WHOLE_FIRMOFF");   // (the checker can compare without JoinedHeightFrom's Firm)

namespace WholeJunctionBits
{
// Twice the signed area of A, B, C (positive turning from x towards z).
double Cross(const P2& A, const P2& B, const P2& C)
{
	return (B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X);
}
// The tarmac inside a rim (a simple polygon, either way round): ear clipping, the fullest ear first so no slivers, then each
// triangle split in four Levels times, two sharing an edge sharing its points. Pt: the rim's points, then the new ones.
void FillRim(const std::vector<P2>& Rim, int Levels, std::vector<P2>& Pt, std::vector<std::array<size_t, 3>>& Ears, const std::function<double(const P2&)>* Height, double Tol)
{
	Ears.clear();
	const size_t R = Rim.size();
	double Area2 = 0.0;
	for (size_t I = 0; I < R; ++I) { const P2& A = Rim[I]; const P2& B = Rim[(I + 1) % R]; Area2 += A.X * B.Z - B.X * A.Z; }
	const double Turn = Area2 > 0.0 ? 1.0 : -1.0;
	{
		std::vector<size_t> Left(R);
		for (size_t I = 0; I < R; ++I) Left[I] = I;
		size_t Guard = 0;
		while (Left.size() > 3 && Guard++ < 4 * R * R)
		{
			// (of the ears, the fullest: its smallest angle the greatest, so no slivers; a corner on a straight run
			// (a mouth's points) is no ear)
			size_t BestK = Left.size();
			double BestQ = -1.0;
			for (size_t K = 0; K < Left.size(); ++K)
			{
				const size_t A = Left[(K + Left.size() - 1) % Left.size()], B = Left[K], C = Left[(K + 1) % Left.size()];
				const double Ab = Rim[A].DistanceTo(Rim[B]), Bc = Rim[B].DistanceTo(Rim[C]), Ca = Rim[C].DistanceTo(Rim[A]);
				const double Cr = Cross(Rim[A], Rim[B], Rim[C]) * Turn;
				if (Cr <= 0.02 * Ab * Bc) continue;   // (reflex, or under about a degree from straight)
				bool Inside = false;
				for (size_t Q : Left)
				{
					if (Q == A || Q == B || Q == C) continue;
					const double D1 = Cross(Rim[A], Rim[B], Rim[Q]) * Turn, D2 = Cross(Rim[B], Rim[C], Rim[Q]) * Turn, D3 = Cross(Rim[C], Rim[A], Rim[Q]) * Turn;
					if (D1 >= -1e-9 && D2 >= -1e-9 && D3 >= -1e-9) { Inside = true; break; }
				}
				if (Inside) continue;
				// (its smallest angle's sine: twice its area over the product of the two longest sides)
				const double Longest = std::fmax(Ab, std::fmax(Bc, Ca)), Shortest = std::fmin(Ab, std::fmin(Bc, Ca));
				const double Q = Cr / std::fmax(1e-9, Longest * (Ab + Bc + Ca - Longest - Shortest));
				if (Q > BestQ) { BestQ = Q; BestK = K; }
			}
			if (BestK == Left.size()) break;   // (a rim that crosses itself: what is left goes as a fan)
			Ears.push_back({Left[(BestK + Left.size() - 1) % Left.size()], Left[BestK], Left[(BestK + 1) % Left.size()]});
			Left.erase(Left.begin() + long(BestK));
		}
		for (size_t K = 1; K + 1 < Left.size(); ++K) Ears.push_back({Left[0], Left[K], Left[K + 1]});
	}
	// (the points: the rim's, then the splits' midpoints, each made once)
	Pt.assign(Rim.begin(), Rim.end());
	std::unordered_map<long long, size_t> Mids;
	const auto MidOf = [&Pt, &Mids](size_t A, size_t B)
	{
		const long long Key = static_cast<long long>(std::min(A, B)) * 1000003LL + static_cast<long long>(std::max(A, B));
		const auto It = Mids.find(Key);
		if (It != Mids.end()) return It->second;
		Pt.push_back((Pt[A] + Pt[B]) * 0.5);
		Mids[Key] = Pt.size() - 1;
		return Pt.size() - 1;
	};
	for (int Level = 0; Level < Levels; ++Level)
	{
		std::vector<std::array<size_t, 3>> Finer;
		for (const std::array<size_t, 3>& T : Ears)
		{
			const size_t Ab = MidOf(T[0], T[1]), Bc = MidOf(T[1], T[2]), Ca = MidOf(T[2], T[0]);
			Finer.push_back({T[0], Ab, Ca}); Finer.push_back({Ab, T[1], Bc}); Finer.push_back({Ca, Bc, T[2]}); Finer.push_back({Ab, Bc, Ca});
		}
		Ears.swap(Finer);
	}
	// ([smooth junctions stage 5c] once more where the surface bends: each edge whose middle stands off the straight line
	// between its ends by more than Tol halved (chosen by the edge, so two triangles sharing it agree), each triangle
	// cut by its halved edges; at two levels a whole piece's facets were 2-3 cm out where its arms' roads bend, a knock)
	if (Height != nullptr)
	{
		std::unordered_map<long long, size_t> Split;
		const auto Key = [](size_t A, size_t B) { return static_cast<long long>(std::min(A, B)) * 1000003LL + static_cast<long long>(std::max(A, B)); };
		std::vector<double> H(Pt.size());
		for (size_t I = 0; I < Pt.size(); ++I) H[I] = (*Height)(Pt[I]);
		const auto Bent = [&](size_t A, size_t B)
		{
			const long long K = Key(A, B);
			const auto It = Split.find(K);
			if (It != Split.end()) return It->second;
			const P2 Mid = (Pt[A] + Pt[B]) * 0.5;
			const double Hm = (*Height)(Mid);
			size_t Got = size_t(-1);
			if (std::fabs(Hm - 0.5 * (H[A] + H[B])) > Tol && Pt[A].DistanceTo(Pt[B]) > 0.3) { Pt.push_back(Mid); H.push_back(Hm); Got = Pt.size() - 1; }
			Split[K] = Got;
			return Got;
		};
		std::vector<std::array<size_t, 3>> Finer;
		Finer.reserve(Ears.size() * 2);
		for (const std::array<size_t, 3>& T : Ears)
		{
			const size_t M[3] = {Bent(T[0], T[1]), Bent(T[1], T[2]), Bent(T[2], T[0])};
			const int Count = (M[0] != size_t(-1)) + (M[1] != size_t(-1)) + (M[2] != size_t(-1));
			if (Count == 0) { Finer.push_back(T); continue; }
			if (Count == 3) { Finer.push_back({T[0], M[0], M[2]}); Finer.push_back({M[0], T[1], M[1]}); Finer.push_back({M[2], M[1], T[2]}); Finer.push_back({M[0], M[1], M[2]}); continue; }
			// (one or two halved: turned so the first halved edge is T[0]-T[1])
			int Rot = 0;
			while (M[Rot] == size_t(-1)) ++Rot;
			const size_t A = T[Rot], B = T[(Rot + 1) % 3], C = T[(Rot + 2) % 3];
			const size_t Mab = M[Rot], Mbc = M[(Rot + 1) % 3], Mca = M[(Rot + 2) % 3];
			if (Count == 1) { Finer.push_back({A, Mab, C}); Finer.push_back({Mab, B, C}); continue; }
			if (Mbc != size_t(-1)) { Finer.push_back({A, Mab, C}); Finer.push_back({Mab, B, Mbc}); Finer.push_back({Mab, Mbc, C}); }
			else { Finer.push_back({A, Mab, Mca}); Finer.push_back({Mab, B, C}); Finer.push_back({Mab, C, Mca}); }
		}
		Ears.swap(Finer);
	}
	// (slivers along a straight run of the rim, under a centimetre across: left out, no gap to see; drawn, they stood on
	// edge as spikes)
	Ears.erase(std::remove_if(Ears.begin(), Ears.end(), [&Pt](const std::array<size_t, 3>& T)
	{
		const double Longest = std::fmax(Pt[T[0]].DistanceTo(Pt[T[1]]), std::fmax(Pt[T[1]].DistanceTo(Pt[T[2]]), Pt[T[2]].DistanceTo(Pt[T[0]])));
		return std::fabs(Cross(Pt[T[0]], Pt[T[1]], Pt[T[2]])) < 0.01 * Longest * Longest;
	}), Ears.end());
}
double Ease01(double T)
{
	T = std::clamp(T, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
// The angle between two headings (radians, 0 to pi).
double Apart(const P2& A, const P2& B)
{
	return std::acos(std::clamp(A.X * B.X + A.Z * B.Z, -1.0, 1.0));
}
// Where two lines cross (each a point and a heading); false if they run (nearly) parallel.
bool Crossing(const P2& Pa, const P2& Da, const P2& Pb, const P2& Db, P2& At)
{
	const double Den = Da.X * Db.Z - Da.Z * Db.X;
	if (std::fabs(Den) < 1e-6) return false;
	const P2 D = Pb - Pa;
	const double T = (D.X * Db.Z - D.Z * Db.X) / Den;
	At = Pa + Da * T;
	return true;
}
// The shortest distance from a point to a segment.
double ToSegment(const P2& P, const P2& A, const P2& B)
{
	const P2 Ab = B - A;
	const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
	const double T = L2 > 1e-12 ? std::clamp(((P.X - A.X) * Ab.X + (P.Z - A.Z) * Ab.Z) / L2, 0.0, 1.0) : 0.0;
	return P.DistanceTo(A + Ab * T);
}
} // namespace WholeJunctionBits

void RidePlan::PlanWholeJunctions(const std::vector<int>& Path)
{
	using namespace WholeJunctionBits;
	// [smooth junctions stage 4] Each roundabout drawn as a circle: a place of its own, its edges inside, the roads onto
	// it its arms (the map's places on it stay as they are, not built whole).
	JunctionPlaces.erase(std::remove_if(JunctionPlaces.begin(), JunctionPlaces.end(), [](const JunctionPlace& Jp) { return Jp.Made; }), JunctionPlaces.end());
	WholeRingOf.clear();
	RideHides.clear();
	std::vector<char> OnRide(Area.Roads.Edges.size(), 0);
	for (const RideStep& St : MadeSteps)
		if (St.Edge >= 0 && size_t(St.Edge) < OnRide.size()) OnRide[size_t(St.Edge)] = 1;
	for (int E : Path)   // ([smooth junctions 4b] every edge the ride's road is laid along: none of them a side road)
		if (E >= 0 && size_t(E) < OnRide.size()) OnRide[size_t(E)] = 1;
	if (WholeJunctions)
		for (size_t Ri = 0; Ri < RoadRings.size(); ++Ri)
		{
			const RoadRing& Rr = RoadRings[Ri];
			if (!Rr.Circle || Rr.Mini || Rr.Half <= 0.0 || Rr.Edges.empty()) continue;
			JunctionPlace Jp;
			Jp.Made = true;
			Jp.Kind = EJunction::Ring;
			Jp.Ring = int(Ri);
			Jp.At = Rr.Mid;
			Jp.Nodes = Rr.Nodes;
			// ([smooth junctions 4b] the ride's road round it is its own: not inside, never an arm)
			for (int E : Rr.Edges)
				if (!OnRide[size_t(E)]) Jp.Inside.push_back(E);
			const Region& Rg = Area.Roads;
			const std::vector<RingArm> Ras = RingArms(Rr);
			// (each road off the ring followed through the map's plain points, where only it goes on, until it has some
			// length: the short pieces before are inside the ring, the last its arm)
			const size_t Na = Ras.size();
			std::vector<std::vector<int>> Chain(Na);
			std::vector<int> From(Na), Far(Na);
			std::vector<double> Long(Na, 0.0);
			for (size_t K = 0; K < Na; ++K)
			{
				int Ed = Ras[K].Edge, Nd = Ras[K].Node;
				for (;;)
				{
					const RoadEdge& E = Rg.Edges[size_t(Ed)];
					const int Fn = E.A == Nd ? E.B : E.A;
					Chain[K].push_back(Ed);
					Long[K] += E.Length;
					From[K] = Nd;
					Far[K] = Fn;
					if (Long[K] >= WholeRingArmLeast || size_t(Fn) >= Rg.Adjacent.size() || Rg.Adjacent[size_t(Fn)].size() != 2 || Chain[K].size() > 6) break;
					const int Next = Rg.Adjacent[size_t(Fn)][0] == Ed ? Rg.Adjacent[size_t(Fn)][1] : Rg.Adjacent[size_t(Fn)][0];
					if (Next == Ed || RingOfEdge.count(Next) != 0 || Rg.Edges[size_t(Next)].A == Rg.Edges[size_t(Next)].B) break;
					Ed = Next;
					Nd = Fn;
				}
			}
			if (WhyOn("WHOLE_RING_WHY"))
				for (size_t K = 0; K < Na; ++K)
					std::printf("ring %zu arm edge %d chain %zu len %.1f way %d far %d deg %zu\n", Ri, Ras[K].Edge, Chain[K].size(), Long[K], Ras[K].Way, Far[K], Rg.Adjacent[size_t(Far[K])].size());
			const auto ArmFrom = [&Rg](int Ed, int Nd, JunctionArm& Ja)
			{
				const std::vector<P2> Line = Rg.LineFrom(Ed, Nd);
				if (Line.size() < 2) return false;
				P2 Ahead = Line.back();
				double Run = 0.0;
				for (size_t I = 1; I < Line.size(); ++I) { Run += Line[I].DistanceTo(Line[I - 1]); if (Run >= 15.0) { Ahead = Line[I]; break; } }
				const P2 D = Ahead - Line.front();
				if (D.Length() < 1e-6) return false;
				Ja.Edge = Ed;
				Ja.Node = Nd;
				Ja.Away = D * (1.0 / D.Length());
				return true;
			};
			// (an entry and an exit that meet again a little way out, where their road divides for the ring: both inside
			// it, that road from there its arm)
			std::unordered_map<int, std::vector<size_t>> ByFar;
			for (size_t K = 0; K < Na; ++K) ByFar[Far[K]].push_back(K);
			std::vector<char> Taken(Na, 0);
			const auto Ridden = [&OnRide](const std::vector<int>& Ch)
			{
				for (int E : Ch)
					if (OnRide[size_t(E)]) return true;
				return false;
			};
			for (size_t K = 0; K < Na; ++K) Taken[K] = Ridden(Chain[K]) ? 1 : 0;   // [smooth junctions 4b] (the ride's road in or out)
			for (const auto& Kv : ByFar)
			{
				if (Kv.second.size() != 2 || size_t(Kv.first) >= Rg.Adjacent.size() || Rg.Adjacent[size_t(Kv.first)].size() != 3) continue;
				const size_t Ka = Kv.second[0], Kb = Kv.second[1];
				// ([4b] the ride's road divided for the ring, its other half beside it: inside, the ride's mouth out past where they meet)
				if (Taken[Ka] != Taken[Kb] && Long[Ka] <= WholeRingPairLong && Long[Kb] <= WholeRingPairLong)
				{
					const size_t Kp = Taken[Ka] ? Kb : Ka;
					Jp.Inside.insert(Jp.Inside.end(), Chain[Kp].begin(), Chain[Kp].end());
					Jp.RidePartners.push_back({Ras[Kp].Edge, Ras[Kp].Node, Kv.first});
					Taken[Kp] = 1;
					continue;
				}
				if (Taken[Ka] || Taken[Kb]) continue;
				if (Long[Ka] > WholeRingPairLong || Long[Kb] > WholeRingPairLong) continue;
				int Onward = -1;
				for (int Ae : Rg.Adjacent[size_t(Kv.first)])
					if (Ae != Chain[Ka].back() && Ae != Chain[Kb].back()) Onward = Ae;
				if (Onward < 0 || OnRide[size_t(Onward)] || RingOfEdge.count(Onward) != 0 || Rg.Edges[size_t(Onward)].A == Rg.Edges[size_t(Onward)].B) continue;
				JunctionArm Ja;
				if (!ArmFrom(Onward, Kv.first, Ja)) continue;
				Jp.Arms.push_back(Ja);
				for (const size_t K : {Ka, Kb}) Jp.Inside.insert(Jp.Inside.end(), Chain[K].begin(), Chain[K].end());
				Taken[Ka] = Taken[Kb] = 1;
			}
			for (size_t K = 0; K < Na; ++K)
			{
				if (Taken[K]) continue;
				JunctionArm Ja;
				if (!ArmFrom(Chain[K].back(), From[K], Ja)) continue;
				Jp.Arms.push_back(Ja);
				Jp.Inside.insert(Jp.Inside.end(), Chain[K].begin(), Chain[K].end() - 1);
			}
			Jp.Roads = int(Jp.Arms.size());
			bool Ride = false;
			for (int E : Rr.Edges) Ride = Ride || OnRide[size_t(E)];
			if (!Jp.Arms.empty() || Ride) JunctionPlaces.push_back(std::move(Jp));
		}
	WholeOf.assign(JunctionPlaces.size(), WholePlan());
	WholeCells.clear();
	WholeEnds.clear();
	WholeInside.clear();
	WholeArmsOf.clear();
	WholeEndClash.clear();
	if (!WholeJunctions) return;
	const Region& G = Area.Roads;
	// [smooth junctions stage 5] (why each isn't: 1 kind, 2 dual, 3 ring's, 4 too wide, 5 arms, 6 an arm or inside on the ride,
	// a circuit, paired or a loop, 7 bridge or tunnel near, 8 by the ride's road, 9 an arm twice, 10 an arm too short, 11 a road
	// between two too short)
	WholeWhy.assign(JunctionPlaces.size(), 0);
	std::unordered_set<int> RingRoads;   // [stage 5] (a ring's arms and insides: never followed into by another's arm)
	for (const JunctionPlace& Jp : JunctionPlaces)
		if (Jp.Made)
		{
			for (const JunctionArm& A : Jp.Arms) RingRoads.insert(A.Edge);
			RingRoads.insert(Jp.Inside.begin(), Jp.Inside.end());
		}
	for (size_t Ji = 0; Ji < JunctionPlaces.size(); ++Ji)
	{
		const JunctionPlace& J = JunctionPlaces[Ji];
		const auto Refuse = [this, Ji](unsigned char Code) { WholeWhy[Ji] = Code; };
		if (J.Made)
		{
			PlanWholeRing(Ji, OnRide);
			continue;
		}
		// (the catalogue's designs so far: a T, crossroads, fork, a staggered pair or five ways, on one place of the
		// map or a cluster no wider than JunctionMerge; not a ring, a slip, a dual's gap, a circuit's or the ride's
		// own road (stage 3b), and no bridge or tunnel within 40 m)
		// ([smooth junctions stage 5c] and a dual's gap: a road across the reservation between its halves, their four arms
		// side by side in pairs, the reservation's nose where the pair's mouths are; its cluster as wide as JunctionGap)
		// (or where a dual begins: one road splitting into its two halves, a "bend" of two roads, the pair as one)
		int Pairs = 0;
		for (const JunctionArm& A : J.Arms) Pairs += A.Pair >= 0 ? 1 : 0;
		const bool Gap = !GapOld && (J.Kind == EJunction::DualGap || (J.Kind == EJunction::Bend && J.Dual && J.Arms.size() == 3 && Pairs == 2));
		// ([smooth junctions stage 5c] and a slip's triangle at a T: the links round its little island one piece with the
		// roads, the island tarmac (stage 6 paints it); its links' own pieces overlapped 10-20 cm apart, Penrith)
		const bool Slip = !SlipOld && J.Kind == EJunction::Slip;
		const bool Kind = J.Kind == EJunction::T || J.Kind == EJunction::Cross || J.Kind == EJunction::Fork || J.Kind == EJunction::Stagger || J.Kind == EJunction::Multi || Gap || Slip;
		if (!Kind) { Refuse(1); continue; }
		// ([smooth junctions stage 5c] one half of a dual met there, its other half not: a T or crossroads of one-way arms,
		// built whole as any other; both halves there (an arm with a Pair) is still turned down below, code 6)
		if (J.Dual && DualOld) { Refuse(2); continue; }
		if (J.Ring >= 0) { Refuse(3); continue; }
		if (J.Span > (Gap ? JunctionGap : Slip ? JunctionSlip : JunctionMerge)) { Refuse(4); continue; }
		if (J.Arms.size() < 3 || J.Arms.size() > (Gap ? 6u : 5u)) { Refuse(5); continue; }
		bool Fits = true;
		// ([smooth junctions stage 5c] the ride's road through it or turning at it, two of its arms: built whole too, the ride's
		// road not drawn between their mouth lines, as round a ring (4b); its old mouths and square turns knocked 2-9 cm)
		int RideArms = 0;
		for (const JunctionArm& A : J.Arms) RideArms += OnRide[size_t(A.Edge)] ? 1 : 0;
		// ([smooth junctions stage 5c] or the ride starting or ending at it, one arm on the ride's road: Hartsop, where a ride
		// from the teleport begins, its old mouths torn)
		const bool RideEnd = !RideEndOld && RideArms == 1 && J.Arms.size() >= 3 && (J.At.DistanceTo(Road.front().At) < 8.0 || J.At.DistanceTo(Road.back().At) < 8.0);
		const bool RideMode = WholeRideTurns && (RideArms == 2 || RideEnd) && J.Arms.size() >= 3;
		for (const JunctionArm& A : J.Arms)
			Fits = Fits && (RideMode || !OnRide[size_t(A.Edge)]) && !G.CircuitEdge(A.Edge) && (Gap || A.Pair < 0) && G.Edges[size_t(A.Edge)].A != G.Edges[size_t(A.Edge)].B;
		for (int E : J.Inside) Fits = Fits && !OnRide[size_t(E)] && !G.CircuitEdge(E);
		if (!Fits) { Refuse(6); continue; }
		for (const Span& Sp : G.Spans)
			if (Fits && WholeJunctionBits::ToSegment(J.At, Sp.A, Sp.B) < 40.0) Fits = false;
		if (!Fits) { Refuse(7); continue; }
		// (none within WholeRideClear of the ride's road, whose own junctions are its mouths (stage 3b): 30 m left a lane's
		// junction 30 m up from the ride's road on the old fan, torn down a 27% arm, Hartsop)
		{
			double D = 0.0, H = 0.0;
			if (!RideMode && Nearest(J.At.X, J.At.Z, D, H, nullptr, nullptr, nullptr, true) && D < WholeRideClear) { Refuse(8); continue; }
		}
		// [smooth junctions stage 5] (an arm along a short edge to a plain point of the map, where only it goes on (a bend,
		// a change of name or limit): followed on to a later edge, the short ones before inside; most arms too short for
		// their mouth were these, York, Kendal)
		std::vector<JunctionArm> Arms = J.Arms;
		std::vector<int> Extra;
		// (a road with both ends within 15 m of the ride's road is laid with both on to it (LaySide's Meets): a short one
		// to nothing, so never an arm, York)
		const auto Eaten = [&](int Ed)
		{
			int Near = 0;
			for (const int Nd : {G.Edges[size_t(Ed)].A, G.Edges[size_t(Ed)].B})
			{
				double D = 0.0, H = 0.0;
				Near += Nearest(G.Nodes[size_t(Nd)].X, G.Nodes[size_t(Nd)].Z, D, H, nullptr, nullptr, nullptr, true) && D <= 15.0 ? 1 : 0;
			}
			return Near == 2;
		};
		for (JunctionArm& Arm : Arms)
		{
			if (OnRide[size_t(Arm.Edge)]) continue;   // (the ride's road: its own)
			double Run = 0.0;
			for (int Step = 0; Step < 4; ++Step)
			{
				const RoadEdge& Ed = G.Edges[size_t(Arm.Edge)];
				if (Run + Ed.Length >= WholeArmFollow) break;
				const int Fn = Ed.A == Arm.Node ? Ed.B : Ed.A;
				if (size_t(Fn) >= G.Adjacent.size() || G.Adjacent[size_t(Fn)].size() != 2) break;
				const int Next = G.Adjacent[size_t(Fn)][0] == Arm.Edge ? G.Adjacent[size_t(Fn)][1] : G.Adjacent[size_t(Fn)][0];
				if (Next == Arm.Edge || OnRide[size_t(Next)] || RingRoads.count(Next) != 0 || Eaten(Next) || G.CircuitEdge(Next) || RingOfEdge.count(Next) != 0 || G.Edges[size_t(Next)].A == G.Edges[size_t(Next)].B) break;
				bool Used = std::find(J.Inside.begin(), J.Inside.end(), Next) != J.Inside.end() || std::find(Extra.begin(), Extra.end(), Next) != Extra.end();
				for (const JunctionArm& Other : Arms) Used = Used || Other.Edge == Next;
				if (Used) break;
				Run += Ed.Length;
				Extra.push_back(Arm.Edge);
				Arm.Edge = Next;
				Arm.Node = Fn;
			}
		}
		{
			bool Any = false;
			for (const JunctionArm& Arm : Arms) Any = Any || (!OnRide[size_t(Arm.Edge)] && Eaten(Arm.Edge));
			if (Any) { Refuse(13); continue; }
		}
		// (an arm's own end twice at one junction, a loop: not yet)
		bool Twice = false;
		for (size_t A = 0; A < Arms.size(); ++A)
			for (size_t B = A + 1; B < Arms.size(); ++B) Twice = Twice || Arms[A].Edge == Arms[B].Edge;
		if (Twice) { Refuse(9); continue; }
		// (each arm's half width, and the corners' radius: 10 m where a greater road meets, 6 m among lesser ones)
		std::vector<double> Half(Arms.size());
		bool Greater = false;
		for (size_t A = 0; A < Arms.size(); ++A)
		{
			const std::string& Class = G.ClassOf(Arms[A].Edge);
			Half[A] = RingHalfOf(Arms[A].Edge, RoadX::Of(G, Arms[A].Edge, RoadLine::WidthOf(Class)).Half);
			Greater = Greater || Class == "primary" || Class == "trunk" || Class == "secondary";
		}
		WholePlan W;
		W.Corner = Greater ? WholeCornerGreat : WholeCornerLeast;
		// (each arm's mouth: far enough out that its tarmac is clear of every other arm's that isn't across from it,
		// with room for the corner)
		static const bool StubCut = !WhyOn("WHOLE_NOSTUB");   // (the checker can compare without the stub cut)
		// ([smooth junctions stage 5c] where an arm hasn't the room before the next junction, tried again with tight corners
		// (WholeCornerTight): most arms turned down for room wanted under 2 m more than they had)
		for (int Try = 0; Try < (CornerOld ? 1 : 2); ++Try)
		{
		if (Try == 1)
		{
			if (Fits) break;
			Fits = true;
			W.Corner = std::fmin(W.Corner, WholeCornerTight);
		}
		W.Cut.assign(Arms.size(), 0.0);
		for (size_t A = 0; A < Arms.size(); ++A)
		{
			double Need = Half[A] + 1.5;
			for (size_t B = 0; B < Arms.size(); ++B)
			{
				if (A == B || (Gap && Arms[A].Pair == int(B))) continue;   // (its pair: the reservation between)
				const double Th = Apart(Arms[A].Away, Arms[B].Away);
				if (Th > WholeAcross) continue;
				Need = std::fmax(Need, (Half[B] + Half[A] * std::fabs(std::cos(Th)) + W.Corner * 0.8) / std::fmax(0.25, std::sin(Th)));
			}
			// (measured from the junction's middle; an arm from a cluster's other point less what it already has)
			Need -= std::fmax(0.0, Arms[A].Away.X * (G.Nodes[size_t(Arms[A].Node)].X - J.At.X) + Arms[A].Away.Z * (G.Nodes[size_t(Arms[A].Node)].Z - J.At.Z));
			W.Cut[A] = std::clamp(Need, 2.0, WholeCutMost);
			// (or to near its end where nothing goes on from there)
			const RoadEdge& Own = G.Edges[size_t(Arms[A].Edge)];
			const int FarNode = Own.A == Arms[A].Node ? Own.B : Own.A;
			// ([smooth junctions stage 5c] to 7 m short of the junction at its far end, if that is more than half way; 10 m where
			// that is on the ride's road, whose mouths need more (9 m2 of steps between two at 7, Hawes))
			bool FarOnRide = false;
			if (size_t(FarNode) < G.Adjacent.size())
				for (int Ae : G.Adjacent[size_t(FarNode)]) FarOnRide = FarOnRide || OnRide[size_t(Ae)];
			const double Room = size_t(FarNode) < G.Adjacent.size() && G.Adjacent[size_t(FarNode)].size() == 1 ? 0.9 * Own.Length
				: std::fmax(0.45 * Own.Length, RoomOld ? 0.0 : Own.Length - (FarOnRide ? 10.0 : 7.0));
			// ([smooth junctions stage 5c] a short dead end (a drive, a yard's way in): its mouth at its end, not the whole
			// junction turned down for it; 2,859 of the refusals in a three-town sample were these)
			if (OnRide[size_t(Arms[A].Edge)]) continue;   // (along the ride's road: room checked against its ends and other hides)
			const bool DeadEnd = size_t(FarNode) < G.Adjacent.size() && G.Adjacent[size_t(FarNode)].size() == 1;
			if (W.Cut[A] > Room && DeadEnd && StubCut && Room >= Half[A] + 1.0) W.Cut[A] = Room;
			if (W.Cut[A] > Room)
			{
				Fits = false;   // (no room before the junction at its other end)
				if (WhyOn("WHOLE_WHY")) { const RoadEdge& Ed = G.Edges[size_t(Arms[A].Edge)]; const int Fn = Ed.A == Arms[A].Node ? Ed.B : Ed.A; std::printf("whole10 try %d cut %.1f len %.1f deg %zu class %s\n", Try, W.Cut[A], Ed.Length, G.Adjacent[size_t(Fn)].size(), G.ClassOf(Arms[A].Edge).c_str()); }
			}
		}
		}
		if (!Fits) { Refuse(10); continue; }
		// (the ride's road's two arms: its mouth lines that far along it either way from the junction)
		W.ArmSign.assign(Arms.size(), 0);
		W.ArmAlong.assign(Arms.size(), 0.0);
		if (RideMode)
		{
			double D = 0.0, H = 0.0;
			int Index = -1;
			if (!Nearest(J.At.X, J.At.Z, D, H, &Index, nullptr, nullptr, true) || Index < 0 || D > 8.0) { if (WhyOn("WHOLE_WHY")) std::printf("ride14 %zu node %d d %.1f\n", Ji, J.Nodes.empty() ? -1 : J.Nodes[0], D); Refuse(14); continue; }
			const double At = Road[size_t(Index)].Along;
			for (size_t A = 0; A < Arms.size() && Fits; ++A)
			{
				if (!OnRide[size_t(Arms[A].Edge)]) continue;
				// (which way along: where the arm's line is 6 m out)
				const std::vector<P2> Line = G.LineFrom(Arms[A].Edge, Arms[A].Node);
				P2 Out = Line.back();
				double Run = 0.0;
				for (size_t Q = 1; Q < Line.size(); ++Q)
				{
					const double L = Line[Q].DistanceTo(Line[Q - 1]);
					if (Run + L >= 6.0) { Out = Line[Q - 1] + (Line[Q] - Line[Q - 1]) * ((6.0 - Run) / std::fmax(L, 1e-9)); break; }
					Run += L;
				}
				int Oi = -1;
				if (!Nearest(Out.X, Out.Z, D, H, &Oi, nullptr, nullptr, true) || Oi < 0 || Oi == Index) { Fits = false; break; }
				W.ArmSign[A] = Road[size_t(Oi)].Along > At ? 1 : -1;
				W.ArmAlong[A] = At + W.ArmSign[A] * W.Cut[A];
			}
			if (Fits && W.ArmSign.size() >= 2)
			{
				int Signs = 0;
				W.Hide[0] = 1e18; W.Hide[1] = -1e18;
				for (size_t A = 0; A < Arms.size(); ++A)
					if (W.ArmSign[A] != 0) { Signs += W.ArmSign[A]; W.Hide[0] = std::fmin(W.Hide[0], W.ArmAlong[A]); W.Hide[1] = std::fmax(W.Hide[1], W.ArmAlong[A]); }
				// (one arm, the ride starting or ending here: hidden from its end to that arm's mouth line)
				if (RideEnd && Signs == 1) W.Hide[0] = Road.front().Along;
				if (RideEnd && Signs == -1) W.Hide[1] = Road.back().Along;
				Fits = RideEnd ? (Signs == 1 || Signs == -1) && W.Hide[1] > W.Hide[0] + 1.0
					: Signs == 0 && W.Hide[0] > Road.front().Along + 1.0 && W.Hide[1] < Road.back().Along - 1.0;
				for (const std::pair<double, int>& Hd : RideHides)
					Fits = Fits && !(WholeOf[size_t(Hd.second)].Hide[0] < W.Hide[1] + 1.0 && WholeOf[size_t(Hd.second)].Hide[1] > W.Hide[0] - 1.0);
			}
			if (!Fits) { if (WhyOn("WHOLE_WHY")) std::printf("ride14 %zu node %d signs/hide %.1f %.1f\n", Ji, J.Nodes.empty() ? -1 : J.Nodes[0], W.Hide[0], W.Hide[1]); Refuse(14); continue; }
		}
		// (and no arm whose other end is another whole junction close enough that the two would meet)
		JunctionPlaces[Ji].Arms = Arms;
		JunctionPlaces[Ji].Inside.insert(JunctionPlaces[Ji].Inside.end(), Extra.begin(), Extra.end());
		W.Ok = true;
		W.Owner = 1 << 30;
		for (size_t A = 0; A < Arms.size(); ++A)
		{
			if (W.ArmSign[A] != 0) continue;
			const int Which = Arms[A].Edge + 1;
			const int End = G.Edges[size_t(Arms[A].Edge)].A == Arms[A].Node ? 0 : 1;
			if (Which < W.Owner) { W.Owner = Which; W.OwnerEnd = End; }
		}
		W.Reach = 0.0;
		for (size_t A = 0; A < Arms.size(); ++A) W.Reach = std::fmax(W.Reach, W.Cut[A] + std::fmax(J.Span, G.Nodes[size_t(Arms[A].Node)].DistanceTo(J.At)) + 2.0);
		if (RideMode) { W.Owner = 0; W.OwnerEnd = 0; RideHides.emplace_back(W.Hide[0], int(Ji)); }
		if (RideMode && WhyOn("WHOLE_WHY")) for (size_t A = 0; A < Arms.size(); ++A) std::printf("ridewhole node %d arm %zu edge %d sign %d cut %.1f half %.1f\n", J.Nodes.empty() ? -1 : J.Nodes[0], A, Arms[A].Edge, W.ArmSign[A], W.Cut[A], Half[A]);
		WholeOf[Ji] = W;
		for (size_t A = 0; A < Arms.size(); ++A)
		{
			if (W.ArmSign[A] != 0) continue;
			const int Which = Arms[A].Edge + 1;
			const int End = G.Edges[size_t(Arms[A].Edge)].A == Arms[A].Node ? 0 : 1;
			{
				// ([smooth junctions stage 5c] the same road end claimed by two (a ring's arm walked on to a junction's): the
				// first turned down below; the second's claim replaced the first's, and the first, kept, lost its mouth there)
				const auto Had = WholeEnds.find(static_cast<long long>(Which) * 2 + End);
				if (Had != WholeEnds.end() && Had->second.first != int(Ji)) WholeEndClash.push_back({Had->first, Had->second.first, Had->second.second, int(Ji)});
				WholeEnds[static_cast<long long>(Which) * 2 + End] = {int(Ji), W.Cut[A]};
			}
			WholeArmsOf[Which].push_back(int(Ji));
		}
		for (int E : J.Inside) WholeInside[E + 1] = int(Ji);
	}
	// (a road both of whose ends are whole junctions: its two cuts must leave some of it between them)
	for (auto& KV : WholeEnds)
	{
		const int Which = int(KV.first / 2), End = int(KV.first % 2);
		const auto Other = WholeEnds.find(static_cast<long long>(Which) * 2 + (1 - End));
		if (Other == WholeEnds.end()) continue;
		const double Long = G.Edges[size_t(Which - 1)].Length;
		if (KV.second.second + Other->second.second > Long * 0.9)
		{
			// ([smooth junctions stage 5c] the one wanting more of it turned down, the other kept: its cut leaves 7 m for the
			// old junction there; both turned down lost 192 in the UK sample)
			if (!WholeOf[size_t(KV.second.first)].Ok || !WholeOf[size_t(Other->second.first)].Ok) continue;
			const bool Mine = KV.second.second != Other->second.second ? KV.second.second > Other->second.second : KV.second.first > Other->second.first;
			const int Lose = BothDown || Mine ? KV.second.first : Other->second.first;
			WholeOf[size_t(Lose)].Ok = false;
			WholeWhy[size_t(Lose)] = 11;
			if (BothDown) { WholeOf[size_t(Other->second.first)].Ok = false; WholeWhy[size_t(Other->second.first)] = 11; }
		}
	}
	// ([smooth junctions stage 5] a road inside one whole junction and an arm of another, or inside two (arms followed on
	// past plain points from both ends): [stage 5c] the bigger of the two turned down, the other kept (both turned down,
	// as before, under WHOLE_BOTHDOWN))
	{
		// (Ja the one it is inside; Jb where it is an arm, ArmOf: then Ja turned down, the road left to Jb's arm: kept inside
		// Ja, the road was hidden to Jb's old mouth and the map's line there under no tarmac, Fort William)
		const auto Clash = [this](int Ja, int Jb, bool ArmOf)
		{
			if (!WholeOf[size_t(Ja)].Ok || !WholeOf[size_t(Jb)].Ok) return;
			// (a ring's and a junction's: both, as before. Kept, the ring hid its entry and exit pair up to the junction's old
			// mouth, the road there under no tarmac (Fort William): keeping rings took the UK sample's holes from 430 m to 744 m,
			// and even where its arm was the road in the junction, to 489 m)
			const bool Ring = JunctionPlaces[size_t(Ja)].Made || JunctionPlaces[size_t(Jb)].Made;
			const int Lose = ArmOf ? Ja
				: WholeOf[size_t(Ja)].Reach != WholeOf[size_t(Jb)].Reach ? (WholeOf[size_t(Ja)].Reach > WholeOf[size_t(Jb)].Reach ? Ja : Jb) : std::max(Ja, Jb);
			for (int Jx : {Ja, Jb})
				if (BothDown || Ring || Jx == Lose) { WholeOf[size_t(Jx)].Ok = false; WholeWhy[size_t(Jx)] = 12; }
		};
		std::unordered_map<int, int> InsideOf;
		for (size_t Ji = 0; Ji < JunctionPlaces.size(); ++Ji)
		{
			if (!WholeOf[Ji].Ok) continue;
			for (int E : JunctionPlaces[Ji].Inside)
			{
				const auto Got = InsideOf.emplace(E + 1, int(Ji));
				if (!Got.second && Got.first->second != int(Ji)) Clash(int(Ji), Got.first->second, false);
			}
		}
		for (const auto& KV : WholeEnds)
		{
			const auto In = InsideOf.find(int(KV.first / 2));
			if (In != InsideOf.end() && In->second != KV.second.first) Clash(In->second, KV.second.first, true);
		}
	}
	// (a road end claimed by two: the first claim, replaced by the second's (a ring's, planned last), turned down; or, the
	// second turned down already, given back its claim. After the clashes above, which turn both down where a ring is one)
	for (const EndClash& Two : WholeEndClash)
		if (WholeOf[size_t(Two.First)].Ok)
		{
			if (WholeOf[size_t(Two.Second)].Ok) { WholeOf[size_t(Two.First)].Ok = false; WholeWhy[size_t(Two.First)] = 12; }
			else WholeEnds[Two.Key] = {Two.First, Two.Cut};
		}
	// (those turned down on the way: their arms back to the old way)
	for (auto It = WholeEnds.begin(); It != WholeEnds.end();)
		It = WholeOf[size_t(It->second.first)].Ok ? std::next(It) : WholeEnds.erase(It);
	for (auto It = WholeInside.begin(); It != WholeInside.end();)
		It = WholeOf[size_t(It->second)].Ok ? std::next(It) : WholeInside.erase(It);
	for (auto& KV : WholeArmsOf)
		KV.second.erase(std::remove_if(KV.second.begin(), KV.second.end(), [this](int Ji) { return !WholeOf[size_t(Ji)].Ok; }), KV.second.end());
	RideHides.erase(std::remove_if(RideHides.begin(), RideHides.end(), [this](const std::pair<double, int>& Hd) { return !WholeOf[size_t(Hd.second)].Ok; }), RideHides.end());
	std::sort(RideHides.begin(), RideHides.end());
	WholeCells.clear();
	for (size_t Ji = 0; Ji < WholeOf.size(); ++Ji)
	{
		if (!WholeOf[Ji].Ok) continue;
		const P2 C = JunctionPlaces[Ji].At;
		const double Pad = WholeOf[Ji].Reach + Bench + Blend + 4.0;
		for (long long Cx = static_cast<long long>(std::floor((C.X - Pad) / 64.0)); Cx <= static_cast<long long>(std::floor((C.X + Pad) / 64.0)); ++Cx)
			for (long long Cz = static_cast<long long>(std::floor((C.Z - Pad) / 64.0)); Cz <= static_cast<long long>(std::floor((C.Z + Pad) / 64.0)); ++Cz)
				WholeCells[Cx * 4000037LL + Cz].push_back(int(Ji));
	}
}

bool RidePlan::WholeShape::Inside(const P2& At) const
{
	bool In = false;
	for (size_t I = 0, K = Rim.size() - 1; I < Rim.size(); K = I++)
		if ((Rim[I].Z > At.Z) != (Rim[K].Z > At.Z) && At.X < (Rim[K].X - Rim[I].X) * (At.Z - Rim[I].Z) / (Rim[K].Z - Rim[I].Z) + Rim[I].X) In = !In;
	return In;
}

double RidePlan::WholeShape::HeightAt(const P2& At) const
{
	const double Fx = std::clamp((At.X - X0) / WholeShapeCell, 0.0, double(Nx - 1)), Fz = std::clamp((At.Z - Z0) / WholeShapeCell, 0.0, double(Nz - 1));
	const int Gx = std::min(Nx - 2, int(Fx)), Gz = std::min(Nz - 2, int(Fz));
	if (Gx < 0 || Gz < 0) return H.empty() ? std::nan("") : double(H.front());
	const double Ux = Fx - Gx, Uz = Fz - Gz;
	const auto Q = [this](int X, int Z) { return double(H[size_t(Z) * size_t(Nx) + size_t(X)]); };
	return (1 - Uz) * ((1 - Ux) * Q(Gx, Gz) + Ux * Q(Gx + 1, Gz)) + Uz * ((1 - Ux) * Q(Gx, Gz + 1) + Ux * Q(Gx + 1, Gz + 1));
}

double RidePlan::WholeGround(double X, double Z, double Out) const
{
	// (the ground under a whole junction's tarmac as under a road's: Sink under its surface, so a corner's curve,
	// far from every road's own middle, keeps clear of a field rising beside it)
	if (!WholeJunctions || WholeCells.empty() || std::isnan(Out)) return Out;
	const auto It = WholeCells.find(static_cast<long long>(std::floor(X / 64.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 64.0)));
	if (It == WholeCells.end()) return Out;
	const P2 At(X, Z);
	for (int Ji : It->second)
	{
		if (At.DistanceTo(JunctionPlaces[size_t(Ji)].At) > WholeOf[size_t(Ji)].Reach + Bench + Blend + 4.0) continue;
		const std::shared_ptr<const WholeShape> Sh = WholeShapeOf(Ji);
		if (Sh == nullptr) continue;
		if (Sh->Inside(At))
		{
			double Y = Sh->HeightAt(At);
			// ([smooth junctions stage 5c] the land runs straight between its points 4 m apart: under the lowest of the tarmac
			// round this one, not just its own, or it stood through a dip between them)
			if (!LandOld)
				for (const P2& Off : {P2(2.0, 0.0), P2(-2.0, 0.0), P2(0.0, 2.0), P2(0.0, -2.0)})
					if (Sh->Inside(At + Off)) { const double Yo = Sh->HeightAt(At + Off); if (!std::isnan(Yo)) Y = std::fmin(Y, Yo); }
			if (!std::isnan(Y)) Out = std::fmin(Out, Y - Sink);
			continue;
		}
		// (and a road's bench round it: level with its edge, just under the shoulder, for Bench; then back to the land
		// over Blend. The land is drawn on a 4 m grid: kept down only under the tarmac, it rose through the rim between)
		double Best = 1e18;
		P2 Near;
		for (size_t I = 0, K = Sh->Rim.size() - 1; I < Sh->Rim.size(); K = I++)
		{
			const P2 A = Sh->Rim[K], D = Sh->Rim[I] - A;
			const double L2 = D.X * D.X + D.Z * D.Z;
			const double T = L2 > 1e-12 ? std::clamp(((At.X - A.X) * D.X + (At.Z - A.Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
			const P2 Q = A + D * T;
			const double Dq = At.DistanceTo(Q);
			if (Dq < Best) { Best = Dq; Near = Q; }
		}
		if (Best >= Bench + Blend) continue;
		const double Y = Sh->HeightAt(Near);
		if (std::isnan(Y)) continue;
		const double Edge = Y - (Sink + (FlushSink - Sink) * std::clamp(Best / 0.6, 0.0, 1.0));
		if (Best <= Bench) Out = std::fmin(Out, Edge);
		else if (Edge < Out) Out = Edge + (Out - Edge) * WholeJunctionBits::Ease01((Best - Bench) / Blend);
	}
	return Out;
}

double RidePlan::WholeCut(int Which, int End) const
{
	if (!WholeJunctions) return 0.0;
	const auto It = WholeEnds.find(static_cast<long long>(Which) * 2 + End);
	return It == WholeEnds.end() ? 0.0 : It->second.second;
}

bool RidePlan::WholeHidden(int Which) const
{
	return WholeJunctions && WholeInside.count(Which) > 0;
}

bool RidePlan::WholeNear(int Which, const P2& At) const
{
	if (!WholeJunctions) return false;
	const auto It = WholeArmsOf.find(Which);
	if (It == WholeArmsOf.end()) return false;
	for (int Ji : It->second)
		if (At.DistanceTo(JunctionPlaces[size_t(Ji)].At) < WholeOf[size_t(Ji)].Reach) return true;
	return false;
}

int RidePlan::WholeAt(int Which, int End) const
{
	if (!WholeJunctions) return -1;
	const auto It = WholeEnds.find(static_cast<long long>(Which) * 2 + End);
	return It == WholeEnds.end() ? -1 : It->second.first;
}

bool RidePlan::CutRow(const std::vector<RoadPoint>& Pts, double Along, RoadPoint& P, P2& Dir, double& Rise)
{
	// (the point Along metres along a laid road, its heading and rise there, straight between its points)
	if (Pts.size() < 2) return false;
	size_t K = 0;
	while (K + 2 < Pts.size() && Pts[K + 1].Along < Along) ++K;
	const RoadPoint& A = Pts[K];
	const RoadPoint& B = Pts[K + 1];
	const double Seg = std::fmax(1e-6, B.Along - A.Along);
	const double T = std::clamp((Along - A.Along) / Seg, 0.0, 1.0);
	P = A;
	P.At = A.At + (B.At - A.At) * T;
	P.Along = A.Along + (B.Along - A.Along) * T;
	P.Height = A.Height + (B.Height - A.Height) * T;
	P.Half = A.Half + (B.Half - A.Half) * T;
	P.Bank = A.Bank + (B.Bank - A.Bank) * T;
	const P2 D = B.At - A.At;
	const double L = D.Length();
	Dir = L > 1e-9 ? D * (1.0 / L) : P2(0.0, -1.0);
	Rise = L > 1e-9 ? (B.Height - A.Height) / L : 0.0;
	return true;
}

void RidePlan::WholePiece(int Ji, RideMesh& Out, WholeShape* Shape) const
{
	using namespace WholeJunctionBits;
	if (Ji < 0 || size_t(Ji) >= JunctionPlaces.size() || !WholeOf[size_t(Ji)].Ok) return;
	if (JunctionPlaces[size_t(Ji)].Made) { RingPiece(Ji, Out, Shape); return; }   // [smooth junctions stage 4]
	const JunctionPlace& J = JunctionPlaces[size_t(Ji)];
	const WholePlan& W = WholeOf[size_t(Ji)];
	const Region& G = Area.Roads;
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, Grit[3] = {0.095f, 0.088f, 0.07f}, MeetGrass[3] = {0.10f, 0.16f, 0.03f};
	{
		// ([smooth junctions stage 5] its arms laid first: one followed on past plain points can start out of the blocks laid
		// for the piece asking, York)
		Rect Around;
		const double Pad = W.Reach + 8.0;
		Around.X0 = J.At.X - Pad; Around.Z0 = J.At.Z - Pad; Around.X1 = J.At.X + Pad; Around.Z1 = J.At.Z + Pad;
		LayBlocks(Around);
	}
	// Each arm's mouth: the road's own point Cut metres in from its end, heading out, its edges either side.
	struct MouthLine
	{
		int Which = 0;
		RoadPoint P;
		P2 Out, Left;      // heading out along the road; its left as it heads out
		P2 RoadLeft;       // the road's own left, as its points run (RoadPiece's offsets)
		double Rise = 0.0; // rise per metre heading out
		bool Paved = true;
		const std::vector<RoadPoint>* Pts = nullptr;
		int End = 0;
		double Cut = 0.0;
		double Base = 0.0;       // its mouth line's along its road
		int Edge = -1;           // its edge of the map's
		std::vector<P2> Inner;   // its middle line from the mouth in to its end
	};
	std::vector<MouthLine> Mouths;
	bool AnyPaved = false;
	for (size_t A = 0; A < J.Arms.size(); ++A)
	{
		// ([5c] an arm along the ride's road: its own points, the mouth line that far along it from the junction)
		const int Sign = A < W.ArmSign.size() ? W.ArmSign[A] : 0;
		const int Which = Sign != 0 ? 0 : J.Arms[A].Edge + 1;
		const int End = Sign != 0 ? (Sign > 0 ? 0 : 1) : G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
		const SideRoad* Sd = Which > 0 ? SideOf(Which) : nullptr;
		const std::vector<RoadPoint>& Pts = RoadOf(Which);
		if ((Which > 0 && Sd == nullptr) || Pts.size() < 2) { if (WhyOn("WHOLE_WHY")) std::printf("wholepiece %d arm %zu edge %d unlaid\n", Ji, A, J.Arms[A].Edge); return; }
		const double Long = Pts.back().Along;
		MouthLine M;
		M.Which = Which;
		M.Edge = J.Arms[A].Edge;
		P2 Dir;
		double Rise = 0.0;
		const double MouthAlong = Sign != 0 ? W.ArmAlong[A] : End == 0 ? W.Cut[A] : Long - W.Cut[A];
		if (!CutRow(Pts, MouthAlong, M.P, Dir, Rise)) return;
		M.Out = End == 0 ? Dir : Dir * -1.0;
		M.Rise = End == 0 ? Rise : -Rise;
		M.Left = P2(M.Out.Z, -M.Out.X);
		M.RoadLeft = P2(Dir.Z, -Dir.X);
		// ([smooth junctions stage 5c] the ride's road's arm (no SideRoad) by its map surface too: Hartsop's gravel lanes'
		// junction where the ride starts was tarmac, with give-way paint; by the same road facts as the ride's road's own mesh)
		const RoadFact* const Fact = Sd == nullptr ? FactAt(MouthAlong) : nullptr;
		M.Paved = Sd == nullptr ? Fact == nullptr || !Fact->Gravel : !Sd->Gravel && !Sd->Track;
		M.Pts = &Pts;
		M.End = End;
		M.Cut = W.Cut[A];
		M.Base = MouthAlong;
		M.Inner.push_back(M.P.At);
		// (in to its end, or the ride's road's to the junction)
		const double Stop = Sign != 0 ? MouthAlong - Sign * W.Cut[A] : End == 0 ? -1e18 : 1e18;
		if (End == 0) { for (size_t I = Pts.size(); I-- > 0;) if (Pts[I].Along < M.P.Along && Pts[I].Along >= Stop) M.Inner.push_back(Pts[I].At); }
		else for (const RoadPoint& Q : Pts) if (Q.Along > M.P.Along && Q.Along <= Stop) M.Inner.push_back(Q.At);
		AnyPaved = AnyPaved || M.Paved;
		Mouths.push_back(M);
	}
	const size_t N = Mouths.size();
	// (round the junction in order of the heading out)
	std::vector<size_t> Order(N);
	for (size_t I = 0; I < N; ++I) Order[I] = I;
	std::sort(Order.begin(), Order.end(), [&Mouths, &J](size_t A, size_t B)
	{
		const P2 Pa = Mouths[A].P.At - J.At, Pb = Mouths[B].P.At - J.At;
		return std::atan2(Pa.Z, Pa.X) < std::atan2(Pb.Z, Pb.X);
	});
	// The surface: on a mouth line, the road's own there (crown, tilt and all, as RoadPiece draws its row: the same
	// CrossHeight about the same point); between, each road's carried on in along its own rise, blended by nearness
	// to each mouth line (1/d^2), so it meets every mouth exactly and lies smooth between.
	// (a road's surface carried in past its mouth: its own laid line on in to its end, the ground under it cut to that
	// same line, so the junction keeps clear of the land; out past the mouth, along its rise)
	const auto RoadSurface = [](const MouthLine& M, const P2& At)
	{
		const P2 D = At - M.P.At;
		const double U = D.X * M.Out.X + D.Z * M.Out.Z;
		if (U >= 0.0)
		{
			const double V = D.X * M.RoadLeft.X + D.Z * M.RoadLeft.Z;
			// (RoadPiece's offsets are to the left as the road's points run, its bank the same way)
			return M.P.Height + U * M.Rise + RoadLine::CrossHeight(V, M.P.Half, M.P.Bank);
		}
		const double Long = M.Pts->back().Along;
		const double Along = std::clamp(M.End == 0 ? M.Base + U : M.Base - U, 0.0, Long);
		RoadPoint P;
		P2 Dir;
		double Rise = 0.0;
		CutRow(*M.Pts, Along, P, Dir, Rise);
		const P2 Dp = At - P.At;
		const double V = Dp.X * Dir.Z - Dp.Z * Dir.X;
		return P.Height + RoadLine::CrossHeight(V, P.Half, P.Bank);
	};
	const auto MouthEnds = [](const MouthLine& M, P2& A, P2& B) { A = M.P.At + M.Left * M.P.Half; B = M.P.At - M.Left * M.P.Half; };
	// (weighted by nearness to each road's middle line, as the ground under them is cut to the nearest road: but on a
	// mouth line that road's alone, every other's weight eased to nothing within 2 m of it)
	// [smooth junctions stage 3] The through road's own surface carried straight across, crown and all, as a real
	// junction's is: a lesser road's crown, blended in up to the junction's middle, stood 3 to 6 cm proud of the
	// through road's camber there, a knock straight across (Skipton). Lesser roads run onto its edge from their mouths.
	int ThroughA = -1, ThroughB = -1;
	for (size_t A = 0; A < J.Arms.size() && A < Mouths.size(); ++A)
		if (J.Arms[A].Through) { if (ThroughA < 0) ThroughA = int(A); else if (ThroughB < 0) ThroughB = int(A); }
	const double ThroughEdge = ThroughB >= 0 ? std::fmax(Mouths[size_t(ThroughA)].P.Half, Mouths[size_t(ThroughB)].P.Half) : 0.0;
	const auto ArmsBlend = [&](const P2& At, bool OnlyThrough)
	{
		const size_t Count = Mouths.size();
		double Dm[8], Dc[8];
		for (size_t I = 0; I < Count && I < 8; ++I)
		{
			P2 A, B;
			MouthEnds(Mouths[I], A, B);
			Dm[I] = ToSegment(At, A, B);
			const bool Counts = !OnlyThrough || int(I) == ThroughA || int(I) == ThroughB;
			if (Dm[I] < 1e-4 && Counts) return RoadSurface(Mouths[I], At);
			const std::vector<P2>& L = Mouths[I].Inner;
			Dc[I] = 1e9;
			for (size_t K = 0; K + 1 < L.size(); ++K) Dc[I] = std::fmin(Dc[I], ToSegment(At, L[K], L[K + 1]));
			if (L.size() < 2) Dc[I] = At.DistanceTo(L[0]);
		}
		double Sum = 0.0, Weight = 0.0;
		for (size_t I = 0; I < Count && I < 8; ++I)
		{
			if (OnlyThrough && int(I) != ThroughA && int(I) != ThroughB) continue;
			double Wt = 1.0 / std::pow(Dc[I] + 0.25, 4.0);
			for (size_t K = 0; K < Count && K < 8; ++K)
				if (K != I && (!OnlyThrough || int(K) == ThroughA || int(K) == ThroughB)) Wt *= Ease01(Dm[K] / 2.0);
			Sum += Wt * RoadSurface(Mouths[I], At);
			Weight += Wt;
		}
		return Weight > 1e-30 ? Sum / Weight : Mouths[0].P.Height;
	};
	const auto Surface = [&](const P2& At)
	{
		if (ThroughB >= 0)
		{
			double Dt = 1e9;
			for (int T : {ThroughA, ThroughB})
			{
				const std::vector<P2>& L = Mouths[size_t(T)].Inner;
				for (size_t K = 0; K + 1 < L.size(); ++K) Dt = std::fmin(Dt, ToSegment(At, L[K], L[K + 1]));
			}
			// (the lesser roads' own surfaces from 2.5 m off the through road's edge, or their mouths if nearer)
			double Fade = 2.5;
			for (size_t I = 0; I < Mouths.size(); ++I)
				if (int(I) != ThroughA && int(I) != ThroughB)
				{
					P2 A, B;
					MouthEnds(Mouths[I], A, B);
					Fade = std::fmin(Fade, std::fmax(WhyOn("WHOLE_FADE1") ? 1.0 : 0.25, ToSegment(At, A, B) + (Dt - ThroughEdge)));
				}
			if (Dt <= ThroughEdge) return ArmsBlend(At, true);
			const double Off = Ease01((Dt - ThroughEdge) / Fade);
			if (Off >= 1.0) return ArmsBlend(At, false);
			const double Ht = ArmsBlend(At, true);
			return Ht + (ArmsBlend(At, false) - Ht) * Off;
		}
		return ArmsBlend(At, false);
	};
	// ([build 170] where only gravel lanes meet, the junction is their gravel)
	const float* Top = AnyPaved ? Tarmac : Grit;
	// The rim: round the junction, each mouth across from its edge facing the last road to its edge facing the next,
	// then the rounded corner on to the next road's mouth (a curve through where their edges' lines cross).
	std::vector<P2> Rim;
	std::vector<char> OnMouth;   // (a rim point on a mouth line: no shoulder outside it)
	for (size_t K = 0; K < N; ++K)
	{
		const MouthLine& M = Mouths[Order[K]];
		const MouthLine& Nx = Mouths[Order[(K + 1) % N]];
		const MouthLine& Pv = Mouths[Order[(K + N - 1) % N]];
		// (which edge of this mouth faces the previous road: its left or its right as it heads out)
		const P2 Pdir = Pv.P.At - M.P.At;
		const double FacePrev = Pdir.X * M.Left.X + Pdir.Z * M.Left.Z;
		P2 La, Lb;
		MouthEnds(M, La, Lb);
		const P2 From = FacePrev >= 0.0 ? La : Lb, To = FacePrev >= 0.0 ? Lb : La;
		// (across the mouth, a point a metre or so apart and one on the middle, the crown's break)
		const int Steps = std::max(2, int(std::ceil(2.0 * M.P.Half / 1.2)));
		for (int S = 0; S <= Steps; ++S)
		{
			Rim.push_back(From + (To - From) * (double(S) / Steps));
			OnMouth.push_back(1);
			if (Steps % 2 == 1 && S == Steps / 2) { Rim.push_back(M.P.At); OnMouth.push_back(1); }
		}
		// (the corner on to the next road: from this mouth's far edge out along this road's edge line, round to the
		// next road's edge line and in along it to its mouth's near edge)
		P2 Na, Nb;
		MouthEnds(Nx, Na, Nb);
		const P2 Ndir = M.P.At - Nx.P.At;
		const double NxFace = Ndir.X * Nx.Left.X + Ndir.Z * Nx.Left.Z;
		const P2 NextFrom = NxFace >= 0.0 ? Na : Nb;
		P2 Meet;
		const bool Crosses = Crossing(To, M.Out, NextFrom, Nx.Out, Meet);
		// (where their edge lines meet back towards the middle: a rounded corner; else (a wide gap, more than a half
		// turn) a straight edge across)
		const P2 Mid = (To + NextFrom) * 0.5;
		const bool Corner = Crosses && (Meet - To).X * M.Out.X + (Meet - To).Z * M.Out.Z < 0.5 && (Meet - NextFrom).X * Nx.Out.X + (Meet - NextFrom).Z * Nx.Out.Z < 0.5
			&& Meet.DistanceTo(Mid) < 3.0 * std::fmax(W.Cut[0], 6.0);
		const int Segs = 8;
		for (int S = 1; S < Segs; ++S)
		{
			const double T = double(S) / Segs;
			const P2 Q = Corner ? To * ((1 - T) * (1 - T)) + Meet * (2 * T * (1 - T)) + NextFrom * (T * T) : To + (NextFrom - To) * T;
			Rim.push_back(Q);
			OnMouth.push_back(0);
		}
	}
	// [smooth junctions stage 3] The safety net: a rim that crosses itself (arms meeting at a sharp angle, a corner's
	// curve swinging out across the next road: Haworth's lanes, Fort William) is drawn as the plain apron round every
	// mouth's ends instead, straight edged; crossed, its shoulders fell on its own tarmac and its middle went unfilled.
	{
		bool Crossed = false;
		const size_t Rn = Rim.size();
		for (size_t I = 0; I < Rn && !Crossed; ++I)
			for (size_t K = I + 2; K < Rn && !Crossed; ++K)
			{
				if (I == 0 && K == Rn - 1) continue;
				const P2 &A = Rim[I], &B = Rim[(I + 1) % Rn], &C = Rim[K], &D = Rim[(K + 1) % Rn];
				const double D1 = Cross(C, D, A), D2 = Cross(C, D, B), D3 = Cross(A, B, C), D4 = Cross(A, B, D);
				Crossed = ((D1 > 1e-9 && D2 < -1e-9) || (D1 < -1e-9 && D2 > 1e-9)) && ((D3 > 1e-9 && D4 < -1e-9) || (D3 < -1e-9 && D4 > 1e-9));
			}
		if (Crossed)
		{
			// (the hull of the mouths' points, each knowing its mouth: an edge between two of one mouth is that mouth)
			std::vector<std::pair<P2, int>> Pts2;
			for (size_t K = 0; K < N; ++K)
			{
				P2 A, B;
				MouthEnds(Mouths[K], A, B);
				const int Steps = std::max(2, int(std::ceil(2.0 * Mouths[K].P.Half / 1.2)));
				for (int S = 0; S <= Steps; ++S) Pts2.push_back({A + (B - A) * (double(S) / Steps), int(K)});
				// (and its road's own edges on in to the junction's middle, so the apron covers the way through it)
				const MouthLine& M = Mouths[K];
				for (size_t I = 1; I < M.Inner.size(); ++I)
				{
					const P2 D = M.Inner[I] - M.Inner[I - 1];
					const double L = D.Length();
					if (L < 1e-6) continue;
					const P2 Side(D.Z / L * M.P.Half, -D.X / L * M.P.Half);
					Pts2.push_back({M.Inner[I] + Side, -1 - int(K)});
					Pts2.push_back({M.Inner[I] - Side, -1 - int(K)});
				}
			}
			std::sort(Pts2.begin(), Pts2.end(), [](const std::pair<P2, int>& X, const std::pair<P2, int>& Y) { return X.first.X != Y.first.X ? X.first.X < Y.first.X : X.first.Z < Y.first.Z; });
			std::vector<std::pair<P2, int>> Hull;
			for (int Pass = 0; Pass < 2; ++Pass)
			{
				const size_t Base = Hull.size();
				for (size_t I = 0; I < Pts2.size(); ++I)
				{
					const std::pair<P2, int>& Q = Pass == 0 ? Pts2[I] : Pts2[Pts2.size() - 1 - I];
					while (Hull.size() >= Base + 2 && Cross(Hull[Hull.size() - 2].first, Hull.back().first, Q.first) <= 1e-9) Hull.pop_back();
					Hull.push_back(Q);
				}
				Hull.pop_back();
			}
			if (Hull.size() < 3) return;
			Rim.clear();
			OnMouth.clear();
			for (size_t I = 0; I < Hull.size(); ++I)
			{
				const std::pair<P2, int>& A = Hull[I];
				const std::pair<P2, int>& B = Hull[(I + 1) % Hull.size()];
				Rim.push_back(A.first);
				OnMouth.push_back(A.second == B.second || A.second == Hull[(I + Hull.size() - 1) % Hull.size()].second ? 1 : 0);
				if (A.second != B.second)
				{
					// (a straight edge, a point every 2 m, so its shoulder follows the ground)
					const int Steps = std::max(1, int(std::ceil(A.first.DistanceTo(B.first) / 2.0)));
					for (int S = 1; S < Steps; ++S) { Rim.push_back(A.first + (B.first - A.first) * (double(S) / Steps)); OnMouth.push_back(0); }
				}
			}
		}
	}
	const size_t R = Rim.size();
	if (R < 6) return;
	// (for the ground: the rim and the surface on a grid, no tarmac)
	if (Shape != nullptr)
	{
		Shape->Rim = Rim;
		double X0 = 1e18, Z0 = 1e18, X1 = -1e18, Z1 = -1e18;
		for (const P2& Q : Rim) { X0 = std::fmin(X0, Q.X); Z0 = std::fmin(Z0, Q.Z); X1 = std::fmax(X1, Q.X); Z1 = std::fmax(Z1, Q.Z); }
		Shape->X0 = X0; Shape->Z0 = Z0;
		Shape->Nx = int(std::ceil((X1 - X0) / WholeShapeCell)) + 1;
		Shape->Nz = int(std::ceil((Z1 - Z0) / WholeShapeCell)) + 1;
		Shape->H.assign(size_t(Shape->Nx) * size_t(Shape->Nz), 0.0f);
		for (int Gz = 0; Gz < Shape->Nz; ++Gz)
			for (int Gx = 0; Gx < Shape->Nx; ++Gx)
				Shape->H[size_t(Gz) * size_t(Shape->Nx) + size_t(Gx)] = float(Surface(P2(X0 + Gx * WholeShapeCell, Z0 + Gz * WholeShapeCell)));
		return;
	}
	// (the tarmac: rings from the middle out to the rim, each point's height the surface's; a fan from the middle)
	const auto Vertex = [&Out](const P2& At, double Y, const float* Colour, const P2& Slope)
	{
		Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
		const double Nl = std::sqrt(1.0 + Slope.X * Slope.X + Slope.Z * Slope.Z);
		Out.Nrm.push_back(float(-Slope.X / Nl)); Out.Nrm.push_back(float(1.0 / Nl)); Out.Nrm.push_back(float(-Slope.Z / Nl));
		Out.Uv.push_back(float(At.X / 4.0)); Out.Uv.push_back(float(At.Z / 4.0));
		Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		return uint32_t(Out.Vertices() - 1);
	};
	const auto Tri = [&Out](uint32_t A, uint32_t B, uint32_t C)
	{
		const float* Pa = &Out.Pos[size_t(A) * 3], * Pb = &Out.Pos[size_t(B) * 3], * Pc = &Out.Pos[size_t(C) * 3];
		const double Up = double(Pb[2] - Pa[2]) * double(Pc[0] - Pa[0]) - double(Pb[0] - Pa[0]) * double(Pc[2] - Pa[2]);
		// ([smooth junctions stage 3] a sliver along a straight run of the rim, under a centimetre across: left out, no
		// gap to see, where drawn it stood on edge, a spike (A14 at Huntingdon, 41 of them))
		const auto Len2 = [](const float* P, const float* Q) { return double(P[0] - Q[0]) * double(P[0] - Q[0]) + double(P[2] - Q[2]) * double(P[2] - Q[2]); };
		const double Longest = std::fmax(Len2(Pa, Pb), std::fmax(Len2(Pb, Pc), Len2(Pc, Pa)));
		if (std::fabs(Up) < 1e-9 || std::fabs(Up) < 0.01 * Longest) return;
		if (Up > 0.0) Out.Idx.insert(Out.Idx.end(), {A, B, C});
		else Out.Idx.insert(Out.Idx.end(), {A, C, B});
	};
	const auto SlopeAt = [&Surface](const P2& At)
	{
		const double E = 0.25;
		return P2((Surface(At + P2(E, 0.0)) - Surface(At - P2(E, 0.0))) / (2 * E), (Surface(At + P2(0.0, E)) - Surface(At - P2(0.0, E))) / (2 * E));
	};
	// (the rim cut into triangles by ear clipping (a junction need not be convex: a fork's nose, a cluster's long
	// side), each then split in four twice, every new point on the surface: so the heights follow it, and two
	// triangles sharing an edge share its points)
	double Area2 = 0.0;
	for (size_t I = 0; I < R; ++I) { const P2& A = Rim[I]; const P2& B = Rim[(I + 1) % R]; Area2 += A.X * B.Z - B.X * A.Z; }
	const double Turn = Area2 > 0.0 ? 1.0 : -1.0;   // (which way round the rim runs)
	std::vector<P2> Pt;
	std::vector<std::array<size_t, 3>> Ears;
	// ([5c] one on the ride's road split once more: its crown and banking under the arms' blend read as 2-3 cm facets)
	const bool OnRideRoad = std::any_of(W.ArmSign.begin(), W.ArmSign.end(), [](int S) { return S != 0; });
	const std::function<double(const P2&)> SurfaceAt = [&Surface](const P2& At) { return Surface(At); };
	FillRim(Rim, OnRideRoad ? 3 : 2, Pt, Ears, WhyOn("WHOLE_NOBEND") ? nullptr : &SurfaceAt, 0.005);
	if (WhyOn("WHOLE_TRIS")) std::printf("wholetris %zu\n", Ears.size());
	std::vector<uint32_t> Vtx(Pt.size());
	for (size_t I = 0; I < Pt.size(); ++I) Vtx[I] = Vertex(Pt[I], Surface(Pt[I]), Top, SlopeAt(Pt[I]));
	for (const std::array<size_t, 3>& T : Ears) Tri(Vtx[T[0]], Vtx[T[1]], Vtx[T[2]]);
	// (round each corner, a shoulder out from the tarmac's edge down into the ground, as a road's: ShoulderFlat nearly
	// level, then down to just under the land as drawn by Shoulder)
	std::unordered_map<long long, double> LandCache;
	for (size_t I = 0; I < R; ++I)
	{
		const size_t Jn = (I + 1) % R;
		if (OnMouth[I] && OnMouth[Jn]) continue;
		const auto Outward = [&](size_t K)
		{
			const P2 Before = Rim[(K + R - 1) % R], After = Rim[(K + 1) % R];
			const P2 T = After - Before;
			const double L = std::fmax(1e-6, T.Length());
			// (the rim's outside: to the right of its run where it runs the way of positive area, else the left)
			const P2 Nrm(T.Z / L * Turn, -T.X / L * Turn);
			return Nrm;
		};
		const P2 Oi = Outward(I), Oj = Outward(Jn);
		const double Hi = Surface(Rim[I]), Hj = Surface(Rim[Jn]);
		const auto Band = [&](const P2& Edge, const P2& O, double H, double From, double Lift)
		{
			const P2 Q = Edge + O * From;
			double Y = H + Lift;
			if (Lift < -ShoulderDip)
			{
				const double Drawn = LandDrawnAt(Q.X, Q.Z, &LandCache);
				Y = std::isnan(Drawn) ? H - ShoulderDrop : std::clamp(Drawn - MeetUnder, H - ShoulderDrop, H - ShoulderDip);
			}
			return Vertex(Q, Y, MeetGrass, P2(0.0, 0.0));
		};
		const uint32_t A1 = Band(Rim[I], Oi, Hi, 0.0, -0.002), B1 = Band(Rim[Jn], Oj, Hj, 0.0, -0.002);
		const uint32_t A2 = Band(Rim[I], Oi, Hi, ShoulderFlat, -ShoulderDip), B2 = Band(Rim[Jn], Oj, Hj, ShoulderFlat, -ShoulderDip);
		const uint32_t A3 = Band(Rim[I], Oi, Hi, Shoulder, -1.0), B3 = Band(Rim[Jn], Oj, Hj, Shoulder, -1.0);
		Tri(A1, A2, B2); Tri(A1, B2, B1);
		Tri(A2, A3, B3); Tri(A2, B3, B2);
	}
	// [smooth junctions stage 3] The lines, a hair over the tarmac (they cannot be cut into a surface this shape as a
	// road's are): the through road's centre line carried across, 3 m dashes, 3 m gaps, as a curve from mouth to mouth;
	// each lesser road's give-way, two broken lines (600 mm marks, 300 mm gaps, 200 mm wide, 300 mm apart: Traffic
	// Signs Manual 1003) across its way in, along the through road's edge.
	if (!AnyPaved) { if (!Out.Alpha.empty()) Out.Alpha.resize(Out.Vertices(), 1.0f); return; }
	static constexpr float Paint[3] = {0.83f, 0.83f, 0.75f};
	static constexpr double PaintLift = 0.004;
	// ([smooth junctions stage 6] paint only on the piece's own tarmac: a give-way line worked out from the through road's
	// edge hung out over the verge where a dual's gap or a slip's island moved that edge, Penrith)
	const auto OnTarmac = [&Rim](const P2& Q)
	{
		bool In = false;
		for (size_t I = 0, K = Rim.size() - 1; I < Rim.size(); K = I++)
			if ((Rim[I].Z > Q.Z) != (Rim[K].Z > Q.Z) && Q.X < (Rim[K].X - Rim[I].X) * (Q.Z - Rim[I].Z) / (Rim[K].Z - Rim[I].Z) + Rim[I].X) In = !In;
		return In;
	};
	const auto Quad = [&](const P2& A0, const P2& A1, const P2& B1, const P2& B0)
	{
		if (!PaintOld && (!OnTarmac(A0) || !OnTarmac(A1) || !OnTarmac(B1) || !OnTarmac(B0))) return;
		const uint32_t V0 = Vertex(A0, Surface(A0) + PaintLift, Paint, P2(0.0, 0.0)), V1 = Vertex(A1, Surface(A1) + PaintLift, Paint, P2(0.0, 0.0));
		const uint32_t V2 = Vertex(B1, Surface(B1) + PaintLift, Paint, P2(0.0, 0.0)), V3 = Vertex(B0, Surface(B0) + PaintLift, Paint, P2(0.0, 0.0));
		Tri(V0, V1, V2); Tri(V0, V2, V3);
	};
	int Ta = -1, Tb = -1;
	for (size_t A = 0; A < N; ++A)
		if (J.Arms[A].Through) { if (Ta < 0) Ta = int(A); else if (Tb < 0) Tb = int(A); }
	const auto Lined = [&](const MouthLine& M) { return RoadX::CentreLine(M.P.Lanes, M.P.OneWay, M.P.Half) && M.Paved && !SettsSurface(G.SurfaceOf(M.Edge)); };
	if (Ta >= 0 && Tb >= 0 && Lined(Mouths[size_t(Ta)]) && Lined(Mouths[size_t(Tb)]))
	{
		const MouthLine& A = Mouths[size_t(Ta)];
		const MouthLine& B = Mouths[size_t(Tb)];
		P2 Ctl;
		if (!Crossing(A.P.At, A.Out, B.P.At, B.Out, Ctl) || Ctl.DistanceTo(J.At) > 30.0) Ctl = (A.P.At + B.P.At) * 0.5;
		std::vector<P2> Line;
		for (int S = 0; S <= 40; ++S)
		{
			const double T = S / 40.0;
			Line.push_back(A.P.At * ((1 - T) * (1 - T)) + Ctl * (2 * T * (1 - T)) + B.P.At * (T * T));
		}
		double Gone = 0.0;
		for (size_t I = 1; I < Line.size(); ++I)
		{
			const double Seg = Line[I].DistanceTo(Line[I - 1]);
			const double Mid = Gone + Seg * 0.5;
			Gone += Seg;
			if (std::fmod(Mid, 6.0) > 3.0 || Seg < 1e-6) continue;
			const P2 D = (Line[I] - Line[I - 1]) * (1.0 / Seg);
			const P2 Lf(D.Z * 0.06, -D.X * 0.06);
			Quad(Line[I - 1] + Lf, Line[I] + Lf, Line[I] - Lf, Line[I - 1] - Lf);
		}
	}
	if (Ta >= 0 && Tb >= 0)
	{
		const double ThroughHalf = std::fmax(Mouths[size_t(Ta)].P.Half, Mouths[size_t(Tb)].P.Half);
		for (size_t A = 0; A < N; ++A)
		{
			if (J.Arms[A].Through || !J.Arms[A].In) continue;
			const MouthLine& M = Mouths[A];
			if (!M.Paved || SettsSurface(G.SurfaceOf(M.Edge))) continue;
			// (along the through road's near edge: its middle line, Half and a little out, where this road's middle crosses it)
			const MouthLine& T = Mouths[size_t(Ta)];
			const P2 Mid = (T.P.At + Mouths[size_t(Tb)].P.At) * 0.5;
			P2 ThroughDir = Mouths[size_t(Tb)].P.At - T.P.At;
			const double Tl = ThroughDir.Length();
			if (Tl < 1e-3) continue;
			ThroughDir = ThroughDir * (1.0 / Tl);
			P2 Normal(ThroughDir.Z, -ThroughDir.X);
			if ((M.P.At - Mid).X * Normal.X + (M.P.At - Mid).Z * Normal.Z < 0.0) Normal = Normal * -1.0;
			// (the line: the through road's edge, parallel to it, ThroughHalf + 0.3 out, across this road's way in: the
			// half to the left of traffic coming in where traffic keeps left)
			P2 EdgeAt = Mid + Normal * (ThroughHalf + 0.3);
			// ([smooth junctions stage 6] the through road's edge as its middle runs, curved from mouth to mouth as its centre
			// line is, not the straight line between its mouths: on a bend the line stood out in the road or off it)
			if (!PaintOld)
			{
				P2 Ctl;
				if (!Crossing(T.P.At, T.Out, Mouths[size_t(Tb)].P.At, Mouths[size_t(Tb)].Out, Ctl) || Ctl.DistanceTo(J.At) > 30.0) Ctl = Mid;
				double Best = 1e18;
				for (int S = 0; S < 40; ++S)
				{
					const double T0 = S / 40.0, T1 = (S + 1) / 40.0;
					const auto Bez = [&](double U) { return T.P.At * ((1 - U) * (1 - U)) + Ctl * (2 * U * (1 - U)) + Mouths[size_t(Tb)].P.At * (U * U); };
					const P2 Q0 = Bez(T0), Q1 = Bez(T1);
					P2 Dq = Q1 - Q0;
					const double Lq = Dq.Length();
					if (Lq < 1e-6) continue;
					Dq = Dq * (1.0 / Lq);
					P2 Nq(Dq.Z, -Dq.X);
					if ((M.P.At - Q0).X * Nq.X + (M.P.At - Q0).Z * Nq.Z < 0.0) Nq = Nq * -1.0;
					const P2 E0 = Q0 + Nq * (ThroughHalf + 0.3);
					P2 Cut;
					if (!Crossing(E0, Dq, M.P.At, M.Out, Cut)) continue;
					const double Along = (Cut - E0).X * Dq.X + (Cut - E0).Z * Dq.Z;
					if (Along < -0.01 || Along > Lq + 0.01) continue;
					const double D = Cut.DistanceTo(M.P.At);
					if (D < Best) { Best = D; EdgeAt = E0; ThroughDir = Dq; Normal = Nq; }
				}
			}
			P2 Hit;
			if (!Crossing(EdgeAt, ThroughDir, M.P.At, M.Out, Hit) || Hit.DistanceTo(J.At) > 30.0) continue;
			const double Side = KeepLeft() ? -1.0 : 1.0;   // (traffic coming in runs -Out: its left is -Left)
			const P2 Across = ThroughDir * ((ThroughDir.X * M.Left.X + ThroughDir.Z * M.Left.Z) >= 0.0 ? 1.0 : -1.0);   // (as M.Left, along the edge)
			const double HalfIn = M.P.Half / std::fmax(0.3, std::fabs(M.Out.X * Normal.X + M.Out.Z * Normal.Z));
			for (double Dep : {0.0, 0.5})
				for (double U = 0.05; U + 0.6 <= HalfIn - 0.05; U += 0.9)
				{
					const P2 C0 = Hit + Normal * Dep + Across * (Side * U), C1 = Hit + Normal * Dep + Across * (Side * (U + 0.6));
					Quad(C0, C1, C1 + Normal * 0.2, C0 + Normal * 0.2);
				}
		}
	}
	if (!Out.Alpha.empty()) Out.Alpha.resize(Out.Vertices(), 1.0f);
}

// [smooth junctions stage 4] A roundabout drawn as a circle, planned whole: off the ride's road, no circuit, bridge or
// tunnel by it; each arm's corners a flare along the ring's edge (RingFlare on a big ring, RingFlareLeast on a small
// one, less where the next arm is near: WholeRingFlareLeast at least, else not yet); each arm cut back past its corner.
void RidePlan::PlanWholeRing(size_t Ji, const std::vector<char>& OnRide)
{
	using namespace WholeJunctionBits;
	const JunctionPlace& J = JunctionPlaces[Ji];
	const RoadRing& Rr = RoadRings[size_t(J.Ring)];
	const Region& G = Area.Roads;
	const double TwoPi = 6.283185307179586;
	const double Outer = Rr.Reach + Rr.Half;
	bool Ride = false;   // [smooth junctions 4b] (the ride goes round it)
	for (int E : Rr.Edges) Ride = Ride || OnRide[size_t(E)];
	for (const JunctionArm& A : J.Arms)
		if (OnRide[size_t(A.Edge)] || G.CircuitEdge(A.Edge) || G.Edges[size_t(A.Edge)].A == G.Edges[size_t(A.Edge)].B) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 1\n", J.Ring); return; }
	for (int E : J.Inside)
		if (OnRide[size_t(E)] || G.CircuitEdge(E)) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 2\n", J.Ring); return; }
	for (const Span& Sp : G.Spans)
		if (ToSegment(J.At, Sp.A, Sp.B) < Outer + (RingSpanOld ? 40.0 : 8.0)) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 3\n", J.Ring); return; }
	{
		double D = 0.0, H = 0.0;
		if (!Ride && Nearest(J.At.X, J.At.Z, D, H, nullptr, nullptr, nullptr, true) && D < Outer + WholeRideClear) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 4\n", J.Ring); return; }
	}
	// ([4b] no arm out from the ring starting by the ride's road: laid, it is carried on to that road's middle (LaySide's
	// Meets, 15 m), not from its map point, and its mouth line landed on the ring, Milton Keynes)
	for (const JunctionArm& A : J.Arms)
		{
			const P2 Nd = G.Nodes[size_t(A.Node)];
			double D = 0.0, H = 0.0;
			if (std::find(Rr.Nodes.begin(), Rr.Nodes.end(), A.Node) == Rr.Nodes.end() && Nearest(Nd.X, Nd.Z, D, H, nullptr, nullptr, nullptr, true) && D <= 15.0) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 13\n", J.Ring); return; }
		}
	for (size_t A = 0; A < J.Arms.size(); ++A)
		for (size_t B = A + 1; B < J.Arms.size(); ++B)
			if (J.Arms[A].Edge == J.Arms[B].Edge) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 5\n", J.Ring); return; }
	// (each arm's span of the ring's edge, by bearing: where each of its edges, walked out along its map line, leaves
	// the ring's outer edge; and how far out along it both have: an arm joining the ring near its tangent goes far)
	const size_t N = J.Arms.size();
	std::vector<double> Mid(N), Lo(N), Hi(N), Half(N), Gone(N);
	for (size_t A = 0; A < N; ++A)
	{
		const JunctionArm& Arm = J.Arms[A];
		const std::string& Class = G.ClassOf(Arm.Edge);
		Half[A] = RoadX::Of(G, Arm.Edge, RoadLine::WidthOf(Class)).Half;
		const P2 Node = G.Nodes[size_t(Arm.Node)];
		Mid[A] = std::atan2(Node.Z - Rr.Mid.Z, Node.X - Rr.Mid.X);
		const std::vector<P2> Line = G.LineFrom(Arm.Edge, Arm.Node);
		double Run = 0.0, Left = -1.0, Right = -1.0;
		for (size_t I = 1; I < Line.size() && (Left < 0.0 || Right < 0.0) && Run < 80.0; ++I)
		{
			const P2 D = Line[I] - Line[I - 1];
			const double L = D.Length();
			if (L < 1e-6) continue;
			const P2 U = D * (1.0 / L), Side(U.Z, -U.X);
			for (double T = 0.0; T < L && (Left < 0.0 || Right < 0.0); T += 0.25)
			{
				const P2 C = Line[I - 1] + U * T;
				for (int Sd = 0; Sd < 2; ++Sd)
				{
					double& Got = Sd == 0 ? Left : Right;
					const P2 E = C + Side * (Sd == 0 ? Half[A] : -Half[A]);
					if (Got < 0.0 && Rr.OffOf(E) > Rr.Half)
					{
						Got = Run + T;
						const double B = Mid[A] + std::remainder(std::atan2(E.Z - Rr.Mid.Z, E.X - Rr.Mid.X) - Mid[A], TwoPi);
						if (Sd == 0) Lo[A] = B; else Hi[A] = B;
					}
				}
			}
			Run += L;
		}
		// (an arm from where an entry and an exit divide, out on its own: its edges carried straight in to the ring)
		if (std::find(Rr.Nodes.begin(), Rr.Nodes.end(), Arm.Node) == Rr.Nodes.end())
		{
			// (towards the ring's middle: its own heading in can bend away past the ring, Glasgow)
			const P2 Tc = Rr.Mid - Node;
			const P2 In = Tc.Length() > 1e-6 ? Tc * (1.0 / Tc.Length()) : Arm.Away * -1.0;
			const P2 Side(In.Z, -In.X);
			Left = Right = -1.0;
			for (int Sd = 0; Sd < 2; ++Sd)
				for (double T = 0.0; T < 60.0; T += 0.25)
				{
					const P2 E = Node + Side * (Sd == 0 ? -Half[A] : Half[A]) + In * T;
					if (Rr.OffOf(E) > Rr.Half) continue;
					(Sd == 0 ? Left : Right) = 0.0;
					const double B = Mid[A] + std::remainder(std::atan2(E.Z - Rr.Mid.Z, E.X - Rr.Mid.X) - Mid[A], TwoPi);
					if (Sd == 0) Lo[A] = B; else Hi[A] = B;
					break;
				}
		}
		if (Left < 0.0 || Right < 0.0) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 8 arm %zu onring %d left %.1f right %.1f reach %.1f half %.1f armhalf %.1f node %.0f %.0f mid %.0f %.0f off %.2f\n", J.Ring, A, int(std::find(Rr.Nodes.begin(), Rr.Nodes.end(), Arm.Node) != Rr.Nodes.end()), Left, Right, Rr.Reach, Rr.Half, Half[A], Node.X, Node.Z, Rr.Mid.X, Rr.Mid.Z, Rr.OffOf(Node)); return; }
		if (Lo[A] > Hi[A]) std::swap(Lo[A], Hi[A]);
		Gone[A] = std::fmax(Left, Right);
	}
	// [smooth junctions 4b] (the ride's road in and out: where its edges, walked out from the ring, have both left it)
	std::vector<double> RideEdge;
	std::vector<int> RideOut;
	std::vector<P2> RideAt;
	if (Ride)
	{
		const std::vector<RoadPoint>& R0 = Road;
		int Ia = -1, Ib = -1, Runs = 0;
		bool Was = false;
		for (size_t I = 0; I < R0.size(); ++I)
		{
			const bool In = Rr.OffOf(R0[I].At) <= Rr.Half + 0.5;
			if (In && !Was && ++Runs == 1) Ia = int(I);
			if (In && Runs == 1) Ib = int(I);
			Was = In;
		}
		if (Runs != 1 || Ia <= 0 || Ib >= int(R0.size()) - 1) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 9 runs %d\n", J.Ring, Runs); return; }   // (round it twice, or starting or ending on it: not yet)
		for (const int Way : {-1, 1})
		{
			int I = Way < 0 ? Ia : Ib;
			double Got[2] = {-1.0, -1.0}, Bear[2] = {0.0, 0.0}, Walked = 0.0;
			while ((Got[0] < 0.0 || Got[1] < 0.0) && Walked < 80.0)
			{
				const int K = I + Way;
				if (K < 0 || K >= int(R0.size())) break;
				const RoadPoint& Pa = R0[size_t(I)];
				const RoadPoint& Pb = R0[size_t(K)];
				const P2 D = Pb.At - Pa.At;
				const double L = D.Length();
				I = K;
				if (L < 1e-6) continue;
				const P2 U = D * (1.0 / L), Side(U.Z, -U.X);
				for (double T = 0.0; T < L && (Got[0] < 0.0 || Got[1] < 0.0); T += 0.25)
				{
					const P2 C = Pa.At + U * T;
					const double Hf = Pa.Half + (Pb.Half - Pa.Half) * (T / L);
					for (int Sd = 0; Sd < 2; ++Sd)
					{
						const P2 E = C + Side * (Sd == 0 ? Hf : -Hf);
						if (Got[Sd] >= 0.0 || Rr.OffOf(E) <= Rr.Half) continue;
						Got[Sd] = Pa.Along + (Pb.Along - Pa.Along) * (T / L);
						Bear[Sd] = std::atan2(E.Z - Rr.Mid.Z, E.X - Rr.Mid.X);
					}
				}
				Walked += L;
			}
			if (Got[0] < 0.0 || Got[1] < 0.0) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 8 ride\n", J.Ring); return; }
			double Edge = Way < 0 ? std::fmin(Got[0], Got[1]) : std::fmax(Got[0], Got[1]);
			const double M = Bear[0] + 0.5 * std::remainder(Bear[1] - Bear[0], TwoPi);
			double L0 = M + std::remainder(Bear[0] - M, TwoPi), H0 = M + std::remainder(Bear[1] - M, TwoPi);
			if (L0 > H0) std::swap(L0, H0);
			// (its other half, where it divides for the ring, inside: the mouth out past where the two meet, its span both's)
			for (const std::array<int, 3>& Pr : J.RidePartners)
			{
				const P2 Np = G.Nodes[size_t(Pr[2])];
				double Best = 4.0, As = -1.0;
				for (int K = Way < 0 ? Ia : Ib; K >= 0 && K < int(R0.size()) && std::fabs(R0[size_t(K)].Along - Edge) < 60.0; K += Way)
					if (R0[size_t(K)].At.DistanceTo(Np) < Best) { Best = R0[size_t(K)].At.DistanceTo(Np); As = R0[size_t(K)].Along; }
				if (As < 0.0) continue;
				Edge = Way < 0 ? std::fmin(Edge, As - 1.0) : std::fmax(Edge, As + 1.0);
				const std::string& Pc = G.ClassOf(Pr[0]);
				const double Ph = RoadX::Of(G, Pr[0], RoadLine::WidthOf(Pc)).Half;
				const std::vector<P2> Line = G.LineFrom(Pr[0], Pr[1]);
				bool Gone2[2] = {false, false};
				for (size_t Q = 1; Q < Line.size() && !(Gone2[0] && Gone2[1]); ++Q)
				{
					const P2 D = Line[Q] - Line[Q - 1];
					const double L = D.Length();
					if (L < 1e-6) continue;
					const P2 U = D * (1.0 / L), Side(U.Z, -U.X);
					for (double T = 0.0; T < L && !(Gone2[0] && Gone2[1]); T += 0.25)
						for (int Sd = 0; Sd < 2; ++Sd)
						{
							const P2 E = Line[Q - 1] + U * T + Side * (Sd == 0 ? Ph : -Ph);
							if (Gone2[Sd] || Rr.OffOf(E) <= Rr.Half) continue;
							Gone2[Sd] = true;
							const double B = M + std::remainder(std::atan2(E.Z - Rr.Mid.Z, E.X - Rr.Mid.X) - M, TwoPi);
							L0 = std::fmin(L0, B);
							H0 = std::fmax(H0, B);
						}
				}
			}
			RoadPoint P;
			P2 Dir;
			double Rise = 0.0;
			if (!CutRow(R0, Edge, P, Dir, Rise)) return;
			RideEdge.push_back(Edge);
			RideOut.push_back(Way);
			RideAt.push_back(P.At);
			Mid.push_back(M);
			Lo.push_back(L0);
			Hi.push_back(H0);
			Half.push_back(P.Half);
		}
	}
	const size_t Nt = Mid.size();   // (the arms, then the ride's road in and out)
	std::vector<size_t> Order(Nt);
	for (size_t I = 0; I < Nt; ++I) Order[I] = I;
	std::sort(Order.begin(), Order.end(), [&Mid](size_t A, size_t B) { return std::remainder(Mid[A], 6.283185307179586) < std::remainder(Mid[B], 6.283185307179586); });
	const double Big = Rr.Reach >= 15.0 ? RingFlare : RingFlareLeast;
	WholePlan W;
	W.Flare.assign(2 * Nt, Big);   // (2A: its corner the way round bearings fall, 2A + 1: the way they grow)
	for (size_t K = 0; K < Nt; ++K)
	{
		const size_t A = Order[K], B = Order[(K + 1) % Nt];
		double Gap = Nt == 1 ? TwoPi - (Hi[A] - Lo[A]) : std::remainder(Lo[B] - Hi[A], TwoPi);
		if (Gap < -1.0) Gap += TwoPi;   // (more than half way round to the next: two arms across from each other)
		const double Room = Gap * Outer * 0.5 - 0.25;
		if (Room < -0.5) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 6 room %.1f\n", J.Ring, Room); return; }   // (two arms over each other at the edge; closer than a corner's room, an entry and an exit: the facing corners straight in, the splitter island between)
		W.Flare[2 * A + 1] = std::fmin(W.Flare[2 * A + 1], std::fmax(0.0, Room));
		W.Flare[2 * B] = std::fmin(W.Flare[2 * B], std::fmax(0.0, Room));
	}
	W.Cut.assign(N, 0.0);
	for (size_t A = 0; A < N; ++A)
	{
		const double Long = G.Edges[size_t(J.Arms[A].Edge)].Length;
		double Fl = std::fmax(W.Flare[2 * A], W.Flare[2 * A + 1]);
		W.Cut[A] = Gone[A] + Fl + 1.0;
		// (to its far end less room for the junction there, 6 m, or 45% of it if more)
		const double Most = std::fmax(0.45 * Long, Long - 6.0);
		while (W.Cut[A] > Most && Fl > 0.0)
		{
			Fl = std::fmax(0.0, Fl - 1.0);
			W.Flare[2 * A] = std::fmin(W.Flare[2 * A], Fl);
			W.Flare[2 * A + 1] = std::fmin(W.Flare[2 * A + 1], Fl);
			W.Cut[A] = Gone[A] + Fl + 1.0;
		}
		if (W.Cut[A] > Most) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 7 cut %.1f long %.1f gone %.1f at %.0f %.0f\n", J.Ring, W.Cut[A], Long, Gone[A], J.At.X, J.At.Z); return; }
	}
	// ([smooth junctions 4b] the ride's road's mouths the same way past its corners; not drawn between them)
	for (size_t R = 0; R < RideEdge.size(); ++R)
	{
		const size_t A = N + R;
		const double Fl = std::fmax(W.Flare[2 * A], W.Flare[2 * A + 1]);
		W.RideAlong.push_back(RideEdge[R] + RideOut[R] * (Fl + 1.0));
		W.RideOut.push_back(RideOut[R]);
	}
	if (Ride)
	{
		W.Hide[0] = std::fmin(W.RideAlong[0], W.RideAlong[1]);
		W.Hide[1] = std::fmax(W.RideAlong[0], W.RideAlong[1]);
		if (W.Hide[0] <= Road.front().Along + 1.0 || W.Hide[1] >= Road.back().Along - 1.0) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 10\n", J.Ring); return; }
		for (const std::pair<double, int>& Hd : RideHides)
			if (WholeOf[size_t(Hd.second)].Hide[0] < W.Hide[1] && WholeOf[size_t(Hd.second)].Hide[1] > W.Hide[0]) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 11\n", J.Ring); return; }
	}
	// (each mouth line clear of the ring, as RingPiece needs it: its piece can't be drawn otherwise, its roads hidden: holes, Milton Keynes)
	{
		const auto Clear = [&Rr](const P2& C, const P2& U, double Hf) { const P2 Sd(U.Z, -U.X); return Rr.OffOf(C + Sd * Hf) > Rr.Half + 2.0 && Rr.OffOf(C - Sd * Hf) > Rr.Half + 2.0; };
		for (size_t A = 0; A < N; ++A)
		{
			// (out along it until it is, as far as there is room: 2 m clear on the map's line, for the road as laid beside it)
			const std::vector<P2> Line = G.LineFrom(J.Arms[A].Edge, J.Arms[A].Node);
			const double Most = std::fmax(0.45 * G.Edges[size_t(J.Arms[A].Edge)].Length, G.Edges[size_t(J.Arms[A].Edge)].Length - 6.0);
			bool Fits = false;
			for (; !Fits && W.Cut[A] <= Most; W.Cut[A] += 0.5)
			{
				double Run = 0.0;
				for (size_t Q = 1; Q < Line.size(); ++Q)
				{
					const double L = Line[Q].DistanceTo(Line[Q - 1]);
					if (L < 1e-6) continue;
					if (Run + L >= W.Cut[A] || Q + 1 == Line.size())
					{
						const P2 U = (Line[Q] - Line[Q - 1]) * (1.0 / L);
						Fits = Clear(Line[Q - 1] + U * std::fmin(L, W.Cut[A] - Run), U, Half[A]);
						break;
					}
					Run += L;
				}
				if (Fits) break;
			}
			if (!Fits) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 12\n", J.Ring); return; }
		}
		for (size_t R = 0; R < W.RideAlong.size(); ++R)
		{
			RoadPoint P;
			P2 Dir;
			double Rise = 0.0;
			if (!CutRow(Road, W.RideAlong[R], P, Dir, Rise) || !Clear(P.At, Dir, P.Half)) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 12 ride\n", J.Ring); return; }
		}
	}
	W.Lo = Lo;
	W.Hi = Hi;
	W.Ok = true;
	W.Corner = Big;
	W.Owner = Ride ? 0 : 1 << 30;   // (the ride's road's piece draws one it goes round)
	W.Reach = 0.0;
	for (size_t A = 0; A < N; ++A)
	{
		const int Which = J.Arms[A].Edge + 1;
		const int End = G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
		if (Which < W.Owner) { W.Owner = Which; W.OwnerEnd = End; }
		W.Reach = std::fmax(W.Reach, Rr.Reach + W.Cut[A] + 2.0);
	}
	for (size_t R = 0; R < RideAt.size(); ++R)
	{
		RoadPoint P;
		P2 Dir;
		double Rise = 0.0;
		if (CutRow(Road, W.RideAlong[R], P, Dir, Rise)) W.Reach = std::fmax(W.Reach, P.At.DistanceTo(Rr.Mid) + P.Half + 2.0);
	}
	// ([smooth junctions stage 5c] no bridge or tunnel within its reach (its land is sunk only under the piece itself,
	// WholeGround), not within 40 m of the ring's edge whatever its arms' reach: 80 of Milton Keynes' 135 rings were turned
	// down for the underpasses near them)
	for (const Span& Sp : G.Spans)
		if (!RingSpanOld && ToSegment(J.At, Sp.A, Sp.B) < W.Reach + 1.0) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ring %d no: 3 reach\n", J.Ring); return; }
	WholeOf[Ji] = W;
	if (Ride) RideHides.emplace_back(W.Hide[0], int(Ji));
	WholeRingOf[J.Ring] = int(Ji);
	for (size_t A = 0; A < N; ++A)
	{
		const int Which = J.Arms[A].Edge + 1;
		const int End = G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
		{
				// ([smooth junctions stage 5c] the same road end claimed by two (a ring's arm walked on to a junction's): the
				// first turned down below; the second's claim replaced the first's, and the first, kept, lost its mouth there)
				const auto Had = WholeEnds.find(static_cast<long long>(Which) * 2 + End);
				if (Had != WholeEnds.end() && Had->second.first != int(Ji)) WholeEndClash.push_back({Had->first, Had->second.first, Had->second.second, int(Ji)});
				WholeEnds[static_cast<long long>(Which) * 2 + End] = {int(Ji), W.Cut[A]};
			}
		WholeArmsOf[Which].push_back(int(Ji));
	}
	for (int E : J.Inside) WholeInside[E + 1] = int(Ji);
}

// [smooth junctions stage 4] A roundabout built whole (PlanWholeRing). Its surface: the ring's edges' own heights by
// bearing (each a metre or so of its outer edge), smoothed round over about 8 m, falling WholeRingFall outwards across
// it; out past its edge, by an arm's mouth, blended from that into the arm's own surface by nearness to its mouth
// line. Drawn as a band round the ring (two rows by bearing: the surface is straight across it) and each arm's mouth
// a flare: its edges in from its mouth line to a curve on to the ring's outer edge, Flare along each, the curve a
// parabola about where its edge meets the ring. The flares end on the band's own points, so the two share their seam.
void RidePlan::RingPiece(int Ji, RideMesh& Out, WholeShape* Shape) const
{
	using namespace WholeJunctionBits;
	const JunctionPlace& J = JunctionPlaces[size_t(Ji)];
	const WholePlan& W = WholeOf[size_t(Ji)];
	if (J.Ring < 0 || size_t(J.Ring) >= RoadRings.size() || W.Flare.size() != 2 * (J.Arms.size() + W.RideAlong.size())){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 1\n", J.Ring); return; }
	const RoadRing& Rr = RoadRings[size_t(J.Ring)];
	const Region& G = Area.Roads;
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, MeetGrass[3] = {0.10f, 0.16f, 0.03f};
	const double TwoPi = 6.283185307179586;
	const double Half = Rr.Half;
	{
		Rect Around;
		const double Pad = W.Reach + 8.0;
		Around.X0 = Rr.Mid.X - Pad; Around.Z0 = Rr.Mid.Z - Pad; Around.X1 = Rr.Mid.X + Pad; Around.Z1 = Rr.Mid.Z + Pad;
		LayBlocks(Around);
	}
	// (bins round by bearing, a metre or so apart on the outer edge)
	const int Nb = std::clamp(int(std::ceil(TwoPi * (Rr.Reach + Half) / 1.0)), 48, 720);
	const double Step = TwoPi / Nb;
	const auto BinOf = [Step, Nb](double A) { return ((int(std::lround(A / Step)) % Nb) + Nb) % Nb; };
	const auto Bearing = [&Rr](const P2& P) { return std::atan2(P.Z - Rr.Mid.Z, P.X - Rr.Mid.X); };
	// The ring's own heights by bearing.
	std::vector<double> Hc(static_cast<size_t>(Nb), 0.0), Cnt(static_cast<size_t>(Nb), 0.0);
	for (int E : J.Inside)
	{
		const std::vector<RoadPoint>& Pts = RoadOf(E + 1);
		if (Pts.size() < 2) continue;   // (none laid there, its blocks laid above: no road of its own, Hexham)
		for (size_t I = 0; I + 1 < Pts.size(); ++I)
			for (int S = 0; S < 4; ++S)
			{
				const double T = S / 4.0;
				const P2 Q = Pts[I].At + (Pts[I + 1].At - Pts[I].At) * T;
				const size_t B = size_t(BinOf(Bearing(Q)));
				Hc[B] += Pts[I].Height + (Pts[I + 1].Height - Pts[I].Height) * T;
				Cnt[B] += 1.0;
			}
	}
	// ([smooth junctions 4b] and the ride's road's own, where it goes round)
	if (!W.RideAlong.empty())
		for (size_t I = 0; I + 1 < Road.size(); ++I)
		{
			if (Road[I + 1].Along < W.Hide[0] || Road[I].Along > W.Hide[1]) continue;
			for (int S = 0; S < 4; ++S)
			{
				const double T = S / 4.0;
				const P2 Q = Road[I].At + (Road[I + 1].At - Road[I].At) * T;
				if (std::fabs(Rr.OffOf(Q)) > Half) continue;
				const size_t B = size_t(BinOf(Bearing(Q)));
				Hc[B] += Road[I].Height + (Road[I + 1].Height - Road[I].Height) * T;
				Cnt[B] += 1.0;
			}
		}
	{
		int Known = -1;
		for (int B = 0; B < Nb; ++B)
			if (Cnt[size_t(B)] > 0.0) { Hc[size_t(B)] /= Cnt[size_t(B)]; Known = B; }
		if (Known < 0){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 3\n", J.Ring); return; }
		// (gaps filled straight between the bins either side, round)
		for (int B = 0; B < Nb; ++B)
		{
			if (Cnt[size_t(B)] > 0.0) continue;
			int Lo = B, Hi = B, Dl = 0, Dh = 0;
			while (Cnt[size_t(Lo)] <= 0.0) { Lo = (Lo + Nb - 1) % Nb; ++Dl; }
			while (Cnt[size_t(Hi)] <= 0.0) { Hi = (Hi + 1) % Nb; ++Dh; }
			Hc[size_t(B)] = Hc[size_t(Lo)] + (Hc[size_t(Hi)] - Hc[size_t(Lo)]) * (double(Dl) / double(Dl + Dh));
		}
		for (int B = 0; B < Nb; ++B) Cnt[size_t(B)] = 1.0;
		const int Passes = std::clamp(int(std::lround(8.0 / (Step * std::fmax(1.0, Rr.Reach)))) * 2, 2, 60);
		for (int Pass = 0; Pass < Passes; ++Pass)
		{
			std::vector<double> Next(Hc.size());
			for (int B = 0; B < Nb; ++B)
				Next[size_t(B)] = 0.25 * Hc[size_t((B + Nb - 1) % Nb)] + 0.5 * Hc[size_t(B)] + 0.25 * Hc[size_t((B + 1) % Nb)];
			Hc.swap(Next);
		}
	}
	const auto RingSurface = [&](double A, double Off)
	{
		double U = A / Step;
		U -= std::floor(U / Nb) * Nb;
		const int B0 = int(std::floor(U)) % Nb, B1 = (B0 + 1) % Nb;
		const double F = U - std::floor(U);
		return Hc[size_t(B0)] + (Hc[size_t(B1)] - Hc[size_t(B0)]) * F - WholeRingFall * std::fmax(-Half, Off);
	};
	// Each arm's mouth line and its two corners.
	struct RingMouth
	{
		int Which = 0;
		P2 Mc, Out, Left;
		double Half = 0.0, Aim = 0.0;
		P2 E0[2];                  // its edges at the mouth line: [0] the side bearings fall to, [1] the side they grow to
		std::vector<P2> Chain[2];  // from the ring's edge (its bin) out along the corner and the arm's edge to E0
		int Bin[2] = {0, 0};
	};
	std::vector<RingMouth> Mouths;
	const size_t Na = J.Arms.size();
	for (size_t A = 0; A < Na + W.RideAlong.size(); ++A)
	{
		RingMouth M;
		// ([smooth junctions 4b] after the arms, the ride's road in and out)
		const bool OnRide = A >= Na;
		M.Which = OnRide ? 0 : J.Arms[A].Edge + 1;
		const int End = OnRide ? 0 : G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
		const std::vector<RoadPoint>& Pts = RoadOf(M.Which);
		if (Pts.size() < 2){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 4\n", J.Ring); return; }
		RoadPoint P;
		P2 Dir;
		double Rise = 0.0;
		const double CutAt = OnRide ? W.RideAlong[A - Na] : End == 0 ? W.Cut[A] : Pts.back().Along - W.Cut[A];
		if (!CutRow(Pts, CutAt, P, Dir, Rise)){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 5\n", J.Ring); return; }
		M.Mc = P.At;
		M.Out = OnRide ? (W.RideOut[A - Na] > 0 ? Dir : Dir * -1.0) : End == 0 ? Dir : Dir * -1.0;
		M.Left = P2(M.Out.Z, -M.Out.X);
		M.Half = P.Half;
		M.Aim = Bearing(M.Mc);
		for (int Sd = 0; Sd < 2; ++Sd)
		{
			// (which edge lies the way bearings fall from its middle)
			const P2 Ea = M.Mc + M.Left * M.Half, Eb = M.Mc - M.Left * M.Half;
			const bool AFalls = std::remainder(Bearing(Ea) - M.Aim, TwoPi) < 0.0;
			const P2 E0 = (Sd == 0) == AFalls ? Ea : Eb;
			M.E0[Sd] = E0;
			if (Rr.OffOf(E0) <= Half + 0.5){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 6 arm %zu off %.2f half %.2f mhalf %.2f cut %.1f end %d first %.1f %.1f last %.1f %.1f len %.1f\n", J.Ring, A, Rr.OffOf(E0), Half, M.Half, A < Na ? W.Cut[A] : -1.0, End, Pts.front().At.X, Pts.front().At.Z, Pts.back().At.X, Pts.back().At.Z, Pts.back().Along); return; }   // (its mouth on the ring: no room)
			// (where the edge, heading in, meets the ring's outer edge)
			double Lo = 0.0, Hi = -1.0;
			for (double T = 0.25; T < (A < Na ? W.Cut[A] : 30.0) + Half + 20.0; T += 0.25)
				if (Rr.OffOf(E0 - M.Out * T) <= Half) { Hi = T; break; } else Lo = T;
			P2 C;
			if (Hi >= 0.0)
			{
				for (int It = 0; It < 20; ++It)
				{
					const double Md = 0.5 * (Lo + Hi);
					if (Rr.OffOf(E0 - M.Out * Md) <= Half) Hi = Md; else Lo = Md;
				}
				C = E0 - M.Out * (0.5 * (Lo + Hi));
			}
			else C = Rr.At(Bearing(E0), Half);   // (a road curving in: its edge carried to the ring's nearest point)
			const double Tc = E0.DistanceTo(C);
			if (Tc < 1e-3) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 7\n", J.Ring); return; }
			const P2 Uo = (E0 - C) * (1.0 / Tc);
			const double Fl = W.Flare[2 * A + size_t(Sd)];
			const P2 P1 = C + Uo * std::fmin(Fl, Tc);
			double Ac = Bearing(C);
			// ([4b] never short of the arm's own span of the edge: a divided road's two halves, both inside)
			if (A < W.Lo.size() && A < W.Hi.size())
			{
				const double Sp = Sd == 0 ? Ac + std::remainder(W.Lo[A] - Ac, TwoPi) : Ac + std::remainder(W.Hi[A] - Ac, TwoPi);
				if (Sd == 0 ? Sp < Ac : Sp > Ac) Ac = Sp;
			}
			const double A2 = Ac + (Sd == 0 ? -1.0 : 1.0) * Fl / std::fmax(1.0, Rr.RAt(Ac) + Half);
			M.Bin[Sd] = BinOf(A2);
			const P2 P2r = Rr.At(M.Bin[Sd] * Step, Half);
			std::vector<P2>& Ch = M.Chain[Sd];
			Ch.push_back(P2r);
			for (int S = 1; S <= 10; ++S)
			{
				const double T = S / 10.0;
				Ch.push_back(P2r * ((1 - T) * (1 - T)) + C * (2 * T * (1 - T)) + P1 * (T * T));
			}
			const double Run = P1.DistanceTo(E0);
			const int Bits = int(std::ceil(Run / 2.0));
			for (int S = 1; S <= Bits; ++S) Ch.push_back(P1 + (E0 - P1) * (double(S) / Bits));
			if (Bits == 0) Ch.back() = E0;
		}
		Mouths.push_back(std::move(M));
	}
	// (an arm meeting the ring near its tangent can have its sides the wrong way round: put right)
	for (RingMouth& M : Mouths)
		if (((M.Bin[1] - M.Bin[0]) % Nb + Nb) % Nb >= Nb / 2)
		{
			std::swap(M.Bin[0], M.Bin[1]);
			std::swap(M.E0[0], M.E0[1]);
			std::swap(M.Chain[0], M.Chain[1]);
		}
	const size_t N = Mouths.size();
	std::vector<size_t> Order(N);
	for (size_t I = 0; I < N; ++I) Order[I] = I;
	std::sort(Order.begin(), Order.end(), [&Mouths, Step](size_t A, size_t B) { return Mouths[A].Bin[0] < Mouths[B].Bin[0]; });
	// (each mouth's span of the edge, from its falling bin round to its growing bin; two that reach over each other meet
	// halfway, at one point of the edge: never a gap, the planner left room for their corners)
	const auto Spread = [Nb](int From, int To) { return ((To - From) % Nb + Nb) % Nb; };
	for (size_t K = 0; K < N; ++K)
		if (Spread(Mouths[K].Bin[0], Mouths[K].Bin[1]) >= Nb / 2) { if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: span\n", J.Ring); return; }
	for (size_t K = 0; N > 1 && K < N; ++K)
	{
		RingMouth& A = Mouths[Order[K]];
		RingMouth& B = Mouths[Order[(K + 1) % N]];
		const int Over = Spread(B.Bin[0], A.Bin[1]);
		if (Over == 0 || Over >= Nb / 2) continue;   // (touching, or apart)
		const int Meet = (B.Bin[0] + Over / 2) % Nb;
		A.Bin[1] = Meet;
		B.Bin[0] = Meet;
		A.Chain[1].front() = Rr.At(Meet * Step, Half);
		B.Chain[0].front() = Rr.At(Meet * Step, Half);
	}
	const auto Surface = [&](const P2& P)
	{
		const double A = Bearing(P), Off = Rr.OffOf(P);
		const double Ring = RingSurface(A, Off);
		if (Off <= Half || N == 0) return Ring;
		size_t Best = 0;
		double Dm = 1e18;
		for (size_t K = 0; K < N; ++K)
		{
			const double D = ToSegment(P, Mouths[K].E0[0], Mouths[K].E0[1]);
			if (D < Dm) { Dm = D; Best = K; }
		}
		NearOpts Opts;
		Opts.OnlyRoad = Mouths[Best].Which;
		NearHit Hit;
		if (!NearestOf(P.X, P.Z, Opts, Hit)) return Ring;
		const double Arm = Hit.Height + Hit.Cross;
		const double T = Ease01((Off - Half) / std::fmax(1e-3, Off - Half + Dm));
		return Ring + (Arm - Ring) * T;
	};
	// The rim: round by bearing, the ring's outer edge between mouths, each mouth out along its falling corner, across
	// its mouth line and back along its growing corner.
	std::vector<P2> Rim;
	std::vector<char> OnMouth;
	const auto Across = [](const RingMouth& M, std::vector<P2>& Pts)
	{
		for (int S = 1; S < 4; ++S) Pts.push_back(M.E0[0] + (M.E0[1] - M.E0[0]) * (S / 4.0));
	};
	if (N == 0)
		for (int B = 0; B < Nb; ++B) { Rim.push_back(Rr.At(B * Step, Half)); OnMouth.push_back(0); }
	for (size_t K = 0; K < N; ++K)
	{
		const RingMouth& M = Mouths[Order[K]];
		const RingMouth& Next = Mouths[Order[(K + 1) % N]];
		for (size_t I = 0; I < M.Chain[0].size(); ++I) { Rim.push_back(M.Chain[0][I]); OnMouth.push_back(I + 1 == M.Chain[0].size()); }
		std::vector<P2> Mid;
		Across(M, Mid);
		for (const P2& Q : Mid) { Rim.push_back(Q); OnMouth.push_back(1); }
		for (size_t I = M.Chain[1].size(); I-- > 0;) { Rim.push_back(M.Chain[1][I]); OnMouth.push_back(I + 1 == M.Chain[1].size()); }
		const int Between = N == 1 ? Nb - Spread(M.Bin[0], M.Bin[1]) : Spread(M.Bin[1], Next.Bin[0]);
		for (int S = 1; S < Between; ++S) { Rim.push_back(Rr.At(((M.Bin[1] + S) % Nb) * Step, Half)); OnMouth.push_back(0); }
		if (Between == 0 && N > 1) { Rim.pop_back(); OnMouth.pop_back(); }   // (two mouths meeting at one point: once)
	}
	const size_t R = Rim.size();
	if (R < 6){ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 10\n", J.Ring); return; }
	if (Shape != nullptr)
	{
		Shape->Rim = Rim;
		double X0 = 1e18, Z0 = 1e18, X1 = -1e18, Z1 = -1e18;
		for (const P2& Q : Rim) { X0 = std::fmin(X0, Q.X); Z0 = std::fmin(Z0, Q.Z); X1 = std::fmax(X1, Q.X); Z1 = std::fmax(Z1, Q.Z); }
		Shape->X0 = X0; Shape->Z0 = Z0;
		Shape->Nx = int(std::ceil((X1 - X0) / WholeShapeCell)) + 1;
		Shape->Nz = int(std::ceil((Z1 - Z0) / WholeShapeCell)) + 1;
		Shape->H.assign(size_t(Shape->Nx) * size_t(Shape->Nz), 0.0f);
		for (int Gz = 0; Gz < Shape->Nz; ++Gz)
			for (int Gx = 0; Gx < Shape->Nx; ++Gx)
				Shape->H[size_t(Gz) * size_t(Shape->Nx) + size_t(Gx)] = float(Surface(P2(X0 + Gx * WholeShapeCell, Z0 + Gz * WholeShapeCell)));
		{ if (WhyOn("WHOLE_RING_WHY")) std::printf("ringpiece %d out: 11\n", J.Ring); return; }
	}
	const auto Vertex = [&Out](const P2& At, double Y, const float* Colour, const P2& Slope)
	{
		Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
		const double Nl = std::sqrt(1.0 + Slope.X * Slope.X + Slope.Z * Slope.Z);
		Out.Nrm.push_back(float(-Slope.X / Nl)); Out.Nrm.push_back(float(1.0 / Nl)); Out.Nrm.push_back(float(-Slope.Z / Nl));
		Out.Uv.push_back(float(At.X / 4.0)); Out.Uv.push_back(float(At.Z / 4.0));
		Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		return uint32_t(Out.Vertices() - 1);
	};
	const auto Tri = [&Out](uint32_t A, uint32_t B, uint32_t C)
	{
		const float* Pa = &Out.Pos[size_t(A) * 3], * Pb = &Out.Pos[size_t(B) * 3], * Pc = &Out.Pos[size_t(C) * 3];
		const double Up = double(Pb[2] - Pa[2]) * double(Pc[0] - Pa[0]) - double(Pb[0] - Pa[0]) * double(Pc[2] - Pa[2]);
		const auto Len2 = [](const float* P, const float* Q) { return double(P[0] - Q[0]) * double(P[0] - Q[0]) + double(P[2] - Q[2]) * double(P[2] - Q[2]); };
		const double Longest = std::fmax(Len2(Pa, Pb), std::fmax(Len2(Pb, Pc), Len2(Pc, Pa)));
		if (std::fabs(Up) < 1e-9 || std::fabs(Up) < 0.01 * Longest) return;
		if (Up > 0.0) Out.Idx.insert(Out.Idx.end(), {A, B, C});
		else Out.Idx.insert(Out.Idx.end(), {A, C, B});
	};
	const auto SlopeAt = [&Surface](const P2& At)
	{
		const double E = 0.25;
		return P2((Surface(At + P2(E, 0.0)) - Surface(At - P2(E, 0.0))) / (2 * E), (Surface(At + P2(0.0, E)) - Surface(At - P2(0.0, E))) / (2 * E));
	};
	// (the band: inner and outer edge by bearing)
	{
		std::vector<uint32_t> In(static_cast<size_t>(Nb)), Ou(static_cast<size_t>(Nb));
		for (int B = 0; B < Nb; ++B)
		{
			const double A = B * Step;
			const P2 Pi = Rr.At(A, -Half), Po = Rr.At(A, Half);
			const P2 Sl = SlopeAt(Rr.At(A, 0.0));
			In[size_t(B)] = Vertex(Pi, RingSurface(A, -Half), Tarmac, Sl);
			Ou[size_t(B)] = Vertex(Po, RingSurface(A, Half), Tarmac, Sl);
		}
		for (int B = 0; B < Nb; ++B)
		{
			const int C = (B + 1) % Nb;
			Tri(In[size_t(B)], Ou[size_t(B)], Ou[size_t(C)]);
			Tri(In[size_t(B)], Ou[size_t(C)], In[size_t(C)]);
		}
	}
	// (each flare: its falling chain out, across its mouth, its growing chain back, the ring's edge back round under it)
	for (size_t K = 0; K < N; ++K)
	{
		const RingMouth& M = Mouths[K];
		std::vector<P2> Poly(M.Chain[0].begin(), M.Chain[0].end());
		Across(M, Poly);
		for (size_t I = M.Chain[1].size(); I-- > 0;) Poly.push_back(M.Chain[1][I]);
		for (int S = Spread(M.Bin[0], M.Bin[1]) - 1; S >= 1; --S) Poly.push_back(Rr.At(((M.Bin[0] + S) % Nb) * Step, Half));
		std::vector<P2> Pt;
		std::vector<std::array<size_t, 3>> Ears;
		FillRim(Poly, 2, Pt, Ears);
		std::vector<uint32_t> Vtx(Pt.size());
		for (size_t I = 0; I < Pt.size(); ++I) Vtx[I] = Vertex(Pt[I], Surface(Pt[I]), Tarmac, SlopeAt(Pt[I]));
		for (const std::array<size_t, 3>& T : Ears) Tri(Vtx[T[0]], Vtx[T[1]], Vtx[T[2]]);
	}
	// (round the rim but across the mouths, a shoulder down into the ground, as WholePiece's)
	double Area2 = 0.0;
	for (size_t I = 0; I < R; ++I) { const P2& A = Rim[I]; const P2& B = Rim[(I + 1) % R]; Area2 += A.X * B.Z - B.X * A.Z; }
	const double Turn = Area2 > 0.0 ? 1.0 : -1.0;
	std::unordered_map<long long, double> LandCache;
	for (size_t I = 0; I < R; ++I)
	{
		const size_t Jn = (I + 1) % R;
		if (OnMouth[I] && OnMouth[Jn]) continue;
		const auto Outward = [&](size_t K)
		{
			const P2 T = Rim[(K + 1) % R] - Rim[(K + R - 1) % R];
			const double L = std::fmax(1e-6, T.Length());
			return P2(T.Z / L * Turn, -T.X / L * Turn);
		};
		const P2 Oi = Outward(I), Oj = Outward(Jn);
		const double Hi = Surface(Rim[I]), Hj = Surface(Rim[Jn]);
		const auto Band = [&](const P2& Edge, const P2& O, double H, double From, double Lift)
		{
			const P2 Q = Edge + O * From;
			double Y = H + Lift;
			if (Lift < -ShoulderDip)
			{
				const double Drawn = LandDrawnAt(Q.X, Q.Z, &LandCache);
				Y = std::isnan(Drawn) ? H - ShoulderDrop : std::clamp(Drawn - MeetUnder, H - ShoulderDrop, H - ShoulderDip);
			}
			return Vertex(Q, Y, MeetGrass, P2(0.0, 0.0));
		};
		const uint32_t A1 = Band(Rim[I], Oi, Hi, 0.0, -0.002), B1 = Band(Rim[Jn], Oj, Hj, 0.0, -0.002);
		const uint32_t A2 = Band(Rim[I], Oi, Hi, ShoulderFlat, -ShoulderDip), B2 = Band(Rim[Jn], Oj, Hj, ShoulderFlat, -ShoulderDip);
		const uint32_t A3 = Band(Rim[I], Oi, Hi, Shoulder, -1.0), B3 = Band(Rim[Jn], Oj, Hj, Shoulder, -1.0);
		Tri(A1, A2, B2); Tri(A1, B2, B1);
		Tri(A2, A3, B3); Tri(A2, B3, B2);
	}
	if (!Out.Alpha.empty()) Out.Alpha.resize(Out.Vertices(), 1.0f);
}

double RidePlan::WholeRingTop(int Ring, const P2& At) const
{
	if (!WholeJunctions) return std::nan("");
	const auto It = WholeRingOf.find(Ring);
	if (It == WholeRingOf.end() || !WholeOf[size_t(It->second)].Ok) return std::nan("");
	const std::shared_ptr<const WholeShape> Sh = WholeShapeOf(It->second);
	if (Sh == nullptr) return std::nan("");
	return Sh->HeightAt(At);
}
} // namespace TorqueWorld
