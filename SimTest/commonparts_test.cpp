// [common parts] Every common bolt-on part (Data/BikesV2/common_parts.json) fitted to every V2 bike whose
// family it suits, as the game fits it (BikeParts::FitV2 / V2Common): its pieces come in its slot, the
// bike's own pieces of the same sub are hidden, and the new ones sit where the old ones were (or by the
// bike's anchor point when it had none), on the bike and above the ground. Plain C++, no engine.
#include "BikeLayout.h"
#include "BikeParts.h"
#include "BikeShop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
	if (!Ok)
	{
		if (Failures < 60) std::printf("  FAIL %s\n", What.c_str());
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
		std::ifstream F(std::string(R) + "/Data/BikesV2/common_parts.json");
		if (F) return R;
	}
	return ".";
}

struct Box
{
	V3 Lo = V3(1e9, 1e9, 1e9), Hi = V3(-1e9, -1e9, -1e9);
	bool Any() const { return Lo.x <= Hi.x; }
	void Add(const V3& P)
	{
		Lo = V3(std::min(Lo.x, P.x), std::min(Lo.y, P.y), std::min(Lo.z, P.z));
		Hi = V3(std::max(Hi.x, P.x), std::max(Hi.y, P.y), std::max(Hi.z, P.z));
	}
	void Add(const FittedPart& P)
	{
		if (P.Made)
			for (const PartSurface& S : P.Made->Surfaces)
				for (const V3& Q : S.Pos) Add(Q);
	}
	V3 Mid() const { return (Lo + Hi) * 0.5; }
};

