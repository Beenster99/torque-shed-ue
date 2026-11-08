#include "Shed/TorquePartsStore.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ATorquePartsStore::ATorquePartsStore()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	// Solid to the walker, but not to the look: you look into the bays at the parts.
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(Root);
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ShapeCube = Cube.Object;
	Tags.Add(TEXT("PartsStore"));
}

void ATorquePartsStore::BeginPlay()
{
	Super::BeginPlay();
	Blocker->SetBoxExtent(FVector(0.5 * Depth, 0.5 * Width, 0.5 * Height));
	Blocker->SetRelativeLocation(FVector(0.0, 0.0, 0.5 * Height));
	Rebuild();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		Watch = B->OnChanged.AddUObject(this, &ATorquePartsStore::Rebuild);
}

void ATorquePartsStore::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnChanged.Remove(Watch);
	Super::EndPlay(Reason);
}

void ATorquePartsStore::BuildFrame(int32 Columns)
{
	for (UStaticMeshComponent* C : Frame)
		if (C) C->DestroyComponent();
	Frame.Reset();
	FrameColumns = Columns;
	auto Bar = [this](const FVector& Centre, const FVector& SizeCm)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(ShapeCube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(Centre);
		C->SetRelativeScale3D(SizeCm / 100.0);
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0))
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.43f, 0.44f));
		Frame.Add(C);
	};
	const double Bay = Width / FMath::Max(Columns, 1);
	for (int32 I = 0; I <= Columns; ++I)
		for (const double X : {-0.5 * Depth + 2.0, 0.5 * Depth - 2.0})
			Bar(FVector(X, -0.5 * Width + I * Bay, 0.5 * Height), FVector(4.0, 4.0, Height));
	const double Rise = (Height - 15.0) / FMath::Max(Levels, 1);
	for (int32 L = 0; L <= Levels; ++L)
		Bar(FVector(0.0, 0.0, 15.0 + L * Rise - 1.0), FVector(Depth, Width, 2.0));
}

void ATorquePartsStore::Rebuild()
{
	for (UStaticMeshComponent* C : Items)
		if (C) C->DestroyComponent();
	for (USceneComponent* H : Holders)
		if (H) H->DestroyComponent();
	for (UTextRenderComponent* T : Labels)
		if (T) T->DestroyComponent();
	Items.Reset();
	Holders.Reset();
	Labels.Reset();

	const FTorqueBuild& Build = UTorqueBuildSubsystem::BuildFor(this);
	const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
	const TArray<FName> Slots = TorqueFit::Slots(Build.Bike);
	const int32 Columns = FMath::Max(1, FMath::DivideAndRoundUp(Slots.Num(), FMath::Max(Levels, 1)));
	if (Columns != FrameColumns) BuildFrame(Columns);
	const double Bay = Width / Columns;
	const double Rise = (Height - 15.0) / FMath::Max(Levels, 1);
	const FTransform& W = Root->GetComponentTransform();
	int32 Shown = 0;
	for (int32 S = 0; S < Slots.Num(); ++S)
	{
		// Top shelf first, left to right, as you'd read it.
		const int32 Col = S % Columns, Shelf = Levels - 1 - S / Columns;
		const double BayY = -0.5 * Width + (Col + 0.5) * Bay, ShelfZ = 15.0 + Shelf * Rise;
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(Root);
		T->SetRelativeLocation(FVector(0.5 * Depth + 0.5, BayY, ShelfZ - 1.0));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextTop);
		T->SetWorldSize(3.5f);
		T->SetTextRenderColor(FColor(30, 30, 30));
		T->SetText(FText::FromString(TorqueShedModel::SlotLabel(Slots[S])));
		T->RegisterComponent();
		Labels.Add(T);

		const FName Fitted = Sub ? Sub->FittedIn(Slots[S]) : NAME_None;
		TArray<FName> Spares;
		for (const FTorqueFitOption& O : TorqueFit::Options(Build.Bike, Slots[S]))
			if (O.PartId != Fitted) Spares.Add(O.PartId);
		if (Spares.Num() == 0) continue;
		// Up to three across at the front, the rest behind them.
		const int32 Across = FMath::Min(Spares.Num(), 3);
		const int32 Rows = FMath::DivideAndRoundUp(Spares.Num(), Across);
		const double Cell = FMath::Min(Bay / Across, FMath::Min(Depth / Rows, Rise - 8.0)) - 3.0;
		for (int32 I = 0; I < Spares.Num(); ++I)
		{
			const int32 Row = I / Across, At = I % Across;
			const double X = 0.5 * Depth - (Row + 0.5) * (Depth / Rows);
			const double Y = BayY - 0.5 * Bay + (At + 0.5) * (Bay / Across);
			USceneComponent* Holder = nullptr;
			Shown += TorqueShedModel::MakeSpare(this, Root, Build, Slots[S], Spares[I], W.TransformPosition(FVector(X, Y, ShelfZ)), Cell, Items, Holder) > 0 ? 1 : 0;
			if (Holder) Holders.Add(Holder);
		}
	}
	TorqueShedModel::ApplyLooks(this, Items);
	UE_LOG(LogTemp, Log, TEXT("Torque: parts store: %d slots, %d spares on the shelves"), Slots.Num(), Shown);
}
