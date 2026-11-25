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
// How far off the line of the grip a piece on the end of the bar sits (beenster, build 171: "bar end mirrors don't
// line up on beach bar handlebars"): each side, the grip's long axis and outer end, and the middle of the piece's
// points within 30 mm of a point 15 mm beyond that end (its clamp or plug, which is round about the bar's line): how
// far that middle is from the line. 1e9 when the piece has nothing there at all.
double EndOffLine(const std::vector<FittedPart>& Fitted, const std::string& Sub)
{
	double Worst = 0.0;
	for (double SideSign : {-1.0, 1.0})
	{
		std::vector<V3> Grip;
		for (int Shop = 1; Shop >= 0 && Grip.empty(); --Shop)
			for (const FittedPart& P : Fitted)
				if (P.Sub == "grip" && P.Made && !P.Hidden && (P.Glb.rfind("v2c/", 0) == 0) == (Shop == 1))
					for (const PartSurface& Sf : P.Made->Surfaces)
						for (const V3& Q : Sf.Pos)
							if (Q.z * SideSign > 0.0) Grip.push_back(Q);
		if (Grip.size() < 8) return 1e9;
		V3 Mid(0.0, 0.0, 0.0);
		for (const V3& Q : Grip) Mid = Mid + Q;
		Mid = Mid * (1.0 / double(Grip.size()));
		double C[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
		for (const V3& Q : Grip)
		{
			const double D[3] = {Q.x - Mid.x, Q.y - Mid.y, Q.z - Mid.z};
			for (int I = 0; I < 3; ++I)
				for (int J = 0; J < 3; ++J) C[I][J] += D[I] * D[J];
		}
		V3 A(0.0, 0.0, 1.0);
		for (int It = 0; It < 40; ++It)
		{
			A = V3(C[0][0] * A.x + C[0][1] * A.y + C[0][2] * A.z, C[1][0] * A.x + C[1][1] * A.y + C[1][2] * A.z, C[2][0] * A.x + C[2][1] * A.y + C[2][2] * A.z);
			A = A * (1.0 / std::sqrt(A.x * A.x + A.y * A.y + A.z * A.z));
		}
		if (A.z * SideSign < 0.0) A = A * -1.0;
		double Far = -1e9;
		for (const V3& Q : Grip) Far = std::max(Far, (Q.x - Mid.x) * A.x + (Q.y - Mid.y) * A.y + (Q.z - Mid.z) * A.z);
		const V3 At = Mid + A * (Far + 0.015);
		V3 Sum(0.0, 0.0, 0.0);
		int N = 0;
		for (const FittedPart& P : Fitted)
			if (P.Sub == Sub && P.Made && !P.Hidden && P.Glb.rfind("v2c/", 0) == 0)
				for (const PartSurface& Sf : P.Made->Surfaces)
					for (const V3& Q : Sf.Pos)
						if (Q.z * SideSign > 0.0 && Dist(Q, At) < 0.030) { Sum = Sum + Q; ++N; }
		if (N < 12 && std::getenv("ENDDBG2"))
		{
			double Nearest = 1e9; int All = 0;
			for (const FittedPart& P : Fitted)
				if (P.Sub == Sub && P.Made && P.Glb.rfind("v2c/", 0) == 0)
					for (const PartSurface& Sf : P.Made->Surfaces)
						for (const V3& Q : Sf.Pos) { ++All; if (Q.z * SideSign > 0.0) Nearest = std::min(Nearest, Dist(Q, At)); }
			std::printf("ENDDBG2 side %.0f grip %zu axis %.2f %.2f %.2f far %.3f at %.3f %.3f %.3f piece pts %d nearest %.3f n %d\n", SideSign, Grip.size(), A.x, A.y, A.z, Far, At.x, At.y, At.z, All, Nearest, N);
		}
		if (N < 12) return 1e9;
		const V3 Cen = Sum * (1.0 / double(N)), Rel = Cen - Mid;
		const double Along = Rel.x * A.x + Rel.y * A.y + Rel.z * A.z;
		Worst = std::max(Worst, Dist(Rel, A * Along));
	}
	return Worst;
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
	int BarFits = 0;
	double WorstHand = 0.0, WorstBar = 0.0, WorstBy = 0.0, WorstCable = 0.0, WorstEnd = 0.0, WorstReach = 0.0;
	int EndFits = 0;
	int MirFits = 0;
	int SideHangers = 0;
	double WorstHanger = 0.0;
	double WorstOffLine = 0.0;
	double WorstMir = 0.0;
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
				// A bar: the grips the rider's hands go to (the layout's grip point) are the drawn grips, on the end of
				// the drawn bar, with the levers and switches by them (beenster, build 167: "ape bars dont seem to work":
				// the hands stayed at the bike's own grips, far below the fitted bar's).
				if (Slot == "handlebars" && E.Part->Get("common")->Get("grip_offset_mm"))
				{
					const Layout Lb = Lay.Solve(Parts);
					auto RightIn = [](const std::vector<FittedPart>& Fitted, const std::string& Sub, bool CommonOnly, V3& Mid, const V3* Near, double& Nearest) {
						V3 Lo(1e9, 1e9, 1e9), Hi(-1e9, -1e9, -1e9);
						bool Any = false;
						for (const FittedPart& P : Fitted)
						{
							if (P.Sub != Sub || P.Hidden || !P.Made || (CommonOnly && P.Glb.rfind("v2c/", 0) != 0)) continue;
							for (const PartSurface& Sf : P.Made->Surfaces)
								for (const V3& Q : Sf.Pos)
								{
									if (Q.z <= 0.0) continue;
									Any = true;
									Lo = V3(std::min(Lo.x, Q.x), std::min(Lo.y, Q.y), std::min(Lo.z, Q.z));
									Hi = V3(std::max(Hi.x, Q.x), std::max(Hi.y, Q.y), std::max(Hi.z, Q.z));
									if (Near) Nearest = std::min(Nearest, Dist(Q, *Near));
								}
						}
						Mid = (Lo + Hi) * 0.5;
						return Any;
					};
					auto RightOf = [&After, &RightIn](const std::string& Sub, bool CommonOnly, V3& Mid, const V3* Near, double& Nearest) {
						return RightIn(After, Sub, CommonOnly, Mid, Near, Nearest);
					};
					// (How far the bike's own hand point is from the middle of its own drawn grip: a slanted grip's, a few cm.)
					V3 OwnMid;
					double OwnUnused = 1e9;
					const double OwnOff = RightIn(Before, "grip", false, OwnMid, nullptr, OwnUnused) ? Dist(OwnMid, L.GripR) : 0.0;
					V3 GripMid, Other;
					double Unused = 1e9, BarGap = 1e9;
					++BarFits;
					if (RightOf("grip", false, GripMid, nullptr, Unused))
					{
						const double HandOff = Dist(GripMid, Lb.GripR);
						WorstHand = std::max(WorstHand, HandOff);
						// (The layout's grip is the bar's catalogue figure, within 6 cm; the game's hands take the drawn grip itself.)
						Expect(HandOff < OwnOff + 0.06, Frame + ": " + E.Id + " the rider's hand goes to the drawn grip (" + std::to_string(HandOff) + " m off, " + std::to_string(OwnOff) + " on the bike's own bar; the bar moved the grip " + std::to_string(Dist(Lb.GripR, L.GripR)) + " m)");
						Expect(RightOf("bar", true, Other, &GripMid, BarGap) && BarGap < 0.03, Frame + ": " + E.Id + " the grip is on the bar (" + std::to_string(BarGap) + " m from it)");
						WorstBar = std::max(WorstBar, BarGap);
						// The levers and switches are clamped on the bar as they are on the bike's own: no further from the
						// fitted bar's tube than they were from the bike's own (they were left behind by the grips, floating
						// 4 to 5 cm off four of the bars: build 167's ape bars).
						auto BarGapOf = [](const std::vector<FittedPart>& Fitted, const std::string& Sub, bool ShopBar) {
							double Nearest = 1e9;
							for (const FittedPart& A : Fitted)
							{
								if (A.Sub != Sub || A.Hidden || !A.Made) continue;
								for (const FittedPart& B : Fitted)
								{
									if (B.Sub != "bar" || B.Hidden || !B.Made || (B.Glb.rfind("v2c/", 0) == 0) != ShopBar) continue;
									for (const PartSurface& Sa : A.Made->Surfaces)
										for (size_t Ia = 0; Ia < Sa.Pos.size(); Ia += 3)
										{
											if (Sa.Pos[Ia].z <= 0.0) continue;
											for (const PartSurface& Sb : B.Made->Surfaces)
												for (size_t Ib = 0; Ib < Sb.Pos.size(); Ib += 3) Nearest = std::min(Nearest, Dist(Sa.Pos[Ia], Sb.Pos[Ib]));
										}
								}
							}
							return Nearest;
						};
						for (const char* By : {"levers", "switches"})
						{
							const double OwnGap = BarGapOf(Before, By, false), Gap = BarGapOf(After, By, true);
							if (OwnGap > 1.0 || Gap > 1e8) continue;
							WorstBy = std::max(WorstBy, Gap - OwnGap);
							Expect(Gap < OwnGap + 0.012, Frame + ": " + E.Id + " the " + By + " are on the bar (" + std::to_string(Gap) + " m from it, " + std::to_string(OwnGap) + " on the bike's own bar)");
						}
						// The cables and the brake hose reach the levers, each side: as near them as on the bike's own bar
						// (beenster, build 167: "for the ape bars you need to join up the cables").
						auto HoseGap = [](const std::vector<FittedPart>& Fitted, double SideSign, bool Throttle) {
							double Nearest = 1e9;
							for (const FittedPart& A : Fitted)
							{
								// (the throttle cable runs to the twist grip)
								if ((Throttle ? A.Sub != "grip" : A.Sub != "levers" && A.Sub != "switches") || A.Hidden || !A.Made) continue;
								for (const FittedPart& B : Fitted)
								{
									if (B.Hidden || !B.Made || B.Glb.rfind("v2c/", 0) == 0) continue;
									for (const PartSurface& Sb : B.Made->Surfaces)
									{
										// (the hoses and the clutch cable, or the throttle cable: the rubber in the bars' own piece)
										if (Throttle ? !(B.Slot == "handlebars" && B.Sub.empty() && Sb.Material.find("/rubber") != std::string::npos) : Sb.Material.find("d3_hose") == std::string::npos) continue;
										for (const PartSurface& Sa : A.Made->Surfaces)
											for (size_t Ia = 0; Ia < Sa.Pos.size(); Ia += 2)
											{
												if (Sa.Pos[Ia].z * SideSign <= 0.0) continue;
												for (size_t Ib = 0; Ib < Sb.Pos.size(); Ib += 2) Nearest = std::min(Nearest, Dist(Sa.Pos[Ia], Sb.Pos[Ib]));
											}
									}
								}
							}
							return Nearest;
						};
						for (const double SideSign : {1.0, -1.0, 2.0})
						{
							const bool Throttle = SideSign > 1.5;
							const double OwnGap = HoseGap(Before, SideSign, Throttle), Gap = HoseGap(After, SideSign, Throttle);
							if (OwnGap > 0.08) continue;   // (no cable to this side's lever on the bike's own bar either)
							WorstCable = std::max(WorstCable, Gap - OwnGap);
							Expect(Gap < OwnGap + 0.02, Frame + ": " + E.Id + " the " + (Throttle ? "throttle" : SideSign > 0.0 ? "right" : "left") + " cables reach the lever (" + std::to_string(Gap) + " m short, " + std::to_string(OwnGap) + " on the bike's own bar)");
						}
						// Mirrors sit on their perches with the bar fitted as they do on the bike's own bar, whatever mirrors
						// and levers are on (beenster, build 168: "little gap on the mirrors for ape bars").
						{
							auto MirGap = [](const std::vector<FittedPart>& Fitted) {
								double Worst = 0.0;
								for (double SideSign : {-1.0, 1.0})
								{
									double Nearest = 1e9;
									for (const FittedPart& A : Fitted)
									{
										if (A.Sub != "mirrors" || A.Hidden || !A.Made) continue;
										for (const FittedPart& B : Fitted)
										{
											if ((B.Sub != "levers" && B.Sub != "switches" && B.Sub != "bar" && B.Sub != "grip" && B.Sub != "bar_end") || B.Hidden || !B.Made) continue;
											for (const PartSurface& Sa : A.Made->Surfaces)
												for (size_t Ia = 0; Ia < Sa.Pos.size(); Ia += 2)
												{
													if (Sa.Pos[Ia].z * SideSign <= 0.0) continue;
													for (const PartSurface& Sb : B.Made->Surfaces)
														for (size_t Ib = 0; Ib < Sb.Pos.size(); Ib += 2) Nearest = std::min(Nearest, Dist(Sa.Pos[Ia], Sb.Pos[Ib]));
												}
										}
									}
									if (Nearest < 1e8) Worst = std::max(Worst, Nearest);
								}
								return Worst;
							};
							std::vector<std::pair<std::string, std::string>> Tries = {{"", ""}, {"front_master", ""}, {"levers", ""}};
							for (const char* TrySlot : {"mirrors", "levers", "switchgear"})
								for (const std::string& Opt : Shop::Options(Lay, TrySlot, Style, Stock))
									if (!Opt.empty() && (!Stock.count(TrySlot) || Opt != Stock.at(TrySlot))) Tries.push_back({TrySlot, Opt});
							// (no clutch lever on at all, a scratch build part way: beenster, build 171, "misfit mirror", the left mirror
							// standing in the air in front of the bar where the perch would be)
							for (const std::string& Opt : Shop::Options(Lay, "mirrors", Style, Stock))
								if (!Opt.empty()) Tries.push_back({"nolevers", Opt});
							for (const auto& Try : Tries)
							{
								PartsMap On = Parts, Off = Stock;
								if (Try.first == "nolevers") { On.erase("levers"); Off.erase("levers"); On["mirrors"] = Try.second; Off["mirrors"] = Try.second; }
								else if (!Try.first.empty() && Try.second.empty()) { On.erase(Try.first); Off.erase(Try.first); }
								else if (!Try.first.empty()) { On[Try.first] = Try.second; Off[Try.first] = Try.second; }
								const double OwnGap = MirGap(Placer.Fit(Off, L)), Gap = MirGap(Placer.Fit(On, L));
								if (std::getenv("MIRGAP")) std::printf("MIRGAP %s %s %s %.4f %.4f\n", Frame.c_str(), E.Id.c_str(), (Try.first + "=" + Try.second).c_str(), OwnGap, Gap);
								++MirFits;
								if (Family != "sport") WorstMir = std::max(WorstMir, Gap);
								Expect(Gap < 0.006 || (OwnGap > 0.08 && Family == "sport"), Frame + ": " + E.Id + " with " + (Try.first.empty() ? std::string("the bike's own mirrors") : Try.second.empty() ? "no " + Try.first : Try.second) + ": the mirrors sit on their perches (" + std::to_string(Gap) + " m off, " + std::to_string(OwnGap) + " on the bike's own bar)");
							}
						}
						// Bar ends fitted with the bar sit on the bar's end ("when I tried to fit bar ends to them they hung in the air").
						for (const Entry& End : Common)
						{
							if (End.Kind != Lay.KindOfSlot("bar_ends")) continue;
							PartsMap WithEnd = Parts;
							WithEnd["bar_ends"] = End.Id;
							const std::vector<FittedPart> Ended = Placer.Fit(WithEnd, L);
							double EndGap = 1e9;
							for (const FittedPart& A : Ended)
							{
								if (A.Sub != "bar_end" || A.Hidden || !A.Made || A.Glb.rfind("v2c/", 0) != 0) continue;
								for (const FittedPart& B : Ended)
								{
									if (B.Sub != "bar" || B.Hidden || !B.Made || B.Glb.rfind("v2c/", 0) != 0) continue;
									for (const PartSurface& Sa : A.Made->Surfaces)
										for (size_t Ia = 0; Ia < Sa.Pos.size(); Ia += 2)
											for (const PartSurface& Sb : B.Made->Surfaces)
												for (size_t Ib = 0; Ib < Sb.Pos.size(); Ib += 2) EndGap = std::min(EndGap, Dist(Sa.Pos[Ia], Sb.Pos[Ib]));
								}
							}
							const double OffLine = EndOffLine(Ended, "bar_end");
							if (std::getenv("ENDDBG")) std::printf("ENDOFF %s %s %s %.4f\n", Frame.c_str(), E.Id.c_str(), End.Id.c_str(), OffLine);
							WorstOffLine = std::max(WorstOffLine, OffLine);
							Expect(OffLine < 0.004, Frame + ": " + E.Id + " with " + End.Id + ": the bar end is in line with the grip (" + std::to_string(OffLine) + " m off its line)");
							++EndFits;
							WorstEnd = std::max(WorstEnd, EndGap);
							Expect(EndGap < 0.012, Frame + ": " + E.Id + " with " + End.Id + ": the bar end is on the bar (" + std::to_string(EndGap) + " m off it)");
						}
						// Bar-end mirrors fitted with the bar stand on the bar's end, in line with it.
						for (const Entry& Mir : Common)
						{
							if (Mir.Kind != Lay.KindOfSlot("mirrors") || Mir.Id.find("barend") == std::string::npos) continue;
							PartsMap WithMir = Parts;
							WithMir["mirrors"] = Mir.Id;
							const double OffLine = EndOffLine(Placer.Fit(WithMir, L), "mirrors");
							if (std::getenv("ENDDBG")) std::printf("ENDOFF %s %s %s %.4f\n", Frame.c_str(), E.Id.c_str(), Mir.Id.c_str(), OffLine);
							++EndFits;
							WorstOffLine = std::max(WorstOffLine, OffLine);
							Expect(OffLine < 0.004, Frame + ": " + E.Id + " with " + Mir.Id + ": the bar-end mirror is in line with the grip (" + std::to_string(OffLine) + " m off its line)");
						}
						// The rider can reach the grips: with the back leant anywhere from 20 degrees back to 65 forward, a
						// shoulder comes within an arm of the grip. A rider of ordinary build: hip joint to shoulder 0.47 m,
						// shoulders 0.19 m either side, shoulder to the wrist 0.55 m (0.97 of it used, as the pose does) and the wrist 0.085 m short of the grip's middle.
						{
							double Needs = 1e9;
							for (int Deg = -20; Deg <= 65; ++Deg)
							{
								const double A = Deg * 3.14159265 / 180.0;
								const V3 Shoulder(Lb.Hip.x + 0.47 * std::sin(A), Lb.Hip.y + 0.47 * std::cos(A), 0.19);
								Needs = std::min(Needs, Dist(Shoulder, GripMid));
							}
							WorstReach = std::max(WorstReach, Needs);
							Expect(Needs < 0.55 * 0.97 + 0.085, Frame + ": " + E.Id + " the rider can reach the grips (needs an arm of " + std::to_string(Needs) + " m)");
						}
					}
					else Expect(false, Frame + ": " + E.Id + " has no drawn grips");
				}
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
					// A bar end or a bar-end mirror on the bike's own bar: in line with the grip, on its end.
					if (Sub == "bar_end" || (Sub == "mirrors" && E.Id.find("barend") != std::string::npos))
					{
						const double OffLine = EndOffLine(After, Sub);
						if (std::getenv("ENDDBG")) std::printf("ENDOFF %s own %s %.4f\n", Frame.c_str(), E.Id.c_str(), OffLine);
						WorstOffLine = std::max(WorstOffLine, OffLine);
						Expect(OffLine < 0.004, Frame + ": " + E.Id + " on the bike's own bar: in line with the grip (" + std::to_string(OffLine) + " m off its line)");
					}
					// A side plate hanger is on the left end of the rear axle (beenster, build 171: "the side reg plate fitment
					// hangs in the air"): its boss within 2.5 cm of the rear wheel's middle, seen from the side.
					if (E.Id == "v2c_plate_hanger_side")
					{
						Box Wheel;
						for (const FittedPart& P : After)
							if (P.Slot == "rear_tyre" || P.Slot == "rear_wheel") Wheel.Add(P);
						double Nearest = 1e9;
						for (const FittedPart& P : After)
							if (P.Sub == "plate_hanger" && P.Made && !P.Hidden && P.Glb.rfind("v2c/", 0) == 0)
								for (const PartSurface& Sf : P.Made->Surfaces)
									for (const V3& Q : Sf.Pos) Nearest = std::min(Nearest, std::hypot(Q.x - Wheel.Mid().x, Q.y - Wheel.Mid().y));
						++SideHangers;
						WorstHanger = std::max(WorstHanger, Nearest);
						Expect(Wheel.Any() && Nearest < 0.025, Frame + ": the side plate hanger is on the rear axle's end (" + std::to_string(Nearest) + " m from it)");
					}
					// On the bike: inside the bike as drawn (with room), above the ground.
					const double Room = Sub == "silencer" ? 0.5 : 0.25;   // (a long can for a short one: drag pipes on a chopper)
					Expect(New.Lo.x > Whole.Lo.x - Room && New.Hi.x < Whole.Hi.x + Room && New.Hi.y < Whole.Hi.y + Room && New.Lo.y > Whole.Lo.y - 0.01 &&
						std::fabs(New.Mid().z) < 0.5, Frame + ": " + E.Id + " " + Sub + " on the bike");
					if (Old.Any())
					{
						++Lined[Sub];
						// (A silencer goes by its front end, where the pipe meets it; a plate hanger can be a side one for a tidy.)
						// (A long one, a classic's megaphone: the can at its rear end and a link pipe from its front, so both ends match.)
						const double Ends = std::max(std::fabs(Old.Hi.x - New.Hi.x), std::fabs(Old.Lo.x - New.Lo.x));
						const double Off = Sub == "silencer" ? std::min(Ends, Dist(V3(Old.Hi.x, Old.Mid().y, Old.Mid().z), V3(New.Hi.x, New.Mid().y, New.Mid().z))) : Dist(Old.Mid(), New.Mid());
						// (a piece for the end of the bar goes by the bar's end, not by where the bike's own mirrors or ends were)
						const bool bOnBarEnd = E.Id.find("barend") != std::string::npos || E.Id == "v2c_bar_end_mirror";
						Expect(bOnBarEnd || E.Id == "v2c_plate_hanger_side" || Off < (Sub == "bar" ? 0.2 : Sub == "plate_hanger" ? 0.1 : 0.06), Frame + ": " + E.Id + " " + Sub + " where the bike's was (" + std::to_string(Off) + " m off)");
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
			if (P.Hidden) continue;
			// (a part drawn from the game's own part glbs, as the game loads it: the brake pads)
			std::shared_ptr<const PartMesh> Loaded = P.Made;
			if (!Loaded)
			{
				std::string Bytes, Err;
				PartMesh G;
				if (!ReadFile(R + "/Data/Parts/Blender/" + P.Glb, Bytes) || !G.LoadGlb(Bytes, Err)) continue;
				Loaded = std::make_shared<PartMesh>(BikeParts::Draw(G, P));
			}
			const PartMesh* Mesh = Loaded.get();
			Obj << "o " << P.Slot << "_" << (P.Sub.empty() ? "piece" : P.Sub) << (P.Glb.rfind("v2c/", 0) == 0 ? "_COMMON" : "") << "\n";
			for (const PartSurface& S : Mesh->Surfaces)
			{
				if (std::getenv("COMMON_DUMP_NAMES") && (P.Slot == "handlebars" || S.Material.find("hose") != std::string::npos)) std::printf("  %s|%s|%s: %s (%zu points)\n", P.Slot.c_str(), P.Node.c_str(), P.Sub.c_str(), S.Material.c_str(), S.Pos.size());
				for (const V3& Q : S.Pos) Obj << "v " << Q.x << " " << Q.y << " " << Q.z << "\n";
				for (size_t T = 0; T + 2 < S.Idx.size(); T += 3) Obj << "f " << Base + S.Idx[T] << " " << Base + S.Idx[T + 1] << " " << Base + S.Idx[T + 2] << "\n";
				Base += S.Pos.size();
			}
		}
		std::printf("wrote %s\n", Path.c_str());
	}
	for (const auto& KV : Fits)
		std::printf("%-14s %4d fitted, %4d lined up on the bike's own, %4d by anchor\n", KV.first.c_str(), KV.second, Lined[KV.first], ByAnchor[KV.first]);
	std::printf("%d bike-and-bar fits: layout grip to drawn grip at worst %.3f m, grip to bar at worst %.3f m, levers and switches further off the bar than on the bike's own by at worst %.3f m\n", BarFits, WorstHand, WorstBar, WorstBy);
	std::printf("cables short of the levers by at worst %.3f m more than on the bike's own bar; %d bar-and-bar-end fits, a bar end at worst %.3f m off its bar; the longest reach to a grip needs an arm of %.3f m\n", WorstCable, EndFits, WorstEnd, WorstReach);
	std::printf("%d side plate hangers, at worst %.3f m from the rear axle's end\n", SideHangers, WorstHanger);
	std::printf("%d bar, mirror and lever fits: bar mirrors at worst %.3f m off their perches; bar ends and bar-end mirrors at worst %.4f m off the line of the grip\n", MirFits, WorstMir, WorstOffLine);
	std::printf("%d V2 bikes, %d bike-and-part fits, %d failures\n", Bikes, Pairs, Failures);
	return Failures ? 1 : 0;
}
