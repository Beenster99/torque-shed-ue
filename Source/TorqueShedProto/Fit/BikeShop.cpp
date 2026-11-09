#include "BikeShop.h"

#include "BikeRideSpec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace TorqueFit
{
namespace Shop
{
// (Names kept to this file: the Fit sources are built as one in the game, see BikeLayout.cpp.)
namespace BikeShopImpl
{
const char* const ShopTierNo = "won't fit";

std::string ShopGet(const PartsMap& Parts, const std::string& Slot)
{
	const auto It = Parts.find(Slot);
	return It == Parts.end() ? std::string() : It->second;
}

bool ShopHas(const std::vector<std::string>& L, const std::string& S)
{
	return std::find(L.begin(), L.end(), S) != L.end();
}

std::string ShopLower(std::string S)
{
	for (char& C : S) C = (char)std::tolower((unsigned char)C);
	return S;
}

// The wheel slot a tyre slot goes on ("" for any other slot), and the tyre slot a wheel takes.
std::string ShopWheelOf(const std::string& Slot)
{
	if (Slot == "front_tyre") return "front_wheel";
	if (Slot == "rear_tyre") return "rear_wheel";
	return std::string();
}

// "sports_road_120" -> "sports_road" (get_slice("_", 0) + "_" + get_slice("_", 1)).
std::string ShopFamily(const std::string& Id)
{
	const size_t A = Id.find('_');
	if (A == std::string::npos) return Id + "_";
	const size_t B = Id.find('_', A + 1);
	return B == std::string::npos ? Id : Id.substr(0, B);
}

bool ShopStartsWith(const std::string& S, const std::string& Prefix)
{
	return S.size() >= Prefix.size() && S.compare(0, Prefix.size(), Prefix) == 0;
}

void ShopRgb(const Json* A, double Out[3])
{
	if (!A || !A->IsArray() || A->A.size() < 3) return;
	for (int I = 0; I < 3; ++I) Out[I] = std::min(std::max(A->A[(size_t)I].N, 0.0), 1.0);
}

Json ShopRgbJson(const double C[3])
{
	Json A = Json::MakeArray();
	for (int I = 0; I < 3; ++I) A.Push(Json::MakeNumber(std::round(C[I] * 10000.0) / 10000.0));
	return A;
}

// String.naturalnocasecmp_to: case aside, runs of digits compared as numbers.
int ShopNatural(const std::string& A, const std::string& B)
{
	size_t I = 0, J = 0;
	while (I < A.size() && J < B.size())
	{
		const unsigned char Ca = (unsigned char)A[I], Cb = (unsigned char)B[J];
		if (std::isdigit(Ca) && std::isdigit(Cb))
		{
			size_t Ei = I, Ej = J;
			while (Ei < A.size() && A[Ei] == '0') ++Ei;
			while (Ej < B.size() && B[Ej] == '0') ++Ej;
			size_t Ni = Ei, Nj = Ej;
			while (Ni < A.size() && std::isdigit((unsigned char)A[Ni])) ++Ni;
			while (Nj < B.size() && std::isdigit((unsigned char)B[Nj])) ++Nj;
			if (Ni - Ei != Nj - Ej) return Ni - Ei < Nj - Ej ? -1 : 1;
			const int Cmp = A.compare(Ei, Ni - Ei, B, Ej, Nj - Ej);
			if (Cmp != 0) return Cmp < 0 ? -1 : 1;
			I = Ni;
			J = Nj;
			continue;
		}
		const int La = std::tolower(Ca), Lb = std::tolower(Cb);
		if (La != Lb) return La < Lb ? -1 : 1;
		++I;
		++J;
	}
	if (I < A.size()) return 1;
	if (J < B.size()) return -1;
	return 0;
}

double ShopClamp(double V, double Lo, double Hi) { return std::min(std::max(V, Lo), Hi); }

const std::vector<std::string>& ShopFinishes()
{
	static const std::vector<std::string> F = {"gloss", "matte", "metallic", "chrome", "patina"};
	return F;
}
} // namespace BikeShopImpl
namespace BSI = BikeShopImpl;

const std::vector<std::string>& ConfigSlots()
{
	static const std::vector<std::string> S = {"frame", "engine", "tank", "fairing", "saddle", "exhaust", "front_wheel", "front_tyre",
		"rear_wheel", "rear_tyre", "fork", "headlight", "handlebars", "dashboard", "front_mudguard", "tail", "panniers", "top_box", "satnav",
		"toolbox"};
	return S;
}

bool NoColour(const std::string& Slot) { return Slot == "front_tyre" || Slot == "rear_tyre"; }

const std::vector<std::string>& ColourGroups()
{
	static const std::vector<std::string> G = {"wheels", "mudguards"};
	return G;
}

bool Painted(const std::string& Slot) { return Slot == "tank" || Slot == "fairing"; }

std::vector<std::string> KindIds(const BikeLayout& Lay, const std::string& Kind)
{
	if (!Lay.Catalogue()) return {};
	std::vector<std::string> Out = Lay.Catalogue()->Ids(Kind);
	if (Kind == "wheel")
		std::stable_sort(Out.begin(), Out.end(), [&Lay](const std::string& A, const std::string& B) {
			const Json* Pa = Lay.Part(A);
			const Json* Pb = Lay.Part(B);
			const double Da = Pa ? Pa->Num("diameter", 17.0) : 17.0, Db = Pb ? Pb->Num("diameter", 17.0) : 17.0;
			if (std::fabs(Da - Db) > 1e-5) return Da < Db;
			const double Wa = Pa ? Pa->Num("rim_width", 0.0) : 0.0, Wb = Pb ? Pb->Num("rim_width", 0.0) : 0.0;
			if (std::fabs(Wa - Wb) > 1e-5) return Wa < Wb;
			return BSI::ShopNatural(Pa ? Pa->Str("name", A) : A, Pb ? Pb->Str("name", B) : B) < 0;
		});
	return Out;
}

int TyreFit(const BikeLayout& Lay, const std::string& Tyre, const std::string& Wheel)
{
	const Json* T = Lay.Part(Tyre);
	const Json* W = Lay.Part(Wheel);
	if (!T || !W || T->Size() == 0 || W->Size() == 0) return 0;
	long Lo = 0, Hi = 0;
	const Json* R = W->Get("tyres");
	if (R && R->IsArray() && R->A.size() == 2)
	{
		Lo = (long)R->A[0].N;
		Hi = (long)R->A[1].N;
	}
	else
	{
		const double Rim = W->Num("rim_width", 3.5);
		Lo = std::lround(Rim * 24.0);
		Hi = std::lround(Rim * 62.0);
	}
	const double Mm = T->Num("width", 120.0);
	if (Mm > (double)Hi) return 1;
	if (Mm < (double)Lo) return -1;
	const Json* Rims = T->Get("rims");
	if (Rims && Rims->IsArray() && !Rims->A.empty())
	{
		const double Dia = W->Num("diameter", 17.0);
		bool Any = false;
		for (const Json& D : Rims->A) Any = Any || std::fabs(D.N - Dia) < 1e-5;
		if (!Any) return 2;
	}
	return 0;
}

std::vector<std::string> TyresFor(const BikeLayout& Lay, const std::string& Wheel)
{
	std::vector<std::string> Out;
	for (const std::string& T : KindIds(Lay, "tyre"))
		if (TyreFit(Lay, T, Wheel) == 0) Out.push_back(T);
	return Out;
}

std::vector<std::string> Options(const BikeLayout& Lay, const std::string& Slot, const std::string& Style, const PartsMap& Parts)
{
	std::vector<std::string> Out;
	const std::string Kind = Lay.KindOfSlot(Slot);
	if (Kind.empty()) return Out;
	std::vector<std::string> All = KindIds(Lay, Kind);
	if (All.empty()) return Out;
	const std::string Wheel = BSI::ShopWheelOf(Slot);
	if (!Wheel.empty()) All = TyresFor(Lay, BSI::ShopGet(Parts, Wheel));
	std::vector<std::string> Suits;
	for (const std::string& Id : All)
		if (BSI::ShopHas(Lay.StylesOf(Id), Style)) Suits.push_back(Id);
	Out = Suits.size() >= 2 ? Suits : All;
	if (Lay.MayBeEmpty(Slot) && !BSI::ShopHas(Out, "none")) Out.push_back("none");
	const std::string Now = BSI::ShopGet(Parts, Slot);
	if (!Now.empty() && !BSI::ShopHas(Out, Now)) Out.insert(Out.begin(), Now);
	return Out;
}

PartsMap FitTyres(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style)
{
	PartsMap Out = Parts;
	for (const char* Ws : {"front_wheel", "rear_wheel"})
	{
		const std::string Ts = std::string(Ws) == "front_wheel" ? "front_tyre" : "rear_tyre";
		const std::string Now = BSI::ShopGet(Out, Ts);
		const std::string Wheel = BSI::ShopGet(Out, Ws);
		if (TyreFit(Lay, Now, Wheel) == 0) continue;
		const std::vector<std::string> Fits = TyresFor(Lay, Wheel);
		if (Fits.empty()) continue;
		const std::string Family = BSI::ShopFamily(Now);
		std::vector<std::string> Pools[4];
		for (const std::string& T : Fits)
		{
			if (BSI::ShopStartsWith(T, Family)) Pools[0].push_back(T);
			if (BSI::ShopHas(Lay.StylesOf(T), Style)) Pools[1].push_back(T);
			const Json* P = Lay.Part(T);
			if (!P || P->Str("style") != "slick") Pools[2].push_back(T);
			Pools[3].push_back(T);
		}
		std::string Pick;
		for (const std::vector<std::string>& Pool : Pools)
			if (!Pool.empty())
			{
				Pick = Pool[0];
				break;
			}
		Out[Ts] = Pick;
	}
	return Out;
}

PartsMap StockBuild(const BikeLayout& Lay, const std::string& Style)
{
	return FitTyres(Lay, RideParts(Lay, Lay.ReferenceBuild(Style)), Style);
}

PartsMap SetPart(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style, const std::string& Slot, const std::string& Id)
{
	PartsMap In = Parts;
	In[Slot] = Id;
	return FitTyres(Lay, RideParts(Lay, In), Style);
}

bool IsRearGuard(const BikeLayout& Lay, const std::string& Id)
{
	const Json* P = Lay.Part(Id);
	if (!P || Lay.KindOfPart(Id) != "tail") return false;
	const std::string N = BSI::ShopLower(P->Str("name", Id));
	return N.find("guard") != std::string::npos || N.find("fender") != std::string::npos;
}

std::vector<std::string> ColourSlots(const BikeLayout& Lay, const std::string& Key, const PartsMap& Parts)
{
	std::vector<std::string> Out;
	if (Key == "wheels") return {"front_wheel", "rear_wheel"};
	if (Key == "mudguards")
	{
		const std::string Front = BSI::ShopGet(Parts, "front_mudguard");
		if (!Front.empty() && Front != "none") Out.push_back("front_mudguard");
		const std::string Tail = BSI::ShopGet(Parts, "tail");
		if (IsRearGuard(Lay, Tail.empty() ? std::string("none") : Tail)) Out.push_back("tail");
		return Out;
	}
	const std::string On = BSI::ShopGet(Parts, Key);
	if (!Lay.KindOfSlot(Key).empty() && !NoColour(Key) && !On.empty() && On != "none") Out.push_back(Key);
	return Out;
}

std::vector<FabJob> FabWork(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style)
{
	std::vector<FabJob> Out;
	const std::vector<std::string> Stock = Lay.Solve(RideParts(Lay, Lay.ReferenceBuild(Style))).Fit.Work;
	for (const std::string& What : Lay.Solve(Parts).Fit.Work)
		if (!BSI::ShopHas(Stock, What)) Out.push_back(BikeLayout::FabJobFor(What));
	return Out;
}

std::string FabText(const BikeLayout& Lay, const PartsMap& Parts, const std::string& Style)
{
	const std::vector<FabJob> Work = FabWork(Lay, Parts, Style);
	if (Work.empty()) return std::string();
	std::string Out = "Needs fabrication, done by the works: ";
	for (size_t I = 0; I < Work.size(); ++I) Out += (I ? ", " : "") + Work[I].What;
	return Out + ".";
}

std::string StyleNoun(const BikeLayout& Lay, const std::string& Style)
{
	const std::string Name = Lay.StyleName(Style);
	// "MX bike" keeps its capitals (its first two letters are capitals); the rest go lower case.
	std::string Two = Name.substr(0, 2), Upper = Two;
	for (char& C : Upper) C = (char)std::toupper((unsigned char)C);
	return Two == Upper ? Name : BSI::ShopLower(Name);
}

std::string StylePhrase(const BikeLayout& Lay, const std::string& Style)
{
	const std::string Noun = StyleNoun(Lay, Style);
	const char First = Noun.empty() ? 'x' : (char)std::tolower((unsigned char)Noun[0]);
	const bool bAn = First == 'a' || First == 'e' || First == 'i' || First == 'o' || First == 'u' || BSI::ShopStartsWith(Noun, "MX");
	return (bAn ? "an " : "a ") + Noun;
}

std::string BriefText(const BikeLayout& Lay, const PartsMap& Parts)
{
	const Layout L = Lay.Solve(Parts);
	std::string Out = "Laid out to " + StylePhrase(Lay, L.Style) + "'s lines";
	if (!L.MixedTail.empty()) Out += ", with " + StylePhrase(Lay, L.MixedTail) + "'s tail";
	return Out + ".";
}

std::string ModelText(const BikeLayout& Lay, const PartsMap& Parts)
{
	const Json* E = Lay.Part(BSI::ShopGet(Parts, "engine"));
	if (!E || E->Size() == 0) return "Bike";
	const int Cyl = (int)E->Num("cylinders", 1.0);
	std::string Word;
	switch (Cyl)
	{
	case 1: Word = "single"; break;
	case 2: Word = "twin"; break;
	case 3: Word = "triple"; break;
	case 4: Word = "four"; break;
	case 6: Word = "six"; break;
	default: Word = std::to_string(Cyl) + "-cylinder"; break;
	}
	return std::to_string((int)E->Num("displacement", 0.0)) + "cc " + Word;
}

const std::vector<std::pair<std::string, std::string>>& Logos()
{
	static const std::vector<std::pair<std::string, std::string>> L = {{"wings", "Wings"}, {"star", "Star"}, {"shield", "Shield"},
		{"bolt", "Lightning bolt"}, {"roundel", "Roundel"}, {"diamond", "Diamond"}, {"piston", "Piston"}, {"flame", "Flame"},
		{"crown", "Crown"}, {"spanners", "Crossed spanners"}};
	return L;
}

const std::vector<std::pair<std::string, std::string>>& Fonts()
{
	static const std::vector<std::pair<std::string, std::string>> F = {{"plain", "Plain"}, {"bebas", "Bebas Neue"},
		{"racing", "Racing Sans"}, {"russo", "Russo One"}, {"audiowide", "Audiowide"}, {"blackops", "Black Ops"},
		{"graduate", "Graduate"}, {"righteous", "Righteous"}, {"playfair", "Playfair"}, {"lobster", "Lobster"},
		{"pacifico", "Pacifico"}, {"kaushan", "Kaushan Script"}};
	return F;
}

Badge CleanBadge(const Badge& In)
{
	Badge Out;
	bool bLogo = false, bFont = false;
	for (const auto& L : Logos()) bLogo = bLogo || L.first == In.Logo;
	for (const auto& F : Fonts()) bFont = bFont || (F.first == In.Font && F.first != "plain");
	if (bLogo)
	{
		Out.Logo = In.Logo;
		for (int I = 0; I < 3; ++I) Out.LogoColour[I] = BSI::ShopClamp(In.LogoColour[I], 0.0, 1.0);
	}
	// The brand on one line, trimmed, at most BrandMax letters.
	std::string B = In.Brand;
	std::replace(B.begin(), B.end(), '\n', ' ');
	auto Trim = [](std::string& S) {
		const size_t A = S.find_first_not_of(" \t\r");
		if (A == std::string::npos)
		{
			S.clear();
			return;
		}
		S = S.substr(A, S.find_last_not_of(" \t\r") - A + 1);
	};
	Trim(B);
	if (B.size() > (size_t)BrandMax) B.resize((size_t)BrandMax);
	Trim(B);
	if (!B.empty())
	{
		Out.Brand = B;
		for (int I = 0; I < 3; ++I) Out.BrandColour[I] = BSI::ShopClamp(In.BrandColour[I], 0.0, 1.0);
		if (bFont) Out.Font = In.Font;
	}
	if (!Out.Logo.empty())
	{
		Out.LogoSize = BSI::ShopClamp(In.LogoSize, BadgeSizeMin, BadgeSizeMax);
		Out.LogoX = BSI::ShopClamp(In.LogoX, -BadgeMoveMax, BadgeMoveMax);
		Out.LogoY = BSI::ShopClamp(In.LogoY, -BadgeMoveMax, BadgeMoveMax);
	}
	if (!Out.Brand.empty())
	{
		Out.BrandSize = BSI::ShopClamp(In.BrandSize, BadgeSizeMin, BadgeSizeMax);
		Out.BrandX = BSI::ShopClamp(In.BrandX, -BadgeMoveMax, BadgeMoveMax);
		Out.BrandY = BSI::ShopClamp(In.BrandY, -BadgeMoveMax, BadgeMoveMax);
	}
	return Out;
}

std::string OrderText(const Order& O)
{
	Json J = Json::MakeObject();
	J.Set("style", Json::MakeString(O.Style));
	J.Set("paint", BSI::ShopRgbJson(O.Paint));
	// Only the configurator's slots (the rest follow from them as the host makes it whole).
	Json P = Json::MakeObject();
	for (const std::string& S : ConfigSlots())
	{
		const std::string Id = BSI::ShopGet(O.Parts, S);
		if (!Id.empty()) P.Set(S, Json::MakeString(Id));
	}
	J.Set("parts", P);
	Json C = Json::MakeObject();
	for (const auto& KV : O.Colours)
		if (KV.second.size() >= 3) C.Set(KV.first, BSI::ShopRgbJson(KV.second.data()));
	J.Set("colours", C);
	Json F = Json::MakeObject();
	for (const auto& KV : O.Finishes) F.Set(KV.first, Json::MakeString(KV.second));
	J.Set("finishes", F);
	const Badge B = CleanBadge(O.TankBadge);
	if (!B.Empty())
	{
		Json Bj = Json::MakeObject();
		if (!B.Logo.empty())
		{
			Bj.Set("logo", Json::MakeString(B.Logo));
			Bj.Set("logo_color", BSI::ShopRgbJson(B.LogoColour));
			Bj.Set("logo_size", Json::MakeNumber(B.LogoSize));
			Bj.Set("logo_x", Json::MakeNumber(B.LogoX));
			Bj.Set("logo_y", Json::MakeNumber(B.LogoY));
		}
		if (!B.Brand.empty())
		{
			Bj.Set("brand", Json::MakeString(B.Brand));
			Bj.Set("brand_color", BSI::ShopRgbJson(B.BrandColour));
			if (!B.Font.empty()) Bj.Set("brand_font", Json::MakeString(B.Font));
			Bj.Set("brand_size", Json::MakeNumber(B.BrandSize));
			Bj.Set("brand_x", Json::MakeNumber(B.BrandX));
			Bj.Set("brand_y", Json::MakeNumber(B.BrandY));
		}
		J.Set("badge", Bj);
	}
	return J.Dump(0);
}

bool ParseOrder(const BikeLayout& Lay, const std::string& Text, Order& Out)
{
	Json J;
	if (!Json::Parse(Text, J) || !J.IsObject()) return false;
	Out = Order();
	Out.Style = J.Str("style");
	if (!BSI::ShopHas(Lay.Styles(), Out.Style)) return false;
	BSI::ShopRgb(J.Get("paint"), Out.Paint);
	// The style's stock bike with the configurator's slots over it, made whole: a part that isn't
	// the slot's kind is dropped for the stock one (sanitize_build), and the tyres put right.
	PartsMap Parts = StockBuild(Lay, Out.Style);
	if (const Json* P = J.Get("parts"))
		for (const auto& KV : P->O)
			if (BSI::ShopHas(ConfigSlots(), KV.first) && KV.second.IsString()) Parts[KV.first] = KV.second.S;
	Out.Parts = FitTyres(Lay, RideParts(Lay, Parts), Out.Style);
	if (const Json* C = J.Get("colours"))
		for (const auto& KV : C->O)
		{
			const bool bKey = !Lay.KindOfSlot(KV.first).empty() || BSI::ShopHas(ColourGroups(), KV.first);
			if (!bKey || !KV.second.IsArray() || KV.second.A.size() < 3) continue;
			double Rgb[3] = {0.5, 0.5, 0.5};
			BSI::ShopRgb(&KV.second, Rgb);
			Out.Colours[KV.first] = {Rgb[0], Rgb[1], Rgb[2]};
		}
	if (const Json* F = J.Get("finishes"))
		for (const auto& KV : F->O)
			if (KV.second.IsString() && BSI::ShopHas(BSI::ShopFinishes(), KV.second.S)) Out.Finishes[KV.first] = KV.second.S;
	if (const Json* B = J.Get("badge"))
	{
		Badge In;
		In.Logo = B->Str("logo");
		In.Brand = B->Str("brand");
		In.Font = B->Str("brand_font");
		BSI::ShopRgb(B->Get("logo_color"), In.LogoColour);
		BSI::ShopRgb(B->Get("brand_color"), In.BrandColour);
		In.LogoSize = B->Num("logo_size", 1.0);
		In.LogoX = B->Num("logo_x", 0.0);
		In.LogoY = B->Num("logo_y", 0.0);
		In.BrandSize = B->Num("brand_size", 1.0);
		In.BrandX = B->Num("brand_x", 0.0);
		In.BrandY = B->Num("brand_y", 0.0);
		Out.TankBadge = CleanBadge(In);
	}
	return true;
}

std::map<std::string, SlotLook> OrderLooks(const BikeLayout& Lay, const Order& O)
{
	std::map<std::string, SlotLook> Out;
	auto Rgb = [](SlotLook& L, const double C[3]) {
		for (int I = 0; I < 3; ++I) L.Colour[I] = C[I];
	};
	// The tank's colour is the bike's paint (BikeShop.src_of): on the tank, and the fairing unless
	// it has its own (BikeCopy.build_src).
	double Paint[3] = {O.Paint[0], O.Paint[1], O.Paint[2]};
	const auto Tank = O.Colours.find("tank");
	if (Tank != O.Colours.end() && Tank->second.size() >= 3)
		for (int I = 0; I < 3; ++I) Paint[I] = Tank->second[(size_t)I];
	for (const std::string& S : {std::string("tank"), std::string("fairing")})
	{
		const std::string On = BSI::ShopGet(O.Parts, S);
		if (!On.empty() && On != "none") Rgb(Out[S], Paint);
	}
	// The groups first, then the slots picked one by one over them (the configurator clears a
	// group's slots when the group is coloured, so a slot set after it is the later word).
	for (int Pass = 0; Pass < 2; ++Pass)
		for (const auto& KV : O.Colours)
		{
			const bool bGroup = BSI::ShopHas(ColourGroups(), KV.first);
			if (bGroup != (Pass == 0) || KV.second.size() < 3) continue;
			for (const std::string& S : ColourSlots(Lay, KV.first, O.Parts)) Rgb(Out[S], KV.second.data());
		}
	// Each finish picked (a slot or a group) on its parts, in their colour now; a part not
	// coloured yet takes the factory's colour under it (BikeShop.factory_colour).
	for (int Pass = 0; Pass < 2; ++Pass)
		for (const auto& KV : O.Finishes)
		{
			const bool bGroup = BSI::ShopHas(ColourGroups(), KV.first);
			if (bGroup != (Pass == 0)) continue;
			for (const std::string& S : ColourSlots(Lay, KV.first, O.Parts))
			{
				auto It = Out.find(S);
				if (It == Out.end())
				{
					SlotLook L;
					if (Painted(S)) Rgb(L, Paint);
					else if (const Json* Part = Lay.Part(BSI::ShopGet(O.Parts, S))) BSI::ShopRgb(Part->Get("color"), L.Colour);
					It = Out.emplace(S, L).first;
				}
				It->second.Finish = KV.second;
			}
		}
	return Out;
}
} // namespace Shop
} // namespace TorqueFit
