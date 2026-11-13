// The sat nav (Ride UK stage 7): the turns along the ride's road and the places to stop on the way.
//
// The turns come from the map's road graph, as the route was found: at each junction the ride goes
// through, which way it goes against the road ahead and the others there. The stops are the map's
// fuel, cafes, pubs, viewpoints and lay-bys by the road.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

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

void RidePlan::FindTurnsAndStops(int From, const std::vector<int>& Path, double RawLength)
{
	using namespace RideNavBits;
	Turns.clear();
	Stops.clear();
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
	int Node = From;
	double Raw = 0.0;
	for (size_t I = 0; I < Path.size(); ++I)
	{
		const int Next = G.OtherEnd(Path[I], Node);
		Raw += G.Edges[size_t(Path[I])].Length;
		Node = Next;
		if (I + 1 >= Path.size()) break;
		const int Came = Path[I], Onto = Path[I + 1];
		const std::vector<int>& Here = G.Adjacent[size_t(Node)];
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
		if (!Turns.empty() && Along - Turns.back().Along < 30.0)
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
		if (Lm.Str("kind") == "castle") Add(StopCastle, Lm.Str("name"), P2(Lm.Num("x"), Lm.Num("z")), 600.0);
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
} // namespace TorqueWorld
