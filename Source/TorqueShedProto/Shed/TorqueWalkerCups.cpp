// The spray gun's two cups (beenster, 19 Nov 2026: "when I hit [ it allows me to select the colour
// and finish for cup 1. I need to accept this colour somehow and I should be able to visibly see the
// full cup. When I select ] it allows me to select the colour and finish for cup 2"). [ and ] open
// the picker on a cup: the colour (left / right, the wheel) and the finish (up / down), Enter fills
// the cup to the brim and you watch it fill. With the gun in hand both cups stand at the side of the
// view, filled as they are, with what's loaded. A fade is then cup 1's base coat all over and cup 2
// over about half (TorqueWalkerSpray.cpp FinishFades).
#include "Shed/TorqueWalker.h"

#include "Coop/TorqueKeys.h"
#include "Coop/TorqueUi.h"
#include "SonyPad.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "TorqueAudio/TorqueSfx.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

namespace TorqueWalkerCupsImpl
{
// The colour as its finish draws it (a matte darker, chrome near silver), for the swatches and cups.
FLinearColor Shown(int32 Colour, FName Finish)
{
	const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
	FTorqueItem It;
	It.Colour = Paints.IsValidIndex(Colour) ? Paints[Colour] : FLinearColor::White;
	It.Finish = Finish;
	It.PaintQ = 1.0f;
	FLinearColor C = It.Colour;
	float Metal = 0.0f, Rough = 0.0f;
	UTorqueBuildSubsystem::FinishLook(It, C, Metal, Rough);
	C.A = 1.0f;
	return C;
}

// A cup picked here keeps its colour and finish in its code ("P3:matte"): read it back.
bool Picked(const FTorquePaintCup& Cup, int32& OutColour, int32& OutFinish)
{
	FString Idx, Fin;
	if (!Cup.Spec.Code.StartsWith(TEXT("P")) || !Cup.Spec.Code.Split(TEXT(":"), &Idx, &Fin)) return false;
	OutColour = FMath::Clamp(FCString::Atoi(*Idx.Mid(1)), 0, UTorqueBuildSubsystem::Paints().Num() - 1);
	OutFinish = FMath::Max(UTorqueBuildSubsystem::Finishes().IndexOfByKey(FName(*Fin)), 0);
	return true;
}

FLinearColor CupColour(const FTorquePaintCup& Cup)
{
	int32 Colour = 0, Finish = 0;
	if (Picked(Cup, Colour, Finish)) return Shown(Colour, UTorqueBuildSubsystem::Finishes()[Finish]);
	FLinearColor C = Cup.Spec.IsLacquer() ? FLinearColor(0.85f, 0.85f, 0.8f, 1.0f) : Cup.Spec.Colour;
	C.A = 1.0f;
	return C;
}

constexpr double FillTime = 0.9;    // the cup filling, seconds
constexpr double FullHold = 0.6;    // ... then shown full a moment before the picker shuts
} // namespace TorqueWalkerCupsImpl

namespace TWCI = TorqueWalkerCupsImpl;

void ATorqueWalker::OpenCupPicker(int32 Which)
{
	CupPick = FMath::Clamp(Which, 0, 1);
	PickFill = -1.0;
	// Starts on what's in the cup, if it was picked here.
	if (const UTorqueBuildSubsystem* B = Shed())
		if (const FTorquePaintCup* Cup = B->PaintBay().CupOf(B->Me(), CupPick))
			TWCI::Picked(*Cup, PickColour[CupPick], PickFinish[CupPick]);
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("switch"), Camera->GetComponentLocation(), -10.0f);
}

void ATorqueWalker::CloseCupPicker()
{
	CupPick = INDEX_NONE;
	PickFill = -1.0;
	ComputerClosedFrame = GFrameCounter;   // (the Esc that shut it isn't the pause menu's too)
	bWaitRelease = true;                   // (the click that filled it doesn't also work or spray)
	bSprayNeedRelease = true;
}

