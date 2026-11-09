// The walker at the paint mixing bench (Torque Shed's PaintBench, paint_bench.gd) and carrying a
// tin from the mixing bay: ATorqueWalker's bench mode, its HUD lines and the test hooks.
#include "Shed/TorqueWalker.h"
#include "Coop/TorquePlayerController.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Shed/TorqueShedLayout.h"

namespace TorqueWalkerBenchImpl
{
constexpr float SendEvery = 0.15f;       // paint_bench.gd SEND_EVERY
constexpr float TiltPerSecond = 0.38f;   // the bottle tips further the longer the button's held
constexpr float TurnsPerSecond = 1.5f;   // stirring, round and round
const TCHAR* const PageNames[] = {TEXT("Mix"), TEXT("Recipes"), TEXT("Supplies")};
constexpr int32 Pages = 3;

FColor Shown(const FLinearColor& C)
{
	// Torque Shed's sRGB numbers, as they are.
	return FColor((uint8)FMath::RoundToInt(FMath::Clamp(C.R, 0.0f, 1.0f) * 255.0f), (uint8)FMath::RoundToInt(FMath::Clamp(C.G, 0.0f, 1.0f) * 255.0f),
		(uint8)FMath::RoundToInt(FMath::Clamp(C.B, 0.0f, 1.0f) * 255.0f));
}
}

namespace TWBI = TorqueWalkerBenchImpl;

// --- Stepping up to it and away ------------------------------------------------------------------------

void ATorqueWalker::OpenBench()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::PaintBench;
	R.Item = 1;
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		Say(WhyNot, false);
		return;
	}
	bBench = true;
	BenchPage = 0;
	BenchRow = 0;
	BenchName.Reset();
	bNaming = bMousePour = bStirTrack = false;
	BenchDragTilt = 0.0f;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// The card's worked with the mouse (paint_bench.gd).
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	BenchTilt = BenchPourHeld = PourUnsent = StirUnsent = BenchSendIn = 0.0f;
	PouringTint = INDEX_NONE;
	Progress = 0.0;
	// Stand at the bench and look down at the scale (the bench camera's view).
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Stand = TorquePaintBayLayout::BenchStand;
	SetActorLocation(FVector(Stand.X, Stand.Y, Half + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Eye = Camera ? Camera->GetComponentLocation() : FVector(Stand.X, Stand.Y, 160.0);
	if (AController* C = GetController()) C->SetControlRotation((TorquePaintBayLayout::ScaleAt + FVector(20.0, 0.0, 10.0) - Eye).Rotation());
	ATorquePlayerController::PlaceOnHost(this, GetActorLocation(), GetControlRotation());   // [co-op] a joined copy: the host too
	Doing(TEXT("mixing paint at the bench"), 30.0);
}

void ATorqueWalker::CloseBench()
{
	if (!bBench) return;
	FlushBench(true);
	bBench = false;
	bNaming = bMousePour = bStirTrack = false;
	ComputerClosedFrame = GFrameCounter;   // (Esc closing it doesn't open the pause menu too)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
	if (UTorqueBuildSubsystem* B = Shed())
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintBench;
		R.Item = 0;
		FString WhyNot;
		B->Request(R, WhyNot);
	}
	Doing(FString(), 0.0);
}

bool ATorqueWalker::BenchAsk(const FTorqueShedRequest& R)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	FString WhyNot;
	if (B->Request(R, WhyNot)) return true;
	Say(WhyNot, false);
	return false;
}

// --- The rows -------------------------------------------------------------------------------------------

