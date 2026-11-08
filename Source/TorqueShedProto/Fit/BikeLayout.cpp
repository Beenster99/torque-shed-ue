#include "BikeLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace TorqueFit
{
using TorqueSim::Clamp;
using TorqueSim::DegToRad;
using TorqueSim::Lerp;

namespace
{
const char* TierBolts = "bolts on";
const char* TierFab = "fabricate";
const char* TierNo = "won't fit";

// BikeRig's constants (bike_rig.gd).
constexpr double SpineR = 0.03;
constexpr double BarrelFoot = 0.1;
constexpr double HeadReach = 0.215;
constexpr double Deck = 0.14;
constexpr double HeadKMin = 0.62;
constexpr double VtwinTop = 0.88;
constexpr double TankHigh = 0.12;
constexpr double RearClear = 0.04;
constexpr double TankSqueeze = 0.75;
constexpr double BellyPan = 0.07;
constexpr double PostClear = 0.07;
constexpr double OutputBack = 0.17;
constexpr double OutputUp = 0.03;
constexpr double OutputBackVtwin = 0.22;
const double Inf = std::numeric_limits<double>::infinity();

struct Family
{
	const char* Name;
	std::vector<std::string> Layouts;
	double Cc;
};

const std::vector<Family>& Families()
{
	static const std::vector<Family> F = {
		{"cradle", {"single", "twin", "parallel", "triple", "inline", "boxer", "vtwin", "2stroke"}, 1300.0},
		{"backbone", {"single", "twin", "parallel", "triple", "boxer", "vtwin", "2stroke"}, 1300.0},
		{"trestle", {"single", "twin", "parallel", "triple", "inline", "vtwin"}, 1300.0},
		{"spar", {"twin", "parallel", "triple", "inline", "vtwin"}, 1400.0},
		{"perimeter", {"twin", "parallel", "triple", "inline", "vtwin"}, 1300.0},
		{"adv", {"single", "twin", "parallel", "triple", "inline", "boxer", "vtwin"}, 1300.0},
		{"mx", {"single", "2stroke"}, 700.0},
		{"softail", {"vtwin"}, 2000.0},
		{"chopper", {"vtwin"}, 2000.0},
		{"cruiser", {"vtwin", "boxer", "inline"}, 2000.0},
	};
	return F;
}

const Family& FamilyOf(const std::string& Name)
{
	for (const Family& F : Families())
		if (Name == F.Name) return F;
	return Families()[0];
}

bool NoFit(const std::string& FamilyName, const std::string& LayoutName)
{
	if (FamilyName == "mx") return LayoutName == "boxer" || LayoutName == "inline" || LayoutName == "triple";
	if (FamilyName == "perimeter" || FamilyName == "spar") return LayoutName == "boxer";
	return false;
}

bool IsPostFamily(const std::string& F)
{
	return F == "cradle" || F == "softail" || F == "chopper" || F == "cruiser";
}

// A frame's own Rig built proportions (BikeRig.FRAME_RIG, with the frame's "rig" over them).
struct FrameRig
{
	double Gauge = 1.0, Twin = 0.0, Top = 0.0, TankLow = 0.0, Drop = 0.0, Deep = 1.0, Wide = 1.0, Loop = 0.06, Slope = 0.0;
	bool Split = false, Gusset = false, Steady = false;
};

FrameRig FrameRigOf(const Json* Frame)
{
	FrameRig R;
	const Json* Own = Frame ? Frame->Get("rig") : nullptr;
	if (!Own || !Own->IsObject()) return R;
	R.Gauge = Own->Num("gauge", R.Gauge);
	R.Twin = Own->Num("twin", R.Twin);
	R.Top = Own->Num("top", R.Top);
	R.TankLow = Own->Num("tank_low", R.TankLow);
	R.Drop = Own->Num("drop", R.Drop);
	R.Deep = Own->Num("deep", R.Deep);
	R.Wide = Own->Num("wide", R.Wide);
	R.Loop = Own->Num("loop", R.Loop);
	R.Slope = Own->Num("slope", R.Slope);
	R.Split = Own->Flag("split", R.Split);
	R.Gusset = Own->Flag("gusset", R.Gusset);
	R.Steady = Own->Flag("steady", R.Steady);
	return R;
}

double Num(const Json* J, const std::string& Key, double Default)
{
	return J ? J->Num(Key, Default) : Default;
}

std::string Str(const Json* J, const std::string& Key, const std::string& Default = std::string())
{
	return J ? J->Str(Key, Default) : Default;
}

const Json* Sub(const Json* J, const std::string& Key)
{
	return J ? J->Get(Key) : nullptr;
}

double Item(const Json* Arr, size_t I, double Default)
{
	return (Arr && Arr->IsArray() && I < Arr->A.size() && Arr->A[I].IsNumber()) ? Arr->A[I].N : Default;
}

V3 LerpV(const V3& A, const V3& B, double T) { return A + (B - A) * T; }

// A profile key list's [half-width, rise, drop] at u along it (BikeModel._key_at).
void KeyAt(const Json& Keys, double U, double Out[3])
{
	const size_t N = Keys.A.size();
	for (size_t I = 0; I + 1 < N; ++I)
	{
		const Json& K0 = Keys.A[I];
		const Json& K1 = Keys.A[I + 1];
		if (U <= Item(&K1, 0, 0.0))
		{
			double F = (U - Item(&K0, 0, 0.0)) / std::max(Item(&K1, 0, 0.0) - Item(&K0, 0, 0.0), 0.0001);
			F = F * F * (3.0 - 2.0 * F);
			for (size_t C = 0; C < 3; ++C) Out[C] = Lerp(Item(&K0, C + 1, 0.0), Item(&K1, C + 1, 0.0), F);
			return;
		}
	}
	const Json& Last = Keys.A[N - 1];
	for (size_t C = 0; C < 3; ++C) Out[C] = Item(&Last, C + 1, 0.0);
}

// A Catmull-Rom curve through Points, PerSeg steps between each pair (ShapeKit.smooth).
V3 Catmull(const V3& P0, const V3& P1, const V3& P2, const V3& P3, double T)
{
	const double T2 = T * T, T3 = T2 * T;
	return (P1 * 2.0 + (P2 - P0) * T + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P1 * 3.0 - P0 - P2 * 3.0 + P3) * T3) * 0.5;
}

std::vector<V3> Smooth(const std::vector<V3>& P, int PerSeg)
{
	if (P.size() < 3) return P;
	std::vector<V3> Out;
	const int N = static_cast<int>(P.size());
	for (int I = 0; I + 1 < N; ++I)
	{
		const V3& P0 = P[static_cast<size_t>(std::max(I - 1, 0))];
		const V3& P1 = P[static_cast<size_t>(I)];
		const V3& P2 = P[static_cast<size_t>(I + 1)];
		const V3& P3 = P[static_cast<size_t>(std::min(I + 2, N - 1))];
		for (int K = 0; K < PerSeg; ++K) Out.push_back(Catmull(P0, P1, P2, P3, static_cast<double>(K) / PerSeg));
	}
	Out.push_back(P.back());
	return Out;
}

// The knee, between the hip and the peg, a thigh and a shin of 0.45 m each, bent forward.
V3 Knee(const V3& Hip, const V3& Peg)
{
	const V3 D = Peg - Hip;
	const double N = D.Length();
	const double Leg = 0.45;
	const double Half = std::min(N * 0.5, Leg - 0.001);
	const double Rise = std::sqrt(std::max(Leg * Leg - Half * Half, 0.0));
	const V3 Mid = Hip + D * 0.5;
	V3 Fwd = V3(0.0, D.z, -D.y).Normalized();
	if (Fwd.z > 0.0) Fwd = -Fwd;
	return Mid + Fwd * Rise;
}

std::string LayoutWord(const std::string& L)
{
	if (L == "inline") return "inline four";
	if (L == "vtwin") return "V-twin";
	if (L == "parallel") return "parallel twin";
	if (L == "mono") return "single";
	return L;
}

std::string FamilyWord(const std::string& F)
{
	if (F == "mx") return "MX";
	if (F == "adv") return "adventure";
	return F;
}

std::string WithA(const std::string& W)
{
	const char C = W.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(W[0])));
	return ((C == 'a' || C == 'e' || C == 'i' || C == 'o' || C == 'u' || W == "MX") ? "an " : "a ") + W;
}

