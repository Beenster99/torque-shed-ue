// How quickly each Bike Models V2 bike flicks from one side to the other, as through a chicane (beenster,
// 19 Nov 2026: a friend finds the sport and naked bikes "slow to tip into a corner and especially through
// chicanes"). A measurement, not a pass/fail test: for every V2 bike as the shop sells it, on flat dry tarmac
// at a steady 40, 60 and 90 mph, the rider holds 35 degrees one way, then puts the stick over to hold 35 the
// other way: the time from -30 to +30 degrees, the average and the peak roll rate, and the peak roll rate
// with the stick put fully over instead (what a player slamming it across gets); at Riding realism 0
// (full sim), 0.6 (the game's default) and 1. With the Flick each bike gets and the style it's taken for.
//
//     build/simtest/handling_test [--only STYLE] [--bike ID] [--mph 40|60|90] [--realism 0|0.6|1] [--steer-rate-x K] [--flick-x K]
// (HTRACE=1 in the environment prints each flick as it goes.)
#include "BikeLayout.h"
#include "BikeRideSpec.h"
#include "BikeShop.h"
#include "BikeSim.h"
#include "PartsDb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TorqueFit;
using TorqueSim::BikeSim;
using TorqueSim::BikeSpec;
using TorqueSim::V3;

namespace
{
constexpr double Dt = 1.0 / 120.0;
constexpr double Deg = 57.29577951308232;
constexpr double Mph = 0.44704;

bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::stringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

std::string Root()
{
	for (const char* R : {".", "..", "../..", "../../.."})
	{
		std::ifstream F(std::string(R) + "/Data/Fitment/layout.json");
		if (F) return R;
	}
	return ".";
}

TorqueSim::RayHit FlatGround(const V3& From, const V3& To)
{
	TorqueSim::RayHit H;
	if (From.y < 0.0 || To.y > 0.0) return H;
	H.Hit = true;
	H.Position = From + (To - From) * (From.y / (From.y - To.y));
	H.Normal = V3(0.0, 1.0, 0.0);
	H.Surface = TorqueSim::SURF_TARMAC;
	return H;
}

// The engine as the game works it out for a new bike (TorqueShedPower::Numbers and EngineTorque).
void GameCurve(const BikeLayout& Lay, const PartsMap& Parts, BikeSpec& S)
{
	auto F = [&](const char* Slot, const char* Key, double Def) {
		auto It = Parts.find(Slot);
		if (It == Parts.end() || It->second == "none") return Def;
		const Json* P = Lay.Part(It->second);
		return P ? P->Num(Key, Def) : Def;
	};
	auto There = [&](const char* Slot) { auto It = Parts.find(Slot); return It != Parts.end() && It->second != "none" && Lay.Part(It->second); };
	const double Disp = F("engine", "displacement", 1200.0);
	double Peak = Disp * F("engine", "torque_per_cc", 112.0 / 1200.0);
	const double PeakRpm = F("engine", "peak_torque_rpm", 6000.0) * F("head", "peak_shift", 1.0) * F("fuel", "peak_shift", 1.0);
	const double Redline = F("engine", "redline", 8500.0) + F("head", "redline_add", 0.0);
	const double Idle = F("engine", "idle_rpm", 950.0);
	double Mult = F("head", "torque_mult", 1.0) * F("fuel", "torque_mult", 1.0);
	const double Demand = Disp * F("head", "peak_shift", 1.0) * F("engine", "breathing", 1.0), Flow = F("fuel", "flow", 1300.0);
	if (Flow < Demand) Mult *= std::max(0.7, 1.0 + (Flow / Demand - 1.0) * 0.6);
	Mult *= F("air_filter", "flow", 1.0) * F("spark_plugs", "torque_mult", 1.0);
	Mult *= There("exhaust") ? F("exhaust", "torque_mult", 1.0) : 0.9;
	Peak *= Mult;
	S.IdleRpm = Idle;
	S.Redline = Redline;
	S.PeakTorque = Peak;
	S.PeakTorqueRpm = PeakRpm;
	S.TorqueRpm.clear();
	S.TorqueNm.clear();
	for (int I = 0; I < 25; ++I)
	{
		const double Rpm = Idle + (Redline - Idle) * I / 24.0;
		const double X = (Rpm - PeakRpm) / std::max(1.0, PeakRpm);
		const double Shape = 1.0 - (X < 0.0 ? 0.7 : 1.5) * X * X;
		S.TorqueRpm.push_back(Rpm);
		S.TorqueNm.push_back(Peak * std::clamp(Shape, 0.3, 1.0));
	}
}

// Rides at a steady Speed (m/s) with the lean stick at Lean for Seconds; returns the lean (rad) at the end.
struct Rider
{
	BikeSim Sim;
	double Speed = 0.0, Held = 0.0;
	void Start(const BikeSpec& S, double Assist, double V)
	{
		Sim.Setup(S);
		Sim.Ray = FlatGround;
		Sim.Place(V3(0.0, 0.0, 0.0), 0.0);
		Sim.AutoShift = true;
		Sim.Assist = Assist;
		Speed = V;
		// Up to speed in a straight line (pushed along, so the launch doesn't matter), then held there.
		for (int I = 0; I < 12 * 120 && Sim.Speed < V - 0.5; ++I) Step(0.0);
		for (int I = 0; I < 3 * 120; ++I) Step(0.0);
	}
	void Step(double Lean)
	{
		Held = std::clamp(Held + (Speed - Sim.Speed) * 0.4 * Dt, -0.5, 1.0);
		Sim.Throttle = std::clamp(0.3 + (Speed - Sim.Speed) * 0.5 + Held, 0.0, 1.0);
		Sim.FrontBrake = Sim.Speed > Speed + 2.0 ? 0.2 : 0.0;
		Sim.LeanInput = Lean;
		Sim.Step(Dt);
	}
};

// The stick that holds about 35 degrees at this speed.
double StickFor35(const BikeSpec& S, double V)
{
	double Lo = 0.05, Hi = 1.0;
	for (int K = 0; K < 9; ++K)
	{
		const double Mid = 0.5 * (Lo + Hi);
		Rider R;
		R.Start(S, 0.6, V);
		for (int I = 0; I < 4 * 120; ++I) R.Step(Mid);
		(std::fabs(R.Sim.LeanAngle) * Deg < 35.0 ? Lo : Hi) = Mid;
	}
	return 0.5 * (Lo + Hi);
}

struct Flick
{
	double T = -1.0, Avg = 0.0, Peak = 0.0, FullPeak = 0.0, Over = 0.0;
	bool Crashed = false;
};

// Holds 35 degrees left, then the stick to Stick right: from -30 to +30 degrees, the time and rates.
Flick Measure(const BikeSpec& S, double Assist, double V, double Hold, double Stick)
{
	Flick F;
	Rider R;
	R.Start(S, Assist, V);
	for (int I = 0; I < 4 * 120; ++I) R.Step(-Hold);
	const double Thirty = 30.0 / Deg;
	const double Was = R.Sim.LeanAngle >= 0.0 ? 1.0 : -1.0;   // the side it's leant over to
	double T0 = -1.0;
	for (int I = 0; I < 4 * 120 && !R.Sim.IsCrashed; ++I)
	{
		R.Step(Stick);
		const double L = R.Sim.LeanAngle;
		F.Peak = std::max(F.Peak, std::fabs(R.Sim.LeanRate) * Deg);
		if (T0 < 0.0 && L * Was < Thirty) T0 = I * Dt;
		if (T0 >= 0.0 && F.T < 0.0 && -L * Was >= Thirty) F.T = I * Dt - T0;
		F.Over = std::max(F.Over, -L * Was * Deg);
		if (std::getenv("HTRACE") && I % 6 == 0)
			std::printf("    t %.2f lean %6.1f rate %6.1f steer %6.2f bar %6.1f grip %.2f/%.2f v %.1f crashed %d\n", I * Dt, L * Deg, R.Sim.LeanRate * Deg, R.Sim.Steer * Deg,
				R.Sim.BarTorque, R.Sim.Wheels[0].GripUsed, R.Sim.Wheels[1].GripUsed, R.Sim.Speed, R.Sim.IsCrashed ? 1 : 0);
	}
	F.Crashed = R.Sim.IsCrashed;
	if (F.T > 0.0) F.Avg = 60.0 / F.T;
	return F;
}
} // namespace

