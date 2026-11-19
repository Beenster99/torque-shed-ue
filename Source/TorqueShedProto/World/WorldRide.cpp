#include "WorldRide.h"
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
#include <cmath>
#include <ctime>
#include <functional>
#include <queue>
#include <set>

namespace TorqueWorld
{
using TorqueFit::Json;

// [ride anywhere] The network's state, shared by every thread that lays or reads it.
struct RidePlan::Network
{
	std::vector<char> OnRide;                                     // the edges the ride's own road is made of
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
	std::map<std::pair<int, long long>, std::shared_ptr<const std::vector<RidePlan::Furnish>>> Furnished;   // [ride anywhere 4] by (0 road | 1 block | 2 ring, its key)
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
			const int Node = G0.NearestConnected(At.X, At.Z, SnapM);
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
		bool Through = true;
		for (size_t I = 0; I + 1 < R->PinNodes.size() && Through; ++I)
		{
			if (R->PinNodes[I] == R->PinNodes[I + 1]) continue;
			const std::vector<int> Leg = G0.CheapestPath(R->PinNodes[I], R->PinNodes[I + 1], &Costs);
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
	Tell("Smoothing the road");
	const std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, Fair);
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
	R->Land = R->Area.Land->ForRoad(Resample(G.PathLine(From, Path), 30.0));
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
	R->LaySides(Path);
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
	R->IndexSkyline();   // [OSM stage 2 batch B] (after the furniture: it strings its poles)
	R->IndexFields();    // [OSM stage 2 batch C]
	R->IndexWater();     // [build 147 feedback] (after the fields: a river's outline gives it its width)
	R->IndexWild();      // [OSM stage 2 batch D]
	R->IndexTowers();    // [towns stage 2]
	R->IndexPlaceRecipes();   // [places stage 3] (after the towers and the churches' buildings)
	R->FindTurnsAndStops(From, Path, RawAlong.empty() ? 0.0 : RawAlong.back());
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
	const Region& G = Area.Roads;
	Network& N = *Net;
	N.OnRide.assign(G.Edges.size(), 0);
	for (int E : Path) N.OnRide[size_t(E)] = 1;
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
		Meets[End] = true;
		const P2 Mid = Road[size_t(Index)].At;
		if (Mid.DistanceTo(At) > 0.5)
		{
			if (End == 0) Raw.insert(Raw.begin(), Mid);
			else Raw.push_back(Mid);
		}
	}
	const std::string& Class = G.ClassOf(int(E));
	std::vector<P2> Line = RoadLine::SmoothLine(Raw, RoadLine::MinRadius, std::vector<double>(Raw.size(), OnRoundRing(E) ? 0.0 : RoadLine::FairOf(Class)));
	// (the knots stop at the last whole step: on to the junction itself, so no road ends short of it)
	if (Line.empty() || Line.back().DistanceTo(Raw.back()) > 0.3) Line.push_back(Raw.back());
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
	for (size_t I = 0; I < Line.size(); ++I)
	{
		const double Y = Land->Height(Line[I].X, Line[I].Z);
		if (!std::isnan(Y)) Last = Y;
		H[I] = std::fmax(std::isnan(Y) ? Last : Y, 0.0);
		if (I > 0) Along[I] = Along[I - 1] + Line[I].DistanceTo(Line[I - 1]);
	}
	const double EndH[2] = {H.front(), H.back()};
	// (its own bridges and tunnels, as the ride's road has its)
	std::vector<char> Kind;
	LevelSpans(Line, H, Kind, NBridges, NTunnels);
	H = RoadLine::GradeLimit(RoadLine::SmoothHeights(RoadLine::SmoothHeights(H, 9), 9));
	Side.Length = Along.back();
	Side.Track = Class == "track" || Class == "service";   // ([OSM stage 4] a service road: no paint either)
	Side.Gravel = G.SurfaceOf(int(E)) == "gravel";
	Side.Setts = SettsSurface(G.SurfaceOf(int(E))) || SettsStreet(Line);   // ([towns stage 8] or a famous street's)
	const std::vector<double> Banks = RoadLine::BankOf(Line, {RoadLine::DesignSpeedOf(Class)});
	const RoadX::Across Ax = RoadX::Of(G, int(E), RoadLine::WidthOf(Class));   // [OSM stage 5]
	const double Half = RingHalfOf(int(E), Ax.Half);   // [roundabouts] (round a ring, the ring's one width)
	// (each end eased to the ground's height at its junction over 30 m, or over half the road where it is
	// shorter than 60 m: beenster, build 148, Brierley Green. A 10 m link between two junctions had its
	// first end pulled three quarters of the way to the far end's height, a metre and more off the
	// junction every other road there meets at, so the ground, cut to the junction, buried it)
	const double EaseOver = std::fmax(1.0, std::fmin(30.0, Side.Length * 0.5));
	for (size_t I = 0; I < Line.size(); ++I)
		for (int End = 0; End < 2; ++End)
		{
			if (Meets[End]) continue;
			const double From = End == 0 ? Along[I] : Side.Length - Along[I];
			H[I] += (EndH[End] - H[I]) * (1.0 - Smooth01(From / EaseOver));
		}
	// (no ramp steeper than 1 in 5 from all that, then the ride's road's own height where it is near)
	for (int Round = 0; Round < 3; ++Round)
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
		P.Bank = Banks[I] * (1.0 - Meeting) * Smooth01(std::fmin(Along[I], Side.Length - Along[I]) / 20.0);
		Side.Pts.push_back(P);
	}
	// Its mouths: from each end on the ride's road, out to where its middle leaves that road's edge.
	for (int End = 0; End < 2; ++End)
	{
		if (!Meets[End]) continue;
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

void RidePlan::KnotPiece(const Knot& K, RideMesh& Out) const
{
	const double Pi = 3.14159265358979323846;
	const int Count = int(K.Arms.size());
	if (Count < 2) return;
	// The tarmac's height at a point: the surface of whichever of its roads it is on (or nearest the edge
	// of), as drawn, a hair over it.
	const auto Top = [this, &K](const P2& At)
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
		return Y + 0.012;
	};
	static constexpr float Tarmac[3] = {0.022f, 0.022f, 0.025f}, VergeGrass[3] = {0.10f, 0.16f, 0.03f};
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
		RimV[I] = Vertex(Rim[I], Across ? RimH[I] - 0.012 : RimH[I], Tarmac);
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
				const uint32_t In0 = Vertex(Rim[I] + Outward[I] * Outs[Ln], RimH[I] - 0.012 + Lifts[Ln], Looks[Ln]), In1 = Vertex(Rim[J] + Outward[J] * Outs[Ln], RimH[J] - 0.012 + Lifts[Ln], Looks[Ln]);
				// ([build 155] the pavement's back down to just under the ground as drawn)
				const auto OutY = [&](size_t V)
				{
					const double Y = RimH[V] - 0.012 + Lifts[Ln + 1];
					if (Ln + 2 != Lines) return Y;
					const P2 P = Rim[V] + Outward[V] * Outs[Ln + 1];
					const double Drawn = LandDrawnAt(P.X, P.Z);
					return std::isnan(Drawn) ? Y : std::clamp(Drawn - MeetUnder, Y, RimH[V] - 0.012 + KerbHigh + 0.02);
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
		const uint32_t In0 = Vertex(Rim[I], RimH[I] - 0.012, VergeGrass), In1 = Vertex(Rim[J], RimH[J] - 0.012, VergeGrass);
		const uint32_t Mid0 = Vertex(Rim[I] + Outward[I] * ShoulderFlat, RimH[I] - 0.012 - ShoulderDip, VergeGrass), Mid1 = Vertex(Rim[J] + Outward[J] * ShoulderFlat, RimH[J] - 0.012 - ShoulderDip, VergeGrass);
		const uint32_t Out0 = Vertex(Rim[I] + Outward[I] * Shoulder, RimH[I] - 0.012 - ShoulderDrop, VergeGrass), Out1 = Vertex(Rim[J] + Outward[J] * Shoulder, RimH[J] - 0.012 - ShoulderDrop, VergeGrass);
		Tri(In0, In1, Mid1); Tri(In0, Mid1, Mid0); Tri(Mid0, Mid1, Out1); Tri(Mid0, Out1, Out0);
	}
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

double RidePlan::Ground(double X, double Z, double Coarse) const
{
	return GroundOf(X, Z, Coarse, Coarse > 8.5);
}

double RidePlan::GroundOf(double X, double Z, double Coarse, bool NoBores, bool Trough) const
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
	// [build 147 feedback] Water's channels and beds (World/WorldRideWater.cpp): a river's, stream's or
	// canal's bed under its level, a lip just over the level at its edge, its bank rising BankRun across
	// for each metre up to the land; a lake's bed under its level; the sea's under the sea.
	if (!std::isnan(Lie))
	{
		double WDist = 0.0, WSurf = 0.0, WHalf = 0.0, WDepth = 0.0;
		bool Beside = true;
		if (NearWater(X, Z, WDist, WSurf, WHalf, WDepth, nullptr, &Beside))
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
		if (NearestOf(X, Z, Other, Also) && !Also.Deck && !Also.Bore && Also.Dist <= Also.Half + 0.5)
		{
			// (its surface as drawn: a side road settled onto a greater one where it joins it)
			const double Drawn = Also.Which == 0 ? Also.Height + Also.Cross : JoinedHeight(Also.Which, P2(X, Z), Also.Height + Also.Cross);
			const double Low = Drawn - Sink;
			if (Low < Out && Out - Low < OverlapStep) Out = Low;
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
	return std::isnan(Clear) ? Out : std::fmin(Out, Clear);
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
	{
		std::shared_ptr<RideMesh> Wet = std::make_shared<RideMesh>();
		WaterTile(X0, Z0, Size, Cells, Out, *Wet);
		if (Wet->Vertices() > 0) Out.Water = Wet;
	}
	if (std::fabs(Step - LandGrid) < 0.1) BanksTile(X0, Z0, Size, Out);   // [build 155 feedback]
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
	// A side road where it lies on a greater road (the ride's, or a side road made before it) sinks just
	// under that road's own surface, and for a few metres out from that road's edge leans into it: two
	// surfaces at one place fought (the flicker at junctions in build 140), and a flat side road laid
	// across a main road on a slope stood proud of it at one edge (the slight bump).
	if (Which <= 0) return Y;
	// (the ride's road first; then whichever side road made before it is nearest)
	NearOpts Greater[2];
	Greater[0].MainOnly = true;
	Greater[1].NotRoad = Which;
	for (NearOpts& O : Greater)
	{
		O.NoDecks = true;
		O.NoBores = true;
		NearHit Hit;
		if (!NearestOf(At.X, At.Z, O, Hit) || Hit.Which > Which) continue;
		const double Out2 = Hit.Dist - Hit.Half;
		if (Out2 > JoinLean) continue;
		const double Theirs = Hit.Height + Hit.Cross - JoinSink;
		if (std::fabs(Theirs - Y) > 1.2) continue;   // (over or under it, not joining it)
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

bool RidePlan::NearestIn(const JoinSet& Set, const P2& At, bool MainOnly, JoinHit& Hit) const
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
	}
	return Hit.Which >= 0 && Hit.Dist - Hit.Half <= JoinLean;
}

double RidePlan::JoinedHeightFrom(const JoinSet& Set, int Which, const P2& At, double Y) const
{
	// (as JoinedHeight: the ride's road first, then whichever other road is nearest)
	if (Which <= 0) return Y;
	if (!Set.Holds(At)) return JoinedHeight(Which, At, Y);
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		JoinHit Hit;
		if (!NearestIn(Set, At, Pass == 0, Hit) || Hit.Which > Which) continue;
		const double Out2 = Hit.Dist - Hit.Half;
		const double Theirs = Hit.Height + Hit.Cross - JoinSink;
		if (std::fabs(Theirs - Y) > 1.2) continue;
		const double T = Out2 <= 0.0 ? 0.0 : Smooth01(Out2 / JoinLean);
		return Theirs + (Y - Theirs) * T;
	}
	return Y;
}

void RidePlan::RoadPiece(int Which, int First, int Last, RideMesh& Out) const
{
	Out = RideMesh();
	if (Which < 0 || Which >= RoadCount()) return;
	const SideRoad* Mine = Which > 0 ? SideOf(Which) : nullptr;
	if (Which > 0 && Mine == nullptr) return;
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	First = std::max(0, First);
	Last = std::min(int(Pts.size()) - 1, Last);
	if (Last <= First) return;
	Out.OriginX = Pts[size_t(First)].At.X;
	Out.OriginZ = Pts[size_t(First)].At.Z;
	// (a side road's paint stops short of the junctions at its ends)
	const double PaintFrom = Which == 0 ? -1.0 : 12.0, PaintTo = Which == 0 ? 1e18 : Mine->Length - 12.0;
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
	};
	std::vector<Row> Rows;
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
		Rows.push_back(R);
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
	const auto Settle = [this, Which, &Joins](const Row& R, const P2& At, double Y) { return R.Deck || R.Bore ? Y : JoinedHeightFrom(Joins, Which, At, Y); };
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
	if ((!PlaceStops.empty() || !Forecourts.empty()) && Rows.size() >= 2)
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
	// ([build 154] UV0 is metres across and along over 4, the along counted from a whole 36 m behind this bit
	// of road, so it stays small enough for the half-precision UVs UE draws (KEY GOTCHA 147) and the pavement's
	// slabs, 0.9 m long, line up across bits of road)
	double AlongBase = 0.0;
	const auto Put = [&Into, &Settle, &AlongBase](const Row& R, double Off, double Lift, const float* Colour)
	{
		// (Off is to the left of the middle)
		RideMesh& M = *Into;
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
			Joined[End] = Sd.Mouths[End].Is || KnotAt(Which, End) != nullptr;
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
					NearOpts Opts;
					Opts.OnlyRoad = 0;
					NearHit Hit;
					const P2 Probe = Corner + AlongMain * Reach;
					Good = NearestOf(Probe.X, Probe.Z, Opts, Hit) && !Hit.Deck && !Hit.Bore;
					if (!Good) break;
					Ends[Sg] = Hit.Along;
					OnSide = Hit.Off >= 0.0 ? 0 : 1;
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
	for (size_t K = 0; K + 1 < Rows.size(); ++K)
	{
		Row RowA = Rows[K], RowB = Rows[K + 1];
		// A side road starts at the ride's road's edge, not under it, and its own shoulders start where
		// a mouth's rounded corners end; the ride's road has none across a mouth.
		const auto Between = [](const Row& From, const Row& To, double T)
		{
			Row R = From;
			R.At = From.At + (To.At - From.At) * T;
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
			if (std::fmax(Fa, Fb) <= M.From) { UnderMain = true; break; }
			if (std::fmin(Fa, Fb) < M.From)
			{
				const double T = (M.From - Fa) / (Fb - Fa);
				if (Fa < Fb) { RowA = Between(RowA, RowB, T); Fa = M.From; }
				else { RowB = Between(RowA, RowB, T); Fb = M.From; }
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
		if (!(A.Along < PaintFrom || B.Along > PaintTo || Track || Gravel || Setts || OnRingTar))
		{
			const double Half = std::fmin(A.Half, B.Half);
			// Paint: edge lines on a road 6 m wide or more, and where there are two lanes a centre line of
			// 3 m dashes with 6 m gaps.
			// ([OSM stage 5D] none along a town street's kerbs)
			if (Half >= 2.95 && !(StreetRow[K] != 0 && StreetRow[K + 1] != 0))
			{
				for (double Side : {1.0, -1.0})
				{
					// (none across a junction's mouth)
					const size_t At0 = size_t(First) + K;
					if (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) continue;
					// ([OSM stage 5C] on a motorway's nearside, between the lanes and the hard shoulder: 0.2 m wide)
					const bool Hard = A.Near != 0 && Side == double(A.Near) && A.Shoulder > 0.5 && B.Shoulder > 0.5;
					const double Ia = Hard ? A.Half - A.Shoulder : A.Half - 0.3, Ib = Hard ? B.Half - B.Shoulder : B.Half - 0.3, W = Hard ? 0.1 : 0.065;
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
					if (Code == 0 || (Code == 3 && Half < 2.9) || (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0)) continue;
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
					if (Which == 0 && At0 + 1 < MouthAt.size() && ((MouthAt[At0] | MouthAt[At0 + 1]) & (Side > 0.0 ? 1 : 2)) != 0) continue;
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
			if (std::fmod(A.Along, RoadX::DashCycle) < RoadX::DashLength)
			{
				if (RoadX::CentreLine(A.Lanes, A.OneWay, Half)) Cut(0.0, 1.0, 0.06, -0.06, 0.06, -0.06, Paint);
				// ([OSM stage 5C] a motorway's lanes between its offside edge and its hard shoulder)
				const double Ca = -double(A.Near) * A.Shoulder * 0.5, Cb = -double(B.Near) * B.Shoulder * 0.5;
				const double Ha = A.Half - A.Shoulder * 0.5, Hb = B.Half - B.Shoulder * 0.5;
				for (const double Off : RoadX::LaneLines(A.Lanes, A.OneWay, 1.0))
					Cut(0.0, 1.0, Ca + Off * Ha + 0.06, Ca + Off * Ha - 0.06, Cb + Off * Hb + 0.06, Cb + Off * Hb - 0.06, Paint);
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
				if (Tb - Ta >= 1e-4) Cut(Ta, Tb, Mk.Left, Mk.Right, Mk.Left, Mk.Right, Mk.Colour);
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
			Lay(Gravel ? Grit : Setts ? SettsKey : Tarmac);
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
			const double In = 0.15, Out2 = 0.55, Top = 1.0, Under = -1.2;
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
			for (double Side : {1.0, -1.0})
			{
				const double A0 = A.Half + In, A1 = A.Half + Out2, B0 = B.Half + In, B1 = B.Half + Out2;
				const P2 Lm = (A.Left + B.Left) * 0.5;
				const RideSceneBits::P3 Away = RideSceneBits::At3(MidAB.X + Lm.X * Side * 100.0, HighAB, MidAB.Z + Lm.Z * Side * 100.0);
				// (kerb strip, top, inner face, outer face)
				Face(Side * A0, Side * A.Half, Side * B0, Side * B.Half, 0.0, 0.0, Above, 0.4f, false);
				Face(Side * A1, Side * A0, Side * B1, Side * B0, Top, Top, Above, 0.5f, false);
				Face(Side * A0, Side * A0, Side * B0, Side * B0, Top, 0.0, Away, 0.3f, true);
				Face(Side * A1, Side * A1, Side * B1, Side * B1, Top, Under, Away, 0.3f, true);
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
					static constexpr float LaneGrit[3] = {0.022f + 0.75f * 0.073f, 0.022f + 0.75f * 0.066f, 0.025f + 0.75f * 0.045f}, LaneTar[3] = {0.022f, 0.022f, 0.025f};
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
						// (each row's core and outer edge each side, as the band thins to its ends)
						const auto Edges = [&](const Row& R, double* E)
						{
							const double Ends = std::clamp(std::fmin(R.Along - Mk.From, Mk.To - R.Along) / 4.0, 0.0, 1.0);
							const double Core = Half0 * (0.35 + 0.6 * Wob(R.Along, 81)) * Ends;
							E[0] = Mid + Core + (0.25 + 0.5 * Wob(R.Along, 82)) * Ends;   // (left outer)
							E[1] = Mid + Core * (0.7 + 0.5 * Wob(R.Along, 83));
							E[2] = Mid - Core * (0.7 + 0.5 * Wob(R.Along, 84));
							E[3] = Mid - Core - (0.25 + 0.5 * Wob(R.Along, 85)) * Ends;   // (right outer)
						};
						double E0[4], E1[4];
						Edges(R0, E0);
						Edges(R1, E1);
						const float* Cols[4] = {LaneTar, LaneGrit, LaneGrit, LaneTar};
						for (int Band = 0; Band < 3; ++Band)
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
	}
	// A side road's mouths on the ride's road, each with the piece its end is in.
	for (int End = 0; End < 2 && Which > 0; ++End)
	{
		const Mouth& M = Mine->Mouths[End];
		if (!M.Is || (End == 0 ? First != 0 : Last != Count - 1)) continue;
		const double Half = Pts[size_t(End == 0 ? 0 : Count - 1)].Half;
		const P2 MainAt = Road[size_t(M.MainIndex)].At;
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
		// (a hair over the ride's road there: the side road itself now lies just under it, so the fan needs
		// no more than that to show, and stands no higher for a wheel running along the edge)
		const P3 Hub3 = At3(Hub, Surface(0, Hub, MouthH) + 0.012);
		const int Segs = 6;
		P2 Rim[2 * (Segs + 1)];
		double RimH[2 * (Segs + 1)];
		for (int Sd = 0; Sd < 2; ++Sd)
		{
			const double Sigma = Sd == 0 ? 1.0 : -1.0;
			P2 Corner, AlongMain;
			double Reach = 0.0;
			MouthCorner(M, MainAt, Half, Sigma, Corner, AlongMain, Reach);
			// (the roads bend: each end of the curve, and the corner, moved onto its road's edge as it really runs)
			const auto ToEdge = [this](int Of, const P2& At)
			{
				NearOpts Opts;
				Opts.OnlyRoad = Of;
				NearHit Hit;
				if (!NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist < 0.5) return At;
				return Hit.Foot + (At - Hit.Foot) * (Hit.Half / Hit.Dist);
			};
			Corner = ToEdge(0, Corner);
			const P2 OnMain = ToEdge(0, Corner + AlongMain * Reach), OnSide = ToEdge(Which, Corner + M.Away * Reach);
			const double MainH = Surface(0, OnMain, MouthH) + 0.01, SideH = Surface(Which, OnSide, MouthH) + 0.01;
			P2 CurveAt[Segs + 1], CurveOut[Segs + 1], CurveMid[Segs + 1], CurveDir[Segs + 1];
			double CurveH[Segs + 1];
			for (int S = 0; S <= Segs; ++S)
			{
				const double U = double(S) / Segs;
				CurveAt[S] = OnMain * ((1.0 - U) * (1.0 - U)) + Corner * (2.0 * U * (1.0 - U)) + OnSide * (U * U);
				// (the side road's surface as drawn, settled onto the ride's road near its edge, a hair over it)
				CurveH[S] = Surface(Which, CurveAt[S], MainH + (SideH - MainH) * U) + 0.01 * (1.0 - U) + JoinSink * (1.0 - U);
				// (out from the corner, square to the curve)
				const P2 Tangent = (Corner - OnMain) * (1.0 - U) + (OnSide - Corner) * U;
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
			// [roundabouts round 4] (no kerb, pavement or grass round a corner in a roundabout's mouth)
			const bool InRingMouth = PaintedRingNear(Corner, RingCornerClear) >= 0 || PaintedRingNear(OnMain, RingCornerClear) >= 0 || PaintedRingNear(OnSide, RingCornerClear) >= 0;
			if (StreetCorner && !InRingMouth)
			{
				// (the kerb's face, its top, the pavement and its back, as the street's own (RoadPiece's Band),
				// each line out from the curve gathering to a point on a tight one rather than cross itself)
				static constexpr int Lines = 6;
				const double Outs[Lines] = {0.0, KerbHigh, KerbHigh + 0.15, PaveWide, PaveWide, PaveWide + CornerBack};
				const double Lifts[Lines] = {0.0, KerbHigh, KerbHigh, KerbHigh + 0.02, KerbHigh + 0.02, -ShoulderDrop};
				const float* Looks[Lines - 1] = {KerbStone, KerbStone, Paving, nullptr, VergeGrass};
				P2 Kerbline[Lines][Segs + 1];
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
				// (as two triangles, each made to face up: on a tight curve the four points may not lie in order)
				const P3 In0 = At3(CurveAt[S], CurveH[S]), In1 = At3(CurveAt[S + 1], CurveH[S + 1]), Mid0 = At3(CurveMid[S], CurveH[S] - ShoulderDip), Mid1 = At3(CurveMid[S + 1], CurveH[S + 1] - ShoulderDip),
					Out0 = At3(CurveOut[S], CurveH[S] - ShoulderDrop), Out1 = At3(CurveOut[S + 1], CurveH[S + 1] - ShoulderDrop);
				Quad3(In0, In1, Mid1, In0, Sky, VergeGrass, false);
				Quad3(In0, Mid1, Mid0, In0, Sky, VergeGrass, false);
				Quad3(Mid0, Mid1, Out1, Mid0, Sky, VergeGrass, false);
				Quad3(Mid0, Out1, Out0, Mid0, Sky, VergeGrass, false);
			}
		}
		for (int S = 0; S + 1 < 2 * (Segs + 1); ++S) Quad3(Hub3, At3(Rim[S], RimH[S]), At3(Rim[S + 1], RimH[S + 1]), Hub3, Sky, Tarmac, false);
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
				const auto Top = [&Surface, Which, MouthH](const P2& At) { return std::fmax(Surface(Which, At, MouthH), Surface(0, At, MouthH)) + 0.025; };
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