TArray<ATorqueWalker::FBenchRow> ATorqueWalker::BenchRows() const
{
	namespace TPM = TorquePaintMix;
	TArray<FBenchRow> Rows;
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return Rows;
	const FTorquePaintBay& P = B->PaintBay();
	auto Row = [&Rows](ETorqueShedAct Kind, int32 Item, int32 Colour, const FString& Text)
	{
		FBenchRow R;
		R.Kind = Kind;
		R.Item = Item;
		R.Colour = Colour;
		R.Text = Text;
		Rows.Add(R);
	};
	if (BenchPage == 0)
	{
		const FTorqueTin* Tin = P.BenchTinPtr();
		if (!Tin)
		{
			for (int32 I = 0; I < TPM::Bases().Num(); ++I)
				Row(ETorqueShedAct::PaintTinNew, 0, I, FString::Printf(TEXT("A new tin: a litre of %s (%.1f l on the shelf)"),
					*FString(TPM::Bases()[I].Name).ToLower(), P.StockOf(FTorquePaintBay::BaseStock(I))));
			return Rows;
		}
		// The rack's bottles that can go in this tin, the recipe card's weight against each.
		for (int32 I = 0; I < TPM::Tints().Num(); ++I)
		{
			const bool bFlattener = TPM::Tints()[I].Kind == TPM::ETintKind::Flattener;
			if (bFlattener != TPM::IsLacquerBase(Tin->Mix.Base)) continue;
			FString Text = FString::Printf(TEXT("Pour %s: %s g in the tin, %d g in the bottle"), TPM::Tints()[I].Name,
				*TPM::GramsText(Tin->Mix.GramsOf(I)), FMath::RoundToInt(P.StockOf(FTorquePaintBay::TintStock(I))));
			if (P.CardRecipe != 0 && P.CardMix.GramsOf(I) > 0.0f) Text += FString::Printf(TEXT("   (card: %s g)"), *TPM::GramsText(P.CardMix.GramsOf(I)));
			Row(ETorqueShedAct::PaintPour, 0, I, Text);
		}
		Row(ETorqueShedAct::PaintTare, 0, 0, TEXT("Tare: zero the scale, to weigh the next tint on its own"));
		Row(ETorqueShedAct::PaintAddBase, 0, 0, FString::Printf(TEXT("Add %d g more base (to fix an over-pour)"), FMath::RoundToInt(TPM::AddBaseGrams)));
		Row(ETorqueShedAct::PaintTest, 0, 0, TEXT("Spray a test card (the spray-out stand, in the daylight booth)"));
		Row(ETorqueShedAct::PaintShake, 0, 0, FString::Printf(TEXT("Into the shaker (%d s: an even mix)"), FMath::RoundToInt(FTorquePaintBay::ShakeTime)));
		Row(ETorqueShedAct::PaintTinSave, 0, 0, FString::Printf(TEXT("Save it as \"Mix %d\": onto the tin shelf and the swatch wall"), P.Mixed + 1));
		Row(ETorqueShedAct::PaintTinEmpty, 0, 0, TEXT("Tip it away"));
	}
	else if (BenchPage == 1)
	{
		if (P.bTarget) Row(ETorqueShedAct::PaintTarget, 0, 0, FString::Printf(TEXT("Put away the card (%s)"), *P.TargetName));
		for (const FTorquePaintRecipe* Card : P.Swatches())
			Row(ETorqueShedAct::PaintRecipe, Card->Id, 0, FString::Printf(TEXT("%s   %s"), *Card->Name, *Card->Code));
		if (Rows.Num() == 0) Row(ETorqueShedAct::Count, 0, 0, TEXT("No recipe cards yet: save a mix and its card goes on the swatch wall."));
	}
	else
	{
		for (int32 I = 0; I < P.Stock.Num(); ++I)
			Row(ETorqueShedAct::PaintBuy, 1, I, FString::Printf(TEXT("Restock %s (a %s)"), *P.StockText(I),
				I < TPM::Bases().Num() ? TEXT("1 litre tin") : TEXT("250 g bottle")));
	}
	return Rows;
}

// --- The keys -------------------------------------------------------------------------------------------

