// The road's surface (Ride UK stage 7): what the tyres ride on, and what RoadTile draws to match.
//
// Every mark comes from nothing but the road and how far along it, so the patch a rider sees is the
// patch the tyres feel, and every rider in co-op has the same ones.
#include "WorldRide.h"
#include "WorldRideRoadX.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RideSurfaceBits
{
// A number from 0 to 1, the same every time for the same road, stretch and salt.
double Dice(int Which, int Bucket, uint32_t Salt)
{
	uint32_t H = static_cast<uint32_t>(Which) * 2654435761u ^ static_cast<uint32_t>(Bucket) * 2246822519u ^ Salt * 3266489917u;
	H ^= H >> 15;
	H *= 0x2c1b3c6du;
	H ^= H >> 12;
	H *= 0x297a2d39u;
	H ^= H >> 15;
	return double(H & 0xffffffu) / double(0x1000000);
}

// The point of a road at Along (the one before it: its index), by halving.
size_t PointAt(const std::vector<RoadPoint>& Pts, double Along)
{
	size_t Lo = 0, Hi = Pts.size();
	while (Hi - Lo > 1)
	{
		const size_t Mid = (Lo + Hi) / 2;
		if (Pts[Mid].Along <= Along) Lo = Mid;
		else Hi = Mid;
	}
	return Lo;
}

double HalfAt(const std::vector<RoadPoint>& Pts, double Along)
{
	if (Pts.empty()) return 0.0;
	const size_t I = PointAt(Pts, Along);
	if (I + 1 >= Pts.size()) return Pts.back().Half;
	const double Span = Pts[I + 1].Along - Pts[I].Along;
	const double T = Span > 1e-9 ? std::clamp((Along - Pts[I].Along) / Span, 0.0, 1.0) : 0.0;
	return Pts[I].Half + (Pts[I + 1].Half - Pts[I].Half) * T;
}

void ColourMark(RidePlan::RoadMark& M, float R, float G, float B)
{
	M.Colour[0] = R;
	M.Colour[1] = G;
	M.Colour[2] = B;
}
} // namespace RideSurfaceBits

