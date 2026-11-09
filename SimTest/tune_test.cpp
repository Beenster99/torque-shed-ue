// Checks the dyno desk's tune (Fit/BikeTune, and what Fit/BikeRideSpec does with it) against the
// original game: each style's reference bike and a few swaps (ECUs with rider aids, adjustable
// suspension, a cassette gearbox, steering dampers) under a fixed set of tunes, BikeBuild's numbers
// and the desk's readings dumped from Godot by Tools/make_godot_reference.py --tune
// (SimTest/data/godot_tune.json). Numbers to a part in 10^5, readings word for word.
#include "BikeRideSpec.h"
#include "BikeTune.h"
#include "PartsDb.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace TorqueFit;
using TorqueSim::BikeSpec;

namespace
{
int Failures = 0;

void Expect(bool Ok, const std::string& What)
{
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What.c_str());
	if (!Ok) ++Failures;
}

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

struct Field
{
	double Worst = 0.0;
	std::string WorstAt;
	int Bad = 0, Count = 0;
};
std::map<std::string, Field> Fields;

void Same(const std::string& Name, double Port, double Godot, const std::string& Case)
{
	Field& F = Fields[Name];
	++F.Count;
	const double Miss = std::fabs(Port - Godot) / std::max(1.0, std::fabs(Godot));
	if (Miss > F.Worst)
	{
		F.Worst = Miss;
		F.WorstAt = Case + " (port " + std::to_string(Port) + ", Godot " + std::to_string(Godot) + ")";
	}
	if (Miss > 1e-5) ++F.Bad;
}

void SameText(const std::string& Name, const std::string& Port, const std::string& Godot, const std::string& Case)
{
	Field& F = Fields[Name];
	++F.Count;
	if (Port == Godot) return;
	if (F.Bad++ == 0) F.WorstAt = Case + " (port \"" + Port + "\", Godot \"" + Godot + "\")";
	F.Worst = 1.0;
}

std::string Without(std::string S, const std::string& Cut)
{
	const size_t At = S.find(Cut);
	if (At != std::string::npos) S.erase(At, Cut.size());
	return S;
}

