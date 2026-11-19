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
#include "ShockMove.h"

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
	int Sprung = 0;
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
	Json PartsJson, LayoutJson;
	Json::Parse(PartsText, PartsJson);
	Json::Parse(LayoutText, LayoutJson);
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
	Expect(Db.Find("frame", "steel_backbone") && Db.Find("disc", "floating_310"), "parts the kept types use stay");
	int Bikes = 0;
	int MixChecks = 0;
	int KitChecks = 0;
	double WorstKit = 0.0;
	double WorstMix = 0.0;
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
		// It rides as its family's stock bike did before the bare-frame parts (the physics preset and the top
		// speeds are tuned on those): every part the reference build has, the V2 build has one in the same
		// slot, with the same numbers (the engine's torque, revs and size; the head's, exhaust's... multipliers).
		{
			const Json* RefBuilds = LayoutJson.Get("reference_builds");
			const Json* Ref = RefBuilds ? RefBuilds->Get(Style) : nullptr;
			int Off = 0;
			std::string First;
			for (const auto& KV : Ref ? Ref->O : std::vector<std::pair<std::string, Json>>())
			{
				if (!KV.second.IsString() || KV.second.S.empty() || KV.second.S == "none" || KV.first == "frame") continue;
				const Json* Old = nullptr;
				for (const auto& Kind : PartsJson.O)
					if (const Json* P = Kind.second.Get(KV.second.S)) { Old = P; break; }
				if (!Old) continue;   // (gone from the catalogue altogether: the cruiser's saddlebags)
				const std::string Now = Parts[KV.first];
				if (Now.empty() || Now == "none")
				{
					if (!Lay.MayBeEmpty(KV.first)) { ++Off; if (First.empty()) First = KV.first + " empty"; }
					continue;
				}
				const Json* New = Lay.Part(Now);
				if (!New || Now == KV.second.S) continue;
				// [sizes] Only the family's lead keeps the reference engine: another brand's block is its own size
				// (BikesV2/data/engine_sizes.json), its torque scaled with the capacity (engine parts pass stage 5).
				const Json* HostV2 = Lay.V2Of(Parts);
				if (KV.first == "engine" && !(HostV2 && HostV2->Flag("lead"))) continue;
				for (const auto& F : Old->O)
					if (F.second.IsNumber() && F.first != "price" && std::fabs(New->Num(F.first, -1e9) - F.second.N) > 1e-6 * std::max(1.0, std::fabs(F.second.N)))
					{
						++Off;
						if (First.empty()) First = KV.first + " " + F.first + " " + Num2(New->Num(F.first, -1e9)) + " (was " + Num2(F.second.N) + ")";
					}
			}
			Expect(Ref != nullptr && Off == 0, Id + " rides as the stock " + Style + " (" + std::to_string(Off) + " off: " + First + ")");
		}
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
			// [shocks] A sprung bike's shock works in the game: its body end, its rod end and its spring are pieces
			// of their own, and on a bump the shock shortens, squeezing the spring, its bottom eye staying on the arm.
			const Json* Pm = V2->Get("points_mm");
			const Json* Top = Pm ? Pm->Get("shock_top") : nullptr;
			const Json* Bottom = Pm ? Pm->Get("shock_bottom") : nullptr;
			const Json* Pivot = Pm ? Pm->Get("swing_pivot") : nullptr;
			const Json* Axle = Pm ? Pm->Get("rear_axle") : nullptr;
			if (Top && Bottom && Pivot && Axle && Top->A.size() >= 3 && Bottom->A.size() >= 3 && Pivot->A.size() >= 3 && Axle->A.size() >= 3)
			{
				++Sprung;
				Expect(Nodes.count("shock_top") && Nodes.count("shock_bottom") && Nodes.count("shock_spring"), Id + " shock has a body end, a rod end and a spring the game can work");
				const double Px = Pivot->A[0].N, Pz = Pivot->A[2].N, Arm = std::hypot(Axle->A[0].N - Px, Axle->A[2].N - Pz);
				const double RestAngle = std::atan2(Axle->A[2].N - Pz, Axle->A[0].N - Px);
				const double BumpAngle = TorqueSim::Pi - std::asin(std::clamp((Axle->A[2].N + 60.0 - Pz) / Arm, -1.0, 1.0));   // (the axle is behind the pivot)
				const ShockPose Still = ShockAt(Px, Pz, Top->A[0].N, Top->A[2].N, Bottom->A[0].N, Bottom->A[2].N, 0.0);
				const ShockPose Bump = ShockAt(Px, Pz, Top->A[0].N, Top->A[2].N, Bottom->A[0].N, Bottom->A[2].N, BumpAngle - RestAngle);
				Expect(std::abs(Still.Length - Still.Rest) < 1e-6 && std::abs(Still.Turn) < 1e-9, Id + " shock at rest is as drawn");
				Expect(Bump.Length < Bump.Rest - 2.0 && Bump.Length > Bump.Rest * 0.75, Id + " shock shortens on a 60 mm bump (" + std::to_string(Bump.Rest - Bump.Length) + " mm)");
				Expect(std::abs(std::hypot(Bump.BottomX - Px, Bump.BottomZ - Pz) - std::hypot(Bottom->A[0].N - Px, Bottom->A[2].N - Pz)) < 1e-6, Id + " shock's bottom eye stays on the swingarm");
				const double Squeeze = SpringScale(60.0, Bump.Rest - 60.0, Bump);
				Expect(Squeeze < 1.0 && Squeeze > 0.45, Id + " spring squeezed, not coil-bound, on a 60 mm bump");
			}
		}
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
		Expect(Tris > 20000 && Tris < 800000, Id + " triangles " + std::to_string(Tris));

		// [bare frame] A frame on its own is only the frame (and its swingarm and hangers): beenster after 155,
		// "it built the bike (except for tyres)". The bike's own parts are shop parts, the engine built up from its
		// block, head and gearbox.
		{
			PartsMap Bare;
			for (const auto& KV : Parts) Bare[KV.first] = "none";
			Bare["frame"] = Id;
			int Pieces = 0;
			for (const FittedPart& P : Placer.Fit(Bare, Lay.Solve(Bare)))
			{
				++Pieces;
				Expect(P.Slot == "frame", Id + " bare frame draws nothing in " + P.Slot);
			}
			Expect(Pieces > 0, Id + " bare frame is drawn");
			const PartsMap Own = Lay.V2Build(Id);
			Expect(Own.size() >= 18, Id + " has its own shop parts (" + std::to_string(Own.size()) + ")");
			for (const auto& KV : Own)
			{
				Expect(Parts[KV.first] == KV.second, Id + " comes with its own " + KV.first);
				Expect(Lay.V2FitReason(Parts, KV.first, KV.second).empty(), Id + " its own " + KV.first + " fits it: " + Lay.V2FitReason(Parts, KV.first, KV.second));
			}
			Expect(Slots.count("head") == 1, Id + " cylinder head pieces cut from the engine");
			Expect(!Own.count("gearbox") || Slots.count("gearbox") == 1, Id + " gearbox pieces");
			// One at a time, each own part drawn on the bare frame is only that part.
			for (const char* One : {"engine", "head", "tank", "front_wheel", "fork"})
			{
				if (!Own.count(One)) continue;
				PartsMap Just = Bare;
				Just[One] = Own.at(One);
				int Got = 0;
				for (const FittedPart& P : Placer.Fit(Just, Lay.Solve(Just)))
					if (P.Slot != "frame") { ++Got; Expect(P.Slot == One, Id + " only " + std::string(One) + " drawn, not " + P.Slot); }
				Expect(Got > 0, Id + " " + One + " on its own is drawn");
			}
		}
		// [bare frame] Mixing brands in a family: every other model's parts that fit go on this frame where its own did.
		{
			auto Centre = [](const std::vector<FittedPart>& Ps, const std::string& Slot, V3& C) {
				V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
				for (const FittedPart& P : Ps)
					if (P.Slot == Slot && P.Made && !P.Hidden)
						for (const PartSurface& S : P.Made->Surfaces)
							for (const V3& V : S.Pos) { Lo = V3(std::min(Lo.x, V.x), std::min(Lo.y, V.y), std::min(Lo.z, V.z)); Hi = V3(std::max(Hi.x, V.x), std::max(Hi.y, V.y), std::max(Hi.z, V.z)); }
				C = (Lo + Hi) * 0.5;
				return Lo.x <= Hi.x;
			};
			const std::string Family = V2->Str("family");
			for (const std::string& Other : Db.Ids("frame"))
			{
				const Json* OF = Db.Find("frame", Other);
				const Json* OV = OF ? OF->Get("v2") : nullptr;
				if (Other == Id || !OV || OV->Str("family") != Family) continue;
				// [engine parts] Another brand's engine parts go on this engine as its own version of them (beenster in 157).
				for (const auto& KV : Lay.V2Build(Other))
				{
					const Json* OP = Lay.Part(KV.second);
					const Json* OVp = OP ? OP->Get("v2part") : nullptr;
					if (!OVp || OVp->Str("fits") != "engine" || KV.first == "engine" || !Parts.count("engine")) continue;
					Expect(Lay.V2FitReason(Parts, KV.first, KV.second).empty(), Id + " takes " + Other + "'s " + KV.first + ": " + Lay.V2FitReason(Parts, KV.first, KV.second));
					Expect(Lay.V2EngineTwin(Parts, KV.first, KV.second) == Parts.at(KV.first), Id + " draws " + Other + "'s " + KV.first + " as its own");
				}
				PartsMap Mixed = Parts;
				std::vector<std::string> Took;
				for (const auto& KV : Lay.V2Build(Other))
					if (Lay.V2FitReason(Parts, KV.first, KV.second).empty() && Lay.V2Build(Id).count(KV.first))
					{
						Mixed[KV.first] = KV.second;
						Took.push_back(KV.first);
					}
				const std::vector<FittedPart> Got = Placer.Fit(Mixed, Lay.Solve(Mixed));
				for (const std::string& Slot : Took)
				{
					V3 Mine, Theirs;
					const bool bMine = Centre(Fitted, Slot, Mine), bTheirs = Centre(Got, Slot, Theirs);
					Expect(bTheirs || !bMine, Id + " takes " + Other + "'s " + Slot);
					if (!bMine || !bTheirs) continue;
					const double Off = std::hypot(Mine.x - Theirs.x, Mine.y - Theirs.y);
					++MixChecks;
					WorstMix = std::max(WorstMix, Off);
					// (Another brand's part is its own shape: how far its middle is from this bike's own is only shown.
					// It must be on the bike: over the ground, between the wheels' ends.)
					Expect(Theirs.y > -L.Stats.RearRadius && Theirs.x > L.RearAxle.x - 0.6 && Theirs.x < L.FrontAxle.x + 0.6,
						Id + " with " + Other + "'s " + Slot + " on the bike");
				}
			}
		}   // (with the tyres at the catalogue's full tread detail, as every catalogue bike has them near the camera)

		// [kits] Every kit piece sold for this bike goes on it, in its slot, where the bike's own was.
		{
			auto Box = [](const std::vector<FittedPart>& Ps, const std::string& Slot, V3& Lo, V3& Hi) {
				Lo = V3(1e9, 1e9, 1e9);
				Hi = V3(-1e9, -1e9, -1e9);
				for (const FittedPart& P : Ps)
					if (P.Slot == Slot && P.Made && !P.Hidden)
						for (const PartSurface& S : P.Made->Surfaces)
							for (const V3& V : S.Pos) { Lo = V3(std::min(Lo.x, V.x), std::min(Lo.y, V.y), std::min(Lo.z, V.z)); Hi = V3(std::max(Hi.x, V.x), std::max(Hi.y, V.y), std::max(Hi.z, V.z)); }
				return Lo.x <= Hi.x;
			};
			for (const std::string& Kind : Db.Kinds())
				for (const std::string& Kid : Db.Ids(Kind))
				{
					const Json* Part = Db.Find(Kind, Kid);
					const Json* Vp = Part ? Part->Get("v2part") : nullptr;
					if (!Vp || Vp->Str("kit").empty() || Vp->Str("frame") != Id) continue;
					const std::string Slot = Vp->Str("slot");
					++KitChecks;
					Expect(Lay.V2FitReason(Parts, Slot, Kid).empty(), Kid + " fits its own bike: " + Lay.V2FitReason(Parts, Slot, Kid));
					// [engine parts] An open intake goes on only with its own fuel system; a carb only with the airbox off.
					if (!Vp->Str("with_fuel").empty())
					{
						PartsMap Need = Parts;
						Need["fuel"] = Vp->Str("with_fuel");
						Expect(Lay.Part(Vp->Str("with_fuel")) != nullptr, Kid + " has its fuel system in the shop (" + Vp->Str("with_fuel") + ")");
						Expect(Lay.V2NeedsReason(Need, Slot, Kid).empty(), Kid + " goes on with its fuel system: " + Lay.V2NeedsReason(Need, Slot, Kid));
						if (Parts.count("fuel") && Parts.at("fuel") != Vp->Str("with_fuel"))
							Expect(!Lay.V2NeedsReason(Parts, Slot, Kid).empty(), Kid + " wants its own fuel system first");
					}
					if (Vp->Flag("open_air"))
					{
						PartsMap Open = Parts;
						Open["air_filter"] = "none";
						Expect(Lay.V2NeedsReason(Open, Slot, Kid).empty(), Kid + " goes on with the airbox off");
						if (Parts.count("air_filter") && !Parts.at("air_filter").empty() && Parts.at("air_filter") != "none")
							Expect(!Lay.V2NeedsReason(Parts, Slot, Kid).empty(), Kid + " wants the airbox off first");
					}
					PartsMap With = Parts;
					With[Slot] = Kid;
					const std::vector<FittedPart> Got = Placer.Fit(With, Lay.Solve(With));
					V3 Lo, Hi, OwnLo, OwnHi;
					const bool bGot = Box(Got, Slot, Lo, Hi);
					Expect(bGot, Kid + " is drawn on " + Id);
					if (!bGot) continue;
					Expect(Lo.y > -L.Stats.RearRadius - 0.02 && Lo.x > L.RearAxle.x - 0.75 && Hi.x < L.FrontAxle.x + 0.6 && Hi.y < 1.75 && std::fabs(Lo.z + Hi.z) < 0.5,
						Kid + " sits on the bike (x " + std::to_string(Lo.x) + " to " + std::to_string(Hi.x) + ", y " + std::to_string(Lo.y) + " to " + std::to_string(Hi.y) + ")");
					if (Box(Fitted, Slot, OwnLo, OwnHi))
					{
						const double Off = std::hypot((Lo.x + Hi.x - OwnLo.x - OwnHi.x) * 0.5, (Lo.y + Hi.y - OwnLo.y - OwnHi.y) * 0.5);
						WorstKit = std::max(WorstKit, Off);
						Expect(Off < 0.45, Kid + " is where the bike's own " + Slot + " was (" + std::to_string(Off) + " m off)");
					}
				}
		}

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
	std::printf("%d mixed-brand part fits, worst %.3f m from the bike's own\n", MixChecks, WorstMix);
	std::printf("%d sprung bikes with a working shock\n", Sprung);
	std::printf("%d kit pieces fitted, worst %.3f m from the bike's own\n", KitChecks, WorstKit);
	std::printf("%d V2 bikes, %d failures\n", Bikes, Failures);
	return Failures ? 1 : 0;
}
