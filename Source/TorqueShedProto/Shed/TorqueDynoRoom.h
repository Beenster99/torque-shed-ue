// The dyno room's moving kit, as Torque Shed's DynoRoom draws it (dyno_room.gd): the drum under
// the rear tyre turning with a run, the cooling fan's blades, the ratchet straps from each post to
// the bike once they're done up, the room's lights and the rockers on their switches. The room
// itself (the rig's plate, posts, clamp, fan housing, desk and screen frame) is the level's
// (Tools/ue_build_shed.py dyno_room); the run's graph is ATorqueShedScreens' dyno page.
//
// Everything here follows the shed state (UTorqueBuildSubsystem's dyno room and each bike's runs),
// so every player sees the same: a run plays back from the time it started.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueDynoRoom.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class URectLightComponent;

UCLASS()
class ATorqueDynoRoom : public AActor
{
	GENERATED_BODY()

public:
	ATorqueDynoRoom();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// What the engine on the rig is doing now (for the screen and the HUD): false when nothing runs.
	static bool Live(const UObject* WorldContext, float& OutRpm, int32& OutGear, float& OutProgress);

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) USceneComponent* Drum = nullptr;
	UPROPERTY(Transient) USceneComponent* Fan = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Straps;
	UPROPERTY(Transient) TArray<URectLightComponent*> Lights;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Strips;
	UPROPERTY(Transient) UStaticMeshComponent* LightsRocker = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* FanRocker = nullptr;
	double DrumAngle = 0.0, FanAngle = 0.0;
	float FanSpeed = 0.0f;
	bool bLightsShown = true;
	UStaticMeshComponent* Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm,
		const FRotator& Rot, const FLinearColor& Colour, float Metallic, float Roughness, float Emissive = 0.0f);
};
