#include "Shed/TorquePartsBench.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Components/StaticMeshComponent.h"

ATorquePartsBench::ATorquePartsBench()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Tags.Add(TEXT("PartsBench"));
}

void ATorquePartsBench::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		Watch = B->OnChanged.AddUObject(this, &ATorquePartsBench::Rebuild);
}

void ATorquePartsBench::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnChanged.Remove(Watch);
	Super::EndPlay(Reason);
}

void ATorquePartsBench::Rebuild()
{
	for (UStaticMeshComponent* C : Items)
		if (C) C->DestroyComponent();
	for (USceneComponent* H : Holders)
		if (H) H->DestroyComponent();
	Items.Reset();
	Holders.Reset();

	const FTorqueBuild& Build = UTorqueBuildSubsystem::BuildFor(this);
	const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
	// The last few parts taken off the bike (the rest are on the parts store's shelves).
	TArray<FTorqueRemoved> Spares;
	if (Sub)
		for (const FTorqueRemoved& R : Sub->RecentlyRemoved())
			if (Sub->FittedIn(R.Slot) != R.Part) Spares.Add(R);
	const int32 Places = FMath::Max(Spares.Num(), 1);
	const double Step = Length / Places;
	const FTransform& W = Root->GetComponentTransform();
	for (int32 I = 0; I < Spares.Num(); ++I)
	{
		USceneComponent* Holder = nullptr;
		const FVector At = W.TransformPosition(FVector(-0.5 * Length + (I + 0.5) * Step, 0.0, 0.0));
		TorqueShedModel::MakeSpare(this, Root, Build, Spares[I].Slot, Spares[I].Part, At, Step - 10.0, Items, Holder);
		if (Holder) Holders.Add(Holder);
	}
	TorqueShedModel::ApplyLooks(this, Items);   // a part off the bike keeps its dirt
	UE_LOG(LogTemp, Log, TEXT("Torque: %d spare parts on the bench"), Spares.Num());
}
