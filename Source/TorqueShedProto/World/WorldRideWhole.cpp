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
#include "WorldRoadLine.h"
#include "WorldRideRoadX.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace TorqueWorld
{
bool RidePlan::WholeJunctions = false;

namespace WholeJunctionBits
{
// Twice the signed area of A, B, C (positive turning from x towards z).
double Cross(const P2& A, const P2& B, const P2& C)
{
	return (B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X);
}
// The tarmac inside a rim (a simple polygon, either way round): ear clipping, the fullest ear first so no slivers, then each
// triangle split in four Levels times, two sharing an edge sharing its points. Pt: the rim's points, then the new ones.
void FillRim(const std::vector<P2>& Rim, int Levels, std::vector<P2>& Pt, std::vector<std::array<size_t, 3>>& Ears)
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

void RidePlan::PlanWholeJunctions()
{
	using namespace WholeJunctionBits;
	WholeOf.assign(JunctionPlaces.size(), WholePlan());
	WholeCells.clear();
	WholeEnds.clear();
	WholeInside.clear();
	WholeArmsOf.clear();
	if (!WholeJunctions) return;
	const Region& G = Area.Roads;
	std::vector<char> OnRide(G.Edges.size(), 0);
	for (const RideStep& St : MadeSteps)
		if (St.Edge >= 0 && size_t(St.Edge) < OnRide.size()) OnRide[size_t(St.Edge)] = 1;
	for (size_t Ji = 0; Ji < JunctionPlaces.size(); ++Ji)
	{
		const JunctionPlace& J = JunctionPlaces[Ji];
		// (the catalogue's designs so far: a T, crossroads, fork, a staggered pair or five ways, on one place of the
		// map or a cluster no wider than JunctionMerge; not a ring, a slip, a dual's gap, a circuit's or the ride's
		// own road (stage 3b), and no bridge or tunnel within 40 m)
		const bool Kind = J.Kind == EJunction::T || J.Kind == EJunction::Cross || J.Kind == EJunction::Fork || J.Kind == EJunction::Stagger || J.Kind == EJunction::Multi;
		if (!Kind || J.Dual || J.Ring >= 0 || J.Span > JunctionMerge || J.Arms.size() < 3 || J.Arms.size() > 5) continue;
		bool Fits = true;
		for (const JunctionArm& A : J.Arms)
			Fits = Fits && !OnRide[size_t(A.Edge)] && !G.CircuitEdge(A.Edge) && A.Pair < 0 && G.Edges[size_t(A.Edge)].A != G.Edges[size_t(A.Edge)].B;
		for (int E : J.Inside) Fits = Fits && !OnRide[size_t(E)] && !G.CircuitEdge(E);
		for (const Span& Sp : G.Spans)
			if (Fits && WholeJunctionBits::ToSegment(J.At, Sp.A, Sp.B) < 40.0) Fits = false;
		if (!Fits) continue;
		// (none within WholeRideClear of the ride's road, whose own junctions are its mouths (stage 3b): 30 m left a lane's
		// junction 30 m up from the ride's road on the old fan, torn down a 27% arm, Hartsop)
		{
			double D = 0.0, H = 0.0;
			if (Nearest(J.At.X, J.At.Z, D, H, nullptr, nullptr, nullptr, true) && D < WholeRideClear) continue;
		}
		// (an arm's own end twice at one junction, a loop: not yet)
		bool Twice = false;
		for (size_t A = 0; A < J.Arms.size(); ++A)
			for (size_t B = A + 1; B < J.Arms.size(); ++B) Twice = Twice || J.Arms[A].Edge == J.Arms[B].Edge;
		if (Twice) continue;
		// (each arm's half width, and the corners' radius: 10 m where a greater road meets, 6 m among lesser ones)
		std::vector<double> Half(J.Arms.size());
		bool Greater = false;
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			const std::string& Class = G.ClassOf(J.Arms[A].Edge);
			Half[A] = RingHalfOf(J.Arms[A].Edge, RoadX::Of(G, J.Arms[A].Edge, RoadLine::WidthOf(Class)).Half);
			Greater = Greater || Class == "primary" || Class == "trunk" || Class == "secondary";
		}
		WholePlan W;
		W.Corner = Greater ? WholeCornerGreat : WholeCornerLeast;
		// (each arm's mouth: far enough out that its tarmac is clear of every other arm's that isn't across from it,
		// with room for the corner)
		W.Cut.assign(J.Arms.size(), 0.0);
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			double Need = Half[A] + 1.5;
			for (size_t B = 0; B < J.Arms.size(); ++B)
			{
				if (A == B) continue;
				const double Th = Apart(J.Arms[A].Away, J.Arms[B].Away);
				if (Th > WholeAcross) continue;
				Need = std::fmax(Need, (Half[B] + Half[A] * std::fabs(std::cos(Th)) + W.Corner * 0.8) / std::fmax(0.25, std::sin(Th)));
			}
			// (measured from the junction's middle; an arm from a cluster's other point less what it already has)
			Need -= std::fmax(0.0, J.Arms[A].Away.X * (G.Nodes[size_t(J.Arms[A].Node)].X - J.At.X) + J.Arms[A].Away.Z * (G.Nodes[size_t(J.Arms[A].Node)].Z - J.At.Z));
			W.Cut[A] = std::clamp(Need, 2.0, WholeCutMost);
			if (W.Cut[A] > 0.45 * G.Edges[size_t(J.Arms[A].Edge)].Length) Fits = false;   // (no room before the junction at its other end)
		}
		if (!Fits) continue;
		// (and no arm whose other end is another whole junction close enough that the two would meet)
		W.Ok = true;
		W.Owner = 1 << 30;
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			const int Which = J.Arms[A].Edge + 1;
			const int End = G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
			if (Which < W.Owner) { W.Owner = Which; W.OwnerEnd = End; }
		}
		W.Reach = 0.0;
		for (size_t A = 0; A < J.Arms.size(); ++A) W.Reach = std::fmax(W.Reach, W.Cut[A] + J.Span + 2.0);
		WholeOf[Ji] = W;
		for (size_t A = 0; A < J.Arms.size(); ++A)
		{
			const int Which = J.Arms[A].Edge + 1;
			const int End = G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
			WholeEnds[static_cast<long long>(Which) * 2 + End] = {int(Ji), W.Cut[A]};
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
			WholeOf[size_t(KV.second.first)].Ok = false;
			WholeOf[size_t(Other->second.first)].Ok = false;
		}
	}
	// (those turned down on the way: their arms back to the old way)
	for (auto It = WholeEnds.begin(); It != WholeEnds.end();)
		It = WholeOf[size_t(It->second.first)].Ok ? std::next(It) : WholeEnds.erase(It);
	for (auto It = WholeInside.begin(); It != WholeInside.end();)
		It = WholeOf[size_t(It->second)].Ok ? std::next(It) : WholeInside.erase(It);
	for (auto& KV : WholeArmsOf)
		KV.second.erase(std::remove_if(KV.second.begin(), KV.second.end(), [this](int Ji) { return !WholeOf[size_t(Ji)].Ok; }), KV.second.end());
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
			const double Y = Sh->HeightAt(At);
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
	const JunctionPlace& J = JunctionPlaces[size_t(Ji)];
	const WholePlan& W = WholeOf[size_t(Ji)];
	const Region& G = Area.Roads;
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, Grit[3] = {0.095f, 0.088f, 0.07f}, MeetGrass[3] = {0.10f, 0.16f, 0.03f};
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
		std::vector<P2> Inner;   // its middle line from the mouth in to its end
	};
	std::vector<MouthLine> Mouths;
	bool AnyPaved = false;
	for (size_t A = 0; A < J.Arms.size(); ++A)
	{
		const int Which = J.Arms[A].Edge + 1;
		const int End = G.Edges[size_t(J.Arms[A].Edge)].A == J.Arms[A].Node ? 0 : 1;
		const SideRoad* Sd = SideOf(Which);
		const std::vector<RoadPoint>& Pts = RoadOf(Which);
		if (Sd == nullptr || Pts.size() < 2) return;
		const double Long = Pts.back().Along;
		MouthLine M;
		M.Which = Which;
		P2 Dir;
		double Rise = 0.0;
		if (!CutRow(Pts, End == 0 ? W.Cut[A] : Long - W.Cut[A], M.P, Dir, Rise)) return;
		M.Out = End == 0 ? Dir : Dir * -1.0;
		M.Rise = End == 0 ? Rise : -Rise;
		M.Left = P2(M.Out.Z, -M.Out.X);
		M.RoadLeft = P2(Dir.Z, -Dir.X);
		M.Paved = !Sd->Gravel && !Sd->Track;
		M.Pts = &Pts;
		M.End = End;
		M.Cut = W.Cut[A];
		M.Inner.push_back(M.P.At);
		if (End == 0) { for (size_t I = Pts.size(); I-- > 0;) if (Pts[I].Along < M.P.Along) M.Inner.push_back(Pts[I].At); }
		else for (const RoadPoint& Q : Pts) if (Q.Along > M.P.Along) M.Inner.push_back(Q.At);
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
		const double Along = M.End == 0 ? std::fmax(0.0, M.Cut + U) : std::fmin(Long, Long - M.Cut - U);
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
					Fade = std::fmin(Fade, std::fmax(0.25, ToSegment(At, A, B) + (Dt - ThroughEdge)));
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
	FillRim(Rim, 2, Pt, Ears);
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
	const auto Quad = [&](const P2& A0, const P2& A1, const P2& B1, const P2& B0)
	{
		const uint32_t V0 = Vertex(A0, Surface(A0) + PaintLift, Paint, P2(0.0, 0.0)), V1 = Vertex(A1, Surface(A1) + PaintLift, Paint, P2(0.0, 0.0));
		const uint32_t V2 = Vertex(B1, Surface(B1) + PaintLift, Paint, P2(0.0, 0.0)), V3 = Vertex(B0, Surface(B0) + PaintLift, Paint, P2(0.0, 0.0));
		Tri(V0, V1, V2); Tri(V0, V2, V3);
	};
	int Ta = -1, Tb = -1;
	for (size_t A = 0; A < N; ++A)
		if (J.Arms[A].Through) { if (Ta < 0) Ta = int(A); else if (Tb < 0) Tb = int(A); }
	const auto Lined = [&](const MouthLine& M) { return RoadX::CentreLine(M.P.Lanes, M.P.OneWay, M.P.Half) && M.Paved && !SettsSurface(G.SurfaceOf(M.Which - 1)); };
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
			if (!M.Paved || SettsSurface(G.SurfaceOf(M.Which - 1))) continue;
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
			const P2 EdgeAt = Mid + Normal * (ThroughHalf + 0.3);
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
} // namespace TorqueWorld
