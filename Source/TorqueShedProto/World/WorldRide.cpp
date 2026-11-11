#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace TorqueWorld
{
using TorqueFit::Json;

namespace
{
constexpr double NearCell = 16.0;

long long NearKey(double X, double Z)
{
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	return Cx * 4000037LL + Cz;
}

double Dot(const P2& A, const P2& B)
{
	return A.X * B.X + A.Z * B.Z;
}

double Smooth01(double T)
{
	T = std::fmin(1.0, std::fmax(0.0, T));
	return T * T * (3.0 - 2.0 * T);
}

} // namespace

std::shared_ptr<RidePlan> RidePlan::Make(CellStore& Store, HeightStore& Heights, double InLatA, double InLonA, double InLatB, double InLonB,
	const std::function<void(const std::string&)>& Say)
{
	std::shared_ptr<RidePlan> R = std::make_shared<RidePlan>();
	R->LatA = InLatA; R->LonA = InLonA; R->LatB = InLatB; R->LonB = InLonB;
	const auto Tell = [&Say](const std::string& Text) { if (Say) Say(Text); };
	Tell("Reading the world's index");
	const std::shared_ptr<const WorldIndex> Idx = Store.Index();
	if (!Idx || Idx->Empty()) { R->Why = "The world's index can't be had."; return R; }
	const double Pad = 0.03;
	std::vector<CellId> Cells;
	for (const CellId& C : CellsInBox(std::fmin(InLatA, InLatB) - Pad, std::fmin(InLonA, InLonB) - Pad, std::fmax(InLatA, InLatB) + Pad, std::fmax(InLonA, InLonB) + Pad))
	{
		const CellStore::EState St = Store.StateOf(C, *Idx);
		if (St == CellStore::EState::Here || St == CellStore::EState::Baked) Cells.push_back(C);
	}
	if (Cells.empty()) { R->Why = "Nobody has baked that part of the world yet."; return R; }
	Tell("Fetching " + std::to_string(Cells.size()) + " cells of roads");
	if (!LoadCells(Store, Heights, Cells, R->Area, &R->Why)) return R;
	const Region& G = R->Area.Roads;
	const Frame F = G.FrameOf();
	const P2 A = F.FromGeo(InLatA, InLonA), B = F.FromGeo(InLatB, InLonB);
	const int From = G.NearestConnected(A.X, A.Z, 2000.0), To = G.NearestConnected(B.X, B.Z, 2000.0);
	if (From < 0 || To < 0) { R->Why = std::string("There is no road within 2 km of the ") + (From < 0 ? "start." : "end."); return R; }
	Tell("Finding the way by road");
	// The quick way: a metre of main road costs less than one of a back lane.
	std::vector<double> Costs;
	for (const std::string& Class : G.Classes) Costs.push_back(RoadLine::MainCostOf(Class));
	const std::vector<int> Path = G.ShortestPath(From, To, &Costs);
	if (Path.empty()) { R->Why = "There is no way by road between them."; return R; }
	for (int E : Path)
	{
		const std::string Name = G.NameOf(E);
		if (R->Names.empty() || R->Names.back() != Name) R->Names.push_back(Name);
	}
	// The road as the map drew it, and which road each of its points is on.
	std::vector<P2> Raw;
	std::vector<int> Owner;
	{
		int Node = From;
		for (int E : Path)
		{
			const std::vector<P2> Piece = G.LineFrom(E, Node);
			for (size_t I = Raw.empty() ? 0 : 1; I < Piece.size(); ++I)
			{
				Raw.push_back(Piece[I]);
				Owner.push_back(E);
			}
			Node = G.OtherEnd(E, Node);
		}
	}
	std::vector<double> Fair(Raw.size()), RawAlong(Raw.size(), 0.0);
	for (size_t I = 0; I < Raw.size(); ++I)
	{
		Fair[I] = RoadLine::FairOf(G.ClassOf(Owner[I]));
		if (I > 0) RawAlong[I] = RawAlong[I - 1] + Raw[I].DistanceTo(Raw[I - 1]);
	}
	Tell("Smoothing the road");
	const std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, Fair);
	// Each knot as wide as the road it is on (by how far along it is), eased where the width changes.
	std::vector<double> Halves(Line.size(), 2.3), Speeds(Line.size(), 60.0);
	{
		std::vector<double> KnotAlong(Line.size(), 0.0);
		for (size_t I = 1; I < Line.size(); ++I) KnotAlong[I] = KnotAlong[I - 1] + Line[I].DistanceTo(Line[I - 1]);
		const double Scale = !Line.empty() && KnotAlong.back() > 0.0 && !Raw.empty() ? RawAlong.back() / KnotAlong.back() : 1.0;
		size_t J = 0;
		for (size_t I = 0; I < Line.size(); ++I)
		{
			const double At = KnotAlong[I] * Scale;
			while (J + 1 < Raw.size() && RawAlong[J + 1] < At) ++J;
			Halves[I] = RoadLine::WidthOf(G.ClassOf(Owner[std::min(J + 1, Raw.size() - 1)])) * 0.5;
			Speeds[I] = RoadLine::DesignSpeedOf(G.ClassOf(Owner[std::min(J + 1, Raw.size() - 1)]));
		}
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			std::vector<double> Eased = Halves;
			for (size_t I = 0; I < Halves.size(); ++I)
			{
				double Sum = 0.0;
				int Count = 0;
				for (size_t K = I > 6 ? I - 6 : 0; K <= std::min(I + 6, Halves.size() - 1); ++K) { Sum += Halves[K]; ++Count; }
				Eased[I] = Sum / Count;
			}
			Halves.swap(Eased);
		}
	}
	// (the tiles are chosen by the road as the map has it, a point every 30 m, as every rider's are)
	R->Land = R->Area.Land->ForRoad(Resample(G.PathLine(From, Path), 30.0));
	Tell("Fetching the ground: " + std::to_string(R->Land->UsedKeys().size()) + " tiles");
	R->TilesFailed = FetchGround(Store, Heights, *R->Land);
	if (R->TilesFailed > 0) { R->Why = std::to_string(R->TilesFailed) + " of the ground's tiles couldn't be fetched."; return R; }
	Tell("Laying the road");
	R->LayRoad(Line, Halves, Speeds);
	if (R->Road.size() < 2) { R->Why = "The road has no ground under it."; return R; }
	Tell("Laying the roads that meet it");
	R->LaySides(Path);
	R->Ok = true;
	return R;
}

