// The walker at the movable shed and yard kit (TorqueFurniture.h): what's looked at, holding F on a
// piece to take hold of it, R turning it (Shift+R the other way), F putting it down, the HUD's lines
// and the outline round it. Torque Shed's walker.gd furniture moves: "hold F to move one".
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFurniture.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueKeys.h"
#include "Coop/TorqueUi.h"
#include "Engine/Canvas.h"
#include "SonyPad.h"

namespace TorqueWalkerFurnitureImpl
{
constexpr double HoldToMove = 0.35;   // seconds F is held on a piece to take hold of it
constexpr float TurnStep = 15.0f;     // degrees an R press turns it

// Where the line from Start to End first goes into C's model's box (0..1 along it), or false.
bool HitBox(const UStaticMeshComponent* C, const FVector& Start, const FVector& End, double& OutTime)
{
	const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
	if (!Mesh || !C->IsVisible()) return false;
	const FBox Box = Mesh->GetBoundingBox();
	const FTransform& T = C->GetComponentTransform();
	const FVector A = T.InverseTransformPosition(Start), B = T.InverseTransformPosition(End);
	if (Box.IsInside(A)) { OutTime = 0.0; return true; }
	FVector HitAt, HitNormal;
	float Time = 0.0f;
	if (!FMath::LineExtentBoxIntersection(Box, A, B, FVector::ZeroVector, HitAt, HitNormal, Time)) return false;
	OutTime = Time;
	return true;
}
} // namespace TorqueWalkerFurnitureImpl

namespace TWFI = TorqueWalkerFurnitureImpl;

void ATorqueWalker::FindFurniture()
{
	TargetFurniture = 0;
	UWorld* World = GetWorld();
	const UTorqueBuildSubsystem* B = Shed();
	if (!World || !Camera || !B || bComputer || bRidingOut) return;
	if (B->FurnitureMovedBy(B->Me()) != 0) return;   // moving one: nothing else is looked at
	const FVector Start = Camera->GetComponentLocation();
	const FVector Look = Controller ? GetControlRotation().Vector() : Camera->GetForwardVector();
	const FVector End = Start + Look * Reach;
	// What stops the look first (walls, benches, the kit's own shapes), so a piece behind a wall
	// isn't picked through it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerFurniture), true, this);
	FHitResult Hit;
	const double Clear = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? Hit.Time + 0.03 : 1.0;
	double Best = Clear;
	for (TActorIterator<ATorqueFurnishings> It(World); It; ++It)
		for (const TPair<int32, UStaticMeshComponent*>& KV : It->Pieces())
		{
			const FTorqueFurniture* F = B->FurnitureById(KV.Key);
			if (!F || F->Mover != 0) continue;
			double Time = 0.0;
			if (!TWFI::HitBox(KV.Value, Start, End, Time) || Time > Best) continue;
			Best = Time;
			TargetFurniture = KV.Key;
		}
	if (TargetFurniture == 0) return;
	// Nearer than what FindTarget found (a part lying on a bench is nearer than the bench): the kit.
	const double FurnDist = Best * Reach;
	if (Target != ETorqueLook::None && Target != ETorqueLook::Floor && Target != ETorqueLook::Door
		&& FVector::Dist(Start, TargetPoint) < FurnDist + 5.0)
	{
		TargetFurniture = 0;
		return;
	}
	Target = ETorqueLook::None;
	TargetItem = TargetEngine = 0;
	TargetBay = INDEX_NONE;
	TargetSlot = NAME_None;
	TargetPlace = nullptr;
	TargetPoint = Start + Look * FurnDist;
}