int main(int Argc, char** Argv)
{
	// --only STYLE: that family's bikes; --steer-rate-x K, --flick-x K: try the bars' speed or the Flick K times.
	std::string Only, OnlyBike;
	double OnlyMph = -1.0, OnlyAssist = -1.0;
	double SteerX = 1.0, FlickX = 1.0;
	for (int A = 1; A + 1 < Argc; ++A)
	{
		if (std::strcmp(Argv[A], "--only") == 0) Only = Argv[A + 1];
		if (std::strcmp(Argv[A], "--bike") == 0) OnlyBike = Argv[A + 1];
		if (std::strcmp(Argv[A], "--mph") == 0) OnlyMph = std::atof(Argv[A + 1]);
		if (std::strcmp(Argv[A], "--realism") == 0) OnlyAssist = std::atof(Argv[A + 1]);
		if (std::strcmp(Argv[A], "--steer-rate-x") == 0) SteerX = std::atof(Argv[A + 1]);
		if (std::strcmp(Argv[A], "--flick-x") == 0) FlickX = std::atof(Argv[A + 1]);
	}
	const std::string R = Root();
	std::string PartsText, ExtraText, LayoutText, Error;
	PartsDb Db;
	BikeLayout Lay;
	if (!ReadFile(R + "/Data/Parts/parts.json", PartsText) || !ReadFile(R + "/Data/BikesV2/parts_v2.json", ExtraText) ||
		!ReadFile(R + "/Data/Fitment/layout.json", LayoutText) || !Db.LoadFromText(PartsText, Error) || !Db.AddFromText(ExtraText, Error) ||
		!Lay.Load(LayoutText, Db, Error))
	{
		std::printf("FAIL the catalogue with the V2 bikes didn't load under %s (%s)\n", R.c_str(), Error.c_str());
		return 1;
	}
	const double Speeds[3] = {40.0, 60.0, 90.0};
	const double Assists[3] = {0.0, 0.6, 1.0};
	std::printf("bike,style,detected,coherence,flick,mass_kg,wheelbase_mm,trail_mm,bar_mm,steer_rate,mph,realism,t_30_30_s,avg_dps,peak_dps,full_stick_peak_dps,full_stick_t_s,crashed\n");
	int Bikes = 0;
	for (const std::string& Id : Db.Ids("frame"))
	{
		const Json* Frame = Db.Find("frame", Id);
		if (!Frame || !Frame->Get("v2")) continue;
		const Json* Styles = Frame->Get("styles");
		const std::string Style = Styles && !Styles->A.empty() ? Styles->A[0].S : std::string("naked");
		if ((!Only.empty() && Style != Only) || (!OnlyBike.empty() && Id != OnlyBike)) continue;
		const PartsMap Parts = Shop::SetPart(Lay, Shop::StockBuild(Lay, Style), Style, "frame", Id);
		RideSpecInput In;
		In.Parts = Parts;
		BikeSpec S;
		if (!RideSpec(Lay, In, S))
		{
			std::printf("%s,%s,no riding spec\n", Id.c_str(), Style.c_str());
			continue;
		}
		GameCurve(Lay, Parts, S);
		S.SteerRate *= SteerX;
		S.Flick *= FlickX;
		double Coherence = 0.0;
		const std::string Detected = Lay.DetectStyle(Parts, &Coherence);
		++Bikes;
		for (const double M : Speeds)
		{
			if (OnlyMph > 0.0 && M != OnlyMph) continue;
			const double V = M * Mph;
			const double Hold = StickFor35(S, V);
			for (const double A : Assists)
			{
				if (OnlyAssist >= 0.0 && std::fabs(A - OnlyAssist) > 0.01) continue;
				const Flick F = Measure(S, A, V, Hold, Hold);
				const Flick Full = Measure(S, A, V, Hold, 1.0);
				std::printf("%s,%s,%s,%.2f,%.2f,%.0f,%.0f,%.0f,%.0f,%.2f,%.0f,%.1f,%.2f,%.0f,%.0f,%.0f,%.2f,%d\n", Id.c_str(), Style.c_str(), Detected.c_str(), Coherence, S.Flick,
					S.TotalMass, S.Wheelbase * 1000.0, S.Trail * 1000.0, S.BarWidthMm, S.SteerRate, M, A, F.T, F.Avg, F.Peak, Full.Peak, Full.T, (F.Crashed || Full.Crashed) ? 1 : 0);
			}
		}
	}
	std::printf("%d V2 bikes\n", Bikes);
	return Bikes > 0 ? 0 : 1;
}