// (a port of route_planner.gd's spans_along)
std::vector<int> RidePlan::SpansAlong(const std::vector<P2>& Line, int Kind) const
{
	std::vector<int> Out;
	const Region& G = Area.Roads;
	if (G.Spans.empty() || Line.size() < 2) return Out;
	const double Cell = 32.0;
	std::unordered_map<long long, std::vector<int>> Grid;
	const auto Key = [Cell](double X, double Z) { return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell)); };
	// (most spans are nowhere near: only those with an end in the line's own box are looked up)
	double MinX = 1e18, MaxX = -1e18, MinZ = 1e18, MaxZ = -1e18;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		Grid[Key(Line[I].X, Line[I].Z)].push_back(int(I));
		MinX = std::fmin(MinX, Line[I].X); MaxX = std::fmax(MaxX, Line[I].X);
		MinZ = std::fmin(MinZ, Line[I].Z); MaxZ = std::fmax(MaxZ, Line[I].Z);
	}
	MinX -= BridgeReach; MaxX += BridgeReach; MinZ -= BridgeReach; MaxZ += BridgeReach;
	const auto NearTo = [&](const P2& P)
	{
		std::vector<int> Found;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Grid.find(Key(P.X + Dx * Cell, P.Z + Dz * Cell));
				if (It == Grid.end()) continue;
				for (int I : It->second)
					if (Line[size_t(I)].DistanceTo(P) <= BridgeReach) Found.push_back(I);
			}
		return Found;
	};
	std::vector<std::pair<int, int>> Found;
	for (const Span& Sp : G.Spans)
	{
		if (Sp.Kind != Kind || Sp.A.X < MinX || Sp.A.X > MaxX || Sp.A.Z < MinZ || Sp.A.Z > MaxZ) continue;
		const std::vector<int> AtA = NearTo(Sp.A);
		if (AtA.empty()) continue;
		const std::vector<int> AtB = NearTo(Sp.B);
		const double Long = Sp.A.DistanceTo(Sp.B);
		std::pair<int, int> Best(-1, -1);
		double BestOff = 1e18;
		for (int Ia : AtA)
			for (int Ib : AtB)
			{
				const double Along = std::abs(Ib - Ia) * RoadLine::Step;
				// (the road passes both ends, but not across the bridge between them)
				if (Along < Long * 0.5 - RoadLine::Step || Along > Long * 1.5 + RoadLine::Step * 2.0) continue;
				const double Off = std::fabs(Along - Long);
				if (Off < BestOff)
				{
					BestOff = Off;
					Best = {std::min(Ia, Ib), std::max(Ia, Ib)};
				}
			}
		if (Best.first >= 0 && Best.second > Best.first) Found.push_back(Best);
	}
	std::sort(Found.begin(), Found.end());
	for (const std::pair<int, int>& F : Found)
	{
		// (overlapping, or end to end: one bridge)
		if (Out.size() >= 2 && F.first <= Out.back() + 1) Out.back() = std::max(Out.back(), F.second);
		else { Out.push_back(F.first); Out.push_back(F.second); }
	}
	return Out;
}

void RidePlan::LevelSpans(const std::vector<P2>& Line, std::vector<double>& H, std::vector<char>& Kind, int& BridgeCount, int& TunnelCount) const
{
	Kind.assign(Line.size(), 0);
	for (int Sort = 0; Sort < 2; ++Sort)
	{
		const std::vector<int> Knots = SpansAlong(Line, Sort);
		(Sort == 0 ? BridgeCount : TunnelCount) += int(Knots.size() / 2);
		for (size_t K = 0; K + 1 < Knots.size(); K += 2)
		{
			const int I0 = Knots[K], I1 = std::min(Knots[K + 1], int(H.size()) - 1);
			for (int I = I0 + 1; I < I1; ++I) H[size_t(I)] = H[size_t(I0)] + (H[size_t(I1)] - H[size_t(I0)]) * double(I - I0) / double(I1 - I0);
			for (int I = I0; I <= I1; ++I) Kind[size_t(I)] = static_cast<char>(Sort + 1);
		}
	}
}

void RidePlan::LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves, const std::vector<double>& Speeds)
{
	const std::vector<double> Banks = RoadLine::BankOf(Line, Speeds);
	Road.clear();
	Near.clear();
	Bridges = Tunnels = 0;
	std::vector<double> H(Line.size());
	double Last = 0.0;
	bool Any = false;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		const double Y = Land->Height(Line[I].X, Line[I].Z);
		if (!std::isnan(Y)) { Last = Y; Any = true; }
		H[I] = std::fmax(std::isnan(Y) ? Last : Y, 0.0);   // (a hole in the ground is bridged at the last height known; never under the sea)
	}
	if (!Any) return;
	// Over a bridge the road runs straight from one end to the other, not down into what it crosses;
	// through a tunnel the same, not up over the hill.
	std::vector<char> Kind;
	LevelSpans(Line, H, Kind, Bridges, Tunnels);
	// The road's own height: the ground's along its middle, evened out over 75 m three times, so it neither
	// follows every hummock nor leaves the land far.
	for (int Pass = 0; Pass < 3; ++Pass)
	{
		std::vector<double> S(H.size());
		const int Reach = 9;
		for (size_t I = 0; I < H.size(); ++I)
		{
			double Sum = 0.0, Wt = 0.0;
			for (int K = -Reach; K <= Reach; ++K)
			{
				const long long J = static_cast<long long>(I) + K;
				if (J < 0 || J >= static_cast<long long>(H.size())) continue;
				const double W = double(Reach + 1 - std::abs(K));
				Sum += H[size_t(J)] * W;
				Wt += W;
			}
			S[I] = Sum / Wt;
		}
		H.swap(S);
	}
	// (the Godot game averages over 9 knots once; three wider passes leave no crest a bike takes off
	// from at speed. The grade's limit is its own.)
	H = RoadLine::GradeLimit(H);
	WidestHalf = 0.0;
	double Along = 0.0;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		if (I > 0) Along += Line[I].DistanceTo(Line[I - 1]);
		RoadPoint P;
		P.At = Line[I];
		P.Along = Along;
		P.Height = H[I];
		P.Half = I < Halves.size() ? Halves[I] : 2.3;
		P.Bridge = Kind[I] == 1;
		P.Tunnel = Kind[I] == 2;
		P.Bank = Banks[I];
		WidestHalf = std::fmax(WidestHalf, P.Half);
		Near[NearKey(P.At.X, P.At.Z)].push_back({0, int(Road.size())});
		Road.push_back(P);
	}
	Length = Along;
}

bool RidePlan::Nearest(double X, double Z, double& Dist, double& Height, int* Index, double* Half, int* Which, bool MainOnly, bool* OnBridge, double* Cross) const
{
	NearOpts Opts;
	Opts.MainOnly = MainOnly;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit)) return false;
	Dist = Hit.Dist;
	Height = Hit.Height;
	if (Index != nullptr) *Index = Hit.Index;
	if (Half != nullptr) *Half = Hit.Half;
	if (Which != nullptr) *Which = Hit.Which;
	if (OnBridge != nullptr) *OnBridge = Hit.Deck;
	if (Cross != nullptr) *Cross = Hit.Cross;
	return true;
}

bool RidePlan::NearestOf(double X, double Z, const NearOpts& Opts, NearHit& Hit) const
{
	// (best by how far outside the road's edge the point is, so a point on a lane beside a main road
	// is the lane's; the ride's own road wins a tie)
	double Best = 1e18;
	Hit = NearHit();
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	for (long long Dx = -3; Dx <= 3; ++Dx)
		for (long long Dz = -3; Dz <= 3; ++Dz)
		{
			const auto It = Near.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == Near.end()) continue;
			for (const std::pair<int, int>& Entry : It->second)
			{
				if ((Opts.MainOnly && Entry.first != 0) || Entry.first == Opts.NotRoad || (Opts.OnlyRoad >= 0 && Entry.first != Opts.OnlyRoad)) continue;
				const std::vector<RoadPoint>& Pts = RoadOf(Entry.first);
				// The bit of road from this point to the next (the last point's is filed under the one before).
				const int S = Entry.second;
				if (S + 1 >= int(Pts.size())) continue;
				const RoadPoint& Pa = Pts[size_t(S)];
				const RoadPoint& Pb = Pts[size_t(S) + 1];
				const bool SegDeck = Pa.Bridge && Pb.Bridge, SegBore = Pa.Tunnel && Pb.Tunnel;
				if ((Opts.NoDecks && SegDeck) || (Opts.NoBores && SegBore)) continue;
				const P2 Ab = Pb.At - Pa.At;
				const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
				const double T = L2 > 1e-12 ? std::fmin(1.0, std::fmax(0.0, ((X - Pa.At.X) * Ab.X + (Z - Pa.At.Z) * Ab.Z) / L2)) : 0.0;
				const double Px = Pa.At.X + Ab.X * T, Pz = Pa.At.Z + Ab.Z * T;
				const double D2 = (X - Px) * (X - Px) + (Z - Pz) * (Z - Pz);
				if (D2 > 40.0 * 40.0) continue;
				const double D = std::sqrt(D2);
				const double HalfHere = Pa.Half + (Pb.Half - Pa.Half) * T;
				const double Score = D - HalfHere - (Entry.first == 0 ? 0.01 : 0.0);
				if (Score >= Best) continue;
				Best = Score;
				Hit.Dist = D;
				Hit.Height = Pa.Height + (Pb.Height - Pa.Height) * T;
				Hit.Half = HalfHere;
				Hit.Index = T < 0.5 ? S : S + 1;
				Hit.Which = Entry.first;
				Hit.Deck = SegDeck;
				Hit.Bore = SegBore;
				Hit.Foot = P2(Px, Pz);
				// (how far to the left of the road's middle: left of a heading (dx, dz) is (dz, -dx))
				const double Len = std::sqrt(L2);
				const double Off = Len > 1e-9 ? ((X - Px) * Ab.Z - (Z - Pz) * Ab.X) / Len : 0.0;
				Hit.Cross = RoadLine::CrossHeight(Off, HalfHere, Pa.Bank + (Pb.Bank - Pa.Bank) * T);
			}
		}
	return Hit.Index >= 0;
}

