// [driveways] The houses' cars on their drives (beenster, Nov 22: "I think we should add some of the fab cars
// onto driveways now to make the villages and towns come to life more"; and 15:22: "dont put them on the sides
// of the roads yet": drives only, never at the kerb). BuildingsTile hands each house here as it knows it
// (GardenHouse: its front, its door, its drive's end, its party walls); about DriveCarShare of the houses with a
// drive get a car on it:
//  - in the front garden, in the gap the front wall leaves for the drive, nose to the house or reversed in,
//    where the garden is deep enough for a car (5.1 m from the house to the pavement's back);
//  - else beside the house's free gable end (not a party wall), its nose to the road, where there's room;
// each on a drive of its own out to the pavement (tarmac, gravel, block paving or concrete, one a house).
// Only on dry, level, clear ground: off the road and its pavement, clear of every building, of the rails and
// of the square's other drive cars, never where the game draws water (CarSpotWet: the Uig pier lesson).
//
// Performance first: the cars are the car parks' (PropParkedCar: the Fab cars, instanced, the kit's cull
// distance), only on the finest squares; the drive is a few flat triangles a house. -TorqueRideDriveCars=0
// leaves them out (the hub's frame rate A/B), =0.3 a smaller share.
//
// Also here: CarSpotWet (never a drive car where the game draws water: the sea flooding a low shore, a river, a lake).
#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace TorqueWorld
{
struct RidePlan::DriveCarMemo
{
	std::mutex Lock;
	struct Square { double X0 = 0.0, Z0 = 0.0, Size = 0.0; std::vector<ParkedCarBox> Cars; };
	std::unordered_map<long long, Square> Squares;
};

// (named, not anonymous: the game's unity build puts several of these files into one)
namespace RideDriveways
{
using RideShapes::Paint;
using RideShapes::Pt;
using RideShapes::V3;

// A parked car's size (as the car parks', WorldRideCarParks.cpp) and the gaps it keeps.
inline constexpr double CarLong = 4.3, CarWide = 1.8, CarHigh = 1.45;
inline constexpr double FromHouse = 0.8;      // its back (or nose) off the house's front wall
inline constexpr double FromFront = 0.35;     // off the front wall or the pavement's back
inline constexpr double FrontDeepLeast = FromHouse + CarLong + FromFront;   // a front garden a car fits in
inline constexpr double GradeMost = 0.12;     // the steepest drive a car is left on, nose to tail (1 in 8)
inline constexpr double AcrossMost = 0.2;     // its two sides' ground within this (m)
inline constexpr double DriveLift = 0.04;     // the drive's surface over the drawn ground
inline constexpr double KeepWithin = 3000.0;  // squares' cars kept for the bike within this of the newest

// The drives' surfaces: tarmac (M_RideRoad's key), gravel, block paving (a warm grey-red), concrete.
inline const Paint DriveTarmac = {0.03f, 0.03f, 0.033f}, DriveGravel = {0.2f, 0.18f, 0.14f}, DriveBlocks = {0.2f, 0.12f, 0.09f}, DriveConcrete = {0.3f, 0.3f, 0.28f};
// The desk test's plain cars (the game has the Fab models).
inline const Paint DeskPaints[] = {{0.55f, 0.56f, 0.57f}, {0.012f, 0.03f, 0.09f}, {0.8f, 0.8f, 0.78f}, {0.2f, 0.012f, 0.012f}, {0.08f, 0.08f, 0.085f}, {0.18f, 0.3f, 0.45f}};
inline const Paint DeskGlass = {0.02f, 0.025f, 0.03f};

inline long long SquareKey(double X0, double Z0)
{
	return static_cast<long long>(std::floor(X0 / 16.0)) * 4000037LL + static_cast<long long>(std::floor(Z0 / 16.0));
}

// Whether two level boxes (middle, unit direction, half length and width) overlap, with Gap between: the
// separating axis test on their four axes.
inline bool BoxesMeet(const RidePlan::ParkedCarBox& A, const RidePlan::ParkedCarBox& B, double Gap)
{
	const double Ax[2][2] = {{A.Ux, A.Uz}, {-A.Uz, A.Ux}}, Bx[2][2] = {{B.Ux, B.Uz}, {-B.Uz, B.Ux}};
	const double Ah[2] = {A.HalfLong + Gap * 0.5, A.HalfWide + Gap * 0.5}, Bh[2] = {B.HalfLong + Gap * 0.5, B.HalfWide + Gap * 0.5};
	const double Dx = B.X - A.X, Dz = B.Z - A.Z;
	for (int Side = 0; Side < 2; ++Side)
		for (int K = 0; K < 2; ++K)
		{
			const double* N = Side == 0 ? Ax[K] : Bx[K];
			const double Ra = Ah[0] * std::fabs(Ax[0][0] * N[0] + Ax[0][1] * N[1]) + Ah[1] * std::fabs(Ax[1][0] * N[0] + Ax[1][1] * N[1]);
			const double Rb = Bh[0] * std::fabs(Bx[0][0] * N[0] + Bx[0][1] * N[1]) + Bh[1] * std::fabs(Bx[1][0] * N[0] + Bx[1][1] * N[1]);
			if (std::fabs(Dx * N[0] + Dz * N[1]) > Ra + Rb) return false;
		}
	return true;
}

// A level box for the desk test's plain cars: middle (X, Z), along unit Dir, HalfU along and HalfV across, Y0 to Y1.
inline void DeskBox(RideMesh& Out, const P2& Mid, const P2& Dir, double HalfU, double HalfV, double Y0, double Y1, const Paint& C)
{
	const P2 Across(-Dir.Z, Dir.X);
	const P2 Q[4] = {Mid - Dir * HalfU - Across * HalfV, Mid + Dir * HalfU - Across * HalfV, Mid + Dir * HalfU + Across * HalfV, Mid - Dir * HalfU + Across * HalfV};
	for (int K = 0; K < 4; ++K)
	{
		const P2 &A = Q[K], &B = Q[(K + 1) % 4];
		RideShapes::SkyQuad(Out, Pt(A.X, Y0, A.Z), Pt(B.X, Y0, B.Z), Pt(B.X, Y1, B.Z), Pt(A.X, Y1, A.Z), C, true);
	}
	RideShapes::SkyQuad(Out, Pt(Q[0].X, Y1, Q[0].Z), Pt(Q[1].X, Y1, Q[1].Z), Pt(Q[2].X, Y1, Q[2].Z), Pt(Q[3].X, Y1, Q[3].Z), C, true);
}
} // namespace RideDriveways

bool RidePlan::CarSpotWet(double X, double Z) const
{
	if (!Land) return false;
	if (Land->Cover(X, Z) == Terrain::CoverWater || Land->SeaAt(X, Z)) return true;
	const double Y = LandDrawnAt(X, Z);
	if (std::isnan(Y)) return false;   // (not known yet: the car parks' bays are kept once laid, so not ruled out for it)
	// (the sea: WaterTile lays it at its mean level and spreads it over every point within 80 m of it lower than
	// its biggest springs' high water, TideTop; a car a little over that is dry)
	const double TideTop = TideHalfAt(X, Z) * 1.25 + 0.2;
	if (Y < TideTop + 0.3)
		for (const double R : {12.0, 30.0, 55.0, 85.0})
			for (int K = 0; K < 8; ++K)
				if (Land->SeaAt(X + std::cos(K * 0.7853981633974483) * R, Z + std::sin(K * 0.7853981633974483) * R)) return true;
	// (a river's or a stream's run beside it, its water over the ground there; a lake or reservoir at its level)
	double Wd = 0.0, Ws = 0.0, Wh = 0.0, Wdp = 0.0;
	if (NearWater(X, Z, Wd, Ws, Wh, Wdp) && Wd < Wh + 3.0 && Y < Ws + 0.3) return true;
	const double Still = StillLevel(X, Z);
	return !std::isnan(Still) && Y < Still + 0.3;
}

void RidePlan::DrivewayCar(const GardenHouse& House, const GardenClear& Clear, std::vector<ParkedCarBox>& Taken, RideMesh& Out) const
{
	using namespace RideSceneBits;
	using namespace RideDriveways;
	if (DriveCarShare <= 0.0 || !Land || House.DriveTo <= House.DriveFrom) return;
	// (only a house a road comes up to: past 25 m from its front there's no drive to the road)
	if (House.FrontGap > 25.0) return;
	const auto Roll = [&House](int Salt) { return Chance(House.X * 1.13 + 0.5, House.Z * 0.87 - 0.5, Salt); };
	if (Roll(901) >= std::fmin(1.0, DriveCarShare)) return;
	const double Fs = House.FrontSide, Hl = House.HalfLong, Hs = House.HalfShort;
	const double Deep = House.FrontGap + 1.0 - 1.6;   // (the front garden: the house's front to the pavement's back, as GardenPlants')
	const double End = House.DriveTo > Hl ? 1.0 : -1.0;   // (the drive's end of the house, along)
	// (house frame to the world: along U, across V, as BuildingsTile's Local and GardenPlants' World)
	const auto World = [&House](double U, double V)
	{
		const double Lx = House.LongX ? U : V, Lz = House.LongX ? V : U;
		return P2(House.X + Lx * House.Cs + Lz * House.Sn, House.Z - Lx * House.Sn + Lz * House.Cs);
	};
	const P2 Origin = World(0.0, 0.0);
	const P2 AlongV = World(0.0, 1.0) - Origin;   // (unit, in the world: across the house, to its front when Fs is 1)
	const auto Edge = [this](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return NearestOf(P.X, P.Z, Opts, Hit) && Hit.Dist <= Hit.Half + Spare;
	};
	// One place to try: the car's middle in the house's frame (Uc, Vc), lying along V (to and from the road),
	// its drive from Vd0 to Vd1 (across) and Ud0 to Ud1 (along). False if it doesn't fit there.
	struct Spot { double Uc, Vc, Ud0, Ud1, Vd0, Vd1; };
	const auto Try = [&](const Spot& S) -> bool
	{
		const P2 Mid = World(S.Uc, S.Vc);
		// (nose to the house, or reversed in: nose to the road, half of them; beside the house, nose out a third)
		const bool NoseOut = Roll(905) < (Deep >= FrontDeepLeast ? 0.45 : 0.35);
		const P2 Dir = AlongV * (Fs * (NoseOut ? 1.0 : -1.0));
		ParkedCarBox Box;
		Box.X = Mid.X;
		Box.Z = Mid.Z;
		Box.Ux = Dir.X;
		Box.Uz = Dir.Z;
		Box.HalfLong = CarLong * 0.5;
		Box.HalfWide = CarWide * 0.5;
		// Every corner and side's middle: clear of the buildings (the house's own too, by 0.25 m), off the road
		// and its pavement, dry, off the rails; the wheels' ground level enough.
		const P2 Across(-Dir.Z, Dir.X);
		double Lo = 1e18, Hi = -1e18, NoseY = 0.0, TailY = 0.0, LeftY = 0.0, RightY = 0.0;
		for (const double A : {-1.0, 0.0, 1.0})
			for (const double C : {-1.0, 0.0, 1.0})
			{
				const P2 P = Mid + Dir * (A * CarLong * 0.5) + Across * (C * CarWide * 0.5);
				if (Clear.Blocked(P.X, P.Z, 0.25) || Edge(P, 1.7) || CarSpotWet(P.X, P.Z)) return false;
				double RailDist = 0.0, RailTop = 0.0;
				int RailOn = 0;
				if (NearRail(P.X, P.Z, RailDist, RailTop, RailOn) && RailOn != 2 && RailDist < 4.0) return false;
				const int Fa = FieldAt(P.X, P.Z);
				if (Fa >= 0)
				{
					const int Fk = FieldAreas[size_t(Fa)].Kind;
					if (Fk != FieldLawn && Fk != FieldPark && Fk != FieldHard && Fk != FieldScrub && Fk != FieldSite) return false;
				}
				if (A != 0.0 && C != 0.0)
				{
					const double Y = LandDrawnAt(P.X, P.Z);
					if (std::isnan(Y)) return false;
					Lo = std::fmin(Lo, Y);
					Hi = std::fmax(Hi, Y);
					(A > 0.0 ? NoseY : TailY) += Y * 0.5;
					(C > 0.0 ? LeftY : RightY) += Y * 0.5;
				}
			}
		// (tipped up the drive's slope, nose to tail, as a car sits; across it only a little: a car can't lean)
		if (std::fabs(NoseY - TailY) > CarLong * GradeMost || std::fabs(LeftY - RightY) > AcrossMost || Hi - Lo > CarLong * GradeMost + AcrossMost) return false;
		for (const ParkedCarBox& Other : Taken)
			if (BoxesMeet(Box, Other, 0.6)) return false;
		// The drive: from the pavement's back to the house (or past the car's far end beside it), every metre
		// laid on the drawn ground, a little over it; not drawn on the road or the pavement.
		const double Ud0 = S.Ud0, Ud1 = S.Ud1, Vd0 = std::fmin(S.Vd0, S.Vd1), Vd1 = std::fmax(S.Vd0, S.Vd1);
		const int Nu = std::max(1, int(std::ceil((Ud1 - Ud0) / 1.0))), Nv = std::max(1, int(std::ceil((Vd1 - Vd0) / 1.0)));
		const double Pick = Roll(903);
		const Paint& Surface = Pick < 0.3 ? DriveTarmac : Pick < 0.55 ? DriveBlocks : Pick < 0.8 ? DriveGravel : DriveConcrete;
		std::vector<double> Ys(size_t(Nu + 1) * size_t(Nv + 1), std::nan(""));
		std::vector<P2> Ps(Ys.size());
		for (int J = 0; J <= Nv; ++J)
			for (int I = 0; I <= Nu; ++I)
			{
				const P2 P = World(Ud0 + (Ud1 - Ud0) * I / Nu, Vd0 + (Vd1 - Vd0) * J / Nv);
				const size_t K = size_t(J) * size_t(Nu + 1) + size_t(I);
				Ps[K] = P;
				const double Y = LandDrawnAt(P.X, P.Z);
				// (each point over the ground round it too: the land's 4 m triangles bend between them)
				double Most = Y;
				for (const double D : {0.5, -0.5})
				{
					Most = std::fmax(Most, LandDrawnAt(P.X + D, P.Z));
					Most = std::fmax(Most, LandDrawnAt(P.X, P.Z + D));
				}
				if (!std::isnan(Y) && !Edge(P, 1.5)) Ys[K] = Most + DriveLift;
			}
		for (int J = 0; J < Nv; ++J)
			for (int I = 0; I < Nu; ++I)
			{
				const size_t K00 = size_t(J) * size_t(Nu + 1) + size_t(I), K10 = K00 + 1, K01 = K00 + size_t(Nu + 1), K11 = K01 + 1;
				if (std::isnan(Ys[K00]) || std::isnan(Ys[K10]) || std::isnan(Ys[K01]) || std::isnan(Ys[K11])) continue;
				const V3 Q0 = Pt(Ps[K00].X, Ys[K00], Ps[K00].Z), Q1 = Pt(Ps[K10].X, Ys[K10], Ps[K10].Z), Q2 = Pt(Ps[K11].X, Ys[K11], Ps[K11].Z), Q3 = Pt(Ps[K01].X, Ys[K01], Ps[K01].Z);
				// (facing up, whichever way the house's frame turns: SkyQuad shows the side (Q2 - Q0) x (Q3 - Q1) points to)
				const double Ny = (Q2.Z - Q0.Z) * (Q3.X - Q1.X) - (Q2.X - Q0.X) * (Q3.Z - Q1.Z);
				if (Ny > 0.0) RideShapes::SkyQuad(Out, Q0, Q1, Q2, Q3, Surface, false);
				else RideShapes::SkyQuad(Out, Q0, Q3, Q2, Q1, Surface, false);
			}
		// The car, its wheels on the drive: its middle at the four wheels' mean, tipped nose to tail.
		const double Foot = (NoseY + TailY) * 0.5 + DriveLift, Rise = NoseY - TailY;
		Box.Y0 = Foot - std::fabs(Rise) * 0.5;
		Box.Y1 = Foot + CarHigh + std::fabs(Rise) * 0.5;
		Taken.push_back(Box);
		// (not quite square on the drive)
		const double Skew = (Roll(906) - 0.5) * 0.05;
		const P2 Facing(Dir.X * std::cos(Skew) - Dir.Z * std::sin(Skew), Dir.X * std::sin(Skew) + Dir.Z * std::cos(Skew));
		if (PropModelled[PropParkedCar])
		{
			RideProp P;
			P.Kind = PropParkedCar;
			P.Mapped = false;
			P.Id = static_cast<uint32_t>(Roll(907) * 4294967295.0);
			P.Variant = static_cast<uint8_t>(Roll(908) * 255.0);
			P.X = float(Mid.X - Out.OriginX);
			P.Z = float(Mid.Z - Out.OriginZ);
			P.Y = float(Foot);
			P.Yaw = float(std::atan2(Facing.Z, Facing.X));
			P.Pitch = float(std::atan2(Rise, CarLong));   // (as a wall's length up its slope: the nose end climbs Rise)
			Out.Props.push_back(P);
		}
		else
		{
			// (the desk test: a plain body and cabin, nose marked by the cabin sitting back)
			const Paint& Body = DeskPaints[size_t(Roll(908) * 5.999)];
			DeskBox(Out, Mid, Facing, CarLong * 0.5, CarWide * 0.5, Foot + 0.2, Foot + 0.85, Body);
			const P2 Cab = Mid - Facing * 0.25;
			DeskBox(Out, Cab, Facing, 1.15, CarWide * 0.5 - 0.08, Foot + 0.85, Foot + 1.3, DeskGlass);
			DeskBox(Out, Cab, Facing, 1.0, CarWide * 0.5 - 0.1, Foot + 1.3, Foot + CarHigh, Body);
		}
		return true;
	};
	const double Ucar = End * (Hl - 0.95);   // (the middle of the drive's gap, as BuildingsTile leaves it)
	const double Front = Hs + Deep;           // (the pavement's back, across)
	// In the front garden, where a car fits between the house and the pavement.
	if (Deep >= FrontDeepLeast)
	{
		const double Vc = Hs + FromHouse + CarLong * 0.5 + (Deep - FrontDeepLeast) * 0.3 * Roll(904);
		if (Try({Ucar, Fs * Vc, Ucar - 1.3, Ucar + 1.3, Fs * Hs, Fs * Front})) return;
	}
	// Beside the house's free end (not a party wall: a semi's outer end, a detached house's), its front 0.6 m
	// past the house's front (or the garden's own depth less a little), its drive on out to the pavement.
	if ((End > 0.0 && House.PartyPlus) || (End < 0.0 && House.PartyMinus)) return;
	if (Deep < 0.3) return;
	const double Uside = End * (Hl + 0.4 + CarWide * 0.5 + 0.1);
	const double Nose = Hs + std::fmin(0.6, Deep - FromFront);
	const double Vc = Nose - CarLong * 0.5;
	Try({Uside, Fs * Vc, Uside - 1.3, Uside + 1.3, Fs * (Vc - CarLong * 0.5 - 0.3), Fs * Front});
}

void RidePlan::ResetDriveCars()
{
	DriveMemo = std::make_shared<DriveCarMemo>();
}

void RidePlan::KeepDriveCars(double X0, double Z0, double Size, std::vector<ParkedCarBox>&& Cars) const
{
	using namespace RideDriveways;
	if (!DriveMemo) return;
	std::lock_guard<std::mutex> Hold(DriveMemo->Lock);
	DriveCarMemo::Square& Sq = DriveMemo->Squares[SquareKey(X0, Z0)];
	Sq.X0 = X0;
	Sq.Z0 = Z0;
	Sq.Size = Size;
	Sq.Cars = std::move(Cars);
	// (the squares far behind dropped: the bike only asks round itself)
	const double Mx = X0 + Size * 0.5, Mz = Z0 + Size * 0.5;
	for (auto It = DriveMemo->Squares.begin(); It != DriveMemo->Squares.end();)
	{
		if (std::hypot(It->second.X0 + It->second.Size * 0.5 - Mx, It->second.Z0 + It->second.Size * 0.5 - Mz) > KeepWithin) It = DriveMemo->Squares.erase(It);
		else ++It;
	}
}

void RidePlan::DriveCarsNear(double X, double Z, double Reach, std::vector<ParkedCarBox>& Out) const
{
	if (!DriveMemo) return;
	std::lock_guard<std::mutex> Hold(DriveMemo->Lock);
	for (const auto& Each : DriveMemo->Squares)
	{
		const DriveCarMemo::Square& Sq = Each.second;
		if (X < Sq.X0 - Reach || X > Sq.X0 + Sq.Size + Reach || Z < Sq.Z0 - Reach || Z > Sq.Z0 + Sq.Size + Reach) continue;
		for (const ParkedCarBox& B : Sq.Cars)
			if (std::hypot(B.X - X, B.Z - Z) <= Reach) Out.push_back(B);
	}
}
} // namespace TorqueWorld
