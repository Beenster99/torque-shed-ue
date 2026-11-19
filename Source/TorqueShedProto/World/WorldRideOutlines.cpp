// [towns stage 3] Every building with its true outline and height (see RidePlan::OutlinesIn): the
// cells' buildings zips read a tile at a time, each outline cut into the boxes the buildings are drawn
// with (WorldRideBuildings.cpp).
#include "WorldRide.h"
#include "CellStore.h"
#include "WorldZip.h"

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace TorqueWorld
{
struct RidePlan::OutlineStore
{
	struct CellZip
	{
		int State = 0;   // 0 not looked at, 1 here, 2 none
		std::string Zip;
		std::vector<ZipEntry> Entries;
		std::vector<std::string> Words;
		int Tiles = 8;
		std::string Release;
		// [LiDAR stage 2] By tile ("TX_TZ"), then row: the row's first latitude (micro-degrees, mod 10000, so a
		// row that has moved is not given another's height), its eaves and its ridge (metres).
		LidarTiles Lidar;
	};
	std::mutex Lock;
	std::map<std::string, CellZip> Cells;
	std::map<std::string, std::shared_ptr<const std::vector<RidePlan::OutlinePiece>>> TilesRead;   // "ROW_COL/TX_TZ"
	std::vector<std::string> Notes;   // what each cell's first read came to, for the log (TakeOutlineNotes)
};

std::vector<std::string> RidePlan::TakeOutlineNotes() const
{
	std::vector<std::string> Out;
	if (!Outlines) return Out;
	std::lock_guard<std::mutex> Lock(Outlines->Lock);
	Out.swap(Outlines->Notes);
	return Out;
}

size_t RidePlan::ReadLidar(const std::string& Text, const std::string& Release, LidarTiles& Out)
{
	TorqueFit::Json J;
	if (!TorqueFit::Json::Parse(Text, J) || !J.IsObject() || J.Str("release") != Release) return 0;
	const TorqueFit::Json* Tiles = J.Get("tiles");
	if (Tiles == nullptr) return 0;
	size_t Rows = 0;
	for (const auto& [TileName, Flat] : Tiles->O)
	{
		auto& Into = Out[TileName];
		for (size_t K = 0; K + 3 < Flat.A.size(); K += 4)
		{
			const float Eaves = float(Flat.A[K + 2].N * 0.1), Ridge = float(Flat.A[K + 3].N * 0.1);
			if (!(Eaves >= 2.0f && Ridge >= Eaves && Ridge < 400.0f)) continue;
			Into[int(Flat.A[K].N)] = {float(Flat.A[K + 1].N), Eaves, Ridge};
			++Rows;
		}
	}
	return Rows;
}

bool RidePlan::LidarOfRow(const LidarTiles& Tiles, const std::string& Tile, int Row, long long FirstLat, float& Eaves, float& Ridge)
{
	const auto T = Tiles.find(Tile);
	if (T == Tiles.end()) return false;
	const auto R = T->second.find(Row);
	if (R == T->second.end() || int(R->second[0]) != int(FirstLat % 10000)) return false;
	Eaves = R->second[1];
	Ridge = R->second[2];
	return true;
}

std::shared_ptr<RidePlan::OutlineStore> RidePlan::NewOutlineStore()
{
	return std::make_shared<OutlineStore>();
}

int RidePlan::KindOfClass(const std::string& Class, double Area)
{
	// (land_bake.py's building_kind: house, shed, barn, shop, block, industrial, worship, civic)
	static const char* Worship[] = {"temple", "church", "cathedral", "chapel", "mosque", "synagogue", "shrine", "religious"};
	static const char* Block[] = {"apartments", "dormitory", "terrace"};
	static const char* Works[] = {"warehouse", "industrial", "factory", "storage_tank", "service", "manufacture"};
	static const char* Barn[] = {"barn", "farm_auxiliary", "agricultural", "greenhouse", "cowshed", "stable", "sty", "farm"};
	static const char* Shed[] = {"garage", "garages", "carport", "shelter", "roof", "shed", "hut", "outbuilding", "allotment_house"};
	static const char* Shop[] = {"retail", "commercial", "kiosk", "supermarket", "office", "hotel"};
	static const char* Civic[] = {"civic", "public", "school", "college", "university", "hospital", "government", "train_station", "fire_station",
		"police", "library", "museum", "townhall", "community_centre", "sports_hall", "kindergarten"};
	const auto In = [&Class](const char* const* List, size_t N) { return std::find_if(List, List + N, [&Class](const char* W) { return Class == W; }) != List + N; };
	if (In(Worship, std::size(Worship))) return 6;
	if (In(Block, std::size(Block))) return Class == "terrace" ? 0 : 4;   // (a terrace is houses in a row: the drawer makes the row)
	if (In(Works, std::size(Works))) return 5;
	if (In(Barn, std::size(Barn))) return 2;
	if (In(Shed, std::size(Shed))) return 1;
	if (In(Shop, std::size(Shop))) return 3;
	if (In(Civic, std::size(Civic))) return 7;
	if (Class == "residential" && Area > 600.0) return 4;
	return 0;
}

std::vector<Building> RidePlan::PiecesOf(const std::vector<P2>& Ring, double Height, int Kind)
{
	std::vector<Building> Out;
	const size_t N = Ring.size();
	if (N < 3) return Out;
	double Area = 0.0;
	for (size_t I = 0; I < N; ++I) Area += Ring[I].X * Ring[(I + 1) % N].Z - Ring[(I + 1) % N].X * Ring[I].Z;
	Area = std::fabs(Area) * 0.5;
	// The smallest box round it, its sides along one of the outline's own.
	double Best = 1e18, Ux = 1.0, Uz = 0.0;
	for (size_t I = 0; I < N; ++I)
	{
		const P2 D = Ring[(I + 1) % N] - Ring[I];
		const double L = D.Length();
		if (L < 0.5) continue;
		const double Ax = D.X / L, Az = D.Z / L;
		double U0 = 1e18, U1 = -1e18, V0 = 1e18, V1 = -1e18;
		for (const P2& P : Ring)
		{
			const double U = P.X * Ax + P.Z * Az, V = -P.X * Az + P.Z * Ax;
			U0 = std::fmin(U0, U); U1 = std::fmax(U1, U); V0 = std::fmin(V0, V); V1 = std::fmax(V1, V);
		}
		const double Box = (U1 - U0) * (V1 - V0);
		if (Box < Best - 1e-6) { Best = Box; Ux = Ax; Uz = Az; }
	}
	if (Best >= 1e17) return Out;
	// (u along the box's longer side)
	{
		double U0 = 1e18, U1 = -1e18, V0 = 1e18, V1 = -1e18;
		for (const P2& P : Ring)
		{
			const double U = P.X * Ux + P.Z * Uz, V = -P.X * Uz + P.Z * Ux;
			U0 = std::fmin(U0, U); U1 = std::fmax(U1, U); V0 = std::fmin(V0, V); V1 = std::fmax(V1, V);
		}
		if (V1 - V0 > U1 - U0) { const double T = Ux; Ux = -Uz; Uz = T; }
	}
	std::vector<double> Us, Vs;
	for (const P2& P : Ring) { Us.push_back(P.X * Ux + P.Z * Uz); Vs.push_back(-P.X * Uz + P.Z * Ux); }
	const auto Piece = [&](double U0, double U1, double V0, double V1)
	{
		Building B;
		const double Uc = (U0 + U1) * 0.5, Vc = (V0 + V1) * 0.5;
		// (the drawer's own frame: x across along (cos, -sin), z deep along (sin, cos))
		B.X = Uc * Ux - Vc * Uz;
		B.Z = Uc * Uz + Vc * Ux;
		B.W = U1 - U0;
		B.D = V1 - V0;
		B.Yaw = std::atan2(-Uz, Ux);
		B.Height = Height;
		B.More.push_back(double(Kind));
		Out.push_back(std::move(B));
	};
	const double U0 = *std::min_element(Us.begin(), Us.end()), U1 = *std::max_element(Us.begin(), Us.end());
	const double V0 = *std::min_element(Vs.begin(), Vs.end()), V1 = *std::max_element(Vs.begin(), Vs.end());
	if (N <= 4 || Area >= 0.9 * (U1 - U0) * (V1 - V0))
	{
		Piece(U0, U1, V0, V1);
		return Out;
	}
	// Cut across the long way at its corners: in each slab the outline's spans across, slabs alike
	// (within 0.8 m) as one; a slab under 1.5 m wide goes in with the next.
	std::vector<double> Cuts = Us;
	std::sort(Cuts.begin(), Cuts.end());
	std::vector<double> Kept;
	for (double C : Cuts)
		if (Kept.empty() || C - Kept.back() >= 1.5) Kept.push_back(C);
	if (Kept.back() < U1 - 1e-6) Kept.back() = U1;
	struct Slab { double A, B; std::vector<std::pair<double, double>> Spans; };
	std::vector<Slab> Slabs;
	for (size_t K = 0; K + 1 < Kept.size(); ++K)
	{
		const double Mid = (Kept[K] + Kept[K + 1]) * 0.5;
		std::vector<double> Hits;
		for (size_t I = 0; I < N; ++I)
		{
			const double Ua = Us[I], Ub = Us[(I + 1) % N];
			if ((Ua <= Mid) == (Ub <= Mid)) continue;
			const double T = (Mid - Ua) / (Ub - Ua);
			Hits.push_back(Vs[I] + (Vs[(I + 1) % N] - Vs[I]) * T);
		}
		std::sort(Hits.begin(), Hits.end());
		Slab S{Kept[K], Kept[K + 1], {}};
		for (size_t H = 0; H + 1 < Hits.size(); H += 2)
			if (Hits[H + 1] - Hits[H] >= 1.0) S.Spans.emplace_back(Hits[H], Hits[H + 1]);
		if (S.Spans.empty()) continue;
		if (!Slabs.empty() && std::fabs(Slabs.back().B - S.A) < 1e-6 && Slabs.back().Spans.size() == S.Spans.size())
		{
			bool Alike = true;
			for (size_t J = 0; J < S.Spans.size() && Alike; ++J)
				Alike = std::fabs(Slabs.back().Spans[J].first - S.Spans[J].first) < 0.8 && std::fabs(Slabs.back().Spans[J].second - S.Spans[J].second) < 0.8;
			if (Alike)
			{
				Slabs.back().B = S.B;
				continue;
			}
		}
		Slabs.push_back(std::move(S));
	}
	size_t Count = 0;
	for (const Slab& S : Slabs) Count += S.Spans.size();
	if (Count == 0 || Count > 8)
	{
		// (a shape too broken up to be a few boxes: its box, shrunk to its own area)
		const double Shrink = std::sqrt(std::fmax(0.25, Area / ((U1 - U0) * (V1 - V0))));
		const double Uc = (U0 + U1) * 0.5, Vc = (V0 + V1) * 0.5, Hu = (U1 - U0) * 0.5 * Shrink, Hv = (V1 - V0) * 0.5 * Shrink;
		Piece(Uc - Hu, Uc + Hu, Vc - Hv, Vc + Hv);
		return Out;
	}
	for (const Slab& S : Slabs)
		for (const auto& Sp : S.Spans) Piece(S.A, S.B, Sp.first, Sp.second);
	return Out;
}

void RidePlan::MarkRows(std::vector<OutlinePiece>& Pieces)
{
	// Two houses' main boxes share a wall where they lie square to each other (within 6 degrees), a side
	// of each on the same line (within 0.7 m) and overlapping along it by 3 m or half the shorter side.
	// Rows are the houses joined so; a pair are semis, three or more a terrace.
	const size_t N = Pieces.size();
	std::unordered_map<long long, std::vector<int>> Grid;
	constexpr double Cell = 24.0;
	const auto Key = [](long long Gx, long long Gz) { return Gx * 4000037LL + Gz; };
	for (size_t I = 0; I < N; ++I)
	{
		const OutlinePiece& P = Pieces[I];
		if (!P.Main || P.B.More.empty() || int(P.B.More[0]) != 0) continue;   // (houses' main boxes)
		Grid[Key(static_cast<long long>(std::floor(P.B.X / Cell)), static_cast<long long>(std::floor(P.B.Z / Cell)))].push_back(int(I));
	}
	std::vector<int> Root(N);
	for (size_t I = 0; I < N; ++I) Root[I] = int(I);
	const std::function<int(int)> Find = [&](int A) { while (Root[size_t(A)] != A) { Root[size_t(A)] = Root[size_t(Root[size_t(A)])]; A = Root[size_t(A)]; } return A; };
	for (const auto& Bucket : Grid)
		for (const int I : Bucket.second)
		{
			OutlinePiece& A = Pieces[size_t(I)];
			const double Cs = std::cos(A.B.Yaw), Sn = std::sin(A.B.Yaw);
			const long long Gx = static_cast<long long>(std::floor(A.B.X / Cell)), Gz = static_cast<long long>(std::floor(A.B.Z / Cell));
			for (long long Dx = -1; Dx <= 1; ++Dx)
				for (long long Dz = -1; Dz <= 1; ++Dz)
				{
					const auto It = Grid.find(Key(Gx + Dx, Gz + Dz));
					if (It == Grid.end()) continue;
					for (const int J : It->second)
					{
						if (J <= I) continue;
						OutlinePiece& Bo = Pieces[size_t(J)];
						if (Bo.Outline == A.Outline) continue;
						double Turn = std::fmod(std::fabs(A.B.Yaw - Bo.B.Yaw), 3.14159265358979 * 0.5);
						Turn = std::fmin(Turn, 3.14159265358979 * 0.5 - Turn);
						if (Turn > 0.105) continue;
						// (Bo's corners in A's frame: x along (cos, -sin), z along (sin, cos), as the drawer has it)
						double Bx0 = 1e18, Bx1 = -1e18, Bz0 = 1e18, Bz1 = -1e18;
						const double Cb = std::cos(Bo.B.Yaw), Sb = std::sin(Bo.B.Yaw);
						for (double Su : {-0.5, 0.5})
							for (double Sv : {-0.5, 0.5})
							{
								const double Wx = Bo.B.X + Su * Bo.B.W * Cb + Sv * Bo.B.D * Sb, Wz = Bo.B.Z - Su * Bo.B.W * Sb + Sv * Bo.B.D * Cb;
								const double Dxw = Wx - A.B.X, Dzw = Wz - A.B.Z;
								const double Lx = Dxw * Cs - Dzw * Sn, Lz = Dxw * Sn + Dzw * Cs;
								Bx0 = std::fmin(Bx0, Lx); Bx1 = std::fmax(Bx1, Lx); Bz0 = std::fmin(Bz0, Lz); Bz1 = std::fmax(Bz1, Lz);
							}
						const double Hw = A.B.W * 0.5, Hd = A.B.D * 0.5;
						const auto Overlap = [](double A0, double A1, double C0, double C1) { return std::fmin(A1, C1) - std::fmax(A0, C0); };
						unsigned char Side = 0;
						const double Oz = Overlap(-Hd, Hd, Bz0, Bz1), Ox = Overlap(-Hw, Hw, Bx0, Bx1);
						const double NeedZ = std::fmin(3.0, 0.5 * std::fmin(A.B.D, Bz1 - Bz0)), NeedX = std::fmin(3.0, 0.5 * std::fmin(A.B.W, Bx1 - Bx0));
						if (std::fabs(Bx0 - Hw) < 0.7 && Oz >= NeedZ) Side = 1;
						else if (std::fabs(Bx1 + Hw) < 0.7 && Oz >= NeedZ) Side = 2;
						else if (std::fabs(Bz0 - Hd) < 0.7 && Ox >= NeedX) Side = 4;
						else if (std::fabs(Bz1 + Hd) < 0.7 && Ox >= NeedX) Side = 8;
						if (Side == 0) continue;
						A.Party |= Side;
						A.Stacks |= Side;   // (the first of a pair draws the stack on their wall)
						// (and the side of Bo that faces A)
						const double Dxw = A.B.X - Bo.B.X, Dzw = A.B.Z - Bo.B.Z;
						const double Lx = Dxw * Cb - Dzw * Sb, Lz = Dxw * Sb + Dzw * Cb;
						Bo.Party |= std::fabs(Lx) / std::fmax(Bo.B.W, 0.1) > std::fabs(Lz) / std::fmax(Bo.B.D, 0.1) ? (Lx > 0.0 ? 1 : 2) : (Lz > 0.0 ? 4 : 8);
						Root[size_t(Find(J))] = Find(I);
					}
				}
		}
	// The rows: their size, each house's place along it, and the row's spot.
	std::unordered_map<int, std::vector<int>> Rows;
	for (size_t I = 0; I < N; ++I)
		if (Pieces[I].Main && Pieces[I].Party != 0) Rows[Find(int(I))].push_back(int(I));
	for (auto& Row : Rows)
	{
		std::vector<int>& M = Row.second;
		const OutlinePiece& First = Pieces[size_t(M.front())];
		// (along the row: across its party walls)
		const bool AlongX = (First.Party & 3) != 0;
		const double Cs = std::cos(First.B.Yaw), Sn = std::sin(First.B.Yaw);
		const double Ax = AlongX ? Cs : Sn, Az = AlongX ? -Sn : Cs;
		std::sort(M.begin(), M.end(), [&](int A, int C) { return Pieces[size_t(A)].B.X * Ax + Pieces[size_t(A)].B.Z * Az < Pieces[size_t(C)].B.X * Ax + Pieces[size_t(C)].B.Z * Az; });
		const P2 Seed(Pieces[size_t(M.front())].B.X, Pieces[size_t(M.front())].B.Z);
		for (size_t K = 0; K < M.size(); ++K)
		{
			OutlinePiece& P = Pieces[size_t(M[K])];
			P.RowSize = int(M.size());
			P.RowPos = int(K);
			P.RowSeed = Seed;
		}
	}
	// (the pieces that aren't main boxes take their outline's row)
	for (OutlinePiece& P : Pieces)
		if (!P.Main && P.Outline >= 0 && size_t(P.Outline) < N)
			for (size_t K = size_t(P.Outline); K < N && Pieces[K].Outline == P.Outline; ++K)
				if (Pieces[K].Main) { P.RowSize = Pieces[K].RowSize; P.RowPos = Pieces[K].RowPos; P.RowSeed = Pieces[K].RowSeed; break; }
	for (OutlinePiece& P : Pieces)
		if (P.RowSize <= 1) P.RowSeed = P2(P.B.X, P.B.Z);
}

void RidePlan::OutlinesIn(double X0, double Z0, double Size, std::vector<std::shared_ptr<const std::vector<OutlinePiece>>>& Hold,
	std::vector<const OutlinePiece*>& Out, std::vector<CellId>& Covered) const
{
	if (!Outlines || CellsFrom == nullptr) return;
	const std::shared_ptr<const WorldIndex> Idx = CellsFrom->Index();
	if (!Idx || Idx->Buildings.empty()) return;
	const Frame F = Area.Roads.FrameOf();
	const Geo A = F.ToGeo(X0, Z0 + Size), B = F.ToGeo(X0 + Size, Z0);   // (south-west, north-east: z runs south)
	OutlineStore& St = *Outlines;
	const size_t First = Hold.size();
	for (const CellId& C : CellsInBox(A.Lat, A.Lon, B.Lat, B.Lon))
	{
		const std::string Name = C.Name();
		OutlineStore::CellZip* Z = nullptr;
		{
			std::lock_guard<std::mutex> Lock(St.Lock);
			Z = &St.Cells[Name];
			if (Z->State == 0)
			{
				// (read once: the zip's list and its words; a cell that can't be had keeps its boxes)
				Z->State = 2;
				std::string Bytes;
				std::vector<ZipEntry> List;
				TorqueFit::Json Head;
				// ([build 155] the hub, from beenster's 154 log: "a fetch was asked for on the game thread and
				// refused (c580_701 .bld.zip)", then "cell 580_701: no outlines", and that town stayed boxes all
				// ride, as Harehills did. Asked where it may not be fetched, a cell is left to be asked again from
				// a worker, its boxes only for this square.)
				bool Deferred = false;
				const bool Got = CellsFrom->BuildingsZip(Name, *Idx, Bytes, &Deferred);
				if (Deferred)
				{
					Z->State = 0;
					continue;
				}
				if (Got && ZipList(Bytes, List))
				{
					std::string Text;
					for (const ZipEntry& E : List)
						if (E.Name == "buildings.json" && ZipRead(Bytes, E, Text) && TorqueFit::Json::Parse(Text, Head) && Head.IsObject())
						{
							Z->Zip = std::move(Bytes);
							Z->Entries = std::move(List);
							Z->Tiles = std::max(1, int(Head.Num("tiles", 8)));
							if (const TorqueFit::Json* W = Head.Get("words"))
								for (const TorqueFit::Json& Word : W->A) Z->Words.push_back(Word.S);
							Z->State = 1;
							Z->Release = Head.Str("release");
							break;
						}
				}
				// [LiDAR stage 2] (the cell's heights, kept only where they were measured on this release's outlines; a
				// local read: LidarFrom never waits on the network, as this holds the lock)
				std::string LidarText;
				if (Z->State == 1 && LidarFrom && LidarFrom(Name, LidarText))
				{
					const size_t LidarRows = ReadLidar(LidarText, Z->Release, Z->Lidar);
					St.Notes.push_back("cell " + Name + ": LiDAR heights for " + std::to_string(LidarRows) + " outlines" + (LidarRows == 0 ? " (none for release " + Z->Release + ")" : ""));
				}
				const auto Have = Idx->Buildings.find(Name);
				St.Notes.push_back(Z->State == 1
					? "cell " + Name + ": outlines read (" + std::to_string(Z->Entries.size() - 1) + " tiles, " + std::to_string(Z->Zip.size() / 1024) + " KB)"
					: "cell " + Name + ": no outlines (" + (Have == Idx->Buildings.end() ? "not in the index" : "zip not had or unreadable") + "), the cell's own boxes");
			}
			if (Z->State != 1) continue;
		}
		Covered.push_back(C);
		const Geo Mid = CellMiddle(C);
		const double South = Mid.Lat - CellDeg * 0.5, West = Mid.Lon - CellDeg * 0.5, Step = CellDeg / Z->Tiles;
		const int Tx0 = std::clamp(int(std::floor((A.Lon - West) / Step)), 0, Z->Tiles - 1), Tx1 = std::clamp(int(std::floor((B.Lon - West) / Step)), 0, Z->Tiles - 1);
		const int Tz0 = std::clamp(int(std::floor((A.Lat - South) / Step)), 0, Z->Tiles - 1), Tz1 = std::clamp(int(std::floor((B.Lat - South) / Step)), 0, Z->Tiles - 1);
		for (int Tx = Tx0; Tx <= Tx1; ++Tx)
			for (int Tz = Tz0; Tz <= Tz1; ++Tz)
			{
				const std::string Key = std::to_string(Tx) + "_" + std::to_string(Tz);
				{
					std::lock_guard<std::mutex> Lock(St.Lock);
					const auto Have = St.TilesRead.find(Name + "/" + Key);
					if (Have != St.TilesRead.end()) { Hold.push_back(Have->second); continue; }
				}
				// (made outside the lock: the zip and words are never changed once read)
				auto Made = std::make_shared<std::vector<OutlinePiece>>();
				std::string Text;
				TorqueFit::Json Rows;
				for (const ZipEntry& E : Z->Entries)
					if (E.Name == "t" + Key + ".json" && ZipRead(Z->Zip, E, Text) && TorqueFit::Json::Parse(Text, Rows) && Rows.IsArray())
						for (size_t Row = 0; Row < Rows.A.size(); ++Row)
						{
							const TorqueFit::Json& R = Rows.A[Row];
							if (R.Size() < 15) continue;
							const auto Word = [&](size_t I) { const int W = int(R.A[I].N); return W > 0 && W < int(Z->Words.size()) ? Z->Words[size_t(W)] : std::string(); };
							std::vector<P2> Ring;
							long long Lat = static_cast<long long>(R.A[9].N), Lon = static_cast<long long>(R.A[10].N);
							Ring.push_back(F.FromGeo(double(Lat) * 1e-6, double(Lon) * 1e-6));
							for (size_t K = 11; K + 1 < R.A.size(); K += 2)
							{
								Lat += static_cast<long long>(R.A[K].N);
								Lon += static_cast<long long>(R.A[K + 1].N);
								Ring.push_back(F.FromGeo(double(Lat) * 1e-6, double(Lon) * 1e-6));
							}
							double Sum = 0.0;
							for (size_t I = 0; I < Ring.size(); ++I) Sum += Ring[I].X * Ring[(I + 1) % Ring.size()].Z - Ring[(I + 1) % Ring.size()].X * Ring[I].Z;
							const double Dm = R.A[0].N, Floors = R.A[2].N;
							const double Height = Dm >= 0.0 ? Dm * 0.1 : 0.0;
							BuildingLook Look;
							// (the drawer's words: class, roof shape, roof colour, roof material, wall material, wall colour)
							Look.Words[0] = Word(3); Look.Words[1] = Word(4); Look.Words[2] = Word(6); Look.Words[3] = Word(5); Look.Words[4] = Word(7); Look.Words[5] = Word(8);
							float LidarEaves = 0.0f, LidarRidge = 0.0f;
							LidarOfRow(Z->Lidar, Key, int(Row), static_cast<long long>(R.A[9].N), LidarEaves, LidarRidge);
							const size_t FirstPiece = Made->size();
							for (Building& Bp : PiecesOf(Ring, Height, KindOfClass(Look.Words[0], std::fabs(Sum) * 0.5)))
							{
								OutlinePiece P;
								Look.At = P2(Bp.X, Bp.Z);
								P.B = std::move(Bp);
								P.Look = Look;
								P.Floors = Floors > 0.0 ? Floors : 0.0;
								P.LidarEaves = LidarEaves;
								P.LidarRidge = LidarRidge;
								Made->push_back(std::move(P));
							}
							// (the outline's number, and its biggest box its main one)
							size_t Biggest = FirstPiece;
							for (size_t K = FirstPiece; K < Made->size(); ++K)
							{
								(*Made)[K].Outline = int(FirstPiece);
								if ((*Made)[K].B.W * (*Made)[K].B.D > (*Made)[Biggest].B.W * (*Made)[Biggest].B.D) Biggest = K;
							}
							if (Biggest < Made->size()) (*Made)[Biggest].Main = true;
						}
				MarkRows(*Made);
				std::lock_guard<std::mutex> Lock(St.Lock);
				Hold.push_back(St.TilesRead.emplace(Name + "/" + Key, Made).first->second);
			}
	}
	for (size_t L = First; L < Hold.size(); ++L)
		for (const OutlinePiece& P : *Hold[L])
			if (P.B.X >= X0 && P.B.X < X0 + Size && P.B.Z >= Z0 && P.B.Z < Z0 + Size) Out.push_back(&P);
}
} // namespace TorqueWorld
