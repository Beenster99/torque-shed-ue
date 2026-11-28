// Rivers, streams, canals and lakes (beenster, build 147, Whaley Bridge: "we need flat water surfaces and
// well formed banks for rivers, stream, brooks, lakes and the coastal shoreline").
//
// Each of the map's watercourses becomes a run of points down it, each with its water's level (level
// across, never rising as it goes downstream), its half width and its depth. Ground cuts a channel under
// that level and banks up from it to the land round it; WaterTile lays the water flat across at the level.
// A lake's bed lies under its own level, and the sea's under the sea's.
#include "WorldRide.h"

#include <unordered_map>
#include <unordered_set>

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
	WaterHouses.clear();
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
	// ([real water, beenster on 171 at Bank Street, Blairgowrie: "odd"] the Ericht's outline (or the land's
	// cover) reached over the mill buildings on its bank, so the river was drawn 42 m wide with its bed cut
	// under them and they stood in it to their doors. A building is never water: its walls end a river's
	// width. The buildings round each 64 m square are fetched once.)
	std::unordered_map<long long, std::vector<Building>> HousesBy;
	std::unordered_set<long long> Filed;
	const auto HousesNear = [&](double X, double Z) -> const std::vector<Building>&
	{
		const long long Key = WaterKey(X, Z, StillCellSize);
		auto It = HousesBy.find(Key);
		if (It == HousesBy.end())
		{
			std::vector<Building> Got;
			HouseFootBoxes(std::floor(X / StillCellSize) * StillCellSize, std::floor(Z / StillCellSize) * StillCellSize, StillCellSize, Got);
			It = HousesBy.emplace(Key, std::move(Got)).first;
		}
		return It->second;
	};
	const auto Housed = [&](double X, double Z)
	{
		for (const Building& B : HousesNear(X, Z))
		{
			const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw), Dx = X - B.X, Dz = Z - B.Z;
			if (std::fabs(Dx * Cs - Dz * Sn) < B.W * 0.5 + 1.0 && std::fabs(Dx * Sn + Dz * Cs) < B.D * 0.5 + 1.0) return true;
		}
		return false;
	};
	const auto WetAt = [&](double X, double Z) { return (Outlined ? InOutline(X, Z) : Land->Cover(X, Z) == Terrain::CoverWater) && !Housed(X, Z); };
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
		std::vector<double> Half(N, Base), Shift(N, 0.0), Low(N, std::nan("")), LeftRoom(N, 1e9), RightRoom(N, 1e9);
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
				// (a building where it stopped: the water's edge may not pass it when the width is eased)
				if (Lw < 40.0 && Housed(Run.At[I].X + L.X * (Lw + 1.5), Run.At[I].Z + L.Z * (Lw + 1.5))) LeftRoom[I] = Lw;
				if (Rw < 40.0 && Housed(Run.At[I].X - L.X * (Rw + 1.5), Run.At[I].Z - L.Z * (Rw + 1.5))) RightRoom[I] = Rw;
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
		// (and never over a building beside it, however wide the water either side)
		for (size_t I = 0; I < N; ++I)
		{
			if (LeftRoom[I] < 1e8) Half[I] = std::fmin(Half[I], std::fmax(1.0, LeftRoom[I] - Shift[I]));
			if (RightRoom[I] < 1e8) Half[I] = std::fmin(Half[I], std::fmax(1.0, RightRoom[I] + Shift[I]));
		}
		for (size_t I = 0; I < N; ++I) Run.At[I] = Run.At[I] + LeftAt(I) * Shift[I];
		// Its bed's lie: the lowest of the land across it.
		// ([170, beenster: the planner took far longer on "Laying the roads that meet it" in 169] this ran over
		// every stream of the ride's area, about a million points round Kirkstone, and 169's look out past the
		// banks made it nine heights a point, most of the plan's time. Now the land is read at every other
		// point (and the last), five heights there: across the water at its banks and middle, and out past its
		// banks; a point between takes the lower of its two neighbours, 3 m either side, so the water still
		// never stands over the land.)
		for (size_t I = 0; I < N; ++I)
		{
			if (I % 2 == 1 && I + 1 < N) continue;
			const P2 L = LeftAt(I);
			double Least = 1e9;
			for (double U : {-1.0, 0.0, 1.0})
			{
				const double Y = Land->Height(Run.At[I].X + L.X * U * Half[I], Run.At[I].Z + L.Z * U * Half[I]);
				if (!std::isnan(Y)) Least = std::fmin(Least, Y);
			}
			// ([real water, beenster on 168 at Totscore, Skye: "a stream hanging in the air"] and out past its
			// banks, BedReach either side: a stream mapped across a hillside, where the heights have no gully
			// for it, lay level with the slope's middle, the land falling away below its lower bank, so the
			// water stood out over the hill as a shelf. Now it lies at the lowest of the land round it and
			// cuts its own channel into the slope, its lower bank as high as its water.)
			for (double U : {-1.0, 1.0})
			{
				const double Across = U * (Half[I] + BedReach);
				const double Y = Land->Height(Run.At[I].X + L.X * Across, Run.At[I].Z + L.Z * Across);
				if (!std::isnan(Y)) Least = std::fmin(Least, Y);
			}
			if (Least < 1e8) Low[I] = Least;
		}
		for (size_t I = 1; I + 1 < N; I += 2)
		{
			const double A = Low[I - 1], B = Low[I + 1];
			Low[I] = std::isnan(A) ? B : std::isnan(B) ? A : std::fmin(A, B);
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
		// ([real water, beenster on 166: "for streams it floated above the ground"] the easing spreads a fall
		// over the points round it, which lifted the water below a step up over its own banks, up to a metre
		// at Gayle's falls, the land beside it lower than the stream: now never above the lowest of the land
		// across it, so a beck drops down its steps as they come, white where it falls)
		for (size_t I = 0; I < N; ++I) Level[I] = std::fmin(Level[I], Low[I]);
		for (size_t I = 1; I < N; ++I) Level[I] = std::fmin(Level[I], Level[I - 1]);
		// ([real water, beenster on 170 at Aysgarth: "is this realistic water?"] the Ure lay 25 cm under the
		// lowest land round it, level with its grassy banks as a flood is. A river runs between banks: now a
		// river sits half a metre under its banks, more the wider it is (up to 0.9 m), a canal 0.35 m, a brook
		// 0.2 m, and the channel's bank (GroundUncut) rises from the water's edge to the land at that height.)
		const double Under = Flow.Kind == 1 ? 0.2 : Flow.Kind == 2 ? 0.35 : 0.5;
		Run.Surface.resize(N);
		Run.Half.resize(N);
		Run.Depth.resize(N);
		Run.Along.resize(N);
		for (size_t I = 0; I < N; ++I)
		{
			const double Sunk = Flow.Kind == 0 ? std::fmin(Under + Half[I] * 0.03, 0.9) : Under;
			Run.Surface[I] = float(std::fmax(Level[I], Low[I] - 2.0) - Sunk);
			Run.Half[I] = float(Half[I]);
			Run.Depth[I] = float(std::fmin(Deep + Half[I] * 0.04, 2.5));
			// (wrapped at whole 80 m, so the ripples meet, and short enough for the UVs' precision)
			Run.Along[I] = I == 0 ? 0.0f : float(std::fmod(double(Run.Along[I - 1]) + Run.At[I].DistanceTo(Run.At[I - 1]), 4000.0));
		}
		// (a river sunk by its width: still never higher than upstream where it narrows)
		for (size_t I = 1; I < N; ++I) Run.Surface[I] = std::fmin(Run.Surface[I], Run.Surface[I - 1]);
		// Filed by every WaterCell square its banks reach.
		const int Index = int(WaterRuns.size());
		for (size_t I = 0; I + 1 < N; ++I)
		{
			const P2 A = Run.At[I], B = Run.At[I + 1];
			const double Reach = std::fmax(Run.Half[I], Run.Half[I + 1]) + BankReach;
			const int X0 = int(std::floor((std::fmin(A.X, B.X) - Reach) / WaterCell)), X1 = int(std::floor((std::fmax(A.X, B.X) + Reach) / WaterCell));
			const int Z0 = int(std::floor((std::fmin(A.Z, B.Z) - Reach) / WaterCell)), Z1 = int(std::floor((std::fmax(A.Z, B.Z) + Reach) / WaterCell));
			for (int Cz = Z0; Cz <= Z1; ++Cz)
				for (int Cx = X0; Cx <= X1; ++Cx)
				{
					const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
					WaterNear[Key].emplace_back(Index, int(I));
					// (the buildings over a river's or canal's reach of this square, filed once)
					if (Run.Kind != 1 && Filed.insert(Key).second)
					{
						const double Cx0 = Cx * WaterCell, Cz0 = Cz * WaterCell;
						if (const std::vector<Building>& Got = HousesNear(Cx0 + WaterCell * 0.5, Cz0 + WaterCell * 0.5); !Got.empty())
							for (const Building& House : Got)
							{
								const double R = 0.5 * std::hypot(House.W, House.D) + 1.0;
								if (House.X + R < Cx0 || House.X - R > Cx0 + WaterCell || House.Z + R < Cz0 || House.Z - R > Cz0 + WaterCell) continue;
								WaterHouses[Key].push_back(House);
							}
					}
				}
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

bool RidePlan::WaterHouseAt(double X, double Z) const
{
	if (WaterHouses.empty()) return false;
	const auto It = WaterHouses.find(WaterKey(X, Z, WaterCell));
	if (It == WaterHouses.end()) return false;
	for (const Building& B : It->second)
	{
		const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw), Dx = X - B.X, Dz = Z - B.Z;
		if (std::fabs(Dx * Cs - Dz * Sn) < B.W * 0.5 + 1.0 && std::fabs(Dx * Sn + Dz * Cs) < B.D * 0.5 + 1.0) return true;
	}
	return false;
}

double RidePlan::StillLevel(double X, double Z) const
{
	if (StillCells.empty() || StillCells.find(WaterKey(X, Z, StillCellSize)) == StillCells.end()) return std::nan("");
	const int Fa = FieldAt(X, Z);
	if (Fa < 0) return std::nan("");
	const FieldArea& F = FieldAreas[size_t(Fa)];
	return F.Kind == FieldReservoir && !F.Flowing ? F.Level : std::nan("");
}

bool RidePlan::InStillWater(double X, double Z) const
{
	// (by the lakes' outlines themselves, not StillLevel's squares: the churchyards and places ask before IndexWater files them)
	const int Fa = FieldAt(X, Z);
	return Fa >= 0 && FieldAreas[size_t(Fa)].Kind == FieldReservoir && !FieldAreas[size_t(Fa)].Flowing;
}

bool RidePlan::Drowned(const Building& B) const
{
	const auto Wet = [this](double X, double Z) { return InStillWater(X, Z); };
	if (FieldAreas.empty() || !Wet(B.X, B.Z)) return false;
	const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
	int Corners = 0;
	for (int K = 0; K < 4; ++K)
	{
		// (each corner a little in, so a wall on the very shore line counts as on the shore)
		const double U = (K & 1 ? 0.5 : -0.5) * B.W * 0.85, V = (K & 2 ? 0.5 : -0.5) * B.D * 0.85;
		Corners += Wet(B.X + U * Cs + V * Sn, B.Z - U * Sn + V * Cs) ? 1 : 0;
	}
	return Corners >= 3;
}
} // namespace TorqueWorld
