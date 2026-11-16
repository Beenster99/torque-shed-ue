// The sat nav (Ride UK stage 7): the turns along the ride's road and the places to stop on the way.
//
// The turns come from the map's road graph, as the route was found: at each junction the ride goes
// through, which way it goes against the road ahead and the others there. The stops are the map's
// fuel, cafes, pubs, viewpoints and lay-bys by the road.
#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>

namespace TorqueWorld
{
namespace RideNavBits
{
// The heading out of a junction along an edge: its first 20 m (or all of it, if shorter).
P2 HeadingOut(const Region& G, int Edge, int Node)
{
	const std::vector<P2> Line = G.LineFrom(Edge, Node);
	if (Line.size() < 2) return P2(1.0, 0.0);
	P2 To = Line.back();
	double Gone = 0.0;
	for (size_t I = 1; I < Line.size(); ++I)
	{
		Gone += Line[I].DistanceTo(Line[I - 1]);
		if (Gone >= 20.0) { To = Line[I]; break; }
	}
	const P2 D = To - Line.front();
	const double L = D.Length();
	return L > 1e-6 ? D * (1.0 / L) : P2(1.0, 0.0);
}

// How far Out turns from In (radians, + to the left: left of a heading (dx, dz) is (dz, -dx)).
double TurnOf(const P2& In, const P2& Out)
{
	return std::atan2(Out.X * In.Z - Out.Z * In.X, Out.X * In.X + Out.Z * In.Z);
}

int TurnKind(double Angle)
{
	const double A = std::fabs(Angle);
	const int Size = A < 0.35 ? 0 : A < 0.87 ? 1 : A < 2.1 ? 2 : 3;
	return Angle > 0.0 ? -Size : Size;
}
} // namespace RideNavBits

const char* RidePlan::StopName(int Kind)
{
	static const char* Names[StopKinds] = {"Fuel", "Cafe", "Pub", "Viewpoint", "Lay-by", "Inn", "Campsite", "Castle"};
	return Kind >= 0 && Kind < StopKinds ? Names[Kind] : "";
}

namespace RideNavBits
{
// [OSM stage 1] The map's places to stay are mostly holiday cottages: an inn worth stopping at is
// one its name says takes a rider in for the night (an inn, a hotel, an arms, a B&B, a guest house,
// a hostel), not a cottage let by the week.
bool Lodging(const std::string& Name)
{
	std::string Low;
	for (char C : Name) Low += char(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C);
	for (const char* Not : {"cottage", "holiday", "self catering", "self-catering", "apartment", "ltd", "properties", "lodges"})
		if (Low.find(Not) != std::string::npos) return false;
	for (const char* Yes : {"inn", "hotel", "arms", "b&b", "b & b", "bed and breakfast", "bed & breakfast", "guest house", "guesthouse", "hostel", "tavern"})
		if (Low.find(Yes) != std::string::npos) return true;
	return false;
}
} // namespace RideNavBits

void RidePlan::FindPathNodes(int From, const std::vector<int>& Path, double RawLength)
{
	// ([OSM stage 6C] as FindTurnsAndStops makes them, ahead of it: the furniture's direction signs want
	// the junctions before the turns are found)
	PathNodes.clear();
	if (Road.size() < 2 || Path.empty()) return;
	const Region& G = Area.Roads;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	int Node = From;
	double Raw = 0.0;
	PathNodes.push_back({From, Path[0], 0.0});
	for (size_t I = 0; I < Path.size(); ++I)
	{
		Raw += G.Edges[size_t(Path[I])].Length;
		Node = G.OtherEnd(Path[I], Node);
		const P2 At = G.Nodes[size_t(Node)];
		const double Guess = Raw * Scale;
		double Best = 1e18, Found = Guess;
		for (const RoadPoint& P : Road)
		{
			if (P.Along < Guess - 80.0) continue;
			if (P.Along > Guess + 80.0) break;
			const double D = P.At.DistanceTo(At);
			if (D < Best) { Best = D; Found = P.Along; }
		}
		PathNodes.push_back({Node, I + 1 < Path.size() ? Path[I + 1] : -1, Found});
	}
}

void RidePlan::FindTurnsAndStops(int From, const std::vector<int>& Path, double RawLength)
{
	using namespace RideNavBits;
	Turns.clear();
	Stops.clear();
	RingGiveWays.clear();
	if (Road.size() < 2 || Path.empty()) return;
	const Region& G = Area.Roads;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	// (where a junction of the map's road is on the road as laid: the nearest point to it within 80 m
	// of where its share of the way says)
	const auto AlongOf = [this](const P2& At, double Guess)
	{
		double Best = 1e18, Found = Guess;
		for (const RoadPoint& P : Road)
		{
			if (P.Along < Guess - 80.0) continue;
			if (P.Along > Guess + 80.0) break;
			const double D = P.At.DistanceTo(At);
			if (D < Best) { Best = D; Found = P.Along; }
		}
		return Found;
	};
	std::vector<double> Angles;
	// [OSM stage 5B] (a roundabout's edges; whether a road off one may be ridden away from it)
	const auto IsRingEdge = [&G](int E)
	{
		const RoadExtra* X = E >= 0 ? G.ExtraOf(E) : nullptr;
		return X != nullptr && X->Has(RoadExtra::Roundabout);
	};
	const auto CanLeave = [&G](int E, int AtNode)
	{
		const RoadEdge& R = G.Edges[size_t(E)];
		return R.Oneway == 0 || (R.Oneway == 1 && R.A == AtNode) || (R.Oneway == -1 && R.B == AtNode);
	};
	bool InRing = false;
	P2 RingIn;
	double RingAlong = 0.0;
	int RingExits = 0;
	int Node = From;
	double Raw = 0.0;
	PathNodes.clear();
	PathNodes.push_back({From, Path[0], 0.0});
	for (size_t I = 0; I < Path.size(); ++I)
	{
		const int Next = G.OtherEnd(Path[I], Node);
		Raw += G.Edges[size_t(Path[I])].Length;
		Node = Next;
		PathNodes.push_back({Node, I + 1 < Path.size() ? Path[I + 1] : -1, AlongOf(G.Nodes[size_t(Node)], Raw * Scale)});
		if (I + 1 >= Path.size()) break;
		const int Came = Path[I], Onto = Path[I + 1];
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		// [OSM stage 5B] Round a roundabout: one turn where the ride enters it, "the Nth exit", its way
		// the way the ride leaves it as against the way it came in; nothing said at the ring's own nodes.
		{
			const bool CameRing = IsRingEdge(Came), OntoRing = IsRingEdge(Onto);
			if (!CameRing && OntoRing)
			{
				const P2 Back0 = HeadingOut(G, Came, Node);
				RingIn = P2(-Back0.X, -Back0.Z);
				RingAlong = AlongOf(G.Nodes[size_t(Node)], Raw * Scale);
				RingExits = 0;
				InRing = true;
				continue;
			}
			if (InRing && CameRing)
			{
				// (each way off the ring here that can be taken counts as an exit)
				for (int E : Here)
					if (E != Came && E != Onto && !IsRingEdge(E) && CanLeave(E, Node)) ++RingExits;
				if (OntoRing) continue;
				InRing = false;
				if (RingAlong > FinishM) break;
				RideTurn T;
				T.Along = RingAlong;
				T.Turn = TurnKind(TurnOf(RingIn, HeadingOut(G, Onto, Node)));
				T.Onto = G.NameOf(Onto);
				T.Exit = RingExits + 1;
				Turns.push_back(T);
				// (and the ride's own give-way line where it comes onto the ring: back from the ring's
				// middle line by half the ring's width)
				RingGiveWays.push_back(RingAlong - 0.5 * RoadLine::WidthOf(G.ClassOf(Onto)) - 0.6);
				Angles.push_back(0.0);
				continue;
			}
		}
		if (Here.size() < 3) continue;   // (a bend in the road, not a junction)
		const P2 Back = HeadingOut(G, Came, Node);
		const P2 In(-Back.X, -Back.Z);
		const double Ours = TurnOf(In, HeadingOut(G, Onto, Node));
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		const std::string Was = G.NameOf(Came), Now = G.NameOf(Onto);
		// (said when the way on isn't the obvious one: a real turn, another road more straight ahead than
		// ours, or a road of another name off at an angle)
		if (!(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (Now != Was && std::fabs(Ours) > 0.26))) continue;
		const double Along = AlongOf(G.Nodes[size_t(Node)], Raw * Scale);
		if (Along > FinishM) break;
		// (a roundabout or a staggered crossroads: two turns close together are one)
		if (!Turns.empty() && Along - Turns.back().Along < 30.0 && Turns.back().Exit == 0)
		{
			Angles.back() += Ours;
			Turns.back().Turn = TurnKind(Angles.back());
			Turns.back().Onto = Now;
			continue;
		}
		RideTurn T;
		T.Along = Along;
		T.Turn = TurnKind(Ours);
		T.Onto = Now;
		Turns.push_back(T);
		Angles.push_back(Ours);
	}
	RideTurn End;
	End.Along = FinishM;
	End.Turn = Finish;
	End.Onto = Request.Pins.empty() ? std::string() : Request.Pins.back().Name;
	Turns.push_back(End);

	// The stops: the map's places by the ride's road.
	NearOpts Opts;
	Opts.MainOnly = true;
	const auto Add = [&](int Kind, const std::string& Name, const P2& At, double Reach = StopReach)
	{
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist > Reach) return;
		for (const RideStop& S : Stops)
			if (S.Kind == Kind && S.Name == Name && std::fabs(S.Along - Hit.Along) < 60.0) return;   // (one place, twice on the map)
		RideStop S;
		S.Along = Hit.Along;
		S.Off = Hit.Off;
		S.Kind = Kind;
		S.Name = Name;
		Stops.push_back(S);
	};
	for (const TorqueFit::Json& Poi : G.Pois)
	{
		const std::string What = Poi.Str("kind");
		const int Kind = What == "fuel" ? StopFuel : What == "cafe" ? StopCafe : What == "pub" || (What == "inn" && Poi.Num("conf", 1.0) >= 0.75) ? StopPub
			: What == "layby" ? StopLayby : What == "inn" && Lodging(Poi.Str("name")) ? StopInn : What == "campsite" ? StopCamp : -1;
		if (Kind >= 0) Add(Kind, Poi.Str("name"), P2(Poi.Num("x"), Poi.Num("z")));
	}
	// [OSM stage 1] The castles: further off the road is still worth the stop (one seen from it).
	for (const TorqueFit::Json& Lm : G.Landmarks)
		if (Lm.Str("kind") == "castle" && Region::LandmarkName(Lm.Str("name"))) Add(StopCastle, Lm.Str("name"), P2(Lm.Num("x"), Lm.Num("z")), 600.0);
	for (const FeaturePoint& P : G.Scenery.Points)
		if (P.Kind == "viewpoint") Add(StopView, P.Name, P.At);
	std::sort(Stops.begin(), Stops.end(), [](const RideStop& A, const RideStop& B) { return A.Along < B.Along; });
}

