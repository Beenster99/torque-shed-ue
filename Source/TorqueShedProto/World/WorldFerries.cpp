#include "WorldFerries.h"

#include "FitJson.h"
#include "WorldRegion.h"
#include "WorldRide.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TorqueWorld
{
namespace Ferries
{
using TorqueFit::Json;

namespace
{
double Dist(const P2& A, const P2& B)
{
	return std::hypot(A.X - B.X, A.Z - B.Z);
}

P2 Unit(const P2& V)
{
	const double L = V.Length();
	return L > 1e-9 ? P2(V.X / L, V.Z / L) : P2(1.0, 0.0);
}

double LineLength(const std::vector<P2>& Line)
{
	double L = 0.0;
	for (size_t I = 1; I < Line.size(); ++I) L += Dist(Line[I - 1], Line[I]);
	return L;
}

// The pieces of one route joined end to end: each time the piece whose nearer end is nearest either end of what's
// joined so far goes on that end (turned round if need be). Pieces cut at a cell's edge share the point they were cut
// at; a long route's open sea, where no cells were baked, comes as a gap between them.
std::vector<P2> Join(std::vector<std::vector<P2>> Pieces)
{
	std::vector<P2> Out;
	if (Pieces.empty()) return Out;
	Out = Pieces.front();
	Pieces.erase(Pieces.begin());
	while (!Pieces.empty())
	{
		size_t Best = 0;
		bool Rev = false, AtEnd = true;
		double BestD = std::numeric_limits<double>::max();
		for (size_t K = 0; K < Pieces.size(); ++K)
		{
			const std::vector<P2>& P = Pieces[K];
			if (P.empty()) continue;
			const double Cand[4] = {Dist(Out.back(), P.front()), Dist(Out.back(), P.back()), Dist(P.back(), Out.front()), Dist(P.front(), Out.front())};
			for (int C = 0; C < 4; ++C)
				if (Cand[C] < BestD)
				{
					BestD = Cand[C];
					Best = K;
					AtEnd = C < 2;
					Rev = C == 1 || C == 3;
				}
		}
		std::vector<P2> P = Pieces[Best];
		Pieces.erase(Pieces.begin() + long(Best));
		if (Rev) std::reverse(P.begin(), P.end());
		if (AtEnd)
		{
			if (!P.empty() && Dist(Out.back(), P.front()) < 0.5) P.erase(P.begin());
			Out.insert(Out.end(), P.begin(), P.end());
		}
		else
		{
			if (!P.empty() && Dist(P.back(), Out.front()) < 0.5) P.pop_back();
			Out.insert(Out.begin(), P.begin(), P.end());
		}
	}
	return Out;
}

// A point Along metres along Line from its start (or its end, FromEnd), and the line's way there (towards the line's
// other end).
P2 AlongLine(const std::vector<P2>& Line, double Along, bool FromEnd, P2* Way = nullptr)
{
	const size_t N = Line.size();
	auto Pt = [&](size_t I) { return FromEnd ? Line[N - 1 - I] : Line[I]; };
	double Left = Along;
	for (size_t I = 1; I < N; ++I)
	{
		const P2 A = Pt(I - 1), B = Pt(I);
		const double D = Dist(A, B);
		if (D >= Left || I == N - 1)
		{
			if (Way != nullptr) *Way = Unit(B - A);
			const double T = D > 1e-9 ? std::min(1.0, Left / D) : 0.0;
			return A + (B - A) * T;
		}
		Left -= D;
	}
	if (Way != nullptr) *Way = P2(1.0, 0.0);
	return N > 0 ? Pt(0) : P2();
}

P2 Bezier(const P2& P0, const P2& P1, const P2& P2_, const P2& P3, double T, P2* Tangent)
{
	const double U = 1.0 - T;
	if (Tangent != nullptr)
		*Tangent = Unit((P1 - P0) * (3.0 * U * U) + (P2_ - P1) * (6.0 * U * T) + (P3 - P2_) * (3.0 * T * T));
	return P0 * (U * U * U) + P1 * (3.0 * U * U * T) + P2_ * (3.0 * U * T * T) + P3 * (T * T * T);
}

double CurveLength(const P2& P0, const P2& P1, const P2& P2_, const P2& P3)
{
	double L = 0.0;
	P2 Was = P0;
	for (int K = 1; K <= 64; ++K)
	{
		const P2 Now = Bezier(P0, P1, P2_, P3, K / 64.0, nullptr);
		L += Dist(Was, Now);
		Was = Now;
	}
	return L;
}

// The two legs a sailing runs (end 0's berth out, and in to end 1's): one curve for a short route, its control
// points along each end's slipway; a long route's leg out runs LongLeg m out along end 0's and its leg in the same
// in along end 1's.
void Legs(const Layout& L, bool Long, P2 (&Out)[2][4])
{
	const End& A = L.Ends[0];
	const End& B = L.Ends[1];
	if (!Long)
	{
		const double K = std::max(20.0, Dist(A.Berth, B.Berth) / 3.0);
		Out[0][0] = A.Berth;
		Out[0][1] = A.Berth + A.Axis * K;
		Out[0][2] = B.Berth + B.Axis * K;
		Out[0][3] = B.Berth;
		for (int I = 0; I < 4; ++I) Out[1][I] = Out[0][I];
		return;
	}
	const double K = LongLeg / 3.0;
	Out[0][0] = A.Berth;
	Out[0][1] = A.Berth + A.Axis * K;
	Out[0][2] = A.Berth + A.Axis * (2.0 * K);
	Out[0][3] = A.Berth + A.Axis * LongLeg;
	Out[1][0] = B.Berth + B.Axis * LongLeg;
	Out[1][1] = B.Berth + B.Axis * (2.0 * K);
	Out[1][2] = B.Berth + B.Axis * K;
	Out[1][3] = B.Berth;
}
} // namespace

const char* KindName(EKind K)
{
	return K == EKind::Chain ? "chain" : K == EKind::Turntable ? "turntable" : "open";
}

bool ReadList(const std::string& Text, std::vector<Listed>& Out)
{
	Out.clear();
	Json Root;
	if (!Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const Json* Rows = Root.Get("crossings");
	if (Rows == nullptr || !Rows->IsArray()) return false;
	for (const Json& R : Rows->A)
	{
		if (!R.IsObject()) continue;
		const Json* A = R.Get("a");
		const Json* B = R.Get("b");
		if (A == nullptr || B == nullptr || A->Size() < 2 || B->Size() < 2) continue;
		Listed L;
		L.Id = R.Str("id");
		L.Name = R.Str("name");
		L.MapName = R.Str("map_name");
		L.Country = R.Str("country");
		L.LatA = A->A[0].N;
		L.LonA = A->A[1].N;
		L.LatB = B->A[0].N;
		L.LonB = B->A[1].N;
		if (const Json* P = R.Get("places"))
			for (size_t K = 0; K < 2 && K < P->Size(); ++K) L.Places[K] = P->A[K].S;
		const std::string Kind = R.Str("kind", "open");
		L.Kind = Kind == "chain" ? EKind::Chain : Kind == "turntable" ? EKind::Turntable : EKind::Open;
		L.Metres = R.Num("m");
		Out.push_back(std::move(L));
	}
	return !Out.empty();
}

std::vector<Crossing> InArea(const std::vector<Listed>& List, const std::vector<FerryLine>& Lines, const Frame& F, const Rect& Box)
{
	std::vector<Crossing> Out;
	auto Inside = [&Box](const P2& P) { return P.X >= Box.X0 && P.X <= Box.X1 && P.Z >= Box.Z0 && P.Z <= Box.Z1; };
	for (size_t I = 0; I < List.size(); ++I)
	{
		const Listed& L = List[I];
		const P2 A = F.FromGeo(L.LatA, L.LonA), B = F.FromGeo(L.LatB, L.LonB);
		if (!Inside(A) || !Inside(B)) continue;
		Crossing C;
		C.Listed = int(I);
		C.Id = L.Id;
		C.Name = L.Name;
		C.Kind = L.Kind;
		// Its pieces in the map by id; else any ferry line with an end near each of the listed ends.
		std::vector<std::vector<P2>> Pieces;
		for (const FerryLine& Fl : Lines)
			if (Fl.Id == L.Id && Fl.Line.size() >= 2) Pieces.push_back(Fl.Line);
		if (Pieces.empty())
			for (const FerryLine& Fl : Lines)
			{
				if (Fl.Line.size() < 2) continue;
				const double Near = 400.0;
				const bool Fwd = Dist(Fl.Line.front(), A) < Near && Dist(Fl.Line.back(), B) < Near;
				const bool Back = Dist(Fl.Line.front(), B) < Near && Dist(Fl.Line.back(), A) < Near;
				if (Fwd || Back)
				{
					Pieces.push_back(Fl.Line);
					break;
				}
			}
		if (!Pieces.empty())
		{
			C.Line = Join(Pieces);
			C.FromMap = true;
			// End 0 is the listed a end.
			if (Dist(C.Line.front(), A) + Dist(C.Line.back(), B) > Dist(C.Line.front(), B) + Dist(C.Line.back(), A)) std::reverse(C.Line.begin(), C.Line.end());
		}
		else
			C.Line = {A, B};
		C.Length = LineLength(C.Line);
		C.Long = C.Length >= LongFrom;
		Out.push_back(std::move(C));
	}
	return Out;
}

std::vector<Crossing> InPlan(const RidePlan& Plan, const std::vector<Listed>& List)
{
	const Region& G = Plan.Area.Roads;
	std::vector<FerryLine> Lines;
	for (const FeatureLine& L : G.Scenery.Lines)
		if (L.Kind == "ferry") Lines.push_back({L.Id, L.Line});
	Rect Box;
	Box.X0 = Box.Z0 = std::numeric_limits<double>::max();
	Box.X1 = Box.Z1 = -std::numeric_limits<double>::max();
	for (const P2& N : G.Nodes)
	{
		Box.X0 = std::min(Box.X0, N.X);
		Box.X1 = std::max(Box.X1, N.X);
		Box.Z0 = std::min(Box.Z0, N.Z);
		Box.Z1 = std::max(Box.Z1, N.Z);
	}
	return InArea(List, Lines, G.FrameOf(), Box);
}

WorldAsk AskOf(const RidePlan& Plan)
{
	WorldAsk Ask;
	const RidePlan* P = &Plan;
	Ask.Height = [P](double X, double Z) { return P->Ground(X, Z); };
	Ask.Sea = [P](double X, double Z) { return P->Land && P->Land->SeaAt(X, Z); };
	Ask.Road = [P](const P2& At, double Reach, double WantY, P2& Foot, double& Y, double& Half)
	{
		// The best point of any of the area's roads (their lines, laid or not): the nearest, unless the way from it to
		// At would be steeper than 1 in 7 (a road up on a bank over the slipway): then the one whose climb is least
		// over what 1 in 7 allows, each metre too much counted as 8 m further.
		const Region& G = P->Area.Roads;
		double Best = 1e18;
		bool Found = false;
		auto HeightAt = [P](const P2& Q)
		{
			double H = P->RoadHeight(Q.X, Q.Z);
			if (std::isnan(H)) H = P->Ground(Q.X, Q.Z);
			return H;
		};
		auto Try = [&](const P2& Q)
		{
			const double Dq = Dist(Q, At);
			if (Dq > Reach || Dq > Best) return;
			const double H = HeightAt(Q);
			if (std::isnan(H)) return;
			const double Cost = Dq + 8.0 * std::max(0.0, std::fabs(H - WantY) - Dq / 7.0);
			if (Cost < Best)
			{
				Best = Cost;
				Foot = Q;
				Y = H;
				Found = true;
			}
		};
		for (const RoadEdge& E : G.Edges)
		{
			if (E.Line.size() < 2) continue;
			for (size_t I = 1; I < E.Line.size(); ++I)
			{
				const P2 A = E.Line[I - 1], B = E.Line[I];
				// (a quick box test first: most of the map's roads are nowhere near)
				if (std::min(A.X, B.X) > At.X + Reach || std::max(A.X, B.X) < At.X - Reach || std::min(A.Z, B.Z) > At.Z + Reach || std::max(A.Z, B.Z) < At.Z - Reach) continue;
				const P2 D = B - A;
				const double Len = std::sqrt(D.X * D.X + D.Z * D.Z);
				if (Len < 1e-6) continue;
				const double T = std::clamp(((At.X - A.X) * D.X + (At.Z - A.Z) * D.Z) / (Len * Len), 0.0, 1.0);
				Try(A + D * T);
				// (and every 10 m along it, for a gentler way down from a road on a bank)
				for (double S = 0.0; S < Len; S += 10.0) Try(A + D * (S / Len));
			}
		}
		if (!Found) return false;
		Half = 2.5;
		RidePlan::NearOpts Opts;
		RidePlan::NearHit Hit;
		if (P->NearestOf(Foot.X, Foot.Z, Opts, Hit) && Hit.Dist < 3.0) Half = Hit.Half;
		return true;
	};
	return Ask;
}

bool Lay(const Crossing& C, const WorldAsk& Ask, Layout& Out)
{
	Out = Layout();
	if (C.Line.size() < 2 || C.Length < 30.0)
	{
		Out.Why = "the route is too short";
		return false;
	}
	// The water's level: the sea's 0, or a loch's (the lowest ground along the route's middle, as the ride's water
	// lies level at the lowest wet ground round it).
	const P2 Mid = AlongLine(C.Line, C.Length * 0.5, false);
	Out.Sea = Ask.Sea && Ask.Sea(Mid.X, Mid.Z);
	if (Out.Sea)
		Out.Water = 0.0;
	else
	{
		double Low = std::numeric_limits<double>::max();
		for (int K = 1; K < 10; ++K)
		{
			const P2 P = AlongLine(C.Line, C.Length * (0.2 + 0.6 * K / 10.0), false);
			const double H = Ask.Height(P.X, P.Z);
			if (!std::isnan(H)) Low = std::min(Low, H);
		}
		if (Low > 1e8)
		{
			Out.Why = "no ground under the route yet";
			return false;
		}
		Out.Water = Low + 0.1;
	}
	const double W = Out.Water;
	for (int E = 0; E < 2; ++E)
	{
		End& S = Out.Ends[E];
		const bool FromEnd = E == 1;
		P2 Way;
		const P2 At = AlongLine(C.Line, 0.0, FromEnd);
		AlongLine(C.Line, std::min(40.0, C.Length * 0.3), FromEnd, &Way);
		const P2 Far = AlongLine(C.Line, std::min(40.0, C.Length * 0.3), FromEnd);
		S.Axis = Unit(Far - At);
		(void)Way;
		// The water's edge along the route: the land (over the water by half a metre) meeting the water, nearest
		// the route's end, looked for back up the land and out over the water.
		const double Wet = W + 0.5;
		bool Found = false, Unknown = false;
		double ShoreD = 0.0, BestOff = std::numeric_limits<double>::max();
		double WasH = std::numeric_limits<double>::quiet_NaN();
		for (double D = -ShoreBack; D <= ShoreOut; D += 1.0)
		{
			const P2 P = At + S.Axis * D;
			const double H = Ask.Height(P.X, P.Z);
			if (std::isnan(H))
			{
				Unknown = true;
				WasH = H;
				continue;
			}
			if (!std::isnan(WasH) && WasH > Wet && H <= Wet && std::fabs(D) < BestOff)
			{
				BestOff = std::fabs(D);
				ShoreD = D;
				Found = true;
			}
			WasH = H;
		}
		if (!Found)
		{
			Out.Why = Unknown ? "no ground at an end yet" : "no water's edge found at an end";
			return false;
		}
		S.Shore = At + S.Axis * ShoreD;
		// The slipway: from its head on the land down past the water's edge, steady, at a slope between 1 in 12 and
		// 1 in 6 that brings it to the ramp's toe a couple of metres out.
		S.Head = S.Shore - S.Axis * SlipHeadIn;
		const double HeadGround = Ask.Height(S.Head.X, S.Head.Z);
		S.HeadY = std::max(std::isnan(HeadGround) ? W + 1.0 : HeadGround + 0.06, W + 0.7);
		const double ToeD0 = SlipHeadIn + 2.0;
		S.Grade = std::clamp((S.HeadY - (W + ToeOverWater)) / ToeD0, SlipGentlest, SlipSteepest);
		double ToeD = (S.HeadY - (W + ToeOverWater)) / S.Grade;
		// Never under the land: where the shore's ground stands over the slipway's line (a beach that is convex, a
		// bank), its head is lifted until the slipway lies on it or over it all the way across (its sides as well
		// as its middle), its grade kept, its toe further out.
		const P2 Across(-S.Axis.Z, S.Axis.X);
		for (int Try = 0; Try < 4; ++Try)
		{
			double Lift = 0.0;
			for (double D = 0.0; D <= ToeD; D += 0.5)
				for (double Side = -SlipHalfW; Side <= SlipHalfW + 1e-6; Side += SlipHalfW)
				{
					const P2 P = S.Head + S.Axis * D + Across * Side;
					const double H = Ask.Height(P.X, P.Z);
					if (!std::isnan(H)) Lift = std::max(Lift, H + 0.04 - (S.HeadY - S.Grade * D));
				}
			if (Lift < 0.01) break;
			S.HeadY += Lift;
			ToeD = (S.HeadY - (W + ToeOverWater)) / S.Grade;
		}
		S.Tip = S.Head + S.Axis * ToeD;
		S.TipY = W + ToeOverWater;
		S.Foot = S.Tip + S.Axis * SlipPastToe;
		S.FootY = S.TipY - S.Grade * SlipPastToe;
		// The boat's ramp from its deck's end down to the toe, and the boat's middle half its deck on.
		const double Drop = W + DeckY - S.TipY;
		const double Run = std::sqrt(std::max(0.5, RampLen * RampLen - Drop * Drop));
		S.RampDown = std::atan2(Drop, Run);
		S.DeckEnd = S.Tip + S.Axis * Run;
		S.Berth = S.DeckEnd + S.Axis * HalfDeck;
		// The approach from the map's nearest road, where it doesn't come to the head itself.
		P2 RoadAt;
		double RoadY = 0.0, Half = 2.5;
		if (Ask.Road && Ask.Road(S.Head, RoadReach, S.HeadY, RoadAt, RoadY, Half))
		{
			S.Road = RoadAt;
			S.RoadY = RoadY;
			const double Gap = Dist(RoadAt, S.Head);
			if (Gap > Half + 1.0)
			{
				// From just inside the road's edge to the head, on the ground a little proud of it, its ends at the
				// road's height and the head's, no steeper than 1 in 7 (eased twice).
				const P2 Dir = Unit(S.Head - RoadAt);
				const P2 From = RoadAt + Dir * std::max(0.0, Half - 0.6);
				const double Len = Dist(From, S.Head);
				const int N = std::max(2, int(std::ceil(Len / 2.0)));
				std::vector<double> Ys(size_t(N) + 1);
				for (int K = 0; K <= N; ++K)
				{
					const P2 P = From + (S.Head - From) * (double(K) / N);
					const double H = Ask.Height(P.X, P.Z);
					Ys[size_t(K)] = std::isnan(H) ? RoadY + (S.HeadY - RoadY) * K / N : H + 0.08;
				}
				Ys.front() = RoadY + 0.01;
				Ys.back() = S.HeadY;
				for (int Pass = 0; Pass < 2; ++Pass)
					for (int K = 1; K < N; ++K) Ys[size_t(K)] = (Ys[size_t(K) - 1] + Ys[size_t(K)] * 2.0 + Ys[size_t(K) + 1]) * 0.25;
				// No steeper than 1 in 7 from either end: each point kept between what both ends allow, or, where the
				// two ends are further apart in height than 1 in 7 allows, a straight grade between them.
				const double Most = Len / N / 7.0;
				const bool Fits = std::fabs(S.HeadY - Ys.front()) <= Most * N;
				for (int K = 1; K < N; ++K)
				{
					const double Up = std::min(Ys.front() + Most * K, S.HeadY + Most * (N - K));
					const double Down = std::max(Ys.front() - Most * K, S.HeadY - Most * (N - K));
					Ys[size_t(K)] = Fits ? std::clamp(Ys[size_t(K)], Down, Up) : Ys.front() + (S.HeadY - Ys.front()) * K / N;
				}
				for (int K = 0; K <= N; ++K)
				{
					const P2 P = From + (S.Head - From) * (double(K) / N);
					S.Approach.push_back({P.X, Ys[size_t(K)], P.Z});
				}
			}
		}
	}
	// The sailing: its curve's length and how long it takes.
	P2 Leg[2][4];
	Legs(Out, C.Long, Leg);
	Out.PathLength = CurveLength(Leg[0][0], Leg[0][1], Leg[0][2], Leg[0][3]) + (C.Long ? CurveLength(Leg[1][0], Leg[1][1], Leg[1][2], Leg[1][3]) : 0.0);
	if (Dist(Out.Ends[0].Berth, Out.Ends[1].Berth) < HalfDeck * 2.5)
	{
		Out.Why = "the two berths are too close";
		return false;
	}
	Out.Seconds = SailSeconds(Out.PathLength, C.Long);
	Out.Ok = true;
	return true;
}

double SailSeconds(double PathM, bool Long)
{
	// Short: a minute for a few hundred metres, up to two and a half for a couple of kilometres. Long: the two legs and
	// the hold, about two minutes.
	if (Long) return 120.0;
	return std::clamp(48.0 + PathM * 0.045, 60.0, 160.0);
}

double Progress(double T)
{
	// Speeding up over the first fifth, steady, slowing over the last fifth.
	const double A = 0.2;
	T = std::clamp(T, 0.0, 1.0);
	const double V = 1.0 / (1.0 - A);
	if (T < A) return V * T * T / (2.0 * A);
	if (T > 1.0 - A)
	{
		const double U = 1.0 - T;
		return 1.0 - V * U * U / (2.0 * A);
	}
	return V * (T - A * 0.5);
}

void Sail(const Layout& L, bool Long, double S, P2& Pos, P2& Bow)
{
	P2 Leg[2][4];
	Legs(L, Long, Leg);
	S = std::clamp(S, 0.0, 1.0);
	if (!Long)
	{
		// (an even pace along the curve: S is a share of its length)
		const double Want = S * L.PathLength;
		double Got = 0.0;
		P2 Was = Leg[0][0];
		double T = 0.0;
		for (int K = 1; K <= 128; ++K)
		{
			const double Tk = K / 128.0;
			const P2 Now = Bezier(Leg[0][0], Leg[0][1], Leg[0][2], Leg[0][3], Tk, nullptr);
			const double D = Dist(Was, Now);
			if (Got + D >= Want)
			{
				T = (Tk - 1.0 / 128.0) + (D > 1e-9 ? (Want - Got) / D : 0.0) / 128.0;
				break;
			}
			Got += D;
			Was = Now;
			T = Tk;
		}
		P2 Tangent;
		Pos = Bezier(Leg[0][0], Leg[0][1], Leg[0][2], Leg[0][3], T, &Tangent);
		Bow = Tangent;
		return;
	}
	// A long route: out from end 0 (bow first), the hold, in to end 1. The legs run straight along each slipway.
	if (S <= HoldFrom)
	{
		Pos = Bezier(Leg[0][0], Leg[0][1], Leg[0][2], Leg[0][3], S / HoldFrom, &Bow);
		return;
	}
	if (S < HoldTo)
	{
		Pos = Leg[0][3];
		Bow = L.Ends[0].Axis;
		return;
	}
	Pos = Bezier(Leg[1][0], Leg[1][1], Leg[1][2], Leg[1][3], (S - HoldTo) / (1.0 - HoldTo), &Bow);
}
} // namespace Ferries
} // namespace TorqueWorld