void RidePlan::RoadMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const
{
	using namespace RideSurfaceBits;
	Out.clear();
	if (Which < 0 || Which >= RoadCount()) return;
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	if (Pts.size() < 2) return;
	if (CircuitRoad(Which)) { CircuitMarks(Which, From, To, Out); return; }   // [circuits] (a circuit's own paint, no patches)
	const double RoadLen = Pts.back().Along;
	const bool Main = Which == 0;
	const auto Built = [&](double Along)
	{
		return Main && !BuiltUp.empty() && BuiltUp[std::min(PointAt(Pts, Along), BuiltUp.size() - 1)] != 0;
	};
	const auto Tunnel = [&](double Along) { return Pts[PointAt(Pts, Along)].Tunnel; };

	// [road rulebook stage 3b] (on the ride's road, the stretches counted along its map road's own way, RideU, and each
	// stretch's dice keyed by where it starts, not by the ride's number or how far along the ride it is: the same
	// patches, seams, grit and covers whichever way and from wherever the road is ridden. A Piece is a run of the
	// road where U = Sign * Along + Off; a mark is cut at a piece's ends, where the map's road meets another)
	struct Piece { double A0, A1, Sign, Off; };
	std::vector<Piece> Pieces;
	const bool ByU = Main && RideU.size() == Pts.size();
	if (ByU)
	{
		const size_t P0 = PointAt(Pts, std::max(From - 70.0, 0.0));
		size_t P1 = PointAt(Pts, To + 70.0);
		P1 = std::min(P1 + 1, Pts.size() - 1);
		Piece Cur{Pts[P0].Along, Pts[P0].Along, 1.0, 0.0};
		bool Open = false;
		for (size_t P = P0; P < P1; ++P)
		{
			const double Da = Pts[P + 1].Along - Pts[P].Along, Du = RideU[P + 1] - RideU[P];
			if (Da < 1e-9) continue;
			const bool Same = std::fabs(std::fabs(Du) - Da) < 0.5;
			if (Same)
			{
				const double Sg = Du < 0.0 ? -1.0 : 1.0, Off = RideU[P] - Sg * Pts[P].Along;
				if (Open && Cur.Sign == Sg && std::fabs(Cur.Off - Off) < 0.5) { Cur.A1 = Pts[P + 1].Along; continue; }
				if (Open) Pieces.push_back(Cur);
				Cur = Piece{Pts[P].Along, Pts[P + 1].Along, Sg, Off};
				Open = true;
			}
			else if (Open)
			{
				// (two map roads meet between these points: each piece runs to the middle)
				Cur.A1 = Pts[P].Along + 0.5 * Da;
				Pieces.push_back(Cur);
				Open = false;
				// (the next piece takes up from that middle on its own U, found from the point after)
				if (P + 2 <= P1 && Pts[P + 2].Along - Pts[P + 1].Along > 1e-9)
				{
					const double Du2 = RideU[P + 2] - RideU[P + 1];
					const double Sg = Du2 < 0.0 ? -1.0 : 1.0;
					Cur = Piece{Pts[P].Along + 0.5 * Da, Pts[P + 1].Along, Sg, RideU[P + 1] - Sg * Pts[P + 1].Along};
					Open = true;
				}
			}
		}
		if (Open) Pieces.push_back(Cur);
	}
	else Pieces.push_back(Piece{-1e18, 1e18, 1.0, 0.0});
	// (where on the road a piece's U is: the ride's road's point there, for the key)
	const auto KeyAt = [&](const Piece& Pc, double U)
	{
		if (!ByU) return Which;
		const double A = std::clamp((U - Pc.Off) / Pc.Sign, Pts.front().Along, Pts.back().Along);
		const size_t I = PointAt(Pts, A);
		const size_t J = std::min(I + 1, Pts.size() - 1);
		const double Sp = Pts[J].Along - Pts[I].Along;
		const double T = Sp > 1e-9 ? std::clamp((A - Pts[I].Along) / Sp, 0.0, 1.0) : 0.0;
		const P2 At = Pts[I].At + (Pts[J].At - Pts[I].At) * T;
		// (a 4 m square: the same for both ways of laying the road, the road's own line being the same within mm)
		const uint32_t Kx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(At.X / 4.0))), Kz = static_cast<uint32_t>(static_cast<int32_t>(std::floor(At.Z / 4.0)));
		uint32_t H = Kx * 73856093u ^ Kz * 19349663u ^ 0x5bd1e995u;
		H ^= H >> 13;
		H *= 0x85ebca6bu;
		H ^= H >> 16;
		return static_cast<int>(H & 0x3fffffffu) + 1;
	};
	// (each bucket of Step m of U over From to To, less Pre before: its piece, number and key; Put turns a mark from U
	// across the piece's own left into the road's along and left, cut to the piece, and keeps it if it reaches From..To)
	const auto Buckets = [&](double Step, double Pre, const auto& Body)
	{
		for (const Piece& Pc : Pieces)
		{
			const double C0 = std::max(Pc.A0, From - Pre - 30.0), C1 = std::min(Pc.A1, To + 30.0);
			if (C1 <= C0) continue;
			const double Ua = Pc.Sign * C0 + Pc.Off, Ub = Pc.Sign * C1 + Pc.Off;
			const double U0 = std::min(Ua, Ub), U1 = std::max(Ua, Ub);
			for (int B = int(std::floor((U0 - Pre) / Step)); double(B) * Step < U1; ++B) Body(Pc, B, KeyAt(Pc, double(B) * Step));
		}
	};
	const auto Put = [&](const Piece& Pc, RoadMark M)
	{
		double A0 = (M.From - Pc.Off) / Pc.Sign, A1 = (M.To - Pc.Off) / Pc.Sign;
		if (A1 < A0) std::swap(A0, A1);
		if (Pc.Sign < 0.0) { const double L = M.Left; M.Left = -M.Right; M.Right = -L; }
		M.From = std::max(A0, Pc.A0);
		M.To = std::min(A1, Pc.A1);
		if (M.To <= M.From || M.To < From || M.From > To) return false;
		Out.push_back(M);
		return true;
	};
	const auto AlongOf = [](const Piece& Pc, double U) { return (U - Pc.Off) / Pc.Sign; };

	// Patches in the wheel tracks, one in a 40 m stretch now and then (more on the lanes, which the
	// council patches and never resurfaces), each ringed with tar where it was sealed.
	Buckets(40.0, 41.0, [&](const Piece& Pc, int B, int Key)
	{
		if (B < 0 || Dice(Key, B, 1) >= (Main ? 0.3 : 0.45)) return;
		RoadMark M;
		M.Kind = MarkPatch;
		M.From = double(B) * 40.0 + Dice(Key, B, 2) * 28.0;
		M.To = M.From + 3.0 + Dice(Key, B, 3) * 9.0;
		const double F = std::min(AlongOf(Pc, M.From), AlongOf(Pc, M.To)), T = std::max(AlongOf(Pc, M.From), AlongOf(Pc, M.To));
		if (T > RoadLen - 2.0 || F < 2.0 || Tunnel(AlongOf(Pc, M.From))) return;
		const double Half = std::min(HalfAt(Pts, F), HalfAt(Pts, T));
		if (Half < 1.2) return;
		const double Wide = std::min(1.2 + Dice(Key, B, 4) * 2.0, Half * 2.0 - 0.4);
		const double Mid = std::clamp((Dice(Key, B, 5) - 0.5) * Half * 1.2, -Half + 0.2 + Wide * 0.5, Half - 0.2 - Wide * 0.5);
		M.Left = Mid + Wide * 0.5;
		M.Right = Mid - Wide * 0.5;
		M.Lift = 0.004;
		if (Dice(Key, B, 6) < 0.5) ColourMark(M, 0.013f, 0.013f, 0.015f);   // (newer: blacker than the road)
		else ColourMark(M, 0.038f, 0.037f, 0.036f);                           // (older: grey, the stone showing)
		const RoadMark Patch = M;
		if (!Put(Pc, M)) return;
		RoadMark Ring;
		Ring.Kind = MarkTar;
		Ring.Lift = 0.005;
		ColourMark(Ring, 0.006f, 0.006f, 0.007f);
		const double Band = 0.09;
		for (double Edge : {Patch.Left, Patch.Right})
		{
			Ring.From = Patch.From - Band;
			Ring.To = Patch.To + Band;
			Ring.Left = Edge + Band * 0.5;
			Ring.Right = Edge - Band * 0.5;
			Put(Pc, Ring);
		}
		for (double End : {Patch.From, Patch.To})
		{
			Ring.From = End - Band * 0.5;
			Ring.To = End + Band * 0.5;
			Ring.Left = Patch.Left + Band * 0.5;
			Ring.Right = Patch.Right - Band * 0.5;
			Put(Pc, Ring);
		}
	});
	// Old seams sealed with a long line of tar, a little off the middle, on roads wide enough for two.
	Buckets(60.0, 61.0, [&](const Piece& Pc, int B, int Key)
	{
		if (B < 0 || Dice(Key, B, 11) >= 0.3) return;
		RoadMark M;
		M.Kind = MarkTar;
		M.From = double(B) * 60.0 + Dice(Key, B, 12) * 10.0;
		M.To = M.From + 25.0 + Dice(Key, B, 13) * 25.0;
		const double F = std::clamp(AlongOf(Pc, M.From), 0.0, RoadLen);   // (where it starts, by U: the same place either way)
		if (HalfAt(Pts, F) < 2.5 || Tunnel(F)) return;
		const double At = (Dice(Key, B, 14) < 0.5 ? 1.0 : -1.0) * (0.3 + 0.3 * Dice(Key, B, 15));
		M.Left = At + 0.06;
		M.Right = At - 0.06;
		M.Lift = 0.003;
		ColourMark(M, 0.006f, 0.006f, 0.007f);
		// (none past the road's last metre)
		const double Fa = (M.From - Pc.Off) / Pc.Sign, Ta = (M.To - Pc.Off) / Pc.Sign;
		if (std::max(Fa, Ta) > RoadLen - 1.0)
		{
			const double Cut = Pc.Sign * (RoadLen - 1.0) + Pc.Off;
			if (Pc.Sign > 0.0) M.To = std::min(M.To, Cut); else M.From = std::max(M.From, Cut);
		}
		if (M.To > M.From) Put(Pc, M);
	});
	// Grit down the middle of a single-track lane, where the tyres never go, out in the country.
	// ([build 168] beenster on 167: "more sparse": on a little over half the lane's 30 m stretches, not 70%)
	Buckets(30.0, 0.0, [&](const Piece& Pc, int B, int Key)
	{
		if (B < 0 || Dice(Key, B, 21) >= 0.55) return;
		const double U0 = double(B) * 30.0;
		const double A0 = std::clamp(std::min(AlongOf(Pc, U0), AlongOf(Pc, U0 + 30.0)), 0.0, RoadLen), A1 = std::clamp(std::max(AlongOf(Pc, U0), AlongOf(Pc, U0 + 30.0)), 0.0, RoadLen);
		const double Start = std::clamp(AlongOf(Pc, U0), 0.0, RoadLen);
		if (HalfAt(Pts, A0) >= 2.0 || HalfAt(Pts, A1) >= 2.0 || Built(Start) || Tunnel(Start)) return;
		RoadMark M;
		M.Kind = MarkGrit;
		M.From = U0;
		M.To = U0 + 30.0;
		const double Wide = 0.35 + 0.25 * Dice(Key, B, 22);
		M.Left = Wide * 0.5;
		M.Right = -Wide * 0.5;
		M.Lift = 0.002;
		ColourMark(M, 0.06f, 0.062f, 0.085f);   // (WorldRide's LaneGrit: M_RideRoad's key for the lane's grit)
		// (cut to the road)
		const double Fa = AlongOf(Pc, M.From), Ta = AlongOf(Pc, M.To);
		if (Pc.Sign > 0.0) { if (Ta > RoadLen) M.To = Pc.Sign * RoadLen + Pc.Off; if (Fa < 0.0) M.From = Pc.Off; }
		else { if (Fa > RoadLen) M.From = Pc.Sign * RoadLen + Pc.Off; if (Ta < 0.0) M.To = Pc.Off; }
		if (M.To > M.From) Put(Pc, M);
	});
	// Iron covers in a town's road: the drains and the water mains, in the wheel tracks more than not.
	Buckets(22.0, 22.0, [&](const Piece& Pc, int B, int Key)
	{
		const double U0 = double(B) * 22.0 + Dice(Key, B, 31) * 15.0;
		const double A0 = AlongOf(Pc, U0);
		if (B < 0 || Dice(Key, B, 32) >= 0.55 || A0 < 0.0 || A0 > RoadLen || !Built(A0) || Tunnel(A0)) return;
		const double Half = HalfAt(Pts, A0);
		const double Size = Dice(Key, B, 33) < 0.6 ? 0.6 : 0.45;
		const double At = (Dice(Key, B, 34) < 0.5 ? 1.0 : -1.0) * std::min(Half * (0.3 + 0.35 * Dice(Key, B, 35)), Half - 0.5);
		RoadMark M;
		M.Kind = MarkCover;
		M.From = U0;
		M.To = U0 + Size;
		M.Left = At + Size * 0.5;
		M.Right = At - Size * 0.5;
		M.Lift = -0.008;
		ColourMark(M, 0.045f, 0.043f, 0.04f);
		Put(Pc, M);
	});
	// A steel joint across the road where it goes onto a bridge and off it again.
	for (size_t I = PointAt(Pts, std::max(From - 4.0, 0.0)); I + 1 < Pts.size() && Pts[I].Along <= To + 4.0; ++I)
	{
		if (Pts[I].Bridge == Pts[I + 1].Bridge) continue;
		const double At = Pts[I].Bridge ? Pts[I].Along : Pts[I + 1].Along;
		RoadMark M;
		M.Kind = MarkJoint;
		M.From = At - 0.08;
		M.To = At + 0.08;
		const double Half = HalfAt(Pts, At);
		M.Left = Half;
		M.Right = -Half;
		M.Lift = 0.006;
		ColourMark(M, 0.11f, 0.11f, 0.11f);
		if (M.To >= From && M.From <= To) Out.push_back(M);
	}
	// [OSM stage 5] What the map puts on it: give-way and stop lines, crossings, humps, passing places.
	MappedMarks(Which, From, To, Out);
	// [junction priority 168] The through roads' lines painted on across the ride's road (none over 2 m long).
	if (Main && !JunctionPaint.empty())
	{
		auto It = std::lower_bound(JunctionPaint.begin(), JunctionPaint.end(), From - 2.0, [](const RoadMark& M, double A) { return M.From < A; });
		for (; It != JunctionPaint.end() && It->From <= To; ++It)
			if (It->To >= From && !(It->EdgeLine && Built(It->From))) Out.push_back(*It);
	}
}

