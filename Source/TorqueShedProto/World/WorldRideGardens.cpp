// [gardens] The villages' and towns' garden planting (beenster, Nov 19: "in the villages and towns there
// is a sparsity of planting, like bushes, shrubs, flowers"). BuildingsTile hands each house here with what
// it knows of it (its front, its door, its drive, its party walls); this lays roses, shrubs, flowers and low
// cover in beds along its front, behind its front wall, along its free gable ends and at the back, and now
// and then a small garden tree. Each house's garden has its own character (a tidy one full of roses and
// flowers, a shrubby one, now and then one nearly bare), the same every time for the same house.
// Only what a rider could see ([Nov 19] beenster: "focus the vegetation on what might be seen from the road
// - no need to create bushes in inaccessible or unseeable areas"): within 30 m of a road's edge (a garden tree
// 45 m) and, but for the trees, with a clear line to that road past every building (SeenFromRoad).
//
// Performance first: the plants are drawn as the trees are (instanced, Nanite, by the square), only as the
// garden models (ours, Tools/make_gardens.py, or scanned; no stand-ins), out to 40 to 90 m (the kit's cull, Tools/ue_build_rideuk.py) and only
// on the finest squares; -TorqueRidePlanting=0 leaves them all out, -TorqueRidePlanting=0.5 half.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
// (named, not anonymous: the game's unity build puts several of these files into one)
namespace RideGardens
{
enum GardenKind : uint16_t
{
	GardenRose = RidePlan::FirstGarden, GardenShrub, GardenFlowers, GardenLow, GardenTree, GardenHedge
};
static_assert(GardenHedge + 1 == RidePlan::PlantKinds, "the garden kinds are the last plant kinds");

inline constexpr double Bucket = 16.0;

// How far from a road's edge garden plants are planted (m): past that no rider sees them; a garden
// tree, seen over the roofs, a little further.
inline constexpr double SeenReach = 30.0, SeenReachTree = 45.0;

inline long long BucketKey(int I, int J)
{
	return static_cast<long long>(I) * 4000037LL + static_cast<long long>(J);
}
} // namespace RideGardens

void RidePlan::GardenClear::Add(const Building& B)
{
	using namespace RideGardens;
	const double Span = 0.5 * std::hypot(B.W, B.D) + 2.0;
	const int I0 = int(std::floor((B.X - Span) / Bucket)), I1 = int(std::floor((B.X + Span) / Bucket));
	const int J0 = int(std::floor((B.Z - Span) / Bucket)), J1 = int(std::floor((B.Z + Span) / Bucket));
	for (int J = J0; J <= J1; ++J)
		for (int I = I0; I <= I1; ++I)
		{
			std::vector<const Building*>& Row = Buckets[BucketKey(I, J)];
			if (std::find(Row.begin(), Row.end(), &B) == Row.end()) Row.push_back(&B);
		}
}

bool RidePlan::GardenClear::Blocked(double X, double Z, double Gap) const
{
	using namespace RideGardens;
	const auto It = Buckets.find(BucketKey(int(std::floor(X / Bucket)), int(std::floor(Z / Bucket))));
	if (It == Buckets.end()) return false;
	for (const Building* B : It->second)
	{
		// (into its own frame, as SceneryTile draws it: x across W, z deep D, turned by Yaw)
		const double Cs = std::cos(B->Yaw), Sn = std::sin(B->Yaw), Dx = X - B->X, Dz = Z - B->Z;
		if (std::fabs(Dx * Cs - Dz * Sn) < B->W * 0.5 + Gap && std::fabs(Dx * Sn + Dz * Cs) < B->D * 0.5 + Gap) return true;
	}
	return false;
}

// [Nov 19] Whether a plant at (X, Z) can be seen from the road whose middle's nearest point is Foot: a clear
// line (no building on it) from the plant to that point or to one 12 m either way along the road (the road
// taken as straight there), so a back garden behind its house, or one shut in by others, is left out.
bool RidePlan::SeenFromRoad(const GardenClear& Clear, double X, double Z, const P2& Foot) const
{
	const double Dx = X - Foot.X, Dz = Z - Foot.Z, Len = std::hypot(Dx, Dz);
	if (Len < 1e-3) return true;
	const double Tx = -Dz / Len, Tz = Dx / Len;   // (along the road, square to the line to it)
	for (const double Shift : {0.0, 12.0, -12.0})
	{
		const double Ex = Foot.X + Tx * Shift, Ez = Foot.Z + Tz * Shift;
		const double Run = std::hypot(Ex - X, Ez - Z);
		bool Open = true;
		// (from 0.7 m off the plant, so a bed against a wall isn't blocked by that wall, every metre)
		for (double T = 0.7; Open && T < Run; T += 1.0)
		{
			const double F = T / Run;
			Open = !Clear.Blocked(X + (Ex - X) * F, Z + (Ez - Z) * F, 0.0);
		}
		if (Open) return true;
	}
	return false;
}

