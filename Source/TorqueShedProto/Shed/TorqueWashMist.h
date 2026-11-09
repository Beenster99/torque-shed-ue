// The wash bay filling up with mist while anyone's pressure washing in it (Torque Shed's
// wash_mist.gd, with Unreal's volumetrics): a soft haze builds up slowly the longer the jet's going
// (whoever's holding it: every machine sees every walker's jet, so the whole crew sees the same),
// then thins away once the washing stops. A local fog volume over the bay as thick as the haze, drawn
// into the volumetric fog (switched on while there's mist and you're near enough to see it, if the
// Settings' volumetric fog is on) so the bay's strip light and the daylight through its open front
// cut shafts through it; and big faint puffs of spray drifting about the bay over it.
//
// One per machine, not replicated: ATorqueWashMist::Ensure makes it the first time a walker's jet goes.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueWashMist.generated.h"

class UInstancedStaticMeshComponent;
class ULocalFogVolumeComponent;
class UExponentialHeightFogComponent;
class UStaticMesh;

UCLASS(NotPlaceable)
class ATorqueWashMist : public AActor
{
	GENERATED_BODY()

public:
	ATorqueWashMist();
	static ATorqueWashMist* Ensure(UWorld* World, UStaticMesh* Plane);
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	// How thick the haze is, 0..1.
	float Level() const { return (float)Haze; }

	UPROPERTY() UStaticMesh* Plane = nullptr;

private:
	UPROPERTY(Transient) ULocalFogVolumeComponent* Fog = nullptr;
	UPROPERTY(Transient) UInstancedStaticMeshComponent* Puffs = nullptr;
	TWeakObjectPtr<UExponentialHeightFogComponent> HeightFog;
	bool bTurnedOnVolumetric = false;
	float WasFogDistance = 6000.0f;   // the height fog's volumetric distance before the mist shortened it
	double Haze = 0.0, Clock = 0.0;
	double FogUnit = 0.0;       // the fog volume's radius at scale 1 (cm)
	struct FPuff
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 8.0f, Size = 100.0f, Seed = 0.0f;
	};
	TArray<FPuff> Live;
	double Debt = 0.0;
	void Make();
	bool AnyoneWashing() const;
	void UpdateVolumetric(bool bWant);
	void DrawPuffs(float Dt);
};
