#include "Shed/TorqueShedKit.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueToolModels.h"
#include "TorqueAudio/TorqueSfx.h"
#include "TorqueAudio/SfxRules.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Light.h"
#include "Engine/RectLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// --- The shed's say (UTorqueBuildSubsystem's kit requests) --------------------------------------------

bool UTorqueBuildSubsystem::IsKitAct(ETorqueShedAct A)
{
	return (uint8)A >= (uint8)ETorqueShedAct::ShedLights && (uint8)A <= (uint8)ETorqueShedAct::ExtinguisherSpray;
}

FString UTorqueBuildSubsystem::CheckKit(int32 Peer, const FTorqueShedRequest& R) const
{
	const bool bHandsFull = HeldBy(Peer) != 0 || Paint.Held.Contains(Peer) || Paint.TapeBy == Peer || PushedBy(Peer) != INDEX_NONE;
	switch (R.Kind)
	{
	case ETorqueShedAct::ShedLights:
	case ETorqueShedAct::Door:
		return FString();
	case ETorqueShedAct::GrabStand:
		if (StandPusher == Peer) return TEXT("You've got hold of the engine stand.");
		if (StandPusher != 0) return TEXT("Someone else is wheeling the engine stand.");
		if (ExtinguisherBy == Peer) return TEXT("Put the fire extinguisher back first (F).");
		if (bHandsFull) return TEXT("Your hands are full: put it down first.");
		return FString();
	case ETorqueShedAct::ReleaseStand:
		if (StandPusher != Peer) return TEXT("You're not wheeling the engine stand.");
		if (!TorqueShedLayout::OnSite(R.Place.GetLocation())) return TEXT("Not out there: keep it in the shed or the yard.");
		return FString();
	case ETorqueShedAct::Extinguisher:
		if (R.Item != 0)
		{
			if (ExtinguisherBy == Peer) return TEXT("You've got the fire extinguisher.");
			if (ExtinguisherBy != 0) return TEXT("Someone else has the fire extinguisher.");
			if (bHandsFull || StandPusher == Peer) return TEXT("Your hands are full: put it down first.");
			return FString();
		}
		return ExtinguisherBy == Peer ? FString() : FString(TEXT("You're not holding the fire extinguisher."));
	case ETorqueShedAct::ExtinguisherSpray:
		return ExtinguisherBy == Peer ? FString() : FString(TEXT("Take the fire extinguisher first: it stands by the end of the workbench."));
	default:
		return TEXT("The shed doesn't know how to do that.");
	}
}

const TCHAR* UTorqueBuildSubsystem::RoomLightsName(int32 Room)
{
	switch (Room)
	{
	case PaintRoom: return TEXT("Paint shop lights");
	case MixingRoom: return TEXT("Mixing bay lights");
	case StorageRoom: return TEXT("Storage room lights");
	default: return TEXT("Shed lights");
	}
}

FString UTorqueBuildSubsystem::ApplyKit(int32 Peer, const FTorqueShedRequest& R)
{
	switch (R.Kind)
	{
	case ETorqueShedAct::ShedLights:
		if (R.Colour >= PaintRoom && R.Colour <= StorageRoom)
		{
			// [kit] Another room's own switch.
			const uint8 Bit = uint8(1 << (R.Colour - 1));
			RoomLightsOff = R.Item != 0 ? uint8(RoomLightsOff & ~Bit) : uint8(RoomLightsOff | Bit);
			return FString::Printf(TEXT("%s %s"), RoomLightsName(R.Colour), R.Item != 0 ? TEXT("on") : TEXT("off"));
		}
		bShedLights = R.Item != 0;
		return bShedLights ? TEXT("Shed lights on") : TEXT("Shed lights off");
	case ETorqueShedAct::Door:
		bDoorUp = R.Item != 0;
		return bDoorUp ? TEXT("Roller door going up") : TEXT("Roller door coming down");
	case ETorqueShedAct::GrabStand:
		StandPusher = Peer;
		return TEXT("Wheeling the engine stand: F lets go.");
	case ETorqueShedAct::ReleaseStand:
	{
		const FVector At = R.Place.GetLocation();
		StandXf = FTransform(FRotator(0.0, R.Place.Rotator().Yaw, 0.0), FVector(At.X, At.Y, 0.0));
		StandPusher = 0;
		return FString();
	}
	case ETorqueShedAct::Extinguisher:
		ExtinguisherBy = R.Item != 0 ? Peer : 0;
		bFoaming = false;
		return R.Item != 0 ? TEXT("Fire extinguisher: hold the work button to spray, F puts it back.") : TEXT("Fire extinguisher back in its place");
	case ETorqueShedAct::ExtinguisherSpray:
		bFoaming = R.Item != 0;
		return FString();
	default:
		return FString();
	}
}

