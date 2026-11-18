// Every style's stock bike ridden against the real bikes it stands for (docs: Torque Shed's
// docs/bike_style_performance.md, magazine road tests, about +-10%). As the Godot game's
// tests/bike_realism_tests.gd rides them, on flat dry tarmac with auto-shift and Riding realism at
// 0.5, but held to the real figures rather than to Godot's wider bands (beenster, 9 Nov 2026: tune
// each bike to the real ones now, closing Godot's known gaps):
//   - flat out from a standstill, weight over the tank: 0-60 mph (s), within 10% of the real figure;
//   - both brakes hard from just over 60 mph, no weight forward: metres from 60 mph to a stop, in the
//     real range (a metre either side), the back wheel kept near the road;
//   - at 20 m/s with the steer input held full: the lean it settles at and the sideways g;
//   - flat out on the level for 3 minutes: the top speed, on the game's own engine curve (16 Nov 2026).
// Each bike is BikeRideSpec's from this game's reference parts (Data/Fitment/layout.json, with the
// overlay's changes), with Godot's curve for the same engine (SimTest/data/godot_spec.json).
//
//     build/simtest/realism_test [--table] [--swap style:slot=part,...]
// --table prints the markdown table for the docs; --swap tries other parts on the reference bikes;
// --trace STYLE prints that style's launch; --top prints each bike's times to 60/100/120/140/160 mph,
// its top speed and what holds it there; --game rides the game's engine curve instead of Godot's.
#include "BikeRideSpec.h"
#include "BikeSim.h"
#include "PartsDb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace TorqueFit;
using TorqueSim::BikeSim;
using TorqueSim::BikeSpec;
using TorqueSim::V3;

