// A bike lift (Torque Shed's workshop_upgrades.gd _build_lift): a pit frame flush in the floor, a
// chequer plate platform that goes up and down carrying the bike on its paddock stand (and anyone
// standing on it), crossed scissor arms under it each side, and a post beside it with up and down
// buttons. F on the post raises or lowers it (a Lift request: TorqueShedLayout place "lift_N").
// The wash bay's (bWash) is a narrow grated runway instead, so the jet gets through to underneath.
//
// Whether it's up is the shed's (UTorqueBuildSubsystem::LiftUp); every player's game runs its own
// lifts towards that, so they go up and down smoothly for everyone. Bikes standing on one ride it
// (ATorqueShedBike asks HeightAt).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueBikeLift.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class ATorqueBikeLift : public AActor
{
	GENERATED_BODY()

public:
	ATorqueBikeLift();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float DeltaSeconds) override;

	// A lift at Xf (x along the bike, nose first; its origin on the floor under the bike's middle),
	// raised and lowered as lift LiftId (a bay, or UTorqueBuildSubsystem::WashLift).
	static ATorqueBikeLift* Spawn(UWorld* World, int32 InLiftId, const FTransform& Xf, bool bInWash);
	// Every lift the shed has (the bays' and the wash bay's), where TorqueShedLayout puts them, that
	// isn't in this world yet.
	static void SpawnMissing(UWorld* World);
	// How far up the lift a thing at At is standing on is right now: 0 off a lift, or with it down
	// (workshop_upgrades.gd lift_height_at: within half a metre of a lift's middle).
	static double HeightAt(const UObject* WorldContext, const FVector& At);
	static ATorqueBikeLift* Find(const UObject* WorldContext, int32 InLiftId);

	UPROPERTY(EditAnywhere, Category = "Lift") int32 LiftId = 0;
	UPROPERTY(EditAnywhere, Category = "Lift") bool bWash = false;
	// Where the control post stands, in the lift's frame (TorqueShedLayout::LiftPostLocal).
	UPROPERTY(EditAnywhere, Category = "Lift") FVector PostLocal = FVector(-160.0, -125.0, 0.0);

	double Height() const { return H; }
	double Top() const { return bWash ? 125.0 : 90.0; }   // Wash.LIFT_TOP, GarageUpgrades.LIFT_TOP
	static constexpr double Speed = 30.0;                 // cm a second (LIFT_SPEED)
	static constexpr double ArmLength = 200.0;            // the scissor arms (LIFT_ARM)

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY(Transient) USceneComponent* Deck = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Arms;
	TArray<double> ArmLean;
	double H = 0.0;
	bool bSeen = false;     // the shed's word on it has been read once (no click for the start)
	bool bWantUp = false;
	bool bMoving = false;
	int32 Motor = INDEX_NONE;

	UStaticMeshComponent* Piece(USceneComponent* Parent, const FVector& Where, const FVector& SizeCm, const FLinearColor& Colour,
		float Metallic, float Roughness, bool bSolid = false, float Emissive = 0.0f);
	void Label(const FString& Text, const FVector& Where, float Size);
	void Pose();
};