std::string Join(const std::vector<std::string>& V, const char* Sep)
{
	std::string Out;
	for (size_t I = 0; I < V.size(); ++I) Out += (I ? Sep : "") + V[I];
	return Out;
}

std::string Capital(const std::string& S)
{
	std::string Out = S;
	if (!Out.empty()) Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	return Out;
}

std::string Get(const PartsMap& P, const std::string& Slot)
{
	auto It = P.find(Slot);
	return It == P.end() || It->second.empty() ? std::string("none") : It->second;
}

// The engine's hard points in the Godot frame (BikeRig.hard_points).
EngineHard HardG(const Json* Engine)
{
	EngineHard H;
	const std::string LayoutName = Str(Engine, "layout", "parallel");
	const int Cyl = static_cast<int>(Num(Engine, "cylinders", 2.0));
	const double S = Clamp(std::pow(Num(Engine, "displacement", 700.0) / 700.0, 0.3), 0.55, 1.2);
	double K = 1.0;
	if (LayoutName != "boxer" && LayoutName != "2stroke")
	{
		const double B = Num(Engine, "bore", 80.0) / 1000.0, St = Num(Engine, "stroke", 70.0) / 1000.0;
		double Want = St * 0.5 + 1.7 * St + 0.45 * B + 1.45 * B;
		if (LayoutName == "vtwin") Want *= VtwinTop;
		K = Clamp((Want - Deck * S) / (BarrelFoot * S + HeadReach), HeadKMin, 1.0);
	}
	const double BoreR = Clamp(Num(Engine, "bore", 80.0) * 0.0006, 0.022, 0.062);
	double BlockW = (0.2 + 0.11 * (Cyl - 1)) * S;
	if (LayoutName == "vtwin")
	{
		BlockW = 0.22 * S;
		const double V = Num(Engine, "v_angle", 0.0);
		const double Half = (V > 0.0 && V < 90.0) ? DegToRad(V) * 0.5 : TorqueSim::Pi / 8.0;
		for (double Dir : {-1.0, 1.0})
		{
			const V3 Up = V3(0.0, std::cos(Half), std::sin(Half) * Dir) * K;
			const V3 Base = V3(0.0, 0.13 * S, 0.07 * Dir * S) + Up * 0.12 * S;
			H.Ups.push_back(Up);
			H.HeadTops.push_back(Base + Up * HeadReach);
		}
	}
	else if (LayoutName == "boxer")
	{
		BlockW = 0.24 * S;
		for (double Dir : {-1.0, 1.0})
		{
			const V3 Up = V3(Dir, 0.08, -0.05).Normalized();
			const V3 Base = V3(0.1 * Dir * S, 0.03, -0.06) + Up * 0.1 * S;
			H.Ups.push_back(Up);
			H.HeadTops.push_back(Base + Up * 0.235);
		}
	}
	else
	{
		const double Lean = LayoutName == "twin" ? 0.04 : (LayoutName == "2stroke" ? 0.4 : 0.25);
		const V3 Up = V3(0.0, 1.0, -Lean).Normalized() * K;
		const double Reach = LayoutName == "2stroke" ? 0.145 : HeadReach;
		for (int C = 0; C < Cyl; ++C)
		{
			const double Cx = (C - (Cyl - 1) * 0.5) * std::max(0.12 * S, BoreR * 2.4);
			const V3 Base = V3(Cx, Deck * S, -0.04) + Up * BarrelFoot * S;
			H.Ups.push_back(Up);
			H.HeadTops.push_back(Base + Up * Reach);
		}
	}
	double HeadY = Deck * S, Front = -0.17 * S, Back = 0.17 * S, HeadW = BlockW * 0.5;
	double TopsBack = -Inf;
	for (const V3& T : H.HeadTops)
	{
		HeadY = std::max(HeadY, T.y);
		Front = std::min(Front, T.z - 0.08);
		HeadW = std::max(HeadW, std::fabs(T.x) + (LayoutName == "boxer" ? 0.03 : 0.07));
		TopsBack = std::max(TopsBack, T.z + 0.08);
	}
	H.Output = V3(-0.125, OutputUp * S, (LayoutName == "vtwin" ? OutputBackVtwin : OutputBack) * S);
	H.HeadFront = V3(0.0, HeadY, Front);
	H.HeadRear = V3(0.0, HeadY, H.HeadTops.empty() ? Back : std::max(Back, TopsBack));
	H.CaseFront = V3(0.0, 0.0, -0.17 * S);
	H.CaseRear = V3(0.0, 0.0, 0.17 * S);
	H.Sump = V3(0.0, -0.13 * S - (LayoutName == "vtwin" ? 0.004 : 0.059), -0.01);
	H.CaseHalfW = BlockW * 0.5;
	H.HeadHalfW = HeadW;
	H.MountFU = V3(0.0, 0.1 * S, -0.15 * S);
	H.MountFL = V3(0.0, -0.1 * S, -0.15 * S);
	H.MountRU = V3(0.0, 0.1 * S, 0.16 * S);
	H.MountRL = V3(0.0, -0.1 * S, 0.16 * S);
	H.HeadSteady = V3(0.0, HeadY + 0.01, 0.0);
	H.AirboxTop = V3(0.0, HeadY + 0.02, 0.06);
	H.TopY = HeadY;
	H.HeadK = K;
	H.Scale = S;
	for (size_t I = 0; I < H.HeadTops.size(); ++I)
	{
		const V3 T = H.HeadTops[I];
		const V3 Up = H.Ups[I].Normalized();
		H.Intakes.push_back({T - Up * 0.05 + V3(0.0, 0.0, 0.06), V3(0.0, 0.3, 1.0).Normalized()});
		H.Exhausts.push_back({T - Up * 0.06 + V3(0.0, 0.0, -0.06), V3(0.0, -0.3, -1.0).Normalized()});
	}
	return H;
}