void RidePlan::FindThinData()
{
	Thin.clear();
	if (Road.size() < 2 || !Land) return;
	const Region& G = Area.Roads;
	const Frame F(G.FrameLat, G.FrameLon);
	const double End = FinishM > 0.0 ? FinishM : Length;
	if (!Land->HasTrees())
	{
		ThinSpot T;
		T.At = F.ToGeo(Road[0].At.X, Road[0].At.Z);
		T.Why = "No tree heights here: the trees are guessed from the land";
		Thin.push_back(T);
	}
	// A town's ground with few buildings: in each kilometre, the land 100 m either side of the road that
	// the survey calls built on, against the buildings the map has within 120 m.
	const auto Buildings = [this, &G](const P2& At)
	{
		int N = 0;
		if (!Scene) return N;
		const long long Cx = static_cast<long long>(std::floor(At.X / 256.0)), Cz = static_cast<long long>(std::floor(At.Z / 256.0));
		for (long long Dz = -1; Dz <= 1; ++Dz)
			for (long long Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find((Cx + Dx) * 4000037LL + (Cz + Dz));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
					if (std::hypot(G.Buildings[size_t(B)].X - At.X, G.Buildings[size_t(B)].Z - At.Z) < 120.0) ++N;
			}
		return N;
	};
	double ThinFrom = -1.0;
	for (double From = 0.0; From < End; From += 1000.0)
	{
		int Urban = 0, Samples = 0, Built = 0;
		size_t I = 0;
		while (I + 1 < Road.size() && Road[I].Along < From) ++I;
		for (; I < Road.size() && Road[I].Along < From + 1000.0; I += 12)   // (every 50 m or so)
		{
			const P2 P = Road[I].At;
			const P2 D = Road[std::min(I + 1, Road.size() - 1)].At - Road[I > 0 ? I - 1 : 0].At;
			const double Len = D.Length();
			const P2 Across = Len > 1e-6 ? P2(D.Z / Len, -D.X / Len) : P2(0.0, 1.0);
			for (double Off : {-100.0, 100.0})
			{
				const P2 Side = P + Across * Off;
				++Samples;
				if (Land->Cover(Side.X, Side.Z) == 5) ++Urban;
			}
			Built += Buildings(P);
		}
		// (100 m off, so a dual carriageway's own width, which the survey calls built on, isn't counted)
		const bool IsThin = Samples >= 8 && Urban * 2 >= Samples && Built * 4 < Samples;
		if (IsThin && ThinFrom < 0.0) ThinFrom = From;
		if ((!IsThin || From + 1000.0 >= End) && ThinFrom >= 0.0)
		{
			size_t K = 0;
			while (K + 1 < Road.size() && Road[K].Along < ThinFrom + 500.0) ++K;
			ThinSpot T;
			T.Along = ThinFrom;
			T.At = F.ToGeo(Road[K].At.X, Road[K].At.Z);
			T.Why = "A town with few of its buildings on the map: it may look bare";
			if (Thin.size() < 12) Thin.push_back(T);
			ThinFrom = -1.0;
		}
	}
}

