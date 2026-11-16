#include "BikeRideSpec.h"

#include "../Sim/TyreModel.h"
#include <cctype>

#include <algorithm>
#include <cmath>

namespace TorqueFit
{
namespace
{
using TorqueSim::V3;

// bike_build.gd's constants.
constexpr double RsRiderMass = 75.0;
constexpr double RsFixedMass = 36.0;
constexpr double RsFuelDensity = 0.74, RsOilDensity = 0.87;
constexpr double RsDrivetrain = 0.92;
constexpr double RsRolling = 0.015;
constexpr double RsPreloadStock = 0.008;
constexpr double RsPreloadClick = 0.008;
constexpr double RsChainPitch = 0.015875;
constexpr double RsShortTrail = 0.06;
constexpr double RsProperStyle = 0.7;   // PartsDB.PROPER_STYLE
constexpr double RsMasterFluid = 0.25;  // PartsDB.MASTER_FLUID
constexpr double RsPi = 3.14159265358979323846;

double RsClamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
double RsLerp(double A, double B, double T) { return A + (B - A) * T; }
double RsDeg(double D) { return D * RsPi / 180.0; }

double RsTable(const char* const (&Keys)[5], const double (&Values)[5], const std::string& Key, double Default)
{
	for (int I = 0; I < 5; ++I)
		if (Keys[I] && Key == Keys[I]) return Values[I];
	return Default;
}

// FORK_OFFSET, FORK_PATH_TILT, FORK_LINK, FORK_PROGRESSION by fork style.
const char* const RsForkStyles[5] = {"conventional", "usd", "girder", "springer", "leading_link"};
const double RsForkOffset[5] = {0.045, 0.032, 0.045, 0.08, 0.05};
const double RsForkTilt[5] = {0.0, 0.0, 14.0, 10.0, 30.0};
const double RsForkLinkTable[5] = {0.0, 0.0, 0.0, 0.0, 0.3};
const double RsForkProgression[5] = {0.2, 0.25, 0.05, 0.05, 0.1};
// LEAN_CLEARANCE by frame style, FLICK by style.
const char* const RsLowStyles[5] = {"softail", "cruiser", "chopper", nullptr, nullptr};
const double RsLowClearance[5] = {31.0, 32.0, 30.0, 0.0, 0.0};
const char* const RsFlickStyles[5] = {"race", "sport", "naked", nullptr, nullptr};
const double RsFlickTable[5] = {3.0, 2.8, 2.2, 0.0, 0.0};

double RsShockProgression(const std::string& Kind)
{
	std::string T = Kind;
	std::transform(T.begin(), T.end(), T.begin(), [](unsigned char C) { return (char)std::tolower(C); });
	const char* Keys[4] = {"air", "linkage", "mono", "twin"};
	const double Values[4] = {0.7, 0.45, 0.3, 0.1};
	for (int I = 0; I < 4; ++I)
		if (T.find(Keys[I]) != std::string::npos) return Values[I];
	return 0.2;
}

// Where each slot's part sits (PART_SPOT): share of the wheelbase back from the front axle, height (m).
struct RsSpot { const char* Slot; double Along, Up; };
const RsSpot RsSpots[] = {
	{"frame", 0.45, 0.55}, {"fork", 0.06, 0.62}, {"shock", 0.72, 0.5}, {"front_wheel", 0.0, 0.3}, {"rear_wheel", 1.0, 0.31},
	{"front_tyre", 0.0, 0.3}, {"rear_tyre", 1.0, 0.31}, {"front_disc", 0.0, 0.3}, {"rear_disc", 1.0, 0.31},
	{"front_caliper", 0.0, 0.35}, {"rear_caliper", 1.0, 0.33}, {"front_master", 0.14, 1.0}, {"rear_master", 0.68, 0.38},
	{"tank", 0.32, 0.86}, {"saddle", 0.62, 0.82}, {"handlebars", 0.14, 1.0}, {"dashboard", 0.12, 1.02},
	{"engine", 0.45, 0.42}, {"head", 0.4, 0.66}, {"fuel", 0.42, 0.62}, {"air_filter", 0.45, 0.68}, {"oil_filter", 0.4, 0.28},
	{"spark_plugs", 0.4, 0.7}, {"cooling", 0.22, 0.52}, {"gearbox", 0.55, 0.38}, {"exhaust", 0.72, 0.38},
	{"final_drive", 0.62, 0.32}, {"chain", 0.8, 0.32}, {"side_stand", 0.58, 0.2}, {"centre_stand", 0.62, 0.2},
	{"headlight", -0.04, 0.95}, {"horn", 0.1, 0.55}, {"ecu", 0.65, 0.65}, {"panniers", 0.92, 0.62}, {"top_box", 1.02, 0.98},
	{"fog_lights", -0.02, 0.6}, {"tail", 0.9, 0.8}, {"fairing", 0.06, 0.98}, {"mirrors", 0.14, 1.12},
	{"front_mudguard", 0.0, 0.62}, {"indicators", 0.5, 0.85}, {"side_panels", 0.6, 0.65}, {"rearsets", 0.68, 0.38},
	{"guards", 0.38, 0.4}, {"number_boards", 0.75, 0.75}, {"swingarm", 0.82, 0.35}, {"steering_damper", 0.16, 0.92},
	{"tank_bag", 0.3, 1.0}, {"satnav", 0.12, 1.12}, {"aux_tank", 0.9, 0.65}, {"toolbox", 0.9, 0.5},
	{"lever_guards", 0.14, 1.0},
};
// STYLE_SPOT: the auxiliary tank and the toolbox by their style.
struct RsStyleSpot { const char* Slot; const char* Style; double Along, Up; };
const RsStyleSpot RsStyleSpots[] = {
	{"aux_tank", "twin", 0.3, 0.42}, {"aux_tank", "rear", 0.95, 0.78}, {"aux_tank", "cell", 0.88, 0.58}, {"aux_tank", "can", 0.92, 0.6},
	{"aux_tank", "bladder", 1.02, 0.98},
	{"toolbox", "bash", 0.42, 0.14}, {"toolbox", "roll", 0.03, 0.62}, {"toolbox", "tube", 0.86, 0.48}, {"toolbox", "box", 0.92, 0.5},
	{"toolbox", "case", 1.05, 0.95},
};

const Json RsEmpty = Json::MakeObject();

// What BikeBuild reads its parts with.
struct RsBuild
{
	const BikeLayout& Lay;
	PartsMap Parts;
	const std::map<std::string, double>& HealthIn;
	int Cylinders = 2;
	std::map<std::string, double> Cut;   // a lightened slot: the share of its mass taken off

