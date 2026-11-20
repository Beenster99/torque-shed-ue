// [ferries stage 5] The sat nav over the water: a journey whose way takes a ferry (RidePlan::Ferry) has the ride's road
// to the boarding slipway; from where the ferry lands, the way on to the pins still to come is found by the map's
// roads (the area has them: the planner found the way through them) and guided as a detour that never rejoins.
#include "WorldRide.h"
#include "WorldRegion.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace TorqueWorld
{
// (WorldRideNav.cpp's, shared: a road's heading out of a junction, the turn between two headings, its kind)
namespace RideNavBits
{
P2 HeadingOut(const Region& G, int Edge, int Node);
double TurnOf(const P2& In, const P2& Out);
int TurnKind(double Angle);
} // namespace RideNavBits

namespace
{
// The cheapest way by the map's roads from one junction to another, every road both ways (a farm track or a service
// road only where nothing else goes); empty when there is none (or they are the same).
std::vector<int> WayOver(const Region& G, int From, int To)
{
	std::vector<int> Path;
	if (From < 0 || To < 0 || From == To) return Path;
	std::vector<double> Dist(G.Nodes.size(), 1e18);
	std::vector<int> Came(G.Nodes.size(), -1);
	using Reach = std::pair<double, int>;
	std::priority_queue<Reach, std::vector<Reach>, std::greater<Reach>> Next;
	Dist[size_t(From)] = 0.0;
	Next.push({0.0, From});
	while (!Next.empty())
	{
		const Reach Top = Next.top();
		Next.pop();
		if (Top.first > Dist[size_t(Top.second)]) continue;
		if (Top.second == To) break;
		for (const int E : G.Adjacent[size_t(Top.second)])
		{
			const int Other = G.OtherEnd(E, Top.second);
			const std::string& Class = G.ClassOf(E);
			const double D = Top.first + G.Edges[size_t(E)].Length * (Class == "track" || Class == "service" ? 4.0 : 1.0);
			if (D < Dist[size_t(Other)]) { Dist[size_t(Other)] = D; Came[size_t(Other)] = E; Next.push({D, Other}); }
		}
	}
	if (Came[size_t(To)] < 0) return Path;
	for (int N = To; N != From; N = G.OtherEnd(Came[size_t(N)], N)) Path.push_back(Came[size_t(N)]);
	std::reverse(Path.begin(), Path.end());
	return Path;
}
} // namespace

std::shared_ptr<const RideDetour> RidePlan::FerryOnward(double X, double Z, double Heading) const
{
	using namespace RideNavBits;
	const Region& G = Area.Roads;
	if (!Ferry || Ferry->Pins.empty() || G.Nodes.empty()) return nullptr;
	NearOpts Opts;
	Opts.NotRoad = 0;   // (the ride's road ends at the other slipway: on a short crossing it may be the nearest)
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit) || Hit.Which <= 0 || Hit.Dist > Hit.Half + 25.0) return nullptr;
	const SideRoad* On = SideOf(Hit.Which);
	if (On == nullptr || On->Edge < 0 || size_t(On->Edge) >= G.Edges.size()) return nullptr;
	// Where it leaves from: the junction ahead of the rider on their road (its end they face).
	const RoadEdge& Edge = G.Edges[size_t(On->Edge)];
	const std::vector<RoadPoint>& Pts = On->Pts;
	if (Pts.size() < 2) return nullptr;
	const size_t I0 = size_t(std::max(0, std::min(Hit.Index, int(Pts.size()) - 2)));
	const P2 Way = Pts[I0 + 1].At - Pts[I0].At;
	const P2 Facing(-std::sin(Heading), -std::cos(Heading));
	const bool ToEnd = Way.X * Facing.X + Way.Z * Facing.Z >= 0.0;
	const bool AFirst = Edge.Line.front().DistanceTo(Pts.front().At) <= Edge.Line.back().DistanceTo(Pts.front().At);
	const int Start = ToEnd == AFirst ? Edge.B : Edge.A;
	const double FirstBit = ToEnd ? On->Length - Hit.Along : Hit.Along;
	std::vector<P2> Line = {P2(X, Z)};
	if (ToEnd) { for (size_t K = I0 + 1; K < Pts.size(); ++K) Line.push_back(Pts[K].At); }
	else { for (size_t K = I0 + 1; K-- > 0;) Line.push_back(Pts[K].At); }
	const auto Append = [&Line](const std::vector<P2>& More)
	{
		for (const P2& P : More)
			if (Line.empty() || P.DistanceTo(Line.back()) > 0.5) Line.push_back(P);
	};
	Append({G.Nodes[size_t(Start)]});
	// Then through each pin still to come in turn (each at the junction nearest it), the finish last.
	std::vector<int> Edges = {On->Edge};
	std::vector<size_t> PinAt;   // each pin's place in Line
	int Node = Start;
	for (const P2& Pin : Ferry->Pins)
	{
		const int To = G.NearestNode(Pin.X, Pin.Z, 30.0);
		if (To < 0) return nullptr;
		const std::vector<int> Leg = WayOver(G, Node, To);
		if (Leg.empty() && Node != To) return nullptr;
		Append(G.PathLine(Node, Leg));
		Edges.insert(Edges.end(), Leg.begin(), Leg.end());
		PinAt.push_back(Line.size() - 1);
		Node = To;
	}
	std::shared_ptr<RideDetour> D = std::make_shared<RideDetour>();
	D->Line = Line;
	D->Along.assign(Line.size(), 0.0);
	for (size_t K = 1; K < Line.size(); ++K) D->Along[K] = D->Along[K - 1] + Line[K].DistanceTo(Line[K - 1]);
	D->Length = D->Along.back();
	D->RejoinAlong = FinishM;   // (nothing of the ride's road left to ride: ToFinish is the way on's own)
	// Its turns, as DetourFrom finds them.
	Node = Start;
	double Gone = FirstBit;
	std::vector<double> Bends;
	for (size_t K = 0; K + 1 < Edges.size(); ++K)
	{
		if (K > 0) { Gone += G.Edges[size_t(Edges[K])].Length; Node = G.OtherEnd(Edges[K], Node); }
		const int Came1 = Edges[K], Onto = Edges[K + 1];
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		const P2 BackOut = HeadingOut(G, Came1, Node);
		const P2 In(-BackOut.X, -BackOut.Z);
		const double Ours = Came1 == Onto ? 3.14159265 : TurnOf(In, HeadingOut(G, Onto, Node));
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came1 && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		if (Came1 != Onto && (Here.size() < 3 || !(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (G.NameOf(Onto) != G.NameOf(Came1) && std::fabs(Ours) > 0.26)))) continue;
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
	// Each pin an arrival, named; the last the finish.
	for (size_t K = 0; K < PinAt.size(); ++K)
	{
		RideTurn T;
		T.Along = D->Along[std::min(PinAt[K], D->Along.size() - 1)];
		T.Turn = Finish;
		T.Onto = K < Ferry->PinNames.size() ? Ferry->PinNames[K] : std::string();
		const auto At = std::upper_bound(D->Turns.begin(), D->Turns.end(), T, [](const RideTurn& A, const RideTurn& B) { return A.Along < B.Along; });
		D->Turns.insert(At, T);
	}
	// (none past the finish)
	while (!D->Turns.empty() && D->Turns.back().Along > D->Length + 0.5) D->Turns.pop_back();
	return D;
}
} // namespace TorqueWorld
