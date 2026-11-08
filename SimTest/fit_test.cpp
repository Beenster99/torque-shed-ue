// FitTest: assembles the Speed Twin from its sockets (Data/Bike/SpeedTwin/fitment.json), checks it
// against the riding physics' datums, runs the clearance checks (full lock, full compression, both),
// tries every alternative part, and exercises geometry mode. Plain C++, no Unreal.
//
//     cmake -S SimTest -B build/simtest && cmake --build build/simtest && build/simtest/fit_test [--pairs]
//
// --pairs also fits every two alternatives from different slots together (a rack with each seat,
// spotlights with each set of engine bars...) and fails if the two clash; it takes a minute or so.
//
// It fails (exit 1) when the data won't load, the stock bike doesn't assemble where it was
// modelled, the physics datums the model is drawn from disagree, a part that should fit doesn't
// (or one that shouldn't does), or geometry mode doesn't do what it says. Clashes are reported;
// the stock bike's are listed so the model can be fixed, a swapped part's are what the shed shows.
#include "BikeSpec.h"
#include "Fitment.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace TorqueFit;

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
	// Run from the repo, build/simtest or SimTest: find Data/.
	for (const char* R : {".", "..", "../..", "../../.."})
	{
		std::ifstream F(std::string(R) + "/Data/Fitment/interfaces.json");
		if (F) return R;
	}
	return ".";
}

void PrintClashes(const std::vector<Clash>& List)
{
	// One line per pair: its worst case, and how many cases clash.
	std::vector<std::string> Order;
	std::map<std::string, Clash> Worst;
	std::map<std::string, int> Cases;
	for (const Clash& C : List)
	{
		const std::string Key = C.A + " x " + C.B;
		if (!Worst.count(Key)) Order.push_back(Key);
		if (!Worst.count(Key) || C.DepthMm > Worst[Key].DepthMm) Worst[Key] = C;
		++Cases[Key];
	}
	for (const std::string& K : Order)
		std::printf("    clash: %-34s %5.1f mm at %-22s (%d of the cases)\n", K.c_str(), Worst[K].DepthMm, Worst[K].Case.c_str(), Cases[K]);
	if (Order.empty()) std::printf("    no clashes\n");
}

PhysicsDatums FromSim(const TorqueSim::BikeSpec& S)
{
	// The sim's frame: +X right, +Y up, +Z back; the model's: x forward, y up, z right.
	PhysicsDatums P;
	P.Wheelbase = S.Wheelbase;
	P.RakeDeg = S.RakeDeg;
	P.ForkOffset = S.ForkOffset;
	P.FrontRadius = S.FrontRadius;
	P.RearRadius = S.RearRadius;
	P.FrontTravel = S.FrontTravel;
	P.RearTravel = S.RearTravel;
	P.SwingPivot = V3(-S.SwingPivot.z, S.SwingPivot.y, S.SwingPivot.x);
	return P;
}

// A loose part's placement, for the log.
void PrintLoose(const Assembly& A, const std::string& Id)
{
	const Placed* P = A.Find(Id);
	if (!P) return;
	const V3& C = P->World.T;
	std::printf("    %s on %s%s%s at (%.3f, %.3f, %.3f), up (%.2f, %.2f, %.2f)\n", Id.c_str(), P->U.Mnt.ParentUnit.c_str(),
		P->U.Mnt.ParentSocket.empty() ? "" : ".", P->U.Mnt.ParentSocket.c_str(), C.x, C.y, C.z, P->World.R.Y.x, P->World.R.Y.y,
		P->World.R.Y.z);
}

