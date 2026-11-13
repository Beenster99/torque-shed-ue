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
						// A dam: a wall 5 m thick, its top level a little over the highest ground along it;
						// a weir: a step 0.6 m high and 1.5 m thick across the river.
						const bool Dam = W.Kind == 1;
						double Crest = -1e9;
						if (Dam)
							for (const P2& P : W.Line)
							{
								const double Y = Ground(P.X, P.Z);
								if (!std::isnan(Y)) Crest = std::fmax(Crest, Y);
							}
						const double Half = Dam ? 2.5 : 0.75;
						for (int K = 0; K < Bits; ++K)
						{
							const P2 Pa = A + U * (L * K / Bits), Pb = A + U * (L * (K + 1) / Bits);
							const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
							if (std::isnan(Ga) || std::isnan(Gb)) continue;
							const double Ta = Dam ? Crest + 0.5 : Ga + 0.6, Tb = Dam ? Crest + 0.5 : Gb + 0.6;
							const double Ba = std::fmin(Ga, Ta) - 2.0, Bb = std::fmin(Gb, Tb) - 2.0;
							const Paint C = Dam ? DamStone : Paint{0.16f, 0.16f, 0.15f};
							for (double Sv : {-1.0, 1.0})
								SkyQuad(Out, Pt(Pa.X + Right.X * Sv * Half, Ba, Pa.Z + Right.Z * Sv * Half), Pt(Pb.X + Right.X * Sv * Half, Bb, Pb.Z + Right.Z * Sv * Half),
									Pt(Pb.X + Right.X * Sv * Half, Tb, Pb.Z + Right.Z * Sv * Half), Pt(Pa.X + Right.X * Sv * Half, Ta, Pa.Z + Right.Z * Sv * Half), C, true);
							SkyQuad(Out, Pt(Pa.X - Right.X * Half, Ta, Pa.Z - Right.Z * Half), Pt(Pb.X - Right.X * Half, Tb, Pb.Z - Right.Z * Half),
								Pt(Pb.X + Right.X * Half, Tb, Pb.Z + Right.Z * Half), Pt(Pa.X + Right.X * Half, Ta, Pa.Z + Right.Z * Half),
								Paint{C.R * 1.15f, C.G * 1.15f, C.B * 1.15f}, true);
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
