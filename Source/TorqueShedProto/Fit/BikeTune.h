// The dyno desk's tune, as Torque Shed's BikeBuild keeps it (bike_build.gd TUNE_LIMITS, tune_block,
// tune_range, tune_default, tune_value, _tune_engine, the rider aids' levels, the tyre pressures'
// grip and the gear ratios' spacing) and the desk shows it (tuning_computer.gd TABS, value_text,
// aids_note, _ecu_text). Plain C++ on the parts catalogue, so the riding spec (BikeRideSpec), the
// shed's power numbers (TorqueShedPower) and the desk (Shed/TorqueDynoDesk) all read one set of
// rules, and SimTest/tune_test checks them against the Godot game.
//
// A tune is setting -> value; a setting missing from it is left stock. Where each setting is kept
// (shed_state.gd TUNE_HOME) is the shed's business: the engine's and the aids' on the bike, the
// clickers, pressures, sprockets and spacing on the part they adjust (they go with it when it's
// swapped), and the chain's slack on the chain.
//
// Not ported: the Fine Tuning skill's wider reach and the ride modes (the UE game has no skills,
// and the dash's modes come later), the petrol's octane (the UE shed runs on 95).
#pragma once

#include "BikeLayout.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
using TuneMap = std::map<std::string, double>;

// One setting on the desk: its key (TUNE_LIMITS), its name and its slider's step.
struct TuneRow
{
	std::string Key, Name;
	double Step = 0.1;
};
struct TuneTab
{
	std::string Name;
	std::vector<TuneRow> Rows;
};
// TuningComputer.TABS: Engine, Suspension, Tyres & chain, Gearing, Rider aids.
const std::vector<TuneTab>& TuneTabs();
// TUNE_LIMITS: whether there's such a setting, and its widest range.
bool TuneLimits(const std::string& Key, double& Lo, double& Hi);
// shed_state TUNE_HOME: the slot whose part keeps it ("bike" for the bike itself) and its field.
bool TuneHome(const std::string& Key, std::string& Slot, std::string& Field);
// TuningComputer.AID_HELP: what an aid does, under its slider ("" for a setting that isn't an aid).
std::string AidHelp(const std::string& Key);
// PartsDB.ECU_AID_NAMES: "traction" -> "traction control".
std::string AidName(const std::string& Aid);
// BikeBuild.AID_KEYS in the dash's order: abs, traction, lean_traction, anti_wheelie, stability.
const std::vector<std::string>& AidOrder();

constexpr int AidLevels = 5;      // AID_LEVELS
constexpr int AidStock = 3;       // AID_STOCK
constexpr double ChainSlackStock = 30.0;
constexpr double ChainSlackLo = 20.0, ChainSlackHi = 40.0;   // shed_state CHAIN_SLACK_GOOD
constexpr double LightenWear = 0.08;                         // shed_state LIGHTEN_WEAR

// A bike's parts (as BikeBuild sees them: RideParts) with its tune.
class BikeTune
{
public:
	BikeTune(const BikeLayout& InLay, const PartsMap& InParts, const TuneMap& InTune, double EcuHealth = 1.0);

	const Json& Part(const std::string& Slot) const;
	bool Has(const std::string& Slot) const;

	// Why a setting can't be changed on these parts ("The telescopic road fork has no rebound
	// adjuster."), or "" when it can.
	std::string Block(const std::string& Key) const;
	// How far the ECU lets the fuel map and ignition go either way (0.25 with none fitted).
	double EcuReach() const;
	// The range a setting can take on these parts.
	void Range(const std::string& Key, double& Lo, double& Hi) const;
	// As it comes from the factory.
	double Default(const std::string& Key) const;
	// As it's applied: the tune's value within what the parts allow, or stock.
	double Value(const std::string& Key) const;

	// The ECU's rider aids (has_aid, aid_level, aid_strength, aid_on): aid "traction", "abs", ...
	bool HasAid(const std::string& Aid) const;
	int AidLevel(const std::string& Aid) const;
	double AidStrength(const std::string& Aid) const;
	double EcuAid() const { return EcuAidLevel; }
	// [TFT] The aids come with the dash (no ECU parts in the catalogue); the dash has ride modes; a
	// feature of the dash ("cruise", "modes", "shift_light").
	bool DashAids() const { return bDashAids; }
	bool HasModes() const;
	bool DashHas(const std::string& Feature) const;

	// How far a tyre is off its best pressure, as a fraction (-0.2: 20% soft).
	double PressureOffset(bool bFront) const;
	// _pressure_grip: grip on a surface ("tarmac", "dirt", "grass") at that offset.
	static double PressureGrip(double Dev, const std::string& Surface);
	// _spread: the ratios respaced about top gear.
	static std::vector<double> Spread(const std::vector<double>& Ratios, double Spread);
	// lighten_cut: the share of a part's mass drilling and trimming takes off (0: nothing to trim).
	double LightenCut(const std::string& PartId) const;

	// _tune_engine: the fuel map and ignition on the torque, the limiter, how hard it's worked.
	struct Engine
	{
		double TorqueMult = 1.0;     // on the peak torque
		double Low = 0.0, High = 0.0;   // tune_low, tune_high: the torque below and above the peak
		double Limiter = 0.0;        // rpm on the stock redline
		double Stress = 1.0;         // engine_stress: multiplies its wear
		std::vector<std::string> Notes;
	};
	Engine EngineTune() const;
	// Hot heads need cooling to match (BikeBuild._compute_engine): the engine's heat, which a lean
	// map and ignition advance raise, against the cooling's capacity (its health 0.15..1). The torque
	// multiplier it costs (1 when it copes), and the note when it can't ("" when it can).
	double CoolingMult(double CoolingHealth, std::string* Note = nullptr) const;
	// engine_torque's tune: the torque at X ((rpm - peak) / peak) times this.
	static double TorqueShape(const Engine& E, double X);

	// How a setting reads on the desk: "Rich 20%", "+3.0° advance", "2.10 bar · 30 psi (best 2.3)".
	// StockRedline: the engine's limiter as it came, for the limiter's reading.
	std::string ValueText(const std::string& Key, double V, double StockRedline) const;
	static std::string AidLevelText(const std::string& Aid, int Level);
	// The Rider aids tab's line on what this ECU has, and the engine settings' line on the ECU.
	std::string AidsNote() const;
	std::string EcuText(const std::string& Key) const;

private:
	const BikeLayout& Lay;
	PartsMap Parts;
	TuneMap Tune;
	double EcuAidLevel = 0.0;
	bool bDashAids = false;
	std::vector<std::string> EcuFeatures;
};
} // namespace TorqueFit
