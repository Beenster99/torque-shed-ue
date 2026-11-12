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
	static const char* Names[StopKinds] = {"Fuel", "Cafe", "Pub", "Viewpoint", "Lay-by"};
	return Kind >= 0 && Kind < StopKinds ? Names[Kind] : "";
}

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
	const auto Add = [&](int Kind, const std::string& Name, const P2& At)
	{
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist > StopReach) return;
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
			: What == "layby" ? StopLayby : -1;
		if (Kind >= 0) Add(Kind, Poi.Str("name"), P2(Poi.Num("x"), Poi.Num("z")));
	}
	for (const FeaturePoint& P : G.Scenery.Points)
		if (P.Kind == "viewpoint") Add(StopView, P.Name, P.At);
	std::sort(Stops.begin(), Stops.end(), [](const RideStop& A, const RideStop& B) { return A.Along < B.Along; });
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
	return N;
}
} // namespace TorqueWorld
