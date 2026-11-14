#include "WorldRideRoadX.h"

#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RoadX
{
double LaneWidth(const std::string& Class)
{
	if (Class == "motorway" || Class == "trunk") return 3.65;
	if (Class == "primary") return 3.5;
	if (Class == "secondary" || Class == "tertiary") return 3.2;
	return 3.0;
}

Across Of(const Region& G, int Edge, double RuleWidth)
{
	Across Out;
	Out.Half = RuleWidth * 0.5;
	if (Edge < 0 || Edge >= int(G.Edges.size())) return Out;
	Out.OneWay = G.Edges[size_t(Edge)].Oneway != 0;
	const RoadExtra* X = G.ExtraOf(Edge);
	if (X == nullptr) return Out;
	Out.OneWay = Out.OneWay || X->Has(RoadExtra::Oneway);
	Out.Dual = Out.OneWay && !X->Half.empty() && !X->Has(RoadExtra::Roundabout);
	Out.Lanes = X->Lanes;
	Out.Banked = X->Has(RoadExtra::Embankment) ? 1 : X->Has(RoadExtra::Cutting) ? 2 : 0;
	const std::string& Class = G.ClassOf(Edge);
	if (X->Width > 0.0)
	{
		// (within reason for the class: a mapped 30 m is a whole dual carriageway's, a mapped 2 m a path's)
		const double Lo = std::fmax(2.6, RuleWidth * 0.55), Hi = std::fmax(RuleWidth * 1.8, RuleWidth + 3.0);
		Out.Half = std::clamp(X->Width, Lo, Hi) * 0.5;
	}
	else if (Out.Lanes == 1 && !Out.OneWay)
		Out.Half = std::fmin(Out.Half, SingleTrack * 0.5);
	else if (Out.Lanes >= 2)
		Out.Half = std::clamp(Out.Lanes * LaneWidth(Class), RuleWidth * 0.8, RuleWidth * 2.2) * 0.5;
	// ([5C] a motorway's half: its hard shoulder besides; the road laid that much further out on its
	// nearside, so its offside edge stays where it was by the central reservation)
	if (Out.Dual && Class == "motorway")
	{
		Out.Shoulder = HardShoulder;
		Out.Half += HardShoulder * 0.5;
	}
	return Out;
}

bool CentreLine(int Lanes, bool OneWay, double Half)
{
	if (OneWay) return false;
	if (Lanes == 1) return false;
	if (Lanes >= 2) return Half >= 2.0;
	return Half >= 2.5;
}

std::vector<double> LaneLines(int Lanes, bool OneWay, double Half)
{
	std::vector<double> Out;
	if (OneWay && Lanes >= 2)
		for (int K = 1; K < Lanes; ++K) Out.push_back(-Half + 2.0 * Half * K / Lanes);
	else if (!OneWay && Lanes >= 4)
	{
		const int Each = Lanes / 2;
		for (int K = 1; K < Each; ++K)
		{
			Out.push_back(Half * K / Each);
			Out.push_back(-Half * K / Each);
		}
	}
	return Out;
}
} // namespace RoadX

double RidePlan::RoadMark::RiseAt(double Along) const
{
	if (Peak <= 0.0 || Length <= 0.0) return 0.0;
	const double T = (Along - Start) / Length;
	if (T <= 0.0 || T >= 1.0) return 0.0;
	// (a round hump a half sine; a table ramps up its ends and is level between)
	if (Flat <= 0.0) return Peak * std::sin(3.141592653589793 * T);
	const double Ramp = (1.0 - Flat) * 0.5;
	const double U = std::fmin(T, 1.0 - T) / std::fmax(Ramp, 1e-6);
	return Peak * std::fmin(1.0, U);
}