void UTorqueBuildSubsystem::DropKitOf(int32 Peer)
{
	bool bChanged = false;
	if (StandPusher == Peer)
	{
		// Where they were wheeling it when they went.
		if (const APawn* P = TorqueShedKit::PawnOf(this, Peer)) StandXf = TorqueShedKit::StandFor(P);
		StandPusher = 0;
		bChanged = true;
	}
	if (ExtinguisherBy == Peer)
	{
		ExtinguisherBy = 0;   // back in its place
		bFoaming = false;
		bChanged = true;
	}
	if (!bChanged) return;
	OnItems.Broadcast();
	MarkSave();
	OnHostChange.Broadcast();
}

// --- Where things are held -----------------------------------------------------------------------------

namespace TorqueShedKit
{
APawn* PawnOf(const UObject* WorldContext, int32 Peer)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || Peer == 0) return nullptr;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APlayerState* PS = It->GetPlayerState();
		if (PS && PS->GetPlayerId() == Peer) return *It;
	}
	return nullptr;
}

FTransform StandFor(const APawn* Pawn)
{
	if (!Pawn) return UTorqueBuildSubsystem::StandHome();
	// Pushed along in front of you by its grab handle, its front (+y) leading.
	const double Yaw = Pawn->GetBaseAimRotation().Yaw;
	const FVector At = Pawn->GetActorLocation() + FRotator(0.0, Yaw, 0.0).Vector() * 85.0;
	return FTransform(FRotator(0.0, Yaw - 90.0, 0.0), FVector(At.X, At.Y, 0.0));
}

FTransform ExtinguisherFor(const APawn* Pawn, FVector& OutNozzle, FVector& OutAim)
{
	OutNozzle = FVector::ZeroVector;
	OutAim = FVector::ForwardVector;
	if (!Pawn) return FTransform::Identity;
	// Carried by its handle (the Poly Haven model's 60 cm up its body) in front of you and to the
	// right, upright, its hose forward; the nozzle held out where you look.
	const FRotator View = Pawn->GetBaseAimRotation();
	const FRotator Yaw(0.0, View.Yaw, 0.0);
	const FVector Hand = Pawn->GetPawnViewLocation() + Yaw.RotateVector(FVector(40.0, 24.0, -62.0));
	OutAim = View.Vector();
	OutNozzle = Hand + Yaw.RotateVector(FVector(22.0, -10.0, 4.0)) + OutAim * 12.0;
	return FTransform(FRotator(0.0, View.Yaw - 90.0, 0.0), Hand - FVector(0.0, 0.0, 60.0));
}
}

// --- The kit as everyone sees it ----------------------------------------------------------------------