void RidePlan::GardenPlants(const GardenHouse& House, const GardenClear& Clear, RideMesh& Out) const
{
	using namespace RideSceneBits;
	using namespace RideGardens;
	if (!RealGardens || GardenShare <= 0.0 || !Land) return;
	const double Share = std::fmin(1.0, GardenShare);
	const double Pi2 = 6.283185307179586;
	// (a spot of this house's own, 0..1)
	const auto Roll = [&House](double U, double V, int Salt) { return Chance(House.X + U * 1.37, House.Z + V * 1.91, Salt); };
	// The garden's character: a tidy one (roses, flowers), a shrubby one (shrubs, low cover), a bare one.
	const double Character = Roll(0.0, 0.0, 301);
	const bool Bare = Character < 0.12;
	const bool Tidy = !Bare && Character < 0.6;
	const double Fill = Bare ? 0.12 : Tidy ? 0.78 : 0.6;   // (a bed's share of its places planted)
	// (house frame to the world: along U, across V, as BuildingsTile's Local)
	const auto World = [&House](double U, double V, double& X, double& Z)
	{
		const double Lx = House.LongX ? U : V, Lz = House.LongX ? V : U;
		X = House.X + Lx * House.Cs + Lz * House.Sn;
		Z = House.Z - Lx * House.Sn + Lz * House.Cs;
	};
	// (the doors along the front: one, or a row's a bay each, in pairs by the party walls)
	const auto ByDoor = [&House](double U)
	{
		if (House.Bays <= 1) return std::fabs(U - House.DoorU) < 0.8;
		const double BayW = House.HalfLong * 2.0 / House.Bays;
		for (int Bay = 0; Bay < House.Bays; ++Bay)
		{
			const double U0 = -House.HalfLong + Bay * BayW;
			if (std::fabs(U - (U0 + (Bay % 2 == 1 ? BayW - 0.9 : 0.9))) < 0.8) return true;
		}
		return false;
	};
	const auto OnDrive = [&House](double U) { return House.DriveTo > House.DriveFrom && U > House.DriveFrom - 0.3 && U < House.DriveTo + 0.3; };
	// One plant: What, Tall metres high, at (U, V) of the house, if the place is fit for it: clear of every
	// building (a tree 2 m clear), of the roads and their pavements, of a railway, of water, and only on a
	// garden's kind of ground (not a car park, a yard, a pitch or a forecourt). A hedge's length is laid
	// along Yaw (else any way), at Size (its length's share, not a height) and with one pick (PickAs) for them all.
	const auto PlantFull = [&](uint16_t What, double U, double V, double Tall, int Salt, double Yaw, double Size, double PickAs)
	{
		if (Roll(U, V, Salt + 1) >= Share) return;
		double X = 0.0, Z = 0.0;
		World(U, V, X, Z);
		if (Clear.Blocked(X, Z, What == GardenTree ? 2.0 : 0.35)) return;
		{
			NearOpts Opts;
			NearHit Hit;
			if (!NearestOf(X, Z, Opts, Hit)) return;
			if (Hit.Dist < Hit.Half + (What == GardenTree ? 3.0 : 1.9)) return;
			// ([Nov 19] beenster: "focus the vegetation on what might be seen from the road": only what a rider
			// on some road could see, within SeenReach of its edge, and (bar a garden tree, which shows over
			// the roofs) with a clear line to the road past every building)
			if (Hit.Dist - Hit.Half > (What == GardenTree ? SeenReachTree : SeenReach)) return;
			if (What != GardenTree && !SeenFromRoad(Clear, X, Z, Hit.Foot)) return;
		}
		{
			double RailDist = 0.0, RailTop = 0.0;
			int RailOn = 0;
			if (NearRail(X, Z, RailDist, RailTop, RailOn) && RailOn != 2 && RailDist < 3.5) return;
		}
		const int Cover = Land->Cover(X, Z);
		if (Cover == 8 || Cover == 6 || Cover == 9) return;   // (water, sand, rock)
		const int Fa = FieldAt(X, Z);
		if (Fa >= 0)
		{
			const int Fk = FieldAreas[size_t(Fa)].Kind;
			if (Fk != FieldLawn && Fk != FieldPark && Fk != FieldAllotments && Fk != FieldOrchard && Fk != FieldScrub && Fk != FieldWood) return;
		}
		const double Y = Ground(X, Z);
		if (std::isnan(Y)) return;
		RidePlant P;
		P.Kind = What;
		P.X = float(X - Out.OriginX);
		P.Z = float(Z - Out.OriginZ);
		P.Y = float(Y - (What == GardenTree ? 0.15 : 0.06));
		P.Yaw = float(Yaw >= -10.0 ? Yaw : Roll(U, V, Salt + 2) * Pi2);
		P.Scale = float(Size);
		P.Height = float(Tall);   // (0 for a hedge: Scale, not a height, PlacePlants)
		P.Pick = float(PickAs >= 0.0 ? PickAs : Roll(U, V, Salt + 3));
		Out.Plants.push_back(P);
	};
	const auto Plant = [&](uint16_t What, double U, double V, double Tall, int Salt) { PlantFull(What, U, V, Tall, Salt, -100.0, 1.0, -1.0); };
	// What grows in a bed's place: a tidy garden's roses and flowers, a shrubby one's shrubs and low cover;
	// Back for the beds against a wall (nothing taller than the sills), and its height.
	const auto Pick = [&](double U, double V, int Salt, bool AgainstWall, double& Tall)
	{
		const double R = Roll(U, V, Salt), H = Roll(U, V, Salt + 7);
		uint16_t What = GardenFlowers;
		if (Tidy) What = R < 0.45 ? GardenFlowers : R < 0.82 ? GardenRose : GardenLow;
		else What = R < 0.35 ? GardenLow : R < 0.7 ? GardenShrub : R < 0.88 ? GardenFlowers : GardenRose;
		switch (What)
		{
		case GardenRose: Tall = 0.8 + 0.6 * H; break;
		case GardenShrub: Tall = AgainstWall ? 0.9 + 0.35 * H : 1.2 + 0.9 * H; break;
		case GardenFlowers: Tall = 0.35 + 0.25 * H; break;
		default: Tall = 0.3 + 0.25 * H; break;
		}
		return What;
	};
	const double Fs = House.FrontSide, Hl = House.HalfLong, Hs = House.HalfShort;
	// (the front garden's depth, its wall to the pavement's back: FrontGap is from 1 m out to the road's edge)
	const double Deep = House.FrontGap + 1.0 - 1.6;

	// [Nov 20] beenster: "please consider building a small library of bushes, hedges for gardens". Half the
	// front gardens with no wall have a clipped hedge along the front instead (privet, beech, laurel, box:
	// Tools/make_gardens.py), knee to chest high, in 1.5 m lengths end to end, one kind a house, open at the
	// path to the door and at the drive; a detached house's runs a little past each side, as a wall does.
	if (!House.FrontWall && !Bare && Deep >= 1.2 && Roll(0.0, 0.0, 450) < 0.5)
	{
		const double Ends = House.Kind == HouseTypes::Detached ? 0.6 : 0.0;
		const double V = Fs * (Hs + Deep - 0.4);
		const double Dx = House.LongX ? House.Cs : House.Sn, Dz = House.LongX ? -House.Sn : House.Cs;   // (along U, in the world)
		const double Yaw = std::atan2(Dz, Dx);
		const double U0 = -Hl - Ends, U1 = Hl + Ends, Len = 1.5;
		const int Count = std::max(1, int(std::floor((U1 - U0) / Len + 0.5)));
		const double Step = (U1 - U0) / Count;
		const double HedgePick = Roll(0.0, 0.0, 452);
		for (int K = 0; K < Count; ++K)
		{
			const double U = U0 + (K + 0.5) * Step;
			if (ByDoor(U) || OnDrive(U)) continue;
			PlantFull(GardenHedge, U, V, 0.0, 460 + 7 * K, Yaw + (Roll(U, 9.0, 461) < 0.5 ? 0.0 : Pi2 * 0.5), Step / Len * 1.04, HedgePick);
		}
	}
	// The bed under the front windows, either side of the door.
	if (Deep >= 0.9)
		for (double U = -Hl + 0.4; U <= Hl - 0.4; U += 0.8)
		{
			const double Uj = U + (Roll(U, 1.0, 310) - 0.5) * 0.2;
			if (ByDoor(Uj) || OnDrive(Uj) || Roll(Uj, 1.0, 311) >= Fill) continue;
			double Tall = 1.0;
			const uint16_t What = Pick(Uj, 1.0, 312, true, Tall);
			Plant(What, Uj, Fs * (Hs + 0.5 + 0.15 * Roll(Uj, 1.0, 313)), Tall, 320);
		}
	// The border along the front wall (or the garden's edge where there is none), when the garden is deep
	// enough for two beds; a detached house's runs a metre past each side, as its wall does.
	if (Deep >= 2.4)
	{
		const double Ends = House.Kind == HouseTypes::Detached ? 0.6 : 0.0;
		const double V = Fs * (Hs + Deep - 0.55);
		for (double U = -Hl - Ends + 0.5; U <= Hl + Ends - 0.5; U += 1.1)
		{
			const double Uj = U + (Roll(U, 2.0, 330) - 0.5) * 0.3;
			if (ByDoor(Uj) || OnDrive(Uj) || Roll(Uj, 2.0, 331) >= Fill * 0.85) continue;
			double Tall = 1.0;
			const uint16_t What = Pick(Uj, 2.0, 332, House.FrontWall, Tall);
			Plant(What, Uj, V, Tall, 340);
		}
		// (a deeper garden: a clump or two on its lawn)
		if (Deep >= 4.0 && !Bare)
			for (int K = 0; K < 2; ++K)
			{
				const double U = (Roll(K, 3.0, 350) - 0.5) * (Hl * 1.6), V2 = Fs * (Hs + 1.6 + (Deep - 3.2) * Roll(K, 3.0, 351));
				if (ByDoor(U) || OnDrive(U) || Roll(K, 3.0, 352) >= 0.5) continue;
				const double R = Roll(K, 3.0, 353);
				Plant(R < 0.5 ? GardenShrub : GardenRose, U, V2, R < 0.5 ? 1.3 + 0.8 * Roll(K, 3.0, 354) : 1.0 + 0.5 * Roll(K, 3.0, 354), 360);
			}
	}
	// Along each free gable end (not a party wall, not the drive's side): shrubs and roses against it.
	for (const double End : {1.0, -1.0})
	{
		if ((End > 0.0 && House.PartyPlus) || (End < 0.0 && House.PartyMinus)) continue;
		if (House.DriveTo > House.DriveFrom && (End > 0.0 ? House.DriveTo > Hl : House.DriveFrom < -Hl)) continue;
		for (double V = -Hs + 0.5; V <= Hs - 0.5; V += 1.0)
		{
			const double Vj = V + (Roll(End * 7.0, V, 370) - 0.5) * 0.25;
			if (Roll(End * 7.0, Vj, 371) >= Fill * 0.65) continue;
			double Tall = 1.0;
			const uint16_t What = Pick(End * 7.0, Vj, 372, false, Tall);
			Plant(What, End * (Hl + 0.55 + 0.2 * Roll(End * 7.0, Vj, 373)), Vj, Tall, 380);
		}
	}
	// At the back: a bed against the house, and shrubs along the garden's far end about 5 m out.
	for (double U = -Hl + 0.5; U <= Hl - 0.5; U += 1.0)
	{
		if (Roll(U, -1.0, 390) >= Fill * 0.5) continue;
		double Tall = 1.0;
		const uint16_t What = Pick(U, -1.0, 391, true, Tall);
		Plant(What, U, -Fs * (Hs + 0.55), Tall, 400);
	}
	if (!Bare)
		for (double U = -Hl + 0.7; U <= Hl - 0.7; U += 1.4)
		{
			if (Roll(U, -5.0, 410) >= 0.45) continue;
			const double R = Roll(U, -5.0, 411);
			Plant(R < 0.6 ? GardenShrub : GardenRose, U, -Fs * (Hs + 4.6 + 0.8 * Roll(U, -5.0, 412)), R < 0.6 ? 1.4 + 0.9 * Roll(U, -5.0, 413) : 1.1 + 0.5 * Roll(U, -5.0, 413), 420);
		}
	// Now and then a small garden tree (a cherry, an elder): at a back corner, or in a deep front garden.
	const bool OwnPlot = House.Kind == HouseTypes::Detached || House.Kind == HouseTypes::Cottage || House.Kind == HouseTypes::Semi;
	if (OwnPlot && !Bare && Roll(0.0, 0.0, 430) < 0.32)
	{
		const bool Front = Deep >= 5.5 && Roll(0.0, 0.0, 431) < 0.35;
		const double Side = Roll(0.0, 0.0, 432) < 0.5 ? -1.0 : 1.0;
		const double U = Side * std::fmax(0.0, Hl - 1.2), V = Front ? Fs * (Hs + Deep - 1.8) : -Fs * (Hs + 4.5);
		if (!(Front && (ByDoor(U) || OnDrive(U)))) Plant(GardenTree, U, V, 3.5 + 2.5 * Roll(0.0, 0.0, 433), 440);
	}
}
} // namespace TorqueWorld