void RidePlan::MapPoints(const std::vector<RoadPoint>& Pts, const std::vector<char>& OnRoad, std::vector<MappedMark>& Out, int OnlyEdge) const
{
	Out.clear();
	const Region& G = Area.Roads;
	if (Pts.size() < 2 || G.RoadPoints.empty()) return;
	double X0 = Pts[0].At.X, X1 = X0, Z0 = Pts[0].At.Z, Z1 = Z0;
	for (const RoadPoint& P : Pts)
	{
		X0 = std::fmin(X0, P.At.X);
		X1 = std::fmax(X1, P.At.X);
		Z0 = std::fmin(Z0, P.At.Z);
		Z1 = std::fmax(Z1, P.At.Z);
	}
	for (const MappedPoint& M : G.RoadPoints)
	{
		if (M.Edge < 0 || (OnlyEdge >= 0 ? M.Edge != OnlyEdge : size_t(M.Edge) >= OnRoad.size() || OnRoad[size_t(M.Edge)] == 0)) continue;
		if (M.At.X < X0 - 20.0 || M.At.X > X1 + 20.0 || M.At.Z < Z0 - 20.0 || M.At.Z > Z1 + 20.0) continue;
		double Best = 1e18, Along = 0.0, Off = 0.0, Half = 0.0;
		P2 Tangent(1.0, 0.0);
		for (size_t S = 0; S + 1 < Pts.size(); ++S)
		{
			const RoadPoint& Pa = Pts[S];
			const RoadPoint& Pb = Pts[S + 1];
			const double Dx = Pb.At.X - Pa.At.X, Dz = Pb.At.Z - Pa.At.Z, L2 = Dx * Dx + Dz * Dz;
			if (L2 < 1e-12) continue;
			const double T = std::clamp(((M.At.X - Pa.At.X) * Dx + (M.At.Z - Pa.At.Z) * Dz) / L2, 0.0, 1.0);
			const double Px = Pa.At.X + Dx * T, Pz = Pa.At.Z + Dz * T;
			const double D = std::hypot(M.At.X - Px, M.At.Z - Pz);
			if (D >= Best) continue;
			Best = D;
			Along = Pa.Along + (Pb.Along - Pa.Along) * T;
			const double Len = std::sqrt(L2);
			Off = ((M.At.X - Px) * Dz - (M.At.Z - Pz) * Dx) / Len;
			Half = Pa.Half + (Pb.Half - Pa.Half) * T;
			Tangent = P2(Dx / Len, Dz / Len);
		}
		// (the map's line and the smoothed road part by a few metres at most; further, it is another road's)
		if (Best > Half + 4.0) continue;
		MappedMark Mk;
		Mk.Kind = M.Kind;
		Mk.Along = Along;
		Mk.Off = Off;
		Mk.Flags = M.Flags;
		if (M.Heading >= 0.0 && (M.Flags & 2) == 0)
		{
			const double H = M.Heading * 3.141592653589793 / 180.0;
			// (a heading's way in the frame: x east, z south)
			const double Dot = std::sin(H) * Tangent.X - std::cos(H) * Tangent.Z;
			Mk.Dir = Dot > 0.3 ? 1 : Dot < -0.3 ? -1 : 0;
		}
		Out.push_back(Mk);
	}
	std::sort(Out.begin(), Out.end(), [](const MappedMark& A, const MappedMark& B) { return A.Along < B.Along; });
}

namespace
{
using Mark = RidePlan::RoadMark;
constexpr float White[3] = {0.83f, 0.83f, 0.75f};
constexpr float HumpTop[3] = {0.03f, 0.03f, 0.033f};
constexpr float BayTarmac[3] = {0.026f, 0.026f, 0.029f};

Mark Paint(double From, double To, double Left, double Right)
{
	Mark M;
	M.Kind = RidePlan::MarkPaint;
	M.From = From;
	M.To = To;
	M.Left = Left;
	M.Right = Right;
	M.Lift = 0.0;
	for (int C = 0; C < 3; ++C) M.Colour[C] = White[C];
	return M;
}

// [OSM stage 5D] A crossing's zig-zag line along the road from A0 to A1 (either way round), centred Lat
// across it: marks 2 m long that swing 0.3 m side to side, each drawn as a staircase of short pieces of
// a 0.1 m line (from a rider's eye, a zig-zag).
void ZigZag(double A0, double A1, double Lat, std::vector<Mark>& Out)
{
	const double Lo = std::fmin(A0, A1), Hi = std::fmax(A0, A1);
	const int Marks = std::max(1, int(std::lround((Hi - Lo) / 2.0)));
	const double Each = (Hi - Lo) / Marks;
	constexpr int Steps = 6;
	for (int Mk = 0; Mk < Marks; ++Mk)
		for (int St = 0; St < Steps; ++St)
		{
			const double F = (St + 0.5) / Steps;
			const double Swing = 0.15 * ((Mk % 2 == 0) ? (2.0 * F - 1.0) : (1.0 - 2.0 * F));
			const double U0 = Lo + Each * (Mk + double(St) / Steps), U1 = Lo + Each * (Mk + double(St + 1) / Steps);
			Out.push_back(Paint(U0, U1, Lat + Swing + 0.05, Lat + Swing - 0.05));
		}
}

// A broken line across Left..Right at Along, Wide along the road: Dash m of paint, Gap m between.
void Broken(double Along, double Wide, double Left, double Right, double Dash, double Gap, std::vector<Mark>& Out)
{
	for (double U = Right; U < Left; U += Dash + Gap) Out.push_back(Paint(Along - Wide * 0.5, Along + Wide * 0.5, std::fmin(U + Dash, Left), U));
}
} // namespace

