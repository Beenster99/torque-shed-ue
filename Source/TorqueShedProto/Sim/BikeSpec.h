// The numbers a ridden bike needs: the Unreal side of the Godot game's BikeBuild, reduced to what
// the riding physics reads. One bike for the prototype: the 2025 Triumph Speed Twin 1200.
#pragma once

#include "SimMath.h"
#include "TyreModel.h"
#include <vector>

namespace TorqueSim
{
struct BikeSpec
{
	// Mass and layout (bike space: +Y up, -Z forward, origin at axle height midway between the wheels).
	double BikeMass = 216.0;          // kg, wet
	double RiderMass = 75.0;          // BikeBuild.RIDER_MASS
	double TotalMass = 291.0;
	double Wheelbase = 1.413;
	double ComHeight = 0.62;          // centre of mass above the ground, with the rider
	V3 ComOffset;                     // extra shift of the centre of mass (BikeBuild.com_offset)
	V3 Inertia;                       // principal inertia (pitch about x, yaw about y, roll about z), kg m^2
	double RakeDeg = 22.8;
	double ForkOffset = 0.030;
	double Trail = 0.094;             // worked out in Finish()
	double SeatHeight = 0.81;
	double CdA = 0.60;
	double LeanClearance = DegToRad(42.0);
	double Flick = 1.0;               // BikeBuild.flick: 1 for a classic
	double BarWidthMm = 760.0;
	double SteerRate = 4.0;           // rad/s the rider moves the bars' aim

	// Steering head.
	double SteerMass = 13.0;          // the front end's mass out on the offset
	double SteerInertia = 0.45;
	double DamperDamping = 0.0;
	double DamperFriction = 0.0;

	// Suspension.
	double FrontTravel = 0.120, FrontSpring = 22000.0, FrontBump = 1500.0, FrontRebound = 2300.0;
	double FrontProgression = 0.2, FrontPreload = 0.008, FrontKnee = 1.0;
	double RearTravel = 0.120, RearSpring = 34000.0, RearBump = 2700.0, RearRebound = 3900.0;
	double RearProgression = 0.1, RearPreload = 0.008, RearKnee = 1.0;
	V3 ForkPath;                      // way the front axle slides up the legs (unit, bike space)
	double ForkLink = 0.0;
	V3 SwingPivot;
	double SwingLength = 0.58;
	double SwingDrop = 0.08;           // how far the pivot sits above the axle line
	V3 Countershaft{0.0, 0.03, -0.075};   // the gearbox sprocket, from the swingarm pivot
	double SprocketRFront = 0.0457, SprocketRRear = 0.1037;

	// Wheels and tyres.
	double FrontRadius = 0.300, RearRadius = 0.312;
	double FrontInertia = 0.72, RearInertia = 1.0;
	TyreParams TyreFront, TyreRear;
	double WetGrip = 1.0;             // the weather: wet roads take about a third of the grip away
	double Rolling = 0.015;

	// Brakes: the torque a full squeeze asks for at the wheel (N m).
	double FrontBrakeTorque = 1500.0, RearBrakeTorque = 520.0;
	bool AbsFront = true, AbsRear = true;
	bool TractionControl = true;
	double TractionStrength = 1.0;
	// The ECU's other rider aids, at how firmly they step in (0: not fitted, or off; 1 the factory
	// level): anti-wheelie, lean-angle traction control and stability control.
	double AntiWheelie = 0.0, LeanTraction = 0.0, Stability = 0.0;

	// Engine and gearbox.
	std::vector<double> TorqueRpm;    // full-throttle crank torque curve, rpm ...
	std::vector<double> TorqueNm;     // ... and N m at each
	double IdleRpm = 950.0, Redline = 8500.0, PeakTorqueRpm = 4250.0, PeakTorque = 112.0;
	double Primary = 1.9, FinalDrive = 41.0 / 18.0;
	std::vector<double> GearRatios{2.6, 1.95, 1.6, 1.36, 1.19, 1.06};
	double ShiftTime = 0.1;
	double DrivetrainEfficiency = 0.92;

	int GearCount() const { return (int)GearRatios.size(); }
	double OverallRatio(int Gear) const;
	double EngineTorque(double Rpm) const;
	static double SpringForce(double Rate, double Q, double Travel, double Progression, double Pre);
	V3 RearAxleAt(double Q) const;
	V3 RearPathAt(double Q) const;
	V3 ChainPullAt(const V3& Axle, bool Top) const;
	double TyreGrip(bool Front, int Surface) const;

	// Works out the derived numbers (trail, inertia, pivots) once the inputs are set.
	void Finish();

	static BikeSpec SpeedTwin1200();
};

// What the ground under a tyre is. Matches the Godot game's surfaces that the prototype has.
enum Surface : int { SURF_TARMAC = 0, SURF_GRAVEL = 1, SURF_GRASS = 2, SURF_GRID = 3 };   // grid: a cattle grid's steel bars
} // namespace TorqueSim
