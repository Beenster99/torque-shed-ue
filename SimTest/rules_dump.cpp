// [road rulebook stage 2] What the road rules checker (Tools/road_rules_check.py) reads: the roads round every
// junction near the ride as the game draws them, and stretches of the ride's road between junctions.
//
// WORLD_RIDE_RULES=<file> (world_ride): one JSON object a line.
//   {"t":"jn", ...}       a junction of JunctionPlaces within 60 m of the ride's road: its kind and arms, each laid
//                         road's points within Reach of it, the signs within Reach, and a picture from above of
//                         what is drawn there (Cell m squares, 2 Reach across: 0 nothing, 1 road surface, 2 white
//                         paint, 3 yellow paint, 4 anything else drawn with the road: verge, kerb, bank), as rows of runs.
//   {"t":"stretch", ...}  the same picture of the ride's road every StretchEvery m, where no junction is within 50 m.
//   {"t":"ring", ...}     a roundabout the ride's road goes round: its middle and the ride's points on it.
//   {"t":"route", ...}    [road rulebook stage 5] a way the planner found over the area's roads (the ride's own, run-out
//                         and all, and RoutesEach more between random junctions for each style): each step that goes
//                         against a one-way road (G2, roundabouts too: G3), takes a banned turn or a private road
//                         between public ones (G4), and how many service roads it uses between public ones.
//   {"t":"turns", ...}    [road rulebook G7] the sat nav's turns along the ride's road (along, which way, roundabout exit),
//                         the ride's road as laid every 5 m (along, x, z), and its junctions (along, x, z, roads there).
// Coordinates are metres east (x) and south (z) of the junction's or stretch's middle. The checker judges; this
// only measures, so the rules can change without building again.

#include "WorldRide.h"
#include "WorldRideSigns.h"
#include "WorldFiles.h"
#include "WorldRoute.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace TorqueWorld;