// The rest of the catalogue: every part placed (an authored part, a placeholder look, a tune part or
// a loose part) or refused with a reason; and loose parts fitted with no authored mount.
void CheckCatalogue(BikeFit& Fit, const PartsDb& Db, const std::string& R, const Assembly& StockA)
{
	std::printf("\nThe catalogue on the %s (Data/Fitment/catalogue.json)\n", Fit.BikeName().c_str());
	std::string Text, Error;
	Expect(ReadFile(R + "/Data/Fitment/catalogue.json", Text) && Fit.AddCatalogue(Db, Text, Error), "catalogue.json loads " + Error);
	std::map<std::string, int> ByState;
	std::map<std::string, std::string> Refusals;   // kind -> reason (one each)
	for (const CatalogueEntry& E : Fit.Catalogue())
	{
		++ByState[E.State];
		if (E.State == "refused" && !Refusals.count(E.Kind)) Refusals[E.Kind] = E.Reason;
	}
	std::printf("  %zu parts:", Fit.Catalogue().size());
	for (const auto& KV : ByState) std::printf(" %s %d,", KV.first.c_str(), KV.second);
	std::printf("\n");
	for (const auto& KV : Refusals) std::printf("    refused %s: %s\n", KV.first.c_str(), KV.second.c_str());
	Expect(Fit.Catalogue().size() == Db.Count(), "every catalogue part has an answer");
	bool AllReasoned = true;
	for (const CatalogueEntry& E : Fit.Catalogue())
		if ((E.State == "refused" || E.State == "loose" || E.State == "placeholder") && E.Reason.empty()) AllReasoned = false;
	Expect(AllReasoned, "every refusal, placeholder and loose part says why or what");
	int Fitted = 0, Failed = 0;
	Build Stock;
	for (const CatalogueEntry& E : Fit.Catalogue())
	{
		if (E.State != "placeholder" && E.State != "tune") continue;
		std::string Why;
		Build B;
		B.Fitted[E.Slot] = E.Id;
		Assembly Ab;
		if (Fit.CanFit(Stock, E.Slot, E.Id, Why) && Fit.Assemble(B, Ab, Error)) ++Fitted;
		else { ++Failed; std::printf("    %s/%s: %s%s\n", E.Kind.c_str(), E.Id.c_str(), Why.c_str(), Error.c_str()); }
	}
	Expect(Failed == 0, "every placeholder and tune part fits and assembles (" + std::to_string(Fitted) + ")");
	const CatalogueEntry* Drag = Fit.FindCatalogue("handlebars", "drag_bar");
	Expect(Drag && Drag->State == "placeholder" && Drag->Slot == "handlebars", "handlebars/drag_bar fits, as a placeholder (" +
		(Drag ? Drag->Reason : std::string()) + ")");
	const CatalogueEntry* Engine = Fit.FindCatalogue("engine", "triple_1100");
	Expect(Engine && Engine->State == "refused" && !Engine->Reason.empty(), "an engine swap is refused, and says why");
	const CatalogueEntry* Horn = Fit.FindCatalogue("horn", "twin_tone");
	Expect(Horn && Horn->State == "tune" && Fit.FindOption("horn", "twin_tone") != nullptr, "a horn is a tune part, in its own slot");
	const CatalogueEntry* Box = Fit.FindCatalogue("top_box", "top_box_30");
	Expect(Box && Box->State == "placeholder" && Box->Slot == "rack", "a top box goes on the rack (" + (Box ? Box->Reason : std::string()) + ")");

	std::printf("\nLoose parts: no authored mount, snapped by their box to the nearest interface they can use\n");
	auto FitLoose = [&](const Build& B, const std::string& Kind, const std::string& Id, const std::string& Parent, Assembly& Out) {
		Build Bl = B;
		Bl.Loose.push_back({Kind, Id, V3(), false});
		const bool Ok = Fit.Assemble(Bl, Out, Error);
		for (const std::string& N : Out.Notes) std::printf("    note: %s\n", N.c_str());
		const std::string UnitId = "loose_" + Kind + "_" + Id;
		const Placed* P = Out.Find(UnitId);
		PrintLoose(Out, UnitId);
		bool Clean = true;
		for (const Clash& C : Fit.Clearance(Out, &StockA))
			if (C.A == UnitId || C.B == UnitId)
			{
				Clean = false;
				std::printf("    clash: %s x %s %.1f mm at %s\n", C.A.c_str(), C.B.c_str(), C.DepthMm, C.Case.c_str());
			}
		Expect(Ok && P && P->U.Mnt.ParentUnit == Parent && Clean, Kind + "/" + Id + " snaps onto the " + Parent + ", clear at rest, full lock and full bump");
		return P;
	};
	Assembly Al;
	int Snapped = 0;
	std::string Missed;
	for (const CatalogueEntry& E : Fit.Catalogue())
	{
		if (E.State != "loose") continue;
		Build Bl;
		Bl.Loose.push_back({E.Kind, E.Id, V3(), false});
		Assembly Ax;
		if (Fit.Assemble(Bl, Ax, Error) && Ax.Find("loose_" + E.Kind + "_" + E.Id)) ++Snapped;
		else Missed += " " + E.Kind + "/" + E.Id + (Ax.Notes.empty() ? std::string() : " (" + Ax.Notes.back() + ")");
	}
	Expect(Missed.empty(), "every loose part finds a place on the stock bike (" + std::to_string(Snapped) + ")" + Missed);
	const LooseSpec* Nav = Fit.FindLoose("satnav", "phone_nav_mount");
	const Placed* P = FitLoose(Stock, "satnav", "phone_nav_mount", "bars", Al);
	if (P && Nav) Expect((P->World.T - Nav->Near).Length() < 0.08, "the sat nav lands near where it was dropped");
	Build Clubman;
	Clubman.Fitted["handlebars"] = "clubman_bar";
	FitLoose(Clubman, "satnav", "phone_nav_mount", "bars", Al);
	P = FitLoose(Stock, "tank_bag", "mag_tank_bag_8", "tank", Al);
	if (P) Expect(P->World.R.Y.y > 0.8, "the tank bag sits on top of the tank");
	Build Teardrop;
	Teardrop.Fitted["tank"] = "teardrop_14";
	P = FitLoose(Teardrop, "tank_bag", "mag_tank_bag_8", "tank", Al);
	if (P) Expect(P->World.R.Y.y > 0.8, "and on the teardrop tank when that's fitted instead");
	Build Rack;
	Rack.Fitted["rack"] = "wooden_crate";
	FitLoose(Rack, "aux_tank", "rack_can_5", "rack", Al);
}

