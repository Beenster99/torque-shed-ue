#include "Shed/TorqueDynoRoom.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueToolModels.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueDynoImpl
{
// dyno_room.gd, in the shed's cm: the rig's middle, the drum (radius 32 cm, its axle 71 cm behind
// the middle, under the rear tyre), the fan 1.95 m in front, the room's ceiling.
const FVector Rig(0.0, -940.0, 0.0);
constexpr double DrumRadius = 32.0;
constexpr double DrumBehind = 71.0;
constexpr double FanAhead = 195.0;
constexpr double Ceiling = 340.0;
// The Speed Twin's gearing, to turn the engine's rpm into the rear wheel's (BikeSpec).
constexpr double Primary = 1.9, FinalDrive = 41.0 / 18.0, RearRadius = 31.2;
const double Gears[] = {2.6, 1.95, 1.6, 1.36, 1.19, 1.06};

// Torque Shed's rig space (x right, y up, z back) to the shed's: x, then y along z, z up.
FVector RigPoint(double X, double Y, double Z) { return Rig + FVector(X * 100.0, Z * 100.0, Y * 100.0); }

// The bike on the rig's latest run, if it's playing now: how far in it is.
const FTorqueDynoRun* Playing(const UTorqueBuildSubsystem* B, float& OutT)
{
	if (!B) return nullptr;
	const int32 Bay = B->DynoBay();
	if (Bay == INDEX_NONE || !B->HasDynoRun(Bay)) return nullptr;
	const FTorqueDynoRun& Run = B->DynoRunOf(Bay);
	OutT = (float)(B->SharedNow() - Run.Start);
	if (OutT < 0.0f || OutT > TorqueShedPower::RunTime(Run)) return nullptr;
	return &Run;
}
}

namespace TDRI = TorqueDynoImpl;

ATorqueDynoRoom::ATorqueDynoRoom()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
}

UStaticMeshComponent* ATorqueDynoRoom::Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm,
	const FRotator& Rot, const FLinearColor& Colour, float Metallic, float Roughness, float Emissive)
{
	if (!Mesh || !Parent) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness, Emissive));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Parent);
	C->SetRelativeLocationAndRotation(At, Rot);
	C->SetRelativeScale3D(SizeCm / 100.0);   // the basic shapes are a metre across
	C->RegisterComponent();
	return C;
}

void ATorqueDynoRoom::BeginPlay()
{
	Super::BeginPlay();
	USceneComponent* Root = GetRootComponent();
	const FLinearColor DrumSteel(0.55f, 0.56f, 0.58f), Dark(0.05f, 0.05f, 0.06f), Steel(0.4f, 0.42f, 0.45f);
	// The drum (_build_rig): its top just under the plate, across the rig, knurled bars to see it turn.
	Drum = NewObject<USceneComponent>(this);
	Drum->SetupAttachment(Root);
	Drum->SetRelativeLocation(TDRI::Rig + FVector(0.0, TDRI::DrumBehind, -TDRI::DrumRadius + 2.5));
	Drum->RegisterComponent();
	Piece(Drum, CylinderMesh, FVector::ZeroVector, FVector(TDRI::DrumRadius * 2.0, TDRI::DrumRadius * 2.0, 54.0), FRotator(90.0, 0.0, 0.0), DrumSteel, 0.9f, 0.3f);
	for (int32 I = 0; I < 12; ++I)
	{
		const double A = UE_TWO_PI * I / 12.0;
		Piece(Drum, CubeMesh, FVector(0.0, FMath::Cos(A), FMath::Sin(A)) * TDRI::DrumRadius, FVector(55.0, 3.0, 2.0),
			FRotator(0.0, 0.0, FMath::RadiansToDegrees(A)), Dark, 0.4f, 0.6f);
	}
	// The fan's blades (_build_fan), turning about its axle towards the bike.
	Fan = NewObject<USceneComponent>(this);
	Fan->SetupAttachment(Root);
	Fan->SetRelativeLocation(TDRI::Rig + FVector(0.0, -TDRI::FanAhead + 12.0, 95.0));
	Fan->RegisterComponent();
	for (int32 I = 0; I < 5; ++I)
	{
		const double A = 360.0 * I / 5.0;
		const FRotator Spoke(0.0, 0.0, A);
		Piece(Fan, CubeMesh, Spoke.RotateVector(FVector(0.0, 0.0, 20.0)), FVector(1.5, 12.0, 40.0), FRotator(0.0, 17.0, 0.0) + Spoke, Steel, 0.85f, 0.35f);
	}
	Piece(Fan, CylinderMesh, FVector::ZeroVector, FVector(14.0, 14.0, 8.0), FRotator(0.0, 0.0, 90.0), Steel, 0.85f, 0.35f);
	// The straps (_strap): from each post's ratchet to where it hooks on the bike, drawn once done up.
	static const FVector2D Posts[4] = {FVector2D(-0.95, -0.6), FVector2D(0.95, -0.6), FVector2D(-0.95, 0.8), FVector2D(0.95, 0.8)};
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector2D P = Posts[I];
		const FVector From = TDRI::RigPoint(P.X, 0.46, P.Y);
		const FVector To = TDRI::RigPoint(FMath::Sign(P.X) * 0.14, P.Y < 0.0 ? 0.78 : 0.62, P.Y < 0.0 ? -0.5 : 0.55);
		const FVector Mid = (From + To) * 0.5;
		UStaticMeshComponent* S = Piece(Root, CubeMesh, Mid, FVector(4.0, 0.6, FVector::Dist(From, To)),
			FRotationMatrix::MakeFromZ(To - From).Rotator(), FLinearColor(0.9f, 0.45f, 0.05f), 0.0f, 0.7f);
		if (S) S->SetVisibility(false);
		Straps.Add(S);
	}
	// The lights (_build_lights): two strips down the room either side, and a light over the rig.
	for (int32 I = 0; I < 3; ++I)
	{
		const FVector At = I < 2 ? TDRI::Rig + FVector(I == 0 ? -180.0 : 180.0, 0.0, TDRI::Ceiling - 6.0) : TDRI::Rig + FVector(0.0, 20.0, TDRI::Ceiling - 4.0);
		if (I < 2) Strips.Add(Piece(Root, CubeMesh, At, FVector(20.0, 160.0, 5.0), FRotator::ZeroRotator, FLinearColor(1.0f, 0.95f, 0.85f), 0.0f, 0.5f, 6.0f));
		URectLightComponent* L = NewObject<URectLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetRelativeLocationAndRotation(At - FVector(0.0, 0.0, 4.0), FRotator(-90.0, 0.0, 0.0));
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Lumens);
		L->SetIntensity(I < 2 ? 1800.0f : 2400.0f);
		L->SetSourceWidth(I < 2 ? 20.0f : 60.0f);
		L->SetSourceHeight(I < 2 ? 160.0f : 60.0f);
		L->SetAttenuationRadius(900.0f);
		L->SetBarnDoorAngle(70.0f);
		L->SetUseTemperature(true);
		L->SetTemperature(4800.0f);
		L->RegisterComponent();
		Lights.Add(L);
	}
	// The rockers on the switches by the door (_build_switches): the lights white, the fan red.
	// Proud of the switch plates' faces (y -586), so a tipped rocker shows both ends.
	LightsRocker = Piece(Root, CubeMesh, FVector(-135.0, -586.6, 135.0), FVector(5.0, 1.5, 8.0), FRotator::ZeroRotator, FLinearColor(0.92f, 0.92f, 0.9f), 0.0f, 0.5f);
	FanRocker = Piece(Root, CubeMesh, FVector(-159.0, -586.6, 135.0), FVector(5.0, 1.5, 8.0), FRotator::ZeroRotator, FLinearColor(0.7f, 0.08f, 0.06f), 0.0f, 0.5f);
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: dyno room kit: drum, fan, %d straps, %d lights"), Straps.Num(), Lights.Num());
	}
}