void RidePlan::LaySides(const std::vector<int>& Path)
{
	Sides.clear();
	MouthAt.assign(Road.size(), 0);
	SideBridges = SideTunnels = 0;
	const Region& G = Area.Roads;
	// Where the ride goes, in squares of SideReach: a road with a point in one of them or the next is near it.
	std::unordered_map<long long, char> Beside;
	const auto Square = [](double X, double Z) { return static_cast<long long>(std::floor(X / SideReach)) * 4000037LL + static_cast<long long>(std::floor(Z / SideReach)); };
	for (const RoadPoint& P : Road)
		for (int Dx = -1; Dx <= 1; ++Dx)
			for (int Dz = -1; Dz <= 1; ++Dz) Beside[Square(P.At.X + Dx * SideReach, P.At.Z + Dz * SideReach)] = 1;
	std::vector<char> OnRide(G.Edges.size(), 0);
	for (int E : Path) OnRide[size_t(E)] = 1;
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& Edge = G.Edges[E];
		if (OnRide[E] != 0 || Edge.Line.size() < 2 || Edge.A == Edge.B) continue;
		bool Near1 = false;
		for (const P2& P : Edge.Line)
			if (Beside.count(Square(P.X, P.Z)) != 0) { Near1 = true; break; }
		if (!Near1) continue;
		// Its line as the map drew it; where an end is at the ride's road (whose line the smoothing may
		// have moved a little off the junction), on to that road's middle.
		std::vector<P2> Raw = Edge.Line;
		bool Meets[2] = {false, false};
		for (int End = 0; End < 2; ++End)
		{
			const P2 At = End == 0 ? Raw.front() : Raw.back();
			double D = 0.0, H = 0.0;
			int Index = -1;
			if (!Nearest(At.X, At.Z, D, H, &Index, nullptr, nullptr, true) || D > 15.0) continue;
			Meets[End] = true;
			const P2 Mid = Road[size_t(Index)].At;
			if (Mid.DistanceTo(At) > 0.5)
			{
				if (End == 0) Raw.insert(Raw.begin(), Mid);
				else Raw.push_back(Mid);
			}
		}
		const std::string& Class = G.ClassOf(int(E));
		std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, std::vector<double>(Raw.size(), RoadLine::FairOf(Class)));
		// (the knots stop at the last whole step: on to the junction itself, so no road ends short of it)
		if (Line.empty() || Line.back().DistanceTo(Raw.back()) > 0.3) Line.push_back(Raw.back());
		if (Line.size() < 2) continue;
		// Its height: the ground's, smoothed as the ride's road is; at a junction off the ride, the
		// ground's at the junction (so every road that meets there agrees); near the ride's road, that road's.
		std::vector<double> H(Line.size()), Along(Line.size(), 0.0);
		double Last = Road.front().Height;
		for (size_t I = 0; I < Line.size(); ++I)
		{
			const double Y = Land->Height(Line[I].X, Line[I].Z);
			if (!std::isnan(Y)) Last = Y;
			H[I] = std::fmax(std::isnan(Y) ? Last : Y, 0.0);
			if (I > 0) Along[I] = Along[I - 1] + Line[I].DistanceTo(Line[I - 1]);
		}
		const double EndH[2] = {H.front(), H.back()};
		// (its own bridges and tunnels, as the ride's road has its)
		std::vector<char> Kind;
		LevelSpans(Line, H, Kind, SideBridges, SideTunnels);
		H = RoadLine::GradeLimit(RoadLine::SmoothHeights(RoadLine::SmoothHeights(H, 9), 9));
		SideRoad Side;
		Side.Length = Along.back();
		const std::vector<double> Banks = RoadLine::BankOf(Line, {RoadLine::DesignSpeedOf(Class)});
		const double Half = RoadLine::WidthOf(Class) * 0.5;
		for (size_t I = 0; I < Line.size(); ++I)
		{
			for (int End = 0; End < 2; ++End)
			{
				if (Meets[End]) continue;
				const double From = End == 0 ? Along[I] : Side.Length - Along[I];
				H[I] += (EndH[End] - H[I]) * (1.0 - Smooth01(From / 30.0));
			}
		}
		// (no ramp steeper than 1 in 5 from all that, then the ride's road's own height where it is near)
		for (int Round = 0; Round < 3; ++Round)
		{
			for (size_t I = 1; I < H.size(); ++I)
			{
				const double Lim = 0.2 * (Along[I] - Along[I - 1]);
				H[I] = std::fmin(std::fmax(H[I], H[I - 1] - Lim), H[I - 1] + Lim);
			}
			for (size_t I = H.size(); I-- > 1;)
			{
				const double Lim = 0.2 * (Along[I] - Along[I - 1]);
				H[I - 1] = std::fmin(std::fmax(H[I - 1], H[I] - Lim), H[I] + Lim);
			}
		}
		// Where it is by the ride's road. At a junction it has no deck of its own; where it goes over or
		// under the ride's road with no junction (one of the two on a bridge or in a tunnel there), each
		// keeps its own height.
		std::vector<NearHit> Main(Line.size());
		std::vector<double> Apart(Line.size(), 1e18);
		NearOpts MainOnly;
		MainOnly.MainOnly = true;
		for (size_t I = 0; I < Line.size(); ++I)
		{
			if (!NearestOf(Line[I].X, Line[I].Z, MainOnly, Main[I])) continue;
			if (Main[I].Dist > Main[I].Half + 2.0) continue;
			const double FromEnd = std::fmin(Meets[0] ? Along[I] : 1e18, Meets[1] ? Side.Length - Along[I] : 1e18);
			if (FromEnd < 25.0) Kind[I] = 0;
			else if (Kind[I] != 0 || Main[I].Deck || Main[I].Bore) Apart[I] = 0.0;
		}
		for (size_t I = 1; I < Line.size(); ++I) Apart[I] = std::fmin(Apart[I], Apart[I - 1] + (Along[I] - Along[I - 1]));
		for (size_t I = Line.size(); I-- > 1;) Apart[I - 1] = std::fmin(Apart[I - 1], Apart[I] + (Along[I] - Along[I - 1]));
		for (size_t I = 0; I < Line.size(); ++I)
		{
			double Y = H[I];
			double Meeting = 0.0;
			if (Main[I].Index >= 0)
			{
				// (the ride's road's surface where this point is on it, tilt and all; its edge's beyond)
				Meeting = (1.0 - Smooth01((Main[I].Dist - Main[I].Half - 2.0) / 30.0)) * Smooth01(Apart[I] / 40.0);
				Y += (Main[I].Height + Main[I].Cross - Y) * Meeting;
			}
			RoadPoint P;
			P.At = Line[I];
			P.Along = Along[I];
			P.Height = Y;
			P.Half = Half;
			P.Bridge = Kind[I] == 1;
			P.Tunnel = Kind[I] == 2;
			// Its own tilt: none where it lies on the ride's road, nor at a junction at either end.
			P.Bank = Banks[I] * (1.0 - Meeting) * Smooth01(std::fmin(Along[I], Side.Length - Along[I]) / 20.0);
			Side.Pts.push_back(P);
		}
		// Its mouths: from each end on the ride's road, out to where its middle leaves that road's edge.
		for (int End = 0; End < 2; ++End)
		{
			if (!Meets[End]) continue;
			const int Count = int(Side.Pts.size()), Dir = End == 0 ? 1 : -1;
			NearHit Last2, Here;
			for (int Gone = 0, I = End == 0 ? 0 : Count - 1; Gone < Count; ++Gone, I += Dir)
			{
				const RoadPoint& P = Side.Pts[size_t(I)];
				if ((End == 0 ? P.Along : Side.Length - P.Along) > 40.0 || !NearestOf(P.At.X, P.At.Z, MainOnly, Here)) break;
				if (Here.Dist <= Here.Half) { Last2 = Here; continue; }
				if (Gone == 0) break;   // (its end is not on the ride's road after all)
				const RoadPoint& Q = Side.Pts[size_t(I - Dir)];
				const double In = std::fmax(0.0, Last2.Half - Last2.Dist), Outside = Here.Dist - Here.Half;
				const P2 Step2 = P.At - Q.At;
				const int Mi = Here.Index;
				const P2 MainDir = Road[size_t(std::min(Mi + 1, int(Road.size()) - 1))].At - Road[size_t(std::max(Mi - 1, 0))].At;
				if (Step2.Length() < 1e-6 || MainDir.Length() < 1e-6) break;
				Mouth M;
				M.At = Q.At + Step2 * (In / (In + Outside));
				M.Away = Step2 * (1.0 / Step2.Length());
				M.Along = MainDir * (1.0 / MainDir.Length());
				M.Across = P2(M.Along.Z, -M.Along.X);   // (the ride's road's left)
				const bool OnLeft = Dot(M.At - Road[size_t(Mi)].At, M.Across) >= 0.0;
				if (!OnLeft) M.Across = M.Across * -1.0;
				if (Dot(M.Away, M.Across) < 0.4) break;   // (it runs in alongside: no corner to round)
				M.Is = true;
				M.MainIndex = Mi;
				M.MainHalf = Here.Half;
				// (a lane meeting a lane has no markings; a lesser road meeting a greater gives way)
				M.GiveWay = Half >= 1.6 && Half < Here.Half - 0.2;
				const double AlongAt = Q.Along + (P.Along - Q.Along) * (In / (In + Outside));
				M.From = End == 0 ? AlongAt : Side.Length - AlongAt;
				// The ride's road's points the mouth and its corners lie beside: no edge line there, and no
				// shoulder across the mouth itself.
				double From = 1e18, To = -1e18, MouthFrom = 1e18, MouthTo = -1e18;
				for (int Sd = 0; Sd < 2; ++Sd)
				{
					const double Sigma = Sd == 0 ? 1.0 : -1.0;
					P2 Corner, AlongMain;
					double Reach = 0.0;
					MouthCorner(M, Road[size_t(Mi)].At, Half, Sigma, Corner, AlongMain, Reach);
					const P2 EdgeAt = M.At + P2(M.Away.Z, -M.Away.X) * (Sigma * Half);
					M.Clear[Sd] = M.From + Dot(Corner - EdgeAt, M.Away) + Reach;
					const double At2 = Dot(Corner + AlongMain * Reach - Road[size_t(Mi)].At, M.Along), At3 = Dot(Corner - Road[size_t(Mi)].At, M.Along);
					From = std::fmin(From, At2);
					To = std::fmax(To, At2);
					MouthFrom = std::fmin(MouthFrom, At3);
					MouthTo = std::fmax(MouthTo, At3);
				}
				Side.Mouths[End] = M;
				for (int J = std::max(0, Mi - 12); J <= std::min(int(Road.size()) - 1, Mi + 12); ++J)
				{
					const double At2 = Dot(Road[size_t(J)].At - Road[size_t(Mi)].At, M.Along);
					if (At2 >= From - 0.5 && At2 <= To + 0.5) MouthAt[size_t(J)] |= static_cast<unsigned char>(OnLeft ? 1 : 2);
					if (At2 >= MouthFrom && At2 <= MouthTo) MouthAt[size_t(J)] |= static_cast<unsigned char>(OnLeft ? 4 : 8);
				}
				break;
			}
		}
		Sides.push_back(std::move(Side));
	}
	// (filed once all are laid, so none was pulled to another side road's height)
	for (size_t K = 0; K < Sides.size(); ++K)
		for (size_t I = 0; I < Sides[K].Pts.size(); ++I) Near[NearKey(Sides[K].Pts[I].At.X, Sides[K].Pts[I].At.Z)].push_back({int(K) + 1, int(I)});
}

