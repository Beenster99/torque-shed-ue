#include "TyreModel.h"

namespace TorqueSim
{
namespace TyreModel
{
TyreParams Make(const std::string& Style, const std::string& Construction, double Profile, double Stiffness,
	double Slide, double WidthMm, double GripTarmac, double GripGravel, double GripGrass)
{
	(void)Style;
	TyreParams P;
	const bool Bias = Construction == "bias";
	P.Construction = Bias ? "bias" : "radial";
	const double Kappa = Bias ? 0.14 : 0.11;
	const double Alpha = Bias ? 0.15 : 0.105;
	P.Camber = Bias ? 0.95 : 0.85;
	const double Relax = Bias ? 0.24 : 0.16;
	P.LoadSens = Bias ? 0.12 : 0.16;
	P.Stiffness = Clamp(Stiffness, 0.4, 2.0);
	P.Slide = Clamp(Slide, 0.4, 0.98);
	P.Profile = Clamp(Profile, 0.5, 2.0);
	P.KappaPeak = Kappa / P.Stiffness;
	P.AlphaPeak = Alpha / P.Stiffness;
	P.Relax = Relax * Lerp(1.25, 0.9, Clamp((P.Stiffness - 0.7) / 0.6, 0.0, 1.0));
	P.Crown = Clamp(WidthMm / 1000.0 * 0.5 * P.Profile, 0.02, 0.16);
	P.GripTarmac = GripTarmac;
	P.GripGravel = GripGravel;
	P.GripGrass = GripGrass;
	ShapeInto(P);
	return P;
}

void ShapeInto(TyreParams& P)
{
	const double Slide = Clamp(P.Slide, 0.4, 0.98);
	P.C = 2.0 - 2.0 * std::asin(Slide) / PI;
	P.B = std::tan(PI / (2.0 * P.C));
}

double Shape(double S, const TyreParams& P) { return std::sin(P.C * std::atan(P.B * S)); }

double StiffnessAtZero(const TyreParams& P) { return P.B * P.C; }

double MuAt(double Mu, double Fz, double Fz0, const TyreParams& P)
{
	const double Over = Fz / std::max(Fz0, 1.0) - 1.0;
	return Mu * Clamp(1.0 - P.LoadSens * Over, 0.72, 1.12);
}

void Combined(double Sx, double Sy, const TyreParams& P, double& OutX, double& OutY)
{
	const double Rho = std::sqrt(Sx * Sx + Sy * Sy);
	if (Rho < 1e-6)
	{
		OutX = OutY = 0.0;
		return;
	}
	const double F = Shape(Rho, P) / Rho;
	OutX = Sx * F;
	OutY = Sy * F;
}
} // namespace TyreModel
} // namespace TorqueSim
