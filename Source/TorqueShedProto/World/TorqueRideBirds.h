// [birds] Ride UK's wild birds drawn (beenster Nov 26: "more birds flying ... flying out of bushes or
// trees as we approach ... occasional birds of prey perched on tree tops").
//
// World/WorldRideBirds works out where every bird is (plain C++, SimTest/bird_test); this draws them with
// Tools/make_ride_birds.py's models (the RideBirds kit actor Tools/ue_build_rideuk.py bird_kit makes: a
// species SM_Bird_<species>_<pose> per model, hand-made LOD1 and LOD2), one instanced component per model,
// made the first time it is wanted. Each frame every bird's instance is put where it is in the pose its
// wings are in; a model nobody is in is hidden. About a hundred instances at most: well under a tenth of a
// millisecond.
//
// The perches are the finest squares' hedges, bushes and trees (RideMesh::Plants), handed over as the
// squares are placed and let go with them. Co-op: every machine runs its own birds off the shared server
// clock and every rider's bike, so everyone sees the same ones (World/WorldRideBirds.h).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "World/WorldRideBirds.h"
#include "TorqueRideBirds.generated.h"

class AActor;
class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
namespace TorqueWorld { struct RideMesh; class RidePlan; }

UCLASS()
class UTorqueRideBirds : public UObject
{
	GENERATED_BODY()
public:
	// Once, as the ride starts: the kit's models. False (and the old code-made birds stay) without the kit.
	bool Start(AActor* InOwner);
	bool HasKit() const { return bKit; }
	// A square of the ground placed (its plants are perches when it is one of the finest) or let go.
	void AddPerches(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh);
	void DropPerches(const FIntVector& Key);
	// Every frame from the ride world's Tick, with what is heard round the rider (RidePlan::SoundsAt) and its region's name.
	void Tick(const TorqueWorld::RidePlan& Plan, const double Heard[6], const FString& Region);
	void Stop();

private:
	TWeakObjectPtr<AActor> Owner;
	bool bKit = false;
	TorqueWorld::RideBirds Sim;
	// (by species and pose) the model, and its instances
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> Models;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
	TArray<int32> Shown;   // instances in use in each part last frame
	FVector Frame = FVector(0.0, 1.0, 1.0);   // the kit's importer frame: yaw, mirror, scale
	double LastSaid = 0.0;
	static int32 Slot(int32 Species, int32 Pose) { return Species * int32(TorqueWorld::BirdPose::Count) + Pose; }
	UInstancedStaticMeshComponent* PartFor(int32 Index);
};
