// [house foot, Nov 22] Settling the buildings into the land (beenster, a drone shot of Uig on build 168:
// "is there any way that we can make the houses blend in a bit better? Not just here but across the game.
// Maybe long grasses at the base of the house"). Three layers, all cheap:
//  - the walls' feet weathered: splash, damp and grime fading up the wall (BuildingsTile's FootWall: the
//    same wall quads cut in three, no new parts);
//  - the ground round every building darker and lusher, fading out over HouseFootReach (LandTile: the
//    land's own vertex colour, so it shows from any distance the finest and next squares are drawn);
//  - rank grass hugging the walls' feet (HouseFootPlants: the grass's own kinds, in its own groups),
//    where the grass carpet stops short of every building (PlantsTile keeps 0.6 m round them bare).
// -TorqueHouseBlend=0 leaves all three out; -TorqueHouseBlend=0.5 half as strong, half as much grass.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
// (named, not anonymous: the game's unity build puts several of these files into one)
namespace RideHouseFoot
{
// The grass's kinds, as WorldRidePlants.cpp numbers them (KindNames there).
inline constexpr uint16_t TuftA = 4, TuftB = 5, TussockA = 6, TussockB = 7, MatGrass = 8, FarA = 9, FarB = 10;

// How far outside building B's walls (X, Z) is (0 inside), in B's own frame (as SceneryTile draws it:
// x across W, z deep D, turned by Yaw).
inline double Outside(const Building& B, double X, double Z)
{
	const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw), Dx = X - B.X, Dz = Z - B.Z;
	const double Ox = std::fmax(0.0, std::fabs(Dx * Cs - Dz * Sn) - B.W * 0.5), Oz = std::fmax(0.0, std::fabs(Dx * Sn + Dz * Cs) - B.D * 0.5);
	return std::hypot(Ox, Oz);
}

// How much a point Out metres from a wall is its foot: 1 at the wall, easing to 0 at Reach.
inline double FootBy(double Out, double Reach)
{
	if (Out >= Reach) return 0.0;
	const double T = 1.0 - Out / Reach;
	return T * T * (3.0 - 2.0 * T);
}
} // namespace RideHouseFoot

void RidePlan::HouseFootTone(float* C, double Foot)
{
	// (damp, shaded, richer ground: darker, the green held a little more than the red and blue, as the
	// lusher grass and soil along a house's walls look against the open field)
	if (Foot <= 0.0) return;
	const double F = std::fmin(1.0, Foot);
	C[0] = float(C[0] * (1.0 - 0.48 * F));
	C[1] = float(C[1] * (1.0 - 0.34 * F));
	C[2] = float(C[2] * (1.0 - 0.5 * F));
}

void RidePlan::HouseFootBoxes(double X0, double Z0, double Size, std::vector<Building>& Out) const
{
	using namespace RideSceneBits;
	Out.clear();
	if (!Scene) return;
	// (as BuildingsTile finds them: the outlines' pieces where the world has outlines, the cells' own boxes
	// elsewhere; every one whose middle lies within reach of the square)
	const double Margin = HouseFootReach + 30.0;
	const double Bx0 = X0 - Margin, Bz0 = Z0 - Margin, Bs = Size + 2.0 * Margin;
	std::vector<std::shared_ptr<const std::vector<OutlinePiece>>> Held;
	std::vector<const OutlinePiece*> Pieces;
	std::vector<CellId> Covered;
	OutlinesIn(Bx0, Bz0, Bs, Held, Pieces, Covered);
	for (const OutlinePiece* P : Pieces)
		if (P->B.W >= 2.0 && P->B.D >= 2.0) Out.push_back(P->B);
	const Region& G = Area.Roads;
	const Frame Fr = G.FrameOf();
	const int Cx0 = int(std::floor(Bx0 / SceneCell)), Cx1 = int(std::floor((Bx0 + Bs) / SceneCell));
	const int Cz0 = int(std::floor(Bz0 / SceneCell)), Cz1 = int(std::floor((Bz0 + Bs) / SceneCell));
	for (int Cz = Cz0; Cz <= Cz1; ++Cz)
		for (int Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = Scene->Buildings.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Buildings.end()) continue;
			for (int I : It->second)
			{
				const Building& B = G.Buildings[size_t(I)];
				if (B.W < 2.0 || B.D < 2.0 || B.X < Bx0 || B.X > Bx0 + Bs || B.Z < Bz0 || B.Z > Bz0 + Bs) continue;
				if (!Covered.empty())
				{
					const Geo At = Fr.ToGeo(B.X, B.Z);
					if (std::find(Covered.begin(), Covered.end(), CellOf(At.Lat, At.Lon)) != Covered.end()) continue;
				}
				Out.push_back(B);
			}
		}
}