namespace TorqueShedKitImpl
{
const TCHAR* const StandMesh = TEXT("/Game/Shed/Props/engine_stand/SM_engine_stand.SM_engine_stand");
const TCHAR* const ExtinguisherMesh = TEXT("/Game/Shed/Props/dress_korean_fire_extinguisher_01/SM_dress_korean_fire_extinguisher_01.SM_dress_korean_fire_extinguisher_01");
const FName StandProp(TEXT("Prop:engine_stand")), StandBoxes(TEXT("Hidden:engine_stand"));
const FName ExtinguisherProp(TEXT("Prop:dress_korean_fire_extinguisher_01"));
const FName LightTag(TEXT("ShedLight")), BattenTag(TEXT("ShedLightStrip"));
const FVector SwitchAt(285.0, 546.0, 130.0);
constexpr int32 Grooves = 32;               // the curtain's ribs, every 10 cm
constexpr double BlobsPerSecond = 110.0;
constexpr double BlobSpeed = 1050.0;        // cm/s out of the nozzle
constexpr double PatchLife = 45.0;          // seconds a patch of foam lies before it's gone
constexpr int32 MaxPatches = 600;
}

namespace TSKI = TorqueShedKitImpl;

ATorqueShedKit::ATorqueShedKit()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;
}

UStaticMeshComponent* ATorqueShedKit::Piece(UStaticMesh* Mesh, const FLinearColor& Colour, float Metallic, float Roughness)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(GetRootComponent());
	C->RegisterComponent();
	return C;
}

void ATorqueShedKit::FindTagged(UWorld* World, const FName& Tag, TArray<TWeakObjectPtr<AActor>>& Out)
{
	if (!World) return;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->ActorHasTag(Tag)) Out.Add(*It);
}

void ATorqueShedKit::ShowLevel(const TArray<TWeakObjectPtr<AActor>>& Actors, bool bShow, bool bKeepCollision)
{
	for (const TWeakObjectPtr<AActor>& W : Actors)
		if (AActor* A = W.Get())
		{
			if (A->IsHidden() == bShow) A->SetActorHiddenInGame(!bShow);
			if (!bKeepCollision && A->GetActorEnableCollision()) A->SetActorEnableCollision(false);
		}
}

void ATorqueShedKit::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	// The roller door's curtain: galvanised steel, ribbed every 10 cm, a dark rail along its foot.
	Curtain = Piece(CubeMesh, FLinearColor(0.42f, 0.44f, 0.46f), 0.7f, 0.42f);
	if (Curtain)
	{
		Curtain->SetCollisionProfileName(TEXT("BlockAll"));
		Curtain->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // only once it's down far enough (DrawDoor)
	}
	Rail = Piece(CubeMesh, FLinearColor(0.06f, 0.06f, 0.065f), 0.5f, 0.5f);
	for (int32 I = 0; I < TSKI::Grooves; ++I) Grooves.Add(Piece(CubeMesh, FLinearColor(0.24f, 0.25f, 0.26f), 0.7f, 0.5f));
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) DoorOpen = B->DoorUp() ? 1.0 : 0.0;
	DrawDoor();
	FindLights();

	UWorld* World = GetWorld();
	// The engine stand: drawn here once the level's model is found (it's hidden then), so it can roll.
	TArray<TWeakObjectPtr<AActor>> StandModel;
	FindTagged(World, TSKI::StandProp, StandModel);
	UStaticMesh* StandSM = LoadObject<UStaticMesh>(nullptr, TSKI::StandMesh);
	if (StandSM && StandModel.Num() > 0)
	{
		Stand = NewObject<UStaticMeshComponent>(this);
		Stand->SetStaticMesh(StandSM);
		Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stand->SetMobility(EComponentMobility::Movable);
		Stand->SetupAttachment(GetRootComponent());
		Stand->RegisterComponent();
		ShowLevel(StandModel, false, true);
		StandLevelProps = StandModel;
		FindTagged(World, TSKI::StandBoxes, StandLevelProps);   // their colliders go when it moves off
	}
	else UE_LOG(LogTemp, Warning, TEXT("Torque kit: no engine stand model in the level (tag %s): it can't roll"), *TSKI::StandProp.ToString());
	// The fire extinguisher: the level's model in its place, this one in someone's hands.
	FindTagged(World, TSKI::ExtinguisherProp, ExtinguisherLevelProps);
	if (UStaticMesh* ExtSM = LoadObject<UStaticMesh>(nullptr, TSKI::ExtinguisherMesh))
	{
		Extinguisher = NewObject<UStaticMeshComponent>(this);
		Extinguisher->SetStaticMesh(ExtSM);
		Extinguisher->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Extinguisher->SetMobility(EComponentMobility::Movable);
		Extinguisher->SetupAttachment(GetRootComponent());
		Extinguisher->RegisterComponent();
		Extinguisher->SetVisibility(false);
	}
	// The foam: soft white blobs in the air, flattened patches where they land.
	UMaterialInterface* FoamLook = TorqueToolModels::Finish(this, FLinearColor(0.9f, 0.91f, 0.88f), 0.0f, 0.92f);
	for (UInstancedStaticMeshComponent** Slot : {&BlobMesh, &PatchMesh})
	{
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetStaticMesh(SphereMesh);
		I->SetMaterial(0, FoamLook);
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCastShadow(Slot == &PatchMesh);
		I->SetMobility(EComponentMobility::Movable);
		I->SetupAttachment(GetRootComponent());
		I->RegisterComponent();
		*Slot = I;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque kit: %d main lights, %d battens, engine stand %s, extinguisher %s (%d in the level)"), ShedLights.Num(), Battens.Num(),
		Stand ? TEXT("rolls") : TEXT("fixed"), Extinguisher ? TEXT("ready") : TEXT("missing"), ExtinguisherLevelProps.Num());
}

