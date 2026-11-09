#include "Shed/TorqueYard.h"

#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePlayerState.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueWalker.h"
#include "TorqueBikePawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

namespace
{
// The roller door's point (tagged RideOut by Tools/ue_build_shed.py), on the floor in its opening.
FVector DoorPoint(UWorld* World)
{
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->ActorHasTag(TEXT("RideOut"))) return It->GetActorLocation();
	return FVector(0.0, 580.0, 0.0);
}
} // namespace

namespace TorqueYard
{
// (Asked from inside the pawn being swapped, mid-tick: the swap waits for the next frame, so the
// pawn finishes its frame before it goes.)
void RideOut(APlayerController* PC, int32 Bay)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	if (PC->GetNetMode() == NM_Client)
	{
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ServerRideYard(Bay);
		return;
	}
	TWeakObjectPtr<APlayerController> Weak(PC);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak, Bay]() {
		if (Weak.IsValid()) HostRideOut(Weak.Get(), Bay);
	}));
}

void WalkBackIn(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	if (PC->GetNetMode() == NM_Client)
	{
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ServerWalkBackIn();
		return;
	}
	TWeakObjectPtr<APlayerController> Weak(PC);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak]() {
		if (Weak.IsValid()) HostWalkBackIn(Weak.Get());
	}));
}

void HostRideOut(APlayerController* PC, int32 Bay)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(PC);
	if (!World || !B || !B->Exists(Bay)) return;
	APawn* Old = PC->GetPawn();
	if (Old && Old->IsA<ATorqueBikePawn>()) return;   // already out on it
	// Whose bike it is, for everyone's copy of it (they draw a ridden bike from its player's bay).
	if (ATorquePlayerState* PS = PC->GetPlayerState<ATorquePlayerState>()) PS->Bay = Bay;
	// Just out of the door, facing out; the bike puts itself on the yard's line (and in co-op on its
	// start slot, two abreast, so riders coming out together aren't on top of each other).
	const FTransform At(FRotator(0.0, 90.0, 0.0), DoorPoint(World) + FVector(0.0, 300.0, 40.0));
	ATorqueBikePawn* Bike = World->SpawnActorDeferred<ATorqueBikePawn>(ATorqueBikePawn::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Bike) return;
	Bike->SetYardBay(Bay);
	Bike->FinishSpawning(At);
	if (Old)
	{
		PC->UnPossess();
		Old->Destroy();
	}
	PC->Possess(Bike);
	UE_LOG(LogTemp, Log, TEXT("Torque: %s rides bay %d's bike out into the yard"),
		PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("a player"), Bay + 1);
}

void HostWalkBackIn(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	ATorqueBikePawn* Bike = PC ? Cast<ATorqueBikePawn>(PC->GetPawn()) : nullptr;
	if (!World || !Bike || !Bike->InYard()) return;
	// Just inside the door, facing the bays (as the shed's player starts), spread out along it when
	// several come in at once.
	const FVector Door = DoorPoint(World);
	FVector At = Door + FVector(0.0, -170.0, 100.0);
	for (int32 Try = 0; Try < 5; ++Try)
	{
		const FVector Here = Door + FVector((Try % 2 ? 1.0 : -1.0) * 90.0 * ((Try + 1) / 2), -170.0, 100.0);
		bool bTaken = false;
		for (TActorIterator<ATorqueWalker> It(World); It && !bTaken; ++It)
			bTaken = FVector::Dist2D(It->GetActorLocation(), Here) < 70.0;
		if (!bTaken) { At = Here; break; }
	}
	const FRotator Facing(0.0, -90.0, 0.0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ATorqueWalker* Walker = World->SpawnActor<ATorqueWalker>(ATorqueWalker::StaticClass(), At, Facing, Params);
	if (!Walker) return;
	PC->UnPossess();
	Bike->Destroy();
	PC->Possess(Walker);
	PC->SetControlRotation(Facing);
	PC->ClientSetRotation(Facing, true);
	UE_LOG(LogTemp, Log, TEXT("Torque: %s parks in the yard and walks back in"),
		PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("a player"));
}
}
