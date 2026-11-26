// [everyday kits stage 7] The bridge set: what holds a road's bridge up and lines its edges, as the models of
// Tools/blender_bridge_props.py (each with two hand-made distance versions), laid from the road's own deck points
// with its piece (RoadPieceIn: Out.Props, instanced as the bridge walls are). The road build still draws the deck
// and, on a country road, its stone walls; this adds, for each bridge whose deck starts in the piece:
//   a country road's (no wider than a lane each way: the stone-walled ones): its stone arches from end to end, one
//     every 14 m or so, in the region's walling, their faces just proud of the deck's own, down to the ground;
//   a wider road's: concrete piers every 32 m or so under the deck (wall piers in water), an abutment at each end
//     where the deck stands clear of the ground, and the modern parapet along both edges (the road build leaves its
//     plain slab parapet off then: BridgeRailsOn). A country road's bridge with no wall kit keeps the slab parapet.
// Nothing within a whole junction's reach of a road end that has one. Off: -TorqueBridges=0 (WORLD_RIDE_BRIDGEKIT=0).
#include "WorldRide.h"
#include "WorldRideForecourtKit.h"
#include "WorldRideSceneBits.h"
#include "WorldRoadLine.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace TorqueWorld
{
bool RidePlan::BridgeRailsOn() const
{
	return BridgeKitOn() && PropModelled[PropBridgeRail];
}

void RidePlan::BridgeKitProps(int Which, int First, int Last, RideMesh& Out) const
{
	if (!BridgeKitOn() || Which < 0 || Which >= RoadCount()) return;
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	const int Count = int(Pts.size());
	if (Count < 2) return;
	const auto Put = [&Out](int Kind, int Variant, const P2& At, double Y, const P2& Dir, double Scale, double Stretch, double Across, double Pitch)
	{
		RideProp Pr;
		Pr.Kind = uint16_t(Kind);
		Pr.Variant = uint8_t(Variant);
		Pr.Mapped = false;
		Pr.X = float(At.X - Out.OriginX);
		Pr.Y = float(Y);
		Pr.Z = float(At.Z - Out.OriginZ);
		Pr.Yaw = float(std::atan2(Dir.Z, Dir.X));
		Pr.Pitch = float(Pitch);
		Pr.Scale = float(Scale);
		Pr.Stretch = float(Stretch);
		Pr.Across = float(Across);
		Out.Props.push_back(Pr);
	};
	const auto Wet = [this](const P2& P) { return Land && (Land->Cover(P.X, P.Z) == Terrain::CoverWater || Land->SeaAt(P.X, P.Z)); };
	const double Total = Pts.back().Along;
	const double NearEnd[2] = {WholeAt(Which, 0) >= 0 ? WholeCutMost + 5.0 : 0.0, WholeAt(Which, 1) >= 0 ? WholeCutMost + 5.0 : 0.0};
	const auto ClearOfWhole = [&](double Along) { return Along >= NearEnd[0] && Along <= Total - NearEnd[1]; };
	for (int S0 = std::max(0, First); S0 <= Last && S0 < Count; ++S0)
	{
		if (!Pts[size_t(S0)].Bridge || (S0 > 0 && Pts[size_t(S0) - 1].Bridge)) continue;
		if (S0 == Last && Last != Count - 1) continue;   // (the next piece's: it starts there)
		int S1 = S0;
		while (S1 + 1 < Count && Pts[size_t(S1) + 1].Bridge) ++S1;
		if (S1 == S0) continue;
		const double A0 = Pts[size_t(S0)].Along, A1 = Pts[size_t(S1)].Along, L = A1 - A0;
		if (L < 3.0 || !ClearOfWhole(A0) || !ClearOfWhole(A1)) continue;
		// (the deck at a distance along it: its middle, its way, its height, its half width)
		const auto DeckAt = [&](double A, P2& At, P2& Dir, double& Y, double& Half)
		{
			A = std::clamp(A, A0, A1);
			int K = S0;
			while (K + 1 < S1 && Pts[size_t(K) + 1].Along < A) ++K;
			const RoadPoint& P = Pts[size_t(K)];
			const RoadPoint& Q = Pts[size_t(K) + 1];
			const double T = std::clamp((A - P.Along) / std::fmax(1e-6, Q.Along - P.Along), 0.0, 1.0);
			At = P.At + (Q.At - P.At) * T;
			Dir = Q.At - P.At;
			const double Dl = Dir.Length();
			Dir = Dl > 1e-9 ? Dir * (1.0 / Dl) : P2(1.0, 0.0);
			Y = P.Height + (Q.Height - P.Height) * T;
			Half = P.Half + (Q.Half - P.Half) * T;
		};
		bool Country = true;
		for (int K = S0; K <= S1; ++K) Country = Country && Pts[size_t(K)].Half <= 4.5 && Pts[size_t(K)].Lanes <= 2;
		// (a country road's bridge over a wide road, a dual carriageway or a motorway, is a modern one: its piers and
		// abutments, the road build's slab parapet left as it is)
		bool OverWide = false;
		for (int I = 1; I < 8 && !OverWide; ++I)
		{
			const RoadPoint& P = Pts[size_t(S0 + (S1 - S0) * I / 8)];
			NearOpts Others;
			Others.NotRoad = Which;
			NearHit Hit;
			OverWide = NearestOf(P.At.X, P.At.Z, Others, Hit) && Hit.Dist < Hit.Half && Hit.Height < P.Height - 3.0 && Hit.Half > 4.5;
		}
		const bool Arches = Country && !OverWide;
		// (the lowest ground under a stretch of deck, across its middle half)
		const auto LowGround = [&](double Ua, double Ub, double Half0)
		{
			double Low = 1e18;
			for (int I = 0; I <= 6; ++I)
				for (double V : {-0.5, 0.0, 0.5})
				{
					P2 At, Dir;
					double Y = 0.0, Half = 0.0;
					DeckAt(Ua + (Ub - Ua) * I / 6.0, At, Dir, Y, Half);
					const P2 Q = At + P2(Dir.Z, -Dir.X) * (V * Half0);
					const double G = Ground(Q.X, Q.Z);
					if (!std::isnan(G)) Low = std::fmin(Low, G);
				}
			return Low;
		};
		if (Arches && L <= 120.0 && PropModelled[PropBridgeArch])
		{
			// A stone bridge: arches from end to end, each no more than a metre lower at one end of the deck than the other.
			const int Style = std::clamp(WallStyle(LookHere(Pts[size_t(S0)].At.X, Pts[size_t(S0)].At.Z)), 0, WallStyles - 1);
			int N = std::max(1, int(std::lround(L / 14.0)));
			N = std::max(N, int(std::ceil(std::fabs(Pts[size_t(S1)].Height - Pts[size_t(S0)].Height) / 0.8)));
			for (int I = 0; I < N; ++I)
			{
				const double Ua = A0 + L * I / N, Ub = A0 + L * (I + 1) / N;
				P2 Pa, Pb, Da, Db;
				double Ya = 0.0, Yb = 0.0, Ha = 0.0, Hb = 0.0;
				DeckAt(Ua, Pa, Da, Ya, Ha);
				DeckAt(Ub, Pb, Db, Yb, Hb);
				P2 Dir = Pb - Pa;
				const double Run = Dir.Length();
				if (Run < 0.5) continue;
				Dir = Dir * (1.0 / Run);
				const double Top = std::fmin(Ya, Yb) - 0.1;
				const double Low = LowGround(Ua, Ub, std::fmax(Ha, Hb));
				if (Low > 1e17) continue;
				const double H = std::fmax(Top - (Low - 0.3), 1.2);
				if (H > 30.0) continue;
				const double Wide = 2.0 * (std::fmax(Ha, Hb) + 0.67);
				// (each a little longer than its share, so where the deck bends the next one closes the joint)
				Put(PropBridgeArch, Style, (Pa + Pb) * 0.5, Top - H, Dir, H, (Run + 0.5) / H, Wide / H, 0.0);
			}
			continue;
		}
		// A modern bridge: piers, abutments, and its parapet.
		const int Spans = L <= 18.0 ? 1 : int(std::ceil(L / 32.0));
		if (PropModelled[PropBridgePier])
			for (int I = 1; I < Spans; ++I)
			{
				const double A = A0 + L * I / Spans;
				for (double Shift : {0.0, 4.0, -4.0, 8.0, -8.0})
				{
					P2 At, Dir;
					double Y = 0.0, Half = 0.0;
					DeckAt(A + Shift, At, Dir, Y, Half);
					const P2 Left(Dir.Z, -Dir.X);
					// (not standing on a road below, nor its verge)
					bool OnRoad = false;
					for (double V : {-0.6, 0.0, 0.6})
					{
						const P2 Q = At + Left * (V * Half);
						NearOpts Others;
						Others.NotRoad = Which;
						NearHit Hit;
						OnRoad = OnRoad || (NearestOf(Q.X, Q.Z, Others, Hit) && Hit.Dist < Hit.Half + 1.5 && Hit.Height < Y - 2.0);
					}
					if (OnRoad) continue;
					double Low = 1e18;
					for (double V : {-0.6, 0.0, 0.6})
					{
						const P2 Q = At + Left * (V * Half);
						const double G = Ground(Q.X, Q.Z);
						if (!std::isnan(G)) Low = std::fmin(Low, G);
					}
					const double Top = Y - 1.1;
					const double H = Top - (Low - 0.3);
					if (Low > 1e17 || H < 1.0 || H > 60.0) break;
					const int Variant = Wet(At) ? 1 : 0;
					Put(PropBridgePier, Variant, At, Top - H, Dir, H, 1.2 / (0.16 * H), 2.0 * Half * 0.85 / (0.8 * H), 0.0);
					break;
				}
			}
		if (PropModelled[PropBridgeAbutment])
			for (int End = 0; End < 2; ++End)
			{
				// (2 m in from the deck's end, its face to the bridge's middle, where the ground there falls away under it)
				P2 At, Dir;
				double Y = 0.0, Half = 0.0;
				DeckAt(End == 0 ? A0 + 2.0 : A1 - 2.0, At, Dir, Y, Half);
				const P2 Face = End == 0 ? Dir : Dir * -1.0;
				const P2 Under = At + Face * 3.0;
				const double G = Ground(Under.X, Under.Z);
				if (std::isnan(G)) continue;
				const double Top = Y - 1.1;
				const double H = Top - (G - 0.3);
				if (H < 1.0 || H > 25.0) continue;
				Put(PropBridgeAbutment, 0, At, Top - H, Face, H, 1.0, 2.0 * (Half + 0.6) / H, 0.0);
			}
		if (!Country && BridgeRailsOn())
		{
			// (the parapet's 2.5 m lengths along each edge, on the deck's edge beam, tipped up its slope; none across
			// another road that joins on the deck)
			const int Lengths = std::max(1, int(std::lround(L / 2.5)));
			for (double Side : {1.0, -1.0})
				for (int I = 0; I < Lengths; ++I)
				{
					P2 Pa, Pb, Da, Db;
					double Ya = 0.0, Yb = 0.0, Ha = 0.0, Hb = 0.0;
					DeckAt(A0 + L * I / Lengths, Pa, Da, Ya, Ha);
					DeckAt(A0 + L * (I + 1) / Lengths, Pb, Db, Yb, Hb);
					const P2 Qa = Pa + P2(Da.Z, -Da.X) * (Side * (Ha + 0.15 + 0.175)), Qb = Pb + P2(Db.Z, -Db.X) * (Side * (Hb + 0.15 + 0.175));
					const P2 Mid = (Qa + Qb) * 0.5;
					NearOpts Others;
					Others.NotRoad = Which;
					NearHit Hit;
					if (NearestOf(Mid.X, Mid.Z, Others, Hit) && Hit.Dist < Hit.Half + 0.3 && std::fabs(Hit.Height - (Ya + Yb) * 0.5) < 2.0) continue;
					P2 Dir = Qb - Qa;
					const double Run = Dir.Length();
					if (Run < 0.2) continue;
					Dir = Dir * (1.0 / Run);
					// (the model's front, +y its inside: laid with its x along the road's way, either side)
					Put(PropBridgeRail, 0, Mid, (Ya + Yb) * 0.5 - 0.02, Dir, 1.0, Run / 2.5, 1.0, std::atan2(Yb - Ya, Run));
				}
		}
	}
}
} // namespace TorqueWorld
