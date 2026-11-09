// The walker at the movable shed and yard kit (TorqueFurniture.h): what's looked at, holding F on a
// piece to take hold of it, R turning it (Shift+R the other way), F putting it down, the HUD's lines
// and the outline round it. Torque Shed's walker.gd furniture moves: "hold F to move one".
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFurniture.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

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
			bFurnArmed = false;
			Say(TEXT("Hold F to take hold of it."), true, 2.0);
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
	if (TargetFurniture == 0 || bComputer || bBench) return false;
	const FTorqueFurniture* F = B->FurnitureById(TargetFurniture);
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	if (!M) return false;
	FString Heading;
	for (const TPair<FName, FString>& K : TorqueFurniture::Kinds())
		if (K.Key == M->Kind) Heading = K.Value;
	Lines.Add({Heading, Title, 0.6f});
	Lines.Add({M->Name, FColor::White, 0.8f});
	if (!M->Desc.IsEmpty()) Lines.Add({M->Desc, Info, 0.55f});
	if (B->HeldBy(B->Me()) == 0) Lines.Add({TEXT("Hold F / Triangle: move it"), Prompt, 0.65f});
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