void ATorqueWalker::CupPickerInput(APlayerController* PC, uint32 SonyHit, float Dt)
{
	if (!PC || CupPick == INDEX_NONE) return;
	// Filling: watch it go in, then it shuts by itself.
	if (PickFill >= 0.0)
	{
		PickFill += Dt;
		if (PickFill >= TWCI::FillTime + TWCI::FullHold) CloseCupPicker();
		return;
	}
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	auto KeyJust = [PC](ETorqueKey A) { return FTorqueKeys::Just(PC, A); };
	const int32 Colours = UTorqueBuildSubsystem::Paints().Num(), Finishes = UTorqueBuildSubsystem::Finishes().Num();
	if (Colours <= 0 || Finishes <= 0) return;
	// Shut it: Esc, right mouse, Backspace (Circle or Triangle on a pad).
	if (Just(EKeys::Escape) || Just(EKeys::RightMouseButton) || Just(EKeys::BackSpace) || Just(EKeys::Gamepad_FaceButton_Right)
		|| Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Circle) || (SonyHit & ESonyButton::Triangle))
	{
		CloseCupPicker();
		return;
	}
	// The other cup: [ or ] (Tab, or L1 / R1 on a pad).
	if (KeyJust(ETorqueKey::SprayCup1)) OpenCupPicker(0);
	else if (KeyJust(ETorqueKey::SprayCup2)) OpenCupPicker(1);
	else if (Just(EKeys::Tab) || Just(EKeys::Gamepad_LeftShoulder) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::L1)
		|| (SonyHit & ESonyButton::R1))
		OpenCupPicker(1 - CupPick);
	int32& Colour = PickColour[CupPick];
	int32& Finish = PickFinish[CupPick];
	// The colour: left / right, A / D, the wheel or Q / E (the D-pad's left / right).
	if (Just(EKeys::Right) || Just(EKeys::D) || KeyJust(ETorqueKey::NextTool) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight))
		Colour = (Colour + 1) % Colours;
	if (Just(EKeys::Left) || Just(EKeys::A) || KeyJust(ETorqueKey::PrevTool) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft))
		Colour = (Colour + Colours - 1) % Colours;
	// The finish: up / down, W / S (the D-pad's up / down).
	if (Just(EKeys::Down) || Just(EKeys::S) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Finish = (Finish + 1) % Finishes;
	if (Just(EKeys::Up) || Just(EKeys::W) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Finish = (Finish + Finishes - 1) % Finishes;
	Colour = FMath::Clamp(Colour, 0, Colours - 1);
	Finish = FMath::Clamp(Finish, 0, Finishes - 1);
	// Fill it: Enter, Space or a click (Cross on a pad).
	if (Just(EKeys::Enter) || Just(EKeys::SpaceBar) || Just(EKeys::LeftMouseButton) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross))
	{
		UTorqueBuildSubsystem* B = Shed();
		if (!B) return;
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintCup;
		R.Item = CupPick;
		R.Colour = Colour;
		R.Finish = UTorqueBuildSubsystem::Finishes()[Finish];
		FString WhyNot;
		if (!B->Request(R, WhyNot))
		{
			Say(WhyNot, false);
			return;
		}
		PickFill = 0.0;
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("pour"), Camera->GetComponentLocation(), -8.0f);
	}
}

bool ATorqueWalker::CupsHudUp() const
{
	if (CupPick != INDEX_NONE) return true;
	if (Tool != ETorqueTool::Gun || bComputer || bBench || bRidingOut) return false;
	const UTorqueBuildSubsystem* B = Shed();
	return B && B->HeldBy(B->Me()) == 0;
}

