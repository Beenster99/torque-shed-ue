// The sat nav (Ride UK stage 7): the turns along the ride's road and the places to stop on the way.
//
// The turns come from the map's road graph, as the route was found: at each junction the ride goes
// through, which way it goes against the road ahead and the others there. The stops are the map's
// fuel, cafes, pubs, viewpoints and lay-bys by the road.
#include "WorldRide.h"
#include "WorldRoadLine.h"
#include "WorldRideRoadX.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>

namespace TorqueWorld
{
namespace RideNavBits
{
// The heading out of a junction along an edge: its first 20 m (or all of it, if shorter).
P2 HeadingOut(const Region& G, int Edge, int Node)
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

// How far Out turns from In (radians, + to the left: left of a heading (dx, dz) is (dz, -dx)).
double TurnOf(const P2& In, const P2& Out)
{
	return std::atan2(Out.X * In.Z - Out.Z * In.X, Out.X * In.X + Out.Z * In.Z);
}

int TurnKind(double Angle)
{
	const double A = std::fabs(Angle);
	const int Size = A < 0.35 ? 0 : A < 0.87 ? 1 : A < 2.1 ? 2 : 3;
	return Angle > 0.0 ? -Size : Size;
}
} // namespace RideNavBits

const char* RidePlan::StopName(int Kind)
{
	static const char* Names[StopKinds] = {"Fuel", "Cafe", "Pub", "Viewpoint", "Lay-by", "Inn", "Campsite", "Castle"};
	return Kind >= 0 && Kind < StopKinds ? Names[Kind] : "";
}

namespace RideNavBits
{
// [OSM stage 1] The map's places to stay are mostly holiday cottages: an inn worth stopping at is
// one its name says takes a rider in for the night (an inn, a hotel, an arms, a B&B, a guest house,
// a hostel), not a cottage let by the week.
bool Lodging(const std::string& Name)
{
	std::string Low;
	for (char C : Name) Low += char(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C);
	for (const char* Not : {"cottage", "holiday", "self catering", "self-catering", "apartment", "ltd", "properties", "lodges"})
		if (Low.find(Not) != std::string::npos) return false;
	for (const char* Yes : {"inn", "hotel", "arms", "b&b", "b & b", "bed and breakfast", "bed & breakfast", "guest house", "guesthouse", "hostel", "tavern"})
		if (Low.find(Yes) != std::string::npos) return true;
	return false;
}
} // namespace RideNavBits

void RidePlan::FindPathNodes(int From, const std::vector<int>& Path, double RawLength)
{
	// ([OSM stage 6C] as FindTurnsAndStops makes them, ahead of it: the furniture's direction signs want
	// the junctions before the turns are found)
	PathNodes.clear();
	if (Road.size() < 2 || Path.empty()) return;
	const Region& G = Area.Roads;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	int Node = From;
	double Raw = 0.0;
	PathNodes.push_back({From, Path[0], 0.0});
	for (size_t I = 0; I < Path.size(); ++I)
	{
		Raw += G.Edges[size_t(Path[I])].Length;
		Node = G.OtherEnd(Path[I], Node);
		const P2 At = G.Nodes[size_t(Node)];
		const double Guess = Raw * Scale;
		double Best = 1e18, Found = Guess;
		for (const RoadPoint& P : Road)
		{
			if (P.Along < Guess - 80.0) continue;
			if (P.Along > Guess + 80.0) break;
			const double D = P.At.DistanceTo(At);
			if (D < Best) { Best = D; Found = P.Along; }
		}
		PathNodes.push_back({Node, I + 1 < Path.size() ? Path[I + 1] : -1, Found});
	}
}

unsigned char RidePlan::ArmRankOf(int Node, int Edge) const
{
	const auto It = ArmRank.find(static_cast<long long>(Node) * 4000037LL + Edge);
	return It == ArmRank.end() ? 0 : It->second;
}

bool RidePlan::LinesStopAt(double Along) const
{
	for (const std::pair<double, double>& R : JunctionNoLines)
		if (Along >= R.first && Along <= R.second) return true;
	return false;
}

bool RidePlan::NoJunctionPriority = false;

void RidePlan::FindPriorities(int From, const std::vector<int>& Path)
{
	using namespace RideNavBits;
	Junctions.clear();
	ArmRank.clear();
	if (Path.empty() || NoJunctionPriority) return;
	const Region& G = Area.Roads;
	const auto IsRing = [&G](int E)
	{
		const RoadExtra* X = E >= 0 ? G.ExtraOf(E) : nullptr;
		return X != nullptr && X->Has(RoadExtra::Roundabout);
	};
	int Node = From;
	double Raw = 0.0;
	for (size_t I = 0; I + 1 < Path.size(); ++I)
	{
		Node = G.OtherEnd(Path[I], Node);
		Raw += G.Edges[size_t(Path[I])].Length;
		const int Came = Path[I], Onto = Path[I + 1];
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		if (Here.size() < 3 || RingAt(Node) >= 0) continue;
		bool Ring = false;
		for (int E : Here) Ring = Ring || IsRing(E);
		if (Ring) continue;
		// (each road there: what a metre of it costs the quick way, the lower the greater; its heading out)
		const size_t N = Here.size();
		std::vector<double> Cost(N);
		std::vector<P2> Out(N);
		for (size_t K = 0; K < N; ++K)
		{
			Cost[K] = RoadLine::MainCostOf(G.ClassOf(Here[K]));
			Out[K] = HeadingOut(G, Here[K], Node);
		}
		// (the through road: the pair of the best class, then the straightest; none where two pairs are
		// as good, an unmarked crossroads)
		int Pa = -1, Pb = -1;
		double PairCost = 1e9, PairBend = 1e9, NextCost = 1e9, NextBend = 1e9;
		for (size_t A = 0; A < N; ++A)
			for (size_t B = A + 1; B < N; ++B)
			{
				if (Here[A] == Here[B]) continue;   // (a loop's two ends)
				const double Bend = WorldPi - std::fabs(TurnOf(Out[A], Out[B]));
				if (Bend > JunctionBent) continue;
				const double C = std::fmax(Cost[A], Cost[B]);
				const bool Better = C < PairCost - 0.05 || (C < PairCost + 0.05 && Bend < PairBend);
				if (Better) { NextCost = PairCost; NextBend = PairBend; PairCost = C; PairBend = Bend; Pa = int(A); Pb = int(B); }
				else if (C < NextCost - 0.05 || (C < NextCost + 0.05 && Bend < NextBend)) { NextCost = C; NextBend = Bend; }
			}
		if (Pa < 0) continue;
		double OthersBest = 1e9;
		int OfBest = 0;
		for (size_t K = 0; K < N; ++K)
		{
			if (int(K) == Pa || int(K) == Pb) continue;
			OthersBest = std::fmin(OthersBest, Cost[K]);
		}
		for (size_t K = 0; K < N; ++K) OfBest += Cost[K] < PairCost + 0.05 ? 1 : 0;
		// (a through road no lesser than the others; bent past JunctionStraight only where it alone is the best class)
		if (OthersBest < PairCost - 0.05) continue;
		if (PairBend > JunctionStraight && OfBest > 2) continue;
		if (NextCost < PairCost + 0.05 && NextBend < PairBend + 0.2 && OthersBest < PairCost + 0.05) continue;
		for (size_t K = 0; K < N; ++K)
			ArmRank[static_cast<long long>(Node) * 4000037LL + Here[K]] = int(K) == Pa || int(K) == Pb ? 2 : 1;
		const bool CameThrough = Came == Here[size_t(Pa)] || Came == Here[size_t(Pb)], OntoThrough = Onto == Here[size_t(Pa)] || Onto == Here[size_t(Pb)];
		JunctionRule J;
		J.Node = Node;
		J.Came = Came;
		J.Onto = Onto;
		J.RawAlong = Raw;
		J.ThroughA = Here[size_t(Pa)];
		J.ThroughB = Here[size_t(Pb)];
		J.MajorHalf = 0.5 * RoadLine::WidthOf(G.ClassOf(Here[size_t(Pa)]));
		const P2 Back = HeadingOut(G, Came, Node);
		J.Turn = TurnOf(P2(-Back.X, -Back.Z), HeadingOut(G, Onto, Node));
		J.CameMinor = !CameThrough;
		J.OntoMinor = !OntoThrough;
		Junctions.push_back(J);
	}
}

void RidePlan::PlaceJunctions(double RawLength)
{
	JunctionNoLines.clear();
	JunctionGiveWayCount = 0;
	JunctionPaint.clear();
	JunctionThroughCount = 0;
	ThroughEnds.clear();
	FarEdges.clear();
	if (Road.size() < 2) return;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	bool Added = false;
	for (JunctionRule& J : Junctions)
	{
		// (where the junction is on the road as laid: the nearest point within 80 m of where its share says)
		const P2 At = Area.Roads.Nodes[size_t(J.Node)];
		const double Guess = J.RawAlong * Scale;
		double Best = 1e18;
		J.Along = Guess;
		for (const RoadPoint& P : Road)
		{
			if (P.Along < Guess - 80.0) continue;
			if (P.Along > Guess + 80.0) break;
			const double D = P.At.DistanceTo(At);
			if (D < Best) { Best = D; J.Along = P.Along; }
		}
		if (!J.CameMinor && !J.OntoMinor) continue;
		// (where the ride's road leaves the through road's carriageway each way: the map's line of the
		// through road, its first 40 m each way from the junction, and the ride's road walked out from the
		// junction till it is half that road's width and a little from it)
		std::vector<P2> ThroughLine;
		for (int Arm : {J.ThroughA, J.ThroughB})
		{
			const std::vector<P2> L = Area.Roads.LineFrom(Arm, J.Node);
			std::vector<P2> Piece;
			double Gone = 0.0;
			for (size_t K = 0; K < L.size(); ++K)
			{
				if (K > 0) Gone += L[K].DistanceTo(L[K - 1]);
				Piece.push_back(L[K]);
				if (Gone > 40.0) break;
			}
			if (Arm == J.ThroughA) ThroughLine.assign(Piece.rbegin(), Piece.rend());
			else ThroughLine.insert(ThroughLine.end(), Piece.begin() + (Piece.empty() ? 0 : 1), Piece.end());
		}
		const auto OffThrough = [&ThroughLine](const P2& P)
		{
			double Near = 1e18;
			for (size_t K = 0; K + 1 < ThroughLine.size(); ++K)
			{
				const P2 A = ThroughLine[K], D = ThroughLine[K + 1] - A, Q = P - A;
				const double L2 = D.X * D.X + D.Z * D.Z, T = L2 > 1e-9 ? std::clamp((Q.X * D.X + Q.Z * D.Z) / L2, 0.0, 1.0) : 0.0;
				Near = std::fmin(Near, P.DistanceTo(A + D * T));
			}
			return Near;
		};
		const auto EdgeAlong = [&](int Way)
		{
			// (the first point that way, from the junction, clear of the through road's carriageway)
			size_t I = 0;
			while (I + 1 < Road.size() && Road[I].Along < J.Along) ++I;
			for (int Steps = 0; Steps < 40; ++Steps)
			{
				if (OffThrough(Road[I].At) > J.MajorHalf + 0.2) return Road[I].Along;
				if ((Way < 0 && I == 0) || (Way > 0 && I + 1 >= Road.size())) break;
				I = Way < 0 ? I - 1 : I + 1;
			}
			return J.Along + Way * J.MajorHalf;
		};
		const double In = J.CameMinor ? EdgeAlong(-1) : J.Along - 1.0, Out = J.OntoMinor ? EdgeAlong(1) : J.Along + 1.0;
		// ([junction priority 168] the through road's own lines on across it, from its arm off the ride's road
		// to where it is the ride's road again: the ride's own stop that far)
		// (and the give-way line moved to the edge of the through road as laid, where that is known)
		double LinesIn = In, LinesOut = Out, GiveIn = In, GiveOut = Out;
		PaintThrough(J, LinesIn, LinesOut, GiveIn, GiveOut);
		// (none of its own lines across the through road; past the junction's middle only on the minor side)
		JunctionNoLines.emplace_back(std::fmin(LinesIn, GiveIn) - 0.4, std::fmax(LinesOut, GiveOut) + 0.4);
		// (the give-way lines, as the map's own are drawn: across the lane coming up to the through road, at
		// its edge; none where the map has its own give-way, stop or lights by it)
		// ([junction priority 168] on the side coming up to the junction only: lights at a crossing on the
		// through road past it left the ride's road with no give-way line, build 167, Whaley Bridge)
		const auto Mapped = [&](int Way)
		{
			for (const MappedMark& M : MainMapped)
				if ((M.Kind == "give_way" || M.Kind == "stop" || M.Kind == "traffic_signals") && (J.Along - M.Along) * Way > -3.0 && (J.Along - M.Along) * Way < 30.0) return true;
			return false;
		};
		for (int Way : {1, -1})
		{
			if (Way > 0 ? !J.CameMinor : !J.OntoMinor) continue;
			// ([square T] the map's own give-way or stop line, wherever its point was put on the side road,
			// drawn at the through road's edge as laid, as the rule's would be)
			const double Line = Way > 0 ? GiveIn : GiveOut;
			if (Mapped(Way))
			{
				for (MappedMark& M : MainMapped)
					if ((M.Kind == "give_way" || M.Kind == "stop") && (J.Along - M.Along) * Way > -3.0 && (J.Along - M.Along) * Way < 30.0 && Line >= 5.0 && Line <= Length - 5.0)
					{
						M.Along = Line + Way * 0.8;
						Added = true;
					}
				continue;
			}
			if (Line < 5.0 || Line > Length - 5.0) continue;
			MappedMark M;
			M.Kind = "give_way";
			M.Dir = Way;
			M.Along = Line + Way * 0.8;   // (MappedMarks draws it a metre before, as the traffic meets it)
			MainMapped.push_back(M);
			++JunctionGiveWayCount;
			Added = true;
		}
	}
	if (Added) std::stable_sort(MainMapped.begin(), MainMapped.end(), [](const MappedMark& A, const MappedMark& B) { return A.Along < B.Along; });
	std::sort(JunctionPaint.begin(), JunctionPaint.end(), [](const RoadMark& A, const RoadMark& B) { return A.From < B.From; });
}

void RidePlan::PaintThrough(const JunctionRule& J, double& In, double& Out, double& GiveIn, double& GiveOut)
{
	if (Road.size() < 2 || (!J.CameMinor && !J.OntoMinor)) return;
	const bool CameMinor = J.CameMinor, OntoMinor = J.OntoMinor;
	// (the ride's road's point nearest Along, and its heading there)
	const auto PointNear = [this](double Along)
	{
		size_t Lo = 0, Hi = Road.size();
		while (Hi - Lo > 1)
		{
			const size_t M = (Lo + Hi) / 2;
			if (Road[M].Along <= Along) Lo = M;
			else Hi = M;
		}
		return Lo;
	};
	const auto HeadingAt = [this, &PointNear](double Along)
	{
		const size_t I = PointNear(Along);
		const size_t A = I > 0 ? I - 1 : I, B = std::min(I + 1, Road.size() - 1);
		const P2 D = Road[B].At - Road[A].At;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const auto AtAlong = [this, &PointNear](double Along)
	{
		const size_t I = PointNear(Along);
		if (I + 1 >= Road.size()) return Road.back().At;
		const RoadPoint& A = Road[I];
		const RoadPoint& B = Road[I + 1];
		const double T = std::clamp((Along - A.Along) / std::fmax(1e-9, B.Along - A.Along), 0.0, 1.0);
		return A.At + (B.At - A.At) * T;
	};
	// (a through road's arm off the ride's road: its side road, its mouth here, and that road's point by it)
	struct Arm
	{
		const SideRoad* Sd = nullptr;
		const Mouth* M = nullptr;
		const RoadPoint* Pt = nullptr;
		int End = 0;
	};
	std::vector<std::pair<long long, double>> Ends;
	const auto MouthOf = [this, &J, &Ends](int Edge, const RoadPoint*& EndPt, Arm* Out1 = nullptr) -> const Mouth*
	{
		const SideRoad* Sd = EnsureSide(Edge);
		if (Sd == nullptr || Sd->Track || Sd->Setts || Sd->Pts.size() < 2) return nullptr;
		const Mouth* Best = nullptr;
		double Near = 40.0;
		int BestEnd = 0;
		for (int End = 0; End < 2; ++End)
		{
			const Mouth& M = Sd->Mouths[End];
			if (!M.Is || M.Ring || M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size()) continue;
			const double D = std::fabs(Road[size_t(M.MainIndex)].Along - J.Along);
			if (D < Near) { Near = D; Best = &M; BestEnd = End; }
		}
		if (Best == nullptr) return nullptr;
		// (its own lines on to its mouth, once the line across is drawn; the point there for its lanes and
		// width, 3 m in from it)
		Ends.emplace_back(static_cast<long long>(Edge) * 2 + BestEnd, std::fmax(0.0, Best->From + 0.2));
		const double In3 = BestEnd == 0 ? Best->From + 3.0 : Sd->Length - Best->From - 3.0;
		EndPt = &Sd->Pts.front();
		for (const RoadPoint& P : Sd->Pts)
			if (std::fabs(P.Along - In3) < std::fabs(EndPt->Along - In3)) EndPt = &P;
		if (Out1 != nullptr) { Out1->Sd = Sd; Out1->M = Best; Out1->Pt = EndPt; Out1->End = BestEnd; }
		return Best;
	};
	// (an arm's own line as laid, its 20 m from its mouth: towards the junction, or away from it)
	const auto ArmLine = [](const Arm& A, bool Toward, std::vector<P2>& Into)
	{
		std::vector<P2> Got;
		for (const RoadPoint& P : A.Sd->Pts)
		{
			const double From = A.End == 0 ? P.Along : A.Sd->Length - P.Along;
			if (From >= A.M->From && From <= A.M->From + 20.0) Got.push_back(P.At);
		}
		// (as the side road runs: from its end 0; towards the junction is towards that end when End is 0)
		if ((A.End == 0) == Toward) std::reverse(Got.begin(), Got.end());
		Into.insert(Into.end(), Got.begin(), Got.end());
	};
	Arm ArmA, ArmB, ArmX;
	// The through road's line across: from one end to the other as a smooth curve (Hermite) leaving each
	// end along its road. Its ends: an arm off the ride's road at its mouth, or the ride's road itself
	// where it is the through road again (past the junction, or before it).
	P2 S0, H0, S1, H1, Minor;
	double Half0 = 0.0, Half1 = 0.0, RideA0 = 0.0, RideA1 = 0.0;
	bool Centre = true, HasMinor = false;
	if (CameMinor && OntoMinor)
	{
		const RoadPoint *Pa = nullptr, *Pb = nullptr;
		const Mouth* Ma = MouthOf(J.ThroughA, Pa, &ArmA);
		const Mouth* Mb = MouthOf(J.ThroughB, Pb, &ArmB);
		if (Ma == nullptr || Mb == nullptr) return;
		S0 = Ma->At; H0 = Ma->Away * -1.0; Half0 = Pa->Half;
		S1 = Mb->At; H1 = Mb->Away; Half1 = Pb->Half;
		Centre = RoadX::CentreLine(Pa->Lanes, Pa->OneWay, Pa->Half) && RoadX::CentreLine(Pb->Lanes, Pb->OneWay, Pb->Half);
	}
	else
	{
		const int Ours = CameMinor ? J.Onto : J.Came;
		const int Other = J.ThroughA == Ours ? J.ThroughB : J.ThroughA;
		const RoadPoint* Px = nullptr;
		const Mouth* Mx = MouthOf(Other, Px, &ArmX);
		if (Mx == nullptr) return;
		const double MouthAlong = Road[size_t(Mx->MainIndex)].Along;
		Centre = RoadX::CentreLine(Px->Lanes, Px->OneWay, Px->Half);
		HasMinor = true;
		if (CameMinor)
		{
			RideA1 = std::clamp(std::max({Out + 0.4, MouthAlong + 2.0, J.Along + 2.0 * J.MajorHalf + 4.0}), J.Along, J.Along + 30.0);
			S0 = Mx->At; H0 = Mx->Away * -1.0; Half0 = Px->Half;
			S1 = AtAlong(RideA1); H1 = HeadingAt(RideA1); Half1 = Road[PointNear(RideA1)].Half;
			Minor = AtAlong(In - 8.0) - AtAlong(J.Along);
		}
		else
		{
			RideA0 = std::clamp(std::min({In - 0.4, MouthAlong - 2.0, J.Along - 2.0 * J.MajorHalf - 4.0}), J.Along - 30.0, J.Along);
			S0 = AtAlong(RideA0); H0 = HeadingAt(RideA0); Half0 = Road[PointNear(RideA0)].Half;
			S1 = Mx->At; H1 = Mx->Away; Half1 = Px->Half;
			Minor = AtAlong(Out + 8.0) - AtAlong(J.Along);
		}
	}
	const double Span = S0.DistanceTo(S1);
	if (Span < 2.0 || Span > 60.0) return;
	const P2 Chord = (S1 - S0) * (1.0 / Span);
	if (H0.X * Chord.X + H0.Z * Chord.Z < 0.2 || H1.X * Chord.X + H1.Z * Chord.Z < 0.2) return;   // (not a road running on across)
	const auto Curve = [&](double T)
	{
		const double T2 = T * T, T3 = T2 * T;
		return S0 * (2.0 * T3 - 3.0 * T2 + 1.0) + H0 * (Span * (T3 - 2.0 * T2 + T)) + S1 * (-2.0 * T3 + 3.0 * T2) + H1 * (Span * (T3 - T2));
	};
	const auto Tangent = [&](double T)
	{
		const double T2 = T * T;
		const P2 D = S0 * (6.0 * T2 - 6.0 * T) + H0 * (Span * (3.0 * T2 - 4.0 * T + 1.0)) + S1 * (-6.0 * T2 + 6.0 * T) + H1 * (Span * (3.0 * T2 - 2.0 * T));
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : Chord;
	};
	// (the through road as laid: its arms' lines, the curve across, and the ride's road where it is the
	// through road; the ride's road gives way where it comes clear of that road's half and a little)
	for (const std::pair<long long, double>& E : Ends) ThroughEnds[E.first] = E.second;
	if (HasMinor && CameMinor) Out = std::fmax(Out, RideA1 - 0.4);
	if (HasMinor && OntoMinor) In = std::fmin(In, RideA0 + 0.4);
	{
		std::vector<P2> Laid;
		if (!HasMinor) ArmLine(ArmA, true, Laid);
		else if (CameMinor) ArmLine(ArmX, true, Laid);
		else
			for (double A = RideA0 - 20.0; A < RideA0; A += 1.0) Laid.push_back(AtAlong(A));
		for (int K = 0; K <= 24; ++K) Laid.push_back(Curve(K / 24.0));
		if (!HasMinor) ArmLine(ArmB, false, Laid);
		else if (OntoMinor) ArmLine(ArmX, false, Laid);
		else
			for (double A = RideA1 + 1.0; A <= RideA1 + 20.0; A += 1.0) Laid.push_back(AtAlong(A));
		const double HalfT = 0.5 * (Half0 + Half1);
		const auto Off = [&Laid](const P2& P)
		{
			double Near = 1e18;
			for (size_t K = 0; K + 1 < Laid.size(); ++K)
			{
				const P2 A = Laid[K], D = Laid[K + 1] - A, Q = P - A;
				const double L2 = D.X * D.X + D.Z * D.Z, T = L2 > 1e-9 ? std::clamp((Q.X * D.X + Q.Z * D.Z) / L2, 0.0, 1.0) : 0.0;
				Near = std::fmin(Near, P.DistanceTo(A + D * T));
			}
			return Near;
		};
		// [square T] (at a T, the through road as the ride's road lies where it is that road again, carried
		// straight on back through the junction: the give-way line at its edge, square across the side road)
		const P2 MainA = HasMinor ? (CameMinor ? AtAlong(RideA1) : AtAlong(RideA0)) : P2(0.0, 0.0);
		const P2 MainB = HasMinor ? (CameMinor ? AtAlong(RideA1 + 15.0) : AtAlong(RideA0 - 15.0)) : P2(0.0, 0.0);
		const double MainLen = MainA.DistanceTo(MainB);
		const double MainHalf = HasMinor ? (CameMinor ? Half1 : Half0) : HalfT;
		const auto OffMain = [&](const P2& P)
		{
			if (!HasMinor || MainLen < 1.0) return Off(P);
			const P2 D = (MainB - MainA) * (1.0 / MainLen);
			return std::fabs((P.X - MainA.X) * D.Z - (P.Z - MainA.Z) * D.X);
		};
		// (walked out from the junction a metre at a time, the first clear of it; left as the map's where none is)
		for (int Way : {-1, 1})
		{
			if (Way < 0 ? !CameMinor : !OntoMinor) continue;
			for (double Step = 0.0; Step <= 30.0; Step += 0.5)
			{
				const double A = J.Along + Way * Step;
				if (A < 0.0 || A > Length) break;
				if (OffMain(AtAlong(A)) > (HasMinor ? MainHalf : HalfT) + 0.3) { (Way < 0 ? GiveIn : GiveOut) = A; break; }
			}
		}
	}
	// (a point on the ride's road's frame: how far along it and how far to the left of its middle, from the
	// nearest of its pieces within 60 m of the junction)
	const size_t Lo = PointNear(J.Along - 60.0), Hi = std::min(Road.size() - 1, PointNear(J.Along + 60.0) + 1);
	const auto OnRide = [&](const P2& P, double& Along, double& Off, double& HalfHere)
	{
		double Best = 1e18;
		for (size_t I = Lo; I < Hi; ++I)
		{
			const RoadPoint& A = Road[I];
			const RoadPoint& B = Road[I + 1];
			const P2 Ab = B.At - A.At;
			const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
			if (L2 < 1e-12) continue;
			const double T = std::clamp(((P.X - A.At.X) * Ab.X + (P.Z - A.At.Z) * Ab.Z) / L2, 0.0, 1.0);
			const P2 Foot = A.At + Ab * T;
			const double D = P.DistanceTo(Foot);
			if (D >= Best) continue;
			Best = D;
			Along = A.Along + (B.Along - A.Along) * T;
			Off = ((P.X - Foot.X) * Ab.Z - (P.Z - Foot.Z) * Ab.X) / std::sqrt(L2);
			HalfHere = A.Half + (B.Half - A.Half) * T;
		}
		return Best < 1e17;
	};
	static constexpr float White[3] = {0.83f, 0.83f, 0.75f};
	bool Painted = false;
	// (a piece of line Wide m across from (A0, C0) to (A1, C1) in the ride's road's frame: cut along the road
	// where it slants along it, a box where it runs nearly square across)
	bool Edge = false;
	const auto Piece = [&](double A0, double C0, double A1, double C1, double Wide, double Half0Here, double Half1Here, double Long)
	{
		if (std::fabs(C0) > Half0Here + 0.1 && std::fabs(C1) > Half1Here + 0.1) return;   // (not on the ride's tarmac)
		if (A1 < A0) { std::swap(A0, A1); std::swap(C0, C1); }
		const double Da = A1 - A0, Dc = C1 - C0, Len = std::hypot(Da, Dc);
		// (none where the road's frame folds, inside a tight corner: the piece there far longer than it is;
		// nor where it runs out of a corner's outside straight at the corner's point, where the road's rows
		// fan out and a thin slice along it is a wedge)
		if (Len < 1e-4 || Len > Long * 1.6 + 0.1 || Da < 0.05 * std::fabs(Dc)) return;
		RoadMark M;
		M.Kind = MarkPaint;
		M.EdgeLine = Edge;
		for (int C = 0; C < 3; ++C) M.Colour[C] = White[C];
		if (Da > 0.25 * std::fabs(Dc))
		{
			const double Across = std::fmin(0.5 * Wide * Len / Da, 0.3);
			M.From = A0; M.To = A1;
			M.Left = C0 + Across; M.Right = C0 - Across;
			M.Skew = Dc / Da;
		}
		else
		{
			const double Ahead = 0.5 * Wide * Len / std::fabs(Dc);
			M.From = A0 - Ahead; M.To = A1 + Ahead;
			M.Left = std::fmax(C0, C1); M.Right = std::fmin(C0, C1);
		}
		JunctionPaint.push_back(M);
		Painted = true;
	};
	// (the line Off m to the left of the curve, painted where Show says, measured along the curve)
	const auto PaintAlong = [&](double OffBy0, double OffBy1, double Wide, const std::function<bool(double)>& Show)
	{
		const int Steps = std::max(8, int(std::ceil(Span * 1.3 / 0.4)));
		double Gone = 0.0, Pa = 0.0, Pc = 0.0, Ph = 0.0;
		bool Have = false;
		P2 Was;
		for (int K = 0; K <= Steps; ++K)
		{
			const double T = double(K) / Steps;
			const P2 Tn = Tangent(T), Left(Tn.Z, -Tn.X);
			const P2 P = Curve(T) + Left * (OffBy0 + (OffBy1 - OffBy0) * T);
			const double Step = K > 0 ? P.DistanceTo(Was) : 0.0;
			Gone += Step;
			Was = P;
			double A = 0.0, C = 0.0, H = 0.0;
			const bool On = OnRide(P, A, C, H);
			if (On && Have && Show(Gone)) Piece(Pa, Pc, A, C, Wide, Ph, H, Step);
			Have = On;
			Pa = A; Pc = C; Ph = H;
		}
	};
	// (Britain's junction warning line down the middle: 6 m marks, 3 m gaps, a mark from the arm's mouth on)
	if (Centre) PaintAlong(0.0, 0.0, 0.12, [](double S) { return std::fmod(S, 9.0) < 6.0; });
	// (at a T, the side away from the ride's road that gives way: the far side)
	const P2 Tm = Tangent(0.5), MidLeft(Tm.Z, -Tm.X);
	const double Sign = MidLeft.X * Minor.X + MidLeft.Z * Minor.Z > 0.0 ? -1.0 : 1.0;
	// [true T] (its far edge, from the ride's road's edge to the arm's, for the mouth to draw in place of
	// its rounded corner there; and none of the ride's own edge line along the ride's road under it)
	if (HasMinor && ArmX.Sd != nullptr && ArmX.Sd->Edge >= 0)
	{
		FarEdge F;
		for (int K = 0; K <= 32; ++K)
		{
			const double T = double(K) / 32.0;
			const P2 Tn = Tangent(T), Left(Tn.Z, -Tn.X);
			F.Pts.push_back(Curve(T) + Left * (Sign * (Half0 + (Half1 - Half0) * T)));
		}
		if (CameMinor) std::reverse(F.Pts.begin(), F.Pts.end());
		double Lo2 = 1e18, Hi2 = -1e18, OffSum = 0.0;
		bool Fits = true;
		for (const P2& P : F.Pts)
		{
			double A = 0.0, C = 0.0, H = 0.0;
			if (!OnRide(P, A, C, H)) { Fits = false; break; }
			Lo2 = std::fmin(Lo2, A);
			Hi2 = std::fmax(Hi2, A);
			OffSum += C;
		}
		const P2 AwayLeft(ArmX.M->Away.Z, -ArmX.M->Away.X);
		F.Sigma = (F.Pts.back().X - ArmX.M->At.X) * AwayLeft.X + (F.Pts.back().Z - ArmX.M->At.Z) * AwayLeft.Z >= 0.0 ? 1.0 : -1.0;
		F.RideFrom = Lo2;
		F.RideTo = Hi2;
		if (Fits && Hi2 - Lo2 < 40.0)
		{
			const unsigned char Bit = OffSum >= 0.0 ? 1 : 2;
			for (size_t I = PointNear(Lo2); I < Road.size() && I < MouthAt.size() && Road[I].Along <= Hi2 + 0.5; ++I) MouthAt[I] |= Bit;
			FarEdges[static_cast<long long>(ArmX.Sd->Edge) * 2 + ArmX.End] = std::move(F);
		}
	}
	// (at a T, the far side's edge line unbroken)
	if (HasMinor && Half0 >= 2.95 && Half1 >= 2.95)
	{
		Edge = true;
		PaintAlong(Sign * (Half0 - 0.3), Sign * (Half1 - 0.3), 0.13, [](double) { return true; });
	}
	if (Painted) ++JunctionThroughCount;
}

void RidePlan::FindTurnsAndStops(int From, const std::vector<int>& Path, double RawLength)
{
	using namespace RideNavBits;
	Turns.clear();
	Stops.clear();
	RingGiveWays.clear();
	if (Road.size() < 2 || Path.empty()) return;
	const Region& G = Area.Roads;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	// (where a junction of the map's road is on the road as laid: the nearest point to it within 80 m
	// of where its share of the way says)
	const auto AlongOf = [this](const P2& At, double Guess)
	{
		double Best = 1e18, Found = Guess;
		for (const RoadPoint& P : Road)
		{
			if (P.Along < Guess - 80.0) continue;
			if (P.Along > Guess + 80.0) break;
			const double D = P.At.DistanceTo(At);
			if (D < Best) { Best = D; Found = P.Along; }
		}
		return Found;
	};
	std::vector<double> Angles;
	// [OSM stage 5B] (a roundabout's edges; whether a road off one may be ridden away from it)
	const auto IsRingEdge = [&G](int E)
	{
		const RoadExtra* X = E >= 0 ? G.ExtraOf(E) : nullptr;
		return X != nullptr && X->Has(RoadExtra::Roundabout);
	};
	const auto CanLeave = [&G](int E, int AtNode)
	{
		const RoadEdge& R = G.Edges[size_t(E)];
		return R.Oneway == 0 || (R.Oneway == 1 && R.A == AtNode) || (R.Oneway == -1 && R.B == AtNode);
	};
	bool InRing = false;
	P2 RingIn;
	double RingAlong = 0.0;
	int RingNodeIn = -1;   // [roundabouts] (the node the ride came onto the ring at)
	int RingExits = 0;
	int Node = From;
	double Raw = 0.0;
	PathNodes.clear();
	PathNodes.push_back({From, Path[0], 0.0});
	for (size_t I = 0; I < Path.size(); ++I)
	{
		const int Next = G.OtherEnd(Path[I], Node);
		Raw += G.Edges[size_t(Path[I])].Length;
		Node = Next;
		PathNodes.push_back({Node, I + 1 < Path.size() ? Path[I + 1] : -1, AlongOf(G.Nodes[size_t(Node)], Raw * Scale)});
		if (I + 1 >= Path.size()) break;
		const int Came = Path[I], Onto = Path[I + 1];
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		// [OSM stage 5B] Round a roundabout: one turn where the ride enters it, "the Nth exit", its way
		// the way the ride leaves it as against the way it came in; nothing said at the ring's own nodes.
		{
			const bool CameRing = IsRingEdge(Came), OntoRing = IsRingEdge(Onto);
			if (!CameRing && OntoRing)
			{
				const P2 Back0 = HeadingOut(G, Came, Node);
				RingIn = P2(-Back0.X, -Back0.Z);
				RingAlong = AlongOf(G.Nodes[size_t(Node)], Raw * Scale);
				RingNodeIn = Node;
				RingExits = 0;
				InRing = true;
				continue;
			}
			if (InRing && CameRing)
			{
				// (each way off the ring here that can be taken counts as an exit)
				for (int E : Here)
					if (E != Came && E != Onto && !IsRingEdge(E) && CanLeave(E, Node)) ++RingExits;
				if (OntoRing) continue;
				InRing = false;
				if (RingAlong > FinishM) break;
				RideTurn T;
				T.Along = RingAlong;
				T.Turn = TurnKind(TurnOf(RingIn, HeadingOut(G, Onto, Node)));
				T.Onto = G.NameOf(Onto);
				T.Exit = RingExits + 1;
				Turns.push_back(T);
				// (and the ride's own give-way line where it comes onto the ring: back from the ring's
				// middle line by half the ring's width)
				// ([roundabouts] a ring drawn as a circle paints every entry's itself: RingPaint)
				const int RingIn2 = RingAt(RingNodeIn);
				if (RingIn2 < 0 || !RoadRings[size_t(RingIn2)].Circle)
					RingGiveWays.push_back(RingAlong - 0.5 * RoadLine::WidthOf(G.ClassOf(Onto)) - 0.6);
				Angles.push_back(0.0);
				continue;
			}
		}
		if (Here.size() < 3) continue;   // (a bend in the road, not a junction)
		const P2 Back = HeadingOut(G, Came, Node);
		const P2 In(-Back.X, -Back.Z);
		const double Ours = TurnOf(In, HeadingOut(G, Onto, Node));
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		const std::string Was = G.NameOf(Came), Now = G.NameOf(Onto);
		// (said when the way on isn't the obvious one: a real turn, another road more straight ahead than
		// ours, or a road of another name off at an angle)
		if (!(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (Now != Was && std::fabs(Ours) > 0.26))) continue;
		const double Along = AlongOf(G.Nodes[size_t(Node)], Raw * Scale);
		if (Along > FinishM) break;
		// (a roundabout or a staggered crossroads: two turns close together are one)
		if (!Turns.empty() && Along - Turns.back().Along < 30.0 && Turns.back().Exit == 0)
		{
			Angles.back() += Ours;
			Turns.back().Turn = TurnKind(Angles.back());
			Turns.back().Onto = Now;
			continue;
		}
		RideTurn T;
		T.Along = Along;
		T.Turn = TurnKind(Ours);
		T.Onto = Now;
		Turns.push_back(T);
		Angles.push_back(Ours);
	}
	RideTurn End;
	End.Along = FinishM;
	End.Turn = Finish;
	End.Onto = Request.Pins.empty() ? std::string() : Request.Pins.back().Name;
	Turns.push_back(End);

	// The stops: the map's places by the ride's road.
	NearOpts Opts;
	Opts.MainOnly = true;
	const auto Add = [&](int Kind, const std::string& Name, const P2& At, double Reach = StopReach)
	{
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist > Reach) return;
		for (const RideStop& S : Stops)
			if (S.Kind == Kind && S.Name == Name && std::fabs(S.Along - Hit.Along) < 60.0) return;   // (one place, twice on the map)
		RideStop S;
		S.Along = Hit.Along;
		S.Off = Hit.Off;
		S.Kind = Kind;
		S.Name = Name;
		Stops.push_back(S);
	};
	for (const TorqueFit::Json& Poi : G.Pois)
	{
		const std::string What = Poi.Str("kind");
		const int Kind = What == "fuel" ? StopFuel : What == "cafe" ? StopCafe : What == "pub" || (What == "inn" && Poi.Num("conf", 1.0) >= 0.75) ? StopPub
			: What == "layby" ? StopLayby : What == "inn" && Lodging(Poi.Str("name")) ? StopInn : What == "campsite" ? StopCamp : -1;
		if (Kind >= 0) Add(Kind, Poi.Str("name"), P2(Poi.Num("x"), Poi.Num("z")));
	}
	// [OSM stage 1] The castles: further off the road is still worth the stop (one seen from it).
	for (const TorqueFit::Json& Lm : G.Landmarks)
		if (Lm.Str("kind") == "castle" && Region::LandmarkName(Lm.Str("name"))) Add(StopCastle, Lm.Str("name"), P2(Lm.Num("x"), Lm.Num("z")), 600.0);
	for (const FeaturePoint& P : G.Scenery.Points)
		if (P.Kind == "viewpoint") Add(StopView, P.Name, P.At);
	std::sort(Stops.begin(), Stops.end(), [](const RideStop& A, const RideStop& B) { return A.Along < B.Along; });
}

void RidePlan::FindThinData()
{
	Thin.clear();
	if (Road.size() < 2 || !Land) return;
	const Region& G = Area.Roads;
	const Frame F(G.FrameLat, G.FrameLon);
	const double End = FinishM > 0.0 ? FinishM : Length;
	if (!Land->HasTrees())
	{
		ThinSpot T;
		T.At = F.ToGeo(Road[0].At.X, Road[0].At.Z);
		T.Why = "No tree heights here: the trees are guessed from the land";
		Thin.push_back(T);
	}
	// A town's ground with few buildings: in each kilometre, the land 100 m either side of the road that
	// the survey calls built on, against the buildings the map has within 120 m.
	const auto Buildings = [this, &G](const P2& At)
	{
		int N = 0;
		if (!Scene) return N;
		const long long Cx = static_cast<long long>(std::floor(At.X / 256.0)), Cz = static_cast<long long>(std::floor(At.Z / 256.0));
		for (long long Dz = -1; Dz <= 1; ++Dz)
			for (long long Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find((Cx + Dx) * 4000037LL + (Cz + Dz));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
					if (std::hypot(G.Buildings[size_t(B)].X - At.X, G.Buildings[size_t(B)].Z - At.Z) < 120.0) ++N;
			}
		return N;
	};
	double ThinFrom = -1.0;
	for (double From = 0.0; From < End; From += 1000.0)
	{
		int Urban = 0, Samples = 0, Built = 0;
		size_t I = 0;
		while (I + 1 < Road.size() && Road[I].Along < From) ++I;
		for (; I < Road.size() && Road[I].Along < From + 1000.0; I += 12)   // (every 50 m or so)
		{
			const P2 P = Road[I].At;
			const P2 D = Road[std::min(I + 1, Road.size() - 1)].At - Road[I > 0 ? I - 1 : 0].At;
			const double Len = D.Length();
			const P2 Across = Len > 1e-6 ? P2(D.Z / Len, -D.X / Len) : P2(0.0, 1.0);
			for (double Off : {-100.0, 100.0})
			{
				const P2 Side = P + Across * Off;
				++Samples;
				if (Land->Cover(Side.X, Side.Z) == 5) ++Urban;
			}
			Built += Buildings(P);
		}
		// (100 m off, so a dual carriageway's own width, which the survey calls built on, isn't counted)
		const bool IsThin = Samples >= 8 && Urban * 2 >= Samples && Built * 4 < Samples;
		if (IsThin && ThinFrom < 0.0) ThinFrom = From;
		if ((!IsThin || From + 1000.0 >= End) && ThinFrom >= 0.0)
		{
			size_t K = 0;
			while (K + 1 < Road.size() && Road[K].Along < ThinFrom + 500.0) ++K;
			ThinSpot T;
			T.Along = ThinFrom;
			T.At = F.ToGeo(Road[K].At.X, Road[K].At.Z);
			T.Why = "A town with few of its buildings on the map: it may look bare";
			if (Thin.size() < 12) Thin.push_back(T);
			ThinFrom = -1.0;
		}
	}
}

RidePlan::RideNav RidePlan::NavAt(double X, double Z, double Was) const
{
	RideNav N;
	NearOpts Opts;
	Opts.MainOnly = true;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit))
	{
		N.OffRoute = true;
		N.Along = std::max(Was, 0.0);
	}
	else
	{
		N.Along = Hit.Along;
		N.OffRoute = Hit.Dist > Hit.Half + 40.0;
		// (where the road passes close by itself, keep to the stretch the rider was on)
		if (Was >= 0.0 && std::fabs(Hit.Along - Was) > 150.0 && Hit.Dist > Hit.Half) N.Along = Was;
	}
	N.ToFinish = std::max(FinishM - N.Along, 0.0);
	for (const RideTurn& T : Turns)
		if (T.Along > N.Along + 3.0 || T.Turn == Finish)
		{
			N.Turn = T.Turn;
			N.ToTurn = std::max(T.Along - N.Along, 0.0);
			N.Onto = T.Onto;
			N.Exit = T.Exit;
			break;
		}
	// [junction priority 168] (the next give-way or stop line across the ride's own lane, as its sign says)
	if (!N.OffRoute)
		for (const MappedMark& M : MainMapped)
		{
			if (M.Dir <= 0 || (M.Kind != "give_way" && M.Kind != "stop")) continue;
			const double To = M.Along - 1.0 - N.Along;
			if (To < -2.0 || To > GiveWayWarn || (N.ToGiveWay >= 0.0 && std::max(To, 0.0) >= N.ToGiveWay)) continue;
			N.ToGiveWay = std::max(To, 0.0);
			N.GiveWayStop = M.Kind == "stop";
		}
	for (const RideStop& S : Stops)
		if (S.Along > N.Along - 15.0 && N.ToStop[S.Kind] < 0.0 && S.Along < FinishM + 50.0)
		{
			N.ToStop[S.Kind] = std::max(S.Along - N.Along, 0.0);
			N.StopCalled[S.Kind] = S.Name;
		}
	// [OSM stage 1] The road's facts here.
	if (const LimitStretch* L = LimitAt(N.Along))
	{
		N.Limit = ShownLimit(L->Kmh, &N.LimitNational);
		N.LimitAssumed = L->Assumed;
	}
	N.LimitMph = Britain();
	if (const RoadFact* F = FactAt(N.Along)) N.Road = F->Name;
	N.Place = PlaceAt(X, Z);
	return N;
}
std::shared_ptr<const RideDetour> RidePlan::DetourFrom(double X, double Z, double Heading, double Was) const
{
	using namespace RideNavBits;
	const Region& G = Area.Roads;
	NearOpts Opts;
	NearHit Hit;
	if (PathNodes.empty() || !NearestOf(X, Z, Opts, Hit) || Hit.Which <= 0 || Hit.Dist > Hit.Half + 25.0) return nullptr;
	const SideRoad* On = SideOf(Hit.Which);
	if (On == nullptr || On->Edge < 0 || size_t(On->Edge) >= G.Edges.size()) return nullptr;
	const RoadEdge& Edge = G.Edges[size_t(On->Edge)];
	// Which end the rider is heading for: the road's own way at their point against their heading.
	const std::vector<RoadPoint>& Pts = On->Pts;
	const size_t I0 = size_t(std::max(0, std::min(Hit.Index, int(Pts.size()) - 2)));
	const P2 Way = Pts[I0 + 1].At - Pts[I0].At;
	const P2 Facing(-std::sin(Heading), -std::cos(Heading));
	const bool ToEnd = Way.X * Facing.X + Way.Z * Facing.Z >= 0.0;
	// (the laid road's ends are its edge's: A at its first point unless the edge was laid the other way)
	const bool AFirst = Edge.Line.front().DistanceTo(Pts.front().At) <= Edge.Line.back().DistanceTo(Pts.front().At);
	const int Start = ToEnd == AFirst ? Edge.B : Edge.A;
	const double FirstBit = ToEnd ? On->Length - Hit.Along : Hit.Along;
	// The ride's junctions ahead, by what each leaves to ride.
	std::unordered_map<int, size_t> Ahead;
	for (size_t K = 0; K < PathNodes.size(); ++K)
		if (PathNodes[K].Along > Was + 30.0 && Ahead.count(PathNodes[K].Node) == 0) Ahead[PathNodes[K].Node] = K;
	if (Ahead.empty()) return nullptr;
	// Out from the junction ahead by the map's roads (every road both ways), nearest first, until the
	// nearest left can't beat the best found.
	std::vector<double> Dist(G.Nodes.size(), 1e18);
	std::vector<int> Came(G.Nodes.size(), -1);
	using Reach = std::pair<double, int>;
	std::priority_queue<Reach, std::vector<Reach>, std::greater<Reach>> Next;
	Dist[size_t(Start)] = 0.0;
	Next.push({0.0, Start});
	double Best = 1e18;
	int BestNode = -1;
	while (!Next.empty())
	{
		const Reach Top = Next.top();
		Next.pop();
		if (Top.first > Dist[size_t(Top.second)]) continue;
		if (Top.first >= Best) break;
		const auto It = Ahead.find(Top.second);
		if (It != Ahead.end())
		{
			const double Left = Top.first + (FinishM - PathNodes[It->second].Along);
			if (Left < Best) { Best = Left; BestNode = Top.second; }
		}
		for (const int E : G.Adjacent[size_t(Top.second)])
		{
			const int Other = G.OtherEnd(E, Top.second);
			// (a farm track or a service road only where nothing else goes)
			const std::string& Class = G.ClassOf(E);
			const double D = Top.first + G.Edges[size_t(E)].Length * (Class == "track" || Class == "service" ? 4.0 : 1.0);
			if (D < Dist[size_t(Other)]) { Dist[size_t(Other)] = D; Came[size_t(Other)] = E; Next.push({D, Other}); }
		}
	}
	if (BestNode < 0) return nullptr;
	std::vector<int> Path;
	for (int N = BestNode; N != Start; N = G.OtherEnd(Came[size_t(N)], N)) Path.push_back(Came[size_t(N)]);
	std::reverse(Path.begin(), Path.end());
	std::shared_ptr<RideDetour> D = std::make_shared<RideDetour>();
	// Its line: from the rider along their road to the junction ahead, then the map's roads.
	{
		std::vector<P2> Line = {P2(X, Z)};
		if (ToEnd) { for (size_t K = I0 + 1; K < Pts.size(); ++K) Line.push_back(Pts[K].At); }
		else { for (size_t K = I0 + 1; K-- > 0;) Line.push_back(Pts[K].At); }
		const std::vector<P2> Rest = G.PathLine(Start, Path);
		for (const P2& P : Rest)
			if (Line.empty() || P.DistanceTo(Line.back()) > 0.5) Line.push_back(P);
		D->Line = Line;
		D->Along.assign(Line.size(), 0.0);
		for (size_t K = 1; K < Line.size(); ++K) D->Along[K] = D->Along[K - 1] + Line[K].DistanceTo(Line[K - 1]);
		D->Length = D->Along.back();
	}
	const PathNode& Rejoin = PathNodes[Ahead[BestNode]];
	D->RejoinAlong = Rejoin.Along;
	// Its turns: at each junction on the way, as the ride's own are found; the last onto the ride's road.
	std::vector<int> Edges = {On->Edge};
	Edges.insert(Edges.end(), Path.begin(), Path.end());
	if (Rejoin.Next >= 0) Edges.push_back(Rejoin.Next);
	int Node = Start;
	double Gone = FirstBit;
	std::vector<double> Bends;
	for (size_t K = 0; K + 1 < Edges.size(); ++K)
	{
		if (K > 0) { Gone += G.Edges[size_t(Edges[K])].Length; Node = G.OtherEnd(Edges[K], Node); }
		const int Came1 = Edges[K], Onto = Edges[K + 1];
		const bool Last = K + 2 == Edges.size() && Rejoin.Next >= 0;
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		const P2 Back = HeadingOut(G, Came1, Node);
		const P2 In(-Back.X, -Back.Z);
		const double Ours = TurnOf(In, HeadingOut(G, Onto, Node));
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came1 && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		if (!Last && (Here.size() < 3 || !(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (G.NameOf(Onto) != G.NameOf(Came1) && std::fabs(Ours) > 0.26)))) continue;
		// (a roundabout or a staggered crossroads: two turns close together are one)
		if (!D->Turns.empty() && Gone - D->Turns.back().Along < 30.0)
		{
			Bends.back() += Ours;
			D->Turns.back().Turn = TurnKind(Bends.back());
			D->Turns.back().Onto = G.NameOf(Onto);
			continue;
		}
		RideTurn T;
		T.Along = std::min(Gone, D->Length);
		T.Turn = TurnKind(Ours);
		T.Onto = G.NameOf(Onto);
		D->Turns.push_back(T);
		Bends.push_back(Ours);
	}
	if (D->Turns.empty() || D->Turns.back().Along < D->Length - 1.0)
	{
		RideTurn T;
		T.Along = D->Length;
		T.Turn = 0;
		T.Onto = G.NameOf(Rejoin.Next >= 0 ? Rejoin.Next : Path.empty() ? On->Edge : Path.back());
		D->Turns.push_back(T);
	}
	return D;
}

