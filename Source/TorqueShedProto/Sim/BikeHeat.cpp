#include "BikeHeat.h"
#include "BikeSim.h"
#include <algorithm>
#include <cmath>

namespace TorqueSim
{
namespace
{
constexpr double KELVIN = 273.15;
constexpr double OVERHEAT_SPAN = 45.0;   // °C past the window over which an overcooked tyre goes greasy
constexpr double OVERHEAT_LOSS = 0.25;   // ... losing this much grip
constexpr double OFF_ROAD_SHARE = 0.4;   // on dirt and grass the compound's temperature matters this much
constexpr double DISC_AIRFLOW = 10.0;    // W/m²K per (m/s)^0.8: the air riding past cools a disc
constexpr double SOFTEN = 0.75;          // how much of the effects Riding realism 1 takes away
// The engine: liquid-cooled by coolant temperature, air-cooled by head temperature, °C.
struct EngineTemps { double Warm, Hot, LossFrom, LossSpan, Running; };
constexpr EngineTemps LIQUID{60.0, 108.0, 112.0, 23.0, 85.0};
constexpr EngineTemps AIRC{70.0, 200.0, 210.0, 50.0, 120.0};
constexpr double THERMOSTAT = 78.0, FAN_ON = 102.0, FAN_OFF = 96.0;
constexpr double OIL_WARM = 60.0;        // °C the oil has to reach for full power
constexpr double COLD_POWER = 0.9;       // share of the power a stone cold engine makes
constexpr double FAST_IDLE = 0.35;       // how far over its idle a cold engine runs on the choke
constexpr double SIGMA = 5.67e-8;

double C01(double X) { return std::min(std::max(X, 0.0), 1.0); }
double Lrp(double A, double B, double T) { return A + (B - A) * T; }
} // namespace

void BikeHeat::Setup(const BikeSpec& Spec)
{
	Parts = Spec.Heat;
	Air = Road = 15.0;
	WarmThrough();
}

double BikeHeat::Working(int I) const
{
	return Parts.Tyre[I].Lo + 0.75 * (Parts.Tyre[I].Hi - Parts.Tyre[I].Lo);
}

void BikeHeat::WarmThrough()
{
	for (int I = 0; I < 2; ++I)
	{
		Surface[I] = Carcass[I] = Working(I);
		Discs[I] = Pads[I] = std::max(Parts.Brake[I].PadFrom + 60.0, 70.0);
	}
	Engine = Parts.Liquid ? LIQUID.Running : AIRC.Running;
	Oil = Air + (Engine - Air) * Parts.OilFollow;
	Fan = false;
	UpdateEffects(0.5);
}

void BikeHeat::Soak(double T)
{
	for (int I = 0; I < 2; ++I) Surface[I] = Carcass[I] = Discs[I] = Pads[I] = T;
	Engine = Oil = T;
	Fan = false;
	UpdateEffects(0.5);
}

void BikeHeat::Rest(double Seconds, double T)
{
	Air = Road = T;
	double Left = std::min(Seconds, 3.0 * 3600.0);
	while (Left > 0.0)
	{
		const double Dt = std::min(Left, 2.0);
		TyreStep(0, Dt, 0.0, 0.0, 0.0, false);
		TyreStep(1, Dt, 0.0, 0.0, 0.0, false);
		BrakeStep(0, Dt, 0.0, 0.0);
		BrakeStep(1, Dt, 0.0, 0.0);
		EngineStep(Dt, 0.0, 0.0, 0.0, false);
		Left -= Dt;
	}
	if (Seconds > 3.0 * 3600.0) Soak(T);
	UpdateEffects(0.5);
}

void BikeHeat::Step(const BikeSim& Bike, double Delta)
{
	const double V = std::fabs(Bike.Speed);
	for (int I = 0; I < 2; ++I)
	{
		const Wheel& W = Bike.Wheels[I];
		const bool On = W.Contact && !Bike.IsCrashed;
		TyreStep(I, Delta, V, On ? W.PSlip : 0.0, On ? W.PRoll : 0.0, On);
		// The brake's work: the torque it really put through the wheel against its spin (a locked
		// wheel heats the tyre instead), not the lever's full squeeze.
		const double Omega = std::fabs(W.Omega);
		BrakeStep(I, Delta, V, Omega > 0.5 ? W.BrakeUsed * Omega : 0.0);
	}
	EngineStep(Delta, V, Bike.Rpm, Bike.EngineKw, Bike.EngineOn);
	UpdateEffects(Bike.Assist);
}

void BikeHeat::TyreStep(int I, double Dt, double V, double PSlip, double PRoll, bool Touching)
{
	const BikeHeatParts::TyrePart& T = Parts.Tyre[I];
	const double Ts = Surface[I], Tc = Carcass[I];
	const double C = T.Mass * 1600.0;
	const double Cs = C * 0.12, Cc = C - Cs;
	const double Flow = std::pow(V, 0.8);
	const double HTread = (5.0 + 2.5 * Flow) * T.TreadArea * (1.0 + 2.0 * Wetness);
	const double HSide = (4.0 + 1.5 * Flow) * T.SideArea;
	const double GRoad = Touching ? (4.0 + 0.3 * V) * (1.0 + 2.0 * Wetness) : 0.0;
	const double GSc = 14.6 * T.TreadArea / 0.35;   // rubber conducting through about 6 mm of tread
	// Sliding heats the tread; flexing heats the tread and the carcass alike.
	const double Qs = 0.6 * PSlip + 0.45 * PRoll + GSc * (Tc - Ts) - HTread * (Ts - Air) - GRoad * (Ts - Road);
	const double Qc = 0.1 * PSlip + 0.45 * PRoll + GSc * (Ts - Tc) - HSide * (Tc - Air) - 3.0 * (Tc - Air);
	Surface[I] = Ts + Qs * Dt / Cs;
	Carcass[I] = Tc + Qc * Dt / Cc;
}

void BikeHeat::BrakeStep(int I, double Dt, double V, double P)
{
	const BikeHeatParts::BrakePart& B = Parts.Brake[I];
	const double Td = Discs[I], Tp = Pads[I];
	const double Tk = Td + KELVIN, Ta = Air + KELVIN;
	const double Cool = (10.0 + DISC_AIRFLOW * std::pow(V, 0.8)) * B.DiscArea * B.DiscCool * (Td - Air)
		+ 0.5 * SIGMA * B.DiscArea * (Tk * Tk * Tk * Tk - Ta * Ta * Ta * Ta);
	const double Link = 8.0 * (Td - Tp);
	Discs[I] = Td + (0.92 * P - Cool - Link) * Dt / B.DiscC;
	Pads[I] = Tp + (0.08 * P + Link - 1.0 * (Tp - Air)) * Dt / 270.0;
}

void BikeHeat::EngineStep(double Dt, double V, double Rpm, double Kw, bool Running)
{
	const double Disp = std::max(Parts.Displacement, 30.0);
	double P = 0.0;
	if (Running)
	{
		const double Spin = std::max(Rpm, 0.0) / std::max(Parts.IdleRpm, 500.0);
		P = Parts.Head * (1.1 * Disp * std::pow(Spin, 0.6) + (Parts.Liquid ? 350.0 : 120.0) * std::max(Kw, 0.0));
	}
	double Flow = 0.06 + std::min(V / 25.0, 2.6);
	const double Cap = std::max(Parts.CoolingCapacity, 0.2);
	double Ua, EngineC;
	if (Parts.Liquid)
	{
		EngineC = 6000.0 + 12.0 * Disp;
		const double Radiator = Cap * (60.0 + 0.08 * Disp);
		if (Engine > FAN_ON && Running) Fan = true;
		else if (Engine < FAN_OFF || !Running) Fan = false;
		if (Fan) Flow = std::max(Flow, 0.6);
		const double Open = std::min(std::max((Engine - THERMOSTAT) / 10.0, 0.03), 1.0);
		Ua = Radiator * Flow * Open + 4.0;
	}
	else
	{
		EngineC = 1500.0 + 3.0 * Disp;
		Ua = Cap * (10.0 + 0.018 * Disp) * Flow + 1.5;
	}
	Engine += (P - Ua * (Engine - Air)) * Dt / EngineC;
	const double Target = Air + (Engine - Air) * Parts.OilFollow;
	Oil += (Target - Oil) * (1.0 - std::exp(-Dt / 120.0));
}

double BikeHeat::TempGrip(int I) const
{
	const BikeHeatParts::TyrePart& T = Parts.Tyre[I];
	const double Tt = TyreTemp(I);
	if (Tt < T.Lo) return Lrp(T.ColdGrip, 1.0, C01(Tt / std::max(T.Lo, 1.0)));
	if (Tt > T.Hi) return 1.0 - OVERHEAT_LOSS * C01((Tt - T.Hi) / OVERHEAT_SPAN);
	return 1.0;
}

double BikeHeat::TyreGrip(int I, int Surf) const
{
	double G = TempGrip(I);
	if (Surf != SURF_TARMAC && Surf != SURF_GRID && Surf != SURF_STEEL && Surf != SURF_SLICK) G = 1.0 - (1.0 - G) * OFF_ROAD_SHARE;
	return G < 1.0 ? Lrp(G, 1.0, Soften) : G;
}

double BikeHeat::BrakeTempMult(int I) const
{
	const BikeHeatParts::BrakePart& B = Parts.Brake[I];
	const double T = 0.7 * Discs[I] + 0.3 * Pads[I];
	return Lrp(B.PadCold, 1.0, C01((T - 10.0) / std::max(B.PadFrom - 10.0, 1.0)));
}

void BikeHeat::UpdateEffects(double Assist)
{
	Soften = C01(Assist) * SOFTEN;
	for (int I = 0; I < 2; ++I)
	{
		Bite[I] = Lrp(BrakeTempMult(I), 1.0, Soften);
		Grip[I] = TyreGrip(I, SURF_TARMAC);
	}
	const EngineTemps& E = Parts.Liquid ? LIQUID : AIRC;
	const double WarmOil = C01((Oil - 10.0) / (OIL_WARM - 10.0));
	const double Cold = Lrp(COLD_POWER, 1.0, WarmOil);
	const double Hot = 1.0 - 0.3 * C01((Engine - E.LossFrom) / E.LossSpan);
	Power = Lrp(Cold * Hot, 1.0, Soften);
	const double WarmE = C01((Engine - 10.0) / (E.Warm - 10.0));
	Idle = 1.0 + FAST_IDLE * (1.0 - WarmE) * (1.0 - Soften);
}

const char* BikeHeat::TyreState() const
{
	const char* Out = "warm";
	for (int I = 0; I < 2; ++I)
	{
		const double T = TyreTemp(I);
		if (T > Parts.Tyre[I].Hi) return "hot";
		if (TempGrip(I) < 0.97 && T < Parts.Tyre[I].Lo) Out = "cold";
	}
	return Out;
}

const char* BikeHeat::EngineState() const
{
	const EngineTemps& E = Parts.Liquid ? LIQUID : AIRC;
	if (Engine >= E.Hot) return "hot";
	if (Engine < E.Warm) return "cold";
	return "warm";
}
} // namespace TorqueSim