// Every two alternatives from different slots, fitted together: they mustn't clash with each other
// (each is already checked against the stock bike on its own).
void CheckPairs(const BikeFit& Fit)
{
	std::printf("\nEvery pair of alternatives from different slots, fitted together\n");
	struct Opt { std::string Slot; const Option* O; std::set<std::string> Units; };
	std::vector<Opt> All;
	for (const std::string& S : Fit.Slots())
		for (const Option& O : *Fit.OptionsFor(S))
		{
			if (O.Stock || O.Catalogue) continue;   // placeholders look like an authored part: nothing new to clash
			Opt E{S, &O, {}};
			for (const Unit& U : O.Units) E.Units.insert(U.Id);
			for (const auto& M : O.Moves) E.Units.insert(M.first);
			All.push_back(E);
		}
	std::string Error;
	Build Stock;
	Assembly Base;
	Fit.Assemble(Stock, Base, Error);
	int Pairs = 0, Bad = 0;
	for (size_t I = 0; I < All.size(); ++I)
		for (size_t J = I + 1; J < All.size(); ++J)
		{
			if (All[I].Slot == All[J].Slot) continue;
			Build B;
			B.Fitted[All[I].Slot] = All[I].O->Id;
			B.Fitted[All[J].Slot] = All[J].O->Id;
			Assembly A;
			if (!Fit.Assemble(B, A, Error)) continue;
			++Pairs;
			for (const Clash& C : Fit.Clearance(A, &Base))
				if (C.DepthMm > 1.0 && ((All[I].Units.count(C.A) && All[J].Units.count(C.B)) ||
														(All[I].Units.count(C.B) && All[J].Units.count(C.A))))
				{
					std::printf("    clash: %s/%s with %s/%s: %s x %s %.1f mm at %s\n", All[I].Slot.c_str(), All[I].O->Id.c_str(),
						All[J].Slot.c_str(), All[J].O->Id.c_str(), C.A.c_str(), C.B.c_str(), C.DepthMm, C.Case.c_str());
					++Bad;
					break;
				}
		}
	Expect(Bad == 0, std::to_string(Pairs) + " pairs from " + std::to_string(All.size()) + " alternatives, none clashing with each other");
}
}   // namespace

