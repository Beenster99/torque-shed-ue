#include "Shed/TorqueBikeLift.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
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
// The lift's paint and parts (a workshop lift table's: painted steel, a rubber mat, checker plate,
// a chrome ram), and Torque Shed's grate and hazard yellow.
const FLinearColor Paint(0.50f, 0.035f, 0.03f);
const FLinearColor Rubber(0.03f, 0.03f, 0.03f);
const FLinearColor Base(0.10f, 0.10f, 0.11f);
const FLinearColor Alu(0.62f, 0.63f, 0.65f);
const FLinearColor Chrome(0.90f, 0.90f, 0.92f);
const FLinearColor Black(0.02f, 0.02f, 0.02f);
const FLinearColor Grate(0.30f, 0.31f, 0.32f);
const FLinearColor Hazard(0.95f, 0.70f, 0.05f);
// The scissor (cm, the lift's frame): its fixed pivots' x at the front, the low pivots' height,
// how far under the platform's top the high pivots are; the ram's foot, its barrel's length, where
// on the inner arms it pushes (from their low pivot); the ramp's length.
constexpr double PivotX = 80.0, LowPivotZ = 5.0, HighPivotDrop = 11.0;
constexpr double RamFootX = PivotX - 0.95 * 150.0, RamFootZ = 7.0, BarrelLength = 45.0, RamAt = 0.45;
constexpr double RampLength = TorqueShedLayout::LiftRampLength;
// The vice's stop: how far in front of the jaws' middle its face is, and how high its middle is.
constexpr double StopFace = 17.5, StopZ = 11.5;
}
namespace TBLI = TorqueBikeLiftImpl;