EngineHard HardToOurs(const EngineHard& G)
{
	EngineHard O = G;
	auto C = [](V3& V) { V = FromGodot(V); };
	for (V3* V : {&O.Output, &O.HeadFront, &O.HeadRear, &O.CaseFront, &O.CaseRear, &O.Sump, &O.MountFU, &O.MountFL,
			 &O.MountRU, &O.MountRL, &O.HeadSteady, &O.AirboxTop})
		C(*V);
	for (V3& V : O.HeadTops) C(V);
	for (V3& V : O.Ups) C(V);
	for (auto& P : O.Intakes) { C(P.first); C(P.second); }
	for (auto& P : O.Exhausts) { C(P.first); C(P.second); }
	return O;
}
} // namespace

LayoutAdjust LayoutAdjust::Clean() const
{
	auto Fix = [](double V, double Lo, double Hi) { return std::isfinite(V) && std::fabs(V) > 0.0001 ? Clamp(V, Lo, Hi) : 0.0; };
	LayoutAdjust A;
	A.TankUpMm = Fix(TankUpMm, 0.0, 30.0);
	A.PegsMm = Fix(PegsMm, -40.0, 40.0);
	A.HoopUpMm = Fix(HoopUpMm, -20.0, 40.0);
	A.HoopDeg = Fix(HoopDeg, -6.0, 8.0);
	return A;
}

std::vector<AdjustRange> BikeLayout::AdjustRanges()
{
	return {{"tank_up", "Tank mounts up", "mm", 0.0, 30.0, 1.0},
		{"pegs", "Pegs back along the plate", "mm", -40.0, 40.0, 1.0},
		{"hoop_up", "Seat hoop height", "mm", -20.0, 40.0, 1.0},
		{"hoop_deg", "Seat hoop angle", "deg", -6.0, 8.0, 0.5}};
}

bool BikeLayout::Load(const std::string& Text, const PartsDb& InDb, std::string& Error)
{
	Json Parsed;
	if (!Json::Parse(Text, Parsed, &Error)) return false;
	const Json* St = Parsed.Get("styles");
	const Json* Sl = Parsed.Get("slots");
	const Json* Sk = Parsed.Get("slot_kind");
	if (!St || !St->IsArray() || !Sl || !Sl->IsArray() || !Sk || !Sk->IsObject() || !Parsed.Get("lines") || !Parsed.Get("tank_shapes"))
	{
		Error = "layout.json: missing styles, slots, slot_kind, lines or tank_shapes";
		return false;
	}
	Data = std::move(Parsed);
	StyleList.clear();
	SlotList.clear();
	SlotKind.clear();
	PartKind.clear();
	for (const Json& S : Data.Get("styles")->A) StyleList.push_back(S.S);
	for (const Json& S : Data.Get("slots")->A) SlotList.push_back(S.S);
	for (const auto& KV : Data.Get("slot_kind")->O) SlotKind[KV.first] = KV.second.S;
	for (const std::string& K : InDb.Kinds())
		for (const std::string& Id : InDb.Ids(K)) PartKind[Id] = K;
	Db = &InDb;
	return true;
}

std::string BikeLayout::StyleName(const std::string& Style) const
{
	const Json* N = Data.Get("style_names");
	return N ? N->Str(Style, Style) : Style;
}

std::string BikeLayout::KindOfSlot(const std::string& Slot) const
{
	auto It = SlotKind.find(Slot);
	return It == SlotKind.end() ? std::string() : It->second;
}

std::string BikeLayout::KindOfPart(const std::string& Id) const
{
	auto It = PartKind.find(Id);
	return It == PartKind.end() ? std::string() : It->second;
}

const Json* BikeLayout::Part(const std::string& Id) const
{
	if (!Db || Id.empty() || Id == "none") return nullptr;
	auto It = PartKind.find(Id);
	return It == PartKind.end() ? nullptr : Db->Find(It->second, Id);
}

bool BikeLayout::MayBeEmpty(const std::string& Slot) const
{
	for (const char* Key : {"optional_slots", "frame_own_slots"})
		if (const Json* L = Data.Get(Key))
			for (const Json& S : L->A)
				if (S.S == Slot) return true;
	return false;
}

PartsMap BikeLayout::ReferenceBuild(const std::string& Style) const
{
	PartsMap Out;
	const Json* B = Sub(Data.Get("reference_builds"), Style);
	if (!B) B = Sub(Data.Get("reference_builds"), "classic");
	if (B)
		for (const auto& KV : B->O) Out[KV.first] = KV.second.S;
	return Out;
}