void Check(const BikeLayout& Lay, const std::string& Case, const Json& G, const Json& Base)
{
	PartsMap Parts;
	if (const Json* P = G.Get("parts"))
		for (const auto& [Slot, Id] : P->O) Parts[Slot] = Id.S;
	TuneMap Tune;
	if (const Json* T = G.Get("tune"))
		for (const auto& [Key, V] : T->O) Tune[Key] = V.N;
	RideSpecInput In;
	In.Parts = Parts;
	In.Tune = Tune;
	if (const Json* C = G.Get("condition"))
		for (const auto& [Slot, Cond] : C->O)
		{
			if (Cond.Flag("lightened")) In.Lightened.push_back(Slot);
			if (Slot == "chain") In.ChainSlack = Cond.Num("slack", ChainSlackStock);
		}
	const BikeTune T(Lay, RideParts(Lay, Parts), Tune);

	// The rules and the desk's readings, setting by setting.
	if (const Json* Keys = G.Get("keys"))
		for (const auto& [Key, K] : Keys->O)
		{
			SameText("block", T.Block(Key), K.Str("block"), Case + " " + Key);
			double Lo = 0.0, Hi = 0.0;
			if (Key == "chain") TuneLimits(Key, Lo, Hi);
			else T.Range(Key, Lo, Hi);
			if (Key != "chain")
			{
				Same("range.lo", Lo, K.Num("lo"), Case + " " + Key);
				Same("range.hi", Hi, K.Num("hi"), Case + " " + Key);
			}
			Same("default", T.Default(Key), K.Num("default"), Case + " " + Key);
			const double V = Key == "chain" ? In.ChainSlack : T.Value(Key);
			Same("value", V, K.Num("value"), Case + " " + Key);
			SameText("value_text", T.ValueText(Key, K.Num("value"), G.Num("stock_redline")), K.Str("text"), Case + " " + Key);
		}
	if (const Json* Aids = G.Get("aids"))
		for (const auto& [Aid, A] : Aids->O)
		{
			Same("aid.has", T.HasAid(Aid) ? 1.0 : 0.0, A.Flag("has") ? 1.0 : 0.0, Case + " " + Aid);
			Same("aid.level", T.AidLevel(Aid), A.Num("level"), Case + " " + Aid);
			Same("aid.strength", T.AidStrength(Aid), A.Num("strength"), Case + " " + Aid);
		}
	// (The UE game has no ride modes on the dash yet, so the desk doesn't promise them.)
	SameText("aids_note", T.AidsNote(), Without(G.Str("aids_note"), ", or on the bike's dash as you ride"), Case);
	SameText("ecu_text.map", T.EcuText("map"), G.Str("ecu_map"), Case);
	SameText("ecu_text.limiter", T.EcuText("limiter"), G.Str("ecu_limiter"), Case);

	// The engine's tune: the torque (against the same bike untuned), the limiter, the stress, the notes.
	const BikeTune::Engine E = T.EngineTune();
	{
		// (The heat against the cooling too: a lean map runs the engine hotter.)
		const BikeTune Untuned(Lay, RideParts(Lay, Parts), TuneMap());
		const double Mult = E.TorqueMult * T.CoolingMult(1.0) / Untuned.CoolingMult(1.0);
		Same("engine.torque_mult", Mult, G.Num("peak_torque") / Base.Num("peak_torque"), Case);
	}
	Same("engine.limiter", E.Limiter, G.Num("redline") - G.Num("stock_redline"), Case);
	Same("engine.stress", E.Stress, G.Num("engine_stress"), Case);
	if (const Json* Notes = G.Get("notes"))
		for (const std::string& N : E.Notes)
		{
			bool Found = false;
			for (const Json& X : Notes->A) Found = Found || X.S == N;
			SameText("engine.notes", Found ? N : std::string("(missing)"), N, Case);
		}

	// What the tune does to the riding numbers.
	BikeSpec S;
	if (!RideSpec(Lay, In, S))
	{
		Same("loads", 0.0, 1.0, Case);
		return;
	}
	Same("bike_mass", S.BikeMass, G.Num("bike_mass"), Case);
	Same("com_height", S.ComHeight, G.Num("com_height"), Case);
	Same("front_preload", S.FrontPreload, G.Num("front_preload"), Case);
	Same("front_bump", S.FrontBump, G.Num("front_bump"), Case);
	Same("front_rebound", S.FrontRebound, G.Num("front_rebound"), Case);
	Same("rear_preload", S.RearPreload, G.Num("rear_preload"), Case);
	Same("rear_bump", S.RearBump, G.Num("rear_bump"), Case);
	Same("rear_rebound", S.RearRebound, G.Num("rear_rebound"), Case);
	Same("damper_damping", S.DamperDamping, G.Num("damper_damping"), Case);
	Same("damper_friction", S.DamperFriction, G.Num("damper_friction"), Case);
	Same("rolling", S.Rolling, G.Num("rolling"), Case);
	Same("final_drive", S.FinalDrive, G.Num("final_drive"), Case);
	Same("sprocket_r_front", S.SprocketRFront, G.Num("sprocket_r_front"), Case);
	Same("sprocket_r_rear", S.SprocketRRear, G.Num("sprocket_r_rear"), Case);
	Same("drivetrain_efficiency", S.DrivetrainEfficiency, G.Num("drivetrain_efficiency"), Case);
	if (const Json* R = G.Get("gear_ratios"))
		for (size_t I = 0; I < R->A.size() && I < S.GearRatios.size(); ++I) Same("gear_ratios", S.GearRatios[I], R->A[I].N, Case);
	Same("abs_front", S.AbsFront ? 1.0 : 0.0, G.Flag("abs_front") ? 1.0 : 0.0, Case);
	Same("abs_rear", S.AbsRear ? 1.0 : 0.0, G.Flag("abs_rear") ? 1.0 : 0.0, Case);
	Same("traction", S.TractionControl ? S.TractionStrength : 0.0, G.Num("traction"), Case);
	Same("anti_wheelie", S.AntiWheelie, G.Num("anti_wheelie"), Case);
	Same("lean_traction", S.LeanTraction, G.Num("lean_traction"), Case);
	Same("stability", S.Stability, G.Num("stability"), Case);
	for (int End = 0; End < 2; ++End)
	{
		const TorqueSim::TyreParams& Ty = End == 0 ? S.TyreFront : S.TyreRear;
		const Json* Gt = G.Get(End == 0 ? "tyre_front" : "tyre_rear");
		const Json* Gg = G.Get(End == 0 ? "grip_front" : "grip_rear");
		const std::string P = End == 0 ? "tyre_front." : "tyre_rear.";
		if (!Gt || !Gg) continue;
		Same(P + "kappa_peak", Ty.KappaPeak, Gt->Num("kappa_peak"), Case);
		Same(P + "alpha_peak", Ty.AlphaPeak, Gt->Num("alpha_peak"), Case);
		Same(P + "relax", Ty.Relax, Gt->Num("relax"), Case);
		Same(P + "grip.tarmac", Ty.GripTarmac, Gg->Num("tarmac"), Case);
		Same(P + "grip.dirt", Ty.GripGravel, Gg->Num("dirt"), Case);
		Same(P + "grip.grass", Ty.GripGrass, Gg->Num("grass"), Case);
	}
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string PartsText, LayoutText, RefText, Error;
	if (!ReadFile(R + "/Data/Parts/parts.json", PartsText) || !ReadFile(R + "/Data/Fitment/layout.json", LayoutText) ||
		!ReadFile(R + "/SimTest/data/godot_tune.json", RefText))
	{
		std::printf("FAIL: couldn't read the data (run from the repo root)\n");
		return 1;
	}
	// Godot's numbers are for Godot's parts: the parts this repo's overlay took off sale
	// (Data/Parts/Redrawn/godot_removed.json) are put back for the comparisons, as spec_test does.
	Json PartsJson, Removed, Ref;
	std::string RemovedText;
	if (ReadFile(R + "/Data/Parts/Redrawn/godot_removed.json", RemovedText) && Json::Parse(RemovedText, Removed, &Error)
		&& Json::Parse(PartsText, PartsJson, &Error))
	{
		for (const auto& [Kind, KindParts] : Removed.O)
			if (Json* Into = PartsJson.GetMutable(Kind))
				for (const auto& [Id, P] : KindParts.O) Into->Set(Id, P);
		PartsText = PartsJson.Dump(0);
	}
	PartsDb Db;
	BikeLayout Lay;
	if (!Db.LoadFromText(PartsText, Error) || !Lay.Load(LayoutText, Db, Error) || !Json::Parse(RefText, Ref, &Error))
	{
		std::printf("FAIL: %s\n", Error.c_str());
		return 1;
	}
	const Json* Cases = Ref.Get("cases");
	Expect(Cases && Cases->O.size() > 100, "Godot's tune cases are there");
	if (!Cases) return 1;
	int N = 0;
	for (const auto& [Name, G] : Cases->O)
	{
		// The same bike untuned, for the engine's torque.
		const std::string Bike = Name.substr(0, Name.find('/'));
		const Json* Base = Cases->Get(Bike + "/tune0");
		if (!Base) continue;
		Check(Lay, Name, G, *Base);
		++N;
	}
	std::printf("\nThe dyno desk's tune on %d cases, against Godot:\n", N);
	for (const auto& [Name, F] : Fields)
		Expect(F.Bad == 0, Name + " (" + std::to_string(F.Count) + " checked" + (F.Bad ? ", " + std::to_string(F.Bad) + " off; worst " + F.WorstAt : std::string()) + ")");
	std::printf("\n%s (%d failures)\n", Failures ? "FAILED" : "PASSED", Failures);
	return Failures ? 1 : 0;
}