	bool Has(const std::string& Slot) const
	{
		const auto It = Parts.find(Slot);
		return It != Parts.end() && It->second != "none" && !It->second.empty();
	}
	const Json& Part(const std::string& Slot) const
	{
		const auto It = Parts.find(Slot);
		if (It == Parts.end()) return RsEmpty;
		const Json* P = Lay.Part(It->second);
		return P ? *P : RsEmpty;
	}
	double Health(const std::string& Slot) const
	{
		if (!Has(Slot)) return 1.0;
		const auto It = HealthIn.find(Slot);
		return It == HealthIn.end() ? 1.0 : RsClamp(It->second, 0.15, 1.0);
	}
	double Mass(const std::string& Slot) const
	{
		const Json& P = Part(Slot);
		const auto C = Cut.find(Slot);
		return P.Num("mass", P.Num("mass_per_cyl", 0.0) * Cylinders) * (C == Cut.end() ? 1.0 : 1.0 - C->second);
	}
	double Grip(const Json& Tyre, const std::string& Surf) const
	{
		const Json* G = Tyre.Get("grip");
		const Json* V = G ? G->Get(Surf) : nullptr;
		return V ? V->N : 0.0;
	}
	void Spot(const std::string& Slot, double& Along, double& Up) const
	{
		if (Has(Slot))
		{
			const std::string Style = Part(Slot).Str("style");
			for (const RsStyleSpot& S : RsStyleSpots)
				if (Slot == S.Slot && Style == S.Style) { Along = S.Along; Up = S.Up; return; }
		}
		for (const RsSpot& S : RsSpots)
			if (Slot == S.Slot) { Along = S.Along; Up = S.Up; return; }
		Along = 0.5;
		Up = 0.5;
	}
};

// PartsDB.tyre_fit: 0 if the tyre suits the wheel's rim, 1 too wide, -1 too narrow, 2 made for
// another rim diameter.
int RsTyreFit(const Json& T, const Json& W)
{
	if (T.Size() == 0 || W.Size() == 0) return 0;
	int Lo = 0, Hi = 0;
	const Json* R = W.Get("tyres");
	if (R && R->A.size() == 2)
	{
		Lo = (int)R->A[0].N;
		Hi = (int)R->A[1].N;
	}
	else
	{
		const double Rim = W.Num("rim_width", 3.5);
		Lo = (int)std::lround(Rim * 24.0);
		Hi = (int)std::lround(Rim * 62.0);
	}
	const double Mm = T.Num("width", 120.0);
	if (Mm > Hi) return 1;
	if (Mm < Lo) return -1;
	const Json* Rims = T.Get("rims");
	if (Rims && !Rims->A.empty())
	{
		const double Dia = W.Num("diameter", 17.0);
		bool Any = false;
		for (const Json& D : Rims->A) Any = Any || std::fabs(D.N - Dia) < 1e-5;
		if (!Any) return 2;
	}
	return 0;
}

double RsWidthGrip(double Width, bool Tarmac)
{
	const double Mm = Width * 1000.0;
	if (Tarmac) return RsClamp(1.0 + 0.14 * (Mm - 160.0) / 160.0, 0.9, 1.06);
	return RsClamp(1.0 - 0.1 * (Mm - 120.0) / 120.0, 0.9, 1.06);
}

// TyreModel.params_of with STYLE_DEFAULTS, and the grip table.
// tyre_params: a tyre at a pressure Dev off its best squirms soft (slips further before it peaks,
// builds grip over a longer roll) and is twitchier hard.
TorqueSim::TyreParams RsTyre(const Json& T, double Tarmac, double Dirt, double Grass, double Dev)
{
	const std::string Style = T.Str("style", "road");
	std::string Construction = "radial";
	double Profile = 1.0, Stiffness = 1.0, Slide = 0.8;
	if (Style == "slick") { Profile = 0.9; Stiffness = 1.15; Slide = 0.72; }
	else if (Style == "offroad") { Construction = "bias"; Profile = 1.05; Stiffness = 0.8; Slide = 0.86; }
	const std::string C = T.Str("construction", Construction);
	if (C == "radial" || C == "bias") Construction = C;
	TorqueSim::TyreParams P = TorqueSim::TyreModel::Make(Style, Construction, T.Num("profile", Profile), T.Num("stiffness", Stiffness),
		T.Num("slide", Slide), T.Num("width", 140.0), Tarmac, Dirt, Grass);
	const double Firm = RsClamp(1.0 + 0.35 * Dev, 0.75, 1.2);
	P.KappaPeak /= Firm;
	P.AlphaPeak /= Firm;
	P.Relax /= Firm;
	return P;
}

// _side_brake, new fluid: the torque a full squeeze asks for (N m).
double RsSideBrake(const RsBuild& B, const std::string& Side)
{
	const Json& Disc = B.Part(Side + "_disc");
	const Json& Caliper = B.Part(Side + "_caliper");
	const Json& Pads = B.Part(Side + "_pads");
	const Json& Master = B.Part(Side + "_master");
	double H = 0.0;
	for (const char* S : {"disc", "caliper", "pads", "master"})
		H += B.Health(Side + "_" + S) * (std::string(S) == "pads" ? 0.4 : 0.2);
	// Full and fresh fluid (dot4: brake 1).
	const Json* Fluid = B.Lay.Part("dot4");
	const double FluidMult = Fluid ? Fluid->Num("brake", 1.0) : 1.0;
	double Drum = 1.0;
	if (Disc.Str("style") == "drum")
	{
		const std::string Shoes = Disc.Str("shoes", "sls");
		Drum = Shoes == "tls" ? 0.85 : (Shoes == "4ls" ? 0.95 : 0.7);
	}
	return 1600.0 * Disc.Num("diameter") / 300.0 * Caliper.Num("force") * Pads.Num("mu") / 0.42 * Master.Num("ratio") * RsLerp(0.5, 1.0, H) *
		FluidMult * Drum;
}

} // namespace

double PartHealth(double Wear, double Rust, double Dirt)
{
	return RsClamp(1.0 - 0.6 * Wear - 0.35 * Rust - 0.12 * Dirt, 0.15, 1.0);
}

PartsMap RideParts(const BikeLayout& Lay, const PartsMap& Parts)
{
	PartsMap Out;
	const Json* Default = Lay.Tables().Get("default_build");
	for (const std::string& Slot : Lay.Slots())
	{
		const auto It = Parts.find(Slot);
		std::string Id = It == Parts.end() ? std::string() : It->second;
		if (Id == "none" && Lay.MayBeEmpty(Slot))
		{
			Out[Slot] = Id;
			continue;
		}
		if (!Lay.Part(Id) || Lay.KindOfPart(Id) != Lay.KindOfSlot(Slot)) Id = Default ? Default->Str(Slot, "none") : std::string("none");
		Out[Slot] = Id;
	}
	return Out;
}

double BeadGrip(double Bead)
{
	return RsLerp(0.92, 1.0, RsClamp(Bead / BeadOk, 0.0, 1.0));
}

bool RideSpec(const BikeLayout& Lay, const RideSpecInput& In, TorqueSim::BikeSpec& Out, std::vector<std::string>* Notes)
{
	if (!Lay.Loaded()) return false;
	RsBuild B{Lay, RideParts(Lay, In.Parts), In.Health, 2, {}};
	// (the aids' box: the dash's own electronics where the catalogue has no ECUs)
	const bool DashAids = !Lay.Catalogue() || Lay.Catalogue()->Ids("ecu").empty();
	const BikeTune Tn(Lay, B.Parts, In.Tune, B.Health(DashAids ? "dashboard" : "ecu"));
	for (const std::string& Slot : In.Lightened)
	{
		const auto P = B.Parts.find(Slot);
		if (P != B.Parts.end() && B.Has(Slot)) B.Cut[Slot] = Tn.LightenCut(P->second);
	}
	const Json& Frame = B.Part("frame");
	const Json& Fork = B.Part("fork");
	const Json& Shock = B.Part("shock");
	const Json& Block = B.Part("engine");
	const Json& Saddle = B.Part("saddle");
	const Json& Bars = B.Part("handlebars");
	const Json& Tank = B.Part("tank");
	if (Frame.Size() == 0 || Fork.Size() == 0 || Block.Size() == 0) return false;
	B.Cylinders = (int)Block.Num("cylinders", 2.0);
	TorqueSim::BikeSpec& S = Out;

	// Mass: the frame's fixed figure, the fluids and every part.
	const double MainCapacity = Tank.Num("capacity", 15.0);
	const double AuxCapacity = B.Has("aux_tank") ? B.Part("aux_tank").Num("capacity", 0.0) : 0.0;
	// (PartsDB.fluid_capacity: an engine holds 1 litre and a litre more for every 400 cc, to 0.1 l.)
	const double OilCapacity = std::floor((1.0 + Block.Num("displacement", 700.0) / 400.0) / 0.1 + 0.5) * 0.1;
	const double Fuel = In.FuelLitres < 0.0 ? MainCapacity + AuxCapacity : RsClamp(In.FuelLitres, 0.0, MainCapacity + AuxCapacity);
	const double AuxLitres = std::min(std::max(0.0, Fuel - MainCapacity), AuxCapacity);   // (the main tank is kept topped up from it)
	const double Oil = In.OilLitres < 0.0 ? OilCapacity : RsClamp(In.OilLitres, 0.0, OilCapacity);
	double BikeMass = Frame.Num("fixed_mass", RsFixedMass) + Fuel * RsFuelDensity + Oil * RsOilDensity;
	for (const auto& [Slot, Id] : B.Parts)
		if (Id != "none") BikeMass += B.Mass(Slot);
	S.BikeMass = BikeMass;
	S.RiderMass = RsRiderMass;
	S.TotalMass = BikeMass + RsRiderMass;

	// The frame's geometry.
	S.Wheelbase = Frame.Num("wheelbase", 1.4);
	S.ComHeight = Frame.Num("com_height", 0.62);
	S.RakeDeg = Frame.Num("rake_deg", 25.0);
	S.SeatHeight = Frame.Num("seat_height", 0.85) + Saddle.Num("height", 0.0);
	double CdA = Frame.Num("cda", 0.55) + Bars.Num("cda", 0.0) + B.Part("panniers").Num("cda", 0.0) + B.Part("top_box").Num("cda", 0.0) +
		B.Part("tank_bag").Num("cda", 0.0);
	CdA += B.Part("fairing").Num("cda", 0.0) * RsLerp(0.7, 1.0, B.Health("fairing"));
	CdA += B.Part("mirrors").Num("cda", 0.0);
	S.CdA = CdA;
	S.BarWidthMm = Bars.Num("width", 720.0);
	S.SteerRate = 4.0 * S.BarWidthMm / 720.0 * RsClamp(1.0 - 0.3 * (B.Part("front_tyre").Num("width", 120.0) - 160.0) / 160.0, 0.85, 1.12);

	// Suspension.
	S.FrontTravel = Fork.Num("travel", 0.13);
	S.FrontSpring = Fork.Num("spring", 30000.0) * RsLerp(0.85, 1.0, B.Health("fork"));
	S.FrontBump = Fork.Num("bump", 2100.0) * RsLerp(0.45, 1.0, B.Health("fork"));
	S.FrontRebound = Fork.Num("rebound", 3100.0) * RsLerp(0.45, 1.0, B.Health("fork"));
	S.RearTravel = Shock.Num("travel", 0.11);
	S.RearSpring = Shock.Num("spring", 36000.0) * RsLerp(0.85, 1.0, B.Health("shock"));
	S.RearBump = Shock.Num("bump", 2300.0) * RsLerp(0.45, 1.0, B.Health("shock"));
	S.RearRebound = Shock.Num("rebound", 3500.0) * RsLerp(0.45, 1.0, B.Health("shock"));
	// The clickers: preload pre-squashes the spring (less sag, the same rate), bump and rebound the damping.
	S.FrontPreload = std::max(Fork.Num("preload", RsPreloadStock) + RsPreloadClick * Tn.Value("f_preload"), 0.0);
	S.FrontBump *= 1.0 + 0.45 * Tn.Value("f_bump");
	S.FrontRebound *= 1.0 + 0.45 * Tn.Value("f_rebound");
	S.RearPreload = std::max(Shock.Num("preload", RsPreloadStock) + RsPreloadClick * Tn.Value("r_preload"), 0.0);
	S.RearBump *= 1.0 + 0.45 * Tn.Value("r_bump");
	S.RearRebound *= 1.0 + 0.45 * Tn.Value("r_rebound");
	const std::string ForkStyle = Fork.Str("style", "conventional");
	S.FrontProgression = Fork.Num("progression", RsTable(RsForkStyles, RsForkProgression, ForkStyle, 0.2));
	S.RearProgression = Shock.Num("progression", RsShockProgression(Shock.Str("type")));
	S.FrontKnee = Fork.Num("knee", Fork.Num("adjust", 0.0) >= 4.0 ? 0.6 : 1.0);
	S.RearKnee = Shock.Num("knee", Shock.Num("adjust", 0.0) >= 4.0 ? 0.6 : 1.0);
	const double PathRake = RsDeg(S.RakeDeg - RsTable(RsForkStyles, RsForkTilt, ForkStyle, 0.0));
	S.ForkPath = V3(0.0, std::cos(PathRake), std::sin(PathRake));
	S.ForkLink = Fork.Num("link", RsTable(RsForkStyles, RsForkLinkTable, ForkStyle, 0.0));
	S.LeanClearance = RsDeg(Frame.Num("clearance", RsTable(RsLowStyles, RsLowClearance, Frame.Str("style"), 52.0)));
	{
		// flick_of: a proper sports, race or naked bike all of its style's, mostly one a share.
		double Coherence = 0.0;
		const std::string Style = Lay.DetectStyle(B.Parts, &Coherence);
		const double F = RsTable(RsFlickStyles, RsFlickTable, Style, 0.0);
		S.Flick = F > 0.0 ? 1.0 + (F - 1.0) * RsClamp(Coherence / RsProperStyle, 0.0, 1.0) : 1.0;
	}
	{
		const Json* Geo = Frame.Get("geo");
		const double PivotY = Geo ? Geo->Num("pivot_y", 0.16) : 0.16;
		const double Arm = Geo ? Geo->Num("arm", 0.6) : 0.6;
		S.SwingPivot = V3(0.0, PivotY, S.Wheelbase * 0.5 - Arm);
		S.SwingLength = (V3(0.0, 0.0, S.Wheelbase * 0.5) - S.SwingPivot).Length();
	}

	// Wheels and tyres (_compute_wheels), at the pressures they're set to.
	const Json& Tf = B.Part("front_tyre");
	const Json& Tr = B.Part("rear_tyre");
	const Json& Wf = B.Part("front_wheel");
	const Json& Wr = B.Part("rear_wheel");
	const double FrontWidth = Tf.Num("width", 120.0) / 1000.0, RearWidth = Tr.Num("width", 160.0) / 1000.0;
	S.FrontRadius = Wf.Num("diameter", 17.0) * 0.0127 + FrontWidth * Tf.Num("aspect", 0.7);
	S.RearRadius = Wr.Num("diameter", 17.0) * 0.0127 + RearWidth * Tr.Num("aspect", 0.6);
	{
		const double ScaleF = RsLerp(0.7, 1.0, B.Health("front_tyre")) * RsLerp(0.92, 1.0, B.Health("front_wheel"));
		const double ScaleR = RsLerp(0.7, 1.0, B.Health("rear_tyre")) * RsLerp(0.92, 1.0, B.Health("rear_wheel"));
		// A tyre on a rim it doesn't suit (from before the tyre changer checked) is pinched or ballooned.
		const int FitF = RsTyreFit(Tf, Wf), FitR = RsTyreFit(Tr, Wr);
		if (Notes)
			for (const auto& [Fit, End] : {std::pair<int, const char*>{FitF, "front"}, std::pair<int, const char*>{FitR, "rear"}})
				if (Fit != 0) Notes->push_back(std::string("The ") + End + " tyre doesn't suit its rim. Change it on the tyre changer.");
		// A bead that wasn't seated properly on the tyre changer lets the tyre squirm on the rim.
		if (Notes)
			for (const auto& [Bead, End] : {std::pair<double, const char*>{In.BeadFront, "front"}, std::pair<double, const char*>{In.BeadRear, "rear"}})
				if (Bead < BeadOk)
					Notes->push_back(std::string("The ") + End + " tyre's bead isn't seated right: it grips less. Refit it on the tyre changer.");
		// Off a tyre's best pressure it grips less on tarmac; a little soft finds more grip in the dirt.
		const double DevF = Tn.PressureOffset(true), DevR = Tn.PressureOffset(false);
		// [TFT] The pressures as set (bar, cold) and how far off their best, for the dash's tyre page.
		S.TyreBar[0] = Tn.Value("psi_f");
		S.TyreBar[1] = Tn.Value("psi_r");
		S.TyreOff[0] = DevF;
		S.TyreOff[1] = DevR;
		const double BeadF = BeadGrip(In.BeadFront), BeadR = BeadGrip(In.BeadRear);
		auto GripOf = [&](const Json& Ty, double Width, double Scale, int Fit, const char* Surf, double Dev, double BeadK) {
			const bool Road = std::string(Surf) == "tarmac";
			return B.Grip(Ty, Surf) * Scale * RsWidthGrip(Width, Road) * (Fit != 0 ? 0.9 : 1.0) * BikeTune::PressureGrip(Dev, Surf) * BeadK;
		};
		S.TyreFront = RsTyre(Tf, GripOf(Tf, FrontWidth, ScaleF, FitF, "tarmac", DevF, BeadF), GripOf(Tf, FrontWidth, ScaleF, FitF, "dirt", DevF, BeadF),
			GripOf(Tf, FrontWidth, ScaleF, FitF, "grass", DevF, BeadF), DevF);
		S.TyreRear = RsTyre(Tr, GripOf(Tr, RearWidth, ScaleR, FitR, "tarmac", DevR, BeadR), GripOf(Tr, RearWidth, ScaleR, FitR, "dirt", DevR, BeadR),
			GripOf(Tr, RearWidth, ScaleR, FitR, "grass", DevR, BeadR), DevR);
		S.Rolling = RsRolling * (RsClamp(1.0 - 0.9 * DevF, 0.6, 2.5) + RsClamp(1.0 - 0.9 * DevR, 0.6, 2.5)) * 0.5;
		if (Notes && std::min(DevF, DevR) < -0.3) Notes->push_back("The tyres are soft: they'll squirm, drag and wear out fast.");
	}
	const double MassF = B.Mass("front_wheel") + Tf.Num("mass", 0.0);
	const double MassR = B.Mass("rear_wheel") + Tr.Num("mass", 0.0);
	S.FrontInertia = 0.8 * MassF * S.FrontRadius * S.FrontRadius;
	S.RearInertia = 0.8 * MassR * S.RearRadius * S.RearRadius + 0.15;   // plus the spinning drivetrain
	S.ForkOffset = Fork.Num("offset", RsTable(RsForkStyles, RsForkOffset, Fork.Str("style"), 0.04));
	{
		const double Rake = RsDeg(S.RakeDeg);
		S.Trail = RsClamp((S.FrontRadius * std::sin(Rake) - S.ForkOffset) / std::cos(Rake), -0.05, 0.4);
	}
	if (Notes && S.Trail < RsShortTrail)
		Notes->push_back("The trail is very short (" + std::to_string((int)std::lround(S.Trail * 1000.0)) +
			" mm): the steering will be nervous and may shake its head.");
	S.SteerMass = MassF + B.Mass("front_disc") + B.Mass("front_caliper");
	S.SteerInertia = 0.5 * S.FrontInertia + (MassF + B.Mass("front_disc")) * S.ForkOffset * S.ForkOffset + 0.012 * Fork.Num("mass", 9.0);
	{
		const std::pair<const char*, double> Out2[] = {{"handlebars", 0.3}, {"dashboard", 0.22}, {"headlight", 0.26}, {"fog_lights", 0.2},
			{"front_mudguard", 0.2}, {"front_caliper", 0.16}, {"front_master", 0.3}, {"mirrors", 0.4}};
		for (const auto& [Slot, R] : Out2)
			if (B.Has(Slot)) S.SteerInertia += B.Mass(Slot) * R * R;
	}
	{
		const Json& Damper = B.Part("steering_damper");
		const double Dh = B.Health("steering_damper");
		// Its clicker firms it up or softens it.
		const double Click = 1.0 + 0.6 * Tn.Value("damper");
		S.DamperDamping = Damper.Num("damping", 0.0) * RsLerp(0.4, 1.0, Dh) * Click;
		S.DamperFriction = Damper.Num("friction", 0.0) * RsLerp(0.5, 1.0, Dh) * Click;
	}

	// Brakes.
	S.FrontBrakeTorque = RsSideBrake(B, "front");
	S.RearBrakeTorque = RsSideBrake(B, "rear") * 0.7;

	// Rider aids from the ECU, at the levels the dyno desk set (aid_strength: off, or the ECU's own
	// firmness scaled by the level; ABS just on or off). No ride modes yet: the desk's levels (Custom).
	{
		S.AntiWheelie = Tn.AidStrength("anti_wheelie");
		S.LeanTraction = Tn.AidStrength("lean_traction");
		S.Stability = Tn.AidStrength("stability");
		S.AbsFront = S.AbsRear = Tn.AidStrength("abs") > 0.0;
		S.TractionStrength = Tn.AidStrength("traction");
		S.TractionControl = S.TractionStrength > 0.0;
		// [ride modes] What the dash lets the rider set from the saddle (Sim/RideModes.h): Touring is
		// these, the desk's levels.
		TorqueSim::BikeSpec::AidCaps& C = S.Aids;
		C.Modes = Tn.HasModes();
		C.Abs = Tn.HasAid("abs");
		C.Traction = Tn.HasAid("traction");
		C.Lean = Tn.HasAid("lean_traction");
		C.Wheelie = Tn.HasAid("anti_wheelie");
		C.Stability = Tn.HasAid("stability");
		C.Cruise = Tn.DashAids() && Tn.DashHas("cruise");
		C.Firm = Tn.EcuAid();
		C.Desk[0] = Tn.AidLevel("abs");
		C.Desk[1] = Tn.AidLevel("traction");
		C.Desk[2] = Tn.AidLevel("lean_traction");
		C.Desk[3] = Tn.AidLevel("anti_wheelie");
		C.Desk[4] = Tn.AidLevel("stability");
		const std::string Style = Lay.StyleFor(B.Parts);
		C.Enduro = Style == "adventure" || Style == "scrambler" || Style == "mx" || Style == "supermoto";
		C.Race = Style == "sport" || Style == "race" || Style == "naked" || Style == "supermoto";
		C.SemiActive = B.Part("fork").Flag("electronic") && B.Part("shock").Flag("electronic");
	}

	// Gearbox, sprockets and chain.
	{
		const Json& Gearbox = B.Part("gearbox");
		const Json& Chain = B.Part("chain");
		S.Primary = Gearbox.Num("primary", 2.9);
		S.GearRatios.clear();
		if (const Json* R = Gearbox.Get("ratios"))
			for (const Json& X : R->A) S.GearRatios.push_back(X.N);
		if (S.GearRatios.empty()) S.GearRatios = {2.6, 1.95, 1.6, 1.36, 1.19, 1.06};
		S.GearRatios = BikeTune::Spread(S.GearRatios, Tn.Value("spread"));
		const double Front = Tn.Value("sprocket_f"), Rear = Tn.Value("sprocket_r");
		S.FinalDrive = Rear / Front;
		S.ShiftTime = Gearbox.Num("shift_time", 0.12);
		double Eff = RsDrivetrain * RsLerp(0.8, 1.0, B.Health("gearbox")) * RsLerp(0.88, 1.0, B.Health("final_drive"));
		// _compute_chain, freshly lubed: a slack or tight chain soaks up more.
		const double Slack = RsClamp(In.ChainSlack, 5.0, 90.0);
		double Loss = Chain.Num("friction", 0.025);
		if (Slack > 40.0)
		{
			Loss += (Slack - 40.0) * 0.0012;
			if (Notes) Notes->push_back(Slack < 65.0 ? "The chain is slapping about: tighten it." : "The chain is so slack it could jump off! Tighten it.");
		}
		else if (Slack < 20.0)
		{
			Loss += (20.0 - Slack) * 0.002;
			if (Notes) Notes->push_back("The chain is too tight and will wear the sprockets. Slacken it off.");
		}
		Eff *= (1.0 - Loss) * RsLerp(0.9, 1.0, B.Health("chain"));
		S.DrivetrainEfficiency = Eff;
		S.SprocketRFront = RsChainPitch / (2.0 * std::sin(RsPi / std::max(Front, 8.0)));
		S.SprocketRRear = RsChainPitch / (2.0 * std::sin(RsPi / std::max(Rear, 8.0)));
		S.Countershaft = V3(0.0, 0.03, -0.075);
	}

	// Where the parts sit (_compute_mass_spread): against the stock part in each slot, at its spot.
	{
		V3 ComOffset;
		V3 InertiaAdd;
		const V3 Base(0.0, S.ComHeight, 0.06);
		std::vector<std::pair<double, V3>> Extra;
		const Json* Default = Lay.Tables().Get("default_build");
		for (const std::string& Slot : Lay.Slots())
		{
			const Json* Stock = Default ? Lay.Part(Default->Str(Slot, "none")) : nullptr;
			const double M0 = Stock ? Stock->Num("mass", Stock->Num("mass_per_cyl", 0.0) * 2.0) : 0.0;
			const double Dm = (B.Has(Slot) ? B.Mass(Slot) : 0.0) - M0;
			if (std::fabs(Dm) < 0.01) continue;
			double Along = 0.5, Up = 0.5;
			B.Spot(Slot, Along, Up);
			Extra.push_back({Dm, V3(0.0, Up, (Along - 0.5) * S.Wheelbase)});
		}
		{
			double Along = 0.32, Up = 0.86;   // PART_SPOT tank
			Extra.push_back({(Fuel - AuxLitres - 15.0) * RsFuelDensity, V3(0.0, Up, (Along - 0.5) * S.Wheelbase)});
			if (AuxLitres > 0.0)
			{
				B.Spot("aux_tank", Along, Up);
				Extra.push_back({AuxLitres * RsFuelDensity, V3(0.0, Up, (Along - 0.5) * S.Wheelbase)});
			}
		}
		for (const auto& [Dm, At] : Extra) ComOffset += (At - Base) * (Dm / std::max(S.TotalMass, 1.0));
		ComOffset = V3(0.0, RsClamp(ComOffset.y, -0.1, 0.1), RsClamp(ComOffset.z, -0.12, 0.12));
		const V3 Com = Base + ComOffset;
		for (const auto& [Dm, At] : Extra)
		{
			const V3 D = At - Com;
			InertiaAdd += V3(D.y * D.y + D.z * D.z, D.z * D.z, D.y * D.y) * Dm;
		}
		S.ComHeight += ComOffset.y;
		S.ComOffset = ComOffset;
		// Bike.setup: rough rider-plus-bike inertia, plus the parts' spread (never under 60% of it).
		const double M = S.TotalMass, Wb = S.Wheelbase;
		const V3 Rough(M * Wb * Wb * 0.16, M * Wb * Wb * 0.13, M * 0.11);
		const V3 Sum = Rough + InertiaAdd;
		S.Inertia = V3(std::max(Sum.x, Rough.x * 0.6), std::max(Sum.y, Rough.y * 0.6), std::max(Sum.z, Rough.z * 0.6));
	}

	// What its heat works from (BikeHeat.setup): the tyres' windows and sizes, the discs and pads,
	// the engine and its cooling.
	{
		TorqueSim::BikeHeatParts& H = S.Heat;
		const char* Sides[2] = {"front", "rear"};
		for (int I = 0; I < 2; ++I)
		{
			const Json& T = I == 0 ? Tf : Tr;
			const Json& Rim = I == 0 ? Wf : Wr;
			TorqueSim::BikeHeatParts::TyrePart& Tp = H.Tyre[I];
			// window_of: by compound; an off-road knobbly's soft compound is made for the cold mud.
			std::string Compound = T.Str("compound", "Medium");
			for (char& C : Compound) C = (char)std::tolower((unsigned char)C);
			double Lo = 15.0, Hi = 90.0, Cold = 0.9;
			if (Compound == "super soft") { Lo = 70.0; Hi = 110.0; Cold = 0.55; }
			else if (Compound == "soft") { Lo = 35.0; Hi = 95.0; Cold = 0.8; }
			else if (Compound == "dual") { Lo = 10.0; Hi = 95.0; Cold = 0.92; }
			else if (Compound == "hard") { Lo = 10.0; Hi = 85.0; Cold = 0.9; }
			if (T.Str("style") == "offroad" && Compound == "soft") { Lo = 15.0; Hi = 85.0; Cold = 0.9; }
			if (const Json* W = T.Get("temp_window"))
				if (W->A.size() >= 2) { Lo = W->A[0].N; Hi = W->A[1].N; }
			Tp.Lo = Lo;
			Tp.Hi = Hi;
			Tp.ColdGrip = T.Num("cold_grip", Cold);
			Tp.Mass = std::max(T.Num("mass", 4.5), 1.0);
			const double R = I == 0 ? S.FrontRadius : S.RearRadius;
			const double RimR = Rim.Num("diameter", 17.0) * 0.0127;
			const double W = I == 0 ? FrontWidth : RearWidth;
			Tp.TreadArea = 2.0 * RsPi * R * W * 1.2;
			Tp.SideArea = 2.0 * RsPi * std::max(R * R - RimR * RimR, 0.005);
			// The disc (or drum) and pads.
			const Json& Disc = B.Part(std::string(Sides[I]) + "_disc");
			const std::string DiscStyle = Disc.Str("style");
			const double D = Disc.Num("diameter", 280.0) / 1000.0;
			TorqueSim::BikeHeatParts::BrakePart& Bp = H.Brake[I];
			Bp.DiscC = (Disc.Num("mass", 1.0) + 0.5) * 480.0;
			Bp.DiscArea = DiscStyle == "drum" ? RsPi * D * 0.06 + RsPi * (D * 0.5) * (D * 0.5)
				: 2.0 * RsPi * std::max((D * 0.5) * (D * 0.5) - (D * 0.5 - 0.04) * (D * 0.5 - 0.04), 0.01);
			Bp.DiscCool = DiscStyle == "drum" ? 0.5 : (DiscStyle == "drilled" || DiscStyle == "wave" || DiscStyle == "floating") ? 1.25 : 1.0;
			std::string Pad = B.Part(std::string(Sides[I]) + "_pads").Str("type", "semi-metallic");
			for (char& C : Pad) C = (char)std::tolower((unsigned char)C);
			Bp.PadFrom = 80.0;
			Bp.PadCold = 0.95;
			if (Pad == "organic") { Bp.PadFrom = 40.0; Bp.PadCold = 1.0; }
			else if (Pad == "sintered") { Bp.PadCold = 0.94; }
			else if (Pad == "ceramic") { Bp.PadFrom = 120.0; Bp.PadCold = 0.9; }
			else if (Pad == "carbon-ceramic") { Bp.PadFrom = 250.0; Bp.PadCold = 0.65; }
		}
		const Json& Cooling = B.Part("cooling");
		std::string Kind = Cooling.Str("type", "Air");
		for (char& C : Kind) C = (char)std::tolower((unsigned char)C);
		H.Liquid = Kind.find("liquid") != std::string::npos;
		H.OilFollow = H.Liquid ? 1.0 : Kind.find("oil") != std::string::npos ? 0.6 : 0.75;
		H.Displacement = std::max(Block.Num("displacement", 700.0), 30.0);
		H.IdleRpm = std::max(Block.Num("idle_rpm", 950.0), 500.0);
		// engine_heat over the litres: the head's heat and how hard the block breathes.
		H.Head = RsClamp(B.Part("head").Num("heat", 1.0) * Block.Num("breathing", 1.0), 0.5, 2.0);
		H.CoolingCapacity = std::max(Cooling.Num("capacity", 1.0) * RsLerp(0.6, 1.0, B.Health("cooling")), 0.2);
	}
	return true;
}
} // namespace TorqueFit
