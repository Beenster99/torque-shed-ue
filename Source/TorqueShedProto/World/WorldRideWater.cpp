// Rivers, streams, canals and lakes (beenster, build 147, Whaley Bridge: "we need flat water surfaces and
// well formed banks for rivers, stream, brooks, lakes and the coastal shoreline").
//
// Each of the map's watercourses becomes a run of points down it, each with its water's level (level
// across, never rising as it goes downstream), its half width and its depth. Ground cuts a channel under
// that level and banks up from it to the land round it; WaterTile lays the water flat across at the level.
// A lake's bed lies under its own level, and the sea's under the sea's.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace
{
long long WaterKey(double X, double Z, double Cell)
{
	return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell));
}

constexpr double StillCellSize = 64.0;   // (as the fields' own squares)
} // namespace

void RidePlan::IndexWater()
{
	WaterRuns.clear();
	WaterNear.clear();
	StillCells.clear();
	if (!Land) return;
	const Region& G = Area.Roads;
	// The lakes' squares (not the rivers' own outlines: those runs are their water).
	for (const FieldArea& F : FieldAreas)
	{
		if (F.Kind != FieldReservoir || F.Flowing) continue;
		for (double Z = std::floor(F.MinZ / StillCellSize) * StillCellSize; Z <= F.MaxZ; Z += StillCellSize)
			for (double X = std::floor(F.MinX / StillCellSize) * StillCellSize; X <= F.MaxX; X += StillCellSize) StillCells[WaterKey(X + 1.0, Z + 1.0, StillCellSize)] = 1;
	}
	// (whether a point is water by the land's cover or inside a river's own outline: what gives a river its width)
	// (the map's outline where it draws one, which follows the banks; else the land's cover, 10 m
	// squares from space, a little generous: taken in by a square's half)
	const auto InOutline = [&](double X, double Z)
	{
		const int Fa = FieldAt(X, Z);
		return Fa >= 0 && FieldAreas[size_t(Fa)].Kind == FieldReservoir && FieldAreas[size_t(Fa)].Flowing;
	};
	bool Outlined = false;
	const auto WetAt = [&](double X, double Z) { return Outlined ? InOutline(X, Z) : Land->Cover(X, Z) == Terrain::CoverWater; };
	for (const Stream& Flow : G.Streams)
	{
		if (Flow.Line.size() < 2) continue;
		// A point every WaterStep metres down it.
		WaterRun Run;
		Run.Kind = Flow.Kind;
		double Carry = 0.0;
		for (size_t I = 0; I + 1 < Flow.Line.size(); ++I)
		{
			const double Len = Flow.Line[I].DistanceTo(Flow.Line[I + 1]);
			if (Len < 1e-6) continue;
			double S = Carry;
			for (; S < Len; S += WaterStep) Run.At.push_back(Flow.Line[I] + (Flow.Line[I + 1] - Flow.Line[I]) * (S / Len));
			Carry = S - Len;
		}
		if (Run.At.empty() || Run.At.back().DistanceTo(Flow.Line.back()) > 0.5) Run.At.push_back(Flow.Line.back());
		if (Run.At.size() < 2) continue;
		// ([build 154] beenster: "too many long water objects for streams and rivers, with very jagged corners":
		// the map's line turns sharply at each of its points, and the water ran straight between them. Now its
		// points every WaterStep m are eased round each turn, over about 10 m either side, its ends kept.)
		if (Run.At.size() > 4)
		{
			static constexpr double Weights[4] = {1.0, 0.8, 0.5, 0.2};
			for (int Pass = 0; Pass < 2; ++Pass)
			{
				const std::vector<P2> Was = Run.At;
				const size_t Count = Was.size();
				for (size_t I = 1; I + 1 < Count; ++I)
				{
					// (as far either side as the line reaches both ways, so the ends stay put)
					const size_t Reach = std::min<size_t>({3, I, Count - 1 - I});
					P2 Sum(0.0, 0.0);
					double Total = 0.0;
					for (size_t D = 0; D <= Reach; ++D)
					{
						const double W = Weights[D];
						Sum = Sum + Was[I - D] * W;
						Total += W;
						if (D == 0) continue;
						Sum = Sum + Was[I + D] * W;
						Total += W;
					}
					Run.At[I] = Sum * (1.0 / Total);
				}
			}
		}
		const size_t N = Run.At.size();
		// (downhill: the map draws a watercourse the way it flows, but not always)
		{
			const double Y0 = Land->Height(Run.At.front().X, Run.At.front().Z), Y1 = Land->Height(Run.At.back().X, Run.At.back().Z);
			if (!std::isnan(Y0) && !std::isnan(Y1) && Y1 > Y0 + 1.0) std::reverse(Run.At.begin(), Run.At.end());
		}
		const double Base = Flow.Kind == 0 ? 3.5 : Flow.Kind == 2 ? 3.0 : 1.1;
		const double Deep = Flow.Kind == 0 ? 1.1 : Flow.Kind == 2 ? 1.2 : 0.4;
		std::vector<double> Half(N, Base), Shift(N, 0.0), Low(N, std::nan(""));
		Outlined = false;
		for (size_t I = 0; I < N && !Outlined; I += 4) Outlined = InOutline(Run.At[I].X, Run.At[I].Z);
		const auto LeftAt = [&Run, N](size_t I)
		{
			const P2 D = Run.At[std::min(N - 1, I + 1)] - Run.At[I > 0 ? I - 1 : 0];
			const double L = D.Length();
			return L > 1e-9 ? P2(D.Z / L, -D.X / L) : P2(1.0, 0.0);
		};
		for (size_t I = 0; I < N; ++I)
		{
			const P2 L = LeftAt(I);
			// A river's (or a canal's) width: the water either side of its line, as far as it goes on
			// unbroken (40 m at most), its middle where that water's middle is.
			if (Flow.Kind != 1 && WetAt(Run.At[I].X, Run.At[I].Z))
			{
				double Lw = 0.0, Rw = 0.0;
				while (Lw < 40.0 && WetAt(Run.At[I].X + L.X * (Lw + 1.5), Run.At[I].Z + L.Z * (Lw + 1.5))) Lw += 1.5;
				while (Rw < 40.0 && WetAt(Run.At[I].X - L.X * (Rw + 1.5), Run.At[I].Z - L.Z * (Rw + 1.5))) Rw += 1.5;
				const double Trim = Outlined ? 0.0 : 5.0;
				if (Lw + Rw - Trim * 2.0 > Base * 2.0)
				{
					Half[I] = std::fmin(Outlined ? 40.0 : 15.0, (Lw + Rw) * 0.5 + 0.75 - Trim);
					Shift[I] = (Lw - Rw) * 0.5;
				}
			}
		}
		// (eased along: no sudden widening, its middle swinging gently)
		{
			const std::vector<double> H0 = Half, S0 = Shift;
			for (size_t I = 0; I < N; ++I)
			{
				double Sh = 0.0, Ss = 0.0;
				int Count = 0;
				for (size_t J = I > 5 ? I - 5 : 0; J <= std::min(N - 1, I + 5); ++J) { Sh += H0[J]; Ss += S0[J]; ++Count; }
				Half[I] = std::fmax(Base, Sh / Count);
				Shift[I] = Ss / Count;
			}
		}
		for (size_t I = 0; I < N; ++I) Run.At[I] = Run.At[I] + LeftAt(I) * Shift[I];
		// Its bed's lie: the lowest of the land across it.
		for (size_t I = 0; I < N; ++I)
		{
			const P2 L = LeftAt(I);
			double Least = 1e9;
			for (double U : {-1.0, -0.5, 0.0, 0.5, 1.0})
			{
				const double Y = Land->Height(Run.At[I].X + L.X * U * Half[I], Run.At[I].Z + L.Z * U * Half[I]);
				if (!std::isnan(Y)) Least = std::fmin(Least, Y);
			}
			if (Least < 1e8) Low[I] = Least;
		}
		// (a point with no ground: as its neighbour)
		for (size_t I = 1; I < N; ++I) if (std::isnan(Low[I])) Low[I] = Low[I - 1];
		for (size_t I = N - 1; I-- > 0;) if (std::isnan(Low[I])) Low[I] = Low[I + 1];
		if (std::isnan(Low[0])) continue;
		// Its water: never higher than upstream, eased, never higher again; and never so far under the
		// land there that it runs in a trench (a hump in the heights: a weir's pool instead).
		std::vector<double> Level = Low;
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			for (size_t I = 1; I < N; ++I) Level[I] = std::fmin(Level[I], Level[I - 1]);
			const std::vector<double> Was = Level;
			for (size_t I = 0; I < N; ++I)
			{
				double Sum = 0.0;
				int Count = 0;
				for (size_t J = I > 4 ? I - 4 : 0; J <= std::min(N - 1, I + 4); ++J) { Sum += Was[J]; ++Count; }
				Level[I] = Sum / Count;
			}
		}
		for (size_t I = 1; I < N; ++I) Level[I] = std::fmin(Level[I], Level[I - 1]);
		const double Under = Flow.Kind == 1 ? 0.12 : 0.25;
		Run.Surface.resize(N);
		Run.Half.resize(N);
		Run.Depth.resize(N);
		for (size_t I = 0; I < N; ++I)
		{
			Run.Surface[I] = float(std::fmax(Level[I], Low[I] - 2.0) - Under);
			Run.Half[I] = float(Half[I]);
			Run.Depth[I] = float(std::fmin(Deep + Half[I] * 0.04, 2.5));
		}
		// Filed by every WaterCell square its banks reach.
		const int Index = int(WaterRuns.size());
		for (size_t I = 0; I + 1 < N; ++I)
		{
			const P2 A = Run.At[I], B = Run.At[I + 1];
			const double Reach = std::fmax(Run.Half[I], Run.Half[I + 1]) + BankReach;
			const int X0 = int(std::floor((std::fmin(A.X, B.X) - Reach) / WaterCell)), X1 = int(std::floor((std::fmax(A.X, B.X) + Reach) / WaterCell));
			const int Z0 = int(std::floor((std::fmin(A.Z, B.Z) - Reach) / WaterCell)), Z1 = int(std::floor((std::fmax(A.Z, B.Z) + Reach) / WaterCell));
			for (int Cz = Z0; Cz <= Z1; ++Cz)
				for (int Cx = X0; Cx <= X1; ++Cx) WaterNear[static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz)].emplace_back(Index, int(I));
		}
		WaterRuns.push_back(std::move(Run));
	}
}

