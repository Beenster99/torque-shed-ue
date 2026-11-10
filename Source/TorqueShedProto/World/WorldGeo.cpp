#include "WorldGeo.h"

#include <algorithm>
#include <cstdlib>

namespace TorqueWorld
{
void Rect::Merge(const Rect& O)
{
	if (Empty()) { *this = O; return; }
	if (O.Empty()) return;
	X0 = std::min(X0, O.X0);
	Z0 = std::min(Z0, O.Z0);
	X1 = std::max(X1, O.X1);
	Z1 = std::max(Z1, O.Z1);
}

MetresPerDeg MetresPerDegree(double LatDeg)
{
	const double P = LatDeg * WorldPi / 180.0;
	MetresPerDeg M;
	// (held to a float's precision, as the Godot game holds them, so both games place a point alike to the last bit)
	M.Lat = double(float(111132.92 - 559.82 * std::cos(2.0 * P) + 1.175 * std::cos(4.0 * P)));
	M.Lon = double(float(111412.84 * std::cos(P) - 93.5 * std::cos(3.0 * P) + 0.118 * std::cos(5.0 * P)));
	return M;
}

CellId CellOf(double Lat, double Lon)
{
	return {int(std::floor((Lat + 90.0) / CellDeg)), int(std::floor((Lon + 180.0) / CellDeg))};
}

Geo CellMiddle(CellId C)
{
	return {(C.Row + 0.5) * CellDeg - 90.0, (C.Col + 0.5) * CellDeg - 180.0};
}

std::vector<CellId> CellsInBox(double South, double West, double North, double East)
{
	const CellId A = CellOf(South, West), B = CellOf(North - 1e-9, East - 1e-9);
	std::vector<CellId> Out;
	for (int I = A.Row; I <= B.Row; ++I)
	{
		for (int J = A.Col; J <= B.Col; ++J) Out.push_back({I, J});
	}
	return Out;
}

bool ParseCell(const std::string& Name, CellId& Out)
{
	const size_t Bar = Name.find('_');
	if (Bar == std::string::npos || Bar == 0 || Bar + 1 >= Name.size()) return false;
	for (size_t I = 0; I < Name.size(); ++I)
	{
		if (I != Bar && (Name[I] < '0' || Name[I] > '9')) return false;
	}
	if (Name.size() > 12) return false;
	Out.Row = std::atoi(Name.substr(0, Bar).c_str());
	Out.Col = std::atoi(Name.substr(Bar + 1).c_str());
	return Out.Valid();
}

double MercatorX(double Lon, int Zoom)
{
	return (Lon + 180.0) / 360.0 * double(1 << Zoom);
}

double MercatorY(double Lat, int Zoom)
{
	const double S = std::sin(std::max(-85.05, std::min(85.05, Lat)) * WorldPi / 180.0);
	return (0.5 - std::log((1.0 + S) / (1.0 - S)) / (4.0 * WorldPi)) * double(1 << Zoom);
}

double LatOfRow(double Y, int Zoom)
{
	return std::atan(std::sinh(WorldPi * (1.0 - 2.0 * Y / double(1 << Zoom)))) * 180.0 / WorldPi;
}

int ZoomFor(double Lat, double Spacing, int Size)
{
	const double C = 40075016.686 * std::cos(Lat * WorldPi / 180.0);
	const int Z = int(std::ceil(std::log(C / (Size * Spacing)) / std::log(2.0)));
	return std::max(0, std::min(15, Z));
}

GeoBox TileBox(const TileKey& K)
{
	const double Scale = double(1 << K.Zoom);
	return {LatOfRow(K.Y + 1, K.Zoom), K.X / Scale * 360.0 - 180.0, LatOfRow(K.Y, K.Zoom), (K.X + 1) / Scale * 360.0 - 180.0};
}

std::vector<CellId> CellsUnder(const TileKey& K)
{
	const GeoBox B = TileBox(K);
	return CellsInBox(B.South, B.West, B.North, B.East);
}
} // namespace TorqueWorld
