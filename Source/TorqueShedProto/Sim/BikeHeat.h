// Heat on the ridden bike (Torque Shed's bike_heat.gd): each tyre's tread surface and carcass, each
// brake's disc and pads, and the engine (its coolant, or its head if it's air-cooled) and oil.
//
// Every temperature is a lump of mass that heat flows into and out of each step:
//   tyres   heated by sliding (slip speed times the force it carries) on the tread and by flexing as
//           they roll (the rolling resistance) in the carcass; cooled by the air, more the faster they
//           go, and by the road under them. A wet road cools them hard.
//   brakes  the braking work goes into the disc (and a little into the pads); the disc sheds it to
//           the air, more at speed and, red hot, by glowing.
//   engine  friction and pumping heat grow with the revs, and a share of the power made; a radiator
//           behind a thermostat (and a fan when it's hot) or the fins carry it away with the airflow.
//           The oil follows the engine more slowly.
// What it does to the bike, each softened by Riding realism:
//   grip    each tyre compound works in a window: race slicks need real heat, a touring or
//           dual-sport tyre works almost cold, and any tyre goes greasy overcooked.
//   brakes  pads bite a little less cold (race carbon a lot less), and never fade hot.
//   engine  a cold engine idles fast (on the choke) and makes a little less power until the oil is
//           warm; overheating, it loses power.
// (Godot's tyre pressures rising with the heat aren't here: the shed doesn't set pressures yet.)
#pragma once

#include "BikeSpec.h"

namespace TorqueSim
{
class BikeSim;

class BikeHeat
{
public:
	// [front, rear] tyre tread surface and carcass, disc and pad temperatures, °C.
	double Surface[2] = {20.0, 20.0}, Carcass[2] = {20.0, 20.0};
	double Discs[2] = {20.0, 20.0}, Pads[2] = {20.0, 20.0};
	double Engine = 20.0;   // coolant (liquid) or cylinder head (air), °C
	double Oil = 20.0;
	double Air = 15.0;      // the air around the bike, °C: set by the game from the weather and time
	double Road = 15.0;     // the road's surface, °C
	double Wetness = 0.0;   // 0 dry .. 1 soaking (the game's weather)
	bool Fan = false;
	// The effects as they are this step (softened): grip and brake bite per wheel, engine power, idle.
	double Grip[2] = {1.0, 1.0}, Bite[2] = {1.0, 1.0};
	double Power = 1.0, Idle = 1.0, Soften = 0.0;

	void Setup(const BikeSpec& Spec);
	// Everything at the temperatures a bike settles at being ridden (tests, a fresh build); at one
	// temperature through and through (a bike that has stood a long while); or cooled for Seconds
	// parked in air at T.
	void WarmThrough();
	void Soak(double T);
	void Rest(double Seconds, double T);
	// One step, after the wheels have worked out their forces.
	void Step(const BikeSim& Bike, double Delta);

	// A tyre's grip share on a surface from its heat (1 = as built), softened by Riding realism.
	double TyreGrip(int I, int Surf) const;
	double TyreTemp(int I) const { return 0.65 * Surface[I] + 0.35 * Carcass[I]; }
	// "cold", "warm" or "hot", for the HUD: the tyres together, and the engine.
	const char* TyreState() const;
	const char* EngineState() const;
	// One tyre for the telemetry: -1 cold (gripping less), 0 working, 1 overheated.
	int TyreBand(int I) const;
	bool Liquid() const { return Parts.Liquid; }

private:
	BikeHeatParts Parts;
	double TempGrip(int I) const;
	double Working(int I) const;
	double BrakeTempMult(int I) const;
	void TyreStep(int I, double Dt, double V, double PSlip, double PRoll, bool Touching);
	void BrakeStep(int I, double Dt, double V, double P);
	void EngineStep(double Dt, double V, double Rpm, double Kw, bool Running);
	void UpdateEffects(double Assist);
};
} // namespace TorqueSim
