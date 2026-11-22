#include "Dash/TftPlannerMap.h"

#include "Dash/TftMapFill.h"
#include "World/CellStore.h"
#include "World/WorldRegion.h"

#include <algorithm>
#include <cmath>

namespace TorqueTft
{
std::vector<TorqueWorld::CellId> PlannerMap::CellsFor(const TorqueWorld::Route::MapView& View)
{
	if (View.Zoom < From) return {};
	const TorqueWorld::Geo NW = View.PlaceAt(0.0, 0.0), SE = View.PlaceAt(double(View.W), double(View.H));
	std::vector<TorqueWorld::CellId> Out = TorqueWorld::CellsInBox(SE.Lat, NW.Lon, NW.Lat, SE.Lon);
	if (int(Out.size()) > MostCells) return {};
	const TorqueWorld::CellId Mid = TorqueWorld::CellOf(View.Lat, View.Lon);
	std::stable_sort(Out.begin(), Out.end(), [&Mid](const TorqueWorld::CellId& A, const TorqueWorld::CellId& B)
	{
		const auto D = [&Mid](const TorqueWorld::CellId& C) { return std::abs(C.Row - Mid.Row) + std::abs(C.Col - Mid.Col); };
		return D(A) < D(B);
	});
	return Out;
}

std::shared_ptr<const PlannerMap::Cell> PlannerMap::Read(TorqueWorld::CellStore& Store, TorqueWorld::CellId C, const std::string& Dir)
{
	// (the cell as a ride's area reads it, alone, in a frame at its middle: its roads, buildings, places and
	// stops, then its streams, ground, paths and railways from its own files)
	const TorqueWorld::Geo Mid = TorqueWorld::CellMiddle(C);
	TorqueWorld::Region Land;
	if (!TorqueWorld::Region::Stitch("map:" + C.Name(), C.Name(), {Dir}, Mid.Lat, Mid.Lon, Land)) return nullptr;
	if (const std::shared_ptr<const TorqueWorld::WorldIndex> Idx = Store.IndexNow())
	{
		const std::string Name = C.Name();
		TorqueFit::Json File;
		if (Store.Kept(TorqueWorld::CellStore::EKind::Streams, Name, *Idx, File))
			if (const TorqueFit::Json* Rows = File.Get("streams"))
				if (Rows->IsArray()) Land.AddStreams(*Rows);
		if (Store.Kept(TorqueWorld::CellStore::EKind::Features, Name, *Idx, File) && File.Get("points") != nullptr) Land.AddFeatures(File);
		if (Store.Kept(TorqueWorld::CellStore::EKind::Grounds, Name, *Idx, File)) Land.AddGround(File);
		if (Store.Kept(TorqueWorld::CellStore::EKind::Paths, Name, *Idx, File)) Land.AddPaths(File);
	}
	auto Out = std::make_shared<Cell>();
	FillMap(Out->Data, Land);
	Out->Lat0 = Mid.Lat;
	Out->Lon0 = Mid.Lon;
	return Out;
}

int PlannerMap::Know(TorqueWorld::CellStore& Store, const TorqueWorld::Route::MapView& View, int Load, const std::function<bool()>& Stop, int* LoadedOut)
{
	if (LoadedOut != nullptr) *LoadedOut = 0;
	const std::vector<TorqueWorld::CellId> Cells = CellsFor(View);
	if (Cells.empty()) return 0;
	const std::shared_ptr<const TorqueWorld::WorldIndex> Idx = Store.IndexNow();
	const std::vector<std::string> Roots = Store.CellRoots();
	int Missing = 0, Loaded = 0;
	for (const TorqueWorld::CellId& C : Cells)
	{
		{
			std::lock_guard<std::mutex> G(Lock);
			if (Mem.count(C) != 0) continue;
		}
		std::string D = TorqueWorld::Region::CellDir(Roots, C);
		// (the sea, or not baked yet, or offline: nothing to draw)
		if (D.empty() && (Store.Offline || !Idx || Store.StateOf(C, *Idx) != TorqueWorld::CellStore::EState::Baked)) continue;
		if (Loaded >= Load || (Stop && Stop())) { ++Missing; continue; }
		++Loaded;
		if (LoadedOut != nullptr) *LoadedOut = Loaded;
		if (D.empty() && (!Store.FetchCells({C}).empty() || (D = TorqueWorld::Region::CellDir(Roots, C)).empty())) { ++Missing; continue; }
		std::shared_ptr<const Cell> Got = Read(Store, C, D);
		if (!Got) { ++Missing; continue; }
		std::lock_guard<std::mutex> G(Lock);
		Mem[C] = {std::move(Got), ++Tick};
		while (Mem.size() > size_t(Memory))
		{
			auto Oldest = Mem.begin();
			for (auto It = Mem.begin(); It != Mem.end(); ++It)
				if (It->second.second < Oldest->second.second) Oldest = It;
			Mem.erase(Oldest);
		}
	}
	return Missing;
}

bool PlannerMap::Draw(const TorqueWorld::Route::MapView& View, std::vector<uint8_t>& Bgra, bool Night)
{
	const int W = std::max(1, View.W), H = std::max(1, View.H);
	if (Bgra.size() != size_t(W) * size_t(H) * 4) return false;
	const std::vector<TorqueWorld::CellId> Cells = CellsFor(View);
	if (Cells.empty()) return false;
	std::vector<std::shared_ptr<const Cell>> Held;
	{
		std::lock_guard<std::mutex> G(Lock);
		for (const TorqueWorld::CellId& C : Cells)
		{
			auto It = Mem.find(C);
			if (It != Mem.end())
			{
				It->second.second = ++Tick;
				Held.push_back(It->second.first);
			}
		}
	}
	if (Held.empty()) return false;
	// Every cell's land in the view's own flat frame: metres from its middle along the picture's pixels (so
	// it lies exactly where the planner's pins and line are), only what is near the view.
	const double Mpp = View.MetresPerPixel();
	const double Pad = 40.0;   // (pixels round the view still taken, for lines that cross its edge)
	MapData Data;
	for (const std::shared_ptr<const Cell>& Ce : Held)
	{
		const TorqueWorld::Frame F(Ce->Lat0, Ce->Lon0);
		const auto To = [&](const MapPt& Q)
		{
			const TorqueWorld::Geo G = F.ToGeo(Q.X, Q.Z);
			const TorqueWorld::P2 Px = View.PixelOf(G.Lat, G.Lon);
			return MapPt((Px.X - W * 0.5) * Mpp, (Px.Z - H * 0.5) * Mpp);
		};
		const auto Shows = [&](double X0, double Z0, double X1, double Z1)
		{
			const MapPt A = To(MapPt(X0, Z0)), B = To(MapPt(X1, Z1));
			const double HalfW = (W * 0.5 + Pad) * Mpp, HalfH = (H * 0.5 + Pad) * Mpp;
			return std::max(A.X, B.X) >= -HalfW && std::min(A.X, B.X) <= HalfW && std::max(A.Z, B.Z) >= -HalfH && std::min(A.Z, B.Z) <= HalfH;
		};
		for (const MapArea& A : Ce->Data.Areas)
		{
			if (!Shows(A.X0, A.Z0, A.X1, A.Z1)) continue;
			MapArea Moved;
			Moved.Kind = A.Kind;
			Moved.Tris.reserve(A.Tris.size());
			for (const MapPt& Q : A.Tris) Moved.Tris.push_back(To(Q));
			Moved.X0 = Moved.X1 = Moved.Tris.empty() ? 0.0 : Moved.Tris[0].X;
			Moved.Z0 = Moved.Z1 = Moved.Tris.empty() ? 0.0 : Moved.Tris[0].Z;
			for (const MapPt& Q : Moved.Tris)
			{
				Moved.X0 = std::min(Moved.X0, Q.X); Moved.X1 = std::max(Moved.X1, Q.X);
				Moved.Z0 = std::min(Moved.Z0, Q.Z); Moved.Z1 = std::max(Moved.Z1, Q.Z);
			}
			Data.Areas.push_back(std::move(Moved));
		}
		for (const MapLine& L : Ce->Data.Lines)
		{
			if (!Shows(L.X0, L.Z0, L.X1, L.Z1)) continue;
			std::vector<MapPt> Pts;
			Pts.reserve(L.Pts.size());
			for (const MapPt& Q : L.Pts) Pts.push_back(To(Q));
			Data.AddLine(L.Kind, std::string(), Pts);
		}
	}
	Data.Index();
	// The sea as the relief has it (its blue), the land the map's own colour, then the map over them.
	const Rgba Land = MapLand(Night), Water = MapWater(Night);
	const auto B8 = [](float V) { return uint8_t(std::lround(std::max(0.0f, std::min(1.0f, V)) * 255.0f)); };
	for (size_t K = 0; K + 3 < Bgra.size(); K += 4)
	{
		const bool Sea = Bgra[K] > Bgra[K + 1] && Bgra[K] > Bgra[K + 2] + 30;   // (blue: B over G and R)
		const Rgba& C = Sea ? Water : Land;
		Bgra[K] = B8(C.B);
		Bgra[K + 1] = B8(C.G);
		Bgra[K + 2] = B8(C.R);
		Bgra[K + 3] = 255;
	}
	MapScene Scene;
	Scene.Data = &Data;
	Scene.NorthUp = true;
	Scene.Night = Night;
	Scene.Metres = W * Mpp;
	Scene.LandOnly = true;
	Scene.CloseUnder = W * 3.0;   // (buildings and paths from about 3 m a pixel)
	Scene.FarOver = W * 12.0;     // (tracks and streams up to about 12 m a pixel)
	Paint P(static_cast<float>(W), static_cast<float>(H));
	DrawMap(P, 0.0f, 0.0f, float(W), float(H), Scene, 0.5f);
	Rasterize(P, Bgra, W, H);
	return true;
}

void Rasterize(const Paint& P, std::vector<uint8_t>& Bgra, int W, int H)
{
	if (Bgra.size() != size_t(W) * size_t(H) * 4) return;
	// (each pixel's cover by four samples in it, so edges are smoothed; a command's triangles gathered
	// first, so where two of them meet no seam of the ground shows through)
	static const float Sx[4] = {0.375f, 0.875f, 0.125f, 0.625f}, Sy[4] = {0.125f, 0.375f, 0.625f, 0.875f};
	std::vector<uint8_t> Mask(size_t(W) * size_t(H), 0);
	std::vector<size_t> Touched;
	for (const Cmd& C : P.Cmds)
	{
		if (C.IsText || C.Colour.A <= 0.0f) continue;
		Touched.clear();
		for (size_t T = 0; T + 2 < C.Tris.size(); T += 3)
		{
			const P2 A = C.Tris[T], B = C.Tris[T + 1], Q = C.Tris[T + 2];
			const float Area = (B.X - A.X) * (Q.Y - A.Y) - (B.Y - A.Y) * (Q.X - A.X);
			if (std::fabs(Area) < 1.0e-6f) continue;
			const int X0 = std::max(0, int(std::floor(std::min({A.X, B.X, Q.X})))), X1 = std::min(W - 1, int(std::floor(std::max({A.X, B.X, Q.X}))));
			const int Y0 = std::max(0, int(std::floor(std::min({A.Y, B.Y, Q.Y})))), Y1 = std::min(H - 1, int(std::floor(std::max({A.Y, B.Y, Q.Y}))));
			const float Sign = Area > 0.0f ? 1.0f : -1.0f;
			const auto Edge = [](const P2& U, const P2& V, float X, float Y) { return (V.X - U.X) * (Y - U.Y) - (V.Y - U.Y) * (X - U.X); };
			for (int Y = Y0; Y <= Y1; ++Y)
				for (int X = X0; X <= X1; ++X)
				{
					uint8_t Bits = 0;
					for (int S = 0; S < 4; ++S)
					{
						const float Px = float(X) + Sx[S], Py = float(Y) + Sy[S];
						if (Edge(A, B, Px, Py) * Sign >= 0.0f && Edge(B, Q, Px, Py) * Sign >= 0.0f && Edge(Q, A, Px, Py) * Sign >= 0.0f) Bits = uint8_t(Bits | (1 << S));
					}
					if (Bits == 0) continue;
					const size_t At = size_t(Y) * size_t(W) + size_t(X);
					if (Mask[At] == 0) Touched.push_back(At);
					Mask[At] = uint8_t(Mask[At] | Bits);
				}
		}
		const float CR = std::max(0.0f, std::min(1.0f, C.Colour.R)) * 255.0f, CG = std::max(0.0f, std::min(1.0f, C.Colour.G)) * 255.0f, CB = std::max(0.0f, std::min(1.0f, C.Colour.B)) * 255.0f;
		for (const size_t At : Touched)
		{
			const uint8_t Bits = Mask[At];
			Mask[At] = 0;
			const int In = (Bits & 1) + ((Bits >> 1) & 1) + ((Bits >> 2) & 1) + ((Bits >> 3) & 1);
			const float Cover = std::min(1.0f, C.Colour.A) * float(In) * 0.25f;
			uint8_t* Px8 = &Bgra[At * 4];
			Px8[0] = uint8_t(std::lround(Px8[0] + (CB - Px8[0]) * Cover));
			Px8[1] = uint8_t(std::lround(Px8[1] + (CG - Px8[1]) * Cover));
			Px8[2] = uint8_t(std::lround(Px8[2] + (CR - Px8[2]) * Cover));
		}
	}
}
} // namespace TorqueTft
