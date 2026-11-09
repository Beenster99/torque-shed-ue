// Every style's stock bike ridden against the real bikes it stands for (docs: Torque Shed's
// docs/bike_style_performance.md, magazine road tests, about +-10%). As the Godot game's
// tests/bike_realism_tests.gd rides them, on flat dry tarmac with auto-shift and Riding realism at
// 0.5, but held to the real figures rather than to Godot's wider bands (beenster, 9 Nov 2026: tune
// each bike to the real ones now, closing Godot's known gaps):
//   - flat out from a standstill, weight over the tank: 0-60 mph (s), within 10% of the real figure;
//   - both brakes hard from just over 60 mph, no weight forward: metres from 60 mph to a stop, in the
//     real range (a metre either side), the back wheel kept near the road;
//   - at 20 m/s with the steer input held full: the lean it settles at and the sideways g.
// Each bike is BikeRideSpec's from this game's reference parts (Data/Fitment/layout.json, with the
// overlay's changes), with Godot's curve for the same engine (SimTest/data/godot_spec.json).
//
//     build/simtest/realism_test [--table] [--swap style:slot=part,...]
// --table prints the markdown table for the docs; --swap tries other parts on the reference bikes.
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

bool SpecFor(const BikeLayout& Lay, const std::string& Style, const Json& G, BikeSpec& S)
{
	RideSpecInput In;
	In.Parts = Lay.ReferenceBuild(Style);
	for (const auto& [Slot, Id] : Swaps[Style]) In.Parts[Slot] = Id;
	// The engine's curve is Godot's for the same engine (the shed works it out the same way).
	const Json* Engine = G.Get("parts") ? G.Get("parts")->Get("engine") : nullptr;
	if (!Engine || In.Parts["engine"] != Engine->S) return false;
	if (!RideSpec(Lay, In, S)) return false;
	S.IdleRpm = G.Num("idle_rpm", 1000.0);
	S.Redline = G.Num("redline", 8000.0);
	S.PeakTorque = G.Num("peak_torque", 100.0);
	S.PeakTorqueRpm = G.Num("peak_torque_rpm", 5000.0);
	S.TorqueRpm.clear();
	S.TorqueNm.clear();
	if (const Json* R = G.Get("torque_rpm"))
		for (const Json& V : R->A) S.TorqueRpm.push_back(V.N);
	if (const Json* N = G.Get("torque_nm"))
		for (const Json& V : N->A) S.TorqueNm.push_back(V.N);
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

Result Ride(const BikeSpec& S)
{
	Result R;
	{   // Launch: 8 s flat out.
		BikeSim Sim;
		Start(Sim, S);
		for (int I = 0; I < 8 * 120; ++I)
		{
			Sim.Throttle = 1.0;
			Sim.BodyShift = 0.6;
			Sim.Step(Dt);
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
	}
	if (!Table) std::printf("\n%s: %d failed\n", Failures ? "FAIL" : "ok", Failures);
	return Failures ? 1 : 0;
}