double RidePlan::HouseFootAt(double X, double Z) const
{
	using namespace RideHouseFoot;
	if (!Scene || HouseBlend <= 0.0) return 0.0;
	std::vector<Building> Near;
	HouseFootBoxes(X - 1.0, Z - 1.0, 2.0, Near);
	double Best = 0.0;
	for (const Building& B : Near)
	{
		const double Span = 0.5 * (B.W + B.D) + HouseFootReach;
		if (std::fabs(B.X - X) > Span || std::fabs(B.Z - Z) > Span) continue;
		Best = std::fmax(Best, FootBy(Outside(B, X, Z), HouseFootReach));
	}
	return Best * HouseBlend;
}

void RidePlan::HouseFootPlants(const GardenHouse& House, bool Front, bool Hard, const GardenClear& Clear, RideMesh& Out) const
{
	using namespace RideSceneBits;
	using namespace RideHouseFoot;
	if (!Land || HouseBlend <= 0.0 || GrassDensity <= 0.0) return;
	const double Keep = std::fmin(1.0, HouseBlend * std::fmin(1.0, GrassDensity));
	const double Pi2 = 6.283185307179586;
	const auto Roll = [&House](double U, double V, int Salt) { return Chance(House.X + U * 1.37, House.Z + V * 1.91, Salt); };
	const auto World = [&House](double U, double V, double& X, double& Z)
	{
		const double Lx = House.LongX ? U : V, Lz = House.LongX ? V : U;
		X = House.X + Lx * House.Cs + Lz * House.Sn;
		Z = House.Z - Lx * House.Sn + Lz * House.Cs;
	};
	// (its doors along the front, as GardenPlants finds them: one, or a row's a bay each in pairs)
	const auto ByDoor = [&House](double U)
	{
		if (House.Bays <= 1) return std::fabs(U - House.DoorU) < 0.9;
		const double BayW = House.HalfLong * 2.0 / House.Bays;
		for (int Bay = 0; Bay < House.Bays; ++Bay)
		{
			const double U0 = -House.HalfLong + Bay * BayW;
			if (std::fabs(U - (U0 + (Bay % 2 == 1 ? BayW - 0.9 : 0.9))) < 0.9) return true;
		}
		return false;
	};
	const auto OnDrive = [&House](double U) { return House.DriveTo > House.DriveFrom && U > House.DriveFrom - 0.5 && U < House.DriveTo + 0.5; };
	// (the house's own character: some neglected and rank all round, some kept trim; a town's yards and
	// paths leave only a weed or two at the foot)
	const double Rank = Roll(0.0, 0.0, 520);
	const double Share = (Hard ? 0.3 : 0.7 + 0.3 * Rank) * Keep;
	// One clump at (U, V) of the house, Out metres from its wall: on soft ground only, clear of every building,
	// a road and its pavement, a railway, water and the hard places (yards, car parks, pitches).
	const auto Clump = [&](double U, double V, double OutFromWall, int Salt)
	{
		if (Roll(U, V, Salt) >= Share) return;
		double X = 0.0, Z = 0.0;
		World(U, V, X, Z);
		if (Clear.Blocked(X, Z, 0.08)) return;
		{
			NearOpts Opts;
			NearHit Hit;
			if (NearestOf(X, Z, Opts, Hit) && Hit.Dist < Hit.Half + (Hard ? 2.2 : 1.0)) return;
		}
		{
			double RailDist = 0.0, RailTop = 0.0;
			int RailOn = 0;
			if (NearRail(X, Z, RailDist, RailTop, RailOn) && RailOn != 2 && RailDist < 3.0) return;
		}
		const int Cover = Land->Cover(X, Z);
		if (Cover == 8 || Cover == 6 || Cover == 9) return;   // (water, sand, rock)
		const int Fa = FieldAt(X, Z);
		if (Fa >= 0)
		{
			const int Fk = FieldAreas[size_t(Fa)].Kind;
			if (Fk == FieldHard || Fk == FieldCarPark || Fk == FieldPitch || Fk == FieldQuarry || Fk == FieldForecourt || Fk == FieldPullOff || Fk == FieldSand
				|| Fk == FieldGraves) return;
		}
		if (CircuitGroundAt(X, Z) >= 0) return;
		const double Y = Ground(X, Z);
		if (std::isnan(Y)) return;
		// (rank tussocks against the wall, lower tufts and mat grass further out; a town's foot only small tufts)
		const double R = Roll(U, V, Salt + 1);
		uint16_t What = Hard ? (R < 0.5 ? TuftA : TuftB)
			: OutFromWall < 0.5 ? (R < 0.55 ? (R < 0.28 ? TussockA : TussockB) : R < 0.85 ? (R < 0.7 ? TuftA : TuftB) : MatGrass)
			: (R < 0.35 ? (R < 0.18 ? TussockA : TussockB) : R < 0.75 ? (R < 0.55 ? TuftA : TuftB) : R < 0.9 ? MatGrass : (R < 0.95 ? FarA : FarB));
		RidePlant P;
		P.Kind = What;
		P.X = float(X - Out.OriginX);
		P.Z = float(Z - Out.OriginZ);
		P.Y = float(Y - 0.05);
		P.Yaw = float(Roll(U, V, Salt + 2) * Pi2);
		// (taller against the wall, where nothing mows or grazes it)
		const double Tall = Hard ? 0.55 + 0.3 * Roll(U, V, Salt + 3) : (OutFromWall < 0.5 ? 0.95 : 0.8) + 0.45 * Roll(U, V, Salt + 3);
		P.Scale = float(Tall);
		P.Pick = float(Roll(U, V, Salt + 4));
		Out.Plants.push_back(P);
	};
	const double Fs = House.FrontSide, Hl = House.HalfLong, Hs = House.HalfShort;
	// Along each long side (the front only where it has soft ground before it), then each free end:
	// a row against the wall's foot and a looser one beyond it, round the corners too.
	for (const double Side : {1.0, -1.0})
	{
		const bool IsFront = Side == Fs;
		if (IsFront && !Front) continue;
		for (double U = -Hl - 0.3; U <= Hl + 0.3; U += 0.45)
		{
			const double Uj = U + (Roll(U, Side, 530) - 0.5) * 0.25;
			if (IsFront && (ByDoor(Uj) || OnDrive(Uj))) continue;
			const double Out1 = 0.18 + 0.22 * Roll(Uj, Side, 531);
			Clump(Uj, Side * (Hs + Out1), Out1, 540);
			if (!Hard && Roll(Uj, Side, 532) < 0.8)
			{
				const double Out2 = 0.55 + 0.6 * Roll(Uj, Side, 533);
				Clump(Uj + 0.22, Side * (Hs + Out2), Out2, 550);
			}
			if (!Hard && Roll(Uj, Side, 534) < 0.35)
			{
				const double Out3 = 1.3 + 1.0 * Roll(Uj, Side, 535);
				Clump(Uj - 0.15, Side * (Hs + Out3), Out3, 555);
			}
		}
	}
	for (const double End : {1.0, -1.0})
	{
		if ((End > 0.0 && House.PartyPlus) || (End < 0.0 && House.PartyMinus)) continue;
		for (double V = -Hs + 0.3; V <= Hs - 0.3; V += 0.45)
		{
			if (Fs * V > Hs - 0.8 && OnDrive(End * Hl)) continue;
			const double Vj = V + (Roll(End * 7.0, V, 560) - 0.5) * 0.25;
			const double Out1 = 0.18 + 0.22 * Roll(End * 7.0, Vj, 561);
			Clump(End * (Hl + Out1), Vj, Out1, 570);
			if (!Hard && Roll(End * 7.0, Vj, 562) < 0.8)
			{
				const double Out2 = 0.55 + 0.6 * Roll(End * 7.0, Vj, 563);
				Clump(End * (Hl + Out2), Vj + 0.22, Out2, 580);
			}
			if (!Hard && Roll(End * 7.0, Vj, 564) < 0.35)
			{
				const double Out3 = 1.3 + 1.0 * Roll(End * 7.0, Vj, 565);
				Clump(End * (Hl + Out3), Vj - 0.15, Out3, 585);
			}
		}
	}
}
} // namespace TorqueWorld
