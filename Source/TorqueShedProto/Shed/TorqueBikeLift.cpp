#include "Shed/TorqueBikeLift.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueToolModels.h"
#include "TorqueAudio/TorqueSfx.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueBikeLiftImpl
{
// Torque Shed's lift materials (workshop_upgrades.gd: plate, grate, hazard, dark, steel).
const FLinearColor Plate(0.42f, 0.43f, 0.45f);
const FLinearColor Grate(0.30f, 0.31f, 0.32f);
const FLinearColor Hazard(0.95f, 0.70f, 0.05f);
const FLinearColor Dark(0.035f, 0.035f, 0.04f);
const FLinearColor Steel(0.55f, 0.56f, 0.58f);
}
namespace TBLI = TorqueBikeLiftImpl;

ATorqueBikeLift::ATorqueBikeLift()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = Cube.Object;
}

ATorqueBikeLift* ATorqueBikeLift::Spawn(UWorld* World, int32 InLiftId, const FTransform& Xf, bool bInWash)
{
	if (!World) return nullptr;
	ATorqueBikeLift* L = World->SpawnActorDeferred<ATorqueBikeLift>(ATorqueBikeLift::StaticClass(), Xf);
	if (!L) return nullptr;
	L->LiftId = InLiftId;
	L->bWash = bInWash;
	L->PostLocal = TorqueShedLayout::LiftPostLocal(InLiftId);
	L->FinishSpawning(Xf);
	return L;
}

void ATorqueBikeLift::SpawnMissing(UWorld* World)
{
	if (!World) return;
	for (int32 Id = 0; Id <= UTorqueBuildSubsystem::WashLift; ++Id)
		if (UTorqueBuildSubsystem::IsLift(Id) && !Find(World, Id))
			Spawn(World, Id, TorqueShedLayout::LiftXf(Id), Id == UTorqueBuildSubsystem::WashLift);
}

ATorqueBikeLift* ATorqueBikeLift::Find(const UObject* WorldContext, int32 InLiftId)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return nullptr;
	for (TActorIterator<ATorqueBikeLift> It(World); It; ++It)
		if (It->LiftId == InLiftId) return *It;
	return nullptr;
}

double ATorqueBikeLift::HeightAt(const UObject* WorldContext, const FVector& At)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return 0.0;
	for (TActorIterator<ATorqueBikeLift> It(World); It; ++It)
		if (FVector::Dist2D(It->GetActorLocation(), At) < 50.0) return It->Height();
	return 0.0;
}

UStaticMeshComponent* ATorqueBikeLift::Piece(USceneComponent* Parent, const FVector& Where, const FVector& SizeCm, const FLinearColor& Colour,
	float Metallic, float Roughness, bool bSolid, float Emissive)
{
	if (!CubeMesh || !Parent) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(CubeMesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness, Emissive));
	if (bSolid) C->SetCollisionProfileName(TEXT("BlockAll"));
	else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(Where);
	C->SetRelativeScale3D(SizeCm / 100.0);   // the engine's cube is a metre across
	C->RegisterComponent();
	return C;
}

void ATorqueBikeLift::Label(const FString& Text, const FVector& Where, float Size)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetMobility(EComponentMobility::Movable);
	T->SetupAttachment(GetRootComponent());
	T->RegisterComponent();
	T->SetRelativeLocationAndRotation(Where, FRotator::ZeroRotator);   // reads from +x
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(FColor(20, 20, 20));
	T->SetCastShadow(false);
	T->SetText(FText::FromString(Text));
}

