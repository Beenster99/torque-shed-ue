// The spray gun's two cups (beenster, 19 Nov 2026: "when I hit [ it allows me to select the colour
// and finish for cup 1. I need to accept this colour somehow and I should be able to visibly see the
// full cup. When I select ] it allows me to select the colour and finish for cup 2"). [ and ] open
// the picker on a cup: the colour (left / right, the wheel) and the finish (up / down), Enter fills
// the cup to the brim and you watch it fill. With the gun in hand both cups stand at the side of the
// view, filled as they are, with what's loaded. A fade is then cup 1's base coat all over and cup 2
// over about half (TorqueWalkerSpray.cpp FinishFades).
//
// Worked with the mouse (beenster, 20 Nov 2026: "more mouse driven ... pick the cup, pick the colour
// from a colour picker (not the limited palette), then select finish, fill the cup, repeat for cup 2
// (maybe see a preview of the blend before starting)"): a tab for each cup, a colour square (richness
// across, brightness up) and hue strip with the gun's colours as quick picks, the finishes, a Fill
// button, and under them the fade as it would come out, cup 1 into cup 2, with the six fades to click
// (which loads Fade on the gun). The keys and the pad still work it.
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
#include "Math/Box2D.h"

namespace TorqueWalkerCupsImpl
{
// A colour as hue (0 .. 360), richness and brightness of its sRGB, and back.
FColor HsvRgb(const FVector3f& Hsv)
{
	const FLinearColor C = FLinearColor(FMath::Fmod(FMath::Max(Hsv.X, 0.0f), 360.0f), FMath::Clamp(Hsv.Y, 0.0f, 1.0f), FMath::Clamp(Hsv.Z, 0.0f, 1.0f)).HSVToLinearRGB();
	return FColor((uint8)FMath::RoundToInt(FMath::Clamp(C.R, 0.0f, 1.0f) * 255.0f), (uint8)FMath::RoundToInt(FMath::Clamp(C.G, 0.0f, 1.0f) * 255.0f),
		(uint8)FMath::RoundToInt(FMath::Clamp(C.B, 0.0f, 1.0f) * 255.0f), 255);
}

FVector3f RgbHsv(const FColor& C)
{
	const FLinearColor H = FLinearColor(C.R / 255.0f, C.G / 255.0f, C.B / 255.0f, 1.0f).LinearRGBToHSV();
	return FVector3f(H.R, H.G, H.B);
}

FColor PaletteSrgb(int32 I)
{
	const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
	return Paints.IsValidIndex(I) ? Paints[I].ToFColor(true) : FColor::White;
}

// The colour (linear) as its finish draws it (a matte darker, chrome near silver), for the swatches and cups.
FLinearColor Shown(const FLinearColor& Colour, FName Finish)
{
	FTorqueItem It;
	It.Colour = Colour;
	It.Finish = Finish;
	It.PaintQ = 1.0f;
	FLinearColor C = It.Colour;
	float Metal = 0.0f, Rough = 0.0f;
	UTorqueBuildSubsystem::FinishLook(It, C, Metal, Rough);
	C.A = 1.0f;
	return C;
}

FLinearColor ShownHsv(const FVector3f& Hsv, FName Finish) { return Shown(FLinearColor::FromSRGBColor(HsvRgb(Hsv)), Finish); }

// A cup picked here keeps its colour and finish in its code ("P3:matte", or "C1A2B3C:matte" for any
// colour): read them back.
bool Picked(const FTorquePaintCup& Cup, FVector3f& OutHsv, int32& OutFinish)
{
	FString Col, Fin;
	if (!Cup.Spec.Code.Split(TEXT(":"), &Col, &Fin)) return false;
	if (Col.StartsWith(TEXT("P"))) OutHsv = RgbHsv(PaletteSrgb(FMath::Clamp(FCString::Atoi(*Col.Mid(1)), 0, UTorqueBuildSubsystem::Paints().Num() - 1)));
	else if (Col.StartsWith(TEXT("C")) && Col.Len() == 7) OutHsv = RgbHsv(FColor::FromHex(Col.Mid(1)));
	else return false;
	OutFinish = FMath::Max(UTorqueBuildSubsystem::Finishes().IndexOfByKey(FName(*Fin)), 0);
	return true;
}

FLinearColor CupColour(const FTorquePaintCup& Cup)
{
	FVector3f Hsv;
	int32 Finish = 0;
	if (Picked(Cup, Hsv, Finish)) return ShownHsv(Hsv, UTorqueBuildSubsystem::Finishes()[Finish]);
	FLinearColor C = Cup.Spec.IsLacquer() ? FLinearColor(0.85f, 0.85f, 0.8f, 1.0f) : Cup.Spec.Colour;
	C.A = 1.0f;
	return C;
}

// The gun colour nearest a picked one (for stepping through them with the keys).
int32 NearestPaint(const FVector3f& Hsv)
{
	const FColor C = HsvRgb(Hsv);
	int32 Best = 0, BestD = MAX_int32;
	for (int32 I = 0; I < UTorqueBuildSubsystem::Paints().Num(); ++I)
	{
		const FColor P = PaletteSrgb(I);
		const int32 D = FMath::Square((int32)P.R - (int32)C.R) + FMath::Square((int32)P.G - (int32)C.G) + FMath::Square((int32)P.B - (int32)C.B);
		if (D < BestD)
		{
			BestD = D;
			Best = I;
		}
	}
	return Best;
}

constexpr double FillTime = 0.9;    // the cup filling, seconds
constexpr double FullHold = 0.6;    // ... then shown full a moment before the picker shuts
constexpr int32 SwatchRow = 8;      // the gun's colours, eight to a row
constexpr int32 FadeCount = TorquePaintMix::TapePatterns - TorquePaintMix::FirstFade + 1;

// Where the picker's parts are on a screen ClipW x ClipH (the drawing and the mouse both use it).
struct FLayout
{
	float S = 1.0f, X0 = 0.0f, Y0 = 0.0f, W = 0.0f, H = 0.0f, In = 0.0f;
	FBox2D Tab[2], Close, CupBox, Fill, Square, Hue, Preview;
	TArray<FBox2D> Swatches, FinishBox, Fades;
};

FBox2D Box(float X, float Y, float W, float H) { return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)); }