int main(int argc, char** argv)
{
	const std::string R = Root();
	std::string PartsText, FitText, TypesText, Error;
	std::printf("Parts catalogue\n");
	PartsDb Db;
	Expect(ReadFile(R + "/Data/Parts/parts.json", PartsText) && Db.LoadFromText(PartsText, Error), "Data/Parts/parts.json loads " + Error);
	std::printf("  %zu kinds, %zu parts\n", Db.Kinds().size(), Db.Count());
	Expect(Db.Count() > 500, "the whole Godot catalogue is there");
	Expect(Db.Number("engine", "parallel_1200", "displacement") == 1200.0, "engine/parallel_1200 is 1200 cc");
	Expect(Db.Number("handlebars", "clubman_bar", "width") == 650.0, "handlebars/clubman_bar is 650 mm wide");
	Expect(Db.HasStyle("exhaust", "peashooter_twins", "classic"), "exhaust/peashooter_twins suits a classic");

	std::printf("\nSpeed Twin fitment data\n");
	BikeFit Fit;
	const bool Loaded = ReadFile(R + "/Data/Bike/SpeedTwin/fitment.json", FitText) && ReadFile(R + "/Data/Fitment/interfaces.json", TypesText) &&
		Fit.Load(FitText, TypesText, Error);
	Expect(Loaded, "fitment.json and interfaces.json load " + Error);
	if (!Loaded) return 1;
	size_t Proxies = 0;
	for (const Unit& U : Fit.StockUnits()) Proxies += U.Proxies.size();
	std::printf("  %s: %zu fit units, %zu collision proxies, slots:", Fit.BikeName().c_str(), Fit.StockUnits().size(), Proxies);
	for (const std::string& S : Fit.Slots()) std::printf(" %s (%zu)", S.c_str(), Fit.OptionsFor(S)->size());
	std::printf("\n");
	for (const std::string& S : Fit.Slots())
		for (const Option& O : *Fit.OptionsFor(S))
			if (!O.Stock && !O.CatKind.empty()) Expect(Db.Find(O.CatKind, O.CatId) != nullptr, S + "/" + O.Id + " is in the catalogue");   // grips and levers have no entry

	std::printf("\nStock bike, assembled from sockets\n");
	Build Stock;
	Assembly A;
	Expect(Fit.Assemble(Stock, A, Error), "assembles " + Error);
	double Worst = 0.0;
	std::string WorstUnit;
	for (const Placed& P : A.Units)
	{
		const double D = Xf::Drift(P.World, Xf());
		if (D > Worst) { Worst = D; WorstUnit = P.U.Id; }
	}
	Expect(Worst < 1e-4, "every part lands where it was modelled (worst " + WorstUnit + ": " + std::to_string(Worst * 1000.0) + " mm)");
	Expect(A.Notes.empty(), "no conforming part's ends moved");
	for (const char* Side : {"mirror_l", "mirror_r"})
	{
		// The stock bike is beenster's own: its mirrors stay on the bar ends, exactly as modelled.
		const Placed* M = A.Find(Side);
		Expect(M && M->OptionId == "stock" && M->U.Mnt.FromEnd < 0.01 && Xf::Drift(M->World, Xf()) < 1e-9 &&
			M->U.Meshes.size() == 3 && M->U.Meshes[0].rfind("mirrors_", 0) == 0,
			std::string("the stock ") + Side + " is the modelled one, on the bar end, unmoved");
	}

	std::printf("\nDatums: the model against the riding physics (BikeSpec::SpeedTwin1200)\n");
	const PhysicsDatums Phys = FromSim(TorqueSim::BikeSpec::SpeedTwin1200());
	for (const DatumCheck& D : Fit.CheckDatums(A, Phys))
	{
		std::printf("  %-6s %-24s model %8.4f  physics %8.4f\n", D.Ok ? "ok" : "DRIFT", D.Name.c_str(), D.Model, D.Physics);
		// The steering geometry and wheels are what the model is drawn from: they must agree.
		const bool Critical = D.Name.find("swingarm") == std::string::npos;
		if (Critical && !D.Ok) ++Failures;
	}

	std::printf("\nClearance, stock bike: what full lock and full compression add (rest overlaps are as modelled)\n");
	const std::vector<Clash> StockClashes = Fit.Clearance(A);
	PrintClashes(StockClashes);

	std::printf("\nAlternative parts\n");
	for (const std::string& S : Fit.Slots())
	{
		for (const Option& O : *Fit.OptionsFor(S))
		{
			if (O.Stock) continue;
			std::string Why;
			const bool Fits = Fit.CanFit(Stock, S, O.Id, Why);
			std::printf("  %s/%s (%s): %s%s%s\n", S.c_str(), O.Id.c_str(), O.Name.c_str(), Fits ? "fits" : "won't fit: ", Why.c_str(),
				Fits && !Why.empty() ? "" : "");
			const bool ShouldFit = O.Id != "scrambler_moto_chrome";
			Expect(Fits == ShouldFit, ShouldFit ? "it fits" : "it doesn't (28.6 mm bar, 22 mm clamps), and says why");
			if (!Fits) continue;
			Build B;
			B.Fitted[S] = O.Id;
			Assembly Ab;
			Expect(Fit.Assemble(B, Ab, Error), "assembles " + Error);
			for (const std::string& N : Ab.Notes) std::printf("    note: %s\n", N.c_str());
			if (S == "handlebars") Expect(Ab.Notes.empty(), "the option's own cables reach its levers (no re-routing left)");
			std::printf("    clashes beyond the stock part's:\n");
			PrintClashes(Fit.Clearance(Ab, &A));
			if (S == "handlebars")
			{
				// Whatever mirrors are fitted go on these bars' lever clamps (the bar option's "moves").
				const Placed* Mirror = Ab.Find("mirror_l");
				const Placed* Bar = Ab.Find("bars");
				const Socket* LeftHalf = Bar ? Bar->U.FindSocket("bar_l") : nullptr;
				if (Mirror && LeftHalf)
				{
					const V3 Base = Mirror->World.Point(V3(0.4157, 0.6484, -0.385));   // the stock mirror's base
					const Xf At = Bar->World * LeftHalf->FrameAt(LeftHalf->Length() - 0.21);
					Expect(Mirror->OptionId == "stock" && (Base - At.T).Length() < 0.002,
						"the stock mirrors move to these bars' lever clamps (" + std::to_string((Base - At.T).Length() * 1000.0) + " mm)");
				}
				// The grips, levers and mirrors ride the new bar's ends.
				const Placed* Bars = Ab.Find("bars");
				const Placed* Grip = Ab.Find("grip_r");
				const Socket* Half = Bars ? Bars->U.FindSocket("bar_r") : nullptr;
				if (Bars && Grip && Half)
				{
					const V3 End = Bars->World.Point(Half->Path.back());
					const V3 GripEnd = Grip->World.Point(V3(0.4157, 0.6504, 0.375));   // the stock grip's outer end
					std::printf("    right bar end (%.3f, %.3f, %.3f), grip end (%.3f, %.3f, %.3f)\n", End.x, End.y, End.z, GripEnd.x,
						GripEnd.y, GripEnd.z);
					Expect((End - GripEnd).Length() < 0.03, "the right grip sits on the new bar's end");
				}
			}
		}
	}

	std::printf("\nSlot names\n");
	Expect(Fit.SlotFor("bars") == "handlebars" && Fit.SlotFor("Handlebars") == "handlebars", "'bars' and 'Handlebars' mean the handlebars slot");
	Expect(Fit.SlotFor("pipes") == "exhaust" && Fit.SlotFor("silencers") == "exhaust", "'pipes' and 'silencers' mean the exhaust slot");
	{
		Build Alias;
		Alias.Fitted["bars"] = "clubman_bar";
		Assembly Aa;
		std::string Why;
		Expect(Fit.CanFit(Stock, "bars", "clubman_bar", Why) && Fit.Assemble(Alias, Aa, Error), "bars:clubman_bar fits and assembles " + Why + Error);
		Build Bad;
		Bad.Fitted["mudguards"] = "x";
		Expect(!Fit.Assemble(Bad, Aa, Error) && Error.find("slots: handlebars") != std::string::npos, "an unknown slot lists the slots: " + Error);
	}
	std::printf("\nGeometry mode\n");
	{
		// Player fitting: drag the headlight 3 cm straight down; it may only slide down the fork tubes.
		const Placed* H = A.Find("headlight");
		const Adjust Hd = Fit.ConstrainDrag(A, "headlight", Adjust(), V3(0, -0.03, 0), 0.0);
		Build B;
		B.Adjusts["headlight"] = Hd;
		Assembly Ab;
		Fit.Assemble(B, Ab, Error);
		const V3 Moved = Ab.Find("headlight")->World.Point(V3(0.79, 0.49, 0)) - H->World.Point(V3(0.79, 0.49, 0));
		const V3 Fork = H->Socket0.R.X;
		const double Off = (Moved - Fork * Moved.Dot(Fork)).Length();
		std::printf("  headlight dragged 30 mm down: slides %.1f mm along the fork tubes, %.2f mm off them\n", Hd.Slide * 1000.0, Off * 1000.0);
		Expect(std::fabs(Hd.Slide) > 0.02 && Off < 1e-4, "the headlight only moves along the fork tubes");
		// Rearsets snap to the plate's holes.
		const Adjust Rs = Fit.ConstrainDrag(A, "footrests", Adjust(), V3(-0.011, 0.016, 0), 0.0);
		std::printf("  footrests dragged 19 mm up and back: hole %d\n", Rs.Hole);
		Expect(Rs.Hole == 1, "the footrests snap to the next hole (20 mm up and back)");
		// The engine bolts on: no movement.
		const Adjust En = Fit.ConstrainDrag(A, "engine", Adjust(), V3(0.05, 0.05, 0), 10.0);
		Expect(En.Slide == 0.0 && En.TurnDeg == 0.0, "the engine doesn't move (hard points)");
		// The bars roll in their clamp, only about the clamp's axis, and no further than it allows.
		const Adjust Br = Fit.ConstrainDrag(A, "bars", Adjust(), V3(0.1, 0.1, 0.1), 35.0);
		Expect(Br.TurnDeg == 20.0 && Br.Slide == 0.0, "the bars roll, up to 20 degrees");
		// Author fitting: roll the bars 8 degrees, save it as the part's fix, and the stock build gets it.
		Build Rolled;
		Rolled.Adjusts["bars"].TurnDeg = 8.0;
		Assembly Ar;
		Fit.Assemble(Rolled, Ar, Error);
		Json FixEntry;
		Expect(Fit.AuthorFix(Ar, "bars", FixEntry, Error), "author fix made " + Error);
		Json Fixes = Json::MakeObject();
		Fixes.Set("fixes", Json::MakeArray().Push(FixEntry));
		BikeFit Fixed;
		Fixed.Load(FitText, TypesText, Error);
		Expect(Fixed.ApplyFixes(Fixes.Dump(), Error), "fix loads back " + Error);
		Assembly Af;
		Fixed.Assemble(Stock, Af, Error);
		const double D1 = Xf::Drift(Af.Find("bars")->World, Ar.Find("bars")->World);
		const double D2 = Xf::Drift(Af.Find("mirror_l")->World, Ar.Find("mirror_l")->World);
		std::printf("  bars rolled 8 degrees and saved: stock build now matches within %.3f mm (bars), %.3f mm (left mirror)\n", D1 * 1000.0,
			D2 * 1000.0);
		Expect(D1 < 1e-4 && D2 < 1e-4, "an author fix moves the part (and what rides on it) for every build");
	}

	CheckCatalogue(Fit, Db, R, A);
	for (int K = 1; K < argc; ++K)
		if (std::string(argv[K]) == "--pairs") CheckPairs(Fit);

	std::printf("\n%s (%d failure%s)\n", Failures ? "FAILED" : "PASSED", Failures, Failures == 1 ? "" : "s");
	return Failures ? 1 : 0;
}
