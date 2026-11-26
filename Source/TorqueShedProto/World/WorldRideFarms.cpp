// [everyday kits stage 4] The farm kit: reading it and which of its classes a building is. See WorldRideFarms.h;
// laid by BuildWarehouse (WorldRideWarehouses.cpp) from RidePlan::BuildingsTile.
#include "WorldRideFarms.h"

#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideWarehouses.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex FarmKitLock;
std::shared_ptr<const WarehouseKit>& FarmKitTable()
{
	static std::shared_ptr<const WarehouseKit> Table;
	return Table;
}

bool Is(const std::string& W, std::initializer_list<const char*> List)
{
	for (const char* S : List)
		if (W == S) return true;
	return false;
}
} // namespace

bool SetFarmKit(const std::string& Json, const std::string& Bin)
{
	std::shared_ptr<WarehouseKit> Kit = ReadWarehouseKit(Json, Bin);
	if (!Kit) return false;
	std::lock_guard<std::mutex> Hold(FarmKitLock);
	FarmKitTable() = std::move(Kit);
	return true;
}

std::shared_ptr<const WarehouseKit> FarmKitNow()
{
	std::lock_guard<std::mutex> Hold(FarmKitLock);
	return FarmKitTable();
}

const char* FarmClassOf(const std::string& Kind, int Baked, double Long, double Short, bool InYard, double Brick, double Seed)
{
	// Which buildings: the map's farm kinds (not a farmhouse, building=farm, nor a glasshouse); in a farmyard also
	// the baker's sheds, barns and works, and a big untagged building (a farmhouse is seldom 250 m2 on the ground).
	const double Area = Long * Short;
	const bool FarmTag = Is(Kind, {"barn", "farm_auxiliary", "agricultural", "cowshed", "stable", "sty", "silo_shed"});
	const bool Plain = Kind.empty() || Kind == "yes";
	const bool Take = FarmTag || (Plain && Baked == 2)
		|| (InYard && (Plain || Is(Kind, {"shed", "industrial", "warehouse", "storage", "hangar", "roof"})) && (Baked == 1 || Baked == 2 || Baked == 5 || (Baked == 0 && Area >= 250.0)));
	if (!Take || Short < 3.0 || Area < 12.0) return "";
	// Big ones are modern steel sheds; small ones tin; between, an old barn or a smaller shed.
	const bool Stock = Is(Kind, {"cowshed", "sty"});
	if (Area >= 300.0 || Short >= 13.0)
		return Stock ? "farm_livestock" : Seed < 0.62 ? "farm_shed" : Seed < 0.85 ? "farm_store" : "farm_livestock";
	if (Area < 45.0)
		return Kind == "barn" && Brick <= 0.0 && Area >= 25.0 ? "barn_stone" : "farm_small";   // (a little field barn in stone country is still stone)
	const bool Old = Kind == "barn" || Kind == "stable" || (!InYard && Area < 200.0) || Seed < 0.5;
	if (!Old)
		return "farm_shed";
	if (Brick <= 0.0)
		return "barn_stone";
	return std::fmod(Seed * 7.31, 1.0) < Brick ? "barn_brick" : "barn_boarded";
}

namespace
{
// A yard thing: its kind, how likely, its half length along its model's x and half width across, the least yard it
// goes in (m2), and whether it belongs more to the hill farms' stock (1) or the lowland's crops (-1).
struct YardThing { int Kind; double Weight, HalfX, HalfY, MinYard; int Leaning; };
const YardThing YardThings[] = {
	{RidePlan::PropFarmBales, 1.4, 3.7, 1.25, 0.0, -1},
	{RidePlan::PropFarmBale, 1.0, 0.8, 0.65, 0.0, 1},
	{RidePlan::PropFarmClamp, 0.5, 6.0, 6.8, 2500.0, 1},
	{RidePlan::PropFarmTank, 0.25, 8.4, 8.4, 4000.0, 1},
	{RidePlan::PropFarmFeedbin, 0.7, 3.8, 1.4, 900.0, 1},
	{RidePlan::PropFarmTrough, 0.6, 1.2, 0.45, 0.0, 1},
	{RidePlan::PropFarmTractor, 0.8, 3.0, 1.4, 0.0, 0},
	{RidePlan::PropFarmTractorUtility, 0.7, 2.3, 1.2, 0.0, 1},
	{RidePlan::PropFarmTractorCompact, 0.5, 2.75, 0.85, 0.0, 0},   // (its bucket well out in front)
	{RidePlan::PropFarmTrailer, 0.8, 4.0, 1.3, 600.0, -1},
	{RidePlan::PropFarmDiesel, 0.4, 1.3, 0.7, 0.0, 0},
	{RidePlan::PropFarmMuck, 0.6, 3.1, 2.1, 900.0, 1},
};
} // namespace

