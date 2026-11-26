// [everyday kits stage 10] The coast set: the seafront's things from the kits bake (a cell's kits file: Region::
// KitThings) as Tools/blender_coast_props.py's models, each with two hand-made distance versions. A mapped beach hut
// stands as a hut with its doors to the sea (HutFront; a hut the map draws as a building is the building's:
// BuildingsTile); a promenade as its railing on the seaward side (the side the ground falls away), at the
// promenade's level, with a lifebuoy housing every 70 m or so. (The sea walls are the wall kit's, as the map's other
// walls, for now.) Every square works a whole line out alike and keeps its own props.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace TorqueWorld
{
P2 RidePlan::HutFront(const P2& C) const
{
	// (the way the ground falls, 15 m out, of eight: the sea, or the beach down to it; the water's own counts lowest;
	// where it's level, the nearest road; else east)
	double Low = 1e18, High = -1e18;
	P2 Best(1.0, 0.0);
	for (int K = 0; K < 8; ++K)
	{
		const double A = 0.7854 * K;
		const P2 D(std::cos(A), std::sin(A));
		double Y = Ground(C.X + D.X * 15.0, C.Z + D.Z * 15.0);
		if (std::isnan(Y)) Y = -1e9;
		if (Y < Low) { Low = Y; Best = D; }
		High = std::fmax(High, Y);
	}
	if (High - Low > 0.3) return Best;
	NearOpts Opts;
	NearHit Hit;
	if (NearestOf(C.X, C.Z, Opts, Hit))
	{
		const P2 To = Hit.Foot - C;
		if (To.Length() > 0.1) return To * (1.0 / To.Length());
	}
	return P2(1.0, 0.0);
}

void RidePlan::CoastProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	using namespace RideSceneBits;
	bool Any = false;
	for (int K = PropCoastBeachHut; K <= PropCoastLifebuoy; ++K) Any = Any || PropModelled[K];
	const std::vector<GroundShape>& Things = Area.Roads.KitThings;
	if (!Any || !Scene || Things.empty()) return;
	const Region& G = Area.Roads;
	const auto Mine = [&](const P2& C) { return C.X >= X0 && C.X < X0 + Size && C.Z >= Z0 && C.Z < Z0 + Size; };
	// (MinSide: only buildings at least that big both ways, the ones BuildingsTile draws)
	const auto OnBuilding = [&](double X, double Z, double Spare, double MinSide = 0.0)
	{
		const auto It = Scene->Buildings.find(SceneKey(X, Z));
		if (It != Scene->Buildings.end())
			for (int I : It->second)
			{
				const Building& B = G.Buildings[size_t(I)];
				if (B.W < MinSide || B.D < MinSide) continue;
				const double Dx = X - B.X, Dz = Z - B.Z, Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
				if (std::fabs(Dx * Cs - Dz * Sn) < B.W * 0.5 + Spare && std::fabs(Dx * Sn + Dz * Cs) < B.D * 0.5 + Spare) return true;
			}
		return UnderTower(X, Z);
	};
	const auto Nearest = [&](double X, double Z, NearHit& Hit)
	{
		NearOpts Opts;
		return NearestOf(X, Z, Opts, Hit);
	};
	const auto OnRoad = [&](double X, double Z, double Spare)
	{
		NearHit Hit;
		return Nearest(X, Z, Hit) && Hit.Dist < Hit.Half + Spare;
	};
	const auto Put = [&](int Kind, const P2& C, const P2& Front, double Y, uint32_t Id, int Variant, double Stretch = 1.0, double Pitch = 0.0)
	{
		if (!PropModelled[Kind] || !Mine(C)) return;
		RideProp P;
		P.Kind = uint16_t(Kind);
		P.Id = Id;
		P.Variant = uint8_t(Variant & 0xff);
		P.X = float(C.X - X0);
		P.Z = float(C.Z - Z0);
		P.Y = float(Y);
		P.Yaw = float(std::atan2(Front.Z, Front.X));
		P.Stretch = float(Stretch);
		P.Pitch = float(Pitch);
		Out.push_back(P);
	};
	// (panels of Kind, Panel m, fitted to each straight of Line offset Off m to its left (negative: right), tipped up
	// its slope; none on a road or a building; a lifebuoy housing every Buoy m on the landward side, facing the line)
	// (each panel at the line's own level, not the ground's where it is offset: a promenade's railing stays up on the
	// promenade where the beach falls away under it)
	const auto Along = [&](const std::vector<P2>& Line, int Kind, double Panel, double Off, int Variant, uint32_t Seed, double Buoy)
	{
		double Run = Buoy * 0.5;
		for (size_t K = 0; K + 1 < Line.size(); ++K)
		{
			const P2 A0 = Line[K], B0 = Line[K + 1];
			const double L = A0.DistanceTo(B0);
			if (L < 0.5) continue;
			const P2 D = (B0 - A0) * (1.0 / L), Left(D.Z, -D.X);
			const P2 A = A0 + Left * Off, B = B0 + Left * Off;
			const int Panels = std::max(1, int(std::lround(L / Panel)));
			for (int I = 0; I < Panels; ++I)
			{
				const P2 Pa = A + (B - A) * (double(I) / Panels), Pb = A + (B - A) * (double(I + 1) / Panels), M = (Pa + Pb) * 0.5;
				const P2 La = Pa - Left * Off, Lb = Pb - Left * Off;
				const double Ya = Ground(La.X, La.Z), Yb = Ground(Lb.X, Lb.Z);
				if (std::isnan(Ya) || std::isnan(Yb) || OnRoad(M.X, M.Z, 0.5) || OnBuilding(M.X, M.Z, 0.3)) continue;
				Put(Kind, M, D, (Ya + Yb) * 0.5, Seed * 131u + uint32_t(K * 64 + size_t(I)), Variant, L / Panels / Panel, std::atan2(Yb - Ya, L / Panels));
			}
			if (Buoy <= 0.0) continue;
			for (double At = Buoy - Run; At <= L; At += Buoy)
			{
				const P2 C = A0 + D * At + Left * (Off - (Off >= 0.0 ? 0.7 : -0.7));
				const double Y = Ground(A0.X + D.X * At, A0.Z + D.Z * At);
				if (std::isnan(Y) || OnRoad(C.X, C.Z, 0.5) || OnBuilding(C.X, C.Z, 0.3)) continue;
				Put(PropCoastLifebuoy, C, Off >= 0.0 ? Left * -1.0 : Left, Y, Seed * 977u + uint32_t(K), 0);
			}
			Run = std::fmod(Run + L, Buoy);
		}
	};
	for (size_t Ti = 0; Ti < Things.size(); ++Ti)
	{
		const GroundShape& T = Things[Ti];
		double MinX = 1e18, MaxX = -1e18, MinZ = 1e18, MaxZ = -1e18;
		for (const P2& P : T.Pts)
		{
			MinX = std::fmin(MinX, P.X); MaxX = std::fmax(MaxX, P.X);
			MinZ = std::fmin(MinZ, P.Z); MaxZ = std::fmax(MaxZ, P.Z);
		}
		if (MaxX < X0 - 10.0 || MinX > X0 + Size + 10.0 || MaxZ < Z0 - 10.0 || MinZ > Z0 + Size + 10.0) continue;
		const uint32_t Seed = static_cast<uint32_t>(std::lround(T.Pts[0].X * 7.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(T.Pts[0].Z * 7.0)) * 19349663u;
		if (T.Kind == "beach_hut")
		{
			// (a point, or a small area's middle; a hut the map also draws as a building is the building's)
			P2 C(0.0, 0.0);
			for (const P2& P : T.Pts) C = C + P;
			C = C * (1.0 / double(T.Pts.size()));
			if (!Mine(C) || MaxX - MinX > 8.0 || MaxZ - MinZ > 8.0 || OnBuilding(C.X, C.Z, 0.2, 2.0) || OnRoad(C.X, C.Z, 1.5)) continue;
			const double Y = Ground(C.X, C.Z);
			if (std::isnan(Y)) continue;
			Put(PropCoastBeachHut, C, HutFront(C), Y, Seed, int(Chance(C.X, C.Z, 901) * 6.0));
		}
		// (a sea wall: the map's walls already stand there as the wall kit's; the parapet model waits for those to give
		// way to it)
		else if (T.Kind == "promenade" && T.Pts.size() >= 2)
		{
			// (the railing on the side the ground falls away, 3 m out from the path's line; none where it is level)
			double Fall = 0.0;
			int N = 0;
			for (size_t K = 0; K + 1 < T.Pts.size(); ++K)
			{
				const P2 A = T.Pts[K], B = T.Pts[K + 1];
				const double L = A.DistanceTo(B);
				if (L < 1.0) continue;
				const P2 M = (A + B) * 0.5, Left((B.Z - A.Z) / L, -(B.X - A.X) / L);
				const double Yl = Ground(M.X + Left.X * 8.0, M.Z + Left.Z * 8.0), Yr = Ground(M.X - Left.X * 8.0, M.Z - Left.Z * 8.0);
				if (std::isnan(Yl) || std::isnan(Yr)) continue;
				Fall += Yr - Yl;   // (positive: the left lower)
				++N;
			}
			if (N == 0 || std::fabs(Fall / N) < 0.4) continue;
			Along(T.Pts, PropCoastPromRail, 2.5, Fall > 0.0 ? 2.2 : -2.2, int(Chance(T.Pts[0].X, T.Pts[0].Z, 903) * 2.0), Seed, 70.0);
		}
	}
}
} // namespace TorqueWorld
