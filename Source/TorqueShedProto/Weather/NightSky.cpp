#include "NightSky.h"

#include <algorithm>
#include <cmath>

// (in a named namespace: Unreal's unity build joins this file with others)
namespace TorqueNight::NightMath
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Rad = Pi / 180.0;
double Rev(double Deg) { const double R = std::fmod(Deg, 360.0); return R < 0.0 ? R + 360.0 : R; }
double SinD(double Deg) { return std::sin(Deg * Rad); }
double CosD(double Deg) { return std::cos(Deg * Rad); }
double Atan2D(double Y, double X) { return std::atan2(Y, X) / Rad; }
double AsinD(double V) { return std::asin(V < -1.0 ? -1.0 : V > 1.0 ? 1.0 : V) / Rad; }

// Ecliptic to equatorial (of date), all degrees.
void ToEquatorial(double Lon, double Lat, double Ecl, double& Ra, double& Dec)
{
	const double X = CosD(Lat) * CosD(Lon);
	const double Y = CosD(Lat) * SinD(Lon) * CosD(Ecl) - SinD(Lat) * SinD(Ecl);
	const double Z = CosD(Lat) * SinD(Lon) * SinD(Ecl) + SinD(Lat) * CosD(Ecl);
	Ra = Rev(Atan2D(Y, X));
	Dec = Atan2D(Z, std::sqrt(X * X + Y * Y));
}

void ToHorizon(double HaDeg, double DecDeg, double LatDeg, double& Elevation, double& Bearing)
{
	const double SinAlt = SinD(DecDeg) * SinD(LatDeg) + CosD(DecDeg) * CosD(LatDeg) * CosD(HaDeg);
	Elevation = AsinD(SinAlt);
	// (bearing from north, clockwise: east 90)
	const double Y = -CosD(DecDeg) * SinD(HaDeg);
	const double X = SinD(DecDeg) * CosD(LatDeg) - CosD(DecDeg) * SinD(LatDeg) * CosD(HaDeg);
	Bearing = Rev(Atan2D(Y, X));
}
} // namespace TorqueNight::NightMath