namespace RulesDumpImpl
{
constexpr double Reach = 30.0, Cell = 0.1, StretchEvery = 400.0;
constexpr int Cells = int(2.0 * Reach / Cell);

static bool Near(const float* C, float R, float G, float B) { return std::fabs(C[0] - R) < 0.004f && std::fabs(C[1] - G) < 0.004f && std::fabs(C[2] - B) < 0.004f; }

// What is drawn from above round C: the topmost surface in each cell.
static void Picture(const RidePlan& Plan, const P2& C, std::vector<unsigned char>& Kind)
{
	Kind.assign(size_t(Cells) * Cells, 0);
	std::vector<float> Top(size_t(Cells) * Cells, -1e9f);
	const Rect Round{C.X - Reach - 30.0, C.Z - Reach - 30.0, C.X + Reach + 30.0, C.Z + Reach + 30.0};
	Plan.LayBlocks(Round);
	std::vector<int> Roads = Plan.RoadsNear(Round, false);
	if (std::find(Roads.begin(), Roads.end(), 0) == Roads.end()) Roads.push_back(0);
	// (a road inside a junction built whole is drawn with its owner's piece, which may be farther off)
	std::vector<int> Owners;
	for (int W : Roads)
	{
		const auto It = Plan.WholeInside.find(W);
		if (It == Plan.WholeInside.end() || size_t(It->second) >= Plan.WholeOf.size() || !Plan.WholeOf[size_t(It->second)].Ok) continue;
		const int Ow = Plan.WholeOf[size_t(It->second)].Owner;
		if (std::find(Roads.begin(), Roads.end(), Ow) == Roads.end() && std::find(Owners.begin(), Owners.end(), Ow) == Owners.end()) Owners.push_back(Ow);
	}
	// (and the owner of every junction built whole in the picture: its piece is drawn with the owner's end, which a
	// neighbouring junction's picture would otherwise leave out, fens jn 6774 beside 5670)
	for (size_t J = 0; J < Plan.WholeOf.size() && J < Plan.JunctionPlaces.size(); ++J)
	{
		if (!Plan.WholeOf[J].Ok || Plan.WholeOf[J].Owner <= 0 || Plan.JunctionPlaces[J].At.DistanceTo(C) > Reach + 30.0) continue;
		const int Ow = Plan.WholeOf[J].Owner;
		if (std::find(Owners.begin(), Owners.end(), Ow) == Owners.end()) Owners.push_back(Ow);
	}
	for (int Ow : Owners)
		if (std::find(Roads.begin(), Roads.end(), Ow) == Roads.end()) Roads.push_back(Ow);
	for (int W : Roads)
	{
		const std::vector<RoadPoint>& Pts = Plan.RoadOf(W);
		const bool Owner = std::find(Owners.begin(), Owners.end(), W) != Owners.end();
		for (int First = 0; First < int(Pts.size()) - 1; First += 64)
		{
			const int Last = std::min(First + 64, int(Pts.size()) - 1);
			bool Close = Owner;
			// (the ride's chunk where a whole junction's hidden stretch begins draws its piece: laid whenever that
			// junction is in the picture, or a neighbour chunk hides the ride's road there with nothing in its place)
			if (W == 0 && !Close)
				for (const std::pair<double, int>& Hd : Plan.RideHides)
					if (size_t(Hd.second) < Plan.JunctionPlaces.size() && Plan.JunctionPlaces[size_t(Hd.second)].At.DistanceTo(C) < Reach + 30.0 &&
						Hd.first >= Pts[size_t(First)].Along && Hd.first < Pts[size_t(Last)].Along)
						Close = true;
			for (int I = First; I <= Last && !Close; ++I) Close = Pts[size_t(I)].At.DistanceTo(C) < Reach + 25.0;
			if (!Close) continue;
			RideMesh M;
			Plan.RoadPiece(W, First, Last, M);
			if (M.Col.empty()) continue;
			for (size_t T = 0; T + 2 < M.Idx.size(); T += 3)
			{
				const float* Cl = &M.Col[size_t(M.Idx[T]) * 3];
				// (the road's own surfaces, as the junction check reads them: tarmac, its patches, gravel and setts; else
				// verge, kerb or bank, which are not road)
				const bool Paint = Near(Cl, 0.83f, 0.83f, 0.75f), Yellow = Near(Cl, 0.78f, 0.52f, 0.03f);
				const float Fade = (Cl[0] - 0.022f) / (0.095f - 0.022f);
				const bool Faded = Fade > 0.0f && Fade < 1.0f && std::fabs(Cl[1] - (0.022f + Fade * 0.066f)) < 0.004f && std::fabs(Cl[2] - (0.025f + Fade * 0.045f)) < 0.004f;
				const bool Tar = Faded || Near(Cl, 0.022f, 0.022f, 0.025f) || Near(Cl, 0.010f, 0.010f, 0.011f) || Near(Cl, 0.06f, 0.062f, 0.085f) || Near(Cl, 0.038f, 0.037f, 0.036f)
					|| Near(Cl, 0.095f, 0.088f, 0.07f) || Near(Cl, 0.09f, 0.095f, 0.11f);
				const unsigned char K = Paint ? 2 : Yellow ? 3 : Tar ? 1 : 4;
				double X[3], Y[3], Z[3];
				for (int Q = 0; Q < 3; ++Q)
				{
					const uint32_t V = M.Idx[T + size_t(Q)];
					X[Q] = M.OriginX + double(M.Pos[V * 3]) - C.X + Reach;
					Y[Q] = double(M.Pos[V * 3 + 1]);
					Z[Q] = M.OriginZ + double(M.Pos[V * 3 + 2]) - C.Z + Reach;
				}
				const double D = (X[1] - X[0]) * (Z[2] - Z[0]) - (X[2] - X[0]) * (Z[1] - Z[0]);
				if (std::fabs(D) < 1e-9) continue;
				const int Cx0 = std::max(0, int(std::min({X[0], X[1], X[2]}) / Cell)), Cx1 = std::min(Cells - 1, int(std::max({X[0], X[1], X[2]}) / Cell));
				const int Cz0 = std::max(0, int(std::min({Z[0], Z[1], Z[2]}) / Cell)), Cz1 = std::min(Cells - 1, int(std::max({Z[0], Z[1], Z[2]}) / Cell));
				for (int Cz = Cz0; Cz <= Cz1; ++Cz)
					for (int Cx = Cx0; Cx <= Cx1; ++Cx)
					{
						const double Px = (Cx + 0.5) * Cell, Pz = (Cz + 0.5) * Cell;
						const double W0 = ((X[1] - Px) * (Z[2] - Pz) - (X[2] - Px) * (Z[1] - Pz)) / D;
						const double W1 = ((X[2] - Px) * (Z[0] - Pz) - (X[0] - Px) * (Z[2] - Pz)) / D;
						const double W2 = 1.0 - W0 - W1;
						if (W0 < -1e-9 || W1 < -1e-9 || W2 < -1e-9) continue;
						const float H = float(W0 * Y[0] + W1 * Y[1] + W2 * Y[2]);
						const size_t Ki = size_t(Cz) * Cells + size_t(Cx);
						// (paint lies a few mm on its road: it wins a tie with the road under it)
						if (H > Top[Ki] + 0.002f || (K >= 2 && H > Top[Ki] - 0.002f))
						{
							Top[Ki] = std::max(Top[Ki], H);
							Kind[Ki] = K;
						}
					}
			}
		}
	}
}

static void Runs(const std::vector<unsigned char>& Kind, std::string& Out)
{
	Out += "\"cell\":" + std::to_string(Cell) + ",\"n\":" + std::to_string(Cells) + ",\"runs\":[";
	char Buf[64];
	bool Any = false;
	for (int Z = 0; Z < Cells; ++Z)
	{
		int X = 0;
		while (X < Cells)
		{
			const unsigned char K = Kind[size_t(Z) * Cells + size_t(X)];
			int E = X + 1;
			while (E < Cells && Kind[size_t(Z) * Cells + size_t(E)] == K) ++E;
			if (K != 0)
			{
				std::snprintf(Buf, sizeof(Buf), "%s[%d,%d,%d,%d]", Any ? "," : "", Z, X, E - X, int(K));
				Out += Buf;
				Any = true;
			}
			X = E;
		}
	}
	Out += "]";
}

static std::string Esc(const std::string& S)
{
	std::string O;
	for (char Ch : S) if (Ch != '"' && Ch != '\\' && Ch >= 32) O += Ch;
	return O;
}

static int MphOf(const RidePlan& Plan, int W, double Along, const P2& At)
{
	if (W == 0)
	{
		const RidePlan::LimitStretch* L = Plan.LimitAt(std::clamp(Along, 0.0, Plan.Road.empty() ? 0.0 : Plan.Road.back().Along));
		return L != nullptr ? Plan.ShownLimit(L->Kmh) : 0;
	}
	const RidePlan::SideRoad* Sd = Plan.SideOf(W);
	if (Sd == nullptr || Sd->Edge < 0) return 0;
	// (a side road's limit as the rule assumes it: built up where the ride's road nearest is)
	bool Built = false;
	double Best = 1e18;
	for (size_t I = 0; I < Plan.Road.size() && I < Plan.BuiltUp.size(); I += 2)
	{
		const double D = Plan.Road[I].At.DistanceTo(At);
		if (D < Best) { Best = D; Built = Plan.BuiltUp[I] != 0; }
	}
	return Plan.ShownLimit(RidePlan::AssumedKmh(Plan.Area.Roads.ClassOf(Sd->Edge), Built, Plan.Britain()));
}

static std::string ClassOf(const RidePlan& Plan, int W, double Along)
{
	if (W == 0)
	{
		const RidePlan::RoadFact* F = Plan.FactAt(Along);
		return F != nullptr ? F->Class : "";
	}
	const RidePlan::SideRoad* Sd = Plan.SideOf(W);
	return Sd != nullptr && Sd->Edge >= 0 ? Plan.Area.Roads.ClassOf(Sd->Edge) : "";
}

// Each laid road's points within Reach + 10 of C, and its class and limit there.
static void RoadsRound(const RidePlan& Plan, const P2& C, std::string& Out)
{
	const Rect Round{C.X - Reach - 10.0, C.Z - Reach - 10.0, C.X + Reach + 10.0, C.Z + Reach + 10.0};
	std::vector<int> Roads = Plan.RoadsNear(Round, false);
	if (std::find(Roads.begin(), Roads.end(), 0) == Roads.end()) Roads.push_back(0);
	std::sort(Roads.begin(), Roads.end());
	Out += "\"roads\":[";
	bool AnyRoad = false;
	char Buf[160];
	for (int W : Roads)
	{
		const std::vector<RoadPoint>& Pts = Plan.RoadOf(W);
		const RidePlan::SideRoad* Sd = W > 0 ? Plan.SideOf(W) : nullptr;
		if (W > 0 && Sd == nullptr) continue;
		// (in runs of points within reach: the ride's road may pass more than once)
		size_t I = 0;
		while (I < Pts.size())
		{
			while (I < Pts.size() && Pts[I].At.DistanceTo(C) > Reach + 10.0) ++I;
			if (I >= Pts.size()) break;
			size_t J = I;
			while (J + 1 < Pts.size() && Pts[J + 1].At.DistanceTo(C) <= Reach + 10.0) ++J;
			const size_t Mid = (I + J) / 2;
			const bool Track = Sd != nullptr && (Sd->Track || Sd->Gravel || Sd->Setts);
			std::snprintf(Buf, sizeof(Buf), "%s{\"w\":%d,\"edge\":%d,\"cls\":\"%s\",\"mph\":%d,\"unpaved\":%d,\"pts\":[", AnyRoad ? "," : "", W, Sd ? Sd->Edge : -1,
				Esc(ClassOf(Plan, W, Pts[Mid].Along)).c_str(), MphOf(Plan, W, Pts[Mid].Along, Pts[Mid].At), Track ? 1 : 0);
			Out += Buf;
			AnyRoad = true;
			for (size_t K = I; K <= J; ++K)
			{
				const RoadPoint& P = Pts[K];
				std::snprintf(Buf, sizeof(Buf), "%s[%.2f,%.2f,%.2f,%.1f,%d,%d,%d,%d,%d]", K > I ? "," : "", P.At.X - C.X, P.At.Z - C.Z, P.Half, P.Along, P.Lanes, P.OneWay ? 1 : 0,
					P.Bridge || P.Tunnel ? 1 : 0, W == 0 && K < Plan.BuiltUp.size() ? int(Plan.BuiltUp[K]) : -1, int(P.Near));
				Out += Buf;
			}
			Out += "]}";
			I = J + 1;
		}
	}
	Out += "]";
}

static void SignsRound(const RidePlan& Plan, const P2& C, std::string& Out)
{
	Out += "\"signs\":[";
	bool Any = false;
	char Buf[200];
	for (const RidePlan::Furnish& F : Plan.Furniture)
	{
		if (F.At.DistanceTo(C) > Reach) continue;
		if (F.Kind != RidePlan::WarnSign && F.Kind != RidePlan::Limit30 && F.Kind != RidePlan::LimitNational && F.Kind != RidePlan::NameSign) continue;
		const char* What = F.Kind == RidePlan::Limit30 ? "limit30" : F.Kind == RidePlan::LimitNational ? "national" : F.Kind == RidePlan::NameSign ? "name"
			: int(F.Size) == int(SignPlate::SymGiveWay) ? "give_way" : int(F.Size) == int(SignPlate::SymStop) ? "stop" : "warning";
		std::snprintf(Buf, sizeof(Buf), "%s{\"kind\":\"%s\",\"sym\":%d,\"x\":%.2f,\"z\":%.2f,\"fx\":%.3f,\"fz\":%.3f,\"big\":%.2f}", Any ? "," : "", What, int(F.Size), F.At.X - C.X, F.At.Z - C.Z, F.Face.X, F.Face.Z, F.Big);
		Out += Buf;
		Any = true;
	}
	Out += "]";
}

static void Where(const RidePlan& Plan, const P2& C, std::string& Out)
{
	const Geo G = Plan.Area.Roads.FrameOf().ToGeo(C.X, C.Z);
	char Buf[96];
	std::snprintf(Buf, sizeof(Buf), "\"lat\":%.6f,\"lon\":%.6f,\"x\":%.1f,\"z\":%.1f", G.Lat, G.Lon, C.X, C.Z);
	Out += Buf;
}

// [road rulebook stage 5] One way over the area's roads, judged step by step.
static void RouteRec(const Region& G, const std::vector<RidePlan::RideStep>& Steps, const char* Mode, int No, std::string& Out, const std::vector<double>* Costs = nullptr)
{
	char Buf[512];
	if (Steps.empty()) return;
	const auto Private = [&G](int E) { const RoadExtra* X = G.ExtraOf(E); return X != nullptr && X->Has(RoadExtra::Private); };
	const auto Service = [&G](int E) { return G.ClassOf(E) == "service"; };
	// (a private or service road between public ones: not where the way starts or ends on such roads)
	const auto Between = [&Steps](size_t K, const auto& Is)
	{
		bool Before = false, After = false;
		for (size_t J = 0; J < K; ++J) Before = Before || !Is(Steps[J].Edge);
		for (size_t J = K + 1; J < Steps.size(); ++J) After = After || !Is(Steps[J].Edge);
		return Before && After;
	};
	// (a private road where there is no other way, the planner made to refuse them all: not a breach, the map's roads)
	bool Only = false;
	for (size_t K = 0; K < Steps.size() && Costs != nullptr; ++K)
		if (Private(Steps[K].Edge) && Between(K, Private))
		{
			std::vector<double> Refuse = *Costs;
			for (size_t E = 0; E < Refuse.size(); ++E) if (Private(int(E))) Refuse[E] = 1e7;
			for (int E : G.CheapestPath(Steps.front().From, Steps.back().To, &Refuse)) Only = Only || Private(E);
			break;
		}
	double Km = 0.0;
	int Services = 0, Rings = 0;
	std::string Bad;
	const auto Breach = [&](const char* Rule, const char* Say, int Node)
	{
		const P2 At = G.Nodes[size_t(Node)];
		const Geo Gx = G.FrameOf().ToGeo(At.X, At.Z);
		std::snprintf(Buf, sizeof(Buf), "%s{\"rule\":\"%s\",\"say\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"node\":%d}", Bad.empty() ? "" : ",", Rule, Say, Gx.Lat, Gx.Lon, Node);
		Bad += Buf;
	};
	for (size_t K = 0; K < Steps.size(); ++K)
	{
		const RidePlan::RideStep& S = Steps[K];
		Km += G.Edges[size_t(S.Edge)].Length / 1000.0;
		const RoadExtra* X = G.ExtraOf(S.Edge);
		const bool Ring = X != nullptr && X->Has(RoadExtra::Roundabout);
		Rings += Ring ? 1 : 0;
		if (!G.Passable(S.Edge, S.From)) Breach(Ring ? "G3" : "G2", Ring ? "round a roundabout the wrong way" : "against a one-way road", S.From);
		if (K > 0 && G.TurnBanned(Steps[K - 1].Edge, S.From, S.Edge)) Breach("G4", "a banned turn", S.From);
		if (K > 0 && Steps[K - 1].Edge == S.Edge && G.Adjacent[size_t(S.From)].size() > 1) Breach("G4", "a U-turn in the road", S.From);
		if (Private(S.Edge) && Between(K, Private)) Breach(Only ? "G4only" : "G4", Only ? "through a private road, the only way" : "through a private road", S.From);
		if (Service(S.Edge) && Between(K, Service)) ++Services;
	}
	std::snprintf(Buf, sizeof(Buf), "{\"t\":\"route\",\"mode\":\"%s\",\"no\":%d,\"steps\":%zu,\"km\":%.2f,\"rings\":%d,\"services\":%d,\"bad\":[", Mode, No, Steps.size(), Km, Rings, Services);
	Out += Buf;
	Out += Bad + "]}\n";
}

static void Routes(const RidePlan& Plan, std::string& Out, int& Count)
{
	const Region& G = Plan.Area.Roads;
	const std::vector<double> MainCosts = Route::EdgeCosts(G, "main");
	RouteRec(G, Plan.MadeSteps, "ride", 0, Out, &MainCosts);
	++Count;
	const char* Each = std::getenv("WORLD_RIDE_RULES_ROUTES");
	const int RoutesEach = Each != nullptr ? std::atoi(Each) : 60;
	if (RoutesEach <= 0 || G.Nodes.empty() || G.Component.size() != G.Nodes.size()) return;
	// (between junctions of the area's biggest piece of road, 0.5 to 6 km apart, the same each run)
	int Big = 0;
	for (size_t C = 0; C < G.ComponentSize.size(); ++C) if (G.ComponentSize[C] > G.ComponentSize[size_t(Big)]) Big = int(C);
	std::vector<int> Ends;
	for (size_t N = 0; N < G.Nodes.size(); ++N)
		if (G.Component[N] == Big && G.Adjacent[N].size() >= 3) Ends.push_back(int(N));
	if (Ends.size() < 2) return;
	unsigned long long Seed = 88172645463325252ULL;
	const auto Next = [&Seed]() { Seed ^= Seed << 13; Seed ^= Seed >> 7; Seed ^= Seed << 17; return Seed; };
	for (const char* Mode : {"main", "scenic", "curvy", "tracks"})
	{
		const std::vector<double> Costs = Route::EdgeCosts(G, Mode);
		for (int R = 0, Tries = 0; R < RoutesEach && Tries < RoutesEach * 20; ++Tries)
		{
			const int A = Ends[size_t(Next() % Ends.size())], B = Ends[size_t(Next() % Ends.size())];
			const double D = G.Nodes[size_t(A)].DistanceTo(G.Nodes[size_t(B)]);
			if (D < 500.0 || D > 6000.0) continue;
			const std::vector<int> Path = G.CheapestPath(A, B, &Costs);
			if (Path.empty()) continue;
			std::vector<RidePlan::RideStep> Steps;
			int Node = A;
			for (int E : Path) { const int To = G.OtherEnd(E, Node); Steps.push_back({Node, E, To}); Node = To; }
			RouteRec(G, Steps, Mode, ++R, Out, &Costs);
			++Count;
		}
	}
}

// [road rulebook G7] What the sat nav says against the road as laid: its turns, the road, the junctions.
static void TurnsRec(const RidePlan& Plan, std::string& Out)
{
	char Buf[256];
	const Region& G = Plan.Area.Roads;
	Out += "{\"t\":\"turns\",\"turns\":[";
	for (size_t K = 0; K < Plan.Turns.size(); ++K)
	{
		std::snprintf(Buf, sizeof(Buf), "%s[%.1f,%d,%d]", K ? "," : "", Plan.Turns[K].Along, Plan.Turns[K].Turn, Plan.Turns[K].Exit);
		Out += Buf;
	}
	Out += "],\"road\":[";
	double Last = -1e9;
	bool Any = false;
	for (const RoadPoint& P : Plan.Road)
	{
		if (P.Along - Last < 5.0) continue;
		Last = P.Along;
		std::snprintf(Buf, sizeof(Buf), "%s[%.1f,%.2f,%.2f]", Any ? "," : "", P.Along, P.At.X, P.At.Z);
		Out += Buf;
		Any = true;
	}
	Out += "],\"nodes\":[";
	Any = false;
	for (const RidePlan::PathNode& N : Plan.PathNodes)
	{
		if (N.Node < 0 || size_t(N.Node) >= G.Nodes.size()) continue;
		const RoadExtra* X = N.Next >= 0 ? G.ExtraOf(N.Next) : nullptr;
		std::snprintf(Buf, sizeof(Buf), "%s[%.1f,%.2f,%.2f,%zu,%d]", Any ? "," : "", N.Along, G.Nodes[size_t(N.Node)].X, G.Nodes[size_t(N.Node)].Z, G.Adjacent[size_t(N.Node)].size(),
			X != nullptr && X->Has(RoadExtra::Roundabout) ? 1 : 0);
		Out += Buf;
		Any = true;
	}
	Out += "]}\n";
}
} // namespace RulesDumpImpl