void ATorqueWalker::BenchInput(float Dt, int32 RowStep, int32 PageStep, bool bChoose, bool bWorkDown, bool bAltDown, bool bClose)
{
	namespace TPM = TorquePaintMix;
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	if (!bWorkDown && !bAltDown) bWaitRelease = false;
	if (bClose)
	{
		CloseBench();
		bWaitRelease = true;
		return;
	}
	const FTorquePaintBay& P = B->PaintBay();
	if (P.BenchBy != 0 && P.BenchBy != B->Me())
	{
		// Someone else has the bench (the host had them at it first).
		bBench = false;
		return;
	}
	if (PouringTint == INDEX_NONE)
	{
		if (PageStep != 0)
		{
			BenchPage = (BenchPage + PageStep + TWBI::Pages) % TWBI::Pages;
			BenchRow = 0;
		}
	}
	const TArray<FBenchRow> Rows = BenchRows();
	if (Rows.Num() == 0) return;
	if (PouringTint == INDEX_NONE) BenchRow = (FMath::Clamp(BenchRow, 0, Rows.Num() - 1) + RowStep + Rows.Num()) % Rows.Num();
	if (RowStep != 0) BenchPanel.Show(BenchRow);
	const FBenchRow& Picked = Rows[FMath::Clamp(BenchRow, 0, Rows.Num() - 1)];

	// Pouring: hold the work button on a tint; the bottle tips further the longer it's held.
	if (Picked.Kind == ETorqueShedAct::PaintPour && bWorkDown && !bWaitRelease && P.BenchTinPtr())
	{
		if (PouringTint != Picked.Colour)
		{
			FlushBench(PouringTint != INDEX_NONE);
			PouringTint = Picked.Colour;
			BenchPourHeld = 0.0f;
			const FString Why = TPM::PourProblem(P.BenchTinPtr()->Mix, Picked.Colour);
			if (!Why.IsEmpty())
			{
				Say(Why, false);
				PouringTint = INDEX_NONE;
				bWaitRelease = true;
			}
		}
		if (PouringTint != INDEX_NONE)
		{
			BenchPourHeld += Dt;
			BenchTilt = FMath::Clamp(FMath::Max(0.15f + TWBI::TiltPerSecond * BenchPourHeld, BenchDragTilt), 0.0f, 1.0f);
			const float Left = P.StockOf(FTorquePaintBay::TintStock(PouringTint)) - PourUnsent;
			PourUnsent += FMath::Max(0.0f, FMath::Min(TPM::PourRate(BenchTilt) * Dt, Left));
		}
	}
	else if (PouringTint != INDEX_NONE)
	{
		FlushBench(true);   // let go: the bottle back on the rack
		BenchTilt = 0.0f;
	}
	else if (bChoose) DoBenchRow(Picked, false);
	// Stirring: hold R (or the right mouse, Square) and the stick goes round.
	if (bAltDown && P.BenchTinPtr() && BenchPage == 0) StirUnsent += TPM::StirPerTurn * TWBI::TurnsPerSecond * Dt;
	else if (bAltDown && BenchPage == 1 && Picked.Kind == ETorqueShedAct::PaintRecipe && !bWaitRelease)
	{
		DoBenchRow(Picked, true);   // on a card: match its colour by eye (no weights)
		bWaitRelease = true;
	}
	BenchSendIn -= Dt;
	if (BenchSendIn <= 0.0f) FlushBench(false);
}

void ATorqueWalker::DoBenchRow(const FBenchRow& Row, bool bByEye)
{
	// A line on the card pressed (a tint's poured by holding it instead).
	if (Row.Kind == ETorqueShedAct::PaintPour || Row.Kind == ETorqueShedAct::Count) return;
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = Row.Kind;
	R.Item = Row.Item;
	R.Colour = Row.Colour;
	if (bByEye)
	{
		if (Row.Kind != ETorqueShedAct::PaintRecipe) return;
		R.Colour = 1;
	}
	if (R.Kind == ETorqueShedAct::PaintTinSave)
	{
		R.Text = BenchName.TrimStartAndEnd();
		if (R.Text.IsEmpty()) R.Text = FString::Printf(TEXT("Mix %d"), B->PaintBay().Mixed + 1);
	}
	FlushBench(false);
	if (!BenchAsk(R)) return;
	if (R.Kind == ETorqueShedAct::PaintTinNew) BenchRow = 0;
	if (R.Kind == ETorqueShedAct::PaintTinSave) BenchName.Reset();
}

