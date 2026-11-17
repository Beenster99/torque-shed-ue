// [bikes v2] Every Bike Models V2 bike in the catalogue (Data/BikesV2/parts_v2.json: a frame part with
// "v2") laid out, fitted and given its riding spec as the game does it: its style's stock bike with the
// V2 frame in it. The layout must be the drawn bike's (axles level and a wheelbase apart, the head over
// the front wheel, the hips over the bike), the fitted pieces must come from the V2 files (one for each
// slot and moving group, standing on the ground, each wheel round its axle) and the spec must load and
// agree with the layout. Plain C++, no engine.
#include "BikeLayout.h"
#include "BikeParts.h"
#include "BikeRideSpec.h"
#include "BikeShop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

using namespace TorqueFit;

namespace
{
int Failures = 0;

void Expect(bool Ok, const std::string& What)
{
	if (!Ok)
	{
		std::printf("  FAIL %s\n", What.c_str());
		++Failures;
	}
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
		std::ifstream F(std::string(R) + "/Data/BikesV2/parts_v2.json");
		if (F) return R;
	}
	return ".";
}

std::string Num2(double V)
{
	char B[32];
	std::snprintf(B, sizeof(B), "%.3f", V);
	return B;
}
} // namespace

int main()
{
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
	BikeParts::SetV2Source([&R](const std::string& Model, PartMesh& Mesh, std::string& Info) {
		std::string Bytes, Err;
		return ReadFile(R + "/Data/BikesV2/" + Model + ".glb", Bytes) && Mesh.LoadGlb(Bytes, Err) && ReadFile(R + "/Data/BikesV2/" + Model + ".json", Info);
	});
	// The game's own part glbs (the tyres' tread sections, which a V2 bike's tyres are bent from).
	Json Index = Json::MakeObject();
	{
		std::string IndexText, IndexError;
		Json Whole;
		if (ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Json::Parse(IndexText, Whole, &IndexError) && Whole.Get("parts")) Index = *Whole.Get("parts");
		else std::printf("FAIL the parts index didn't load (%s)\n", IndexError.c_str());
	}
	// The six bike types with no V2 bikes and the parts made only for them are out of the game.
	Expect(!Db.Find("frame", "tourer_heavy_spar") && !Db.Find("frame", "race_carbon_white"), "the retired types' frames are gone");
	Expect(Db.Find("frame", "steel_backbone") && Db.Find("saddle", "roader_sport_seat"), "parts the kept types use stay");
	int Bikes = 0;
	// The old bikes are retired: the shop sells the eight styles that have V2 bikes, each starting as
	// its lead V2 bike, and only V2 frames are offered.
	Expect(Lay.HasV2() && Lay.ShopStyles().size() == 8, "the shop sells eight bike types, got " + std::to_string(Lay.ShopStyles().size()));
	for (const std::string& Style : Lay.ShopStyles())
	{
		const PartsMap Stock = Shop::StockBuild(Lay, Style);
		const PartsMap::const_iterator Fr = Stock.find("frame");
		const Json* Frame = Fr != Stock.end() ? Db.Find("frame", Fr->second) : nullptr;
		const Json* V2 = Frame ? Frame->Get("v2") : nullptr;
		Expect(V2 && V2->Flag("lead"), "a new " + Style + " starts as its lead V2 bike, got " + (Fr != Stock.end() ? Fr->second : std::string("none")));
		for (const std::string& Id : Shop::Options(Lay, "frame", Style, Stock))
			Expect(Id.rfind("v2_", 0) == 0, "only V2 frames are offered on a new " + Style + ", got " + Id);
	}
	for (const char* Old : {"scrambler", "roader", "tourer", "bobber", "race", "supermoto"})
		Expect(std::find(Lay.ShopStyles().begin(), Lay.ShopStyles().end(), Old) == Lay.ShopStyles().end(), std::string(Old) + " is no longer sold");
	for (const std::string& Id : Db.Ids("frame"))
	{
		const Json* Frame = Db.Find("frame", Id);
		const Json* V2 = Frame ? Frame->Get("v2") : nullptr;
		if (!V2) continue;
		++Bikes;
		const Json* Styles = Frame->Get("styles");
		const std::string Style = Styles && !Styles->A.empty() ? Styles->A[0].S : std::string("naked");
		PartsMap Parts = Shop::SetPart(Lay, Shop::StockBuild(Lay, Style), Style, "frame", Id);
		Expect(Parts["frame"] == Id, Id + " goes in its style's stock bike");
		Expect(Lay.V2Of(Parts) != nullptr, Id + " is a V2 bike");
		const std::vector<std::string> Offered = Shop::Options(Lay, "frame", Style, Shop::StockBuild(Lay, Style));
		Expect(std::find(Offered.begin(), Offered.end(), Id) != Offered.end(), Id + " is offered as a frame on a new " + Style);
		const Layout L = Lay.Solve(Parts);
		const double Wb = L.FrontAxle.x - L.RearAxle.x;
		Expect(std::fabs(L.FrontAxle.y) < 1e-6 && std::fabs(L.RearAxle.y) < 1e-6, Id + " axles level");
		Expect(std::fabs(L.FrontAxle.x + L.RearAxle.x) < 1e-6, Id + " origin midway between the axles");
		Expect(Wb > 1.2 && Wb < 2.2 && std::fabs(Wb - L.Stats.Wheelbase) < 1e-6, Id + " wheelbase " + Num2(Wb));
		Expect(L.Head.y > 0.45 && L.Head.x < L.FrontAxle.x && L.Head.x > 0.0, Id + " head over and behind the front axle");
		Expect(L.SteerAxis.y > 0.6 && L.SteerAxis.x < 0.0, Id + " steering axis leans back");
		Expect(L.Stats.RakeDeg > 15.0 && L.Stats.RakeDeg < 50.0, Id + " rake " + Num2(L.Stats.RakeDeg));
		Expect(L.Hip.x > L.RearAxle.x && L.Hip.x < 0.25 && L.Hip.y > 0.2, Id + " hips over the bike");
		Expect(L.Peg.z > 0.1 && L.GripR.z > 0.15 && L.GripR.x > L.Hip.x, Id + " right peg and grip");
		Expect(L.Pivot.x > L.RearAxle.x && L.Pivot.x < 0.3, Id + " swingarm pivot ahead of the rear axle");
		Expect(L.Fit.Tier == "bolts on", Id + " needs no fabrication");

		BikeParts Placer(Lay, Index);
		const std::vector<FittedPart> Fitted = Placer.Fit(Parts, L);
		size_t Tris = 0, Surfaces = 0;
		int AmberLeft = 0, AmberRight = 0, AmberBoth = 0;
		int HeadLens = 0, RearLens = 0;
		std::set<std::string> Nodes, Slots;
		double Low = 1e9, FrontLow = 1e9, RearLow = 1e9;
		V3 FrontMin(1e9, 1e9, 1e9), FrontMax(-1e9, -1e9, -1e9);
		bool AllMade = !Fitted.empty(), Named = true;
		int TyresMade = 0;
		for (const FittedPart& Each : Fitted)
		{
			FittedPart P = Each;
			if (!P.Made && (P.Slot == "front_tyre" || P.Slot == "rear_tyre") && P.Glb.rfind("tyre/", 0) == 0)
			{
				// The game's tread section bent round the drawn wheel (BikeParts::V2Tyres), drawn as the game draws it.
				std::string Bytes, GlbError;
				PartMesh Section;
				if (ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) && Section.LoadGlb(Bytes, GlbError))
				{
					P.Made = std::make_shared<PartMesh>(BikeParts::Draw(Section, P));
					P.Code = true;
					++TyresMade;
				}
			}
			if (!P.Made) { AllMade = false; continue; }
			Nodes.insert(P.Node);
			Slots.insert(P.Slot);
			const auto In = Parts.find(P.Slot);
			Expect(In != Parts.end() && !In->second.empty() && In->second != "none", Id + " piece in a slot the bike has a part in: " + P.Slot);
			Tris += P.Made->Triangles();
			for (const PartSurface& S : P.Made->Surfaces)
			{
				++Surfaces;
				// [lights] Each amber lens is all on one side of the bike, so the game can tell left from right.
				if (S.Material.size() > 10 && S.Material.rfind("/lens_amber") == S.Material.size() - 11 && !S.Pos.empty())
				{
					double LoZ = 1e9, HiZ = -1e9;
					for (const V3& V : S.Pos) { LoZ = std::min(LoZ, V.z); HiZ = std::max(HiZ, V.z); }
					if (HiZ < 0.0) ++AmberLeft; else if (LoZ > 0.0) ++AmberRight; else ++AmberBoth;
				}
				{
					// [lights] A headlamp lens where the lights look for one, and a red rear lens.
					auto Has = [&S](const char* T) { const std::string K = std::string("/") + T; return S.Material == T || S.Material.find(K + "/") != std::string::npos ||
						(S.Material.size() > K.size() && S.Material.compare(S.Material.size() - K.size(), K.size(), K) == 0); };
					if ((Has("lens_white") || Has("lens_drl")) && (P.Slot == "headlight" || P.Slot == "fairing" || P.Slot == "number_boards")) ++HeadLens;
					if (Has("lens_red") && P.Gauge.empty() && P.Slot != "dashboard") ++RearLens;
				}
				if (S.Material.rfind("v2/", 0) != 0 && !P.Code) Named = false;   // (a TFT dash's unit is made in code)
				for (const V3& V : S.Pos)
				{
					Low = std::min(Low, V.y);
					if (P.Slot == "front_tyre")
					{
						FrontLow = std::min(FrontLow, V.y);
						FrontMin = V3(std::min(FrontMin.x, V.x), std::min(FrontMin.y, V.y), std::min(FrontMin.z, V.z));
						FrontMax = V3(std::max(FrontMax.x, V.x), std::max(FrontMax.y, V.y), std::max(FrontMax.z, V.z));
					}
					if (P.Slot == "rear_tyre") RearLow = std::min(RearLow, V.y);
				}
			}
		}
		Expect(AllMade, Id + " every fitted piece is a V2 one");
		Expect(TyresMade == 2, Id + " both tyres are the game's own tread sections");
		Expect(HeadLens >= 1, Id + " has a headlamp lens the game can light");
		Expect(RearLens >= 1, Id + " has a rear lamp lens the game can light");
		Expect(AmberLeft >= 1 && AmberRight == AmberLeft && AmberBoth == 0, Id + " has indicator lenses the game can flash a side at a time");
		Expect(Named, Id + " every surface carries its look");
		{
			// [TFT] A TFT dash shows as itself with a screen for the game to light, in place of the
			// model's drawn clocks; any other dash leaves the model's clocks.
			const Json* Dash = Db.Find("dashboard", Parts["dashboard"]);
			const bool Tft = Dash && Dash->Str("style") == "tft";
			// [bikes v2] A model drawn with a display keeps it, lit; one drawn with dials gets a working needle
			// on each (and a TFT fitted to it stands in the dials' place).
			bool DrawsScreen = false, DrawsDials = false;
			if (const Json* Pm = V2->Get("points_mm"))
				if (const Json* Gs = Pm->Get("gauges"))
					for (const Json& G : Gs->A)
					{
						DrawsScreen = DrawsScreen || G.Str("what") == "screen";
						DrawsDials = DrawsDials || G.Str("what") == "speed";
					}
			Expect(DrawsScreen || DrawsDials, Id + " has clocks the game can work");
			int Screens = 0, DrawnShown = 0, Needles = 0;
			for (const FittedPart& P : Fitted)
			{
				if (P.Gauge.rfind("screen_", 0) == 0) ++Screens;
				if (P.Gauge == "speed" || P.Gauge == "rpm") ++Needles;
				if (P.Slot == "dashboard" && !P.Code && !P.Hidden) ++DrawnShown;
			}
			Expect(Screens == (Tft || DrawsScreen ? 1 : 0), Id + " one screen face for a TFT dash or a drawn display (" + Parts["dashboard"] + ")");
			Expect(!(Tft && !DrawsScreen) || DrawnShown == 0, Id + " a TFT dash fitted to a bike drawn with dials hides them");
			Expect(Needles == (DrawsDials && !Tft ? 2 : 0), Id + " a needle on each drawn dial (" + std::to_string(Needles) + ")");
		}
		Expect(Nodes.count("steer") && Nodes.count("front_wheel") && Nodes.count("rear_wheel") && Nodes.count("front_hub") && Nodes.count(""), Id + " pieces on the steering, the wheels and the bike");
		Expect(Slots.count("tank") && Slots.count("engine") && Slots.count("frame") && Slots.count("saddle") && Slots.count("exhaust"), Id + " tank, engine, frame, saddle and exhaust pieces");
		Expect(std::fabs(FrontLow + L.Stats.FrontRadius) < 0.006, Id + " front tyre reaches the ground (" + Num2(FrontLow) + " for radius " + Num2(L.Stats.FrontRadius) + ")");
		Expect(std::fabs(RearLow + L.Stats.RearRadius) < 0.006, Id + " rear tyre reaches the ground (" + Num2(RearLow) + " for radius " + Num2(L.Stats.RearRadius) + ")");
		Expect(std::fabs((FrontMin.x + FrontMax.x) * 0.5 - L.FrontAxle.x) < 0.004 && std::fabs((FrontMin.y + FrontMax.y) * 0.5) < 0.004 &&
			std::fabs((FrontMin.z + FrontMax.z) * 0.5) < 0.004, Id + " front tyre round its axle");
		Expect(Low > -std::max(L.Stats.FrontRadius, L.Stats.RearRadius) - 0.02, Id + " nothing below the ground (" + Num2(Low) + ")");
		Expect(Tris > 20000 && Tris < 800000, Id + " triangles " + std::to_string(Tris));   // (with the tyres at the catalogue's full tread detail, as every catalogue bike has them near the camera)

		RideSpecInput In;
		In.Parts = Parts;
		TorqueSim::BikeSpec Spec;
		const bool Rides = RideSpec(Lay, In, Spec);
		Expect(Rides, Id + " has a riding spec");
		if (Rides)
		{
			Expect(std::fabs(Spec.Wheelbase - L.Stats.Wheelbase) < 1e-6 && std::fabs(Spec.RakeDeg - L.Stats.RakeDeg) < 1e-6 && std::fabs(Frame->Num("wheelbase") - Wb) < 1e-3 && std::fabs(Frame->Num("rake_deg") - L.Stats.RakeDeg) < 1e-2, Id + " spec and layout agree on wheelbase and rake");
			Expect(std::fabs(Spec.FrontRadius - L.Stats.FrontRadius) < 0.02 && std::fabs(Spec.RearRadius - L.Stats.RearRadius) < 0.02, Id + " spec wheels the drawn size");
		}
		std::printf("%s %s: wheelbase %.3f, rake %.1f, seat %.2f, %zu pieces, %zu surfaces, %zu triangles\n", Failures ? "    " : "ok  ", Id.c_str(), Wb,
			L.Stats.RakeDeg, L.Stats.SeatHeight, Fitted.size(), Surfaces, Tris);
	}
	Expect(Bikes > 0, "there are V2 bikes in the catalogue");
	std::printf("%d V2 bikes, %d failures\n", Bikes, Failures);
	return Failures ? 1 : 0;
}