bool ATorqueWalker::FurnitureInput(float Dt, bool bF, bool bFDown, bool bAltJust)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	FString WhyNot;
	// Moving one: R turns it, F puts it down where it is (or where you look, for a small one or a
	// wall one).
	if (const int32 Moving = B->FurnitureMovedBy(B->Me()))
	{
		bFurnArmed = false;
		const FTorqueFurniture* F = B->FurnitureById(Moving);
		const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
		if (bAltJust)
		{
			const APlayerController* PC = Cast<APlayerController>(Controller);
			const bool bBack = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::TurnFurniture;
			R.Item = Moving;
			R.Amount = bBack ? -TWFI::TurnStep : TWFI::TurnStep;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		}
		if (!bF) return true;   // (nothing else happens with your hands full of it)
		if (!M) return true;
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::ReleaseFurniture;
		R.Item = Moving;
		if (!TorqueFurniture::PutDownXf(this, *M, F->Turn, R.Place, WhyNot) || !B->Request(R, WhyNot)) Say(WhyNot, false);
		else Doing(FString(), 0.0);
		return true;
	}
	// Looking at one with empty hands: hold F to take hold of it (a tap does nothing else here).
	if (bF && TargetFurniture != 0)
	{
		bFurnArmed = true;
		FurnFHeld = 0.0;
		return true;
	}
	if (bFurnArmed)
	{
		FurnFHeld += Dt;
		if (!bFDown)
		{
			// A tap: sit on it when it's a seat (Torque Shed's seats), else how to move it.
			bFurnArmed = false;
			const FTorqueFurniture* F = B->FurnitureById(TargetFurniture);
			const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
			if (!(M && M->Sit > 0.0 && Sit(TargetFurniture))) Say(TEXT("Hold F to take hold of it."), true, 2.0);
			return true;
		}
		if (FurnFHeld >= TWFI::HoldToMove)
		{
			bFurnArmed = false;
			const FTorqueFurniture* F = B->FurnitureById(TargetFurniture);
			const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::GrabFurniture;
			R.Item = TargetFurniture;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
			else Doing(M ? FString::Printf(TEXT("moving the %s"), *M->Name.ToLower()) : FString(TEXT("moving kit")), 30.0);
		}
		return true;
	}
	return false;
}

bool ATorqueWalker::FurnitureHud(TArray<FHudLine>& Lines) const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	const FColor Title(190, 210, 255), Prompt(255, 214, 120), Blocked(255, 140, 120), Info(220, 220, 220);
	if (const int32 Moving = B->FurnitureMovedBy(B->Me()))
	{
		const FTorqueFurniture* F = B->FurnitureById(Moving);
		const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
		if (!M) return false;
		Lines.Add({FString::Printf(TEXT("Moving the %s"), *M->Name.ToLower()), FColor::White, 0.8f});
		Lines.Add({M->bWall ? TEXT("Look at a wall: F hangs it there.") : (M->Small() ? TEXT("Look where it goes (a bench, a shelf, the floor): F puts it down.")
			: TEXT("Walk to push it about, back up to pull it.")), Info, 0.6f});
		Lines.Add({FString::Printf(TEXT("R / Square: turn it (Shift+R the other way, %+.0f deg)   F / Triangle: put it down"), F->Turn), Prompt, 0.65f});
		if (NoteFor > 0.0) Lines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
		return true;
	}
	if (SeatId != 0)
	{
		const FTorqueFurniture* Seat = B->FurnitureById(SeatId);
		const TorqueFurniture::FModel* SM = Seat ? TorqueFurniture::Find(Seat->Model) : nullptr;
		Lines.Add({FString::Printf(TEXT("Sitting on the %s"), SM ? *SM->Name.ToLower() : TEXT("seat")), FColor::White, 0.8f});
		Lines.Add({TEXT("F / Triangle, Space / Circle or a step: get up"), Prompt, 0.65f});
		if (NoteFor > 0.0) Lines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
		return true;
	}
	if (TargetFurniture == 0 || bComputer || bBench) return false;
	const FTorqueFurniture* F = B->FurnitureById(TargetFurniture);
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	if (!M) return false;
	FString Heading;
	for (const TPair<FName, FString>& K : TorqueFurniture::Kinds())
		if (K.Key == M->Kind) Heading = K.Value;
	Lines.Add({Heading, Title, 0.6f});
	Lines.Add({TorqueFurniture::NameOf(*F), FColor::White, 0.8f});
	if (!M->Desc.IsEmpty()) Lines.Add({M->Desc, Info, 0.55f});
	if (M->Sit > 0.0) Lines.Add({TEXT("F / Triangle: sit on it"), Prompt, 0.65f});
	if (B->HeldBy(B->Me()) == 0) Lines.Add({TEXT("Hold F / Triangle: move it"), Prompt, 0.65f});
	// [label] (walker.gd: "write a label for these shelves")
	if (TorqueFurniture::HasLabel(*M))
		Lines.Add({FString::Printf(TEXT("%s: %s"), *FTorqueKeys::Text(ETorqueKey::LabelShelf),
			F->Label.IsEmpty() ? TEXT("write a label for these shelves") : TEXT("change the label on these shelves")), Prompt, 0.65f});
	return false;
}

