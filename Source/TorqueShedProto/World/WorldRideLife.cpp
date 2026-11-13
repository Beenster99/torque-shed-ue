// Life on a ride's land (Ride UK stage 6): the sheep and cattle in the fields, by region and season.
//
// Plain placements from nothing but the cells and the month, so every rider has the same flocks in
// the same fields. The models are Tools/make_ride_animals.py's; World/TorqueRideWorld draws them.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RideLifeBits
{
constexpr double Square = 256.0;   // (as the scenery's index files its lines and buildings)

long long KeyOf(double X, double Z)
{
	return static_cast<long long>(std::floor(X / Square)) * 4000037LL + static_cast<long long>(std::floor(Z / Square));
}

// A number from 0 to 1, the same every time for the same place and salt.
double Roll(double X, double Z, uint32_t Salt)
{
	uint32_t H = static_cast<uint32_t>(static_cast<int32_t>(std::floor(X * 4.0))) * 2654435761u ^ static_cast<uint32_t>(static_cast<int32_t>(std::floor(Z * 4.0))) * 40503u
		^ Salt * 2246822519u;
	H ^= H >> 15;
	H *= 0x2c1b3c6du;
	H ^= H >> 12;
	H *= 0x297a2d39u;
	H ^= H >> 15;
	return double(H & 0xffffffu) / double(0x1000000);
}
} // namespace RideLifeBits

const char* RidePlan::AnimalName(int Kind)
{
	static const char* Names[AnimalKinds] = {"ewe_graze", "ewe_look", "ewe_lie", "lamb", "cow_graze", "cow_stand", "cow_lie", "highland"};
	return Kind >= 0 && Kind < AnimalKinds ? Names[Kind] : "";
}

