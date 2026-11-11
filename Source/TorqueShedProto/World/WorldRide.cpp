#include "WorldRide.h"

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

// Corners cut once (Chaikin), ends kept: the map's road lines turn at points, a road doesn't.
std::vector<P2> Rounded(const std::vector<P2>& Line)
{
	if (Line.size() < 3) return Line;
	std::vector<P2> Out;
	Out.reserve(Line.size() * 2);
	Out.push_back(Line.front());
	for (size_t I = 0; I + 1 < Line.size(); ++I)
	{
		const P2 A = Line[I], B = Line[I + 1];
		// (short bits are left alone: cutting them only makes them shorter)
		if (A.DistanceTo(B) < 2.0) { if (I + 2 < Line.size()) Out.push_back(B); continue; }
		if (I > 0) Out.push_back(A + (B - A) * 0.25);
		if (I + 2 < Line.size()) Out.push_back(A + (B - A) * 0.75);
	}
	Out.push_back(Line.back());
	return Out;
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
	const std::vector<int> Path = G.ShortestPath(From, To);
	if (Path.empty()) { R->Why = "There is no way by road between them."; return R; }
	for (int E : Path)
	{
		const std::string Name = G.NameOf(E);
		if (R->Names.empty() || R->Names.back() != Name) R->Names.push_back(Name);
	}
	const std::vector<P2> Line = Resample(Rounded(G.PathLine(From, Path)), 4.0);
	// (the tiles are chosen by the road as the map has it, a point every 30 m, as every rider's are)
	R->Land = R->Area.Land->ForRoad(Resample(G.PathLine(From, Path), 30.0));
	Tell("Fetching the ground: " + std::to_string(R->Land->UsedKeys().size()) + " tiles");
	R->TilesFailed = FetchGround(Store, Heights, *R->Land);
	if (R->TilesFailed > 0) { R->Why = std::to_string(R->TilesFailed) + " of the ground's tiles couldn't be fetched."; return R; }
	Tell("Laying the road");
	R->LayRoad(Line);
	if (R->Road.size() < 2) { R->Why = "The road has no ground under it."; return R; }
	R->Ok = true;
	return R;
}

void RidePlan::LayRoad(const std::vector<P2>& Line)
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
		H[I] = std::isnan(Y) ? Last : Y;   // (a hole in the ground is bridged at the last height known)
	}
	if (!Any) return;
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
	double Along = 0.0;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		if (I > 0) Along += Line[I].DistanceTo(Line[I - 1]);
		RoadPoint P;
		P.At = Line[I];
		P.Along = Along;
		P.Height = H[I];
		Near[NearKey(P.At.X, P.At.Z)].push_back(int(Road.size()));
		Road.push_back(P);
	}
	Length = Along;
}

bool RidePlan::Nearest(double X, double Z, double& Dist, double& Height, int* Index) const
{
	double Best = 1e18, BestH = 0.0;
	int BestI = -1;
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	for (long long Dx = -3; Dx <= 3; ++Dx)
		for (long long Dz = -3; Dz <= 3; ++Dz)
		{
			const auto It = Near.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == Near.end()) continue;
			for (int I : It->second)
			{
				// The nearest of the bit of road before this point and the bit after it.
				for (int S = std::max(0, I - 1); S <= I && S + 1 < int(Road.size()); ++S)
				{
					const P2 A = Road[size_t(S)].At, B = Road[size_t(S) + 1].At;
					const P2 Ab = B - A;
					const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
					const double T = L2 > 1e-12 ? std::fmin(1.0, std::fmax(0.0, ((X - A.X) * Ab.X + (Z - A.Z) * Ab.Z) / L2)) : 0.0;
					const double Px = A.X + Ab.X * T, Pz = A.Z + Ab.Z * T;
					const double D2 = (X - Px) * (X - Px) + (Z - Pz) * (Z - Pz);
					if (D2 < Best)
					{
						Best = D2;
						BestH = Road[size_t(S)].Height + (Road[size_t(S) + 1].Height - Road[size_t(S)].Height) * T;
						BestI = T < 0.5 ? S : S + 1;
					}
				}
			}
		}
	if (BestI < 0 || Best > 40.0 * 40.0) return false;
	Dist = std::sqrt(Best);
	Height = BestH;
	if (Index != nullptr) *Index = BestI;
	return true;
}

double RidePlan::Ground(double X, double Z) const
{
	const double Lie = Land ? Land->Height(X, Z) : std::nan("");
	double Dist = 0.0, Height = 0.0;
	if (!Nearest(X, Z, Dist, Height)) return Lie;
	const double Flat = HalfWidth + Bench;
	if (Dist <= Flat || std::isnan(Lie)) return Dist <= Flat + Blend ? Height - Sink : Lie;
	if (Dist >= Flat + Blend) return Lie;
	const double T = Smooth01((Dist - Flat) / Blend);
	return (Height - Sink) + (Lie - (Height - Sink)) * T;
}

double RidePlan::RoadHeight(double X, double Z) const
{
	double Dist = 0.0, Height = 0.0;
	return Nearest(X, Z, Dist, Height) && Dist <= HalfWidth ? Height : std::nan("");
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
			const double Y = Ground(X0 + (I - 1) * Step, Z0 + (J - 1) * Step);
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

void RidePlan::RoadPiece(int First, int Last, RideMesh& Out) const
{
	Out = RideMesh();
	First = std::max(0, First);
	Last = std::min(int(Road.size()) - 1, Last);
	if (Last <= First) return;
	Out.OriginX = Road[size_t(First)].At.X;
	Out.OriginZ = Road[size_t(First)].At.Z;
	for (int I = First; I <= Last; ++I)
	{
		const RoadPoint& P = Road[size_t(I)];
		const P2 Before = Road[size_t(std::max(0, I - 1))].At, After = Road[size_t(std::min(int(Road.size()) - 1, I + 1))].At;
		P2 Dir = After - Before;
		const double Len = Dir.Length();
		Dir = Len > 1e-9 ? Dir * (1.0 / Len) : P2(0.0, -1.0);
		// Its left as you ride it: with x east and z south, left of a heading (dx, dz) is (dz, -dx).
		const P2 Left(Dir.Z, -Dir.X);
		const double Rise = Len > 1e-9 ? (Road[size_t(std::min(int(Road.size()) - 1, I + 1))].Height - Road[size_t(std::max(0, I - 1))].Height) / Len : 0.0;
		const double Nl = std::sqrt(1.0 + Rise * Rise);
		for (int Side = 0; Side < 2; ++Side)
		{
			const P2 At = P.At + Left * (Side == 0 ? HalfWidth : -HalfWidth);
			Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(P.Height)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
			Out.Nrm.push_back(float(-Dir.X * Rise / Nl)); Out.Nrm.push_back(float(1.0 / Nl)); Out.Nrm.push_back(float(-Dir.Z * Rise / Nl));
			Out.Uv.push_back(Side == 0 ? 0.0f : 1.0f); Out.Uv.push_back(float(P.Along / (2.0 * HalfWidth)));
		}
	}
	for (int I = 0; I < Last - First; ++I)
	{
		// (left, right) of this point, then of the next: up-facing whichever way the road runs.
		const uint32_t L0 = uint32_t(I * 2), R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
		Out.Idx.insert(Out.Idx.end(), {L0, R0, L1, R0, R1, L1});
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
	Set("road_half_width_m", Json::MakeNumber(HalfWidth));
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