void RidePlan::FarmYardProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	using namespace RideSceneBits;
	bool Any = false;
	for (const YardThing& T : YardThings) Any = Any || PropModelled[T.Kind];
	if (!Any || !Scene || FieldAreas.empty()) return;
	std::vector<int> Fields;
	FieldsIn(X0, Z0, Size, Fields);
	const Region& G = Area.Roads;
	// (the buildings round about, as boxes: the cells' own and the true outlines' pieces)
	std::vector<std::shared_ptr<const std::vector<OutlinePiece>>> Held;
	std::vector<const OutlinePiece*> Pieces;
	std::vector<CellId> Covered;
	const auto OnBuilding = [&](double X, double Z, double Spare)
	{
		const auto In = [X, Z, Spare](const Building& B)
		{
			const double Dx = X - B.X, Dz = Z - B.Z, Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
			const double Ax = Dx * Cs - Dz * Sn, Az = Dx * Sn + Dz * Cs;
			return std::fabs(Ax) < B.W * 0.5 + Spare && std::fabs(Az) < B.D * 0.5 + Spare;
		};
		for (const OutlinePiece* Pc : Pieces)
			if (std::fabs(Pc->B.X - X) < 80.0 && std::fabs(Pc->B.Z - Z) < 80.0 && In(Pc->B)) return true;
		const auto It = Scene->Buildings.find(SceneKey(X, Z));
		if (It != Scene->Buildings.end())
			for (int I : It->second)
				if (In(G.Buildings[size_t(I)])) return true;
		return UnderTower(X, Z);
	};
	for (int Fi : Fields)
	{
		const FieldArea& F = FieldAreas[size_t(Fi)];
		if (F.Kind != FieldFarmyard || F.Ring.size() < 3) continue;
		if (F.MaxX < X0 - 20.0 || F.MinX > X0 + Size + 20.0 || F.MaxZ < Z0 - 20.0 || F.MinZ > Z0 + Size + 20.0) continue;
		P2 Mid;
		const double YardArea = std::fabs(RideShapes::RingArea(F.Ring, Mid));
		if (YardArea < 150.0 || YardArea > 60000.0) continue;
		// (the buildings over the whole yard, so every square works it out alike)
		Held.clear(); Pieces.clear(); Covered.clear();
		OutlinesIn(std::floor((F.MinX - 20.0) / 64.0) * 64.0, std::floor((F.MinZ - 20.0) / 64.0) * 64.0,
			std::ceil((std::fmax(F.MaxX - F.MinX, F.MaxZ - F.MinZ) + 40.0) / 64.0) * 64.0 + 64.0, Held, Pieces, Covered);
		const LocalLook& Look = LookHere(Mid.X, Mid.Z);
		const bool Hills = std::strcmp(Look.Name, "lowland") != 0 && std::strcmp(Look.Name, "fens") != 0;
		const double Sx = double(F.Id % 9973u) + 0.5, Sz = double(F.Id / 9973u % 9973u) + 0.5;   // (the yard's own seed)
		const int Want = std::clamp(int(YardArea / 900.0) + 1, 1, 7);
		// (the ring's length, to pick spots along it)
		double Perimeter = 0.0;
		for (size_t K = 0; K < F.Ring.size(); ++K) Perimeter += (F.Ring[(K + 1) % F.Ring.size()] - F.Ring[K]).Length();
		if (Perimeter < 10.0) continue;
		struct Placed { double X, Z, R; };
		std::vector<Placed> Done;
		for (int Try = 0; Try < Want * 6 && int(Done.size()) < Want; ++Try)
		{
			// (which thing: by weight, the hill farms' stock or the lowland's crops more)
			double Sum = 0.0;
			for (const YardThing& T : YardThings)
				if (PropModelled[T.Kind] && YardArea >= T.MinYard) Sum += T.Weight * (T.Leaning == 0 ? 1.0 : (T.Leaning > 0) == Hills ? 1.4 : 0.6);
			if (Sum <= 0.0) break;
			double At = Chance(Sx, Sz, 711 + Try) * Sum;
			const YardThing* Pick = nullptr;
			for (const YardThing& T : YardThings)
			{
				if (!PropModelled[T.Kind] || YardArea < T.MinYard) continue;
				At -= T.Weight * (T.Leaning == 0 ? 1.0 : (T.Leaning > 0) == Hills ? 1.4 : 0.6);
				if (At <= 0.0) { Pick = &T; break; }
			}
			if (!Pick) continue;
			// (a spot along the yard's edge, set in from it by the thing's depth, the thing along the edge)
			double Along = Chance(Sx, Sz, 751 + Try) * Perimeter;
			size_t K = 0;
			for (; K < F.Ring.size(); ++K)
			{
				const double El = (F.Ring[(K + 1) % F.Ring.size()] - F.Ring[K]).Length();
				if (Along <= El) break;
				Along -= El;
			}
			if (K >= F.Ring.size()) continue;
			const P2 A = F.Ring[K], B = F.Ring[(K + 1) % F.Ring.size()];
			const P2 E = B - A;
			const double El = E.Length();
			if (El < Pick->HalfX * 2.0 + 1.0) continue;
			const P2 Dir = E * (1.0 / El);
			Along = std::clamp(Along, Pick->HalfX + 0.5, El - Pick->HalfX - 0.5);
			P2 In(-Dir.Z, Dir.X);
			const P2 Foot = A + Dir * Along;
			if (!RideShapes::InRing(F.Ring, Foot.X + In.X * 2.0, Foot.Z + In.Z * 2.0)) In = In * -1.0;
			const P2 C = Foot + In * (Pick->HalfY + 1.0);
			const double R = std::hypot(Pick->HalfX, Pick->HalfY);
			bool Clear = true;
			for (const Placed& Pl : Done) Clear = Clear && std::hypot(Pl.X - C.X, Pl.Z - C.Z) > Pl.R + R + 1.0;
			// (its middle and corners in the yard, off the buildings, the ground fairly level, the road clear)
			double Lo = 1e9, Hi = -1e9;
			for (double Su : {-1.0, 0.0, 1.0})
				for (double Sv : {-1.0, 1.0})
				{
					if (!Clear) break;
					const P2 Q = C + Dir * (Su * Pick->HalfX) + In * (Sv * Pick->HalfY);
					if (!RideShapes::InRing(F.Ring, Q.X, Q.Z) || OnBuilding(Q.X, Q.Z, 0.8)) Clear = false;
					const double Gy = Ground(Q.X, Q.Z);
					if (std::isnan(Gy)) Clear = false;
					else { Lo = std::fmin(Lo, Gy); Hi = std::fmax(Hi, Gy); }
				}
			if (!Clear || Hi - Lo > 1.2) continue;
			NearOpts Opts;
			NearHit Hit;
			if (NearestOf(C.X, C.Z, Opts, Hit) && Hit.Dist < Hit.Half + R + 1.5) continue;
			Done.push_back({C.X, C.Z, R});
			// (the whole yard is worked out alike from every square it touches; each places its own)
			if (C.X < X0 || C.X >= X0 + Size || C.Z < Z0 || C.Z >= Z0 + Size) continue;
			RideProp P;
			P.Kind = uint16_t(Pick->Kind);
			P.Id = F.Id * 31u + uint32_t(Try);
			P.Variant = uint8_t(Chance(Sx, Sz, 791 + Try) * 255.0);
			P.X = float(C.X - X0);
			P.Z = float(C.Z - Z0);
			P.Y = float(Lo);
			const P2 Front = Chance(Sx, Sz, 771 + Try) < 0.5 ? Dir : Dir * -1.0;
			P.Yaw = float(std::atan2(Front.Z, Front.X));
			P.Scale = 1.0f;
			Out.push_back(P);
		}
	}
}
} // namespace TorqueWorld
