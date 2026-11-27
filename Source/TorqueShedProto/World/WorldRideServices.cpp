// [everyday kits stage 8b] The motorway service areas: what IndexTownLife (World/WorldRideTownLife.cpp) found for each of
// the kits bake's services areas (RidePlan::ServiceAreas), laid on the squares they stand in: the main building as the
// services set's hall (Tools/blender_services_props.py, two designs, each with two hand-made distance versions)
// stretched to the map's box, which the buildings then leave undrawn (HallBuilding), its made-up name over the doors in
// the sign alphabet; the name pylon on the verge facing the main road, the name on both its faces; the parked lorries
// on the lorry park's tarmac (a field area, FieldForecourt). The fuel forecourt is the forecourts' own (the map's fuel
// there). Our own names: no operators'.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideSigns.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
namespace
{
std::mutex HallLock;
std::unordered_map<const void*, std::unordered_map<size_t, Building>> HallsFound;   // (by plan, by area)
}

bool RidePlan::HallOf(size_t Which, Building& Hall) const
{
	if (Which >= ServiceAreas.size()) return false;
	{
		std::lock_guard<std::mutex> Hold(HallLock);
		const auto Pl = HallsFound.find(this);
		if (Pl != HallsFound.end())
		{
			const auto It = Pl->second.find(Which);
			if (It != Pl->second.end()) { Hall = It->second; return true; }
		}
	}
	// (the boxes the buildings draw there: the outlines' pieces or the cells' own; none under a tower's outline. Not
	// kept until found: a cell's buildings can come late)
	const ServiceArea& A = ServiceAreas[Which];
	std::vector<Building> Boxes;
	HouseFootBoxes(A.MinX, A.MinZ, std::fmax(A.MaxX - A.MinX, A.MaxZ - A.MinZ), Boxes);
	double Most = 300.0;
	bool Got = false;
	for (const Building& H : Boxes)
	{
		if (H.W * H.D < Most || !RideShapes::InRing(A.Ring, H.X, H.Z) || UnderTower(H.X, H.Z)) continue;
		Most = H.W * H.D;
		Hall = H;
		Got = true;
	}
	if (Got)
	{
		std::lock_guard<std::mutex> Hold(HallLock);
		HallsFound[this][Which] = Hall;
	}
	return Got;
}

bool RidePlan::HallBuilding(const Building* B) const
{
	if (!PropModelled[PropServicesHall] || B == nullptr) return false;
	for (size_t I = 0; I < ServiceAreas.size(); ++I)
	{
		const ServiceArea& A = ServiceAreas[I];
		if (B->X < A.MinX || B->X > A.MaxX || B->Z < A.MinZ || B->Z > A.MaxZ) continue;
		Building H;
		if (HallOf(I, H) && std::fabs(H.X - B->X) < 0.05 && std::fabs(H.Z - B->Z) < 0.05 && std::fabs(H.W - B->W) < 0.05 && std::fabs(H.D - B->D) < 0.05) return true;
	}
	return false;
}

// (whether a lorry at At heading Front stands clear of the buildings there)
static bool LorryClear(const std::vector<Building>& Boxes, const P2& At, const P2& Front)
{
	for (const Building& B : Boxes)
	{
		const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
		for (double U = -8.5; U <= 8.51; U += 2.125)
			for (const double V : {-1.4, 1.4})
			{
				const double X = At.X + Front.X * U - Front.Z * V, Z = At.Z + Front.Z * U + Front.X * V;
				const double Dx = X - B.X, Dz = Z - B.Z;
				if (std::fabs(Dx * Cs - Dz * Sn) < B.W * 0.5 + 0.5 && std::fabs(Dx * Sn + Dz * Cs) < B.D * 0.5 + 0.5) return false;
			}
	}
	return true;
}

