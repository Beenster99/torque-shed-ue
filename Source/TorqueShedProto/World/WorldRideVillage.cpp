// [everyday kits stage 6] The village set: what stands in the map's playgrounds, on its pitches and courts and round
// its village greens (Tools/blender_village_props.py's models, each with a hand-made distance version). Placed by
// rule on the map's own outlines: a playground's pieces inside its bow-top fence, a football pitch's goals and a
// rugby pitch's posts at its ends, a court's nets, wire fence and hoops, a green's village sign, notice board, seat
// and bin on the side nearest the road. Every square works a whole outline out alike and keeps its own props.
#include "WorldRideVillage.h"

#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideWarehouses.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <mutex>
#include <vector>

namespace TorqueWorld
{
namespace
{
std::mutex VillageKitLock;
std::shared_ptr<const WarehouseKit>& VillageKitTable()
{
	static std::shared_ptr<const WarehouseKit> Table;
	return Table;
}
} // namespace

bool SetVillageKit(const std::string& Json, const std::string& Bin)
{
	std::shared_ptr<WarehouseKit> Kit = ReadWarehouseKit(Json, Bin);
	if (!Kit) return false;
	std::lock_guard<std::mutex> Hold(VillageKitLock);
	VillageKitTable() = std::move(Kit);
	return true;
}

std::shared_ptr<const WarehouseKit> VillageKitNow()
{
	std::lock_guard<std::mutex> Hold(VillageKitLock);
	return VillageKitTable();
}

const char* VillageClassOf(const std::string& Kind, double Long, double Short, double Floors, bool Stone, double Seed)
{
	// A school the map draws, of one or two floors (a tall one, a city's secondary block, stays drawn); small ones
	// old (a Victorian board school, stone in stone country) or a timber classroom, big ones post-war or new.
	if (Kind != "school" && Kind != "kindergarten") return "";
	const double Area = Long * Short;
	if (Floors >= 3.0 || Short < 5.0 || Area < 40.0) return "";
	if (Area < 400.0 && Short < 11.0)
		return Area < 140.0 && Seed < 0.4 ? "school_small" : Stone ? "school_old_stone" : "school_old_brick";
	return "school_new";
}

namespace
{
// (an outline's best-fitting oblong: its middle, its long way and across, half its length and width)
struct Oblong
{
	P2 Mid, U, V;
	double A = 0.0, B = 0.0;
};

Oblong FitOblong(const std::vector<P2>& Ring)
{
	Oblong Best;
	double BestArea = 1e18;
	for (size_t K = 0; K < Ring.size(); ++K)
	{
		const P2 E = Ring[(K + 1) % Ring.size()] - Ring[K];
		const double El = E.Length();
		if (El < 0.5) continue;
		const P2 U = E * (1.0 / El), V(-U.Z, U.X);
		double U0 = 1e18, U1 = -1e18, V0 = 1e18, V1 = -1e18;
		for (const P2& P : Ring)
		{
			const double Pu = P.X * U.X + P.Z * U.Z, Pv = P.X * V.X + P.Z * V.Z;
			U0 = std::fmin(U0, Pu); U1 = std::fmax(U1, Pu);
			V0 = std::fmin(V0, Pv); V1 = std::fmax(V1, Pv);
		}
		const double Area = (U1 - U0) * (V1 - V0);
		if (Area >= BestArea) continue;
		BestArea = Area;
		const double Cu = (U0 + U1) * 0.5, Cv = (V0 + V1) * 0.5;
		Best.Mid = P2(U.X * Cu + V.X * Cv, U.Z * Cu + V.Z * Cv);
		Best.U = U; Best.V = V;
		Best.A = (U1 - U0) * 0.5; Best.B = (V1 - V0) * 0.5;
	}
	if (Best.B > Best.A)
	{
		std::swap(Best.A, Best.B);
		const P2 U = Best.U;
		Best.U = Best.V;
		Best.V = U * -1.0;
	}
	return Best;
}

// (a playground's pieces: kind, weight, its footprint about the model's origin along x and across, and how far its
// footprint's middle is along x from the origin)
struct PlayPiece
{
	int Kind;
	double Weight, HalfX, HalfY, MidX, MinArea;
};
const PlayPiece PlayPieces[] = {
	{RidePlan::PropPlaySwings, 1.2, 1.5, 1.95, 0.0, 0.0},
	{RidePlan::PropPlaySlide, 1.0, 3.1, 1.2, 1.4, 0.0},
	{RidePlan::PropPlayClimber, 0.8, 4.1, 2.2, 0.6, 350.0},
	{RidePlan::PropPlaySpringer, 1.2, 0.8, 0.6, 0.0, 0.0},
	{RidePlan::PropPlayRoundabout, 0.5, 1.7, 1.7, 0.0, 0.0},
	{RidePlan::PropPlaySeesaw, 0.5, 2.1, 0.7, 0.0, 0.0},
};
} // namespace

void RidePlan::VillageProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	using namespace RideSceneBits;
	bool Any = false;
	for (int K = PropPlaySwings; K <= PropVillageSign; ++K) Any = Any || PropModelled[K];
	if (!Any || !Scene || FieldAreas.empty()) return;
	std::vector<int> Fields;
	FieldsIn(X0, Z0, Size, Fields);
	const Region& G = Area.Roads;
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
	// (how near the nearest road's edge is: <0 on it)
	const auto RoadGap = [&](double X, double Z)
	{
		NearOpts Opts;
		NearHit Hit;
		return NearestOf(X, Z, Opts, Hit) ? Hit.Dist - Hit.Half : 1e9;
	};
	const auto Mine = [&](const P2& C) { return C.X >= X0 && C.X < X0 + Size && C.Z >= Z0 && C.Z < Z0 + Size; };
	const auto Put = [&](int Kind, const P2& C, const P2& Front, double Y, uint32_t Id, int Variant, float Stretch = 1.0f, float Pitch = 0.0f)
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
		P.Scale = 1.0f;
		P.Stretch = Stretch;
		P.Pitch = Pitch;
		Out.push_back(P);
	};
	// (a fence of Kind round a ring, panels of Panel m fitted to each edge and tipped up its slope; Gap: leave out
	// the panel nearest the road, the way in)
	const auto FenceRound = [&](const FieldArea& F, int Kind, double Panel, int Variant, bool Gap)
	{
		if (!PropModelled[Kind]) return;
		double GateGap = 1e18;
		size_t GateEdge = size_t(-1), GatePanel = 0;
		if (Gap)
			for (size_t K = 0; K < F.Ring.size(); ++K)
			{
				const P2 A = F.Ring[K], B = F.Ring[(K + 1) % F.Ring.size()];
				const double El = (B - A).Length();
				const int N = std::max(1, int(std::lround(El / Panel)));
				for (int I = 0; I < N; ++I)
				{
					const P2 M = A + (B - A) * ((I + 0.5) / N);
					const double Gp = RoadGap(M.X, M.Z);
					if (Gp < GateGap) { GateGap = Gp; GateEdge = K; GatePanel = size_t(I); }
				}
			}
		for (size_t K = 0; K < F.Ring.size(); ++K)
		{
			const P2 A = F.Ring[K], B = F.Ring[(K + 1) % F.Ring.size()];
			const P2 E = B - A;
			const double El = E.Length();
			if (El < 0.8) continue;
			const int N = std::max(1, int(std::lround(El / Panel)));
			const double L = El / N;
			const P2 Dir = E * (1.0 / El);
			for (int I = 0; I < N; ++I)
			{
				if (K == GateEdge && size_t(I) == GatePanel) continue;
				const P2 P0 = A + Dir * (L * I), P1 = A + Dir * (L * (I + 1)), M = (P0 + P1) * 0.5;
				if (!Mine(M)) continue;
				const double Y0 = Ground(P0.X, P0.Z), Y1 = Ground(P1.X, P1.Z), Ym = Ground(M.X, M.Z);
				if (std::isnan(Y0) || std::isnan(Y1) || std::isnan(Ym)) continue;
				if (RoadGap(M.X, M.Z) < 0.3 || OnBuilding(M.X, M.Z, 0.1)) continue;
				Put(Kind, M, Dir, std::fmin(Ym, (Y0 + Y1) * 0.5), F.Id * 131u + uint32_t(K * 64 + I), Variant, float(L / Panel), float(std::atan2(Y1 - Y0, L)));
			}
		}
	};
	for (int Fi : Fields)
	{
		const FieldArea& F = FieldAreas[size_t(Fi)];
		if (F.Sub == SubNone || F.Ring.size() < 3) continue;
		if (F.MaxX < X0 - 20.0 || F.MinX > X0 + Size + 20.0 || F.MaxZ < Z0 - 20.0 || F.MinZ > Z0 + Size + 20.0) continue;
		P2 Mid;
		const double RingArea = std::fabs(RideShapes::RingArea(F.Ring, Mid));
		if (RingArea < 40.0 || RingArea > 40000.0) continue;
		const Oblong O = FitOblong(F.Ring);
		const double Sx = double(F.Id % 9973u) + 0.5, Sz = double(F.Id / 9973u % 9973u) + 0.5;   // (the outline's own seed)
		const auto Level = [&](const P2& C, double Hx, double Hy, const P2& Ux, double& Low)
		{
			// (its corners and middle on the ground, in the outline, off buildings and roads; the ground fairly level)
			const P2 Uy(-Ux.Z, Ux.X);
			double Lo = 1e9, Hi = -1e9;
			for (double Su : {-1.0, 0.0, 1.0})
				for (double Sv : {-1.0, 0.0, 1.0})
				{
					const P2 Q = C + Ux * (Su * Hx) + Uy * (Sv * Hy);
					const double Gy = Ground(Q.X, Q.Z);
					if (std::isnan(Gy)) return false;
					Lo = std::fmin(Lo, Gy); Hi = std::fmax(Hi, Gy);
				}
			Low = Lo;
			return Hi - Lo < 0.8;
		};
		if (F.Sub == SubPlayground || F.Sub == SubGreen)
		{
			Held.clear(); Pieces.clear(); Covered.clear();
			OutlinesIn(std::floor((F.MinX - 20.0) / 64.0) * 64.0, std::floor((F.MinZ - 20.0) / 64.0) * 64.0,
				std::ceil((std::fmax(F.MaxX - F.MinX, F.MaxZ - F.MinZ) + 40.0) / 64.0) * 64.0 + 64.0, Held, Pieces, Covered);
		}
		if (F.Sub == SubPlayground)
		{
			if (RingArea > 6000.0) continue;
			// (the fence round it, the way in towards the road; green mostly, black in one in three)
			FenceRound(F, PropPlayFence, 2.5, Chance(Sx, Sz, 3) < 0.33 ? 1 : 0, true);
			// (its pieces, by weight, each inside the fence with room round it)
			struct Placed { P2 C; double R; };
			std::vector<Placed> Done;
			const int Want = std::clamp(int(RingArea / 110.0) + 1, 1, 7);
			int Made = 0;
			for (int Try = 0; Try < Want * 10 && Made < Want; ++Try)
			{
				double Sum = 0.0;
				for (const PlayPiece& T : PlayPieces)
					if (PropModelled[T.Kind] && RingArea >= T.MinArea) Sum += T.Weight;
				if (Sum <= 0.0) break;
				double At = Chance(Sx, Sz, 101 + Try) * Sum;
				const PlayPiece* Pick = nullptr;
				for (const PlayPiece& T : PlayPieces)
				{
					if (!PropModelled[T.Kind] || RingArea < T.MinArea) continue;
					At -= T.Weight;
					if (At <= 0.0) { Pick = &T; break; }
				}
				if (!Pick) continue;
				const P2 C(F.MinX + (F.MaxX - F.MinX) * Chance(Sx, Sz, 131 + Try), F.MinZ + (F.MaxZ - F.MinZ) * Chance(Sx, Sz, 151 + Try));
				const double Turn = Chance(Sx, Sz, 171 + Try);
				const P2 Ux = Turn < 0.25 ? O.U : Turn < 0.5 ? O.U * -1.0 : Turn < 0.75 ? O.V : O.V * -1.0;
				const P2 Uy(-Ux.Z, Ux.X);
				const double R = std::hypot(Pick->HalfX, Pick->HalfY);
				bool Clear = true;
				for (const Placed& Pl : Done) Clear = Clear && (Pl.C - C).Length() > Pl.R + R + 0.8;
				for (double Su : {-1.0, 1.0})
					for (double Sv : {-1.0, 1.0})
					{
						const P2 Q = C + Ux * (Su * (Pick->HalfX + 0.6)) + Uy * (Sv * (Pick->HalfY + 0.6));
						Clear = Clear && RideShapes::InRing(F.Ring, Q.X, Q.Z) && !OnBuilding(Q.X, Q.Z, 0.5);
					}
				double Low = 0.0;
				if (!Clear || !Level(C, Pick->HalfX, Pick->HalfY, Ux, Low) || RoadGap(C.X, C.Z) < R + 1.0) continue;
				Done.push_back({C, R});
				++Made;
				// (the model's origin: its footprint's middle back along its x)
				Put(Pick->Kind, C - Ux * Pick->MidX, Ux, Low, F.Id * 31u + uint32_t(Try), int(Chance(Sx, Sz, 191 + Try) * 255.0));
			}
			// (a litter bin inside by the way in: the ring's point nearest the road, set in)
			if (PropModelled[PropLitterBin])
			{
				double Best = 1e18;
				P2 Gate = F.Ring[0], In;
				for (size_t K = 0; K < F.Ring.size(); ++K)
				{
					const P2 M = (F.Ring[K] + F.Ring[(K + 1) % F.Ring.size()]) * 0.5;
					const double Gp = RoadGap(M.X, M.Z);
					if (Gp < Best) { Best = Gp; Gate = M; }
				}
				In = Mid - Gate;
				if (In.Length() > 2.5)
				{
					In = In * (1.0 / In.Length());
					const P2 C = Gate + In * 1.2 + P2(-In.Z, In.X) * 1.8;
					const double Gy = Ground(C.X, C.Z);
					if (!std::isnan(Gy) && RideShapes::InRing(F.Ring, C.X, C.Z) && !OnBuilding(C.X, C.Z, 0.3))
						Put(PropLitterBin, C, In * -1.0, Gy, F.Id * 37u + 1u, 0);
				}
			}
			continue;
		}
		if (F.Sub == SubGreen)
		{
			if (RingArea < 300.0) continue;
			// (on the side nearest the road: the sign facing it, the notice board and a seat along from it, a bin)
			double Best = 1e18;
			size_t BestEdge = 0;
			double BestAlong = 0.0;
			for (size_t K = 0; K < F.Ring.size(); ++K)
			{
				const P2 A = F.Ring[K], B = F.Ring[(K + 1) % F.Ring.size()];
				const double El = (B - A).Length();
				if (El < 12.0) continue;
				for (double T = 6.0; T <= El - 6.0; T += 4.0)
				{
					const P2 M = A + (B - A) * (T / El);
					const double Gp = RoadGap(M.X, M.Z);
					if (Gp > 0.5 && Gp < Best) { Best = Gp; BestEdge = K; BestAlong = T; }
				}
			}
			if (Best > 25.0) continue;
			const P2 A = F.Ring[BestEdge], B = F.Ring[(BestEdge + 1) % F.Ring.size()];
			const double El = (B - A).Length();
			const P2 Dir = (B - A) * (1.0 / El);
			P2 In(-Dir.Z, Dir.X);
			const P2 Foot = A + Dir * BestAlong;
			if (!RideShapes::InRing(F.Ring, Foot.X + In.X * 2.0, Foot.Z + In.Z * 2.0)) In = In * -1.0;
			struct Thing { int Kind; double Along, In; int Variant; };
			const double Side = Chance(Sx, Sz, 7) < 0.5 ? -1.0 : 1.0;
			const Thing Things[] = {
				{PropVillageSign, 0.0, 2.0, int(Chance(Sx, Sz, 9) * 255.0)},
				{PropNoticeBoard, 4.5 * Side, 1.6, 0},
				{PropSeat, -4.0 * Side, 1.8, int(Chance(Sx, Sz, 11) * 255.0)},
				{PropLitterBin, -5.4 * Side, 1.4, Chance(Sx, Sz, 13) < 0.5 ? 0 : 1},
			};
			int Kept = 0;
			for (const Thing& T : Things)
			{
				const double Along = BestAlong + T.Along;
				if (Along < 1.0 || Along > El - 1.0 || !PropModelled[T.Kind]) continue;
				const P2 C = A + Dir * Along + In * T.In;
				double Low = 0.0;
				if (!RideShapes::InRing(F.Ring, C.X, C.Z) || OnBuilding(C.X, C.Z, 0.6) || !Level(C, 0.6, 0.6, Dir, Low) || RoadGap(C.X, C.Z) < 1.0) continue;
				// (all face the road: the sign and board to be read from it, the seat looking out at it)
				Put(T.Kind, C, In * -1.0, Low, F.Id * 41u + uint32_t(Kept++), T.Variant);
			}
			continue;
		}
		// (a pitch or court)
		if (F.Sub == SubFootball || F.Sub == SubRugby)
		{
			if (O.A < 18.0 || O.A > 75.0 || O.B < 10.0) continue;
			const int Kind = F.Sub == SubFootball ? PropPitchGoal : PropPitchPosts;
			for (double S : {-1.0, 1.0})
			{
				// (on the goal line, a little in from the outline's end)
				const P2 C = O.Mid + O.U * (S * (O.A - (F.Sub == SubFootball ? 2.5 : 4.0)));
				double Low = 0.0;
				if (!RideShapes::InRing(F.Ring, C.X, C.Z) || !Level(C, 1.0, 3.8, O.U, Low) || RoadGap(C.X, C.Z) < 4.0) continue;
				Put(Kind, C, O.U * -S, Low, F.Id * 43u + (S > 0 ? 1u : 0u), 0);
			}
			continue;
		}
		if (F.Sub == SubTennis || F.Sub == SubBasketball || F.Sub == SubMultiCourt)
		{
			if (RingArea > 9000.0) continue;
			FenceRound(F, PropCourtFence, 3.0, 0, false);
			if (F.Sub == SubTennis)
			{
				// (courts side by side: each about 36 m long and 17 m wide with its run-backs)
				P2 Long = O.U, Across = O.V;
				double Len = O.A * 2.0, Wide = O.B * 2.0;
				if (Len > 45.0 && Wide >= 26.0 && Wide <= 45.0) { std::swap(Len, Wide); std::swap(Long, Across); }
				if (Len < 26.0 || Len > 45.0) continue;
				const int N = std::clamp(int(std::lround(Wide / 17.0)), 1, 8);
				for (int I = 0; I < N; ++I)
				{
					const P2 C = O.Mid + Across * ((I + 0.5) * Wide / N - Wide * 0.5);
					double Low = 0.0;
					if (!Level(C, 0.3, 6.4, Long, Low)) continue;
					Put(PropTennisNet, C, Long, Low, F.Id * 47u + uint32_t(I), 0);
				}
			}
			else
			{
				if (O.A < 7.0 || O.A > 22.0) continue;
				for (double S : {-1.0, 1.0})
				{
					const P2 C = O.Mid + O.U * (S * (O.A - 1.8));
					double Low = 0.0;
					if (!Level(C, 1.2, 0.9, O.U, Low)) continue;
					Put(PropBasketHoop, C, O.U * -S, Low, F.Id * 53u + (S > 0 ? 1u : 0u), 0);
				}
			}
		}
	}
}
} // namespace TorqueWorld