std::vector<std::string> BikeLayout::StylesOf(const std::string& Id) const
{
	std::vector<std::string> Out;
	if (const Json* S = Sub(Part(Id), "styles"))
		for (const Json& T : S->A) Out.push_back(T.S);
	return Out;
}

std::string BikeLayout::DetectStyle(const PartsMap& Parts, double* Coherence) const
{
	std::map<std::string, double> Scores;
	double Total = 0.0;
	const Json* Weights = Data.Get("style_weight");
	const Json* Empty = Data.Get("empty_styles");
	if (Weights)
		for (const auto& KV : Weights->O)
		{
			const std::string Id = Get(Parts, KV.first);
			std::vector<std::string> Tags;
			if (Id == "none")
			{
				if (const Json* E = Sub(Empty, KV.first))
					for (const Json& T : E->A) Tags.push_back(T.S);
			}
			else
				Tags = StylesOf(Id);
			if (Tags.empty()) continue;
			Total += KV.second.N;
			for (const std::string& T : Tags) Scores[T] += KV.second.N;
		}
	std::string Best;
	for (const std::string& St : StyleList)
		if (Scores[St] > 0.0 && (Best.empty() || Scores[St] > Scores[Best] + 0.001)) Best = St;
	if (Coherence) *Coherence = (!Best.empty() && Total > 0.0) ? Scores[Best] / Total : 0.0;
	return Best;
}

const Json* BikeLayout::Lines(const std::string& Style) const
{
	return Sub(Sub(Data.Get("lines"), Style), "lines");
}

double BikeLayout::LineNumber(const std::string& Style, const std::string& Line, const std::string& Key, double Default) const
{
	return Num(Sub(Lines(Style), Line), Key, Default);
}

std::string BikeLayout::StyleFor(const PartsMap& Parts) const
{
	const std::vector<std::string> Own = StylesOf(Get(Parts, "frame"));
	const std::string Best = DetectStyle(Parts);
	if (!Best.empty() && (Own.empty() || std::find(Own.begin(), Own.end(), Best) != Own.end()) && Lines(Best)) return Best;
	for (const std::string& St : Own)
		if (Lines(St)) return St;
	return "classic";
}

std::string BikeLayout::Brief(const PartsMap& Parts, Json& LinesOut) const
{
	const std::string Style = StyleFor(Parts);
	LinesOut = *Lines(Style);
	const std::string Saddle = Get(Parts, "saddle");
	const std::vector<std::string> Own = Saddle == "none" ? std::vector<std::string>() : StylesOf(Saddle);
	if (!Own.empty() && std::find(Own.begin(), Own.end(), Style) == Own.end())
		for (const std::string& St : Own)
			if (const Json* Theirs = Lines(St))
			{
				Json* Seat = LinesOut.GetMutable("4_seat");
				const Json* TheirSeat = Theirs->Get("4_seat");
				if (Seat && TheirSeat)
					for (const char* K : {"tail_u", "tail_rise_mm", "rail_deg"})
						if (const Json* V = TheirSeat->Get(K)) Seat->Set(K, *V);
				return St;
			}
	return std::string();
}

BuildStats BikeLayout::Stats(const PartsMap& Parts) const
{
	BuildStats S;
	const Json* Frame = Part(Get(Parts, "frame"));
	S.Wheelbase = Num(Frame, "wheelbase", S.Wheelbase);
	S.RakeDeg = Num(Frame, "rake_deg", S.RakeDeg);
	S.SeatHeight = Num(Frame, "seat_height", 0.8) + Num(Part(Get(Parts, "saddle")), "height", 0.05);
	const Json* Geo = Sub(Frame, "geo");
	S.PivotY = Num(Geo, "pivot_y", 0.16);
	S.Arm = Num(Geo, "arm", 0.6);
	const Json* Fork = Part(Get(Parts, "fork"));
	S.ForkLength = Num(Fork, "length", S.ForkLength);
	S.FrontTravel = Num(Fork, "travel", S.FrontTravel);
	S.RearTravel = Num(Part(Get(Parts, "shock")), "travel", S.RearTravel);
	auto Radius = [&](const char* Wheel, const char* Tyre, double Default) {
		const Json* W = Part(Get(Parts, Wheel));
		const Json* T = Part(Get(Parts, Tyre));
		if (!W || !T) return Default;
		return Num(W, "diameter", 17.0) * 0.0127 + Num(T, "width", 120.0) / 1000.0 * Num(T, "aspect", 0.7);
	};
	S.FrontRadius = Radius("front_wheel", "front_tyre", S.FrontRadius);
	S.RearRadius = Radius("rear_wheel", "rear_tyre", S.RearRadius);
	return S;
}

EngineHard BikeLayout::HardPoints(const std::string& EngineId) const
{
	return HardToOurs(HardG(Part(EngineId)));
}