void ATorqueShedKit::EndPlay(const EEndPlayReason::Type Why)
{
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this))
	{
		if (MotorLoop != INDEX_NONE) Sfx->FreeLoop(MotorLoop);
		if (SprayLoop != INDEX_NONE) Sfx->FreeLoop(SprayLoop);
	}
	MotorLoop = SprayLoop = INDEX_NONE;
	Super::EndPlay(Why);
}

void ATorqueShedKit::FindLights()
{
	bFoundLights = true;
	ShedLights.Reset();
	Battens.Reset();
	BattenLit.Reset();
	UWorld* World = GetWorld();
	if (!World) return;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TSKI::LightTag))
		{
			TInlineComponentArray<ULightComponent*> L(*It);
			ShedLights.Append(L);
		}
		else if (It->ActorHasTag(TSKI::BattenTag))
			if (UStaticMeshComponent* C = It->FindComponentByClass<UStaticMeshComponent>())
			{
				Battens.Add(C);
				BattenLit.Add(C->GetMaterial(0));
			}
	}
	if (ShedLights.Num() == 0)
	{
		// A level built before the tags: the rect lights up under the main shed's roof.
		for (TActorIterator<ARectLight> It(World); It; ++It)
		{
			const FVector At = It->GetActorLocation();
			if (FMath::Abs(At.X) < 650.0 && FMath::Abs(At.Y) < 550.0 && At.Z > 380.0)
			{
				TInlineComponentArray<ULightComponent*> L(*It);
				ShedLights.Append(L);
			}
		}
	}
	BattenDark = TorqueToolModels::Finish(this, FLinearColor(0.05f, 0.05f, 0.055f), 0.0f, 0.35f);
	// [kit] The other rooms' lights and glowing tubes, by which room they're in (TorqueSfxRules::RoomAt).
	static const char* const RoomNames[3] = {"paint", "mixing", "storage"};
	auto RoomOf = [](const FVector& P) -> int32 {
		const std::string Room = TorqueSfxRules::RoomAt(P.X, P.Y, P.Z);
		for (int32 I = 0; I < 3; ++I)
			if (Room == RoomNames[I]) return I;
		return INDEX_NONE;
	};
	for (int32 I = 0; I < 3; ++I)
	{
		RoomLights[I].Reset();
		RoomGlow[I].Reset();
		RoomGlowLit[I].Reset();
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		// Only the level's own lights and meshes (not a bike's lamps parked in there).
		if (It->ActorHasTag(TSKI::LightTag) || It->ActorHasTag(TSKI::BattenTag)) continue;
		const bool bLamp = It->IsA<ALight>(), bMesh = It->IsA<AStaticMeshActor>();
		if (!bLamp && !bMesh) continue;
		const int32 Room = RoomOf(It->GetActorLocation());
		if (Room == INDEX_NONE) continue;
		TInlineComponentArray<ULocalLightComponent*> Lamps(*It);
		for (ULocalLightComponent* Lamp : Lamps) RoomLights[Room].Add(Lamp);
		if (!bMesh) continue;
		TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
		for (UStaticMeshComponent* M : Meshes)
		{
			UMaterialInterface* Mat = M ? M->GetMaterial(0) : nullptr;
			const FString MatName = Mat ? Mat->GetName() : FString();
			if (MatName.Contains(TEXT("ShedLed")) || MatName.Contains(TEXT("HexLed")))
			{
				RoomGlow[Room].Add(M);
				RoomGlowLit[Room].Add(Mat);
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Torque kit: room lights: paint %d (%d glowing), mixing %d (%d), storage %d (%d)"), RoomLights[0].Num(), RoomGlow[0].Num(),
		RoomLights[1].Num(), RoomGlow[1].Num(), RoomLights[2].Num(), RoomGlow[2].Num());
	// Each room's switch plate, white on the wall inside its door (its place: TorqueShedLayout's ShedSwitch 2 .. 4).
	if (RoomPlates.Num() == 0)
		for (int32 Index = 2; Index <= 4; ++Index)
			for (const FTorquePlace& P : TorqueShedLayout::Places())
			{
				if (P.Kind != ETorquePlace::ShedSwitch || P.Index != Index) continue;
				// Flat against the wall: thin across x (the walls these are on run along y).
				const bool bFacesEast = P.Box.Min.X >= P.Put.X - 3.0;   // the wall's face at the box's west side
				const double WallX = bFacesEast ? P.Box.Min.X : P.Box.Max.X;
				const double Out = bFacesEast ? 1.0 : -1.0;
				if (UStaticMeshComponent* Plate = Piece(CubeMesh, FLinearColor(0.86f, 0.86f, 0.84f), 0.0f, 0.45f))
				{
					Plate->SetWorldLocation(FVector(WallX + Out * 0.6, P.Put.Y, P.Put.Z));
					Plate->SetWorldScale3D(FVector(1.2, 8.6, 8.6) / 100.0);
					RoomPlates.Add(Plate);
				}
				if (UStaticMeshComponent* Rocker = Piece(CubeMesh, FLinearColor(0.93f, 0.93f, 0.91f), 0.0f, 0.35f))
				{
					Rocker->SetWorldLocation(FVector(WallX + Out * 1.4, P.Put.Y, P.Put.Z));
					Rocker->SetWorldScale3D(FVector(0.8, 2.4, 4.0) / 100.0);
					RoomPlates.Add(Rocker);
				}
			}
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		for (int32 I = 0; I < 3; ++I) ShowRoom(I, B->RoomLightsOn(I + 1));
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) ShowLights(B->ShedLightsOn());
}