double RidePlan::MarkUp(int Kind)
{
	return Kind == MarkPaint ? 0.0 : Kind == MarkHump ? 0.004 : Kind == MarkBay ? 0.0 : Kind == MarkTar ? 0.009 : Kind == MarkJoint ? 0.012 : Kind == MarkCover ? 0.01
		: Kind == MarkGrit ? 0.007 : 0.006;
}

RidePlan::RideSurface RidePlan::SurfaceAt(double X, double Z) const
{
	RideSurface S;
	NearOpts Opts;
	NearHit Hit;
	if (!NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + 0.05)
	{
		if (OnPullOff(X, Z)) S.Kind = SurfGravel;   // [places stage 2]
		return S;
	}
	// [OSM stage 1] A road the map says is gravel: gravel all over (no marks, no paint on it).
	{
		const RoadFact* F = Hit.Which == 0 ? FactAt(Hit.Along) : nullptr;
		if (Hit.Which == 0 ? F != nullptr && F->Gravel : SideOf(Hit.Which) != nullptr && SideOf(Hit.Which)->Gravel)
		{
			S.Kind = SurfGravel;
			return S;
		}
	}
	// A level crossing's rails, flush with the road: steel under the tyre for a rail head's width.
	{
		double RailDist = 0.0, RailTop = 0.0;
		int RailOn = 0;
		bool Disused = false;
		if (NearRail(X, Z, RailDist, RailTop, RailOn, &Disused) && RailOn == 3 && !Disused && std::fabs(RailDist - RailGauge * 0.5) < 0.05)
		{
			S.Kind = SurfSteel;
			return S;
		}
	}
	std::vector<RoadMark> Marks;
	RoadMarks(Hit.Which, Hit.Along - 1.0, Hit.Along + 1.0, Marks);
	// (the last mark that covers the point wins, as RoadTile draws them over each other in turn)
	for (const RoadMark& M : Marks)
	{
		const double MLeft = M.LeftAt(Hit.Along), MRight = M.RightAt(Hit.Along);   // ([junction priority 168] a slanting line's here)
		if (Hit.Along < M.From || Hit.Along > M.To || Hit.Off > MLeft || Hit.Off < MRight) continue;
		S.Kind = M.Kind == MarkTar || M.Kind == MarkPaint ? SurfSlick : M.Kind == MarkGrit ? SurfGrit : M.Kind == MarkCover || M.Kind == MarkJoint ? SurfSteel : SurfTarmac;
		// (eased in over 3 cm from its edges, or a joint's hump over its width)
		const double In = std::min({Hit.Along - M.From, M.To - Hit.Along, MLeft - Hit.Off, Hit.Off - MRight});
		S.Drawn = MarkUp(M.Kind);
		S.Lift = M.Kind == MarkHump ? M.RiseAt(Hit.Along)
			: M.Kind == MarkJoint ? M.Lift * std::sin(3.141592653589793 * (Hit.Along - M.From) / (M.To - M.From)) : M.Lift * std::clamp(In / 0.03, 0.0, 1.0);
	}
	// The paint: a centre line of 3 m dashes with 6 m gaps on a road wide enough for two, edge lines on
	// one 6 m wide or more (as RoadTile paints them; slick in the wet, as the tar).
	if (S.Kind == SurfTarmac)
	{
		const std::vector<RoadPoint>& Pts = RoadOf(Hit.Which);
		const double RoadLen = Pts.empty() ? 0.0 : Pts.back().Along;
		const bool Painted = Hit.Which == 0 || (Hit.Along > 12.0 && Hit.Along < RoadLen - 12.0);
		const RoadPoint& Here = Pts[size_t(std::clamp(Hit.Index, 0, int(Pts.size()) - 1))];
		// ([road rulebook] the marks by the limit, as RoadPiece paints them; the warning line's longer marks read as the
		// centre line's here: a few metres of slick either way)
		const int Mph = PaintMph(Hit.Which, Hit.Along, false);
		// ([road rulebook stage 3a] the ride's road's dashes by its map road's own way along, RideU)
		double DashSign = 1.0;
		const double DashAt = Hit.Which == 0 ? RideDashU(Hit.Along, DashSign) : Hit.Along;
		if (Painted && RoadX::CentreLine(Here.Lanes, Here.OneWay, Hit.Half) && RoadX::OnDash(DashAt, RoadX::CentreDash(Mph)) && std::fabs(Hit.Off) < RoadX::CentreHalfWidth(Hit.Half)) S.Kind = SurfSlick;
		if (Painted && RoadX::OnDash(DashAt, RoadX::LaneDash(Mph)))
			for (const double L : RoadX::LaneLines(Here.Lanes, Here.OneWay, Hit.Half))
				if (std::fabs(Hit.Off - L) < 0.05) S.Kind = SurfSlick;
		const double EdgeAt = Hit.Half - RoadX::EdgeWorn - RoadX::EdgeLineIn - RoadX::EdgeLineHalfWidth;
		if (Painted && Hit.Half >= 2.95 && std::fabs(std::fabs(Hit.Off) - EdgeAt) < RoadX::EdgeLineHalfWidth) S.Kind = SurfSlick;
	}
	return S;
}
} // namespace TorqueWorld
