// The shed's own kit that does things (beenster, build 101): the switch boxes right of the roller
// door (Torque Shed's workshop.gd door box and light switch) working the main lights and the roller
// door, the engine stand rolling on its castors with the engine on it, and the fire extinguisher
// that can be taken off its place and sprayed, its foam seen by everyone.
//
// The state is the shed's (UTorqueBuildSubsystem: ShedLightsOn, DoorUp, StandTransform /
// StandPushedBy, ExtinguisherHeldBy / ExtinguisherSpraying; the ShedLights, Door, GrabStand,
// ReleaseStand, Extinguisher and ExtinguisherSpray requests, checked and applied in
// TorqueShedKit.cpp), so the host decides and every player's game draws the same: this actor reads
// it each frame. The level's own pieces it works are found by tag (Tools/ue_build_shed.py lights:
// "ShedLight" on the main strip lights, "ShedLightStrip" on their glowing battens;
// Tools/ue_import_props.py dress: "Prop:<id>" on a placed model, "Hidden:<id>" on the grey boxes
// it stands in for).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedKit.generated.h"

class APawn;
class UInstancedStaticMeshComponent;
class ULightComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

namespace TorqueShedKit
{
// The roller door's opening (ue_build_shed.py DOOR_HALF, DOOR_TOP, the front wall at y 550): the
// curtain hangs just inside the wall, under the drum's bar.
constexpr double DoorHalf = 229.0;
constexpr double DoorTop = 318.0;
constexpr double DoorY = 544.0;
constexpr double DoorSeconds = 9.0;   // all the way up or down
// Where the engine stand goes while Pawn wheels it: in front of them, its front (+y) leading.
FTransform StandFor(const APawn* Pawn);
// Where the fire extinguisher is while Pawn holds it (its bottom centre, its hose (+y) forward),
// and its nozzle: where the foam comes out and which way.
FTransform ExtinguisherFor(const APawn* Pawn, FVector& OutNozzle, FVector& OutAim);
// The pawn of the player with this peer id, or null.
APawn* PawnOf(const UObject* WorldContext, int32 Peer);
}

UCLASS()
class ATorqueShedKit : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedKit();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* SphereMesh = nullptr;

	// The roller door: its curtain, the grooves across it, its bottom rail; how far up it is (1 up).
	UPROPERTY(Transient) UStaticMeshComponent* Curtain = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Rail = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Grooves;
	double DoorOpen = 1.0;
	bool bDoorMoving = false;
	int32 MotorLoop = INDEX_NONE;
	void DrawDoor();

	// The main lights: each strip light, and each batten's mesh with its lit and its dark look.
	UPROPERTY(Transient) TArray<ULightComponent*> ShedLights;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Battens;
	UPROPERTY(Transient) TArray<UMaterialInterface*> BattenLit;
	UPROPERTY(Transient) UMaterialInterface* BattenDark = nullptr;
	bool bLightsShown = true;
	bool bFoundLights = false;
	void FindLights();
	void ShowLights(bool bOn);
	// [kit] The other rooms' lights (paint shop, mixing bay, storage room; beenster, build 141), found
	// by where they are: each room's lights, its glowing tubes and tiles with their lit look, and the
	// switch plate drawn inside its door.
	// (Not UPROPERTYs: the level's actors own the lights and meshes, their meshes the lit looks.)
	TArray<TWeakObjectPtr<ULightComponent>> RoomLights[3];
	TArray<TWeakObjectPtr<UStaticMeshComponent>> RoomGlow[3];
	TArray<TWeakObjectPtr<UMaterialInterface>> RoomGlowLit[3];
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> RoomPlates;
	bool bRoomShown[3] = {true, true, true};
	void ShowRoom(int32 Room, bool bOn);

	// The engine stand: drawn here (the level's model hidden), where the shed says it is or rolling
	// along in front of whoever's wheeling it.
	UPROPERTY(Transient) UStaticMeshComponent* Stand = nullptr;
	TArray<TWeakObjectPtr<AActor>> StandLevelProps;   // the level's model and its grey boxes
	bool bStandMovedOff = false;

	// The fire extinguisher: the level's model on its place, this one in someone's hands.
	UPROPERTY(Transient) UStaticMeshComponent* Extinguisher = nullptr;
	TArray<TWeakObjectPtr<AActor>> ExtinguisherLevelProps;
	int32 SprayLoop = INDEX_NONE;

	// The foam: blobs in flight and the patches they leave where they land, drawn as instances.
	struct FBlob
	{
		FVector At, Velocity;
		double Age = 0.0, Life = 1.0, Size = 4.0;
	};
	struct FPatch
	{
		FTransform Xf;
		double Age = 0.0, Size = 10.0;
	};
	TArray<FBlob> Blobs;
	TArray<FPatch> Patches;
	double EmitDebt = 0.0;
	UPROPERTY(Transient) UInstancedStaticMeshComponent* BlobMesh = nullptr;
	UPROPERTY(Transient) UInstancedStaticMeshComponent* PatchMesh = nullptr;
	void Foam(float Dt, bool bSpraying, const FVector& Nozzle, const FVector& Aim);

	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FLinearColor& Colour, float Metallic, float Roughness);
	static void FindTagged(UWorld* World, const FName& Tag, TArray<TWeakObjectPtr<AActor>>& Out);
	static void ShowLevel(const TArray<TWeakObjectPtr<AActor>>& Actors, bool bShow, bool bKeepCollision);
};
