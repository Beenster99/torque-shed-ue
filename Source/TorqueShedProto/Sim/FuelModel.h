// The petrol an engine burns (bike_build.gd's fuel_rate), plain C++ so the game's dash (TorqueDash)
// and SimTest's realism_test (--calib, --mpg) work it out the same way.
//
// Litres an hour = (the power it makes at the crank + its friction and pumping) x 0.30 L per kWh,
// richer near full throttle, a two-stroke thirstier. Friction and pumping are a mean pressure on the
// pistons (bar) over one power stroke every two turns: (0.9 + 0.13 per 1000 rpm) x the head's thirst
// and the cylinders' drag, plus 0.75 bar of pumping with the throttle shut, less as it opens.
#pragma once

#include <algorithm>

namespace TorqueSim
{
struct FuelModel
{
	static constexpr double PerKwh = 0.30;
	static constexpr double FrictionBar = 0.9;
	static constexpr double FrictionBarPerKrpm = 0.13;
	static constexpr double PumpingBar = 0.75;
	static constexpr double FrictionPerCylinder = 0.08;
	static constexpr double FullThrottleRich = 0.15;
	static constexpr double TwoStrokeThirst = 1.35;

	double Displacement = 700.0;   // cc
	double FrictionMult = 1.0;     // the head's thirst x the cylinders' drag
	bool bTwoStroke = false;

	static double FrictionOf(double HeadThirst, double Cylinders) { return HeadThirst * (1.0 + FrictionPerCylinder * (Cylinders - 2.0)); }

	// Litres an hour making Kw at the crank at Rpm, where the engine could make FullKw flat out.
	double LitresPerHour(double Kw, double Rpm, double FullKw) const
	{
		const double Load = std::clamp(std::max(Kw, 0.0) / std::max(FullKw, 0.1), 0.0, 1.0);
		const double Bar = (FrictionBar + FrictionBarPerKrpm * Rpm / 1000.0) * FrictionMult + PumpingBar * (1.0 - Load);
		const double FrictionKw = Bar * 100000.0 * Displacement / 1000000.0 * Rpm / 120.0 / 1000.0;
		double Litres = (std::max(Kw, 0.0) + FrictionKw) * PerKwh * (bTwoStroke ? TwoStrokeThirst : 1.0);
		Litres *= 1.0 + FullThrottleRich * std::clamp((Load - 0.8) / 0.2, 0.0, 1.0);
		return Litres;
	}

	// UK miles per gallon from km per litre.
	static double UkMpg(double KmPerLitre) { return KmPerLitre / 1.609344 * 4.54609; }
};
} // namespace TorqueSim
