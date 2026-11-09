#include "Shed/TorqueBikeConfigurator.h"

#include "Shed/TorqueBikeBadge.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShopPreview.h"
#include "Fit/BikeRideSpec.h"
#include "Fit/BikeShop.h"
#include "Coop/TorqueUi.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "GameFramework/HUD.h"

namespace TorqueBikeConfiguratorImpl
{
// The game's UI kit (Coop/TorqueUi.h, the menus' look), as the parts computer's other aisles have it.
const FLinearColor Column = TorqueUi::PanelLight;
const FLinearColor Button(1.0f, 1.0f, 1.0f, 0.07f);
const FLinearColor ButtonHover(1.0f, 1.0f, 1.0f, 0.15f);
const FLinearColor Picked(TorqueUi::Accent.R, TorqueUi::Accent.G, TorqueUi::Accent.B, 0.88f);
const FLinearColor PickedHover = TorqueUi::Accent;
const FLinearColor PopupBack(0.06f, 0.065f, 0.07f, 0.98f);
const FLinearColor Edge = TorqueUi::Line;
const FLinearColor Field(0.0f, 0.0f, 0.0f, 0.45f);
const FLinearColor Accent = TorqueUi::Accent;
const FLinearColor Text = TorqueUi::Ink;
const FLinearColor Dim = TorqueUi::Dim;
const FLinearColor Off = TorqueUi::Faint;
const FLinearColor Ink(0.03f, 0.03f, 0.035f, 1.0f);   // text on the amber
const FLinearColor Good(0.45f, 0.88f, 0.52f, 1.0f);
const FLinearColor Bad = TorqueUi::Warn;

// BikeShop.SLOT_LABELS: where the parts list's name for a slot isn't clear enough.
FString CfgSlotLabel(const FString& Slot)
{
	if (Slot == TEXT("tail")) return TEXT("Rear guard, tail");
	if (Slot == TEXT("badge_logo")) return TEXT("Logo");
	if (Slot == TEXT("badge_brand")) return TEXT("Brand name");
	return TorqueShedParts::SlotName(FName(*Slot));
}

FString CfgUe(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

std::string CfgStd(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

// Text in pixel sizes, through the kit (Slate's font at the size it's shown).
struct FCfgPen
{
	UCanvas* C = nullptr;
	float Width(const FString& S, float Px) const { return (float)TorqueUi::Measure(S, Px).X; }
	void Draw(float X, float Y, const FString& S, float Px, const FLinearColor& Colour) const
	{
		if (S.IsEmpty()) return;
		TorqueUi::Text(C, S, X, Y, Px, Colour, false, TorqueUi::EAlign::Left, Colour != Ink);
	}
	// Cut to fit, with "..." if it was cut.
	FString Fit(const FString& S, float Px, float MaxW) const
	{
		if (Width(S, Px) <= MaxW) return S;
		FString Out = S;
		while (Out.Len() > 1 && Width(Out + TEXT("..."), Px) > MaxW) Out.LeftChopInline(1);
		return Out.TrimEnd() + TEXT("...");
	}
	// Word-wrapped lines (a new line starts a new one).
	TArray<FString> Wrap(const FString& S, float Px, float MaxW) const
	{
		TArray<FString> Out, Paras;
		S.ParseIntoArray(Paras, TEXT("\n"), false);
		for (const FString& P : Paras)
		{
			const TArray<FString> Lines = TorqueUi::WrapLines(P, Px, MaxW);
			if (Lines.Num() == 0) Out.Add(FString());
			else Out.Append(Lines);
		}
		return Out;
	}
};

// The colours picked lately (ColourChoice.recent: shared by every swatch, at most ten).
TArray<FLinearColor>& Recent()
{
	static TArray<FLinearColor> L;
	return L;
}

// The colour picker's square: how many cells across (saturation) and down (value), and the hue strip.
constexpr int32 SvCols = 16, SvRows = 12, HueCells = 30;
} // namespace TorqueBikeConfiguratorImpl

namespace TBCI = TorqueBikeConfiguratorImpl;

// The bike being configured, in fitting's own terms (kept out of the header: the walker's).
struct FTorqueBikeConfigurator::FBikeState
{
	std::string Style;
	TorqueFit::PartsMap Parts;
	TorqueFit::Shop::Badge Badge;   // the tank's (it stays when the style changes)
};

FTorqueBikeConfigurator::FBikeState& FTorqueBikeConfigurator::B() const
{
	if (!St.IsValid()) St = MakeShared<FBikeState>();
	return *St;
}

void FTorqueBikeConfigurator::Open()
{
	if (B().Style.empty() || B().Parts.empty()) NewStyle("cafe");
	ClosePopup();
	bDirty = true;
}

FLinearColor FTorqueBikeConfigurator::StylePaint(const std::string& StyleId) const
{
	// A colour of its own for each style (Shed.PAINTS[hash(style) % size]).
	const TArray<FLinearColor>& P = UTorqueBuildSubsystem::Paints();
	return P.Num() > 0 ? P[GetTypeHash(TBCI::CfgUe(StyleId)) % (uint32)P.Num()] : FLinearColor::Red;
}

void FTorqueBikeConfigurator::NewStyle(const std::string& NewStyleId)
{
	// BikeShopTab._new_style: its stock build, in a colour of its own.
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return;
	B().Style = NewStyleId;
	bool bKnown = false;
	for (const std::string& S : Lay->Styles()) bKnown = bKnown || S == B().Style;
	if (!bKnown && !Lay->Styles().empty()) B().Style = Lay->Styles().front();
	B().Parts = TorqueFit::Shop::StockBuild(*Lay, B().Style);
	Paint = StylePaint(B().Style);
	Colours.Reset();
	Finishes.Reset();
	bDirty = true;
}

void FTorqueBikeConfigurator::SetPart(const FString& Slot, const std::string& Id)
{
	// BikeShopTab.set_part: the part on, the rest sorted out to fit (a new wheel keeps its tyre when
	// it goes on, else takes one that does).
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return;
	B().Parts = TorqueFit::Shop::SetPart(*Lay, B().Parts, B().Style, TBCI::CfgStd(Slot), Id);
	bDirty = true;
}

void FTorqueBikeConfigurator::Relist()
{
	bDirty = false;
	SlotRows.Reset();
	About.Reset();
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return;
	for (const std::string& Slot : TorqueFit::Shop::ConfigSlots())
	{
		FSlotRow Row;
		Row.Slot = TBCI::CfgUe(Slot);
		const auto Now = B().Parts.find(Slot);
		const std::string On = Now == B().Parts.end() ? std::string("none") : Now->second;
		for (const std::string& Id : TorqueFit::Shop::Options(*Lay, Slot, B().Style, B().Parts))
		{
			// How it goes on this bike (BikeRig.part_fit): the ones that won't fit greyed out with
			// why, the ones that need fabrication marked.
			const TorqueFit::PartTier Tier = Lay->PartFit(B().Parts, Slot, Id);
			FChoice C;
			C.Id = TBCI::CfgUe(Id);
			C.Label = Id == "none" ? FString(TEXT("None")) : TorqueShedParts::PartName(FName(*TBCI::CfgUe(Id)));
			if (Tier.Tier == "fabricate") C.Label += TEXT("  (needs fabrication)");
			else if (Tier.Tier == "won't fit") C.Label += TEXT("  (won't fit)");
			C.bOff = Tier.Tier == "won't fit" && Id != On;
			C.Tip = TBCI::CfgUe(Tier.Reason);
			if (Id == On) Row.Picked = Row.Choices.Num();
			Row.Choices.Add(C);
		}
		SlotRows.Add(Row);
	}
	// BikeShopTab._refresh, the new bike's lines (the shed has no money: no price).
	About.Add(FString::Printf(TEXT("A new %s, %s"), *TBCI::CfgUe(Lay->StyleName(B().Style)).ToLower(), *TBCI::CfgUe(TorqueFit::Shop::ModelText(*Lay, B().Parts))));
	About.Add(TEXT("Built new in your colours and ready to ride. ") + TBCI::CfgUe(TorqueFit::Shop::BriefText(*Lay, B().Parts)));
	const std::string Fab = TorqueFit::Shop::FabText(*Lay, B().Parts, B().Style);
	if (!Fab.empty()) About.Add(TBCI::CfgUe(Fab));
	About.Add(TEXT("It's built into the first free bay, ready to ride out."));
}

FLinearColor FTorqueBikeConfigurator::FactoryColour(const FString& Slot) const
{
	// BikeShop.factory_colour: the paint on the tank and fairing, the catalogue's colour on the rest.
	if (TorqueFit::Shop::Painted(TBCI::CfgStd(Slot))) return Paint;
	const auto It = B().Parts.find(TBCI::CfgStd(Slot));
	return It == B().Parts.end() || It->second == "none" ? FLinearColor(0.5f, 0.5f, 0.5f) : TorqueShedParts::DefaultColour(FName(*TBCI::CfgUe(It->second)));
}

FLinearColor FTorqueBikeConfigurator::ColourOf(const FString& Slot) const
{
	// The badge's logo and brand name in their own colours.
	if (Slot == TEXT("badge_logo") || Slot == TEXT("badge_brand"))
	{
		const double* Rgb = Slot == TEXT("badge_logo") ? B().Badge.LogoColour : B().Badge.BrandColour;
		return FLinearColor((float)Rgb[0], (float)Rgb[1], (float)Rgb[2]);
	}
	// BikeShopTab.colour_of: picked, or as it comes.
	if (const FLinearColor* C = Colours.Find(Slot)) return *C;
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (Lay)
		for (const std::string& G : TorqueFit::Shop::ColourGroups())
		{
			const FLinearColor* C = Colours.Find(TBCI::CfgUe(G));
			if (!C) continue;
			for (const std::string& S : TorqueFit::Shop::ColourSlots(*Lay, G, B().Parts))
				if (TBCI::CfgUe(S) == Slot) return *C;
		}
	return FactoryColour(Slot);
}

bool FTorqueBikeConfigurator::Changed(const FString& Slot) const
{
	return Finishes.Contains(Slot) || !ColourOf(Slot).Equals(FactoryColour(Slot), 0.001f);
}

bool FTorqueBikeConfigurator::TakesColour(const FString& Slot) const
{
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	return Lay && !TorqueFit::Shop::ColourSlots(*Lay, TBCI::CfgStd(Slot), B().Parts).empty();
}

void FTorqueBikeConfigurator::SetColour(const FString& Key, const FLinearColor& C)
{
	// BikeShopTab.set_colour: the tank's colour is the bike's paint (the fairing's too unless it has
	// its own); a group's colour clears its slots' own. The badge's are its own.
	if (Key == TEXT("badge_logo") || Key == TEXT("badge_brand"))
	{
		double* Rgb = Key == TEXT("badge_logo") ? B().Badge.LogoColour : B().Badge.BrandColour;
		Rgb[0] = C.R;
		Rgb[1] = C.G;
		Rgb[2] = C.B;
		return;
	}
	if (Key == TEXT("tank")) Paint = C;
	Colours.Add(Key, C);
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	bool bGroup = false;
	for (const std::string& G : TorqueFit::Shop::ColourGroups()) bGroup = bGroup || TBCI::CfgUe(G) == Key;
	if (bGroup && Lay)
		for (const std::string& S : TorqueFit::Shop::ColourSlots(*Lay, TBCI::CfgStd(Key), B().Parts)) Colours.Remove(TBCI::CfgUe(S));
}

void FTorqueBikeConfigurator::SetFinish(const FString& Key, FName Finish)
{
	if (UTorqueBuildSubsystem::Finishes().Contains(Finish)) Finishes.Add(Key, Finish);
	else Finishes.Remove(Key);
}

void FTorqueBikeConfigurator::ResetColour(const FString& Slot)
{
	// BikeShopTab.reset_colour: back to the colour and finish it comes in.
	Colours.Remove(Slot);
	Finishes.Remove(Slot);
	if (const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout())
		for (const std::string& G : TorqueFit::Shop::ColourGroups())
			for (const std::string& S : TorqueFit::Shop::ColourSlots(*Lay, G, B().Parts))
				if (TBCI::CfgUe(S) == Slot) Colours.Remove(TBCI::CfgUe(G));
	if (Slot == TEXT("tank")) Paint = StylePaint(B().Style);
}

TorqueFit::Shop::Order FTorqueBikeConfigurator::MakeOrder() const
{
	// Everything picked as the order takes it: each slot's colour, the tank's being the paint.
	TorqueFit::Shop::Order O;
	O.Style = B().Style;
	O.Parts = B().Parts;
	O.Paint[0] = Paint.R;
	O.Paint[1] = Paint.G;
	O.Paint[2] = Paint.B;
	for (const TPair<FString, FLinearColor>& KV : Colours) O.Colours[TBCI::CfgStd(KV.Key)] = {KV.Value.R, KV.Value.G, KV.Value.B};
	O.Colours["tank"] = {Paint.R, Paint.G, Paint.B};
	for (const TPair<FString, FName>& KV : Finishes) O.Finishes[TBCI::CfgStd(KV.Key)] = TBCI::CfgStd(KV.Value.ToString());
	O.TankBadge = TorqueFit::Shop::CleanBadge(B().Badge);
	return O;
}

FString FTorqueBikeConfigurator::OrderText() const
{
	return TBCI::CfgUe(TorqueFit::Shop::OrderText(MakeOrder()));
}

FString FTorqueBikeConfigurator::Describe() const
{
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return TEXT("New bike: no layout");
	FString Out = FString::Printf(TEXT("New bike aisle, %s:"), *TBCI::CfgUe(Lay->StyleName(B().Style)));
	for (const std::string& S : TorqueFit::Shop::ConfigSlots())
	{
		const auto It = B().Parts.find(S);
		Out += FString::Printf(TEXT(" %s=%s"), *TBCI::CfgUe(S), It == B().Parts.end() ? TEXT("?") : *TBCI::CfgUe(It->second));
	}
	const std::string Badge = TorqueFit::Shop::BadgeText(B().Badge);
	if (!Badge.empty()) Out += TEXT(" badge=") + TBCI::CfgUe(Badge);
	return Out;
}

FString FTorqueBikeConfigurator::TestSetup(const FString& NewStyleId, const TArray<FString>& Swaps)
{
	Open();
	if (!NewStyleId.IsEmpty()) NewStyle(TBCI::CfgStd(NewStyleId.ToLower()));
	for (const FString& Swap : Swaps)
	{
		FString Slot, Id;
		if (!Swap.Split(TEXT("="), &Slot, &Id)) continue;
		// The badge: logo=star, brand=Kestrel, font=bebas.
		if (Slot.ToLower() == TEXT("logo")) B().Badge.Logo = TBCI::CfgStd(Id.ToLower());
		else if (Slot.ToLower() == TEXT("brand")) B().Badge.Brand = TBCI::CfgStd(Id);
		else if (Slot.ToLower() == TEXT("font")) B().Badge.Font = TBCI::CfgStd(Id.ToLower());
		else SetPart(Slot.ToLower(), TBCI::CfgStd(Id.ToLower()));
	}
	// A colour of its own on the tank and the wheels, metallic, so the order shows it carries looks.
	SetColour(TEXT("tank"), FLinearColor(0.1f, 0.35f, 0.8f));
	SetColour(TEXT("wheels"), FLinearColor(0.06f, 0.06f, 0.06f));
	SetFinish(TEXT("tank"), FName(TEXT("metallic")));
	Relist();
	return Describe();
}

void FTorqueBikeConfigurator::ShowOnPreview(AHUD* Hud)
{
	ATorqueShopPreview* P = Preview.Get();
	if (!P && Hud)
	{
		P = ATorqueShopPreview::For(Hud->PlayerOwner);
		Preview = P;
	}
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!P || !Lay) return;
	TMap<FString, FString> Shown;
	for (const auto& KV : B().Parts)
		if (!KV.second.empty() && KV.second != "none") Shown.Add(TBCI::CfgUe(KV.first), TBCI::CfgUe(KV.second));
	// Each slot's colour and finish as the order will build it (the shed's own paint looks).
	TMap<FName, FTorqueShopFinish> Looks;
	for (const auto& KV : TorqueFit::Shop::OrderLooks(*Lay, MakeOrder()))
	{
		FTorqueItem It;
		It.Colour = FLinearColor((float)KV.second.Colour[0], (float)KV.second.Colour[1], (float)KV.second.Colour[2]);
		It.Finish = KV.second.Finish.empty() ? NAME_None : FName(*TBCI::CfgUe(KV.second.Finish));
		FTorqueShopFinish F;
		F.Colour = It.Colour;
		float Metallic = -1.0f, Roughness = -1.0f;
		if (UTorqueBuildSubsystem::FinishLook(It, F.Colour, Metallic, Roughness))
		{
			F.Metallic = Metallic;
			F.Roughness = Roughness;
		}
		Looks.Add(FName(*TBCI::CfgUe(KV.first)), F);
	}
	P->ShowBike(Shown, Looks, TBCI::CfgUe(TorqueFit::Shop::BadgeText(B().Badge)));
}

void FTorqueBikeConfigurator::OpenPopup(EPopup Kind, const FString& Key, const FBox2D& Anchor)
{
	Popup = Kind;
	PopupKey = Key;
	PopupAnchor = Anchor;
	PopupScroll = 0;
	if (Kind == EPopup::Colour)
	{
		PopupStart = ColourOf(Key);
		const FLinearColor Hsv = PopupStart.LinearRGBToHSV();
		PopupHue = Hsv.R;
		PopupSat = Hsv.G;
		PopupVal = Hsv.B;
	}
	// A long list opens at the one on now.
	if (Kind == EPopup::Part)
		for (const FSlotRow& R : SlotRows)
			if (R.Slot == Key) PopupScroll = FMath::Max(0, R.Picked - 4);
}

void FTorqueBikeConfigurator::ClosePopup()
{
	// The colour picker remembers a colour that was picked (ColourChoice._closed).
	if (Popup == EPopup::Colour)
	{
		const FLinearColor Now = ColourOf(PopupKey);
		if (!Now.Equals(PopupStart, 0.001f))
		{
			TArray<FLinearColor>& R = TBCI::Recent();
			R.RemoveAll([&Now](const FLinearColor& C) { return C.Equals(Now, 0.001f); });
			R.Insert(Now, 0);
			if (R.Num() > 10) R.SetNum(10);
		}
	}
	Popup = EPopup::None;
	PopupKey.Reset();
}

int32 FTorqueBikeConfigurator::PopupCount() const
{
	if (Popup == EPopup::Style)
	{
		const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
		return Lay ? (int32)Lay->Styles().size() : 0;
	}
	if (Popup == EPopup::Finish) return UTorqueBuildSubsystem::Finishes().Num() + 1;
	if (Popup == EPopup::Logo) return (int32)TorqueFit::Shop::Logos().size() + 1;
	if (Popup == EPopup::Part)
		for (const FSlotRow& R : SlotRows)
			if (R.Slot == PopupKey) return R.Choices.Num();
	return 0;
}

double& FTorqueBikeConfigurator::Placing(int32 Slider) const
{
	// BikeBadge's logo_size, logo_x, logo_y, brand_size, brand_x, brand_y.
	TorqueFit::Shop::Badge& Bd = B().Badge;
	double* Vals[6] = {&Bd.LogoSize, &Bd.LogoX, &Bd.LogoY, &Bd.BrandSize, &Bd.BrandX, &Bd.BrandY};
	return *Vals[FMath::Clamp(Slider, 0, 5)];
}

void FTorqueBikeConfigurator::SetSlider(int32 Slider, double Value)
{
	// BikeShopTab.place_badge: sizes SIZE_MIN .. SIZE_MAX, moves -MOVE_MAX .. MOVE_MAX, in 0.01 steps.
	const bool bSize = Slider % 3 == 0;
	const double Lo = bSize ? TorqueFit::Shop::BadgeSizeMin : -TorqueFit::Shop::BadgeMoveMax;
	const double Hi = bSize ? TorqueFit::Shop::BadgeSizeMax : TorqueFit::Shop::BadgeMoveMax;
	Placing(Slider) = FMath::RoundToDouble(FMath::Clamp(Value, Lo, Hi) * 100.0) / 100.0;
}

void FTorqueBikeConfigurator::Hold(const FVector2D& Mouse, bool bLeftDown)
{
	// A badge slider being dragged follows the mouse; anything else is the preview's (turning it).
	if (DragSlider != INDEX_NONE)
	{
		if (!bLeftDown)
		{
			DragSlider = INDEX_NONE;
			return;
		}
		const bool bSize = DragSlider % 3 == 0;
		const double Lo = bSize ? TorqueFit::Shop::BadgeSizeMin : -TorqueFit::Shop::BadgeMoveMax;
		const double Hi = bSize ? TorqueFit::Shop::BadgeSizeMax : TorqueFit::Shop::BadgeMoveMax;
		const double At = FMath::Clamp((Mouse.X - DragBox.Min.X) / FMath::Max(1.0, DragBox.GetSize().X), 0.0, 1.0);
		SetSlider(DragSlider, Lo + (Hi - Lo) * At);
		return;
	}
	if (ATorqueShopPreview* P = Preview.Get()) P->Hold(Mouse, bLeftDown);
}

void FTorqueBikeConfigurator::Typed()
{
	// BikeShopTab.set_brand (the LineEdit's max_length: BRAND_MAX).
	BrandEdit = BrandEdit.Left(TorqueFit::Shop::BrandMax);
	B().Badge.Brand = TBCI::CfgStd(BrandEdit);
}

bool FTorqueBikeConfigurator::Back()
{
	if (bTyping)
	{
		bTyping = false;
		return true;
	}
	if (Popup == EPopup::None) return false;
	ClosePopup();
	return true;
}

void FTorqueBikeConfigurator::Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const FBox2D& Area, float S, const FVector2D& Mouse,
	const FString& Status, bool bStatusOk)
{
	Hits.Reset();
	if (!Hud || !Canvas || !GEngine) return;
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	TBCI::FCfgPen Pen;
	Pen.C = Canvas;
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C) { TorqueUi::Rect(Canvas, X, Y, W, H, C); };
	auto Over = [&Mouse](float X, float Y, float W, float H) { return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H; };
	auto AddHit = [this](float X, float Y, float W, float H, EHit What, int32 Arg) { Hits.Add({FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), What, Arg}); };
	const bool bPopup = Popup != EPopup::None;
	// A button: lit when the mouse is over it (not while a list is open over the page).
	auto Btn = [&](float X, float Y, float W, float H, const FString& Label, float Px, bool bLit, EHit What, int32 Arg, bool bLeft = false)
	{
		const bool bOver = !bPopup && Over(X, Y, W, H);
		Box(X, Y, W, H, bLit ? (bOver ? TBCI::PickedHover : TBCI::Picked) : (bOver ? TBCI::ButtonHover : TBCI::Button));
		const FString Fitted = Pen.Fit(Label, Px, W - 14.0f * S);
		const float TX = bLeft ? X + 7.0f * S : X + (W - Pen.Width(Fitted, Px)) * 0.5f;
		Pen.Draw(TX, Y + (H - Px * 1.2f) * 0.5f, Fitted, Px, bLit ? TBCI::Ink : TBCI::Text);
		if (What != EHit::None) AddHit(X, Y, W, H, What, Arg);
	};
	if (!Lay)
	{
		Pen.Draw(Area.Min.X, Area.Min.Y, TEXT("The part list's bike layout didn't load (Data/Fitment/layout.json): no bikes to configure."), 16.0f * S, TBCI::Bad);
		return;
	}
	if (B().Style.empty() || B().Parts.empty()) NewStyle("cafe");
	if (bDirty) Relist();

	const float X0 = Area.Min.X, Y0 = Area.Min.Y, W0 = Area.GetSize().X, H0 = Area.GetSize().Y;
	const float Gap = 14.0f * S;
	// The list takes a bit over half; the bike the rest (BikeShopTab: about two fifths and the rest,
	// with a part's colour, finish and Reset beside it here).
	const float LW = FMath::Min(700.0f * S, W0 * 0.56f);
	const float RX = X0 + LW + Gap, RW = X0 + W0 - RX;
	Box(X0, Y0, LW, H0, TBCI::Column);
	ListBox = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + LW, Y0 + H0));

	// --- The list: the style, then each slot's part, colour, finish and Reset. ---
	RowH = 30.0f * S;
	const float HeadH = 26.0f * S, Inset = 8.0f * S;
	const float LabelW = 150.0f * S, CellW = 250.0f * S;
	const float PartX = X0 + Inset + LabelW, PartW = LW - 2.0f * Inset - LabelW - CellW - 6.0f * S;
	const float CellX = PartX + PartW + 6.0f * S;
	const float StylePx = 14.0f * S;
	constexpr int32 BadgeRows = 8;   // the logo, its place, the brand, four of lettering, its place
	ListHeight = HeadH + RowH + HeadH + SlotRows.Num() * RowH + HeadH + BadgeRows * RowH + Inset;
	ListScroll = FMath::Clamp(ListScroll, 0.0f, FMath::Max(0.0f, ListHeight - (H0 - Inset)));
	float Y = Y0 + 6.0f * S - ListScroll;
	auto Visible = [&](float RY, float RH) { return RY >= Y0 && RY + RH <= Y0 + H0; };
	auto Focused = [&](int32 Index, float RY, float RH)
	{
		if (Focus != Index) return;
		Box(X0 + 2.0f * S, RY, 3.0f * S, RH - 4.0f * S, FLinearColor(TBCI::Accent));
	};
	if (Visible(Y, HeadH)) Pen.Draw(X0 + Inset, Y + 6.0f * S, TEXT("STYLE"), 12.0f * S, TBCI::Accent);
	Y += HeadH;
	if (Visible(Y, RowH))
	{
		Focused(0, Y, RowH);
		Btn(X0 + Inset, Y, LW - 2.0f * Inset, RowH - 4.0f * S, TBCI::CfgUe(Lay->StyleName(B().Style)) + TEXT("   v"), StylePx, Popup == EPopup::Style, EHit::Style, 0, true);
	}
	Y += RowH;
	if (Visible(Y, HeadH))
	{
		Pen.Draw(X0 + Inset, Y + 6.0f * S, TEXT("PARTS AND COLOURS"), 12.0f * S, TBCI::Accent);
		Pen.Draw(CellX, Y + 6.0f * S, TEXT("COLOUR, FINISH"), 12.0f * S, TBCI::Dim);
	}
	Y += HeadH;
	const TArray<FName>& FinishIds = UTorqueBuildSubsystem::Finishes();
	for (int32 I = 0; I < SlotRows.Num(); ++I, Y += RowH)
	{
		if (!Visible(Y, RowH)) continue;
		const FSlotRow& R = SlotRows[I];
		Focused(I + 1, Y, RowH);
		Pen.Draw(X0 + Inset + 4.0f * S, Y + (RowH - 4.0f * S - 13.0f * S * 1.2f) * 0.5f, Pen.Fit(TBCI::CfgSlotLabel(R.Slot), 13.0f * S, LabelW - 8.0f * S), 13.0f * S, TBCI::Dim);
		const FString Label = R.Choices.IsValidIndex(R.Picked) ? R.Choices[R.Picked].Label : FString(TEXT("?"));
		Btn(PartX, Y, PartW, RowH - 4.0f * S, Label + TEXT("   v"), 13.0f * S, Popup == EPopup::Part && PopupKey == R.Slot, EHit::Part, I, true);
		if (!TakesColour(R.Slot)) continue;
		// Its colour (a swatch with a thin edge, so black shows), its finish, and Reset once changed.
		const float SwW = 36.0f * S, SwH = RowH - 8.0f * S, FinW = 128.0f * S;
		const float SwY = Y + 2.0f * S;
		const FLinearColor Col = ColourOf(R.Slot);
		const bool bSwOver = !bPopup && Over(CellX, SwY, SwW, SwH);
		Box(CellX - 1.0f, SwY - 1.0f, SwW + 2.0f, SwH + 2.0f, bSwOver ? FLinearColor(1, 1, 1, 0.6f) : TBCI::Edge);
		Box(CellX, SwY, SwW, SwH, FLinearColor(Col.R, Col.G, Col.B, 1.0f));
		AddHit(CellX, SwY, SwW, SwH, EHit::Colour, I);
		const FName* F = Finishes.Find(R.Slot);
		Btn(CellX + SwW + 6.0f * S, Y, FinW, RowH - 4.0f * S, F ? UTorqueBuildSubsystem::FinishName(*F) : FString(TEXT("As it comes")), 12.0f * S,
			Popup == EPopup::Finish && PopupKey == R.Slot, EHit::Finish, I, true);
		if (Changed(R.Slot))
			Btn(CellX + SwW + FinW + 12.0f * S, Y, CellW - SwW - FinW - 12.0f * S, RowH - 4.0f * S, TEXT("Reset"), 11.0f * S, false, EHit::Reset, I);
	}
	// --- The badge on the tank (BikeShopTab._badge_rows): the logo and its colour, its size and
	// place; the brand name (typed), its colour, its lettering, its size and place. ---
	const TorqueFit::Shop::Badge& Bdg = B().Badge;
	if (Visible(Y, HeadH)) Pen.Draw(X0 + Inset, Y + 6.0f * S, TEXT("BADGE ON THE TANK"), 12.0f * S, TBCI::Accent);
	Y += HeadH;
	const float BtnH = RowH - 4.0f * S;
	auto RowLabel = [&](float RY, const FString& L)
	{
		Pen.Draw(X0 + Inset + 4.0f * S, RY + (BtnH - 13.0f * S * 1.2f) * 0.5f, Pen.Fit(L, 13.0f * S, LabelW - 8.0f * S), 13.0f * S, TBCI::Dim);
	};
	auto BadgeSwatch = [&](float RY, int32 Which)
	{
		const float BsW = 36.0f * S, BsH = RowH - 8.0f * S, BsY = RY + 2.0f * S;
		const FLinearColor BsCol = ColourOf(Which == 0 ? TEXT("badge_logo") : TEXT("badge_brand"));
		const bool bBsOver = !bPopup && Over(CellX, BsY, BsW, BsH);
		Box(CellX - 1.0f, BsY - 1.0f, BsW + 2.0f, BsH + 2.0f, bBsOver ? FLinearColor(1, 1, 1, 0.6f) : TBCI::Edge);
		Box(CellX, BsY, BsW, BsH, FLinearColor(BsCol.R, BsCol.G, BsCol.B, 1.0f));
		AddHit(CellX, BsY, BsW, BsH, EHit::BadgeColour, Which);
	};
	// Size, along the tank and up it (BikeShopTab._place_row): a mark where each comes, and Reset.
	auto PlaceRow = [&](float RY, int32 Which)
	{
		RowLabel(RY, TEXT("   size, along, up"));
		const float Sep = 10.0f * S, SlW = (PartW - 2.0f * Sep) / 3.0f, Mid = RY + BtnH * 0.5f;
		for (int32 Ax = 0; Ax < 3; ++Ax)
		{
			const int32 SlId = Which * 3 + Ax;
			const float SlX = PartX + Ax * (SlW + Sep);
			const double Lo = Ax == 0 ? TorqueFit::Shop::BadgeSizeMin : -TorqueFit::Shop::BadgeMoveMax;
			const double Hi = Ax == 0 ? TorqueFit::Shop::BadgeSizeMax : TorqueFit::Shop::BadgeMoveMax;
			const float Frac = (float)FMath::Clamp((Placing(SlId) - Lo) / (Hi - Lo), 0.0, 1.0);
			const float Home = (float)(((Ax == 0 ? 1.0 : 0.0) - Lo) / (Hi - Lo));
			const bool bSlLit = DragSlider == SlId || (!bPopup && Over(SlX, RY, SlW, BtnH));
			Box(SlX, Mid - 2.0f * S, SlW, 4.0f * S, TBCI::Button);
			Box(SlX + Home * SlW - 1.0f, Mid - 6.0f * S, 2.0f, 12.0f * S, TBCI::Edge);
			Box(SlX + Frac * SlW - 5.0f * S, Mid - 8.0f * S, 10.0f * S, 16.0f * S, bSlLit ? TBCI::PickedHover : TBCI::Picked);
			AddHit(SlX, RY, SlW, BtnH, EHit::Slider, SlId);
		}
		Btn(CellX, RY, 90.0f * S, BtnH, TEXT("Reset"), 11.0f * S, false, EHit::PlaceReset, Which);
	};
	// The logo: its list (with its picture), its colour.
	if (Visible(Y, RowH))
	{
		RowLabel(Y, TEXT("Logo"));
		FString LogoName = TEXT("None");
		for (const auto& Lg : TorqueFit::Shop::Logos())
			if (Lg.first == Bdg.Logo) LogoName = TBCI::CfgUe(Lg.second);
		Btn(PartX, Y, PartW, BtnH, (Bdg.Logo.empty() ? FString() : FString(TEXT("        "))) + LogoName + TEXT("   v"), 13.0f * S, Popup == EPopup::Logo, EHit::Logo, 0, true);
		UTexture* Icon = Bdg.Logo.empty() ? nullptr : TorqueBikeBadge::Logo(TBCI::CfgUe(Bdg.Logo));
		if (Icon && Icon->GetResource())
		{
			FCanvasTileItem Tile(FVector2D(PartX + 8.0f * S, Y + 3.0f * S), Icon->GetResource(), FVector2D(BtnH - 6.0f * S, BtnH - 6.0f * S), ColourOf(TEXT("badge_logo")));
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
		BadgeSwatch(Y, 0);
	}
	Y += RowH;
	if (Visible(Y, RowH)) PlaceRow(Y, 0);
	Y += RowH;
	// The brand name: typed in (click it), its colour.
	if (Visible(Y, RowH))
	{
		RowLabel(Y, TEXT("Brand name"));
		const bool bFieldOver = !bPopup && Over(PartX, Y, PartW, BtnH);
		Box(PartX - 1.0f, Y - 1.0f, PartW + 2.0f, BtnH + 2.0f, bTyping ? FLinearColor(TBCI::Accent) : (bFieldOver ? FLinearColor(1, 1, 1, 0.5f) : TBCI::Edge));
		Box(PartX, Y, PartW, BtnH, TBCI::Field);
		const FString BrandNow = bTyping ? BrandEdit : TBCI::CfgUe(Bdg.Brand);
		const bool bCaret = bTyping && FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.55;
		const bool bPlaceholder = BrandNow.IsEmpty() && !bTyping;
		const FString BrandShown = bPlaceholder ? FString(TEXT("Your own make (click to type)")) : BrandNow + (bCaret ? TEXT("|") : TEXT(""));
		Pen.Draw(PartX + 8.0f * S, Y + (BtnH - 13.0f * S * 1.2f) * 0.5f, Pen.Fit(BrandShown, 13.0f * S, PartW - 16.0f * S), 13.0f * S,
			bPlaceholder ? TBCI::Off : TBCI::Text);
		AddHit(PartX, Y, PartW, BtnH, EHit::Brand, 0);
		BadgeSwatch(Y, 1);
	}
	Y += RowH;
	// Its lettering: a button for each, its name written in it.
	{
		const std::vector<std::pair<std::string, std::string>>& FontList = TorqueFit::Shop::Fonts();
		const std::string FontNow = Bdg.Font.empty() ? std::string("plain") : Bdg.Font;
		const float FSep = 4.0f * S, FW = (PartW - 2.0f * FSep) / 3.0f;
		for (int32 Fr = 0; Fr < 4; ++Fr, Y += RowH)
		{
			if (!Visible(Y, RowH)) continue;
			if (Fr == 0) RowLabel(Y, TEXT("   lettering"));
			for (int32 Fc = 0; Fc < 3; ++Fc)
			{
				const int32 Fi = Fr * 3 + Fc;
				if (Fi >= (int32)FontList.size()) continue;
				const float FX = PartX + Fc * (FW + FSep);
				const bool bFontOn = FontList[(size_t)Fi].first == FontNow;
				Btn(FX, Y, FW, BtnH, FString(), 13.0f * S, bFontOn, EHit::Font, Fi);
				const FString FontName = TBCI::CfgUe(FontList[(size_t)Fi].second);
				const FLinearColor FontInk = bFontOn ? FLinearColor(TBCI::Ink) : FLinearColor(TBCI::Text);
				if (!TorqueBikeBadge::DrawLettering(Canvas, FBox2D(FVector2D(FX + 4.0f * S, Y), FVector2D(FX + FW - 4.0f * S, Y + BtnH)),
						FontName, TBCI::CfgUe(FontList[(size_t)Fi].first), 15.0f * S, FontInk))
				{
					const FString Fitted = Pen.Fit(FontName, 13.0f * S, FW - 10.0f * S);
					Pen.Draw(FX + (FW - Pen.Width(Fitted, 13.0f * S)) * 0.5f, Y + (BtnH - 13.0f * S * 1.2f) * 0.5f, Fitted, 13.0f * S, bFontOn ? TBCI::Ink : TBCI::Text);
				}
			}
		}
	}
	if (Visible(Y, RowH)) PlaceRow(Y, 1);
	Y += RowH;
	if (ListHeight > H0)
		Pen.Draw(X0 + LW - 80.0f * S, Y0 + H0 - 20.0f * S, TEXT("(wheel)"), 12.0f * S, TBCI::Dim);

	// --- The bike: the picture, the tips under it, what it is, and Order. ---
	const float OrderH = 44.0f * S;
	const float PicH = FMath::Min(RW / 1.45f, H0 * 0.6f);
	float DY = Y0;
	ShowOnPreview(Hud);
	if (ATorqueShopPreview* P = Preview.Get())
	{
		P->Draw(Hud, Canvas, FBox2D(FVector2D(RX, DY), FVector2D(RX + RW, DY + PicH)), bPopup ? FVector2D(-1.0, -1.0) : Mouse);
		DY += PicH + 4.0f * S;
		const FString Hint = TEXT("Drag: turn,  scroll: zoom,  right-click: stop or start turning");
		const FString Tip = Pen.Fit(Hint, 12.0f * S, RW);
		Pen.Draw(RX + (RW - Pen.Width(Tip, 12.0f * S)) * 0.5f, DY, Tip, 12.0f * S, TorqueUi::Faint);
		DY += 22.0f * S;
	}
	const float StatusY = Y0 + H0 - OrderH - 28.0f * S;
	for (const FString& Para : About)
		for (const FString& L : Pen.Wrap(Para, 14.0f * S, RW))
		{
			if (DY > StatusY - 18.0f * S) break;
			Pen.Draw(RX, DY, L, 14.0f * S, TBCI::Text);
			DY += 19.0f * S;
		}
	// Can it be ordered now (a free bay)?
	FString Why;
	if (Shed)
	{
		FTorqueShedRequest Req;
		Req.Kind = ETorqueShedAct::OrderBike;
		Req.Text = OrderText();
		Why = Shed->Check(Shed->Me(), Req);
	}
	if (!Status.IsEmpty()) Pen.Draw(RX, StatusY, Pen.Fit(Status, 14.0f * S, RW), 14.0f * S, bStatusOk ? TBCI::Good : TBCI::Bad);
	else if (!Why.IsEmpty()) Pen.Draw(RX, StatusY, Pen.Fit(Why, 14.0f * S, RW), 14.0f * S, TBCI::Bad);
	const int32 OrderRow = SlotRows.Num() + 1;
	if (Focus == OrderRow) Box(RX - 4.0f * S, Y0 + H0 - OrderH - 2.0f * S, RW + 8.0f * S, OrderH + 4.0f * S, FLinearColor(TBCI::Accent));
	Btn(RX, Y0 + H0 - OrderH, RW, OrderH, Why.IsEmpty() ? FString(TEXT("Order it")) : FString(TEXT("Order it (no room)")), 18.0f * S, Why.IsEmpty(), EHit::Order, 0);

	// --- A list or the colour picker, open over the page. ---
	if (Popup == EPopup::None) return;
	const float PopX = PopupAnchor.Min.X;
	float PopY = PopupAnchor.Max.Y + 2.0f * S;
	if (Popup == EPopup::Colour)
	{
		// ColourChoice's panel: the colour square for the hue, the hue strip beside it, the paint
		// shop's colours, the colours picked lately and the shades of grey.
		const float Cell = 15.0f * S, Pd = 10.0f * S;
		const float SqW = TBCI::SvCols * Cell, SqH = TBCI::SvRows * Cell, HueW = 22.0f * S;
		const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
		const float Sw = 24.0f * S;
		const float PW = Pd * 2.0f + SqW + 8.0f * S + HueW;
		const float PH = Pd + 20.0f * S + SqH + 10.0f * S + 18.0f * S + 2.0f * (Sw + 4.0f * S) + 18.0f * S + (Sw + 4.0f * S) + Pd;
		if (PopY + PH > Canvas->ClipY) PopY = PopupAnchor.Min.Y - PH - 2.0f * S;
		const float PX = FMath::Clamp(PopX, 0.0f, Canvas->ClipX - PW);
		Box(PX - 1.0f, PopY - 1.0f, PW + 2.0f, PH + 2.0f, FLinearColor(1, 1, 1, 0.14f));
		Box(PX, PopY, PW, PH, TBCI::PopupBack);
		AddHit(PX, PopY, PW, PH, EHit::None, 0);   // (a click on the panel's back keeps it open)
		float CY = PopY + Pd;
		const FLinearColor Now = ColourOf(PopupKey);
		Pen.Draw(PX + Pd, CY, FString::Printf(TEXT("The %s's colour"), *TBCI::CfgSlotLabel(PopupKey).ToLower()), 13.0f * S, TBCI::Text);
		Box(PX + PW - Pd - 40.0f * S, CY, 40.0f * S, 16.0f * S, FLinearColor(Now.R, Now.G, Now.B, 1.0f));
		CY += 20.0f * S;
		for (int32 Cx = 0; Cx < TBCI::SvCols; ++Cx)
			for (int32 Cy = 0; Cy < TBCI::SvRows; ++Cy)
			{
				const float Sat = (Cx + 0.5f) / TBCI::SvCols, Val = 1.0f - (float)Cy / (TBCI::SvRows - 1);
				const FLinearColor C = FLinearColor(PopupHue, Sat, Val).HSVToLinearRGB();
				Box(PX + Pd + Cx * Cell, CY + Cy * Cell, Cell, Cell, FLinearColor(C.R, C.G, C.B, 1.0f));
				AddHit(PX + Pd + Cx * Cell, CY + Cy * Cell, Cell, Cell, EHit::PopupSv, Cx * 100 + Cy);
			}
		const float HueX = PX + Pd + SqW + 8.0f * S, HueCell = SqH / TBCI::HueCells;
		for (int32 Hc = 0; Hc < TBCI::HueCells; ++Hc)
		{
			const FLinearColor C = FLinearColor(360.0f * Hc / TBCI::HueCells, 1.0f, 1.0f).HSVToLinearRGB();
			Box(HueX, CY + Hc * HueCell, HueW, HueCell + 0.5f, FLinearColor(C.R, C.G, C.B, 1.0f));
			AddHit(HueX, CY + Hc * HueCell, HueW, HueCell, EHit::PopupHue, Hc);
		}
		Box(HueX - 2.0f * S, CY + PopupHue / 360.0f * SqH - 1.0f * S, HueW + 4.0f * S, 2.0f * S, FLinearColor::White);
		CY += SqH + 10.0f * S;
		auto Swatches = [&](const TArray<FLinearColor>& L, int32 Base, int32 PerRow, float SY)
		{
			for (int32 I = 0; I < L.Num(); ++I)
			{
				const float SX = PX + Pd + (I % PerRow) * (Sw + 4.0f * S), SYY = SY + (I / PerRow) * (Sw + 4.0f * S);
				const bool bOn = L[I].Equals(Now, 0.002f);
				Box(SX - 1.0f, SYY - 1.0f, Sw + 2.0f, Sw + 2.0f, bOn ? FLinearColor::White : (Over(SX, SYY, Sw, Sw) ? FLinearColor(1, 1, 1, 0.6f) : TBCI::Edge));
				Box(SX, SYY, Sw, Sw, FLinearColor(L[I].R, L[I].G, L[I].B, 1.0f));
				AddHit(SX, SYY, Sw, Sw, EHit::PopupSwatch, Base + I);
			}
		};
		Pen.Draw(PX + Pd, CY, TEXT("The paint shop's colours"), 12.0f * S, TBCI::Dim);
		CY += 18.0f * S;
		Swatches(Paints, 0, 8, CY);
		CY += 2.0f * (Sw + 4.0f * S);
		Pen.Draw(PX + Pd, CY, TBCI::Recent().Num() > 0 ? TEXT("Picked lately") : TEXT("Shades"), 12.0f * S, TBCI::Dim);
		CY += 18.0f * S;
		if (TBCI::Recent().Num() > 0) Swatches(TBCI::Recent(), 100, 10, CY);
		else
		{
			TArray<FLinearColor> Greys;
			for (int32 G = 0; G < 8; ++G) Greys.Add(FLinearColor(G / 7.0f, G / 7.0f, G / 7.0f));
			Swatches(Greys, 200, 8, CY);
		}
		return;
	}
	// A list: the choices under the button, the ones that won't go on greyed, why under the list.
	TArray<FString> Labels, Tips;
	TArray<bool> OffRows;
	int32 On = INDEX_NONE;
	if (Popup == EPopup::Style)
		for (int32 I = 0; I < (int32)Lay->Styles().size(); ++I)
		{
			Labels.Add(TBCI::CfgUe(Lay->StyleName(Lay->Styles()[(size_t)I])));
			Tips.Add(FString());
			OffRows.Add(false);
			if (Lay->Styles()[(size_t)I] == B().Style) On = I;
		}
	else if (Popup == EPopup::Logo)
	{
		Labels.Add(TEXT("None"));
		Tips.Add(FString());
		OffRows.Add(false);
		On = B().Badge.Logo.empty() ? 0 : INDEX_NONE;
		for (const auto& Lg : TorqueFit::Shop::Logos())
		{
			if (Lg.first == B().Badge.Logo) On = Labels.Num();
			Labels.Add(TEXT("        ") + TBCI::CfgUe(Lg.second));
			Tips.Add(FString());
			OffRows.Add(false);
		}
	}
	else if (Popup == EPopup::Finish)
	{
		const FName* F = Finishes.Find(PopupKey);
		Labels.Add(TEXT("As it comes"));
		Tips.Add(TEXT("The colour and finish it comes in from the factory"));
		OffRows.Add(false);
		On = F ? INDEX_NONE : 0;
		for (int32 I = 0; I < FinishIds.Num(); ++I)
		{
			Labels.Add(UTorqueBuildSubsystem::FinishName(FinishIds[I]));
			Tips.Add(FString());
			OffRows.Add(false);
			if (F && *F == FinishIds[I]) On = I + 1;
		}
	}
	else
		for (const FSlotRow& R : SlotRows)
			if (R.Slot == PopupKey)
			{
				for (const FChoice& C : R.Choices)
				{
					Labels.Add(C.Label);
					Tips.Add(C.Tip);
					OffRows.Add(C.bOff);
				}
				On = R.Picked;
			}
	const float IH = 26.0f * S;
	const int32 Shown = FMath::Min(Labels.Num(), 14);
	PopupScroll = FMath::Clamp(PopupScroll, 0, FMath::Max(0, Labels.Num() - Shown));
	const float PW = FMath::Max(PopupAnchor.GetSize().X, 340.0f * S);
	const float TipH = 40.0f * S;
	const float PH = Shown * IH + 8.0f * S + TipH;
	if (PopY + PH > Canvas->ClipY - 4.0f) PopY = FMath::Max(4.0f, PopupAnchor.Min.Y - PH - 2.0f * S);
	const float PX = FMath::Clamp(PopX, 0.0f, Canvas->ClipX - PW);
	Box(PX - 1.0f, PopY - 1.0f, PW + 2.0f, PH + 2.0f, FLinearColor(1, 1, 1, 0.14f));
	Box(PX, PopY, PW, PH, TBCI::PopupBack);
	AddHit(PX, PopY, PW, PH, EHit::None, 0);
	FString HoverTip;
	for (int32 I = PopupScroll; I < Labels.Num() && I < PopupScroll + Shown; ++I)
	{
		const float IY = PopY + 4.0f * S + (I - PopupScroll) * IH;
		const bool bOver = Over(PX + 4.0f * S, IY, PW - 8.0f * S, IH - 2.0f * S);
		if (I == On) Box(PX + 4.0f * S, IY, PW - 8.0f * S, IH - 2.0f * S, TBCI::Picked);
		else if (bOver && !OffRows[I]) Box(PX + 4.0f * S, IY, PW - 8.0f * S, IH - 2.0f * S, TBCI::ButtonHover);
		Pen.Draw(PX + 12.0f * S, IY + (IH - 13.0f * S * 1.2f) * 0.5f, Pen.Fit(Labels[I], 13.0f * S, PW - 24.0f * S), 13.0f * S,
			I == On ? TBCI::Ink : (OffRows[I] ? TBCI::Off : TBCI::Text));
		// The logos' pictures beside their names.
		if (Popup == EPopup::Logo && I > 0 && I <= (int32)TorqueFit::Shop::Logos().size())
			if (UTexture* Icon = TorqueBikeBadge::Logo(TBCI::CfgUe(TorqueFit::Shop::Logos()[(size_t)(I - 1)].first)); Icon && Icon->GetResource())
			{
				FCanvasTileItem Tile(FVector2D(PX + 12.0f * S, IY + 2.0f * S), Icon->GetResource(), FVector2D(IH - 6.0f * S, IH - 6.0f * S),
					I == On ? FLinearColor(TBCI::Ink) : FLinearColor::White);
				Tile.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Tile);
			}
		if (bOver) HoverTip = Tips[I];
		AddHit(PX + 4.0f * S, IY, PW - 8.0f * S, IH - 2.0f * S, EHit::PopupRow, I);
	}
	if (Labels.Num() > Shown)
		Pen.Draw(PX + PW - 130.0f * S, PopY + PH - TipH - 2.0f * S, FString::Printf(TEXT("%d more (wheel)"), Labels.Num() - Shown), 11.0f * S, TBCI::Dim);
	float TY = PopY + PH - TipH + 4.0f * S;
	for (const FString& L : Pen.Wrap(HoverTip, 12.0f * S, PW - 20.0f * S))
	{
		if (TY > PopY + PH - 14.0f * S) break;
		Pen.Draw(PX + 10.0f * S, TY, L, 12.0f * S, TBCI::Dim);
		TY += 16.0f * S;
	}
}

