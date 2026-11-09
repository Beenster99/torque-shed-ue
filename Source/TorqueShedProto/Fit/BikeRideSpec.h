// How a bike of catalogue parts rides, as Torque Shed's BikeBuild works it out (bike_build.gd
// _compute, _compute_wheels, _compute_brakes, _compute_chain, _compute_mass_spread, _compute_squat
// and Bike.setup's inertia): the frame's wheelbase, rake and centre of mass, the fork's offset and
// the wheels' size giving the trail, the suspension, tyres, brakes, gearbox and rider aids from
// their parts, the mass from every part, and where they sit moving the centre of mass and adding
// to the inertia. Each part's wear, rust and dirt (its health) takes off what Torque Shed takes off.
//
// The engine's torque curve is the caller's (the shed works it out, TorqueShedPower, with every
// engine part's multiplier): Out's TorqueRpm / TorqueNm, IdleRpm, Redline and the peak are left
// as they are.
//
// As new parts with no tune (the factory settings: pressures, sprockets, clickers, aids at their
// factory level, the screen down). Checked against the Godot game itself by SimTest/spec_test.
#pragma once

#include "BikeLayout.h"
#include "../Sim/BikeSpec.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
// Health: slot -> 0.15..1 (BikeBuild.part_health of its wear, rust and dirt); missing slots 1.
// Fuel and oil: litres in the tank (and any auxiliary tank) and the engine, < 0 for full.
struct RideSpecInput
{
	PartsMap Parts;
	std::map<std::string, double> Health;
	double FuelLitres = -1.0, OilLitres = -1.0;
};

// BikeBuild.part_health.
double PartHealth(double Wear, double Rust, double Dirt);

// Fills Out with every number BikeBuild gives that the riding physics uses, but the engine's
// curve. Notes: what's wrong with how the parts suit each other (the trail, the tyres on their
// rims). False if the parts won't load.
bool RideSpec(const BikeLayout& Lay, const RideSpecInput& In, TorqueSim::BikeSpec& Out, std::vector<std::string>* Notes = nullptr);

// The parts as BikeBuild sees them (PartsDB.sanitize_build): every slot that can't be empty filled
// with its stock part when it's empty or holds something that isn't its kind.
PartsMap RideParts(const BikeLayout& Lay, const PartsMap& Parts);
}