void RidePlan::MouthCorner(const Mouth& M, const P2& MainAt, double Half, double Sigma, P2& Corner, P2& AlongMain, double& Reach)
{
	// The side road's edge, followed back to the ride's road's edge (a line, near enough, by the mouth).
	const P2 Left(M.Away.Z, -M.Away.X);
	const P2 Edge = M.At + Left * (Sigma * Half);
	const double Square = std::fmax(0.4, Dot(M.Away, M.Across));
	Corner = Edge + M.Away * ((M.MainHalf - Dot(Edge - MainAt, M.Across)) / Square);
	// Along the ride's road's edge, off from the mouth on this side.
	AlongMain = Dot(M.Along, Left) * Sigma >= 0.0 ? M.Along : M.Along * -1.0;
	// A 6 m radius: the rounding starts further back the sharper the corner (2 to 9 m).
	const double Angle = std::acos(std::fmin(1.0, std::fmax(-1.0, Dot(AlongMain, M.Away))));
	Reach = std::fmin(9.0, std::fmax(2.0, 6.0 / std::fmax(0.05, std::tan(Angle * 0.5))));
}

double RidePlan::Ground(double X, double Z, double Coarse) const
{
	return GroundOf(X, Z, Coarse, Coarse > 8.5);
}

double RidePlan::GroundOf(double X, double Z, double Coarse, bool NoBores) const
{
	const double Lie = Land ? Land->Height(X, Z) : std::nan("");
	NearOpts Opts;
	Opts.NoBores = NoBores;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit)) return Lie;
	// Under a bridge the land lies as it does (the river's banks, not a causeway), well clear of the
	// deck; if another road runs under it, that road's bench.
	double Clear = std::nan("");
	if (Hit.Deck && !std::isnan(Lie))
	{
		Clear = Hit.Height + Hit.Cross - 1.5;
		Opts.NoDecks = true;
		Opts.NotRoad = Hit.Which;
		if (!NearestOf(X, Z, Opts, Hit)) return std::fmin(Lie, Clear);
	}
	// (the bench lies under the road's surface as it is tilted, and level with its edge beyond)
	const double Height = Hit.Height + Hit.Cross;
	const double Wider = std::fmax(0.0, Coarse - 4.0);
	const double Flat = Hit.Half + Bench + Wider;
	const double Under = Height - Sink - std::fmin(0.45, Wider * 0.06);
	double Out = Lie;
	if (Hit.Dist <= Flat || std::isnan(Lie)) Out = Hit.Dist <= Flat + Blend ? Under : Lie;
	else if (Hit.Dist < Flat + Blend) Out = Under + (Lie - Under) * Smooth01((Hit.Dist - Flat) / Blend);
	return std::isnan(Clear) ? Out : std::fmin(Out, Clear);
}

