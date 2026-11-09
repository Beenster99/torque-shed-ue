// Checks Fit/BikeShop (the New bike configurator's rules) against the original game: every style's
// stock build as the configurator starts it, the parts each configurator slot offers and how each
// goes on (BikeLayout::PartFit), and a fixed walk of part changes through each style with the
// fabrication, brief, model and mudguard group after each, dumped from Godot by
// Tools/make_godot_reference.py --shop (SimTest/data/godot_shop.json). Then the order's text
// round trip and the colours it puts on each slot.
#include "BikeShop.h"
#include "BikeRideSpec.h"
#include "PartsDb.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace TorqueFit;

namespace
{
int Failures = 0;
int Checks = 0;

void Expect(bool Ok, const std::string& What)
{
	++Checks;
	if (!Ok || What.rfind("  ", 0) != 0) std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What.c_str());
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

std::string List(const std::vector<std::string>& L)
{
	std::string Out;
	for (const std::string& S : L) Out += (Out.empty() ? "" : ",") + S;
	return Out;
}

std::vector<std::string> Strings(const Json* J)
{
	std::vector<std::string> Out;
	if (J)
		for (const Json& V : J->A) Out.push_back(V.S);
	return Out;
}

PartsMap PartsOf(const Json* J)
{
	PartsMap Out;
	if (J)
		for (const auto& KV : J->O) Out[KV.first] = KV.second.S;
	return Out;
}

std::string Diff(const PartsMap& Got, const PartsMap& Want)
{
	std::string Out;
	for (const auto& KV : Want)
	{
		const auto It = Got.find(KV.first);
		const std::string G = It == Got.end() ? "(missing)" : It->second;
		if (G != KV.second) Out += " " + KV.first + "=" + G + " (Godot " + KV.second + ")";
	}
	return Out;
}

// Each configurator slot's options on Parts, with how each goes on, against Godot's.
void CheckOptions(const BikeLayout& Lay, const std::string& Style, const PartsMap& Parts, const Json& Want, const std::string& At)
{
	for (const auto& [Slot, Rows] : Want.O)
	{
		const std::vector<std::string> Got = Shop::Options(Lay, Slot, Style, Parts);
		std::vector<std::string> Ids, GotTiers, WantTiers;
		for (const Json& R : Rows.A)
		{
			Ids.push_back(R.A[0].S);
			WantTiers.push_back(R.A[1].S);
		}
		for (const std::string& Id : Got) GotTiers.push_back(Lay.PartFit(Parts, Slot, Id).Tier);
		Expect(Got == Ids, "  " + At + " " + Slot + " offers " + List(Got) + (Got == Ids ? "" : " (Godot " + List(Ids) + ")"));
		if (Got == Ids)
			for (size_t I = 0; I < Ids.size(); ++I)
				Expect(GotTiers[I] == WantTiers[I], "  " + At + " " + Slot + " " + Ids[I] + ": " + GotTiers[I] +
					(GotTiers[I] == WantTiers[I] ? "" : " (Godot " + WantTiers[I] + ")"));
	}
}

void CheckState(const BikeLayout& Lay, const std::string& Style, const PartsMap& Parts, const Json& Want, const std::string& At)
{
	const std::string D = Diff(Parts, PartsOf(Want.Get("parts")));
	Expect(D.empty(), "  " + At + " parts" + D);
	std::vector<std::string> Fab;
	for (const FabJob& J : Shop::FabWork(Lay, Parts, Style)) Fab.push_back(J.What);
	const std::vector<std::string> WantFab = Strings(Want.Get("fab"));
	Expect(Fab == WantFab, "  " + At + " fabrication [" + List(Fab) + "]" + (Fab == WantFab ? "" : " (Godot [" + List(WantFab) + "])"));
	const std::string Brief = Shop::BriefText(Lay, Parts);
	Expect(Brief == Want.Str("brief"), "  " + At + " brief " + Brief + (Brief == Want.Str("brief") ? "" : " (Godot " + Want.Str("brief") + ")"));
	const std::string Model = Shop::ModelText(Lay, Parts);
	Expect(Model == Want.Str("model"), "  " + At + " model " + Model + (Model == Want.Str("model") ? "" : " (Godot " + Want.Str("model") + ")"));
	const std::vector<std::string> Guards = Shop::ColourSlots(Lay, "mudguards", Parts);
	const std::vector<std::string> WantGuards = Strings(Want.Get("mudguards"));
	Expect(Guards == WantGuards, "  " + At + " mudguards [" + List(Guards) + "]" + (Guards == WantGuards ? "" : " (Godot [" + List(WantGuards) + "])"));
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string PartsText, LayoutText, RefText, Error;
	Json Ref;
	const bool Have = ReadFile(R + "/SimTest/data/godot_shop.json", RefText) && Json::Parse(RefText, Ref, &Error);
	Expect(Have, "the Godot configurator dump loads (SimTest/data/godot_shop.json) " + Error);
	Expect(ReadFile(R + "/Data/Parts/parts.json", PartsText) && ReadFile(R + "/Data/Fitment/layout.json", LayoutText), "the parts and layout files read");
	if (!Have) return 1;

	// The same catalogue as Godot's: this repo's parts (with any its overlay took off sale put back,
	// Data/Parts/Redrawn/godot_removed.json, and its own new ones left out), in Godot's order.
	PartsDb GDb;
	BikeLayout GLay;
	{
		Json PartsJson, Removed, Godot = Json::MakeObject();
		std::string RemovedText;
		Json::Parse(PartsText, PartsJson, &Error);
		if (ReadFile(R + "/Data/Parts/Redrawn/godot_removed.json", RemovedText)) Json::Parse(RemovedText, Removed, &Error);
		for (const auto& [Kind, Ids] : Ref.Get("by_kind")->O)
		{
			Json K = Json::MakeObject();
			for (const Json& Id : Ids.A)
			{
				const Json* P = PartsJson.Get(Kind) ? PartsJson.Get(Kind)->Get(Id.S) : nullptr;
				if (!P && Removed.Get(Kind)) P = Removed.Get(Kind)->Get(Id.S);
				if (!P) continue;
				// With the styles Godot gives it (the overlay moved a few to the redrawn tanks).
				Json Part = *P;
				const Json* Suits = Ref.Get("styles_of")->Get(Id.S);
				Part.Set("styles", Suits ? *Suits : Json::MakeArray());
				K.Set(Id.S, Part);
			}
			Godot.Set(Kind, K);
		}
		Expect(GDb.LoadFromText(Godot.Dump(0), Error) && GLay.Load(LayoutText, GDb, Error), "Godot's catalogue loads " + Error);
	}
	// Wheels in Godot's order (PartsDB.wheel_before).
	for (const auto& [Kind, Ids] : Ref.Get("by_kind")->O)
	{
		const std::vector<std::string> Got = Shop::KindIds(GLay, Kind);
		Expect(Got == Strings(&Ids), "  " + Kind + " in Godot's order");
	}
	Expect(Shop::ConfigSlots() == Strings(Ref.Get("slots")), "the configurator's slots: " + List(Shop::ConfigSlots()));

	int Styles = 0, Steps = 0;
	for (const auto& [Style, E] : Ref.Get("styles")->O)
	{
		++Styles;
		const Json& Entry = E;
		const std::string Before = std::to_string(Failures);
		PartsMap Parts = Shop::StockBuild(GLay, Style);
		// This repo's reference bike may name its own redrawn parts (Data/Parts/Redrawn), which
		// Godot's catalogue hasn't got: start from Godot's stock bike then, said so.
		for (const auto& [Slot, Id] : GLay.ReferenceBuild(Style))
			if (Id != "none" && !GLay.Part(Id))
			{
				std::printf("  note %s's reference %s is this repo's own %s: starting from Godot's stock bike\n", Style.c_str(), Slot.c_str(), Id.c_str());
				Parts = PartsOf(Entry.Get("stock")->Get("parts"));
			}
		Expect(GLay.StyleName(Style) == Entry.Str("name"), "  " + Style + " is called " + GLay.StyleName(Style));
		CheckState(GLay, Style, Parts, *Entry.Get("stock"), Style + " stock");
		CheckOptions(GLay, Style, Parts, *Entry.Get("options"), Style + " stock");
		int K = 0;
		for (const Json& W : Entry.Get("walk")->A)
		{
			++Steps;
			const std::string At = Style + " step " + std::to_string(++K) + " (" + W.Str("slot") + " " + W.Str("id") + ")";
			Parts = Shop::SetPart(GLay, Parts, Style, W.Str("slot"), W.Str("id"));
			CheckState(GLay, Style, Parts, W, At);
			CheckOptions(GLay, Style, Parts, *W.Get("options"), At);
			// (Carry on from Godot's build, so one miss doesn't spoil the steps after it.)
			Parts = PartsOf(W.Get("parts"));
		}
		std::printf("  %s %s: stock and %zu steps\n", std::to_string(Failures) == Before ? "ok  " : "FAIL", Style.c_str(), Entry.Get("walk")->A.size());
	}

	// The order: text and back, on the game's own catalogue.
	PartsDb Db;
	BikeLayout Lay;
	Expect(Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error), "the repo's catalogue loads " + Error);
	Shop::Order O;
	O.Style = "cafe";
	O.Parts = Shop::SetPart(Lay, Shop::StockBuild(Lay, "cafe"), "cafe", "front_wheel", Shop::Options(Lay, "front_wheel", "cafe", Shop::StockBuild(Lay, "cafe")).back());
	O.Paint[0] = 0.1; O.Paint[1] = 0.35; O.Paint[2] = 0.8;
	O.Colours["tank"] = {0.1, 0.35, 0.8};
	O.Colours["wheels"] = {0.06, 0.06, 0.06};
	O.Colours["rear_wheel"] = {0.95, 0.85, 0.1};
	O.Finishes["mudguards"] = "chrome";
	O.Finishes["tank"] = "metallic";
	O.TankBadge.Logo = "wings";
	O.TankBadge.Brand = "  Beenster Motorcycles Ltd  ";
	O.TankBadge.Font = "lobster";
	O.TankBadge.BrandSize = 9.0;
	Shop::Order Back;
	const std::string Text = Shop::OrderText(O);
	Expect(Shop::ParseOrder(Lay, Text, Back), "an order reads back (" + std::to_string(Text.size()) + " bytes)");
	Expect(Diff(Back.Parts, O.Parts).empty(), "  its parts" + Diff(Back.Parts, O.Parts));
	Expect(Back.TankBadge.Brand == "Beenster Motorcy" && Back.TankBadge.Font == "lobster" && Back.TankBadge.BrandSize == Shop::BadgeSizeMax,
		"its badge cleaned: \"" + Back.TankBadge.Brand + "\" in " + Back.TankBadge.Font);
	const std::map<std::string, Shop::SlotLook> Looks = Shop::OrderLooks(Lay, Back);
	auto Is = [&Looks](const std::string& S, double Rr, double G, double B, const std::string& F) {
		const auto It = Looks.find(S);
		return It != Looks.end() && std::fabs(It->second.Colour[0] - Rr) < 1e-3 && std::fabs(It->second.Colour[1] - G) < 1e-3 &&
			std::fabs(It->second.Colour[2] - B) < 1e-3 && It->second.Finish == F;
	};
	Expect(Is("tank", 0.1, 0.35, 0.8, "metallic"), "  the tank in the paint, metallic");
	Expect(Is("front_wheel", 0.06, 0.06, 0.06, ""), "  the front wheel in the wheels' black");
	Expect(Is("rear_wheel", 0.95, 0.85, 0.1, ""), "  the rear wheel in its own yellow over the group");
	Expect(Looks.count("front_mudguard") && Looks.at("front_mudguard").Finish == "chrome", "  the front mudguard chromed in its factory colour");
	Expect(!Shop::ParseOrder(Lay, "{\"style\":\"hovercraft\"}", Back) && !Shop::ParseOrder(Lay, "not json", Back), "nonsense isn't an order");

	std::printf("%d styles, %d steps, %d checks: %s\n", Styles, Steps, Checks, Failures ? "FAILED" : "all passed");
	return Failures ? 1 : 0;
}