std::shared_ptr<const RideDetour> RidePlan::DetourVia(double X, double Z, double Heading, double Was, double ViaX, double ViaZ, const std::string& ViaName) const
{
	using namespace RideNavBits;
	const Region& G = Area.Roads;
	if (PathNodes.empty() || G.Nodes.empty()) return nullptr;
	const int ViaNode = G.NearestConnected(ViaX, ViaZ, 3000.0);
	if (ViaNode < 0) return nullptr;
	NearOpts Opts;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + 25.0) return nullptr;
	// Where it leaves from: the junction ahead of the rider, the road there, and the way to it.
	std::vector<P2> Line = {P2(X, Z)};
	int Start = -1, FirstEdge = -1;
	double FirstBit = 0.0, From = Was;
	if (Hit.Which > 0)
	{
		const SideRoad* On = SideOf(Hit.Which);
		if (On == nullptr || On->Edge < 0 || size_t(On->Edge) >= G.Edges.size()) return nullptr;
		const RoadEdge& Edge = G.Edges[size_t(On->Edge)];
		const std::vector<RoadPoint>& Pts = On->Pts;
		const size_t I0 = size_t(std::max(0, std::min(Hit.Index, int(Pts.size()) - 2)));
		const P2 Way = Pts[I0 + 1].At - Pts[I0].At;
		const P2 Facing(-std::sin(Heading), -std::cos(Heading));
		const bool ToEnd = Way.X * Facing.X + Way.Z * Facing.Z >= 0.0;
		const bool AFirst = Edge.Line.front().DistanceTo(Pts.front().At) <= Edge.Line.back().DistanceTo(Pts.front().At);
		Start = ToEnd == AFirst ? Edge.B : Edge.A;
		FirstBit = ToEnd ? On->Length - Hit.Along : Hit.Along;
		FirstEdge = On->Edge;
		if (ToEnd) { for (size_t K = I0 + 1; K < Pts.size(); ++K) Line.push_back(Pts[K].At); }
		else { for (size_t K = I0 + 1; K-- > 0;) Line.push_back(Pts[K].At); }
	}
	else
	{
		// On the ride's road: on along it to its next junction.
		From = std::max(Was, Hit.Along);
		size_t K = 0;
		while (K < PathNodes.size() && PathNodes[K].Along <= From + 5.0) ++K;
		if (K == 0 || K >= PathNodes.size()) return nullptr;
		Start = PathNodes[K].Node;
		FirstEdge = PathNodes[K - 1].Next;
		FirstBit = PathNodes[K].Along - From;
		for (const RoadPoint& P : Road)
			if (P.Along > From && P.Along < PathNodes[K].Along) Line.push_back(P.At);
		if (FirstEdge < 0) return nullptr;
	}
	if (Start < 0 || size_t(Start) >= G.Nodes.size()) return nullptr;
	// The map's roads, every one both ways (a farm track or a service road only where nothing else goes).
	using Reach = std::pair<double, int>;
	const auto Search = [&G](int Origin, const std::function<double(int, double)>& Done, std::vector<int>& Came) -> int
	{
		std::vector<double> Dist(G.Nodes.size(), 1e18);
		Came.assign(G.Nodes.size(), -1);
		std::priority_queue<Reach, std::vector<Reach>, std::greater<Reach>> Next;
		Dist[size_t(Origin)] = 0.0;
		Next.push({0.0, Origin});
		double Best = 1e18;
		int BestNode = -1;
		while (!Next.empty())
		{
			const Reach Top = Next.top();
			Next.pop();
			if (Top.first > Dist[size_t(Top.second)]) continue;
			if (Top.first >= Best) break;
			const double Score = Done(Top.second, Top.first);   // < 0: not an end
			if (Score >= 0.0 && Score < Best) { Best = Score; BestNode = Top.second; }
			for (const int E : G.Adjacent[size_t(Top.second)])
			{
				const int Other = G.OtherEnd(E, Top.second);
				const std::string& Class = G.ClassOf(E);
				const double Step = Top.first + G.Edges[size_t(E)].Length * (Class == "track" || Class == "service" ? 4.0 : 1.0);
				if (Step < Dist[size_t(Other)]) { Dist[size_t(Other)] = Step; Came[size_t(Other)] = E; Next.push({Step, Other}); }
			}
		}
		return BestNode;
	};
	const auto PathTo = [&G](int Origin, int End, const std::vector<int>& Came)
	{
		std::vector<int> Path;
		for (int N = End; N != Origin; N = G.OtherEnd(Came[size_t(N)], N)) Path.push_back(Came[size_t(N)]);
		std::reverse(Path.begin(), Path.end());
		return Path;
	};
	// There: the junction nearest the waypoint.
	std::vector<int> Came;
	if (Search(Start, [ViaNode](int N, double D) { return N == ViaNode ? D : -1.0; }, Came) != ViaNode) return nullptr;
	const std::vector<int> There = PathTo(Start, ViaNode, Came);
	// And back: the junction of the ride's road ahead that leaves least to ride to the finish.
	std::unordered_map<int, size_t> Ahead;
	for (size_t K = 0; K < PathNodes.size(); ++K)
		if (PathNodes[K].Along > From + 30.0 && Ahead.count(PathNodes[K].Node) == 0) Ahead[PathNodes[K].Node] = K;
	if (Ahead.empty()) return nullptr;
	const double FinishAt = FinishM;
	const int BackTo = Search(ViaNode, [&Ahead, this, FinishAt](int N, double D)
	{
		const auto It = Ahead.find(N);
		return It == Ahead.end() ? -1.0 : D + (FinishAt - PathNodes[It->second].Along);
	}, Came);
	if (BackTo < 0) return nullptr;
	const std::vector<int> Back = PathTo(ViaNode, BackTo, Came);
	std::shared_ptr<RideDetour> D = std::make_shared<RideDetour>();
	// Its line: to the junction ahead, there by the map's roads, and back.
	const auto Append = [&Line](const std::vector<P2>& More)
	{
		for (const P2& P : More)
			if (Line.empty() || P.DistanceTo(Line.back()) > 0.5) Line.push_back(P);
	};
	Append({G.Nodes[size_t(Start)]});
	Append(G.PathLine(Start, There));
	size_t ViaIndex = Line.size() - 1;
	Append(G.PathLine(ViaNode, Back));
	D->Line = Line;
	D->Along.assign(Line.size(), 0.0);
	for (size_t K = 1; K < Line.size(); ++K) D->Along[K] = D->Along[K - 1] + Line[K].DistanceTo(Line[K - 1]);
	D->Length = D->Along.back();
	ViaIndex = std::min(ViaIndex, Line.size() - 1);
	D->ViaAlong = D->Along[ViaIndex];
	D->Via = G.Nodes[size_t(ViaNode)];
	const PathNode& Rejoin = PathNodes[Ahead[BackTo]];
	D->RejoinAlong = Rejoin.Along;
	// Its turns, as DetourFrom finds them; the last onto the ride's road.
	std::vector<int> Edges = {FirstEdge};
	Edges.insert(Edges.end(), There.begin(), There.end());
	Edges.insert(Edges.end(), Back.begin(), Back.end());
	if (Rejoin.Next >= 0) Edges.push_back(Rejoin.Next);
	int Node = Start;
	double Gone = FirstBit;
	std::vector<double> Bends;
	for (size_t K = 0; K + 1 < Edges.size(); ++K)
	{
		if (K > 0) { Gone += G.Edges[size_t(Edges[K])].Length; Node = G.OtherEnd(Edges[K], Node); }
		const int Came1 = Edges[K], Onto = Edges[K + 1];
		const bool Last = K + 2 == Edges.size() && Rejoin.Next >= 0;
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		const P2 BackOut = HeadingOut(G, Came1, Node);
		const P2 In(-BackOut.X, -BackOut.Z);
		const double Ours = Came1 == Onto ? 3.14159265 : TurnOf(In, HeadingOut(G, Onto, Node));   // (the same road back: turn round)
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came1 && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		if (!Last && Came1 != Onto && (Here.size() < 3 || !(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (G.NameOf(Onto) != G.NameOf(Came1) && std::fabs(Ours) > 0.26)))) continue;
		if (!D->Turns.empty() && Gone - D->Turns.back().Along < 30.0)
		{
			Bends.back() += Ours;
			D->Turns.back().Turn = TurnKind(Bends.back());
			D->Turns.back().Onto = G.NameOf(Onto);
			continue;
		}
		RideTurn T;
		T.Along = std::min(Gone, D->Length);
		T.Turn = TurnKind(Ours);
		T.Onto = G.NameOf(Onto);
		D->Turns.push_back(T);
		Bends.push_back(Ours);
	}
	// The waypoint: an arrival there, as at the finish, named.
	{
		RideTurn T;
		T.Along = D->ViaAlong;
		T.Turn = Finish;
		T.Onto = ViaName;
		const auto At = std::lower_bound(D->Turns.begin(), D->Turns.end(), T, [](const RideTurn& A, const RideTurn& B) { return A.Along < B.Along; });
		D->Turns.insert(At, T);
	}
	if (D->Turns.back().Along < D->Length - 1.0)
	{
		RideTurn T;
		T.Along = D->Length;
		T.Turn = 0;
		T.Onto = G.NameOf(Rejoin.Next >= 0 ? Rejoin.Next : Back.empty() ? FirstEdge : Back.back());
		D->Turns.push_back(T);
	}
	return D;
}