double RidePlan::RoadHeight(double X, double Z) const
{
	double Dist = 0.0, Height = 0.0, Half = 0.0, Cross = 0.0;
	return Nearest(X, Z, Dist, Height, nullptr, &Half, nullptr, false, nullptr, &Cross) && Dist <= Half ? Height + Cross : std::nan("");
}

void RidePlan::LandTile(double X0, double Z0, double Size, int Cells, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	const int N = Cells + 1;
	const double Step = Size / Cells;
	// Heights one cell past the edge each way, for the slopes at the edge.
	const int M = N + 2;
	std::vector<double> H(size_t(M) * M);
	double Fallback = 0.0;
	bool Any = false;
	for (int J = 0; J < M; ++J)
		for (int I = 0; I < M; ++I)
		{
			const double Y = Ground(X0 + (I - 1) * Step, Z0 + (J - 1) * Step, Step);
			H[size_t(J) * M + I] = Y;
			if (!std::isnan(Y) && !Any) { Fallback = Y; Any = true; }
		}
	if (!Any) return;   // (off the world's edge: nothing to draw)
	for (double& Y : H) if (std::isnan(Y)) Y = Fallback;
	const auto At = [&H, M](int I, int J) { return H[size_t(J + 1) * M + (I + 1)]; };
	const auto Put = [&Out](double X, double Y, double Z, double Nx, double Ny, double Nz, double U, double V)
	{
		Out.Pos.push_back(float(X)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z));
		Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
		Out.Uv.push_back(float(U)); Out.Uv.push_back(float(V));
	};
	for (int J = 0; J < N; ++J)
		for (int I = 0; I < N; ++I)
		{
			const double Sx = (At(I + 1, J) - At(I - 1, J)) / (2.0 * Step), Sz = (At(I, J + 1) - At(I, J - 1)) / (2.0 * Step);
			const double Len = std::sqrt(Sx * Sx + 1.0 + Sz * Sz);
			Put(I * Step, At(I, J), J * Step, -Sx / Len, 1.0 / Len, -Sz / Len, (X0 + I * Step) / 8.0, (Z0 + J * Step) / 8.0);
		}
	const auto Quad = [&Out](uint32_t A, uint32_t B, uint32_t C, uint32_t D)
	{
		// A B across the top (x growing), C D below them (z growing): up-facing seen from above.
		Out.Idx.insert(Out.Idx.end(), {A, C, B, B, C, D});
	};
	for (int J = 0; J < Cells; ++J)
		for (int I = 0; I < Cells; ++I)
			Quad(uint32_t(J * N + I), uint32_t(J * N + I + 1), uint32_t((J + 1) * N + I), uint32_t((J + 1) * N + I + 1));
	// The skirt: each edge's points again, lower, joined to the edge.
	const double Drop = std::fmax(2.0, Step * 1.5);
	const auto Skirt = [&](int I0, int J0, int Di, int Dj, bool Flip)
	{
		const uint32_t Base = uint32_t(Out.Vertices());
		for (int K = 0; K < N; ++K)
		{
			const int I = I0 + Di * K, J = J0 + Dj * K;
			const size_t V = size_t(J * N + I) * 3;
			Put(I * Step, At(I, J) - Drop, J * Step, Out.Nrm[V], Out.Nrm[V + 1], Out.Nrm[V + 2], (X0 + I * Step) / 8.0, (Z0 + J * Step) / 8.0);
		}
		for (int K = 0; K + 1 < N; ++K)
		{
			const uint32_t A = uint32_t((J0 + Dj * K) * N + I0 + Di * K), B = uint32_t((J0 + Dj * (K + 1)) * N + I0 + Di * (K + 1));
			const uint32_t C = Base + uint32_t(K), D = Base + uint32_t(K) + 1;
			if (Flip) Out.Idx.insert(Out.Idx.end(), {A, B, C, B, D, C});
			else Out.Idx.insert(Out.Idx.end(), {A, C, B, B, C, D});
		}
	};
	Skirt(0, 0, 1, 0, true);            // north edge
	Skirt(0, Cells, 1, 0, false);       // south
	Skirt(0, 0, 0, 1, false);           // west
	Skirt(Cells, 0, 0, 1, true);        // east
}

