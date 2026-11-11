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
	R->Ok = true;
	return R;
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
		WidestHalf = std::fmax(WidestHalf, P.Half);
		Near[NearKey(P.At.X, P.At.Z)].push_back(int(Road.size()));
		Road.push_back(P);
	}
	Length = Along;
}

bool RidePlan::Nearest(double X, double Z, double& Dist, double& Height, int* Index, double* Half) const
{
	double Best = 1e18, BestH = 0.0, BestHalf = 0.0;
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
						BestHalf = Road[size_t(S)].Half + (Road[size_t(S) + 1].Half - Road[size_t(S)].Half) * T;
						BestI = T < 0.5 ? S : S + 1;
					}
				}
			}
		}
	if (BestI < 0 || Best > 40.0 * 40.0) return false;
	Dist = std::sqrt(Best);
	Height = BestH;
	if (Index != nullptr) *Index = BestI;
	if (Half != nullptr) *Half = BestHalf;
	return true;
}

double RidePlan::Ground(double X, double Z, double Coarse) const
{
	const double Lie = Land ? Land->Height(X, Z) : std::nan("");
	double Dist = 0.0, Height = 0.0, Half = 0.0;
	if (!Nearest(X, Z, Dist, Height, nullptr, &Half)) return Lie;
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

void RidePlan::RoadPiece(int First, int Last, RideMesh& Out) const
{
	Out = RideMesh();
	First = std::max(0, First);
	Last = std::min(int(Road.size()) - 1, Last);
	if (Last <= First) return;
	Out.OriginX = Road[size_t(First)].At.X;
	Out.OriginZ = Road[size_t(First)].At.Z;
	// (the Godot game's colours, linear: tarmac, the verge's gravel, paint)
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, Verge[3] = {0.119f, 0.107f, 0.078f}, Paint[3] = {0.83f, 0.83f, 0.75f};
	const int Count = int(Road.size());
	struct Row
	{
		P2 At, Left;
		double Height = 0.0, Half = 0.0, Along = 0.0, Nx = 0.0, Ny = 1.0, Nz = 0.0;
	};
	std::vector<Row> Rows;
	for (int I = First; I <= Last; ++I)
	{
		const RoadPoint& P = Road[size_t(I)];
		const RoadPoint& Before = Road[size_t(std::max(0, I - 1))];
		const RoadPoint& After = Road[size_t(std::min(Count - 1, I + 1))];
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
	const auto Strip = [&Out, &Put](const Row& R0, const Row& R1, double A0, double B0, double A1, double B1, double LiftA, double LiftB, const float* Colour)
	{
		const uint32_t V = uint32_t(Out.Vertices());
		Put(R0, A0, LiftA, Colour);
		Put(R0, B0, LiftB, Colour);
		Put(R1, A1, LiftA, Colour);
		Put(R1, B1, LiftB, Colour);
		Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
	};
	const double PaintLift = 0.015;
	for (size_t K = 0; K + 1 < Rows.size(); ++K)
	{
		const Row& A = Rows[K];
		const Row& B = Rows[K + 1];
		// The left shoulder (from down in the ground up to the edge), the tarmac, the right shoulder.
		Strip(A, B, A.Half + Shoulder, A.Half, B.Half + Shoulder, B.Half, -ShoulderDrop, 0.0, Verge);
		Strip(A, B, A.Half, -A.Half, B.Half, -B.Half, 0.0, 0.0, Tarmac);
		Strip(A, B, -A.Half, -A.Half - Shoulder, -B.Half, -B.Half - Shoulder, 0.0, -ShoulderDrop, Verge);
		// Paint: edge lines on a road 6 m wide or more, and where there are two lanes a centre line of
		// 3 m dashes with 6 m gaps.
		const double Half = std::fmin(A.Half, B.Half);
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
