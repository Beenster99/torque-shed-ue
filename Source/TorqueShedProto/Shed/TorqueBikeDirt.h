#pragma once

#include "CoreMinimal.h"

// Where road muck lies on a bike: the same sums as the bike materials' dirt (Tools/ue_bike_dirt.py
// DIRT_HLSL, F.Spray and the "How much is on it here" block), for the wash game (TorqueWashJob), which
// has to know how much muck each point it washes carries, as it's drawn. Change one, change the other.
//
// All in the bike's frame (cm: x forward, y right, z up). A wheel (SprayF, SprayR) is its axle's x and
// z, its tyre's radius and half width (cm), all 0 for none (a loose part, off a bike). Spin (on a
// part that turns with a wheel): its axle's x and z, 1, its axle's y.
namespace TorqueBikeDirt
{
inline double Smooth(double A, double B, double X)
{
	const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

// The muck a wheel throws onto a point facing N (F.Spray): what let go of the tyre where the tangent
// through the point touches it, if the point faces back along that line, as wide as the tread and
// spreading as it goes; beside the wheel, inside its round, the spray off its sides.
inline double Spray(const FVector& P, const FVector& N, const FLinearColor& W)
{
	if (W.B < 1.0f) return 0.0;
	const double Vx = P.X - W.R, Vz = P.Z - W.G;
	const double R = FMath::Sqrt(Vx * Vx + Vz * Vz);
	const double D = R - W.B;
	const double Side = FMath::Abs(P.Y) - W.A;
	if (D < 0.0) return 0.5 * (1.0 - Smooth(4.0, 16.0, Side));
	const double Psi = FMath::Atan2(Vz, -Vx);
	const double Rel = Psi - FMath::Acos(FMath::Clamp(W.B / R, 0.0, 1.0));
	const double RelD = FMath::RadiansToDegrees(Rel);
	const double Let = Smooth(-100.0, -70.0, RelD) * FMath::Lerp(1.0, 0.25, Smooth(15.0, 120.0, RelD)) * (1.0 - Smooth(130.0, 165.0, RelD));
	const double Tx = FMath::Sin(Rel), Tz = FMath::Cos(Rel);
	const double Face = Smooth(-0.05, 0.45, -(N.X * Tx + N.Z * Tz));
	const double Lat = 1.0 - Smooth(0.25 * D, 6.0 + 0.6 * D, Side);
	return Let * Face * Lat * FMath::Exp(-D / 35.0);
}

// How much muck lies at P facing N, for a Grime of 1 (before the cap at 1): DIRT_HLSL's
// max(pat, FlyGain * flyw * 1.6). ZLo: the bottom of the bike's tyres (ZRange.x).
inline float Pattern(const FVector& P, const FVector& N, double ZLo, double ZHi, const FLinearColor& Spin,
	const FLinearColor& SprayF, const FLinearColor& SprayR, float FlyGain)
{
	// A part that turns with a wheel: its muck evenly round it (DIRT_HLSL's mw and nw).
	FVector Pw = P, Nw = N;
	if (Spin.B > 0.5f)
	{
		Pw.Z = Spin.G - 0.5 * FVector2D(P.X - Spin.R, P.Z - Spin.G).Size();
		Nw.Z = 0.0;
		Nw = Nw.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}
	const double Up = (Pw.Z - ZLo) * 0.01;
	double Pat;
	if (SprayF.B > 0.5f || SprayR.B > 0.5f)
	{
		double Muck, Dust = 0.1 + 0.1 * Smooth(0.6, 1.0, Nw.Z);
		if (Spin.B > 0.5f)
		{
			const FLinearColor& W = FMath::Abs(P.X - SprayF.R) < FMath::Abs(P.X - SprayR.R) ? SprayF : SprayR;
			const double R = FMath::Max((double)W.B, 1.0), Hw = FMath::Max((double)W.A, 1.0);
			const double Rr = FVector2D(P.X - Spin.R, P.Z - Spin.G).Size();
			Muck = 0.35 + 0.85 * Smooth(0.4 * R, 0.95 * R, Rr);
			const double Crown = Smooth(0.82 * R, 0.88 * R, Rr) * (1.0 - Smooth(0.85, 0.97, FMath::Abs(N.Y)));
			const double Mid = 1.0 - Smooth(0.6 * Hw, 0.72 * Hw, FMath::Abs(P.Y - Spin.A));
			const double Kept = 1.0 - 0.97 * Crown * Mid;
			Muck *= Kept;
			Dust *= Kept;
		}
		else
		{
			Muck = FMath::Max(Spray(P, N, SprayF), Spray(P, N, SprayR)) * 1.4;
			Muck = FMath::Max(Muck, (1.0 - Smooth(0.08, 0.4, Up)) * (0.55 + 0.45 * FMath::Clamp(0.5 - N.Z, 0.0, 1.0)) * 1.1);
			Muck = FMath::Max(Muck, Smooth(0.5, 0.9, -N.Z) * (1.0 - Smooth(0.3, 0.8, Up)) * 0.7);
		}
		Pat = FMath::Max(Muck, Dust);
	}
	else
	{
		const double H = FMath::Clamp((Pw.Z - ZLo) / FMath::Max(ZHi - ZLo, 1.0), 0.0, 1.0);
		Pat = FMath::Lerp(1.35, 0.4, H * H * (3.0 - 2.0 * H));
		if (Nw.Z < -0.5) Pat *= 1.45;
		else if (Nw.Z > 0.6) Pat *= 0.75;
	}
	const double FlyW = FlyGain > 0.0f ? Smooth(0.45, 0.8, Nw.X) * Smooth(0.12, 0.4, Up) : 0.0;
	return (float)FMath::Max(Pat, FlyGain * FlyW * 1.6);
}
}