void RidePlan::RoadPiece(int Which, int First, int Last, RideMesh& Out) const
{
	Out = RideMesh();
	if (Which < 0 || Which >= RoadCount()) return;
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	First = std::max(0, First);
	Last = std::min(int(Pts.size()) - 1, Last);
	if (Last <= First) return;
	Out.OriginX = Pts[size_t(First)].At.X;
	Out.OriginZ = Pts[size_t(First)].At.Z;
	// (a side road's paint stops short of the junctions at its ends)
	const double PaintFrom = Which == 0 ? -1.0 : 12.0, PaintTo = Which == 0 ? 1e18 : Sides[size_t(Which) - 1].Length - 12.0;
	// (the Godot game's colours, linear: tarmac, the verge's gravel, paint)
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, Verge[3] = {0.119f, 0.107f, 0.078f}, Paint[3] = {0.83f, 0.83f, 0.75f},
		Stone[3] = {0.2f, 0.19f, 0.17f}, Tile[3] = {0.51f, 0.49f, 0.43f}, Concrete[3] = {0.21f, 0.21f, 0.2f}, Soot[3] = {0.06f, 0.06f, 0.06f},
		Portal[3] = {0.34f, 0.33f, 0.3f}, Grass[3] = {0.088f, 0.105f, 0.04f};
	const int Count = int(Pts.size());
	struct Row
	{
		P2 At, Left;
		double Height = 0.0, Half = 0.0, Along = 0.0, Nx = 0.0, Ny = 1.0, Nz = 0.0;
		bool Deck = false;
		double Bank = 0.0;
		bool Bore = false, Mouth = false;   // in a tunnel; at one of its ends
	};
	std::vector<Row> Rows;
	for (int I = First; I <= Last; ++I)
	{
		const RoadPoint& P = Pts[size_t(I)];
		const RoadPoint& Before = Pts[size_t(std::max(0, I - 1))];
		const RoadPoint& After = Pts[size_t(std::min(Count - 1, I + 1))];
		P2 Dir = After.At - Before.At;
		const double Len = Dir.Length();
		Dir = Len > 1e-9 ? Dir * (1.0 / Len) : P2(0.0, -1.0);
		const double Rise = Len > 1e-9 ? (After.Height - Before.Height) / Len : 0.0;
		const double Nl = std::sqrt(1.0 + Rise * Rise);
		Row R;
		R.At = P.At;
		// Its left as you ride it: with x east and z south, left of a heading (dx, dz) is (dz, -dx).
		R.Left = P2(Dir.Z, -Dir.X);
		R.Height = P.Height;
		R.Half = P.Half;
		R.Along = P.Along;
		R.Deck = P.Bridge;
		R.Bank = P.Bank;
		R.Bore = P.Tunnel;
		if (R.Bore)
		{
			const bool Before2 = I > 0 && Pts[size_t(I) - 1].Tunnel, After2 = I + 1 < Count && Pts[size_t(I) + 1].Tunnel;
			R.Mouth = Before2 != After2;
		}
		R.Nx = -Dir.X * Rise / Nl;
		R.Ny = 1.0 / Nl;
		R.Nz = -Dir.Z * Rise / Nl;
		Rows.push_back(R);
	}
	const auto Put = [&Out](const Row& R, double Off, double Lift, const float* Colour)
	{
		// (Off is to the left of the middle)
		const P2 At = R.At + R.Left * Off;
		Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(R.Height + RoadLine::CrossHeight(Off, R.Half, R.Bank) + Lift)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
		Out.Nrm.push_back(float(R.Nx)); Out.Nrm.push_back(float(R.Ny)); Out.Nrm.push_back(float(R.Nz));
		Out.Uv.push_back(float(Off / 4.0)); Out.Uv.push_back(float(R.Along / 4.0));
		Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
	};
	// A strip between two rows from OffA to OffB (left to right: OffA the greater), up-facing.
	const auto Strip = [&Out, &Put](const Row& R0, const Row& R1, double A0, double B0, double A1, double B1, double LiftA, double LiftB, const float* Colour, bool BothSides = false)
	{
		const uint32_t V = uint32_t(Out.Vertices());
		Put(R0, A0, LiftA, Colour);
		Put(R0, B0, LiftB, Colour);
		Put(R1, A1, LiftA, Colour);
		Put(R1, B1, LiftB, Colour);
		Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
		if (BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V + 1, V + 2, V + 3});
	};
	// A tunnel's parts: any four points in the ride's frame, as a quad that faces Toward.
	struct P3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
	};
	const auto Quad3 = [&Out](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, const float* Colour, bool BothSides)
	{
		// (across its diagonals: a quad one of whose ends has closed to a point still has a facing)
		const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
		double Nx = Ay * Bz - Az * By, Ny = Az * Bx - Ax * Bz, Nz = Ax * By - Ay * Bx;
		const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
		if (Len < 1e-9) return;
		const bool Flip = Nx * (Toward.X - Q0.X) + Ny * (Toward.Y - Q0.Y) + Nz * (Toward.Z - Q0.Z) < 0.0;
		const double Sign = (Flip ? -1.0 : 1.0) / Len;
		Nx *= Sign; Ny *= Sign; Nz *= Sign;
		const uint32_t V = uint32_t(Out.Vertices());
		for (const P3* Q : {&Q0, &Q1, &Q2, &Q3})
		{
			Out.Pos.push_back(float(Q->X - Out.OriginX)); Out.Pos.push_back(float(Q->Y)); Out.Pos.push_back(float(Q->Z - Out.OriginZ));
			Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
			Out.Uv.push_back(float((Q->X + Q->Y) / 4.0)); Out.Uv.push_back(float((Q->Z + Q->Y) / 4.0));
			Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		}
		if (!Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
		if (Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
	};
	const auto Across = [](const Row& R, double Off, double Y)
	{
		const P2 At = R.At + R.Left * Off;
		P3 Q;
		Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
		return Q;
	};
	const double Pi = 3.14159265358979323846;
	// The tube's section at a row, from the road's left edge along the kerb to the wall's foot, up over
	// the arch and down to the right foot and edge (AdventureTunnels.profile).
	const auto Ring = [&Across, Pi](const Row& R)
	{
		const double E = R.Half, W = E + TubeSide;
		const double EdgeL = R.Height + RoadLine::CrossHeight(E, R.Half, R.Bank), EdgeR = R.Height + RoadLine::CrossHeight(-E, R.Half, R.Bank);
		std::vector<P3> Pts3;
		Pts3.push_back(Across(R, E, EdgeL));
		Pts3.push_back(Across(R, W, EdgeL - 0.1));
		for (int K = 0; K <= TubeArchSegs; ++K)
		{
			const double Th = Pi * K / TubeArchSegs;
			Pts3.push_back(Across(R, W * std::cos(Th), R.Height + TubeWall + TubeArch * std::sin(Th)));
		}
		Pts3.push_back(Across(R, -W, EdgeR - 0.1));
		Pts3.push_back(Across(R, -E, EdgeR));
		return Pts3;
	};
	// The hill's lid over it at a row: the ground as it would lie with no tunnel, from well out on the
	// left to well out on the right, never lower than just over the arch; its rim tucked under the land.
	const auto Lid = [this, &Across, Pi](const Row& R)
	{
		const double E = R.Half, W = E + TubeSide;
		std::vector<P3> Pts3;
		const auto Add = [&](double Off, double Least, double Tuck)
		{
			P3 Q = Across(R, Off, 0.0);
			const double G = GroundOf(Q.X, Q.Z, 0.0, true);
			Q.Y = std::fmax(std::isnan(G) ? R.Height + TubeWall + TubeArch + 3.0 : G, Least) - Tuck;
			Pts3.push_back(Q);
		};
		for (double Far : {LidReach, 16.0, 10.0, 4.0}) Add(E + Far, -1e18, Far == LidReach ? 0.4 : 0.0);
		for (int K = 0; K <= TubeArchSegs; ++K)
		{
			const double Th = Pi * K / TubeArchSegs;
			Add(W * std::cos(Th), R.Height + TubeWall + TubeArch * std::sin(Th) + 0.6, 0.0);
		}
		for (double Far : {4.0, 10.0, 16.0, LidReach}) Add(-E - Far, -1e18, Far == LidReach ? 0.4 : 0.0);
		return Pts3;
	};
	const int LidArch = 4;   // (the lid's first point over the arch)
	std::vector<P3> RingA, RingB, LidA, LidB;
	const double PaintLift = 0.015;
	for (size_t K = 0; K + 1 < Rows.size(); ++K)
	{
		Row RowA = Rows[K], RowB = Rows[K + 1];
		// A side road starts at the ride's road's edge, not under it, and its own shoulders start where
		// a mouth's rounded corners end; the ride's road has none across a mouth.
		const auto Between = [](const Row& From, const Row& To, double T)
		{
			Row R = From;
			R.At = From.At + (To.At - From.At) * T;
			R.Height += (To.Height - From.Height) * T;
			R.Along += (To.Along - From.Along) * T;
			R.Half += (To.Half - From.Half) * T;
			R.Bank += (To.Bank - From.Bank) * T;
			return R;
		};
		bool NoShoulder[2] = {false, false};   // left, right
		double ShoulderFrom[2] = {0.0, 0.0}, ShoulderTo[2] = {1.0, 1.0};   // (the part of this bit with a shoulder, 0 at A to 1 at B)
		bool UnderMain = false;
		for (int End = 0; End < 2 && Which > 0; ++End)
		{
			const SideRoad& Side = Sides[size_t(Which) - 1];
			const Mouth& M = Side.Mouths[End];
			if (!M.Is) continue;
			double Fa = End == 0 ? RowA.Along : Side.Length - RowA.Along, Fb = End == 0 ? RowB.Along : Side.Length - RowB.Along;
			if (std::fmax(Fa, Fb) <= M.From) { UnderMain = true; break; }
			if (std::fmin(Fa, Fb) < M.From)
			{
				const double T = (M.From - Fa) / (Fb - Fa);
				if (Fa < Fb) { RowA = Between(RowA, RowB, T); Fa = M.From; }
				else { RowB = Between(RowA, RowB, T); Fb = M.From; }
			}
			// (the left as this road's points run is Away's left from its first end, its right from its last)
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				const double Clear = M.Clear[End == 0 ? Sd : 1 - Sd];
				if (std::fmax(Fa, Fb) <= Clear) NoShoulder[Sd] = true;
				else if (std::fmin(Fa, Fb) < Clear)
				{
					const double T = (Clear - Fa) / (Fb - Fa);
					if (Fa < Fb) ShoulderFrom[Sd] = std::fmax(ShoulderFrom[Sd], T);
					else ShoulderTo[Sd] = std::fmin(ShoulderTo[Sd], T);
				}
			}
		}
		if (UnderMain) continue;
		if (Which == 0 && size_t(First) + K + 1 < MouthAt.size())
		{
			const unsigned char Both = MouthAt[size_t(First) + K] & MouthAt[size_t(First) + K + 1];
			NoShoulder[0] = (Both & 4) != 0;
			NoShoulder[1] = (Both & 8) != 0;
		}
		const Row& A = RowA;
		const Row& B = RowB;
		if (!(A.Bore && B.Bore)) { RingA.clear(); LidA.clear(); }
		// The tarmac, in two halves either side of the crown.
		Strip(A, B, A.Half, 0.0, B.Half, 0.0, 0.0, 0.0, Tarmac);
		Strip(A, B, 0.0, -A.Half, 0.0, -B.Half, 0.0, 0.0, Tarmac);
		if (A.Deck && B.Deck)
		{
			// A bridge: a stone parapet each side (its top, and its faces down past the deck to the
			// deck's underside), and the underside. Faces that stand upright are drawn from both sides.
			const double In = 0.15, Out2 = 0.55, Top = 1.0, Under = -1.2;
			for (double Side : {1.0, -1.0})
			{
				const double A0 = A.Half + In, A1 = A.Half + Out2, B0 = B.Half + In, B1 = B.Half + Out2;
				// (kerb strip, top, inner face, outer face)
				if (Side > 0.0)
				{
					Strip(A, B, A0, A.Half, B0, B.Half, 0.0, 0.0, Stone);
					Strip(A, B, A1, A0, B1, B0, Top, Top, Stone);
				}
				else
				{
					Strip(A, B, -A.Half, -A0, -B.Half, -B0, 0.0, 0.0, Stone);
					Strip(A, B, -A0, -A1, -B0, -B1, Top, Top, Stone);
				}
				Strip(A, B, Side * A0, Side * A0, Side * B0, Side * B0, Top, 0.0, Stone, true);
				Strip(A, B, Side * A1, Side * A1, Side * B1, Side * B1, Top, Under, Stone, true);
			}
			Strip(A, B, A.Half + Out2, -A.Half - Out2, B.Half + Out2, -B.Half - Out2, Under, Under, Stone, true);
		}
		else if (A.Bore && B.Bore)
		{
			// A tunnel: the tube in place of the shoulders, seen from inside; the hill laid back over it.
			if (RingA.empty()) { RingA = Ring(A); LidA = Lid(A); }
			RingB = Ring(B);
			LidB = Lid(B);
			P3 Axis = Across(A, 0.0, A.Height + TubeWall * 0.5), Sky = Across(A, 0.0, A.Height + 1e5);
			for (size_t Q = 0; Q + 1 < RingA.size(); ++Q)
			{
				const bool Kerb = Q == 0 || Q + 2 == RingA.size(), Wall = Q == 1 || Q + 3 == RingA.size();
				Quad3(RingA[Q], RingB[Q], RingB[Q + 1], RingA[Q + 1], Axis, Kerb ? Tile : Wall ? Concrete : Soot, true);
			}
			for (size_t Q = 0; Q + 1 < LidA.size(); ++Q) Quad3(LidA[Q], LidA[Q + 1], LidB[Q + 1], LidB[Q], Sky, Grass, false);
			// A mouth: a headwall from the arch and the cutting's floor up to the lid.
			for (const Row* M : {&A, &B})
			{
				if (!M->Mouth) continue;   // (drawn once: by the bit of tube it ends)
				const std::vector<P3>& Rg = M == &A ? RingA : RingB;
				const std::vector<P3>& Ld = M == &A ? LidA : LidB;
				const Row& Inner = M == &A ? B : A;
				P3 Face = Across(*M, 0.0, M->Height + 2.0);
				Face.X += (M->At.X - Inner.At.X) * 10.0;
				Face.Z += (M->At.Z - Inner.At.Z) * 10.0;
				for (int Q = 0; Q < TubeArchSegs; ++Q) Quad3(Rg[size_t(Q) + 2], Rg[size_t(Q) + 3], Ld[size_t(LidArch + Q) + 1], Ld[size_t(LidArch + Q)], Face, Portal, true);
				const auto Foot = [this, &Rg, &Ld](size_t J)
				{
					// (under a point of the lid beside the arch: the wall's foot, or the cutting's floor)
					P3 Q = Ld[J];
					if (J == size_t(LidArch)) Q.Y = Rg[1].Y;
					else if (J == size_t(LidArch + TubeArchSegs)) Q.Y = Rg[Rg.size() - 2].Y;
					else
					{
						const double G = Ground(Q.X, Q.Z);
						Q.Y = std::fmin(Q.Y, (std::isnan(G) ? Q.Y : G) - 0.3);
					}
					return Q;
				};
				for (size_t J = 0; J + 1 < Ld.size(); ++J)
				{
					if (J >= size_t(LidArch) && J < size_t(LidArch + TubeArchSegs)) continue;
					Quad3(Foot(J), Foot(J + 1), Ld[J + 1], Ld[J], Face, Portal, true);
				}
			}
			RingA.swap(RingB);
			LidA.swap(LidB);
		}
		else
		{
			// The left shoulder (from down in the ground up to the edge) and the right.
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				if (NoShoulder[Sd] || ShoulderTo[Sd] <= ShoulderFrom[Sd]) continue;
				const Row Sa = ShoulderFrom[Sd] > 0.0 ? Between(A, B, ShoulderFrom[Sd]) : A, Sb = ShoulderTo[Sd] < 1.0 ? Between(A, B, ShoulderTo[Sd]) : B;
				if (Sd == 0) Strip(Sa, Sb, Sa.Half + Shoulder, Sa.Half, Sb.Half + Shoulder, Sb.Half, -ShoulderDrop, 0.0, Verge);
				else Strip(Sa, Sb, -Sa.Half, -Sa.Half - Shoulder, -Sb.Half, -Sb.Half - Shoulder, 0.0, -ShoulderDrop, Verge);
			}
		}
		// Paint: edge lines on a road 6 m wide or more, and where there are two lanes a centre line of
		// 3 m dashes with 6 m gaps.
		const double Half = std::fmin(A.Half, B.Half);
		if (A.Along < PaintFrom || B.Along > PaintTo) continue;
		if (Half >= 2.95)
		{
			for (double Side : {1.0, -1.0})
			{
				// (none across a junction's mouth)
				const size_t At0 = size_t(First) + K;
				if (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) continue;
				Strip(A, B, Side * (A.Half - 0.3) + 0.065, Side * (A.Half - 0.3) - 0.065, Side * (B.Half - 0.3) + 0.065, Side * (B.Half - 0.3) - 0.065, PaintLift, PaintLift, Paint);
			}
		}
		if (Half >= 2.5 && std::fmod(A.Along, 9.0) < 3.0) Strip(A, B, 0.06, -0.06, 0.06, -0.06, PaintLift, PaintLift, Paint);
	}
	// A side road's mouths on the ride's road, each with the piece its end is in.
	for (int End = 0; End < 2 && Which > 0; ++End)
	{
		const SideRoad& Side = Sides[size_t(Which) - 1];
		const Mouth& M = Side.Mouths[End];
		if (!M.Is || (End == 0 ? First != 0 : Last != Count - 1)) continue;
		const double Half = Pts[size_t(End == 0 ? 0 : Count - 1)].Half;
		const P2 MainAt = Road[size_t(M.MainIndex)].At;
		// The surface of one road at a point (past its edge, its edge's height).
		const auto Surface = [this](int Of, const P2& At, double Else)
		{
			NearOpts Opts;
			Opts.OnlyRoad = Of;
			NearHit Hit;
			return NearestOf(At.X, At.Z, Opts, Hit) ? Hit.Height + Hit.Cross : Else;
		};
		const double MouthH = Surface(0, M.At, Pts[size_t(End == 0 ? 0 : Count - 1)].Height);
		const auto At3 = [](const P2& At, double Y)
		{
			P3 Q;
			Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
			return Q;
		};
		const P3 Sky = At3(M.At, MouthH + 1e5);
		// The mouth's tarmac, as a fan from just inside the ride's road's edge: out to a curve round each
		// corner between the two roads' edges (about a 6 m radius), and across the side road between
		// them, a hair over both roads' own tarmac and the ride's road's shoulder. A shoulder round each curve.
		const P2 Hub = M.At - M.Across * 0.5;
		// (no lower than the road's edge beside it, where the road is banked up towards this side)
		const P3 Hub3 = At3(Hub, std::fmax(Surface(0, Hub, MouthH), MouthH) + 0.02);
		const int Segs = 6;
		P2 Rim[2 * (Segs + 1)];
		double RimH[2 * (Segs + 1)];
		for (int Sd = 0; Sd < 2; ++Sd)
		{
			const double Sigma = Sd == 0 ? 1.0 : -1.0;
			P2 Corner, AlongMain;
			double Reach = 0.0;
			MouthCorner(M, MainAt, Half, Sigma, Corner, AlongMain, Reach);
			// (the roads bend: each end of the curve, and the corner, moved onto its road's edge as it really runs)
			const auto ToEdge = [this](int Of, const P2& At)
			{
				NearOpts Opts;
				Opts.OnlyRoad = Of;
				NearHit Hit;
				if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist < 0.5) return At;
				return Hit.Foot + (At - Hit.Foot) * (Hit.Half / Hit.Dist);
			};
			Corner = ToEdge(0, Corner);
			const P2 OnMain = ToEdge(0, Corner + AlongMain * Reach), OnSide = ToEdge(Which, Corner + M.Away * Reach);
			const double MainH = Surface(0, OnMain, MouthH) + 0.01, SideH = Surface(Which, OnSide, MouthH) + 0.01;
			P2 CurveAt[Segs + 1], CurveOut[Segs + 1];
			double CurveH[Segs + 1];
			for (int S = 0; S <= Segs; ++S)
			{
				const double U = double(S) / Segs;
				CurveAt[S] = OnMain * ((1.0 - U) * (1.0 - U)) + Corner * (2.0 * U * (1.0 - U)) + OnSide * (U * U);
				CurveH[S] = MainH + (SideH - MainH) * U;
				// (out from the corner, square to the curve)
				const P2 Tangent = (Corner - OnMain) * (1.0 - U) + (OnSide - Corner) * U;
				P2 Outward(Tangent.Z, -Tangent.X);
				if (Dot(Outward, CurveAt[S] - Hub) < 0.0) Outward = Outward * -1.0;
				const double Len = Outward.Length();
				// (the shoulder's outer edge; on a tight curve it gathers to a point rather than cross itself)
				CurveOut[S] = CurveAt[S] + (Len > 1e-9 ? Outward * (Shoulder / Len) : P2(0.0, 0.0));
				if (S > 0 && Dot(CurveOut[S] - CurveOut[S - 1], CurveAt[S] - CurveAt[S - 1]) <= 0.0) CurveOut[S] = CurveOut[S - 1];
				// (round the rim: the left corner from the ride's road to the side road, then the right one back)
				const int At4 = Sd == 0 ? S : 2 * Segs + 1 - S;
				Rim[At4] = CurveAt[S];
				RimH[At4] = CurveH[S];
			}
			for (int S = 0; S < Segs; ++S)
			{
				// (as two triangles, each made to face up: on a tight curve the four points may not lie in order)
				const P3 In0 = At3(CurveAt[S], CurveH[S]), In1 = At3(CurveAt[S + 1], CurveH[S + 1]), Out0 = At3(CurveOut[S], CurveH[S] - ShoulderDrop), Out1 = At3(CurveOut[S + 1], CurveH[S + 1] - ShoulderDrop);
				Quad3(In0, In1, Out1, In0, Sky, Verge, false);
				Quad3(In0, Out1, Out0, In0, Sky, Verge, false);
			}
		}
		for (int S = 0; S + 1 < 2 * (Segs + 1); ++S) Quad3(Hub3, At3(Rim[S], RimH[S]), At3(Rim[S + 1], RimH[S + 1]), Hub3, Sky, Tarmac, false);
		if (!M.GiveWay) continue;
		// Give way: two broken lines (0.6 m marks, 0.3 m gaps, 0.2 m wide, 0.3 m apart) along the ride's
		// road's edge, across the lane that comes up to it (the whole of a single-track road).
		const P2 Left(M.Away.Z, -M.Away.X);
		const double Slant = Dot(M.Along, Left);
		if (std::fabs(Slant) < 0.3) continue;
		// (along the ride's road's edge from the side road's middle: U to its left edge is Half / Slant)
		const double ToLeft = Half / Slant;
		const double U0 = Half >= 2.5 ? 0.0 : ToLeft, U1 = -ToLeft;   // (the lane coming up to the junction is on the right, looking away from it)
		const double Lo = std::fmin(U0, U1) + 0.15, Hi = std::fmax(U0, U1) - 0.15;
		for (double Back : {0.2, 0.7})
			for (double U = Lo; U + 0.6 <= Hi + 1e-9; U += 0.9)
			{
				const P2 A0 = M.At + M.Along * U + M.Across * (Back - 0.1), A1 = M.At + M.Along * (U + 0.6) + M.Across * (Back - 0.1);
				const P2 B0 = A0 + M.Across * 0.2, B1 = A1 + M.Across * 0.2;
				// (on whichever is the higher there: the side road's surface or the threshold's)
				const auto Top = [&Surface, Which, MouthH](const P2& At) { return std::fmax(Surface(Which, At, MouthH), Surface(0, At, MouthH)) + 0.04; };
				Quad3(At3(A0, Top(A0)), At3(A1, Top(A1)), At3(B1, Top(B1)), At3(B0, Top(B0)), Sky, Paint, false);
			}
	}
}