bool RidePlan::NearWater(double X, double Z, double& Dist, double& Surface, double& Half, double& Depth, int* Kind, bool* Beside) const
{
	if (WaterNear.empty()) return false;
	const auto It = WaterNear.find(WaterKey(X, Z, WaterCell));
	if (It == WaterNear.end()) return false;
	const P2 Here(X, Z);
	double Best = 1e9;   // (by how far past its bank's foot: the nearest run that reaches here)
	bool Found = false;
	for (const std::pair<int, int>& Bit : It->second)
	{
		const WaterRun& Run = WaterRuns[size_t(Bit.first)];
		const size_t I = size_t(Bit.second);
		const P2 A = Run.At[I], B = Run.At[I + 1];
		const P2 D = B - A;
		const double L2 = D.X * D.X + D.Z * D.Z;
		const double Along = L2 > 1e-12 ? ((X - A.X) * D.X + (Z - A.Z) * D.Z) / L2 : 0.0;
		const double T = std::clamp(Along, 0.0, 1.0);
		const double Dd = Here.DistanceTo(A + D * T);
		const double Hh = Run.Half[I] + (Run.Half[I + 1] - Run.Half[I]) * T;
		const double Past = Dd - Hh;
		if (Past > BankReach || Past >= Best) continue;
		Best = Past;
		Found = true;
		Dist = Dd;
		Half = Hh;
		Surface = Run.Surface[I] + (Run.Surface[I + 1] - Run.Surface[I]) * T;
		Depth = Run.Depth[I] + (Run.Depth[I + 1] - Run.Depth[I]) * T;
		if (Kind) *Kind = Run.Kind;
		if (Beside) *Beside = !((I == 0 && Along < 0.0) || (I + 2 == Run.At.size() && Along > 1.0));
	}
	return Found;
}

double RidePlan::StillLevel(double X, double Z) const
{
	if (StillCells.empty() || StillCells.find(WaterKey(X, Z, StillCellSize)) == StillCells.end()) return std::nan("");
	const int Fa = FieldAt(X, Z);
	if (Fa < 0) return std::nan("");
	const FieldArea& F = FieldAreas[size_t(Fa)];
	return F.Kind == FieldReservoir && !F.Flowing ? F.Level : std::nan("");
}
} // namespace TorqueWorld