void ATorqueWalker::BenchMouse(APlayerController* PC, float)
{
	namespace TPM = TorquePaintMix;
	float Mx = 0.0f, My = 0.0f;
	const UTorqueBuildSubsystem* B = Shed();
	if (!PC || !B || !PC->GetMousePosition(Mx, My))
	{
		bMousePour = bStirTrack = false;
		return;
	}
	const FVector2D Mouse(Mx, My);
	int32 VW = 0, VH = 0;
	PC->GetViewportSize(VW, VH);
	const float Scale = FMath::Max(VH, 1) / 1080.0f;
	const TArray<FBenchRow> Rows = BenchRows();
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) BenchPanel.Scroll(Mouse, 1);
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) BenchPanel.Scroll(Mouse, -1);
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const FTorqueBenchClick C = BenchPanel.Click(Mouse);
		switch (C.Kind)
		{
		case FTorqueBenchClick::EKind::Close:
			CloseBench();
			bWaitRelease = true;
			return;
		case FTorqueBenchClick::EKind::Page:
			if (PouringTint == INDEX_NONE && BenchPage != C.Arg)
			{
				BenchPage = FMath::Clamp(C.Arg, 0, TWBI::Pages - 1);
				BenchRow = 0;
			}
			break;
		case FTorqueBenchClick::EKind::Name:
			bNaming = true;
			if (BenchName.IsEmpty()) BenchName = FString::Printf(TEXT("Mix %d"), B->PaintBay().Mixed + 1);
			break;
		case FTorqueBenchClick::EKind::Row:
			if (Rows.IsValidIndex(C.Arg))
			{
				BenchRow = C.Arg;
				if (Rows[C.Arg].Kind == ETorqueShedAct::PaintPour)
				{
					bMousePour = true;   // held on the tint: it pours
					PourPressY = My;
				}
				else DoBenchRow(Rows[C.Arg], false);
			}
			break;
		case FTorqueBenchClick::EKind::Eye:
			if (Rows.IsValidIndex(C.Arg)) DoBenchRow(Rows[C.Arg], true);
			break;
		default:
			// In the view: the tint picked, poured.
			if (!BenchPanel.Over(Mouse) && Rows.IsValidIndex(BenchRow) && Rows[BenchRow].Kind == ETorqueShedAct::PaintPour)
			{
				bMousePour = true;
				PourPressY = My;
			}
			break;
		}
	}
	if (!PC->IsInputKeyDown(EKeys::LeftMouseButton)) bMousePour = false;
	// paint_bench.gd TILT_DRAG: 260 px of drag (at 1080p) tips the bottle straight down.
	BenchDragTilt = bMousePour ? FMath::Clamp((My - PourPressY) / (260.0f * Scale), 0.0f, 1.0f) : 0.0f;
	// Stirring: the right mouse held in the view, going round the scale (a turn of the mouse is a turn
	// of the stick).
	const FTorquePaintBay& P = B->PaintBay();
	if (PC->IsInputKeyDown(EKeys::RightMouseButton) && !BenchPanel.Over(Mouse) && P.BenchTinPtr() && BenchPage == 0)
	{
		FVector2D Centre(VW * 0.35f, VH * 0.5f);
		FVector2D OnScreen;
		if (PC->ProjectWorldLocationToScreen(TorquePaintBayLayout::ScaleAt, OnScreen)) Centre = OnScreen;
		const FVector2D D = Mouse - Centre;
		if (D.Size() > 10.0f * Scale)
		{
			const float A = FMath::Atan2(D.Y, D.X);
			if (bStirTrack)
			{
				const float Turned = FMath::Min(FMath::Abs(FMath::FindDeltaAngleRadians(StirAngle, A)), 1.0f);
				StirUnsent += TPM::StirPerTurn * Turned / (2.0f * PI);
			}
			StirAngle = A;
			bStirTrack = true;
		}
	}
	else bStirTrack = false;
}

void ATorqueWalker::BenchTyping(APlayerController* PC)
{
	// The mix's name, typed: letters (Shift for capitals), digits, space, hyphen; Backspace; Enter or
	// Esc when it's done.
	if (!PC) return;
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	if (Just(EKeys::Enter) || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Bottom) || Just(EKeys::Gamepad_FaceButton_Right))
	{
		bNaming = false;
		return;
	}
	if (Just(EKeys::BackSpace) && BenchName.Len() > 0) BenchName.LeftChopInline(1);
	if (BenchName.Len() >= 28) return;
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	static const TCHAR* const Letters = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	for (int32 I = 0; I < 26; ++I)
	{
		const TCHAR KeyName[2] = {Letters[I], 0};
		if (Just(FKey(FName(KeyName)))) BenchName.AppendChar(bShift ? Letters[I] : FChar::ToLower(Letters[I]));
	}
	static const TCHAR* const Digits[] = {TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"),
		TEXT("Seven"), TEXT("Eight"), TEXT("Nine")};
	for (int32 I = 0; I < 10; ++I)
		if (Just(FKey(FName(Digits[I])))) BenchName.AppendChar((TCHAR)(TEXT('0') + I));
	if (Just(EKeys::SpaceBar)) BenchName.AppendChar(TEXT(' '));
	if (Just(EKeys::Hyphen)) BenchName.AppendChar(TEXT('-'));
}

void ATorqueWalker::DrawBench(AHUD* Hud, UCanvas* Canvas) const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FVector2D Mouse(-1.0, -1.0);
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		float Mx = 0.0f, My = 0.0f;
		if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
	}
	FTorqueBenchState St;
	St.Page = BenchPage;
	St.Focus = BenchRow;
	St.Pouring = PouringTint;
	St.Tilt = BenchTilt;
	St.PourUnsent = PourUnsent;
	St.StirUnsent = StirUnsent;
	St.Name = (BenchName.IsEmpty() && !bNaming) ? FString::Printf(TEXT("Mix %d"), B->PaintBay().Mixed + 1) : BenchName;
	St.bNaming = bNaming;
	if (NoteFor > 0.0) St.Note = Note;
	St.bNoteOk = bNoteOk;
	BenchPanel.Draw(Hud, Canvas, B, BenchRows(), St, Mouse);
}