double Dist(const V3& A, const V3& B)
{
	const V3 D = A - B;
	return std::sqrt(D.x * D.x + D.y * D.y + D.z * D.z);
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string PartsText, ExtraText, CommonText, LayoutText, Error;
	PartsDb Db;
	BikeLayout Lay;
	if (!ReadFile(R + "/Data/Parts/parts.json", PartsText) || !ReadFile(R + "/Data/BikesV2/parts_v2.json", ExtraText) ||
		!ReadFile(R + "/Data/BikesV2/common_parts.json", CommonText) || !ReadFile(R + "/Data/Fitment/layout.json", LayoutText) ||
		!Db.LoadFromText(PartsText, Error) || !Db.AddFromText(ExtraText, Error) || !Db.AddFromText(CommonText, Error) || !Lay.Load(LayoutText, Db, Error))
	{
		std::printf("FAIL the catalogue with the common parts didn't load under %s (%s)\n", R.c_str(), Error.c_str());
		return 1;
	}
	BikeParts::SetV2Source([&R](const std::string& Model, PartMesh& Mesh, std::string& Info) {
		std::string Bytes, Err;
		return ReadFile(R + "/Data/BikesV2/" + Model + ".glb", Bytes) && Mesh.LoadGlb(Bytes, Err) && ReadFile(R + "/Data/BikesV2/" + Model + ".json", Info);
	});
	Json Index = Json::MakeObject();
	{
		std::string IndexText;
		Json Whole;
		if (ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) && Json::Parse(IndexText, Whole) && Whole.Get("parts")) Index = *Whole.Get("parts");
	}
	// The new slots are on every bike, optional, each its own kind.
	for (const CommonSlot& C : CommonSlots())
	{
		Expect(std::find(Lay.Slots().begin(), Lay.Slots().end(), C.Slot) != Lay.Slots().end(), std::string("slot ") + C.Slot + " is laid out");
		Expect(Lay.KindOfSlot(C.Slot) == C.Slot && Lay.MayBeEmpty(C.Slot), std::string("slot ") + C.Slot + " is its own kind and optional");
	}
	// Each common part, with the slots it can go in.
	struct Entry { std::string Kind, Id; const Json* Part; };
	std::vector<Entry> Common;
	for (const std::string& Kind : Db.Kinds())
		for (const std::string& Id : Db.Ids(Kind))
			if (const Json* P = Db.Find(Kind, Id))
				if (P->Get("common")) Common.push_back({Kind, Id, P});
	Expect(Common.size() >= 100, "the common parts are in the catalogue (" + std::to_string(Common.size()) + ")");
	std::map<std::string, int> Fits, ByAnchor, Lined;
	int Bikes = 0, Pairs = 0;
	for (const std::string& Frame : Db.Ids("frame"))
	{
		const Json* FramePart = Db.Find("frame", Frame);
		const Json* V2 = FramePart ? FramePart->Get("v2") : nullptr;
		if (!V2) continue;
		++Bikes;
		const std::string Family = V2->Str("family");
		const Json* Styles = FramePart->Get("styles");
		const std::string Style = Styles && !Styles->A.empty() ? Styles->A[0].S : std::string("naked");
		const PartsMap Stock = Shop::SetPart(Lay, Shop::StockBuild(Lay, Style), Style, "frame", Frame);
		const Layout L = Lay.Solve(Stock);
		BikeParts Placer(Lay, Index);
		const std::vector<FittedPart> Before = Placer.Fit(Stock, L);
		Box Whole;
		for (const FittedPart& P : Before) Whole.Add(P);
		for (const Entry& E : Common)
		{
			bool Suits = false;
			if (const Json* S = E.Part->Get("styles"))
				for (const Json& Each : S->A) Suits = Suits || Each.S == Family;
			if (!Suits) continue;
			const Json* Pieces = E.Part->Get("common")->Get("pieces");
			if (!Pieces || Pieces->A.empty()) continue;
			for (const std::string& Slot : Lay.Slots())
			{
				if (Lay.KindOfSlot(Slot) != E.Kind) continue;
				const PartTier Tier = Lay.PartFit(Stock, Slot, E.Id);
				Expect(Tier.Tier != "won't fit" || Slot == "front_mudguard", Frame + ": " + E.Id + " fits in " + Slot + " (" + Tier.Reason + ")");
				PartsMap Parts = Stock;
				Parts[Slot] = E.Id;
				const std::vector<FittedPart> After = Placer.Fit(Parts, L);
				++Pairs;
				const bool Tft = E.Part->Str("style") == "tft";
				for (const Json& Piece : Pieces->A)
				{
					const std::string Sub = Piece.Str("sub");
					if ((Slot.rfind("front_", 0) == 0 && Sub.find("_rear") != std::string::npos) || (Slot.rfind("rear_", 0) == 0 && Sub.find("_front") != std::string::npos)) continue;
					Box Old, New;
					for (const FittedPart& P : Before)
						if (P.Sub == Sub && !P.Code && !P.Hidden) Old.Add(P);
					int Shown = 0;
					for (const FittedPart& P : After)
					{
						if (P.Sub != Sub || P.Hidden) continue;
						if (P.Glb.rfind("v2c/", 0) == 0) { New.Add(P); ++Shown; Expect(P.Slot == Slot, Frame + ": " + E.Id + " piece in its slot"); }
						else Expect(false, Frame + ": " + E.Id + " leaves the bike's own " + Sub + " showing");
					}
					if (Tft) { Expect(Shown == 0, Frame + ": " + E.Id + " is the TFT unit, not its glb"); continue; }
					Expect(Shown == 1, Frame + ": " + E.Id + " one " + Sub + " piece (" + std::to_string(Shown) + ")");
					if (!New.Any()) continue;
					++Fits[Sub];
					// The lamps light: a headlamp's or fog lamp's lit face is a white lens the game's lights find (TorqueLights).
					if (Sub == "headlamp" || Sub == "fog_lights")
					{
						bool Lens = false;
						for (const FittedPart& P : After)
							if (P.Sub == Sub && P.Made && P.Glb.rfind("v2c/", 0) == 0)
								for (const PartSurface& S : P.Made->Surfaces) Lens = Lens || S.Material.find("/lens_white/") != std::string::npos || (S.Material.size() > 11 && S.Material.compare(S.Material.size() - 11, 11, "/lens_white") == 0);
						Expect(Lens, Frame + ": " + E.Id + " has a lit white lens");
					}
					// On the bike: inside the bike as drawn (with room), above the ground.
					const double Room = Sub == "silencer" ? 0.5 : 0.25;   // (a long can for a short one: drag pipes on a chopper)
					Expect(New.Lo.x > Whole.Lo.x - Room && New.Hi.x < Whole.Hi.x + Room && New.Hi.y < Whole.Hi.y + Room && New.Lo.y > Whole.Lo.y - 0.01 &&
						std::fabs(New.Mid().z) < 0.5, Frame + ": " + E.Id + " " + Sub + " on the bike");
					if (Old.Any())
					{
						++Lined[Sub];
						// (A silencer goes by its front end, where the pipe meets it; a plate hanger can be a side one for a tidy.)
						const double Off = Sub == "silencer" ? Dist(V3(Old.Hi.x, Old.Mid().y, Old.Mid().z), V3(New.Hi.x, New.Mid().y, New.Mid().z)) : Dist(Old.Mid(), New.Mid());
						Expect(Off < (Sub == "bar" ? 0.2 : Sub == "plate_hanger" ? 0.1 : 0.06), Frame + ": " + E.Id + " " + Sub + " where the bike's was (" + std::to_string(Off) + " m off)");
					}
					else ++ByAnchor[Sub];
				}
			}
		}
	}
	// COMMON_DUMP="<frame> <path.obj> <part id> ...": that bike with those parts, as fitted, to an obj (ours, metres) to look at.
	if (const char* Dump = std::getenv("COMMON_DUMP"))
	{
		std::istringstream Words(Dump);
		std::string Frame, Path, Id;
		Words >> Frame >> Path;
		const Json* FramePart = Db.Find("frame", Frame);
		const Json* Styles = FramePart ? FramePart->Get("styles") : nullptr;
		const std::string Style = Styles && !Styles->A.empty() ? Styles->A[0].S : std::string("naked");
		PartsMap Parts = Shop::SetPart(Lay, Shop::StockBuild(Lay, Style), Style, "frame", Frame);
		// (also "BARE": every slot but the frame emptied; "-slot": that slot emptied; a V2 model's part goes in its own slot)
		while (Words >> Id)
		{
			if (Id == "BARE")
			{
				for (auto& KV : Parts)
					if (KV.first != "frame") KV.second = "none";
				continue;
			}
			if (Id[0] == '-') { Parts[Id.substr(1)] = "none"; continue; }
			const Json* Part = Lay.Part(Id);
			const Json* Vp = Part ? Part->Get("v2part") : nullptr;
			if (Vp) { Parts[Vp->Str("slot")] = Id; continue; }
			for (const std::string& Slot : Lay.Slots())
				if (Lay.KindOfSlot(Slot) == Lay.KindOfPart(Id)) Parts[Slot] = Id;
		}
		const Layout L = Lay.Solve(Parts);
		BikeParts Placer(Lay, Index);
		std::ofstream Obj(Path);
		size_t Base = 1;
		for (const FittedPart& P : Placer.Fit(Parts, L))
		{
			if (P.Hidden || !P.Made) continue;
			Obj << "o " << P.Slot << "_" << (P.Sub.empty() ? "piece" : P.Sub) << (P.Glb.rfind("v2c/", 0) == 0 ? "_COMMON" : "") << "\n";
			for (const PartSurface& S : P.Made->Surfaces)
			{
				for (const V3& Q : S.Pos) Obj << "v " << Q.x << " " << Q.y << " " << Q.z << "\n";
				for (size_t T = 0; T + 2 < S.Idx.size(); T += 3) Obj << "f " << Base + S.Idx[T] << " " << Base + S.Idx[T + 1] << " " << Base + S.Idx[T + 2] << "\n";
				Base += S.Pos.size();
			}
		}
		std::printf("wrote %s\n", Path.c_str());
	}
	for (const auto& KV : Fits)
		std::printf("%-14s %4d fitted, %4d lined up on the bike's own, %4d by anchor\n", KV.first.c_str(), KV.second, Lined[KV.first], ByAnchor[KV.first]);
	std::printf("%d V2 bikes, %d bike-and-part fits, %d failures\n", Bikes, Pairs, Failures);
	return Failures ? 1 : 0;
}
