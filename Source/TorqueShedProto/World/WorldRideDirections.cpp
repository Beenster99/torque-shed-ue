// [OSM stage 6C] The direction signs of the ride's road's junctions (see RidePlan::DirectionSigns).
#include "WorldRide.h"
#include "WorldRideSigns.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <cmath>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>

namespace TorqueWorld
{
namespace
{
// (a class's rank: the lower the greater; -1 for no road a car takes)
int ClassRank(const std::string& C)
{
	if (C == "motorway" || C == "motorway_link") return 0;
	if (C == "trunk" || C == "trunk_link") return 1;
	if (C == "primary" || C == "primary_link") return 2;
	if (C == "secondary" || C == "secondary_link") return 3;
	if (C == "tertiary" || C == "tertiary_link") return 4;
	if (C == "unclassified") return 5;
	if (C == "residential" || C == "living_street") return 6;
	return -1;
}
} // namespace

std::vector<RidePlan::Furnish> RidePlan::DirectionSigns() const
{
	std::vector<Furnish> Out;
	if (!Britain() || Road.size() < 2 || PathNodes.size() < 2) return Out;
	const std::shared_ptr<const Route::Gazetteer> Places = SignPlacesNow();
	if (!Places) return Out;
	const Region& G = Area.Roads;
	if (G.Nodes.empty() || G.Adjacent.size() != G.Nodes.size()) return Out;
	// The places the signs can name: the gazetteer's towns and villages over the map loaded, each at the
	// junction nearest it within 400 m.
	struct Named { std::string Name; int Pop = 0; int Node = -1; };
	std::vector<Named> Towns;
	{
		const Frame F = G.FrameOf();
		const Geo A = F.ToGeo(G.Bounds.X0, G.Bounds.Z0), B = F.ToGeo(G.Bounds.X1, G.Bounds.Z1);
		const auto Cell = [](double X, double Z) { return static_cast<long long>(std::floor(X / 400.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 400.0)); };
		std::unordered_map<long long, std::vector<int>> Grid;
		for (size_t N = 0; N < G.Nodes.size(); ++N)
		{
			bool Motor = false;
			for (int E : G.Adjacent[N]) Motor = Motor || ClassRank(G.ClassOf(E)) >= 0;
			if (Motor) Grid[Cell(G.Nodes[N].X, G.Nodes[N].Z)].push_back(int(N));
		}
		for (const Route::Place* P : Places->InBox(std::fmin(A.Lat, B.Lat), std::fmin(A.Lon, B.Lon), std::fmax(A.Lat, B.Lat), std::fmax(A.Lon, B.Lon), "city town village", 600))
		{
			const P2 At = F.FromGeo(P->Lat, P->Lon);
			int Best = -1;
			double BestD = 400.0;
			for (int Dz = -1; Dz <= 1; ++Dz)
				for (int Dx = -1; Dx <= 1; ++Dx)
				{
					const auto It = Grid.find(Cell(At.X + Dx * 400.0, At.Z + Dz * 400.0));
					if (It == Grid.end()) continue;
					for (int N : It->second)
					{
						const double D = G.Nodes[size_t(N)].DistanceTo(At);
						if (D < BestD) { BestD = D; Best = N; }
					}
				}
			if (Best < 0) continue;
			// (an estate, a park of businesses: no place a sign sends anyone to)
			bool Works = false;
			for (const char* Word : {"Estate", "Trading", "Industrial", "Retail", "Business", "Enterprise"}) Works = Works || P->Name.find(Word) != std::string::npos;
			if (Works) continue;
			Named T;
			T.Name = P->Name;
			T.Pop = P->Population > 0 ? P->Population : P->Kind == "city" ? 100000 : P->Kind == "town" ? 5000 : 300;
			T.Node = Best;
			Towns.push_back(T);
		}
	}
	if (Towns.empty()) return Out;
	// [6D] The places worth a brown sign, each at the junction nearest it within 600 m (and how far off it).
	struct Seen { std::string Name; int Node = -1; double Off = 0.0; };
	std::vector<Seen> Sights;
	{
		static const char* const Worth[] = {"cathedral", "abbey", "castle", "castle_ruin", "lighthouse", "windmill", "watermill", "pier", "hillfort", "historic_house",
			"battlefield", "museum", "attraction", "waterfall", "cave", "stone_circle", "industrial_heritage"};
		for (const Sight& S : G.Sights)
		{
			if (S.Name.empty() || std::none_of(std::begin(Worth), std::end(Worth), [&S](const char* K) { return S.Kind == K; })) continue;
			// (none in sight of the ride's road: within 300 m of it)
			NearOpts Opts;
			NearHit Hit;
			if (NearestOf(S.At.X, S.At.Z, Opts, Hit) && Hit.Which == 0 && Hit.Dist < 300.0) continue;
			int Best = -1;
			double BestD = 600.0;
			for (size_t N = 0; N < G.Nodes.size(); ++N)
			{
				const double D = G.Nodes[N].DistanceTo(S.At);
				if (D < BestD) { BestD = D; Best = int(N); }
			}
			if (Best >= 0) Sights.push_back({S.Name, Best, BestD});
		}
	}
	const auto Dir = [this](size_t I)
	{
		const size_t A = I > 0 ? I - 1 : 0, B = std::min(Road.size() - 1, I + 1);
		const P2 D = Road[B].At - Road[A].At;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const auto PointAt = [this](double Along)
	{
		size_t Lo = 0, Hi = Road.size();
		while (Hi - Lo > 1)
		{
			const size_t M = (Lo + Hi) / 2;
			if (Road[M].Along <= Along) Lo = M;
			else Hi = M;
		}
		return Lo;
	};
	const auto Verge = [&](size_t I, double Side, double Past)
	{
		const P2 D = Dir(I);
		const P2 L(D.Z, -D.X);
		return Road[I].At + L * (Side * (Road[I].Half + Past));
	};
	const auto OffAll = [this](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	// (an edge's heading out of a node, 20 m along it or its far end)
	const auto HeadingOut = [&G](int E, int Node)
	{
		const RoadEdge& Ed = G.Edges[size_t(E)];
		const bool FromA = Ed.A == Node;
		const P2 Start = FromA ? Ed.Line.front() : Ed.Line.back();
		P2 Far = FromA ? Ed.Line.back() : Ed.Line.front();
		for (size_t K = 1; K < Ed.Line.size(); ++K)
		{
			const P2& P = FromA ? Ed.Line[K] : Ed.Line[Ed.Line.size() - 1 - K];
			if (P.DistanceTo(Start) >= 20.0) { Far = P; break; }
		}
		const P2 D = Far - Start;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const double Keep = KeepLeft() ? 1.0 : -1.0;
	// (the plan's letter heights: 100 mm capitals at 30, 150 mm at 40 to 50, 250 mm at 60 to 70; a lane's
	// signs no bigger than 150 mm)
	const auto CapFor = [](int Mph) { return Mph <= 30 ? 0.1 : Mph <= 50 ? 0.15 : 0.25; };
	const auto MphAt = [this](double Along)
	{
		const LimitStretch* L = LimitAt(std::clamp(Along, 0.0, Length));
		return L != nullptr ? ShownLimit(L->Kmh) : 0;
	};
	// (the ride's road's junctions with any road a car takes off it: a sign stands between two)
	std::vector<double> Meets;
	for (size_t Pn = 1; Pn + 1 < PathNodes.size(); ++Pn)
	{
		const PathNode& J = PathNodes[Pn];
		if (J.Node < 0 || size_t(J.Node) >= G.Nodes.size()) continue;
		for (int E : G.Adjacent[size_t(J.Node)])
			if (E != PathNodes[Pn - 1].Next && E != J.Next && ClassRank(G.ClassOf(E)) >= 0) { Meets.push_back(J.Along); break; }
	}
	const auto Gap = [&Meets](double Along, int Way)
	{
		double Best = 1e9;
		for (double M : Meets)
			if ((M - Along) * Way > 1.0) Best = std::fmin(Best, std::fabs(M - Along));
		return Best;
	};
	const auto Link = [&G](int E) { const std::string& C = G.ClassOf(E); return C == "motorway" || C.find("_link") != std::string::npos; };
	// (each place worth seeing signed once each way: from the junction nearest it)
	struct Brown { Furnish F; std::string Name; int Way = 0; double D = 0.0; };
	std::vector<Brown> Browned;
	std::vector<double> Dist(G.Nodes.size(), 1e18);
	std::vector<int> Branch(G.Nodes.size(), -1);
	std::vector<int> Touched;
	for (size_t Pn = 1; Pn + 1 < PathNodes.size(); ++Pn)
	{
		const PathNode& J = PathNodes[Pn];
		if (J.Node < 0 || size_t(J.Node) >= G.Nodes.size() || J.Along < 60.0 || J.Along > Length - 60.0 || RingOfNode.count(J.Node) != 0) continue;
		const int Before = PathNodes[Pn - 1].Next, After = J.Next;
		if (Before < 0 || After < 0) continue;
		const int RideRank = std::min(ClassRank(G.ClassOf(Before)), ClassRank(G.ClassOf(After)));
		if (RideRank < 0) continue;
		// (a side road of tertiary or more, no greater than the ride's road: signed from the ride's road)
		std::vector<int> Sides;
		bool Worth = false, Lesser = true;
		for (int E : G.Adjacent[size_t(J.Node)])
		{
			if (E == Before || E == After) continue;
			const int R = ClassRank(G.ClassOf(E));
			if (R < 0) continue;
			Sides.push_back(E);
			Worth = Worth || R <= 4;
			Lesser = Lesser && R >= RideRank;
		}
		bool Slips = Link(Before) || Link(After);
		for (int E : Sides) Slips = Slips || Link(E);
		// (in a town only a secondary road or more is signed; a motorway's junctions and slip roads are
		// stage 6F's)
		const size_t Ij = std::min(Road.size() - 1, size_t(std::lower_bound(Road.begin(), Road.end(), J.Along, [](const RoadPoint& P, double A) { return P.Along < A; }) - Road.begin()));
		bool Major = false;
		for (int E : Sides) Major = Major || ClassRank(G.ClassOf(E)) <= 3;
		const bool Town = Ij < BuiltUp.size() && BuiltUp[Ij];
		// (what this junction gets: map-type signs; [6D] a fingerpost where only lanes meet the ride's road out
		// of town; a brown sign where a place worth seeing is off one of its side roads)
		const bool Signed = !Sides.empty() && Worth && Lesser && !Slips && !(Town && !Major);
		const bool Fingers = !Sides.empty() && !Worth && !Slips && !Town && RideRank >= 3;
		const bool Browns = !Sides.empty() && !Slips && !Sights.empty();
		if (!Signed && !Fingers && !Browns) continue;
		const double Reach = Signed ? 60000.0 : 25000.0;
		// The shortest way to every junction within 60 km, and which road out of this one it starts by.
		for (int N : Touched) { Dist[size_t(N)] = 1e18; Branch[size_t(N)] = -1; }
		Touched.clear();
		using Item = std::pair<double, int>;
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> Open;
		Dist[size_t(J.Node)] = 0.0;
		Touched.push_back(J.Node);
		Open.push({0.0, J.Node});
		while (!Open.empty())
		{
			const Item Top = Open.top();
			Open.pop();
			const int N = Top.second;
			if (Top.first > Dist[size_t(N)] || Top.first > Reach) continue;
			for (int E : G.Adjacent[size_t(N)])
			{
				if (ClassRank(G.ClassOf(E)) < 0) continue;
				const RoadEdge& Ed = G.Edges[size_t(E)];
				const int M = Ed.A == N ? Ed.B : Ed.A;
				const double D = Top.first + std::fmax(1.0, Ed.Length);
				if (D >= Dist[size_t(M)]) continue;
				if (Dist[size_t(M)] >= 1e17) Touched.push_back(M);
				Dist[size_t(M)] = D;
				Branch[size_t(M)] = N == J.Node ? E : Branch[size_t(N)];
				Open.push({D, M});
			}
		}
		// Each road out's places: the nearest town or village 800 m on or more, and a bigger place further
		// on (5 km or more, at least twice its size), the nearer first.
		const auto PlacesBy = [&](int E, std::vector<std::pair<std::string, double>>& List)
		{
			List.clear();
			const Named* Near = nullptr;
			const Named* Big = nullptr;
			for (const Named& T : Towns)
			{
				if (Branch[size_t(T.Node)] != E) continue;
				const double D = Dist[size_t(T.Node)];
				if (D < 800.0 || D > 60000.0) continue;
				if (Near == nullptr || D < Dist[size_t(Near->Node)]) Near = &T;
			}
			for (const Named& T : Towns)
			{
				if (Branch[size_t(T.Node)] != E || &T == Near) continue;
				const double D = Dist[size_t(T.Node)];
				if (D < 5000.0 || D > 60000.0 || T.Pop < 1000 || (Near != nullptr && (D < Dist[size_t(Near->Node)] * 1.5 || T.Pop < Near->Pop * 2))) continue;
				if (Big == nullptr || T.Pop > Big->Pop) Big = &T;
			}
			if (Near != nullptr) List.emplace_back(Near->Name, Dist[size_t(Near->Node)]);
			if (Big != nullptr && (Near == nullptr || Big->Name != Near->Name)) List.emplace_back(Big->Name, Dist[size_t(Big->Node)]);
		};
		const size_t I = PointAt(J.Along);
		const RoadFact* Fact = FactAt(J.Along);
		const int Rank = Fact != nullptr ? ClassRank(Fact->Class) : RideRank;
		// (in Britain's mapping trunk is a primary route, green; primary is any other A road, white as a B road's)
		const int Scheme = Rank == 0 ? SignPlate::SchemeMotorway : Rank == 1 ? SignPlate::SchemePrimary : SignPlate::SchemeLocal;
		// [6D] Brown signs: the nearest place worth seeing (8 km or less, not already in sight of the ride's
		// road) by each side road, a brown flag at the junction pointing to it, for the traffic each way.
		for (int Way : {1, -1})
		{
			if (!Browns) break;
			const P2 Going = Dir(I) * double(Way);
			const P2 LeftOf(Going.Z, -Going.X);
			const Seen* Best = nullptr;
			double BestD = 8000.0, BestSide = 0.0;
			for (const Seen& S : Sights)
			{
				const int E = Branch[size_t(S.Node)];
				if (E < 0 || std::find(Sides.begin(), Sides.end(), E) == Sides.end()) continue;
				const double D = Dist[size_t(S.Node)] + S.Off;
				const P2 H = HeadingOut(E, J.Node);
				const double Sd = H.X * LeftOf.X + H.Z * LeftOf.Z;
				if (D >= BestD || std::fabs(Sd) < 0.4) continue;
				Best = &S;
				BestD = D;
				BestSide = Sd;
			}
			if (Best == nullptr) continue;
			const std::string Words = std::string(BestSide > 0.0 ? "<" : ">") + Best->Name + "\t" + SignPlate::Miles(std::fmax(BestD, 402.0));
			const int Mph = MphAt(J.Along - Way * 30.0);
			const double Cap = std::fmin(0.1, CapFor(Mph));
			double W = 0.0, H = 0.0;
			if (Mph <= 0 || !SignPlate::FlagBoard(nullptr, nullptr, SignPlate::Frame(), Cap, SignPlate::SchemeBrown, Words, W, H)) continue;
			for (double Back = 15.0; Back <= 35.0; Back += 10.0)
			{
				const double At = J.Along - Way * Back;
				if (At < 10.0 || At > Length - 10.0 || Back > Gap(J.Along, -Way) - 10.0) break;
				const size_t K = PointAt(At);
				Furnish F;
				F.Kind = BrownFlag;
				F.Mapped = false;
				F.Size = SignPlate::SchemeBrown;
				F.Big = Cap;
				F.Text = Words;
				F.At = Verge(K, Way * Keep, 0.7 + W * 0.5);
				F.Face = Dir(K) * -double(Way);
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (std::isnan(F.Height) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (W * 0.5), 0.5) || !OffAll(F.At - Across * (W * 0.5), 0.5)) continue;
				Browned.push_back({F, Best->Name, Way, BestD});
				break;
			}
		}
		// [6D] A fingerpost: an arm for each road out (the ride's road both ways too) with its nearest place
		// and the miles, on the corner of a lane's mouth.
		if (Fingers)
		{
			std::string Arms;
			int Count = 0;
			std::vector<int> Outs = Sides;
			Outs.push_back(Before);
			Outs.push_back(After);
			for (int E : Outs)
			{
				const Named* Near = nullptr;
				for (const Named& T : Towns)
				{
					if (Branch[size_t(T.Node)] != E) continue;
					const double D = Dist[size_t(T.Node)];
					if (D >= 400.0 && D <= 25000.0 && (Near == nullptr || D < Dist[size_t(Near->Node)])) Near = &T;
				}
				if (Near == nullptr) continue;
				const P2 H = HeadingOut(E, J.Node);
				char Head[48];
				std::snprintf(Head, sizeof(Head), "%.3f,%.3f;", H.X, H.Z);
				Arms += (Arms.empty() ? "" : "|") + std::string(Head) + Near->Name + "\t" + SignPlate::Miles(Dist[size_t(Near->Node)]);
				++Count;
			}
			if (Count >= 2)
			{
				// (on the first lane's corner, its left or its right, wherever is off every road)
				const P2 H = HeadingOut(Sides[0], J.Node);
				const P2 L(H.Z, -H.X);
				const double Half = 2.5;
				for (double Sigma : {1.0, -1.0})
				{
					Furnish F;
					F.Kind = Fingerpost;
					F.Mapped = false;
					F.Big = 0.055;
					F.Text = Arms;
					F.At = G.Nodes[size_t(J.Node)] + H * 9.0 + L * (Sigma * (Half + 1.6));
					F.Face = H * -1.0;
					F.Height = Ground(F.At.X, F.At.Z);
					if (std::isnan(F.Height) || !OffAll(F.At, 1.4)) continue;
					Out.push_back(F);
					break;
				}
			}
		}
		for (int Way : {1, -1})
		{
			if (!Signed) break;
			const P2 Going = Dir(I) * double(Way);
			const P2 LeftOf(Going.Z, -Going.X);
			const int Ahead = Way > 0 ? After : Before;
			std::string Arms;
			std::vector<std::pair<std::string, double>> List, AheadList;
			PlacesBy(Ahead, AheadList);
			bool AnySide = false;
			std::string Left, Right;
			for (int E : Sides)
			{
				const P2 H = HeadingOut(E, J.Node);
				const double Sd = H.X * LeftOf.X + H.Z * LeftOf.Z;
				if (std::fabs(Sd) < 0.4) continue;   // (one going on or back the way: no arm for it)
				PlacesBy(E, List);
				std::string& To = Sd > 0.0 ? Left : Right;
				for (const auto& P : List)
					if (To.find(P.first) == std::string::npos) To += (To.empty() ? "" : ";") + P.first;
				AnySide = AnySide || !List.empty();
			}
			if (!AnySide) continue;
			std::string AheadNames;
			for (const auto& P : AheadList) AheadNames += (AheadNames.empty() ? "" : ";") + P.first;
			if (!AheadNames.empty()) Arms += "^" + AheadNames;
			if (!Left.empty()) Arms += (Arms.empty() ? "<" : "|<") + Left;
			if (!Right.empty()) Arms += (Arms.empty() ? ">" : "|>") + Right;
			// The advance sign, as far before the junction as a warning would stand, on the traffic's side.
			const int Mph = MphAt(J.Along - Way * 60.0);
			if (Mph <= 0) continue;
			const double Cap = Rank >= 3 ? std::fmin(0.15, CapFor(Mph)) : CapFor(Mph);
			double W = 0.0, H = 0.0;
			if (!SignPlate::AdvanceBoard(nullptr, nullptr, SignPlate::Frame(), Cap, Scheme, Arms, W, H)) continue;
			// (no further back than 20 m short of the junction before it: down to 30 m before this one)
			const double Room = Gap(J.Along, -Way) - 20.0;
			const double Before2 = std::fmin(Mph <= 30 ? 45.0 : Mph <= 40 ? 65.0 : Mph <= 50 ? 100.0 : Mph <= 60 ? 150.0 : 200.0, Room);
			for (double Extra = 0.0; Extra <= 30.0 && Before2 >= 30.0; Extra += 10.0)
			{
				if (Before2 + Extra > Room) break;
				const double At = J.Along - Way * (Before2 + Extra);
				if (At < 20.0 || At > Length - 20.0) break;
				const size_t K = PointAt(At);
				Furnish F;
				F.Kind = DirAdvance;
				F.Mapped = false;
				F.Size = Scheme;
				F.Big = Cap;
				F.Text = Arms;
				F.At = Verge(K, Way * Keep, 0.7 + W * 0.5);
				F.Face = Dir(K) * -double(Way);
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (std::isnan(F.Height) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (W * 0.5), 0.5) || !OffAll(F.At - Across * (W * 0.5), 0.5)) continue;
				Out.push_back(F);
				break;
			}
			// After a junction on a primary or secondary road, the places ahead with their miles.
			if (Rank > 3 || AheadList.empty() || Gap(J.Along, Way) < 250.0) continue;
			std::string Lines;
			for (const auto& P : AheadList)
			{
				const std::string M = SignPlate::Miles(P.second - 60.0);
				Lines += (Lines.empty() ? "" : "|") + P.first + "\t" + M;
			}
			const int MphOn = MphAt(J.Along + Way * 60.0);
			if (MphOn <= 0) continue;
			const double CapOn = Rank >= 3 ? std::fmin(0.15, CapFor(MphOn)) : CapFor(MphOn);
			if (!SignPlate::ConfirmBoard(nullptr, nullptr, SignPlate::Frame(), CapOn, Scheme, Lines, W, H)) continue;
			for (double Extra = 0.0; Extra <= 40.0; Extra += 10.0)
			{
				const double At = J.Along + Way * (60.0 + Extra);
				if (At < 20.0 || At > Length - 20.0) break;
				const size_t K = PointAt(At);
				Furnish F;
				F.Kind = DirConfirm;
				F.Mapped = false;
				F.Size = Scheme;
				F.Big = CapOn;
				F.Text = Lines;
				F.At = Verge(K, Way * Keep, 0.7 + W * 0.5);
				F.Face = Dir(K) * -double(Way);
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (std::isnan(F.Height) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (W * 0.5), 0.5) || !OffAll(F.At - Across * (W * 0.5), 0.5)) continue;
				Out.push_back(F);
				break;
			}
		}
	}
	// [6F] Countdown markers before a motorway's exits: where a slip road leaves the ride's road on a
	// motorway (bearing off ahead, on the nearside), three bars at 300 yards, two at 200, one at 100.
	// (Junction numbers and gantries wait for the map's motorway_junction points in the roadx bake.)
	for (size_t Pn = 1; Pn + 1 < PathNodes.size(); ++Pn)
	{
		const PathNode& J = PathNodes[Pn];
		const int Before = PathNodes[Pn - 1].Next, After = J.Next;
		if (J.Node < 0 || size_t(J.Node) >= G.Nodes.size() || Before < 0 || After < 0) continue;
		if (G.ClassOf(Before) != "motorway" || G.ClassOf(After) != "motorway") continue;
		const size_t I = PointAt(J.Along);
		for (int Way : {1, -1})
		{
			// (a one-way carriageway only the way it runs)
			if (Road[I].OneWay && Way < 0) continue;
			const P2 Going = Dir(I) * double(Way);
			const P2 NearSide = P2(Going.Z, -Going.X) * Keep;   // (the left in Britain)
			bool Exit = false;
			for (int E : G.Adjacent[size_t(J.Node)])
			{
				// (a slip road: mapped as a link, or as motorway where the bake keeps no links; one that can be
				// ridden away from here, not one joining)
				if (E == Before || E == After || (G.ClassOf(E) != "motorway_link" && G.ClassOf(E) != "motorway")) continue;
				const RoadEdge& Ed = G.Edges[size_t(E)];
				if (!(Ed.Oneway == 0 || (Ed.Oneway == 1 && Ed.A == J.Node) || (Ed.Oneway == -1 && Ed.B == J.Node))) continue;
				const P2 H = HeadingOut(E, J.Node);
				// (a slip runs alongside at first: which side it is on 100 m along it, against the ride's road there)
				const bool FromA = Ed.A == J.Node;
				P2 Far = FromA ? Ed.Line.back() : Ed.Line.front();
				double Run = 0.0;
				for (size_t Kk = 1; Kk < Ed.Line.size(); ++Kk)
				{
					const P2& P0 = FromA ? Ed.Line[Kk - 1] : Ed.Line[Ed.Line.size() - Kk];
					const P2& P1 = FromA ? Ed.Line[Kk] : Ed.Line[Ed.Line.size() - 1 - Kk];
					Run += P0.DistanceTo(P1);
					if (Run >= 100.0) { Far = P1; break; }
				}
				const size_t Ka = PointAt(std::clamp(J.Along + Way * std::fmin(Run, 100.0), 0.0, Length));
				const P2 Off = Far - Road[Ka].At;
				Exit = Exit || (H.X * Going.X + H.Z * Going.Z > 0.5 && Off.X * NearSide.X + Off.Z * NearSide.Z > 2.0);
			}
			if (!Exit) continue;
			for (int Bars = 3; Bars >= 1; --Bars)
			{
				const double At = J.Along - Way * Bars * 91.44;
				if (At < 10.0 || At > Length - 10.0) continue;
				const size_t K = PointAt(At);
				Furnish F;
				F.Kind = Countdown;
				F.Mapped = false;
				F.Size = Bars;
				F.At = Verge(K, Way * Keep, 1.2 + 0.45);
				F.Face = Dir(K) * -double(Way);
				F.Height = Ground(F.At.X, F.At.Z);
				if (!std::isnan(F.Height) && OffAll(F.At, 0.8)) Out.push_back(F);
			}
		}
	}
	// [6D/6E] Boundaries: where the ride's road crosses a mapped county or country border and the places
	// either side (3 km off along the road) are in different ones, a white board each way naming the one
	// being entered: the county's name; a country's welcome ("Croeso i Gymru" above "Welcome to Wales").
	for (const FeatureLine& L : G.Scenery.Lines)
	{
		if ((L.Kind != "region_border" && L.Kind != "country_border") || L.Line.size() < 2) continue;
		for (size_t S = 0; S + 1 < L.Line.size(); ++S)
		{
			const P2 A = L.Line[S], B = L.Line[S + 1];
			for (size_t K = 0; K + 1 < Road.size(); ++K)
			{
				const P2 C = Road[K].At, D = Road[K + 1].At;
				if (std::fmax(C.X, D.X) < std::fmin(A.X, B.X) || std::fmin(C.X, D.X) > std::fmax(A.X, B.X) || std::fmax(C.Z, D.Z) < std::fmin(A.Z, B.Z) || std::fmin(C.Z, D.Z) > std::fmax(A.Z, B.Z)) continue;
				const auto Side = [](const P2& P, const P2& Q, const P2& R) { return (Q.X - P.X) * (R.Z - P.Z) - (Q.Z - P.Z) * (R.X - P.X); };
				if ((Side(A, B, C) > 0.0) == (Side(A, B, D) > 0.0) || (Side(C, D, A) > 0.0) == (Side(C, D, B) > 0.0)) continue;
				const double Cross = Road[K].Along;
				for (int Way : {1, -1})
				{
					const size_t From = PointAt(std::clamp(Cross - Way * 3000.0, 0.0, Length)), Into = PointAt(std::clamp(Cross + Way * 3000.0, 0.0, Length));
					const std::string Was = PlaceAt(Road[From].At.X, Road[From].At.Z), Now = PlaceAt(Road[Into].At.X, Road[Into].At.Z);
					if (Was.empty() || Now.empty() || Was == Now) continue;
					const size_t Ca = Was.rfind(", "), Cb = Now.rfind(", ");
					const std::string CountryWas = Ca == std::string::npos ? Was : Was.substr(Ca + 2), CountryNow = Cb == std::string::npos ? Now : Now.substr(Cb + 2);
					const std::string County = Cb == std::string::npos ? std::string() : Now.substr(0, Cb);
					std::string Words;
					if (CountryNow != CountryWas)
						Words = CountryNow == "Wales" ? "Croeso i Gymru\tWelcome to Wales" : "Welcome to " + CountryNow;
					else if (L.Kind == "region_border" && !County.empty())
						Words = County;
					if (Words.empty()) continue;
					std::string Lines;
					for (char Ch : Words) Lines += Ch == '\t' ? '|' : Ch;
					const int Mph = MphAt(Cross);
					const double Cap = Mph <= 30 ? 0.1 : 0.15;
					double W = 0.0, H = 0.0;
					if (!SignPlate::ConfirmBoard(nullptr, nullptr, SignPlate::Frame(), Cap, SignPlate::SchemeLocal, Lines, W, H)) continue;
					for (double Off = 5.0; Off <= 35.0; Off += 10.0)
					{
						const double At = Cross + Way * Off;
						if (At < 5.0 || At > Length - 5.0) break;
						const size_t Kk = PointAt(At);
						Furnish F;
						F.Kind = BoundarySign;
						F.Mapped = false;
						F.Big = Cap;
						F.Text = Lines;
						F.At = Verge(Kk, Way * Keep, 0.7 + W * 0.5);
						F.Face = Dir(Kk) * -double(Way);
						F.Height = Ground(F.At.X, F.At.Z);
						const P2 Across(F.Face.Z, -F.Face.X);
						if (std::isnan(F.Height) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (W * 0.5), 0.5) || !OffAll(F.At - Across * (W * 0.5), 0.5)) continue;
						Out.push_back(F);
						break;
					}
				}
			}
		}
	}
	for (const Brown& B : Browned)
		if (std::none_of(Browned.begin(), Browned.end(), [&B](const Brown& O) { return &O != &B && O.Name == B.Name && O.Way == B.Way && (O.D < B.D || (O.D == B.D && &O < &B)); }))
			Out.push_back(B.F);
	return Out;
}
} // namespace TorqueWorld
