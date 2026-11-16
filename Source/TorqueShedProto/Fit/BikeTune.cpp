#include "BikeTune.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>

namespace TorqueFit
{
namespace
{
const Json TnEmpty;

double TnClamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
double TnLerp(double A, double B, double T) { return A + (B - A) * T; }
// GDScript's roundi: halves away from zero.
long TnRound(double V) { return std::lround(V); }

std::string TnFormat(const char* Fmt, ...)
{
	char Buf[512];
	va_list Args;
	va_start(Args, Fmt);
	std::vsnprintf(Buf, sizeof(Buf), Fmt, Args);
	va_end(Args);
	return Buf;
}

bool TnEndsWith(const std::string& S, const std::string& End)
{
	return S.size() >= End.size() && S.compare(S.size() - End.size(), End.size(), End) == 0;
}
bool TnStartsWith(const std::string& S, const std::string& Start) { return S.compare(0, Start.size(), Start) == 0; }

std::string TnLower(std::string S)
{
	for (char& C : S) C = (char)std::tolower((unsigned char)C);
	return S;
}

struct FTnLimit { const char* Key; double Lo, Hi; };
// bike_build.gd TUNE_LIMITS.
const FTnLimit TnLimits[] = {
	{"map", -1.0, 1.0}, {"ignition", -1.0, 1.0}, {"limiter", -2500.0, 1000.0},
	{"f_preload", -1.0, 1.0}, {"f_bump", -1.0, 1.0}, {"f_rebound", -1.0, 1.0},
	{"r_preload", -1.0, 1.0}, {"r_bump", -1.0, 1.0}, {"r_rebound", -1.0, 1.0},
	{"psi_f", 0.6, 3.2}, {"psi_r", 0.6, 3.2}, {"sprocket_f", 12.0, 18.0}, {"sprocket_r", 36.0, 56.0},
	{"spread", -1.0, 1.0}, {"chain", 10.0, 70.0},
	{"aid_traction", 0.0, 5.0}, {"aid_stability", 0.0, 5.0}, {"aid_anti_wheelie", 0.0, 5.0}, {"aid_lean_traction", 0.0, 5.0},
	{"aid_abs", 0.0, 1.0}, {"damper", -1.0, 1.0}, {"screen", 0.0, 1.0},
};

struct FTnHome { const char* Key; const char* Slot; const char* Field; };
// shed_state.gd TUNE_HOME.
const FTnHome TnHomes[] = {
	{"map", "bike", "map"}, {"ignition", "bike", "ignition"}, {"limiter", "bike", "limiter"},
	{"aid_traction", "bike", "aid_traction"}, {"aid_stability", "bike", "aid_stability"},
	{"aid_anti_wheelie", "bike", "aid_anti_wheelie"}, {"aid_lean_traction", "bike", "aid_lean_traction"}, {"aid_abs", "bike", "aid_abs"},
	{"f_preload", "fork", "preload"}, {"f_bump", "fork", "bump"}, {"f_rebound", "fork", "rebound"},
	{"r_preload", "shock", "preload"}, {"r_bump", "shock", "bump"}, {"r_rebound", "shock", "rebound"},
	{"psi_f", "front_tyre", "pressure"}, {"psi_r", "rear_tyre", "pressure"},
	{"sprocket_f", "final_drive", "front"}, {"sprocket_r", "final_drive", "rear"}, {"spread", "gearbox", "spread"},
	{"chain", "chain", "slack"}, {"damper", "steering_damper", "clicks"}, {"screen", "fairing", "screen"},
};

// AID_SCALE: how firmly each level steps in next to the factory's.
const double TnAidScale[AidLevels + 1] = {0.0, 0.4, 0.7, 1.0, 1.25, 1.5};

// LIGHTEN by kind.
double TnLightenOf(const std::string& Kind)
{
	static const std::map<std::string, double> L = {{"frame", 0.08}, {"fork", 0.06}, {"shock", 0.06}, {"wheel", 0.12}, {"disc", 0.15},
		{"saddle", 0.25}, {"handlebars", 0.15}, {"exhaust", 0.15}, {"final_drive", 0.3}, {"gearbox", 0.06}, {"side_stand", 0.3},
		{"centre_stand", 0.3}};
	const auto It = L.find(Kind);
	return It == L.end() ? 0.0 : It->second;
}

std::string TnAidOf(const std::string& Key) { return TnStartsWith(Key, "aid_") ? Key.substr(4) : std::string(); }
bool TnIsSwitch(const std::string& Aid) { return Aid == "abs"; }   // AID_SWITCHES
} // namespace

const std::vector<TuneTab>& TuneTabs()
{
	static const std::vector<TuneTab> Tabs = {
		{"Engine", {{"map", "Fuel map", 0.05}, {"ignition", "Ignition timing", 0.05}, {"limiter", "Rev limiter", 100.0}}},
		{"Suspension", {{"f_preload", "Front preload", 0.1}, {"f_bump", "Front compression", 0.1}, {"f_rebound", "Front rebound", 0.1},
			{"r_preload", "Rear preload", 0.1}, {"r_bump", "Rear compression", 0.1}, {"r_rebound", "Rear rebound", 0.1},
			{"damper", "Steering damper", 0.1}}},
		{"Tyres & chain", {{"psi_f", "Front tyre pressure", 0.05}, {"psi_r", "Rear tyre pressure", 0.05}, {"chain", "Chain slack", 1.0}}},
		{"Gearing", {{"sprocket_f", "Front sprocket", 1.0}, {"sprocket_r", "Rear sprocket", 1.0}, {"spread", "Ratio spacing", 0.1}}},
		{"Rider aids", {{"aid_abs", "ABS", 1.0}, {"aid_traction", "Traction control", 1.0}, {"aid_lean_traction", "Lean-angle traction control", 1.0},
			{"aid_stability", "Stability control", 1.0}, {"aid_anti_wheelie", "Anti-wheelie", 1.0}}},
	};
	return Tabs;
}

bool TuneLimits(const std::string& Key, double& Lo, double& Hi)
{
	for (const FTnLimit& L : TnLimits)
		if (Key == L.Key)
		{
			Lo = L.Lo;
			Hi = L.Hi;
			return true;
		}
	Lo = Hi = 0.0;
	return false;
}

bool TuneHome(const std::string& Key, std::string& Slot, std::string& Field)
{
	for (const FTnHome& H : TnHomes)
		if (Key == H.Key)
		{
			Slot = H.Slot;
			Field = H.Field;
			return true;
		}
	return false;
}

std::string AidHelp(const std::string& Key)
{
	if (Key == "aid_traction") return "Cuts the power when the back tyre spins up. Higher steps in sooner and harder; off lets it slide.";
	if (Key == "aid_stability") return "Damps a weave, a tank-slapper or a twitchy slide. Higher holds the bike steadier.";
	if (Key == "aid_anti_wheelie") return "Trims the power to keep the front wheel down. TnLower lets it lift further; off for wheelies.";
	if (Key == "aid_lean_traction") return "Traction control that tightens as the bike leans: cuts the power sooner the further over you are.";
	if (Key == "aid_abs") return "Stops the wheels locking under hard braking. Off lets them lock.";
	return std::string();
}

std::string AidName(const std::string& Aid)
{
	if (Aid == "traction") return "traction control";
	if (Aid == "abs") return "ABS";
	if (Aid == "anti_wheelie") return "anti-wheelie";
	if (Aid == "stability") return "stability control";
	if (Aid == "lean_traction") return "lean-angle traction control";
	return Aid;
}

const std::vector<std::string>& AidOrder()
{
	static const std::vector<std::string> Order = {"abs", "traction", "lean_traction", "anti_wheelie", "stability"};
	return Order;
}

BikeTune::BikeTune(const BikeLayout& InLay, const PartsMap& InParts, const TuneMap& InTune, double EcuHealth)
	: Lay(InLay), Parts(InParts), Tune(InTune)
{
	// [TFT] This game has no ECU parts: the rider aids, and how far the engine can be tuned, come with
	// the dash (a TFT or the race logger has the lot, clocks and an LCD plain ABS). A catalogue that
	// still lists ECUs (the Godot game's, as SimTest compares against) works as the Godot game does.
	bDashAids = !Lay.Catalogue() || Lay.Catalogue()->Ids("ecu").empty();
	const Json& Ecu = Part(bDashAids ? "dashboard" : "ecu");
	if (const Json* F = Ecu.Get(bDashAids ? "aids" : "features"))
		for (const Json& X : F->A)
			if (X.S != "remap") EcuFeatures.push_back(X.S);
	EcuAidLevel = Ecu.Num("aid_level", 0.0) * TnLerp(0.5, 1.0, TnClamp(EcuHealth, 0.15, 1.0));
}

bool BikeTune::HasModes() const
{
	if (!bDashAids) return false;
	if (const Json* F = Part("dashboard").Get("features"))
		for (const Json& X : F->A)
			if (X.S == "modes") return true;
	return false;
}

bool BikeTune::DashHas(const std::string& Feature) const
{
	if (const Json* F = Part("dashboard").Get("features"))
		for (const Json& X : F->A)
			if (X.S == Feature) return true;
	return false;
}

bool BikeTune::Has(const std::string& Slot) const
{
	const auto It = Parts.find(Slot);
	return It != Parts.end() && !It->second.empty() && It->second != "none";
}

const Json& BikeTune::Part(const std::string& Slot) const
{
	const auto It = Parts.find(Slot);
	if (It == Parts.end()) return TnEmpty;
	const Json* P = Lay.Part(It->second);
	return P ? *P : TnEmpty;
}

std::string BikeTune::Block(const std::string& Key) const
{
	double Lo = 0.0, Hi = 0.0;
	if (!TuneLimits(Key, Lo, Hi)) return "There's nothing like that to adjust.";
	if (TnEndsWith(Key, "_preload") || TnEndsWith(Key, "_bump") || TnEndsWith(Key, "_rebound"))
	{
		const Json& P = Part(TnStartsWith(Key, "f_") ? "fork" : "shock");
		const std::string What = Key.substr(2);
		const int Need = What == "preload" ? 1 : (What == "rebound" ? 2 : 4);
		if ((int)P.Num("adjust", 0.0) < Need) return "The " + TnLower(P.Str("name", "suspension")) + " has no " + What + " adjuster.";
	}
	else if (Key == "spread" && !Part("gearbox").Flag("cassette")) return "Only a cassette gearbox lets you respace the ratios.";
	else if (TnStartsWith(Key, "aid_") && !HasAid(TnAidOf(Key))) return std::string(bDashAids ? "This dash has no " : "This ECU has no ") + AidName(TnAidOf(Key)) + ".";
	else if (Key == "screen")
	{
		if (!Has("fairing")) return "There's no fairing fitted.";
		if (Part("fairing").Num("screen_adjust", 0.0) <= 0.0) return "This screen doesn't adjust.";
	}
	else if (Key == "damper")
	{
		if (!Has("steering_damper")) return "There's no steering damper fitted.";
		if ((int)Part("steering_damper").Num("adjust", 0.0) < 1) return "This steering damper has no adjuster.";
	}
	return std::string();
}

double BikeTune::EcuReach() const
{
	const char* Slot = bDashAids ? "dashboard" : "ecu";
	return Has(Slot) ? Part(Slot).Num("tune_range", 0.25) : 0.25;
}

void BikeTune::Range(const std::string& Key, double& Lo, double& Hi) const
{
	TuneLimits(Key, Lo, Hi);
	if (Key == "map" || Key == "ignition")
	{
		const double E = std::min(EcuReach(), Hi);
		Lo = -E;
		Hi = E;
	}
	else if (Key == "limiter")
	{
		const char* Slot = bDashAids ? "dashboard" : "ecu";
		Hi = Has(Slot) ? Part(Slot).Num("limiter_add", 0.0) : 0.0;
	}
}

double BikeTune::Default(const std::string& Key) const
{
	if (Key == "psi_f" || Key == "psi_r")
	{
		const std::string Style = Part(Key == "psi_f" ? "front_tyre" : "rear_tyre").Str("style", "road");
		// PSI_BEST.
		if (Style == "offroad") return 1.0;
		if (Style == "slick") return Key == "psi_f" ? 2.1 : 1.8;
		return Key == "psi_f" ? 2.3 : 2.5;
	}
	if (Key == "sprocket_f") return Part("final_drive").Num("front", 15.0);
	if (Key == "sprocket_r") return Part("final_drive").Num("rear", 45.0);
	if (Key == "chain") return ChainSlackStock;
	if (Key == "aid_traction" || Key == "aid_stability" || Key == "aid_anti_wheelie" || Key == "aid_lean_traction") return (double)AidStock;
	if (Key == "aid_abs") return 1.0;
	return 0.0;
}

double BikeTune::Value(const std::string& Key) const
{
	const auto It = Tune.find(Key);
	if (It == Tune.end() || !Block(Key).empty()) return Default(Key);
	double Lo = 0.0, Hi = 0.0;
	Range(Key, Lo, Hi);
	return TnClamp(It->second, Lo, Hi);
}

bool BikeTune::HasAid(const std::string& Aid) const
{
	return std::find(EcuFeatures.begin(), EcuFeatures.end(), Aid) != EcuFeatures.end();
}

int BikeTune::AidLevel(const std::string& Aid) const
{
	const std::vector<std::string>& Order = AidOrder();
	if (std::find(Order.begin(), Order.end(), Aid) == Order.end()) return 0;
	const int Top = TnIsSwitch(Aid) ? 1 : AidLevels;
	return (int)TnClamp((double)TnRound(Value("aid_" + Aid)), 0.0, (double)Top);
}

double BikeTune::AidStrength(const std::string& Aid) const
{
	if (!HasAid(Aid)) return 0.0;
	const std::vector<std::string>& Order = AidOrder();
	if (std::find(Order.begin(), Order.end(), Aid) == Order.end()) return EcuAidLevel;
	if (TnIsSwitch(Aid)) return AidLevel(Aid) > 0 ? EcuAidLevel : 0.0;
	return TnClamp(EcuAidLevel * TnAidScale[AidLevel(Aid)], 0.0, 1.2);
}

double BikeTune::PressureOffset(bool bFront) const
{
	const char* Key = bFront ? "psi_f" : "psi_r";
	const double Best = Default(Key);
	return (Value(Key) - Best) / Best;
}

double BikeTune::PressureGrip(double Dev, const std::string& Surface)
{
	if (Surface == "tarmac") return 1.0 - 0.5 * Dev * Dev - 0.12 * std::max(Dev, 0.0);
	return 1.0 + 0.15 * TnClamp(-Dev, 0.0, 0.4) - 0.5 * Dev * Dev;
}

std::vector<double> BikeTune::Spread(const std::vector<double>& Ratios, double S)
{
	if (S == 0.0 || Ratios.empty()) return Ratios;
	const double Top = Ratios.back();
	std::vector<double> Out;
	for (const double R : Ratios) Out.push_back(Top * std::pow(R / Top, 1.0 + 0.3 * S));
	return Out;
}

double BikeTune::LightenCut(const std::string& PartId) const
{
	const Json* P = Lay.Part(PartId);
	if (!P || P->Flag("lightweight")) return 0.0;
	return std::min(TnLightenOf(Lay.KindOfPart(PartId)), 0.5);
}

BikeTune::Engine BikeTune::EngineTune() const
{
	Engine E;
	const double M = Value("map");
	const double A = Value("ignition");
	const double KnockMargin = 0.45;   // knock_margin on 95 octane
	const double Knock = std::max(0.0, A - KnockMargin);
	E.TorqueMult = (1.0 + 0.04 * M - 0.055 * M * M) * (1.0 + 0.045 * A - 0.02 * A * A) * (1.0 - 0.15 * Knock);
	E.Low = 0.05 * M;
	E.High = 0.03 * A;
	E.Limiter = Value("limiter");
	const double Over = std::max(E.Limiter, 0.0);
	E.Stress = 1.0 + 1.2 * std::max(-M, 0.0) + 0.5 * std::max(A, 0.0) + 3.0 * Knock + Over / 1000.0;
	if (Knock > 0.0) E.Notes.push_back("Pinking: too much ignition advance for this petrol. Back it off or use higher octane.");
	if (M < -0.5) E.Notes.push_back("Running lean: thrifty, but hot and hard on the engine.");
	if (Over > 0.0) E.Notes.push_back("The rev limiter is raised: more revs, more wear.");
	return E;
}

double BikeTune::CoolingMult(double CoolingHealth, std::string* Note) const
{
	const Json& Block = Part("engine");
	const Json& Cooling = Part("cooling");
	const double Lean = std::max(-Value("map"), 0.0);
	const double Heat = Block.Num("displacement", 0.0) / 1000.0 * Part("head").Num("heat", 1.0) * Block.Num("breathing", 1.0) *
		(1.0 + 0.15 * Lean + 0.06 * std::max(Value("ignition"), 0.0));
	const double Capacity = Cooling.Num("capacity", 0.0) * TnLerp(0.6, 1.0, TnClamp(CoolingHealth, 0.15, 1.0));
	if (Heat <= Capacity) return 1.0;
	if (Note) *Note = "Runs hot: the " + TnLower(Cooling.Str("name", "cooling")) + " can't cope.";
	return std::max(0.8, 1.0 - (Heat - Capacity) * 0.4);
}

double BikeTune::TorqueShape(const Engine& E, double X)
{
	if (X < 0.0) return 1.0 + E.Low * std::min(-X * 2.0, 1.0);
	return 1.0 + E.High * std::min(X * 2.0, 1.0);
}

std::string BikeTune::AidLevelText(const std::string& Aid, int Level)
{
	if (Level <= 0) return "Off";
	if (TnIsSwitch(Aid)) return "On";
	return TnFormat("Level %d of %d%s", Level, AidLevels, Level == AidStock ? " (factory)" : "");
}

std::string BikeTune::ValueText(const std::string& Key, double V, double StockRedline) const
{
	if (Key == "map")
		return std::fabs(V) < 0.01 ? std::string("Stock") : TnFormat("%s %ld%%", V > 0.0 ? "Rich" : "Lean", TnRound(std::fabs(V) * 100.0));
	if (Key == "ignition")
		return std::fabs(V) < 0.01 ? std::string("Stock") : TnFormat("%+.1f\xC2\xB0 %s", V * 6.0, V > 0.0 ? "advance" : "retard");
	if (Key == "limiter") return TnFormat("%ld rpm (%+ld)", TnRound(StockRedline + V), TnRound(V));
	if (Key == "psi_f" || Key == "psi_r")
	{
		const double Best = Default(Key);
		return TnFormat("%.2f bar \xC2\xB7 %ld psi", V, TnRound(V * 14.504)) + (std::fabs(V - Best) < 0.03 ? std::string() : TnFormat(" (best %.1f)", Best));
	}
	if (Key == "chain")
		return TnFormat("%ld mm (%s)", TnRound(V), V < ChainSlackLo ? "too tight" : (V > ChainSlackHi ? "too slack" : "right"));
	if (Key == "sprocket_f" || Key == "sprocket_r")
	{
		const double F = Key == "sprocket_f" ? V : Value("sprocket_f");
		const double R = Key == "sprocket_r" ? V : Value("sprocket_r");
		return TnFormat("%ld teeth (%.2f:1)", TnRound(V), R / F);
	}
	if (Key == "spread")
		return std::fabs(V) < 0.01 ? std::string("Stock") : TnFormat("%s %ld%%", V > 0.0 ? "Wider" : "Closer", TnRound(std::fabs(V) * 100.0));
	if (TnStartsWith(Key, "aid_")) return AidLevelText(TnAidOf(Key), (int)TnRound(V));
	const long Clicks = TnRound(V * 10.0);
	return Clicks == 0 ? std::string("Stock") : TnFormat("%+ld clicks (%s)", Clicks, Clicks > 0 ? "firmer" : "softer");
}

std::string BikeTune::AidsNote() const
{
	if (bDashAids)
	{
		const std::string Dash = Has("dashboard") ? Part("dashboard").Str("name", Parts.at("dashboard")) : std::string("No dash");
		std::vector<std::string> Has2;
		for (const std::string& Aid : AidOrder())
			if (HasAid(Aid)) Has2.push_back(AidName(Aid));
		if (Has2.empty()) return Dash + ": no rider aids. A TFT dash brings traction control, wheelie control and ride modes.";
		if (Has2.size() == 1) return Dash + ": " + Has2[0] + " only, on or off. A TFT dash brings traction control, wheelie control and ride modes.";
		std::string Words;
		for (size_t I = 0; I + 1 < Has2.size(); ++I) Words += (I ? ", " : "") + Has2[I];
		Words += " and " + Has2.back();
		return Dash + ": set its " + Words + TnFormat(" here, for Touring mode. Each goes from off to level %d (ABS just on or off); level %d is how it comes from the factory. The other ride modes are set from the bike's screen.", AidLevels, AidStock);
	}
	const std::string Ecu = Has("ecu") ? Part("ecu").Str("name", Parts.at("ecu")) : std::string();
	std::vector<std::string> Names;
	for (const std::string& Aid : AidOrder())
		if (HasAid(Aid)) Names.push_back(AidName(Aid));
	if (Names.empty())
		return Ecu.empty() ? std::string("No ECU fitted, so no rider aids. ECU Safety, or a sport or race ECU, brings rider aids such as traction control and anti-wheelie.")
			: Ecu + ": no rider aids to set. ECU Safety, or a sport or race ECU, brings rider aids such as traction control and anti-wheelie.";
	std::string List;
	if (Names.size() <= 2) List = Names.size() == 1 ? Names[0] : Names[0] + " and " + Names[1];
	else
	{
		for (size_t I = 0; I + 1 < Names.size(); ++I) List += (I ? ", " : "") + Names[I];
		List += " and " + Names.back();
	}
	return Ecu + ": set its " + List + TnFormat(" here. Each goes from off to level %d (ABS just on or off); level %d is how it comes from the factory.", AidLevels, AidStock);
}

std::string BikeTune::EcuText(const std::string& Key) const
{
	if (bDashAids)
	{
		const std::string Dash = Has("dashboard") ? Part("dashboard").Str("name", Parts.at("dashboard")) : std::string("No dash");
		if (Key == "limiter")
		{
			double Lo = 0.0, Hi = 0.0;
			Range("limiter", Lo, Hi);
			return Dash + TnFormat(": up to %+ld rpm over stock.", TnRound(Hi));
		}
		if (EcuReach() < 1.0) return Dash + TnFormat(": %ld%% of the range (jets and timing plate). A TFT dash's engine management opens it all up.", TnRound(EcuReach() * 100.0));
		return Dash + ": full range.";
	}
	const std::string Ecu = Has("ecu") ? Part("ecu").Str("name", Parts.at("ecu")) : std::string("No ECU (jets and timing plate)");
	if (Key == "limiter")
	{
		double Lo = 0.0, Hi = 0.0;
		Range("limiter", Lo, Hi);
		return Ecu + TnFormat(": up to %+ld rpm over stock.", TnRound(Hi));
	}
	if (EcuReach() < 1.0) return Ecu + TnFormat(": %ld%% of the range. A remap or race ECU opens it all up.", TnRound(EcuReach() * 100.0));
	return Ecu + ": full range.";
}
} // namespace TorqueFit