void ATorqueWalker::FurnitureOutline(TArray<UPrimitiveComponent*>& Lined) const
{
	if (TargetFurniture == 0) return;
	UWorld* World = GetWorld();
	if (!World) return;
	for (TActorIterator<ATorqueFurnishings> It(World); It; ++It)
		if (UStaticMeshComponent* const* C = It->Pieces().Find(TargetFurniture))
			if (*C) Lined.Add(*C);
}

// --- Sitting ------------------------------------------------------------------------------------------

bool ATorqueWalker::Sit(int32 Piece)
{
	UTorqueBuildSubsystem* B = Shed();
	const FTorqueFurniture* F = B ? B->FurnitureById(Piece) : nullptr;
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	if (!M || M->Sit <= 0.0 || !Camera) return false;
	if (F->Mover != 0)
	{
		Say(TEXT("Someone's moving it."), false);
		return false;
	}
	if (bIsCrouched) SetCrouched(false);
	// On it, a little back from its middle, facing out of its front (+y at yaw 0), and still: the
	// look goes round, the walk gets you up (ReadInput).
	const FVector Front = F->Xf.GetRotation().RotateVector(FVector(0.0, 1.0, 0.0)).GetSafeNormal2D();
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Under = F->Xf.GetLocation() - Front * (M->Size.Z * 0.12);
	const FVector At(Under.X, Under.Y, F->Xf.GetLocation().Z + Half + 2.0);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
	const FRotator View(-8.0, Front.Rotation().Yaw, 0.0);
	if (AController* C = GetController()) C->SetControlRotation(View);
	ATorquePlayerController::PlaceOnHost(this, At, View);   // [co-op] a joined copy: the host too
	// Sat, the eyes are about 72 cm over the seat.
	SeatEye = FVector(0.0, 0.0, F->Xf.GetLocation().Z + M->Sit + 72.0 - At.Z);
	SeatId = Piece;
	Camera->SetRelativeLocation(SeatEye);
	Target = ETorqueLook::None;
	TargetFurniture = 0;
	bTestAim = false;
	Say(FString::Printf(TEXT("Sat on the %s. F or a step to get up."), *M->Name.ToLower()), true, 2.5);
	return true;
}

void ATorqueWalker::StandUp()
{
	if (SeatId == 0) return;
	const UTorqueBuildSubsystem* B = Shed();
	const FTorqueFurniture* F = B ? B->FurnitureById(SeatId) : nullptr;
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	SeatId = 0;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!F || !M) return;
	// Up and a step out of its front, on the floor.
	const FVector Front = F->Xf.GetRotation().RotateVector(FVector(0.0, 1.0, 0.0)).GetSafeNormal2D();
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Out = F->Xf.GetLocation() + Front * (FMath::Max(M->Size.Z, M->Size.X) * 0.5 + 40.0);
	const FVector At(Out.X, Out.Y, Half + 2.0);
	SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
	const AController* C = GetController();
	ATorquePlayerController::PlaceOnHost(this, At, C ? C->GetControlRotation() : GetActorRotation());
	Camera->SetRelativeLocation(EyeSpot());
}

// --- The shelf label box (label_dialog.gd) ---------------------------------------------------------------

bool ATorqueWalker::OpenLabel(int32 Piece)
{
	const UTorqueBuildSubsystem* B = Shed();
	const FTorqueFurniture* F = B ? B->FurnitureById(Piece) : nullptr;
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	if (!M || !TorqueFurniture::HasLabel(*M)) return false;
	if (F->Mover != 0)
	{
		Say(TEXT("Someone's moving it."), false);
		return true;
	}
	LabelPiece = Piece;
	LabelEdit = F->Label;
	bFurnArmed = false;
	return true;
}

void ATorqueWalker::CloseLabel(bool bSave)
{
	const int32 Piece = LabelPiece;
	LabelPiece = 0;
	ComputerClosedFrame = GFrameCounter;   // (the Esc that closed it isn't also the pause menu's)
	if (!bSave || Piece == 0) return;
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::LabelFurniture;
	R.Item = Piece;
	R.Text = TorqueFurniture::CleanLabel(LabelEdit);
	FString WhyNot;
	if (!B->Request(R, WhyNot)) Say(WhyNot, false);
}