Layout BikeLayout::Solve(const PartsMap& Parts, const LayoutAdjust& InAdjust) const
{
	const LayoutAdjust Adj = InAdjust.Clean();
	Layout R;
	Json L;
	R.MixedTail = Brief(Parts, L);
	R.Style = StyleFor(Parts);
	const BuildStats B = Stats(Parts);
	R.Stats = B;
	const Json* Frame = Part(Get(Parts, "frame"));
	const Json* Geo = Sub(Frame, "geo");
	const std::string FamilyName = Str(Frame, "style", "cradle");
	R.Family = FamilyName;
	R.Frame = Get(Parts, "frame");
	const FrameRig Frig = FrameRigOf(Frame);
	const double DrawnR = SpineR * Frig.Gauge;
	const Json* Engine = Part(Get(Parts, "engine"));
	const double Wb = B.Wheelbase, Zf = -Wb * 0.5, Zr = Wb * 0.5;
	const double Ground = -B.RearRadius;
	auto AtU = [&](double U) { return Zf + U * Wb; };
	const double Rake = DegToRad(B.RakeDeg);
	const V3 Axis(0.0, std::cos(Rake), std::sin(Rake));
	const V3 Head = V3(0.0, 0.0, Zf) + Axis * B.ForkLength;
	const V3 HeadBottom = Head - Axis * 0.16;
	const V3 Pivot(0.0, Num(Geo, "pivot_y", 0.16), Zr - Num(Geo, "arm", 0.6));
	const double SeatY = B.SeatHeight - B.RearRadius;
	std::vector<std::pair<std::string, bool>> Notes;

	// 6: the engine, its output shaft just ahead of the pivot; 8: its sump at the style's ground clearance.
	const EngineHard Hp = HardG(Engine);
	const double S = Hp.Scale;
	const Json* Drive = L.Get("6_drive");
	const double OutZ = Pivot.z - Num(Drive, "countershaft_ahead_mm", 70.0) / 1000.0;
	V3 Crank(0.0, 0.0, OutZ - Hp.Output.z);
	const Json* G8 = L.Get("8_ground");
	const double Clear = Num(G8, "clear_mm", 150.0) / 1000.0;
	const double ClearMin = Clear - Num(G8, "tol_mm", 40.0) / 1000.0;
	Crank.y = Ground + Clear + 0.02 - Hp.Sump.y;
	Crank.y = std::max(Crank.y, Pivot.y - 0.15 - Hp.Output.y);
	const Json* Fairing = Part(Get(Parts, "fairing"));
	if (Str(Fairing, "style") == "race" && Str(Sub(Fairing, "look"), "shape", "race") != "seeley") Crank.y += BellyPan;
	// 7: the cases and heads clear of the front tyre, the fork fully compressed.
	const double FrontClearWant = Num(L.Get("7_front"), "clear_mm", 30.0) / 1000.0;
	const V3 Wheel = V3(0.0, B.FrontRadius - B.RearRadius, Zf) + Axis * 0.1;
	double Nearest = Inf;
	for (const V3& P : {Hp.CaseFront, Hp.Sump}) Nearest = std::min(Nearest, (Crank + P - Wheel).Length() - B.FrontRadius);
	const double HeadsY = Crank.y + Hp.TopY;
	double HeadsZ = Crank.z;
	for (const V3& T : Hp.HeadTops)
		if (Crank.y + T.y >= HeadsY - 0.001) HeadsZ = Crank.z + T.z;
	const Json* L1 = L.Get("1_backbone");
	const bool Tube = Str(L1, "kind", "tube") == "tube" || IsPostFamily(FamilyName);
	const double SpineRad = Tube ? DrawnR : 0.0;
	double ClearOver = Num(L1, "clear_mm", 40.0) / 1000.0 + SpineRad * 2.0;
	if (Frig.Top > 0.0) ClearOver += 0.012;
	std::vector<V3> OverHeads;
	for (const V3& T : Hp.HeadTops) OverHeads.push_back(V3(0.0, Crank.y + T.y + ClearOver, Crank.z + T.z));
	if (OverHeads.empty()) OverHeads.push_back(V3(0.0, HeadsY + ClearOver, HeadsZ));

	// 1-3: the backbone over the heads, the tank on it, its tail meeting the seat's nose.
	const Json* T2 = L.Get("2_tank");
	const Json* T3 = L.Get("3_tank_seat");
	const Json* T4 = L.Get("4_seat");
	const Json* Tank = Part(Get(Parts, "tank"));
	const Json* TankLook = Sub(Tank, "look");
	const Json* Shapes = Data.Get("tank_shapes");
	const Json* Shape = Sub(Shapes, Str(TankLook, "shape", Str(Tank, "style", "cafe")));
	if (!Shape) Shape = Sub(Shapes, "cafe");
	const Json* Keys = Shape->Get("rig_keys") ? Shape->Get("rig_keys") : Shape->Get("keys");
	const double Size = std::pow(Num(Tank, "capacity", 15.0) / 15.0, 0.33);
	const Json* LookScale = Sub(TankLook, "scale");
	const bool HasScale = LookScale && LookScale->IsArray() && LookScale->A.size() >= 3;
	const double ScZ = HasScale ? Item(LookScale, 2, 1.0) : 1.0;
	const double ScX = HasScale ? Item(LookScale, 0, 1.0) : 1.0;
	double Tall = 0.0;
	for (const Json& K : Keys->A) Tall = std::max(Tall, (Item(&K, 2, 0.0) + Item(&K, 3, 0.0)) * Size * ScZ);
	const Json& EndK = Keys->A.back();
	const double EndH = (Item(&EndK, 2, 0.0) + Item(&EndK, 3, 0.0)) * Size * ScZ;
	const double TopMax = Head.y + Num(T2, "top_over_yoke_mm", 0.0) / 1000.0 - Frig.TankLow;
	const V3 Nose = Head + V3(0.0, -0.03, Num(T2, "nose_behind_axis_mm", 70.0) / 1000.0);
	double SeatFrontZ = AtU(Num(T4, "front_u", 0.55));
	double RearZ = AtU(Num(T3, "tank_rear_u", 0.55));
	const bool Overlap = T3 ? T3->Flag("seat_overlap", false) : false;
	if (!Overlap) RearZ = std::min(RearZ, SeatFrontZ + 0.015);
	const double ShapeLen = Shape->Num("len", 0.6);
	const double WantLen = ShapeLen * Lerp(0.85, 1.0, Size) * ScX * TankSqueeze;
	if (!Overlap)
	{
		RearZ = Clamp(Nose.z + 0.03 + WantLen - 0.03, RearZ, std::max(RearZ, Pivot.z + 0.02));
		SeatFrontZ = std::max(SeatFrontZ, RearZ - 0.015);
	}
	if (IsPostFamily(FamilyName))
	{
		double BackOfHeads = -Inf;
		for (const V3& T : Hp.HeadTops) BackOfHeads = std::max(BackOfHeads, Crank.z + T.z + 0.09);
		const double Post = BackOfHeads + PostClear;
		if (Post > SeatFrontZ)
		{
			SeatFrontZ = Post;
			if (!Overlap) RearZ = std::max(RearZ, SeatFrontZ - 0.015);
		}
	}
	const double SeatNoseY = SeatY - (Num(T4, "rider_u", 0.66) - Num(T4, "front_u", 0.55)) * Wb * std::tan(DegToRad(Num(T4, "rail_deg", 3.0)));
	V3 Bt(0.0, SeatNoseY - EndH - (Overlap ? 0.03 : 0.0), RearZ);
	V3 At(0.0, TopMax - Tall, Nose.z);
	const double Back = Bt.z + 0.03;
	const double Room = Back - (At.z + 0.03);
	const double Length = std::min(Clamp(ShapeLen * Lerp(0.85, 1.0, Size) * ScX, 0.25, std::max(Room, 0.25)), Room);
	double Best = Inf;
	for (int I = 0; I < 15; ++I)
	{
		const double U = I / 14.0;
		const double Z = Lerp(Back - Length, Back, U);
		double K[3];
		KeyAt(*Keys, U, K);
		const double H = (K[1] + K[2]) * Size * ScZ;
		const double Fz = Clamp((Z - At.z) / std::max(Bt.z - At.z, 0.01), 0.0, 1.0);
		if (Fz < 0.98) Best = std::min(Best, (TopMax - H - Bt.y * Fz) / (1.0 - Fz));
	}
	if (Best < Inf) At.y = Best;
	const bool Vtwin = Str(Engine, "layout") == "vtwin";
	const double SlopeDeg = (Frig.Slope > 0.0 && Vtwin) ? Frig.Slope : Num(Geo, "tank_slope", 13.0);
	const double MaxRise = std::tan(DegToRad(SlopeDeg)) * std::max(Bt.z - At.z, 0.01);
	At.y = Clamp(At.y, Bt.y - MaxRise, Bt.y + MaxRise);
	bool Lifted = false;
	for (const V3& Need : OverHeads)
	{
		const double F = Clamp((Need.z - At.z) / std::max(Bt.z - At.z, 0.01), 0.0, 1.0);
		double Short = Need.y - Lerp(At.y, Bt.y, F);
		if (Short <= 0.0) continue;
		Lifted = true;
		if (F > 0.05)
		{
			const double Nb = std::min(Bt.y + Short / F, std::max(At.y, Bt.y));
			Short -= (Nb - Bt.y) * F;
			Bt.y = Nb;
		}
		if (Short > 0.0)
		{
			At.y += Short;
			Bt.y += Short;
		}
	}
	At.y = std::min(At.y, Head.y - 0.02);
	if (At.y - Bt.y > MaxRise)
	{
		double FloorA = -Inf;
		for (const V3& Need : OverHeads)
		{
			const double F = Clamp((Need.z - At.z) / std::max(Bt.z - At.z, 0.01), 0.0, 0.95);
			FloorA = std::max(FloorA, (Need.y - Bt.y * F) / (1.0 - F));
		}
		At.y = std::max(Bt.y + MaxRise, FloorA);
		if (At.y - Bt.y > MaxRise) Bt.y = At.y - MaxRise;
	}
	const double TankUp = Adj.TankUpMm / 1000.0;
	At.y += TankUp;
	Bt.y += TankUp;
	double Top = -Inf;
	for (int I = 0; I < 15; ++I)
	{
		const double U = I / 14.0;
		const double Z = Lerp(Back - Length, Back, U);
		double K[3];
		KeyAt(*Keys, U, K);
		const double Fz = Clamp((Z - At.z) / std::max(Bt.z - At.z, 0.01), 0.0, 1.0);
		Top = std::max(Top, Lerp(At.y, Bt.y, Fz) + (K[1] + K[2]) * Size * ScZ);
	}
	if (Lifted && Top - TopMax > TankHigh && !RigBodywork)
		Notes.push_back({"raised tank mounts", true});

	// 4: the seat line and the tail; 5: the rider and the pegs.
	const double Rail = DegToRad(Num(T4, "rail_deg", 3.0));
	const double Drop = Num(Geo, "seat_drop", 0.1);
	const V3 SeatFront(0.0, SeatNoseY - Drop, SeatFrontZ);
	V3 Hip(0.0, SeatY, AtU(Num(T4, "rider_u", 0.66)));
	Hip.z = std::max(Hip.z, SeatFrontZ + 0.14);
	const double TailZ = AtU(Num(T4, "tail_u", 0.98));
	V3 Tail(0.0, SeatY - 0.02 + std::max(0.0, (TailZ - Hip.z) * std::tan(Rail)), TailZ);
	Tail.y += Adj.HoopUpMm / 1000.0 + (TailZ - Hip.z) * std::tan(DegToRad(Adj.HoopDeg));
	const V3 SeatNose(0.0, SeatY - 0.02, SeatFrontZ);
	for (int I = 0; I < 9; ++I)
	{
		const double Z = Lerp(Zr - B.RearRadius * 0.9, std::min(TailZ, Zr), I / 8.0);
		const double Dz = Z - Zr;
		const double Need = B.RearTravel + std::sqrt(std::max(0.0, B.RearRadius * B.RearRadius - Dz * Dz)) + RearClear;
		const double F = (Z - SeatNose.z) / std::max(TailZ - SeatNose.z, 0.01);
		if (F > 0.05 && Lerp(SeatNose.y, Tail.y, F) < Need) Tail.y = SeatNose.y + (Need - SeatNose.y) / F;
	}
	const Json* T5 = L.Get("5_rider");
	const Json* PegMm = Sub(T5, "peg_mm");
	const Json* GripMm = Sub(T5, "grip_mm");
	V3 Peg(0.17, SeatY - Item(PegMm, 1, 440.0) / 1000.0, Hip.z - Item(PegMm, 0, 120.0) / 1000.0);
	const Json* Sets = Part(Get(Parts, "rearsets"));
	const Json* SetsPeg = Sub(Sets, "peg");
	if (SetsPeg && SetsPeg->IsArray())
	{
		const Json* Own = Sub(Geo, "peg");
		double Own0 = Item(Own, 0, 0.02), Own1 = Item(Own, 1, 0.02);
		const Json* StockPeg = Sub(Part(Get(ReferenceBuild(R.Style), "rearsets")), "peg");
		if (StockPeg && StockPeg->IsArray())
		{
			Own0 = Item(StockPeg, 0, Own0);
			Own1 = Item(StockPeg, 1, Own1);
		}
		Peg += V3(0.0, Item(SetsPeg, 0, 0.0) - Own0, Item(SetsPeg, 1, 0.0) - Own1);
	}
	Peg.y = std::max(Peg.y, Ground + ClearMin + 0.02);
	Peg.z = std::max(Peg.z, Crank.z - 0.17 * S - 0.2);
	Peg.z += Adj.PegsMm / 1000.0;
	const V3 Grip(0.31, SeatY + Item(GripMm, 1, 260.0) / 1000.0, Hip.z - Item(GripMm, 0, 550.0) / 1000.0);
	const V3 KneeP = Knee(Hip + V3(0.0, 0.1, 0.0), Peg);

	// The backbone: from the neck under the tank's floor, then down to the seat.
	const V3 Neck = Head + V3(0.0, -0.01, 0.02);
	const V3 SeatEnd = SeatFront + V3(0.0, -0.04 - Frig.Drop, 0.0);
	const V3 UnderA = At + V3(0.0, -DrawnR, 0.0);
	const V3 UnderB = Bt + V3(0.0, -DrawnR, 0.0);
	std::vector<V3> Path = {Neck, LerpV(UnderA, UnderB, 0.33), LerpV(UnderA, UnderB, 0.75), UnderB};
	if (UnderB.y - SeatEnd.y > 0.03) Path.push_back(LerpV(UnderB, SeatEnd, 0.5) + V3(0.0, 0.0, -0.02));
	Path.push_back(SeatEnd);
	const std::vector<V3> Spine = Smooth(Path, 3);
	std::vector<V3> Floor;
	for (int I = 0; I < 9; ++I) Floor.push_back(LerpV(At, Bt, I / 8.0));

	const std::string Cooling = Str(Part(Get(Parts, "cooling")), "style");
	V3 BoxMin = Crank + V3(-Hp.HeadHalfW, Hp.Sump.y, -0.17 * S), BoxMax = Crank + V3(Hp.HeadHalfW, Hp.TopY, 0.17 * S);
	auto Expand = [&](const V3& P) {
		BoxMin = V3(std::min(BoxMin.x, P.x), std::min(BoxMin.y, P.y), std::min(BoxMin.z, P.z));
		BoxMax = V3(std::max(BoxMax.x, P.x), std::max(BoxMax.y, P.y), std::max(BoxMax.z, P.z));
	};
	for (const V3& T : Hp.HeadTops)
	{
		Expand(Crank + T + V3(0.0, 0.0, -0.08));
		Expand(Crank + T + V3(0.0, 0.0, 0.08));
	}

	// Into our frame.
	R.FrontAxle = FromGodot(V3(0.0, 0.0, Zf));
	R.RearAxle = FromGodot(V3(0.0, 0.0, Zr));
	R.SteerAxis = FromGodot(Axis);
	R.Head = FromGodot(Head);
	R.HeadBottom = FromGodot(HeadBottom);
	R.Pivot = FromGodot(Pivot);
	R.Hard = HardToOurs(Hp);
	R.Crank = FromGodot(Crank);
	R.Output = FromGodot(Crank + V3(0.0, Hp.Output.y, Hp.Output.z));
	for (const V3& P : Spine) R.Spine.push_back(FromGodot(P));
	for (const V3& P : Floor) R.TankFloor.push_back(FromGodot(P));
	R.TankNose = FromGodot(At);
	R.TankRear = FromGodot(Bt);
	R.TankTopMax = TopMax;
	R.TunnelHalfW = DrawnR + 0.01;
	R.SpineR = DrawnR;
	R.KneeL = FromGodot(V3(-0.17, KneeP.y, KneeP.z));
	R.KneeR = FromGodot(V3(0.17, KneeP.y, KneeP.z));
	R.SeatFront = FromGodot(SeatFront);
	R.Hip = FromGodot(Hip);
	R.Tail = FromGodot(Tail);
	R.TailTop = Tail.y + Num(T4, "tail_rise_mm", 20.0) / 1000.0;
	R.SeatOverlap = Overlap;
	R.Peg = FromGodot(Peg);
	R.GripR = FromGodot(Grip);
	R.StayHead = FromGodot(Head + V3(0.0, 0.0, -0.05));
	R.ScreenBase = FromGodot(Head + V3(0.0, 0.04, -0.12));
	R.SparFrom = FromGodot(HeadBottom + V3(0.065, 0.05, 0.0));
	R.SparTo = FromGodot(Pivot + V3(0.12, 0.1, 0.0));
	R.BellyY = Ground + Clear;
	R.HasRadiator = Cooling == "radiator";
	R.RadiatorX = R.HasRadiator ? -(Crank.z - 0.17 * S - 0.06) : 0.0;
	const V3 Bm = FromGodot(BoxMin), Bx = FromGodot(BoxMax);
	R.EngineMin = V3(std::min(Bm.x, Bx.x), std::min(Bm.y, Bx.y), std::min(Bm.z, Bx.z));
	R.EngineMax = V3(std::max(Bm.x, Bx.x), std::max(Bm.y, Bx.y), std::max(Bm.z, Bx.z));
	R.FrontClear = Nearest;
	R.Fit = FitOf(R, Engine, FrontClearWant, Notes);
	return R;
}

