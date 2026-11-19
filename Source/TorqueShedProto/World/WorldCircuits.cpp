// [race tracks] The UK's race circuits in the ride's roads (the plan "UK race tracks you can ride", stages
// 1 to 3): each cell's circuit file (the world repo's scripts/circuit_bake.py, from OpenStreetMap's
// highway=raceway) read into the region, then its tracks and pit lanes made roads like any other, so the
// ride anywhere network lays them, the bike rides them and the riders reach them by road.
//
// A circuit is a road of class "raceway" (its pit lane "pit_lane"), as wide as the map says (else 12 m,
// a pit lane 8 m), painted as a circuit is (RidePlan::CircuitMarks, WorldRideCircuits.cpp) and with none
// of a public road's furniture. Its edges come after every cell's own (from CircuitFirst), so a cell's
// roads keep their numbers; as the area grows they are numbered again (RidePlan::Grow draws them again).
//
// It joins the public roads where one reaches it: a road's dead end within 25 m of the track, or any
// junction within 4 m, is joined to the nearest point of it. A circuit no road reaches (the map draws the
// paddock's roads as service roads, which often stop short) gets a paddock road (class "service") from
// its nearest point to the nearest junction of the network within 600 m.
#include "WorldRegion.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace TorqueWorld
{
using TorqueFit::Json;

namespace
{
inline double F32c(double V) { return double(float(V)); }

// A kind's number (the order of WorldRegion.h's CircuitWay and CircuitArea), from its word; -1 unknown.
int KindOf(const std::string& Word)
{
	static const char* Words[] = {"track", "pit", "kart", "dirt", "outline", "gravel", "sand", "grass", "tarmac", "grandstand"};
	for (int K = 0; K < 10; ++K)
		if (Word == Words[K]) return K;
	return -1;
}

std::string WordAt(const Json* List, const Json& Index)
{
	if (List == nullptr || !List->IsArray()) return std::string();
	const int I = int(Index.N);
	return I >= 0 && I < int(List->A.size()) ? List->A[size_t(I)].S : std::string();
}

std::vector<P2> StepsFrom(const Frame& F, const Json& Row, size_t First)
{
	std::vector<P2> Out;
	if (Row.A.size() < First + 2) return Out;
	long long Lat = static_cast<long long>(Row.A[First].N), Lon = static_cast<long long>(Row.A[First + 1].N);
	Out.push_back(F.FromGeo(double(Lat) * 1e-5, double(Lon) * 1e-5));
	for (size_t K = First + 2; K + 1 < Row.A.size(); K += 2)
	{
		Lat += static_cast<long long>(Row.A[K].N);
		Lon += static_cast<long long>(Row.A[K + 1].N);
		Out.push_back(F.FromGeo(double(Lat) * 1e-5, double(Lon) * 1e-5));
	}
	return Out;
}

std::string TextOf(const Json& V)
{
	if (V.IsString()) return V.S;
	if (V.IsNumber()) return std::to_string(static_cast<long long>(V.N));
	return std::string();
}

// The point of segment A-B nearest P: how far along it (0 to 1) and how far off.
double NearOnSegment(const P2& A, const P2& B, const P2& P, double& T)
{
	const double Dx = B.X - A.X, Dz = B.Z - A.Z, L2 = Dx * Dx + Dz * Dz;
	T = L2 > 1e-12 ? std::clamp(((P.X - A.X) * Dx + (P.Z - A.Z) * Dz) / L2, 0.0, 1.0) : 0.0;
	return std::hypot(A.X + Dx * T - P.X, A.Z + Dz * T - P.Z);
}

// A place to the half metre, as a key.
long long KeyOf(const P2& P) { return static_cast<long long>(std::llround(P.X * 2.0)) * 40000037LL + static_cast<long long>(std::llround(P.Z * 2.0)); }
} // namespace

void Region::AddCircuits(const Json& File)
{
	if (!File.IsObject() || int(File.Num("format")) != CircuitsFormat || CircuitFirst >= 0) return;
	const Frame F = FrameOf();
	const Json* Kinds = File.Get("kinds");
	const Json* NamesJ = File.Get("names");
	// (a way, area or circuit over two cells is in both files, the same: kept once, by its id)
	std::unordered_set<std::string> Have;
	for (const CircuitWay& W : CircuitWays) Have.insert("w" + W.Id);
	for (const CircuitArea& A : CircuitAreas) Have.insert("a" + A.Id);
	for (const CircuitInfo& C : Circuits) Have.insert("c" + C.Id);
	if (const Json* Rows = File.Get("circuits"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 6 || !Have.insert("c" + TextOf(Row.A[0])).second) continue;
			CircuitInfo C;
			C.Id = TextOf(Row.A[0]);
			C.Name = Row.A[1].S;
			C.Kind = Row.A[2].S;
			C.At = F.FromGeo(Row.A[3].N, Row.A[4].N);
			C.Length = Row.A[5].N;
			Circuits.push_back(std::move(C));
		}
	if (const Json* Rows = File.Get("ways"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 10 || !Have.insert("w" + TextOf(Row.A[0])).second) continue;
			CircuitWay W;
			W.Id = TextOf(Row.A[0]);
			W.Kind = KindOf(WordAt(Kinds, Row.A[1]));
			W.Width = Row.A[2].N * 0.1;
			W.Oneway = int(Row.A[3].N);
			W.Name = WordAt(NamesJ, Row.A[4]);
			W.Circuit = TextOf(Row.A[5]);
			W.Line = StepsFrom(F, Row, 6);
			if (W.Kind >= 0 && W.Kind <= 3 && W.Line.size() >= 2) CircuitWays.push_back(std::move(W));
		}
	if (const Json* Rows = File.Get("areas"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 10 || !Have.insert("a" + TextOf(Row.A[0])).second) continue;
			CircuitArea A;
			A.Id = TextOf(Row.A[0]);
			A.Kind = KindOf(WordAt(Kinds, Row.A[1]));
			A.Name = WordAt(NamesJ, Row.A[2]);
			A.Circuit = TextOf(Row.A[3]);
			A.Ring = StepsFrom(F, Row, 4);
			if (A.Kind >= 4 && A.Ring.size() >= 3) CircuitAreas.push_back(std::move(A));
		}
}

int Region::JoinCircuits()
{
	if (CircuitFirst >= 0 || CircuitWays.empty()) return 0;
	const auto ClassIx = [this](const char* Name)
	{
		const auto It = std::find(Classes.begin(), Classes.end(), std::string(Name));
		if (It != Classes.end()) return int(It - Classes.begin());
		Classes.push_back(Name);
		return int(Classes.size()) - 1;
	};
	const auto SurfaceIx = [this](const char* Name)
	{
		const auto It = std::find(Surfaces.begin(), Surfaces.end(), std::string(Name));
		if (It != Surfaces.end()) return int(It - Surfaces.begin());
		Surfaces.push_back(Name);
		return int(Surfaces.size()) - 1;
	};
	const auto NameIx = [this](const std::string& Name)
	{
		const auto It = std::find(Names.begin(), Names.end(), Name);
		if (It != Names.end()) return int(It - Names.begin());
		Names.push_back(Name);
		return int(Names.size()) - 1;
	};
	std::unordered_map<std::string, int> CircuitAt;
	for (size_t C = 0; C < Circuits.size(); ++C) CircuitAt[Circuits[C].Id] = int(C);

	// The ways that are roads: track, pit lane and kart track (loose ground is left as it lies).
	std::vector<int> Ways;
	for (size_t W = 0; W < CircuitWays.size(); ++W)
		if (CircuitWays[W].Kind <= 2) Ways.push_back(int(W));
	if (Ways.empty()) return 0;
	std::vector<std::vector<P2>> Lines;
	for (int W : Ways) Lines.push_back(CircuitWays[size_t(W)].Line);

	// Where the public roads reach them: (public node, way, segment, along it, the point).
	struct Link
	{
		int Node = -1;
		size_t Way = 0, Seg = 0;
		double T = 0.0;
		P2 At;
	};
	std::vector<Link> Links;
	constexpr double Bucket = 30.0, DeadEndReach = 25.0, JunctionReach = 4.0, PaddockReach = 600.0;
	std::unordered_map<long long, std::vector<std::pair<size_t, size_t>>> SegGrid;
	const auto Square = [](double X, double Z) { return static_cast<long long>(std::floor(X / Bucket)) * 4000037LL + static_cast<long long>(std::floor(Z / Bucket)); };
	for (size_t W = 0; W < Lines.size(); ++W)
		for (size_t S = 0; S + 1 < Lines[W].size(); ++S)
		{
			const P2 &A = Lines[W][S], &B = Lines[W][S + 1];
			const int Steps = std::max(1, int(std::ceil(A.DistanceTo(B) / Bucket)));
			long long Last = -1;
			for (int K = 0; K <= Steps; ++K)
			{
				const P2 P = A + (B - A) * (double(K) / Steps);
				for (int Dx = -1; Dx <= 1; ++Dx)
					for (int Dz = -1; Dz <= 1; ++Dz)
					{
						const long long Key = Square(P.X + Dx * Bucket, P.Z + Dz * Bucket);
						if (Key == Last && Dx == 0 && Dz == 0) continue;
						std::vector<std::pair<size_t, size_t>>& Row = SegGrid[Key];
						if (Row.empty() || Row.back() != std::make_pair(W, S)) Row.push_back({W, S});
					}
				Last = Square(P.X, P.Z);
			}
		}
	const size_t PublicNodes = Nodes.size();
	std::vector<char> Joined(Circuits.size() + 1, 0);
	const auto CircuitIx = [&](size_t W)
	{
		const auto It = CircuitAt.find(CircuitWays[size_t(Ways[W])].Circuit);
		return It == CircuitAt.end() ? Circuits.size() : size_t(It->second);
	};
	for (size_t N = 0; N < PublicNodes && N < Adjacent.size(); ++N)
	{
		const size_t Deg = Adjacent[N].size();
		if (Deg == 0) continue;
		const double Reach = Deg == 1 ? DeadEndReach : JunctionReach;
		const auto It = SegGrid.find(Square(Nodes[N].X, Nodes[N].Z));
		if (It == SegGrid.end()) continue;
		Link Best;
		double BestD = Reach;
		for (const std::pair<size_t, size_t>& Ws : It->second)
		{
			double T = 0.0;
			const double D = NearOnSegment(Lines[Ws.first][Ws.second], Lines[Ws.first][Ws.second + 1], Nodes[N], T);
			if (D < BestD)
			{
				BestD = D;
				Best.Node = int(N);
				Best.Way = Ws.first;
				Best.Seg = Ws.second;
				Best.T = T;
			}
		}
		if (Best.Node < 0) continue;
		const P2 &A = Lines[Best.Way][Best.Seg], &B = Lines[Best.Way][Best.Seg + 1];
		Best.At = A + (B - A) * Best.T;
		Links.push_back(Best);
		Joined[CircuitIx(Best.Way)] = 1;
	}
	// A circuit no road reaches: a paddock road from its nearest point to the nearest junction of a real
	// network (not a stray bit of road), within PaddockReach.
	for (size_t C = 0; C < Circuits.size(); ++C)
	{
		if (Joined[C]) continue;
		Link Best;
		double BestD = PaddockReach;
		for (size_t W = 0; W < Lines.size(); ++W)
		{
			if (CircuitIx(W) != C) continue;
			for (size_t S = 0; S < Lines[W].size(); S += 2)
			{
				const P2& P = Lines[W][S];
				// (the junctions in the region's own squares round it)
				const int Span = int(std::ceil(BestD / GridCell));
				const int Cx = int(std::floor(P.X / GridCell)), Cz = int(std::floor(P.Z / GridCell));
				for (int Gz = Cz - Span; Gz <= Cz + Span; ++Gz)
					for (int Gx = Cx - Span; Gx <= Cx + Span; ++Gx)
					{
						const auto Gt = Grid.find({Gx, Gz});
						if (Gt == Grid.end()) continue;
						for (int N : Gt->second)
						{
							if (size_t(N) >= PublicNodes || size_t(N) >= Adjacent.size() || Adjacent[size_t(N)].empty()) continue;
							if (size_t(Component[size_t(N)]) >= ComponentSize.size() || ComponentSize[size_t(Component[size_t(N)])] < 20) continue;
							const double D = Nodes[size_t(N)].DistanceTo(P);
							if (D >= BestD) continue;
							BestD = D;
							Best.Node = N;
							Best.Way = W;
							Best.Seg = std::min(S, Lines[W].size() - 2);
							Best.T = S + 1 < Lines[W].size() ? 0.0 : 1.0;
							Best.At = P;
						}
					}
			}
		}
		if (Best.Node >= 0) Links.push_back(Best);
	}

	// Each way with its links' points put in, then cut where a way ends on another, at a link, and a closed
	// loop in two (an edge's ends are two junctions).
	std::unordered_set<long long> Cuts;
	for (const std::vector<P2>& L : Lines)
	{
		Cuts.insert(KeyOf(L.front()));
		Cuts.insert(KeyOf(L.back()));
	}
	std::sort(Links.begin(), Links.end(), [](const Link& P, const Link& Q) { return P.Way != Q.Way ? P.Way < Q.Way : P.Seg != Q.Seg ? P.Seg > Q.Seg : P.T > Q.T; });
	for (Link& L : Links)
	{
		std::vector<P2>& Line = Lines[L.Way];
		// (onto a point of the line already there when within a metre of one)
		const P2 &A = Line[L.Seg], &B = Line[L.Seg + 1];
		if (A.DistanceTo(L.At) < 1.0) L.At = A;
		else if (B.DistanceTo(L.At) < 1.0) L.At = B;
		else Line.insert(Line.begin() + std::ptrdiff_t(L.Seg + 1), L.At);
		Cuts.insert(KeyOf(L.At));
	}
	struct Piece
	{
		size_t Way = 0;
		std::vector<P2> Line;
	};
	std::vector<Piece> Pieces;
	for (size_t W = 0; W < Lines.size(); ++W)
	{
		Piece Cur;
		Cur.Way = W;
		for (size_t K = 0; K < Lines[W].size(); ++K)
		{
			const P2& P = Lines[W][K];
			if (!Cur.Line.empty() && Cur.Line.back().DistanceTo(P) < 0.05) continue;
			Cur.Line.push_back(P);
			if (K > 0 && K + 1 < Lines[W].size() && Cuts.count(KeyOf(P)) != 0 && Cur.Line.size() >= 2)
			{
				Pieces.push_back(Cur);
				Cur.Line.assign(1, P);
			}
		}
		if (Cur.Line.size() >= 2) Pieces.push_back(Cur);
	}
	for (size_t I = 0; I < Pieces.size(); ++I)
	{
		Piece& Pc = Pieces[I];
		if (KeyOf(Pc.Line.front()) != KeyOf(Pc.Line.back())) continue;
		if (Pc.Line.size() < 4) { Pc.Line.clear(); continue; }
		const size_t Mid = Pc.Line.size() / 2;
		Piece Second;
		Second.Way = Pc.Way;
		Second.Line.assign(Pc.Line.begin() + std::ptrdiff_t(Mid), Pc.Line.end());
		Pc.Line.resize(Mid + 1);
		Pieces.push_back(Second);
	}

	// The junctions (one a half-metre place) and the roads.
	std::unordered_map<long long, int> NodeAt;
	const auto NodeOf = [&](const P2& P)
	{
		const long long K = KeyOf(P);
		const auto It = NodeAt.find(K);
		if (It != NodeAt.end()) return It->second;
		Nodes.push_back(P2(F32c(P.X), F32c(P.Z)));
		NodeAt[K] = int(Nodes.size()) - 1;
		return int(Nodes.size()) - 1;
	};
	const int Race = ClassIx("raceway"), Pit = ClassIx("pit_lane"), Paddock = ClassIx("service"), Tarmac = SurfaceIx("tarmac");
	CircuitFirst = int(Edges.size());
	CircuitOf.clear();
	int Added = 0;
	std::unordered_map<size_t, int> PieceNo;
	for (const Piece& Pc : Pieces)
	{
		if (Pc.Line.size() < 2) continue;
		const CircuitWay& W = CircuitWays[size_t(Ways[Pc.Way])];
		const size_t C = CircuitIx(Pc.Way);
		RoadEdge R;
		R.A = NodeOf(Pc.Line.front());
		R.B = NodeOf(Pc.Line.back());
		if (R.A == R.B) continue;
		R.Class = W.Kind == 1 ? Pit : Race;
		R.Surface = Tarmac;
		R.Name = NameIx(!W.Name.empty() ? W.Name : C < Circuits.size() ? Circuits[C].Name : std::string());
		// (both ways for the sat nav and the riders: the paint is a one-way road's, by its RoadExtra)
		R.Oneway = 0;
		R.Gid = "circuit:" + W.Id + ":" + std::to_string(PieceNo[Pc.Way]++);
		R.Line = Pc.Line;
		R.Line.front() = Nodes[size_t(R.A)];
		R.Line.back() = Nodes[size_t(R.B)];
		RoadExtra X;
		X.Width = W.Width > 0.0 ? W.Width : W.Kind == 1 ? PitWidth : W.Kind == 2 ? KartWidth : TrackWidth;
		X.Flags = RoadExtra::Oneway;
		Extras[R.Gid] = X;
		Edges.push_back(std::move(R));
		CircuitOf.push_back(C < Circuits.size() ? int(C) : int(Circuits.size()));
		++Added;
	}
	// (a way of a circuit the file didn't list: one of its own, named after the way)
	for (int& C : CircuitOf)
		if (C == int(Circuits.size()))
		{
			CircuitInfo Info;
			Info.Id = "unlisted";
			Info.Name = "Circuit";
			Info.Kind = "track";
			Circuits.push_back(Info);
			for (int& D : CircuitOf)
				if (D == C) D = int(Circuits.size()) - 1;
			break;
		}
	for (const Link& L : Links)
	{
		RoadEdge R;
		R.A = L.Node;
		R.B = NodeOf(L.At);
		if (R.A == R.B || Nodes[size_t(R.A)].DistanceTo(Nodes[size_t(R.B)]) < 0.5) continue;
		R.Class = Paddock;
		R.Surface = Tarmac;
		R.Name = 0;
		R.Line = {Nodes[size_t(R.A)], Nodes[size_t(R.B)]};
		Edges.push_back(std::move(R));
		CircuitOf.push_back(-1);
		++Added;
	}
	Finish();
	return Added;
}

int Region::CircuitAt(double X, double Z, double Within) const
{
	int Best = -1;
	double BestD = Within;
	for (size_t E = size_t(std::max(CircuitFirst, 0)); CircuitFirst >= 0 && E < Edges.size(); ++E)
	{
		if (!CircuitEdge(int(E))) continue;
		const std::vector<P2>& L = Edges[E].Line;
		for (size_t S = 0; S + 1 < L.size(); ++S)
		{
			double T = 0.0;
			const double D = NearOnSegment(L[S], L[S + 1], P2(X, Z), T);
			if (D < BestD)
			{
				BestD = D;
				Best = CircuitOf[E - size_t(CircuitFirst)];
			}
		}
	}
	return Best;
}
} // namespace TorqueWorld