void RidePlan::MappedMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const
{
	const std::vector<MappedMark>* List = nullptr;
	if (Which == 0) List = &MainMapped;
	else if (const SideRoad* Sd = SideOf(Which)) List = &Sd->Mapped;
	if (List == nullptr || List->empty()) return;
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	if (Pts.size() < 2) return;
	const double RoadLen = Pts.back().Along;
	const bool Left = KeepLeft();
	const auto HalfAt = [&Pts](double Along)
	{
		size_t Lo = 0, Hi = Pts.size();
		while (Hi - Lo > 1)
		{
			const size_t Mid = (Lo + Hi) / 2;
			if (Pts[Mid].Along <= Along) Lo = Mid;
			else Hi = Mid;
		}
		return Lo + 1 < Pts.size() ? std::fmin(Pts[Lo].Half, Pts[Lo + 1].Half) : Pts[Lo].Half;   // (the narrower end of the bit it is on)
	};
	std::vector<RoadMark> Made;
	// [OSM stage 5D] A crossing's zig-zags (no stopping, no overtaking): along both edges, and down the
	// middle of a two-way road wide enough, from Clear m either side of it out to 16 m (shorter where the
	// road ends sooner).
	const auto CrossingZigZags = [&Pts, &Made, RoadLen](double At, double Clear, double Half)
	{
		size_t Near = 0;
		for (size_t P = 1; P < Pts.size(); ++P)
			if (std::fabs(Pts[P].Along - At) < std::fabs(Pts[Near].Along - At)) Near = P;
		const bool TwoWay = !Pts[Near].OneWay && Half >= 3.0;
		for (double Sd : {-1.0, 1.0})
		{
			const double A0 = At + Sd * Clear, A1 = std::clamp(At + Sd * 16.0, 0.5, RoadLen - 0.5);
			if ((A1 - A0) * Sd < 2.0) continue;
			ZigZag(A0, A1, Half - 0.45, Made);
			ZigZag(A0, A1, -Half + 0.45, Made);
			if (TwoWay) ZigZag(A0, A1, 0.0, Made);
		}
	};
	for (const MappedMark& M : *List)
	{
		if (M.Along < From - 30.0 || M.Along > To + 30.0) continue;
		const double Half = HalfAt(M.Along);
		// Which lane the traffic it governs is in: the one coming towards a junction, keeping left in
		// Britain. Not mapped: the lane heading for the road's nearer end (a junction, mostly).
		const int Dir = M.Dir != 0 ? M.Dir : (M.Along < RoadLen * 0.5 ? -1 : 1);
		const double LaneSign = (Dir > 0) == Left ? 1.0 : -1.0;   // (+ the left half)
		const double LaneLo = LaneSign > 0.0 ? 0.1 : -Half + 0.15, LaneHi = LaneSign > 0.0 ? Half - 0.15 : -0.1;
		// (a line across the lane just before the point, as the traffic meets it)
		const double Before = M.Along - Dir * 1.0;
		const std::string& K = M.Kind;
		if (K == "give_way")
		{
			// (Britain's: two broken lines, 600 mm dashes, 300 mm gaps, 200 mm wide, 300 mm apart)
			Broken(Before, 0.2, LaneHi, LaneLo, 0.6, 0.3, Made);
			Broken(Before - Dir * 0.5, 0.2, LaneHi, LaneLo, 0.6, 0.3, Made);
		}
		else if (K == "stop" || K == "traffic_signals")
		{
			if (K == "traffic_signals" && M.Dir == 0) continue;   // (which way it faces unknown: the poles say it)
			Made.push_back(Paint(Before - 0.15, Before + 0.15, LaneHi, LaneLo));
		}
		else if (K == "crossing_zebra")
		{
			// (black and white stripes along the road, 600 mm each, across its whole width, 3 m deep)
			for (double U = -Half + 0.1; U + 0.5 < Half; U += 1.2) Made.push_back(Paint(M.Along - 1.5, M.Along + 1.5, U + 0.6, U));
			// ([OSM stage 5D] a give-way line a metre short of the stripes each way, and the zig-zags)
			for (double Sd : {-1.0, 1.0}) Broken(M.Along + Sd * 2.6, 0.2, Half - 0.15, -Half + 0.15, 0.6, 0.3, Made);
			CrossingZigZags(M.Along, 3.2, Half);
		}
		else if (K == "crossing_signals")
		{
			// (two broken lines of studs marking the crossing's edges, and a stop line each way)
			for (double Edge : {-1.2, 1.2}) Broken(M.Along + Edge, 0.1, Half - 0.15, -Half + 0.15, 0.2, 0.2, Made);
			Made.push_back(Paint(M.Along - 3.0 - 0.15, M.Along - 3.0 + 0.15, Left ? Half - 0.15 : -0.1, Left ? 0.1 : -Half + 0.15));
			Made.push_back(Paint(M.Along + 3.0 - 0.15, M.Along + 3.0 + 0.15, Left ? -0.1 : Half - 0.15, Left ? -Half + 0.15 : 0.1));
			CrossingZigZags(M.Along, 3.4, Half);   // [OSM stage 5D]
		}
		else if (K == "mini_roundabout")
		{
			// (a white disc 2.5 m across at the junction's middle, flat: as stacked bands along the road)
			const double R = std::fmin(1.25, Half * 0.6);
			for (int Bd = -5; Bd < 5; ++Bd)
			{
				const double A0 = R * Bd / 5.0, A1 = R * (Bd + 1) / 5.0, Mid = (A0 + A1) * 0.5;
				const double W = std::sqrt(std::fmax(0.0, R * R - Mid * Mid));
				Made.push_back(Paint(M.Along + A0, M.Along + A1, M.Off + W, M.Off - W));
			}
		}
		else if (K == "hump" || K == "table" || K == "cushion" || K == "bump")
		{
			// (Britain's sizes: a hump 75 mm high and 3.7 m long; a table level on top; a cushion 1.7 m
			// wide in each lane, the buses straddling it; a bump short and sharp)
			const double Len = K == "table" ? 6.0 : K == "bump" ? 0.9 : K == "cushion" ? 2.5 : 3.7;
			const double Peak = K == "bump" ? 0.05 : K == "cushion" ? 0.07 : 0.075;
			const double Flat = K == "table" ? 0.6 : K == "cushion" ? 0.3 : 0.0;
			std::vector<std::pair<double, double>> Spans;
			if (K == "cushion")
				for (double Sg : {1.0, -1.0}) Spans.push_back({Sg * Half * 0.5 + 0.85, Sg * Half * 0.5 - 0.85});
			else
				Spans.push_back({Half, -Half});
			const int Slices = std::max(4, int(std::ceil(Len / 0.4)));
			for (const auto& Sp : Spans)
				for (int Sl = 0; Sl < Slices; ++Sl)
				{
					RoadMark H;
					H.Kind = MarkHump;
					H.Start = M.Along - Len * 0.5;
					H.Length = Len;
					H.Peak = Peak;
					H.Flat = Flat;
					H.From = H.Start + Len * Sl / Slices;
					H.To = H.Start + Len * (Sl + 1) / Slices;
					H.Left = Sp.first;
					H.Right = Sp.second;
					for (int C = 0; C < 3; ++C) H.Colour[C] = HumpTop[C];
					Made.push_back(H);
				}
		}
		else if (K == "passing_place" && Which == 0)
		{
			// (2.5 m more tarmac for 15 m on the side the traffic keeps to, tapering in and out over 5 m)
			const double Sg = Left ? 1.0 : -1.0;
			for (int Sl = -5; Sl < 5; ++Sl)
			{
				const double A0 = M.Along + Sl * 2.5, A1 = A0 + 2.5;
				const double Mid = std::fabs((A0 + A1) * 0.5 - M.Along);
				const double W = 2.5 * std::clamp((12.5 - Mid) / 5.0, 0.0, 1.0);
				if (W <= 0.05) continue;
				RoadMark Bay;
				Bay.Kind = MarkBay;
				Bay.From = A0;
				Bay.To = A1;
				Bay.Left = Sg > 0.0 ? Half + W : -Half + 0.02;
				Bay.Right = Sg > 0.0 ? Half - 0.02 : -Half - W;
				for (int C = 0; C < 3; ++C) Bay.Colour[C] = BayTarmac[C];
				Made.push_back(Bay);
			}
		}
	}
	for (RoadMark& Mk : Made)
	{
		if (Mk.To < From || Mk.From > To) continue;
		// (paint and humps never past the road's edge where the road narrows under them)
		if (Mk.Kind != MarkBay)
		{
			const double Edge = std::fmin(HalfAt(Mk.From), HalfAt(Mk.To)) - 0.02;
			Mk.Left = std::fmin(Mk.Left, Edge);
			Mk.Right = std::fmax(Mk.Right, -Edge);
			if (Mk.Left <= Mk.Right) continue;
		}
		Out.push_back(Mk);
	}
}
} // namespace TorqueWorld

