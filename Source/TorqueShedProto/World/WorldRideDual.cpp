// [OSM stage 5C] Dual carriageways: a road the map marks as one half of a pair (RoadExtra::Half, from
// the roadx bake) finds its other half across from it, and the ground between them is laid as a central
// reservation: a grass strip from the one's edge to the other's, a little under both, so the land
// never rises through either. Its barrier is drawn with the road (RoadPiece).
#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace
{
double DotOf(const P2& A, const P2& B) { return A.X * B.X + A.Z * B.Z; }

// A road's heading at its point I (unit, as its points run).
P2 HeadingAt(const std::vector<RoadPoint>& Pts, int I)
{
	const int N = int(Pts.size());
	if (N < 2) return P2(0.0, -1.0);
	const P2 D = Pts[size_t(std::min(N - 1, I + 1))].At - Pts[size_t(std::max(0, I - 1))].At;
	const double L = D.Length();
	return L > 1e-9 ? D * (1.0 / L) : P2(0.0, -1.0);
}
} // namespace

bool RidePlan::DualAcross(int Which, int Index, int OnlySide, DualHit& Out) const
{
	Out = DualHit();
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	// (one way, and one of the two the map's half of a pair: a town's one-way streets side by side are not)
	if (Index < 0 || Index >= int(Pts.size()) || !Pts[size_t(Index)].OneWay) return false;
	const RoadPoint& P = Pts[size_t(Index)];
	const P2 Dir = HeadingAt(Pts, Index);
	const P2 Left(Dir.Z, -Dir.X);
	double BestGap = 1e9;
	for (int Side : {1, -1})
	{
		if (OnlySide != 0 && Side != OnlySide) continue;
		// (a little way out on this side: the nearest road there other than this one)
		const P2 Probe = P.At + Left * (double(Side) * (P.Half + 1.5));
		NearOpts Opts;
		Opts.NotRoad = Which;
		NearHit Hit;
		bool Got = NearestOf(Probe.X, Probe.Z, Opts, Hit);
		// (or this same road further along: the ride's road can come back down the other half)
		NearOpts Same;
		Same.OnlyRoad = Which;
		NearHit Back;
		if (NearestOf(Probe.X, Probe.Z, Same, Back) && std::fabs(Back.Along - P.Along) > 150.0 && (!Got || Back.Dist - Back.Half < Hit.Dist - Hit.Half))
		{
			Hit = Back;
			Got = true;
		}
		if (!Got) continue;
		const std::vector<RoadPoint>& Other = RoadOf(Hit.Which);
		// (a one-way road: the map may name the pair from one half only)
		if (Hit.Index < 0 || Hit.Index >= int(Other.size()) || !Other[size_t(Hit.Index)].OneWay || Hit.Deck != P.Bridge) continue;
		if (!P.Dual && !Other[size_t(Hit.Index)].Dual) continue;
		// (alongside: running the same way or the other, near enough)
		if (std::fabs(DotOf(HeadingAt(Other, Hit.Index), Dir)) < 0.8) continue;
		const double Apart = DotOf(Hit.Foot - P.At, Left) * double(Side);
		const double Gap = Apart - P.Half - Hit.Half;
		if (Gap < 0.3 || Gap > MedianWidest || Gap >= BestGap) continue;
		// (the other half's surface at its edge on this side: the probe's side of it)
		const double FarSide = Hit.Off >= 0.0 ? 1.0 : -1.0;
		const double FarY = Hit.Height + RoadLine::CrossHeight(FarSide * Hit.Half, Hit.Half, Other[size_t(Hit.Index)].Bank);
		BestGap = Gap;
		Out.Side = Side;
		Out.Which = Hit.Which;
		Out.Gap = Gap;
		Out.Near = P.Height + RoadLine::CrossHeight(double(Side) * P.Half, P.Half, P.Bank);
		Out.Far = FarY;
		Out.FarAlong = Hit.Along;
	}
	return Out.Which >= 0;
}

double RidePlan::MedianGround(const NearHit& Hit) const
{
	const double Nan = std::nan("");
	if (Hit.Index < 0 || Hit.Dist <= Hit.Half || Hit.Deck || Hit.Bore) return Nan;
	const std::vector<RoadPoint>& Pts = RoadOf(Hit.Which);
	if (Hit.Index >= int(Pts.size()) || !Pts[size_t(Hit.Index)].OneWay) return Nan;
	DualHit D;
	if (!DualAcross(Hit.Which, Hit.Index, Hit.Off >= 0.0 ? 1 : -1, D)) return Nan;
	const double Out = Hit.Dist - Hit.Half;
	if (Out > D.Gap) return Nan;
	// (this half's edge where the point is across from it, not at its nearest knot)
	const double Bank = Pts[size_t(Hit.Index)].Bank;
	D.Near = Hit.Height + RoadLine::CrossHeight(Hit.Off >= 0.0 ? Hit.Half : -Hit.Half, Hit.Half, Bank);
	return MedianAcross(D, Out);
}

double RidePlan::MedianAcross(const DualHit& D, double Out)
{
	// Straight across from the one's edge to the other's, a little under both; and as each road's own
	// verge is, level with its edge for MedianPad before it slopes: never over either edge near it, though
	// one half lies higher than the other.
	const double T = D.Gap > 1e-6 ? std::clamp(Out / D.Gap, 0.0, 1.0) : 0.5;
	const auto Rise = [](double From) { return std::fmax(0.0, From - MedianPad) * 0.6; };
	const double Line = D.Near + (D.Far - D.Near) * T;
	return std::fmin(Line, std::fmin(D.Near + Rise(Out), D.Far + Rise(D.Gap - Out))) - MedianSink;
}
} // namespace TorqueWorld
