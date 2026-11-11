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
	std::vector<double> Halves(Line.size(), 2.3);
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
	R->LayRoad(Line, Halves);
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
	for (size_t I = 0; I < Line.size(); ++I) Grid[Key(Line[I].X, Line[I].Z)].push_back(int(I));
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
		if (Sp.Kind != Kind) continue;
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

void RidePlan::LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves)
{
	Road.clear();
	Near.clear();
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
	const std::vector<int> BridgeKnots = SpansAlong(Line, 0), TunnelKnots = SpansAlong(Line, 1);
	Bridges = int(BridgeKnots.size() / 2);
	Tunnels = int(TunnelKnots.size() / 2);
	std::vector<char> OnDeck(Line.size(), 0);
	const auto Level = [&H](const std::vector<int>& Knots)
	{
		for (size_t K = 0; K + 1 < Knots.size(); K += 2)
		{
			const int I0 = Knots[K], I1 = std::min(Knots[K + 1], int(H.size()) - 1);
			for (int I = I0 + 1; I < I1; ++I) H[size_t(I)] = H[size_t(I0)] + (H[size_t(I1)] - H[size_t(I0)]) * double(I - I0) / double(I1 - I0);
		}
	};
	Level(BridgeKnots);
	Level(TunnelKnots);
	for (size_t K = 0; K + 1 < BridgeKnots.size(); K += 2)
		for (int I = BridgeKnots[K]; I <= BridgeKnots[K + 1] && I < int(Line.size()); ++I) OnDeck[size_t(I)] = 1;
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
		P.Bridge = OnDeck[I] != 0;
		WidestHalf = std::fmax(WidestHalf, P.Half);
		Near[NearKey(P.At.X, P.At.Z)].push_back({0, int(Road.size())});
		Road.push_back(P);
	}
	Length = Along;
}

bool RidePlan::Nearest(double X, double Z, double& Dist, double& Height, int* Index, double* Half, int* Which, bool MainOnly, bool* OnBridge) const
{
	bool BestDeck = false;
	// (best by how far outside the road's edge the point is, so a point on a lane beside a main road
	// is the lane's; the ride's own road wins a tie)
	double Best = 1e18, BestD = 0.0, BestH = 0.0, BestHalf = 0.0;
	int BestI = -1, BestRoad = 0;
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	for (long long Dx = -3; Dx <= 3; ++Dx)
		for (long long Dz = -3; Dz <= 3; ++Dz)
		{
			const auto It = Near.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == Near.end()) continue;
			for (const std::pair<int, int>& Entry : It->second)
			{
				if (MainOnly && Entry.first != 0) continue;
				const std::vector<RoadPoint>& Pts = RoadOf(Entry.first);
				const int I = Entry.second;
				// The bit of road from this point to the next (the last point's is filed under the one before).
				for (int S = I; S == I && S + 1 < int(Pts.size()); ++S)
				{
					const RoadPoint& Pa = Pts[size_t(S)];
					const RoadPoint& Pb = Pts[size_t(S) + 1];
					const P2 Ab = Pb.At - Pa.At;
					const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
					const double T = L2 > 1e-12 ? std::fmin(1.0, std::fmax(0.0, ((X - Pa.At.X) * Ab.X + (Z - Pa.At.Z) * Ab.Z) / L2)) : 0.0;
					const double Px = Pa.At.X + Ab.X * T, Pz = Pa.At.Z + Ab.Z * T;
					const double D2 = (X - Px) * (X - Px) + (Z - Pz) * (Z - Pz);
					if (D2 > 40.0 * 40.0) continue;
					const double D = std::sqrt(D2);
					const double HalfHere = Pa.Half + (Pb.Half - Pa.Half) * T;
					const double Score = D - HalfHere - (Entry.first == 0 ? 0.01 : 0.0);
					if (Score < Best)
					{
						Best = Score;
						BestD = D;
						BestH = Pa.Height + (Pb.Height - Pa.Height) * T;
						BestHalf = HalfHere;
						BestI = T < 0.5 ? S : S + 1;
						BestRoad = Entry.first;
						BestDeck = Pa.Bridge && Pb.Bridge;
					}
				}
			}
		}
	if (BestI < 0) return false;
	Dist = BestD;
	Height = BestH;
	if (Index != nullptr) *Index = BestI;
	if (Half != nullptr) *Half = BestHalf;
	if (Which != nullptr) *Which = BestRoad;
	if (OnBridge != nullptr) *OnBridge = BestDeck;
	return true;
}

