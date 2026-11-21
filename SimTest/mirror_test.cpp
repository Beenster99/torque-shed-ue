// [mirrors] The mirror aim the ridden bike uses (Fit/MirrorAim.h): for eyes and mirrors all over the place, the
// turned glass must show the eye the direction asked for, the ball joint must stay put, and a head never turns
// further than its ball allows.
#include "MirrorAim.h"
#include <cstdio>
#include <random>

namespace
{
struct V { double x, y, z; };
V Turned(const V& P, const TorqueFit::MirrorTurn& T)
{
	// Rodrigues: P about the axis by the angle.
	const double C = std::cos(T.Angle), S = std::sin(T.Angle), D = (T.Ax * P.x + T.Ay * P.y + T.Az * P.z) * (1.0 - C);
	return {P.x * C + (T.Ay * P.z - T.Az * P.y) * S + T.Ax * D, P.y * C + (T.Az * P.x - T.Ax * P.z) * S + T.Ay * D,
		P.z * C + (T.Ax * P.y - T.Ay * P.x) * S + T.Az * D};
}
}

int main()
{
	std::mt19937 Rng(166);
	std::uniform_real_distribution<double> U(-1.0, 1.0);
	int Failures = 0, Clamped = 0, Cases = 0;
	double Worst = 0.0;
	for (int I = 0; I < 4000; ++I)
	{
		// A rider's eye about 1.25 m up, a mirror 35 to 75 cm ahead, 25 to 45 cm out and 15 to 45 cm lower, drawn
		// than the eye.
		const double SideSign = I % 2 ? 1.0 : -1.0;
		const V Eye{-10.0 + 8.0 * U(Rng), 3.0 * U(Rng), 125.0 + 10.0 * U(Rng)};
		const V G{Eye.x + 55.0 + 20.0 * U(Rng), SideSign * (35.0 + 10.0 * U(Rng)), Eye.z - 30.0 - 15.0 * U(Rng)};
		const V W{-1.0, SideSign * 0.10, -0.03};
		// As the files draw it: aimed for a guessed eye, up to 25 cm from the real one, either way round.
		const V Guess{Eye.x + 10.0 * U(Rng), Eye.y + 5.0 * U(Rng), Eye.z + 25.0 * U(Rng)};
		V N{Guess.x - G.x, Guess.y - G.y, Guess.z - G.z};
		const double Gl = std::sqrt(N.x * N.x + N.y * N.y + N.z * N.z), Wn = std::sqrt(W.x * W.x + W.y * W.y + W.z * W.z);
		N = {N.x / Gl + W.x / Wn, N.y / Gl + W.y / Wn, N.z / Gl + W.z / Wn};
		if (I % 3 == 0) N = {-N.x, -N.y, -N.z};
		const double Ball = 1.4;
		const TorqueFit::MirrorTurn T = TorqueFit::MirrorAim(G.x, G.y, G.z, N.x, N.y, N.z, Eye.x, Eye.y, Eye.z, W.x, W.y, W.z, Ball);
		++Cases;
		if (T.Angle < 0.0 || T.Angle > 0.35 + 1e-9) { ++Failures; std::printf("FAIL case %d: turn %.3f rad is outside what the ball allows\n", I, T.Angle); continue; }
		if (T.Angle > 0.35 - 1e-9) { ++Clamped; continue; }
		// The turned glass: its middle swings about the ball, its normal turns with it.
		const V Rel = Turned({G.x - T.PivotX, G.y - T.PivotY, G.z - T.PivotZ}, T);
		const V G2{T.PivotX + Rel.x, T.PivotY + Rel.y, T.PivotZ + Rel.z};
		const V N2 = Turned({T.Nx, T.Ny, T.Nz}, T);
		const double Back = std::sqrt(Rel.x * Rel.x + Rel.y * Rel.y + Rel.z * Rel.z);
		if (std::fabs(Back - Ball) > 1e-6) { ++Failures; std::printf("FAIL case %d: the glass came off its ball (%.4f cm)\n", I, Back); continue; }
		// What the eye now sees in it: the line from the eye to the glass, bounced off the glass.
		V In{G2.x - Eye.x, G2.y - Eye.y, G2.z - Eye.z};
		const double L = std::sqrt(In.x * In.x + In.y * In.y + In.z * In.z);
		In = {In.x / L, In.y / L, In.z / L};
		const double Dn = In.x * N2.x + In.y * N2.y + In.z * N2.z;
		if (Dn >= 0.0) { ++Failures; std::printf("FAIL case %d: the glass faces away from the eye\n", I); continue; }
		const V Out{In.x - 2.0 * Dn * N2.x, In.y - 2.0 * Dn * N2.y, In.z - 2.0 * Dn * N2.z};
		const double Wl = std::sqrt(W.x * W.x + W.y * W.y + W.z * W.z);
		const double Cos = (Out.x * W.x + Out.y * W.y + Out.z * W.z) / Wl;
		const double Off = std::acos(std::fmin(1.0, Cos)) * 57.29578;
		Worst = std::fmax(Worst, Off);
		// (The glass's middle moves a few mm as the head swings on its ball, which shifts the view a touch.)
		if (Off > 1.5 || Out.x > -0.9) { ++Failures; std::printf("FAIL case %d: the mirror shows %.2f degrees away from the road behind\n", I, Off); }
	}
	if (Clamped > Cases / 10) { ++Failures; std::printf("FAIL %d of %d mirrors needed more turn than the ball allows\n", Clamped, Cases); }
	std::printf("%d mirror aims, worst %.2f degrees off the view asked for, %d at the ball's limit, %d failures\n", Cases, Worst, Clamped, Failures);
	return Failures ? 1 : 0;
}