FLayout Layout(float ClipW, float ClipH)
{
	FLayout L;
	const float S = ClipH / 1080.0f;
	L.S = S;
	L.W = FMath::Min(1180.0f * S, ClipW - 40.0f * S);
	L.H = FMath::Min(740.0f * S, ClipH - 40.0f * S);
	L.X0 = (ClipW - L.W) * 0.5f;
	L.Y0 = (ClipH - L.H) * 0.5f;
	L.In = 24.0f * S;
	const float X0 = L.X0, Y0 = L.Y0, In = L.In;
	L.Tab[0] = Box(X0 + In, Y0 + 58.0f * S, 210.0f * S, 44.0f * S);
	L.Tab[1] = Box(X0 + In + 222.0f * S, Y0 + 58.0f * S, 210.0f * S, 44.0f * S);
	L.Close = Box(X0 + L.W - In - 130.0f * S, Y0 + 16.0f * S, 130.0f * S, 38.0f * S);
	L.CupBox = Box(X0 + In + 30.0f * S, Y0 + 150.0f * S, 150.0f * S, 230.0f * S);
	L.Fill = Box(X0 + In, Y0 + 150.0f * S + 230.0f * S + 62.0f * S, 210.0f * S, 54.0f * S);
	const float GX = X0 + In + 262.0f * S, GY = Y0 + 150.0f * S, SqW = 300.0f * S, SqH = 230.0f * S;
	L.Square = Box(GX, GY, SqW, SqH);
	L.Hue = Box(GX + SqW + 14.0f * S, GY, 28.0f * S, SqH);
	const float RowW = SqW + 42.0f * S, Gap = 6.0f * S, Sw = (RowW - Gap * (SwatchRow - 1)) / SwatchRow;
	for (int32 I = 0; I < UTorqueBuildSubsystem::Paints().Num(); ++I)
		L.Swatches.Add(Box(GX + (I % SwatchRow) * (Sw + Gap), GY + SqH + 18.0f * S + (I / SwatchRow) * (Sw + Gap), Sw, Sw));
	const float FX = GX + RowW + 36.0f * S, FW = X0 + L.W - In - FX;
	for (int32 I = 0; I < UTorqueBuildSubsystem::Finishes().Num(); ++I)
		L.FinishBox.Add(Box(FX, GY + I * 52.0f * S, FW, 44.0f * S));
	// The fade: the preview on the left, the six fades to click on the right (front to back, then top to bottom).
	const float PY = Y0 + L.H - 168.0f * S;
	const float PW = (L.W - 2.0f * In) * 0.42f;
	L.Preview = Box(X0 + In, PY + 26.0f * S, PW, 96.0f * S);
	const float BX = X0 + In + PW + 30.0f * S, BW = (X0 + L.W - In - BX - 2.0f * 8.0f * S) / 3.0f;
	for (int32 I = 0; I < FadeCount; ++I)
		L.Fades.Add(Box(BX + (I % 3) * (BW + 8.0f * S), PY + 26.0f * S + (I / 3) * 52.0f * S, BW, 44.0f * S));
	return L;
}

