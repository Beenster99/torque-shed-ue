// [OSM stage 6C] The direction signs of the ride's road's junctions (see RidePlan::DirectionSigns).
#include "WorldRide.h"
#include "WorldRideSigns.h"

#include <algorithm>
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
	const auto CapFor = [](int Mph) { return Mph <= 30 ? 0.07 : Mph <= 40 ? 0.1 : Mph <= 50 ? 0.125 : Mph <= 60 ? 0.15 : 0.2; };
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
		if (Sides.empty() || !Worth || !Lesser || Slips || (Ij < BuiltUp.size() && BuiltUp[Ij] && !Major)) continue;
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
			if (Top.first > Dist[size_t(N)] || Top.first > 60000.0) continue;
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
		const int Scheme = Rank == 0 ? SignPlate::SchemeMotorway : Rank >= 1 && Rank <= 2 ? SignPlate::SchemePrimary : SignPlate::SchemeLocal;
		for (int Way : {1, -1})
		{
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
			const double Cap = CapFor(Mph);
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
			const double CapOn = CapFor(MphOn);
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
	return Out;
}
} // namespace TorqueWorld