namespace
{
int Failures = 0;
constexpr double MPH60 = 26.8224;
constexpr double Dt = 1.0 / 120.0;
constexpr double Deg = 57.29577951308232;

// The real bikes (bike_style_performance.md): 0-60 s, the 60-0 stop's range (m), the steady full-lean
// turn's lean range (deg) and sideways g range. Godot's ridden figures (its own realism tests, run
// headless on 9 Nov 2026) are kept to show the change. The race bike's g starts at 1.10 as its stock
// tyres are sport road ones, not slicks; the tall roader's goes to 1.12 on sport-touring tyres.
//
// Known gaps, not yet closed (they fail): the scrambler's and adventure bike's launches (4.5 and
// 4.3 s against 3.9 and 3.4: the rear tyre spins at about twice its peak slip with the front nearly
// light, and the adventure bike is 25 kg heavy), and the chopper's (4.4 against 4.0).
struct Real
{
	const char* Name;
	double T60;            // < 0: no road figure (the MX bike on tarmac)
	double Stop[2], Lean[2], G[2];
	double GodotT60, GodotStop, GodotLean, GodotG;
};
const Real Reals[] = {
	{"classic", 5.5, {36, 42}, {42, 48}, {0.88, 1.02}, 5.0, 35.4, 46, 0.93},
	{"cafe", 3.7, {35, 40}, {44, 50}, {0.95, 1.05}, 3.8, 35.2, 46, 0.94},
	{"scrambler", 3.9, {37, 42}, {40, 45}, {0.80, 0.90}, 4.6, 37.8, 41, 0.82},
	{"tourer", 3.4, {36, 40}, {44, 49}, {0.95, 1.05}, 3.7, 35.4, 47, 0.92},
	{"roader", 3.3, {34, 38}, {47, 52}, {1.00, 1.12}, 3.2, 33.5, 49, 1.03},
	{"naked", 3.1, {34, 37}, {47, 52}, {1.05, 1.15}, 3.2, 32.8, 49, 1.04},
	{"sport", 3.3, {34, 37}, {49, 55}, {1.08, 1.20}, 3.3, 34.2, 49, 1.03},
	{"race", 2.9, {33, 36}, {50, 56}, {1.10, 1.25}, 3.0, 34.2, 49, 1.04},
	{"bobber", 4.0, {38, 42}, {27, 32}, {0.50, 0.60}, 4.3, 36.0, 31, 0.55},
	{"chopper", 4.0, {40, 44}, {27, 33}, {0.50, 0.60}, 4.4, 36.0, 31, 0.53},
	{"cruiser", 4.2, {38, 42}, {28, 33}, {0.55, 0.65}, 4.4, 33.9, 33, 0.56},
	{"adventure", 3.4, {36, 40}, {43, 47}, {0.90, 1.00}, 4.2, 36.4, 44, 0.88},
	{"supermoto", 3.5, {35, 38}, {48, 54}, {1.03, 1.15}, 3.7, 34.4, 47, 0.98},
	{"mx", -1.0, {36, 48}, {30, 42}, {0.60, 0.90}, 5.5, 42.6, 35, 0.72},
};
// Top speed flat out on the level in still air, rider sat on (mph): the real bikes each style stands for
// (road tests and makers' figures; beenster, 16 Nov 2026: "I could only make 110 mph on the sports bike").
// Ridden on the game's own engine curve (TorqueShedPower, as the shop sells it), not Godot's.
struct RealTop { const char* Name; double Lo, Hi; };
const RealTop RealTops[] = {
	{"classic", 95, 112}, {"cafe", 120, 140}, {"scrambler", 105, 120}, {"tourer", 130, 145}, {"roader", 132, 150},
	{"naked", 132, 155}, {"sport", 148, 165}, {"race", 180, 200}, {"bobber", 110, 125}, {"chopper", 105, 125},
	{"cruiser", 110, 125}, {"adventure", 120, 135}, {"supermoto", 100, 115}, {"mx", 78, 92},
};
constexpr double T60_SHARE = 0.10;   // 0-60 within this share of the real figure
constexpr double STOP_SLACK = 1.0;   // m either side of the real range

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

// Swaps for trying a change before it goes in the data: --swap cruiser:front_caliper=twin_piston,...
std::map<std::string, std::map<std::string, std::string>> Swaps;
const Json* Sweep = nullptr;   // Godot's swept builds (an engine's curve on another bike)

// The engine as the game itself works it out for a bike fresh from the shop (TorqueShedPower::Numbers and
// EngineTorque, every part new, the dyno desk untouched): --game rides that curve instead of Godot's.
bool UseGameCurve = false;
void GameCurve(const BikeLayout& Lay, const std::string& Style, BikeSpec& S)
{
	PartsMap Parts = Lay.ReferenceBuild(Style);
	for (const auto& [Slot, Id] : Swaps[Style]) Parts[Slot] = Id;
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
		S.TorqueNm.push_back(Rpm > Redline * 1.02 ? 0.0 : Peak * std::clamp(Shape, 0.3, 1.0));
	}
}

