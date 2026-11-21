// [real water stage 9] The made-up tide (WorldTide.h).
#include "WorldTide.h"

#include <cmath>

namespace TorqueWorld
{
namespace Tide
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double M2 = 12.4206 * 3600.0;            // the moon's semi-diurnal tide (s)
constexpr double SpringNeap = 14.7653 * 86400.0;   // new moon to full and back to springs (s)
// A new moon (2000-01-06 18:14 UTC); springs come about two days after one.
constexpr double NewMoon = 947182440.0, SpringLag = 2.0 * 86400.0;
// Dover's high water on a made-up morning (2026-01-01 03:20 UTC); every port's is its lag after it.
constexpr double DoverHigh = 1767237600.0;

struct Port
{
	double Lat, Lon;
	double Half;   // half the spring range (m)
	double Lag;    // hours after Dover's high water
};
// (rounded from the usual pilot book figures; the lags are wrapped to a tide's 12.42 h)
constexpr Port Ports[] = {
	{51.12, 1.32, 3.0, 0.0},      // Dover
	{51.45, 0.75, 2.7, 1.3},      // Thames estuary (Sheerness)
	{51.95, 1.29, 1.8, 0.8},      // Harwich
	{52.47, 1.75, 0.9, -2.8},     // Lowestoft
	{52.80, 0.37, 3.2, -4.6},     // the Wash (King's Lynn)
	{53.63, -0.19, 3.2, -5.2},    // the Humber
	{54.49, -0.61, 2.3, 5.0},     // Whitby
	{55.01, -1.44, 2.2, 4.4},     // the Tyne
	{55.98, -3.17, 2.3, 3.6},     // Leith
	{57.14, -2.08, 1.8, 2.5},     // Aberdeen
	{58.44, -3.09, 1.5, -2.9},    // Wick
	{58.98, -2.96, 1.4, -3.0},    // Kirkwall
	{60.15, -1.14, 0.9, -2.0},    // Lerwick
	{58.21, -6.39, 2.1, -4.3},    // Stornoway
	{57.90, -5.16, 2.4, -4.5},    // Ullapool
	{57.00, -5.83, 2.3, -5.0},    // Mallaig
	{56.41, -5.48, 1.7, -5.3},    // Oban
	{55.68, -5.73, 0.45, 6.0},    // West Kintyre, Gigha (near the tide's still point)
	{55.42, -5.60, 0.9, 1.5},     // Campbeltown
	{55.95, -4.76, 1.5, 1.2},     // the Clyde (Greenock)
	{54.85, -5.80, 1.2, 0.0},     // Larne
	{54.60, -5.90, 1.5, 0.0},     // Belfast
	{53.35, -6.20, 1.8, 0.0},     // Dublin
	{54.65, -3.57, 3.6, 0.4},     // the Solway (Workington)
	{54.15, -4.47, 3.0, 0.1},     // Douglas
	{54.07, -2.86, 4.4, 0.3},     // Morecambe Bay
	{53.40, -3.00, 4.4, 0.2},     // Liverpool
	{53.30, -4.60, 2.4, -0.8},    // Holyhead
	{52.41, -4.08, 2.2, -3.4},    // Aberystwyth
	{51.70, -5.05, 3.2, -5.5},    // Milford Haven
	{51.60, -3.90, 4.3, -4.5},    // Swansea
	{51.50, -2.70, 6.2, -4.0},    // the Severn (Avonmouth)
	{51.20, -4.10, 3.6, -5.0},    // Ilfracombe
	{50.10, -5.50, 2.6, -5.8},    // Penzance
	{50.37, -4.14, 2.4, -5.5},    // Plymouth
	{50.62, -2.45, 1.0, -4.8},    // Weymouth
	{50.90, -1.40, 2.0, -1.5},    // Southampton
	{50.80, -0.10, 3.2, -0.2},    // Brighton
};

// The range and lag here: the nearest ports weighed by the inverse square of their distance (the lags
// as angles round the tide's cycle, so 6 and -6 hours meet).
void Here(double Lat, double Lon, double& Half, double& Lag)
{
	const double Cl = std::cos(Lat * Pi / 180.0);
	double W = 0.0, H = 0.0, C = 0.0, S = 0.0;
	for (const Port& P : Ports)
	{
		const double Dy = (P.Lat - Lat) * 111.0, Dx = (P.Lon - Lon) * 111.0 * Cl;
		const double D2 = Dx * Dx + Dy * Dy + 25.0;   // (km squared; 5 km's worth so a port's own figure holds round it)
		const double Wi = 1.0 / (D2 * D2);
		W += Wi;
		H += Wi * P.Half;
		const double A = 2.0 * Pi * P.Lag * 3600.0 / M2;
		C += Wi * std::cos(A);
		S += Wi * std::sin(A);
	}
	Half = W > 0.0 ? H / W : 1.5;
	Lag = std::atan2(S, C) * M2 / (2.0 * Pi) / 3600.0;
}

// Springs 1.2, neaps 0.75 (of the spring half range's 1, scaled so the mean springs reach about 1.05).
double Spring(double T)
{
	return 0.975 + 0.225 * std::cos(2.0 * Pi * (T - NewMoon - SpringLag) / SpringNeap);
}
} // namespace

double HalfRange(double Lat, double Lon)
{
	double Half = 0.0, Lag = 0.0;
	Here(Lat, Lon, Half, Lag);
	return Half;
}

double Height(double Lat, double Lon, double UnixSeconds)
{
	double Half = 0.0, Lag = 0.0;
	Here(Lat, Lon, Half, Lag);
	const double Since = UnixSeconds - DoverHigh - Lag * 3600.0;
	return Spring(UnixSeconds) * std::cos(2.0 * Pi * Since / M2);
}

double LastHigh(double Lat, double Lon, double UnixSeconds)
{
	double Half = 0.0, Lag = 0.0;
	Here(Lat, Lon, Half, Lag);
	const double Since = UnixSeconds - DoverHigh - Lag * 3600.0;
	const double Back = std::fmod(std::fmod(Since, M2) + M2, M2);   // (since the last high water)
	return Spring(UnixSeconds - Back);
}

double UnixOf(int Year, int DayOfYear, double Hours)
{
	// (days from 1970 to the year's first day, by the Gregorian calendar)
	long long Days = 0;
	for (int Y = 1970; Y < Year; ++Y) Days += (Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0)) ? 366 : 365;
	return double(Days + DayOfYear - 1) * 86400.0 + Hours * 3600.0;
}
} // namespace Tide
} // namespace TorqueWorld
