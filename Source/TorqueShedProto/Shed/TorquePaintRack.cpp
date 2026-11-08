#include "Shed/TorquePaintRack.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

const TArray<FTorquePaint>& ATorquePaintRack::Paints()
{
	// Linear colours of some Triumph-ish paints and a few classics.
	static const TArray<FTorquePaint> List = {
		{TEXT("Jet Black"), FLinearColor(0.012f, 0.012f, 0.013f)},
		{TEXT("Carnival Red"), FLinearColor(0.42f, 0.02f, 0.015f)},
		{TEXT("Competition Green"), FLinearColor(0.02f, 0.12f, 0.05f)},
		{TEXT("Cobalt Blue"), FLinearColor(0.02f, 0.06f, 0.30f)},
		{TEXT("Storm Grey"), FLinearColor(0.10f, 0.105f, 0.11f)},
		{TEXT("Silver Ice"), FLinearColor(0.60f, 0.61f, 0.62f)},
		{TEXT("Fusion White"), FLinearColor(0.80f, 0.79f, 0.76f)},
		{TEXT("Baja Orange"), FLinearColor(0.70f, 0.18f, 0.02f)},
	};
	return List;
}

ATorquePaintRack::ATorquePaintRack()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Board = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Board"));
	Board->SetupAttachment(Root);
	Board->SetStaticMesh(Cube.Object);
	const int32 N = Paints().Num();
	const double Pitch = 16.0, Width = N * Pitch + 10.0;
	Board->SetRelativeLocation(FVector(1.0, 0.0, 0.0));
	Board->SetRelativeScale3D(FVector(2.0, Width, 40.0) / 100.0);
	for (int32 I = 0; I < N; ++I)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Card%d"), I));
		C->SetupAttachment(Root);
		C->SetStaticMesh(Cube.Object);
		C->SetRelativeLocation(FVector(3.0, -0.5 * Width + 5.0 + (I + 0.5) * Pitch, 4.0));
		C->SetRelativeScale3D(FVector(1.0, 12.0, 22.0) / 100.0);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cards.Add(C);
	}
	Tags.Add(TEXT("PaintRack"));
}

void ATorquePaintRack::BeginPlay()
{
	Super::BeginPlay();
	// The engine's basic shape material takes a Color.
	if (UMaterialInstanceDynamic* M = Board->CreateDynamicMaterialInstance(0))
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 0.28f, 0.18f));
	for (int32 I = 0; I < Cards.Num(); ++I)
		if (UMaterialInstanceDynamic* M = Cards[I]->CreateDynamicMaterialInstance(0))
			M->SetVectorParameterValue(TEXT("Color"), Paints()[I].Colour);
}