void ATorqueWalker::LabelInput(APlayerController* PC, uint32 SonyHit)
{
	// Typed: letters (Shift for capitals), digits, space, hyphen; Backspace, Delete clears it, Ctrl+V
	// pastes. Enter (Cross) writes it on, Esc (Circle) leaves it as it was.
	const UTorqueBuildSubsystem* B = Shed();
	const FTorqueFurniture* F = B ? B->FurnitureById(LabelPiece) : nullptr;
	if (!PC || !F || F->Mover != 0)
	{
		CloseLabel(false);   // gone, or someone's taken hold of it
		return;
	}
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	if (Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross))
	{
		CloseLabel(true);
		return;
	}
	if (Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle))
	{
		CloseLabel(false);
		return;
	}
	ATorquePlayerController::TypeText(PC, LabelEdit, TorqueFurniture::LabelMax);
}

void ATorqueWalker::DrawLabelBox(UCanvas* Canvas) const
{
	// label_dialog.gd: a small box in the middle, SHELF LABEL over a line to type in, Save and Cancel.
	if (!Canvas || LabelPiece == 0) return;
	namespace UI = TorqueUi;
	const float S = UI::Scale(Canvas);
	UI::Backdrop(Canvas, 0.35f);
	const float W = 520.0f * S, H = 196.0f * S;
	const float X = 0.5f * (Canvas->ClipX - W), Y = 0.5f * (Canvas->ClipY - H);
	UI::Rect(Canvas, X, Y, W, H, UI::Panel);
	UI::Rect(Canvas, X, Y, 3.0f * S, H, UI::Accent);
	const float Margin = 26.0f * S;
	UI::Text(Canvas, TEXT("SHELF LABEL"), X + Margin, Y + 20.0f * S, 24.0f * S, UI::Accent, true);
	// The line to type on, with its caret; the hint in it while it's empty.
	const float FX = X + Margin, FY = Y + 66.0f * S, FW = W - 2.0f * Margin, FH = 46.0f * S;
	UI::Rect(Canvas, FX, FY, FW, FH, UI::PanelLight);
	UI::Frame(Canvas, FX, FY, FW, FH, UI::Accent, FMath::Max(1.0f, 1.5f * S));
	const float Px = 22.0f * S, TY = FY + 0.5f * (FH - Px * 1.2f);
	float Typed = 0.0f;
	if (LabelEdit.IsEmpty()) UI::Text(Canvas, TEXT("Brakes, engine spares, Barn find..."), FX + 12.0f * S, TY, Px, UI::Faint, false, UI::EAlign::Left, false);
	else Typed = UI::Text(Canvas, LabelEdit, FX + 12.0f * S, TY, Px, UI::Ink);
	if (FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.6)
		UI::Rect(Canvas, FX + 12.0f * S + Typed + 2.0f * S, FY + 9.0f * S, FMath::Max(1.0f, 2.0f * S), FH - 18.0f * S, UI::Accent);
	UI::Text(Canvas, FString::Printf(TEXT("%d / %d"), LabelEdit.Len(), TorqueFurniture::LabelMax), FX + FW, FY + FH + 8.0f * S, 14.0f * S, UI::Dim,
		false, UI::EAlign::Right, false);
	float HX = FX;
	const float HY = Y + H - 44.0f * S;
	HX += UI::KeyHint(Canvas, TEXT("Enter"), TEXT("Save"), HX, HY, S) + 22.0f * S;
	HX += UI::KeyHint(Canvas, TEXT("Esc"), TEXT("Cancel"), HX, HY, S) + 22.0f * S;
	UI::KeyHint(Canvas, TEXT("Delete"), TEXT("Clear"), HX, HY, S);
}

// --- Tests ------------------------------------------------------------------------------------------------

namespace TorqueWalkerFurnitureImpl
{
FString Where(const FTorqueFurniture& F)
{
	const FVector At = F.Xf.GetLocation();
	return FString::Printf(TEXT("piece %d %s at %.0f, %.0f, %.0f yaw %.0f"), F.Id, *F.Model.ToString(), At.X, At.Y, At.Z, F.Xf.Rotator().Yaw);
}
} // namespace TorqueWalkerFurnitureImpl