void ATorqueWalker::FlushBench(bool bStopPour)
{
	BenchSendIn = TWBI::SendEvery;
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	if (PourUnsent > 0.0f && PouringTint != INDEX_NONE)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintPour;
		R.Colour = PouringTint;
		R.Amount = PourUnsent;
		R.Tilt = BenchTilt;
		PourUnsent = 0.0f;
		if (!BenchAsk(R))
		{
			PouringTint = INDEX_NONE;
			bWaitRelease = true;
		}
	}
	if (StirUnsent > 0.0f)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintStir;
		R.Amount = FMath::Min(StirUnsent, 0.5f);
		StirUnsent = 0.0f;
		BenchAsk(R);
	}
	if (bStopPour && PouringTint != INDEX_NONE)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintPourStop;
		PouringTint = INDEX_NONE;
		PourUnsent = 0.0f;
		BenchPourHeld = 0.0f;
		BenchAsk(R);
	}
}

// --- A tin in your hands --------------------------------------------------------------------------------

bool ATorqueWalker::TinInteract()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	const FTorquePaintBay& P = B->PaintBay();
	const FTorqueTin* Holding = P.HeldTin(B->Me());
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::PaintShelf)
	{
		FTorqueShedRequest R;
		R.Kind = Holding ? ETorqueShedAct::PaintTinPut : ETorqueShedAct::PaintTinTake;
		R.Item = Holding ? TargetPlace->Index : P.TinInSlot(TargetPlace->Index);
		if (!Holding && R.Item == 0)
		{
			Say(TEXT("There's no tin there."), false);
			return true;
		}
		if (BenchAsk(R) && !Holding) Say(TEXT("Holding the tin. R fills the spray gun's cup from it; F on the bench puts it on the scale."), true, 4.0);
		return true;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::PaintBench)
	{
		if (Holding)
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::PaintTinBench;
			if (!BenchAsk(R)) return true;
		}
		OpenBench();
		return true;
	}
	if (Holding && TargetPlace && TargetPlace->Kind == ETorquePlace::Bin)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintTinBin;
		BenchAsk(R);
		return true;
	}
	return false;
}

// --- The HUD --------------------------------------------------------------------------------------------

bool ATorqueWalker::BenchSwatches(FLinearColor& OutMix, bool& bOutTarget, FLinearColor& OutTarget) const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!bBench || !B) return false;
	const FTorquePaintBay& P = B->PaintBay();
	bOutTarget = P.bTarget;
	OutTarget = P.TargetColour;
	const FTorqueTin* Tin = P.BenchTinPtr();
	if (!Tin) return false;
	OutMix = Tin->Spec().Colour;
	return true;
}