void RidePlan::LaySides(const std::vector<int>& Path)
{
	Sides.clear();
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
		H = RoadLine::GradeLimit(RoadLine::SmoothHeights(RoadLine::SmoothHeights(H, 9), 9));
		SideRoad Side;
		Side.Length = Along.back();
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
		for (size_t I = 0; I < Line.size(); ++I)
		{
			double Y = H[I];
			double D = 0.0, MainH = 0.0, MainHalf = 0.0;
			if (Nearest(Line[I].X, Line[I].Z, D, MainH, nullptr, &MainHalf, nullptr, true))
				Y += (MainH - Y) * (1.0 - Smooth01((D - MainHalf - 2.0) / 30.0));
			RoadPoint P;
			P.At = Line[I];
			P.Along = Along[I];
			P.Height = Y;
			P.Half = Half;
			Side.Pts.push_back(P);
		}
		Sides.push_back(std::move(Side));
	}
	// (filed once all are laid, so none was pulled to another side road's height)
	for (size_t K = 0; K < Sides.size(); ++K)
		for (size_t I = 0; I < Sides[K].Pts.size(); ++I) Near[NearKey(Sides[K].Pts[I].At.X, Sides[K].Pts[I].At.Z)].push_back({int(K) + 1, int(I)});
}

double RidePlan::Ground(double X, double Z, double Coarse) const
{
	const double Lie = Land ? Land->Height(X, Z) : std::nan("");
	double Dist = 0.0, Height = 0.0, Half = 0.0;
	bool Deck = false;
	if (!Nearest(X, Z, Dist, Height, nullptr, &Half, nullptr, false, &Deck)) return Lie;
	// (under a bridge the land lies as it does: the river's banks, not a causeway)
	if (Deck && !std::isnan(Lie)) return std::fmin(Lie, Height - 1.5);
	const double Wider = std::fmax(0.0, Coarse - 4.0);
	const double Flat = Half + Bench + Wider;
	const double Under = Height - Sink - std::fmin(0.45, Wider * 0.06);
	if (Dist <= Flat || std::isnan(Lie)) return Dist <= Flat + Blend ? Under : Lie;
	if (Dist >= Flat + Blend) return Lie;
	const double T = Smooth01((Dist - Flat) / Blend);
	return Under + (Lie - Under) * T;
}

double RidePlan::RoadHeight(double X, double Z) const
{
	double Dist = 0.0, Height = 0.0, Half = 0.0;
	return Nearest(X, Z, Dist, Height, nullptr, &Half) && Dist <= Half ? Height : std::nan("");
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
		Stone[3] = {0.2f, 0.19f, 0.17f};
	const int Count = int(Pts.size());
	struct Row
	{
		P2 At, Left;
		double Height = 0.0, Half = 0.0, Along = 0.0, Nx = 0.0, Ny = 1.0, Nz = 0.0;
		bool Deck = false;
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
		R.Nx = -Dir.X * Rise / Nl;
		R.Ny = 1.0 / Nl;
		R.Nz = -Dir.Z * Rise / Nl;
		Rows.push_back(R);
	}
	const auto Put = [&Out](const Row& R, double Off, double Lift, const float* Colour)
	{
		// (Off is to the left of the middle)
		const P2 At = R.At + R.Left * Off;
		Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(R.Height + Lift)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
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
	const double PaintLift = 0.015;
	for (size_t K = 0; K + 1 < Rows.size(); ++K)
	{
		const Row& A = Rows[K];
		const Row& B = Rows[K + 1];
		Strip(A, B, A.Half, -A.Half, B.Half, -B.Half, 0.0, 0.0, Tarmac);
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
		else
		{
			// The left shoulder (from down in the ground up to the edge) and the right.
			Strip(A, B, A.Half + Shoulder, A.Half, B.Half + Shoulder, B.Half, -ShoulderDrop, 0.0, Verge);
			Strip(A, B, -A.Half, -A.Half - Shoulder, -B.Half, -B.Half - Shoulder, 0.0, -ShoulderDrop, Verge);
		}
		// Paint: edge lines on a road 6 m wide or more, and where there are two lanes a centre line of
		// 3 m dashes with 6 m gaps.
		const double Half = std::fmin(A.Half, B.Half);
		if (A.Along < PaintFrom || B.Along > PaintTo) continue;
		if (Half >= 2.95)
		{
			for (double Side : {1.0, -1.0})
				Strip(A, B, Side * (A.Half - 0.3) + 0.065, Side * (A.Half - 0.3) - 0.065, Side * (B.Half - 0.3) + 0.065, Side * (B.Half - 0.3) - 0.065, PaintLift, PaintLift, Paint);
		}
		if (Half >= 2.5 && std::fmod(A.Along, 9.0) < 3.0) Strip(A, B, 0.06, -0.06, 0.06, -0.06, PaintLift, PaintLift, Paint);
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
