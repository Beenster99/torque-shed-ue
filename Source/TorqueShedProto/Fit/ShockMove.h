#pragma once

#include <algorithm>
#include <cmath>

// [shocks] A rear shock working as the swingarm moves (beenster: shocks that compress). All in the bike's
// side view (x forward, z up; any unit), angles as atan2(z, x). The shock's top mount stays on the frame;
// its bottom mount rides on the swingarm, turning about the arm's pivot. The body turns about the top mount
// to keep pointing at the bottom one, the rod and bottom eye ride on the bottom mount pointing back at the
// top, and the spring between them is squeezed by what the shock has shortened.
namespace TorqueFit
{
struct ShockPose
{
	double BottomX = 0.0, BottomZ = 0.0;   // the bottom mount now
	double Turn = 0.0;                     // how far the shock has turned from rest (radians)
	double Rest = 0.0, Length = 0.0;       // mount to mount: at rest, and now
};

// Swing: how far the swingarm has turned from rest (the angle of pivot -> axle now, less at rest).
inline ShockPose ShockAt(double PivotX, double PivotZ, double TopX, double TopZ, double BottomX, double BottomZ, double Swing)
{
	ShockPose P;
	const double C = std::cos(Swing), S = std::sin(Swing);
	const double Bx = BottomX - PivotX, Bz = BottomZ - PivotZ;
	P.BottomX = PivotX + Bx * C - Bz * S;
	P.BottomZ = PivotZ + Bx * S + Bz * C;
	const double Rx = BottomX - TopX, Rz = BottomZ - TopZ, Nx = P.BottomX - TopX, Nz = P.BottomZ - TopZ;
	P.Rest = std::hypot(Rx, Rz);
	P.Length = std::hypot(Nx, Nz);
	P.Turn = P.Rest > 1e-9 && P.Length > 1e-9 ? std::atan2(Nz, Nx) - std::atan2(Rz, Rx) : 0.0;
	return P;
}

// The spring's scale along the shock: it runs from A to B along the shock from the top mount at rest; its top end
// stays on the body, its bottom end comes up with the bottom mount. Never squeezed past coil-bound or pulled long.
inline double SpringScale(double A, double B, const ShockPose& P)
{
	const double Span = B - A;
	if (Span <= 1e-9) return 1.0;
	return std::clamp((Span - (P.Rest - P.Length)) / Span, 0.45, 1.15);
}
} // namespace TorqueFit