namespace TorqueNight
{
using namespace NightMath;
double DaysFrom2000(double UnixSeconds)
{
	// 1970-01-01 0h UT is JD 2440587.5; Schlyter's day 0 is JD 2451543.5.
	return UnixSeconds / 86400.0 + 2440587.5 - 2451543.5;
}

double SiderealDeg(double UnixSeconds, double LonDeg)
{
	const double Jd = UnixSeconds / 86400.0 + 2440587.5;
	return Rev(280.46061837 + 360.98564736629 * (Jd - 2451545.0) + LonDeg);
}

void HorizonOf(double RaDeg, double DecDeg, double UnixSeconds, double LatDeg, double LonDeg, double& Elevation, double& Bearing)
{
	ToHorizon(SiderealDeg(UnixSeconds, LonDeg) - RaDeg, DecDeg, LatDeg, Elevation, Bearing);
}

Moon MoonAt(double UnixSeconds, double LatDeg, double LonDeg)
{
	const double D = DaysFrom2000(UnixSeconds);
	const double Ecl = 23.4393 - 3.563e-7 * D;
	// The sun.
	const double Ws = 282.9404 + 4.70935e-5 * D;
	const double Es = 0.016709 - 1.151e-9 * D;
	const double Ms = Rev(356.0470 + 0.9856002585 * D);
	const double Eas = Ms + Es / Rad * SinD(Ms) * (1.0 + Es * CosD(Ms));
	const double Xs = CosD(Eas) - Es, Ys = std::sqrt(1.0 - Es * Es) * SinD(Eas);
	const double SunLon = Rev(Atan2D(Ys, Xs) + Ws);
	// The moon's orbit.
	const double N = Rev(125.1228 - 0.0529538083 * D);
	const double I = 5.1454;
	const double W = Rev(318.0634 + 0.1643573223 * D);
	const double A = 60.2666;
	const double E = 0.054900;
	const double M = Rev(115.3654 + 13.0649929509 * D);
	double Ea = M + E / Rad * SinD(M) * (1.0 + E * CosD(M));
	for (int K = 0; K < 6; ++K) Ea = Ea - (Ea - E / Rad * SinD(Ea) - M) / (1.0 - E * CosD(Ea));
	const double Xv = A * (CosD(Ea) - E), Yv = A * std::sqrt(1.0 - E * E) * SinD(Ea);
	const double V = Atan2D(Yv, Xv);
	double R = std::sqrt(Xv * Xv + Yv * Yv);
	const double Xh = R * (CosD(N) * CosD(V + W) - SinD(N) * SinD(V + W) * CosD(I));
	const double Yh = R * (SinD(N) * CosD(V + W) + CosD(N) * SinD(V + W) * CosD(I));
	const double Zh = R * (SinD(V + W) * SinD(I));
	double Lon = Atan2D(Yh, Xh);
	double Lat = Atan2D(Zh, std::sqrt(Xh * Xh + Yh * Yh));
	// The biggest perturbations.
	const double Ls = Ms + Ws, Lm = M + W + N;
	const double Dm = Lm - Ls, F = Lm - N;
	Lon += -1.274 * SinD(M - 2.0 * Dm) + 0.658 * SinD(2.0 * Dm) - 0.186 * SinD(Ms) - 0.059 * SinD(2.0 * M - 2.0 * Dm)
		- 0.057 * SinD(M - 2.0 * Dm + Ms) + 0.053 * SinD(M + 2.0 * Dm) + 0.046 * SinD(2.0 * Dm - Ms) + 0.041 * SinD(M - Ms)
		- 0.035 * SinD(Dm) - 0.031 * SinD(M + Ms) - 0.015 * SinD(2.0 * F - 2.0 * Dm) + 0.011 * SinD(M - 4.0 * Dm);
	Lat += -0.173 * SinD(F - 2.0 * Dm) - 0.055 * SinD(M - F - 2.0 * Dm) - 0.046 * SinD(M + F - 2.0 * Dm)
		+ 0.033 * SinD(F + 2.0 * Dm) + 0.017 * SinD(2.0 * M + F);
	R += -0.58 * CosD(M - 2.0 * Dm) - 0.46 * CosD(2.0 * Dm);
	Lon = Rev(Lon);
	Moon Out;
	ToEquatorial(Lon, Lat, Ecl, Out.RaDeg, Out.DecDeg);
	ToEquatorial(SunLon, 0.0, Ecl, Out.SunRaDeg, Out.SunDecDeg);
	Out.DistanceKm = R * 6378.14;
	// Where it stands from here, lowered by the parallax (we stand an Earth's radius off its centre).
	double Alt = 0.0, Az = 0.0;
	HorizonOf(Out.RaDeg, Out.DecDeg, UnixSeconds, LatDeg, LonDeg, Alt, Az);
	Out.Elevation = Alt - AsinD(1.0 / R) * CosD(Alt);
	Out.Bearing = Az;
	// Its phase.
	Out.Elongation = std::acos(std::max(-1.0, std::min(1.0, CosD(SunLon - Lon) * CosD(Lat)))) / Rad;
	Out.PhaseAngle = 180.0 - Out.Elongation;
	Out.Lit = (1.0 + CosD(Out.PhaseAngle)) * 0.5;
	Out.bWaxing = Rev(Lon - SunLon) < 180.0;
	return Out;
}

double MoonBrightness(double PhaseAngle)
{
	// Allen's phase law, in magnitudes: 0.026 |a| + 4e-9 a^4; next to full.
	const double Aa = std::fabs(PhaseAngle);
	return std::pow(10.0, -0.4 * (0.026 * Aa + 4.0e-9 * Aa * Aa * Aa * Aa));
}
} // namespace TorqueNight