void ATorqueBikeLift::BeginPlay()
{
	Super::BeginPlay();
	// workshop_upgrades.gd _build_lift, in cm, its long side along x (Torque Shed's z).
	const FVector PlateSize = bWash ? FVector(230.0, 30.0, 3.0) : FVector(250.0, 90.0, 2.4);
	// The dark pit frame round it, flush with the floor.
	Piece(GetRootComponent(), FVector(0.0, 0.0, 0.3), FVector(PlateSize.X + 16.0, PlateSize.Y + 16.0, 0.6), TBLI::Dark, 0.3f, 0.7f);
	// The platform: its top is the lift's height (in the floor when it's down), solid so a walker
	// stands on it and rides it.
	Deck = NewObject<USceneComponent>(this);
	Deck->SetMobility(EComponentMobility::Movable);
	Deck->SetupAttachment(GetRootComponent());
	Deck->RegisterComponent();
	if (bWash)
	{
		// A grating: cross bars between the side rails, open between them; a thin floor to stand on.
		for (int32 I = 0; I < 24; ++I)
			Piece(Deck, FVector(-PlateSize.X * 0.5 + 5.0 + I * (PlateSize.X - 10.0) / 23.0, 0.0, -PlateSize.Z * 0.5), FVector(2.0, PlateSize.Y, PlateSize.Z), TBLI::Grate, 0.9f, 0.5f);
		UStaticMeshComponent* Floor = Piece(Deck, FVector(0.0, 0.0, -PlateSize.Z * 0.5), PlateSize, TBLI::Grate, 0.9f, 0.5f, true);
		if (Floor) Floor->SetVisibility(false);
	}
	else Piece(Deck, FVector(0.0, 0.0, -PlateSize.Z * 0.5), PlateSize, TBLI::Plate, 0.85f, 0.42f, true);
	// Hazard rails down its sides, and a wheel chock at its front.
	for (const double Side : {-1.0, 1.0})
		Piece(Deck, FVector(0.0, Side * (PlateSize.Y * 0.5 + 2.0), -PlateSize.Z * 0.5 + 0.2), FVector(PlateSize.X + 5.0, 5.0, PlateSize.Z + 0.4), TBLI::Hazard, 0.2f, 0.5f);
	Piece(Deck, FVector(PlateSize.X * 0.5 - 8.0, 0.0, 6.0), FVector(10.0, 24.0, 12.0), TBLI::Hazard, 0.2f, 0.5f);
	// Scissor arms: a crossed pair each side, opening as the platform goes up.
	for (const double Side : {-1.0, 1.0})
		for (const double Lean : {-1.0, 1.0})
		{
			UStaticMeshComponent* Arm = Piece(GetRootComponent(), FVector(0.0, Side * (bWash ? 12.0 : 30.0), 0.0), FVector(ArmLength, 5.0, 4.0), TBLI::Steel, 1.0f, 0.35f);
			if (!Arm) continue;
			Arms.Add(Arm);
			ArmLean.Add(Lean);
		}
	// The control post beside it, its buttons on the face towards the bike's nose: green up, red down.
	Piece(GetRootComponent(), PostLocal + FVector(0.0, 0.0, 55.0), FVector(14.0, 18.0, 110.0), TBLI::Hazard, 0.2f, 0.5f, true);
	Piece(GetRootComponent(), PostLocal + FVector(7.5, 0.0, 100.0), FVector(2.0, 6.0, 6.0), FLinearColor(0.1f, 0.6f, 0.15f), 0.0f, 0.4f, false, 0.6f);
	Piece(GetRootComponent(), PostLocal + FVector(7.5, 0.0, 88.0), FVector(2.0, 6.0, 6.0), FLinearColor(0.7f, 0.05f, 0.04f), 0.0f, 0.4f, false, 0.6f);
	Piece(GetRootComponent(), PostLocal + FVector(7.2, 0.0, 72.0), FVector(0.6, 15.0, 12.0), FLinearColor(0.9f, 0.9f, 0.88f), 0.0f, 0.6f);
	Label(TEXT("UP"), PostLocal + FVector(7.8, -6.5, 100.0), 2.6f);
	Label(TEXT("DN"), PostLocal + FVector(7.8, -6.5, 88.0), 2.6f);
	Label(TEXT("LIFT"), PostLocal + FVector(7.8, 0.0, 72.0), 4.5f);
	// Its motor (Sfx "door", as Torque Shed's), under the platform.
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Motor = Sfx->MakeLoop(GetRootComponent(), FVector(0.0, 0.0, 30.0));
	// Where the shed has it now, at once (a lift that's up when the game comes back is up).
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	bWantUp = B && B->LiftUp(LiftId);
	H = bWantUp ? Top() : 0.0;
	Pose();
	UE_LOG(LogTemp, Log, TEXT("Torque lift: %s at (%.0f, %.0f), %s"), *UTorqueBuildSubsystem::LiftName(LiftId), GetActorLocation().X, GetActorLocation().Y,
		bWantUp ? TEXT("up") : TEXT("down"));
}

void ATorqueBikeLift::EndPlay(const EEndPlayReason::Type Why)
{
	if (Motor != INDEX_NONE)
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->FreeLoop(Motor);
	Motor = INDEX_NONE;
	Super::EndPlay(Why);
}

void ATorqueBikeLift::Pose()
{
	// workshop_upgrades.gd _pose_lift: the platform at H, the arms half way up, leaning to meet it.
	if (Deck) Deck->SetRelativeLocation(FVector(0.0, 0.0, H));
	const double A = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(H / ArmLength, 0.0, 1.0)));
	for (int32 I = 0; I < Arms.Num(); ++I)
	{
		if (!Arms[I]) continue;
		const FVector Was = Arms[I]->GetRelativeLocation();
		// 3 cm down, so they're in the floor (under the plate) when it's down.
		Arms[I]->SetRelativeLocationAndRotation(FVector(Was.X, Was.Y, H * 0.5 - 3.0), FRotator(A * ArmLean[I], 0.0, 0.0));
	}
}

void ATorqueBikeLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const bool bUp = B && B->LiftUp(LiftId);
	UTorqueSfx* Sfx = UTorqueSfx::Get(this);
	if (bUp != bWantUp)
	{
		bWantUp = bUp;
		// Someone pressed its button.
		if (Sfx) Sfx->PlayAt(TEXT("switch"), GetActorTransform().TransformPosition(PostLocal + FVector(8.0, 0.0, bUp ? 100.0 : 88.0)), -6.0f);
	}
	const double Want = bWantUp ? Top() : 0.0;
	const double Was = H;
	const double Step = Speed * (double)DeltaSeconds;
	H = Want > H ? FMath::Min(Want, H + Step) : FMath::Max(Want, H - Step);
	const bool bNowMoving = !FMath::IsNearlyEqual(H, Was);
	if (bNowMoving) Pose();
	if (bNowMoving != bMoving)
	{
		bMoving = bNowMoving;
		if (Sfx && Motor != INDEX_NONE) Sfx->SetLoop(Motor, bMoving ? TEXT("door") : TEXT(""));
	}
}