FString ATorqueWalker::TestFurniture(const FString& What, const TArray<FString>& Args)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("not furniture: no shed");
	FString WhyNot;
	const int32 Moving = B->FurnitureMovedBy(B->Me());
	if (What == TEXT("log"))
	{
		TMap<FName, int32> Count;
		for (const FTorqueFurniture& F : B->Furniture()) ++Count.FindOrAdd(F.Model);
		FString Out = FString::Printf(TEXT("%d pieces"), B->Furniture().Num());
		for (const TPair<FName, int32>& KV : Count) Out += FString::Printf(TEXT(", %s x%d"), *KV.Key.ToString(), KV.Value);
		if (Moving != 0)
			if (const FTorqueFurniture* F = B->FurnitureById(Moving)) Out += TEXT("; moving ") + TWFI::Where(*F);
		return Out;
	}
	if (What == TEXT("look"))
	{
		if (Args.Num() == 0) return TEXT("not furniture: look at what? a model or a piece id");
		if (Moving != 0) return TEXT("not looking: you're moving a piece (furn:drop first)");
		StandUp();
		// A piece id, or the nearest piece of that model.
		const FTorqueFurniture* Pick = Args[0].IsNumeric() ? B->FurnitureById(FCString::Atoi(*Args[0])) : nullptr;
		if (!Pick)
		{
			double Best = 1e30;
			for (const FTorqueFurniture& F : B->Furniture())
			{
				if (F.Mover != 0 || F.Model != FName(*Args[0])) continue;
				const double D = FVector::DistSquared(F.Xf.GetLocation(), GetActorLocation());
				if (D < Best) { Best = D; Pick = &F; }
			}
		}
		if (!Pick) return TEXT("not furniture: no ") + Args[0] + TEXT(" out");
		const TorqueFurniture::FModel* M = TorqueFurniture::Find(Pick->Model);
		const double Height = M ? M->Size.Y : 60.0;
		const double Across = M ? FMath::Max(M->Size.X, M->Size.Z) * 0.5 : 50.0;
		// From its front (+y at yaw 0), a little off to the side, close enough for the reach.
		const FVector Front = Pick->Xf.GetRotation().RotateVector(FVector(0.25, 1.0, 0.0)).GetSafeNormal2D();
		const FVector Mid = Pick->Xf.GetLocation() + FVector(0.0, 0.0, FMath::Min(Height * 0.6, 120.0));
		const int32 Id = Pick->Id;
		const FString Seen = TestLookAt(Mid, Front, FMath::Min(Across + 110.0, (double)Reach - 40.0));
		return FString::Printf(TEXT("%s (wanted piece %d)%s"), *Seen, Id, TargetFurniture == Id ? TEXT("") : TEXT(": not the one looked at"));
	}
	if (What == TEXT("grab"))
	{
		ReAim();
		FindTarget();
		if (TargetFurniture == 0) return TEXT("not grabbed: no kit piece looked at (") + Describe() + TEXT(")");
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::GrabFurniture;
		R.Item = TargetFurniture;
		if (!B->Request(R, WhyNot)) return TEXT("not grabbed: ") + WhyNot;
		bTestAim = false;
		const FTorqueFurniture* F = B->FurnitureById(R.Item);
		return F && F->Mover == B->Me() ? TEXT("picked up ") + TWFI::Where(*F) : FString(TEXT("not grabbed: the host didn't give it"));
	}
	if (What == TEXT("sit"))
	{
		ReAim();
		FindTarget();
		const int32 Piece = TargetFurniture;
		if (Piece == 0) return TEXT("not sitting: no kit piece looked at (") + Describe() + TEXT(")");
		if (!Sit(Piece)) return TEXT("not sitting: ") + Note;
		return FString::Printf(TEXT("sitting on piece %d, at %.0f, %.0f, eyes %.0f cm up"), Piece, GetActorLocation().X, GetActorLocation().Y,
			GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + SeatEye.Z);
	}
	if (What == TEXT("stand"))
	{
		if (SeatId == 0) return TEXT("not standing up: not sat down");
		StandUp();
		return FString::Printf(TEXT("stood up at %.0f, %.0f"), GetActorLocation().X, GetActorLocation().Y);
	}
	if (What == TEXT("label"))
	{
		// [label] Write label:<words> (none: wipe it) on the shelving unit looked at, as the box does.
		ReAim();
		FindTarget();
		if (TargetFurniture == 0) return TEXT("not labelled: no kit piece looked at (") + Describe() + TEXT(")");
		const int32 Piece = TargetFurniture;
		if (!OpenLabel(Piece) || LabelPiece != Piece) return TEXT("not labelled: ") + (NoteFor > 0.0 ? Note : FString(TEXT("that isn't shelving")));
		LabelEdit = FString::Join(Args, TEXT(" "));
		CloseLabel(true);
		bTestAim = false;
		const FTorqueFurniture* F = B->FurnitureById(Piece);
		return F ? FString::Printf(TEXT("labelled piece %d \"%s\" (%s)"), Piece, *F->Label, *TorqueFurniture::NameOf(*F)) : FString(TEXT("not labelled: gone"));
	}
	if (Moving == 0 && What != TEXT("order")) return TEXT("not furniture: not moving a piece (furn:grab first)");
	if (What == TEXT("turn"))
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::TurnFurniture;
		R.Item = Moving;
		R.Amount = Args.Num() > 0 ? FCString::Atof(*Args[0]) : TWFI::TurnStep;
		if (!B->Request(R, WhyNot)) return TEXT("not turned: ") + WhyNot;
		const FTorqueFurniture* F = B->FurnitureById(Moving);
		return FString::Printf(TEXT("turned to %+.0f deg"), F ? F->Turn : 0.0f);
	}
	if (What == TEXT("walk"))
	{
		// Carry it there: stand short of the spot facing it, so a big one sits on the spot.
		if (Args.Num() < 2) return TEXT("not furniture: walk where? x:y");
		const FTorqueFurniture* F = B->FurnitureById(Moving);
		const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
		const FVector To(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), 0.0);
		const FVector Dir = FVector(To - GetActorLocation()).GetSafeNormal2D();
		const double Short = M && !M->Small() && !M->bWall ? FMath::Max(M->Size.Z, M->Size.X) * 0.5 + 55.0 : 55.0;
		const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Stand = To - (Dir.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : Dir) * Short;
		SetActorLocation(FVector(Stand.X, Stand.Y, Half + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
		const FRotator View = FRotator(-10.0, (Dir.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : Dir).Rotation().Yaw, 0.0);
		if (AController* C = GetController()) C->SetControlRotation(View);
		Camera->SetWorldRotation(View);
		const FTransform Xf = M ? TorqueFurniture::MovedXf(this, *M, F->Turn) : FTransform::Identity;
		return FString::Printf(TEXT("carried to %.0f, %.0f (it's at %.0f, %.0f)"), Stand.X, Stand.Y, Xf.GetLocation().X, Xf.GetLocation().Y);
	}
	if (What == TEXT("drop"))
	{
		const FTorqueFurniture* F = B->FurnitureById(Moving);
		const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
		if (!M) return TEXT("not dropped: no model");
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::ReleaseFurniture;
		R.Item = Moving;
		if (!TorqueFurniture::PutDownXf(this, *M, F->Turn, R.Place, WhyNot) || !B->Request(R, WhyNot)) return TEXT("not dropped: ") + WhyNot;
		F = B->FurnitureById(Moving);
		return F ? TEXT("dropped ") + TWFI::Where(*F) : FString(TEXT("dropped (gone?)"));
	}
	if (What == TEXT("order"))
	{
		const TorqueFurniture::FModel* M = Args.Num() > 0 ? TorqueFurniture::Find(FName(*Args[0])) : nullptr;
		if (!M) return TEXT("not ordered: no such kit");
		int32 Before = 0;
		for (const FTorqueFurniture& F : B->Furniture()) Before = FMath::Max(Before, F.Id);
		FString Out = TestComputer(FName(*(TEXT("kit:") + M->Kind.ToString())), M->Id, true);
		CloseComputer();
		for (const FTorqueFurniture& F : B->Furniture())
			if (F.Id > Before && F.Model == M->Id) return Out + TEXT("; landed: ") + TWFI::Where(F);
		return Out + TEXT("; not ordered: no new piece");
	}
	return TEXT("not furniture: look, grab, turn, walk, drop, order, sit, stand, label or log");
}