ATorqueBikeLift::ATorqueBikeLift()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
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
	float Metallic, float Roughness, bool bSolid, float Emissive, bool bCylinder)
{
	UStaticMesh* Mesh = bCylinder ? CylinderMesh : CubeMesh;
	if (!Mesh || !Parent) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
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

void ATorqueBikeLift::Span(UStaticMeshComponent* C, const FVector& A, const FVector& B, double Diameter)
{
	// The engine's cylinder is a metre tall along z and a metre across.
	if (!C) return;
	const FVector Along = B - A;
	const double Length = FMath::Max(Along.Size(), 0.1);
	C->SetRelativeLocationAndRotation((A + B) * 0.5, FRotationMatrix::MakeFromZ(Along / Length).Rotator());
	C->SetRelativeScale3D(FVector(Diameter / 100.0, Diameter / 100.0, Length / 100.0));
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
	USceneComponent* Root = GetRootComponent();
	const double L = DeckLength(), W = DeckWidth(), Y = ArmY();
	const double RailY = Y + 12.0;
	// --- The base frame on the floor: two rails (the rollers' tracks), cross members, the front
	// pivots' brackets, and the power pack (pump and motor) with its hose to the ram.
	for (const double Side : {-1.0, 1.0})
	{
		Piece(Root, FVector(0.0, Side * RailY, 3.0), FVector(200.0, 8.0, 6.0), TBLI::Base, 0.4f, 0.55f);
		Piece(Root, FVector(TBLI::PivotX, Side * (Y + 8.5), 5.0), FVector(9.0, 3.0, 10.0), TBLI::Base, 0.4f, 0.55f);
	}
	for (const double X : {92.0, -95.0})
		Piece(Root, FVector(X, 0.0, 2.5), FVector(6.0, RailY * 2.0 + 8.0, 5.0), TBLI::Base, 0.4f, 0.55f);
	const FVector Pump = bWash ? FVector(-40.0, RailY + 26.0, 0.0) : FVector(45.0, 0.0, 0.0);
	Piece(Root, Pump + FVector(0.0, 0.0, 5.0), FVector(30.0, 20.0, 10.0), FLinearColor(0.08f, 0.12f, 0.2f), 0.3f, 0.5f);
	UStaticMeshComponent* PumpMotor = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Black, 0.4f, 0.45f, false, 0.0f, true);
	Span(PumpMotor, Pump + FVector(15.0, 0.0, 5.0), Pump + FVector(27.0, 0.0, 5.0), 9.0);
	UStaticMeshComponent* Hose = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Black, 0.0f, 0.8f, false, 0.0f, true);
	Span(Hose, Pump + FVector(-15.0, 0.0, 3.0), FVector(TBLI::RamFootX, 0.0, TBLI::RamFootZ - 2.0), 1.6);
	// --- The scissor: an inner and an outer arm each side, a pin through where they cross, a cross
	// tube on each arm pair's sliding end (on rollers in the tracks), and the ram under the middle
	// pushing a cross tube on the inner arms.
	for (const double Side : {-1.0, 1.0})
	{
		InnerArms.Add(Piece(Root, FVector(0.0, Side * Y, 0.0), FVector(ArmLength + 8.0, 3.0, 7.0), TBLI::Paint, 0.3f, 0.45f));
		OuterArms.Add(Piece(Root, FVector(0.0, Side * (Y + 5.0), 0.0), FVector(ArmLength + 8.0, 3.0, 7.0), TBLI::Paint, 0.3f, 0.45f));
	}
	Pin = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Chrome, 1.0f, 0.2f, false, 0.0f, true);
	LowRoller = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Base, 0.5f, 0.45f, false, 0.0f, true);
	HighRoller = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Base, 0.5f, 0.45f, false, 0.0f, true);
	RamTube = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Paint, 0.3f, 0.45f, false, 0.0f, true);
	RamBarrel = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Black, 0.4f, 0.4f, false, 0.0f, true);
	RamRod = Piece(Root, FVector::ZeroVector, FVector::OneVector, TBLI::Chrome, 1.0f, 0.08f, false, 0.0f, true);

	// --- The platform: its top is the lift's height. Solid, so a walker stands on it and rides it.
	Deck = NewObject<USceneComponent>(this);
	Deck->SetMobility(EComponentMobility::Movable);
	Deck->SetupAttachment(Root);
	Deck->RegisterComponent();
	if (bWash)
	{
		// A grating: cross bars between the side members, open between them; a hidden floor to stand on.
		for (int32 I = 0; I < 23; ++I)
			Piece(Deck, FVector(-L * 0.5 + 5.0 + I * (L - 10.0) / 22.0, 0.0, -1.5), FVector(2.0, W - 4.0, 3.0), TBLI::Grate, 0.9f, 0.5f);
		UStaticMeshComponent* Floor = Piece(Deck, FVector(0.0, 0.0, -1.5), FVector(L, W, 3.0), TBLI::Grate, 0.9f, 0.5f, true);
		if (Floor) Floor->SetVisibility(false);
	}
	else
	{
		Piece(Deck, FVector(0.0, 0.0, -1.5), FVector(L, W, 3.0), TBLI::Paint, 0.3f, 0.45f, true);
		Piece(Deck, FVector(-5.0, 0.0, 0.25), FVector(L - 26.0, W - 10.0, 0.5), TBLI::Rubber, 0.0f, 0.85f);
		// The rear wheel drop-out panel: its seams across the mat, and the two pins that hold it.
		for (const double X : {-L * 0.5 + 18.0, -L * 0.5 + 58.0})
			Piece(Deck, FVector(X, 0.0, 0.55), FVector(0.8, W - 10.0, 0.2), TBLI::Black, 0.0f, 0.9f);
		for (const double Side : {-1.0, 1.0})
			Piece(Deck, FVector(-L * 0.5 + 38.0, Side * (W * 0.5 - 2.5), 0.6), FVector(4.0, 2.5, 1.2), TBLI::Chrome, 1.0f, 0.2f);
		// Checker plate wings each side, on their brackets.
		for (const double Side : {-1.0, 1.0})
		{
			Piece(Deck, FVector(-8.0, Side * (W * 0.5 + 11.0), -1.0), FVector(L - 70.0, 22.0, 2.0), TBLI::Alu, 1.0f, 0.38f, true);
			for (const double X : {-L * 0.5 + 45.0, L * 0.5 - 60.0})
				Piece(Deck, FVector(X, Side * (W * 0.5 + 5.0), -4.0), FVector(4.0, 10.0, 4.0), TBLI::Paint, 0.3f, 0.45f);
		}
	}
	// The table's box skirt.
	for (const double Side : {-1.0, 1.0})
	{
		Piece(Deck, FVector(0.0, Side * (W * 0.5 - 1.5), -6.0), FVector(L, 3.0, 9.0), TBLI::Paint, 0.3f, 0.45f);
		Piece(Deck, FVector(Side * (L * 0.5 - 1.5), 0.0, -6.0), FVector(3.0, W, 9.0), TBLI::Paint, 0.3f, 0.45f);
	}
	// The wheel vice: a carriage on a slotted rail down the platform's middle that slides to the
	// front tyre of the bike on it (Tick), its base plate, the front stop, the screw and its crank,
	// and two side jaws that close on the tyre (PoseJaws). Its frame's origin is the jaws' middle.
	Piece(Deck, FVector(L * 0.25 - 10.0, 0.0, 0.3), FVector(L * 0.5 + 10.0, 10.0, 0.6), TBLI::Base, 0.5f, 0.5f);
	for (const double Side : {-1.0, 1.0})
		Piece(Deck, FVector(L * 0.25 - 10.0, Side * 3.5, 0.65), FVector(L * 0.5 + 10.0, 1.0, 0.1), TBLI::Black, 0.0f, 0.9f);
	Vice = NewObject<USceneComponent>(this);
	Vice->SetMobility(EComponentMobility::Movable);
	Vice->SetupAttachment(Deck);
	Vice->RegisterComponent();
	Piece(Vice, FVector(4.0, 0.0, 0.75), FVector(44.0, FMath::Min(W - 4.0, 40.0), 1.5), TBLI::Paint, 0.3f, 0.45f);
	Piece(Vice, FVector(TBLI::StopFace + 1.5, 0.0, TBLI::StopZ), FVector(3.0, 24.0, 20.0), TBLI::Paint, 0.3f, 0.45f);
	UStaticMeshComponent* Screw = Piece(Vice, FVector::ZeroVector, FVector::OneVector, TBLI::Chrome, 1.0f, 0.2f, false, 0.0f, true);
	Span(Screw, FVector(0.0, -20.0, 4.0), FVector(0.0, 20.0, 4.0), 2.0);
	UStaticMeshComponent* Crank = Piece(Vice, FVector::ZeroVector, FVector::OneVector, TBLI::Black, 0.3f, 0.5f, false, 0.0f, true);
	Span(Crank, FVector(0.0, W * 0.5 + 0.5, 4.0), FVector(0.0, W * 0.5 + 2.5, 4.0), 10.0);
	for (int32 I = 0; I < 2; ++I)
		Jaws.Add(Piece(Vice, FVector(0.0, 0.0, 10.5), FVector(30.0, 2.0, 18.0), TBLI::Base, 0.6f, 0.4f));
	// --- The ramp at the back, hinged on the table: down to the floor when it's down, folding up as
	// a stop as it rises.
	Ramp = Piece(Root, FVector::ZeroVector, FVector(TBLI::RampLength, W - 6.0, 2.5), bWash ? TBLI::Grate : TBLI::Alu, 1.0f, 0.38f, true);

	// --- The control box on its stand: up and down buttons and the emergency stop.
	Piece(Root, PostLocal + FVector(0.0, 0.0, 0.75), FVector(24.0, 24.0, 1.5), TBLI::Base, 0.4f, 0.55f);
	Piece(Root, PostLocal + FVector(0.0, 0.0, 48.0), FVector(6.0, 6.0, 95.0), TBLI::Base, 0.4f, 0.55f, true);
	Piece(Root, PostLocal + FVector(0.0, 0.0, 106.0), FVector(12.0, 18.0, 22.0), TBLI::Hazard, 0.2f, 0.5f, true);
	Piece(Root, PostLocal + FVector(6.4, 3.0, 111.0), FVector(1.5, 5.0, 5.0), FLinearColor(0.1f, 0.6f, 0.15f), 0.0f, 0.4f, false, 0.6f);
	Piece(Root, PostLocal + FVector(6.4, 3.0, 103.0), FVector(1.5, 5.0, 5.0), FLinearColor(0.1f, 0.1f, 0.1f), 0.0f, 0.4f, false, 0.0f);
	UStaticMeshComponent* Stop = Piece(Root, FVector::ZeroVector, FVector::OneVector, FLinearColor(0.75f, 0.03f, 0.02f), 0.0f, 0.4f, false, 0.2f, true);
	Span(Stop, PostLocal + FVector(0.0, 0.0, 117.0), PostLocal + FVector(0.0, 0.0, 120.5), 7.0);
	Label(TEXT("UP"), PostLocal + FVector(7.3, -4.0, 111.0), 2.6f);
	Label(TEXT("DN"), PostLocal + FVector(7.3, -4.0, 103.0), 2.6f);
	Label(TEXT("LIFT"), PostLocal + FVector(6.1, 0.0, 97.0), 3.2f);

	// Its motor (Sfx "door", as Torque Shed's), at the power pack.
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Motor = Sfx->MakeLoop(Root, Pump + FVector(0.0, 0.0, 10.0));
	// Where the shed has it now, at once (a lift that's up when the game comes back is up).
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	bWantUp = B && B->LiftUp(LiftId);
	H = bWantUp ? Top() : LowTop();
	JawGap = (B && B->BikeOnLift(LiftId) != INDEX_NONE) ? 0.0 : 1.0;
	ViceX = ViceRest();
	if (Vice) Vice->SetRelativeLocation(FVector(ViceX, 0.0, 0.0));
	Pose();
	PoseJaws();
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
	// The platform at H; the scissor opened to meet it: each pair's fixed pivots at the front, their
	// other ends rolling back along the tracks and the table's underside as it rises.
	if (Deck) Deck->SetRelativeLocation(FVector(0.0, 0.0, H));
	const double Y = ArmY();
	const double LowZ = TBLI::LowPivotZ, HighZ = FMath::Max(H - TBLI::HighPivotDrop, LowZ);
	const double A = FMath::Asin(FMath::Clamp((HighZ - LowZ) / ArmLength, 0.0, 1.0));
	const double Reach = ArmLength * FMath::Cos(A);
	const double BackX = TBLI::PivotX - Reach;
	const double MidX = TBLI::PivotX - Reach * 0.5, MidZ = (LowZ + HighZ) * 0.5;
	const double Deg = FMath::RadiansToDegrees(A);
	for (UStaticMeshComponent* Arm : InnerArms)   // low at the front, high at the back
		if (Arm) Arm->SetRelativeLocationAndRotation(FVector(MidX, Arm->GetRelativeLocation().Y, MidZ), FRotator(-Deg, 0.0, 0.0));
	for (UStaticMeshComponent* Arm : OuterArms)   // high at the front, low at the back
		if (Arm) Arm->SetRelativeLocationAndRotation(FVector(MidX, Arm->GetRelativeLocation().Y, MidZ), FRotator(Deg, 0.0, 0.0));
	const double Across = Y + 8.0;
	Span(Pin, FVector(MidX, -Across, MidZ), FVector(MidX, Across, MidZ), 4.0);
	Span(LowRoller, FVector(BackX, -Across, LowZ), FVector(BackX, Across, LowZ), 5.5);
	Span(HighRoller, FVector(BackX, -Y, HighZ), FVector(BackX, Y, HighZ), 5.5);
	// The ram: its foot on the base's back, its rod pushing the inner arms' cross tube.
	const FVector Push(TBLI::PivotX - TBLI::RamAt * Reach, 0.0, LowZ + TBLI::RamAt * (HighZ - LowZ));
	Span(RamTube, FVector(Push.X, -Y, Push.Z), FVector(Push.X, Y, Push.Z), 4.0);
	const FVector Foot(TBLI::RamFootX, 0.0, TBLI::RamFootZ);
	const FVector Dir = (Push - Foot).GetSafeNormal();
	Span(RamBarrel, Foot, Foot + Dir * TBLI::BarrelLength, 8.0);
	Span(RamRod, Foot + Dir * (TBLI::BarrelLength - 5.0), Push, 3.5);
	// The ramp: from the table's back edge down to the floor when it's down, folded up to a stop at the top.
	if (Ramp)
	{
		const double Hinge = H - 1.5;
		const double DownAngle = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((LowTop() - 1.5) / TBLI::RampLength, 0.0, 1.0)));
		const double T = FMath::Clamp((H - LowTop()) / FMath::Max(Top() - LowTop(), 1.0), 0.0, 1.0);
		const double Angle = FMath::Lerp(DownAngle, -80.0, FMath::Clamp(T * 1.6, 0.0, 1.0));
		const double Rad = FMath::DegreesToRadians(Angle);
		const FVector Pivot(-DeckLength() * 0.5, 0.0, Hinge);
		Ramp->SetRelativeLocationAndRotation(Pivot - FVector(FMath::Cos(Rad), 0.0, FMath::Sin(Rad)) * (TBLI::RampLength * 0.5), FRotator(Angle, 0.0, 0.0));
	}
}

