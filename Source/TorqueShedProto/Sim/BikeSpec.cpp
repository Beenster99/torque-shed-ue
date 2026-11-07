#include "BikeSpec.h"

namespace TorqueSim
{
double BikeSpec::OverallRatio(int Gear) const
{
	const int G = std::clamp(Gear, 1, GearCount());
	return Primary * GearRatios[G - 1] * FinalDrive;
}

double BikeSpec::EngineTorque(double Rpm) const
{
	if (Rpm > Redline * 1.02 || TorqueRpm.empty()) return 0.0;
	if (Rpm <= TorqueRpm.front()) return TorqueNm.front() * Clamp(Rpm / TorqueRpm.front(), 0.3, 1.0);
	for (size_t i = 1; i < TorqueRpm.size(); ++i)
	{
		if (Rpm <= TorqueRpm[i])
		{
			const double T = (Rpm - TorqueRpm[i - 1]) / (TorqueRpm[i] - TorqueRpm[i - 1]);
			return Lerp(TorqueNm[i - 1], TorqueNm[i], T);
		}
	}
	return TorqueNm.back();
}

double BikeSpec::SpringForce(double Rate, double Q, double Travel, double Progression, double Pre)
{
	const double X = Q / std::max(Travel, 0.01);
	return Rate * (Q + Pre + Progression * Q * X * X);
}

V3 BikeSpec::RearAxleAt(double Q) const
{
	const V3 Arm = V3(0.0, 0.0, Wheelbase * 0.5) - SwingPivot;
	return SwingPivot + Arm.Rotated(RIGHT, -Q / std::max(SwingLength, 0.2));
}

V3 BikeSpec::RearPathAt(double Q) const
{
	const V3 Arm = RearAxleAt(Q) - SwingPivot;
	return V3(0.0, Arm.z, -Arm.y).Normalized();
}

V3 BikeSpec::ChainPullAt(const V3& Axle, bool Top) const
{
	const V3 Countershaft(0.0, 0.03, -0.075);
	const V3 Front = SwingPivot + Countershaft;
	const V3 D = Axle - Front;
	const double Dist = std::max(D.Length(), 0.1);
	const V3 Dn = D / Dist;
	const V3 Side = V3(0.0, Dn.z, -Dn.y) * (Top ? 1.0 : -1.0);
	const double S = Clamp(-(SprocketRRear - SprocketRFront) / Dist, -0.9, 0.9);
	const V3 M = Side * std::sqrt(1.0 - S * S) + Dn * S;
	return -((Axle + M * SprocketRRear) - (Front + M * SprocketRFront)).Normalized();
}

double BikeSpec::TyreGrip(bool Front, int Surf) const
{
	const TyreParams& T = Front ? TyreFront : TyreRear;
	switch (Surf)
	{
	case SURF_GRAVEL: return T.GripGravel;
	case SURF_GRASS: return T.GripGrass;
	default: return T.GripTarmac;
	}
}

void BikeSpec::Finish()
{
	TotalMass = BikeMass + RiderMass;
	const double Rake = DegToRad(RakeDeg);
	// BikeBuild.trail_of: where the steering axis meets the road, ahead of the contact patch.
	Trail = Clamp((FrontRadius * std::sin(Rake) - ForkOffset) / std::cos(Rake), -0.05, 0.4);
	// A telescopic fork's axle slides up its legs, parallel to the steering axis.
	ForkPath = V3(0.0, std::cos(Rake), std::sin(Rake));
	// The swingarm pivots a little above the axle line, SwingLength ahead of the back axle.
	const double Drop = 0.08;
	SwingPivot = V3(0.0, Drop, Wheelbase * 0.5 - std::sqrt(SwingLength * SwingLength - Drop * Drop));
	// Rough rider-plus-bike inertia, as Bike.setup.
	const double M = TotalMass, Wb = Wheelbase;
	Inertia = V3(M * Wb * Wb * 0.16, M * Wb * Wb * 0.13, M * 0.11);
	SteerRate = 4.0 * BarWidthMm / 720.0 * Clamp(1.0 - 0.3 * (120.0 - 160.0) / 160.0, 0.85, 1.12);
}

BikeSpec BikeSpec::SpeedTwin1200()
{
	// The 2025 Triumph Speed Twin 1200, from published figures (approximate: check against the
	// real bike and its handbook): 1200 cc 270 degree parallel twin, about 105 PS at 7750 rpm and
	// 112 N m at 4250 rpm, 216 kg wet, 1413 mm wheelbase, 22.8 degree rake, 93.5 mm trail,
	// 120/70 ZR17 and 160/60 ZR17 tyres, 120 mm of travel at each end, cornering ABS and
	// traction control. Tyres use the Godot catalogue's road radials (sport_road_120 and _160).
	BikeSpec S;
	S.TorqueRpm = {1000, 2000, 3000, 4250, 5500, 6500, 7750, 8500};
	S.TorqueNm = {68, 86, 100, 112, 108, 104, 96, 80};
	S.TyreFront = TyreModel::Make("road", "radial", 0.9, 1.05, 0.78, 120.0, 1.32, 0.72, 0.6);
	S.TyreRear = TyreModel::Make("road", "radial", 0.9, 1.05, 0.78, 160.0, 1.32, 0.72, 0.6);
	S.Finish();
	return S;
}
} // namespace TorqueSim
