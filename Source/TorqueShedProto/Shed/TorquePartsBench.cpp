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
	// Every option that isn't fitted, as it would be on the bike, then laid on the bench.
	struct FSpare { FName Slot, Part; };
	TArray<FSpare> Spares;
	for (const FName& Slot : TorqueFit::Slots(Build.Bike))
	{
		const FName Fitted = Sub ? Sub->FittedIn(Slot) : NAME_None;
		for (const FTorqueFitOption& O : TorqueFit::Options(Build.Bike, Slot))
		{
			if (O.PartId == Fitted || (Fitted.IsNone() && O.bStock)) continue;
			Spares.Add({Slot, O.PartId});
		}
	}
	const double Step = Spares.Num() > 0 ? Length / Spares.Num() : Length;
	for (int32 I = 0; I < Spares.Num(); ++I)
	{
		FTorqueBuild Trial = Build;
		Trial.Fitted.Add(Spares[I].Slot, Spares[I].Part);
		USceneComponent* Holder = NewObject<USceneComponent>(this);
		Holder->SetupAttachment(Root);
		Holder->RegisterComponent();
		Holders.Add(Holder);
		TArray<UStaticMeshComponent*> Made;
		TorqueShedModel::MakeParts(this, Holder, Trial, Spares[I].Slot, Made);
		if (Made.Num() == 0) continue;   // nothing to show (no model yet, or an option that takes parts off)
		// Lay the spare down: centred in its share of the bench, resting on the top.
		FBox Box(ForceInit);
		for (UStaticMeshComponent* C : Made)
		{
			C->UpdateBounds();
			Box += C->Bounds.GetBox();
			C->ComponentTags.Add(FName(*FString::Printf(TEXT("Option:%s"), *Spares[I].Part.ToString())));
			Items.Add(C);
		}
		const FTransform& W = Root->GetComponentTransform();
		const FVector Want = W.TransformPosition(FVector(-0.5 * Length + (I + 0.5) * Step, 0.0, 0.0));
		const FVector Centre = Box.GetCenter();
		Holder->SetWorldLocation(Holder->GetComponentLocation() + FVector(Want.X - Centre.X, Want.Y - Centre.Y, Want.Z + 1.0 - Box.Min.Z));
	}
	TorqueShedModel::ApplyLooks(this, Items);   // a part off the bike keeps its dirt
	UE_LOG(LogTemp, Log, TEXT("Torque: %d spare parts on the bench"), Spares.Num());
}
