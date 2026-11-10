// [fluids] The petrol pump on the shed's front wall outside (Torque Shed's shed_pump.gd and
// shed_pump_view.gd): the red cabinet (Data/Props petrol_pump), its display showing the litres of
// the sale going on, a ding when the tank's full, and the nozzle on its hose, drawn here: hung in its holster, in the hand of
// whoever's lifted it, or in a bike's filler while it's pumping. The state is the shed's
// (UTorqueBuildSubsystem PumpHeldBy / PumpSale, the PumpNozzle and PumpFill requests), so every
// player's game draws the same.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedPump.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class ATorqueShedPump : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedPump();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Puts one in the world if there isn't one.
	static void SpawnMissing(UWorld* World);

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Cabinet = nullptr;
	UPROPERTY(Transient) USceneComponent* Nozzle = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Hose;
	UPROPERTY(Transient) UTextRenderComponent* Litres = nullptr;
	UPROPERTY(Transient) UTextRenderComponent* Grade = nullptr;

	UStaticMeshComponent* Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FLinearColor& Colour, float Metallic, float Roughness);
	void Bar(UStaticMeshComponent* C, const FVector& From, const FVector& To, double Radius);

	float LastSale = 0.0f;
	double Pumping = 0.0;        // seconds since the sale last went up
	FTransform NozzleAt;         // eased towards where it should be
	bool bPlaced = false;
	bool bDinged = false;        // the full-tank ding's gone for this fill
};
