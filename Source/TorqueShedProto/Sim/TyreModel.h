// A motorcycle tyre as a simplified Pacejka "Magic Formula" with combined slip.
// Port of the Godot game's scripts/tyre_model.gd, function for function.
#pragma once

#include "SimMath.h"
#include <string>

namespace TorqueSim
{
struct TyreParams
{
	std::string Construction = "radial";
	double Profile = 1.0;      // crown radius as a share of half the section width
	double Stiffness = 1.0;
	double Slide = 0.8;        // grip left once it slides, share of the peak
	double KappaPeak = 0.11;   // slip ratio at peak grip
	double AlphaPeak = 0.105;  // slip angle at peak grip (rad)
	double Camber = 0.85;      // camber thrust, N per N of load per radian
	double Relax = 0.16;       // relaxation length (m)
	double LoadSens = 0.16;
	double Crown = 0.06;       // crown radius (m)
	double B = 0.0, C = 0.0;   // Magic Formula shape
	double GripTarmac = 1.2;   // peak friction on dry tarmac (parts.json "grip")
	double GripGravel = 0.72;
	double GripGrass = 0.62;
};

namespace TyreModel
{
constexpr double MAX_SLIP_ANGLE = 80.0 * Pi / 180.0;

// Fills the derived numbers from the catalogue's: construction, profile, stiffness, slide and width (mm).
TyreParams Make(const std::string& Style, const std::string& Construction, double Profile, double Stiffness,
	double Slide, double WidthMm, double GripTarmac, double GripGravel, double GripGrass);
void ShapeInto(TyreParams& P);
double Shape(double S, const TyreParams& P);
double StiffnessAtZero(const TyreParams& P);
double MuAt(double Mu, double Fz, double Fz0, const TyreParams& P);
// Combined-slip force shares (fx, fy) of mu * load for normalised slips sx and sy.
void Combined(double Sx, double Sy, const TyreParams& P, double& OutX, double& OutY);
} // namespace TyreModel
} // namespace TorqueSim