std::string RidePlan::RouteJson() const
{
	Json J = Json::MakeObject();
	const auto Set = [&J](const char* Key, Json V) { J.O.emplace_back(Key, std::move(V)); };
	const auto Triple = [](double A, double B, double C) { Json T = Json::MakeArray(); T.A.push_back(Json::MakeNumber(A)); T.A.push_back(Json::MakeNumber(B)); T.A.push_back(Json::MakeNumber(C)); return T; };
	std::string Called = Title;
	if (Called.empty()) Called = Names.empty() ? std::string("Ride UK") : "Ride UK: " + Names.front() + " to " + Names.back();
	Set("name", Json::MakeString(Called));
	Set("length_m", Json::MakeNumber(Length));
	Set("road_half_width_m", Json::MakeNumber(WidestHalf));
	Json Geo = Json::MakeArray();
	Geo.A.push_back(Json::MakeNumber(Area.Roads.FrameLat));
	Geo.A.push_back(Json::MakeNumber(Area.Roads.FrameLon));
	Set("origin_geo", std::move(Geo));
	Json Line = Json::MakeArray();
	// (a point every 10 m or so is what the bike's own route has)
	for (size_t I = 0; I < Road.size(); I += (I + 3 < Road.size() ? 3 : 1)) Line.A.push_back(Triple(Road[I].At.X, Road[I].Height, Road[I].At.Z));
	Set("line_sim", std::move(Line));
	if (Road.size() >= 2)
	{
		Json Spawn = Json::MakeObject();
		const P2 D = Road[std::min<size_t>(3, Road.size() - 1)].At - Road[0].At;
		// (a little above the road, as the Buttertubs route's is: the bike settles onto it)
		Spawn.O.emplace_back("sim", Triple(Road[0].At.X, Road[0].Height + 0.35, Road[0].At.Z));
		Spawn.O.emplace_back("ue", Triple(Road[0].At.X * 100.0, Road[0].At.Z * 100.0, (Road[0].Height + 0.35) * 100.0));
		// (the sim's heading: 0 faces north, growing as it turns left)
		Spawn.O.emplace_back("heading", Json::MakeNumber(std::atan2(-D.X, -D.Z)));
		Set("spawn", std::move(Spawn));
	}
	Json Credits = Json::MakeArray();
	for (const std::string& C : Area.Roads.Credits) Credits.A.push_back(Json::MakeString(C));
	Set("credits", std::move(Credits));
	return J.Dump();
}
} // namespace TorqueWorld