bool SpecFor(const BikeLayout& Lay, const std::string& Style, const Json& G, BikeSpec& S)
{
	RideSpecInput In;
	In.Parts = Lay.ReferenceBuild(Style);
	for (const auto& [Slot, Id] : Swaps[Style]) In.Parts[Slot] = Id;
	// The engine's curve is Godot's for the same engine (the shed works it out the same way).
	// A swapped (or newly chosen) engine takes its curve from Godot's sweep build with that engine.
	const Json* Curve = &G;
	const Json* Engine = G.Get("parts") ? G.Get("parts")->Get("engine") : nullptr;
	if (!Engine || In.Parts["engine"] != Engine->S)
	{
		Curve = Sweep ? Sweep->Get("engine/" + In.Parts["engine"]) : nullptr;
		if (!Curve) return false;
	}
	const Json& Gc = *Curve;
	if (!RideSpec(Lay, In, S)) return false;
	S.IdleRpm = Gc.Num("idle_rpm", 1000.0);
	S.Redline = Gc.Num("redline", 8000.0);
	S.PeakTorque = Gc.Num("peak_torque", 100.0);
	S.PeakTorqueRpm = Gc.Num("peak_torque_rpm", 5000.0);
	S.TorqueRpm.clear();
	S.TorqueNm.clear();
	if (const Json* R = Gc.Get("torque_rpm"))
		for (const Json& V : R->A) S.TorqueRpm.push_back(V.N);
	if (const Json* N = Gc.Get("torque_nm"))
		for (const Json& V : N->A) S.TorqueNm.push_back(V.N);
	if (Curve != &G && Gc.Get("parts"))
	{
		// The sweep build had its own head, exhaust and so on: take those out and put this bike's in, as
		// the shed's engine numbers multiply them (TorqueShedPower Numbers).
		const Json& Had = *Gc.Get("parts");
		auto Num = [&Lay](const std::string& Id, const char* Key, double Def) { const Json* P = Lay.Part(Id); return P ? P->Num(Key, Def) : Def; };
		auto Was = [&Had](const char* Slot) { const Json* V = Had.Get(Slot); return V ? V->S : std::string("none"); };
		double Mult = 1.0, Shift = 1.0, RevAdd = 0.0;
		for (const char* Slot : {"head", "exhaust", "fuel", "spark_plugs"})
			Mult *= Num(In.Parts[Slot], "torque_mult", 1.0) / Num(Was(Slot), "torque_mult", 1.0);
		Mult *= Num(In.Parts["air_filter"], "flow", 1.0) / Num(Was("air_filter"), "flow", 1.0);
		Mult *= Num(In.Parts["ecu"], "power", 1.0) / Num(Was("ecu"), "power", 1.0);
		for (const char* Slot : {"head", "fuel"})
			Shift *= Num(In.Parts[Slot], "peak_shift", 1.0) / Num(Was(Slot), "peak_shift", 1.0);
		RevAdd = Num(In.Parts["head"], "redline_add", 0.0) - Num(Was("head"), "redline_add", 0.0)
			+ Num(In.Parts["ecu"], "rev_add", 0.0) - Num(Was("ecu"), "rev_add", 0.0);
		const double Top = S.Redline, NewTop = Top + RevAdd;
		for (double& Nm : S.TorqueNm) Nm *= Mult;
		for (double& R : S.TorqueRpm) R = S.IdleRpm + (R - S.IdleRpm) * (NewTop - S.IdleRpm) / std::max(1.0, Top - S.IdleRpm);
		S.PeakTorque *= Mult;
		S.PeakTorqueRpm *= Shift;
		S.Redline = NewTop;
	}
	if (UseGameCurve) GameCurve(Lay, Style, S);
	return true;
}

// A fresh bike on flat dry tarmac, as the Godot test's ride(): auto-shift, Riding realism 0.5.
void Start(BikeSim& Sim, const BikeSpec& S)
{
	Sim.Setup(S);
	Sim.Ray = FlatGround;
	Sim.Place(V3(0.0, 0.0, 0.0), 0.0);
	Sim.AutoShift = true;
	Sim.Assist = 0.5;
}

struct Result
{
	double T60 = -1, Stop = -1, Nose = 0, Lean = 0, MaxLean = 0, G = 0;
	bool Crashed[3] = {false, false, false};
};

// --trace STYLE prints that style's launch every tenth of a second (what holds it back).
std::string TraceStyle, Riding;

