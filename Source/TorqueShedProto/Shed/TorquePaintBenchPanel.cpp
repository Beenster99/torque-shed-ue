#include "Shed/TorquePaintBenchPanel.h"

#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
#include "HAL/PlatformTime.h"

namespace TorquePaintBenchPanelImpl
{
// Torque Shed's UITheme, as the parts computer has it.
const FLinearColor CardBack(0.07f, 0.08f, 0.10f, 0.94f);
const FLinearColor Well(0.0f, 0.0f, 0.0f, 0.35f);
const FLinearColor Button(0.16f, 0.18f, 0.22f, 1.0f);
const FLinearColor ButtonHover(0.24f, 0.27f, 0.33f, 1.0f);
const FLinearColor Picked(0.95f, 0.62f, 0.2f, 0.9f);
const FLinearColor PickedHover(1.0f, 0.7f, 0.3f, 1.0f);
const FColor Accent(255, 190, 90);
const FColor Text(235, 235, 235);
const FColor Dim(170, 170, 175);
const FColor Ink(20, 20, 24);
const FColor Good(140, 230, 150);
const FColor Bad(255, 140, 120);
const FColor Lcd(150, 255, 160);

// The bay's colours are Torque Shed's sRGB numbers, as they are: shown as those numbers.
FLinearColor Swatch(const FLinearColor& C)
{
	return FLinearColor(FColor((uint8)FMath::RoundToInt(FMath::Clamp(C.R, 0.0f, 1.0f) * 255.0f), (uint8)FMath::RoundToInt(FMath::Clamp(C.G, 0.0f, 1.0f) * 255.0f),
		(uint8)FMath::RoundToInt(FMath::Clamp(C.B, 0.0f, 1.0f) * 255.0f)));
}

// Text in pixel sizes on the canvas (the engine's large font, scaled).
struct FPen
{
	UCanvas* C = nullptr;
	UFont* Font = nullptr;
	float LineH = 24.0f;
	float K(float Px) const { return Px / FMath::Max(LineH, 1.0f); }
	float Width(const FString& S, float Px) const
	{
		float W = 0.0f, H = 0.0f;
		C->StrLen(Font, S, W, H);
		return W * K(Px);
	}
	void Put(float X, float Y, const FString& S, float Px, const FColor& Colour) const
	{
		if (S.IsEmpty()) return;
		C->SetDrawColor(Colour);
		C->DrawText(Font, S, X, Y, K(Px), K(Px));
	}
	FString Fit(const FString& S, float Px, float MaxW) const
	{
		if (Width(S, Px) <= MaxW) return S;
		FString Out = S;
		while (Out.Len() > 1 && Width(Out + TEXT("..."), Px) > MaxW) Out.LeftChopInline(1);
		return Out + TEXT("...");
	}
};

FLinearColor StockColour(int32 Index)
{
	const int32 NB = TorquePaintMix::Bases().Num();
	if (Index < NB) return TorquePaintMix::Bases()[Index].Colour;
	if (TorquePaintMix::Tints().IsValidIndex(Index - NB)) return TorquePaintMix::Tints()[Index - NB].Colour;
	return FLinearColor::Gray;
}
}

namespace TPBPI = TorquePaintBenchPanelImpl;

const TCHAR* FTorquePaintBenchPanel::PageName(int32 Page)
{
	static const TCHAR* const Names[] = {TEXT("Mix"), TEXT("Recipes"), TEXT("Supplies")};
	return Names[FMath::Clamp(Page, 0, Pages - 1)];
}

FTorqueBenchClick FTorquePaintBenchPanel::Click(const FVector2D& Mouse) const
{
	for (const FHit& H : Hits)
		if (H.Box.IsInside(Mouse)) return H.Click;
	return FTorqueBenchClick();
}

void FTorquePaintBenchPanel::Scroll(const FVector2D& Mouse, int32 Dir)
{
	if (ListBox.bIsValid && ListBox.IsInside(Mouse))
		ListScroll = FMath::Clamp(ListScroll + Dir * 2, 0, FMath::Max(0, ListRows - ListShown));
}

