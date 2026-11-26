#include "WorldRide.h"
#include "WorldFerries.h"
#include "WorldRideChurches.h"
#include "WorldRideRoadX.h"
#include "WorldRideSceneBits.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <cmath>
#include <ctime>
#include <functional>
#include <queue>
#include <set>

namespace TorqueWorld
{
namespace RideNavBits   // [junction priority 168] (WorldRideNav.cpp's)
{
P2 HeadingOut(const Region& G, int Edge, int Node);
double TurnOf(const P2& In, const P2& Out);
} // namespace RideNavBits
using TorqueFit::Json;

// [ride anywhere] The network's state, shared by every thread that lays or reads it.
struct RidePlan::Network
{
	std::vector<char> OnRide;                                     // the edges the ride's own road is made of
	std::unordered_set<int> RideNodes, StepNodes;                 // nodes on the ride's road; nodes one side edge off it
	std::unordered_map<long long, std::vector<int>> BlockEdges;   // the edges through each block (set with the plan)
	std::unique_ptr<std::atomic<const SideRoad*>[]> Side;         // by edge, once laid (null: not, or none)
	std::mutex Lay;                                               // EdgeState, BlockState, Own
	std::condition_variable Done;
	std::vector<char> EdgeState;                                  // 0 not laid, 1 being laid, 2 laid
	std::unordered_map<long long, char> BlockState;
	std::vector<std::unique_ptr<SideRoad>> Own;
	std::shared_mutex Read;                                       // Near, Knots
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Near;   // (road, point) by 16 m square
	std::unordered_map<long long, std::shared_ptr<const Knot>> Knots;      // by road * 2 + end, each arm's
	std::atomic<int> Blocks{0}, Bridges{0}, Tunnels{0};
	std::mutex Fetching;                                          // one fetch of ground at a time
	std::mutex Furnishing;                                        // Furnished
	std::map<std::pair<int, long long>, std::shared_ptr<const std::vector<RidePlan::Furnish>>> Furnished;
	std::mutex Shaping;                                           // Shapes
	std::unordered_map<int, std::shared_ptr<const RidePlan::WholeShape>> Shapes;   // [smooth junctions stage 3] by whole junction   // [ride anywhere 4] by (0 road | 1 block | 2 ring, its key)
};

namespace
{
constexpr double NearCell = 16.0;

long long NearKey(double X, double Z)
{
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	return Cx * 4000037LL + Cz;
}

// [ride anywhere] The network's blocks: by column and row of RidePlan::BlockSize.
long long BlockOf(double V)
{
	return static_cast<long long>(std::floor(V / RidePlan::BlockSize));
}

long long BlockKey(long long Bx, long long Bz)
{
	return Bx * 4000037LL + Bz;
}

Rect BlockBox(long long Key)
{
	// (the inverse of BlockKey: Bz in -2000018..2000018)
	long long Bz = Key % 4000037LL;
	if (Bz > 2000018LL) Bz -= 4000037LL;
	else if (Bz < -2000018LL) Bz += 4000037LL;
	const long long Bx = (Key - Bz) / 4000037LL;
	Rect R;
	R.X0 = double(Bx) * RidePlan::BlockSize;
	R.Z0 = double(Bz) * RidePlan::BlockSize;
	R.X1 = R.X0 + RidePlan::BlockSize;
	R.Z1 = R.Z0 + RidePlan::BlockSize;
	return R;
}

double Dot(const P2& A, const P2& B)
{
	return A.X * B.X + A.Z * B.Z;
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
	RouteRequest Two;
	Two.Pins.resize(2);
	Two.Pins[0].Lat = InLatA; Two.Pins[0].Lon = InLonA;
	Two.Pins[1].Lat = InLatB; Two.Pins[1].Lon = InLonB;
	Two.Mode = "main";
	return Make(Store, Heights, Two, Say);
}

std::vector<CellId> RidePlan::CellsFor(const RouteRequest& Want, bool Wide)
{
	// Along the straight lines from pin to pin: those within RoamPad (about 11 km) of them, or (Wide, when the
	// roads wander further) every cell next to one they cross, as AdventureJourney.cells_for does.
	std::set<CellId> Seen;
	std::vector<CellId> Out;
	const auto Add = [&](double Lat, double Lon)
	{
		const double Pad = Wide ? CellDeg : RoamPad;
		for (const CellId& C : CellsInBox(Lat - Pad, Lon - Pad, Lat + Pad, Lon + Pad))
			if (Seen.insert(C).second) Out.push_back(C);
	};
	for (size_t I = 0; I < Want.Pins.size(); ++I)
	{
		const RoutePin& B = Want.Pins[I];
		if (I == 0) { Add(B.Lat, B.Lon); continue; }
		const RoutePin& A = Want.Pins[I - 1];
		const int Steps = std::max(1, int(std::ceil(std::hypot(B.Lat - A.Lat, B.Lon - A.Lon) / 0.02)));
		for (int K = 0; K <= Steps; ++K) Add(A.Lat + (B.Lat - A.Lat) * K / Steps, A.Lon + (B.Lon - A.Lon) * K / Steps);
	}
	return Out;
}

std::shared_ptr<RidePlan> RidePlan::Make(CellStore& Store, HeightStore& Heights, const RouteRequest& Want, const std::function<void(const std::string&)>& Say)
{
	std::shared_ptr<RidePlan> R = std::make_shared<RidePlan>();
	R->Request = Want;
	{
		// (the month now, from the clock's days since 1970: Hinnant's civil-from-days)
		const long long Days = static_cast<long long>(std::time(nullptr)) / 86400LL + 719468LL;
		const long long Era = (Days >= 0 ? Days : Days - 146096LL) / 146097LL;
		const long long Doe = Days - Era * 146097LL;
		const long long Yoe = (Doe - Doe / 1460 + Doe / 36524 - Doe / 146096) / 365;
		const long long Doy = Doe - (365 * Yoe + Yoe / 4 - Yoe / 100);
		const long long Mp = (5 * Doy + 2) / 153;
		R->Month = int(Mp < 10 ? Mp + 3 : Mp - 9);
		if (SeasonMonth >= 1 && SeasonMonth <= 12) R->Month = SeasonMonth;
	}
	if (!Route::IsStyle(R->Request.Mode)) R->Request.Mode = "main";
	if (Want.Pins.size() >= 2)
	{
		R->LatA = Want.Pins.front().Lat; R->LonA = Want.Pins.front().Lon;
		R->LatB = Want.Pins.back().Lat; R->LonB = Want.Pins.back().Lon;
	}
	const auto Tell = [&Say](const std::string& Text) { if (Say) Say(Text); };
	if (Want.Pins.size() < 2) { R->Why = "Pin a start and somewhere to get to."; return R; }
	if (Want.Pins.size() > size_t(Route::MaxPinsAnyway)) { R->Why = "That's more pins than a ride can have."; return R; }
	Tell("Reading the world's index");
	const std::shared_ptr<const WorldIndex> Idx = Store.Index();
	if (!Idx || Idx->Empty()) { R->Why = "The world's index can't be had."; return R; }
	// The roads near the pins' lines, and the way through the pins: the cheapest roads for the style,
	// leg by leg. Where a leg finds no way in the cells close by, again with a wider band of them.
	std::vector<RideStep> Steps;
	for (int Attempt = 0; Attempt < 2; ++Attempt)
	{
		std::vector<CellId> Cells;
		for (const CellId& C : CellsFor(Want, Attempt == 1))
		{
			const CellStore::EState St = Store.StateOf(C, *Idx);
			if (St == CellStore::EState::Here || St == CellStore::EState::Baked) Cells.push_back(C);
		}
		if (Cells.empty()) { R->Why = "Nobody has baked that part of the world yet."; return R; }
		if (Cells.size() > size_t(MaxCells)) { R->Why = "That journey is too long for one ride: pin somewhere nearer."; return R; }
		Tell("Fetching " + std::to_string(Cells.size()) + " cells of roads");
		R->Area = LoadedArea();
		if (!LoadCells(Store, Heights, Cells, R->Area, &R->Why)) return R;
		const Region& G0 = R->Area.Roads;
		const Frame F0 = G0.FrameOf();
		R->PinNodes.clear();
		bool Snapped = true;
		for (size_t I = 0; I < Want.Pins.size(); ++I)
		{
			const P2 At = F0.FromGeo(Want.Pins[I].Lat, Want.Pins[I].Lon);
			int Node = G0.NearestConnected(At.X, At.Z, SnapM);
			// [ferries stage 5] (a pin on a small island, Luing or Cumbrae, on its own roads when they are near it and a
			// listed ferry goes there: not the mainland's bigger network across the water)
			if (const int Near = G0.NearestNode(At.X, At.Z, 500.0); Node >= 0 && Near >= 0 && Near != Node && G0.Component.size() == G0.Nodes.size()
				&& G0.Component[size_t(Near)] != G0.Component[size_t(Node)] && G0.ComponentSize[size_t(G0.Component[size_t(Near)])] >= 12
				&& G0.Nodes[size_t(Near)].DistanceTo(At) * 2.0 + 200.0 < G0.Nodes[size_t(Node)].DistanceTo(At) && Ferries::ServedPiece(G0, Near))
				Node = Near;
			if (Node < 0)
			{
				Snapped = false;
				R->Why = I == 0 ? "There is no road within 2 km of the start." : I + 1 == Want.Pins.size() ? "There is no road within 2 km of the end."
					: "There is no road within 2 km of pin " + std::to_string(I + 1) + ".";
				break;
			}
			R->PinNodes.push_back(Node);
		}
		if (!Snapped) { if (Attempt == 1) return R; continue; }
		Tell("Finding the way by road");
		const std::vector<double> Costs = Route::EdgeCosts(G0, R->Request.Mode);
		Steps.clear();
		R->Ferry.reset();
		bool Through = true;
		for (size_t I = 0; I + 1 < R->PinNodes.size() && Through; ++I)
		{
			if (R->PinNodes[I] == R->PinNodes[I + 1]) continue;
			const std::vector<int> Leg = G0.CheapestPath(R->PinNodes[I], R->PinNodes[I + 1], &Costs);
			// [ferries stage 5] (a listed ferry where it beats the roads, or where they have no way over: the ride's
			// road ends at its slipway, and the sat nav takes them on from where it lands)
			if (const std::shared_ptr<const std::vector<Ferries::Listed>> FerryList = Ferries::ListNow(); FerryList && !FerryList->empty())
			{
				const Ferries::Link L = Ferries::BestLink(G0, Costs, R->PinNodes[I], R->PinNodes[I + 1], *FerryList, Leg);
				if (L.Row >= 0)
				{
					int Node = R->PinNodes[I];
					for (int E : L.ToBoard)
					{
						const int Next = G0.OtherEnd(E, Node);
						Steps.push_back({Node, E, Next});
						Node = Next;
					}
					std::shared_ptr<FerryLink> Took = std::make_shared<FerryLink>();
					const Ferries::Listed& Row = (*FerryList)[size_t(L.Row)];
					Took->Id = Row.Id;
					Took->Name = Row.Name;
					Took->BoardEnd = L.BoardEnd;
					Took->Board = G0.Nodes[size_t(L.BoardNode)];
					Took->Land = G0.Nodes[size_t(L.LandNode)];
					int On = L.LandNode;
					for (size_t K = I + 1; K < R->PinNodes.size(); ++K)
					{
						Took->Pins.push_back(G0.Nodes[size_t(R->PinNodes[K])]);   // (the pin's junction, as the planner snapped it)
						Took->PinNames.push_back(Want.Pins[K].Name);
						for (int E : G0.CheapestPath(On, R->PinNodes[K], &Costs)) Took->OnM += G0.Edges[size_t(E)].Length;
						On = R->PinNodes[K];
					}
					R->Ferry = Took;
					// (pinned at the slipway itself: the ride's road is the last road in to it)
					if (Steps.empty())
						for (int E : G0.Adjacent[size_t(L.BoardNode)])
							if (G0.Edges[size_t(E)].A != G0.Edges[size_t(E)].B && G0.Passable(E, G0.OtherEnd(E, L.BoardNode)))
							{
								Steps.push_back({G0.OtherEnd(E, L.BoardNode), E, L.BoardNode});
								break;
							}
					break;
				}
			}
			if (Leg.empty()) { Through = false; break; }
			int Node = R->PinNodes[I];
			for (int E : Leg)
			{
				const int Next = G0.OtherEnd(E, Node);
				Steps.push_back({Node, E, Next});
				Node = Next;
			}
		}
		if (Through) break;
		R->Why = "There is no way by road between those places.";
		if (Attempt == 1) return R;
	}
	const Region& G = R->Area.Roads;
	// (short out-and-backs to a pin on a dead end left out; then on past the last pin, so the ride has
	// somewhere to run out to)
	Steps = StripSpurs(G, Steps);
	if (Steps.empty()) { R->Why = "Those pins are the same place: pin somewhere else to get to."; return R; }
	double PinsEnd = 0.0;
	for (const RideStep& St : Steps) PinsEnd += G.Edges[size_t(St.Edge)].Length;
	if (!R->Ferry)   // ([ferries stage 5] none past a ferry's slipway: the ride's road ends there)
	{
		const std::vector<double> Costs = Route::EdgeCosts(G, R->Request.Mode);
		const std::vector<RideStep> Tail = RunOut(G, Steps, Costs);
		Steps.insert(Steps.end(), Tail.begin(), Tail.end());
	}
	R->RunOutFrom = PinsEnd;
	R->MadeSteps = Steps;
	return LayOut(R, Store, Heights, Tell, nullptr);
}

std::shared_ptr<RidePlan> RidePlan::LayOut(const std::shared_ptr<RidePlan>& R, CellStore& Store, HeightStore& Heights,
	const std::function<void(const std::string&)>& Said, const P2* StartAt)
{
	const auto Tell = [&Said](const std::string& Text) { if (Said) Said(Text); };
	// [OSM stage 5B] The roundabouts found and the round ones made true circles, before any road is laid.
	R->IndexRings();
	if (R->RoundRings() > 0) R->IndexRings();
	R->IndexJunctions();   // [smooth junctions stage 2] (the map's points tidied into whole junctions)
	const Region& G = R->Area.Roads;
	const std::vector<RideStep>& Steps = R->MadeSteps;
	const int From = Steps.front().From;
	std::vector<int> Path;
	for (const RideStep& St : Steps) Path.push_back(St.Edge);
	for (int E : Path)
	{
		const std::string Name = G.NameOf(E);
		if (R->Names.empty() || R->Names.back() != Name) R->Names.push_back(Name);
	}
	// The road as the map drew it, and which road each of its points is on.
	std::vector<P2> Raw;
	std::vector<int> Owner;
	std::vector<char> OwnerBack;   // [OSM stage 5D] (ridden from its node B to A)
	{
		int Node = From;
		for (int E : Path)
		{
			const std::vector<P2> Piece = G.LineFrom(E, Node);
			const char Back = G.Edges[size_t(E)].A != Node ? 1 : 0;
			for (size_t I = Raw.empty() ? 0 : 1; I < Piece.size(); ++I)
			{
				Raw.push_back(Piece[I]);
				Owner.push_back(E);
				OwnerBack.push_back(Back);
			}
			Node = G.OtherEnd(E, Node);
		}
	}
	std::vector<double> Fair(Raw.size()), RawAlong(Raw.size(), 0.0);
	for (size_t I = 0; I < Raw.size(); ++I)
	{
		Fair[I] = RoadLine::FairOf(G.ClassOf(Owner[I]));
		// [OSM stage 5B] (round a roundabout drawn as a circle: kept to it, not faired across the island)
		if (R->OnRoundRing(Owner[I])) Fair[I] = 0.0;
		if (I > 0) RawAlong[I] = RawAlong[I - 1] + Raw[I].DistanceTo(Raw[I - 1]);
	}
	// [junction priority] (who gives way at each junction; where the ride turns at one to or from a road
	// that gives way, a junction's corner, hardly faired, not a sweeping bend)
	R->FindPriorities(From, Path);
	// [junction priority, square T] (beenster, Nov 21 23:30: "The t junction is called a T junction because it
	// looks like a T. The main road is straight, a road which joins it does not always have curvature... the
	// rider would stop at the junction and then take a much sharper turn".) Where the ride turns to or from a
	// road that gives way, each road's last bit into the junction laid straight to the map's junction (the
	// road's own line there, square on, not its bend into the other road), so the main road's kerbs run straight
	// past it and the side road meets it as a T; the turn itself tight, on the main road's tarmac.
	struct SquareTurn
	{
		P2 Hub, In, Out;   // the junction, and each road's line into it this far out
		double Radius = 6.0;
		// [crunched junctions, 169] (the map's line on out past In and Out, nearest first, up to 40 m: where the
		// smoothed line has rejoined it, the turn is spliced in)
		std::vector<P2> Back, On;
		double RawAt = 0.0;   // (how far along the map's line of the ride it is)
	};
	std::vector<SquareTurn> Squares;
	{
		std::vector<double> Nodes;   // (the ride's other junctions: no straightening past one)
		{
			int Node = From;
			double At = 0.0;
			for (size_t I = 0; I + 1 < Path.size(); ++I)
			{
				Node = G.OtherEnd(Path[I], Node);
				At += G.Edges[size_t(Path[I])].Length;
				if (G.Adjacent[size_t(Node)].size() >= 3) Nodes.push_back(At);
			}
		}
		const auto PointAt = [&Raw, &RawAlong](double A)
		{
			size_t K = 0;
			while (K + 2 < Raw.size() && RawAlong[K + 1] < A) ++K;
			const double T = std::clamp((A - RawAlong[K]) / std::fmax(1e-9, RawAlong[K + 1] - RawAlong[K]), 0.0, 1.0);
			return Raw[K] + (Raw[K + 1] - Raw[K]) * T;
		};
		for (const JunctionRule& J : R->Junctions)
		{
			if ((!J.CameMinor && !J.OntoMinor) || std::fabs(J.Turn) <= JunctionTurn || Raw.size() < 3) continue;
			size_t Mid = 0;
			for (size_t I = 0; I < Raw.size(); ++I)
				if (std::fabs(RawAlong[I] - J.RawAlong) < std::fabs(RawAlong[Mid] - J.RawAlong)) Mid = I;
			if (std::fabs(RawAlong[Mid] - J.RawAlong) > 2.0) continue;
			// [square T] (the through road first: one straight line through the junction, fitted to the map's
			// line of both its arms within 40 m; the ride's road along it on its side, the junction on it)
			const int Ours = J.CameMinor ? J.Onto : J.Came;
			const int ArmX = J.CameMinor && J.OntoMinor ? -1 : (J.ThroughA == Ours ? J.ThroughB : J.ThroughA);
			if (ArmX >= 0)
			{
				const int MainWay = J.CameMinor ? 1 : -1;
				const P2 Node = Raw[Mid];
				P2 ArmSum(0.0, 0.0), LegSum(0.0, 0.0);
				int ArmN = 0, LegN = 0;
				std::vector<P2> Fit;
				{
					const std::vector<P2> XL = G.LineFrom(ArmX, J.Node);
					double Gone = 0.0;
					for (size_t K = 1; K < XL.size() && Gone < 40.0; ++K)
					{
						const double Seg = XL[K].DistanceTo(XL[K - 1]);
						// (the line every 2 m, so a long straight piece counts as much as a bendy one)
						for (double S = 2.0; S <= Seg && Gone + S <= 40.0; S += 2.0)
						{
							const P2 P = XL[K - 1] + (XL[K] - XL[K - 1]) * (S / Seg);
							if (Gone + S < 3.0) continue;
							ArmSum = ArmSum + (P - Node); ++ArmN; Fit.push_back(P);
						}
						Gone += Seg;
					}
				}
				for (size_t I = 0; I < Raw.size(); ++I)
				{
					const double D = (RawAlong[I] - RawAlong[Mid]) * MainWay;
					if (D < 3.0 || D > 40.0) continue;
					LegSum = LegSum + (Raw[I] - Node); ++LegN; Fit.push_back(Raw[I]);
				}
				const double La = ArmSum.Length(), Lb = LegSum.Length();
				if (ArmN > 0 && LegN > 0 && La > 1e-6 && Lb > 1e-6)
				{
					const P2 LegDir = LegSum * (1.0 / Lb), ArmDir = ArmSum * (1.0 / La);
					P2 Dir = LegDir - ArmDir;
					const double Ld = Dir.Length();
					if (Ld > 1.0)
					{
						// (where the map's through road runs straight on, within 12 degrees, one straight line
						// fitted to both arms; where it bends at the junction, as Buxworth's New Road into Station
						// Road, each arm its own straight line from the junction, as the map draws it)
						const bool Straight = Ld > 2.0 * std::cos(6.0 * 3.14159265358979 / 180.0);
						Dir = Straight ? Dir * (1.0 / Ld) : LegDir;   // (along the through road, towards the ride's side of it)
						const P2 Across(Dir.Z, -Dir.X);
						double Off = 0.0;
						for (const P2& P : Fit) Off += (P.X - Node.X) * Across.X + (P.Z - Node.Z) * Across.Z;
						Off = Straight ? Off / double(Fit.size()) : 0.0;
						const P2 Hub2 = Node + Across * Off;
						// (the ride's road on its side onto that line: all of it within 30 m, eased back to the
						// map's over the next 15)
						for (size_t I = 0; I < Raw.size(); ++I)
						{
							const double D = (RawAlong[I] - RawAlong[Mid]) * MainWay;
							if (D <= 0.0 || D >= 45.0) continue;
							const double S = (Raw[I].X - Hub2.X) * Dir.X + (Raw[I].Z - Hub2.Z) * Dir.Z;
							const P2 On = Hub2 + Dir * std::fmax(0.0, S);
							const double W = std::clamp((45.0 - D) / 15.0, 0.0, 1.0);
							Raw[I] = Raw[I] + (On - Raw[I]) * W;
						}
						Raw[Mid] = Hub2;
						ThroughStraight Ts;
						Ts.Hub = Hub2;
						Ts.Dir = Straight ? Dir * -1.0 : ArmDir;
						R->ThroughLines[ArmX] = Ts;
					}
				}
			}
			SquareTurn Sq;
			Sq.Hub = Raw[Mid];
			Sq.RawAt = RawAlong[Mid];
			Sq.Radius = std::clamp(J.MajorHalf + 2.5, 5.0, 9.0);
			bool Both = true;
			for (const int Way : {-1, 1})
			{
				double Span = JunctionSquare;
				for (const double N : Nodes)
					if (std::fabs(N - J.RawAlong) > 1.0 && (N - J.RawAlong) * Way > 0.0) Span = std::fmin(Span, std::fabs(N - J.RawAlong) * 0.5);
				const double EndA = RawAlong[Mid] + Way * Span;
				if (EndA <= 0.0 || EndA >= RawAlong.back()) { Both = false; continue; }
				const P2 Hub = Raw[Mid], Far = PointAt(EndA);
				(Way < 0 ? Sq.In : Sq.Out) = Far;
				std::vector<P2>& Beyond = Way < 0 ? Sq.Back : Sq.On;
				for (double D = Span + 1.0; D <= Span + 40.0; D += 1.0)
				{
					const double At = RawAlong[Mid] + Way * D;
					if (At <= 0.0 || At >= RawAlong.back()) break;
					Beyond.push_back(PointAt(At));
				}
				for (size_t I = 0; I < Raw.size(); ++I)
				{
					const double D = (RawAlong[I] - RawAlong[Mid]) * Way;
					if (D > 0.0 && D < Span) Raw[I] = Hub + (Far - Hub) * (D / Span);
				}
			}
			if (Both) Squares.push_back(Sq);
		}
	}
	for (const JunctionRule& J : R->Junctions)
		if ((J.CameMinor || J.OntoMinor) && std::fabs(J.Turn) > JunctionTurn)
			for (size_t I = 0; I < Raw.size(); ++I)
				if (std::fabs(RawAlong[I] - J.RawAlong) < JunctionCorner) Fair[I] = std::fmin(Fair[I], JunctionFair);
	// [junction priority 168] (beenster, 167: "The route that we plot should map exactly to the road". Where the
	// ride turns from one road to another at a junction of the map (three roads or more, not a roundabout),
	// a junction's corner: hardly faired and as tight as JunctionRadius, not the 10 m least of a bend. Whaley
	// Bridge's Reservoir Road corners had the road 9 m off the map, through the pub.)
	std::vector<double> Tight(Raw.size(), 0.0);
	{
		int Node = From;
		double At = 0.0;
		for (size_t I = 0; I + 1 < Path.size(); ++I)
		{
			Node = G.OtherEnd(Path[I], Node);
			At += G.Edges[size_t(Path[I])].Length;
			if (G.Adjacent[size_t(Node)].size() < 3 || R->RingAt(Node) >= 0 || R->OnRoundRing(Path[I]) || R->OnRoundRing(Path[I + 1])) continue;
			const P2 Back = RideNavBits::HeadingOut(G, Path[I], Node);
			if (std::fabs(RideNavBits::TurnOf(P2(-Back.X, -Back.Z), RideNavBits::HeadingOut(G, Path[I + 1], Node))) <= JunctionTurn) continue;
			for (size_t K = 0; K < Raw.size(); ++K)
				if (std::fabs(RawAlong[K] - At) < JunctionCorner)
				{
					Fair[K] = std::fmin(Fair[K], JunctionFair);
					Tight[K] = RoadLine::JunctionRadius;
				}
		}
	}
	Tell("Smoothing the road");
	std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, Fair, Tight);
	// [square T] (the turn at each such junction made exactly: straight in along the one road, round a
	// circle of Radius, straight out along the other, a knot a metre round it; so the side road's inner corner
	// is a tight kerb's, the main road's edges straight up to it, and the ride's line a sharp turn)
	for (const SquareTurn& Sq : Squares)
	{
		const P2 InD = Sq.Hub - Sq.In, OutD = Sq.Out - Sq.Hub;
		const double Li = InD.Length(), Lo = OutD.Length();
		if (Li < 4.0 || Lo < 4.0) continue;
		const P2 U0 = InD * (1.0 / Li), U1 = OutD * (1.0 / Lo);
		const double Cos = std::clamp(U0.X * U1.X + U0.Z * U1.Z, -1.0, 1.0);
		const double Theta = std::acos(Cos);
		// ([crunched junctions, 169] up to a hairpin of 160 degrees: past 150, as the A592 onto The Struggle at
		// Kirkstone Pass, the turn was left to the smoothing, which swung the ride's road round in a loop 20 m
		// inside the junction and left the other arm's end standing alone with a hole of grass between)
		if (Theta < 0.3 || Theta > 2.8) continue;
		double Radius = Sq.Radius;
		double Cut = Radius * std::tan(Theta * 0.5);
		const double Most = std::fmin(Li, Lo) - 2.0;
		if (Cut > Most) { Cut = Most; Radius = Cut / std::tan(Theta * 0.5); }
		const P2 T0 = Sq.Hub - U0 * Cut, T1 = Sq.Hub + U1 * Cut;
		// (the knots nearest where the straight lines begin and end: In and Out; [crunched junctions, 169] or,
		// where the smoothing pulled the line away there (a sharp turn opened into a loop), where it is back on
		// the map's line further out, the map's line followed in from there)
		// (only the knots within 100 m of the junction along the road: a ride up a dead end and back passes the
		// same places twice)
		std::vector<double> LineAt(Line.size(), 0.0);
		for (size_t I = 1; I < Line.size(); ++I) LineAt[I] = LineAt[I - 1] + Line[I].DistanceTo(Line[I - 1]);
		const double ToRaw = !LineAt.empty() && LineAt.back() > 0.0 && !RawAlong.empty() ? RawAlong.back() / LineAt.back() : 1.0;
		const auto Rejoin = [&Line, &LineAt, ToRaw, &Sq](const P2& Start, const std::vector<P2>& Further, size_t& Index, std::vector<P2>& Lead)
		{
			std::vector<P2> Chain = {Start};
			Chain.insert(Chain.end(), Further.begin(), Further.end());
			for (size_t C = 0; C < Chain.size(); ++C)
			{
				size_t Best = Line.size();
				for (size_t I = 0; I < Line.size(); ++I)
					if (std::fabs(LineAt[I] * ToRaw - Sq.RawAt) < 100.0 && (Best == Line.size() || Line[I].DistanceTo(Chain[C]) < Line[Best].DistanceTo(Chain[C]))) Best = I;
				if (Best == Line.size() || Line[Best].DistanceTo(Chain[C]) > (C == 0 ? 4.0 : 1.0)) continue;
				Index = Best;
				Lead.assign(Chain.begin(), Chain.begin() + static_cast<long>(C));   // (nearest the hub first)
				return true;
			}
			return false;
		};
		size_t A = 0, B = 0;
		std::vector<P2> LeadIn, LeadOut;
		if (!Rejoin(Sq.In, Sq.Back, A, LeadIn) || !Rejoin(Sq.Out, Sq.On, B, LeadOut) || B <= A + 1) continue;
		std::vector<P2> Mid;
		const auto Straight = [&Mid](const P2& P0, const P2& P1, double Each)
		{
			const double L = P0.DistanceTo(P1);
			const int N = std::max(1, int(std::ceil(L / Each)));
			for (int K = 1; K < N; ++K) Mid.push_back(P0 + (P1 - P0) * (double(K) / N));
		};
		{
			P2 Walk = Line[A];
			for (size_t K = LeadIn.size(); K-- > 0;) { Straight(Walk, LeadIn[K], RoadLine::Step); Mid.push_back(LeadIn[K]); Walk = LeadIn[K]; }
			if (!LeadIn.empty()) { Straight(Walk, Sq.In, RoadLine::Step); Mid.push_back(Sq.In); Walk = Sq.In; }
			Straight(Walk, T0, RoadLine::Step);
		}
		Mid.push_back(T0);
		// (the circle's middle, Radius to the inside of the turn)
		const P2 Inside = P2(U0.Z, -U0.X) * (U1.X * U0.Z - U1.Z * U0.X >= 0.0 ? 1.0 : -1.0);
		const P2 Centre = T0 + Inside * Radius;
		const double Arc = Radius * Theta;
		const int Bits = std::max(2, int(std::ceil(Arc / 1.0)));
		const double Ang0 = std::atan2(T0.Z - Centre.Z, T0.X - Centre.X);
		double Sweep = std::atan2(T1.Z - Centre.Z, T1.X - Centre.X) - Ang0;
		while (Sweep > 3.14159265358979) Sweep -= 6.28318530717959;
		while (Sweep < -3.14159265358979) Sweep += 6.28318530717959;
		for (int K = 1; K < Bits; ++K)
		{
			const double Ang = Ang0 + Sweep * K / Bits;
			Mid.push_back(Centre + P2(std::cos(Ang), std::sin(Ang)) * Radius);
		}
		Mid.push_back(T1);
		{
			P2 Walk = T1;
			if (!LeadOut.empty()) { Straight(Walk, Sq.Out, RoadLine::Step); Mid.push_back(Sq.Out); Walk = Sq.Out; }
			for (const P2& Pt : LeadOut) { Straight(Walk, Pt, RoadLine::Step); Mid.push_back(Pt); Walk = Pt; }
			Straight(Walk, Line[B], RoadLine::Step);
		}
		std::vector<P2> Next(Line.begin(), Line.begin() + static_cast<long>(A) + 1);
		Next.insert(Next.end(), Mid.begin(), Mid.end());
		Next.insert(Next.end(), Line.begin() + static_cast<long>(B), Line.end());
		Line.swap(Next);
	}
	// Each knot as wide as the road it is on (by how far along it is), eased where the width changes.
	std::vector<double> Halves(Line.size(), 2.3), Speeds(Line.size(), 60.0);
	std::vector<int> KnotLanes(Line.size(), 0);
	std::vector<char> KnotOneWay(Line.size(), 0);
	std::vector<double> KnotShoulder(Line.size(), 0.0);   // [OSM stage 5C]
	{
		std::vector<double> KnotAlong(Line.size(), 0.0);
		for (size_t I = 1; I < Line.size(); ++I) KnotAlong[I] = KnotAlong[I - 1] + Line[I].DistanceTo(Line[I - 1]);
		const double Scale = !Line.empty() && KnotAlong.back() > 0.0 && !Raw.empty() ? RawAlong.back() / KnotAlong.back() : 1.0;
		size_t J = 0;
		for (size_t I = 0; I < Line.size(); ++I)
		{
			const double At = KnotAlong[I] * Scale;
			while (J + 1 < Raw.size() && RawAlong[J + 1] < At) ++J;
			// ([OSM stage 5] as wide as the map records, or its lanes make it; else as its class)
			const int OwnerEdge = Owner[std::min(J + 1, Raw.size() - 1)];
			const RoadX::Across Ax = RoadX::Of(G, OwnerEdge, RoadLine::WidthOf(G.ClassOf(OwnerEdge)));
			Halves[I] = R->RingHalfOf(OwnerEdge, Ax.Half);   // [roundabouts] (round a ring, the ring's one width)
			KnotLanes[I] = Ax.Lanes;
			// ([5E] 4 embankment, 8 cutting; [5D] the map's parking as the ride runs, left 16-32, right 64-128)
			const bool Back = OwnerBack[std::min(J + 1, Raw.size() - 1)] != 0;
			const int ParkL = Back ? Ax.ParkRight : Ax.ParkLeft, ParkR = Back ? Ax.ParkLeft : Ax.ParkRight;
			KnotOneWay[I] = static_cast<char>(static_cast<unsigned char>((Ax.OneWay ? 1 : 0) | (Ax.Dual ? 2 : 0) | (Ax.Banked << 2) | (ParkL << 4) | (ParkR << 6)));
			KnotShoulder[I] = Ax.Shoulder;
			Speeds[I] = RoadLine::DesignSpeedOf(G.ClassOf(Owner[std::min(J + 1, Raw.size() - 1)]));
			// ([square T] a junction lies flat: no banking round the turn into or out of a side road)
			for (const SquareTurn& Sq : Squares)
				if (Line[I].DistanceTo(Sq.Hub) < 25.0) Speeds[I] = 0.0;
		}
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			std::vector<double> Eased = Halves, EasedShoulder = KnotShoulder;
			for (size_t I = 0; I < Halves.size(); ++I)
			{
				double Sum = 0.0, SumShoulder = 0.0;
				int Count = 0;
				for (size_t K = I > 6 ? I - 6 : 0; K <= std::min(I + 6, Halves.size() - 1); ++K) { Sum += Halves[K]; SumShoulder += KnotShoulder[K]; ++Count; }
				Eased[I] = Sum / Count;
				EasedShoulder[I] = SumShoulder / Count;
			}
			Halves.swap(Eased);
			KnotShoulder.swap(EasedShoulder);
		}
	}
	// (the tiles are chosen by the road as the map has it, a point every 30 m, as every rider's are)
	std::vector<P2> TilesFor = Resample(G.PathLine(From, Path), 30.0);
	// [build 168 feedback] (beenster at Uig: "i dont see the ferry here": a long crossing's far side, 47 km off, had
	// no ground, so the crossing never laid) The ground where a ferry lands and along the way on from it too.
	if (R->Ferry)
	{
		TilesFor.push_back(R->Ferry->Land);
		int On = G.NearestNode(R->Ferry->Land.X, R->Ferry->Land.Z, 30.0);
		for (const P2& Pin : R->Ferry->Pins)
		{
			const int To = G.NearestNode(Pin.X, Pin.Z, 30.0);
			if (On < 0 || To < 0) break;
			if (To != On)
			{
				const std::vector<int> Onward = G.CheapestPath(On, To, nullptr);
				const std::vector<P2> More = Resample(G.PathLine(On, Onward), 30.0);
				TilesFor.insert(TilesFor.end(), More.begin(), More.end());
			}
			TilesFor.push_back(Pin);
			On = To;
		}
	}
	R->Land = R->Area.Land->ForRoad(TilesFor);
	R->Land->SetRoam(true);   // [ride anywhere]
	R->CellsFrom = &Store;
	R->HeightsFrom = &Heights;
	R->LidarFrom = LidarReader();   // [LiDAR stage 2]
	Tell("Fetching the ground: " + std::to_string(R->Land->UsedKeys().size()) + " tiles");
	R->TilesFailed = FetchGround(Store, Heights, *R->Land);
	if (R->TilesFailed > 0)
	{
		R->Why = Store.Offline ? "Offline, and the ground for this road isn't on this machine: ride a road ridden before, or go online (Settings > Gameplay)."
							   : std::to_string(R->TilesFailed) + " of the ground's tiles couldn't be fetched.";
		return R;
	}
	Tell("Laying the road");
	// [OSM stage 5C] (along a motorway, laid out on its nearside by half its hard shoulder: the traffic's
	// left as it is ridden in Britain)
	std::vector<P2> Laid = Line;
	ShiftLine(Laid, KnotShoulder, R->KeepLeft() ? 0.5 : -0.5);
	R->LayRoad(Laid, Halves, Speeds, &KnotLanes, &KnotOneWay, &KnotShoulder);
	// (where the last pin is on the road as laid: the run-out is past it)
	R->FinishM = !RawAlong.empty() && RawAlong.back() > 1.0 ? std::fmin(R->Length, R->RunOutFrom * R->Length / RawAlong.back()) : R->Length;
	if (R->Road.size() < 2) { R->Why = "The road has no ground under it."; return R; }
	Tell("Laying the roads that meet it");
	// ([156] the facts first too: a side road's mouth asks the class of the road it meets, FactAt)
	R->FindFacts(From, Path, RawAlong.empty() ? 0.0 : RawAlong.back());
	R->PlanWholeJunctions(Path);   // [smooth junctions stage 3] (which junctions are built whole: before any side road is laid)
	R->LaySides(Path);
	R->PlaceJunctions(RawAlong.empty() ? 0.0 : RawAlong.back());   // [junction priority] (after the map's own marks)
	{
		// [ride anywhere] The network round the start laid now, its ground fetched (not on the game thread
		// as the first squares are made).
		const P2 Mid = StartAt != nullptr ? *StartAt : R->Road[0].At;
		Rect Start;
		Start.X0 = Mid.X - 700.0; Start.Z0 = Mid.Z - 700.0; Start.X1 = Mid.X + 700.0; Start.Z1 = Mid.Z + 700.0;
		R->LayBlocks(Start);
	}
	R->IndexTowns();     // [towns stage 1] (before anything asks a place's look)
	R->IndexScenery();
	R->IndexRails();
	R->FindFacts(From, Path, RawAlong.empty() ? 0.0 : RawAlong.back());
	R->FindPathNodes(From, Path, RawAlong.empty() ? 0.0 : RawAlong.back());   // [OSM stage 6C] (the direction signs' junctions)
	R->IndexFurniture();
	R->IndexPlaces();     // [places stage 2] (after the furniture, before the fields: its pull-offs are field areas)
	R->IndexTownLife();   // [roads stage 7] (before the fields too: forecourts are field areas)
	R->IndexCarParks();   // [car parks]
	R->IndexCircuits();   // [circuits]
	R->IndexSkyline();   // [OSM stage 2 batch B] (after the furniture: it strings its poles)
	R->IndexFields();    // [OSM stage 2 batch C]
	R->IndexWater();     // [build 147 feedback] (after the fields: a river's outline gives it its width)
	R->IndexFerries();   // [ferries] (after the water: a slipway goes down to its level)
	R->IndexWild();      // [OSM stage 2 batch D]
	R->IndexTowers();    // [towns stage 2]
	R->IndexPlaceRecipes();   // [places stage 3] (after the towers and the churches' buildings)
	R->FindTurnsAndStops(From, Path, RawAlong.empty() ? 0.0 : RawAlong.back());
	// [ferries stage 5] (a way by a ferry: the ride's road's finish is its slipway, named for it)
	if (R->Ferry && !R->Turns.empty() && R->Turns.back().Turn == Finish)
		R->Turns.back().Onto = R->Ferry->Name.find("Ferry") != std::string::npos || R->Ferry->Name.find("Bridge") != std::string::npos ? R->Ferry->Name : R->Ferry->Name + " Ferry";
	R->FindThinData();
	R->Ok = true;
	return R;
}

std::vector<CellId> RidePlan::CellsToGrow(double X, double Z, double Pad) const
{
	std::vector<CellId> Out;
	if (!Ok) return Out;
	const Geo At = Area.Roads.FrameOf().ToGeo(X, Z);
	for (const CellId& C : CellsInBox(At.Lat - Pad, At.Lon - Pad, At.Lat + Pad, At.Lon + Pad))
		if (std::find(Area.Cells.begin(), Area.Cells.end(), C) == Area.Cells.end()) Out.push_back(C);
	return Out;
}

bool RidePlan::WasRead(double X0, double Z0, double X1, double Z1) const
{
	const auto In = [this](double X, double Z)
	{
		for (const Rect& R : GrewFrom)
			if (X >= R.X0 && X <= R.X1 && Z >= R.Z0 && Z <= R.Z1) return true;
		return false;
	};
	return In(X0, Z0) && In(X1, Z0) && In(X0, Z1) && In(X1, Z1);
}

std::shared_ptr<RidePlan> RidePlan::Grow(CellStore& Store, HeightStore& Heights, const RidePlan& Old, double X, double Z, std::string* Why)
{
	const auto No = [Why](const std::string& Text) { if (Why != nullptr) *Why = Text; return std::shared_ptr<RidePlan>(); };
	if (!Old.Ok || Old.MadeSteps.empty() || Old.CellsFrom == nullptr) return No("no ride to grow");
	const std::shared_ptr<const WorldIndex> Idx = Store.Index();
	if (!Idx || Idx->Empty()) return No("the world's index can't be had");
	// (the old cells first, in their order: Stitch then gives every junction and road the number it had)
	std::vector<CellId> Cells = Old.Area.Cells;
	const size_t Had = Cells.size();
	for (const CellId& C : Old.CellsToGrow(X, Z, RoamPad))
	{
		const CellStore::EState St = Store.StateOf(C, *Idx);
		if (St == CellStore::EState::Here || St == CellStore::EState::Baked) Cells.push_back(C);
	}
	if (Cells.size() == Had) return No("no more of the world baked round here");
	if (Cells.size() > size_t(MaxRoamCells)) return No("the ride has read as many cells as it can hold");
	std::shared_ptr<RidePlan> R = std::make_shared<RidePlan>();
	R->Request = Old.Request;
	R->Month = Old.Month;
	R->LatA = Old.LatA; R->LonA = Old.LonA; R->LatB = Old.LatB; R->LonB = Old.LonB;
	R->PinNodes = Old.PinNodes;
	R->RunOutFrom = Old.RunOutFrom;
	R->Title = Old.Title;
	R->MadeSteps = Old.MadeSteps;
	R->Ferry = Old.Ferry;   // [ferries stage 5]
	R->Grown = Old.Grown + 1;
	R->StoresAlive = Old.StoresAlive;
	R->LidarFrom = Old.LidarFrom;
	if (!LoadArea(Store, Heights, Cells, Old.Area.Roads.FrameLat, Old.Area.Roads.FrameLon, "the roads here", R->Area, &R->Why)) return No(R->Why);
	// (a cell that was read before and can't be now would renumber what follows it)
	if (R->Area.Cells.size() < Had || !std::equal(Old.Area.Cells.begin(), Old.Area.Cells.end(), R->Area.Cells.begin())
		|| R->Area.Roads.Edges.size() < Old.Area.Roads.Edges.size())
		return No("the cells read before couldn't all be read again");
	const P2 At(X, Z);
	LayOut(R, Store, Heights, nullptr, &At);
	if (!R->Ok) return No(R->Why);
	{
		// (the network as far round the rider as the game draws roads, so none drawn is left unlaid)
		Rect Round;
		Round.X0 = X - GrowLayM; Round.Z0 = Z - GrowLayM; Round.X1 = X + GrowLayM; Round.Z1 = Z + GrowLayM;
		R->LayBlocks(Round);
	}
	// The roads whose ends meet new roads where the old area stopped: drawn again.
	const Region& G0 = Old.Area.Roads;
	const Region& G1 = R->Area.Roads;
	for (size_t N = 0; N < G0.Nodes.size(); ++N)
		if (G1.Adjacent[N].size() != G0.Adjacent[N].size())
			for (int E : G1.Adjacent[N])
				if (E < int(G0.Edges.size())) R->RedrawRoads.push_back(1 + E);
	// [circuits] (the circuits' roads come after the cells' own, so they are numbered again as the area grows)
	for (int E = G0.CircuitFirst; E >= 0 && E < int(G0.Edges.size()); ++E) R->RedrawRoads.push_back(1 + E);
	std::sort(R->RedrawRoads.begin(), R->RedrawRoads.end());
	R->RedrawRoads.erase(std::unique(R->RedrawRoads.begin(), R->RedrawRoads.end()), R->RedrawRoads.end());
	const Frame F = G0.FrameOf();
	for (const CellId& C : Old.Area.Cells)
	{
		const Geo Mid = CellMiddle(C);
		const P2 A = F.FromGeo(Mid.Lat + CellDeg * 0.5, Mid.Lon - CellDeg * 0.5), B = F.FromGeo(Mid.Lat - CellDeg * 0.5, Mid.Lon + CellDeg * 0.5);
		Rect Box;
		Box.X0 = A.X; Box.Z0 = A.Z; Box.X1 = B.X; Box.Z1 = B.Z;
		R->GrewFrom.push_back(Box);
	}
	return R;
}

// (a port of route_planner.gd's spans_along)
std::vector<int> RidePlan::SpansAlong(const std::vector<P2>& Line, int Kind) const
{
	std::vector<int> Out;
	const Region& G = Area.Roads;
	if (G.Spans.empty() || Line.size() < 2) return Out;
	const double Cell = 32.0;
	std::unordered_map<long long, std::vector<int>> Grid;
	const auto Key = [Cell](double X, double Z) { return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell)); };
	// (most spans are nowhere near: only those with an end in the line's own box are looked up)
	double MinX = 1e18, MaxX = -1e18, MinZ = 1e18, MaxZ = -1e18;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		Grid[Key(Line[I].X, Line[I].Z)].push_back(int(I));
		MinX = std::fmin(MinX, Line[I].X); MaxX = std::fmax(MaxX, Line[I].X);
		MinZ = std::fmin(MinZ, Line[I].Z); MaxZ = std::fmax(MaxZ, Line[I].Z);
	}
	MinX -= BridgeReach; MaxX += BridgeReach; MinZ -= BridgeReach; MaxZ += BridgeReach;
	const auto NearTo = [&](const P2& P)
	{
		std::vector<int> Found;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Grid.find(Key(P.X + Dx * Cell, P.Z + Dz * Cell));
				if (It == Grid.end()) continue;
				for (int I : It->second)
					if (Line[size_t(I)].DistanceTo(P) <= BridgeReach) Found.push_back(I);
			}
		return Found;
	};
	std::vector<std::pair<int, int>> Found;
	for (const Span& Sp : G.Spans)
	{
		if (Sp.Kind != Kind || Sp.A.X < MinX || Sp.A.X > MaxX || Sp.A.Z < MinZ || Sp.A.Z > MaxZ) continue;
		const std::vector<int> AtA = NearTo(Sp.A);
		if (AtA.empty()) continue;
		const std::vector<int> AtB = NearTo(Sp.B);
		const double Long = Sp.A.DistanceTo(Sp.B);
		std::pair<int, int> Best(-1, -1);
		double BestOff = 1e18;
		for (int Ia : AtA)
			for (int Ib : AtB)
			{
				const double Along = std::abs(Ib - Ia) * RoadLine::Step;
				// (the road passes both ends, but not across the bridge between them)
				if (Along < Long * 0.5 - RoadLine::Step || Along > Long * 1.5 + RoadLine::Step * 2.0) continue;
				const double Off = std::fabs(Along - Long);
				if (Off < BestOff)
				{
					BestOff = Off;
					Best = {std::min(Ia, Ib), std::max(Ia, Ib)};
				}
			}
		if (Best.first >= 0 && Best.second > Best.first) Found.push_back(Best);
	}
	std::sort(Found.begin(), Found.end());
	for (const std::pair<int, int>& F : Found)
	{
		// (overlapping, or end to end: one bridge)
		if (Out.size() >= 2 && F.first <= Out.back() + 1) Out.back() = std::max(Out.back(), F.second);
		else { Out.push_back(F.first); Out.push_back(F.second); }
	}
	return Out;
}

std::vector<RidePlan::RideStep> RidePlan::StripSpurs(const Region& G, const std::vector<RideStep>& Steps)
{
	// (RoutePlanner.strip_spurs: where the way doubles back on itself along the same edges, up to SpurM
	// metres each way, both the out and the back are left out)
	std::vector<RideStep> Out;
	size_t I = 0;
	while (I < Steps.size())
	{
		size_t K = 0;
		double Run = 0.0;
		while (I + K < Steps.size() && Out.size() > K)
		{
			const RideStep& Back = Out[Out.size() - 1 - K];
			const RideStep& St = Steps[I + K];
			if (Back.Edge != St.Edge || Back.To != St.From || Back.From != St.To) break;
			Run += G.Edges[size_t(St.Edge)].Length;
			++K;
		}
		if (K > 0 && Run <= SpurM)
		{
			Out.resize(Out.size() - K);
			I += K;
		}
		else
		{
			Out.push_back(Steps[I]);
			++I;
		}
	}
	return Out;
}

std::vector<RidePlan::RideStep> RidePlan::RunOut(const Region& G, const std::vector<RideStep>& Steps, const std::vector<double>& EdgeCost)
{
	// (RoutePlanner._run_out: on along the biggest road that goes on, the straightest of equals, never
	// back the way it came)
	std::vector<RideStep> Out;
	if (Steps.empty()) return Out;
	int At = Steps.back().To, Came = Steps.back().Edge;
	const auto Bearing = [](const P2& D) { return std::atan2(D.X, -D.Z); };
	std::vector<P2> Last = G.LineFrom(Came, Steps.back().From);
	double Heading = Last.size() >= 2 ? Bearing(Last.back() - Last[Last.size() - 2]) : 0.0;
	std::set<int> Seen;
	double Run = 0.0;
	const double Pi = 3.14159265358979323846;
	while (Run < RunOutM + 50.0)
	{
		int Best = -1;
		double BestCost = 1e18, BestTurn = 1e18;
		for (int Ei : G.Adjacent[size_t(At)])
		{
			if (Ei == Came || Seen.count(Ei) != 0 || !G.Passable(Ei, At) || G.TurnBanned(Came, At, Ei)) continue;
			const std::vector<P2> L = G.LineFrom(Ei, At);
			if (L.size() < 2) continue;
			double Turn = Bearing(L[1] - L[0]) - Heading;
			while (Turn > Pi) Turn -= 2.0 * Pi;
			while (Turn < -Pi) Turn += 2.0 * Pi;
			Turn = std::fabs(Turn);
			const double C = size_t(Ei) < EdgeCost.size() ? EdgeCost[size_t(Ei)] : 1.0;
			if (C < BestCost - 1e-9 || (std::fabs(C - BestCost) <= 1e-9 && Turn < BestTurn)) { Best = Ei; BestCost = C; BestTurn = Turn; }
		}
		if (Best < 0) break;
		Seen.insert(Best);
		const int To = G.OtherEnd(Best, At);
		Out.push_back({At, Best, To});
		const std::vector<P2> L = G.LineFrom(Best, At);
		Heading = Bearing(L.back() - L[L.size() - 2]);
		Run += G.Edges[size_t(Best)].Length;
		At = To;
		Came = Best;
	}
	return Out;
}

void RidePlan::LevelSpans(const std::vector<P2>& Line, std::vector<double>& H, std::vector<char>& Kind, int& BridgeCount, int& TunnelCount) const
{
	Kind.assign(Line.size(), 0);
	for (int Sort = 0; Sort < 2; ++Sort)
	{
		const std::vector<int> Spans = SpansAlong(Line, Sort);
		(Sort == 0 ? BridgeCount : TunnelCount) += int(Spans.size() / 2);
		for (size_t K = 0; K + 1 < Spans.size(); K += 2)
		{
			const int I0 = Spans[K], I1 = std::min(Spans[K + 1], int(H.size()) - 1);
			for (int I = I0 + 1; I < I1; ++I) H[size_t(I)] = H[size_t(I0)] + (H[size_t(I1)] - H[size_t(I0)]) * double(I - I0) / double(I1 - I0);
			for (int I = I0; I <= I1; ++I) Kind[size_t(I)] = static_cast<char>(Sort + 1);
		}
	}
}

void RidePlan::LayRoad(const std::vector<P2>& Line, const std::vector<double>& Halves, const std::vector<double>& Speeds, const std::vector<int>* Lanes,
	const std::vector<char>* OneWays, const std::vector<double>* Shoulders)
{
	const std::vector<double> Banks = RoadLine::BankOf(Line, Speeds);
	Road.clear();
	Net = std::make_shared<Network>();
	Bridges = Tunnels = 0;
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
	// Over a bridge the road runs straight from one end to the other, not down into what it crosses;
	// through a tunnel the same, not up over the hill.
	std::vector<char> Kind;
	LevelSpans(Line, H, Kind, Bridges, Tunnels);
	// The road's own height: the ground's along its middle, evened out over 75 m three times, so it neither
	// follows every hummock nor leaves the land far. ([crunched junctions, 169] by the metre, not by the knot:
	// a junction's turn is laid in knots a metre apart, and evened out over 9 of them either side and held to
	// the grade per 4 m knot, its rise came in a quarter of the distance, a ramp of 35% across the junction
	// and spikes where the side roads met it; Whaley Bridge, Kirkstone Pass)
	std::vector<double> KnotAlong(Line.size(), 0.0);
	for (size_t I = 1; I < Line.size(); ++I) KnotAlong[I] = KnotAlong[I - 1] + Line[I].DistanceTo(Line[I - 1]);
	for (int Pass = 0; Pass < 3; ++Pass)
	{
		std::vector<double> S(H.size());
		const double Reach = 10.0 * RoadLine::Step;   // (weights falling to nothing 40 m off, as 9 knots of 4 m either side)
		size_t J0 = 0;
		for (size_t I = 0; I < H.size(); ++I)
		{
			while (KnotAlong[J0] < KnotAlong[I] - Reach) ++J0;
			double Sum = 0.0, Wt = 0.0;
			for (size_t J = J0; J < H.size() && KnotAlong[J] < KnotAlong[I] + Reach; ++J)
			{
				// (each knot weighted by the stretch of road it stands for, so close knots count no more than far ones)
				const double Span = 0.5 * (KnotAlong[std::min(J + 1, H.size() - 1)] - KnotAlong[J > 0 ? J - 1 : 0]);
				const double W = (Reach - std::fabs(KnotAlong[J] - KnotAlong[I])) * std::fmax(Span, 0.05);
				Sum += H[J] * W;
				Wt += W;
			}
			S[I] = Wt > 0.0 ? Sum / Wt : H[I];
		}
		H.swap(S);
	}
	// (the Godot game averages over 9 knots once; three wider passes leave no crest a bike takes off
	// from at speed. The grade's limit is its own.)
	H = RoadLine::GradeLimit(H, RoadLine::MaxGrade, &KnotAlong);
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
		P.Lanes = Lanes != nullptr && I < Lanes->size() ? (*Lanes)[I] : 0;
		P.OneWay = OneWays != nullptr && I < OneWays->size() && ((*OneWays)[I] & 1) != 0;
		P.Dual = OneWays != nullptr && I < OneWays->size() && ((*OneWays)[I] & 2) != 0;
		const unsigned Code = OneWays != nullptr && I < OneWays->size() ? static_cast<unsigned char>((*OneWays)[I]) : 0u;
		P.Banked = static_cast<unsigned char>((Code >> 2) & 3u);
		P.Park = static_cast<unsigned char>((Code >> 4) & 15u);
		P.Shoulder = Shoulders != nullptr && I < Shoulders->size() ? (*Shoulders)[I] : 0.0;
		P.Near = P.OneWay ? static_cast<signed char>(KeepLeft() ? 1 : -1) : 0;
		P.Bridge = Kind[I] == 1;
		P.Tunnel = Kind[I] == 2;
		P.Bank = Banks[I];
		WidestHalf = std::fmax(WidestHalf, P.Half);
		Net->Near[NearKey(P.At.X, P.At.Z)].push_back({0, int(Road.size())});
		Road.push_back(P);
		if (Road.size() == 1) { RoadBox.X0 = RoadBox.X1 = P.At.X; RoadBox.Z0 = RoadBox.Z1 = P.At.Z; }
		RoadBox.X0 = std::fmin(RoadBox.X0, P.At.X - 50.0);
		RoadBox.X1 = std::fmax(RoadBox.X1, P.At.X + 50.0);
		RoadBox.Z0 = std::fmin(RoadBox.Z0, P.At.Z - 50.0);
		RoadBox.Z1 = std::fmax(RoadBox.Z1, P.At.Z + 50.0);
	}
	Length = Along;
}

bool RidePlan::Nearest(double X, double Z, double& Dist, double& Height, int* Index, double* Half, int* Which, bool MainOnly, bool* OnBridge, double* Cross) const
{
	NearOpts Opts;
	Opts.MainOnly = MainOnly;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit)) return false;
	Dist = Hit.Dist;
	Height = Hit.Height;
	if (Index != nullptr) *Index = Hit.Index;
	if (Half != nullptr) *Half = Hit.Half;
	if (Which != nullptr) *Which = Hit.Which;
	if (OnBridge != nullptr) *OnBridge = Hit.Deck;
	if (Cross != nullptr) *Cross = Hit.Cross;
	return true;
}

bool RidePlan::NearestOf(double X, double Z, const NearOpts& Opts, NearHit& Hit) const
{
	// (best by how far outside the road's edge the point is, so a point on a lane beside a main road
	// is the lane's; the ride's own road wins a tie)
	double Best = 1e18;
	Hit = NearHit();
	if (Net == nullptr) return false;
	std::shared_lock<std::shared_mutex> Hold(Net->Read);
	const long long Cx = static_cast<long long>(std::floor(X / NearCell)), Cz = static_cast<long long>(std::floor(Z / NearCell));
	for (long long Dx = -3; Dx <= 3; ++Dx)
		for (long long Dz = -3; Dz <= 3; ++Dz)
		{
			const auto It = Net->Near.find((Cx + Dx) * 4000037LL + (Cz + Dz));
			if (It == Net->Near.end()) continue;
			for (const std::pair<int, int>& Entry : It->second)
			{
				if ((Opts.MainOnly && Entry.first != 0) || Entry.first == Opts.NotRoad || (Opts.OnlyRoad >= 0 && Entry.first != Opts.OnlyRoad)) continue;
				if (Opts.Settles > 0 && (Entry.first == Opts.Settles || !JoinsOnto(Opts.Settles, Entry.first))) continue;
				const std::vector<RoadPoint>& Pts = RoadOf(Entry.first);
				// The bit of road from this point to the next (the last point's is filed under the one before).
				const int S = Entry.second;
				if (S + 1 >= int(Pts.size())) continue;
				const RoadPoint& Pa = Pts[size_t(S)];
				const RoadPoint& Pb = Pts[size_t(S) + 1];
				const bool SegDeck = Pa.Bridge && Pb.Bridge, SegBore = Pa.Tunnel && Pb.Tunnel;
				if ((Opts.NoDecks && SegDeck) || (Opts.NoBores && SegBore)) continue;
				const P2 Ab = Pb.At - Pa.At;
				const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
				const double T = L2 > 1e-12 ? std::fmin(1.0, std::fmax(0.0, ((X - Pa.At.X) * Ab.X + (Z - Pa.At.Z) * Ab.Z) / L2)) : 0.0;
				const double Px = Pa.At.X + Ab.X * T, Pz = Pa.At.Z + Ab.Z * T;
				const double D2 = (X - Px) * (X - Px) + (Z - Pz) * (Z - Pz);
				if (D2 > NearestReach * NearestReach) continue;
				const double D = std::sqrt(D2);
				const double HalfHere = Pa.Half + (Pb.Half - Pa.Half) * T;
				const double Score = D - HalfHere - (Entry.first == 0 ? 0.01 : 0.0);
				if (Score >= Best) continue;
				Best = Score;
				Hit.Dist = D;
				Hit.Height = Pa.Height + (Pb.Height - Pa.Height) * T;
				Hit.Half = HalfHere;
				Hit.Index = T < 0.5 ? S : S + 1;
				Hit.Which = Entry.first;
				Hit.Deck = SegDeck;
				Hit.Bore = SegBore;
				Hit.Banked = (T < 0.5 ? Pa : Pb).Banked;
				Hit.Foot = P2(Px, Pz);
				Hit.Along = Pa.Along + (Pb.Along - Pa.Along) * T;
				// (how far to the left of the road's middle: left of a heading (dx, dz) is (dz, -dx))
				const double Len = std::sqrt(L2);
				const double Off = Len > 1e-9 ? ((X - Px) * Ab.Z - (Z - Pz) * Ab.X) / Len : 0.0;
				Hit.Off = Off;
				const double BankHere = Pa.Bank + (Pb.Bank - Pa.Bank) * T;
				Hit.Cross = RoadLine::CrossHeight(Off, HalfHere, BankHere);
				Hit.Fall = HalfHere > 1e-6 ? std::fmax(0.0, -RoadLine::CrossHeight(Off >= 0.0 ? HalfHere : -HalfHere, HalfHere, BankHere) / HalfHere) : 0.0;
			}
		}
	return Hit.Index >= 0;
}

void RidePlan::LaySides(const std::vector<int>& Path)
{
	Planned.clear();
	MouthAt.assign(Road.size(), 0);
	StreetMouthAt.assign(Road.size(), 0);
	SideBridges = SideTunnels = 0;
	// [road rulebook C6] The junctions the ride goes straight through on the through road: its centre line turns to a
	// warning line either side (a junction built whole has no mouth on the ride's road to say so).
	RideThroughAt.clear();
	for (size_t I = 0; I + 1 < MadeSteps.size() && !Road.empty(); ++I)
	{
		const int Node = MadeSteps[I].To;
		if (Node < 0 || size_t(Node) >= JunctionOfNode.size() || JunctionOfNode[size_t(Node)] < 0) continue;
		const JunctionPlace& J = JunctionPlaces[size_t(JunctionOfNode[size_t(Node)])];
		if (J.Roads < 3 || J.Lights || J.Ring >= 0) continue;
		int Through = 0;
		for (const JunctionArm& A : J.Arms)
			if (A.Through && (A.Edge == MadeSteps[I].Edge || A.Edge == MadeSteps[I + 1].Edge)) ++Through;
		if (Through < 2) continue;
		const P2 At = Area.Roads.Nodes[size_t(Node)];
		size_t Best = 0;
		for (size_t K = 1; K < Road.size(); ++K)
			if (Road[K].At.DistanceTo(At) < Road[Best].At.DistanceTo(At)) Best = K;
		if (Road[Best].At.DistanceTo(At) < 15.0) RideThroughAt.push_back(Road[Best].Along);
	}
	std::sort(RideThroughAt.begin(), RideThroughAt.end());
	const Region& G = Area.Roads;
	Network& N = *Net;
	N.OnRide.assign(G.Edges.size(), 0);
	for (int E : Path) N.OnRide[size_t(E)] = 1;
	N.RideNodes.clear();
	N.StepNodes.clear();
	for (int E : Path) { N.RideNodes.insert(G.Edges[size_t(E)].A); N.RideNodes.insert(G.Edges[size_t(E)].B); }
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		if (N.OnRide[E] != 0) continue;
		const RoadEdge& Edge = G.Edges[E];
		if (N.RideNodes.count(Edge.A) != 0) N.StepNodes.insert(Edge.B);
		if (N.RideNodes.count(Edge.B) != 0) N.StepNodes.insert(Edge.A);
	}
	MapPoints(Road, N.OnRide, MainMapped);   // [OSM stage 5]
	N.Side = std::make_unique<std::atomic<const SideRoad*>[]>(G.Edges.size());
	for (size_t E = 0; E < G.Edges.size(); ++E) N.Side[E].store(nullptr, std::memory_order_relaxed);
	N.EdgeState.assign(G.Edges.size(), 0);
	// Every road the network can lay, filed under each block a bit of it passes through.
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& Edge = G.Edges[E];
		if (N.OnRide[E] != 0 || Edge.Line.size() < 2 || Edge.A == Edge.B) continue;
		for (size_t I = 0; I + 1 < Edge.Line.size(); ++I)
		{
			const P2& A = Edge.Line[I];
			const P2& B = Edge.Line[I + 1];
			const long long X0 = BlockOf(std::fmin(A.X, B.X)), X1 = BlockOf(std::fmax(A.X, B.X));
			const long long Z0 = BlockOf(std::fmin(A.Z, B.Z)), Z1 = BlockOf(std::fmax(A.Z, B.Z));
			for (long long Bx = X0; Bx <= X1; ++Bx)
				for (long long Bz = Z0; Bz <= Z1; ++Bz)
				{
					std::vector<int>& In = N.BlockEdges[BlockKey(Bx, Bz)];
					if (In.empty() || In.back() != int(E)) In.push_back(int(E));
				}
		}
	}
	// Those near the ride's road are laid now, in the map's order: their mouths are on it. (Where the ride
	// goes, in squares of SideReach: a road with a point in one of them or the next is near it.)
	std::unordered_map<long long, char> Beside;
	const auto Square = [](double X, double Z) { return static_cast<long long>(std::floor(X / SideReach)) * 4000037LL + static_cast<long long>(std::floor(Z / SideReach)); };
	for (const RoadPoint& P : Road)
		for (int Dx = -1; Dx <= 1; ++Dx)
			for (int Dz = -1; Dz <= 1; ++Dz) Beside[Square(P.At.X + Dx * SideReach, P.At.Z + Dz * SideReach)] = 1;
	std::vector<std::pair<int, unsigned char>> Marks;
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& Edge = G.Edges[E];
		if (N.OnRide[E] != 0 || Edge.Line.size() < 2 || Edge.A == Edge.B) continue;
		bool Near1 = false;
		for (const P2& P : Edge.Line)
			if (Beside.count(Square(P.X, P.Z)) != 0) { Near1 = true; break; }
		if (!Near1) continue;
		Marks.clear();
		if (EnsureSide(int(E), &Marks) == nullptr) continue;
		Planned.push_back(int(E) + 1);
		for (const std::pair<int, unsigned char>& M : Marks) MouthAt[size_t(M.first)] |= M.second;
		if (StreetClass(G.ClassOf(int(E))))
			for (const std::pair<int, unsigned char>& M : Marks) StreetMouthAt[size_t(M.first)] |= M.second;
	}
	SideBridges = N.Bridges.load();
	SideTunnels = N.Tunnels.load();
}

bool RidePlan::LaySide(int E, SideRoad& Side, std::vector<std::pair<int, unsigned char>>& Marks, int& NBridges, int& NTunnels) const
{
	const Region& G = Area.Roads;
	const RoadEdge& Edge = G.Edges[size_t(E)];
	Side.Edge = E;
	// Its line as the map drew it; where an end is at the ride's road (whose line the smoothing may
	// have moved a little off the junction), on to that road's middle.
	std::vector<P2> Raw = Edge.Line;
	bool Meets[2] = {false, false};
	for (int End = 0; End < 2; ++End)
	{
		const P2 At = End == 0 ? Raw.front() : Raw.back();
		double D = 0.0, H = 0.0;
		int Index = -1;
		if (!Nearest(At.X, At.Z, D, H, &Index, nullptr, nullptr, true) || D > 15.0) continue;
		// ([smooth junctions stage 5c] a node a short side edge away from the ride's road meets it through that edge, not
		// on it: pulled on, the next road ran straight onto the ride's road over the short one, and a junction built whole
		// there put its mouth 2.5 m from the ride's road's middle, York)
		const int Node = End == 0 ? Edge.A : Edge.B;
		if (WholeJunctions && WholeArmsOf.count(E + 1) != 0 && Net->RideNodes.count(Node) == 0 && Net->StepNodes.count(Node) != 0) continue;
		Meets[End] = true;
		// [square T] (the through road's arm at a T the ride turns at: laid along the through road's one
		// straight line into the junction, not bent onto the ride's road where it turns)
		const auto Ts = ThroughLines.find(E);
		if (Ts != ThroughLines.end() && Ts->second.Hub.DistanceTo(At) < 15.0)
		{
			const P2 Hub = Ts->second.Hub, Dir = Ts->second.Dir;
			std::vector<P2> Was = Raw;
			if (End == 1) std::reverse(Was.begin(), Was.end());
			std::vector<P2> Now;
			double Gone = 0.0;
			for (size_t K = 0; K < Was.size(); ++K)
			{
				if (K > 0) Gone += Was[K].DistanceTo(Was[K - 1]);
				if (Gone < 30.0) continue;
				const double S = (Was[K].X - Hub.X) * Dir.X + (Was[K].Z - Hub.Z) * Dir.Z;
				const double W = std::clamp((45.0 - Gone) / 15.0, 0.0, 1.0);
				Now.push_back(Was[K] + (Hub + Dir * std::fmax(0.0, S) - Was[K]) * W);
			}
			// (the straight in from 30 m, every 4 m, to the junction itself)
			const double From = Now.empty() ? 30.0 : std::fmin(30.0, std::fmax(0.0, (Now.front().X - Hub.X) * Dir.X + (Now.front().Z - Hub.Z) * Dir.Z));
			std::vector<P2> In;
			for (double S = 0.0; S < From - 1.0; S += 4.0) In.push_back(Hub + Dir * S);
			std::reverse(In.begin(), In.end());
			std::reverse(Now.begin(), Now.end());
			Now.insert(Now.end(), In.begin(), In.end());
			if (Now.size() >= 2)
			{
				if (End == 0) std::reverse(Now.begin(), Now.end());
				Raw = Now;
				continue;
			}
		}
		const P2 Mid = Road[size_t(Index)].At;
		if (Mid.DistanceTo(At) > 0.5)
		{
			if (End == 0) Raw.insert(Raw.begin(), Mid);
			else Raw.push_back(Mid);
		}
		else (End == 0 ? Raw.front() : Raw.back()) = Mid;   // ([junction priority 168] on it exactly: a junction's tight corner is banked)
	}
	const std::string& Class = G.ClassOf(int(E));
	std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, std::vector<double>(Raw.size(), OnRoundRing(E) ? 0.0 : RoadLine::FairOf(Class)));
	// (the knots stop at the last whole step: on to the junction itself, so no road ends short of it)
	if (Line.empty() || Line.back().DistanceTo(Raw.back()) > 0.3) Line.push_back(Raw.back());
	// ([smooth junctions stage 5] a short piece the smoothing left as one point: as the map has it, not left out, a hole at its
	// junction, York)
	if (Line.size() < 2 && Raw.size() >= 2 && Raw.front().DistanceTo(Raw.back()) > 0.5) Line = Raw;
	if (Line.size() < 2) return false;
	// [OSM stage 5C] A motorway's half with its hard shoulder: laid that much further out on its nearside
	// (the traffic's left in Britain), easing in over 30 m from an end on the ride's road.
	const int NearSide = (Edge.Oneway < 0 ? -1 : 1) * (KeepLeft() ? 1 : -1);
	const double HardWidth = RoadX::Of(G, int(E), RoadLine::WidthOf(Class)).Shoulder;
	if (HardWidth > 0.0) ShiftLine(Line, double(NearSide) * HardWidth * 0.5, Meets[0] ? 30.0 : 0.0, Meets[1] ? 30.0 : 0.0);
	// Its height: the ground's, smoothed as the ride's road is; at a junction off the ride, the
	// ground's at the junction (so every road that meets there agrees); near the ride's road, that road's.
	std::vector<double> H(Line.size()), Along(Line.size(), 0.0);
	double Last = Road.front().Height;
	const bool Circuit = G.CircuitEdge(int(E));   // [circuits, build 168] (silk smooth: see CircuitGround)
	for (size_t I = 0; I < Line.size(); ++I)
	{
		const double Y = Circuit ? CircuitGround(Line[I].X, Line[I].Z) : Land->Height(Line[I].X, Line[I].Z);
		if (!std::isnan(Y)) Last = Y;
		H[I] = std::fmax(std::isnan(Y) ? Last : Y, 0.0);
		if (I > 0) Along[I] = Along[I - 1] + Line[I].DistanceTo(Line[I - 1]);
	}
	const double EndH[2] = {H.front(), H.back()};
	// (its own bridges and tunnels, as the ride's road has its)
	std::vector<char> Kind;
	LevelSpans(Line, H, Kind, NBridges, NTunnels);
	// [circuits, build 169] (a circuit's track: CircuitGround's as it is, already smooth, with no grade limit's kinks)
	if (!Circuit) H = RoadLine::GradeLimit(RoadLine::SmoothHeights(RoadLine::SmoothHeights(H, 9), 9));
	Side.Length = Along.back();
	Side.Track = Class == "track" || Class == "service";   // ([OSM stage 4] a service road: no paint either)
	Side.Gravel = G.SurfaceOf(int(E)) == "gravel";
	Side.Setts = SettsSurface(G.SurfaceOf(int(E))) || SettsStreet(Line);   // ([towns stage 8] or a famous street's)
	const RoadX::Across Ax = RoadX::Of(G, int(E), RoadLine::WidthOf(Class));   // [OSM stage 5]
	const double Half = RingHalfOf(int(E), Ax.Half);   // [roundabouts] (round a ring, the ring's one width)
	std::vector<double> Banks = RoadLine::BankOf(Line, {RoadLine::DesignSpeedOf(Class)});
	// [circuits, build 169] (a circuit's tilted across only as CircuitGround is, not banked for its bends: no roll
	// where pieces meet, and two side by side or crossing agree all across, not only down their middles)
	if (Circuit) CircuitTilts(int(E), Line, Half, Banks);
	CircuitBanks(int(E), Line, Banks);   // [circuits stage 6] (a circuit's famous corners, banked by hand)
	// (each end eased to the ground's height at its junction over 30 m, or over half the road where it is
	// shorter than 60 m: beenster, build 148, Brierley Green. A 10 m link between two junctions had its
	// first end pulled three quarters of the way to the far end's height, a metre and more off the
	// junction every other road there meets at, so the ground, cut to the junction, buried it)
	const double EaseOver = std::fmax(1.0, std::fmin(30.0, Side.Length * 0.5));
	for (size_t I = 0; I < Line.size(); ++I)
		for (int End = 0; End < 2; ++End)
		{
			if (Meets[End] || Circuit) continue;   // ([circuits, build 169] CircuitGround's agrees at every junction: easing flattened each end)
			const double From = End == 0 ? Along[I] : Side.Length - Along[I];
			H[I] += (EndH[End] - H[I]) * (1.0 - Smooth01(From / EaseOver));
		}
	// (no ramp steeper than 1 in 5 from all that, then the ride's road's own height where it is near)
	for (int Round = 0; Round < (Circuit ? 0 : 3); ++Round)   // ([circuits] not a circuit's: its kinks were bumps)
	{
		for (size_t I = 1; I < H.size(); ++I)
		{
			const double Lim = 0.2 * (Along[I] - Along[I - 1]);
			H[I] = std::fmin(std::fmax(H[I], H[I - 1] - Lim), H[I - 1] + Lim);
		}
		for (size_t I = H.size(); I-- > 1;)
		{
			const double Lim = 0.2 * (Along[I] - Along[I - 1]);
			H[I - 1] = std::fmin(std::fmax(H[I - 1], H[I] - Lim), H[I] + Lim);
		}
	}
	// (and its ends back on their junctions' heights, should the ramp limit have moved them, eased over
	// enough road to add at most 3 in 8 to its own 1 in 5: a short, steep link between two junctions still meets both)
	for (int End = 0; End < 2; ++End)
	{
		if (Meets[End]) continue;
		const double Off = EndH[End] - (End == 0 ? H.front() : H.back());
		if (std::fabs(Off) < 0.01) continue;
		const double Over = std::fmax(1.0, std::fmin(Side.Length, std::fabs(Off) * 4.0));
		for (size_t I = 0; I < Line.size(); ++I)
		{
			const double From = End == 0 ? Along[I] : Side.Length - Along[I];
			H[I] += Off * (1.0 - Smooth01(From / Over));
		}
	}
	// [smooth junctions stage 3] (into a junction built whole, knots every metre and a half for its last 40 m, its
	// height, tilt and kind straight between the knots it had: its rise evened out there by the metre, not bent at a
	// knot every 4 m, the knocks a bike felt over a hill junction. After its heights are worked out on the knots it
	// had, as every other road's are, so its evening out over 9 knots is no shorter)
	const bool WholeEnd[2] = {!Circuit && WholeAt(E + 1, 0) >= 0, !Circuit && WholeAt(E + 1, 1) >= 0};
	if (WholeEnd[0] || WholeEnd[1])
	{
		const double Total = Side.Length;
		const double Reach0 = WholeEnd[1] ? WholeEase : std::fmin(WholeEase, std::fmax(Total * 0.5, Total - 10.0));
		const double Reach1 = WholeEnd[0] ? WholeEase : std::fmin(WholeEase, std::fmax(Total * 0.5, Total - 10.0));
		std::vector<P2> FineLine{Line.front()};
		std::vector<double> FineH{H.front()}, FineAlong{Along.front()}, FineBanks{Banks.front()};
		std::vector<char> FineKind{Kind.front()};
		for (size_t I = 1; I < Line.size(); ++I)
		{
			const bool Near = (WholeEnd[0] && Along[I - 1] < Reach0) || (WholeEnd[1] && Along[I] > Total - Reach1);
			const int Parts = Near ? std::max(1, int(std::ceil((Along[I] - Along[I - 1]) / 1.5))) : 1;
			for (int K = 1; K < Parts; ++K)
			{
				const double T = double(K) / Parts;
				FineLine.push_back(Line[I - 1] + (Line[I] - Line[I - 1]) * T);
				FineH.push_back(H[I - 1] + (H[I] - H[I - 1]) * T);
				FineAlong.push_back(Along[I - 1] + (Along[I] - Along[I - 1]) * T);
				FineBanks.push_back(Banks[I - 1] + (Banks[I] - Banks[I - 1]) * T);
				FineKind.push_back(Kind[I - 1] == Kind[I] ? Kind[I] : char(0));
			}
			FineLine.push_back(Line[I]); FineH.push_back(H[I]); FineAlong.push_back(Along[I]); FineBanks.push_back(Banks[I]); FineKind.push_back(Kind[I]);
		}
		Line.swap(FineLine); H.swap(FineH); Along.swap(FineAlong); Banks.swap(FineBanks); Kind.swap(FineKind);
	}
	// [smooth junctions stage 3] (into a junction built whole, its rise evened out over 12 m by the metre for its last
	// 40 m, mirrored about its end so the end keeps its height and its slope: a grade's change spread over 12 m, not
	// bent at one knot; twice, so no corner is left where the evening starts)
	if (WholeEnd[0] || WholeEnd[1])
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			const std::vector<double> Was = H;
			const auto HeightAt = [&Was, &Along](double A)
			{
				const auto It = std::lower_bound(Along.begin(), Along.end(), A);
				if (It == Along.begin()) return Was.front();
				if (It == Along.end()) return Was.back();
				const size_t K = size_t(It - Along.begin());
				const double Seg = std::fmax(1e-9, Along[K] - Along[K - 1]);
				return Was[K - 1] + (Was[K] - Was[K - 1]) * ((A - Along[K - 1]) / Seg);
			};
			for (size_t I = 0; I < H.size(); ++I)
				for (int End = 0; End < 2; ++End)
				{
					if (!WholeEnd[End]) continue;
					const double From = End == 0 ? Along[I] : Side.Length - Along[I];
					const double Reach = WholeEnd[1 - End] ? WholeEase : std::fmin(WholeEase, std::fmax(Side.Length * 0.5, Side.Length - 10.0));   // (a short road's other end, a junction of the old kind, left as it was)
					if (From >= Reach) continue;
					const double EndAt = End == 0 ? 0.0 : Side.Length, Sign = End == 0 ? 1.0 : -1.0;
					const double H0 = End == 0 ? Was.front() : Was.back();
					double Sum = 0.0;
					int Count = 0;
					for (double D = -6.0; D <= 6.001; D += 0.5)
					{
						const double S = From + D;   // (metres in from the end; past it, mirrored)
						Sum += S >= 0.0 ? HeightAt(EndAt + Sign * S) : 2.0 * H0 - HeightAt(EndAt - Sign * S);
						++Count;
					}
					const double Keep = Smooth01((From - Reach * 0.6) / (Reach * 0.4));
					H[I] = Sum / Count + (H[I] - Sum / Count) * Keep;
				}
		}
	// [smooth junctions stage 3] (a lesser road into a junction built whole: level with its end across the through
	// road's width and half a metre more, rising to its own over 8 m beyond: the through road's surface is carried
	// across the junction, and this road's own line, standing higher on a hill, brought the ground up through it)
	for (int End = 0; End < 2; ++End)
	{
		if (!WholeEnd[End]) continue;
		const int Ji = WholeAt(E + 1, End);
		double Flat = -1.0;
		bool Minor = false;
		for (const JunctionArm& Arm : JunctionPlaces[size_t(Ji)].Arms)
		{
			if (Arm.Edge == int(E)) Minor = !Arm.Through;
			if (Arm.Through) Flat = std::fmax(Flat, RoadX::Of(G, Arm.Edge, RoadLine::WidthOf(G.ClassOf(Arm.Edge))).Half + 0.5);
		}
		if (!Minor || Flat <= 0.0) continue;
		const double H0 = End == 0 ? H.front() : H.back();
		for (size_t I = 0; I < H.size(); ++I)
		{
			const double From = End == 0 ? Along[I] : Side.Length - Along[I];
			if (From < Flat + 8.0) H[I] = H0 + (H[I] - H0) * Smooth01((From - Flat) / 8.0);
		}
	}
	// Where it is by the ride's road. At a junction it has no deck of its own; where it goes over or
	// under the ride's road with no junction (one of the two on a bridge or in a tunnel there), each
	// keeps its own height.
	std::vector<NearHit> Main(Line.size());
	std::vector<double> Apart(Line.size(), 1e18);
	NearOpts MainOnly;
	MainOnly.MainOnly = true;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		if (!NearestOf(Line[I].X, Line[I].Z, MainOnly, Main[I])) continue;
		if (Main[I].Dist > Main[I].Half + 2.0) continue;
		const double FromEnd = std::fmin(Meets[0] ? Along[I] : 1e18, Meets[1] ? Side.Length - Along[I] : 1e18);
		if (FromEnd < 25.0) Kind[I] = 0;
		else if (Kind[I] != 0 || Main[I].Deck || Main[I].Bore) Apart[I] = 0.0;
	}
	for (size_t I = 1; I < Line.size(); ++I) Apart[I] = std::fmin(Apart[I], Apart[I - 1] + (Along[I] - Along[I - 1]));
	for (size_t I = Line.size(); I-- > 1;) Apart[I - 1] = std::fmin(Apart[I - 1], Apart[I] + (Along[I] - Along[I - 1]));
	for (size_t I = 0; I < Line.size(); ++I)
	{
		double Y = H[I];
		double Meeting = 0.0;
		if (Main[I].Index >= 0)
		{
			// (the ride's road's surface where this point is on it, tilt and all; its edge's beyond)
			Meeting = (1.0 - Smooth01((Main[I].Dist - Main[I].Half - 2.0) / 30.0)) * Smooth01(Apart[I] / 40.0);
			Y += (Main[I].Height + Main[I].Cross - Y) * Meeting;
		}
		RoadPoint P;
		P.At = Line[I];
		P.Along = Along[I];
		P.Height = Y;
		P.Half = Half;
		P.Lanes = Ax.Lanes;
		P.OneWay = Ax.OneWay;
		P.Dual = Ax.Dual;
		P.Shoulder = Ax.Shoulder;
		P.Banked = static_cast<unsigned char>(Ax.Banked);   // [OSM stage 5E]
		P.Park = static_cast<unsigned char>(Ax.ParkLeft | (Ax.ParkRight << 2));   // [OSM stage 5D] (laid as the edge runs)
		P.Near = Ax.OneWay ? static_cast<signed char>(NearSide) : 0;
		P.Bridge = Kind[I] == 1;
		P.Tunnel = Kind[I] == 2;
		// Its own tilt: none where it lies on the ride's road, nor at a junction at either end.
		// ([circuits, build 169] a circuit's track keeps its tilt to its ends: CircuitTilts' agrees with the next piece's
		// there, and fading it out at every junction rolled the edges up and down)
		P.Bank = Banks[I] * (1.0 - Meeting) * (Circuit ? 1.0 : Smooth01(std::fmin(Along[I], Side.Length - Along[I]) / 20.0));
		Side.Pts.push_back(P);
	}
	// Its mouths: from each end on the ride's road, out to where its middle leaves that road's edge.
	for (int End = 0; End < 2; ++End)
	{
		if (!Meets[End]) continue;
		if (WholeAt(E + 1, End) >= 0) continue;   // [smooth junctions 4b] (onto a ring the ride goes round, built whole: its piece has the mouth)
		const int Count = int(Side.Pts.size()), Dir = End == 0 ? 1 : -1;
		NearHit Last2, Here;
		for (int Gone = 0, I = End == 0 ? 0 : Count - 1; Gone < Count; ++Gone, I += Dir)
		{
			const RoadPoint& P = Side.Pts[size_t(I)];
			if ((End == 0 ? P.Along : Side.Length - P.Along) > 40.0 || !NearestOf(P.At.X, P.At.Z, MainOnly, Here)) break;
			if (Here.Dist <= Here.Half) { Last2 = Here; continue; }
			if (Gone == 0) break;   // (its end is not on the ride's road after all)
			const RoadPoint& Q = Side.Pts[size_t(I - Dir)];
			const double In = std::fmax(0.0, Last2.Half - Last2.Dist), Outside = Here.Dist - Here.Half;
			const P2 Step2 = P.At - Q.At;
			const int Mi = Here.Index;
			const P2 MainDir = Road[size_t(std::min(Mi + 1, int(Road.size()) - 1))].At - Road[size_t(std::max(Mi - 1, 0))].At;
			if (Step2.Length() < 1e-6 || MainDir.Length() < 1e-6) break;
			Mouth M;
			M.At = Q.At + Step2 * (In / (In + Outside));
			M.Away = Step2 * (1.0 / Step2.Length());
			M.Along = MainDir * (1.0 / MainDir.Length());
			M.Across = P2(M.Along.Z, -M.Along.X);   // (the ride's road's left)
			const bool OnLeft = Dot(M.At - Road[size_t(Mi)].At, M.Across) >= 0.0;
			if (!OnLeft) M.Across = M.Across * -1.0;
			if (Dot(M.Away, M.Across) < 0.4) break;   // (it runs in alongside: no corner to round)
			M.Is = true;
			M.MainIndex = Mi;
			M.MainHalf = Here.Half;
			// (a lane meeting a lane has no markings; a lesser road meeting a greater gives way)
			M.GiveWay = Half >= 1.6 && Half < Here.Half - 0.2;
			// ([156] or a lesser class of road, though as wide: a residential street, 6 m now, onto a B road)
			if (!M.GiveWay && Half >= 1.6)
				if (const RoadFact* MainFact = FactAt(Here.Along))
					M.GiveWay = RoadLine::MainCostOf(G.ClassOf(E)) > RoadLine::MainCostOf(MainFact->Class) + 0.05;
			// [junction priority] (at a junction of the ride's, the real road's priority: a through road never
			// gives way, a road that meets it always does, the ride's own road one or the other)
			if (Half >= 1.6)
				if (const unsigned char Rank = ArmRankOf(End == 0 ? Edge.A : Edge.B, E)) M.GiveWay = Rank == 1;
			// [OSM stage 5B] (at a roundabout every road onto the ring gives way to it, with one broken line;
			// the ring's own edges never do)
			{
				const RoadExtra* X = G.ExtraOf(E);
				const int RingHere = RingAt(End == 0 ? Edge.A : Edge.B);
				if (X != nullptr && X->Has(RoadExtra::Roundabout)) M.GiveWay = false;
				else if (RingHere >= 0 && Half >= 1.6) { M.GiveWay = true; M.Ring = true; }
				// [roundabouts] (a ring drawn as a circle paints its own give-ways, RingPaint; its corners flared)
				if (RingHere >= 0 && RoadRings[size_t(RingHere)].Circle) { M.GiveWay = false; M.Flare = true; }
			}
			const double AlongAt = Q.Along + (P.Along - Q.Along) * (In / (In + Outside));
			M.From = End == 0 ? AlongAt : Side.Length - AlongAt;
			// The ride's road's points the mouth and its corners lie beside: no edge line there, and no
			// shoulder across the mouth itself.
			double From = 1e18, To = -1e18, MouthFrom = 1e18, MouthTo = -1e18;
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				const double Sigma = Sd == 0 ? 1.0 : -1.0;
				P2 Corner, AlongMain;
				double Reach = 0.0;
				MouthCorner(M, Road[size_t(Mi)].At, Half, Sigma, Corner, AlongMain, Reach);
				const P2 EdgeAt = M.At + P2(M.Away.Z, -M.Away.X) * (Sigma * Half);
				M.Clear[Sd] = M.From + Dot(Corner - EdgeAt, M.Away) + Reach;
				const double At2 = Dot(Corner + AlongMain * Reach - Road[size_t(Mi)].At, M.Along), At3 = Dot(Corner - Road[size_t(Mi)].At, M.Along);
				From = std::fmin(From, At2);
				To = std::fmax(To, At2);
				MouthFrom = std::fmin(MouthFrom, At3);
				MouthTo = std::fmax(MouthTo, At3);
			}
			Side.Mouths[End] = M;
			for (int J = std::max(0, Mi - 12); J <= std::min(int(Road.size()) - 1, Mi + 12); ++J)
			{
				const double At2 = Dot(Road[size_t(J)].At - Road[size_t(Mi)].At, M.Along);
				if (At2 >= From - 0.5 && At2 <= To + 0.5) Marks.emplace_back(J, static_cast<unsigned char>(OnLeft ? 1 : 2));
				if (At2 >= MouthFrom && At2 <= MouthTo) Marks.emplace_back(J, static_cast<unsigned char>(OnLeft ? 4 : 8));
			}
			break;
		}
	}
	// [OSM stage 5] Its mapped lights, crossings and humps; a mapped give-way or stop line at a mouth
	// stands in for the rule's (mapped first, rule second, never both).
	MapPoints(Side.Pts, Net->OnRide, Side.Mapped, E);
	// [roundabouts] (a ring drawn as a circle paints its own give-ways where its roads come onto it: the
	// map's give-way and stop points by it go, often mapped well back up the road)
	Side.Mapped.erase(std::remove_if(Side.Mapped.begin(), Side.Mapped.end(), [this, &Side](const MappedMark& Mk)
	{
		if (Mk.Kind != "give_way" && Mk.Kind != "stop") return false;
		for (size_t I = 1; I < Side.Pts.size(); ++I)
			if (Side.Pts[I].Along >= Mk.Along) return PaintedRingNear(Side.Pts[I].At, 30.0) >= 0;
		return false;
	}), Side.Mapped.end());
	for (int End = 0; End < 2; ++End)
	{
		Mouth& M = Side.Mouths[End];
		if (!M.Is || !M.GiveWay) continue;
		for (const MappedMark& Mk : Side.Mapped)
			if ((Mk.Kind == "give_way" || Mk.Kind == "stop") && (End == 0 ? Mk.Along : Side.Length - Mk.Along) < 25.0) M.GiveWay = false;
	}
	return true;
}

const RidePlan::SideRoad* RidePlan::EnsureSide(int E, std::vector<std::pair<int, unsigned char>>* Marks) const
{
	Network& N = *Net;
	if (E < 0 || size_t(E) >= N.EdgeState.size() || N.OnRide[size_t(E)] != 0) return nullptr;
	{
		std::unique_lock<std::mutex> Hold(N.Lay);
		for (;;)
		{
			const char State = N.EdgeState[size_t(E)];
			if (State == 2) return N.Side[size_t(E)].load(std::memory_order_acquire);
			if (State == 0) { N.EdgeState[size_t(E)] = 1; break; }
			N.Done.wait(Hold);
		}
	}
	std::unique_ptr<SideRoad> Made = std::make_unique<SideRoad>();
	std::vector<std::pair<int, unsigned char>> Mine;
	int Nb = 0, Nt = 0;
	const bool Laid = LaySide(E, *Made, Marks != nullptr ? *Marks : Mine, Nb, Nt);
	const SideRoad* Out = Laid ? Made.get() : nullptr;
	{
		std::lock_guard<std::mutex> Hold(N.Lay);
		if (Laid) N.Own.push_back(std::move(Made));
		N.Side[size_t(E)].store(Out, std::memory_order_release);
		N.EdgeState[size_t(E)] = 2;
	}
	N.Bridges += Nb;
	N.Tunnels += Nt;
	if (Out != nullptr)
	{
		std::unique_lock<std::shared_mutex> Write(N.Read);
		for (size_t I = 0; I < Out->Pts.size(); ++I) N.Near[NearKey(Out->Pts[I].At.X, Out->Pts[I].At.Z)].push_back({E + 1, int(I)});
	}
	N.Done.notify_all();
	return Out;
}

const RidePlan::SideRoad* RidePlan::SideOf(int Which) const
{
	if (Which <= 0 || Net == nullptr || Net->Side == nullptr || size_t(Which - 1) >= Net->EdgeState.size()) return nullptr;
	return Net->Side[size_t(Which - 1)].load(std::memory_order_acquire);
}

const std::vector<RoadPoint>& RidePlan::RoadOf(int Which) const
{
	static const std::vector<RoadPoint> None;
	if (Which <= 0) return Road;
	const SideRoad* Side = SideOf(Which);
	return Side != nullptr ? Side->Pts : None;
}

std::vector<int> RidePlan::RoadsNear(const Rect& Box, bool Lay) const
{
	std::vector<int> Out;
	if (Net == nullptr) return Out;
	if (!(RoadBox.X1 < Box.X0 || RoadBox.X0 > Box.X1 || RoadBox.Z1 < Box.Z0 || RoadBox.Z0 > Box.Z1)) Out.push_back(0);
	if (Lay) LayBlocks(Box);
	for (long long Bx = BlockOf(Box.X0); Bx <= BlockOf(Box.X1); ++Bx)
		for (long long Bz = BlockOf(Box.Z0); Bz <= BlockOf(Box.Z1); ++Bz)
		{
			const auto It = Net->BlockEdges.find(BlockKey(Bx, Bz));
			if (It == Net->BlockEdges.end()) continue;
			for (int E : It->second)
				if (SideOf(E + 1) != nullptr) Out.push_back(E + 1);
		}
	std::sort(Out.begin(), Out.end());
	Out.erase(std::unique(Out.begin(), Out.end()), Out.end());
	return Out;
}

void RidePlan::LayBlocks(const Rect& Box) const
{
	if (Net == nullptr) return;
	for (long long Bx = BlockOf(Box.X0); Bx <= BlockOf(Box.X1); ++Bx)
		for (long long Bz = BlockOf(Box.Z0); Bz <= BlockOf(Box.Z1); ++Bz) LayBlock(BlockKey(Bx, Bz));
}

void RidePlan::NetworkFurniture(double X0, double Z0, double Size, std::vector<std::shared_ptr<const std::vector<Furnish>>>& Hold, std::vector<const Furnish*>& Out) const
{
	if (!Scene || Net == nullptr) return;
	Rect Box;
	Box.X0 = X0; Box.Z0 = Z0; Box.X1 = X0 + Size; Box.Z1 = Z0 + Size;
	const size_t First = Hold.size();
	const auto Once = [this, &Hold](int Kind, long long Key, const std::function<std::vector<Furnish>()>& Make)
	{
		{
			std::lock_guard<std::mutex> Lock(Net->Furnishing);
			const auto It = Net->Furnished.find({Kind, Key});
			if (It != Net->Furnished.end()) { Hold.push_back(It->second); return; }
		}
		// (made outside the lock: two workers may both make one, and the first kept is the one used)
		std::shared_ptr<const std::vector<Furnish>> Made = std::make_shared<const std::vector<Furnish>>(Make());
		std::lock_guard<std::mutex> Lock(Net->Furnishing);
		Hold.push_back(Net->Furnished.emplace(std::make_pair(Kind, Key), Made).first->second);
	};
	for (const int Which : RoadsNear(Box, false))
		if (Which > 0) Once(0, Which, [this, Which]() { return RoadFurniture(Which); });
	// [roundabouts round 2] (each ring's chevron boards and warnings, once a ring)
	for (size_t Rg = 0; Rg < RoadRings.size(); ++Rg)
	{
		const RoadRing& Ring = RoadRings[Rg];
		const double Pad = Ring.Reach + Ring.Half + 75.0;
		if (Ring.Mid.X < Box.X0 - Pad || Ring.Mid.X > Box.X1 + Pad || Ring.Mid.Z < Box.Z0 - Pad || Ring.Mid.Z > Box.Z1 + Pad) continue;
		const int Index = int(Rg);
		Once(2, Index, [this, Index]() { return RingFurniture(Index); });
	}
	for (long long Bx = BlockOf(Box.X0); Bx <= BlockOf(Box.X1 - 0.001); ++Bx)
		for (long long Bz = BlockOf(Box.Z0); Bz <= BlockOf(Box.Z1 - 0.001); ++Bz)
		{
			const long long Key = BlockKey(Bx, Bz);
			{
				std::lock_guard<std::mutex> Lock(Net->Lay);
				const auto It = Net->BlockState.find(Key);
				if (It == Net->BlockState.end() || It->second != 2) continue;
			}
			const Rect In = BlockBox(Key);
			Once(1, Key, [this, In]() { return BlockFurniture(In); });
		}
	for (size_t L = First; L < Hold.size(); ++L)
		for (const Furnish& F : *Hold[L])
			if (F.At.X >= X0 && F.At.X < X0 + Size && F.At.Z >= Z0 && F.At.Z < Z0 + Size) Out.push_back(&F);
}

bool RidePlan::BlockLaid(double X, double Z) const
{
	if (Net == nullptr) return false;
	std::lock_guard<std::mutex> Hold(Net->Lay);
	const auto It = Net->BlockState.find(BlockKey(BlockOf(X), BlockOf(Z)));
	return It != Net->BlockState.end() && It->second == 2;
}

bool RidePlan::BlockLaidNow(double X, double Z) const
{
	if (Net == nullptr) return false;
	std::unique_lock<std::mutex> Hold(Net->Lay, std::try_to_lock);
	if (!Hold.owns_lock()) return false;
	const auto It = Net->BlockState.find(BlockKey(BlockOf(X), BlockOf(Z)));
	return It != Net->BlockState.end() && It->second == 2;
}

int RidePlan::BlocksLaid() const
{
	return Net == nullptr ? 0 : Net->Blocks.load();
}

void RidePlan::LayBlock(long long Key) const
{
	Network& N = *Net;
	{
		std::unique_lock<std::mutex> Hold(N.Lay);
		for (;;)
		{
			char& State = N.BlockState[Key];
			if (State == 2) return;
			if (State == 0) { State = 1; break; }
			N.Done.wait(Hold);
		}
	}
	EnsureGround(BlockBox(Key));
	const auto It = N.BlockEdges.find(Key);
	if (It != N.BlockEdges.end())
		for (int E : It->second) EnsureSide(E);
	KnotsOfBlock(Key);
	{
		std::lock_guard<std::mutex> Hold(N.Lay);
		N.BlockState[Key] = 2;
	}
	++N.Blocks;
	N.Done.notify_all();
}

int RidePlan::EnsureGround(const Rect& Box) const
{
	if (CellsFrom == nullptr || HeightsFrom == nullptr || Land == nullptr || Net == nullptr || CellsFrom->Offline) return 0;
	if (Land->Ready(Rect{Box.X0 - TileMargin, Box.Z0 - TileMargin, Box.X1 + TileMargin, Box.Z1 + TileMargin}, 0)) return 0;
	// (every tile within a tile's width of the box: the fine tiles round any point of it are all in, so
	// none of it is eased into the coarser level)
	std::vector<P2> Points;
	for (double Z = Box.Z0; Z <= Box.Z1 + 1.0; Z += 128.0)
		for (double X = Box.X0; X <= Box.X1 + 1.0; X += 128.0) Points.push_back(P2(X, Z));
	const std::vector<TileKey> Keys = HeightStore::KeysNear(Points, TileMargin, Land->Zoom(0), Land->FrameOf());
	std::lock_guard<std::mutex> Hold(Net->Fetching);
	return FetchKeys(*CellsFrom, *HeightsFrom, Keys);
}

int RidePlan::GroundRound(double X, double Z) const
{
	if (CellsFrom == nullptr || HeightsFrom == nullptr || Land == nullptr || Net == nullptr || CellsFrom->Offline) return 0;
	int Came = 0;
	const std::vector<P2> At = {P2(X, Z)};
	for (int L = 0; L < Terrain::LevelCount; ++L)
	{
		const std::vector<TileKey> Keys = HeightStore::KeysNear(At, L == 0 ? 1500.0 : Terrain::LiveReach[L], Land->Zoom(L), Land->FrameOf());
		std::vector<TileKey> Want;
		for (const TileKey& K : Keys)
			if (!HeightsFrom->Has(K)) Want.push_back(K);
		if (Want.empty()) continue;
		std::lock_guard<std::mutex> Hold(Net->Fetching);
		Came += int(Want.size()) - FetchKeys(*CellsFrom, *HeightsFrom, Want);
	}
	return Came;
}

const RidePlan::Knot* RidePlan::KnotAt(int Which, int End) const
{
	if (Net == nullptr) return nullptr;
	std::shared_lock<std::shared_mutex> Hold(Net->Read);
	const auto It = Net->Knots.find(static_cast<long long>(Which) * 2 + End);
	return It == Net->Knots.end() ? nullptr : It->second.get();
}

std::vector<RidePlan::Knot> RidePlan::KnotsLaid() const
{
	std::vector<Knot> Out;
	if (Net == nullptr) return Out;
	std::shared_lock<std::shared_mutex> Hold(Net->Read);
	for (const auto& KV : Net->Knots)
		if (static_cast<long long>(KV.second->Owner) * 2 + KV.second->OwnerEnd == KV.first) Out.push_back(*KV.second);
	std::sort(Out.begin(), Out.end(), [](const Knot& X, const Knot& Y) { return X.Owner != Y.Owner ? X.Owner < Y.Owner : X.OwnerEnd < Y.OwnerEnd; });
	return Out;
}

void RidePlan::KnotsOfBlock(long long Key) const
{
	// The junctions whose roads' ends are in the block: two or more ends at one place, none a mouth.
	// (Every road meeting at a junction passes through the block its ends are in, so is laid with it.)
	const Rect Box = BlockBox(Key);
	const auto Inside = [&Box](const P2& At) { return At.X >= Box.X0 && At.X < Box.X1 && At.Z >= Box.Z0 && At.Z < Box.Z1; };
	struct EndOf
	{
		KnotArm Arm;
		P2 At;
		double Height = 0.0;
		bool Used = false;
	};
	std::vector<EndOf> Ends;
	std::vector<int> Roads = {0};
	const auto It = Net->BlockEdges.find(Key);
	if (It != Net->BlockEdges.end())
		for (int E : It->second) Roads.push_back(E + 1);
	for (int W : Roads)
	{
		const SideRoad* Sd = W > 0 ? SideOf(W) : nullptr;
		if (W > 0 && Sd == nullptr) continue;
		const std::vector<RoadPoint>& Pts = RoadOf(W);
		if (Pts.size() < 3) continue;
		for (int End = 0; End < 2; ++End)
		{
			if (Sd != nullptr && Sd->Mouths[End].Is) continue;
			if (W > 0 && (WholeAt(W, End) >= 0 || WholeHidden(W))) continue;   // [smooth junctions stage 3] (its junction is built whole)
			const size_t I0 = End == 0 ? 0 : Pts.size() - 1;
			const RoadPoint& P = Pts[I0];
			if (P.Bridge || P.Tunnel || !Inside(P.At)) continue;
			// (its heading from a few metres in: the last step can be a stub)
			size_t I1 = I0;
			for (size_t Step = 1; Step < Pts.size(); ++Step)
			{
				I1 = End == 0 ? Step : Pts.size() - 1 - Step;
				if (Pts[I1].At.DistanceTo(P.At) >= 3.0) break;
			}
			const P2 D = Pts[I1].At - P.At;
			if (D.Length() < 1e-6) continue;
			EndOf En;
			En.Arm.Which = W;
			En.Arm.End = End;
			En.Arm.Away = D * (1.0 / D.Length());
			En.Arm.Half = P.Half;
			En.At = P.At;
			En.Height = P.Height;
			Ends.push_back(En);
		}
	}
	std::vector<std::shared_ptr<const Knot>> Made;
	for (size_t A = 0; A < Ends.size(); ++A)
	{
		if (Ends[A].Used) continue;
		std::vector<size_t> Group = {A};
		for (size_t B = A + 1; B < Ends.size(); ++B)
			if (!Ends[B].Used && Ends[B].At.DistanceTo(Ends[A].At) < 1.5 && std::fabs(Ends[B].Height - Ends[A].Height) < 1.5) Group.push_back(B);
		if (Group.size() < 2) continue;
		std::shared_ptr<Knot> K = std::make_shared<Knot>();
		K->Owner = 1 << 30;
		bool Main = false, Side = false;
		double Hmax = -1e18;
		for (size_t Gi : Group)
		{
			Ends[Gi].Used = true;
			K->Arms.push_back(Ends[Gi].Arm);
			K->At = K->At + Ends[Gi].At * (1.0 / double(Group.size()));
			Hmax = std::fmax(Hmax, Ends[Gi].Height);
			if (Ends[Gi].Arm.Which == 0) { Main = true; K->Height = Ends[Gi].Height; K->At = Ends[Gi].At; }
			else Side = true;
			if (Ends[Gi].Arm.Which < K->Owner) { K->Owner = Ends[Gi].Arm.Which; K->OwnerEnd = Ends[Gi].Arm.End; }
		}
		if (!Side) continue;
		if (Main)
		{
			for (size_t Gi : Group) if (Ends[Gi].Arm.Which == 0) K->At = Ends[Gi].At;
		}
		else K->Height = Hmax;
		std::sort(K->Arms.begin(), K->Arms.end(), [](const KnotArm& X, const KnotArm& Y) { return std::atan2(X.Away.Z, X.Away.X) < std::atan2(Y.Away.Z, Y.Away.X); });
		Made.push_back(K);
	}
	std::unique_lock<std::shared_mutex> Write(Net->Read);
	for (const std::shared_ptr<const Knot>& K : Made)
		for (const KnotArm& Arm : K->Arms) Net->Knots[static_cast<long long>(Arm.Which) * 2 + Arm.End] = K;
}

bool RidePlan::KnotPaved(const Knot& K) const
{
	for (const KnotArm& A : K.Arms)
	{
		if (A.Which > 0)
		{
			const SideRoad* Sd = SideOf(A.Which);
			if (Sd == nullptr || !Sd->Gravel) return true;
			continue;
		}
		const std::vector<RoadPoint>& Pts = RoadOf(0);
		const RoadFact* F = Pts.empty() ? nullptr : FactAt(A.End == 0 ? Pts.front().Along : Pts.back().Along);
		if (F == nullptr || !F->Gravel) return true;
	}
	return false;
}

void RidePlan::KnotPiece(const Knot& K, RideMesh& Out) const
{
	const double Pi = 3.14159265358979323846;
	const int Count = int(K.Arms.size());
	if (Count < 2) return;
	// [circuits, build 169] (where only a circuit's pieces meet, the junction's tarmac lies a centimetre under them in their
	// own black, only filling any gap between their ends: over them, 12 mm up in a road's grey, it was a grey patch
	// across the track and a little ramp at every join; beenster on 168: "so many bumps on the tracks")
	bool AllCircuit = true;
	for (const KnotArm& A : K.Arms) AllCircuit = AllCircuit && CircuitRoad(A.Which);
	const double Lift = AllCircuit ? -0.01 : 0.012;
	// The tarmac's height at a point: the surface of whichever of its roads it is on (or nearest the edge
	// of), as drawn, a hair over it.
	const auto Top = [this, &K, Lift](const P2& At)
	{
		double Best = 1e18, Y = K.Height;
		for (const KnotArm& A : K.Arms)
		{
			NearOpts Opts;
			Opts.OnlyRoad = A.Which;
			NearHit Hit;
			if (!NearestOf(At.X, At.Z, Opts, Hit)) continue;
			// (on the ride's road, that road's surface whatever else it is on: the others lie under it there)
			const double Score = A.Which == 0 && Hit.Dist < Hit.Half ? -1e9 : Hit.Dist - Hit.Half;
			if (Score >= Best) continue;
			Best = Score;
			Y = A.Which == 0 ? Hit.Height + Hit.Cross : JoinedHeight(A.Which, At, Hit.Height + Hit.Cross);
		}
		return Y + Lift;
	};
	static constexpr float RoadTarmac[3] = {0.022f, 0.022f, 0.025f}, VergeGrass[3] = {0.10f, 0.16f, 0.03f};
	static constexpr float CircuitTarmac[3] = {0.010f, 0.010f, 0.011f};   // ([circuits, build 169] RoadPiece's)
	// ([build 170, Hartsop] beenster: torn, patchy pieces where three gravel lanes meet: their junction was laid in
	// tarmac, and each lane grew a ragged tarmac tongue with loose stones out of it. Where only gravel lanes meet,
	// the junction is their gravel (RoadPiece's Grit), and they run into it whole.)
	static constexpr float KnotGrit[3] = {0.095f, 0.088f, 0.07f};
	const float* const Tarmac = AllCircuit ? CircuitTarmac : KnotPaved(K) ? RoadTarmac : KnotGrit;
	const auto Vertex = [&Out](const P2& At, double Y, const float* Colour)
	{
		Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
		Out.Nrm.push_back(0.0f); Out.Nrm.push_back(1.0f); Out.Nrm.push_back(0.0f);
		Out.Uv.push_back(float(At.X / 4.0)); Out.Uv.push_back(float(At.Z / 4.0));
		Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		return uint32_t(Out.Vertices() - 1);
	};
	// (a triangle, made to face up whichever way round its points came)
	const auto Tri = [&Out](uint32_t A, uint32_t B, uint32_t C)
	{
		const float* Pa = &Out.Pos[size_t(A) * 3], * Pb = &Out.Pos[size_t(B) * 3], * Pc = &Out.Pos[size_t(C) * 3];
		const double Up = double(Pb[2] - Pa[2]) * double(Pc[0] - Pa[0]) - double(Pb[0] - Pa[0]) * double(Pc[2] - Pa[2]);
		if (std::fabs(Up) < 1e-9) return;
		if (Up > 0.0) Out.Idx.insert(Out.Idx.end(), {A, B, C});
		else Out.Idx.insert(Out.Idx.end(), {A, C, B});
	};
	// [roundabouts] At a ring drawn as a circle: the corners onto it flared (RingFlare), as an entry's and exit's.
	bool Flared = false;
	for (const KnotArm& A : K.Arms)
	{
		const SideRoad* Sd = A.Which > 0 ? SideOf(A.Which) : nullptr;
		Flared = Flared || (Sd != nullptr && OnRoundRing(Sd->Edge));
	}
	// [roundabouts 156] (the flare as the ring's size: a 15 m corner on every ring made the mouths of small
	// ones very wide aprons of tarmac (PC check, build 155, York))
	double Flare = RingFlare;
	if (Flared)
	{
		const int Rg = PaintedRingNear(K.At, 4.0);
		if (Rg >= 0) Flare = std::clamp(RoadRings[size_t(Rg)].Radius * 0.3, RingFlareLeast, RingFlare);
		else Flare = RingFlareLeast;
	}
	// Round the junction: between each road and the next anticlockwise, a rounded corner where the gap
	// between them is less than a half turn, or a round outside (about the junction) where it is more.
	std::vector<P2> Rim, Outward;
	std::vector<char> Edged;   // (a corner or outside: a shoulder beyond it; across a road: none)
	for (int I = 0; I < Count; ++I)
	{
		const KnotArm& A = K.Arms[size_t(I)];
		const KnotArm& B = K.Arms[size_t((I + 1) % Count)];
		const P2 Qa(-A.Away.Z, A.Away.X), Qb(-B.Away.Z, B.Away.X);   // (each one's anticlockwise side)
		double Gap = std::atan2(B.Away.Z, B.Away.X) - std::atan2(A.Away.Z, A.Away.X);
		while (Gap <= 0.0) Gap += 2.0 * Pi;
		const P2 EdgeA = K.At + Qa * A.Half, EdgeB = K.At - Qb * B.Half;
		bool Done = false;
		// [OSM stage 5B] A fork (the two a narrow gap apart, as a roundabout's one-way arms where they part,
		// or an arm running onto the ring): no corner between them, just across from the one's edge to the
		// other's. The roads curve apart, so a corner along their headings would fill the nose between them.
		// Where they run on side by side (a slip road merging), the tarmac between their facing edges out
		// to where those part (the nose), so no ground shows between them.
		if (Gap < KnotFork)
		{
			// (each one's facing edge, out from the junction, while the other's tarmac is within a hair of it)
			const auto FacingEdge = [this](const KnotArm& Of, const KnotArm& Other, double Sign, std::vector<P2>& Into)
			{
				const std::vector<RoadPoint>& Pts = RoadOf(Of.Which);
				const int N = int(Pts.size());
				NearOpts Opts;
				Opts.OnlyRoad = Other.Which;
				for (int Kp = 1; Kp < N; ++Kp)
				{
					const int At = Of.End == 0 ? Kp : N - 1 - Kp;
					const int Prev = Of.End == 0 ? At - 1 : At + 1;
					const RoadPoint& P = Pts[size_t(At)];
					if (P.Along > Pts[size_t(Of.End == 0 ? 0 : N - 1)].Along + KnotNoseMost || P.Along < Pts[size_t(Of.End == 0 ? 0 : N - 1)].Along - KnotNoseMost) break;
					P2 D = P.At - Pts[size_t(Prev)].At;
					const double L = D.Length();
					if (L < 1e-9) continue;
					D = D * (1.0 / L);
					const P2 Edge = P.At + P2(-D.Z, D.X) * (Sign * P.Half);
					NearHit Hit;
					if (!NearestOf(Edge.X, Edge.Z, Opts, Hit) || Hit.Dist - Hit.Half > 0.25) break;
					Into.push_back(Edge);
				}
			};
			std::vector<P2> AlongA, AlongB;
			FacingEdge(A, B, 1.0, AlongA);
			FacingEdge(B, A, -1.0, AlongB);
			Rim.push_back(EdgeA);
			Outward.push_back(Qa);
			Edged.push_back(0);
			for (const P2& Pt : AlongA) { Rim.push_back(Pt); Outward.push_back(Qa); Edged.push_back(0); }
			for (size_t J = AlongB.size(); J-- > 0;) { Rim.push_back(AlongB[J]); Outward.push_back(Qb * -1.0); Edged.push_back(0); }
			Rim.push_back(EdgeB);
			Outward.push_back(Qb * -1.0);
			Edged.push_back(0);
			Done = true;
		}
		if (!Done && Gap < Pi - 0.05)
		{
			// Where the two facing edges meet: EdgeA + t A.Away = EdgeB + s B.Away.
			const double Det = A.Away.X * (-B.Away.Z) - A.Away.Z * (-B.Away.X);
			if (std::fabs(Det) > 1e-6)
			{
				const P2 D = EdgeB - EdgeA;
				const double T = (D.X * (-B.Away.Z) - D.Z * (-B.Away.X)) / Det;
				const double S = (A.Away.X * D.Z - A.Away.Z * D.X) / Det;
				if (T > -1.0 && S > -1.0 && T < (Flared ? 40.0 : 25.0) && S < (Flared ? 40.0 : 25.0))
				{
					const P2 X = EdgeA + A.Away * T;
					const double Reach = std::fmin(Flared ? Flare * 1.3 : 9.0, std::fmax(2.0, (Flared ? Flare : 6.0) / std::fmax(0.05, std::tan(Gap * 0.5))));
					// (each end of the curve on its road's edge as the road really runs: it may bend away)
					const auto ToEdge = [this](const KnotArm& Of, const P2& At)
					{
						NearOpts Opts;
						Opts.OnlyRoad = Of.Which;
						NearHit Hit;
						if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist < 0.5) return At;
						return Hit.Foot + (At - Hit.Foot) * (Hit.Half / Hit.Dist);
					};
					const P2 Pa = ToEdge(A, X + A.Away * Reach), Pb = ToEdge(B, X + B.Away * Reach);
					const int Segs = 6;
					for (int Sg = 0; Sg <= Segs; ++Sg)
					{
						const double U = double(Sg) / Segs;
						const P2 At = Pa * ((1.0 - U) * (1.0 - U)) + X * (2.0 * U * (1.0 - U)) + Pb * (U * U);
						const P2 Tangent = (X - Pa) * (1.0 - U) + (Pb - X) * U;
						P2 Normal(Tangent.Z, -Tangent.X);
						if (Dot(Normal, X - K.At) < 0.0) Normal = Normal * -1.0;
						const double Len = Normal.Length();
						Rim.push_back(At);
						Outward.push_back(Len > 1e-9 ? Normal * (1.0 / Len) : P2(0.0, 0.0));
						Edged.push_back(Sg < Segs ? 1 : 0);
					}
					Done = true;
				}
			}
		}
		if (!Done)
		{
			// (the outside: round about the junction from one road's edge to the next's)
			// (from the one's anticlockwise edge to the next's clockwise edge: the gap less a half turn)
			const double From = std::atan2(Qa.Z, Qa.X);
			const double Sweep = std::fmax(0.0, Gap - Pi);
			const int Segs = std::max(1, int(std::ceil(Sweep / (Pi / 8.0))));
			for (int Sg = 0; Sg <= Segs; ++Sg)
			{
				const double U = double(Sg) / Segs, Th = From + Sweep * U;
				const double R = A.Half + (B.Half - A.Half) * U;
				const P2 Dir(std::cos(Th), std::sin(Th));
				Rim.push_back(K.At + Dir * R);
				Outward.push_back(Dir);
				Edged.push_back(Sg < Segs ? 1 : 0);
			}
		}
	}
	const size_t N = Rim.size();
	if (N < 3) return;
	const uint32_t Hub = Vertex(K.At, Top(K.At), Tarmac);
	std::vector<uint32_t> RimV(N);
	std::vector<double> RimH(N);
	for (size_t I = 0; I < N; ++I)
	{
		RimH[I] = Top(Rim[I]);
		// [roundabouts 156] (where the rim runs across a road, level with that road, not the hair over it:
		// that 12 mm step's edge drew a thin dark seam across every mouth in the game (PC check, build 155))
		const bool Across = Edged[I] == 0 || (I > 0 && Edged[I - 1] == 0);
		RimV[I] = Vertex(Rim[I], Across && !AllCircuit ? RimH[I] - Lift : RimH[I], Tarmac);
	}
	// [roundabouts] (in rings between the hub and the rim, each point on the roads' surface there: one fan
	// from the middle out to a rim 20 m off rode up to 0.25 m over the roads between, a hump in the
	// junction, and over a roundabout's give-way lines)
	{
		static constexpr int Rounds = 3;
		std::vector<std::vector<uint32_t>> Rings(Rounds);
		for (int Rd = 1; Rd < Rounds; ++Rd)
			for (size_t I = 0; I < N; ++I)
			{
				const P2 At = K.At + (Rim[I] - K.At) * (double(Rd) / Rounds);
				Rings[size_t(Rd)].push_back(Vertex(At, Top(At), Tarmac));
			}
		Rings[0].assign(N, Hub);
		Rings.push_back(RimV);
		for (int Rd = 0; Rd < Rounds; ++Rd)
			for (size_t I = 0; I < N; ++I)
			{
				const size_t J = (I + 1) % N;
				if (Rd == 0) Tri(Hub, Rings[1][I], Rings[1][J]);
				else { Tri(Rings[size_t(Rd)][I], Rings[size_t(Rd) + 1][I], Rings[size_t(Rd) + 1][J]); Tri(Rings[size_t(Rd)][I], Rings[size_t(Rd) + 1][J], Rings[size_t(Rd)][J]); }
			}
	}
	// [OSM stage 5D] In a town (two of its roads or more town streets where they meet it), the kerb and
	// pavement round each corner as the streets' own, not the verge's shoulder (beenster Nov 14).
	int StreetArms = 0;
	for (const KnotArm& A : K.Arms)
	{
		if (A.Which == 0)
		{
			const size_t At = A.End == 0 ? 0 : Road.size() - 1;
			StreetArms += !BuiltUp.empty() && BuiltUp[std::min(At, BuiltUp.size() - 1)] != 0 ? 1 : 0;
			continue;
		}
		const SideRoad* Sd = SideOf(A.Which);
		if (Sd == nullptr || Sd->Track || Sd->Gravel || Sd->Edge < 0 || !StreetClass(Area.Roads.ClassOf(Sd->Edge))) continue;
		const std::vector<char> Along = StreetsAlong(A.Which);
		StreetArms += !Along.empty() && (A.End == 0 ? Along.front() : Along.back()) != 0 ? 1 : 0;
	}
	// [roundabouts round 2] No kerb, pavement or shoulder at a corner where it would lie on a road's
	// tarmac: at a roundabout the corners between arms often run over a splitter carriageway or the
	// next arm (beenster: "green lumps").
	NearOpts AnyRoad;
	AnyRoad.NoDecks = true;
	AnyRoad.NoBores = true;
	const auto OnTarmac = [&](const P2& P, double Y)
	{
		NearHit Hit;
		return NearestOf(P.X, P.Z, AnyRoad, Hit) && Hit.Dist < Hit.Half - 0.05 && std::fabs(Hit.Height - Y) < 1.5;
	};
	// [roundabouts round 4] Nor in a roundabout's mouth: within RingMouthClear of a ring's tarmac the
	// corners between arms and splitters left grass ticks and paving slabs on the road (beenster's circles).
	// ([156] a whole corner or none: a corner cut part way left its kerb and pavement ending in a tilted
	// slab by the small York ring's mouth (PC check, build 155))
	std::vector<char> CornerInMouth(N, 0);
	for (size_t I = 0; I + 1 < N;)
	{
		if (Edged[I] == 0) { ++I; continue; }
		size_t J = I;
		bool Near = false;
		while (J + 1 < N && Edged[J] != 0) { Near = Near || PaintedRingNear(Rim[J], RingCornerClear) >= 0 || PaintedRingNear(Rim[J + 1], RingCornerClear) >= 0; ++J; }
		for (size_t Q = I; Q < J; ++Q) CornerInMouth[Q] = Near ? 1 : 0;
		I = J;
	}
	const auto InRingMouth = [&](size_t I) { return CornerInMouth[I] != 0; };
	if (StreetArms >= 2)
	{
		static constexpr float KerbGrey[3] = {0.16f, 0.16f, 0.15f}, Paved[3] = {0.12f, 0.118f, 0.11f};
		static constexpr int Lines = 6;
		static constexpr double Outs[Lines] = {0.0, KerbHigh, KerbHigh + 0.15, PaveWide, PaveWide, PaveWide + CornerBack};
		static constexpr double Lifts[Lines] = {0.0, KerbHigh, KerbHigh, KerbHigh + 0.02, KerbHigh + 0.02, -ShoulderDrop};
		static constexpr float BackGrass[3] = {0.10f, 0.16f, 0.03f};
		const float* Looks[Lines - 1] = {KerbGrey, KerbGrey, Paved, nullptr, BackGrass};
		for (size_t I = 0; I + 1 < N; ++I)
		{
			if (Edged[I] == 0) continue;
			const size_t J = I + 1;
			if (InRingMouth(I)) continue;
			if (OnTarmac(Rim[I] + Outward[I] * PaveWide, RimH[I]) || OnTarmac(Rim[J] + Outward[J] * PaveWide, RimH[J])) continue;
			for (int Ln = 0; Ln + 1 < Lines; ++Ln)
			{
				if (Looks[Ln] == nullptr) continue;
				const uint32_t In0 = Vertex(Rim[I] + Outward[I] * Outs[Ln], RimH[I] - Lift + Lifts[Ln], Looks[Ln]), In1 = Vertex(Rim[J] + Outward[J] * Outs[Ln], RimH[J] - Lift + Lifts[Ln], Looks[Ln]);
				// ([build 155] the pavement's back down to just under the ground as drawn)
				const auto OutY = [&](size_t V)
				{
					const double Y = RimH[V] - Lift + Lifts[Ln + 1];
					if (Ln + 2 != Lines) return Y;
					const P2 P = Rim[V] + Outward[V] * Outs[Ln + 1];
					const double Drawn = LandDrawnAt(P.X, P.Z);
					return std::isnan(Drawn) ? Y : std::clamp(Drawn - MeetUnder, Y, RimH[V] - Lift + KerbHigh + 0.02);
				};
				const uint32_t Out0 = Vertex(Rim[I] + Outward[I] * Outs[Ln + 1], OutY(I), Looks[Ln]), Out1 = Vertex(Rim[J] + Outward[J] * Outs[Ln + 1], OutY(J), Looks[Ln]);
				Tri(In0, In1, Out1); Tri(In0, Out1, Out0);
			}
		}
		return;
	}
	// A shoulder round each corner and outside, as the roads' own: nearly level, then down into the ground.
	for (size_t I = 0; I + 1 < N; ++I)
	{
		if (Edged[I] == 0) continue;
		const size_t J = I + 1;
		if (InRingMouth(I)) continue;
		{
			const P2 MidA = Rim[I] + Outward[I] * ShoulderFlat, MidB = Rim[J] + Outward[J] * ShoulderFlat;
			const P2 OutA = Rim[I] + Outward[I] * Shoulder, OutB = Rim[J] + Outward[J] * Shoulder;
			if (OnTarmac(MidA, RimH[I]) || OnTarmac(MidB, RimH[J]) || OnTarmac((OutA + OutB) * 0.5, (RimH[I] + RimH[J]) * 0.5)) continue;
		}
		const uint32_t In0 = Vertex(Rim[I], RimH[I] - Lift, VergeGrass), In1 = Vertex(Rim[J], RimH[J] - Lift, VergeGrass);
		const uint32_t Mid0 = Vertex(Rim[I] + Outward[I] * ShoulderFlat, RimH[I] - Lift - ShoulderDip, VergeGrass), Mid1 = Vertex(Rim[J] + Outward[J] * ShoulderFlat, RimH[J] - Lift - ShoulderDip, VergeGrass);
		const uint32_t Out0 = Vertex(Rim[I] + Outward[I] * Shoulder, RimH[I] - Lift - ShoulderDrop, VergeGrass), Out1 = Vertex(Rim[J] + Outward[J] * Shoulder, RimH[J] - Lift - ShoulderDrop, VergeGrass);
		Tri(In0, In1, Mid1); Tri(In0, Mid1, Mid0); Tri(Mid0, Mid1, Out1); Tri(Mid0, Out1, Out0);
	}
}

double RidePlan::MouthCut(int Which, int End) const
{
	// [smooth junctions 3b] A side road's mouth on the ride's road drawn as one piece with its corners, from the ride's
	// road's edge out to where both corners have met the side road's edges: no fan over the two roads, a hair above them
	// (a 2 to 4 cm lip a tyre felt turning in or out, Hawes). Not a roundabout's, nor a through road's arm (a far edge).
	if (!WholeJunctions || Which <= 0 || End < 0 || End > 1) return -1.0;
	const SideRoad* Sd = SideOf(Which);
	if (Sd == nullptr || Sd->Pts.size() < 2) return -1.0;
	const Mouth& M = Sd->Mouths[End];
	if (!M.Is || M.Ring || M.Flare || M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size()) return -1.0;
	if (Sd->Edge >= 0 && FarEdges.count(static_cast<long long>(Sd->Edge) * 2 + End) > 0) return -1.0;
	const double Cut = std::fmax(M.Clear[0], M.Clear[1]) + 0.3;
	// ([smooth junctions stage 5c] up to half way where its other end has a mouth too; else up to 90% of it: a sharp
	// corner's long clear on a short road left the old fan, mouth and road at odds by 2 to 9 cm, Hawes)
	const double Most = Sd->Mouths[1 - End].Is ? 0.45 : 0.9;
	if (Cut > Sd->Length * Most || Cut > M.From + 25.0) return -1.0;
	return Cut;
}

void RidePlan::MouthCorner(const Mouth& M, const P2& MainAt, double Half, double Sigma, P2& Corner, P2& AlongMain, double& Reach)
{
	// The side road's edge, followed back to the ride's road's edge (a line, near enough, by the mouth).
	const P2 Left(M.Away.Z, -M.Away.X);
	const P2 Edge = M.At + Left * (Sigma * Half);
	const double Square = std::fmax(0.4, Dot(M.Away, M.Across));
	Corner = Edge + M.Away * ((M.MainHalf - Dot(Edge - MainAt, M.Across)) / Square);
	// Along the ride's road's edge, off from the mouth on this side.
	AlongMain = Dot(M.Along, Left) * Sigma >= 0.0 ? M.Along : M.Along * -1.0;
	// A 6 m radius: the rounding starts further back the sharper the corner (2 to 9 m). [roundabouts] Onto
	// a ring drawn as a circle, flared: a 10 m radius (to 12 m back; the ring curves away from the straight
	// the corner is drawn along, so no further).
	const double Angle = std::acos(std::fmin(1.0, std::fmax(-1.0, Dot(AlongMain, M.Away))));
	const double Radius = M.Flare ? 10.0 : 6.0, Most = M.Flare ? 12.0 : 9.0;
	Reach = std::fmin(Most, std::fmax(2.0, Radius / std::fmax(0.05, std::tan(Angle * 0.5))));
}

bool RidePlan::OnOtherRoad(const P2& P, int Which, double MainAlong, double Margin) const
{
	for (const int Sw : Planned)
	{
		if (Sw == Which) continue;
		const SideRoad* Sr = SideOf(Sw);
		if (Sr == nullptr) continue;
		bool Near = false;
		for (const Mouth& Mo : Sr->Mouths)
			Near = Near || (Mo.Is && Mo.MainIndex >= 0 && size_t(Mo.MainIndex) < Road.size() && std::fabs(Road[size_t(Mo.MainIndex)].Along - MainAlong) < 30.0);
		if (!Near) continue;
		NearOpts Opts;
		Opts.OnlyRoad = Sw;
		NearHit Hit;
		if (NearestOf(P.X, P.Z, Opts, Hit) && !Hit.Deck && !Hit.Bore && Hit.Dist < Hit.Half + Margin) return true;
	}
	return false;
}

bool RidePlan::FitCorner(const Mouth& M, int Which, const P2& Corner, const P2& AlongMain, double& Reach) const
{
	if (M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size()) return true;
	const double MainAlong = Road[size_t(M.MainIndex)].Along;
	for (int Try = 0; Try < 8; ++Try)
	{
		const P2 OnMain = Corner + AlongMain * Reach, OnSide = Corner + M.Away * Reach;
		bool Crosses = false;
		for (const double U : {0.0, 0.2, 0.4, 0.6})
		{
			const P2 P = OnMain * ((1.0 - U) * (1.0 - U)) + Corner * (2.0 * U * (1.0 - U)) + OnSide * (U * U);
			if (OnOtherRoad(P, Which, MainAlong, 0.3)) { Crosses = true; break; }
		}
		if (!Crosses) return true;
		if (Reach <= 1.0) return false;
		Reach = std::fmax(1.0, Reach * 0.7);
	}
	return false;
}

double RidePlan::Ground(double X, double Z, double Coarse) const
{
	return GroundOf(X, Z, Coarse, Coarse > 8.5);
}

double RidePlan::GroundOf(double X, double Z, double Coarse, bool NoBores, bool Trough) const
{
	const double Lie = GroundUncut(X, Z, Coarse, NoBores, Trough);
	// [ferries] (cut down along a slipway's approach and the slipway itself, so the tarmac shows and can be ridden;
	// last, as the ride's road's bench beside it would fill it again)
	return std::isnan(Lie) || FerryLays.empty() ? Lie : FerryCut(X, Z, Lie);
}

double RidePlan::GroundUncut(double X, double Z, double Coarse, bool NoBores, bool Trough) const
{
	double Lie = Land ? Land->Height(X, Z) : std::nan("");
	// A railway's cutting or bank (Ride UK stage 5): its bed level 5 m either side of it, the land
	// sloping 2 in 3 from there to meet it (not under a railway bridge, nor over its tunnel).
	double RailDist = 0.0, RailTop = 0.0;
	int RailOn = 0;
	double RailRoom = 1e9;
	const bool RailBed = !std::isnan(Lie) && NearRail(X, Z, RailDist, RailTop, RailOn, nullptr, &RailRoom) && (RailOn == 0 || RailOn == 3) && RailDist <= RailRoom;
	const double RailLevel = RailTop - 0.47, RailRise = std::fmax(0.0, RailDist - RailBedHalf) / 1.5;
	if (RailBed) Lie = std::fmin(std::fmax(Lie, RailLevel - RailRise), RailLevel + RailRise);
	// [car parks, build 157 feedback] (a car park's ground laid level, its banks eased back to the land)
	if (!std::isnan(Lie) && !CarParks.empty()) Lie = CarParkLevel(X, Z, Lie);
	// [build 147 feedback] Water's channels and beds (World/WorldRideWater.cpp): a river's, stream's or
	// canal's bed under its level, a lip just over the level at its edge, its bank rising BankRun across
	// for each metre up to the land; a lake's bed under its level; the sea's under the sea.
	if (!std::isnan(Lie))
	{
		double WDist = 0.0, WSurf = 0.0, WHalf = 0.0, WDepth = 0.0;
		bool Beside = true;
		// ([real water, Nov 25] not under a building: the map puts Blairgowrie's mills on Bank Street over the
		// Ericht's outline, and the channel cut under them stood them in the river to their doors)
		if (NearWater(X, Z, WDist, WSurf, WHalf, WDepth, nullptr, &Beside) && !WaterHouseAt(X, Z))
		{
			if (WDist < WHalf)
			{
				const double U = WDist / WHalf;
				Lie = std::fmin(Lie, WSurf + 0.05 - (WDepth + 0.05) * (1.0 - U * U));
			}
			else
			{
				const double Out = WDist - WHalf;
				Lie = std::fmin(Lie, WSurf + 0.06 + Out / BankRun);
				if (Out < 1.2) Lie = std::fmax(Lie, WSurf + 0.06 + Out * 0.15);
			}
			// [build 155 feedback] (beenster: "I still see brooks represented as floating rectangles": a
			// brook's channel is narrower than the near land's 4 m squares, which drew the field straight
			// across it at its banks' height, the water showing through only here and there.) The near
			// squares' points by a watercourse lie under its water, and BanksTile draws its banks and
			// channel in the land's own colours over them, finely; only for the near squares' own heights.
			if (Trough && Beside && WDist < WHalf + Coarse * BankTrough) Lie = std::fmin(Lie, WSurf - 0.3);
		}
		// [build 155 feedback] (beenster: "Lakesides and river banks and edges seem rough edged".) A lake's
		// bed, and the sea's, came in one step at its outline as the 4 m squares (and the sea's map pixels)
		// fall, so its shore was a staircase. Now how much of the ground round a point is lake (or sea), out
		// to ShoreReach, eases the land from its own lie down to the bed: the shore follows the outline
		// smoothly, a steep bank in a slope, a gentle one on the flat.
		const auto Share = [X, Z](const auto& In)
		{
			int Hits = In(X, Z) ? 1 : 0, Count = 1;
			for (const double R : {ShoreReach * 0.5, ShoreReach})
				for (int K = 0; K < 8; ++K)
				{
					const double A = K * 0.7853981633974483 + (R > ShoreReach * 0.75 ? 0.3926990816987241 : 0.0);
					Hits += In(X + std::cos(A) * R, Z + std::sin(A) * R) ? 1 : 0;
					++Count;
				}
			return double(Hits) / Count;
		};
		const auto Ease = [](double F) { const double T = std::clamp((F - 0.15) / 0.7, 0.0, 1.0); return T * T * (3.0 - 2.0 * T); };
		{
			double Level = StillLevel(X, Z);
			bool Near = !std::isnan(Level);
			for (int K = 0; K < 4 && !Near; ++K)
			{
				const double S = StillLevel(X + (K == 0 ? ShoreReach : K == 1 ? -ShoreReach : 0.0), Z + (K == 2 ? ShoreReach : K == 3 ? -ShoreReach : 0.0));
				if (!std::isnan(S)) { Level = S; Near = true; }
			}
			if (Near)
			{
				const double W = Ease(Share([this](double Px, double Pz) { return !std::isnan(StillLevel(Px, Pz)); }));
				Lie = std::fmin(Lie, Lie + (Level - 0.6 - Lie) * W);
			}
		}
		if (Lie < 12.0)
		{
			const bool Here = Land->SeaAt(X, Z);
			bool Mixed = false;
			for (int K = 0; K < 4 && !Mixed; ++K)
				Mixed = Land->SeaAt(X + (K == 0 ? ShoreReach : K == 1 ? -ShoreReach : 0.0), Z + (K == 2 ? ShoreReach : K == 3 ? -ShoreReach : 0.0)) != Here;
			if (Mixed) Lie = std::fmin(Lie, Lie + (-0.9 - Lie) * Ease(Share([this](double Px, double Pz) { return Land->SeaAt(Px, Pz); })));
			else if (Here) Lie = std::fmin(Lie, -0.9);
		}
	}
	NearOpts Opts;
	Opts.NoBores = NoBores;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit)) return Lie;
	// Under a bridge the land lies as it does (the river's banks, not a causeway), well clear of the
	// deck; if another road runs under it, that road's bench.
	double Clear = std::nan("");
	if (Hit.Deck && !std::isnan(Lie))
	{
		Clear = Hit.Height + Hit.Cross - 1.5;
		Opts.NoDecks = true;
		Opts.NotRoad = Hit.Which;
		if (!NearestOf(X, Z, Opts, Hit)) return std::fmin(Lie, Clear);
	}
	// [OSM stage 5C] Between a dual carriageway's halves, the reservation's ground: straight across from
	// the one's edge to the other's (each road's own bench would rise over the other's edge where the two
	// lie at different heights).
	if (!std::isnan(Lie))
	{
		const double Median = MedianGround(Hit);
		if (!std::isnan(Median)) return std::isnan(Clear) ? Median : std::fmin(Median, Clear);
	}
	// (the bench lies under the road's surface as it is tilted, and level with its edge beyond)
	double Height = Hit.Height + Hit.Cross - Hit.Fall * std::fmin(FallOn, std::fmax(0.0, Hit.Dist - Hit.Half));
	// [roundabouts] (on and by a ring drawn as a circle, under the roads as drawn, each settled onto the
	// one it joins: the ground came up through the ring's tarmac where an edge was eased onto its neighbour)
	if (Hit.Which > 0 && !Hit.Deck && Hit.Dist <= Hit.Half + 2.0 && PaintedRingNear(P2(X, Z), 3.0) >= 0)
		Height = std::fmin(Height, JoinedHeight(Hit.Which, P2(X, Z), Hit.Height + Hit.Cross));
	const double Wider = std::fmax(0.0, Coarse - 4.0);
	const double Flat = Hit.Half + Bench + Wider;
	// ([build 154] beenster: "the grass needs to meet the road on the same plane... Buttertubs is how I want
	// it": the ground 10 cm under the road all across its bench left the road's grassed shoulder standing
	// proud of the field as a strip. Now the ground rises from under the tarmac to just under the shoulder's
	// level, FlushSink, within 0.6 m of the edge, so the field and the road's own grass lie as one surface.)
	const double ToFlush = std::clamp((Hit.Dist - Hit.Half) / 0.6, 0.0, 1.0);
	const double Under = Height - (Sink + (FlushSink - Sink) * ToFlush) - std::fmin(0.45, Wider * 0.06);
	double Out = Lie;
	// [OSM stage 5E] The bank from there back to the land: wider the more height it makes up (as the
	// Godot game's road_net.gd has it), so a deep cutting or a high embankment is a slope, not a wall;
	// where the map marks an embankment or a cutting, an engineered bank instead: a straight 1 in 2
	// batter with a clean top and toe. (Never past the reach NearestOf looks for roads in.)
	const double Rise = std::isnan(Lie) ? 0.0 : std::fabs(Lie - Under);
	const double Room = std::fmax(Blend, NearestReach - 1.0 - Flat);
	const bool Engineered = Hit.Banked != 0;
	const double Bank = Engineered ? std::clamp(Rise * BatterRun, BatterLeast, Room) : std::clamp(Blend + Rise * BankWiden, Blend, std::fmin(BankMost, Room));
	if (Hit.Dist <= Flat || std::isnan(Lie)) Out = Hit.Dist <= Flat + Bank ? Under : Lie;
	else if (Hit.Dist < Flat + Bank)
	{
		const double T = (Hit.Dist - Flat) / Bank;
		Out = Under + (Lie - Under) * (Engineered ? 0.75 * T + 0.25 * Smooth01(T) : Smooth01(T));
	}
	// [OSM stage 5C] Where two roads' tarmac overlaps (lanes merging, a slip road joining), under the lower
	// of the two a few centimetres apart, so the land shows through neither.
	// (not on the ride's own road: the others lie just under it there)
	if (Hit.Dist <= Flat && Wider <= 0.0 && !Hit.Deck && !Hit.Bore && !(Hit.Which == 0 && Hit.Dist <= Hit.Half))
	{
		NearOpts Other = Opts;
		Other.NotRoad = Hit.Which;
		NearHit Also;
		if (NearestOf(X, Z, Other, Also) && !Also.Deck && !Also.Bore && Also.Dist <= Also.Half + 2.0)
		{
			// (its surface as drawn: a side road settled onto a greater one where it joins it)
			const double Drawn = Also.Which == 0 ? Also.Height + Also.Cross : JoinedHeight(Also.Which, P2(X, Z), Also.Height + Also.Cross);
			const double Low = Drawn - Sink;
			// ([true T] well on the other's tarmac, under it by any step; and on a wider road's bench beside a
			// narrower one higher up (2 m out), its edge's level: Buxworth, a track's bench 0.8 m above New Road beside it
			// stood the coarse ground over New Road's lane by the junction; and in a junction's corner, by both
			// roads' edges, under the lower: the corner's tarmac runs between them)
			if (Low < Out && ((Out - Low < OverlapStep && Also.Dist <= Also.Half + 0.5) || Also.Dist <= Also.Half - 0.3 || Also.Half > Hit.Half + 0.5 || (Also.Dist <= Also.Half + 2.0 && Hit.Dist <= Hit.Half + 2.0))) Out = Low;
		}
	}
	// [roundabouts round 3] (beenster: "still a little too much grass mess") By a ring the roads crowd in
	// (flared mouths, splitter carriageways, the ring itself, each at its own height): under every road
	// whose tarmac or edge is here, the lowest, by any step, so the land comes up through none of them.
	if (Hit.Dist <= Flat + 1.0 && !Hit.Deck && !Hit.Bore && PaintedRingNear(P2(X, Z), 25.0) >= 0)
	{
		NearOpts Other = Opts;
		Other.NotRoad = Hit.Which;
		Other.NoDecks = true;
		NearHit Also;
		if (NearestOf(X, Z, Other, Also) && Also.Dist <= Also.Half + 1.5)
		{
			const double Drawn = Also.Which == 0 ? Also.Height + Also.Cross : JoinedHeight(Also.Which, P2(X, Z), Also.Height + Also.Cross);
			Out = std::fmin(Out, Drawn - Sink - (Also.Dist > Also.Half ? 0.05 : 0.0));
		}
		const double Own = Hit.Which == 0 ? Hit.Height + Hit.Cross : JoinedHeight(Hit.Which, P2(X, Z), Hit.Height + Hit.Cross);
		if (Hit.Dist <= Hit.Half + 1.0) Out = std::fmin(Out, Own - Sink);
	}
	// (beenster, build 147, Whaley Bridge: a road's bank cut away the bank a railway beside it stands on,
	// leaving the line in the air. Off the road's bench the railway's own bank and cutting win.)
	if (RailBed && Hit.Dist > Flat + 0.3) Out = std::fmax(Out, RailLevel - RailRise);
	// [smooth junctions 3b] In a side road's mouth on the ride's road drawn whole (MouthCut), under the lower of the two
	// roads: the piece's corners run between them at a blend of their heights, and the ground benched to the nearer one
	// came up through it 5 cm in a fork's corner (Keswick; Hartsop's dirt patch where the lane meets the tarmac). By
	// 30 cm at most, so the field beside a mouth is not dug out where the two roads lie far apart in height.
	if (WholeJunctions && Wider <= 0.0 && !Hit.Deck && !Hit.Bore && Hit.Dist <= Hit.Half + MouthGroundReach && !std::isnan(Out))
	{
		NearHit H0, Hs;
		bool Got0 = Hit.Which == 0, GotS = Hit.Which > 0;
		if (Got0) H0 = Hit; else if (GotS) Hs = Hit;
		NearOpts Other = Opts;
		Other.NoDecks = true;
		if (!Got0) { Other.OnlyRoad = 0; Got0 = NearestOf(X, Z, Other, H0) && !H0.Deck && !H0.Bore; }
		else { Other.NotRoad = 0; GotS = NearestOf(X, Z, Other, Hs) && Hs.Which > 0 && !Hs.Deck && !Hs.Bore; }
		const SideRoad* Sr = GotS ? SideOf(Hs.Which) : nullptr;
		if (Got0 && Sr != nullptr && H0.Dist <= H0.Half + MouthGroundReach && Hs.Dist <= Hs.Half + MouthGroundReach)
		{
			const int End = Hs.Along < Sr->Length * 0.5 ? 0 : 1;
			const double Cut = MouthCut(Hs.Which, End);
			const double FromEnd = End == 0 ? Hs.Along : Sr->Length - Hs.Along;
			if (Cut > 0.0 && FromEnd <= Cut + 1.0)
			{
				const double Low = std::fmin(H0.Height + H0.Cross, JoinedHeight(Hs.Which, P2(X, Z), Hs.Height + Hs.Cross)) - Sink;
				Out = std::fmax(std::fmin(Out, Low), Out - 0.3);
			}
		}
	}
	Out = WholeGround(X, Z, Out);   // [smooth junctions stage 3]
	return std::isnan(Clear) ? Out : std::fmin(Out, Clear);
}

// [smooth junctions stage 3] (WorldRideWhole.cpp's: here, where the network's insides are known)
std::shared_ptr<const RidePlan::WholeShape> RidePlan::WholeShapeOf(int Ji) const
{
	if (Net == nullptr) return nullptr;
	{
		std::lock_guard<std::mutex> Hold(Net->Shaping);
		const auto It = Net->Shapes.find(Ji);
		if (It != Net->Shapes.end()) return It->second;
	}
	// (made outside the lock, from the roads as laid: none until every arm is laid, and then kept)
	std::shared_ptr<WholeShape> Made = std::make_shared<WholeShape>();
	RideMesh Unused;
	WholePiece(Ji, Unused, Made.get());
	if (Made->Rim.size() < 3 || Made->H.empty())
	{
		// ([smooth junctions 4b] a ring's piece lays its own roads first: one that can't be drawn never will be)
		if (size_t(Ji) < JunctionPlaces.size() && JunctionPlaces[size_t(Ji)].Made)
		{
			std::lock_guard<std::mutex> Hold(Net->Shaping);
			Net->Shapes.emplace(Ji, nullptr);
		}
		return nullptr;
	}
	std::lock_guard<std::mutex> Hold(Net->Shaping);
	return Net->Shapes.emplace(Ji, Made).first->second;
}

double RidePlan::RoadHeight(double X, double Z) const
{
	double Dist = 0.0, Height = 0.0, Half = 0.0, Cross = 0.0;
	return Nearest(X, Z, Dist, Height, nullptr, &Half, nullptr, false, nullptr, &Cross) && Dist <= Half ? Height + Cross : std::nan("");
}

double RidePlan::LandDrawnAt(double X, double Z, std::unordered_map<long long, double>* Cache) const
{
	const double Fi = std::floor(X / LandGrid), Fj = std::floor(Z / LandGrid);
	const long long I = static_cast<long long>(Fi), J = static_cast<long long>(Fj);
	const auto At = [this, Cache](long long Gi, long long Gj)
	{
		const long long Key = Gi * 4000037LL + Gj;
		if (Cache != nullptr)
		{
			const auto It = Cache->find(Key);
			if (It != Cache->end()) return It->second;
		}
		const double Y = LandGround(double(Gi) * LandGrid, double(Gj) * LandGrid, LandGrid);
		if (Cache != nullptr) (*Cache)[Key] = Y;
		return Y;
	};
	const double Fx = X / LandGrid - Fi, Fz = Z / LandGrid - Fj;
	// (as LandTile lays a cell: A B across the top, C D below, triangles A C B and B C D)
	const double Ha = At(I, J), Hb = At(I + 1, J), Hc = At(I, J + 1), Hd = At(I + 1, J + 1);
	return Fx + Fz <= 1.0 ? Ha + Fx * (Hb - Ha) + Fz * (Hc - Ha) : Hd + (1.0 - Fx) * (Hc - Hd) + (1.0 - Fz) * (Hb - Hd);
}

void RidePlan::LandColourDrawnAt(double X, double Z, float* Out, std::unordered_map<long long, std::array<float, 3>>* Cache) const
{
	const double Fi = std::floor(X / LandGrid), Fj = std::floor(Z / LandGrid);
	const long long I = static_cast<long long>(Fi), J = static_cast<long long>(Fj);
	const auto At = [this, Cache](long long Gi, long long Gj)
	{
		const long long Key = Gi * 4000037LL + Gj;
		if (Cache != nullptr)
		{
			const auto It = Cache->find(Key);
			if (It != Cache->end()) return It->second;
		}
		// (as LandTile colours its points: over 12 m round, by the slope across its neighbours)
		const double Gx = double(Gi) * LandGrid, Gz = double(Gj) * LandGrid;
		const double E = LandGround(Gx + LandGrid, Gz, LandGrid), W = LandGround(Gx - LandGrid, Gz, LandGrid), Sn = LandGround(Gx, Gz + LandGrid, LandGrid), Nn = LandGround(Gx, Gz - LandGrid, LandGrid);
		const double Sx = std::isnan(E) || std::isnan(W) ? 0.0 : (E - W) / (2.0 * LandGrid), Sz = std::isnan(Sn) || std::isnan(Nn) ? 0.0 : (Sn - Nn) / (2.0 * LandGrid);
		std::array<float, 3> C{};
		LandColour(Gx, Gz, 12.0, std::sqrt(Sx * Sx + Sz * Sz), C.data());
		HouseFootTone(C.data(), HouseFootAt(Gx, Gz));   // [house foot] (as LandTile tones its finest points)
		if (Cache != nullptr) (*Cache)[Key] = C;
		return C;
	};
	const double Fx = X / LandGrid - Fi, Fz = Z / LandGrid - Fj;
	const std::array<float, 3> Ca = At(I, J), Cb = At(I + 1, J), Cc = At(I, J + 1), Cd = At(I + 1, J + 1);
	for (int K = 0; K < 3; ++K)
		Out[K] = float(Fx + Fz <= 1.0 ? Ca[size_t(K)] + Fx * (Cb[size_t(K)] - Ca[size_t(K)]) + Fz * (Cc[size_t(K)] - Ca[size_t(K)])
			: Cd[size_t(K)] + (1.0 - Fx) * (Cc[size_t(K)] - Cd[size_t(K)]) + (1.0 - Fz) * (Cb[size_t(K)] - Cd[size_t(K)]));
}

void RidePlan::LandTile(double X0, double Z0, double Size, int Cells, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	const int N = Cells + 1;
	const double Step = Size / Cells;
	// [ride anywhere] The network's roads over a near square (and a little round it) laid before its
	// ground is cut for them; a far one has whatever roads are laid already.
	if (Step <= 8.0)
	{
		Rect Around;
		Around.X0 = X0 - 64.0; Around.Z0 = Z0 - 64.0; Around.X1 = X0 + Size + 64.0; Around.Z1 = Z0 + Size + 64.0;
		LayBlocks(Around);
	}
	// Heights one cell past the edge each way, for the slopes at the edge.
	const int M = N + 2;
	std::vector<double> H(size_t(M) * M);
	double Fallback = 0.0;
	bool Any = false;
	for (int J = 0; J < M; ++J)
		for (int I = 0; I < M; ++I)
		{
			const double Y = LandGround(X0 + (I - 1) * Step, Z0 + (J - 1) * Step, Step);
			H[size_t(J) * M + I] = Y;
			if (!std::isnan(Y) && !Any) { Fallback = Y; Any = true; }
		}
	if (!Any) return;   // (off the world's edge: nothing to draw)
	// (where the ride's heights end, far out past the skyline's reach, the square stops: no quad or
	// skirt on a point with no height)
	std::vector<char> Known(size_t(N) * N, 1);
	for (int J = 0; J < N; ++J)
		for (int I = 0; I < N; ++I) Known[size_t(J) * N + I] = std::isnan(H[size_t(J + 1) * M + (I + 1)]) ? 0 : 1;
	for (double& Y : H) if (std::isnan(Y)) Y = Fallback;
	const auto At = [&H, M](int I, int J) { return H[size_t(J + 1) * M + (I + 1)]; };
	const auto Put = [&Out](double X, double Y, double Z, double Nx, double Ny, double Nz, double U, double V, const float* Colour)
	{
		Out.Pos.push_back(float(X)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z));
		Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
		Out.Uv.push_back(float(U)); Out.Uv.push_back(float(V));
		Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
	};
	for (int J = 0; J < N; ++J)
		for (int I = 0; I < N; ++I)
		{
			const double Sx = (At(I + 1, J) - At(I - 1, J)) / (2.0 * Step), Sz = (At(I, J + 1) - At(I, J - 1)) / (2.0 * Step);
			const double Len = std::sqrt(Sx * Sx + 1.0 + Sz * Sz);
			float Colour[3];
			LandColour(X0 + I * Step, Z0 + J * Step, std::fmax(12.0, Step), std::sqrt(Sx * Sx + Sz * Sz), Colour);
			Put(I * Step, At(I, J), J * Step, -Sx / Len, 1.0 / Len, -Sz / Len, (X0 + I * Step) / 8.0, (Z0 + J * Step) / 8.0, Colour);
		}
	// [house foot, Nov 22] (beenster, Uig on 168: houses "dropped onto" the land) The ground round every building
	// darker and lusher, fading out over HouseFootReach: each building's points near it toned, the strongest
	// building's at each point (as HouseFootAt, which the land's shoulders read, so they match). The near
	// squares only (8 m or finer): further out a point is too coarse for a building's foot.
	if (Step <= 8.5 && Scene && HouseBlend > 0.0)
	{
		std::vector<Building> Feet;
		HouseFootBoxes(X0, Z0, Size, Feet);
		std::vector<float> Foot(size_t(N) * N, 0.0f);
		bool AnyFoot = false;
		for (const Building& B : Feet)
		{
			const double Span = 0.5 * (B.W + B.D) + HouseFootReach;
			const int I0 = std::max(0, int(std::floor((B.X - Span - X0) / Step))), I1 = std::min(N - 1, int(std::ceil((B.X + Span - X0) / Step)));
			const int J0 = std::max(0, int(std::floor((B.Z - Span - Z0) / Step))), J1 = std::min(N - 1, int(std::ceil((B.Z + Span - Z0) / Step)));
			if (I0 > I1 || J0 > J1) continue;
			const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
			for (int J = J0; J <= J1; ++J)
				for (int I = I0; I <= I1; ++I)
				{
					const double Dx = X0 + I * Step - B.X, Dz = Z0 + J * Step - B.Z;
					const double Ox = std::fmax(0.0, std::fabs(Dx * Cs - Dz * Sn) - B.W * 0.5), Oz = std::fmax(0.0, std::fabs(Dx * Sn + Dz * Cs) - B.D * 0.5);
					const double Off = std::hypot(Ox, Oz);
					if (Off >= HouseFootReach) continue;
					const double T = 1.0 - Off / HouseFootReach;
					float& F = Foot[size_t(J) * N + I];
					F = std::fmax(F, float(T * T * (3.0 - 2.0 * T)));
					AnyFoot = true;
				}
		}
		if (AnyFoot)
			for (size_t V = 0; V < Foot.size(); ++V)
				if (Foot[V] > 0.0f) HouseFootTone(&Out.Col[V * 3], double(Foot[V]) * HouseBlend);
	}
	const auto Quad = [&Out](uint32_t A, uint32_t B, uint32_t C, uint32_t D)
	{
		// A B across the top (x growing), C D below them (z growing): up-facing seen from above.
		Out.Idx.insert(Out.Idx.end(), {A, C, B, B, C, D});
	};
	for (int J = 0; J < Cells; ++J)
		for (int I = 0; I < Cells; ++I)
		{
			const uint32_t A = uint32_t(J * N + I), B = A + 1, C = uint32_t((J + 1) * N + I), D = C + 1;
			if (Known[A] && Known[B] && Known[C] && Known[D]) Quad(A, B, C, D);
		}
	// The skirt: each edge's points again, lower, joined to the edge.
	const double Drop = std::fmax(2.0, Step * 1.5);
	const auto Skirt = [&](int I0, int J0, int Di, int Dj, bool Flip)
	{
		const uint32_t Base = uint32_t(Out.Vertices());
		for (int K = 0; K < N; ++K)
		{
			const int I = I0 + Di * K, J = J0 + Dj * K;
			const size_t V = size_t(J * N + I) * 3;
			const float Colour[3] = {Out.Col[V], Out.Col[V + 1], Out.Col[V + 2]};
			Put(I * Step, At(I, J) - Drop, J * Step, Out.Nrm[V], Out.Nrm[V + 1], Out.Nrm[V + 2], (X0 + I * Step) / 8.0, (Z0 + J * Step) / 8.0, Colour);
		}
		for (int K = 0; K + 1 < N; ++K)
		{
			const uint32_t A = uint32_t((J0 + Dj * K) * N + I0 + Di * K), B = uint32_t((J0 + Dj * (K + 1)) * N + I0 + Di * (K + 1));
			const uint32_t C = Base + uint32_t(K), D = Base + uint32_t(K) + 1;
			if (!Known[A] || !Known[B]) continue;
			if (Flip) Out.Idx.insert(Out.Idx.end(), {A, B, C, B, D, C});
			else Out.Idx.insert(Out.Idx.end(), {A, C, B, B, C, D});
		}
	};
	Skirt(0, 0, 1, 0, true);            // north edge
	Skirt(0, Cells, 1, 0, false);       // south
	Skirt(0, 0, 0, 1, false);           // west
	Skirt(Cells, 0, 0, 1, true);        // east
	std::vector<float> Shore;   // [real water stage 2] (the land's UV1: its height over the water near it, the shore's kind)
	{
		std::shared_ptr<RideMesh> Wet = std::make_shared<RideMesh>();
		WaterTile(X0, Z0, Size, Cells, Out, *Wet, &Shore);
		if (Wet->Vertices() > 0) Out.Water = Wet;
	}
	const size_t SheetPoints = Out.Vertices();
	if (std::fabs(Step - LandGrid) < 0.1) BanksTile(X0, Z0, Size, Out);   // [build 155 feedback]
	{
		// [real water stage 4] The land's shore for its material (wet banks, beaches, the bed under the
		// water): the sheet's points as WaterTile found them, a river's banks (BanksTile's strips and the
		// sheet beside a run) by the run's own level.
		const size_t Grid = size_t(Cells + 1) * size_t(Cells + 1);
		bool AnyShore = !Shore.empty();
		std::vector<float> Uv1(Out.Vertices() * 2, 0.0f);
		if (!Shore.empty()) std::copy(Shore.begin(), Shore.begin() + std::min(Shore.size(), Grid * 2), Uv1.begin());
		if (Step <= 8.5 && !WaterNear.empty())
			for (size_t V = 0; V < Out.Vertices(); ++V)
			{
				if (V >= Grid && V < SheetPoints) continue;   // (the skirts, out of sight)
				if (Uv1[V * 2] != 0.0f) continue;
				const double X = Out.OriginX + Out.Pos[V * 3], Z = Out.OriginZ + Out.Pos[V * 3 + 2];
				double Wd = 0.0, Ws = 0.0, Wh = 0.0, Wdp = 0.0;
				if (!NearWater(X, Z, Wd, Ws, Wh, Wdp) || Wd > Wh + 10.0) continue;
				// (past the water's edge the land is never under it, however far below the stream's level a
				// hillside falls: it was drawn as wet bed, pale patches down the slope below a stream)
				double Over = double(Out.Pos[V * 3 + 1]) - Ws;
				if (Wd > Wh) Over = std::fmax(Over, Wd - Wh);
				Uv1[V * 2] = float(ShoreZero - std::clamp(Over, -ShoreZero + 1.0, ShoreZero));
				Uv1[V * 2 + 1] = float(ShoreBank * 10);
				AnyShore = true;
			}
		if (AnyShore) Out.Uv1 = std::move(Uv1);
	}
	if (Step <= 8.5 && Scene)
	{
		std::shared_ptr<RideMesh> Stands = std::make_shared<RideMesh>();
		SceneryTile(X0, Z0, Size, Step, *Stands);
		SkylineTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);   // [OSM stage 2 batch B]
		FieldsTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);    // [OSM stage 2 batch C]
		WildTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);      // [OSM stage 2 batch D]
		TowersTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);    // [towns stage 2]
		PlacesTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);    // [places stage 3]
		TownLifeTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);   // [roads stage 7]
		CarParksTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);   // [car parks] (the finest squares only)
		CircuitSandTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);   // [circuits] (the gravel traps' sand)
		RingsTile(X0, Z0, Size, Step <= 4.5 ? 2 : 1, *Stands);     // [OSM stage 5B] the roundabouts' islands
		PathsTile(X0, Z0, Size, Step, *Stands);                    // [OSM stage 4]
		if (Stands->Any()) Out.Extra = Stands;
		PlantsTile(X0, Z0, Size, Cells, Out, Out.Plants);
		// [build 149] (the hedges' models, which the scenery laid along their lines)
		if (!Stands->Plants.empty())
		{
			for (RidePlant P : Stands->Plants)
			{
				P.X += float(Stands->OriginX - Out.OriginX);
				P.Z += float(Stands->OriginZ - Out.OriginZ);
				Out.Plants.push_back(P);
			}
			Stands->Plants.clear();
		}
		AnimalsTile(X0, Z0, Size, Out.Animals);
		PropsTile(X0, Z0, Size, Out.Props);   // [OSM stage 2]
		// [fps plan step 2] (the houses' windows and doors, which the scenery placed as the facade kit's)
		for (RideProp P : Stands->Props)
		{
			P.X += float(Stands->OriginX - Out.OriginX);
			P.Z += float(Stands->OriginZ - Out.OriginZ);
			Out.Props.push_back(P);
		}
		Stands->Props.clear();
		// [warehouses, round 175 fps] (the warehouse kit's pieces, drawn as instances of the pieces' meshes)
		for (RideKitPart P : Stands->KitParts)
		{
			P.X += float(Stands->OriginX - Out.OriginX);
			P.Z += float(Stands->OriginZ - Out.OriginZ);
			Out.KitParts.push_back(P);
		}
		Stands->KitParts.clear();
	}
	else if (Scene)
	{
		// [OSM stage 2 batch B] (farther off, only the tall things, as far shapes)
		std::shared_ptr<RideMesh> Stands = std::make_shared<RideMesh>();
		Stands->OriginX = X0;
		Stands->OriginZ = Z0;
		SkylineTile(X0, Z0, Size, Step <= 70.0 ? 0 : -1, *Stands);
		WildTile(X0, Z0, Size, Step <= 70.0 ? 0 : -1, *Stands);   // [OSM stage 2 batch D] (the cliffs and dams)
		TowersTile(X0, Z0, Size, Step <= 70.0 ? 0 : -1, *Stands); // [towns stage 2] (the tall buildings)
		PlacesTile(X0, Z0, Size, Step <= 70.0 ? 0 : -1, *Stands); // [places stage 3] (the tall places)
		TownLifeTile(X0, Z0, Size, Step <= 70.0 ? 0 : -1, *Stands);   // [roads stage 7] (the canopies from further off)
		if (Step <= 70.0) FarBuildingsTile(X0, Z0, Size, Step, Step <= 20.0 ? 60.0 : 200.0, *Stands);   // [build 141] (the villages and towns far off)
		if (Stands->Any()) Out.Extra = Stands;
	}
}

double RidePlan::JoinedHeight(int Which, const P2& At, double Y) const
{
	return JoinedHeightDeep(Which, At, Y, JoinDepth);
}

double RidePlan::JoinedHeightDeep(int Which, const P2& At, double Y, int Depth) const
{
	// A side road where it lies on a greater road (the ride's, or a side road made before it) sinks just
	// under that road's own surface, and for a few metres out from that road's edge leans into it: two
	// surfaces at one place fought (the flicker at junctions in build 140), and a flat side road laid
	// across a main road on a slope stood proud of it at one edge (the slight bump).
	if (Which <= 0) return Y;
	if (WholeNear(Which, At)) return Y;   // [smooth junctions stage 3] (its own heights up to its mouth: the junction meets them)
	// (the ride's road first; then whichever side road made before it is nearest)
	// ([crunched junctions, 169] the nearest road it settles onto: a lesser road nearer (one that settles onto
	// it) stood in the way, and where three roads lie over each other at a junction this one kept its own
	// height over the greater one, a step of up to 40 cm, the torn tarmac at Kirkstone Pass)
	NearOpts Greater[2];
	Greater[0].MainOnly = true;
	Greater[1].NotRoad = Which;
	Greater[1].Settles = Which;
	for (NearOpts& O : Greater)
	{
		O.NoDecks = true;
		O.NoBores = true;
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, O, Hit) || !JoinsOnto(Which, Hit.Which)) continue;   // ([circuits, build 169] JoinsOnto)
		const double Out2 = Hit.Dist - Hit.Half;
		if (Out2 > JoinLeanOf(Which, Hit.Which)) continue;
		double Theirs = Hit.Height + Hit.Cross - JoinSink;
		if (std::fabs(Theirs - Y) > 1.2) continue;   // (over or under it, not joining it)
		if (Depth > 0 && Hit.Which > 0) Theirs = JoinedHeightDeep(Hit.Which, At, Theirs + JoinSink, Depth - 1) - JoinSink;
		const double T = Out2 <= 0.0 ? 0.0 : Smooth01(Out2 / JoinLean);
		return Theirs + (Y - Theirs) * T;
	}
	return Y;
}

void RidePlan::JoinSegs(int Which, double X0, double Z0, double X1, double Z1, JoinSet& Out) const
{
	Out = JoinSet();
	Out.X0 = X0; Out.Z0 = Z0; Out.X1 = X1; Out.Z1 = Z1;
	Out.Nx = std::max(1, int(std::ceil((X1 - X0) / JoinCell)));
	Out.Nz = std::max(1, int(std::ceil((Z1 - Z0) / JoinCell)));
	Out.Cells.resize(size_t(Out.Nx) * size_t(Out.Nz));
	if (Net == nullptr) return;
	std::shared_lock<std::shared_mutex> Hold(Net->Read);
	const long long Cx0 = static_cast<long long>(std::floor(X0 / NearCell)) - 3, Cx1 = static_cast<long long>(std::floor(X1 / NearCell)) + 3;
	const long long Cz0 = static_cast<long long>(std::floor(Z0 / NearCell)) - 3, Cz1 = static_cast<long long>(std::floor(Z1 / NearCell)) + 3;
	for (long long Cx = Cx0; Cx <= Cx1; ++Cx)
		for (long long Cz = Cz0; Cz <= Cz1; ++Cz)
		{
			const auto It = Net->Near.find(Cx * 4000037LL + Cz);
			if (It == Net->Near.end()) continue;
			for (const std::pair<int, int>& Entry : It->second)
			{
				if (Entry.first == Which) continue;
				const std::vector<RoadPoint>& Pts = RoadOf(Entry.first);
				const int S = Entry.second;
				if (S + 1 >= int(Pts.size())) continue;
				const RoadPoint& Pa = Pts[size_t(S)];
				const RoadPoint& Pb = Pts[size_t(S) + 1];
				if ((Pa.Bridge && Pb.Bridge) || (Pa.Tunnel && Pb.Tunnel)) continue;   // (NoDecks, NoBores)
				// (only where its edge comes within JoinLean: further off it can't be the answer NearestIn gives)
				const double Pad = std::fmax(Pa.Half, Pb.Half) + JoinLean + 0.05;
				const double Sx0 = std::fmin(Pa.At.X, Pb.At.X) - Pad, Sx1 = std::fmax(Pa.At.X, Pb.At.X) + Pad;
				const double Sz0 = std::fmin(Pa.At.Z, Pb.At.Z) - Pad, Sz1 = std::fmax(Pa.At.Z, Pb.At.Z) + Pad;
				if (Sx1 < X0 || Sx0 > X1 || Sz1 < Z0 || Sz0 > Z1) continue;
				JoinSeg J;
				J.Which = Entry.first;
				J.Cx = Cx;
				J.Cz = Cz;
				J.A = Pa.At;
				J.B = Pb.At;
				J.HalfA = Pa.Half; J.HalfB = Pb.Half;
				J.HeightA = Pa.Height; J.HeightB = Pb.Height;
				J.BankA = Pa.Bank; J.BankB = Pb.Bank;
				const int Index = int(Out.Segs.size());
				Out.Segs.push_back(J);
				const int Gx0 = std::max(0, int(std::floor((Sx0 - X0) / JoinCell))), Gx1 = std::min(Out.Nx - 1, int(std::floor((Sx1 - X0) / JoinCell)));
				const int Gz0 = std::max(0, int(std::floor((Sz0 - Z0) / JoinCell))), Gz1 = std::min(Out.Nz - 1, int(std::floor((Sz1 - Z0) / JoinCell)));
				for (int Gz = Gz0; Gz <= Gz1; ++Gz)
					for (int Gx = Gx0; Gx <= Gx1; ++Gx) Out.Cells[size_t(Gz) * size_t(Out.Nx) + size_t(Gx)].push_back(Index);
			}
		}
}

bool RidePlan::NearestIn(const JoinSet& Set, const P2& At, bool MainOnly, JoinHit& Hit, int Settles) const
{
	// (scored as NearestOf scores them, among the bits it would look at from here)
	Hit = JoinHit();
	if (!Set.Holds(At)) return false;
	const int Gx = std::min(Set.Nx - 1, int((At.X - Set.X0) / JoinCell)), Gz = std::min(Set.Nz - 1, int((At.Z - Set.Z0) / JoinCell));
	const long long Px0 = static_cast<long long>(std::floor(At.X / NearCell)), Pz0 = static_cast<long long>(std::floor(At.Z / NearCell));
	double Best = 1e18;
	for (const int Index : Set.Cells[size_t(Gz) * size_t(Set.Nx) + size_t(Gx)])
	{
		const JoinSeg& J = Set.Segs[size_t(Index)];
		if (Settles > 0 && (J.Which == Settles || !JoinsOnto(Settles, J.Which))) continue;   // [crunched junctions, 169]
		if ((MainOnly && J.Which != 0) || J.Cx < Px0 - 3 || J.Cx > Px0 + 3 || J.Cz < Pz0 - 3 || J.Cz > Pz0 + 3) continue;
		const P2 Ab = J.B - J.A;
		const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
		const double T = L2 > 1e-12 ? std::fmin(1.0, std::fmax(0.0, ((At.X - J.A.X) * Ab.X + (At.Z - J.A.Z) * Ab.Z) / L2)) : 0.0;
		const double Px = J.A.X + Ab.X * T, Pz = J.A.Z + Ab.Z * T;
		const double D2 = (At.X - Px) * (At.X - Px) + (At.Z - Pz) * (At.Z - Pz);
		if (D2 > NearestReach * NearestReach) continue;
		const double D = std::sqrt(D2);
		const double HalfHere = J.HalfA + (J.HalfB - J.HalfA) * T;
		const double Score = D - HalfHere - (J.Which == 0 ? 0.01 : 0.0);
		if (Score >= Best) continue;
		Best = Score;
		Hit.Which = J.Which;
		Hit.Dist = D;
		Hit.Half = HalfHere;
		Hit.Height = J.HeightA + (J.HeightB - J.HeightA) * T;
		const double Len = std::sqrt(L2);
		const double Off = Len > 1e-9 ? ((At.X - Px) * Ab.Z - (At.Z - Pz) * Ab.X) / Len : 0.0;
		Hit.Cross = RoadLine::CrossHeight(Off, HalfHere, J.BankA + (J.BankB - J.BankA) * T);
		Hit.Dir = Len > 1e-9 ? Ab * (1.0 / Len) : P2(0.0, -1.0);
	}
	return Hit.Which >= 0 && Hit.Dist - Hit.Half <= JoinLean;
}

int RidePlan::JoinedOn(const JoinSet& Set, int Which, const P2& At, const P2& Left, double& By) const
{
	// (the road whose tarmac At lies on or nearly, running alongside (Left across this road), as JoinedHeightFrom would
	// settle it onto there, -1 none; By 1 on it, down to 0 JoinFirmEase out from its edge. Not a road leaving across
	// another at a junction: its edges, carried on the other's fall past its edge, sank under the land there)
	By = 0.0;
	if (Which <= 0 || !Set.Holds(At)) return -1;
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		JoinHit Hit;
		if (!NearestIn(Set, At, Pass == 0, Hit, Which) || !JoinsOnto(Which, Hit.Which)) continue;
		if (Hit.Dist - Hit.Half > JoinLeanOf(Which, Hit.Which)) continue;
		By = (1.0 - Smooth01((Hit.Dist - Hit.Half) / JoinFirmEase)) * (1.0 - Smooth01((std::fabs(Left.X * Hit.Dir.X + Left.Z * Hit.Dir.Z) - 0.2) / 0.2));
		return By > 0.0 ? Hit.Which : -1;
	}
	return -1;
}

double RidePlan::JoinedHeightFrom(const JoinSet& Set, int Which, const P2& At, double Y, int Firm, double FirmBy) const
{
	// (as JoinedHeight: the ride's road first, then whichever other road is nearest)
	if (Which <= 0) return Y;
	if (WholeNear(Which, At)) return Y;   // [smooth junctions stage 3]
	if (!Set.Holds(At)) return JoinedHeight(Which, At, Y);
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		JoinHit Hit;
		if (!NearestIn(Set, At, Pass == 0, Hit, Which) || !JoinsOnto(Which, Hit.Which)) continue;   // ([circuits, build 169] JoinsOnto)
		const double Out2 = Hit.Dist - Hit.Half;
		if (Out2 > JoinLeanOf(Which, Hit.Which)) continue;
		double Theirs = Hit.Height + Hit.Cross - JoinSink;
		if (std::fabs(Theirs - Y) > 1.2) continue;
		if (Hit.Which > 0) Theirs = JoinedHeightDeep(Hit.Which, At, Theirs + JoinSink, JoinDepth - 1) - JoinSink;   // [crunched junctions, 169]
		const double T = Out2 <= 0.0 ? 0.0 : Smooth01(Out2 / JoinLean);
		const double Plain = Theirs + (Y - Theirs) * T;
		// ([smooth junctions stage 5c] a road whose middle lies on the other's tarmac there, alongside it (Firm, FirmBy): its
		// edge beyond that tarmac carried on the other's tilt, not eased back to its own height from the other's edge; the
		// piece's one triangle from its middle to that edge sloped down under the other's edge, a lip of 15-25 cm along a
		// dual's half laid alongside, Moseley. Only ever raised by it: lowered, the land came up through it, York; and not
		// where it lies on a third road's tarmac: onto that road there)
		double Firmly = WholeFirmOff || Firm != Hit.Which || Out2 <= 0.0 ? 0.0 : FirmBy * (1.0 - Smooth01((Out2 - JoinFirm) / 1.0));
		JoinHit Other;
		if (Firmly > 0.0 && NearestIn(Set, At, false, Other, Which) && Other.Which != Hit.Which && Other.Dist <= Other.Half)
		{
			if (!JoinsOnto(Which, Other.Which)) return Plain;
			const double Its = Other.Height + Other.Cross;
			return (Other.Which > 0 ? JoinedHeightDeep(Other.Which, At, Its, JoinDepth - 1) : Its) - JoinSink;
		}
		if (Firmly <= 0.0) return Plain;
		double Carried = Hit.Height + Hit.Cross * (Hit.Half > 0.5 ? Hit.Dist / Hit.Half : 1.0) - JoinSink;
		if (Hit.Which > 0) Carried = JoinedHeightDeep(Hit.Which, At, Carried + JoinSink, JoinDepth - 1) - JoinSink;
		const double Firmed = Carried + (Y - Carried) * (T * (1.0 - Firmly));
		return std::fmax(Plain, Firmed);
	}
	return Y;
}

namespace
{
// [smooth junctions 4b] One mesh added to another: its points moved to the other's origin, its sub-meshes into the
// other's, its optional channels filled where only one of the two has them.
void AppendMesh(RideMesh& To, const RideMesh& From)
{
	const size_t V0 = To.Vertices(), V1 = From.Vertices();
	const float Dx = float(From.OriginX - To.OriginX), Dz = float(From.OriginZ - To.OriginZ);
	if (V1 > 0)
	{
		if (V0 == 0) { To.OriginX = From.OriginX; To.OriginZ = From.OriginZ; }
		const float Sx = V0 == 0 ? 0.0f : Dx, Sz = V0 == 0 ? 0.0f : Dz;
		for (size_t I = 0; I < V1; ++I)
		{
			To.Pos.push_back(From.Pos[I * 3] + Sx);
			To.Pos.push_back(From.Pos[I * 3 + 1]);
			To.Pos.push_back(From.Pos[I * 3 + 2] + Sz);
		}
		To.Nrm.insert(To.Nrm.end(), From.Nrm.begin(), From.Nrm.end());
		const auto Channel = [V0, V1](std::vector<float>& A, const std::vector<float>& B, size_t Per, float Else)
		{
			if (A.empty() && B.empty()) return;
			A.resize(V0 * Per, Else);
			A.insert(A.end(), B.begin(), B.begin() + static_cast<std::ptrdiff_t>(std::min(B.size(), V1 * Per)));
			A.resize((V0 + V1) * Per, Else);
		};
		Channel(To.Uv, From.Uv, 2, 0.0f);
		Channel(To.Col, From.Col, 3, 1.0f);
		Channel(To.Alpha, From.Alpha, 1, 1.0f);
		Channel(To.Uv1, From.Uv1, 2, 0.0f);
		for (uint32_t I : From.Idx) To.Idx.push_back(I + uint32_t(V0));
	}
	std::shared_ptr<RideMesh> RideMesh::*const Subs[] = {&RideMesh::Extra, &RideMesh::Stone, &RideMesh::Masonry, &RideMesh::Render, &RideMesh::Roofing,
		&RideMesh::Brickwork, &RideMesh::Water, &RideMesh::Night, &RideMesh::Signals, &RideMesh::Signs, &RideMesh::Glow, &RideMesh::Turf};
	for (std::shared_ptr<RideMesh> RideMesh::*Sub : Subs)
	{
		const std::shared_ptr<RideMesh>& B = From.*Sub;
		if (!B) continue;
		std::shared_ptr<RideMesh>& A = To.*Sub;
		if (!A) { A = std::make_shared<RideMesh>(); A->OriginX = B->OriginX; A->OriginZ = B->OriginZ; }
		AppendMesh(*A, *B);
	}
	// (the rest kept in this mesh's own origin: both pieces are made about one)
	for (RideMesh::Label L : From.Labels) { L.X += Dx; L.Z += Dz; To.Labels.push_back(L); }
	for (size_t I = 0; I + 2 < From.Lights.size(); I += 3) { To.Lights.push_back(From.Lights[I] + Dx); To.Lights.push_back(From.Lights[I + 1]); To.Lights.push_back(From.Lights[I + 2] + Dz); }
	To.Plants.insert(To.Plants.end(), From.Plants.begin(), From.Plants.end());
	To.Animals.insert(To.Animals.end(), From.Animals.begin(), From.Animals.end());
	To.Props.insert(To.Props.end(), From.Props.begin(), From.Props.end());
}
} // namespace

void RidePlan::RoadPiece(int Which, int First, int Last, RideMesh& Out) const
{
	Out = RideMesh();
	// [smooth junctions 4b] (the ride's road round a ring built whole: not drawn there, the ring's piece in its place)
	if (Which == 0 && WholeJunctions && !RideHides.empty())
	{
		const std::vector<RoadPoint>& Pts = RoadOf(0);
		const int F = std::max(0, First), L = std::min(int(Pts.size()) - 1, Last);
		if (L > F)
		{
			const double A0 = Pts[size_t(F)].Along, A1 = Pts[size_t(L)].Along;
			std::vector<std::pair<double, double>> Keep = {{A0, A1}};
			std::vector<int> Rings;
			bool Any = false;
			for (const std::pair<double, int>& Hd : RideHides)
			{
				const WholePlan& W = WholeOf[size_t(Hd.second)];
				if (W.Hide[1] <= A0 || W.Hide[0] >= A1) continue;
				if (W.Hide[0] >= A0 && W.Hide[0] < A1) Rings.push_back(Hd.second);
				Any = true;
				std::vector<std::pair<double, double>> Next;
				for (const std::pair<double, double>& K : Keep)
				{
					if (K.first < W.Hide[0]) Next.emplace_back(K.first, std::fmin(K.second, W.Hide[0]));
					if (K.second > W.Hide[1]) Next.emplace_back(std::fmax(K.first, W.Hide[1]), K.second);
				}
				Keep.swap(Next);
			}
			if (Any || !Rings.empty())
			{
				const double Ox = Pts[size_t(F)].At.X, Oz = Pts[size_t(F)].At.Z;
				Out.OriginX = Ox;
				Out.OriginZ = Oz;
				for (const std::pair<double, double>& K : Keep)
				{
					if (K.second - K.first < 0.05) continue;
					int Kf = F, Kl = L;
					while (Kf + 1 < L && Pts[size_t(Kf) + 1].Along <= K.first) ++Kf;
					while (Kl - 1 > Kf && Pts[size_t(Kl) - 1].Along >= K.second) --Kl;
					RideMesh Part;
					RoadPieceIn(0, Kf, Kl, K.first > A0 ? K.first : -1e18, K.second < A1 ? K.second : 1e18, Ox, Oz, Part);
					AppendMesh(Out, Part);
				}
				Out.OriginX = Ox;   // (AppendMesh takes the first part's when this has none yet: the same)
				Out.OriginZ = Oz;
				for (int Ji : Rings) WholePiece(Ji, Out);
				return;
			}
		}
	}
	RoadPieceIn(Which, First, Last, -1e18, 1e18, std::nan(""), std::nan(""), Out);
}

void RidePlan::RoadPieceIn(int Which, int First, int Last, double KeepFrom, double KeepTo, double Ox, double Oz, RideMesh& Out) const
{
	Out = RideMesh();
	if (Which < 0 || Which >= RoadCount()) return;
	const SideRoad* Mine = Which > 0 ? SideOf(Which) : nullptr;
	if (Which > 0 && Mine == nullptr) return;
	if (WholeHidden(Which)) return;   // [smooth junctions stage 3] (wholly inside a junction built whole: its piece covers it)
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	First = std::max(0, First);
	Last = std::min(int(Pts.size()) - 1, Last);
	if (Last <= First) return;
	if (std::isnan(Ox)) { Ox = Pts[size_t(First)].At.X; Oz = Pts[size_t(First)].At.Z; }
	Out.OriginX = Ox;
	Out.OriginZ = Oz;
	// (a side road's paint stops short of the junctions at its ends)
	// ([junction priority 168] but for a through road's arm at a junction where the ride gives way: on to its mouth)
	const auto ThroughEnd = [this, Mine](int End)
	{
		const auto It = Mine != nullptr && Mine->Edge >= 0 ? ThroughEnds.find(static_cast<long long>(Mine->Edge) * 2 + End) : ThroughEnds.end();
		return It == ThroughEnds.end() ? 12.0 : It->second;
	};
	double PaintFrom = Which == 0 ? -1.0 : ThroughEnd(0), PaintTo = Which == 0 ? 1e18 : Mine->Length - ThroughEnd(1);
	// [smooth junctions stage 3] (at a junction built whole, on to its mouth: its piece paints on from there)
	if (Which > 0 && WholeCut(Which, 0) > 0.0) PaintFrom = WholeCut(Which, 0);
	if (Which > 0 && WholeCut(Which, 1) > 0.0) PaintTo = Mine->Length - WholeCut(Which, 1);
	// (the Godot game's colours, linear: tarmac, the verge (worn grass and grit: the Godot gravel read as a pale
	// band beside the road in build 140), paint)
	// [build 147] (and the verge's grass: the shoulder runs from grit at the tarmac's edge to grass, which
	// M_RideRoad draws in the land's grass scan with a ragged edge; beenster, build 146: "remove these green
	// road borders and blend the road into the side")
	static constexpr float VergeGrass[3] = {0.10f, 0.16f, 0.03f};
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, Verge[3] = {0.072f, 0.078f, 0.045f}, Paint[3] = {0.83f, 0.83f, 0.75f},
		Tile[3] = {0.51f, 0.49f, 0.43f}, Concrete[3] = {0.21f, 0.21f, 0.2f}, Soot[3] = {0.06f, 0.06f, 0.06f},
		Portal[3] = {0.34f, 0.33f, 0.3f}, Grass[3] = {0.088f, 0.105f, 0.04f};
	static constexpr float Yellow[3] = {0.78f, 0.52f, 0.03f};   // [OSM stage 5D] no-parking lines
	static constexpr double YellowReach = 12.0;
	static constexpr float RedLine[3] = {0.55f, 0.04f, 0.03f};   // [OSM stage 5D] no stopping
	static constexpr float KerbStone[3] = {0.16f, 0.16f, 0.15f}, Paving[3] = {0.12f, 0.118f, 0.11f};   // [OSM stage 5D]
	static constexpr double BayDeepest = 2.0, BayLong = 6.0;       // marked parking bays (m)
	// [circuits, build 168] (a race circuit's track and pit lane: M_RideRoad keys this colour to black tarmac, smooth as silk)
	static constexpr float CircuitTarmac[3] = {0.010f, 0.010f, 0.011f};
	const bool CircuitTrack = CircuitRoad(Which);
	const int Count = int(Pts.size());
	struct Row
	{
		P2 At, Left;
		double Height = 0.0, Half = 0.0, Along = 0.0, Nx = 0.0, Ny = 1.0, Nz = 0.0;
		int Lanes = 0;          // [OSM stage 5] (RoadPoint's)
		bool OneWay = false;
		double Shoulder = 0.0;  // [OSM stage 5C] (RoadPoint's)
		int Near = 0;
		int Park = 0;           // [OSM stage 5D] (RoadPoint's)
		bool Deck = false;
		double Bank = 0.0;
		bool Bore = false, Mouth = false;   // in a tunnel; at one of its ends
		double Line = 1e3, Brake = 0.0;     // [circuits, build 169] (CircuitLine's)
	};
	std::vector<Row> Rows;
	std::vector<double> RaceLine, RaceBrake;
	if (CircuitTrack) CircuitLine(Which, RaceLine, RaceBrake);
	for (int I = First; I <= Last; ++I)
	{
		const RoadPoint& P = Pts[size_t(I)];
		const RoadPoint& Before = Pts[size_t(std::max(0, I - 1))];
		const RoadPoint& After = Pts[size_t(std::min(Count - 1, I + 1))];
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
		R.Lanes = P.Lanes;
		R.OneWay = P.OneWay;
		R.Shoulder = P.Shoulder;
		R.Near = P.Near;
		R.Park = P.Park;
		R.Along = P.Along;
		R.Deck = P.Bridge;
		R.Bank = P.Bank;
		R.Bore = P.Tunnel;
		if (R.Bore)
		{
			const bool Before2 = I > 0 && Pts[size_t(I) - 1].Tunnel, After2 = I + 1 < Count && Pts[size_t(I) + 1].Tunnel;
			R.Mouth = Before2 != After2;
		}
		R.Nx = -Dir.X * Rise / Nl;
		R.Ny = 1.0 / Nl;
		R.Nz = -Dir.Z * Rise / Nl;
		if (size_t(I) < RaceLine.size()) { R.Line = RaceLine[size_t(I)]; R.Brake = RaceBrake[size_t(I)]; }
		Rows.push_back(R);
	}
	// [smooth junctions stage 3] (an end at a junction built whole: the road stops at its mouth line, square across, Cut
	// metres in from that end; the junction's piece is drawn from that same row (CutRow))
	if (!Rows.empty() && WholeJunctions && (Which > 0 || KeepFrom > -1e17 || KeepTo < 1e17))
	{
		const double Long = Pts.back().Along;
		const auto CutRowOf = [&](double Along)
		{
			RoadPoint P;
			P2 Dir;
			double Rise = 0.0;
			CutRow(Pts, Along, P, Dir, Rise);
			const double Nl = std::sqrt(1.0 + Rise * Rise);
			Row R;
			R.At = P.At; R.Left = P2(Dir.Z, -Dir.X); R.Height = P.Height; R.Half = P.Half; R.Lanes = P.Lanes; R.OneWay = P.OneWay;
			R.Shoulder = P.Shoulder; R.Near = P.Near; R.Park = P.Park; R.Along = P.Along; R.Deck = P.Bridge; R.Bank = P.Bank; R.Bore = P.Tunnel;
			R.Nx = -Dir.X * Rise / Nl; R.Ny = 1.0 / Nl; R.Nz = -Dir.Z * Rise / Nl;
			return R;
		};
		// ([smooth junctions 4b] the ride's road's the same way, round a ring built whole)
		const double CutA = Which > 0 ? WholeCut(Which, 0) : (KeepFrom > -1e17 ? KeepFrom : 0.0), CutB = Which > 0 ? WholeCut(Which, 1) : (KeepTo < 1e17 ? Long - KeepTo : 0.0);
		if (CutA > 0.0 && Rows.front().Along < CutA)
		{
			size_t Drop = 0;
			while (Drop < Rows.size() && Rows[Drop].Along <= CutA) ++Drop;
			Rows.erase(Rows.begin(), Rows.begin() + long(Drop));
			Rows.insert(Rows.begin(), CutRowOf(CutA));
		}
		if (CutB > 0.0 && Rows.back().Along > Long - CutB)
		{
			while (!Rows.empty() && Rows.back().Along >= Long - CutB) Rows.pop_back();
			Rows.push_back(CutRowOf(Long - CutB));
		}
		// (a piece wholly within a junction's mouth: nothing of it)
		if (Rows.size() < 2 || Rows.back().Along <= Rows.front().Along + 0.01)
		{
			Out = RideMesh();
			Out.OriginX = Ox;
			Out.OriginZ = Oz;
			for (int End = 0; End < 2; ++End)
			{
				const int Wj = (End == 0 ? First == 0 : Last == int(Pts.size()) - 1) ? WholeAt(Which, End) : -1;
				if (Wj >= 0 && WholeOf[size_t(Wj)].Owner == Which && WholeOf[size_t(Wj)].OwnerEnd == End) WholePiece(Wj, Out);
			}
			return;
		}
	}
	// ([build 150] the other roads' bits near this piece gathered once for all its points; a point
	// outside the box they were gathered for asks NearestOf itself)
	JoinSet Joins;
	if (Which > 0 && !Rows.empty())
	{
		double Jx0 = 1e18, Jz0 = 1e18, Jx1 = -1e18, Jz1 = -1e18, Reach = 0.0;
		for (const Row& R : Rows)
		{
			Jx0 = std::fmin(Jx0, R.At.X); Jx1 = std::fmax(Jx1, R.At.X); Jz0 = std::fmin(Jz0, R.At.Z); Jz1 = std::fmax(Jz1, R.At.Z);
			Reach = std::fmax(Reach, R.Half + R.Shoulder);
		}
		Reach += 12.0;   // (the verge and the kerbs beyond the shoulder)
		JoinSegs(Which, Jx0 - Reach, Jz0 - Reach, Jx1 + Reach, Jz1 + Reach, Joins);
	}
	// ([smooth junctions stage 5c] the road the row's middle lies on, if any: see JoinedHeightFrom's Firm)
	P2 FirmAt(1e18, 1e18);
	int FirmOn = -1;
	double FirmBy = 0.0;
	const auto Settle = [this, Which, &Joins, &FirmAt, &FirmOn, &FirmBy](const Row& R, const P2& At, double Y)
	{
		if (R.Deck || R.Bore) return Y;
		if (WholeJunctions && Which > 0 && (R.At.X != FirmAt.X || R.At.Z != FirmAt.Z))
		{
			FirmAt = R.At;
			FirmOn = JoinedOn(Joins, Which, R.At, R.Left, FirmBy);
		}
		return JoinedHeightFrom(Joins, Which, At, Y, WholeJunctions ? FirmOn : -1, FirmBy);
	};
	// [places stage 2] The pull-offs made for places near this bit of road (drawn at its end; no grass
	// shoulder where one runs along it).
	// ([roads stage 7] and the petrol stations' forecourts the same way, in tarmac)
	struct Apron
	{
		P2 RoadMid, RoadDir, Out;
		double Depth = 0.0, Reach = 0.0;   // out from the road's edge; along it either way from RoadMid
		bool Tarmac = false;
	};
	std::vector<Apron> PullNear;
	if ((!PlaceStops.empty() || !Forecourts.empty() || !RoadLaybys.empty()) && Rows.size() >= 2)
	{
		double X0 = 1e18, Z0 = 1e18, X1 = -1e18, Z1 = -1e18;
		for (const Row& R : Rows) { X0 = std::fmin(X0, R.At.X); X1 = std::fmax(X1, R.At.X); Z0 = std::fmin(Z0, R.At.Z); Z1 = std::fmax(Z1, R.At.Z); }
		std::vector<int> Near;
		for (int Cz = int(std::floor((Z0 - 40.0) / 64.0)); Cz <= int(std::floor((Z1 + 40.0) / 64.0)); ++Cz)
			for (int Cx = int(std::floor((X0 - 40.0) / 64.0)); Cx <= int(std::floor((X1 + 40.0) / 64.0)); ++Cx)
			{
				const auto It = PlaceGrid.find(static_cast<long long>(Cx) * 4000037LL + Cz);
				if (It != PlaceGrid.end()) Near.insert(Near.end(), It->second.begin(), It->second.end());
			}
		for (int I : Near)
		{
			const PlaceStop& S = PlaceStops[size_t(I)];
			if (S.How == PlaceMade && S.Depth > 0.0) PullNear.push_back({S.RoadMid, S.RoadDir, S.Out, S.Depth, PullOffLength * 0.5, false});
		}
		std::vector<int> Courts;
		for (int Cz = int(std::floor((Z0 - 40.0) / 64.0)); Cz <= int(std::floor((Z1 + 40.0) / 64.0)); ++Cz)
			for (int Cx = int(std::floor((X0 - 40.0) / 64.0)); Cx <= int(std::floor((X1 + 40.0) / 64.0)); ++Cx)
			{
				const auto It = ForecourtGrid.find(static_cast<long long>(Cx) * 4000037LL + Cz);
				if (It != ForecourtGrid.end()) Courts.insert(Courts.end(), It->second.begin(), It->second.end());
			}
		for (int I : Courts)
		{
			const Forecourt& F = Forecourts[size_t(I)];
			PullNear.push_back({F.RoadMid, F.RoadDir, F.Out, F.Depth, F.Length * 0.5, true});
		}
		// [everyday kits stage 5b] (the lay-bys: as a pull-off's, tarmac on an A or B road)
		for (int Cz = int(std::floor((Z0 - 40.0) / 64.0)); Cz <= int(std::floor((Z1 + 40.0) / 64.0)); ++Cz)
			for (int Cx = int(std::floor((X0 - 40.0) / 64.0)); Cx <= int(std::floor((X1 + 40.0) / 64.0)); ++Cx)
			{
				const auto It = LaybyGrid.find(static_cast<long long>(Cx) * 4000037LL + Cz);
				if (It == LaybyGrid.end()) continue;
				for (int I : It->second)
				{
					const Layby& L = RoadLaybys[size_t(I)];
					PullNear.push_back({L.RoadMid, L.RoadDir, L.Out, L.Depth, LaybyLong * 0.5, L.Tarmac});
				}
			}
	}
	const auto PullSpot = [](const Apron& S, const P2& At, double& U, double& Off)
	{
		const P2 D = At - S.RoadMid;
		U = D.X * S.RoadDir.X + D.Z * S.RoadDir.Z;
		Off = D.X * S.Out.X + D.Z * S.Out.Z;
	};
	// (the mesh Put and Strip write to: the road's own, or its night glow for the cat's eyes, or the turf of
	// its country shoulders, drawn as the land: Out.Turf, finished at the end)
	RideMesh* Into = &Out;
	std::shared_ptr<RideMesh> TurfMesh = std::make_shared<RideMesh>();
	TurfMesh->OriginX = Out.OriginX;
	TurfMesh->OriginZ = Out.OriginZ;
	std::shared_ptr<RideMesh> StoneMesh;   // (a bridge's parapets and underside: Out.Stone, made when first needed)
	// ([Nov 20] a bridge's walls from the dry stone wall kit when the game has its models: see the pass at the end)
	// (on a country road's bridge: a wide road's, a dual carriageway's or a motorway's keeps the plain parapet)
	const bool BridgeWalls = PropModelled[PropWallStone] && PropModelled[PropWallEnd];
	const auto StoneParapet = [BridgeWalls](double Half, int Lanes) { return BridgeWalls && Half <= 4.5 && Lanes <= 2; };
	const bool RailsHere = BridgeRailsOn();   // [everyday kits stage 7] (a bridge with no stone walls: the bridge set's parapet)
	static constexpr double ParapetFootHalf = 0.25, ParapetHigh = 1.15;   // (the walls laid on a deck: half their width at the foot, their height)
	// ([build 154] UV0 is metres across and along over 4, the along counted from a whole 36 m behind this bit
	// of road, so it stays small enough for the half-precision UVs UE draws (KEY GOTCHA 147) and the pavement's
	// slabs, 0.9 m long, line up across bits of road)
	double AlongBase = 0.0;
	// ([weather, Nov 18] beenster on 156: no puddles on Ride UK's roads. The road's own mesh carries where
	// rain water stands in its vertex alpha, for M_RideRoad's puddles as Buttertubs' road.glb carries it:
	// 0 the crown .. 0.9 the edge, scaled by the camber, most on a bend's inside half where the road leans
	// in and least on its outside. 1 means no puddle data (any vertex not written here).)
	const auto StandsAt = [](const Row& R, double Off)
	{
		const double Edge = R.Half > 0.1 ? std::clamp(std::fabs(Off) / R.Half, 0.0, 1.0) : 0.0;
		const double Tilt = Off >= 0.0 ? R.Bank : -R.Bank;   // + this half is the bend's outside
		const double Inside = std::clamp(-Tilt / 0.03, 0.0, 1.0), Outside = std::clamp(Tilt / 0.03, 0.0, 1.0);
		return float(0.9 * Edge * (0.8 + 0.2 * Inside - 0.3 * Outside));
	};
	const auto Put = [&Into, &Out, &Settle, &AlongBase, &StandsAt, CircuitTrack](const Row& R, double Off, double Lift, const float* Colour)
	{
		// (Off is to the left of the middle)
		RideMesh& M = *Into;
		if (&M == &Out)
		{
			M.Alpha.resize(M.Vertices(), 1.0f);
			M.Alpha.push_back(StandsAt(R, Off));
			// [circuits, build 169] (a circuit's track: its UV1 metres to the left of the racing line, and how hard
			// riders brake there, for M_RideRoad's rubbered line and braking marks; 1000 where none)
			if (CircuitTrack)
			{
				M.Uv1.resize(M.Vertices() * 2, 1e3f);
				M.Uv1.push_back(float(std::clamp(Off - R.Line, -1e3, 1e3)));
				M.Uv1.push_back(float(R.Brake));
			}
		}
		const P2 At = R.At + R.Left * Off;
		const double Y = Settle(R, At, R.Height + RoadLine::CrossHeight(Off, R.Half, R.Bank));
		M.Pos.push_back(float(At.X - M.OriginX)); M.Pos.push_back(float(Y + Lift)); M.Pos.push_back(float(At.Z - M.OriginZ));
		M.Nrm.push_back(float(R.Nx)); M.Nrm.push_back(float(R.Ny)); M.Nrm.push_back(float(R.Nz));
		M.Uv.push_back(float(Off / 4.0)); M.Uv.push_back(float((R.Along - AlongBase) / 4.0));
		M.Col.push_back(Colour[0]); M.Col.push_back(Colour[1]); M.Col.push_back(Colour[2]);
	};
	// A strip between two rows from OffA to OffB (left to right: OffA the greater), up-facing.
	// (a strip whose A edge is one colour and B edge another)
	const auto Graded = [&Into, &Put](const Row& R0, const Row& R1, double A0, double B0, double A1, double B1, double LiftA, double LiftB, const float* ColourA, const float* ColourB)
	{
		RideMesh& M = *Into;
		const uint32_t V = uint32_t(M.Vertices());
		Put(R0, A0, LiftA, ColourA);
		Put(R0, B0, LiftB, ColourB);
		Put(R1, A1, LiftA, ColourA);
		Put(R1, B1, LiftB, ColourB);
		M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
	};
	const auto Strip = [&Into, &Put](const Row& R0, const Row& R1, double A0, double B0, double A1, double B1, double LiftA, double LiftB, const float* Colour, bool BothSides = false)
	{
		RideMesh& M = *Into;
		const uint32_t V = uint32_t(M.Vertices());
		Put(R0, A0, LiftA, Colour);
		Put(R0, B0, LiftB, Colour);
		Put(R1, A1, LiftA, Colour);
		Put(R1, B1, LiftB, Colour);
		M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
		if (BothSides) M.Idx.insert(M.Idx.end(), {V, V + 2, V + 1, V + 1, V + 2, V + 3});
	};
	// [road edges, Nov 17] The shoulder's outer band, From to To out from the edge on side S (1 left, -1
	// right): its inner edge ShoulderDip under the road's edge, its outer just under the ground as drawn
	// (LandDrawnAt), never higher than its inner edge nor lower than ShoulderDrop.
	std::unordered_map<long long, double> LandCache;
	// ([build 155] and a pavement's back: Inner its inner edge's lift, Highest the most its outer edge may be)
	const auto Meet = [this, &Into, &Put, &Settle, &LandCache](const Row& R0, const Row& R1, double S, double From, double To, double Inner, double Highest)
	{
		static constexpr float MeetGrass[3] = {0.10f, 0.16f, 0.03f};
		RideMesh& M = *Into;
		const uint32_t V = uint32_t(M.Vertices());
		const auto Outer = [&](const Row& R)
		{
			const double Off = S * (R.Half + To);
			const P2 At = R.At + R.Left * Off;
			const double Y = Settle(R, At, R.Height + RoadLine::CrossHeight(Off, R.Half, R.Bank));
			const double Drawn = LandDrawnAt(At.X, At.Z, &LandCache);
			const double Lift = std::isnan(Drawn) ? -ShoulderDrop : std::clamp(Drawn - MeetUnder - Y, -ShoulderDrop, Highest);
			Put(R, Off, Lift, MeetGrass);
		};
		if (S > 0.0)
		{
			Outer(R0);
			Put(R0, R0.Half + From, Inner, MeetGrass);
			Outer(R1);
			Put(R1, R1.Half + From, Inner, MeetGrass);
		}
		else
		{
			Put(R0, -R0.Half - From, Inner, MeetGrass);
			Outer(R0);
			Put(R1, -R1.Half - From, Inner, MeetGrass);
			Outer(R1);
		}
		M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
	};
	// A tunnel's parts: any four points in the ride's frame, as a quad that faces Toward.
	struct P3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
	};
	const auto Quad3 = [&Out](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, const float* Colour, bool BothSides)
	{
		// (across its diagonals: a quad one of whose ends has closed to a point still has a facing)
		const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
		double Nx = Ay * Bz - Az * By, Ny = Az * Bx - Ax * Bz, Nz = Ax * By - Ay * Bx;
		const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
		if (Len < 1e-9) return;
		const bool Flip = Nx * (Toward.X - Q0.X) + Ny * (Toward.Y - Q0.Y) + Nz * (Toward.Z - Q0.Z) < 0.0;
		const double Sign = (Flip ? -1.0 : 1.0) / Len;
		Nx *= Sign; Ny *= Sign; Nz *= Sign;
		const uint32_t V = uint32_t(Out.Vertices());
		for (const P3* Q : {&Q0, &Q1, &Q2, &Q3})
		{
			Out.Pos.push_back(float(Q->X - Out.OriginX)); Out.Pos.push_back(float(Q->Y)); Out.Pos.push_back(float(Q->Z - Out.OriginZ));
			Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
			Out.Uv.push_back(float((Q->X + Q->Y) / 4.0)); Out.Uv.push_back(float((Q->Z + Q->Y) / 4.0));
			Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		}
		if (!Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
		if (Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
	};
	const auto Across = [](const Row& R, double Off, double Y)
	{
		const P2 At = R.At + R.Left * Off;
		P3 Q;
		Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
		return Q;
	};
	const double Pi = 3.14159265358979323846;
	// The tube's section at a row, from the road's left edge along the kerb to the wall's foot, up over
	// the arch and down to the right foot and edge (AdventureTunnels.profile).
	// (build 141, beenster: a block of turf across the A30 at Trewint. An underpass is a short tunnel
	// under another road with less than the tube's headroom: the tube is squashed to fit under that road,
	// and the lid never rises above it; the cap is that road's tarmac less 0.35 m, or none)
	NearOpts Over;
	Over.NotRoad = Which;
	Over.NoBores = true;
	const auto CapOver = [this, &Over](const Row& R)
	{
		// (an underpass: within 25 m of a road 2 m or more above it. There the tube and its lid stay
		// under the ground as it lies, and under that road's tarmac where it passes over)
		NearHit Hit;
		if (!NearestOf(R.At.X, R.At.Z, Over, Hit) || Hit.Dist > Hit.Half + 25.0 || Hit.Height < R.Height + 2.0) return 1e18;
		const double G = GroundOf(R.At.X, R.At.Z, 0.0, true);
		double Top = std::isnan(G) ? Hit.Height - 0.35 : G - 0.3;
		if (Hit.Dist < Hit.Half + 8.0) Top = std::fmin(Top, Hit.Height - 0.35);
		return Top;
	};
	const auto Squash = [&CapOver](const Row& R)
	{
		const double Cap = CapOver(R);
		return Cap > 1e17 ? 1.0 : std::clamp((Cap - 0.25 - R.Height) / (TubeWall + TubeArch), 0.45, 1.0);
	};
	const auto Ring = [&Across, &Squash, Pi](const Row& R)
	{
		const double E = R.Half, W = E + TubeSide;
		const double Sq = Squash(R);
		const double EdgeL = R.Height + RoadLine::CrossHeight(E, R.Half, R.Bank), EdgeR = R.Height + RoadLine::CrossHeight(-E, R.Half, R.Bank);
		std::vector<P3> Pts3;
		Pts3.push_back(Across(R, E, EdgeL));
		Pts3.push_back(Across(R, W, EdgeL - 0.1));
		for (int K = 0; K <= TubeArchSegs; ++K)
		{
			const double Th = Pi * K / TubeArchSegs;
			Pts3.push_back(Across(R, W * std::cos(Th), R.Height + (TubeWall + TubeArch * std::sin(Th)) * Sq));
		}
		Pts3.push_back(Across(R, -W, EdgeR - 0.1));
		Pts3.push_back(Across(R, -E, EdgeR));
		return Pts3;
	};
	// The hill's lid over it at a row: the ground as it would lie with no tunnel, from well out on the
	// left to well out on the right, never lower than just over the arch; its rim tucked under the land.
	const auto Lid = [this, &Across, &Over, &CapOver, &Squash, Pi](const Row& R)
	{
		const double E = R.Half, W = E + TubeSide;
		std::vector<P3> Pts3;
		const double Cap = CapOver(R), Sq = Squash(R);
		const auto Add = [&](double Off, double Least, double Tuck)
		{
			P3 Q = Across(R, Off, 0.0);
			const double G = GroundOf(Q.X, Q.Z, 0.0, true);
			Q.Y = std::fmax(std::isnan(G) ? std::fmin(R.Height + TubeWall + TubeArch + 3.0, Cap) : G, std::fmin(Least, Cap)) - Tuck;
			NearHit Hit;
			if (NearestOf(Q.X, Q.Z, Over, Hit) && Hit.Dist < Hit.Half + 0.6 && Hit.Height > R.Height + 2.0) Q.Y = std::fmin(Q.Y, Hit.Height - 0.35);
			Pts3.push_back(Q);
		};
		for (double Far : {LidReach, 16.0, 10.0, 4.0}) Add(E + Far, -1e18, Far == LidReach ? 0.4 : 0.0);
		for (int K = 0; K <= TubeArchSegs; ++K)
		{
			const double Th = Pi * K / TubeArchSegs;
			Add(W * std::cos(Th), R.Height + (TubeWall + TubeArch * std::sin(Th)) * Sq + 0.6, 0.0);
		}
		for (double Far : {4.0, 10.0, 16.0, LidReach}) Add(-E - Far, -1e18, Far == LidReach ? 0.4 : 0.0);
		return Pts3;
	};
	// (a gravel side road: how far a point along it is from an end where it joins another road, a mouth
	// or a junction; 1e18 for the ride's own road, or a road with no join)
	bool Joined[2] = {false, false};
	double JoinAt[2] = {0.0, 0.0};
	if (Which > 0)
	{
		const SideRoad& Sd = *Mine;
		for (int End = 0; End < 2; ++End)
		{
			// ([build 170, Hartsop] a junction of gravel lanes only is gravel: no tongue into it)
			const Knot* JoinKnot = KnotAt(Which, End);
			Joined[End] = Sd.Mouths[End].Is || (JoinKnot != nullptr && KnotPaved(*JoinKnot));
			JoinAt[End] = Sd.Mouths[End].Is ? std::fmax(Sd.Mouths[End].Clear[0], Sd.Mouths[End].Clear[1]) : 0.0;   // (from where the mouth's tarmac ends)
		}
	}
	const double SideLength = Which > 0 ? Mine->Length : 0.0;
	const auto GravelFade = [&Joined, &JoinAt, SideLength](double Along)
	{
		double D = 1e18;
		if (Joined[0]) D = std::fmin(D, Along - JoinAt[0]);
		if (Joined[1]) D = std::fmin(D, SideLength - Along - JoinAt[1]);
		return std::fmax(0.0, D);
	};
	// [OSM stage 5D] Which of its rows are a town street's: a kerb and pavement each side in place of the
	// verge (the ride's road where it is built up; another road where the buildings crowd it, not a track,
	// in stretches of 40 m or more).
	std::vector<char> StreetRow(Rows.size(), 0);
	if (Which == 0)
	{
		for (size_t K = 0; K < Rows.size(); ++K) StreetRow[K] = !BuiltUp.empty() && BuiltUp[std::min(size_t(First) + K, BuiltUp.size() - 1)] != 0 ? 1 : 0;
	}
	else if (!Mine->Track && !Mine->Gravel && Mine->Edge >= 0 && StreetClass(Area.Roads.ClassOf(Mine->Edge)))
	{
		const std::vector<char> Whole = StreetsAlong(Which);
		for (size_t K = 0; K < Rows.size(); ++K) StreetRow[K] = Whole[std::min(size_t(First) + K, Whole.size() - 1)];
	}
	// [road rulebook C6] Where its centre line is a warning line: either side of a side road's mouth on the ride's road,
	// and before a junction at its own end where it is the through road, for as far as the limit there asks
	// (RoadX::WarningReach).
	std::vector<std::pair<double, double>> WarnZones;
	if (Which == 0)
	{
		for (size_t I = size_t(std::max(0, First - 24)); I < MouthAt.size() && I <= size_t(Last) + 24 && I < Pts.size(); ++I)
		{
			if (MouthAt[I] == 0) continue;
			const double Reach = RoadX::WarningReach(PaintMph(0, Pts[I].Along, false));
			WarnZones.emplace_back(Pts[I].Along - Reach, Pts[I].Along + Reach);
		}
		const double A0 = Pts[size_t(First)].Along - 70.0, A1 = Pts[size_t(Last)].Along + 70.0;
		for (const double T : RideThroughAt)
			if (T > A0 && T < A1)
			{
				const double Reach = RoadX::WarningReach(PaintMph(0, T, false));
				WarnZones.emplace_back(T - Reach, T + Reach);
			}
	}
	else
	{
		const bool Street0 = !StreetRow.empty() && StreetRow.front() != 0, Street1 = !StreetRow.empty() && StreetRow.back() != 0;
		if (ThroughAtEnd(Which, 0)) WarnZones.emplace_back(-1.0, RoadX::WarningReach(PaintMph(Which, 0.0, Street0)));
		if (ThroughAtEnd(Which, 1)) WarnZones.emplace_back(Mine->Length - RoadX::WarningReach(PaintMph(Which, Mine->Length, Street1)), Mine->Length + 1.0);
	}
	const auto WarnAt = [&WarnZones](double Along)
	{
		for (const std::pair<double, double>& Z : WarnZones)
			if (Along >= Z.first && Along <= Z.second) return true;
		return false;
	};
	// [OSM stage 5D] Where a side road's mouth on the ride's road takes the shoulder (or the kerb and
	// pavement) away: exactly between the two ends of its rounded corners on the ride's road's edge, as the
	// mouth draws them, so the corner's own kerb carries straight on from the street's (beenster Nov 14:
	// "the pavements need to curve round, not just stop"). By side (0 left, 1 right), from and to along.
	std::vector<std::pair<double, double>> MouthGap[2];
	if (Which == 0 && Rows.size() >= 2)
	{
		const double Lo = Rows.front().Along - 20.0, Hi = Rows.back().Along + 20.0;
		for (const int Sw : Planned)
		{
			const SideRoad* Sr = SideOf(Sw);
			if (Sr == nullptr || Sr->Pts.size() < 2) continue;
			for (int End = 0; End < 2; ++End)
			{
				const Mouth& M = Sr->Mouths[End];
				if (!M.Is || M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size()) continue;
				const double MainAlong = Road[size_t(M.MainIndex)].Along;
				if (MainAlong < Lo || MainAlong > Hi) continue;
				const double SideHalf = Sr->Pts[End == 0 ? 0 : Sr->Pts.size() - 1].Half;
				double Ends[2] = {0.0, 0.0};
				int OnSide = -1;
				bool Good = true;
				for (int Sg = 0; Sg < 2 && Good; ++Sg)
				{
					P2 Corner, AlongMain;
					double Reach = 0.0;
					MouthCorner(M, Road[size_t(M.MainIndex)].At, SideHalf, Sg == 0 ? 1.0 : -1.0, Corner, AlongMain, Reach);
					FitCorner(M, Sw, Corner, AlongMain, Reach);
					NearOpts Opts;
					Opts.OnlyRoad = 0;
					NearHit Hit;
					const P2 Probe = Corner + AlongMain * Reach;
					Good = NearestOf(Probe.X, Probe.Z, Opts, Hit) && !Hit.Deck && !Hit.Bore;
					if (!Good) break;
					Ends[Sg] = Hit.Along;
					OnSide = Hit.Off >= 0.0 ? 0 : 1;
				}
				// [true T] (on the far side of a through road's arm, out to where its far edge meets the ride's road)
				const auto Far = Sr->Edge >= 0 ? FarEdges.find(static_cast<long long>(Sr->Edge) * 2 + End) : FarEdges.end();
				if (Good && Far != FarEdges.end())
				{
					const int FarSg = Far->second.Sigma > 0.0 ? 0 : 1;
					Ends[FarSg] = Ends[FarSg] >= Ends[1 - FarSg] ? std::fmax(Ends[FarSg], Far->second.RideTo) : std::fmin(Ends[FarSg], Far->second.RideFrom);
				}
				if (Good && OnSide >= 0 && std::fabs(Ends[1] - Ends[0]) < 60.0)
					MouthGap[OnSide].push_back({std::fmin(Ends[0], Ends[1]), std::fmax(Ends[0], Ends[1])});
			}
		}
	}
	const int LidArch = 4;   // (the lid's first point over the arch)
	std::vector<P3> RingA, RingB, LidA, LidB;
	const double PaintLift = 0.015;
	const double PaintOverLift = 0.004;   // ([paint, Nov 18] lines on a gravel road's tarmac tongue, the only ones still laid over it)
	std::vector<RoadMark> MarksHere;
	// [roundabouts] The rings drawn as circles this piece comes onto: their own markings are RingPaint's,
	// so none of the road's own lines (cut short and stepped where its edges met) on their tarmac.
	std::vector<const RoadRing*> RingsHere;
	{
		double Bx0 = 1e18, Bz0 = 1e18, Bx1 = -1e18, Bz1 = -1e18;
		for (const Row& Rw : Rows) { Bx0 = std::fmin(Bx0, Rw.At.X); Bx1 = std::fmax(Bx1, Rw.At.X); Bz0 = std::fmin(Bz0, Rw.At.Z); Bz1 = std::fmax(Bz1, Rw.At.Z); }
		for (const RoadRing& Rg : RoadRings)
		{
			const double Pad = Rg.Reach + Rg.Half + 2.0;
			if (Rg.Circle && Rg.Mid.X > Bx0 - Pad && Rg.Mid.X < Bx1 + Pad && Rg.Mid.Z > Bz0 - Pad && Rg.Mid.Z < Bz1 + Pad) RingsHere.push_back(&Rg);
		}
	}
	// ([roundabouts round 2] and those near enough that a road's grass shoulder may land on their tarmac)
	std::vector<const RoadRing*> RingsAbout;
	{
		double Bx0 = 1e18, Bz0 = 1e18, Bx1 = -1e18, Bz1 = -1e18;
		for (const Row& Rw : Rows) { Bx0 = std::fmin(Bx0, Rw.At.X); Bx1 = std::fmax(Bx1, Rw.At.X); Bz0 = std::fmin(Bz0, Rw.At.Z); Bz1 = std::fmax(Bz1, Rw.At.Z); }
		for (const RoadRing& Rg : RoadRings)
		{
			const double Pad = Rg.Reach + Rg.Half + RingClearReach;
			if (Rg.Circle && Rg.Mid.X > Bx0 - Pad && Rg.Mid.X < Bx1 + Pad && Rg.Mid.Z > Bz0 - Pad && Rg.Mid.Z < Bz1 + Pad) RingsAbout.push_back(&Rg);
		}
	}
	const auto OnRingTarmac = [&RingsHere](const P2& At)
	{
		for (const RoadRing* Rg : RingsHere)
			if (std::fabs(Rg->OffOf(At)) <= Rg->Half + 0.3) return true;
		return false;
	};
	struct SplitBit
	{
		size_t K = 0;
		int Sd = 0;
		Row A, B;
		double F0 = 0.0, F1 = 0.0;
	};
	std::vector<SplitBit> Splits;
	// ([paint, Nov 18] a bit of road's lines, painted into its tarmac: each from T0 to T1 along the bit (0 its
	// first row, 1 its last), its left and right offsets at T0 (L0, R0) and at T1 (L1, R1))
	struct PaintCut
	{
		double T0 = 0.0, T1 = 1.0, L0 = 0.0, R0 = 0.0, L1 = 0.0, R1 = 0.0;
		const float* Colour = nullptr;
	};
	std::vector<PaintCut> PaintCuts;
	const auto Cut = [&PaintCuts](double T0, double T1, double L0, double R0, double L1, double R1, const float* Colour)
	{
		if (T1 - T0 < 1e-6 || Colour == nullptr) return;
		PaintCut C;
		C.T0 = T0; C.T1 = T1; C.L0 = L0; C.R0 = R0; C.L1 = L1; C.R1 = R1; C.Colour = Colour;
		PaintCuts.push_back(C);
	};
	// ([Nov 20] which rows' bridge has the wall kit's walls: a whole run of deck or none of it)
	std::vector<char> WallRow(Rows.size(), 0), RailRow(Rows.size(), 0);
	for (size_t S0 = 0; S0 < Rows.size(); ++S0)
	{
		if (!Rows[S0].Deck) continue;
		size_t S1 = S0;
		while (S1 + 1 < Rows.size() && Rows[S1 + 1].Deck) ++S1;
		bool Country = true, Plain = true;
		for (size_t J = S0; J <= S1; ++J) Country = Country && StoneParapet(Rows[J].Half, Rows[J].Lanes);
		for (size_t J = S0; J <= S1; ++J) Plain = Plain && Rows[J].Half <= 4.5 && Rows[J].Lanes <= 2;   // [everyday kits stage 7]
		for (size_t J = S0; J <= S1; ++J) WallRow[J] = Country ? 1 : 0;
		for (size_t J = S0; J <= S1; ++J) RailRow[J] = RailsHere && !Plain ? 1 : 0;   // (a wide road's: the bridge set's parapet)
		S0 = S1;
	}
	for (size_t K = 0; K + 1 < Rows.size(); ++K)
	{
		Row RowA = Rows[K], RowB = Rows[K + 1];
		// A side road starts at the ride's road's edge, not under it, and its own shoulders start where
		// a mouth's rounded corners end; the ride's road has none across a mouth.
		const auto Between = [](const Row& From, const Row& To, double T)
		{
			Row R = From;
			R.At = From.At + (To.At - From.At) * T;
			// ([square T] its left between the two rows' too, so a line cut in part way along lies where it
			// would on the undivided piece: where the rows fan round a junction's arc, the left as at From put
			// the far side of a give-way line half a metre out of place, Cullingworth)
			R.Left = From.Left + (To.Left - From.Left) * T;
			R.Height += (To.Height - From.Height) * T;
			R.Along += (To.Along - From.Along) * T;
			R.Half += (To.Half - From.Half) * T;
			R.Bank += (To.Bank - From.Bank) * T;
			return R;
		};
		bool NoShoulder[2] = {false, false};   // left, right
		double Fill[2][2] = {{0.0, 0.0}, {0.0, 0.0}};   // [roundabouts round 3] (tarmac out over a narrow gap to the next road: left, right; at A, at B)
		bool Splitter[2] = {false, false};   // [roundabouts 156] (that gap to another road, not the ring: a painted island)
		double ShoulderFrom[2] = {0.0, 0.0}, ShoulderTo[2] = {1.0, 1.0};   // (the part of this bit with a shoulder, 0 at A to 1 at B)
		bool UnderMain = false;
		for (int End = 0; End < 2 && Which > 0; ++End)
		{
			const SideRoad& Side = *Mine;
			const Mouth& M = Side.Mouths[End];
			if (!M.Is) continue;
			double Fa = End == 0 ? RowA.Along : Side.Length - RowA.Along, Fb = End == 0 ? RowB.Along : Side.Length - RowB.Along;
			// ([smooth junctions 3b] a mouth drawn whole: its piece is the road up to where its corners end)
			const double Hide = std::fmax(M.From, MouthCut(Which, End));
			if (std::fmax(Fa, Fb) <= Hide) { UnderMain = true; break; }
			if (std::fmin(Fa, Fb) < Hide)
			{
				const double T = (Hide - Fa) / (Fb - Fa);
				if (Fa < Fb) { RowA = Between(RowA, RowB, T); Fa = Hide; }
				else { RowB = Between(RowA, RowB, T); Fb = Hide; }
			}
			// (the left as this road's points run is Away's left from its first end, its right from its last)
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				const double Clear = M.Clear[End == 0 ? Sd : 1 - Sd];
				if (std::fmax(Fa, Fb) <= Clear) NoShoulder[Sd] = true;
				else if (std::fmin(Fa, Fb) < Clear)
				{
					const double T = (Clear - Fa) / (Fb - Fa);
					if (Fa < Fb) ShoulderFrom[Sd] = std::fmax(ShoulderFrom[Sd], T);
					else ShoulderTo[Sd] = std::fmin(ShoulderTo[Sd], T);
				}
			}
		}
		if (UnderMain) continue;
		// [true T] (a side road's shoulder, near a junction, not over another road's tarmac: Buxworth, a track's
		// grass verge lay across New Road's lane by the junction, the two side by side into it)
		if (Which > 0)
		{
			bool NearEnd = false;
			for (int End = 0; End < 2; ++End)
			{
				const Mouth& M = Mine->Mouths[End];
				const double Fa = End == 0 ? RowA.Along : Mine->Length - RowA.Along;
				NearEnd = NearEnd || (M.Is && Fa < M.From + 40.0);
			}
			for (int Sd = 0; Sd < 2 && NearEnd; ++Sd)
			{
				const double S = Sd == 0 ? 1.0 : -1.0;
				const auto OnOther = [&](const Row& Rw)
				{
					const P2 P = Rw.At + Rw.Left * (S * (Rw.Half + 0.8));
					NearOpts Opts;
					Opts.NotRoad = Which;
					NearHit Hit;
					return NearestOf(P.X, P.Z, Opts, Hit) && !Hit.Deck && !Hit.Bore && Hit.Dist < Hit.Half - 0.1;
				};
				const bool OnA = OnOther(RowA), OnB = OnOther(RowB);
				if (OnA && OnB) NoShoulder[Sd] = true;
				else if (OnA) ShoulderFrom[Sd] = std::fmax(ShoulderFrom[Sd], 0.5);
				else if (OnB) ShoulderTo[Sd] = std::fmin(ShoulderTo[Sd], 0.5);
			}
		}
		if (Which == 0 && size_t(First) + K + 1 < MouthAt.size())
		{
			const unsigned char Both = MouthAt[size_t(First) + K] & MouthAt[size_t(First) + K + 1];
			NoShoulder[0] = (Both & 4) != 0;
			NoShoulder[1] = (Both & 8) != 0;
			// ([OSM stage 5D] a mouth's exact gap, where one is near, in place of whole rows)
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				bool NearGap = false;
				for (const std::pair<double, double>& Gp : MouthGap[Sd]) NearGap = NearGap || (Gp.first < RowB.Along + 8.0 && Gp.second > RowA.Along - 8.0);
				if (!NearGap) continue;
				NoShoulder[Sd] = false;
				const double Len = RowB.Along - RowA.Along;
				for (const std::pair<double, double>& Gp : MouthGap[Sd])
				{
					if (Gp.second <= RowA.Along || Gp.first >= RowB.Along || Len <= 1e-6) continue;
					const double T0 = (Gp.first - RowA.Along) / Len, T1 = (Gp.second - RowA.Along) / Len;
					if (T0 <= 0.0 && T1 >= 1.0) NoShoulder[Sd] = true;
					else if (T0 > 0.0) ShoulderTo[Sd] = std::fmin(ShoulderTo[Sd], T0);   // (the gap starts in this bit)
					else ShoulderFrom[Sd] = std::fmax(ShoulderFrom[Sd], T1);             // (it ends in it)
				}
			}
		}
		// Where a side road meets another side road or runs over one (build 140: pale patches and a
		// bump at such junctions): none of its shoulder lies on another road's tarmac.
		if (Which > 0 && !(RowA.Deck && RowB.Deck))
		{
			NearOpts Others;
			Others.NotRoad = Which;
			Others.NoDecks = true;
			Others.NoBores = true;
			const auto OnOther = [&](const P2& P, double OwnHeight)
			{
				// ([build 150] from the bits gathered for the piece, the same answer: a point on another
				// road's tarmac has that road's edge within JoinLean)
				if (Joins.Holds(P))
				{
					JoinHit Near;
					return NearestIn(Joins, P, false, Near) && Near.Dist < Near.Half && std::fabs(Near.Height - OwnHeight) < 1.5;
				}
				NearHit Hit;
				return NearestOf(P.X, P.Z, Others, Hit) && Hit.Dist < Hit.Half && std::fabs(Hit.Height - OwnHeight) < 1.5;
			};
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				const double Sign = Sd == 0 ? 1.0 : -1.0;
				const P2 Pa = RowA.At + RowA.Left * (Sign * (RowA.Half + Shoulder)), Pb = RowB.At + RowB.Left * (Sign * (RowB.Half + Shoulder));
				const P2 Ia = RowA.At + RowA.Left * (Sign * (RowA.Half + ShoulderFlat * 0.5)), Ib = RowB.At + RowB.Left * (Sign * (RowB.Half + ShoulderFlat * 0.5));
				if (OnOther(Pa, RowA.Height) || OnOther(Pb, RowB.Height) || OnOther(Ia, RowA.Height) || OnOther(Ib, RowB.Height))
					NoShoulder[Sd] = true;
			}
		}
		// [roundabouts round 2] Near a roundabout (beenster: "green lumps round the edges"): the
		// roads there cross and overlap (flared mouths, splitter carriageways, the ring itself), so a
		// grass shoulder that lands anywhere on tarmac, the ring's or any other road's, is left out,
		// tested all along the bit and not only at its ends.
		if (!RingsAbout.empty() && !(RowA.Deck || RowB.Deck) && !(RowA.Bore || RowB.Bore))
		{
			bool Close = false;
			for (const RoadRing* Rg : RingsAbout)
				Close = Close || std::fmin(Rg->OffOf(RowA.At), Rg->OffOf(RowB.At)) < Rg->Half + RingClearReach;
			// [roundabouts round 4] (beenster's circled bits: short ends of grass and pavement left on the
			// junction's tarmac) In the mouths, RingMouthClear m out from the ring's edge, a road onto it has
			// no shoulder or pavement of its own: the flared corners' kerbs and grass meet the ring there. (The
			// ring's own outer shoulder stays.)
			for (const RoadRing* Rg : RingsAbout)
			{
				const double Oa = Rg->OffOf(RowA.At), Ob = Rg->OffOf(RowB.At);
				const bool OnRing = std::fabs(Oa) < Rg->Half + 0.5 && std::fabs(Ob) < Rg->Half + 0.5;
				if (!OnRing && std::fmin(Oa, Ob) < Rg->Half + RingMouthClear) { NoShoulder[0] = NoShoulder[1] = true; }
			}
			if (Close)
			{
				NearOpts Others;
				Others.NotRoad = Which;
				Others.NoDecks = true;
				Others.NoBores = true;
				const auto OnTar = [&](const P2& P, double OwnHeight)
				{
					for (const RoadRing* Rg : RingsAbout)
						if (Rg->OffOf(P) < Rg->Half + 0.3) return true;
					NearHit Hit;
					return NearestOf(P.X, P.Z, Others, Hit) && Hit.Dist < Hit.Half + 0.2 && std::fabs(Hit.Height - OwnHeight) < 1.5;
				};
				// [roundabouts round 3] (beenster: "still a little too much grass mess") A gap of under 3 m to
				// the next road's tarmac all along the bit (a sliver of field between a mouth's carriageways, a
				// slip road and the ring): tarmac across it, not a strip of grass and ground.
				const auto OnRoad = [&](const P2& P, double OwnHeight)
				{
					for (const RoadRing* Rg : RingsAbout)
					{
						const double O = Rg->OffOf(P);
						if (O < -Rg->Half - 0.1) return -1;   // (the island: never)
						if (O < Rg->Half) return 2;
					}
					NearHit Hit;
					return NearestOf(P.X, P.Z, Others, Hit) && Hit.Dist < Hit.Half && std::fabs(Hit.Height - OwnHeight) < 1.0 ? 1 : 0;
				};
				for (int Sd = 0; Sd < 2; ++Sd)
				{
					const double Sign = Sd == 0 ? 1.0 : -1.0;
					double Gap[3] = {-1.0, -1.0, -1.0};
					bool ToRing = false;
					for (int Step = 0; Step < 3; ++Step)
					{
						const Row Rw = Between(RowA, RowB, Step * 0.5);
						// ([round 4] beenster's circled slivers and wedges: up to RingGapFill to another road, 3 m
						// to the ring's own edge, so the grass corners between its arms stay)
						for (double Off = 0.3; Off <= RingGapFill + 1e-9; Off += 0.3)
						{
							const int On = OnRoad(Rw.At + Rw.Left * (Sign * (Rw.Half + Off)), Rw.Height);
							if (On < 0 || (On == 2 && Off > 3.0)) break;
							if (On > 0) { Gap[Step] = Off; ToRing = ToRing || On == 2; break; }
						}
					}
					// (a wedge too: the gap closing to nothing at one end, as between a mouth's carriageways)
					const int Hits = (Gap[0] > 0.0 ? 1 : 0) + (Gap[1] > 0.0 ? 1 : 0) + (Gap[2] > 0.0 ? 1 : 0);
					if (Hits >= 2 && Gap[1] > 0.0)
					{
						Fill[Sd][0] = (Gap[0] > 0.0 ? std::fmax(Gap[0], 0.5 * Gap[1]) : 0.5 * Gap[1]) + 0.3;
						Fill[Sd][1] = (Gap[2] > 0.0 ? std::fmax(Gap[2], 0.5 * Gap[1]) : 0.5 * Gap[1]) + 0.3;
						NoShoulder[Sd] = true;
						Splitter[Sd] = !ToRing && std::fmax(Gap[0], std::fmax(Gap[1], Gap[2])) >= 1.2;
					}
				}
				for (int Sd = 0; Sd < 2; ++Sd)
				{
					if (NoShoulder[Sd]) continue;
					const double Sign = Sd == 0 ? 1.0 : -1.0;
					for (int Step = 0; Step <= 4 && !NoShoulder[Sd]; ++Step)
					{
						const double T = Step / 4.0;
						const Row Rw = Between(RowA, RowB, T);
						for (const double Off : {0.3, ShoulderFlat * 0.5, Shoulder, PaveWide + 0.35})
							if (OnTar(Rw.At + Rw.Left * (Sign * (Rw.Half + Off)), Rw.Height)) { NoShoulder[Sd] = true; break; }
					}
				}
			}
		}
		for (const Apron& S : PullNear)
		{
			// [places stage 2] (a pull-off's gravel runs on from the road's edge: no grass shoulder under it;
			// [roads stage 7] nor kerb and pavement across a forecourt's mouth)
			double Ua = 0.0, Ub = 0.0, Oa = 0.0, Ob = 0.0;
			PullSpot(S, RowA.At, Ua, Oa);
			PullSpot(S, RowB.At, Ub, Ob);
			const double Reach = S.Reach - 2.0;
			if (std::fabs(Oa) > 2.0 || std::fabs(Ob) > 2.0 || std::fmax(Ua, Ub) < -Reach || std::fmin(Ua, Ub) > Reach) continue;
			NoShoulder[(RowA.Left.X * S.Out.X + RowA.Left.Z * S.Out.Z) >= 0.0 ? 0 : 1] = true;
		}
		const Row& A = RowA;
		const Row& B = RowB;
		AlongBase = std::floor(A.Along / 36.0) * 36.0;
		if (!(A.Bore && B.Bore)) { RingA.clear(); LidA.clear(); }
		// [OSM stage 1] What this bit of road is: a farm track has no paint; a gravel one is drawn in
		// the verge's grit, not tarmac (and the tyres feel it: SurfaceAt).
		bool Track = false, Gravel = false, Setts = false;
		if (Which == 0)
		{
			if (const RoadFact* F = FactAt((A.Along + B.Along) * 0.5))
			{
				Track = F->Class == "track" || F->Class == "service";
				Gravel = F->Gravel;
				Setts = F->Setts;
			}
		}
		else
		{
			Track = Mine->Track;
			Gravel = Mine->Gravel;
			Setts = Mine->Setts;
		}
		// ([paint, Nov 18] beenster: "paint the roads with lines": the lines are no longer strips laid 1.5 cm over
		// the road but painted into its tarmac, the tarmac cut where each one runs and that piece drawn in
		// the paint's colour, flush, so nothing stands proud (the bike rides over them flat) and nothing is
		// drawn twice. Each line is gathered first, as a piece of this bit of road (from T0 to T1 along it,
		// its left and right offsets at each end); Lay draws the tarmac round them.)
		PaintCuts.clear();
		// (the tarmac across from edge to edge, in Colour, with the lines cut into it: split along at each
		// line's ends, then across at its edges and at the crown, each piece flush with the road)
		const auto Lay = [&](const float* Colour)
		{
			std::vector<double> Ts = {0.0, 1.0};
			for (const PaintCut& C : PaintCuts) { Ts.push_back(std::clamp(C.T0, 0.0, 1.0)); Ts.push_back(std::clamp(C.T1, 0.0, 1.0)); }
			std::sort(Ts.begin(), Ts.end());
			Ts.erase(std::unique(Ts.begin(), Ts.end(), [](double P, double Q) { return std::fabs(P - Q) < 1e-6; }), Ts.end());
			struct LineAcross { double La, Ra, Lb, Rb; const float* C; };
			std::vector<LineAcross> Lines;
			for (size_t I = 0; I + 1 < Ts.size(); ++I)
			{
				const double Ta = Ts[I], Tb = Ts[I + 1];
				const Row Ra = Ta <= 0.0 ? A : Between(A, B, Ta), Rb = Tb >= 1.0 ? B : Between(A, B, Tb);
				Lines.clear();
				for (const PaintCut& C : PaintCuts)
				{
					if (C.T0 > Ta + 1e-6 || C.T1 < Tb - 1e-6) continue;
					const double Fa = (Ta - C.T0) / (C.T1 - C.T0), Fb = (Tb - C.T0) / (C.T1 - C.T0);
					LineAcross L;
					L.La = std::fmin(C.L0 + (C.L1 - C.L0) * Fa, Ra.Half); L.Ra = std::fmax(C.R0 + (C.R1 - C.R0) * Fa, -Ra.Half);
					L.Lb = std::fmin(C.L0 + (C.L1 - C.L0) * Fb, Rb.Half); L.Rb = std::fmax(C.R0 + (C.R1 - C.R0) * Fb, -Rb.Half);
					L.C = C.Colour;
					if (L.La > L.Ra && L.Lb > L.Rb) Lines.push_back(L);
				}
				std::sort(Lines.begin(), Lines.end(), [](const LineAcross& P, const LineAcross& Q) { return P.La + P.Lb > Q.La + Q.Lb; });
				// (a piece from left to right, split at the crown where it runs over it)
				const auto Piece = [&](double La, double Rra, double Lb, double Rrb, const float* C)
				{
					if (La - Rra < 1e-4 && Lb - Rrb < 1e-4) return;
					if (La > 1e-4 && Rra < -1e-4 && Lb > 1e-4 && Rrb < -1e-4)
					{
						Strip(Ra, Rb, La, 0.0, Lb, 0.0, 0.0, 0.0, C);
						Strip(Ra, Rb, 0.0, Rra, 0.0, Rrb, 0.0, 0.0, C);
					}
					else Strip(Ra, Rb, La, Rra, Lb, Rrb, 0.0, 0.0, C);
				};
				double Ca = Ra.Half, Cb = Rb.Half;
				for (const LineAcross& L : Lines)
				{
					const double La = std::fmin(L.La, Ca), Lb = std::fmin(L.Lb, Cb);
					if (L.Ra >= Ca && L.Rb >= Cb) continue;   // (wholly under a line already painted)
					Piece(Ca, La, Cb, Lb, Colour);
					Piece(La, std::fmin(L.Ra, La), Lb, std::fmin(L.Rb, Lb), L.C);
					Ca = std::fmin(L.Ra, La); Cb = std::fmin(L.Rb, Lb);
				}
				Piece(Ca, -Ra.Half, Cb, -Rb.Half, Colour);
			}
			PaintCuts.clear();
		};
		// [roundabouts] (none of the road's own lines on a ring's tarmac: its markings are RingPaint's)
		const bool OnRingTar = !RingsHere.empty() && (OnRingTarmac(A.At) || OnRingTarmac(B.At));
		// [roundabouts 156] (no yellow lines in a roundabout's mouth: the kerb there curves away round the
		// flare, and the lines ran on straight across it (PC check, build 155, York compact ring))
		bool YellowMouth = false;
		for (const RoadRing* Rg : RingsAbout)
		{
			const double Oa = Rg->OffOf(A.At), Ob = Rg->OffOf(B.At);
			const bool OnRing = std::fabs(Oa) < Rg->Half + 0.5 && std::fabs(Ob) < Rg->Half + 0.5;
			YellowMouth = YellowMouth || (!OnRing && std::fmin(Oa, Ob) < Rg->Half + RingMouthClear + 3.0);
		}
		// ([square T] a side road's kerbside lines stop where its kerb does, where the corner's rounding
		// starts: none out over the mouth where the side road runs on to the junction's middle (Buxworth,
		// New Road's yellow lines across Brookside's mouth))
		const double SideEnd = Which != 0 ? std::fmin(A.Along, RoadOf(Which).back().Along - B.Along) : 1e18;
		const auto OnRideTarmac = [&](double Side)
		{
			if (SideEnd > 25.0) return false;
			const P2 P = (A.At + A.Left * (Side * (A.Half - 0.36)) + B.At + B.Left * (Side * (B.Half - 0.36))) * 0.5;
			NearOpts Opts;
			Opts.MainOnly = true;
			NearHit Hit;
			return NearestOf(P.X, P.Z, Opts, Hit) && !Hit.Deck && !Hit.Bore && Hit.Dist < Hit.Half + 5.5;   // (a corner's 6 m radius)
		};
		if (!(A.Along < PaintFrom || B.Along > PaintTo || Track || Gravel || Setts || OnRingTar))
		{
			const double Half = std::fmin(A.Half, B.Half);
			// Paint: edge lines on a road 6 m wide or more, and where there are two lanes a centre line of
			// 3 m dashes with 6 m gaps.
			// ([OSM stage 5D] none along a town street's kerbs)
			// ([junction priority] nor any of the ride's own lines across a through road it gives way to)
			const bool AcrossThrough = Which == 0 && (LinesStopAt(A.Along) || LinesStopAt(B.Along));
			if (Half >= 2.95 && !(StreetRow[K] != 0 && StreetRow[K + 1] != 0) && !AcrossThrough)
			{
				for (double Side : {1.0, -1.0})
				{
					// (none across a junction's mouth)
					const size_t At0 = size_t(First) + K;
					if (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) continue;
					if (OnRideTarmac(Side)) continue;
					// ([OSM stage 5C] on a motorway's nearside, between the lanes and the hard shoulder: 0.2 m wide)
					const bool Hard = A.Near != 0 && Side == double(A.Near) && A.Shoulder > 0.5 && B.Shoulder > 0.5;
					// ([road rulebook D6] 1012.1: 100 mm wide, its outer side 225 mm in from the edge as seen, past the worn grass)
					const double In = RoadX::EdgeWorn + RoadX::EdgeLineIn + RoadX::EdgeLineHalfWidth;
					const double Ia = Hard ? A.Half - A.Shoulder : A.Half - In, Ib = Hard ? B.Half - B.Shoulder : B.Half - In, W = Hard ? 0.1 : RoadX::EdgeLineHalfWidth;
					Cut(0.0, 1.0, Side * Ia + W, Side * Ia - W, Side * Ib + W, Side * Ib - W, Paint);
				}
			}
			// [OSM stage 5D] Double yellow lines along a town street's kerbs near a junction (no parking
			// within 10 m of one, as the Highway Code has it): 75 mm lines 75 mm apart, 0.25 m off the kerb,
			// for YellowReach m from a side road's ends and either side of a mouth onto the ride's road.
			// ([OSM stage 5D] and wherever the map says: no parking, double yellow; no stopping, double red;
			// marked bays, a line 2 m out from the kerb with a tick across every 6 m)
			if (Half >= 2.4 && !A.Deck && !A.Bore && !YellowMouth)
				for (double Side : {1.0, -1.0})
				{
					const int Code = (A.Park >> (Side > 0.0 ? 0 : 2)) & 3;
					const size_t At0 = size_t(First) + K;
					if (Code == 0 || (Code == 3 && Half < 2.9) || (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) || OnRideTarmac(Side)) continue;
					if (Code == 3)
					{
						// (as deep as a car, less on a narrow street: a lane's width left between)
						const double BayDeep = std::clamp(Half - 1.2, 1.6, BayDeepest);
						Cut(0.0, 1.0, Side * (A.Half - BayDeep) + 0.05, Side * (A.Half - BayDeep) - 0.05, Side * (B.Half - BayDeep) + 0.05, Side * (B.Half - BayDeep) - 0.05, Paint);
						for (double Tick = std::ceil(A.Along / BayLong) * BayLong; Tick < B.Along && B.Along > A.Along; Tick += BayLong)
						{
							const double T0 = (Tick - A.Along) / (B.Along - A.Along), T1 = std::fmin(1.0, (Tick + 0.1 - A.Along) / (B.Along - A.Along));
							const Row Ta = Between(A, B, T0);
							const double Lo = std::fmin(Side * (Ta.Half - 0.05), Side * (Ta.Half - BayDeep)), Hi = std::fmax(Side * (Ta.Half - 0.05), Side * (Ta.Half - BayDeep));
							Cut(T0, T1, Hi, Lo, Hi, Lo, Paint);
						}
						continue;
					}
					// (no-parking's lines from the junction rule below are drawn there instead)
					if (Code == 1 && StreetRow[K] != 0 && StreetRow[K + 1] != 0) continue;
					for (const double In : {0.29, 0.44})
						Cut(0.0, 1.0, Side * (A.Half - In) + 0.0375, Side * (A.Half - In) - 0.0375, Side * (B.Half - In) + 0.0375, Side * (B.Half - In) - 0.0375, Code == 2 ? RedLine : Yellow);
				}
			if (StreetRow[K] != 0 && StreetRow[K + 1] != 0 && Half >= 2.4 && !YellowMouth)
			{
				const size_t At0 = size_t(First) + K;
				bool NearJunction = false;
				if (Which == 0)
				{
					for (size_t P = At0; P < StreetMouthAt.size() && P < Pts.size() && Pts[P].Along - A.Along < YellowReach && !NearJunction; ++P) NearJunction = StreetMouthAt[P] != 0;
					for (size_t P = At0 + 1; P-- > 0 && P < StreetMouthAt.size() && A.Along - Pts[P].Along < YellowReach && !NearJunction;) NearJunction = StreetMouthAt[P] != 0;
				}
				else
					NearJunction = A.Along < YellowReach || Pts.back().Along - B.Along < YellowReach;
				for (double Side : {1.0, -1.0})
				{
					if (!NearJunction && ((A.Park >> (Side > 0.0 ? 0 : 2)) & 3) != 1) continue;
					if ((Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) || OnRideTarmac(Side)) continue;
					for (const double In : {0.29, 0.44})
						Cut(0.0, 1.0, Side * (A.Half - In) + 0.0375, Side * (A.Half - In) - 0.0375, Side * (B.Half - In) + 0.0375, Side * (B.Half - In) - 0.0375, Yellow);
				}
			}
			// [OSM stage 5B] Where the ride's road comes onto a roundabout: one broken line across its own lane
			// (0.6 m marks, 0.3 m gaps, 0.2 m deep), giving way to the ring.
			if (Which == 0)
				for (const double Give : RingGiveWays)
				{
					if (Give < A.Along || Give >= B.Along || B.Along <= A.Along) continue;
					const double T0 = (Give - A.Along) / (B.Along - A.Along), T1 = std::min((Give + 0.2 - A.Along) / (B.Along - A.Along), 1.0);
					const Row Ga = Between(A, B, T0);
					const double Keep = KeepLeft() ? 1.0 : -1.0;
					for (double U = 0.15; U + 0.6 <= Ga.Half - 0.15 + 1e-9; U += 0.9)
					{
						const double U0 = Keep * U, U1 = Keep * (U + 0.6);
						Cut(T0, T1, std::fmax(U0, U1), std::fmin(U0, U1), std::fmax(U0, U1), std::fmin(U0, U1), Paint);
					}
				}
			// ([OSM stage 5] a centre line where the map's lanes say, else by the rule; lane lines between a
			// one-way road's lanes and within each half of a wide two-way one: Britain's 4 m dashes, 5 m gaps)
			// ([road rulebook D1, D5, C6] marks and gaps by the limit, cut to their own lengths within this stretch: the
			// road's points lie 4 m apart, and a dash per stretch made every line 4 m marks with 4 or 8 m gaps)
			if (!AcrossThrough && B.Along > A.Along + 1e-6)
			{
				const int Mph = PaintMph(Which, 0.5 * (A.Along + B.Along), StreetRow[K] != 0);
				const auto Dashes = [&](const RoadX::DashPattern& P, const auto& Draw)
				{
					for (double S0 = std::floor(A.Along / P.Cycle) * P.Cycle; S0 < B.Along; S0 += P.Cycle)
					{
						const double T0 = std::clamp((S0 - A.Along) / (B.Along - A.Along), 0.0, 1.0), T1 = std::clamp((S0 + P.Mark - A.Along) / (B.Along - A.Along), 0.0, 1.0);
						if (T1 - T0 > 1e-4) Draw(T0, T1);
					}
				};
				if (RoadX::CentreLine(A.Lanes, A.OneWay, Half))
				{
					const double Cw = RoadX::CentreHalfWidth(Half);
					Dashes(WarnAt(0.5 * (A.Along + B.Along)) ? RoadX::WarningDash(Mph) : RoadX::CentreDash(Mph), [&](double T0, double T1) { Cut(T0, T1, Cw, -Cw, Cw, -Cw, Paint); });
				}
				// ([OSM stage 5C] a motorway's lanes between its offside edge and its hard shoulder)
				const double Ca = -double(A.Near) * A.Shoulder * 0.5, Cb = -double(B.Near) * B.Shoulder * 0.5;
				const double Ha = A.Half - A.Shoulder * 0.5, Hb = B.Half - B.Shoulder * 0.5;
				for (const double Off : RoadX::LaneLines(A.Lanes, A.OneWay, 1.0))
					Dashes(RoadX::LaneDash(Mph), [&](double T0, double T1)
					{
						const double C0 = Ca + (Cb - Ca) * T0 + Off * (Ha + (Hb - Ha) * T0), C1 = Ca + (Cb - Ca) * T1 + Off * (Ha + (Hb - Ha) * T1);
						Cut(T0, T1, C0 + 0.05, C0 - 0.05, C1 + 0.05, C1 - 0.05, Paint);
					});
			}
		}
		// ([paint, Nov 18] the map's own lines and crossings (WorldRideRoadX's marks) painted the same way)
		if (!A.Bore && B.Along > A.Along + 1e-6)
		{
			RoadMarks(Which, A.Along, B.Along, MarksHere);
			for (const RoadMark& Mk : MarksHere)
			{
				if (Mk.Kind != MarkPaint) continue;
				const double Ta = std::clamp((Mk.From - A.Along) / (B.Along - A.Along), 0.0, 1.0), Tb = std::clamp((Mk.To - A.Along) / (B.Along - A.Along), 0.0, 1.0);
				// ([junction priority 168] a slanting line's sides where this bit of it starts and ends)
				const double Aa = A.Along + (B.Along - A.Along) * Ta, Ab = A.Along + (B.Along - A.Along) * Tb;
				if (Tb - Ta >= 1e-4) Cut(Ta, Tb, Mk.LeftAt(Aa), Mk.RightAt(Aa), Mk.LeftAt(Ab), Mk.RightAt(Ab), Mk.Colour);
			}
		}
		static constexpr float Grit[3] = {0.095f, 0.088f, 0.07f};
		// ([towns stage 6] setts and cobbles: M_RideRoad's key for its setts, a grey-blue no other strip uses)
		static constexpr float SettsKey[3] = {0.09f, 0.095f, 0.11f};
		// The tarmac, in two halves either side of the crown.
		// (beenster, build 141: a gravel road off tarmac fades into its gravel over its first metres. The
		// tarmac reaches out a ragged tongue, each lane of it worn back its own way, and loose stones
		// thrown from the gravel lie on it, thicker towards its end)
		const double FadeLen = 12.0;
		const double FadeD = Gravel ? std::fmin(GravelFade(A.Along), GravelFade(B.Along)) : 1e18;
		if (FadeD < FadeLen)
		{
			constexpr int Lanes = 8;
			double Lim[Lanes + 1];
			for (int Ln = 0; Ln <= Lanes; ++Ln)
			{
				const double Fr = 1.0 - 2.0 * Ln / Lanes;
				Lim[Ln] = 6.0 + 3.0 * (RideSceneBits::Chance(double(Which), double(Ln), 76) - 0.5) - 2.0 * Fr * Fr;
			}
			// (a quad from two rows at their own offsets: left and right at the near row, then the far one)
			const auto Patch = [&Into, &Put](const Row& L0, double OL0, const Row& R0, double OR0, const Row& L1, double OL1, const Row& R1, double OR1, double Lift, const float* Colour)
			{
				RideMesh& M = *Into;
				const uint32_t V = uint32_t(M.Vertices());
				Put(L0, OL0, Lift, Colour);
				Put(R0, OR0, Lift, Colour);
				Put(L1, OL1, Lift, Colour);
				Put(R1, OR1, Lift, Colour);
				M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
			};
			// (beenster, Nov 13: the tongue's edge was "too low res". Now each vertex's colour runs from the
			// tarmac's to the grit's over a few metres, each lane's reach its own; M_RideRoad reads how far
			// along that line a pixel's colour is and lets the gravel scan's stones break through the
			// scanned tarmac by their own height, so the edge is as fine as the scans)
			const auto Mixed = [&](double D, int Ln, float* C)
			{
				const double G = std::clamp((D - Lim[Ln]) / 4.0 + 0.5, 0.0, 1.0);
				for (int Ch = 0; Ch < 3; ++Ch) C[Ch] = float(Tarmac[Ch] + (Grit[Ch] - Tarmac[Ch]) * G);
			};
			const int Downs = std::max(1, int(std::ceil(B.At.DistanceTo(A.At) / 1.0)));
			for (int Dn = 0; Dn < Downs; ++Dn)
			{
				const Row R0 = Between(A, B, double(Dn) / Downs), R1 = Between(A, B, double(Dn + 1) / Downs);
				const double D0 = GravelFade(R0.Along), D1 = GravelFade(R1.Along);
				for (int Ln = 0; Ln < Lanes; ++Ln)
				{
					const double F0 = 1.0 - 2.0 * Ln / Lanes, F1 = 1.0 - 2.0 * (Ln + 1) / Lanes;   // (left to right)
					float C00[3], C01[3], C10[3], C11[3];
					Mixed(D0, Ln, C00); Mixed(D0, Ln + 1, C01); Mixed(D1, Ln, C10); Mixed(D1, Ln + 1, C11);
					RideMesh& M = *Into;
					const uint32_t Base4 = uint32_t(M.Vertices());
					Put(R0, F0 * R0.Half, 0.0, C00);
					Put(R0, F1 * R0.Half, 0.0, C01);
					Put(R1, F0 * R1.Half, 0.0, C10);
					Put(R1, F1 * R1.Half, 0.0, C11);
					M.Idx.insert(M.Idx.end(), {Base4, Base4 + 1, Base4 + 2, Base4 + 1, Base4 + 3, Base4 + 2});
					// Loose stones: on the tarmac within 4 m of the tongue's end, a few, each its own shape.
					const double Mid = (D0 + D1) * 0.5, Reach = (Lim[Ln] + Lim[Ln + 1]) * 0.5;
					if (Mid > Reach || Mid < Reach - 4.0) continue;
					const P2 Spot = (R0.At + R1.At) * 0.5 + R0.Left * ((F0 + F1) * 0.5 * R0.Half);
					const int Stones = int(1.0 + 3.0 * (1.0 - (Reach - Mid) / 4.0) * RideSceneBits::Chance(Spot.X, Spot.Z, 72));
					for (int St = 0; St < Stones; ++St)
					{
						const double U = RideSceneBits::Chance(Spot.X + St, Spot.Z, 73), V = RideSceneBits::Chance(Spot.X, Spot.Z + St, 74);
						const double Sz = 0.1 + 0.25 * RideSceneBits::Chance(Spot.X + St, Spot.Z + St, 75), Wd = Sz / std::fmax(0.5, R0.Half);
						const double Fa = F1 + (F0 - F1) * U;
						const Row S0 = Between(R0, R1, std::clamp(V - Sz * 0.4, 0.0, 1.0)), S1 = Between(R0, R1, std::clamp(V + Sz * 0.6, 0.0, 1.0));
						const double J = RideSceneBits::Chance(Spot.X - St, Spot.Z, 77) - 0.5;
						Patch(S0, (Fa + Wd * (0.5 + J * 0.4)) * S0.Half, S0, (Fa - Wd * 0.4) * S0.Half, S1, (Fa + Wd * 0.3) * S1.Half, S1, (Fa - Wd * (0.6 - J * 0.4)) * S1.Half, 0.006, Grit);
					}
				}
			}
		}
		else
		{
			Lay(Gravel ? Grit : Setts ? SettsKey : CircuitTrack ? CircuitTarmac : Tarmac);   // [circuits] (M_RideRoad's silk black)
		}
		// (where the tarmac wasn't laid whole (a gravel road's tongue), its lines laid over it as before)
		for (const PaintCut& C : PaintCuts)
		{
			const Row Ra = C.T0 <= 0.0 ? A : Between(A, B, C.T0), Rb = C.T1 >= 1.0 ? B : Between(A, B, C.T1);
			Strip(Ra, Rb, C.L0, C.R0, C.L1, C.R1, PaintOverLift, PaintOverLift, C.Colour);
		}
		PaintCuts.clear();
		if (A.Deck && B.Deck)
		{
			// A bridge: a stone parapet each side (its top, and its faces down past the deck to the
			// deck's underside), and the underside. Faces that stand upright are drawn from both sides.
			// ([Nov 18] beenster: "the very basic bridges ... have a very low def material on the walls": they
			// were the tarmac's scan stretched up the faces. Now in the scanned stone, Out.Stone (M_RideStone),
			// its UVs metres of each face's own surface and each face lit by its own facing; the masks as
			// the rail viaducts': a little lichened, mostly the bare scan.)
			if (!StoneMesh)
			{
				StoneMesh = std::make_shared<RideMesh>();
				StoneMesh->OriginX = Out.OriginX;
				StoneMesh->OriginZ = Out.OriginZ;
			}
			// ([Nov 20] beenster, build 164 by Hawes: "we still have these weird bridge walls": tall smooth slabs,
			// the inner face of one black. The inner faces were drawn both ways with the outer face's normal, so
			// the side seen from the road was lit from behind. Where the wall kit is in (BridgeWalls), the walls
			// themselves are its stone-by-stone lengths laid along the deck (the field walls' own models, lower
			// and thinner: see the pass after this loop); here only the kerb, the deck's outer face below them
			// and the underside. Without the kit, the old slab, each face lit by its own side.)
			const bool Kitted = WallRow[K] != 0 && WallRow[K + 1] != 0;
			const bool Railed = !Kitted && RailRow[K] != 0 && RailRow[K + 1] != 0;   // [everyday kits stage 7] (the bridge set's parapet along it: BridgeKitProps)
			const double In = 0.15, Out2 = Kitted || Railed ? In + 2.0 * ParapetFootHalf : 0.55, Top = 1.0, Under = -1.2;
			const auto Corner = [&Settle](const Row& R, double Off, double Lift)
			{
				const P2 At = R.At + R.Left * Off;
				return RideSceneBits::At3(At.X, Settle(R, At, R.Height + RoadLine::CrossHeight(Off, R.Half, R.Bank)) + Lift, At.Z);
			};
			const auto Face = [&](double A0, double B0, double A1, double B1, double LiftA, double LiftB, const RideSceneBits::P3& Toward, float Moss, bool BothSides)
			{
				RideSceneBits::Rgb Mask;
				Mask.R = 0.46f;
				Mask.G = 0.7f;
				Mask.B = 0.35f;
				RideSceneBits::Quad(*StoneMesh, Corner(A, A0, LiftA), Corner(A, B0, LiftB), Corner(B, B1, LiftB), Corner(B, A1, LiftA), Toward, Mask, BothSides, Moss);
			};
			const P2 MidAB = (A.At + B.At) * 0.5;
			const double HighAB = std::fmax(A.Height, B.Height);
			const RideSceneBits::P3 Above = RideSceneBits::At3(MidAB.X, HighAB + 1e4, MidAB.Z), Below = RideSceneBits::At3(MidAB.X, HighAB - 1e4, MidAB.Z);
			const RideSceneBits::P3 Middle = RideSceneBits::At3(MidAB.X, HighAB + 0.5, MidAB.Z);   // (the road's middle: the inner faces face it)
			for (double Side : {1.0, -1.0})
			{
				const double A0 = A.Half + In, A1 = A.Half + Out2, B0 = B.Half + In, B1 = B.Half + Out2;
				const P2 Lm = (A.Left + B.Left) * 0.5;
				const RideSceneBits::P3 Away = RideSceneBits::At3(MidAB.X + Lm.X * Side * 100.0, HighAB, MidAB.Z + Lm.Z * Side * 100.0);
				// (kerb strip; then the top, the inner face and the outer face, or with the kit, the outer face below the wall)
				Face(Side * A0, Side * A.Half, Side * B0, Side * B.Half, 0.0, 0.0, Above, 0.4f, false);
				if (Kitted)
				{
					Face(Side * A1, Side * A1, Side * B1, Side * B1, 0.0, Under, Away, 0.3f, false);
					continue;
				}
				if (Railed)
				{
					// [everyday kits stage 7] (the edge beam's top and outer face; the parapet on it is the kit's)
					Face(Side * A1, Side * A0, Side * B1, Side * B0, 0.0, 0.0, Above, 0.5f, false);
					Face(Side * A1, Side * A1, Side * B1, Side * B1, 0.0, Under, Away, 0.3f, false);
					continue;
				}
				Face(Side * A1, Side * A0, Side * B1, Side * B0, Top, Top, Above, 0.5f, false);
				Face(Side * A0, Side * A0, Side * B0, Side * B0, Top, 0.0, Middle, 0.3f, false);
				Face(Side * A1, Side * A1, Side * B1, Side * B1, Top, Under, Away, 0.3f, false);
			}
			Face(A.Half + Out2, -A.Half - Out2, B.Half + Out2, -B.Half - Out2, Under, Under, Below, 0.1f, false);
		}
		else if (A.Bore && B.Bore)
		{
			// A tunnel: the tube in place of the shoulders, seen from inside; the hill laid back over it.
			if (RingA.empty()) { RingA = Ring(A); LidA = Lid(A); }
			RingB = Ring(B);
			LidB = Lid(B);
			P3 Axis = Across(A, 0.0, A.Height + TubeWall * 0.5), Sky = Across(A, 0.0, A.Height + 1e5);
			for (size_t Q = 0; Q + 1 < RingA.size(); ++Q)
			{
				const bool Kerb = Q == 0 || Q + 2 == RingA.size(), Wall = Q == 1 || Q + 3 == RingA.size();
				Quad3(RingA[Q], RingB[Q], RingB[Q + 1], RingA[Q + 1], Axis, Kerb ? Tile : Wall ? Concrete : Soot, true);
			}
			for (size_t Q = 0; Q + 1 < LidA.size(); ++Q) Quad3(LidA[Q], LidA[Q + 1], LidB[Q + 1], LidB[Q], Sky, Grass, false);
			// Its lamps: where a step of LampStep falls in this bit, a pair of 1.2 m lamps high on the arch
			// either side of the crown, facing in; every LightStep, a real light under the crown.
			for (double Mark = std::ceil(A.Along / LampStep) * LampStep; Mark < B.Along; Mark += LampStep)
			{
				const double U = (Mark - A.Along) / std::fmax(1e-6, B.Along - A.Along);
				Row L = A;
				L.At = A.At + (B.At - A.At) * U;
				L.Height = A.Height + (B.Height - A.Height) * U;
				L.Along = Mark;
				const P2 Fwd = (B.At - A.At) * (1.0 / std::fmax(1e-6, B.At.DistanceTo(A.At)));
				if (!Out.Glow) Out.Glow = std::make_shared<RideMesh>();
				RideMesh& G = *Out.Glow;
				G.OriginX = Out.OriginX;
				G.OriginZ = Out.OriginZ;
				static constexpr float Warm[3] = {1.0f, 0.86f, 0.66f};
				const double W = L.Half + TubeSide, LampSq = Squash(L);
				for (double Th : {Pi * 0.36, Pi * 0.64})
				{
					// (just in from the arch, square to it)
					const double Off = W * std::cos(Th) * 0.97, Y = L.Height + (TubeWall + TubeArch * std::sin(Th) * 0.97) * LampSq;
					const P2 Mid = L.At + L.Left * Off;
					const P2 Side = L.Left * (std::sin(Th) * 0.12);   // (the lamp's 0.25 m across, along the arch)
					const double Rise = std::cos(Th) * 0.12;
					P3 C[4];
					for (int Corner = 0; Corner < 4; ++Corner)
					{
						const double Along2 = Corner < 2 ? -0.6 : 0.6, Across2 = Corner == 0 || Corner == 3 ? -1.0 : 1.0;
						const P2 At = Mid + Fwd * Along2 + Side * Across2;
						C[Corner].X = At.X; C[Corner].Z = At.Z; C[Corner].Y = Y - Rise * Across2;
					}
					const uint32_t V = uint32_t(G.Vertices());
					const P2 In = L.Left * (-std::cos(Th));
					for (const P3& Q : C)
					{
						G.Pos.push_back(float(Q.X - G.OriginX)); G.Pos.push_back(float(Q.Y)); G.Pos.push_back(float(Q.Z - G.OriginZ));
						G.Nrm.push_back(float(In.X)); G.Nrm.push_back(float(-std::sin(Th))); G.Nrm.push_back(float(In.Z));
						G.Uv.push_back(0.0f); G.Uv.push_back(0.0f);
						G.Col.push_back(Warm[0]); G.Col.push_back(Warm[1]); G.Col.push_back(Warm[2]);
					}
					G.Idx.insert(G.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3, V, V + 2, V + 1, V, V + 3, V + 2});
				}
				if (std::fmod(Mark + 1e-6, LightStep) < LampStep * 0.5)
				{
					Out.Lights.push_back(float(L.At.X - Out.OriginX));
					Out.Lights.push_back(float(L.Height + (TubeWall + TubeArch) * LampSq - 0.6));
					Out.Lights.push_back(float(L.At.Z - Out.OriginZ));
				}
			}
			// A mouth: a headwall from the arch and the cutting's floor up to the lid.
			for (const Row* M : {&A, &B})
			{
				if (!M->Mouth) continue;   // (drawn once: by the bit of tube it ends)
				const std::vector<P3>& Rg = M == &A ? RingA : RingB;
				const std::vector<P3>& Ld = M == &A ? LidA : LidB;
				const Row& Inner = M == &A ? B : A;
				P3 Face = Across(*M, 0.0, M->Height + 2.0);
				Face.X += (M->At.X - Inner.At.X) * 10.0;
				Face.Z += (M->At.Z - Inner.At.Z) * 10.0;
				for (int Q = 0; Q < TubeArchSegs; ++Q) Quad3(Rg[size_t(Q) + 2], Rg[size_t(Q) + 3], Ld[size_t(LidArch + Q) + 1], Ld[size_t(LidArch + Q)], Face, Portal, true);
				const auto Foot = [this, &Rg, &Ld](size_t J)
				{
					// (under a point of the lid beside the arch: the wall's foot, or the cutting's floor)
					P3 Q = Ld[J];
					if (J == size_t(LidArch)) Q.Y = Rg[1].Y;
					else if (J == size_t(LidArch + TubeArchSegs)) Q.Y = Rg[Rg.size() - 2].Y;
					else
					{
						const double G = Ground(Q.X, Q.Z);
						Q.Y = std::fmin(Q.Y, (std::isnan(G) ? Q.Y : G) - 0.3);
					}
					return Q;
				};
				for (size_t J = 0; J + 1 < Ld.size(); ++J)
				{
					if (J >= size_t(LidArch) && J < size_t(LidArch + TubeArchSegs)) continue;
					Quad3(Foot(J), Foot(J + 1), Ld[J + 1], Ld[J], Face, Portal, true);
				}
			}
			RingA.swap(RingB);
			LidA.swap(LidB);
		}
		else
		{
			// The left shoulder (from down in the ground up to the edge) and the right.
			// (beenster: in the country the grass runs right up to the tarmac; only a town street has the grit edge)
			const bool TownEdge = Which == 0 && !BuiltUp.empty() && BuiltUp[std::min(size_t(First) + K, BuiltUp.size() - 1)] != 0;
			const float* Edge = TownEdge ? Verge : VergeGrass;
			const bool Street = StreetRow[K] != 0 && StreetRow[K + 1] != 0 && !A.Deck && !B.Deck;
			// [roundabouts round 3] (the narrow gaps by a ring filled with tarmac, a hair under both roads)
			if (Fill[0][0] > 0.0) Strip(A, B, A.Half + Fill[0][0], A.Half - 0.05, B.Half + Fill[0][1], B.Half - 0.05, -0.004, -0.004, Tarmac);
			if (Fill[1][0] > 0.0) Strip(A, B, -A.Half + 0.05, -A.Half - Fill[1][0], -B.Half + 0.05, -B.Half - Fill[1][1], -0.004, -0.004, Tarmac);
			// [roundabouts 156] (a painted splitter island over that gap: drawn below, once the whole road's are known)
			for (int Sd = 0; Sd < 2; ++Sd)
				if (Splitter[Sd] && Fill[Sd][0] > 0.0) Splits.push_back({K, Sd, A, B, Fill[Sd][0], Fill[Sd][1]});
			for (int Sd = 0; Sd < 2; ++Sd)
			{
				if (NoShoulder[Sd] || ShoulderTo[Sd] <= ShoulderFrom[Sd]) continue;
				const Row Sa = ShoulderFrom[Sd] > 0.0 ? Between(A, B, ShoulderFrom[Sd]) : A, Sb = ShoulderTo[Sd] < 1.0 ? Between(A, B, ShoulderTo[Sd]) : B;
				if (Street)
				{
					// [OSM stage 5D] The kerb: its face a 45 degree slope up KerbHigh (so a wheel rides up it,
					// beenster Nov 14), its top; the pavement out to PaveWide; its back down into the ground.
					const double S = Sd == 0 ? 1.0 : -1.0;
					// [roundabouts 156] (where the pavement stops short of a roundabout's mouth, it comes down to the
					// road over its last bit, as a dropped kerb, not ending in a raised slab (PC check, build 155))
					const auto InMouth = [&](size_t At)
					{
						if (At >= Rows.size()) return false;
						for (const RoadRing* Rg : RingsAbout)
						{
							const double O = Rg->OffOf(Rows[At].At);
							if (std::fabs(O) >= Rg->Half + 0.5 && O < Rg->Half + RingMouthClear) return true;
						}
						return false;
					};
					const double Ka = K > 0 && InMouth(K - 1) ? 0.0 : 1.0, Kb = InMouth(K + 2) ? 0.0 : 1.0;
					const auto Band = [&](double From, double To, double LiftFrom, double LiftTo, const float* Colour)
					{
						// (From and To out from the edge; the strip left to right)
						const double Fa = S * (Sa.Half + From), Ta = S * (Sa.Half + To), Fb = S * (Sb.Half + From), Tb = S * (Sb.Half + To);
						if (Ka == 1.0 && Kb == 1.0)
						{
							if (S > 0.0) Strip(Sa, Sb, Ta, Fa, Tb, Fb, LiftTo, LiftFrom, Colour);
							else Strip(Sa, Sb, Fa, Ta, Fb, Tb, LiftFrom, LiftTo, Colour);
							return;
						}
						RideMesh& M = *Into;
						const uint32_t V = uint32_t(M.Vertices());
						if (S > 0.0) { Put(Sa, Ta, LiftTo * Ka, Colour); Put(Sa, Fa, LiftFrom * Ka, Colour); Put(Sb, Tb, LiftTo * Kb, Colour); Put(Sb, Fb, LiftFrom * Kb, Colour); }
						else { Put(Sa, Fa, LiftFrom * Ka, Colour); Put(Sa, Ta, LiftTo * Ka, Colour); Put(Sb, Fb, LiftFrom * Kb, Colour); Put(Sb, Tb, LiftTo * Kb, Colour); }
						M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
					};
					Band(0.0, KerbHigh, 0.0, KerbHigh, KerbStone);
					Band(KerbHigh, KerbHigh + 0.15, KerbHigh, KerbHigh, KerbStone);
					Band(KerbHigh + 0.15, PaveWide, KerbHigh, KerbHigh + 0.02, Paving);
					// ([build 155] beenster: "make the ground meet the other side of the pavement": its back no longer
					// drops 0.4 m into the ground but runs down as grass to just under the ground as drawn)
					// ([156] beenster: "make the land blend into it (not the colour but the heights)": the back drawn as
					// the land is, the field's own ground rising to the pavement's edge)
					Into = TurfMesh.get();
					Meet(Sa, Sb, S, PaveWide, PaveWide + PaveBack, KerbHigh + 0.02, KerbHigh + 0.02);
					Into = &Out;
					continue;
				}
				if (!TownEdge && !A.Deck && !B.Deck)
				{
					// (beenster, build 147, Jenkin Chapel: the tarmac met the grass in a hard straight line. In the
					// country now its last few centimetres crumble into grit, as far in as each metre's own, and
					// the grass comes up over a ragged lip of dirt: both runs of colour M_RideRoad breaks up with
					// the gravel and grass scans' own shapes, so the edge is worn, not drawn)
					const double S = Sd == 0 ? 1.0 : -1.0;
					const auto Wob = [Which, Sd](double At, int Salt)
					{
						const double F = std::floor(At * 0.8), T = At * 0.8 - F, E = T * T * (3.0 - 2.0 * T);
						return RideSceneBits::Chance(double(Which * 2 + Sd), F, Salt) * (1.0 - E) + RideSceneBits::Chance(double(Which * 2 + Sd), F + 1.0, Salt) * E;
					};
					const int Steps = std::max(1, int(std::ceil((Sb.Along - Sa.Along) / 1.5)));
					for (int St = 0; St < Steps; ++St)
					{
						const Row R0 = Between(Sa, Sb, double(St) / Steps), R1 = Between(Sa, Sb, double(St + 1) / Steps);
						const double C0 = 0.12 + 0.2 * Wob(R0.Along, 91) * Wob(R0.Along * 2.3, 92), C1 = 0.12 + 0.2 * Wob(R1.Along, 91) * Wob(R1.Along * 2.3, 92);
						const double L0 = 0.3 + 0.25 * Wob(R0.Along, 93), L1 = 0.3 + 0.25 * Wob(R1.Along, 93);
						// (offsets out from the edge, as the band runs left to right on this side)
						const auto Band = [&](double From0, double To0, double From1, double To1, double LiftFrom, double LiftTo, const float* ColFrom, const float* ColTo)
						{
							if (S > 0.0) Graded(R0, R1, R0.Half + To0, R0.Half + From0, R1.Half + To1, R1.Half + From1, LiftTo, LiftFrom, ColTo, ColFrom);
							else Graded(R0, R1, -R0.Half - From0, -R0.Half - To0, -R1.Half - From1, -R1.Half - To1, LiftFrom, LiftTo, ColFrom, ColTo);
						};
						// (beenster, build 148: no border strip. The tarmac runs right to its edge and the grass
						// comes over it: one band from C in on the tarmac to L out, its colour running from the
						// verge's key, which M_RideRoad draws as the tarmac's own scan, to the grass; the
						// material's ragged grass line falls about a third of the way across, at the edge)
						// ([road edges, Nov 18] beenster: "the strange 3D ribbon that runs along the road edge everywhere":
						// the shoulder's grass was M_RideRoad's, its own scan and colour, a strip unlike the field
						// beside it. Now only the worn edge itself, a ragged 10-30 cm, is the road's; from there the
						// shoulder is drawn as the land is, Out.Turf, the field's own grass and colour to the edge)
						Band(-C0, L0 * 0.45, -C1, L1 * 0.45, 0.004, -ShoulderDip * 0.25, Verge, VergeGrass);
						Into = TurfMesh.get();
						Band(L0 * 0.45, ShoulderFlat, L1 * 0.45, ShoulderFlat, -ShoulderDip * 0.25, -ShoulderDip, VergeGrass, VergeGrass);
						// ([road edges, Nov 17] beenster: the road must blend with the grass beside it. The
						// shoulder's last half metre went down 0.4 m whatever the ground did, so where the field
						// lay a few centimetres under the shoulder's grass its edge stood up as a little dark bank
						// all along the road. Now the shoulder's outer edge goes to just under the ground as the
						// squares draw it, so it slips under the field at a slant and no face shows; only where
						// the ground falls away more than ShoulderDrop does it still stop there)
						Meet(R0, R1, S, ShoulderFlat, Shoulder, -ShoulderDip, -ShoulderDip);
						Into = &Out;
					}
					continue;
				}
				if (Sd == 0)
				{
					Graded(Sa, Sb, Sa.Half + ShoulderFlat, Sa.Half, Sb.Half + ShoulderFlat, Sb.Half, -ShoulderDip, 0.0, VergeGrass, Edge);
					Strip(Sa, Sb, Sa.Half + Shoulder, Sa.Half + ShoulderFlat, Sb.Half + Shoulder, Sb.Half + ShoulderFlat, -ShoulderDrop, -ShoulderDip, VergeGrass);
				}
				else
				{
					Graded(Sa, Sb, -Sa.Half, -Sa.Half - ShoulderFlat, -Sb.Half, -Sb.Half - ShoulderFlat, 0.0, -ShoulderDip, Edge, VergeGrass);
					Strip(Sa, Sb, -Sa.Half - ShoulderFlat, -Sa.Half - Shoulder, -Sb.Half - ShoulderFlat, -Sb.Half - Shoulder, -ShoulderDip, -ShoulderDrop, VergeGrass);
				}
			}
		}
		// The surface's marks (WorldRideSurface): patches, tar banding, grit, covers, joints, each cut to
		// this bit of road and laid a hair over it (under the paint).
		if (!A.Bore && B.Along > A.Along + 1e-6)
		{
			RoadMarks(Which, A.Along, B.Along, MarksHere);
			for (const RoadMark& Mk : MarksHere)
			{
				const double Ta = std::clamp((Mk.From - A.Along) / (B.Along - A.Along), 0.0, 1.0), Tb = std::clamp((Mk.To - A.Along) / (B.Along - A.Along), 0.0, 1.0);
				if (Tb - Ta < 1e-4) continue;
				if (Setts && (Mk.Kind == MarkPatch || Mk.Kind == MarkTar || Mk.Kind == MarkGrit)) continue;   // ([towns stage 6] no tarmac patches on setts)
				if (Gravel && (Mk.Kind == MarkPatch || Mk.Kind == MarkTar || Mk.Kind == MarkGrit)) continue;   // ([build 170, Hartsop] nor on gravel: grey and black tarmac patches lay over the lanes there)
				if (Mk.Kind == MarkPaint) continue;   // ([paint, Nov 18] painted into the tarmac, by Lay)
				const Row Ma = Between(A, B, Ta), Mb = Between(A, B, Tb);
				const double Up = MarkUp(Mk.Kind);
				if (Mk.Kind == MarkGrit)
				{
					// (beenster, build 147, Rainow: the grit up a lane's middle had hard straight edges. Now a
					// ragged band, wider and narrower as it goes, its colour running from the gravel's grit in
					// the middle out to the tarmac's own at its edges, each side its own way, and thinning to
					// nothing at its ends: M_RideRoad lets the gravel scan's stones break through the tarmac
					// along that run of colour, as on a gravel track's tongue)
					// (beenster, build 148: still too stark, "like sandstone poured on the road": the core now only
					// 55% of the way from tarmac to grit, so the material shows mostly tarmac with grey chippings
					// through it, and M_RideRoad's gravel is greyed to the road's own tone)
					// ([build 154] beenster, shown the gravel fade preview: "use this when there is gravel on the road":
					// the core now three quarters of the way, M_RideRoad's gravel the preview's own scan and grade)
					// ([build 168] beenster on 167: "make gravel on the centre of the road more sparse, better colour
					// matched to the road (it's made up of stones from the road itself), improve the blending, no
					// edges". The band now has its own key, (0.06, 0.062, 0.085) at its core, off the gravel
					// tracks' line, so M_RideRoad draws it as the road's own chippings: the gravel scan's stones
					// in the tarmac's own colour, only the tallest of them, fewer and fewer out to the band's edges
					// where there are none at all, so the tarmac runs on unbroken and no edge shows.
					// The core is now also cut at the crown: drawn flat across it, the road's crown stood up
					// through the band's middle (2.5% fall each side) and showed as a clean stripe between two
					// dark streaks.)
					static constexpr float LaneGrit[3] = {0.06f, 0.062f, 0.085f}, LaneTar[3] = {0.022f, 0.022f, 0.025f};
					const auto Wob = [Which](double S, int Salt)
					{
						const double F = std::floor(S * 0.7), T = S * 0.7 - F, E = T * T * (3.0 - 2.0 * T);
						return RideSceneBits::Chance(double(Which), F, Salt) * (1.0 - E) + RideSceneBits::Chance(double(Which), F + 1.0, Salt) * E;
					};
					const double Mid = (Mk.Left + Mk.Right) * 0.5, Half0 = (Mk.Left - Mk.Right) * 0.5;
					const int Steps = std::max(1, int(std::ceil((Mb.Along - Ma.Along) / 0.8)));
					for (int St = 0; St < Steps; ++St)
					{
						const Row R0 = Between(Ma, Mb, double(St) / Steps), R1 = Between(Ma, Mb, double(St + 1) / Steps);
						// (each row's outer edge, core and crown, as the band thins to its ends)
						const auto Edges = [&](const Row& R, double* E)
						{
							const double Ends = std::clamp(std::fmin(R.Along - Mk.From, Mk.To - R.Along) / 4.0, 0.0, 1.0);
							const double Core = Half0 * (0.35 + 0.6 * Wob(R.Along, 81)) * Ends;
							E[0] = Mid + Core + (0.35 + 0.5 * Wob(R.Along, 82)) * Ends;   // (left outer)
							E[1] = Mid + Core * (0.7 + 0.5 * Wob(R.Along, 83));
							E[2] = Mid;                                                    // (the crown)
							E[3] = Mid - Core * (0.7 + 0.5 * Wob(R.Along, 84));
							E[4] = Mid - Core - (0.35 + 0.5 * Wob(R.Along, 85)) * Ends;   // (right outer)
						};
						double E0[5], E1[5];
						Edges(R0, E0);
						Edges(R1, E1);
						const float* Cols[5] = {LaneTar, LaneGrit, LaneGrit, LaneGrit, LaneTar};
						for (int Band = 0; Band < 4; ++Band)
						{
							RideMesh& M = *Into;
							const uint32_t V = uint32_t(M.Vertices());
							Put(R0, E0[Band], Up, Cols[Band]);
							Put(R0, E0[Band + 1], Up, Cols[Band + 1]);
							Put(R1, E1[Band], Up, Cols[Band]);
							Put(R1, E1[Band + 1], Up, Cols[Band + 1]);
							M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2, V + 1, V + 3, V + 2});
						}
					}
					continue;
				}
				// ([OSM stage 5] a hump's slice rises with it)
				Strip(Ma, Mb, Mk.Left, Mk.Right, Mk.Left, Mk.Right, Up + Mk.RiseAt(Ma.Along), Up + Mk.RiseAt(Mb.Along), Mk.Colour);
			}
		}
		const double Half = std::fmin(A.Half, B.Half);
		if (A.Along < PaintFrom || B.Along > PaintTo || Track || Gravel || Setts) continue;
		// Cat's eyes (stage 7): a stud every 18.3 m (60 ft) down the middle of a road with a centre line,
		// red ones along its edges where it has edge lines, out of the towns (lit streets have none).
		if (RoadX::CentreLine(A.Lanes, A.OneWay, Half) && !A.Bore && !OnRingTar && B.Along > A.Along && Britain())   // [stage 8] (Britain's)
		{
			const bool Town = Which == 0 && !BuiltUp.empty() && BuiltUp[std::min(size_t(First) + K, BuiltUp.size() - 1)] != 0;
			const double Stud = 18.288;
			for (double S = std::ceil(A.Along / Stud) * Stud; S < B.Along && !Town; S += Stud)
			{
				const double T0 = (S - A.Along) / (B.Along - A.Along), T1 = std::min((S + 0.1 - A.Along) / (B.Along - A.Along), 1.0);
				const Row Sa = Between(A, B, T0), Sb = Between(A, B, T1);
				static constexpr float WhiteEye[3] = {1.0f, 1.0f, 0.9f}, RedEye[3] = {1.0f, 0.04f, 0.03f};
				Into = &Out.NightOf();
				Strip(Sa, Sb, 0.09, -0.09, 0.09, -0.09, PaintLift + 0.006, PaintLift + 0.006, WhiteEye);
				if (Half >= 2.95)
					for (double Side : {1.0, -1.0})
					{
						const double Edge = Side * (Sa.Half - 0.15);
						Strip(Sa, Sb, Edge + 0.07, Edge - 0.07, Edge + 0.07, Edge - 0.07, PaintLift + 0.006, PaintLift + 0.006, RedEye);
					}
				Into = &Out;
			}
		}
		// [OSM stage 5C] On a one-way road with edge lines (a dual carriageway's half, a slip road) out of the
		// towns: red studs along its nearside edge, amber along its offside (the central reservation's),
		// white ones on the lines between its lanes.
		if (A.OneWay && Half >= 2.95 && !A.Bore && B.Along > A.Along && Britain())
		{
			const bool Town = Which == 0 && !BuiltUp.empty() && BuiltUp[std::min(size_t(First) + K, BuiltUp.size() - 1)] != 0;
			// (the traffic runs the way the points do on the ride's road, and on a side road as its edge allows)
			const int Edge = Which == 0 ? -1 : Mine->Edge;
			const double Goes = Edge >= 0 && Edge < int(Area.Roads.Edges.size()) && Area.Roads.Edges[size_t(Edge)].Oneway < 0 ? -1.0 : 1.0;
			const double Near = A.Near != 0 ? double(A.Near) : Goes * (KeepLeft() ? 1.0 : -1.0);
			const double Stud = 18.288;
			for (double S = std::ceil(A.Along / Stud) * Stud; S < B.Along && !Town; S += Stud)
			{
				const double T0 = (S - A.Along) / (B.Along - A.Along), T1 = std::min((S + 0.1 - A.Along) / (B.Along - A.Along), 1.0);
				const Row Sa = Between(A, B, T0), Sb = Between(A, B, T1);
				static constexpr float WhiteEye[3] = {1.0f, 1.0f, 0.9f}, RedEye[3] = {1.0f, 0.04f, 0.03f}, AmberEye[3] = {1.0f, 0.42f, 0.0f};
				Into = &Out.NightOf();
				const double C = -Near * Sa.Shoulder * 0.5, Hs = Sa.Half - Sa.Shoulder * 0.5;
				for (const double Off : RoadX::LaneLines(A.Lanes, true, 1.0))
					Strip(Sa, Sb, C + Off * Hs + 0.08, C + Off * Hs - 0.08, C + Off * Hs + 0.08, C + Off * Hs - 0.08, PaintLift + 0.006, PaintLift + 0.006, WhiteEye);
				const double Nearside = Near * (Sa.Shoulder > 0.5 ? Sa.Half - Sa.Shoulder : Sa.Half - 0.15), Offside = -Near * (Sa.Half - 0.15);
				Strip(Sa, Sb, Nearside + 0.07, Nearside - 0.07, Nearside + 0.07, Nearside - 0.07, PaintLift + 0.006, PaintLift + 0.006, RedEye);
				Strip(Sa, Sb, Offside + 0.07, Offside - 0.07, Offside + 0.07, Offside - 0.07, PaintLift + 0.006, PaintLift + 0.006, AmberEye);
				Into = &Out;
			}
		}
	}
	// [roundabouts 156] (PC check, build 155: the mouths read as very wide aprons of tarmac.) Between an
	// arm's way in and its way out, a painted splitter island as the UK marks it: a solid line along each
	// road's edge and diagonal hatching out to the middle of the gap, so the two roads' halves meet as
	// chevrons. Only along a run of 8 m or more of such gap, so no odd scraps of hatching.
	std::stable_sort(Splits.begin(), Splits.end(), [](const SplitBit& P, const SplitBit& Q) { return P.Sd < Q.Sd; });
	for (size_t I = 0; I < Splits.size();)
	{
		size_t J = I + 1;
		while (J < Splits.size() && Splits[J].Sd == Splits[I].Sd && Splits[J].K == Splits[J - 1].K + 1) ++J;
		double Widest = 0.0;
		for (size_t Q = I; Q < J; ++Q) Widest = std::fmax(Widest, std::fmax(Splits[Q].F0, Splits[Q].F1));
		// (and only a splitter: its nose at a ring's edge, not a gap between roads farther out)
		bool AtRing = false;
		for (const RoadRing* Rg : RingsAbout)
			for (const SplitBit* Bt : {&Splits[I], &Splits[J - 1]})
				AtRing = AtRing || Rg->OffOf(Bt->A.At) < Rg->Half + Bt->A.Half + 4.0 || Rg->OffOf(Bt->B.At) < Rg->Half + Bt->B.Half + 4.0;
		if (!AtRing || Splits[J - 1].B.Along - Splits[I].A.Along < 8.0 || Widest < 2.0) { I = J; continue; }
		for (size_t Q = I; Q < J; ++Q)
		{
			const SplitBit& Bt = Splits[Q];
			const double Sign = Bt.Sd == 0 ? 1.0 : -1.0;
			const auto Part = [&Bt](double T)
			{
				Row R = Bt.A;
				R.At = Bt.A.At + (Bt.B.At - Bt.A.At) * T;
				R.Height += (Bt.B.Height - Bt.A.Height) * T;
				R.Along += (Bt.B.Along - Bt.A.Along) * T;
				R.Half += (Bt.B.Half - Bt.A.Half) * T;
				R.Bank += (Bt.B.Bank - Bt.A.Bank) * T;
				return R;
			};
			// (offsets out from each row's edge; Strip wants them across the road, the greater first)
			const auto Band = [&](const Row& Ra, const Row& Rb, double A0, double A1, double B0, double B1)
			{
				const double Pa0 = Sign * (Ra.Half + A0), Pa1 = Sign * (Ra.Half + A1), Pb0 = Sign * (Rb.Half + B0), Pb1 = Sign * (Rb.Half + B1);
				Strip(Ra, Rb, std::fmax(Pa0, Pa1), std::fmin(Pa0, Pa1), std::fmax(Pb0, Pb1), std::fmin(Pb0, Pb1), PaintOverLift, PaintOverLift, Paint);
			};
			Band(Bt.A, Bt.B, -0.25, -0.1, -0.25, -0.1);
			const double Run = Bt.B.Along - Bt.A.Along;
			if (Run <= 1e-6) continue;
			static constexpr double Pitch = 2.5, Wide = 0.3, Slice = 0.25;
			for (double U = 0.0; U < Run - 1e-6; U += Slice)
			{
				const double U1 = std::fmin(Run, U + Slice);
				const Row Ra = Part(U / Run), Rb = Part(U1 / Run);
				// (out to the middle of the gap)
				const double Ma = 0.5 * (Bt.F0 + (Bt.F1 - Bt.F0) * (U / Run)) - 0.15, Mb = 0.5 * (Bt.F0 + (Bt.F1 - Bt.F0) * (U1 / Run)) - 0.15;
				if (Ma < 0.4 || Mb < 0.4) continue;
				// (each stripe runs out at 45 degrees: out from the edge by (Along - Phase))
				const double Sa = Ra.Along, Sb = Rb.Along;
				for (double Phase = std::floor((Sa - Ma - 1.0) / Pitch) * Pitch; Phase <= Sb + 1.0; Phase += Pitch)
				{
					const double Ca = Sa - Phase, Cb = Sb - Phase;
					const double LoA = std::clamp(Ca - Wide, 0.0, Ma), HiA = std::clamp(Ca + Wide, 0.0, Ma);
					const double LoB = std::clamp(Cb - Wide, 0.0, Mb), HiB = std::clamp(Cb + Wide, 0.0, Mb);
					if (HiA - LoA < 0.02 && HiB - LoB < 0.02) continue;
					Band(Ra, Rb, LoA, HiA, LoB, HiB);
				}
			}
		}
		I = J;
	}
	// [OSM stage 5C] A dual carriageway's central reservation: a steel barrier down its middle (two beams
	// on posts every 3.2 m), drawn by one of the two halves (the road with the greater number, or the
	// ride's road where it comes back down the other half: its later part).
	if (Rows.size() >= 2 && Pts[size_t(First)].OneWay + Pts[size_t(Last)].OneWay + Pts[size_t((First + Last) / 2)].OneWay > 0)
	{
		static constexpr float Steel[3] = {0.27f, 0.28f, 0.29f}, PostGrey[3] = {0.2f, 0.2f, 0.21f};
		const bool FastKit = FastBarriersOn();
		struct Mid
		{
			bool Is = false;
			int Of = -1;
			P2 At, Across;   // the barrier's line, and square to it
			double Y = 0.0;  // the reservation's ground there
		};
		std::vector<Mid> Mids(Rows.size());
		for (size_t K = 0; K < Rows.size(); ++K)
		{
			const int I = First + int(K);
			DualHit D;
			if (!Pts[size_t(I)].OneWay || Rows[K].Deck || Rows[K].Bore || !DualAcross(Which, I, 0, D)) continue;
			if (D.Gap < MedianLeast || D.Gap > MedianMost || D.Which < Which || (D.Which == Which && D.FarAlong < Pts[size_t(I)].Along)) continue;
			// [roundabouts 156] (none in a roundabout's approach: a ring's ways in and out are one-way pairs,
			// and a barrier stood on the splitter island in the mouth (York, cloud check))
			bool NearRing = false;
			for (const RoadRing* Rg : RingsAbout) NearRing = NearRing || Rg->OffOf(Rows[K].At) < Rg->Half + RingBarrierClear;
			if (NearRing) continue;
			Mid& M = Mids[K];
			M.Is = true;
			M.Of = D.Which;
			M.Across = Rows[K].Left * double(D.Side);
			M.At = Rows[K].At + M.Across * (Rows[K].Half + D.Gap * 0.5);
			M.Y = MedianAcross(D, D.Gap * 0.5);
		}
		const auto At3 = [](const P2& At, double Y)
		{
			P3 Q;
			Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
			return Q;
		};
		for (size_t K = 0; K + 1 < Rows.size(); ++K)
		{
			const Mid& A = Mids[K];
			const Mid& B = Mids[K + 1];
			if (!A.Is || !B.Is || A.Of != B.Of || A.At.DistanceTo(B.At) > 12.0) continue;
			if (FastKit)   // [everyday kits stage 8] (the fast road set's double-beam barrier: World/WorldRideFastRoads.cpp)
			{
				LayBarrierRun(Out.Props, Out.OriginX, Out.OriginZ, PropFastBarrier, 1, A.At, A.Y, B.At, B.Y);
				continue;
			}
			// (the beams: a face each side, 0.45 to 0.78 m up, 0.18 m out from the posts, a lip over each)
			for (double Sd : {1.0, -1.0})
			{
				const P2 Fa = A.At + A.Across * (0.18 * Sd), Fb = B.At + B.Across * (0.18 * Sd);
				const P3 Out3 = At3(A.At + A.Across * (5.0 * Sd), A.Y + 0.6);
				Quad3(At3(Fa, A.Y + 0.45), At3(Fb, B.Y + 0.45), At3(Fb, B.Y + 0.78), At3(Fa, A.Y + 0.78), Out3, Steel, false);
				const P2 Ia = A.At + A.Across * (0.06 * Sd), Ib = B.At + B.Across * (0.06 * Sd);
				Quad3(At3(Fa, A.Y + 0.78), At3(Fb, B.Y + 0.78), At3(Ib, B.Y + 0.78), At3(Ia, A.Y + 0.78), At3(A.At, A.Y + 1e4), Steel, false);
			}
			// (the posts, 0.12 m square, from under the grass to just under the beams' tops)
			const double La = Rows[K].Along, Lb = Rows[K + 1].Along;
			for (double S = std::ceil(La / 3.2) * 3.2; S < Lb && Lb > La; S += 3.2)
			{
				const double T = (S - La) / (Lb - La);
				const P2 C = A.At + (B.At - A.At) * T;
				const double Y = A.Y + (B.Y - A.Y) * T;
				P2 Along = B.At - A.At;
				const double Len = Along.Length();
				Along = Len > 1e-9 ? Along * (0.06 / Len) : P2(0.06, 0.0);
				const P2 Side(Along.Z, -Along.X);
				const P2 Corner[4] = {C + Along + Side, C - Along + Side, C - Along - Side, C + Along - Side};
				for (int F = 0; F < 4; ++F)
				{
					const P2& Q0 = Corner[F];
					const P2& Q1 = Corner[(F + 1) % 4];
					Quad3(At3(Q0, Y - 0.1), At3(Q1, Y - 0.1), At3(Q1, Y + 0.74), At3(Q0, Y + 0.74), At3(C + (Q0 + Q1 - C * 2.0) * 10.0, Y + 0.3), PostGrey, false);
				}
			}
		}
	}
	// [OSM stage 5C] A fast road (a dual carriageway's half) on an embankment more than 3 m high: a steel
	// barrier along its nearside verge, a metre out from its edge, its beam facing the road.
	if (Rows.size() >= 2 && Land && (Pts[size_t(First)].Dual || Pts[size_t(Last)].Dual))
	{
		static constexpr float Steel[3] = {0.27f, 0.28f, 0.29f}, PostGrey[3] = {0.2f, 0.2f, 0.21f};
		std::vector<char> High(Rows.size(), 0);
		for (size_t K = 0; K < Rows.size(); ++K)
		{
			const Row& R = Rows[K];
			if (!Pts[size_t(First) + K].Dual || R.Near == 0 || R.Deck || R.Bore) continue;
			const P2 Out6 = R.At + R.Left * (double(R.Near) * (R.Half + 6.0));
			const double Lie = Land->Height(Out6.X, Out6.Z);
			if (!std::isnan(Lie) && R.Height - Lie > EmbankBarrier) High[K] = 1;
		}
		// (runs of three rows or more: no barrier for a dip between two)
		for (size_t K = 0; K < Rows.size();)
		{
			size_t E = K;
			while (E < Rows.size() && High[E] == High[K]) ++E;
			if (High[K] != 0 && E - K < 3) for (size_t J = K; J < E; ++J) High[J] = 0;
			K = E;
		}
		const auto At3 = [](const P2& At, double Y)
		{
			P3 Q;
			Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
			return Q;
		};
		for (size_t K = 0; K + 1 < Rows.size(); ++K)
		{
			if (!High[K] || !High[K + 1]) continue;
			const Row& A = Rows[K];
			const Row& B = Rows[K + 1];
			const P2 Ma = A.At + A.Left * (double(A.Near) * (A.Half + 1.0)), Mb = B.At + B.Left * (double(B.Near) * (B.Half + 1.0));
			const double Ya = A.Height - ShoulderDip - 0.05, Yb = B.Height - ShoulderDip - 0.05;
			if (FastBarriersOn())
			{
				// [everyday kits stage 8] (the fast road set's one-beam barrier, the road on its right as it runs: World/WorldRideFastRoads.cpp)
				if (A.Near > 0) LayBarrierRun(Out.Props, Out.OriginX, Out.OriginZ, PropFastBarrier, 0, Ma, Ya, Mb, Yb);
				else LayBarrierRun(Out.Props, Out.OriginX, Out.OriginZ, PropFastBarrier, 0, Mb, Yb, Ma, Ya);
				continue;
			}
			const P2 Fa = Ma - A.Left * (double(A.Near) * 0.12), Fb = Mb - B.Left * (double(B.Near) * 0.12);
			Quad3(At3(Fa, Ya + 0.45), At3(Fb, Yb + 0.45), At3(Fb, Yb + 0.78), At3(Fa, Ya + 0.78), At3(A.At, Ya + 0.6), Steel, true);
			const double La = A.Along, Lb = B.Along;
			for (double S = std::ceil(La / 3.2) * 3.2; S < Lb && Lb > La; S += 3.2)
			{
				const double T = (S - La) / (Lb - La);
				const P2 C = Ma + (Mb - Ma) * T;
				const double Y = Ya + (Yb - Ya) * T;
				P2 Along = Mb - Ma;
				const double Len = Along.Length();
				Along = Len > 1e-9 ? Along * (0.06 / Len) : P2(0.06, 0.0);
				const P2 Side(Along.Z, -Along.X);
				const P2 Corner[4] = {C + Along + Side, C - Along + Side, C - Along - Side, C + Along - Side};
				for (int F = 0; F < 4; ++F)
					Quad3(At3(Corner[F], Y - 0.3), At3(Corner[(F + 1) % 4], Y - 0.3), At3(Corner[(F + 1) % 4], Y + 0.74), At3(Corner[F], Y + 0.74),
						At3(C + (Corner[F] + Corner[(F + 1) % 4] - C * 2.0) * 10.0, Y + 0.3), PostGrey, false);
			}
		}
	}
	// The junctions off the ride's road this road's ends are drawn with.
	for (int End = 0; End < 2; ++End)
	{
		if (End == 0 ? First != 0 : Last != Count - 1) continue;
		const Knot* K = KnotAt(Which, End);
		if (K != nullptr && K->Owner == Which && K->OwnerEnd == End) KnotPiece(*K, Out);
		// [smooth junctions stage 3] (a junction built whole, drawn with its owner's end)
		const int Wj = Which > 0 ? WholeAt(Which, End) : -1;
		if (Wj >= 0 && WholeOf[size_t(Wj)].Owner == Which && WholeOf[size_t(Wj)].OwnerEnd == End) WholePiece(Wj, Out);
	}
	// A side road's mouths on the ride's road, each with the piece its end is in.
	for (int End = 0; End < 2 && Which > 0; ++End)
	{
		const Mouth& M = Mine->Mouths[End];
		if (!M.Is || (End == 0 ? First != 0 : Last != Count - 1)) continue;
		const double Half = Pts[size_t(End == 0 ? 0 : Count - 1)].Half;
		const P2 MainAt = Road[size_t(M.MainIndex)].At;
		const double MainAlongHere = Road[size_t(M.MainIndex)].Along;
		// The surface of one road at a point (past its edge, its edge's height).
		const auto Surface = [this](int Of, const P2& At, double Else)
		{
			NearOpts Opts;
			Opts.OnlyRoad = Of;
			NearHit Hit;
			if (!NearestOf(At.X, At.Z, Opts, Hit)) return Else;
			// (a side road's as it is drawn: where it joins a greater road, settled onto it)
			return Hit.Deck || Hit.Bore ? Hit.Height + Hit.Cross : JoinedHeight(Of, At, Hit.Height + Hit.Cross);
		};
		const double MouthH = Surface(0, M.At, Pts[size_t(End == 0 ? 0 : Count - 1)].Height);
		const auto At3 = [](const P2& At, double Y)
		{
			P3 Q;
			Q.X = At.X; Q.Y = Y; Q.Z = At.Z;
			return Q;
		};
		const P3 Sky = At3(M.At, MouthH + 1e5);
		// ([OSM stage 5D] in a town, the kerb and pavement round each corner, from the ride's road's into
		// the side road's (beenster Nov 14: "the pavements need to curve round, not just stop"))
		const bool StreetCorner = ((!BuiltUp.empty() && BuiltUp[std::min(size_t(M.MainIndex), BuiltUp.size() - 1)] != 0) || (!StreetRow.empty() && (End == 0 ? StreetRow.front() : StreetRow.back()) != 0));
		// The mouth's tarmac, as a fan from just inside the ride's road's edge: out to a curve round each
		// corner between the two roads' edges (about a 6 m radius), and across the side road between
		// them, a hair over both roads' own tarmac and the ride's road's shoulder. A shoulder round each curve.
		const P2 Hub = M.At - M.Across * 0.5;
		// [true T] (a through road's arm: on its far side, the through road's far edge on across in place of
		// a rounded corner, finer as it is longer)
		const auto FarIt = Mine->Edge >= 0 ? FarEdges.find(static_cast<long long>(Mine->Edge) * 2 + End) : FarEdges.end();
		const FarEdge* Far = FarIt != FarEdges.end() && FarIt->second.Pts.size() >= 2 ? &FarIt->second : nullptr;
		static constexpr int MostSegs = 16;
		const int Segs = Far != nullptr ? MostSegs : 6;
		// [smooth junctions 3b] (drawn whole: the piece's surface, the ride's road's own at its edge, the side road's as
		// drawn at the mouth line where its rows start, eased between by how far each is)
		const double MCut = Far == nullptr ? MouthCut(Which, End) : -1.0;
		const bool WholeMouth = MCut > 0.0;
		P2 MouthL, MouthR, MouthLeft;
		if (WholeMouth)
		{
			RoadPoint Mp;
			P2 Md;
			double Mr = 0.0;
			CutRow(Pts, End == 0 ? MCut : Mine->Length - MCut, Mp, Md, Mr);
			const P2 Aw = End == 0 ? Md : Md * -1.0;
			MouthLeft = P2(Aw.Z, -Aw.X);
			MouthL = Mp.At + MouthLeft * Mp.Half;
			MouthR = Mp.At - MouthLeft * Mp.Half;
		}
		// (the ride's road's height read along the mouth's own line, not at its nearest point: inside a kink the nearest
		// point skips a metre along, an 11 cm step up a 12% hill in the piece, Whaley)
		const double MainAlong0 = Road[size_t(M.MainIndex)].Along;
		const auto MainHeightAlong = [this](double AlongQ)
		{
			const auto Past = std::lower_bound(Road.begin(), Road.end(), AlongQ, [](const RoadPoint& Rp, double Vq) { return Rp.Along < Vq; });
			if (Past == Road.begin()) return Road.front().Height;
			if (Past == Road.end()) return Road.back().Height;
			const RoadPoint& Pb = *Past;
			const RoadPoint& Pa = *(Past - 1);
			const double Gap = Pb.Along - Pa.Along;
			return Gap > 1e-6 ? Pa.Height + (Pb.Height - Pa.Height) * ((AlongQ - Pa.Along) / Gap) : Pb.Height;
		};
		const auto EdgeSurface = [&, this](int Of, const P2& At, double Else, double& Out2)
		{
			NearOpts Opts;
			Opts.OnlyRoad = Of;
			NearHit Hit;
			Out2 = 0.0;
			if (!NearestOf(At.X, At.Z, Opts, Hit)) return Else;
			Out2 = std::fmax(0.0, Hit.Dist - Hit.Half);
			if (Of == 0 && !Hit.Deck && !Hit.Bore) Hit.Height = MainHeightAlong(MainAlong0 + Dot(At - MainAt, M.Along));
			const double Y = Hit.Height + Hit.Cross;   // (Cross held at the edge beyond it)
			return Of == 0 || Hit.Deck || Hit.Bore ? Y : JoinedHeight(Of, At, Y);
		};
		const auto PieceH = [&](const P2& At)
		{
			double Off0 = 0.0, OffS = 0.0;
			const double R0 = EdgeSurface(0, At, MouthH, Off0);
			const double Sv = EdgeSurface(Which, At, R0, OffS);
			const double Dm = WholeJunctionBits::ToSegment(At, MouthL, MouthR);
			const double T = std::clamp(Off0 / std::fmax(1e-3, Off0 + Dm), 0.0, 1.0);
			return R0 + (Sv - R0) * (T * T * (3.0 - 2.0 * T));
		};
		P2 Rim[2 * (MostSegs + 1)];
		double RimH[2 * (MostSegs + 1)];
		for (int Sd = 0; Sd < 2; ++Sd)
		{
			const double Sigma = Sd == 0 ? 1.0 : -1.0;
			const bool FarSide = Far != nullptr && Far->Sigma * Sigma > 0.0;
			P2 Corner, AlongMain;
			double Reach = 0.0;
			MouthCorner(M, MainAt, Half, Sigma, Corner, AlongMain, Reach);
			const bool Open = !FarSide && !FitCorner(M, Which, Corner, AlongMain, Reach);
			// (the roads bend: each end of the curve, and the corner, moved onto its road's edge as it really runs)
			const auto ToEdge = [this](int Of, const P2& At)
			{
				NearOpts Opts;
				Opts.OnlyRoad = Of;
				NearHit Hit;
				if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist < 0.5) return At;
				return Hit.Foot + (At - Hit.Foot) * (Hit.Half / Hit.Dist);
			};
			P2 CurveAt[MostSegs + 1], CurveOut[MostSegs + 1], CurveMid[MostSegs + 1], CurveDir[MostSegs + 1];
			double CurveH[MostSegs + 1];
			if (FarSide)
			{
				// (the far edge's line evenly by its length, from the ride's road to the arm)
				std::vector<double> Run(Far->Pts.size(), 0.0);
				for (size_t K = 1; K < Far->Pts.size(); ++K) Run[K] = Run[K - 1] + Far->Pts[K].DistanceTo(Far->Pts[K - 1]);
				size_t K = 0;
				for (int S = 0; S <= Segs; ++S)
				{
					const double Want = Run.back() * S / Segs;
					while (K + 2 < Run.size() && Run[K + 1] < Want) ++K;
					const double T = std::clamp((Want - Run[K]) / std::fmax(1e-9, Run[K + 1] - Run[K]), 0.0, 1.0);
					CurveAt[S] = Far->Pts[K] + (Far->Pts[K + 1] - Far->Pts[K]) * T;
				}
				Corner = CurveAt[Segs / 2];
			}
			else Corner = ToEdge(0, Corner);
			const P2 OnMain = FarSide ? CurveAt[0] : ToEdge(0, Corner + AlongMain * Reach), OnSide = FarSide ? CurveAt[Segs] : ToEdge(Which, Corner + M.Away * Reach);
			const double MainH = Surface(0, OnMain, MouthH) + 0.01, SideH = Surface(Which, OnSide, MouthH) + 0.01;
			for (int S = 0; S <= Segs; ++S)
			{
				const double U = double(S) / Segs;
				if (!FarSide) CurveAt[S] = OnMain * ((1.0 - U) * (1.0 - U)) + Corner * (2.0 * U * (1.0 - U)) + OnSide * (U * U);
				// (the side road's surface as drawn, settled onto the ride's road near its edge, a hair over it;
				// [true T] along a far edge, from the ride's road's surface to the arm's)
				CurveH[S] = FarSide ? Surface(0, CurveAt[S], MainH) * (1.0 - U) + Surface(Which, CurveAt[S], SideH) * U + 0.01 + JoinSink * (1.0 - U)
									: Surface(Which, CurveAt[S], MainH + (SideH - MainH) * U) + 0.01 * (1.0 - U) + JoinSink * (1.0 - U);
				// (out from the corner, square to the curve)
				const P2 Tangent = FarSide ? CurveAt[std::min(S + 1, Segs)] - CurveAt[std::max(S - 1, 0)] : (Corner - OnMain) * (1.0 - U) + (OnSide - Corner) * U;
				P2 Outward(Tangent.Z, -Tangent.X);
				if (Dot(Outward, CurveAt[S] - Hub) < 0.0) Outward = Outward * -1.0;
				const double Len = Outward.Length();
				CurveDir[S] = Len > 1e-9 ? Outward * (1.0 / Len) : P2(0.0, 0.0);
				// (the shoulder's outer edge; on a tight curve it gathers to a point rather than cross itself)
				CurveOut[S] = CurveAt[S] + (Len > 1e-9 ? Outward * (Shoulder / Len) : P2(0.0, 0.0));
				if (S > 0 && Dot(CurveOut[S] - CurveOut[S - 1], CurveAt[S] - CurveAt[S - 1]) <= 0.0) CurveOut[S] = CurveOut[S - 1];
				CurveMid[S] = CurveAt[S] + (CurveOut[S] - CurveAt[S]) * (ShoulderFlat / Shoulder);
				// (round the rim: the left corner from the ride's road to the side road, then the right one back)
				const int At4 = Sd == 0 ? S : 2 * Segs + 1 - S;
				Rim[At4] = CurveAt[S];
				RimH[At4] = CurveH[S];
			}
			if (WholeMouth)
				for (int S = 0; S <= Segs; ++S) CurveH[S] = PieceH(CurveAt[S]);   // [smooth junctions 3b] (kerbs and verges on the piece)
			if (FarSide)
			{
				// [true T] (the fan on this side out to the arm's corner and back along the ride's road's edge; the
				// tarmac out from that edge to the far edge as a strip of its own, so little of it lies over the
				// ride's road and its paint)
				for (int S = 0; S < Segs; ++S)
				{
					const int At4 = Sd == 0 ? S : 2 * Segs + 1 - S;
					Rim[At4] = ToEdge(0, CurveAt[S]);
					RimH[At4] = Surface(0, Rim[At4], CurveH[S]) + 0.005;
				}
				for (int S = 0; S < Segs; ++S)
				{
					const P2 E0 = ToEdge(0, CurveAt[S]), E1 = ToEdge(0, CurveAt[S + 1]);
					const P3 C0 = At3(CurveAt[S], CurveH[S]), C1 = At3(CurveAt[S + 1], CurveH[S + 1]);
					const P3 F0 = At3(E0, Surface(0, E0, CurveH[S]) + 0.005), F1 = At3(E1, Surface(0, E1, CurveH[S + 1]) + 0.005);
					Quad3(F0, F1, C1, F0, Sky, Tarmac, false);
					Quad3(F0, C1, C0, F0, Sky, Tarmac, false);
				}
			}
			// [roundabouts round 4] (no kerb, pavement or grass round a corner in a roundabout's mouth)
			const bool InRingMouth = PaintedRingNear(Corner, RingCornerClear) >= 0 || PaintedRingNear(OnMain, RingCornerClear) >= 0 || PaintedRingNear(OnSide, RingCornerClear) >= 0;
			// [true T] (no kerb or verge across a neighbouring road's tarmac: a corner that would cross it all
			// the way left open, a far edge open where it passes another road's mouth)
			bool SegOpen[MostSegs] = {};
			for (int S = 0; S < Segs; ++S)
				SegOpen[S] = Open || (FarSide && (OnOtherRoad(CurveAt[S], Which, MainAlongHere, -0.2) || OnOtherRoad(CurveAt[S + 1], Which, MainAlongHere, -0.2)));
			if (StreetCorner && !InRingMouth)
			{
				// (the kerb's face, its top, the pavement and its back, as the street's own (RoadPiece's Band),
				// each line out from the curve gathering to a point on a tight one rather than cross itself)
				static constexpr int Lines = 6;
				const double Outs[Lines] = {0.0, KerbHigh, KerbHigh + 0.15, PaveWide, PaveWide, PaveWide + CornerBack};
				const double Lifts[Lines] = {0.0, KerbHigh, KerbHigh, KerbHigh + 0.02, KerbHigh + 0.02, -ShoulderDrop};
				const float* Looks[Lines - 1] = {KerbStone, KerbStone, Paving, nullptr, VergeGrass};
				P2 Kerbline[Lines][MostSegs + 1];
				// (beenster, build 147, Whaley Bridge: "kerb lines don't match up". Each line round the corner is
				// the same curve as the kerb's, drawn on its own corner: where the two roads' lines that far out
				// from their edges meet, so on a tight corner it is a smaller curve, never one crossing itself,
				// and it starts and ends square to each road, on the line its straight pavement runs on)
				const P2 Dm = CurveDir[0], Ds = CurveDir[Segs];
				const P2 ToCornerMain = Corner - OnMain, ToCornerSide = Corner - OnSide;
				for (int Ln = 0; Ln < Lines; ++Ln)
				{
					const P2 A0 = OnMain + Dm * Outs[Ln], B0 = OnSide + Ds * Outs[Ln];
					// (A0 + T ToCornerMain = B0 + R ToCornerSide)
					const double Det = ToCornerMain.X * (-ToCornerSide.Z) - ToCornerMain.Z * (-ToCornerSide.X);
					P2 C0 = Corner + (Dm + Ds) * (Outs[Ln] * 0.5);
					if (std::fabs(Det) > 1e-6)
					{
						const P2 Rhs = B0 - A0;
						const double T = (Rhs.X * (-ToCornerSide.Z) - Rhs.Z * (-ToCornerSide.X)) / Det;
						if (T > -0.5 && T < 1.5) C0 = A0 + ToCornerMain * T;
					}
					for (int S = 0; S <= Segs; ++S)
					{
						const double U = double(S) / Segs;
						Kerbline[Ln][S] = A0 * ((1.0 - U) * (1.0 - U)) + C0 * (2.0 * U * (1.0 - U)) + B0 * (U * U);
						// (and never running back against the corner's own curve, so no strip folds over)
						if (S > 0 && Dot(Kerbline[Ln][S] - Kerbline[Ln][S - 1], CurveAt[S] - CurveAt[S - 1]) <= 0.0) Kerbline[Ln][S] = Kerbline[Ln][S - 1];
						if (Ln > 0 && Dot(Kerbline[Ln][S] - Kerbline[Ln - 1][S], CurveDir[S]) < 0.0) Kerbline[Ln][S] = Kerbline[Ln - 1][S];
					}
				}
				for (int Ln = 0; Ln + 1 < Lines; ++Ln)
				{
					if (Looks[Ln] == nullptr) continue;
					for (int S = 0; S < Segs; ++S)
					{
						if (SegOpen[S]) continue;
						const P3 I0 = At3(Kerbline[Ln][S], CurveH[S] + Lifts[Ln]), I1 = At3(Kerbline[Ln][S + 1], CurveH[S + 1] + Lifts[Ln]);
						// ([build 155] the pavement's back down to just under the ground as drawn)
						const auto BackY = [&](int Sg)
						{
							const double Y = CurveH[Sg] + Lifts[Ln + 1];
							if (Ln + 2 != Lines) return Y;
							const P2& P = Kerbline[Ln + 1][Sg];
							const double Drawn = LandDrawnAt(P.X, P.Z, &LandCache);
							return std::isnan(Drawn) ? Y : std::clamp(Drawn - MeetUnder, Y, CurveH[Sg] + KerbHigh + 0.02);
						};
						const P3 O0 = At3(Kerbline[Ln + 1][S], BackY(S)), O1 = At3(Kerbline[Ln + 1][S + 1], BackY(S + 1));
						// (the kerb's face towards the road, the pavement's back slope away from it, the rest up)
						const P3 Look = Ln == 0 ? At3(CurveAt[S] - CurveDir[S] * 50.0, CurveH[S] + 50.0) : Ln == 4 ? At3(CurveAt[S] + CurveDir[S] * 50.0, CurveH[S] + 50.0) : Sky;
						Quad3(I0, I1, O1, I0, Look, Looks[Ln], false);
						Quad3(I0, O1, O0, I0, Look, Looks[Ln], false);
					}
				}
			}
			for (int S = 0; S < Segs && !StreetCorner && !InRingMouth; ++S)
			{
				if (SegOpen[S]) continue;
				// (as two triangles, each made to face up: on a tight curve the four points may not lie in order)
				const P3 In0 = At3(CurveAt[S], CurveH[S]), In1 = At3(CurveAt[S + 1], CurveH[S + 1]), Mid0 = At3(CurveMid[S], CurveH[S] - ShoulderDip), Mid1 = At3(CurveMid[S + 1], CurveH[S + 1] - ShoulderDip),
					Out0 = At3(CurveOut[S], CurveH[S] - ShoulderDrop), Out1 = At3(CurveOut[S + 1], CurveH[S + 1] - ShoulderDrop);
				Quad3(In0, In1, Mid1, In0, Sky, VergeGrass, false);
				Quad3(In0, Mid1, Mid0, In0, Sky, VergeGrass, false);
				Quad3(Mid0, Mid1, Out1, Mid0, Sky, VergeGrass, false);
				Quad3(Mid0, Out1, Out0, Mid0, Sky, VergeGrass, false);
			}
		}
		// ([square T] in rings out from the hub, each point on the higher of the two roads' surfaces there and
		// a hair over: one flat triangle from the hub to a corner far off sagged under the ground between,
		// Buxworth, where the side road runs downhill from the junction)
		if (WholeMouth)
		{
			// [smooth junctions 3b] (the piece's rim: the left corner out to the side road, its left edge on to the mouth
			// line, across it, the right edge back, the right corner in, and the ride's road's edge back between them)
			std::vector<P2> Poly;
			for (int S = 0; S <= Segs; ++S) Poly.push_back(Rim[S]);
			const auto SideEdge = [&](double From, double To, double Sign)
			{
				const int Steps = std::max(1, int(std::ceil(std::fabs(To - From))));
				for (int K = 1; K < Steps; ++K)
				{
					const double A = From + (To - From) * K / Steps;
					RoadPoint Q;
					P2 Qd;
					double Qr = 0.0;
					CutRow(Pts, End == 0 ? A : Mine->Length - A, Q, Qd, Qr);
					const P2 Aw = End == 0 ? Qd : Qd * -1.0;
					Poly.push_back(Q.At + P2(Aw.Z, -Aw.X) * (Sign * Q.Half));
				}
			};
			SideEdge(M.Clear[0], MCut, 1.0);
			const int AcrossSteps = std::max(2, int(std::ceil(MouthL.DistanceTo(MouthR) / 1.2)));
			for (int K = 0; K <= AcrossSteps; ++K) Poly.push_back(MouthL + (MouthR - MouthL) * (double(K) / AcrossSteps));
			SideEdge(MCut, M.Clear[1], -1.0);
			for (int S = Segs + 1; S < 2 * (Segs + 1); ++S) Poly.push_back(Rim[S]);
			{
				const P2 From = Rim[2 * Segs + 1], To = Rim[0];
				const int Steps = std::max(1, int(std::ceil(From.DistanceTo(To))));
				NearOpts OnMain;
				OnMain.OnlyRoad = 0;
				for (int K = 1; K < Steps; ++K)
				{
					const P2 P = From + (To - From) * (double(K) / Steps);
					NearHit Hit;
					// ([smooth junctions stage 5] a quarter metre in under the ride's road: its edge as drawn runs straight between
					// its rows, inside the true curve on a bend's outside, and left a sliver open beside the piece, a 7 cm jolt, York)
					Poly.push_back(NearestOf(P.X, P.Z, OnMain, Hit) && Hit.Dist > 1e-6 ? Hit.Foot + (P - Hit.Foot) * (std::fmax(0.0, Hit.Half - 0.25) / Hit.Dist) : P);
				}
			}
			std::vector<P2> Pt;
			std::vector<std::array<size_t, 3>> Tris;
			WholeJunctionBits::FillRim(Poly, 2, Pt, Tris);
			std::vector<P3> V(Pt.size());
			for (size_t I = 0; I < Pt.size(); ++I) V[I] = At3(Pt[I], PieceH(Pt[I]));
			for (const std::array<size_t, 3>& T : Tris) Quad3(V[T[0]], V[T[1]], V[T[2]], V[T[0]], Sky, Tarmac, false);
		}
		static constexpr int Rings = 4;
		const P3 FanHub = At3(Hub, Surface(0, Hub, MouthH) - 0.01);
		const auto FanAt = [&](int S, int K)
		{
			if (K == 0) return FanHub;
			if (K == Rings) return At3(Rim[S], RimH[S]);
			const P2 P = Hub + (Rim[S] - Hub) * (double(K) / Rings);
			const double Plane = FanHub.Y + (RimH[S] - FanHub.Y) * (double(K) / Rings);
			// ([square T] on the ride's road, just under it: its own tarmac shows there, with the lines painted
			// into it (a give-way line under a fan, Whaley Bridge))
			NearOpts OnMain;
			OnMain.OnlyRoad = 0;
			NearHit Hit;
			if (NearestOf(P.X, P.Z, OnMain, Hit) && Hit.Dist < Hit.Half) return At3(P, Surface(0, P, Plane) - 0.01);
			// (and on the side road's own tarmac, just under it too: its lines show on to its mouth)
			NearOpts OnSide;
			OnSide.OnlyRoad = Which;
			if (NearestOf(P.X, P.Z, OnSide, Hit) && Hit.Dist < Hit.Half - 0.1) return At3(P, Surface(Which, P, Plane) - 0.01);
			return At3(P, std::fmax(Plane, std::fmax(Surface(0, P, Plane), Surface(Which, P, Plane)) + 0.012));
		};
		for (int S = 0; S + 1 < 2 * (Segs + 1) && !WholeMouth; ++S)
			for (int K = 0; K < Rings; ++K)
			{
				const P3 A0 = FanAt(S, K), B0 = FanAt(S + 1, K), A1 = FanAt(S, K + 1), B1 = FanAt(S + 1, K + 1);
				if (K == 0) Quad3(A0, A1, B1, A0, Sky, Tarmac, false);
				else
				{
					Quad3(A0, A1, B1, A0, Sky, Tarmac, false);
					Quad3(A0, B1, B0, A0, Sky, Tarmac, false);
				}
			}
		if (!M.GiveWay) continue;
		// Give way: two broken lines (0.6 m marks, 0.3 m gaps, 0.2 m wide, 0.3 m apart) along the ride's
		// road's edge, across the lane that comes up to it (the whole of a single-track road).
		const P2 Left(M.Away.Z, -M.Away.X);
		const double Slant = Dot(M.Along, Left);
		if (std::fabs(Slant) < 0.3) continue;
		// (along the ride's road's edge from the side road's middle: U to its left edge is Half / Slant)
		const double ToLeft = Half / Slant;
		// (the lane coming up to the junction is on the right, looking away from it; on the left where
		// the traffic keeps right)
		const double U0 = Half >= 2.5 ? 0.0 : ToLeft, U1 = KeepLeft() || Half < 2.5 ? -ToLeft : ToLeft;
		const double Lo = std::fmin(U0, U1) + 0.15, Hi = std::fmax(U0, U1) - 0.15;
		static constexpr double Backs[2] = {0.2, 0.7};
		for (int Bk = 0; Bk < (M.Ring ? 1 : 2); ++Bk)
			for (double U = Lo; U + 0.6 <= Hi + 1e-9; U += 0.9)
			{
				const double Back = Backs[Bk];
				const P2 A0 = M.At + M.Along * U + M.Across * (Back - 0.1), A1 = M.At + M.Along * (U + 0.6) + M.Across * (Back - 0.1);
				const P2 B0 = A0 + M.Across * 0.2, B1 = A1 + M.Across * 0.2;
				// (on whichever is the higher there: the side road's surface or the threshold's)
				const auto Top = [&](const P2& At) { return WholeMouth ? PieceH(At) + 0.004 : std::fmax(Surface(Which, At, MouthH), Surface(0, At, MouthH)) + 0.025; };
				Quad3(At3(A0, Top(A0)), At3(A1, Top(A1)), At3(B1, Top(B1)), At3(B0, Top(B0)), Sky, Paint, false);
			}
	}
	// [places stage 2] A pull-off made for a place beside this bit of road: gravel (the verge's grit, so
	// M_RideRoad draws it in the gravel scan) from the road's edge out to its depth, tapering at its ends,
	// its outer edge settled onto the ground.
	if (!PullNear.empty() && Rows.size() >= 2)
	{
		static constexpr float PullGrit[3] = {0.095f, 0.088f, 0.07f};
		static constexpr float CourtTar[3] = {0.022f, 0.022f, 0.025f};   // [roads stage 7] (a forecourt: the road's tarmac)
		const float* Colour = PullGrit;
		// ([roads stage 7] UVs from the square's own corner, so they stay small as M_RideRoad needs: KEY GOTCHA 147)
		const auto Vert = [&Out, &Colour](const P2& At, double Y)
		{
			Out.Pos.push_back(float(At.X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(At.Z - Out.OriginZ));
			Out.Nrm.push_back(0.0f); Out.Nrm.push_back(1.0f); Out.Nrm.push_back(0.0f);
			Out.Uv.push_back(float((At.X - Out.OriginX) / 4.0)); Out.Uv.push_back(float((At.Z - Out.OriginZ) / 4.0));
			Out.Col.push_back(Colour[0]); Out.Col.push_back(Colour[1]); Out.Col.push_back(Colour[2]);
		};
		for (const Apron& S : PullNear)
		{
			// (each row of this road where it runs along the pull-off's own stretch, within 2 m of it)
			const auto Spot = [&S, &PullSpot](const P2& At, double& U, double& Off) { PullSpot(S, At, U, Off); };
			Colour = S.Tarmac ? CourtTar : PullGrit;
			for (size_t K = 0; K + 1 < Rows.size(); ++K)
			{
				const Row& A = Rows[K];
				const Row& B = Rows[K + 1];
				if (A.Deck || A.Bore || B.Deck || B.Bore) continue;
				double Ua = 0.0, Ub = 0.0, Ca = 0.0, Cb = 0.0;
				Spot(A.At, Ua, Ca);
				Spot(B.At, Ub, Cb);
				const double Half = S.Reach;
				if (std::fabs(Ca) > 2.0 || std::fabs(Cb) > 2.0 || std::fmax(Ua, Ub) < -Half || std::fmin(Ua, Ub) > Half) continue;
				const double SideA = (A.Left.X * S.Out.X + A.Left.Z * S.Out.Z) >= 0.0 ? 1.0 : -1.0;
				const int Steps = std::max(1, int(std::ceil(A.At.DistanceTo(B.At))));
				for (int St = 0; St < Steps; ++St)
				{
					const double T0 = double(St) / Steps, T1 = double(St + 1) / Steps;
					const double U0 = Ua + (Ub - Ua) * T0, U1 = Ua + (Ub - Ua) * T1;
					if (std::fmax(U0, U1) < -Half || std::fmin(U0, U1) > Half) continue;
					const auto Edge = [&](double T, double U, P2& In, double& InY, P2& Far, double& FarY)
					{
						const P2 At = A.At + (B.At - A.At) * T;
						const P2 Lf = A.Left + (B.Left - A.Left) * T;
						const double H = A.Half + (B.Half - A.Half) * T, Y = A.Height + (B.Height - A.Height) * T, Bk = A.Bank + (B.Bank - A.Bank) * T;
						const double FromEnd = Half - std::fabs(std::clamp(U, -Half, Half));
						const double Deep = S.Depth * std::clamp(FromEnd / 5.0, 0.15, 1.0);
						In = At + Lf * (SideA * (H - 0.05));
						InY = Y + RoadLine::CrossHeight(SideA * H, H, Bk) + 0.004;
						Far = At + Lf * (SideA * (H + Deep));
						const double G = Ground(Far.X, Far.Z);
						FarY = (std::isnan(G) ? InY : G) + 0.05;
					};
					P2 I0, F0, I1, F1;
					double Iy0 = 0.0, Fy0 = 0.0, Iy1 = 0.0, Fy1 = 0.0;
					Edge(T0, U0, I0, Iy0, F0, Fy0);
					Edge(T1, U1, I1, Iy1, F1, Fy1);
					// (three strips across, the middle ones eased between the road's edge and the ground)
					const int Bands = std::max(3, int(std::ceil(S.Depth / 3.0)));
					const uint32_t V = uint32_t(Out.Vertices());
					// (never under the ground: a band's point the ground stands above is lifted onto it)
					const auto OnTop = [this](const P2& At, double Y) { const double Gy = Ground(At.X, At.Z); return std::isnan(Gy) ? Y : std::fmax(Y, Gy + 0.05); };
					for (int C = 0; C <= Bands; ++C)
					{
						const double F = double(C) / Bands;
						const P2 Q0 = I0 + (F0 - I0) * F, Q1 = I1 + (F1 - I1) * F;
						Vert(Q0, C == 0 ? Iy0 : OnTop(Q0, Iy0 + (Fy0 - Iy0) * F));
						Vert(Q1, C == 0 ? Iy1 : OnTop(Q1, Iy1 + (Fy1 - Iy1) * F));
					}
					for (int C = 0; C < Bands; ++C)
					{
						const uint32_t P = V + uint32_t(C) * 2;
						// (wound to face up whichever side of the road it is on)
						if (SideA > 0.0) Out.Idx.insert(Out.Idx.end(), {P, P + 1, P + 2, P + 1, P + 3, P + 2});
						else Out.Idx.insert(Out.Idx.end(), {P, P + 2, P + 1, P + 1, P + 2, P + 3});
					}
					// (its outer edge's face down into the ground, so no gap shows under it where the ground falls away)
					{
						const uint32_t O = uint32_t(Out.Vertices());
						const P2& E0 = F0;
						const P2& E1 = F1;
						const double Top0 = OnTop(E0, Fy0), Top1 = OnTop(E1, Fy1);
						const double G0 = Ground(E0.X, E0.Z), G1 = Ground(E1.X, E1.Z);
						Vert(E0, Top0);
						Vert(E1, Top1);
						Vert(E0, (std::isnan(G0) ? Top0 : G0) - 0.25);
						Vert(E1, (std::isnan(G1) ? Top1 : G1) - 0.25);
						Out.Idx.insert(Out.Idx.end(), {O, O + 1, O + 2, O + 1, O + 3, O + 2, O, O + 2, O + 1, O + 1, O + 2, O + 3});
					}
				}
			}
		}
	}
	// (every vertex of the road's own mesh with its water, so the alpha reaches UE: 1, none, where not Put)
	if (!Out.Alpha.empty()) Out.Alpha.resize(Out.Vertices(), 1.0f);
	if (!Out.Uv1.empty()) Out.Uv1.resize(Out.Vertices() * 2, 1e3f);   // ([circuits, build 169] and the racing line's, 1000 none)
	// [road edges, Nov 18] The country shoulders' turf, as the land draws its own ground: the land's colour
	// there, its UVs the world's metres over 8.
	if (TurfMesh->Vertices() > 0)
	{
		std::unordered_map<long long, std::array<float, 3>> Colours;
		RideMesh& T = *TurfMesh;
		for (size_t V = 0; V < T.Vertices(); ++V)
		{
			const double X = T.OriginX + double(T.Pos[V * 3]), Z = T.OriginZ + double(T.Pos[V * 3 + 2]);
			T.Uv[V * 2] = float(X / 8.0);
			T.Uv[V * 2 + 1] = float(Z / 8.0);
			LandColourDrawnAt(X, Z, &T.Col[V * 3], &Colours);
		}
		Out.Turf = TurfMesh;
	}
	// [Nov 20] A bridge's walls: each run of deck in this piece, each side, the wall kit's 2 m lengths laid end to
	// end along a line just in from the deck's outer face, in the region's walling, brought down to a parapet's
	// height (ParapetHigh above the road) and thinned to ParapetFootHalf each side at the foot; a wall head where
	// the bridge itself ends (not where this piece of road does: the next piece carries the wall on). Instanced
	// as the field walls are, so a bridge costs what as much field wall does.
	if (BridgeWalls)
	{
		const int Style = WallStyle(LookHere(Out.OriginX, Out.OriginZ));
		// (each walling's height to the top of its cope and half width at its foot, Tools/make_wall_kit.py and
		// buttertubs_stone.py's)
		static constexpr double StyleHigh[WallStyles] = {1.37, 1.17, 1.42, 1.33, 1.41, 1.27};
		static constexpr double StyleFoot[WallStyles] = {0.42, 0.36, 0.40, 0.40, 0.45, 0.36};
		const int Sk = std::clamp(Style, 0, WallStyles - 1);
		const double Size = ParapetHigh / StyleHigh[Sk];
		const double Narrow = std::clamp(ParapetFootHalf / (StyleFoot[Sk] * Size), 0.3, 1.0);
		const auto LayWall = [&](int Model, int Variant, const RideSceneBits::P3& Q0, const RideSceneBits::P3& Q1)
		{
			const double Run = std::hypot(Q1.X - Q0.X, Q1.Z - Q0.Z);
			if (Run < 0.05) return;
			RideProp Pr;
			Pr.Kind = uint16_t(Model);
			Pr.Variant = uint8_t(Sk * WallVariants + Variant);
			Pr.Mapped = false;
			Pr.X = float((Q0.X + Q1.X) * 0.5 - Out.OriginX);
			Pr.Y = float((Q0.Y + Q1.Y) * 0.5 - 0.02);
			Pr.Z = float((Q0.Z + Q1.Z) * 0.5 - Out.OriginZ);
			Pr.Yaw = float(std::atan2(Q1.Z - Q0.Z, Q1.X - Q0.X));
			Pr.Pitch = float(std::atan2(Q1.Y - Q0.Y, Run));
			Pr.Scale = float(Size);
			// (Stretch is of the model's own length; the model is already Size times it)
			Pr.Stretch = float(Run / ((Model == PropWallEnd ? 1.0 : 2.0) * Size));
			Pr.Across = float(Narrow);
			Out.Props.push_back(Pr);
		};
		const int RowN = int(Rows.size());
		for (int S0 = 0; S0 + 1 < RowN; ++S0)
		{
			if (!(Rows[size_t(S0)].Deck && Rows[size_t(S0) + 1].Deck)) continue;
			int S1 = S0 + 1;
			while (S1 + 1 < RowN && Rows[size_t(S1) + 1].Deck) ++S1;
			if (WallRow[size_t(S0)] == 0)
			{
				S0 = S1;
				continue;
			}
			// (does the bridge go on past this piece's ends? then no head there)
			const bool OnBefore = S0 == 0 && First > 0 && Pts[size_t(First) - 1].Bridge;
			const bool OnAfter = S1 == RowN - 1 && Last + 1 < Count && Pts[size_t(Last) + 1].Bridge;
			for (double Side : {1.0, -1.0})
			{
				std::vector<RideSceneBits::P3> Line;
				std::vector<double> Dist;
				for (int K = S0; K <= S1; ++K)
				{
					const Row& R = Rows[size_t(K)];
					const double Off = Side * (R.Half + 0.15 + ParapetFootHalf);
					const P2 At = R.At + R.Left * Off;
					const RideSceneBits::P3 Q = RideSceneBits::At3(At.X, R.Height + RoadLine::CrossHeight(Side * R.Half, R.Half, R.Bank), At.Z);
					if (!Line.empty() && std::hypot(Q.X - Line.back().X, Q.Z - Line.back().Z) < 0.05) continue;
					Dist.push_back(Line.empty() ? 0.0 : Dist.back() + std::hypot(Q.X - Line.back().X, Q.Z - Line.back().Z));
					Line.push_back(Q);
				}
				if (Line.size() < 2 || Dist.back() < 0.4) continue;
				const auto PointAt = [&](double D)
				{
					D = std::clamp(D, 0.0, Dist.back());
					size_t J = 0;
					while (J + 2 < Line.size() && Dist[J + 1] < D) ++J;
					const double Span = std::fmax(1e-9, Dist[J + 1] - Dist[J]), T = std::clamp((D - Dist[J]) / Span, 0.0, 1.0);
					RideSceneBits::P3 Q;
					Q.X = Line[J].X + (Line[J + 1].X - Line[J].X) * T;
					Q.Y = Line[J].Y + (Line[J + 1].Y - Line[J].Y) * T;
					Q.Z = Line[J].Z + (Line[J + 1].Z - Line[J].Z) * T;
					return Q;
				};
				const double Total = Dist.back();
				// ([Nov 21] beenster, build 166: "ALL hedges and walls need ends": a short bridge's wall is headed too,
				// its head as long as the wall when it stops at one end, half of it each when at both)
				const bool HeadA = !OnBefore, HeadB = !OnAfter;
				const double HeadLen = std::fmin(1.0, Total / (HeadA && HeadB ? 2.0 : 1.0));
				const double From = HeadA ? HeadLen : 0.0, To = Total - (HeadB ? HeadLen : 0.0);
				const int Lengths = To - From > 0.1 ? std::max(1, int(std::round((To - From) / (2.0 * Size)))) : 0;
				// ([169] beenster, build 168, High Street, Swanley: "bit of a mess". Another road joins on the bridge (or
				// its own deck lies alongside): no wall across its carriageway, the wall headed either side of the gap)
				const auto Blocks = [&](const RideSceneBits::P3& Q)
				{
					NearOpts Others;
					Others.NotRoad = Which;
					NearHit Hit;
					return NearestOf(Q.X, Q.Z, Others, Hit) && Hit.Dist < Hit.Half + 0.3 && std::fabs(Hit.Height - Q.Y) < 2.0;
				};
				std::vector<char> Gap(size_t(Lengths), 0);
				for (int Ln = 0; Ln < Lengths; ++Ln)
				{
					const RideSceneBits::P3 Q0 = PointAt(From + (To - From) * Ln / Lengths), Q1 = PointAt(From + (To - From) * (Ln + 1) / Lengths);
					RideSceneBits::P3 Qm = Q0;
					Qm.X = (Q0.X + Q1.X) * 0.5;
					Qm.Y = (Q0.Y + Q1.Y) * 0.5;
					Qm.Z = (Q0.Z + Q1.Z) * 0.5;
					Gap[size_t(Ln)] = Blocks(Q0) || Blocks(Qm) || Blocks(Q1) ? 1 : 0;
				}
				for (int Ln = 0; Ln < Lengths; ++Ln)
				{
					if (Gap[size_t(Ln)]) continue;
					const RideSceneBits::P3 Q0 = PointAt(From + (To - From) * Ln / Lengths), Q1 = PointAt(From + (To - From) * (Ln + 1) / Lengths);
					// (a length beside a gap is the wall's head there, finished towards the gap)
					const bool GapAfter = Ln + 1 < Lengths && Gap[size_t(Ln) + 1], GapBefore = Ln > 0 && Gap[size_t(Ln) - 1];
					if (GapAfter && !GapBefore) { LayWall(PropWallEnd, int(RideSceneBits::Chance(Q0.X, Q0.Z, 52) * WallVariants) % WallVariants, Q0, Q1); continue; }
					if (GapBefore && !GapAfter) { LayWall(PropWallEnd, int(RideSceneBits::Chance(Q0.X, Q0.Z, 52) * WallVariants) % WallVariants, Q1, Q0); continue; }
					if (GapBefore && GapAfter) continue;   // (a 2 m stub between two gaps: left out)
					LayWall(PropWallStone, int(RideSceneBits::Chance(Q0.X, Q0.Z, 51) * WallVariants) % WallVariants, Q0, Q1);
				}
				// (a head is finished at its model's +x: laid from inside the wall out to the bridge's end)
				const bool GapA = Lengths > 0 && Gap.front(), GapB = Lengths > 0 && Gap.back();
				if (HeadA && !GapA) { const RideSceneBits::P3 E = PointAt(0.0), In2 = PointAt(HeadLen); if (!Blocks(E)) LayWall(PropWallEnd, int(RideSceneBits::Chance(E.X, E.Z, 52) * WallVariants) % WallVariants, In2, E); }
				if (HeadB && !GapB) { const RideSceneBits::P3 E = PointAt(Total), In2 = PointAt(Total - HeadLen); if (!Blocks(E)) LayWall(PropWallEnd, int(RideSceneBits::Chance(E.X, E.Z, 52) * WallVariants) % WallVariants, In2, E); }
			}
			S0 = S1;
		}
	}
	BridgeKitProps(Which, First, Last, Out);   // [everyday kits stage 7] (the bridge set: World/WorldRideBridgeKit.cpp)
	if (StoneMesh && StoneMesh->Vertices() > 0) Out.Stone = StoneMesh;
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
	Set("finish_m", Json::MakeNumber(FinishM > 0.0 ? FinishM : Length));
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

RideSummary RidePlan::Summary(double Cap) const
{
	const double Pi = 3.14159265358979323846;
	RideSummary Out;
	Out.Thin = Thin;
	if (Road.size() < 2) return Out;
	const double End = FinishM > 0.0 ? FinishM : Length;
	const Frame F(Area.Roads.FrameLat, Area.Roads.FrameLon);
	std::vector<P2> Flat;
	// (the road's heights every 10 m or so, so a bump in the survey adds little to the climb)
	double LastH = Road[0].Height, NextH = 10.0, NextLine = 0.0;
	Out.Highest = Out.Lowest = Road[0].Height;
	size_t Last = 0;
	for (size_t I = 0; I < Road.size() && Road[I].Along <= End + 1e-6; ++I)
	{
		const RoadPoint& P = Road[I];
		Last = I;
		Flat.push_back(P.At);
		Out.Highest = std::fmax(Out.Highest, P.Height);
		Out.Lowest = std::fmin(Out.Lowest, P.Height);
		if (P.Along >= NextH)
		{
			const double D = P.Height - LastH;
			if (D > 0.0) Out.Climb += D; else Out.Descent -= D;
			LastH = P.Height;
			NextH = P.Along + 10.0;
		}
		if (P.Along >= NextLine)
		{
			Out.Line.push_back(F.ToGeo(P.At.X, P.At.Z));
			NextLine = P.Along + 50.0;
		}
	}
	Out.Line.push_back(F.ToGeo(Road[Last].At.X, Road[Last].At.Z));
	Out.Metres = Road[Last].Along;
	Out.Twist = Route::Twistiness(Flat);
	// The pace: each bend at the speed that leans a rider about 11 degrees (2 m/s across), braking
	// for it and pulling away from it at about 1.5 m/s^2, no faster than Cap anywhere.
	const size_t N = Last + 1;
	std::vector<double> Fast(N, Cap);
	// [OSM stage 1] No faster than a little under each stretch's limit (a track's, none: 30 km/h).
	for (size_t I = 0; I < N; ++I)
		if (const LimitStretch* L = LimitAt(Road[I].Along)) Fast[I] = std::fmin(Cap, (L->Kmh > 0 ? L->Kmh : 30) / 3.6 * 0.85);
	for (size_t I = 0; I < N; ++I)
	{
		size_t A = I, B = I;
		while (A > 0 && Road[I].Along - Road[A].Along < 15.0) --A;
		while (B + 1 < N && Road[B].Along - Road[I].Along < 15.0) ++B;
		if (A == I || B == I) continue;
		const P2 U = Road[I].At - Road[A].At, V = Road[B].At - Road[I].At;
		double Turn = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
		while (Turn > Pi) Turn -= 2.0 * Pi;
		while (Turn < -Pi) Turn += 2.0 * Pi;
		const double Arc = Road[B].Along - Road[A].Along;
		if (std::fabs(Turn) < 1e-4) continue;
		const double Radius = Arc / std::fabs(Turn);
		Fast[I] = std::fmin(Fast[I], std::sqrt(2.0 * Radius));
	}
	Fast.front() = Fast.back() = std::fmin(Fast.front(), 3.0);
	for (size_t I = 1; I < N; ++I)
		Fast[I] = std::fmin(Fast[I], std::sqrt(Fast[I - 1] * Fast[I - 1] + 2.0 * 1.5 * (Road[I].Along - Road[I - 1].Along)));
	for (size_t I = N - 1; I-- > 0;)
		Fast[I] = std::fmin(Fast[I], std::sqrt(Fast[I + 1] * Fast[I + 1] + 2.0 * 1.5 * (Road[I + 1].Along - Road[I].Along)));
	double Seconds = 0.0;
	for (size_t I = 1; I < N; ++I) Seconds += (Road[I].Along - Road[I - 1].Along) / std::fmax(1.0, 0.5 * (Fast[I] + Fast[I - 1]));
	Out.Minutes = Seconds / 60.0;
	// [OSM stage 1] Where it starts, and what there is to stop at on the way.
	Out.Place = PlaceAt(Road[0].At.X, Road[0].At.Z);
	{
		int Count[StopKinds] = {};
		std::vector<std::string> Castles;
		for (const RideStop& S : Stops)
		{
			if (S.Along > Out.Metres + 50.0) continue;
			++Count[S.Kind];
			if (S.Kind == StopCastle && std::find(Castles.begin(), Castles.end(), S.Name) == Castles.end()) Castles.push_back(S.Name);
		}
		static const char* One[StopKinds] = {"fuel", "a cafe", "a pub", "a viewpoint", "a lay-by", "an inn", "a campsite", ""};
		static const char* Many[StopKinds] = {"fuel stops", "cafes", "pubs", "viewpoints", "lay-bys", "inns", "campsites", ""};
		std::string Line;
		for (int K : {int(StopPub), int(StopCafe), int(StopFuel), int(StopInn), int(StopView), int(StopCamp), int(StopLayby)})
		{
			if (Count[K] == 0) continue;
			Line += (Line.empty() ? "" : ", ") + (Count[K] == 1 ? std::string(One[K]) : std::to_string(Count[K]) + " " + Many[K]);
		}
		for (const std::string& C : Castles) Line += (Line.empty() ? "" : ", ") + C;
		Out.StopsLine = Line;
	}
	return Out;
}
} // namespace TorqueWorld