RidePlan::RideNav RidePlan::NavAt(double X, double Z, double Was) const
{
	RideNav N;
	NearOpts Opts;
	Opts.MainOnly = true;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit))
	{
		N.OffRoute = true;
		N.Along = std::max(Was, 0.0);
	}
	else
	{
		N.Along = Hit.Along;
		N.OffRoute = Hit.Dist > Hit.Half + 40.0;
		// (where the road passes close by itself, keep to the stretch the rider was on)
		if (Was >= 0.0 && std::fabs(Hit.Along - Was) > 150.0 && Hit.Dist > Hit.Half) N.Along = Was;
	}
	N.ToFinish = std::max(FinishM - N.Along, 0.0);
	for (const RideTurn& T : Turns)
		if (T.Along > N.Along + 3.0 || T.Turn == Finish)
		{
			N.Turn = T.Turn;
			N.ToTurn = std::max(T.Along - N.Along, 0.0);
			N.Onto = T.Onto;
			N.Exit = T.Exit;
			break;
		}
	for (const RideStop& S : Stops)
		if (S.Along > N.Along - 15.0 && N.ToStop[S.Kind] < 0.0 && S.Along < FinishM + 50.0)
		{
			N.ToStop[S.Kind] = std::max(S.Along - N.Along, 0.0);
			N.StopCalled[S.Kind] = S.Name;
		}
	// [OSM stage 1] The road's facts here.
	if (const LimitStretch* L = LimitAt(N.Along))
	{
		N.Limit = ShownLimit(L->Kmh, &N.LimitNational);
		N.LimitAssumed = L->Assumed;
	}
	N.LimitMph = Britain();
	if (const RoadFact* F = FactAt(N.Along)) N.Road = F->Name;
	N.Place = PlaceAt(X, Z);
	return N;
}
std::shared_ptr<const RideDetour> RidePlan::DetourFrom(double X, double Z, double Heading, double Was) const
{
	using namespace RideNavBits;
	const Region& G = Area.Roads;
	NearOpts Opts;
	NearHit Hit;
	if (PathNodes.empty() || !NearestOf(X, Z, Opts, Hit) || Hit.Which <= 0 || Hit.Dist > Hit.Half + 25.0) return nullptr;
	const SideRoad* On = SideOf(Hit.Which);
	if (On == nullptr || On->Edge < 0 || size_t(On->Edge) >= G.Edges.size()) return nullptr;
	const RoadEdge& Edge = G.Edges[size_t(On->Edge)];
	// Which end the rider is heading for: the road's own way at their point against their heading.
	const std::vector<RoadPoint>& Pts = On->Pts;
	const size_t I0 = size_t(std::max(0, std::min(Hit.Index, int(Pts.size()) - 2)));
	const P2 Way = Pts[I0 + 1].At - Pts[I0].At;
	const P2 Facing(-std::sin(Heading), -std::cos(Heading));
	const bool ToEnd = Way.X * Facing.X + Way.Z * Facing.Z >= 0.0;
	// (the laid road's ends are its edge's: A at its first point unless the edge was laid the other way)
	const bool AFirst = Edge.Line.front().DistanceTo(Pts.front().At) <= Edge.Line.back().DistanceTo(Pts.front().At);
	const int Start = ToEnd == AFirst ? Edge.B : Edge.A;
	const double FirstBit = ToEnd ? On->Length - Hit.Along : Hit.Along;
	// The ride's junctions ahead, by what each leaves to ride.
	std::unordered_map<int, size_t> Ahead;
	for (size_t K = 0; K < PathNodes.size(); ++K)
		if (PathNodes[K].Along > Was + 30.0 && Ahead.count(PathNodes[K].Node) == 0) Ahead[PathNodes[K].Node] = K;
	if (Ahead.empty()) return nullptr;
	// Out from the junction ahead by the map's roads (every road both ways), nearest first, until the
	// nearest left can't beat the best found.
	std::vector<double> Dist(G.Nodes.size(), 1e18);
	std::vector<int> Came(G.Nodes.size(), -1);
	using Reach = std::pair<double, int>;
	std::priority_queue<Reach, std::vector<Reach>, std::greater<Reach>> Next;
	Dist[size_t(Start)] = 0.0;
	Next.push({0.0, Start});
	double Best = 1e18;
	int BestNode = -1;
	while (!Next.empty())
	{
		const Reach Top = Next.top();
		Next.pop();
		if (Top.first > Dist[size_t(Top.second)]) continue;
		if (Top.first >= Best) break;
		const auto It = Ahead.find(Top.second);
		if (It != Ahead.end())
		{
			const double Left = Top.first + (FinishM - PathNodes[It->second].Along);
			if (Left < Best) { Best = Left; BestNode = Top.second; }
		}
		for (const int E : G.Adjacent[size_t(Top.second)])
		{
			const int Other = G.OtherEnd(E, Top.second);
			// (a farm track or a service road only where nothing else goes)
			const std::string& Class = G.ClassOf(E);
			const double D = Top.first + G.Edges[size_t(E)].Length * (Class == "track" || Class == "service" ? 4.0 : 1.0);
			if (D < Dist[size_t(Other)]) { Dist[size_t(Other)] = D; Came[size_t(Other)] = E; Next.push({D, Other}); }
		}
	}
	if (BestNode < 0) return nullptr;
	std::vector<int> Path;
	for (int N = BestNode; N != Start; N = G.OtherEnd(Came[size_t(N)], N)) Path.push_back(Came[size_t(N)]);
	std::reverse(Path.begin(), Path.end());
	std::shared_ptr<RideDetour> D = std::make_shared<RideDetour>();
	// Its line: from the rider along their road to the junction ahead, then the map's roads.
	{
		std::vector<P2> Line = {P2(X, Z)};
		if (ToEnd) { for (size_t K = I0 + 1; K < Pts.size(); ++K) Line.push_back(Pts[K].At); }
		else { for (size_t K = I0 + 1; K-- > 0;) Line.push_back(Pts[K].At); }
		const std::vector<P2> Rest = G.PathLine(Start, Path);
		for (const P2& P : Rest)
			if (Line.empty() || P.DistanceTo(Line.back()) > 0.5) Line.push_back(P);
		D->Line = Line;
		D->Along.assign(Line.size(), 0.0);
		for (size_t K = 1; K < Line.size(); ++K) D->Along[K] = D->Along[K - 1] + Line[K].DistanceTo(Line[K - 1]);
		D->Length = D->Along.back();
	}
	const PathNode& Rejoin = PathNodes[Ahead[BestNode]];
	D->RejoinAlong = Rejoin.Along;
	// Its turns: at each junction on the way, as the ride's own are found; the last onto the ride's road.
	std::vector<int> Edges = {On->Edge};
	Edges.insert(Edges.end(), Path.begin(), Path.end());
	if (Rejoin.Next >= 0) Edges.push_back(Rejoin.Next);
	int Node = Start;
	double Gone = FirstBit;
	std::vector<double> Bends;
	for (size_t K = 0; K + 1 < Edges.size(); ++K)
	{
		if (K > 0) { Gone += G.Edges[size_t(Edges[K])].Length; Node = G.OtherEnd(Edges[K], Node); }
		const int Came1 = Edges[K], Onto = Edges[K + 1];
		const bool Last = K + 2 == Edges.size() && Rejoin.Next >= 0;
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		const P2 Back = HeadingOut(G, Came1, Node);
		const P2 In(-Back.X, -Back.Z);
		const double Ours = TurnOf(In, HeadingOut(G, Onto, Node));
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came1 && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		if (!Last && (Here.size() < 3 || !(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (G.NameOf(Onto) != G.NameOf(Came1) && std::fabs(Ours) > 0.26)))) continue;
		// (a roundabout or a staggered crossroads: two turns close together are one)
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
	if (D->Turns.empty() || D->Turns.back().Along < D->Length - 1.0)
	{
		RideTurn T;
		T.Along = D->Length;
		T.Turn = 0;
		T.Onto = G.NameOf(Rejoin.Next >= 0 ? Rejoin.Next : Path.empty() ? On->Edge : Path.back());
		D->Turns.push_back(T);
	}
	return D;
}

std::shared_ptr<const RideDetour> RidePlan::DetourVia(double X, double Z, double Heading, double Was, double ViaX, double ViaZ, const std::string& ViaName) const
{
	using namespace RideNavBits;
	const Region& G = Area.Roads;
	if (PathNodes.empty() || G.Nodes.empty()) return nullptr;
	const int ViaNode = G.NearestConnected(ViaX, ViaZ, 3000.0);
	if (ViaNode < 0) return nullptr;
	NearOpts Opts;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + 25.0) return nullptr;
	// Where it leaves from: the junction ahead of the rider, the road there, and the way to it.
	std::vector<P2> Line = {P2(X, Z)};
	int Start = -1, FirstEdge = -1;
	double FirstBit = 0.0, From = Was;
	if (Hit.Which > 0)
	{
		const SideRoad* On = SideOf(Hit.Which);
		if (On == nullptr || On->Edge < 0 || size_t(On->Edge) >= G.Edges.size()) return nullptr;
		const RoadEdge& Edge = G.Edges[size_t(On->Edge)];
		const std::vector<RoadPoint>& Pts = On->Pts;
		const size_t I0 = size_t(std::max(0, std::min(Hit.Index, int(Pts.size()) - 2)));
		const P2 Way = Pts[I0 + 1].At - Pts[I0].At;
		const P2 Facing(-std::sin(Heading), -std::cos(Heading));
		const bool ToEnd = Way.X * Facing.X + Way.Z * Facing.Z >= 0.0;
		const bool AFirst = Edge.Line.front().DistanceTo(Pts.front().At) <= Edge.Line.back().DistanceTo(Pts.front().At);
		Start = ToEnd == AFirst ? Edge.B : Edge.A;
		FirstBit = ToEnd ? On->Length - Hit.Along : Hit.Along;
		FirstEdge = On->Edge;
		if (ToEnd) { for (size_t K = I0 + 1; K < Pts.size(); ++K) Line.push_back(Pts[K].At); }
		else { for (size_t K = I0 + 1; K-- > 0;) Line.push_back(Pts[K].At); }
	}
	else
	{
		// On the ride's road: on along it to its next junction.
		From = std::max(Was, Hit.Along);
		size_t K = 0;
		while (K < PathNodes.size() && PathNodes[K].Along <= From + 5.0) ++K;
		if (K == 0 || K >= PathNodes.size()) return nullptr;
		Start = PathNodes[K].Node;
		FirstEdge = PathNodes[K - 1].Next;
		FirstBit = PathNodes[K].Along - From;
		for (const RoadPoint& P : Road)
			if (P.Along > From && P.Along < PathNodes[K].Along) Line.push_back(P.At);
		if (FirstEdge < 0) return nullptr;
	}
	if (Start < 0 || size_t(Start) >= G.Nodes.size()) return nullptr;
	// The map's roads, every one both ways (a farm track or a service road only where nothing else goes).
	using Reach = std::pair<double, int>;
	const auto Search = [&G](int Origin, const std::function<double(int, double)>& Done, std::vector<int>& Came) -> int
	{
		std::vector<double> Dist(G.Nodes.size(), 1e18);
		Came.assign(G.Nodes.size(), -1);
		std::priority_queue<Reach, std::vector<Reach>, std::greater<Reach>> Next;
		Dist[size_t(Origin)] = 0.0;
		Next.push({0.0, Origin});
		double Best = 1e18;
		int BestNode = -1;
		while (!Next.empty())
		{
			const Reach Top = Next.top();
			Next.pop();
			if (Top.first > Dist[size_t(Top.second)]) continue;
			if (Top.first >= Best) break;
			const double Score = Done(Top.second, Top.first);   // < 0: not an end
			if (Score >= 0.0 && Score < Best) { Best = Score; BestNode = Top.second; }
			for (const int E : G.Adjacent[size_t(Top.second)])
			{
				const int Other = G.OtherEnd(E, Top.second);
				const std::string& Class = G.ClassOf(E);
				const double Step = Top.first + G.Edges[size_t(E)].Length * (Class == "track" || Class == "service" ? 4.0 : 1.0);
				if (Step < Dist[size_t(Other)]) { Dist[size_t(Other)] = Step; Came[size_t(Other)] = E; Next.push({Step, Other}); }
			}
		}
		return BestNode;
	};
	const auto PathTo = [&G](int Origin, int End, const std::vector<int>& Came)
	{
		std::vector<int> Path;
		for (int N = End; N != Origin; N = G.OtherEnd(Came[size_t(N)], N)) Path.push_back(Came[size_t(N)]);
		std::reverse(Path.begin(), Path.end());
		return Path;
	};
	// There: the junction nearest the waypoint.
	std::vector<int> Came;
	if (Search(Start, [ViaNode](int N, double D) { return N == ViaNode ? D : -1.0; }, Came) != ViaNode) return nullptr;
	const std::vector<int> There = PathTo(Start, ViaNode, Came);
	// And back: the junction of the ride's road ahead that leaves least to ride to the finish.
	std::unordered_map<int, size_t> Ahead;
	for (size_t K = 0; K < PathNodes.size(); ++K)
		if (PathNodes[K].Along > From + 30.0 && Ahead.count(PathNodes[K].Node) == 0) Ahead[PathNodes[K].Node] = K;
	if (Ahead.empty()) return nullptr;
	const double FinishAt = FinishM;
	const int BackTo = Search(ViaNode, [&Ahead, this, FinishAt](int N, double D)
	{
		const auto It = Ahead.find(N);
		return It == Ahead.end() ? -1.0 : D + (FinishAt - PathNodes[It->second].Along);
	}, Came);
	if (BackTo < 0) return nullptr;
	const std::vector<int> Back = PathTo(ViaNode, BackTo, Came);
	std::shared_ptr<RideDetour> D = std::make_shared<RideDetour>();
	// Its line: to the junction ahead, there by the map's roads, and back.
	const auto Append = [&Line](const std::vector<P2>& More)
	{
		for (const P2& P : More)
			if (Line.empty() || P.DistanceTo(Line.back()) > 0.5) Line.push_back(P);
	};
	Append({G.Nodes[size_t(Start)]});
	Append(G.PathLine(Start, There));
	size_t ViaIndex = Line.size() - 1;
	Append(G.PathLine(ViaNode, Back));
	D->Line = Line;
	D->Along.assign(Line.size(), 0.0);
	for (size_t K = 1; K < Line.size(); ++K) D->Along[K] = D->Along[K - 1] + Line[K].DistanceTo(Line[K - 1]);
	D->Length = D->Along.back();
	ViaIndex = std::min(ViaIndex, Line.size() - 1);
	D->ViaAlong = D->Along[ViaIndex];
	D->Via = G.Nodes[size_t(ViaNode)];
	const PathNode& Rejoin = PathNodes[Ahead[BackTo]];
	D->RejoinAlong = Rejoin.Along;
	// Its turns, as DetourFrom finds them; the last onto the ride's road.
	std::vector<int> Edges = {FirstEdge};
	Edges.insert(Edges.end(), There.begin(), There.end());
	Edges.insert(Edges.end(), Back.begin(), Back.end());
	if (Rejoin.Next >= 0) Edges.push_back(Rejoin.Next);
	int Node = Start;
	double Gone = FirstBit;
	std::vector<double> Bends;
	for (size_t K = 0; K + 1 < Edges.size(); ++K)
	{
		if (K > 0) { Gone += G.Edges[size_t(Edges[K])].Length; Node = G.OtherEnd(Edges[K], Node); }
		const int Came1 = Edges[K], Onto = Edges[K + 1];
		const bool Last = K + 2 == Edges.size() && Rejoin.Next >= 0;
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
		const P2 BackOut = HeadingOut(G, Came1, Node);
		const P2 In(-BackOut.X, -BackOut.Z);
		const double Ours = Came1 == Onto ? 3.14159265 : TurnOf(In, HeadingOut(G, Onto, Node));   // (the same road back: turn round)
		double Straightest = 1e9;
		for (int E : Here)
			if (E != Came1 && E != Onto) Straightest = std::min(Straightest, std::fabs(TurnOf(In, HeadingOut(G, E, Node))));
		if (!Last && Came1 != Onto && (Here.size() < 3 || !(std::fabs(Ours) > 0.6 || Straightest < std::fabs(Ours) || (G.NameOf(Onto) != G.NameOf(Came1) && std::fabs(Ours) > 0.26)))) continue;
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
	// The waypoint: an arrival there, as at the finish, named.
	{
		RideTurn T;
		T.Along = D->ViaAlong;
		T.Turn = Finish;
		T.Onto = ViaName;
		const auto At = std::lower_bound(D->Turns.begin(), D->Turns.end(), T, [](const RideTurn& A, const RideTurn& B) { return A.Along < B.Along; });
		D->Turns.insert(At, T);
	}
	if (D->Turns.back().Along < D->Length - 1.0)
	{
		RideTurn T;
		T.Along = D->Length;
		T.Turn = 0;
		T.Onto = G.NameOf(Rejoin.Next >= 0 ? Rejoin.Next : Back.empty() ? FirstEdge : Back.back());
		D->Turns.push_back(T);
	}
	return D;
}

RidePlan::RideNav RidePlan::NavDetour(const RideDetour& D, double X, double Z) const
{
	RideNav N;
	double Best = 1e18, At = 0.0;
	for (size_t K = 0; K + 1 < D.Line.size(); ++K)
	{
		const P2 A = D.Line[K], Ab = D.Line[K + 1] - A;
		const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
		const double T = L2 > 1e-9 ? std::clamp(((X - A.X) * Ab.X + (Z - A.Z) * Ab.Z) / L2, 0.0, 1.0) : 0.0;
		const double Dx = A.X + Ab.X * T - X, Dz = A.Z + Ab.Z * T - Z;
		const double D2 = Dx * Dx + Dz * Dz;
		if (D2 < Best) { Best = D2; At = D.Along[K] + (D.Along[K + 1] - D.Along[K]) * T; }
	}
	N.OffRoute = std::sqrt(Best) > 40.0;
	N.Along = At;
	N.ToFinish = std::max(D.Length - At, 0.0) + std::max(FinishM - D.RejoinAlong, 0.0);
	N.Turn = Finish;
	for (const RideTurn& T : D.Turns)
		if (T.Along > At + 3.0)
		{
			N.Turn = T.Turn;
			N.ToTurn = T.Along - At;
			N.Onto = T.Onto;
			break;
		}
	NearOpts Opts;
	NearHit Hit;
	if (NearestOf(X, Z, Opts, Hit) && Hit.Which > 0)
		if (const SideRoad* On = SideOf(Hit.Which)) N.Road = Area.Roads.NameOf(On->Edge);
	N.LimitMph = Britain();
	N.Place = PlaceAt(X, Z);
	return N;
}
} // namespace TorqueWorld