Result Ride(const BikeSpec& S)
{
	Result R;
	{   // Launch: 8 s flat out.
		BikeSim Sim;
		Start(Sim, S);
		if (Riding == TraceStyle)
		{
			std::printf("    mass %.0f kg, com %.2f m, wheelbase %.3f, peak %.0f Nm at %.0f, redline %.0f, TC %d (%.2f), rear kappa peak %.3f, ratios",
				S.TotalMass, S.ComHeight, S.Wheelbase, S.PeakTorque, S.PeakTorqueRpm, S.Redline, S.TractionControl ? 1 : 0, S.TractionStrength, S.TyreRear.KappaPeak);
			for (int Gi = 1; Gi <= S.GearCount(); ++Gi) std::printf(" %.1f m/s", S.Redline / 60.0 * 6.2832 / S.OverallRatio(Gi) * S.RearRadius);
			std::printf("\n    brakes %.0f / %.0f N m, tyres %.2f / %.2f grip, ABS %d / %d\n", S.FrontBrakeTorque, S.RearBrakeTorque, S.TyreFront.GripTarmac, S.TyreRear.GripTarmac, S.AbsFront ? 1 : 0, S.AbsRear ? 1 : 0);
		}
		for (int I = 0; I < 8 * 120; ++I)
		{
			Sim.Throttle = 1.0;
			Sim.BodyShift = 0.6;
			Sim.Step(Dt);
			if (Riding == TraceStyle && I % 12 == 0 && R.T60 < 0)
			{
				const TorqueSim::Wheel& F = Sim.Wheels[0];
				const TorqueSim::Wheel& B = Sim.Wheels[1];
				std::printf("    t %.2f  %5.1f m/s  gear %d  %5.0f rpm  drive %6.0f Nm  rear load %5.0f slip %.3f used %.2f mu %.2f  front load %5.0f  pitch %+.1f deg  thr %.2f tc %.2f\n",
					I * Dt, Sim.Speed, Sim.Gear, Sim.Rpm, B.DriveTorque, B.Load, B.Kappa, B.GripUsed, B.Mu, F.Load, Sim.PitchAngle * Deg, Sim.ThrottleNow(), Sim.TractionNow());
			}
			if (R.T60 < 0 && Sim.Speed >= MPH60) R.T60 = I * Dt;
			R.Crashed[0] |= Sim.IsCrashed;
		}
	}
	{   // Stop: up past 61 mph, then front full, back 0.6, no weight forward.
		BikeSim Sim;
		Start(Sim, S);
		bool Braking = false;
		double At = -1;
		V3 From;
		for (int I = 0; I < 12 * 120; ++I)
		{
			if (!Braking)
			{
				Sim.Throttle = 1.0;
				Sim.BodyShift = 0.6;
				Braking = Sim.Speed >= MPH60 + 1.0;
			}
			else
			{
				Sim.Throttle = 0.0;
				Sim.BodyShift = 0.0;
				Sim.FrontBrake = 1.0;
				Sim.RearBrake = 0.6;
				R.Nose = std::min(R.Nose, Sim.PitchAngle);
				if (At < 0 && Sim.Speed <= MPH60) { At = I * Dt; From = Sim.Position; }
				if (At >= 0 && R.Stop < 0 && Sim.Speed < 0.3) R.Stop = (Sim.Position - From).Length();
			}
			Sim.Step(Dt);
			R.Crashed[1] |= Sim.IsCrashed;
		}
	}
	{   // Corner: 20 m/s, steer input full from 6 s, measured over 9-12 s.
		BikeSim Sim;
		Start(Sim, S);
		double G = 0, Lean = 0;
		int N = 0;
		for (int I = 0; I < 12 * 120; ++I)
		{
			const double At = I * Dt;
			Sim.Throttle = std::clamp(0.25 + (20.0 - Sim.Speed) * 0.4, 0.0, 1.0);
			Sim.LeanInput = At < 6.0 ? 0.0 : 1.0;
			Sim.Step(Dt);
			if (At > 9.0)
			{
				G += std::fabs(Sim.AngularVelocity.y * Sim.Speed) / 9.81;
				Lean += std::fabs(Sim.LeanAngle * Deg);
				++N;
			}
			R.MaxLean = std::max(R.MaxLean, std::fabs(Sim.LeanAngle * Deg));
			R.Crashed[2] |= Sim.IsCrashed;
		}
		N = std::max(N, 1);
		R.G = G / N;
		R.Lean = Lean / N;
	}
	return R;
}

// The most a bike makes flat out on the level from a standstill in 3 minutes (mph), the auto box shifting.
double TopMph(const BikeSpec& S)
{
	BikeSim Sim;
	Start(Sim, S);
	double Top = 0.0;
	for (int I = 0; I < 180 * 120 && !Sim.IsCrashed; ++I)
	{
		Sim.Throttle = 1.0;
		Sim.BodyShift = 0.6;
		Sim.Step(Dt);
		Top = std::max(Top, Sim.Speed * 2.23694);
	}
	return Top;
}

void Expect(bool Ok, const char* Fmt, ...) __attribute__((format(printf, 2, 3)));
void Expect(bool Ok, const char* Fmt, ...)
{
	char Line[400];
	va_list A;
	va_start(A, Fmt);
	std::vsnprintf(Line, sizeof Line, Fmt, A);
	va_end(A);
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", Line);
	if (!Ok) ++Failures;
}
} // namespace

