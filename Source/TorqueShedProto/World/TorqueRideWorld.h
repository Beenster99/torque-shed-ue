// The real world's bare ground and road laid out round the rider (Ride UK stage 2). The game mode
// spawns one in the ride's level (/Game/Maps/RideUK) and hands it the ride the world subsystem
// planned; it then keeps squares of ground and pieces of road in place as the rider goes, made on
// workers from the plan (World/WorldRide.h) and thrown away behind.
//
// The ground is a quadtree of squares in the ride's frame: 4 km squares at a point every 64 m out
// to 10 km, each split into 1 km squares (a point every 8 m) within 1.5 km of the rider, and those
// into 256 m squares (every 4 m) within 400 m. Only the finest have collision, which is all the
// bike's wheels need. A square goes only when those that take its place are there, so the ground
// never has a hole; each hangs a skirt from its edge, so squares of different fineness show no crack.
//
// The road's pieces carry their own colours (tarmac, the verge's gravel, paint) in their vertices.
// So does the ground (what covers it: pasture, moor, crops, woods, rock) and what stands on it: each
// square of the two finer levels brings its dry stone walls, hedges, fences, buildings and stand-in
// trees as a second mesh (RideMesh::Extra), drawn with the road's material and with no collision.
//
// The actor is tagged Land and the road's pieces Road, as the bike's ground trace wants them
// (TorqueBikePawn's GroundTrace): the road is tarmac, the rest grass.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <memory>

#include "TorqueRideWorld.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
namespace TorqueWorld
{
class RidePlan;
struct RideMesh;
}
struct FTorqueRideJobs;

UCLASS()
class ATorqueRideWorld : public AActor
{
	GENERATED_BODY()

public:
	ATorqueRideWorld();
	// The ride to lay out. The ground and road round its start are made before this returns (with
	// their collision), so a bike put down there has something to stand on.
	void Begin(std::shared_ptr<TorqueWorld::RidePlan> InPlan);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	// Where the rider is, in the ride's frame (metres: x east, z south).
	FVector2D RiderAt() const;
	// The squares wanted with the rider at At: (level, column, row).
	void Wanted(const FVector2D& At, TArray<FIntVector>& Out) const;
	void Update(const FVector2D& At, bool bAtOnce);
	void Place(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh, bool bAtOnce);
	UProceduralMeshComponent* MakePart(const TorqueWorld::RideMesh& Mesh, UMaterialInterface* Material, bool bCollide, bool bAtOnce, bool bShadow);
	void UpdateRoad(const FVector2D& At, bool bAtOnce);

	std::shared_ptr<TorqueWorld::RidePlan> Plan;
	std::shared_ptr<FTorqueRideJobs> Jobs;
	// Squares in place (null for one off the world's edge, which has nothing to show).
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Tiles;
	// What stands on a square (walls, buildings, trees), where anything does: it comes and goes with its square.
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> TileExtras;
	TSet<FIntVector> Asked;      // being made on a worker
	TArray<FIntVector> Want;     // as of the last look
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UProceduralMeshComponent>> RoadParts;   // by piece
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LandMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RoadMaterial;
	FVector2D LookedAt = FVector2D(1e12, 1e12);
	double SinceLook = 0.0;
	bool bSaidReady = false;
};