void ATorqueBikeLift::PoseJaws()
{
	// Inner faces 7 cm either side of the middle closed (a tyre's grip), 16 cm open.
	for (int32 I = 0; I < Jaws.Num(); ++I)
		if (Jaws[I])
		{
			const FVector Was = Jaws[I]->GetRelativeLocation();
			Jaws[I]->SetRelativeLocation(FVector(Was.X, (I == 0 ? -1.0 : 1.0) * (8.0 + 9.0 * JawGap), Was.Z));
		}
}

double ATorqueBikeLift::ViceWant(const UTorqueBuildSubsystem* B) const
{
	const int32 On = B ? B->BikeOnLift(LiftId) : INDEX_NONE;
	UWorld* Here = GetWorld();
	if (On == INDEX_NONE || !Here) return ViceRest();
	for (TActorIterator<ATorqueShedBike> It(Here); It; ++It)
	{
		if (It->Bay != On) continue;
		// Its front axle, where the shed has the bike (not where it's drawn gliding there), in the
		// lift's frame; facing the other way on it, the vice stays back.
		const FVector Axle = GetActorTransform().InverseTransformPosition(B->BikeTransform(On).TransformPosition(FVector(It->FrontAxleX, 0.0, 0.0)));
		if (Axle.X < 0.0) return ViceRest();
		// The stop's face against the tyre's front at the stop's middle height.
		const double R = It->FrontRadius, Z = FMath::Min(TBLI::StopZ, R);
		const double Front = FMath::Sqrt(FMath::Max(R * R - (R - Z) * (R - Z), 0.0));
		const double Most = DeckLength() * 0.5 - TBLI::StopFace - 4.5;   // the stop at the platform's front edge
		return FMath::Clamp(Axle.X + Front - TBLI::StopFace, 0.0, Most);
	}
	return ViceRest();
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
		if (Sfx) Sfx->PlayAt(TEXT("switch"), GetActorTransform().TransformPosition(PostLocal + FVector(7.0, 0.0, bUp ? 111.0 : 103.0)), -6.0f);
	}
	const double Want = bWantUp ? Top() : LowTop();
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
	// The vice slides to the front tyre of a bike standing on it, then closes on it (and opens, and
	// goes back, when it's wheeled off).
	const int32 On = B ? B->BikeOnLift(LiftId) : INDEX_NONE;
	const bool bBikeOn = On != INDEX_NONE && B->Bike(On).Pusher == 0;
	const double Slide = ViceWant(B);
	if (!FMath::IsNearlyEqual(ViceX, Slide, 0.05) && (JawGap > 0.99 || !bBikeOn))
	{
		const double SlideStep = 60.0 * (double)DeltaSeconds;
		ViceX = Slide > ViceX ? FMath::Min(Slide, ViceX + SlideStep) : FMath::Max(Slide, ViceX - SlideStep);
		if (Vice) Vice->SetRelativeLocation(FVector(ViceX, 0.0, 0.0));
	}
	const double JawWant = (bBikeOn && FMath::IsNearlyEqual(ViceX, Slide, 0.05)) ? 0.0 : 1.0;
	if (!FMath::IsNearlyEqual(JawGap, JawWant))
	{
		const double JawStep = 1.5 * (double)DeltaSeconds;
		JawGap = JawWant > JawGap ? FMath::Min(JawWant, JawGap + JawStep) : FMath::Max(JawWant, JawGap - JawStep);
		PoseJaws();
	}
}
