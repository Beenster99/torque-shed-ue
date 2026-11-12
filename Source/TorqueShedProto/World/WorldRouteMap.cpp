#include "WorldRouteMap.h"

#include "HeightTiles.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

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

double DrawRelief(HeightStore& Heights, const MapView& View, std::vector<uint8_t>& Bgra)
{
	const int W = std::max(1, View.W), H = std::max(1, View.H);
	Bgra.assign(size_t(W) * size_t(H) * 4, 255);
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
				C = Mix({0.62, 0.76, 0.86}, {0.45, 0.62, 0.78}, std::max(0.0, std::min(1.0, -V / 60.0)));
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
				C = Tint(V);
				C = {C.R * Shade, C.G * Shade, C.B * Shade};
			}
			Px[0] = Byte(C.B);
			Px[1] = Byte(C.G);
			Px[2] = Byte(C.R);
			Px[3] = 255;
		}
	return double(Known) / (double(W) * double(H));
}
} // namespace Route
} // namespace TorqueWorld