const TCHAR* FadeLabel(int32 Style)
{
	static const TCHAR* const Names[] = {TEXT("Front to back, short"), TEXT("Front to back, medium"), TEXT("Front to back, long"),
		TEXT("Top to bottom, short"), TEXT("Top to bottom, medium"), TEXT("Top to bottom, long")};
	return Names[FMath::Clamp(Style, 0, (int32)UE_ARRAY_COUNT(Names) - 1)];
}
} // namespace TorqueWalkerCupsImpl

namespace TWCI = TorqueWalkerCupsImpl;

void ATorqueWalker::OpenCupPicker(int32 Which)
{
	const bool bWasShut = CupPick == INDEX_NONE;
	CupPick = FMath::Clamp(Which, 0, 1);
	PickFill = -1.0;
	CupDrag = 0;
	// Starts on what's in each cup, if it was picked here.
	if (bWasShut)
		if (const UTorqueBuildSubsystem* B = Shed())
			for (int32 C = 0; C < 2; ++C)
				if (const FTorquePaintCup* Cup = B->PaintBay().CupOf(B->Me(), C)) TWCI::Picked(*Cup, PickHsv[C], PickFinish[C]);
	// The pointer, for the mouse to work it.
	if (APlayerController* PC = Cast<APlayerController>(GetController()); PC && bWasShut)
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("switch"), Camera->GetComponentLocation(), -10.0f);
}

void ATorqueWalker::CloseCupPicker()
{
	CupPick = INDEX_NONE;
	PickFill = -1.0;
	CupDrag = 0;
	ComputerClosedFrame = GFrameCounter;   // (the Esc that shut it isn't the pause menu's too)
	bWaitRelease = true;                   // (the click that filled it doesn't also work or spray)
	bSprayNeedRelease = true;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ATorqueWalker::FillPickedCup()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B || CupPick == INDEX_NONE || PickFill >= 0.0) return;
	const TArray<FName>& Finishes = UTorqueBuildSubsystem::Finishes();
	const FColor Rgb = TWCI::HsvRgb(PickHsv[CupPick]);
	// One of the gun's own colours goes by its number (its name), any other by its sRGB.
	int32 Colour = UTorqueBuildSubsystem::OwnColour(Rgb);
	for (int32 I = 0; I < UTorqueBuildSubsystem::Paints().Num(); ++I)
		if (TWCI::PaletteSrgb(I) == Rgb) Colour = I;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::PaintCup;
	R.Item = CupPick;
	R.Colour = Colour;
	R.Finish = Finishes[FMath::Clamp(PickFinish[CupPick], 0, Finishes.Num() - 1)];
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		Say(WhyNot, false);
		return;
	}
	PickFill = 0.0;
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("pour"), Camera->GetComponentLocation(), -8.0f);
}

