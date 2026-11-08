// The paint mixing bay's moving kit, as Torque Shed's PaintBayRoom draws it (paint_bay_room.gd):
// the tint bottles on the rack (the one being poured tipped over the tin on the scale), every tin
// (on the scale, the tin shelf, in the shaker shaking, or in a player's hands) with its paint showing
// at the top, the scale's reading, the recipe cards on the swatch wall and the test card on the
// spray-out stand. The room itself (bench, rack shelves, tin shelf, swatch board, stand, daylight
// booth and shaker) is the level's (Tools/ue_build_shed.py paint_bay).
//
// It follows the shed's state (UTorqueBuildSubsystem's paint bay), so every player sees the same.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorquePaintBayRoom.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class ATorquePaintBayRoom : public AActor
{
	GENERATED_BODY()

public:
	ATorquePaintBayRoom();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Bottles;     // one per tint, on the rack
	UPROPERTY(Transient) USceneComponent* PourBottle = nullptr;      // the one over the tin
	UPROPERTY(Transient) UStaticMeshComponent* PourBody = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* PourStream = nullptr;
	UPROPERTY(Transient) TArray<USceneComponent*> TinRoots;          // a pool of tins
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> TinPaint;     // ... the paint in each
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Swatches;
	UPROPERTY(Transient) UStaticMeshComponent* TestCard = nullptr;
	UPROPERTY(Transient) UTextRenderComponent* ScaleText = nullptr;
	FString Shown;            // what's drawn now (redrawn when the bay changes)
	TArray<FString> TinSigs;  // the paint each pooled tin shows
	double ShakeClock = 0.0;
	UStaticMeshComponent* Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm,
		const FRotator& Rot, const FLinearColor& Colour, float Metallic, float Roughness);
	static void Tint(UStaticMeshComponent* C, const FLinearColor& Colour, float Metallic, float Roughness);
	FVector HeldAt(int32 Peer, FRotator& OutRot) const;
};