bool FTorqueBikeConfigurator::ClickPopup(const FVector2D& Mouse)
{
	// The top hit first (the popup was drawn last).
	const FHit* H = nullptr;
	for (int32 I = Hits.Num() - 1; I >= 0 && !H; --I)
		if (Hits[I].Box.IsInside(Mouse) && (Hits[I].What >= EHit::PopupRow || Hits[I].What == EHit::None)) H = &Hits[I];
	// The popup's own back is the first hit it added: anything outside it closes it.
	if (!H)
	{
		ClosePopup();
		return true;
	}
	const int32 Arg = H->Arg;
	switch (H->What)
	{
	case EHit::PopupRow:
		if (Popup == EPopup::Style)
		{
			const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
			if (Lay && Arg >= 0 && Arg < (int32)Lay->Styles().size() && Lay->Styles()[(size_t)Arg] != B().Style)
			{
				NewStyle(Lay->Styles()[(size_t)Arg]);   // (the preview keeps its view: a bike for a bike)
			}
			ClosePopup();
		}
		else if (Popup == EPopup::Logo)
		{
			// BikeShopTab.set_logo (its colour stays as picked).
			const auto& Lgs = TorqueFit::Shop::Logos();
			B().Badge.Logo = Arg >= 1 && Arg <= (int32)Lgs.size() ? Lgs[(size_t)(Arg - 1)].first : std::string();
			ClosePopup();
		}
		else if (Popup == EPopup::Finish)
		{
			const TArray<FName>& F = UTorqueBuildSubsystem::Finishes();
			SetFinish(PopupKey, Arg == 0 || !F.IsValidIndex(Arg - 1) ? NAME_None : F[Arg - 1]);
			ClosePopup();
		}
		else if (Popup == EPopup::Part)
		{
			for (const FSlotRow& R : SlotRows)
				if (R.Slot == PopupKey && R.Choices.IsValidIndex(Arg))
				{
					if (R.Choices[Arg].bOff) return true;   // (greyed: it won't go on)
					const FString Slot = R.Slot;
					const std::string Id = TBCI::CfgStd(R.Choices[Arg].Id);
					ClosePopup();
					SetPart(Slot, Id);
					return true;
				}
			ClosePopup();
		}
		return true;
	case EHit::PopupSv:
		PopupSat = ((Arg / 100) + 0.5f) / TBCI::SvCols;
		PopupVal = 1.0f - (float)(Arg % 100) / (TBCI::SvRows - 1);
		SetColour(PopupKey, FLinearColor(PopupHue, PopupSat, PopupVal).HSVToLinearRGB());
		return true;
	case EHit::PopupHue:
		PopupHue = 360.0f * Arg / TBCI::HueCells;
		if (PopupSat < 0.05f) PopupSat = 0.8f;   // (a grey picked: a hue means a colour)
		SetColour(PopupKey, FLinearColor(PopupHue, PopupSat, PopupVal).HSVToLinearRGB());
		return true;
	case EHit::PopupSwatch:
	{
		FLinearColor C;
		if (Arg < 100) C = UTorqueBuildSubsystem::Paints().IsValidIndex(Arg) ? UTorqueBuildSubsystem::Paints()[Arg] : FLinearColor::White;
		else if (Arg < 200) C = TBCI::Recent().IsValidIndex(Arg - 100) ? TBCI::Recent()[Arg - 100] : FLinearColor::White;
		else C = FLinearColor((Arg - 200) / 7.0f, (Arg - 200) / 7.0f, (Arg - 200) / 7.0f);
		C.A = 1.0f;
		SetColour(PopupKey, C);
		const FLinearColor Hsv = C.LinearRGBToHSV();
		PopupHue = Hsv.R;
		PopupSat = Hsv.G;
		PopupVal = Hsv.B;
		return true;
	}
	default:
		return true;   // the panel's back: stays open
	}
}