void RidePlan::AnimalsTile(double X0, double Z0, double Size, std::vector<RideAnimal>& Out) const
{
	using namespace RideLifeBits;
	Out.clear();
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	const LocalLook& Look = LookHere(X0 + Size * 0.5, Z0 + Size * 0.5);
	// How many flocks and herds: the hill country is sheep, the lowlands more cattle, the fens arable.
	const std::string& Where = Look.Name;
	const bool Hills = Where == "dales" || Where == "lakes" || Where == "moors" || Where == "peak" || Where == "wales" || Where == "northumberland" || Where == "highlands"
		|| Where == "lowlands";
	double Sheep = Hills ? 1.0 : 0.45, Cattle = Hills ? 0.35 : 0.75;
	if (Where == "fens") { Sheep = 0.05; Cattle = 0.12; }
	const bool Housed = Month >= 11 || Month <= 3;   // (most cattle are in the sheds over winter)
	if (Housed) Cattle *= 0.25;
	const bool Lambing = Month >= 3 && Month <= 6;
	const bool Highlands = Where == "highlands";

	// (whether a point is fit to stand on: pasture, or the moor for sheep in the hills; clear of the
	// roads, railways, buildings, walls and water; not too steep)
	const auto Fit = [&](double X, double Z, bool Moor, double& Y)
	{
		if (X < X0 || X >= X0 + Size || Z < Z0 || Z >= Z0 + Size) return false;
		const int Cover = Land->Cover(X, Z);
		if (!(Cover == 0 || Cover == 2 || (Moor && Cover == 3))) return false;
		if (Land->SeaAt(X, Z)) return false;
		NearOpts Opts;
		NearHit Hit;
		if (NearestOf(X, Z, Opts, Hit) && Hit.Dist < Hit.Half + 5.0) return false;
		// (beenster, build 142: up close they look odd, so they keep to the distance: no road within
		// FarFromRoad, rings of points round it each looking 40 m about)
		for (double R = 35.0; R <= FarFromRoad; R += 35.0)
			for (int K = 0; K < 12; ++K)
			{
				const double A = K * 0.5235987755982988 + R * 0.01;
				if (NearestOf(X + R * std::cos(A), Z + R * std::sin(A), Opts, Hit)) return false;
			}
		double RailDist = 0.0, RailTop = 0.0;
		int RailOn = 0;
		if (NearRail(X, Z, RailDist, RailTop, RailOn) && RailOn != 2 && RailDist < 7.0) return false;
		const auto Builds = Scene->Buildings.find(KeyOf(X, Z));
		if (Builds != Scene->Buildings.end())
			for (int B : Builds->second)
			{
				const Building& Bd = G.Buildings[size_t(B)];
				if (std::hypot(Bd.X - X, Bd.Z - Z) < std::fmax(Bd.W, Bd.D) * 0.6 + 2.0) return false;
			}
		const auto Walls = Scene->Lines.find(KeyOf(X, Z));
		if (Walls != Scene->Lines.end())
			for (int L : Walls->second)
			{
				const std::vector<P2>& Line = G.Scenery.Lines[size_t(L)].Line;
				for (size_t K = 0; K + 1 < Line.size(); ++K)
				{
					const P2 A = Line[K], AB = Line[K + 1] - Line[K];
					const double Len2 = AB.X * AB.X + AB.Z * AB.Z;
					const double T = Len2 > 1e-9 ? std::clamp(((X - A.X) * AB.X + (Z - A.Z) * AB.Z) / Len2, 0.0, 1.0) : 0.0;
					if (std::hypot(A.X + AB.X * T - X, A.Z + AB.Z * T - Z) < 1.3) return false;
				}
			}
		Y = Ground(X, Z);
		if (std::isnan(Y)) return false;
		const double Ex = Ground(X + 1.5, Z) - Ground(X - 1.5, Z), Ez = Ground(X, Z + 1.5) - Ground(X, Z - 1.5);
		return !(std::isnan(Ex) || std::isnan(Ez)) && std::hypot(Ex, Ez) / 3.0 < 0.55;
	};
	const auto Put = [&Out, X0, Z0](int Kind, double X, double Y, double Z, double Yaw, double Scale, double Pick)
	{
		RideAnimal A;
		A.Kind = static_cast<uint16_t>(Kind);
		A.X = float(X - X0);
		A.Y = float(Y);
		A.Z = float(Z - Z0);
		A.Yaw = float(Yaw);
		A.Scale = float(Scale);
		A.Pick = float(Pick);
		Out.push_back(A);
	};
	const double Pi2 = 6.283185307179586;
	// A flock or a herd now and then in each 64 m of the square, gathered round a point of it.
	const double Cell = 64.0;
	for (double Cz = std::floor(Z0 / Cell) * Cell; Cz < Z0 + Size; Cz += Cell)
		for (double Cx = std::floor(X0 / Cell) * Cell; Cx < X0 + Size; Cx += Cell)
		{
			const double Pick = Roll(Cx, Cz, 61);
			const bool Flock = Pick < Sheep * 0.2, Herd = !Flock && Pick < Sheep * 0.2 + Cattle * 0.1;
			if (!Flock && !Herd) continue;
			const double Mx = Cx + Roll(Cx, Cz, 62) * Cell, Mz = Cz + Roll(Cx, Cz, 63) * Cell;
			double Here = 0.0;
			if (!Fit(Mx, Mz, Flock && Hills, Here)) continue;
			const double Heading = Roll(Cx, Cz, 64) * Pi2;
			const int Count = Flock ? 4 + int(Roll(Cx, Cz, 65) * 9.0) : 4 + int(Roll(Cx, Cz, 66) * 7.0);
			const double Spread = Flock ? 7.0 : 10.0, Coat = Roll(Cx, Cz, 67);
			for (int K = 0; K < Count; ++K)
			{
				const double R = Spread * std::sqrt(Roll(Cx + K, Cz, 70)), A = Roll(Cx, Cz + K, 71) * Pi2;
				const double X = Mx + std::cos(A) * R, Z = Mz + std::sin(A) * R;
				double Y = 0.0;
				if (!Fit(X, Z, Flock && Hills, Y)) continue;
				const double Face = Heading + (Roll(X, Z, 72) - 0.5) * 2.4, Pose = Roll(X, Z, 73);
				if (Flock)
				{
					const int Kind = Pose < 0.55 ? EweGraze : Pose < 0.8 ? EweLook : EweLie;
					Put(Kind, X, Y - 0.02, Z, Face, 0.92 + 0.14 * Roll(X, Z, 74), Roll(X, Z, 75));
					// (in spring a lamb or two at her side)
					if (Lambing && Roll(X, Z, 76) < 0.55)
						for (int L = 0; L < (Roll(X, Z, 77) < 0.7 ? 1 : 2); ++L)
						{
							const double Side = (L == 0 ? 1.0 : -1.0) * 0.75;
							const double Lx = X - std::cos(Face) * 0.4 - std::sin(Face) * Side, Lz = Z - std::sin(Face) * 0.4 + std::cos(Face) * Side;
							double Ly = 0.0;
							if (Fit(Lx, Lz, Hills, Ly)) Put(Lamb, Lx, Ly - 0.02, Lz, Face + (Roll(Lx, Lz, 78) - 0.5) * 1.2, 0.5 + 0.08 * Roll(Lx, Lz, 79), Roll(Lx, Lz, 80));
						}
				}
				else
				{
					const int Kind = Highlands ? Highland : Pose < 0.55 ? CowGraze : Pose < 0.8 ? CowStand : CowLie;
					Put(Kind, X, Y - 0.03, Z, Face, 0.94 + 0.12 * Roll(X, Z, 74), Highlands ? Roll(X, Z, 75) : Coat);
				}
			}
		}
}
RidePlace RidePlan::SoundsAt(double X, double Z) const
{
	using namespace RideLifeBits;
	RidePlace P;
	if (!Scene || !Land) return P;
	const Region& G = Area.Roads;
	// The sea: the nearest of 8 ways out at 30 to 400 m.
	if (Land->SeaAt(X, Z)) P.Sea = 1.0;
	else
		for (double R : {30.0, 70.0, 130.0, 220.0, 400.0})
		{
			bool Found = false;
			for (int K = 0; K < 8 && !Found; ++K) Found = Land->SeaAt(X + std::cos(K * 0.7853981633974483) * R, Z + std::sin(K * 0.7853981633974483) * R);
			if (Found) { P.Sea = std::clamp(1.15 - R / 400.0, 0.15, 1.0); break; }
		}
	// Running water: the nearest river, beck or canal (a canal hardly runs), or a lake's edge.
	double Closest = 1e9;
	for (int Dz = -1; Dz <= 1; ++Dz)
		for (int Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = Scene->Streams.find(KeyOf(X + Dx * Square, Z + Dz * Square));
			if (It == Scene->Streams.end()) continue;
			for (int S : It->second)
			{
				const Stream& Flow = G.Streams[size_t(S)];
				const double Quiet = Flow.Kind == 2 ? 50.0 : 0.0;   // (a canal heard as if further off)
				for (size_t I = 0; I + 1 < Flow.Line.size(); ++I)
				{
					const P2 A = Flow.Line[I], AB = Flow.Line[I + 1] - Flow.Line[I];
					const double Len2 = AB.X * AB.X + AB.Z * AB.Z;
					const double T = Len2 > 1e-9 ? std::clamp(((X - A.X) * AB.X + (Z - A.Z) * AB.Z) / Len2, 0.0, 1.0) : 0.0;
					Closest = std::min(Closest, std::hypot(A.X + AB.X * T - X, A.Z + AB.Z * T - Z) + Quiet);
				}
			}
		}
	P.Water = std::clamp(1.0 - (Closest - 8.0) / 72.0, 0.0, 1.0);
	// A town: how many buildings stand within 150 m (40 or more is the middle of one).
	int Built = 0;
	for (int Dz = -1; Dz <= 1; ++Dz)
		for (int Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = Scene->Buildings.find(KeyOf(X + Dx * Square, Z + Dz * Square));
			if (It == Scene->Buildings.end()) continue;
			for (int B : It->second)
				if (std::hypot(G.Buildings[size_t(B)].X - X, G.Buildings[size_t(B)].Z - Z) < 150.0) ++Built;
		}
	P.Town = std::clamp((Built - 4) / 36.0, 0.0, 1.0);
	// The flocks and herds: each animal near by counts, the nearer the more.
	// (they keep their distance from the roads, so a bleat carries a few hundred metres)
	const double X0 = std::floor((X - 352.0) / 64.0) * 64.0, Z0 = std::floor((Z - 352.0) / 64.0) * 64.0;
	std::vector<RideAnimal> Beasts;
	AnimalsTile(X0, Z0, 768.0, Beasts);
	for (const RideAnimal& A : Beasts)
	{
		const double D = std::hypot(X0 + A.X - X, Z0 + A.Z - Z);
		if (D > 350.0) continue;
		const double Heard = 1.0 / (1.0 + (D / 150.0) * (D / 150.0));
		(A.Kind <= Lamb ? P.Sheep : P.Cattle) += Heard * 0.25;
	}
	P.Sheep = std::min(P.Sheep, 1.0);
	P.Cattle = std::min(P.Cattle, 1.0);
	return P;
}
} // namespace TorqueWorld