bool ATorqueDynoRoom::Live(const UObject* WorldContext, float& OutRpm, int32& OutGear, float& OutProgress)
{
	float T = 0.0f;
	const FTorqueDynoRun* Run = TDRI::Playing(UTorqueBuildSubsystem::Of(WorldContext), T);
	if (!Run) return false;
	const TorqueShedPower::FSweep S = TorqueShedPower::SweepState(*Run, T);
	OutRpm = S.Rpm;
	OutGear = S.Gear;
	OutProgress = S.Progress;
	return true;
}

void ATorqueDynoRoom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	const FTorqueDynoRoom& Kit = B->DynoRoom();
	const bool bOnRig = B->DynoBay() != INDEX_NONE;
	for (int32 I = 0; I < Straps.Num(); ++I)
		if (Straps[I]) Straps[I]->SetVisibility(bOnRig && Kit.Straps[I]);
	// The drum turns with the tyre on it while a run plays.
	float Rpm = 0.0f, Progress = 0.0f;
	int32 Gear = 0;
	double WheelOmega = 0.0;
	if (Live(this, Rpm, Gear, Progress) && Gear > 0)
		WheelOmega = Rpm * UE_TWO_PI / 60.0 / (TDRI::Primary * TDRI::FinalDrive * TDRI::Gears[FMath::Clamp(Gear, 1, 6) - 1]);
	DrumAngle = FMath::Fmod(DrumAngle + WheelOmega * TDRI::RearRadius / TDRI::DrumRadius * DeltaSeconds, UE_TWO_PI);
	if (Drum) Drum->SetRelativeRotation(FRotator(0.0, 0.0, FMath::RadiansToDegrees(DrumAngle)));
	// The fan blows while it's switched on, harder as a run builds.
	const float Push = FMath::Clamp((float)(FMath::Abs(WheelOmega) * TDRI::RearRadius / 100.0 / 45.0), 0.0f, 1.0f);
	FanSpeed = FMath::FInterpConstantTo(FanSpeed, Kit.bFan ? 25.0f : 0.0f, DeltaSeconds, Kit.bFan ? 12.0f : 5.0f);
	FanAngle = FMath::Fmod(FanAngle + FanSpeed * (1.0 + 0.5 * Push) * DeltaSeconds, UE_TWO_PI);
	if (Fan) Fan->SetRelativeRotation(FRotator(0.0, 0.0, FMath::RadiansToDegrees(FanAngle)));
	// A rocker tips about the wall's horizontal axis (x): top in or bottom in. Pitch (about y, the
	// wall's normal) turned it in the wall's own plane, which looked crooked rather than switched.
	if (FanRocker) FanRocker->SetRelativeRotation(FRotator(0.0, 0.0, Kit.bFan ? 12.0 : -12.0));
	if (LightsRocker) LightsRocker->SetRelativeRotation(FRotator(0.0, 0.0, Kit.bLights ? 12.0 : -12.0));
	if (Kit.bLights != bLightsShown)
	{
		bLightsShown = Kit.bLights;
		for (URectLightComponent* L : Lights)
			if (L) L->SetVisibility(bLightsShown);
		for (UStaticMeshComponent* S : Strips)
			if (S) S->SetVisibility(bLightsShown);
	}
}
