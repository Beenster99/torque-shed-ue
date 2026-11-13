// Ride modes and the TFT's menu (Sim/RideModes, Dash/TftMenu), and the rider aids coming with the
// dash now the ECU parts are out of the game (Fit/BikeTune):
//   - a bike with clocks has plain ABS and nothing else, no modes; a TFT has the lot;
//   - Touring is the dyno desk's levels: a bike put in Touring rides on exactly the spec it had;
//   - Wet gives less power than Sport, and the sim is slower for it;
//   - the menu: left and right through the views, a mode only with the throttle shut, a setting only
//     at a standstill, the rider's changes saved and loaded.
#include "BikeLayout.h"
#include "BikeRideSpec.h"
#include "BikeSim.h"
#include "RideModes.h"
#include "TftMenu.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace TorqueFit;
using TorqueSim::BikeSim;
using TorqueSim::BikeSpec;
using TorqueSim::ModeRow;
using TorqueSim::ModeSet;
using TorqueSim::RideMode;

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
	std::stringstream S;
	S << F.rdbuf();
	Out = S.str();
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
TorqueSim::RayHit FlatGround(const TorqueSim::V3& From, const TorqueSim::V3& To)
{
	TorqueSim::RayHit H;
	if (From.y < 0.0 || To.y > 0.0) return H;
	H.Hit = true;
	H.Position = From + (To - From) * (From.y / (From.y - To.y));
	H.Normal = TorqueSim::V3(0.0, 1.0, 0.0);
	H.Surface = TorqueSim::SURF_TARMAC;
	return H;
}
// Flat out from a standstill for a while: how fast it ends up.
double Sprint(BikeSpec Base, const ModeSet* Mode, double Seconds)
{
	// (the engine's curve is the shed's to work out: a plain one here)
	Base.TorqueRpm = {1000.0, 4000.0, 7000.0, 9000.0};
	Base.TorqueNm = {70.0, 95.0, 90.0, 70.0};
	Base.PeakTorque = 95.0;
	Base.PeakTorqueRpm = 4000.0;
	Base.Redline = 9000.0;
	BikeSim Sim;
	Sim.Setup(Base);
	Sim.Ray = FlatGround;
	Sim.Place(TorqueSim::V3(0.0, 0.0, 0.0), 0.0);
	if (Mode) TorqueSim::ApplyMode(Sim, Base, *Mode);
	Sim.AutoShift = true;
	Sim.Assist = 0.5;
	const double Dt = 1.0 / 120.0;
	for (int I = 0; I < int(Seconds / Dt); ++I)
	{
		Sim.Throttle = 1.0;
		Sim.BodyShift = 0.6;
		Sim.Step(Dt);
	}
	return Sim.Speed;
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string PartsText, LayoutText, Error;
	PartsDb Db;
	BikeLayout Lay;
	const bool Loaded = ReadFile(R + "/Data/Parts/parts.json", PartsText) && ReadFile(R + "/Data/Fitment/layout.json", LayoutText) &&
		Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error);
	Expect(Loaded, "the parts and layout load " + Error);
	if (!Loaded) return 1;

	std::printf("The parts list:\n");
	Expect(Db.Ids("ecu").empty(), "no ECU parts");
	Expect(Db.Ids("satnav").size() == 1 && Db.Ids("satnav")[0] == "phone_nav_mount", "the phone is the only sat nav");
	int Tfts = 0;
	for (const std::string& Id : Db.Ids("dashboard"))
		if (Lay.Part(Id)->Str("style") == "tft") ++Tfts;
	Expect(Tfts == 4, "four TFT units (" + std::to_string(Tfts) + ")");

	auto SpecOf = [&](const std::string& Style, const std::string& Dash, const TuneMap& Tune, BikeSpec& Out) {
		RideSpecInput In;
		In.Parts = Lay.ReferenceBuild(Style);
		if (!Dash.empty()) In.Parts["dashboard"] = Dash;
		In.Tune = Tune;
		return RideSpec(Lay, In, Out);
	};

	std::printf("Rider aids come with the dash:\n");
	BikeSpec Classic, Adv, Race;
	Expect(SpecOf("classic", "", {}, Classic) && SpecOf("adventure", "", {}, Adv) && SpecOf("race", "", {}, Race), "specs for a classic, an adventure bike and a racer");
	Expect(Classic.AbsFront && Classic.AbsRear && !Classic.TractionControl && Classic.AntiWheelie == 0.0 && Classic.Stability == 0.0 && !Classic.Aids.Modes,
		"clocks: plain ABS, no other aids, no modes");
	Expect(Adv.Aids.Modes && Adv.Aids.Abs && Adv.Aids.Traction && Adv.Aids.Wheelie && Adv.Aids.Lean && Adv.Aids.Stability && Adv.Aids.Cruise && Adv.Aids.Enduro && !Adv.Aids.Race,
		"the adventure bike's TFT: every aid, cruise, modes with Enduro");
	Expect(Adv.TractionControl && Adv.TractionStrength > 0.5 && Adv.AntiWheelie > 0.5 && Adv.AbsFront, "its aids are on at the factory level");
	Expect(Race.Aids.Modes && Race.Aids.Race && !Race.Aids.Enduro, "the racer's logger: modes with Race");
	BikeSpec Compact;
	Expect(SpecOf("cafe", "tft_compact", {}, Compact) && Compact.Aids.Modes && Compact.Aids.Traction, "a cafe racer with the Compact TFT has modes and traction control");
	{
		const std::vector<RideMode> M = TorqueSim::ModesFor(Adv.Aids);
		Expect(M.size() == 5 && M[2] == RideMode::Touring && M[4] == RideMode::Enduro, "Wet, Urban, Touring, Sport, Enduro on the adventure bike");
		Expect(TorqueSim::ModesFor(Classic.Aids).empty(), "no modes on the classic");
	}

	std::printf("Touring is the bike as the desk set it:\n");
	for (const TuneMap& Tune : {TuneMap{}, TuneMap{{"aid_traction", 1.0}, {"aid_anti_wheelie", 5.0}, {"aid_abs", 0.0}, {"aid_stability", 2.0}}})
	{
		BikeSpec Base;
		SpecOf("adventure", "", Tune, Base);
		BikeSpec Live = Base;
		TorqueSim::ApplyMode(Live, Base, TorqueSim::Template(RideMode::Touring, Base.Aids));
		const bool Same = Live.TractionControl == Base.TractionControl && std::fabs(Live.TractionStrength - Base.TractionStrength) < 1e-9 &&
			std::fabs(Live.LeanTraction - Base.LeanTraction) < 1e-9 && std::fabs(Live.AntiWheelie - Base.AntiWheelie) < 1e-9 &&
			std::fabs(Live.Stability - Base.Stability) < 1e-9 && Live.AbsFront == Base.AbsFront && Live.AbsRear == Base.AbsRear &&
			Live.PowerMode == 1.0 && Live.ThrottleSoft == 0.0 && Live.EngineBrake == 1.0 && Live.FrontBump == Base.FrontBump &&
			Live.RearRebound == Base.RearRebound && Live.FrontPreload == Base.FrontPreload;
		Expect(Same, Tune.empty() ? "stock levels: the same spec" : "the desk's own levels (traction 1, wheelie 5, ABS off, stability 2): the same spec");
	}
	{
		BikeSpec Live = Classic;
		Live.TractionStrength = 0.123;
		TorqueSim::ApplyMode(Live, Classic, TorqueSim::Template(RideMode::Wet, Classic.Aids));
		Expect(Live.TractionStrength == 0.123 && Live.PowerMode == 1.0, "a bike without modes is left alone");
	}

	std::printf("The modes change the ride:\n");
	{
		const ModeSet Wet = TorqueSim::Template(RideMode::Wet, Adv.Aids), Sport = TorqueSim::Template(RideMode::Sport, Adv.Aids),
			Enduro = TorqueSim::Template(RideMode::Enduro, Adv.Aids), Touring = TorqueSim::Template(RideMode::Touring, Adv.Aids);
		BikeSpec W = Adv, S = Adv, E = Adv;
		TorqueSim::ApplyMode(W, Adv, Wet);
		TorqueSim::ApplyMode(S, Adv, Sport);
		TorqueSim::ApplyMode(E, Adv, Enduro);
		Expect(W.PowerMode < 0.7 && W.ThrottleSoft > 0.0 && W.TractionStrength > S.TractionStrength && W.AntiWheelie > S.AntiWheelie && W.EngineBrake < 1.0,
			"Wet: low power, a soft throttle, the aids in sooner than Sport, light engine braking");
		Expect(S.PowerMode == 1.0 && S.ThrottleSoft < 0.0, "Sport: full power, a sharp throttle");
		Expect(E.AbsFront && !E.AbsRear && E.TractionStrength < W.TractionStrength, "Enduro: the back wheel can lock and spin");
		Expect(W.FrontBump == Adv.FrontBump, "no semi-active suspension: the damping stays");
		const double Plain = Sprint(Adv, nullptr, 6.0), InTouring = Sprint(Adv, &Touring, 6.0), InWet = Sprint(Adv, &Wet, 6.0), InSport = Sprint(Adv, &Sport, 6.0);
		char Line[200];
		std::snprintf(Line, sizeof(Line), "6 s flat out: %.1f m/s as built, %.1f in Touring, %.1f in Wet, %.1f in Sport", Plain, InTouring, InWet, InSport);
		Expect(std::fabs(Plain - InTouring) < 1e-9 && InWet < InTouring - 3.0 && InSport >= InTouring - 0.5, Line);
		// Semi-active suspension: the mode sets the damping.
		BikeSpec Semi = Adv;
		Semi.Aids.SemiActive = true;
		BikeSpec Soft = Semi, Firm = Semi;
		TorqueSim::ApplyMode(Soft, Semi, Wet);
		TorqueSim::ApplyMode(Firm, Semi, Sport);
		Expect(Soft.FrontBump < Semi.FrontBump && Firm.FrontBump > Semi.FrontBump && Firm.RearRebound > Semi.RearRebound, "semi-active: soft in Wet, firm in Sport");
		BikeSim Sim;
		Sim.Setup(Semi);
		TorqueSim::ApplyMode(Sim, Semi, Sport);
		Expect(Sim.Wheels[0].Bump == Firm.FrontBump && Sim.Wheels[1].Rebound == Firm.RearRebound, "and a bike being ridden takes it at once");
	}
	{
		BikeSpec Fitted;
		RideSpecInput In;
		In.Parts = Lay.ReferenceBuild("adventure");
		In.Parts["fork"] = "semi_active_adventure";
		In.Parts["shock"] = "semi_active_adventure_mono";
		Expect(RideSpec(Lay, In, Fitted) && Fitted.Aids.SemiActive && !Adv.Aids.SemiActive, "the semi-active fork and shock parts make it so");
	}
	{
		double Held = 0.0, Speed = 20.0;
		for (int I = 0; I < 2000; ++I)
		{
			const double T = TorqueSim::CruiseThrottle(Speed, 30.0, Held, 0.01);
			Speed += (T * 6.0 - 0.004 * Speed * Speed) * 0.01;   // a made-up bike: thrust against drag
		}
		Expect(std::fabs(Speed - 30.0) < 0.5, "cruise control settles on its speed (" + std::to_string(Speed) + " for 30 m/s)");
	}

	std::printf("The screen's menu:\n");
	{
		using namespace TorqueTft;
		Menu M;
		M.Setup(Adv.Aids, true);
		State St;
		M.Fill(St);
		Expect(St.Mode == "TOURING" && St.Modes.size() == 5 && St.ModeNow == 2 && St.Aids.size() == 4, "starts in Touring, five modes, four aid boxes");
		M.Do(Press::Right, 20.0, 0.5);
		Expect(M.Now == View::Map, "right: the map");
		M.Do(Press::Right, 20.0, 0.5);
		Expect(M.Now == View::Split, "right again on a wide screen: the split view");
		M.Do(Press::Right, 20.0, 0.5);
		Expect(M.Now == View::Modes, "then the modes");
		M.Do(Press::Down, 20.0, 0.5);
		Expect((M.Do(Press::Right, 20.0, 0.5) & Menu::ModeChanged) == 0 && M.Mode() == RideMode::Touring && !M.Notice.empty(), "a mode isn't changed with the throttle open (it says so)");
		Expect((M.Do(Press::Right, 20.0, 0.0) & Menu::ModeChanged) != 0 && M.Mode() == RideMode::Sport, "throttle shut: Sport");
		M.Do(Press::Right, 20.0, 0.0);
		Expect(M.Now == View::Settings, "right from the modes (on the mode in use): the settings");
		M.Fill(St);
		Expect(St.Settings.size() >= 8 && St.Settings[0].Label == "Screen design", "the settings list the design and the mode's rows");
		M.Do(Press::Right, 20.0, 0.0);
		Expect((M.Do(Press::Up, 20.0, 0.0) & Menu::LookChanged) != 0 && M.Look == Design::Sweep, "the design can be changed on the move");
		M.Do(Press::Left, 20.0, 0.0);
		M.Do(Press::Down, 20.0, 0.0);
		M.Do(Press::Down, 20.0, 0.0);
		M.Do(Press::Down, 20.0, 0.0);   // (power, throttle, traction)
		M.Fill(St);
		Expect(St.Settings[size_t(St.SetCursor)].Label == "Traction control", "down to traction control");
		M.Do(Press::Right, 20.0, 0.0);
		M.Fill(St);
		Expect(!St.SetEditing && !M.Notice.empty(), "a setting isn't changed on the move");
		M.Do(Press::Right, 0.0, 0.0);
		const int Was = M.Set().Traction;
		const int What = M.Do(Press::Up, 0.0, 0.0);
		Expect((What & Menu::ModeChanged) && (What & Menu::SetChanged) && M.Set().Traction == Was + 1 && M.Set().Lean == Was + 1, "stopped: traction control up a level (its lean side with it)");
		M.Do(Press::Left, 0.0, 0.0);
		const std::map<std::string, int> Saved = M.Give();
		Expect(Saved.size() == 1 && Saved.count("sport.traction") == 1 && Saved.at("sport.traction") == Was + 1, "only that change is saved");
		Menu Again;
		Again.Setup(Adv.Aids, true, "sport");
		Again.Take(Saved);
		Expect(Again.Mode() == RideMode::Sport && Again.Set().Traction == Was + 1 && Again.SetOf(RideMode::Wet) == TorqueSim::Template(RideMode::Wet, Adv.Aids), "and comes back, the other modes as they came");
		Menu Plain;
		Plain.Setup(Classic.Aids, false);
		Plain.Do(Press::Right, 0.0, 0.0);
		Plain.Do(Press::Right, 0.0, 0.0);
		Expect(Plain.Now == View::Settings, "a screen without modes: clocks, map, settings");
		// Every view of every screen still draws with the menu's own lists.
		for (const Shape Sh : {Shape::Wide, Shape::Ultra, Shape::Round})
			for (int V = 0; V < int(View::Count); ++V)
			{
				M.Now = View(V);
				M.Fill(St);
				Paint P;
				Draw(P, Sh, St, nullptr);
				if (P.Cmds.size() < 4) Expect(false, "view " + std::to_string(V) + " draws");
			}
	}

	std::printf(Failures ? "\n%d FAILED\n" : "\nall ok\n", Failures);
	return Failures ? 1 : 0;
}
