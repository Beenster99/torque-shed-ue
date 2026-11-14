// Wild ground and water works (OSM stage 2 batch D): the map's cliffs as rock faces along their lines,
// a cairn or a trig pillar on every mapped peak, dams as walls across their valleys, weirs as steps
// across their rivers. (Scree and reservoirs are field areas: WorldRideFields.cpp and WaterTile.)
#include "WorldRide.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace TorqueWorld
{
namespace RideWildBits
{
using namespace RideShapes;
constexpr double WildCell = 256.0;

long long WildKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / WildCell)) * 4000037LL + static_cast<long long>(std::floor(Z / WildCell));
}

uint32_t WildHash(const std::string& Id, double X, double Z)
{
	uint32_t H = 2166136261u;
	for (char C : Id) H = (H ^ static_cast<unsigned char>(C)) * 16777619u;
	H ^= static_cast<uint32_t>(std::lround(X)) * 73856093u ^ static_cast<uint32_t>(std::lround(Z)) * 19349663u;
	H ^= H >> 15; H *= 2246822519u; H ^= H >> 13;
	return H == 0 ? 1u : H;
}

double Shake(uint32_t H, int Salt)
{
	uint32_t V = H ^ (static_cast<uint32_t>(Salt) * 2654435761u);
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
}

const Paint Crag = {0.2f, 0.195f, 0.175f}, DamStone = {0.24f, 0.23f, 0.21f}, TrigWhite = {0.5f, 0.5f, 0.48f}, CairnStone = {0.17f, 0.165f, 0.15f};
} // namespace RideWildBits

void RidePlan::IndexWild()
{
	using namespace RideWildBits;
	WildLines.clear();
	Summits.clear();
	if (!Scene) return;
	const Region& G = Area.Roads;
	const auto AddLine = [&](int Kind, const std::vector<P2>& Line, const std::string& Id)
	{
		if (Line.size() < 2) return;
		WildLine W;
		W.Kind = Kind;
		W.Line = Line;
		W.Id = WildHash(Id, Line[0].X, Line[0].Z);
		const int Index = int(WildLines.size());
		for (size_t K = 0; K + 1 < Line.size(); ++K)
		{
			const P2 Mid = (Line[K] + Line[K + 1]) * 0.5;
			Scene->Wild[WildKey(Mid.X, Mid.Z)].emplace_back(Index, int(K));
		}
		WildLines.push_back(std::move(W));
	};
	for (const FeatureLine& L : G.Scenery.Lines)
	{
		if (L.Kind == "cliff") AddLine(0, L.Line, L.Id);
		else if (L.Kind == "dam") AddLine(1, L.Line, L.Id);
		else if (L.Kind == "weir") AddLine(2, L.Line, L.Id);
	}
	// (a cliff mapped as an area: its outline, closed, as its line)
	for (const FeatureArea& A : G.Scenery.Areas)
		if (A.Kind == "cliff" && A.Ring.size() >= 3)
		{
			std::vector<P2> Closed = A.Ring;
			Closed.push_back(A.Ring[0]);
			AddLine(0, Closed, A.Id);
		}
	for (const FeaturePoint& P : G.Scenery.Points)
	{
		if (P.Kind != "peak") continue;
		Summit S;
		S.At = P.At;
		S.Name = P.Name;
		S.Elevation = P.Elevation;
		// (a trig pillar on the bigger, named tops now and then; a cairn on the rest)
		S.Trig = !P.Name.empty() && Shake(WildHash(P.Id, P.At.X, P.At.Z), 1) < 0.2;
		Scene->Summits[WildKey(P.At.X, P.At.Z)].push_back(int(Summits.size()));
		Summits.push_back(S);
	}
}

