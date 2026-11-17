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
	Json Index = Json::MakeObject();
	int Bikes = 0;
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
		std::set<std::string> Nodes, Slots;
		double Low = 1e9, FrontLow = 1e9, RearLow = 1e9;
		V3 FrontMin(1e9, 1e9, 1e9), FrontMax(-1e9, -1e9, -1e9);
		bool AllMade = !Fitted.empty(), Named = true;
		for (const FittedPart& P : Fitted)
		{
			if (!P.Made) { AllMade = false; continue; }
			Nodes.insert(P.Node);
			Slots.insert(P.Slot);
			const auto In = Parts.find(P.Slot);
			Expect(In != Parts.end() && !In->second.empty() && In->second != "none", Id + " piece in a slot the bike has a part in: " + P.Slot);
			Tris += P.Made->Triangles();
			for (const PartSurface& S : P.Made->Surfaces)
			{
				++Surfaces;
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
		Expect(Named, Id + " every surface carries its look");
		{
			// [TFT] A TFT dash shows as itself with a screen for the game to light, in place of the
			// model's drawn clocks; any other dash leaves the model's clocks.
			const Json* Dash = Db.Find("dashboard", Parts["dashboard"]);
			const bool Tft = Dash && Dash->Str("style") == "tft";
			int Screens = 0, DrawnShown = 0;
			for (const FittedPart& P : Fitted)
			{
				if (P.Gauge.rfind("screen_", 0) == 0) ++Screens;
				if (P.Slot == "dashboard" && !P.Code && !P.Hidden) ++DrawnShown;
			}
			Expect(Screens == (Tft ? 1 : 0), Id + " a screen face only for a TFT dash (" + Parts["dashboard"] + ")");
			Expect(!Tft || DrawnShown == 0, Id + " a TFT dash hides the model's drawn clocks");
		}
		Expect(Nodes.count("steer") && Nodes.count("front_wheel") && Nodes.count("rear_wheel") && Nodes.count("front_hub") && Nodes.count(""), Id + " pieces on the steering, the wheels and the bike");
		Expect(Slots.count("tank") && Slots.count("engine") && Slots.count("frame") && Slots.count("saddle") && Slots.count("exhaust"), Id + " tank, engine, frame, saddle and exhaust pieces");
		Expect(std::fabs(FrontLow + L.Stats.FrontRadius) < 0.006, Id + " front tyre reaches the ground (" + Num2(FrontLow) + " for radius " + Num2(L.Stats.FrontRadius) + ")");
		Expect(std::fabs(RearLow + L.Stats.RearRadius) < 0.006, Id + " rear tyre reaches the ground (" + Num2(RearLow) + " for radius " + Num2(L.Stats.RearRadius) + ")");
		Expect(std::fabs((FrontMin.x + FrontMax.x) * 0.5 - L.FrontAxle.x) < 0.004 && std::fabs((FrontMin.y + FrontMax.y) * 0.5) < 0.004 &&
			std::fabs((FrontMin.z + FrontMax.z) * 0.5) < 0.004, Id + " front tyre round its axle");
		Expect(Low > -std::max(L.Stats.FrontRadius, L.Stats.RearRadius) - 0.02, Id + " nothing below the ground (" + Num2(Low) + ")");
		Expect(Tris > 20000 && Tris < 480000, Id + " triangles " + std::to_string(Tris));

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