void RulesDump(const RidePlan& Plan, const std::string& Path)
{
	using namespace RulesDumpImpl;
	const Region& G = Plan.Area.Roads;
	std::set<int> OnRide;
	for (const auto& St : Plan.MadeSteps) OnRide.insert(St.Edge);
	const auto NearRide = [&](const P2& P, double Within)
	{
		RidePlan::NearOpts O;
		O.MainOnly = true;
		RidePlan::NearHit H;
		return Plan.NearestOf(P.X, P.Z, O, H) && H.Dist < Within;
	};
	std::string All;
	char Buf[256];
	std::snprintf(Buf, sizeof(Buf), "{\"t\":\"meta\",\"whole\":%d,\"priority\":%d,\"length\":%.0f,\"uk\":%d}\n", RidePlan::WholeJunctions ? 1 : 0, RidePlan::NoJunctionPriority ? 0 : 1,
		Plan.Road.empty() ? 0.0 : Plan.Road.back().Along, Plan.Britain() ? 1 : 0);
	All += Buf;
	// (the ride's two ends: a ride begins and finishes differently (its run-out past the last pin), so the checker's A3
	// leaves the junctions by either end out)
	if (!Plan.Road.empty())
	{
		std::string E = "{\"t\":\"ends\",\"a\":{";
		Where(Plan, Plan.Road.front().At, E);
		E += "},\"b\":{";
		Where(Plan, Plan.Road.back().At, E);
		E += "}}\n";
		All += E;
	}
	int Junctions = 0, Stretches = 0, Rings = 0;
	std::vector<unsigned char> Kind;
	std::vector<P2> JunctionsAt;
	for (size_t J = 0; J < Plan.JunctionPlaces.size(); ++J)
	{
		const RidePlan::JunctionPlace& Jp = Plan.JunctionPlaces[J];
		if (Jp.Kind == RidePlan::EJunction::None || Jp.Kind == RidePlan::EJunction::Bend) continue;
		if (!NearRide(Jp.At, 60.0)) continue;
		JunctionsAt.push_back(Jp.At);
		int Laid = 0;
		for (const RidePlan::JunctionArm& A : Jp.Arms)
			if (OnRide.count(A.Edge) != 0 || Plan.SideOf(A.Edge + 1) != nullptr) ++Laid;
		if (Laid < 2) continue;
		std::string L = "{\"t\":\"jn\",\"id\":" + std::to_string(J) + ",\"node\":" + std::to_string(Jp.Nodes.empty() ? -1 : Jp.Nodes.front()) + ",";
		Where(Plan, Jp.At, L);
		std::snprintf(Buf, sizeof(Buf), ",\"kind\":\"%s\",\"ring\":%d,\"lights\":%d,\"dual\":%d,\"unpaved\":%d,\"span\":%.1f,\"whole\":%d,\"arms\":[", RidePlan::JunctionKindName(Jp.Kind), Jp.Ring,
			Jp.Lights ? 1 : 0, Jp.Dual ? 1 : 0, Jp.Unpaved ? 1 : 0, Jp.Span, J < Plan.WholeOf.size() && Plan.WholeOf[J].Ok ? 1 : 0);
		L += Buf;
		for (size_t K = 0; K < Jp.Arms.size(); ++K)
		{
			const RidePlan::JunctionArm& A = Jp.Arms[K];
			const int W = OnRide.count(A.Edge) != 0 ? 0 : Plan.SideOf(A.Edge + 1) != nullptr ? A.Edge + 1 : -1;
			const P2 From = G.Nodes[size_t(A.Node)];
			std::snprintf(Buf, sizeof(Buf), "%s{\"edge\":%d,\"w\":%d,\"cls\":\"%s\",\"ax\":%.3f,\"az\":%.3f,\"fx\":%.1f,\"fz\":%.1f,\"through\":%d,\"in\":%d,\"out\":%d,\"pair\":%d,\"unpaved\":%d}", K > 0 ? "," : "",
				A.Edge, W, Esc(G.ClassOf(A.Edge)).c_str(), A.Away.X, A.Away.Z, From.X - Jp.At.X, From.Z - Jp.At.Z, A.Through ? 1 : 0, A.In ? 1 : 0, A.Out ? 1 : 0, A.Pair, A.Unpaved ? 1 : 0);
			L += Buf;
		}
		L += "],";
		// (the map's own marks there: give-way and stop points, lights, crossings)
		L += "\"mapped\":[";
		bool AnyMark = false;
		for (const MappedPoint& Mp : G.RoadPoints)
		{
			if (Mp.At.DistanceTo(Jp.At) > Reach) continue;
			std::snprintf(Buf, sizeof(Buf), "%s{\"kind\":\"%s\",\"x\":%.1f,\"z\":%.1f,\"edge\":%d}", AnyMark ? "," : "", Esc(Mp.Kind).c_str(), Mp.At.X - Jp.At.X, Mp.At.Z - Jp.At.Z, Mp.Edge);
			L += Buf;
			AnyMark = true;
		}
		L += "],";
		RoadsRound(Plan, Jp.At, L);
		L += ",";
		SignsRound(Plan, Jp.At, L);
		L += ",";
		Picture(Plan, Jp.At, Kind);
		Runs(Kind, L);
		L += "}\n";
		All += L;
		++Junctions;
	}
	// (the ride's road between junctions)
	for (double Along = 100.0; !Plan.Road.empty() && Along < Plan.Road.back().Along - 100.0; Along += StretchEvery)
	{
		size_t I = 0;
		while (I + 1 < Plan.Road.size() && Plan.Road[I + 1].Along < Along) ++I;
		const RoadPoint& P = Plan.Road[I];
		if (P.Bridge || P.Tunnel) continue;
		bool Clear = true;
		for (const P2& At : JunctionsAt) Clear = Clear && At.DistanceTo(P.At) > 50.0;
		for (const RidePlan::RoadRing& Rg : Plan.RoadRings) Clear = Clear && Rg.Mid.DistanceTo(P.At) > 80.0;
		if (!Clear) continue;
		std::snprintf(Buf, sizeof(Buf), "{\"t\":\"stretch\",\"along\":%.0f,", P.Along);
		std::string L = Buf;
		Where(Plan, P.At, L);
		L += ",";
		RoadsRound(Plan, P.At, L);
		L += ",";
		SignsRound(Plan, P.At, L);
		L += ",";
		Picture(Plan, P.At, Kind);
		Runs(Kind, L);
		L += "}\n";
		All += L;
		++Stretches;
	}
	// (the roundabouts the ride's road goes round, and which way)
	for (size_t R = 0; R < Plan.RoadRings.size(); ++R)
	{
		const RidePlan::RoadRing& Rg = Plan.RoadRings[R];
		const double Rr = std::max(Rg.Radius, 4.0) + Rg.Half + 2.0;
		std::string Pts;
		int On = 0;
		for (size_t I = 0; I < Plan.Road.size(); ++I)
		{
			if (Plan.Road[I].At.DistanceTo(Rg.Mid) > Rr) continue;
			std::snprintf(Buf, sizeof(Buf), "%s[%.2f,%.2f,%.1f]", On > 0 ? "," : "", Plan.Road[I].At.X - Rg.Mid.X, Plan.Road[I].At.Z - Rg.Mid.Z, Plan.Road[I].Along);
			Pts += Buf;
			++On;
		}
		if (On < 3) continue;
		std::snprintf(Buf, sizeof(Buf), "{\"t\":\"ring\",\"id\":%zu,\"radius\":%.1f,\"mini\":%d,\"mapped_clockwise\":%d,", R, Rg.Radius, Rg.Mini ? 1 : 0, Rg.Clockwise ? 1 : 0);
		std::string L = Buf;
		Where(Plan, Rg.Mid, L);
		L += ",\"ride\":[" + Pts + "]}\n";
		All += L;
		++Rings;
	}
	int RouteCount = 0;
	Routes(Plan, All, RouteCount);
	TurnsRec(Plan, All);
	Files::Write(Path, All);
	std::printf("rules dump: %d junctions, %d stretches, %d roundabouts, %d routes to %s\n", Junctions, Stretches, Rings, RouteCount, Path.c_str());
}