void ATorqueWalker::BenchHud(const UTorqueBuildSubsystem* B)
{
	namespace TPM = TorquePaintMix;
	const FColor Title(190, 210, 255), Prompt(255, 214, 120), Blocked(255, 140, 120), Help(200, 200, 200), Info(220, 220, 220);
	const FTorquePaintBay& P = B->PaintBay();
	HudLines.Add({FString::Printf(TEXT("MIXING BENCH: < %s >   (Q / E, L1 / R1)"), TWBI::PageNames[FMath::Clamp(BenchPage, 0, TWBI::Pages - 1)]), FColor::White, 0.85f});
	if (const FTorqueTin* Tin = P.BenchTinPtr())
	{
		const TPM::FPaintSpec S = Tin->Spec();
		const float Reading = P.ScaleGrams() + PourUnsent;
		HudLines.Add({FString::Printf(TEXT("Scale: %s g%s   %s   %s"), *TPM::GramsText(Reading), P.Tared() ? TEXT(" (tared)") : TEXT(""),
			*P.TinText(*Tin), *TPM::FullCode(Tin->Mix)), Title, 0.65f});
		HudLines.Add({FString::Printf(TEXT("Stirred: %.0f%%%s"), FMath::Min(1.0f, Tin->Mix.Stir + StirUnsent) * 100.0f,
			Tin->Mix.Stir < 0.98f ? TEXT("   hold R / Square to stir") : TEXT("   an even mix")), Info, 0.6f});
		// The colour as it'd spray, and against the colour to match.
		HudLines.Add({FString::Printf(TEXT("##########  the mix: %s"), *TPM::EffectName(S.Effect).ToLower()), TWBI::Shown(S.Colour), 0.7f});
		if (P.bTarget)
		{
			const float Match = TPM::MatchOf(S.Colour, P.TargetColour, S.Effect, P.TargetEffect);
			HudLines.Add({FString::Printf(TEXT("##########  to match: %s   (a %s match%s)"), *P.TargetName, *TPM::MatchText(Match),
				Match >= TPM::GoodMatch ? TEXT(": spot on") : TEXT("")), TWBI::Shown(P.TargetColour), 0.7f});
		}
		if (P.CardRecipe != 0)
			HudLines.Add({FString::Printf(TEXT("Recipe card: %d%% to it"), FMath::RoundToInt(TPM::RecipeAccuracy(Tin->Mix, P.CardMix) * 100.0f)), Info, 0.6f});
		if (PouringTint != INDEX_NONE)
			HudLines.Add({FString::Printf(TEXT("Pouring %s: the bottle %.0f%% tipped, %.1f g a second"), TPM::Tints()[PouringTint].Name,
				BenchTilt * 100.0f, TPM::PourRate(BenchTilt)), Prompt, 0.65f});
	}
	else HudLines.Add({TEXT("No tin on the scale: pick a base for a new one, or bring a tin from the shelf (F on the bench)."), Info, 0.6f});
	if (P.ShakerTin != 0) HudLines.Add({FString::Printf(TEXT("Shaker: %.0f s to go"), P.ShakerLeft), Info, 0.6f});
	const TArray<FBenchRow> Rows = BenchRows();
	const int32 From = FMath::Clamp(BenchRow - 5, 0, FMath::Max(Rows.Num() - 11, 0));
	for (int32 I = From; I < Rows.Num() && I < From + 11; ++I)
		HudLines.Add({(I == BenchRow ? TEXT("> ") : TEXT("   ")) + Rows[I].Text, I == BenchRow ? Prompt : Info, 0.58f});
	if (BenchPage == 0) HudLines.Add({TEXT("W / S: choose   hold left mouse / Cross on a tint: pour (longer, faster)   left mouse: do it   F / Circle: step back"), Help, 0.52f});
	else if (BenchPage == 1) HudLines.Add({TEXT("W / S: choose   left mouse / Cross: card on the bench (pour to it)   R / Square: match it by eye   F / Circle: step back"), Help, 0.52f});
	else HudLines.Add({TEXT("W / S: choose   left mouse / Cross: restock it   F / Circle: step back"), Help, 0.52f});
	if (NoteFor > 0.0) HudLines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
}

// --- Test hooks -----------------------------------------------------------------------------------------

FString ATorqueWalker::TestBench(bool bOn)
{
	Note.Reset();
	if (bOn)
	{
		OpenBench();
		return bBench ? FString(TEXT("at the mixing bench")) : FString(TEXT("couldn't step up: ")) + Note;
	}
	CloseBench();
	return TEXT("stepped back from the bench");
}

FString ATorqueWalker::TestPaint(const FTorqueShedRequest& R)
{
	Note.Reset();
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("no shed");
	FString WhyNot;
	if (!B->Request(R, WhyNot)) return TEXT("refused: ") + WhyNot;
	return Note.IsEmpty() ? FString(TEXT("done")) : Note;
}

FString ATorqueWalker::TestPour(const FString& Code, float Grams)
{
	const int32 Tint = TorquePaintMix::TintIndex(Code);
	if (Tint == INDEX_NONE) return TEXT("no tint ") + Code;
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("no shed");
	// As holding the button does: flushes of a few grams each, the bottle well tipped, then back on the rack.
	float Left = Grams;
	FString WhyNot;
	while (Left > 0.001f)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PaintPour;
		R.Colour = Tint;
		R.Amount = FMath::Min(Left, 4.0f);
		R.Tilt = 0.7f;
		if (!B->Request(R, WhyNot)) return FString::Printf(TEXT("poured %s g of %s, then refused: %s"), *TorquePaintMix::GramsText(Grams - Left), *Code, *WhyNot);
		Left -= R.Amount;
	}
	FTorqueShedRequest Stop;
	Stop.Kind = ETorqueShedAct::PaintPourStop;
	B->Request(Stop, WhyNot);
	return FString::Printf(TEXT("poured %s g of %s"), *TorquePaintMix::GramsText(Grams), *Code);
}
