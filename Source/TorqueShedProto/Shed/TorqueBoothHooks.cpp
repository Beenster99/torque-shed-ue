#include "Shed/TorqueBoothHooks.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueToolModels.h"
#include "Shed/TorqueWalker.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueBoothHooksImpl
{
constexpr double RailZ = 235.0;             // ue_build_shed.py BOOTH_RAIL_Z
constexpr double MountBelow = 3.5;          // the hanger's underside, under the rail
constexpr double Gravity = 981.0;           // cm/s^2
constexpr double MaxAngle = 0.6;            // radians: a hard shove, no more
const FLinearColor Chrome(0.88f, 0.89f, 0.92f);
}

namespace TBHI = TorqueBoothHooksImpl;

ATorqueBoothHooks::ATorqueBoothHooks()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CylinderMesh = Cylinder.Object;
}

FVector ATorqueBoothHooks::PivotOf(int32 Hook)
{
	const FVector At = TorqueShedLayout::HookAt(Hook);
	return FVector(At.X, At.Y, TBHI::RailZ - TBHI::MountBelow);
}

void ATorqueBoothHooks::BeginPlay()
{
	Super::BeginPlay();
	Hooks.SetNum(TorqueShedLayout::HookCount);
	for (int32 I = 0; I < TorqueShedLayout::HookCount; ++I)
	{
		USceneComponent* S = NewObject<USceneComponent>(this);
		S->SetupAttachment(GetRootComponent());
		S->SetWorldLocation(PivotOf(I));
		S->RegisterComponent();
		Swings.Add(S);
		auto Piece = [this, S](const FVector& SizeCm, const FRotator& Rot)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(CylinderMesh);
			C->SetMaterial(0, TorqueToolModels::Finish(this, TBHI::Chrome, 1.0f, 0.15f));
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->SetupAttachment(S);
			C->SetRelativeRotation(Rot);
			C->SetRelativeScale3D(SizeCm / 100.0);
			C->RegisterComponent();
			return C;
		};
		Wires.Add(Piece(FVector(0.8, 0.8, 100.0), FRotator::ZeroRotator));
		Tips.Add(Piece(FVector(5.6, 5.6, 0.8), FRotator(0.0, 0.0, 90.0)));   // the hook: a flat ring on its side
	}
}

FQuat ATorqueBoothHooks::TurnOf(int32 Hook) const
{
	if (!Hooks.IsValidIndex(Hook)) return FQuat::Identity;
	const FHook& H = Hooks[Hook];
	return FQuat(FVector::ForwardVector, H.Angle.X) * FQuat(FVector::RightVector, H.Angle.Y);
}

bool ATorqueBoothHooks::Swing(const UObject* WorldContext, int32 Hook, FVector& OutPivot, FQuat& OutTurn)
{
	OutPivot = PivotOf(Hook);
	OutTurn = FQuat::Identity;
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return false;
	for (TActorIterator<ATorqueBoothHooks> It(World); It; ++It)
	{
		OutTurn = It->TurnOf(Hook);
		return true;
	}
	return false;
}

void ATorqueBoothHooks::Push(int32 Hook, const FVector2D& Rate)
{
	if (Hooks.IsValidIndex(Hook)) Hooks[Hook].Rate += Rate;
}

void ATorqueBoothHooks::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	for (int32 I = 0; I < Hooks.Num() && I < Swings.Num(); ++I)
	{
		FHook& H = Hooks[I];
		const FVector Pivot = PivotOf(I);
		const double Tip = B->HookTipOf(I);
		const double Wire = FMath::Max(Pivot.Z - Tip - 0.4, 5.0);
		// What hangs on it: a part hung on or taken off sets it going, a coat of paint nudges it.
		const int32 On = B->ItemAtSpot(FName(*FString::Printf(TEXT("hook_%d"), I)));
		const FTorqueItem* It = B->Item(On);
		const FString Sig = It ? FString::Printf(TEXT("%d:%s:%s:%.2f"), On, *It->Colour.ToString(), *It->Finish.ToString(), It->Flat) : FString();
		if (Sig != H.Sig)
		{
			const bool bSameItem = !H.Sig.IsEmpty() && !Sig.IsEmpty() && H.Sig.StartsWith(FString::Printf(TEXT("%d:"), On));
			const double Kick = bSameItem ? 0.08 : 0.5;
			H.Rate += FVector2D(FMath::FRandRange(-Kick, Kick), FMath::FRandRange(-Kick, Kick));
			H.Sig = Sig;
		}
		// Someone walking into the part (or the bare hook): pushed the way they're going.
		const FVector Low = Pivot + TurnOf(I).RotateVector(FVector(0.0, 0.0, -Wire - (It ? 30.0 : 0.0)));
		for (TActorIterator<ATorqueWalker> W(GetWorld()); W; ++W)
		{
			const FVector V = W->GetVelocity();
			if (V.SizeSquared2D() < 2500.0 || FVector::Dist2D(W->GetActorLocation(), Low) > (It ? 55.0 : 30.0)) continue;
			// (A turn about +X swings the bottom towards +Y; about +Y, towards -X.)
			H.Rate += FVector2D(V.Y, -V.X) * 0.0004 * Dt * 30.0;
		}
		// The pendulum: its length to the part's middle (or the hook), damped by the air.
		const double Len = Wire + (It ? 25.0 : 2.0);
		const double W2 = TBHI::Gravity / Len;
		const double Damp = 2.0 * (It ? 0.05 : 0.03) * FMath::Sqrt(W2);
		const int32 Steps = 4;
		const double H2 = Dt / Steps;
		for (int32 S = 0; S < Steps; ++S)
		{
			H.Rate.X += (-W2 * FMath::Sin(H.Angle.X) - Damp * H.Rate.X) * H2;
			H.Rate.Y += (-W2 * FMath::Sin(H.Angle.Y) - Damp * H.Rate.Y) * H2;
			H.Angle += H.Rate * H2;
		}
		if (FMath::Abs(H.Angle.X) > TBHI::MaxAngle) { H.Angle.X = FMath::Clamp(H.Angle.X, -TBHI::MaxAngle, TBHI::MaxAngle); H.Rate.X = 0.0; }
		if (FMath::Abs(H.Angle.Y) > TBHI::MaxAngle) { H.Angle.Y = FMath::Clamp(H.Angle.Y, -TBHI::MaxAngle, TBHI::MaxAngle); H.Rate.Y = 0.0; }
		Swings[I]->SetWorldLocationAndRotation(Pivot, TurnOf(I));
		if ((float)Tip != H.Shown)
		{
			H.Shown = (float)Tip;
			if (Wires.IsValidIndex(I) && Wires[I])
			{
				Wires[I]->SetRelativeLocation(FVector(0.0, 0.0, -Wire * 0.5));
				Wires[I]->SetRelativeScale3D(FVector(0.008, 0.008, Wire / 100.0));
			}
			if (Tips.IsValidIndex(I) && Tips[I]) Tips[I]->SetRelativeLocation(FVector(0.0, 0.0, -Wire - 0.2));
		}
	}
}