bool FTorqueBikeConfigurator::Click(const FVector2D& Mouse, FString& OutOrder)
{
	if (Popup != EPopup::None)
	{
		ClickPopup(Mouse);
		return false;
	}
	const FHit* H = nullptr;
	for (int32 I = Hits.Num() - 1; I >= 0 && !H; --I)
		if (Hits[I].Box.IsInside(Mouse)) H = &Hits[I];
	// A click anywhere but the brand name stops its typing.
	if (bTyping && (!H || H->What != EHit::Brand)) bTyping = false;
	if (!H) return false;
	const FBox2D Anchor = H->Box;
	const int32 Arg = H->Arg;
	const FString Slot = SlotRows.IsValidIndex(Arg) ? SlotRows[Arg].Slot : FString();
	switch (H->What)
	{
	case EHit::Style:
		Focus = 0;
		OpenPopup(EPopup::Style, TEXT("style"), Anchor);
		break;
	case EHit::Part:
		Focus = Arg + 1;
		OpenPopup(EPopup::Part, Slot, Anchor);
		break;
	case EHit::Colour:
		Focus = Arg + 1;
		OpenPopup(EPopup::Colour, Slot, Anchor);
		break;
	case EHit::Finish:
		Focus = Arg + 1;
		OpenPopup(EPopup::Finish, Slot, Anchor);
		break;
	case EHit::Reset:
		ResetColour(Slot);
		break;
	case EHit::Order:
		OutOrder = OrderText();
		return true;
	case EHit::Logo:
		OpenPopup(EPopup::Logo, TEXT("logo"), Anchor);
		break;
	case EHit::BadgeColour:
		OpenPopup(EPopup::Colour, Arg == 0 ? TEXT("badge_logo") : TEXT("badge_brand"), Anchor);
		break;
	case EHit::Brand:
		if (!bTyping) BrandEdit = TBCI::CfgUe(B().Badge.Brand);
		bTyping = true;
		break;
	case EHit::Font:
	{
		// BikeShopTab.set_brand_font ("plain" is none).
		const auto& Fts = TorqueFit::Shop::Fonts();
		if (Arg >= 0 && Arg < (int32)Fts.size()) B().Badge.Font = Fts[(size_t)Arg].first == "plain" ? std::string() : Fts[(size_t)Arg].first;
		break;
	}
	case EHit::Slider:
		DragSlider = Arg;
		DragBox = Anchor;
		Hold(Mouse, true);
		break;
	case EHit::PlaceReset:
		// BikeShopTab.reset_place: back to its usual size and place.
		for (int32 Ax = 0; Ax < 3; ++Ax) Placing(Arg * 3 + Ax) = Ax == 0 ? 1.0 : 0.0;
		break;
	default:
		break;
	}
	return false;
}

