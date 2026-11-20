#include "WorldRouteMap.h"

#include "HeightTiles.h"
#include "WorldFerries.h"
#include "WorldFiles.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <thread>
#include <atomic>

namespace TorqueWorld
{
namespace Route
{
namespace
{
const double MapPi = 3.14159265358979323846;

// The world's pixel at a fractional zoom (256 to a tile at that zoom).
double WorldX(double Lon, double Zoom)
{
	return (Lon + 180.0) / 360.0 * 256.0 * std::pow(2.0, Zoom);
}
double WorldY(double Lat, double Zoom)
{
	const double S = std::sin(std::max(-85.05, std::min(85.05, Lat)) * MapPi / 180.0);
	return (0.5 - std::log((1.0 + S) / (1.0 - S)) / (4.0 * MapPi)) * 256.0 * std::pow(2.0, Zoom);
}
double LonOf(double X, double Zoom)
{
	return X / (256.0 * std::pow(2.0, Zoom)) * 360.0 - 180.0;
}
double LatOf(double Y, double Zoom)
{
	const double N = MapPi * (1.0 - 2.0 * Y / (256.0 * std::pow(2.0, Zoom)));
	return std::atan(std::sinh(N)) * 180.0 / MapPi;
}

struct Rgb
{
	double R, G, B;
};
Rgb Mix(const Rgb& A, const Rgb& B, double T)
{
	return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
}
// The land's tint by height (m), low greens up to bare grey tops.
Rgb Tint(double H)
{
	static const double At[] = {0.0, 80.0, 250.0, 450.0, 700.0, 1000.0, 1350.0};
	static const Rgb Col[] = {{0.47, 0.62, 0.40}, {0.53, 0.67, 0.42}, {0.70, 0.73, 0.50}, {0.74, 0.66, 0.47}, {0.64, 0.55, 0.44}, {0.72, 0.68, 0.64}, {0.90, 0.89, 0.88}};
	const int N = int(sizeof(At) / sizeof(At[0]));
	if (H <= At[0]) return Col[0];
	for (int I = 1; I < N; ++I)
		if (H < At[I]) return Mix(Col[I - 1], Col[I], (H - At[I - 1]) / (At[I] - At[I - 1]));
	return Col[N - 1];
}
uint8_t Byte(double V)
{
	return uint8_t(std::lround(std::max(0.0, std::min(1.0, V)) * 255.0));
}
} // namespace

P2 MapView::PixelOf(double InLat, double InLon) const
{
	return {WorldX(InLon, Zoom) - WorldX(Lon, Zoom) + W * 0.5, WorldY(InLat, Zoom) - WorldY(Lat, Zoom) + H * 0.5};
}

Geo MapView::PlaceAt(double X, double Y) const
{
	return {LatOf(WorldY(Lat, Zoom) + Y - H * 0.5, Zoom), LonOf(WorldX(Lon, Zoom) + X - W * 0.5, Zoom)};
}

double MapView::MetresPerPixel() const
{
	return 156543.03392 * std::cos(Lat * MapPi / 180.0) / std::pow(2.0, Zoom);
}

MapView MapView::Panned(double DX, double DY) const
{
	MapView V = *this;
	const Geo Mid = PlaceAt(W * 0.5 + DX, H * 0.5 + DY);
	V.Lat = std::max(-80.0, std::min(80.0, Mid.Lat));
	V.Lon = Mid.Lon;
	return V;
}

MapView MapView::Zoomed(double Steps, double AtX, double AtY) const
{
	MapView V = *this;
	const Geo Held = PlaceAt(AtX, AtY);
	V.Zoom = std::max(MinZoom, std::min(MaxZoom, Zoom + Steps));
	// (the place under the pointer stays under it)
	const P2 Now = V.PixelOf(Held.Lat, Held.Lon);
	return V.Panned(Now.X - AtX, Now.Z - AtY);
}

int MapView::TileZoom() const
{
	// (a 512 px tile at zoom z is 2^(z+1) 256 px tiles: one zoom finer than the picture, rounded down a little)
	return std::max(0, std::min(HeightStore::AwsTop, int(std::floor(Zoom - 1.0 + 0.35))));
}

std::vector<TileKey> MapView::Tiles() const
{
	const int Z = TileZoom();
	const Geo TopLeft = PlaceAt(0.0, 0.0), BottomRight = PlaceAt(double(W), double(H));
	const int N = 1 << Z;
	const int X0 = int(std::floor(MercatorX(TopLeft.Lon, Z))), X1 = int(std::floor(MercatorX(BottomRight.Lon, Z)));
	const int Y0 = std::max(0, int(std::floor(MercatorY(TopLeft.Lat, Z)))), Y1 = std::min(N - 1, int(std::floor(MercatorY(BottomRight.Lat, Z))));
	std::vector<TileKey> Out;
	for (int Y = Y0; Y <= Y1; ++Y)
		for (int X = X0; X <= X1; ++X) Out.push_back({Z, ((X % N) + N) % N, Y});
	return Out;
}

const char* LayerName(int Layer)
{
	return Layer == RoadMap ? "Road map" : Layer == TerrainMap ? "Terrain" : Layer == Satellite ? "Satellite" : "Relief";
}

bool MapRoads::Claim(const std::string& Block)
{
	std::lock_guard<std::mutex> G(Lock);
	return Blocks.insert(Block).second;
}

void MapRoads::Add(std::vector<Line>&& More)
{
	std::lock_guard<std::mutex> G(Lock);
	for (Line& L : More) Lines.push_back(std::move(L));
}

namespace
{
// Colours of the road map and the terrain map.
const Rgb PaperLand = {0.955, 0.945, 0.905}, PaperSea = {0.66, 0.80, 0.90};
// The terrain's tint by height (m): stronger than the relief's, so heights read at a glance.
Rgb TerrainTint(double H)
{
	static const double At[] = {0.0, 50.0, 150.0, 300.0, 500.0, 750.0, 1000.0, 1300.0};
	static const Rgb Col[] = {{0.56, 0.74, 0.50}, {0.66, 0.80, 0.54}, {0.86, 0.87, 0.60}, {0.92, 0.80, 0.56}, {0.83, 0.64, 0.45}, {0.70, 0.52, 0.40}, {0.75, 0.70, 0.68}, {0.97, 0.97, 0.97}};
	const int N = int(sizeof(At) / sizeof(At[0]));
	if (H <= At[0]) return Col[0];
	for (int I = 1; I < N; ++I)
		if (H < At[I]) return Mix(Col[I - 1], Col[I], (H - At[I - 1]) / (At[I] - At[I - 1]));
	return Col[N - 1];
}
// The contours' spacing (m) for metres a pixel: about one every 12 pixels of a 1 in 10 slope.
double ContourStep(double Mpp)
{
	for (double St : {10.0, 20.0, 50.0, 100.0, 200.0})
		if (St >= Mpp * 0.35) return St;
	return 500.0;
}

// A line on the picture, its edges smoothed: Width pixels wide, Col blended in.
void Stroke(std::vector<uint8_t>& Bgra, int W, int H, double Ax, double Ay, double Bx, double By, double Width, const Rgb& Col)
{
	const double Half = Width * 0.5;
	const int X0 = std::max(0, int(std::floor(std::min(Ax, Bx) - Half - 1.0))), X1 = std::min(W - 1, int(std::ceil(std::max(Ax, Bx) + Half + 1.0)));
	const int Y0 = std::max(0, int(std::floor(std::min(Ay, By) - Half - 1.0))), Y1 = std::min(H - 1, int(std::ceil(std::max(Ay, By) + Half + 1.0)));
	if (X0 > X1 || Y0 > Y1) return;
	const double Dx = Bx - Ax, Dy = By - Ay, L2 = Dx * Dx + Dy * Dy;
	for (int Y = Y0; Y <= Y1; ++Y)
		for (int X = X0; X <= X1; ++X)
		{
			const double Px = X + 0.5 - Ax, Py = Y + 0.5 - Ay;
			const double T = L2 > 1e-9 ? std::max(0.0, std::min(1.0, (Px * Dx + Py * Dy) / L2)) : 0.0;
			const double Ex = Px - T * Dx, Ey = Py - T * Dy;
			const double Cover = std::max(0.0, std::min(1.0, Half + 0.5 - std::sqrt(Ex * Ex + Ey * Ey)));
			if (Cover <= 0.0) continue;
			uint8_t* P = &Bgra[(size_t(Y) * size_t(W) + size_t(X)) * 4];
			P[0] = Byte(P[0] / 255.0 + (Col.B - P[0] / 255.0) * Cover);
			P[1] = Byte(P[1] / 255.0 + (Col.G - P[1] / 255.0) * Cover);
			P[2] = Byte(P[2] / 255.0 + (Col.R - P[2] / 255.0) * Cover);
		}
}

// The road map's roads: casings under all, then the fills, the minor classes first. Zoomed far out,
// only motorways and trunk roads.
void DrawRoads(MapRoads& Roads, const MapView& View, std::vector<uint8_t>& Bgra)
{
	const int W = View.W, H = View.H;
	const Geo TopLeft = View.PlaceAt(-8.0, -8.0), BottomRight = View.PlaceAt(W + 8.0, H + 8.0);
	const double Mpp = View.MetresPerPixel();
	const int Most = Mpp > 900.0 ? 1 : 2;
	const double Grow = std::max(0.55, std::min(1.4, 1.0 + (6.0 - std::log2(std::max(1.0, Mpp))) * 0.12));
	static const Rgb Fill[] = {{0.27, 0.47, 0.84}, {0.22, 0.60, 0.33}, {0.86, 0.33, 0.27}};
	static const double Wide[] = {3.4, 2.8, 2.2};
	std::lock_guard<std::mutex> G(Roads.Lock);
	for (int Pass = 0; Pass < 2; ++Pass)
		for (int Class = Most; Class >= 0; --Class)
		{
			const double Width = Wide[Class] * Grow + (Pass == 0 ? 1.6 : 0.0);
			const Rgb Col = Pass == 0 ? Mix(Fill[Class], {0.1, 0.1, 0.1}, 0.55) : Fill[Class];
			for (const MapRoads::Line& L : Roads.Lines)
			{
				if (L.Class != Class || L.East < TopLeft.Lon || L.West > BottomRight.Lon || L.North < BottomRight.Lat || L.South > TopLeft.Lat) continue;
				P2 Last = View.PixelOf(L.Pts[0].first, L.Pts[0].second);
				for (size_t K = 1; K < L.Pts.size(); ++K)
				{
					const P2 Next = View.PixelOf(L.Pts[K].first, L.Pts[K].second);
					if ((Next.X - Last.X) * (Next.X - Last.X) + (Next.Z - Last.Z) * (Next.Z - Last.Z) < 1.0 && K + 1 < L.Pts.size()) continue;   // (finer than a pixel: skipped)
					Stroke(Bgra, W, H, Last.X, Last.Z, Next.X, Next.Z, Width, Col);
					Last = Next;
				}
			}
		}
}

// [ferries] The listed ferry crossings (Data/RideUK/ferries.json, as the game read it at start) as dashed lines end to
// end, a dot at each end, on every layer, so a route can be planned over them (beenster, Nov 20: "add dashed lines to
// the map to show the locations of the ferry crossings, this helps with planning a route").
void DrawFerries(const MapView& View, std::vector<uint8_t>& Bgra)
{
	const std::shared_ptr<const std::vector<Ferries::Listed>> List = Ferries::ListNow();
	if (!List) return;
	const int W = View.W, H = View.H;
	const Rgb Edge = {0.97, 0.97, 0.97}, Dash = {0.07, 0.20, 0.50};
	const double On = 7.0, Off = 5.0;
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		const double Width = Pass == 0 ? 3.8 : 2.2;
		const Rgb Col = Pass == 0 ? Edge : Dash;
		for (const Ferries::Listed& L : *List)
		{
			const P2 A = View.PixelOf(L.LatA, L.LonA), B = View.PixelOf(L.LatB, L.LonB);
			if (std::max(A.X, B.X) < -10.0 || std::min(A.X, B.X) > W + 10.0 || std::max(A.Z, B.Z) < -10.0 || std::min(A.Z, B.Z) > H + 10.0) continue;
			const double Len = std::hypot(B.X - A.X, B.Z - A.Z);
			for (double D = 0.0; D < Len; D += On + Off)
			{
				const double T0 = D / std::max(Len, 1e-9), T1 = std::min(D + On, Len) / std::max(Len, 1e-9);
				Stroke(Bgra, W, H, A.X + (B.X - A.X) * T0, A.Z + (B.Z - A.Z) * T0, A.X + (B.X - A.X) * T1, A.Z + (B.Z - A.Z) * T1, Width, Col);
			}
			for (const P2& End : {A, B}) Stroke(Bgra, W, H, End.X, End.Z, End.X, End.Z, Width + 2.2, Col);
		}
	}
}
} // namespace

const char* MapImagery::Credit()
{
	return "Satellite: Sentinel-2 cloudless by EOX IT Services GmbH (s2maps.eu; contains modified Copernicus Sentinel data 2024), CC BY-NC-SA 4.0.";
}

std::string MapImagery::Url(const TileKey& Key)
{
	return "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2024_3857/default/g/" + std::to_string(Key.Zoom) + "/" + std::to_string(Key.Y) + "/" + std::to_string(Key.X) + ".jpg";
}

int MapImagery::ZoomFor(const MapView& View)
{
	return std::max(0, std::min(Top, int(std::floor(View.Zoom + 0.3))));
}

std::vector<TileKey> MapImagery::Keys(const MapView& View)
{
	const int Z = ZoomFor(View);
	const int N = 1 << Z;
	const double Scale = std::pow(2.0, double(Z) - View.Zoom) / 256.0;
	const double Left = WorldX(View.Lon, View.Zoom) - View.W * 0.5, Top0 = WorldY(View.Lat, View.Zoom) - View.H * 0.5;
	const int X0 = int(std::floor(Left * Scale)), X1 = int(std::floor((Left + View.W) * Scale));
	const int Y0 = std::max(0, int(std::floor(Top0 * Scale))), Y1 = std::min(N - 1, int(std::floor((Top0 + View.H) * Scale)));
	std::vector<TileKey> Out;
	for (int Y = Y0; Y <= Y1; ++Y)
		for (int X = X0; X <= X1; ++X) Out.push_back({Z, ((X % N) + N) % N, Y});
	return Out;
}

std::string MapImagery::PathOf(const TileKey& Key) const
{
	return Files::Join(Disk, "s2/" + Key.Rel() + ".jpg");
}

bool MapImagery::OnDisk(const TileKey& Key) const
{
	return !Disk.empty() && Files::Exists(PathOf(Key));
}

std::shared_ptr<const MapImagery::Tile> MapImagery::Get(const TileKey& Key)
{
	{
		std::lock_guard<std::mutex> G(Lock);
		auto It = Mem.find(Key);
		if (It != Mem.end()) { It->second.second = ++Tick; return It->second.first; }
	}
	std::string Bytes;
	if (Disk.empty() || !Decode || !Files::Read(PathOf(Key), Bytes) || Bytes.empty()) return nullptr;
	auto T = std::make_shared<Tile>();
	int W = 0, H = 0;
	if (!Decode(Bytes, W, H, T->Rgb) || W <= 0 || W != H || T->Rgb.size() != size_t(W) * size_t(H) * 3) return nullptr;
	T->Size = W;
	std::lock_guard<std::mutex> G(Lock);
	Mem[Key] = {T, ++Tick};
	while (Mem.size() > size_t(Memory))
	{
		auto Oldest = Mem.begin();
		for (auto It = Mem.begin(); It != Mem.end(); ++It)
			if (It->second.second < Oldest->second.second) Oldest = It;
		Mem.erase(Oldest);
	}
	return T;
}

int MapImagery::Fetch(const std::vector<TileKey>& Keys, const std::function<int(const std::string& Url, std::string& Body)>& Getter, int AtOnce)
{
	std::vector<TileKey> Want;
	for (const TileKey& K : Keys)
		if (!OnDisk(K)) Want.push_back(K);
	std::atomic<size_t> Next(0);
	std::atomic<int> Failed(0);
	const auto Work = [&]()
	{
		for (size_t I = Next++; I < Want.size(); I = Next++)
		{
			std::string Body;
			const int Code = Getter ? Getter(Url(Want[I]), Body) : 0;
			// (a JPEG starts FF D8)
			if (Code != 200 || Body.size() < 4 || uint8_t(Body[0]) != 0xFF || uint8_t(Body[1]) != 0xD8 || !Files::Write(PathOf(Want[I]), Body)) ++Failed;
		}
	};
	std::vector<std::thread> Threads;
	for (int T = 0; T < std::max(1, AtOnce) && size_t(T) < Want.size(); ++T) Threads.emplace_back(Work);
	for (std::thread& T : Threads) T.join();
	return Failed.load();
}

double DrawRelief(HeightStore& Heights, const MapView& View, std::vector<uint8_t>& Bgra, MapRoads* Roads, MapImagery* Imagery)
{
	const int W = std::max(1, View.W), H = std::max(1, View.H);
	Bgra.assign(size_t(W) * size_t(H) * 4, 255);
	if (View.Layer == Satellite && Imagery != nullptr)
	{
		// (the pictures, bilinear between their pixels; dark grey where none is on disk yet)
		const int Zi = MapImagery::ZoomFor(View), Ni = 1 << Zi;
		const double Sc = std::pow(2.0, double(Zi) - View.Zoom);
		const double Left = WorldX(View.Lon, View.Zoom) - W * 0.5, Top0 = WorldY(View.Lat, View.Zoom) - H * 0.5;
		std::map<TileKey, std::shared_ptr<const MapImagery::Tile>> Got;
		size_t Had = 0;
		for (int J = 0; J < H; ++J)
			for (int I = 0; I < W; ++I)
			{
				uint8_t* P = &Bgra[(size_t(J) * size_t(W) + size_t(I)) * 4];
				const double Wx = (Left + I + 0.5) * Sc, Wy = (Top0 + J + 0.5) * Sc;
				const int Tx = int(std::floor(Wx / 256.0)), Ty = int(std::floor(Wy / 256.0));
				const TileKey K{Zi, ((Tx % Ni) + Ni) % Ni, Ty};
				auto It = Got.find(K);
				if (It == Got.end()) It = Got.emplace(K, Ty >= 0 && Ty < Ni ? Imagery->Get(K) : nullptr).first;
				const MapImagery::Tile* T = It->second.get();
				if (T == nullptr) { P[0] = 33; P[1] = 31; P[2] = 28; continue; }
				const int S = T->Size;
				const double Px = (Wx / 256.0 - Tx) * S - 0.5, Py = (Wy / 256.0 - Ty) * S - 0.5;
				const int I0 = std::max(0, std::min(S - 1, int(std::floor(Px)))), J0 = std::max(0, std::min(S - 1, int(std::floor(Py))));
				const int I1 = std::min(S - 1, I0 + 1), J1 = std::min(S - 1, J0 + 1);
				const double Fx = std::max(0.0, std::min(1.0, Px - I0)), Fy = std::max(0.0, std::min(1.0, Py - J0));
				for (int Ch = 0; Ch < 3; ++Ch)
				{
					const auto Px3 = [&](int Ii, int Jj) { return double(T->Rgb[(size_t(Jj) * size_t(S) + size_t(Ii)) * 3 + size_t(Ch)]); };
					const double V = (Px3(I0, J0) * (1 - Fx) + Px3(I1, J0) * Fx) * (1 - Fy) + (Px3(I0, J1) * (1 - Fx) + Px3(I1, J1) * Fx) * Fy;
					P[2 - Ch] = uint8_t(std::lround(std::max(0.0, std::min(255.0, V))));
				}
				++Had;
			}
		DrawFerries(View, Bgra);
		return double(Had) / (double(W) * double(H));
	}
	const int Z = View.TileZoom();
	const int N = 1 << Z;
	std::map<TileKey, std::shared_ptr<const HeightTile>> Held;
	const auto TileFor = [&](const TileKey& K) -> const HeightTile*
	{
		auto It = Held.find(K);
		if (It == Held.end()) It = Held.emplace(K, Heights.Has(K) ? Heights.Tile(K) : nullptr).first;
		return It->second.get();
	};
	// Every pixel's height (NaN for none) and whether it is sea.
	std::vector<double> Ht(size_t(W) * size_t(H), std::nan(""));
	std::vector<uint8_t> Sea(size_t(W) * size_t(H), 0);
	const double Scale = std::pow(2.0, double(Z) - View.Zoom) / 256.0;   // world pixels at the view's zoom -> tiles at Z
	const double Left = WorldX(View.Lon, View.Zoom) - W * 0.5, Top = WorldY(View.Lat, View.Zoom) - H * 0.5;
	size_t Known = 0;
	for (int J = 0; J < H; ++J)
	{
		const double TY = (Top + J + 0.5) * Scale;
		const int Ty = int(std::floor(TY));
		if (Ty < 0 || Ty >= N) continue;
		for (int I = 0; I < W; ++I)
		{
			const double TX = (Left + I + 0.5) * Scale;
			const int Tx = int(std::floor(TX));
			const HeightTile* T = TileFor({Z, ((Tx % N) + N) % N, Ty});
			if (!T || T->Rgb.empty()) continue;
			const int S = T->Size;
			// (bilinear between the tile's pixels; at its edge, its own edge pixel)
			const double PX = (TX - Tx) * S - 0.5, PY = (TY - Ty) * S - 0.5;
			const int I0 = std::max(0, std::min(S - 1, int(std::floor(PX)))), J0 = std::max(0, std::min(S - 1, int(std::floor(PY))));
			const int I1 = std::min(S - 1, I0 + 1), J1 = std::min(S - 1, J0 + 1);
			const double FX = std::max(0.0, std::min(1.0, PX - I0)), FY = std::max(0.0, std::min(1.0, PY - J0));
			const double V = (T->At(I0, J0) * (1 - FX) + T->At(I1, J0) * FX) * (1 - FY) + (T->At(I0, J1) * (1 - FX) + T->At(I1, J1) * FX) * FY;
			const size_t At = size_t(J) * size_t(W) + size_t(I);
			Ht[At] = V;
			const int NI = std::max(0, std::min(S - 1, int(std::lround(PX)))), NJ = std::max(0, std::min(S - 1, int(std::lround(PY))));
			Sea[At] = T->IsSea(NI, NJ) ? 1 : 0;
			++Known;
		}
	}
	// Shaded from the north-west and a little above, the slopes exaggerated as maps do when zoomed out.
	const double Mpp = View.MetresPerPixel();
	const double Lift = std::max(1.5, std::min(6.0, 2.0 + std::log2(std::max(1.0, Mpp / 20.0)) * 0.6));
	const double Lx = -0.5, Ly = -0.5, Lz = 0.7071;
	for (int J = 0; J < H; ++J)
		for (int I = 0; I < W; ++I)
		{
			const size_t At = size_t(J) * size_t(W) + size_t(I);
			uint8_t* Px = &Bgra[At * 4];
			const double V = Ht[At];
			Rgb C;
			if (std::isnan(V))
				C = {0.11, 0.12, 0.13};
			else if (Sea[At])
			{
				// (deeper off shore: the sea's floor as the tiles give it)
				C = View.Layer == RoadMap ? PaperSea : Mix({0.62, 0.76, 0.86}, {0.45, 0.62, 0.78}, std::max(0.0, std::min(1.0, -V / 60.0)));
			}
			else
			{
				const auto H2 = [&](int DI, int DJ)
				{
					const int Ii = std::max(0, std::min(W - 1, I + DI)), Jj = std::max(0, std::min(H - 1, J + DJ));
					const double O = Ht[size_t(Jj) * size_t(W) + size_t(Ii)];
					return std::isnan(O) ? V : O;
				};
				const double Dx = (H2(1, 0) - H2(-1, 0)) / (2.0 * Mpp) * Lift, Dy = (H2(0, 1) - H2(0, -1)) / (2.0 * Mpp) * Lift;
				// (the surface's normal is (-Dx, -Dy, 1), y down the picture)
				const double Len = std::sqrt(Dx * Dx + Dy * Dy + 1.0);
				const double Lit = (-Dx * Lx - Dy * Ly + Lz) / Len;
				const double Shade = 0.45 + 0.8 * std::max(0.0, Lit);
				if (View.Layer == RoadMap)
				{
					// (paper, the hills only just shaded)
					const double Soft = 0.93 + 0.09 * std::max(0.0, Lit);
					C = {PaperLand.R * Soft, PaperLand.G * Soft, PaperLand.B * Soft};
				}
				else if (View.Layer == TerrainMap)
				{
					// (tinted by height, lightly shaded, a contour line where the next pixel right or down is in another band)
					const double Soft = 0.78 + 0.3 * std::max(0.0, Lit);
					C = TerrainTint(V);
					C = {C.R * Soft, C.G * Soft, C.B * Soft};
					const double Step = ContourStep(Mpp);
					const double Band = std::floor(V / Step);
					if (std::floor(H2(1, 0) / Step) != Band || std::floor(H2(0, 1) / Step) != Band)
					{
						const bool Index = std::fmod(std::fabs(std::max(Band, std::floor(std::max(H2(1, 0), H2(0, 1)) / Step))) , 5.0) == 0.0;
						C = Mix(C, {0.45, 0.30, 0.18}, Index ? 0.75 : 0.45);
					}
				}
				else
				{
					C = Tint(V);
					C = {C.R * Shade, C.G * Shade, C.B * Shade};
				}
			}
			Px[0] = Byte(C.B);
			Px[1] = Byte(C.G);
			Px[2] = Byte(C.R);
			Px[3] = 255;
		}
	if (View.Layer == RoadMap && Roads != nullptr) DrawRoads(*Roads, View, Bgra);
	DrawFerries(View, Bgra);
	return double(Known) / (double(W) * double(H));
}
} // namespace Route
} // namespace TorqueWorld
