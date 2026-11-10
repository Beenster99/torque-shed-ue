#include "Shed/TorqueYard.h"

#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePlayerState.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueWalker.h"
#include "TorqueBikePawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
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
// Where a bike can be ridden from or parked by its rider: the shed's hall (in through the roller
// door), out on the pad (Tools/shed_yard.py: the shed's walls, the pad from its front), or the wash
// bay, open to the yard at its front, and the strip of yard between it and the pad (beenster, Nov 10:
// ridden in from the test area and washed there). Not the rooms, the rest of the back yard or the dyno.
bool Ridable(const FVector& At)
{
	const bool bHall = FMath::Abs(At.X) < 630.0 && FMath::Abs(At.Y) < 530.0;
	const bool bPad = FMath::Abs(At.X) < 29700.0 && At.Y > 620.0 && At.Y < 160280.0;
	const bool bWashWay = At.X > -1580.0 && At.X < -690.0 && At.Y >= 440.0 && At.Y <= 620.0;
	return bHall || bPad || TorqueShedLayout::InWashBay(At) || bWashWay;
}
// Got on in the wash bay (on its lift, nose to the back wall, or parked in there): wheeled round
// first and on it on the apron at the bay's mouth, facing out to the yard, as there's no room to
// turn a bike in there.
const FVector YardWashMouth(-930.0, 360.0, 0.0);

// Bay's bike, which PC was riding, left at Place as if wheeled there and let go (shed_state _park):
// back in its bay if it's by its paddock stand, locked onto a lift it's over (the wash bay's: ridden
// in to be washed), else on its stand right there. Anywhere a bike isn't kept (the back yard round
// the side, or onto the wash bay's lift while it's up): back in its bay.
void Park(APlayerController* PC, int32 Bay, const FTransform& Place)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(PC);
	if (!B || !B->Exists(Bay)) return;
	const int32 Peer = PC->PlayerState ? PC->PlayerState->GetPlayerId() : B->Me();
	FString Why;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::GrabBike;
	R.Bay = Bay;
	if (!B->Apply(Peer, R, Why))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: can't park bay %d's bike: %s"), Bay + 1, *Why);
		return;
	}
	R.Kind = ETorqueShedAct::ReleaseBike;
	R.Place = Place;
	if (!Ridable(Place.GetLocation()) || !B->Apply(Peer, R, Why))
	{
		R.Place = UTorqueBuildSubsystem::HomeOf(Bay);
		B->Apply(Peer, R, Why);
	}
}

// A walker for PC, put at At facing Facing, in place of its bike (destroyed).
void OnFoot(APlayerController* PC, ATorqueBikePawn* Bike, const FVector& At, const FRotator& Facing)
{
	UWorld* World = PC->GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ATorqueWalker* Walker = World->SpawnActor<ATorqueWalker>(ATorqueWalker::StaticClass(), At, Facing, Params);
	if (!Walker) return;
	PC->UnPossess();
	Bike->Destroy();
	PC->Possess(Walker);
	PC->SetControlRotation(Facing);
	PC->ClientSetRotation(Facing, true);
}
} // namespace

namespace TorqueYard
{
// (Asked from inside the pawn being swapped, mid-tick: the swap waits for the next frame, so the
// pawn finishes its frame before it goes.)
void RideOut(APlayerController* PC, int32 Bay, bool bAtBike)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	if (PC->GetNetMode() == NM_Client)
	{
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ServerRideYard(Bay, bAtBike);
		return;
	}
	TWeakObjectPtr<APlayerController> Weak(PC);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak, Bay, bAtBike]() {
		if (Weak.IsValid()) HostRideOut(Weak.Get(), Bay, bAtBike);
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

void GetOff(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	if (PC->GetNetMode() == NM_Client)
	{
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ServerGetOffYard();
		return;
	}
	TWeakObjectPtr<APlayerController> Weak(PC);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Weak]() {
		if (Weak.IsValid()) HostGetOff(Weak.Get());
	}));
}

void HostGetOff(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	ATorqueBikePawn* Bike = PC ? Cast<ATorqueBikePawn>(PC->GetPawn()) : nullptr;
	if (!World || !Bike || !Bike->InYard()) return;
	const int32 Bay = Bike->GetRideBay();
	FVector At = Bike->GetActorLocation();
	At.Z = 0.0;
	const double Yaw = Bike->GetActorRotation().Yaw;
	Park(PC, Bay, FTransform(FRotator(0.0, Yaw, 0.0), At));
	// The rider steps off to its left (the side stand's side), facing the way it points.
	const FRotator Facing(0.0, Yaw, 0.0);
	const FVector Left = -FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	OnFoot(PC, Bike, At + Left * 90.0 + FVector(0.0, 0.0, 100.0), Facing);
	UE_LOG(LogTemp, Log, TEXT("Torque: %s gets off bay %d's bike at %s"),
		PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("a player"), Bay + 1, *At.ToCompactString());
}

void HostRideOut(APlayerController* PC, int32 Bay, bool bAtBike)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(PC);
	if (!World || !B || !B->Exists(Bay)) return;
	APawn* Old = PC->GetPawn();
	if (Old && Old->IsA<ATorqueBikePawn>()) return;   // already out on it
	// Whose bike it is, for everyone's copy of it (they draw a ridden bike from its player's bay).
	if (ATorquePlayerState* PS = PC->GetPlayerState<ATorquePlayerState>()) PS->Bay = Bay;
	// Got on where the bike stands (in its bay, or parked in the hall or out on the pad): ridden from
	// there, out of the door and back in (from the wash bay: turned round at its mouth). From the door,
	// or a bike somewhere it can't be ridden from (the dyno): just out of the door, facing out, on the yard's line (and in co-op on
	// its start slot, two abreast, so riders coming out together aren't on top of each other).
	FTransform Stands = B->BikeTransform(Bay);
	if (TorqueShedLayout::InWashBay(Stands.GetLocation())) Stands = FTransform(FRotator(0.0, 90.0, 0.0), YardWashMouth);
	const bool bFromBike = bAtBike && !B->OnDyno(Bay) && Ridable(Stands.GetLocation());
	const FTransform At = bFromBike ? FTransform(FRotator(0.0, Stands.Rotator().Yaw, 0.0), Stands.GetLocation() + FVector(0.0, 0.0, 40.0))
		: FTransform(FRotator(0.0, 90.0, 0.0), DoorPoint(World) + FVector(0.0, 300.0, 40.0));
	ATorqueBikePawn* Bike = World->SpawnActorDeferred<ATorqueBikePawn>(ATorqueBikePawn::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Bike) return;
	Bike->SetYardBay(Bay);
	if (bFromBike) Bike->SetYardStart(FVector(Stands.GetLocation().X, Stands.GetLocation().Y, 0.0), Stands.Rotator().Yaw);
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
	Park(PC, Bike->GetRideBay(), UTorqueBuildSubsystem::HomeOf(Bike->GetRideBay()));   // its bike back in its bay
	OnFoot(PC, Bike, At, FRotator(0.0, -90.0, 0.0));
	UE_LOG(LogTemp, Log, TEXT("Torque: %s parks in the yard and walks back in"),
		PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("a player"));
}
}