RidePlan::RideNav RidePlan::NavDetour(const RideDetour& D, double X, double Z) const
{
	RideNav N;
	double Best = 1e18, At = 0.0;
	for (size_t K = 0; K + 1 < D.Line.size(); ++K)
	{
		const P2 A = D.Line[K], Ab = D.Line[K + 1] - A;
		const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
		const double T = L2 > 1e-9 ? std::clamp(((X - A.X) * Ab.X + (Z - A.Z) * Ab.Z) / L2, 0.0, 1.0) : 0.0;
		const double Dx = A.X + Ab.X * T - X, Dz = A.Z + Ab.Z * T - Z;
		const double D2 = Dx * Dx + Dz * Dz;
		if (D2 < Best) { Best = D2; At = D.Along[K] + (D.Along[K + 1] - D.Along[K]) * T; }
	}
	N.OffRoute = std::sqrt(Best) > 40.0;
	N.Along = At;
	N.ToFinish = std::max(D.Length - At, 0.0) + std::max(FinishM - D.RejoinAlong, 0.0);
	N.Turn = Finish;
	for (const RideTurn& T : D.Turns)
		if (T.Along > At + 3.0)
		{
			N.Turn = T.Turn;
			N.ToTurn = T.Along - At;
			N.Onto = T.Onto;
			break;
		}
	NearOpts Opts;
	NearHit Hit;
	if (NearestOf(X, Z, Opts, Hit) && Hit.Which > 0)
		if (const SideRoad* On = SideOf(Hit.Which)) N.Road = Area.Roads.NameOf(On->Edge);
	N.LimitMph = Britain();
	N.Place = PlaceAt(X, Z);
	return N;
}
} // namespace TorqueWorld