void RidePlan::WildTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	using namespace RideWildBits;
	if (!Scene || Detail < 0) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const int CellX0 = int(std::floor(X0 / WildCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / WildCell));
	const int CellZ0 = int(std::floor(Z0 / WildCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / WildCell));
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const double Piece = Detail >= 1 ? 6.0 : 30.0;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = Scene->Wild.find(Key);
			if (It != Scene->Wild.end())
				for (const std::pair<int, int>& Bit : It->second)
				{
					const WildLine& W = WildLines[size_t(Bit.first)];
					if (W.Kind == 2 && Detail < 1) continue;
					const P2 A = W.Line[size_t(Bit.second)], B = W.Line[size_t(Bit.second) + 1];
					if (!Inside((A + B) * 0.5)) continue;
					const double L = A.DistanceTo(B);
					if (L < 0.5) continue;
					const P2 U = (B - A) * (1.0 / L), Right(-U.Z, U.X);
					const int Bits = std::max(1, int(std::ceil(L / Piece)));
					if (W.Kind == 0)
					{
						// A cliff: from the ground at its top down to the ground below on its right, at
						// least 3 m, its face broken into slabs leaning a little out of true.
						for (int K = 0; K < Bits; ++K)
						{
							const P2 Pa = A + U * (L * K / Bits), Pb = A + U * (L * (K + 1) / Bits), Pm = (Pa + Pb) * 0.5;
							const double TopA = Ground(Pa.X - Right.X, Pa.Z - Right.Z), TopB = Ground(Pb.X - Right.X, Pb.Z - Right.Z);
							const double FootA = Ground(Pa.X + Right.X * 8.0, Pa.Z + Right.Z * 8.0), FootB = Ground(Pb.X + Right.X * 8.0, Pb.Z + Right.Z * 8.0);
							if (std::isnan(TopA) || std::isnan(TopB) || std::isnan(FootA) || std::isnan(FootB)) continue;
							if (Detail >= 1 && !OffRoad(Pm.X, Pm.Z, 1.0)) continue;
							const double LowA = std::fmin(FootA, TopA - 3.0) - 0.5, LowB = std::fmin(FootB, TopB - 3.0) - 0.5;
							const double Out1 = (Shake(W.Id, Bit.second * 64 + K) - 0.5) * 0.8;
							const float Tone = float(0.8 + 0.4 * Shake(W.Id, Bit.second * 64 + K + 7));
							const Paint C = {Crag.R * Tone, Crag.G * Tone, Crag.B * Tone};
							SkyQuad(Out, Pt(Pa.X + Right.X * 1.2, LowA, Pa.Z + Right.Z * 1.2), Pt(Pb.X + Right.X * 1.2, LowB, Pb.Z + Right.Z * 1.2),
								Pt(Pb.X + Right.X * Out1, TopB + 0.3, Pb.Z + Right.Z * Out1), Pt(Pa.X + Right.X * Out1, TopA + 0.3, Pa.Z + Right.Z * Out1), C, true);
						}
					}
					else
					{
						// [places stage 4] A dam: its crest level a little over the highest ground along it. A
						// short one (under 350 m) a masonry wall in the scanned stone, its face down the valley
						// battered (wider at the foot by a fifth of its height), a parapet each side of its top
						// and buttresses down its face; a long one an earth bank, grassed, its slopes 1 in 2.5,
						// a stone wave wall along the water's side of its crest. A weir: a step 0.6 m high and
						// 1.5 m thick across the river.
						const bool Dam = W.Kind == 1;
						double Crest = -1e9, DamLong = 0.0;
						if (Dam)
						{
							for (size_t I = 0; I < W.Line.size(); ++I)
							{
								const double Y = Ground(W.Line[I].X, W.Line[I].Z);
								if (!std::isnan(Y)) Crest = std::fmax(Crest, Y);
								if (I > 0) DamLong += W.Line[I].DistanceTo(W.Line[I - 1]);
							}
						}
						if (!Dam)
						{
							for (int K = 0; K < Bits; ++K)
							{
								const P2 Pa = A + U * (L * K / Bits), Pb = A + U * (L * (K + 1) / Bits);
								const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
								if (std::isnan(Ga) || std::isnan(Gb)) continue;
								const double Half = 0.75, Ta = Ga + 0.6, Tb = Gb + 0.6, Ba = Ga - 2.0, Bb = Gb - 2.0;
								const Paint C = {0.16f, 0.16f, 0.15f};
								for (double Sv : {-1.0, 1.0})
									SkyQuad(Out, Pt(Pa.X + Right.X * Sv * Half, Ba, Pa.Z + Right.Z * Sv * Half), Pt(Pb.X + Right.X * Sv * Half, Bb, Pb.Z + Right.Z * Sv * Half),
										Pt(Pb.X + Right.X * Sv * Half, Tb, Pb.Z + Right.Z * Sv * Half), Pt(Pa.X + Right.X * Sv * Half, Ta, Pa.Z + Right.Z * Sv * Half), C, true);
								SkyQuad(Out, Pt(Pa.X - Right.X * Half, Ta, Pa.Z - Right.Z * Half), Pt(Pb.X - Right.X * Half, Tb, Pb.Z - Right.Z * Half),
									Pt(Pb.X + Right.X * Half, Tb, Pb.Z + Right.Z * Half), Pt(Pa.X + Right.X * Half, Ta, Pa.Z + Right.Z * Half), Paint{C.R * 1.15f, C.G * 1.15f, C.B * 1.15f}, true);
							}
							continue;
						}
						// (which side is the water's: the side whose ground 40 m off is higher, or holds a reservoir)
						const P2 Mid = (A + B) * 0.5;
						const auto Wet = [this](const P2& P) { const int Fa = FieldAt(P.X, P.Z); return Fa >= 0 && FieldAreas[size_t(Fa)].Kind == FieldReservoir; };
						double Side = 1.0;   // (+1: the water to the left of A to B, -Right)
						{
							const P2 Lp = Mid - Right * 40.0, Rp = Mid + Right * 40.0;
							const bool Wl = Wet(Lp), Wr = Wet(Rp);
							if (Wl != Wr) Side = Wl ? 1.0 : -1.0;
							else
							{
								const double Gl = Ground(Lp.X, Lp.Z), Gr = Ground(Rp.X, Rp.Z);
								if (!std::isnan(Gl) && !std::isnan(Gr)) Side = Gl >= Gr ? 1.0 : -1.0;
							}
						}
						const P2 Down = Right * Side;   // (towards the valley below)
						const double Top = Crest + 0.5;
						const bool Earth = DamLong > 350.0;
						RideMesh* Mason = nullptr;
						if (!Earth)
						{
							if (!Out.Masonry)
							{
								Out.Masonry = std::make_shared<RideMesh>();
								Out.Masonry->OriginX = Out.OriginX;
								Out.Masonry->OriginZ = Out.OriginZ;
								Out.MasonryTint = LookHere(Mid.X, Mid.Z).HouseStone;
							}
							Mason = Out.Masonry.get();
						}
						const Paint Face = Earth ? Paint{0.07f, 0.09f, 0.035f} : Paint{0.46f, 1.0f, 0.2f};   // (masonry: the scan's masks)
						const Paint Cap = Earth ? Paint{0.22f, 0.21f, 0.19f} : Paint{0.52f, 1.0f, 0.15f};
						for (int K = 0; K < Bits; ++K)
						{
							const P2 Pa = A + U * (L * K / Bits), Pb = A + U * (L * (K + 1) / Bits);
							const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
							if (std::isnan(Ga) || std::isnan(Gb)) continue;
							const double Ha = std::fmax(0.0, Top - Ga), Hb = std::fmax(0.0, Top - Gb);
							// (the crest 5 m wide; the downstream face's foot out by a fifth of the height, or 2.5 times it for a bank)
							const double Crest2 = Earth ? 6.0 : 4.5, Batter = Earth ? 2.5 : 0.2;
							const P2 Ua = Pa - Down * (Crest2 * 0.5), Ub = Pb - Down * (Crest2 * 0.5);   // (the water's edge of the crest)
							const P2 Da = Pa + Down * (Crest2 * 0.5), Db = Pb + Down * (Crest2 * 0.5);
							const P2 Fa = Da + Down * (Ha * Batter), Fb = Db + Down * (Hb * Batter);
							RideMesh& M = Earth ? Out : *Mason;
							// crest
							SkyQuad(M, Pt(Ua.X, Top, Ua.Z), Pt(Ub.X, Top, Ub.Z), Pt(Db.X, Top, Db.Z), Pt(Da.X, Top, Da.Z), Cap, true);
							// the face down the valley, to below the ground at its foot
							const double Fya = Ground(Fa.X, Fa.Z), Fyb = Ground(Fb.X, Fb.Z);
							SkyQuad(M, Pt(Da.X, Top, Da.Z), Pt(Db.X, Top, Db.Z), Pt(Fb.X, (std::isnan(Fyb) ? Gb : Fyb) - 1.0, Fb.Z), Pt(Fa.X, (std::isnan(Fya) ? Ga : Fya) - 1.0, Fa.Z), Face, true);
							// the water's face (into the reservoir, under its level)
							const P2 Wa = Ua - Down * (Earth ? Ha * 2.5 : 0.3), Wb = Ub - Down * (Earth ? Hb * 2.5 : 0.3);
							SkyQuad(M, Pt(Ua.X, Top, Ua.Z), Pt(Ub.X, Top, Ub.Z), Pt(Wb.X, Gb - 2.0, Wb.Z), Pt(Wa.X, Ga - 2.0, Wa.Z), Face, true);
							// parapets (a masonry dam: both sides; a bank: the wave wall on the water's side)
							for (double Edge : {-1.0, 1.0})
							{
								if (Earth && Edge > 0.0) continue;
								const P2 Ea = Pa + Down * (Edge * (Crest2 * 0.5 - 0.3)), Eb = Pb + Down * (Edge * (Crest2 * 0.5 - 0.3));
								const double Ph = Earth ? 1.2 : 1.1;
								for (double Th : {-0.3, 0.3})
									SkyQuad(M, Pt(Ea.X + Down.X * Th, Top, Ea.Z + Down.Z * Th), Pt(Eb.X + Down.X * Th, Top, Eb.Z + Down.Z * Th), Pt(Eb.X + Down.X * Th, Top + Ph, Eb.Z + Down.Z * Th),
										Pt(Ea.X + Down.X * Th, Top + Ph, Ea.Z + Down.Z * Th), Cap, true);
								SkyQuad(M, Pt(Ea.X - Down.X * 0.3, Top + Ph, Ea.Z - Down.Z * 0.3), Pt(Eb.X - Down.X * 0.3, Top + Ph, Eb.Z - Down.Z * 0.3), Pt(Eb.X + Down.X * 0.3, Top + Ph, Eb.Z + Down.Z * 0.3),
									Pt(Ea.X + Down.X * 0.3, Top + Ph, Ea.Z + Down.Z * 0.3), Cap, true);
							}
							// buttresses down a masonry dam's face, every 18 m
							if (!Earth && Detail >= 1 && Ha > 6.0)
							{
								const double At0 = L * K / Bits, At1 = L * (K + 1) / Bits;
								for (double Bt = std::ceil(At0 / 18.0) * 18.0; Bt < At1; Bt += 18.0)
								{
									const P2 Bp = A + U * Bt + Down * (Crest2 * 0.5);
									const double H = Ha + (Hb - Ha) * (Bt - At0) / std::fmax(1e-6, At1 - At0);
									const P2 Fp = Bp + Down * (H * (Batter + 0.12));
									const double Fy = Ground(Fp.X, Fp.Z);
									for (double Sd : {-1.0, 1.0})
										SkyQuad(M, Pt(Bp.X + U.X * Sd * 1.0, Top, Bp.Z + U.Z * Sd * 1.0), Pt(Fp.X + U.X * Sd * 1.0, (std::isnan(Fy) ? Top - H : Fy) - 0.5, Fp.Z + U.Z * Sd * 1.0),
											Pt(Fp.X + U.X * Sd * 1.0 - Down.X * 2.0, (std::isnan(Fy) ? Top - H : Fy) - 0.5, Fp.Z + U.Z * Sd * 1.0 - Down.Z * 2.0), Pt(Bp.X + U.X * Sd * 1.0 - Down.X * 0.1, Top, Bp.Z + U.Z * Sd * 1.0 - Down.Z * 0.1), Face, true);
									SkyQuad(M, Pt(Bp.X - U.X, Top, Bp.Z - U.Z), Pt(Bp.X + U.X, Top, Bp.Z + U.Z), Pt(Fp.X + U.X, (std::isnan(Fy) ? Top - H : Fy) - 0.5, Fp.Z + U.Z),
										Pt(Fp.X - U.X, (std::isnan(Fy) ? Top - H : Fy) - 0.5, Fp.Z - U.Z), Face, true);
								}
							}
						}
					}
				}
			if (Detail < 1) continue;
			const auto St = Scene->Summits.find(Key);
			if (St == Scene->Summits.end()) continue;
			for (int I : St->second)
			{
				const Summit& S = Summits[size_t(I)];
				if (!Inside(S.At)) continue;
				const double Y = Ground(S.At.X, S.At.Z);
				if (std::isnan(Y)) continue;
				if (S.Trig)
				{
					// (an Ordnance Survey pillar: white concrete, tapering, 1.2 m, a plate on top)
					Column(Out, Pt(S.At.X, Y - 0.2, S.At.Z), 0.42, 0.3, 1.4, 4, TrigWhite);
					Column(Out, Pt(S.At.X, Y + 1.2, S.At.Z), 0.12, 0.12, 0.04, 8, Paint{0.15f, 0.15f, 0.15f});
				}
				else
				{
					// (a cairn: a heap of stones, about 1.5 m)
					Column(Out, Pt(S.At.X, Y - 0.2, S.At.Z), 1.2, 0.45, 1.3, 7, CairnStone, 0.35);
					Column(Out, Pt(S.At.X + 0.3, Y + 1.0, S.At.Z - 0.2), 0.25, 0.18, 0.35, 5, Paint{CairnStone.R * 1.2f, CairnStone.G * 1.2f, CairnStone.B * 1.2f}, 0.1);
				}
			}
		}
}
} // namespace TorqueWorld