FitReport BikeLayout::FitOf(const Layout& L, const Json* Engine, double FrontClearWant, const std::vector<std::pair<std::string, bool>>& Notes) const
{
	const std::string LayoutName = Str(Engine, "layout", "parallel");
	const double Cc = Num(Engine, "displacement", 700.0);
	const Family& Fam = FamilyOf(L.Family);
	FitReport F;
	if (NoFit(L.Family, LayoutName))
	{
		F.Tier = TierNo;
		F.Reason = WithA(LayoutWord(LayoutName)) + "'s cylinders can't pass through " + WithA(FamilyWord(L.Family)) + " frame";
		return F;
	}
	if (L.FrontClear < 0.0)
	{
		F.Tier = TierNo;
		F.Reason = "the front tyre would hit the engine on full bump";
		return F;
	}
	if (std::find(Fam.Layouts.begin(), Fam.Layouts.end(), LayoutName) == Fam.Layouts.end())
		F.Work.push_back("engine mounts and cradle made for " + WithA(LayoutWord(LayoutName)));
	if (Cc > Fam.Cc + 0.5)
	{
		char Buf[96];
		std::snprintf(Buf, sizeof(Buf), "cradle widened and backbone stretched for %d cc", static_cast<int>(std::lround(Cc)));
		F.Work.push_back(Buf);
	}
	if (L.FrontClear < FrontClearWant) F.Work.push_back("downtubes reshaped to clear the front tyre");
	for (const auto& N : Notes)
		if (N.second) F.Work.push_back(N.first);
	F.Tier = F.Work.empty() ? TierBolts : TierFab;
	F.Reason = Join(F.Work, ", ");
	return F;
}