void FTorquePaintBenchPanel::Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const TArray<FTorqueBenchRow>& Rows,
	const FTorqueBenchState& State, const FVector2D& Mouse)
{
	namespace TPM = TorquePaintMix;
	Hits.Reset();
	Card = ListBox = FBox2D(ForceInit);
	if (!Hud || !Canvas || !GEngine || !Shed) return;
	TPBPI::FPen Pen;
	Pen.C = Canvas;
	Pen.Font = GEngine->GetLargeFont();
	{
		float W = 0.0f, H = 0.0f;
		Canvas->StrLen(Pen.Font, TEXT("Hg"), W, H);
		Pen.LineH = H;
	}
	const FTorquePaintBay& P = Shed->PaintBay();
	const float S = Canvas->ClipY / 1080.0f;
	auto Box = [Hud](float X, float Y, float W, float H, const FLinearColor& C) { Hud->DrawRect(C, X, Y, W, H); };
	auto IsOver = [&Mouse](float X, float Y, float W, float H) { return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H; };
	auto AddHit = [this](float X, float Y, float W, float H, FTorqueBenchClick::EKind Kind, int32 Arg)
	{
		FHit Hit;
		Hit.Box = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		Hit.Click.Kind = Kind;
		Hit.Click.Arg = Arg;
		Hits.Add(Hit);
	};
	auto Outline = [&Box, S](float X, float Y, float W, float H)
	{
		const float T = 2.0f * S;
		const FLinearColor A(TPBPI::Accent);
		Box(X - T, Y - T, W + 2.0f * T, T, A);
		Box(X - T, Y + H, W + 2.0f * T, T, A);
		Box(X - T, Y, T, H, A);
		Box(X + W, Y, T, H, A);
	};
	auto Btn = [&](float X, float Y, float W, float H, const FString& Label, float Px, bool bPicked, FTorqueBenchClick::EKind Kind, int32 Arg, bool bFocus)
	{
		const bool bOver = IsOver(X, Y, W, H);
		Box(X, Y, W, H, bPicked ? (bOver ? TPBPI::PickedHover : TPBPI::Picked) : (bOver ? TPBPI::ButtonHover : TPBPI::Button));
		const FString Fitted = Pen.Fit(Label, Px, W - 14.0f * S);
		Pen.Put(X + (W - Pen.Width(Fitted, Px)) * 0.5f, Y + (H - Px * 1.2f) * 0.5f, Fitted, Px, bPicked ? TPBPI::Ink : TPBPI::Text);
		AddHit(X, Y, W, H, Kind, Arg);
		if (bFocus) Outline(X, Y, W, H);
	};
	auto RowAt = [&Rows](ETorqueShedAct Kind) { return Rows.IndexOfByPredicate([Kind](const FTorqueBenchRow& R) { return R.Kind == Kind; }); };

	// The card down the right side (paint_bench.gd PANEL_W 470), the bench in view to its left.
	const float PW = FMath::Min(490.0f * S, Canvas->ClipX * 0.45f), PH = Canvas->ClipY - 40.0f * S;
	const float X0 = Canvas->ClipX - PW - 20.0f * S, Y0 = 20.0f * S, M = 16.0f * S;
	const float X = X0 + M, W = PW - 2.0f * M;
	Card = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + PW, Y0 + PH));
	Box(X0, Y0, PW, PH, TPBPI::CardBack);
	Box(X0, Y0, PW, 3.0f * S, FLinearColor(TPBPI::Accent));
	float Y = Y0 + M;
	Pen.Put(X, Y + 4.0f * S, TEXT("MIXING BENCH"), 24.0f * S, TPBPI::Accent);
	Btn(X + W - 150.0f * S, Y, 150.0f * S, 32.0f * S, TEXT("Step back (F)"), 14.0f * S, false, FTorqueBenchClick::EKind::Close, 0, false);
	Y += 44.0f * S;
	const float TabW = (W - 16.0f * S) / 3.0f;
	for (int32 I = 0; I < Pages; ++I)
		Btn(X + I * (TabW + 8.0f * S), Y, TabW, 32.0f * S, PageName(I), 15.0f * S, State.Page == I, FTorqueBenchClick::EKind::Page, I, false);
	Y += 46.0f * S;
	const float FootH = 64.0f * S;
	const float Bottom = Y0 + PH - M - FootH;

	// A list of rows that scrolls: those of Kinds, from Y down to ListBottom, each RowH tall.
	TArray<int32> List;
	auto DrawList = [&](float ListBottom, float RowH, TFunctionRef<void(int32 Row, float RX, float RY, float RW, float RH)> DrawRow)
	{
		ListRows = List.Num();
		ListShown = FMath::Max(1, FMath::FloorToInt((ListBottom - Y) / RowH));
		const int32 At = List.IndexOfByKey(ShowRow);
		if (At != INDEX_NONE)
		{
			if (At < ListScroll) ListScroll = At;
			if (At >= ListScroll + ListShown) ListScroll = At - ListShown + 1;
		}
		ShowRow = INDEX_NONE;
		ListScroll = FMath::Clamp(ListScroll, 0, FMath::Max(0, ListRows - ListShown));
		ListBox = FBox2D(FVector2D(X, Y), FVector2D(X + W, ListBottom));
		Box(X, Y, W, ListBottom - Y, TPBPI::Well);
		for (int32 I = ListScroll; I < List.Num() && I < ListScroll + ListShown; ++I)
			DrawRow(List[I], X + 4.0f * S, Y + 4.0f * S + (I - ListScroll) * RowH, W - 8.0f * S, RowH - 4.0f * S);
		if (ListRows > ListShown)
			Pen.Put(X + W - 150.0f * S, ListBottom - 20.0f * S, FString::Printf(TEXT("%d-%d of %d (wheel)"), ListScroll + 1,
				FMath::Min(ListScroll + ListShown, ListRows), ListRows), 12.0f * S, TPBPI::Dim);
		Y = ListBottom + 10.0f * S;
	};

	const FTorqueTin* Tin = P.BenchTinPtr();
	if (State.Page == 0 && !Tin)
	{
		// No tin: a new one (a litre of base), or one brought from the shelf.
		Pen.Put(X, Y, TEXT("No tin on the scale."), 18.0f * S, TPBPI::Text);
		Y += 28.0f * S;
		Pen.Put(X, Y, TEXT("Start a new litre tin of base, or bring a tin from the shelf (F on the bench)."), 13.0f * S, TPBPI::Dim);
		Y += 30.0f * S;
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].Kind == ETorqueShedAct::PaintTinNew) List.Add(I);
		DrawList(Bottom, 58.0f * S, [&](int32 R, float RX, float RY, float RW, float RH)
		{
			const FTorqueBenchRow& Row = Rows[R];
			const bool bOver = IsOver(RX, RY, RW, RH);
			Box(RX, RY, RW, RH, bOver ? TPBPI::ButtonHover : TPBPI::Button);
			Box(RX + 8.0f * S, RY + 8.0f * S, RH - 16.0f * S, RH - 16.0f * S, TPBPI::Swatch(TPM::Bases()[FMath::Clamp(Row.Colour, 0, TPM::Bases().Num() - 1)].Colour));
			Pen.Put(RX + RH + 4.0f * S, RY + 8.0f * S, TPM::Bases()[FMath::Clamp(Row.Colour, 0, TPM::Bases().Num() - 1)].Name, 16.0f * S, TPBPI::Text);
			Pen.Put(RX + RH + 4.0f * S, RY + 30.0f * S, Pen.Fit(FString::Printf(TEXT("%.1f l on the shelf"), P.StockOf(FTorquePaintBay::BaseStock(Row.Colour))),
				12.0f * S, RW - RH), 12.0f * S, TPBPI::Dim);
			AddHit(RX, RY, RW, RH, FTorqueBenchClick::EKind::Row, R);
			if (R == State.Focus) Outline(RX, RY, RW, RH);
		});
	}
	else if (State.Page == 0)
	{
		const TPM::FPaintSpec Spec = Tin->Spec();
		// The tin, and the scale's reading with Tare beside it.
		Pen.Put(X, Y, Pen.Fit(P.TinText(*Tin), 16.0f * S, W), 16.0f * S, TPBPI::Text);
		Y += 22.0f * S;
		Pen.Put(X, Y, Pen.Fit(TPM::FullCode(Tin->Mix), 13.0f * S, W), 13.0f * S, TPBPI::Dim);
		Y += 24.0f * S;
		Box(X, Y, W, 60.0f * S, FLinearColor(0.01f, 0.02f, 0.015f, 1.0f));
		Pen.Put(X + 12.0f * S, Y + 10.0f * S, TPM::GramsText(P.ScaleGrams() + State.PourUnsent) + TEXT(" g"), 34.0f * S, TPBPI::Lcd);
		if (P.Tared()) Pen.Put(X + 12.0f * S, Y + 44.0f * S, TEXT("tared: this tint on its own"), 11.0f * S, TPBPI::Lcd);
		const int32 Tare = RowAt(ETorqueShedAct::PaintTare);
		if (Tare != INDEX_NONE) Btn(X + W - 110.0f * S, Y + 12.0f * S, 98.0f * S, 36.0f * S, TEXT("Tare"), 15.0f * S, false, FTorqueBenchClick::EKind::Row, Tare, State.Focus == Tare);
		Y += 70.0f * S;
		// The mix as it'd spray, beside the colour to match, and how close.
		const float Sw = 84.0f * S;
		Pen.Put(X, Y, TEXT("THE MIX"), 11.0f * S, TPBPI::Dim);
		Box(X, Y + 16.0f * S, Sw, Sw, TPBPI::Swatch(Spec.Colour));
		float TX = X + Sw + 12.0f * S;
		if (P.bTarget)
		{
			Pen.Put(TX, Y, TEXT("TO MATCH"), 11.0f * S, TPBPI::Dim);
			Box(TX, Y + 16.0f * S, Sw, Sw, TPBPI::Swatch(P.TargetColour));
			TX += Sw + 14.0f * S;
			const float Match = TPM::MatchOf(Spec.Colour, P.TargetColour, Spec.Effect, P.TargetEffect);
			Pen.Put(TX, Y + 16.0f * S, Pen.Fit(P.TargetName, 15.0f * S, X + W - TX), 15.0f * S, TPBPI::Text);
			Pen.Put(TX, Y + 38.0f * S, TPM::MatchText(Match) + TEXT(" match"), 20.0f * S, Match >= TPM::GoodMatch ? TPBPI::Good : TPBPI::Bad);
			Pen.Put(TX, Y + 66.0f * S, Match >= TPM::GoodMatch ? TEXT("spot on") : FString::Printf(TEXT("off by %.1f (delta E)"), TPM::DeltaE(Spec.Colour, P.TargetColour)),
				12.0f * S, TPBPI::Dim);
		}
		else
		{
			Pen.Put(TX, Y + 16.0f * S, TPM::EffectName(Spec.Effect), 16.0f * S, TPBPI::Text);
			Pen.Put(TX, Y + 40.0f * S, Pen.Fit(TEXT("No colour to match: put a recipe card on the bench (Recipes)."), 12.0f * S, X + W - TX), 12.0f * S, TPBPI::Dim);
		}
		if (P.CardRecipe != 0)
			Pen.Put(TX, Y + 84.0f * S, FString::Printf(TEXT("Recipe card: %d%% to it"), FMath::RoundToInt(TPM::RecipeAccuracy(Tin->Mix, P.CardMix) * 100.0f)), 12.0f * S, TPBPI::Accent);
		Y += Sw + 26.0f * S;
		// How well it's stirred.
		const float Stir = FMath::Clamp(Tin->Mix.Stir + State.StirUnsent, 0.0f, 1.0f);
		Pen.Put(X, Y, FString::Printf(TEXT("Stirred %d%%"), FMath::RoundToInt(Stir * 100.0f)), 14.0f * S, Stir >= 0.98f ? TPBPI::Good : TPBPI::Text);
		Pen.Put(X + 120.0f * S, Y + 2.0f * S, Stir >= 0.98f ? TEXT("an even mix") : TEXT("hold the right mouse in the view and stir round (or R)"), 11.0f * S, TPBPI::Dim);
		Y += 20.0f * S;
		Box(X, Y, W, 8.0f * S, TPBPI::Well);
		Box(X, Y, W * Stir, 8.0f * S, Stir >= 0.98f ? FLinearColor(TPBPI::Good) : FLinearColor(TPBPI::Accent));
		Y += 18.0f * S;
		if (State.Pouring != INDEX_NONE && TPM::Tints().IsValidIndex(State.Pouring))
			Pen.Put(X, Y, FString::Printf(TEXT("Pouring %s: the bottle %d%% tipped, %.1f g a second"), TPM::Tints()[State.Pouring].Name,
				FMath::RoundToInt(State.Tilt * 100.0f), TPM::PourRate(State.Tilt)), 13.0f * S, TPBPI::Accent);
		else Pen.Put(X, Y, TEXT("Pick a tint, then hold the left mouse on it (or in the view, dragging down to tip it further)."), 11.0f * S, TPBPI::Dim);
		Y += 22.0f * S;
		// The rack's tints that can go in it.
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].Kind == ETorqueShedAct::PaintPour) List.Add(I);
		const float ButtonsH = 2.0f * 38.0f * S + 8.0f * S + 48.0f * S;
		DrawList(Bottom - ButtonsH, 34.0f * S, [&](int32 R, float RX, float RY, float RW, float RH)
		{
			const FTorqueBenchRow& Row = Rows[R];
			const int32 T = FMath::Clamp(Row.Colour, 0, TPM::Tints().Num() - 1);
			const bool bPicked = R == State.Focus;
			const bool bOver = IsOver(RX, RY, RW, RH);
			Box(RX, RY, RW, RH, bPicked ? (bOver ? TPBPI::PickedHover : TPBPI::Picked) : (bOver ? TPBPI::ButtonHover : TPBPI::Button));
			Box(RX + 5.0f * S, RY + 4.0f * S, RH - 8.0f * S, RH - 8.0f * S, TPBPI::Swatch(TPM::Tints()[T].Colour));
			const FColor Ink = bPicked ? TPBPI::Ink : TPBPI::Text;
			Pen.Put(RX + RH + 2.0f * S, RY + 6.0f * S, Pen.Fit(TPM::Tints()[T].Name, 14.0f * S, RW * 0.45f), 14.0f * S, Ink);
			FString Right = FString::Printf(TEXT("%s g in"), *TPM::GramsText(Tin->Mix.GramsOf(T)));
			if (P.CardRecipe != 0 && P.CardMix.GramsOf(T) > 0.0f) Right += FString::Printf(TEXT("  card %s g"), *TPM::GramsText(P.CardMix.GramsOf(T)));
			Right += FString::Printf(TEXT("  (%d g left)"), FMath::RoundToInt(P.StockOf(FTorquePaintBay::TintStock(T))));
			Pen.Put(RX + RW - Pen.Width(Right, 12.0f * S) - 8.0f * S, RY + 8.0f * S, Right, 12.0f * S, bPicked ? TPBPI::Ink : TPBPI::Dim);
			AddHit(RX, RY, RW, RH, FTorqueBenchClick::EKind::Row, R);
			if (State.Pouring == T) Outline(RX, RY, RW, RH);
		});
		// What else to do with it, two to a line.
		const ETorqueShedAct Acts[] = {ETorqueShedAct::PaintAddBase, ETorqueShedAct::PaintTest, ETorqueShedAct::PaintShake, ETorqueShedAct::PaintTinEmpty};
		const TCHAR* Labels[] = {TEXT("Add 250 g base"), TEXT("Spray a test card"), TEXT("Into the shaker"), TEXT("Tip it away")};
		const float BW = (W - 8.0f * S) * 0.5f;
		for (int32 I = 0; I < 4; ++I)
		{
			const int32 R = RowAt(Acts[I]);
			if (R == INDEX_NONE) continue;
			Btn(X + (I % 2) * (BW + 8.0f * S), Y + (I / 2) * 38.0f * S, BW, 32.0f * S, Labels[I], 14.0f * S, false, FTorqueBenchClick::EKind::Row, R, State.Focus == R);
		}
		Y += 2.0f * 38.0f * S + 8.0f * S;
		// Its name, and Save (onto the tin shelf and the swatch wall).
		const float SaveW = 130.0f * S, FieldW = W - SaveW - 8.0f * S;
		Box(X, Y, FieldW, 36.0f * S, State.bNaming ? FLinearColor(0.2f, 0.22f, 0.26f, 1.0f) : TPBPI::Well);
		const FString Shown = State.Name + (State.bNaming && FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.5 ? TEXT("|") : TEXT(""));
		Pen.Put(X + 10.0f * S, Y + 8.0f * S, Pen.Fit(Shown, 16.0f * S, FieldW - 20.0f * S), 16.0f * S, State.bNaming ? TPBPI::Text : TPBPI::Dim);
		if (!State.bNaming) Pen.Put(X + FieldW - 92.0f * S, Y + 11.0f * S, TEXT("click to name"), 11.0f * S, TPBPI::Dim);
		else Outline(X, Y, FieldW, 36.0f * S);
		AddHit(X, Y, FieldW, 36.0f * S, FTorqueBenchClick::EKind::Name, 0);
		const int32 Save = RowAt(ETorqueShedAct::PaintTinSave);
		if (Save != INDEX_NONE) Btn(X + FieldW + 8.0f * S, Y, SaveW, 36.0f * S, TEXT("Save it"), 15.0f * S, true, FTorqueBenchClick::EKind::Row, Save, State.Focus == Save);
		Y += 46.0f * S;
	}
	else if (State.Page == 1)
	{
		// The swatch wall's cards.
		const int32 Away = RowAt(ETorqueShedAct::PaintTarget);
		if (Away != INDEX_NONE)
		{
			Btn(X, Y, W, 32.0f * S, FString::Printf(TEXT("Put the card away (%s)"), *P.TargetName), 14.0f * S, false, FTorqueBenchClick::EKind::Row, Away, State.Focus == Away);
			Y += 42.0f * S;
		}
		Pen.Put(X, Y, TEXT("On the bench: its weights to pour to, and its colour to match. By eye: just the colour."), 11.0f * S, TPBPI::Dim);
		Y += 20.0f * S;
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].Kind == ETorqueShedAct::PaintRecipe) List.Add(I);
		if (List.Num() == 0) Pen.Put(X, Y + 10.0f * S, TEXT("No recipe cards yet: save a mix and its card goes on the swatch wall."), 13.0f * S, TPBPI::Text);
		else DrawList(Bottom, 52.0f * S, [&](int32 R, float RX, float RY, float RW, float RH)
		{
			const FTorqueBenchRow& Row = Rows[R];
			const FTorquePaintRecipe* Rec = P.Recipe(Row.Item);
			if (!Rec) return;
			Box(RX, RY, RW, RH, TPBPI::Button);
			Box(RX + 5.0f * S, RY + 5.0f * S, RH - 10.0f * S, RH - 10.0f * S, TPBPI::Swatch(Rec->Colour));
			const float BX = RX + RW - 2.0f * 84.0f * S - 10.0f * S;
			Pen.Put(RX + RH + 2.0f * S, RY + 6.0f * S, Pen.Fit(Rec->Name, 15.0f * S, BX - RX - RH - 6.0f * S), 15.0f * S, TPBPI::Text);
			Pen.Put(RX + RH + 2.0f * S, RY + 27.0f * S, Pen.Fit(Rec->Code, 11.0f * S, BX - RX - RH - 6.0f * S), 11.0f * S, TPBPI::Dim);
			Btn(BX, RY + 8.0f * S, 80.0f * S, RH - 16.0f * S, TEXT("On bench"), 12.0f * S, P.CardRecipe == Rec->Id, FTorqueBenchClick::EKind::Row, R, State.Focus == R);
			Btn(BX + 86.0f * S, RY + 8.0f * S, 80.0f * S, RH - 16.0f * S, TEXT("By eye"), 12.0f * S, false, FTorqueBenchClick::EKind::Eye, R, false);
		});
	}
	else
	{
		// The stock, restocked (free: the shed has no money yet).
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].Kind == ETorqueShedAct::PaintBuy) List.Add(I);
		DrawList(Bottom, 40.0f * S, [&](int32 R, float RX, float RY, float RW, float RH)
		{
			const FTorqueBenchRow& Row = Rows[R];
			Box(RX, RY, RW, RH, TPBPI::Button);
			Box(RX + 5.0f * S, RY + 5.0f * S, RH - 10.0f * S, RH - 10.0f * S, TPBPI::Swatch(TPBPI::StockColour(Row.Colour)));
			Pen.Put(RX + RH + 2.0f * S, RY + 9.0f * S, Pen.Fit(P.StockText(Row.Colour), 14.0f * S, RW - RH - 120.0f * S), 14.0f * S, TPBPI::Text);
			Btn(RX + RW - 106.0f * S, RY + 5.0f * S, 100.0f * S, RH - 10.0f * S,
				Row.Colour < TPM::Bases().Num() ? TEXT("+1 litre") : TEXT("+250 g"), 13.0f * S, false, FTorqueBenchClick::EKind::Row, R, State.Focus == R);
		});
	}

	// The foot: what the shed last said, and the shaker.
	float FY = Y0 + PH - M - FootH;
	if (P.ShakerTin != 0)
	{
		Pen.Put(X, FY, FString::Printf(TEXT("Shaker: %d s to go"), FMath::CeilToInt(P.ShakerLeft)), 13.0f * S, TPBPI::Accent);
		FY += 20.0f * S;
	}
	if (!State.Note.IsEmpty()) Pen.Put(X, FY, Pen.Fit(State.Note, 13.0f * S, W), 13.0f * S, State.bNoteOk ? TPBPI::Good : TPBPI::Bad);
	Pen.Put(X, Y0 + PH - M - 16.0f * S, State.bNaming ? TEXT("Type its name, Enter when done") : TEXT("Pad: D-pad down the card, L1 / R1 pages, Cross pours or presses, Square stirs"),
		11.0f * S, TPBPI::Dim);
}