void ATorqueWalker::CupPickerInput(APlayerController* PC, uint32 SonyHit, float Dt)
{
	if (!PC || CupPick == INDEX_NONE) return;
	// Filling: watch it go in, then it goes on to the other cup's tab (or shuts, both done).
	if (PickFill >= 0.0)
	{
		PickFill += Dt;
		if (PickFill >= TWCI::FillTime + TWCI::FullHold)
		{
			PickFill = -1.0;
			const UTorqueBuildSubsystem* B = Shed();
			const FTorquePaintCup* Other = B ? B->PaintBay().CupOf(B->Me(), 1 - CupPick) : nullptr;
			if (CupPick == 0 && (!Other || Other->Litres <= 0.0f)) CupPick = 1;   // (cup 1 done: cup 2 next)
		}
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
	FVector3f& Hsv = PickHsv[CupPick];
	int32& Finish = PickFinish[CupPick];
	// The mouse: the tabs, the colour square and strip, the gun's colours, the finishes, Fill, the fades.
	float Mx = 0.0f, My = 0.0f;
	int32 VW = 0, VH = 0;
	PC->GetViewportSize(VW, VH);
	if (PC->GetMousePosition(Mx, My) && VW > 0 && VH > 0)
	{
		const FVector2D M(Mx, My);
		const TWCI::FLayout L = TWCI::Layout((float)VW, (float)VH);
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) CupDrag = 0;
		if (Just(EKeys::LeftMouseButton))
		{
			if (L.Close.IsInside(M))
			{
				CloseCupPicker();
				return;
			}
			for (int32 C = 0; C < 2; ++C)
				if (L.Tab[C].IsInside(M) && C != CupPick)
				{
					OpenCupPicker(C);
					return;
				}
			if (L.Fill.IsInside(M) || L.CupBox.IsInside(M))
			{
				FillPickedCup();
				return;
			}
			if (L.Square.IsInside(M)) CupDrag = 1;
			else if (L.Hue.IsInside(M)) CupDrag = 2;
			for (int32 I = 0; I < L.Swatches.Num(); ++I)
				if (L.Swatches[I].IsInside(M)) Hsv = TWCI::RgbHsv(TWCI::PaletteSrgb(I));
			for (int32 I = 0; I < L.FinishBox.Num(); ++I)
				if (L.FinishBox[I].IsInside(M)) Finish = I;
			for (int32 I = 0; I < L.Fades.Num(); ++I)
				if (L.Fades[I].IsInside(M))
				{
					// A fade picked: it's what the gun sprays next (its Fade load).
					FadeStyle = I;
					LoadFade();
					Say(FString::Printf(TEXT("Fade on the gun: %s. Spray cup 1 all over, then cup 2 over about half."), TWCI::FadeLabel(I)), true, 3.0);
				}
		}
		if (CupDrag == 1)
		{
			const FVector2D Sz = L.Square.GetSize();
			Hsv.Y = FMath::Clamp((float)((M.X - L.Square.Min.X) / FMath::Max(Sz.X, 1.0)), 0.0f, 1.0f);
			Hsv.Z = FMath::Clamp(1.0f - (float)((M.Y - L.Square.Min.Y) / FMath::Max(Sz.Y, 1.0)), 0.0f, 1.0f);
		}
		else if (CupDrag == 2)
			Hsv.X = FMath::Clamp((float)((M.Y - L.Hue.Min.Y) / FMath::Max(L.Hue.GetSize().Y, 1.0)), 0.0f, 0.9999f) * 360.0f;
	}
	// The other cup: [ or ] (Tab, or L1 / R1 on a pad).
	if (KeyJust(ETorqueKey::SprayCup1)) OpenCupPicker(0);
	else if (KeyJust(ETorqueKey::SprayCup2)) OpenCupPicker(1);
	else if (Just(EKeys::Tab) || Just(EKeys::Gamepad_LeftShoulder) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::L1)
		|| (SonyHit & ESonyButton::R1))
		OpenCupPicker(1 - CupPick);
	if (CupPick == INDEX_NONE) return;
	FVector3f& Now = PickHsv[CupPick];
	int32& NowFinish = PickFinish[CupPick];
	// The gun's colours in turn: left / right, A / D, the wheel or Q / E (the D-pad's left / right).
	if (Just(EKeys::Right) || Just(EKeys::D) || KeyJust(ETorqueKey::NextTool) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight))
		Now = TWCI::RgbHsv(TWCI::PaletteSrgb((TWCI::NearestPaint(Now) + 1) % Colours));
	if (Just(EKeys::Left) || Just(EKeys::A) || KeyJust(ETorqueKey::PrevTool) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft))
		Now = TWCI::RgbHsv(TWCI::PaletteSrgb((TWCI::NearestPaint(Now) + Colours - 1) % Colours));
	// The finish: up / down, W / S (the D-pad's up / down).
	if (Just(EKeys::Down) || Just(EKeys::S) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) NowFinish = (NowFinish + 1) % Finishes;
	if (Just(EKeys::Up) || Just(EKeys::W) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) NowFinish = (NowFinish + Finishes - 1) % Finishes;
	NowFinish = FMath::Clamp(NowFinish, 0, Finishes - 1);
	// Fill it: Enter or Space (Cross on a pad), or the Fill button.
	if (Just(EKeys::Enter) || Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross)) FillPickedCup();
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
		// The picker, in the middle of the view: worked with the mouse (the keys and pad too).
		const int32 Which = CupPick;
		const TWCI::FLayout L = TWCI::Layout(CW, CH);
		const float X0 = L.X0, Y0 = L.Y0, W = L.W, H = L.H, In = L.In;
		const int32 Finish = FMath::Clamp(PickFinish[Which], 0, Finishes.Num() - 1);
		const FVector3f Hsv = PickHsv[Which];
		float MouseX = -1.0f, MouseY = -1.0f;
		if (const APlayerController* PC = Cast<APlayerController>(GetController())) PC->GetMousePosition(MouseX, MouseY);
		const FVector2D Mouse(MouseX, MouseY);
		auto BoxRect = [&Rect](const FBox2D& Bx, const FLinearColor& C) { Rect(C, Bx.Min.X, Bx.Min.Y, Bx.GetSize().X, Bx.GetSize().Y); };
		auto BoxFrame = [&Frame](const FBox2D& Bx, const FLinearColor& C, float T) { Frame(C, Bx.Min.X, Bx.Min.Y, Bx.GetSize().X, Bx.GetSize().Y, T); };
		// A button: lit when on, lighter under the mouse.
		auto Button = [&](const FBox2D& Bx, const FString& Label, bool bOn, float Size)
		{
			const bool bHot = Bx.IsInside(Mouse);
			BoxRect(Bx, bOn ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.85f) : FLinearColor(1.0f, 1.0f, 1.0f, bHot ? 0.18f : 0.08f));
			if (bHot && !bOn) BoxFrame(Bx, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), 1.0f * S);
			Text(Label, Bx.Min.X, Bx.Min.Y + 0.5f * Bx.GetSize().Y - Px(Size) * Em * 0.62f, Px(Size), bOn ? FLinearColor(0.05f, 0.04f, 0.03f, 1.0f) : FLinearColor::White, 1,
				Bx.GetSize().X);
		};
		Rect(Panel, X0, Y0, W, H);
		Rect(Accent, X0, Y0, W, 3.0f * S);
		Text(TEXT("SPRAY GUN CUPS"), X0 + In, Y0 + 16.0f * S, Px(24.0f), Accent);
		Button(L.Close, TEXT("Close"), false, 15.0f);
		// The cups' tabs, each with its cup as it is now.
		for (int32 C = 0; C < 2; ++C)
		{
			const FTorquePaintCup* InCup = B->PaintBay().CupOf(B->Me(), C);
			Button(L.Tab[C], FString::Printf(TEXT("    CUP %d  %s"), C + 1, InCup && InCup->Litres > 0.0f ? TEXT("(full)") : TEXT("(empty)")), C == Which, 16.0f);
			Rect(InCup ? TWCI::CupColour(*InCup) : FLinearColor(0.1f, 0.1f, 0.1f, 1.0f), L.Tab[C].Min.X + 10.0f * S, L.Tab[C].Min.Y + 9.0f * S, 18.0f * S, 26.0f * S);
		}
		Text(Which == 0 ? FString(TEXT("Cup 1: the base colour of a fade")) : FString(TEXT("Cup 2: the colour a fade blends into")),
			L.Tab[1].Max.X + 24.0f * S, L.Tab[1].Min.Y + 12.0f * S, Px(15.0f), Dim);
		// The cup, big, on the left: empty till Fill, then filling to the brim.
		const FLinearColor Pick = TWCI::ShownHsv(Hsv, Finishes[Finish]);
		const float CX = L.CupBox.Min.X, CY = L.CupBox.Min.Y, CWd = L.CupBox.GetSize().X, CHt = L.CupBox.GetSize().Y;
		float CupLevel = 0.0f;
		if (PickFill >= 0.0) CupLevel = (float)FMath::Clamp(PickFill / TWCI::FillTime, 0.0, 1.0);
		else if (const FTorquePaintCup* InNow = B->PaintBay().CupOf(B->Me(), Which))
		{
			FVector3f NowHsv;
			int32 NowFinish = 0;
			if (TWCI::Picked(*InNow, NowHsv, NowFinish) && TWCI::HsvRgb(NowHsv) == TWCI::HsvRgb(Hsv) && NowFinish == Finish)
				CupLevel = FMath::Clamp(InNow->Litres / TorquePaintMix::CupLitres, 0.0f, 1.0f);   // (already this, as full as it is)
		}
		// The colour picked shows faint in the empty cup, so you see what's going in.
		Rect(FLinearColor(Pick.R, Pick.G, Pick.B, 0.25f), CX, CY, CWd, CHt);
		Cup(CX, CY, CWd, CHt, Pick, CupLevel, PickFill >= 0.0);
		const FString Name = UTorqueBuildSubsystem::FinishName(Finishes[Finish]) + TEXT(" ") + UTorqueBuildSubsystem::PaintName(TWCI::NearestPaint(Hsv)).ToLower();
		Text(Name, CX - 30.0f * S, CY + CHt + 22.0f * S, Px(16.0f), FLinearColor::White, 1, CWd + 60.0f * S);
		if (PickFill >= 0.0)
			Text(PickFill >= TWCI::FillTime ? FString(TEXT("FULL")) : FString(TEXT("filling...")), CX - 20.0f * S, CY + CHt / 2.0f - 12.0f * S, Px(22.0f),
				FLinearColor::White, 1, CWd + 40.0f * S);
		Button(L.Fill, FString::Printf(TEXT("FILL CUP %d"), Which + 1), PickFill < 0.0 && CupLevel < 0.99f, 20.0f);
		// The colour square: richness across, brightness up, at the hue picked; the hue strip beside it.
		{
			constexpr int32 Nx = 30, Ny = 23;
			const float SqX = L.Square.Min.X, SqY = L.Square.Min.Y, SqW = L.Square.GetSize().X, SqH = L.Square.GetSize().Y;
			for (int32 Ix = 0; Ix < Nx; ++Ix)
				for (int32 Iy = 0; Iy < Ny; ++Iy)
				{
					const FVector3f Cell(Hsv.X, (Ix + 0.5f) / Nx, 1.0f - (Iy + 0.5f) / Ny);
					Rect(FLinearColor::FromSRGBColor(TWCI::HsvRgb(Cell)), SqX + SqW * Ix / Nx, SqY + SqH * Iy / Ny, SqW / Nx + 1.0f, SqH / Ny + 1.0f);
				}
			const float Kx = SqX + SqW * FMath::Clamp(Hsv.Y, 0.0f, 1.0f), Ky = SqY + SqH * (1.0f - FMath::Clamp(Hsv.Z, 0.0f, 1.0f));
			Frame(FLinearColor::Black, Kx - 8.0f * S, Ky - 8.0f * S, 16.0f * S, 16.0f * S, 2.0f * S);
			Frame(FLinearColor::White, Kx - 6.0f * S, Ky - 6.0f * S, 12.0f * S, 12.0f * S, 2.0f * S);
			constexpr int32 Nh = 48;
			const float HX = L.Hue.Min.X, HY = L.Hue.Min.Y, HW = L.Hue.GetSize().X, HH = L.Hue.GetSize().Y;
			for (int32 I = 0; I < Nh; ++I)
				Rect(FLinearColor::FromSRGBColor(TWCI::HsvRgb(FVector3f(360.0f * (I + 0.5f) / Nh, 1.0f, 1.0f))), HX, HY + HH * I / Nh, HW, HH / Nh + 1.0f);
			const float Hy = HY + HH * FMath::Fmod(FMath::Max(Hsv.X, 0.0f), 360.0f) / 360.0f;
			Rect(FLinearColor::Black, HX - 4.0f * S, Hy - 3.0f * S, HW + 8.0f * S, 6.0f * S);
			Rect(FLinearColor::White, HX - 3.0f * S, Hy - 1.0f * S, HW + 6.0f * S, 2.0f * S);
			Text(TEXT("COLOUR  (drag in the square and the strip)"), SqX, SqY - 26.0f * S, Px(14.0f), Dim);
		}
		// The gun's own colours, as quick picks.
		{
			const FColor Rgb = TWCI::HsvRgb(Hsv);
			for (int32 I = 0; I < L.Swatches.Num(); ++I)
			{
				const FBox2D& Sb = L.Swatches[I];
				BoxRect(Sb, TWCI::Shown(Paints[I], Finishes[Finish]));
				const bool bOn = TWCI::PaletteSrgb(I) == Rgb;
				BoxFrame(Sb, bOn ? FLinearColor::White : (Sb.IsInside(Mouse) ? FLinearColor(1.0f, 1.0f, 1.0f, 0.6f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.6f)),
					bOn ? 3.0f * S : 1.0f * S);
			}
		}
		// The finishes.
		if (L.FinishBox.Num() > 0)
		{
			Text(TEXT("FINISH"), L.FinishBox[0].Min.X, L.FinishBox[0].Min.Y - 26.0f * S, Px(14.0f), Dim);
			for (int32 I = 0; I < L.FinishBox.Num() && I < Finishes.Num(); ++I)
			{
				const FBox2D& Fb = L.FinishBox[I];
				Button(Fb, TEXT("      ") + UTorqueBuildSubsystem::FinishName(Finishes[I]), I == Finish, 15.0f);
				Rect(TWCI::ShownHsv(Hsv, Finishes[I]), Fb.Min.X + 8.0f * S, Fb.Min.Y + 8.0f * S, Fb.GetSize().Y - 16.0f * S, Fb.GetSize().Y - 16.0f * S);
			}
		}
		// The fade as it would come out: cup 1 (as picked) blending into cup 2, the way the fade picked
		// goes (front to back across the box, or top to bottom down it), on a part about 60 cm long.
		{
			const int32 Style = FMath::Clamp(FadeStyle, 0, TWCI::FadeCount - 1);
			const int32 Pattern = TorquePaintMix::FirstFade + Style;
			const bool bAlong = TorquePaintMix::FadeAlong(Pattern);
			const FLinearColor One = TWCI::ShownHsv(PickHsv[0], Finishes[FMath::Clamp(PickFinish[0], 0, Finishes.Num() - 1)]);
			const FLinearColor Two = TWCI::ShownHsv(PickHsv[1], Finishes[FMath::Clamp(PickFinish[1], 0, Finishes.Num() - 1)]);
			const FBox2D& Pb = L.Preview;
			const float PX0 = Pb.Min.X, PY0 = Pb.Min.Y, PWd = Pb.GetSize().X, PHt = Pb.GetSize().Y;
			Text(FString::Printf(TEXT("FADE PREVIEW  -  %s"), TWCI::FadeLabel(Style)), PX0, PY0 - 26.0f * S, Px(14.0f), Dim);
			constexpr int32 Strips = 72;
			for (int32 I = 0; I < Strips; ++I)
			{
				const float T = (I + 0.5f) / Strips;
				// (cm from the middle: x forward, so the box's left is the front; z up, so its top is the top)
				const float Keep = bAlong ? TorquePaintMix::FadeKeep(Pattern, (0.5f - T) * 60.0f, 0.0f) : TorquePaintMix::FadeKeep(Pattern, 0.0f, (0.5f - T) * 30.0f);
				const FLinearColor C = FMath::Lerp(Two, One, Keep);
				if (bAlong) Rect(C, PX0 + PWd * I / Strips, PY0, PWd / Strips + 1.0f, PHt);
				else Rect(C, PX0, PY0 + PHt * I / Strips, PWd, PHt / Strips + 1.0f);
			}
			Frame(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), PX0, PY0, PWd, PHt, 1.0f * S);
			Text(bAlong ? FString(TEXT("front                                    back")) : FString(TEXT("top  /  bottom")), PX0, PY0 + PHt + 6.0f * S, Px(12.0f), Dim, 1, PWd);
			if (L.Fades.Num() > 0) Text(TEXT("FADE  (click one: it goes on the gun)"), L.Fades[0].Min.X, L.Fades[0].Min.Y - 26.0f * S, Px(14.0f), Dim);
			const bool bLoaded = IsFadeLoaded();
			for (int32 I = 0; I < L.Fades.Num(); ++I) Button(L.Fades[I], TWCI::FadeLabel(I), bLoaded && I == Style, 13.0f);
		}
		// The keys.
		Text(TEXT("[ / ] or Tab (L1 / R1): the other cup      left / right: the gun's colours      up / down: finish      Enter (Cross): fill      Esc (Circle): close"),
			X0 + In, Y0 + H - 30.0f * S, Px(12.0f), Dim, 1, W - 2.0f * In);
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
