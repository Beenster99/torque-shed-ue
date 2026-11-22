#pragma once

#include <algorithm>
#include <cmath>

// [mirrors] Aiming a mirror from the rider's own eyes (beenster, build 166: "the mirrors just show your hands,
// can you please align all mirrors so they look back behind the rider?"). The bike files aim each glass at a
// guessed eye; the rider's real head depends on the bike, its bars, the avatar and the first person setting, so
// the game turns each head itself. All in one frame, any unit. A glass shows the eye the direction W when its
// normal halves the angle between the line back to the eye and W.
namespace TorqueFit
{
struct MirrorTurn
{
	double Nx = 0.0, Ny = 0.0, Nz = 0.0;   // the glass's normal as drawn, turned to face the eye
	double Ax = 0.0, Ay = 0.0, Az = 1.0;   // the turn that aims it: about this axis...
	double Angle = 0.0;                    // ...by this much (radians, never more than MaxAngle)
	double PivotX = 0.0, PivotY = 0.0, PivotZ = 0.0;   // the ball joint the head turns on: BallBack behind the glass's middle
};

// G: the glass's middle. N: its normal as drawn (either way round). E: the eye. W: what the eye should see in it
// (a direction away from the bike, e.g. straight back and a little out). BallBack: how far behind the glass the
// head's ball joint is, in the frame's unit. MaxAngle: a head only turns so far on its ball (20 degrees: every
// mirror's arm is measured clear of its glass that far round, BikesV2/foundry/mirror_check.py).
inline MirrorTurn MirrorAim(double Gx, double Gy, double Gz, double Nx, double Ny, double Nz, double Ex, double Ey, double Ez,
	double Wx, double Wy, double Wz, double BallBack, double MaxAngle = 0.35)
{
	MirrorTurn T;
	auto Unit = [](double& X, double& Y, double& Z) {
		const double L = std::sqrt(X * X + Y * Y + Z * Z);
		if (L < 1e-12) return false;
		X /= L; Y /= L; Z /= L;
		return true;
	};
	double Vx = Gx - Ex, Vy = Gy - Ey, Vz = Gz - Ez;   // from the eye to the glass
	if (!Unit(Nx, Ny, Nz) || !Unit(Vx, Vy, Vz) || !Unit(Wx, Wy, Wz)) return T;
	if (Nx * Vx + Ny * Vy + Nz * Vz > 0.0) { Nx = -Nx; Ny = -Ny; Nz = -Nz; }   // (facing the eye)
	T.Nx = Nx; T.Ny = Ny; T.Nz = Nz;
	T.PivotX = Gx - Nx * BallBack; T.PivotY = Gy - Ny * BallBack; T.PivotZ = Gz - Nz * BallBack;
	double Mx = Wx - Vx, My = Wy - Vy, Mz = Wz - Vz;   // the normal that shows W
	if (!Unit(Mx, My, Mz)) return T;
	double Cx = Ny * Mz - Nz * My, Cy = Nz * Mx - Nx * Mz, Cz = Nx * My - Ny * Mx;
	const double Sin = std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz), Cos = Nx * Mx + Ny * My + Nz * Mz;
	if (Sin < 1e-9) return T;
	T.Ax = Cx / Sin; T.Ay = Cy / Sin; T.Az = Cz / Sin;
	T.Angle = std::min(std::atan2(Sin, Cos), MaxAngle);
	return T;
}
} // namespace TorqueFit