FabJob BikeLayout::FabJobFor(const std::string& What)
{
	struct Row { const char* Start; double Hours; int Cost; };
	static const Row Rows[] = {
		{"engine mounts and cradle made for", 6.0, 380},
		{"cradle widened and backbone stretched", 8.0, 520},
		{"downtubes reshaped", 4.0, 240},
		{"raised tank mounts", 2.0, 110},
	};
	for (const Row& Rw : Rows)
		if (What.compare(0, std::string(Rw.Start).size(), Rw.Start) == 0) return {What, Rw.Hours, Rw.Cost};
	return {What, 3.0, 180};
}

PartTier BikeLayout::PartFit(const PartsMap& Parts, const std::string& Slot, const std::string& Id, const std::vector<std::string>& Done) const
{
	PartTier Ok{TierBolts, "", {}};
	auto No = [](const std::string& Why) { return PartTier{TierNo, Why, {}}; };
	if (SlotKind.find(Slot) == SlotKind.end()) return No("A bike has no " + Slot + ".");
	std::string Name = Slot;
	std::replace(Name.begin(), Name.end(), '_', ' ');
	auto MountsOf = [&](const std::string& S, const std::string& P) {
		std::vector<std::string> Out;
		if (const Json* M = Sub(Data.Get("mounts"), S))
			for (const Json& X : M->A) Out.push_back(X.S);
		if (const Json* Own = Sub(Part(P), "mounts_on"))
			for (const Json& X : Own->A)
				if (std::find(Out.begin(), Out.end(), X.S) == Out.end()) Out.push_back(X.S);
		return Out;
	};
	if (Id == "none" || Id.empty())
	{
		if (Get(Parts, Slot) == "none") return Ok;
		if (!MayBeEmpty(Slot)) return No("A bike can't do without its " + Name + ".");
		for (const std::string& S : SlotList)
		{
			const std::string On = Get(Parts, S);
			if (S == Slot || On == "none") continue;
			const std::vector<std::string> M = MountsOf(S, On);
			if (std::find(M.begin(), M.end(), Slot) != M.end())
			{
				std::string Other = S;
				std::replace(Other.begin(), Other.end(), '_', ' ');
				return No("The " + Other + " is mounted on it: take that off first.");
			}
		}
		return Ok;
	}
	if (!Part(Id) || KindOfPart(Id) != KindOfSlot(Slot)) return No("That isn't a " + Name + ".");
	// (Tyre on wheel: PartsDB.tyre_fit, checked by the shed's tyre changer flow; see TyreFits.)
	auto TyreFits = [&](const std::string& Tyre, const std::string& Wheel) {
		const Json* T = Part(Tyre);
		const Json* W = Part(Wheel);
		if (!T || !W) return true;
		double Lo, Hi;
		const Json* Range = W->Get("tyres");
		if (Range && Range->IsArray() && Range->A.size() == 2)
		{
			Lo = Item(Range, 0, 0.0);
			Hi = Item(Range, 1, 0.0);
		}
		else
		{
			const double Rim = W->Num("rim_width", 3.5);
			Lo = std::round(Rim * 24.0);
			Hi = std::round(Rim * 62.0);
		}
		const double Mm = T->Num("width", 120.0);
		if (Mm > Hi || Mm < Lo) return false;
		const Json* Rims = T->Get("rims");
		if (Rims && Rims->IsArray() && !Rims->A.empty())
		{
			const double Dia = W->Num("diameter", 17.0);
			for (const Json& D : Rims->A)
				if (std::fabs(D.N - Dia) < 1e-5) return true;
			return false;
		}
		return true;
	};
	if ((Slot == "front_tyre" || Slot == "rear_tyre"))
	{
		const std::string W = Get(Parts, Slot == "front_tyre" ? "front_wheel" : "rear_wheel");
		if (W != "none" && !TyreFits(Id, W)) return No("That tyre doesn't go on this wheel.");
	}
	if ((Slot == "front_wheel" || Slot == "rear_wheel"))
	{
		const std::string T = Get(Parts, Slot == "front_wheel" ? "front_tyre" : "rear_tyre");
		if (T != "none" && !TyreFits(T, Id)) return No("The tyre on the bike doesn't go on that wheel.");
	}
	for (const std::string& M : MountsOf(Slot, Id))
		if (MayBeEmpty(M) && Get(Parts, M) == "none")
		{
			std::string Mn = M;
			std::replace(Mn.begin(), Mn.end(), '_', ' ');
			return No("It needs a " + Mn + " on the bike first.");
		}
	static const char* RigSlots[] = {"frame", "engine", "tank", "fork", "front_wheel", "front_tyre"};
	bool IsRig = false;
	for (const char* S : RigSlots) IsRig = IsRig || Slot == S;
	if (!IsRig || !Part(Get(Parts, "frame")) || !Part(Get(Parts, "engine"))) return Ok;
	PartsMap After = Parts;
	After[Slot] = Id;
	const FitReport Now = Solve(Parts).Fit;
	const FitReport Then = Solve(After).Fit;
	if (Then.Tier == TierNo) return No(Capital(Then.Reason) + ".");
	PartTier Out = Ok;
	std::vector<std::string> Words;
	for (const std::string& What : Then.Work)
		if (std::find(Now.Work.begin(), Now.Work.end(), What) == Now.Work.end() && std::find(Done.begin(), Done.end(), What) == Done.end())
		{
			Out.Work.push_back(FabJobFor(What));
			Words.push_back(What);
		}
	if (Out.Work.empty()) return Ok;
	Out.Tier = TierFab;
	Out.Reason = "Needs fabrication: " + Join(Words, ", ") + ".";
	return Out;
}
} // namespace TorqueFit