void RidePlan::ServicesTile(double X0, double Z0, double Size, RideMesh& Out) const
{
	if (ServiceAreas.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const auto Put = [&Out](int Kind, int Variant, const P2& At, double Y, const P2& Front, double Stretch, double Across)
	{
		RideProp Pr;
		Pr.Kind = uint16_t(Kind);
		Pr.Variant = uint8_t(Variant);
		Pr.Mapped = false;
		Pr.X = float(At.X - Out.OriginX);
		Pr.Y = float(Y);
		Pr.Z = float(At.Z - Out.OriginZ);
		Pr.Yaw = float(std::atan2(Front.Z, Front.X));
		Pr.Stretch = float(Stretch);
		Pr.Across = float(Across);
		Out.Props.push_back(Pr);
	};
	static constexpr float Cream[3] = {0.85f, 0.82f, 0.7f};
	// (a name across a face at most Wide m long, its capitals Cap m tall or less to fit)
	const auto Name = [&Out](const std::string& Words, const P2& At, double Y, const P2& Face, double Wide, double Cap)
	{
		double W = 0.0;
		if (!SignText::Width(Words, Cap, W) || W <= 0.0) return;
		if (W > Wide) Cap *= Wide / W;
		SignText::Set(Out.SignsOf(), At.X, Y, At.Z, Face.X, Face.Z, Cap, Words, Cream);
	};
	for (size_t Ai = 0; Ai < ServiceAreas.size(); ++Ai)
	{
		const ServiceArea& A = ServiceAreas[Ai];
		if (A.MaxX < X0 - 20.0 || A.MinX > X0 + Size + 20.0 || A.MaxZ < Z0 - 20.0 || A.MinZ > Z0 + Size + 20.0) continue;
		// (the main building: 20 m deep and 40 m across as modelled, stretched to the box; its fascia 16 m across, its middle
		// 5.55 m up on the flat-roofed one, 4.95 on the barrel-roofed, 0.22 m proud of its front)
		Building H;
		if (PropModelled[PropServicesHall] && HallOf(Ai, H) && Inside(P2(H.X, H.Z)))
		{
			// (its front, a long side, to the main road's side of it)
			const double Cs = std::cos(H.Yaw), Sn = std::sin(H.Yaw);
			const P2 Ac = H.W >= H.D ? P2(Sn, Cs) : P2(Cs, -Sn);
			P2 To = A.MainAt - P2(H.X, H.Z);
			const P2 HallFront = To.X * Ac.X + To.Z * Ac.Z >= 0.0 ? Ac : Ac * -1.0;
			const double Base = Ground(H.X, H.Z);
			if (!std::isnan(Base))
			{
				const double Deep = std::fmin(H.W, H.D), Wide = std::fmax(H.W, H.D);
				const P2 At(H.X, H.Z);
				Put(PropServicesHall, A.HallDesign, At, Base, HallFront, Deep / 20.0, Wide / 40.0);
				const double FasciaY = Base + (A.HallDesign == 0 ? 5.55 : 4.95);
				Name(A.Name, At + HallFront * (Deep * 0.5 + 0.23), FasciaY, HallFront, 15.0 * Wide / 40.0, 0.62);
			}
		}
		if (A.Pylon && PropModelled[PropServicesPylon] && Inside(A.PylonAt))
		{
			const double Y = Ground(A.PylonAt.X, A.PylonAt.Z);
			if (!std::isnan(Y))
			{
				Put(PropServicesPylon, 0, A.PylonAt, Y, A.PylonFront, 1.0, 1.0);
				for (const double Side : {1.0, -1.0})
					Name(A.Name, A.PylonAt + A.PylonFront * (Side * 0.18), Y + 6.6, A.PylonFront * Side, 2.8, 0.42);
			}
		}
		// (the lorry park's tarmac: 2 m tiles on the ground, 4 cm proud, those in this square, a white line between bays)
		if (A.Park.size() == 4)
		{
			using namespace RideSceneBits;
			static const Rgb Tar = {0.11f, 0.11f, 0.115f}, Paint = {0.7f, 0.7f, 0.68f};
			const P2 O = A.Park[0], Eu = A.Park[1] - A.Park[0], Ev = A.Park[3] - A.Park[0];
			const double Lu = Eu.Length(), Lv = Ev.Length();
			const P2 Du = Eu * (1.0 / std::fmax(1e-6, Lu)), Dv = Ev * (1.0 / std::fmax(1e-6, Lv));
			const int Nu = std::max(1, int(std::ceil(Lu / 2.0))), Nv = std::max(1, int(std::ceil(Lv / 2.0)));
			const auto G3 = [&](double U, double V, double Lift)
			{
				const P2 Q = O + Du * U + Dv * V;
				const double Y = Ground(Q.X, Q.Z);
				return At3(Q.X, (std::isnan(Y) ? 0.0 : Y) + Lift, Q.Z);
			};
			for (int Iv = 0; Iv < Nv; ++Iv)
				for (int Iu = 0; Iu < Nu; ++Iu)
				{
					const double U0 = Lu * Iu / Nu, U1 = Lu * (Iu + 1) / Nu, V0 = Lv * Iv / Nv, V1 = Lv * (Iv + 1) / Nv;
					if (!Inside(O + Du * ((U0 + U1) * 0.5) + Dv * ((V0 + V1) * 0.5))) continue;
					const P3 Q0 = G3(U0, V0, 0.04);
					Quad(Out, Q0, G3(U1, V0, 0.04), G3(U1, V1, 0.04), G3(U0, V1, 0.04), At3(Q0.X, Q0.Y + 100.0, Q0.Z), Tar);
				}
			// (the bay lines: across the park between the lorries, 4.6 m apart, from 2 m to 2 m short of its far edges)
			const double Mid = Lu * 0.5;
			for (double U = std::fmod(Mid - 2.3, 4.6); U < Lu - 1.0; U += 4.6)
			{
				if (U < 1.0) continue;
				for (int Iv = 0; Iv < Nv; ++Iv)
				{
					const double V0 = std::fmax(2.0, Lv * Iv / Nv), V1 = std::fmin(Lv - 2.0, Lv * (Iv + 1) / Nv);
					if (V1 <= V0 || !Inside(O + Du * U + Dv * ((V0 + V1) * 0.5))) continue;
					const P3 Q0 = G3(U - 0.075, V0, 0.06);
					Quad(Out, Q0, G3(U + 0.075, V0, 0.06), G3(U + 0.075, V1, 0.06), G3(U - 0.075, V1, 0.06), At3(Q0.X, Q0.Y + 100.0, Q0.Z), Paint);
				}
			}
		}
		std::vector<Building> Boxes;
		if (PropModelled[PropServicesLorry] && !A.LorryAt.empty()) HouseFootBoxes(X0, Z0, Size, Boxes);
		if (PropModelled[PropServicesLorry])
			for (size_t K = 0; K < A.LorryAt.size(); ++K)
			{
				if (!Inside(A.LorryAt[K]) || !LorryClear(Boxes, A.LorryAt[K], A.LorryFront[K])) continue;
				const P2 Back = A.LorryAt[K] - A.LorryFront[K] * 8.0, Fore = A.LorryAt[K] + A.LorryFront[K] * 8.0;
				const double Yb = Ground(Back.X, Back.Z), Yf = Ground(Fore.X, Fore.Z), Y = Ground(A.LorryAt[K].X, A.LorryAt[K].Z);
				if (std::isnan(Y) || std::isnan(Yb) || std::isnan(Yf)) continue;
				Put(PropServicesLorry, A.LorryVariant[K], A.LorryAt[K], std::fmin(Y, (Yb + Yf) * 0.5), A.LorryFront[K], 1.0, 1.0);
				Out.Props.back().Pitch = float(std::atan2(Yf - Yb, 16.0));
			}
	}
}
} // namespace TorqueWorld