void ATorqueShedKit::ShowLights(bool bOn)
{
	bLightsShown = bOn;
	for (ULightComponent* L : ShedLights)
		if (L) L->SetVisibility(bOn);
	for (int32 I = 0; I < Battens.Num(); ++I)
		if (Battens[I]) Battens[I]->SetMaterial(0, bOn ? BattenLit[I] : BattenDark);
}

void ATorqueShedKit::ShowRoom(int32 Room, bool bOn)
{
	if (Room < 0 || Room > 2) return;
	bRoomShown[Room] = bOn;
	for (const TWeakObjectPtr<ULightComponent>& L : RoomLights[Room])
		if (L.IsValid()) L->SetVisibility(bOn);
	for (int32 I = 0; I < RoomGlow[Room].Num(); ++I)
		if (UStaticMeshComponent* Glow = RoomGlow[Room][I].Get()) Glow->SetMaterial(0, bOn ? RoomGlowLit[Room][I].Get() : BattenDark);
}

void ATorqueShedKit::DrawDoor()
{
	namespace K = TorqueShedKit;
	// Down from under the drum's bar: 1 is rolled right up (nothing hangs), 0 is on the floor.
	const double Hang = FMath::Max(K::DoorTop * (1.0 - DoorOpen), 0.0);
	const double Bottom = K::DoorTop - Hang;
	const bool bAny = Hang > 1.0;
	if (Curtain)
	{
		Curtain->SetVisibility(bAny);
		Curtain->SetWorldLocation(FVector(0.0, K::DoorY, Bottom + Hang * 0.5));
		Curtain->SetWorldScale3D(FVector(K::DoorHalf * 2.0, 2.5, FMath::Max(Hang, 1.0)) / 100.0);
		// You can't walk under it once it's below your head.
		Curtain->SetCollisionEnabled(Bottom < 190.0 ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (Rail)
	{
		Rail->SetVisibility(bAny);
		Rail->SetWorldLocation(FVector(0.0, K::DoorY, Bottom + 3.0));
		Rail->SetWorldScale3D(FVector(K::DoorHalf * 2.0, 5.0, 6.0) / 100.0);
	}
	// The ribs ride with the curtain: one every 10 cm up from its foot, only those below the bar.
	for (int32 I = 0; I < Grooves.Num(); ++I)
		if (UStaticMeshComponent* G = Grooves[I])
		{
			const double Z = Bottom + 10.0 + 10.0 * I;
			const bool bShow = bAny && Z < K::DoorTop - 2.0;
			G->SetVisibility(bShow);
			if (!bShow) continue;
			G->SetWorldLocation(FVector(0.0, K::DoorY - 1.6, Z));
			G->SetWorldScale3D(FVector(K::DoorHalf * 2.0, 0.8, 1.4) / 100.0);
		}
}

void ATorqueShedKit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	UTorqueSfx* Sfx = UTorqueSfx::Get(this);

	// The roller door rolls up or down at its motor's pace.
	const double Want = B->DoorUp() ? 1.0 : 0.0;
	const bool bMoving = !FMath::IsNearlyEqual(DoorOpen, Want, 1e-4);
	if (bMoving != bDoorMoving && Sfx)
	{
		if (MotorLoop == INDEX_NONE) MotorLoop = Sfx->MakeLoopAt(FVector(0.0, TorqueShedKit::DoorY - 30.0, TorqueShedKit::DoorTop + 30.0), -6.0f);
		if (bMoving) Sfx->PlayAt(TEXT("light_switch"), TSKI::SwitchAt, -4.0f);
		Sfx->SetLoop(MotorLoop, bMoving ? TEXT("compressor") : TEXT(""));
		Sfx->SetLoopPitch(MotorLoop, 0.55f);
	}
	bDoorMoving = bMoving;
	if (bMoving)
	{
		DoorOpen = FMath::FInterpConstantTo(DoorOpen, Want, DeltaSeconds, 1.0 / TorqueShedKit::DoorSeconds);
		DrawDoor();
	}

	// The main lights.
	if (B->ShedLightsOn() != bLightsShown)
	{
		ShowLights(B->ShedLightsOn());
		if (Sfx) Sfx->PlayAt(TEXT("light_switch"), TSKI::SwitchAt - FVector(0.0, 0.0, 8.0), -4.0f);
	}
	// [kit] The other rooms' lights, each on its own switch.
	for (int32 Room = 0; Room < 3; ++Room)
		if (B->RoomLightsOn(Room + 1) != bRoomShown[Room])
		{
			ShowRoom(Room, B->RoomLightsOn(Room + 1));
			for (const FTorquePlace& P : TorqueShedLayout::Places())
				if (P.Kind == ETorquePlace::ShedSwitch && P.Index == Room + 2 && Sfx) Sfx->PlayAt(TEXT("light_switch"), P.Put, -4.0f);
		}

	// The engine stand, where it stands or in front of whoever's wheeling it; what's on it follows
	// its place (ATorqueShedItems), and you point at it where it is.
	FTransform StandXf = B->StandTransform();
	if (const int32 Pusher = B->StandPushedBy())
		if (const APawn* P = TorqueShedKit::PawnOf(this, Pusher)) StandXf = TorqueShedKit::StandFor(P);
	TorqueShedLayout::MoveStand(StandXf);
	if (Stand)
	{
		Stand->SetWorldTransform(StandXf);
		const FTransform Home = UTorqueBuildSubsystem::StandHome();
		if (!bStandMovedOff && (FVector::Dist(StandXf.GetLocation(), Home.GetLocation()) > 2.0 || !StandXf.GetRotation().Equals(Home.GetRotation(), 0.01)))
		{
			bStandMovedOff = true;
			ShowLevel(StandLevelProps, false, false);   // its old place's colliders aren't in the way any more
		}
	}

	// The fire extinguisher: in its place, or in someone's hands spraying foam where they aim.
	FVector Nozzle = FVector::ZeroVector, Aim = FVector::ForwardVector;
	bool bSpraying = false;
	const int32 Holder = B->ExtinguisherHeldBy();
	const APawn* HolderPawn = Holder != 0 ? TorqueShedKit::PawnOf(this, Holder) : nullptr;
	if (Extinguisher)
	{
		Extinguisher->SetVisibility(HolderPawn != nullptr);
		if (HolderPawn)
		{
			Extinguisher->SetWorldTransform(TorqueShedKit::ExtinguisherFor(HolderPawn, Nozzle, Aim));
			bSpraying = B->ExtinguisherSpraying();
		}
	}
	ShowLevel(ExtinguisherLevelProps, Holder == 0, true);
	if (Sfx && Extinguisher)
	{
		if (SprayLoop == INDEX_NONE) SprayLoop = Sfx->MakeLoop(Extinguisher, FVector(0.0, 20.0, 55.0), -2.0f);
		const FString Now = bSpraying ? TEXT("spray") : TEXT("");
		if (Sfx->LoopSound(SprayLoop) != Now) Sfx->SetLoop(SprayLoop, Now);
	}
	Foam(DeltaSeconds, bSpraying, Nozzle, Aim);
}

void ATorqueShedKit::Foam(float Dt, bool bSpraying, const FVector& Nozzle, const FVector& Aim)
{
	UWorld* World = GetWorld();
	if (!World || !BlobMesh || !PatchMesh) return;
	const bool bWasIdle = Blobs.Num() == 0;
	// New blobs out of the nozzle, in a narrow cone, a little slower and fatter towards its edge.
	if (bSpraying)
	{
		EmitDebt += Dt * TSKI::BlobsPerSecond;
		const FMatrix Basis = FRotationMatrix::MakeFromX(Aim);
		while (EmitDebt >= 1.0)
		{
			EmitDebt -= 1.0;
			const double Spread = FMath::FRandRange(0.0, 0.11);
			const double Round = FMath::FRandRange(0.0, UE_TWO_PI);
			const FVector Dir = (Aim + Basis.GetUnitAxis(EAxis::Y) * FMath::Cos(Round) * Spread + Basis.GetUnitAxis(EAxis::Z) * FMath::Sin(Round) * Spread).GetSafeNormal();
			FBlob Blob;
			Blob.At = Nozzle + Dir * FMath::FRandRange(0.0, 6.0);
			Blob.Velocity = Dir * TSKI::BlobSpeed * FMath::FRandRange(0.8, 1.05) * (1.0 - Spread * 2.0);
			Blob.Life = FMath::FRandRange(1.1, 1.6);
			Blob.Size = FMath::FRandRange(2.5, 4.5);
			Blobs.Add(Blob);
		}
	}
	else EmitDebt = 0.0;
	if (bWasIdle && Blobs.Num() == 0 && Patches.Num() == 0) return;

	// Fly: gravity, air drag, and growing as the foam expands; a blob that hits something leaves a patch.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TorqueFoam), false, this);
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		if (APawn* HolderPawn = TorqueShedKit::PawnOf(this, B->ExtinguisherHeldBy())) Query.AddIgnoredActor(HolderPawn);
	bool bPatchesChanged = false;
	auto Land = [this, &bPatchesChanged](const FVector& At, const FVector& Normal, double Size)
	{
		// Onto a patch already there (it piles up), else a new one, the oldest going if there are many.
		for (FPatch& P : Patches)
			if (P.Age < 6.0 && FVector::DistSquared(P.Xf.GetLocation(), At) < FMath::Square(P.Size * 0.35))
			{
				P.Size = FMath::Min(P.Size + Size * 0.18, 32.0);
				P.Age = FMath::Min(P.Age, 1.0);
				bPatchesChanged = true;
				return;
			}
		if (Patches.Num() >= TSKI::MaxPatches) Patches.RemoveAt(0);
		FPatch P;
		P.Size = Size * 2.2;
		P.Xf = FTransform(FRotationMatrix::MakeFromZ(Normal).Rotator(), At + Normal * 0.5);
		Patches.Add(P);
		bPatchesChanged = true;
	};
	for (int32 I = Blobs.Num() - 1; I >= 0; --I)
	{
		FBlob& Blob = Blobs[I];
		Blob.Age += Dt;
		Blob.Velocity *= FMath::Exp(-1.3 * Dt);
		Blob.Velocity.Z -= 650.0 * Dt;
		const FVector To = Blob.At + Blob.Velocity * Dt;
		Blob.Size = FMath::Min(Blob.Size + 7.0 * Dt, 11.0);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Blob.At, To, ECC_Visibility, Query))
		{
			Land(Hit.ImpactPoint, Hit.ImpactNormal, Blob.Size);
			Blobs.RemoveAtSwap(I);
			continue;
		}
		Blob.At = To;
		if (Blob.At.Z <= 0.5)
		{
			Land(FVector(Blob.At.X, Blob.At.Y, 0.0), FVector::UpVector, Blob.Size);
			Blobs.RemoveAtSwap(I);
			continue;
		}
		if (Blob.Age >= Blob.Life) Blobs.RemoveAtSwap(I);   // gone to mist
	}
	// Patches lie a while, then sink away.
	for (int32 I = Patches.Num() - 1; I >= 0; --I)
	{
		FPatch& P = Patches[I];
		P.Age += Dt;
		if (P.Age > TSKI::PatchLife - 5.0) bPatchesChanged = true;
		if (P.Age > TSKI::PatchLife) Patches.RemoveAt(I);
	}

	TArray<FTransform> Shown;
	Shown.Reserve(Blobs.Num());
	for (const FBlob& Blob : Blobs)
		Shown.Add(FTransform(FRotator::ZeroRotator, Blob.At, FVector(Blob.Size / 100.0)));
	BlobMesh->ClearInstances();
	if (Shown.Num() > 0) BlobMesh->AddInstances(Shown, false);
	if (bPatchesChanged)
	{
		Shown.Reset();
		for (const FPatch& P : Patches)
		{
			const double Fade = FMath::Clamp((TSKI::PatchLife - P.Age) / 5.0, 0.0, 1.0);
			const double S = P.Size * Fade / 100.0;
			Shown.Add(FTransform(P.Xf.GetRotation(), P.Xf.GetLocation(), FVector(S, S, S * 0.3)));
		}
		PatchMesh->ClearInstances();
		if (Shown.Num() > 0) PatchMesh->AddInstances(Shown, false);
	}
}