bool FTorqueBikeConfigurator::Scroll(const FVector2D& Mouse, int32 Dir)
{
	if (Popup != EPopup::None && Popup != EPopup::Colour)
	{
		PopupScroll = FMath::Clamp(PopupScroll + Dir * 2, 0, FMath::Max(0, PopupCount() - 14));
		return true;
	}
	for (const FHit& H : Hits)
		if (H.What == EHit::Slider && H.Box.IsInside(Mouse))
		{
			SetSlider(H.Arg, Placing(H.Arg) - Dir * (H.Arg % 3 == 0 ? 0.05 : 0.02));
			return true;
		}
	if (ListBox.bIsValid && ListBox.IsInside(Mouse))
	{
		ListScroll += Dir * RowH * 2.0f;
		return true;
	}
	return false;
}

bool FTorqueBikeConfigurator::Pad(int32 RowStep, bool bChoose, FString& OutOrder)
{
	// (The walker calls this every frame: only a step or Cross closes a list the mouse opened.)
	if (RowStep == 0 && !bChoose) return false;
	if (Popup != EPopup::None) ClosePopup();
	const int32 Rows = SlotRows.Num() + 2;   // the style, the slots, Order
	if (RowStep != 0)
	{
		Focus = (Focus + RowStep + Rows) % Rows;
		// Keep the focused row in the list's view.
		const float RowTop = Focus == 0 ? 26.0f * (RowH / 30.0f) : 26.0f * (RowH / 30.0f) * 2.0f + RowH * Focus;
		const float ViewH = ListBox.bIsValid ? ListBox.GetSize().Y : 600.0f;
		if (Focus <= SlotRows.Num())
		{
			if (RowTop - ListScroll < 0.0f) ListScroll = RowTop - RowH;
			if (RowTop + RowH - ListScroll > ViewH) ListScroll = RowTop + 2.0f * RowH - ViewH;
		}
	}
	if (!bChoose) return false;
	if (Focus == 0)
	{
		// The next style.
		const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
		if (!Lay || Lay->Styles().empty()) return false;
		const std::vector<std::string>& Ids = Lay->Styles();
		size_t At = 0;
		for (size_t I = 0; I < Ids.size(); ++I)
			if (Ids[I] == B().Style) At = I;
		NewStyle(Ids[(At + 1) % Ids.size()]);
		return false;
	}
	if (Focus == Rows - 1)
	{
		OutOrder = OrderText();
		return true;
	}
	// The slot's next part that goes on.
	if (bDirty) Relist();
	if (!SlotRows.IsValidIndex(Focus - 1)) return false;
	const FSlotRow& R = SlotRows[Focus - 1];
	for (int32 Step = 1; Step <= R.Choices.Num(); ++Step)
	{
		const FChoice& C = R.Choices[(R.Picked + Step) % R.Choices.Num()];
		if (C.bOff) continue;
		const FString Slot = R.Slot;
		const std::string Id = TBCI::CfgStd(C.Id);
		SetPart(Slot, Id);
		break;
	}
	return false;
}