void ATorqueWalker::DrawCups(AHUD* Hud, UCanvas* Canvas) const
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	const UTorqueBuildSubsystem* B = Shed();
	if (!Hud || !Canvas || !Font || !B) return;
	const float S = Canvas->ClipY / 1080.0f;
	auto Px = [S](float Size) { return S * Size / 20.0f; };
	auto Rect = [Hud](const FLinearColor& C, float X, float Y, float W, float H) { Hud->DrawRect(C, X, Y, W, H); };
	auto Frame = [&Rect](const FLinearColor& C, float X, float Y, float W, float H, float T)
	{
		Rect(C, X, Y, W, T);
		Rect(C, X, Y + H - T, W, T);
		Rect(C, X, Y, T, H);
		Rect(C, X + W - T, Y, T, H);
	};
	const float Em = (float)FMath::Max(Font->LegacyFontSize, 8);
	auto Text = [Canvas, Em](const FString& T, float X, float Y, float Size, const FLinearColor& C, int32 Align = 0, float Width = 0.0f)
	{
		const TorqueUi::EAlign A = Align == 1 ? TorqueUi::EAlign::Centre : (Align == 2 ? TorqueUi::EAlign::Right : TorqueUi::EAlign::Left);
		const float AX = Align == 1 ? X + Width * 0.5f : (Align == 2 ? X + Width : X);
		TorqueUi::Text(Canvas, T, AX, Y, Em * Size, C, false, A, true);
	};
	const FLinearColor Panel(0.03f, 0.04f, 0.06f, 0.82f), Dim(1.0f, 1.0f, 1.0f, 0.6f), Accent = TorqueUi::Accent;
	// A gun cup: a steel rim and lid, the paint in it to CupLevel (0 .. 1), seen from the side.
	auto Cup = [&Rect, &Frame, S, Accent](float X, float Y, float W, float H, const FLinearColor& Paint, float CupLevel, bool bLit)
	{
		const FLinearColor Steel(0.7f, 0.72f, 0.76f, 1.0f), Glass(0.12f, 0.13f, 0.15f, 0.9f);
		Rect(Steel, X - 3.0f * S, Y - 6.0f * S, W + 6.0f * S, 5.0f * S);   // the lid
		Rect(Glass, X, Y, W, H);
		const float Fill = H * FMath::Clamp(CupLevel, 0.0f, 1.0f);
		Rect(Paint, X, Y + H - Fill, W, Fill);
		if (Fill > 2.0f * S) Rect(FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), X, Y + H - Fill, W, 2.0f * S);   // the paint's top catching the light
		Rect(Steel, X + W * 0.35f, Y + H, W * 0.3f, 8.0f * S);              // the neck down to the gun
		Frame(bLit ? Accent : Steel, X, Y, W, H, bLit ? 3.0f * S : 2.0f * S);
	};
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
	const TArray<FName>& Finishes = UTorqueBuildSubsystem::Finishes();
	if (CupPick != INDEX_NONE && Paints.Num() > 0 && Finishes.Num() > 0)
	{
		// The picker, in the middle of the view.
		const int32 Which = CupPick;
		const int32 Colour = FMath::Clamp(PickColour[Which], 0, Paints.Num() - 1);
		const int32 Finish = FMath::Clamp(PickFinish[Which], 0, Finishes.Num() - 1);
		const float W = FMath::Min(900.0f * S, CW - 40.0f * S), H = 470.0f * S;
		const float X0 = (CW - W) * 0.5f, Y0 = (CH - H) * 0.5f;
		Rect(Panel, X0, Y0, W, H);
		Rect(Accent, X0, Y0, W, 3.0f * S);
		const float In = 24.0f * S;
		Text(FString::Printf(TEXT("SPRAY GUN  -  CUP %d"), Which + 1), X0 + In, Y0 + 16.0f * S, Px(24.0f), Accent);
		Text(Which == 0 ? FString(TEXT("Cup 1: the base colour of a fade")) : FString(TEXT("Cup 2: the colour a fade blends into")),
			X0 + In, Y0 + 18.0f * S, Px(15.0f), Dim, 2, W - 2.0f * In);
		// The cup, big, on the left: empty till Enter, then filling to the brim.
		const FLinearColor Pick = TWCI::Shown(Colour, Finishes[Finish]);
		const float CX = X0 + In + 20.0f * S, CY = Y0 + 110.0f * S, CWd = 150.0f * S, CHt = 230.0f * S;
		float CupLevel = 0.0f;
		if (PickFill >= 0.0) CupLevel = (float)FMath::Clamp(PickFill / TWCI::FillTime, 0.0, 1.0);
		else if (const FTorquePaintCup* Now = B->PaintBay().CupOf(B->Me(), Which))
		{
			int32 NowColour = 0, NowFinish = 0;
			if (TWCI::Picked(*Now, NowColour, NowFinish) && NowColour == Colour && NowFinish == Finish)
				CupLevel = FMath::Clamp(Now->Litres / TorquePaintMix::CupLitres, 0.0f, 1.0f);   // (already this, as full as it is)
		}
		Cup(CX, CY, CWd, CHt, Pick, CupLevel, PickFill >= 0.0);
		const FString Name = UTorqueBuildSubsystem::FinishName(Finishes[Finish]) + TEXT(" ") + UTorqueBuildSubsystem::PaintName(Colour).ToLower();
		Text(Name, CX - 20.0f * S, CY + CHt + 22.0f * S, Px(18.0f), FLinearColor::White, 1, CWd + 40.0f * S);
		if (PickFill >= 0.0)
			Text(PickFill >= TWCI::FillTime ? FString(TEXT("FULL")) : FString(TEXT("filling...")), CX - 20.0f * S, CY + CHt / 2.0f - 12.0f * S, Px(22.0f),
				FLinearColor::White, 1, CWd + 40.0f * S);
		// The colours, two rows of eight; the finishes under them.
		const float GX = X0 + In + 240.0f * S, GY = Y0 + 80.0f * S, GW = W - (GX - X0) - In;
		Text(TEXT("COLOUR"), GX, GY - 4.0f * S, Px(14.0f), Dim);
		Text(TEXT("left / right, wheel"), GX, GY - 4.0f * S, Px(13.0f), Dim, 2, GW);
		const int32 PerRow = 8;
		const float Gap = 8.0f * S, Sw = FMath::Min((GW - Gap * (PerRow - 1)) / PerRow, 64.0f * S);
		for (int32 I = 0; I < Paints.Num(); ++I)
		{
			const float SX = GX + (I % PerRow) * (Sw + Gap), SY = GY + 24.0f * S + (I / PerRow) * (Sw + Gap);
			Rect(TWCI::Shown(I, Finishes[Finish]), SX, SY, Sw, Sw);
			Frame(I == Colour ? FLinearColor::White : FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), SX - 3.0f * S, SY - 3.0f * S, Sw + 6.0f * S, Sw + 6.0f * S,
				I == Colour ? 3.0f * S : 1.0f * S);
		}
		const int32 Rows = (Paints.Num() + PerRow - 1) / PerRow;
		float FY = GY + 24.0f * S + Rows * (Sw + Gap) + 22.0f * S;
		Text(TEXT("FINISH"), GX, FY, Px(14.0f), Dim);
		Text(TEXT("up / down"), GX, FY, Px(13.0f), Dim, 2, GW);
		FY += 24.0f * S;
		const float FW = (GW - Gap * (Finishes.Num() - 1)) / Finishes.Num(), FH = 34.0f * S;
		for (int32 I = 0; I < Finishes.Num(); ++I)
		{
			const float FX = GX + I * (FW + Gap);
			Rect(I == Finish ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.35f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), FX, FY, FW, FH);
			Rect(TWCI::Shown(Colour, Finishes[I]), FX + 6.0f * S, FY + 6.0f * S, FH - 12.0f * S, FH - 12.0f * S);
			if (I == Finish) Frame(FLinearColor::White, FX, FY, FW, FH, 2.0f * S);
			Text(UTorqueBuildSubsystem::FinishName(Finishes[I]), FX + FH, FY + 8.0f * S, Px(13.0f), FLinearColor::White);
		}
		// The keys.
		Text(TEXT("Enter / click (Cross): fill the cup      [ / ] or Tab (L1 / R1): the other cup      Esc / right mouse (Circle): close"),
			X0 + In, Y0 + H - 34.0f * S, Px(13.0f), Dim, 1, W - 2.0f * In);
		return;
	}
	// The gun in hand: its two cups at the right of the view, as full as they are, and what's loaded.
	const float PW = 300.0f * S, PH = 230.0f * S;
	const float PX = CW - PW - 24.0f * S, PY = CH * 0.52f;
	Rect(Panel, PX, PY, PW, PH);
	Rect(Accent, PX, PY, PW, 3.0f * S);
	Text(TEXT("SPRAY GUN CUPS"), PX + 14.0f * S, PY + 10.0f * S, Px(14.0f), Accent);
	const bool bFade = IsFadeLoaded();
	int32 LitCup = INDEX_NONE;
	if (bFade && SprayItem != 0) LitCup = FadeStepFor(SprayItem);
	else if (Tool == ETorqueTool::Gun)
	{
		const FString Load = SprayFinishText();
		if (Load == TEXT("Cup 1")) LitCup = 0;
		else if (Load == TEXT("Cup 2")) LitCup = 1;
	}
	for (int32 Which = 0; Which < 2; ++Which)
	{
		const FTorquePaintCup* C = B->PaintBay().CupOf(B->Me(), Which);
		const float X = PX + 30.0f * S + Which * 140.0f * S, Y = PY + 48.0f * S;
		const float CupLevel = C ? FMath::Clamp(C->Litres / TorquePaintMix::CupLitres, 0.0f, 1.0f) : 0.0f;
		Cup(X, Y, 70.0f * S, 92.0f * S, C ? TWCI::CupColour(*C) : FLinearColor::Black, CupLevel, LitCup == Which);
		Text(FString::Printf(TEXT("%d  %s"), Which + 1, Which == 0 ? TEXT("[") : TEXT("]")), X, Y + 106.0f * S, Px(14.0f), LitCup == Which ? Accent : FLinearColor::White, 1, 70.0f * S);
		Text(C ? C->Spec.Name : FString(TEXT("empty")), X - 30.0f * S, Y + 126.0f * S, Px(12.0f), Dim, 1, 130.0f * S);
	}
	FString Load = TEXT("Load (T): ") + SprayFinishText();
	if (bFade)
		Load = FString::Printf(TEXT("Fade (R): %s"), *FString(TorquePaintMix::PatternName(TorquePaintMix::FirstFade
			+ FMath::Clamp(FadeStyle, 0, TorquePaintMix::TapePatterns - TorquePaintMix::FirstFade))).ToLower());
	Text(Load, PX + 14.0f * S, PY + PH - 30.0f * S, Px(13.0f), FLinearColor::White, 1, PW - 28.0f * S);
}