int main(int Argc, char** Argv)
{
	bool Table = false;
	for (int A = 1; A < Argc; ++A)
	{
		if (std::strcmp(Argv[A], "--table") == 0) Table = true;
		else if (std::strcmp(Argv[A], "--game") == 0) UseGameCurve = true;
		else if (std::strcmp(Argv[A], "--trace") == 0 && A + 1 < Argc) TraceStyle = Argv[++A];
		else if (std::strcmp(Argv[A], "--swap") == 0 && A + 1 < Argc)
		{
			std::stringstream Ss(Argv[++A]);
			std::string Item;
			while (std::getline(Ss, Item, ','))
			{
				const size_t C = Item.find(':'), E = Item.find('=');
				if (C != std::string::npos && E != std::string::npos && E > C) Swaps[Item.substr(0, C)][Item.substr(C + 1, E - C - 1)] = Item.substr(E + 1);
			}
		}
	}
	const std::string R = Root();
	std::string PartsText, LayoutText, RefText, Error;
	PartsDb Db;
	BikeLayout Lay;
	Json Ref;
	if (!(ReadFile(R + "/Data/Parts/parts.json", PartsText) && ReadFile(R + "/Data/Fitment/layout.json", LayoutText) &&
		  Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error) && ReadFile(R + "/SimTest/data/godot_spec.json", RefText) &&
		  Json::Parse(RefText, Ref, &Error)))
	{
		std::printf("FAIL: the parts, layout or Godot reference won't load: %s\n", Error.c_str());
		return 1;
	}
	const Json* Styles = Ref.Get("styles");
	Sweep = Ref.Get("sweep");
	bool WheelieRun = false;
	for (int A = 1; A < Argc; ++A) WheelieRun |= std::strcmp(Argv[A], "--wheelie") == 0;
	if (WheelieRun)
	{
		// --wheelie: each reference bike (with --swap's changes) with every rider aid off, flat out from a
		// standstill in first for 4 s: the most the front comes up (deg), sat normally and with the weight
		// right back, at Riding realism 0 (full sim) and 0.5.
		std::printf("style,engine,hp,sat_r0,back_r0,sat_r05,back_r05,dump_sat_r0,dump_back_r0\n");
		for (const Real& Rl : Reals)
		{
			const Json* G = Styles ? Styles->Get(Rl.Name) : nullptr;
			BikeSpec S;
			UseGameCurve = true;
			if (!G || !SpecFor(Lay, Rl.Name, *G, S)) continue;
			S.TractionControl = false;
			S.TractionStrength = S.LeanTraction = S.AntiWheelie = S.Stability = 0.0;
			double PeakKw = 0.0;
			for (double Rpm = S.IdleRpm; Rpm <= S.Redline; Rpm += 50.0) PeakKw = std::max(PeakKw, S.EngineTorque(Rpm) * Rpm * 6.2832 / 60.0 / 1000.0);
			auto Run = [&](double Shift, double Assist, bool Dump) {
				BikeSim Sim;
				Start(Sim, S);
				Sim.Assist = Assist;
				double Most = 0.0;
				for (int I = 0; I < 2 * 120; ++I) Sim.Step(Dt);   // settled on its wheels before the throttle opens
				for (int I = 0; I < 4 * 120 && !Sim.IsCrashed; ++I)
				{
					// A clutch dump: revs held up on the lever for a second, then let go.
					Sim.ClutchIn = Dump && I < 120;
					Sim.Throttle = 1.0;
					Sim.BodyShift = Shift;
					Sim.Step(Dt);
					Most = std::max(Most, Sim.PitchAngle * Deg);
					if (TraceStyle == Rl.Name && Shift < 0 && !Dump && Assist == 0.5 && I % 12 == 0)
						std::printf("    t %.1f %5.1f m/s gear %d rpm %5.0f pitch %5.1f slip %.3f thrEff %.2f tc %.2f rearload %5.0f front %d drive %5.0f\n", I * Dt, Sim.Speed, Sim.Gear, Sim.Rpm,
							Sim.PitchAngle * Deg, Sim.Wheels[1].Kappa, Sim.ThrottleNow(), Sim.TractionNow(), Sim.Wheels[1].Load, Sim.Wheels[0].Contact ? 1 : 0, Sim.Wheels[1].DriveTorque);
				}
				return Most;
			};
			auto Parts = Lay.ReferenceBuild(Rl.Name);
			for (const auto& [Slot, Id] : Swaps[Rl.Name]) Parts[Slot] = Id;
			std::printf("%s,%s,%.0f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n", Rl.Name, Parts["engine"].c_str(), PeakKw * 1.341, Run(0.0, 0.0, false), Run(-1.0, 0.0, false),
				Run(0.0, 0.5, false), Run(-1.0, 0.5, false), Run(0.0, 0.0, true), Run(-1.0, 0.0, true));
		}
		return 0;
	}
	bool TopRun = false;
	for (int A = 1; A < Argc; ++A) TopRun |= std::strcmp(Argv[A], "--top") == 0;
	if (TopRun)
	{
		// --top: each reference bike flat out on the level from a standstill, tucked in after 60 mph: the
		// times to 60/100/120/140/160 mph, the speed after 30 s and 60 s, the most in 3 minutes, and what
		// holds it there (the gear and revs, the engine's heat).
		std::printf("style,hp,kg_with_rider,cda,t60,t100,t120,t140,t160,mph30s,mph60s,top_mph,gear,rpm,redline,heat_power,engine_c,geared_mph,power_mph,peakkw_rpm,primary,final,top_ratio\n");
		for (const Real& Rl : Reals)
		{
			const Json* G = Styles ? Styles->Get(Rl.Name) : nullptr;
			BikeSpec S;
			if (!G || !SpecFor(Lay, Rl.Name, *G, S)) continue;
			double PeakKw = 0.0;
			for (double Rpm = S.IdleRpm; Rpm <= S.Redline; Rpm += 50.0) PeakKw = std::max(PeakKw, S.EngineTorque(Rpm) * Rpm * 6.2832 / 60.0 / 1000.0);
			BikeSim Sim;
			Start(Sim, S);
			double T[5] = {-1, -1, -1, -1, -1}, Mph[2] = {0, 0}, Top = 0.0;
			const double At[5] = {60, 100, 120, 140, 160};
			for (int I = 0; I < 180 * 120 && !Sim.IsCrashed; ++I)
			{
				Sim.Throttle = 1.0;
				Sim.BodyShift = 0.6;
				Sim.Step(Dt);
				const double M = Sim.Speed * 2.23694;
				for (int K = 0; K < 5; ++K) if (T[K] < 0 && M >= At[K]) T[K] = I * Dt;
				if (I == 30 * 120) Mph[0] = M;
				if (I == 60 * 120) Mph[1] = M;
				Top = std::max(Top, M);
				if (TraceStyle == Rl.Name && I % 240 == 0)
					std::printf("    t %3.0f  %5.1f mph  gear %d  %5.0f rpm  thr %.2f  tc %.2f  heat %.2f\n", I * Dt, M, Sim.Gear, Sim.Rpm, Sim.ThrottleNow(), Sim.TractionNow(), Sim.Heat.Power);
			}
			// Top gear at the limiter, and the speed the power alone holds against the air and the tyres.
			const double Geared = S.Redline * 6.283185 / 60.0 / S.OverallRatio(S.GearCount()) * S.RearRadius;
			double PeakRpm = S.IdleRpm, PeakW = 0.0;
			for (double Rpm = S.IdleRpm; Rpm <= S.Redline; Rpm += 50.0)
				if (S.EngineTorque(Rpm) * Rpm * 6.283185 / 60.0 > PeakW) { PeakW = S.EngineTorque(Rpm) * Rpm * 6.283185 / 60.0; PeakRpm = Rpm; }
			PeakW *= S.DrivetrainEfficiency;
			double Lo = 1.0, Hi = 150.0;
			for (int K = 0; K < 50; ++K)
			{
				const double V = 0.5 * (Lo + Hi);
				(0.5 * 1.2 * S.CdA * V * V * V + S.Rolling * S.TotalMass * 9.81 * V > PeakW ? Hi : Lo) = V;
			}
			std::printf("%s,%.0f,%.0f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%d,%.0f,%.0f,%.2f,%.0f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f\n", Rl.Name, PeakKw * 1.341, S.TotalMass, S.CdA,
				T[0], T[1], T[2], T[3], T[4], Mph[0], Mph[1], Top, Sim.Gear, Sim.Rpm, S.Redline, Sim.Heat.Power, Sim.Heat.Engine, Geared * 2.23694, Lo * 2.23694,
				PeakRpm, S.Primary, S.FinalDrive, S.OverallRatio(S.GearCount()));
		}
		return 0;
	}
	bool Calib = false;
	for (int A = 1; A < Argc; ++A) Calib |= std::strcmp(Argv[A], "--calib") == 0;
	if (Calib)
	{
		// --calib: each style's reference bike as the game builds it, for calibrating against the real
		// figures (docs: bike_style_performance.md): peak power, wet weight, wheelbase, seat height, top
		// speed flat out on the level (3 minutes, the most it reaches), 0-60, and the petrol at a steady
		// 56 mph in top (TorqueDash's sums, UK mpg).
		std::printf("style,hp,wet_kg,wheelbase_mm,seat_mm,top_mph,t60_s,mpg56_uk,cda,gears\n");
		for (const Real& Rl : Reals)
		{
			const Json* G = Styles ? Styles->Get(Rl.Name) : nullptr;
			BikeSpec S;
			if (!G || !SpecFor(Lay, Rl.Name, *G, S)) continue;
			Riding = Rl.Name;
			const Result Res = Ride(S);
			double PeakKw = 0.0;
			for (double Rpm = S.IdleRpm; Rpm <= S.Redline; Rpm += 50.0) PeakKw = std::max(PeakKw, S.EngineTorque(Rpm) * Rpm * 6.2832 / 60.0 / 1000.0);
			double Top = 0.0;
			{
				BikeSim Sim;
				Start(Sim, S);
				for (int I = 0; I < 180 * 120 && !Sim.IsCrashed; ++I)
				{
					Sim.Throttle = 1.0;
					Sim.BodyShift = I > 10 * 120 ? 0.0 : 0.6;
					Sim.Step(Dt);
					Top = std::max(Top, Sim.Speed);
				}
			}
			// TorqueDash::CruiseEconomy at 25 m/s (56 mph), the reference parts' head and block.
			auto Parts = Lay.ReferenceBuild(Rl.Name);
			for (const auto& [Slot, Id] : Swaps[Rl.Name]) Parts[Slot] = Id;
			auto PartJ = [&](const char* Slot) -> const Json* { auto It = Parts.find(Slot); return It == Parts.end() ? nullptr : Lay.Part(It->second); };
			const Json* Block = PartJ("engine");
			const Json* Head = PartJ("head");
			const double Disp = Block ? Block->Num("displacement", 700.0) : 700.0;
			const bool Two = Block && Block->Str("layout") == "2stroke";
			const double Fric = (Head ? Head->Num("thirst", 1.0) : 1.0) * (1.0 + 0.08 * ((Block ? Block->Num("cylinders", 2.0) : 2.0) - 2.0));
			const double V = 25.0;
			const double Kw = (0.5 * 1.2 * S.CdA * V * V * V + S.Rolling * S.TotalMass * 9.81 * V) / S.DrivetrainEfficiency / 1000.0;
			const double Rpm = std::max(V / S.RearRadius * S.OverallRatio(S.GearCount()) * 60.0 / 6.2832, S.IdleRpm);
			const double Pk = S.EngineTorque(Rpm) * Rpm * 6.2832 / 60.0 / 1000.0;
			const double Load = std::clamp(Kw / std::max(Pk, 0.1), 0.0, 1.0);
			const double Bar = (0.9 + 0.13 * Rpm / 1000.0) * Fric + 0.75 * (1.0 - Load);
			const double FricKw = Bar * 100000.0 * Disp / 1e6 * Rpm / 120.0 / 1000.0;
			const double Lph = (Kw + FricKw) * 0.30 * (Two ? 1.35 : 1.0);
			const double KmPerL = 90.0 / Lph;
			const double MpgUk = KmPerL * 0.621371 * 4.54609;
			std::printf("%s,%.0f,%.0f,%.0f,%.0f,%.0f,%.2f,%.0f,%.2f,%d\n", Rl.Name, PeakKw * 1.341, S.BikeMass, S.Wheelbase * 1000.0, S.SeatHeight * 1000.0,
				Top * 2.23694, Res.T60, MpgUk, S.CdA, S.GearCount());
		}
		return 0;
	}
	if (Table)
	{
		std::printf("| Style | 0-60 s (real; Godot) | 60-0 m (real; Godot) | Lean deg (real; Godot) | Sideways g (real; Godot) |\n|---|---|---|---|---|\n");
	}
	for (const Real& Rl : Reals)
	{
		const Json* G = Styles ? Styles->Get(Rl.Name) : nullptr;
		BikeSpec S;
		if (!G || !SpecFor(Lay, Rl.Name, *G, S))
		{
			Expect(false, "%s: the reference bike's spec", Rl.Name);
			continue;
		}
		Riding = Rl.Name;
		const Result Res = Ride(S);
		if (Table)
		{
			char T60[64];
			if (Rl.T60 > 0) std::snprintf(T60, sizeof T60, "%.1f (%.1f; %.1f)", Res.T60, Rl.T60, Rl.GodotT60);
			else std::snprintf(T60, sizeof T60, "%.1f (n/a; %.1f)", Res.T60, Rl.GodotT60);
			std::printf("| %s | %s | %.1f (%.0f-%.0f; %.1f) | %.0f (%.0f-%.0f; %.0f) | %.2f (%.2f-%.2f; %.2f) |\n", Rl.Name, T60, Res.Stop, Rl.Stop[0], Rl.Stop[1],
				Rl.GodotStop, Res.Lean, Rl.Lean[0], Rl.Lean[1], Rl.GodotLean, Res.G, Rl.G[0], Rl.G[1], Rl.GodotG);
			continue;
		}
		std::printf("%s:\n", Rl.Name);
		if (Rl.T60 > 0)
			Expect(!Res.Crashed[0] && Res.T60 > 0 && std::fabs(Res.T60 - Rl.T60) <= Rl.T60 * T60_SHARE, "0-60 mph in %.2f s (real %.1f, Godot %.1f)", Res.T60,
				Rl.T60, Rl.GodotT60);
		else Expect(!Res.Crashed[0] && Res.T60 > 0 && Res.T60 < 6.5, "0-60 mph on tarmac in %.2f s (Godot %.1f)", Res.T60, Rl.GodotT60);
		Expect(!Res.Crashed[1] && Res.Stop >= Rl.Stop[0] - STOP_SLACK && Res.Stop <= Rl.Stop[1] + STOP_SLACK, "stops from 60 mph in %.1f m (real %.0f-%.0f, Godot %.1f)",
			Res.Stop, Rl.Stop[0], Rl.Stop[1], Rl.GodotStop);
		Expect(-Res.Nose * Deg < 9.0, "the back wheel stays near the road (nose down %.1f deg at most)", -Res.Nose * Deg);
		Expect(!Res.Crashed[2], "a full-lean turn at 20 m/s holds");
		Expect(Res.Lean >= Rl.Lean[0] && Res.Lean <= Rl.Lean[1] && Res.MaxLean < Rl.Lean[1] + 4.0, "leans %.1f deg (most %.1f; real %.0f-%.0f, Godot %.0f)", Res.Lean,
			Res.MaxLean, Rl.Lean[0], Rl.Lean[1], Rl.GodotLean);
		Expect(Res.G >= Rl.G[0] && Res.G <= Rl.G[1], "pulls %.2f g (real %.2f-%.2f, Godot %.2f)", Res.G, Rl.G[0], Rl.G[1], Rl.GodotG);
		for (const RealTop& Rt : RealTops)
		{
			if (std::strcmp(Rt.Name, Rl.Name) != 0) continue;
			const bool Was = UseGameCurve;
			UseGameCurve = true;
			BikeSpec Gs;
			const bool Ok = SpecFor(Lay, Rl.Name, *G, Gs);
			UseGameCurve = Was;
			const double Top = Ok ? TopMph(Gs) : 0.0;
			Expect(Ok && Top >= Rt.Lo && Top <= Rt.Hi, "tops out at %.0f mph on the game's engine (real %.0f-%.0f)", Top, Rt.Lo, Rt.Hi);
		}
	}
	if (!Table) std::printf("\n%s: %d failed\n", Failures ? "FAIL" : "ok", Failures);
	return Failures ? 1 : 0;
}
