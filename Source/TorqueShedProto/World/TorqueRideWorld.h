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
// trees as a second part (RideMesh::Extra) with no collision: hedges, fences, roofs and trees in the
// road's material, and the dry stone walls and stone houses in sections of their own drawn in the
// hand-built ride's scanned stone (M_RideStone, M_RideMasonry, made by Tools/ue_build_rideuk.py).
// The ground is drawn in its scanned grass, moor and rock too (M_RideLandScan) when that was made.
//
// The grass and trees (RideMesh::Plants) are drawn with Buttertubs' own plant and tree models: the
// RideUK level holds two ATorqueFoliage actors that plant nothing (Density 0), tagged RideGrass and
// RideTrees, whose species Tools/ue_build_rideuk.py copied from the Buttertubs level. Without them
// the ground has no grass and the trees stay stand-in shapes.
//
// The actor is tagged Land and the road's pieces Road, as the bike's ground trace wants them
// (TorqueBikePawn's GroundTrace): the road is tarmac, the rest grass.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueFoliage.h"

#include <memory>

#include "TorqueRideWorld.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
namespace TorqueWorld
{
class RidePlan;
struct RideMesh;
struct Tint;
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
	// What the rider hears of the place round them (RidePlan::SoundsAt, each 0..1: the sea, running
	// water, a town, sheep, cattle), from the ride being laid out now; false with no ride.
	static bool PlaceHeard(double& Sea, double& Water, double& Town, double& Sheep, double& Cattle);

private:
	static TWeakObjectPtr<ATorqueRideWorld> Current;
	double Heard[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
	double SinceHeard = 1.0;
	// Where the rider is, in the ride's frame (metres: x east, z south).
	FVector2D RiderAt() const;
	// The squares wanted with the rider at At: (level, column, row).
	void Wanted(const FVector2D& At, TArray<FIntVector>& Out) const;
	void Update(const FVector2D& At, bool bAtOnce);
	void Place(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh, bool bAtOnce);
	UProceduralMeshComponent* MakePart(const TorqueWorld::RideMesh& Mesh, UMaterialInterface* Material, bool bCollide, bool bAtOnce, bool bShadow);
	// One part of several meshes over the same origin, a section each in its own material (null meshes
	// and empty ones are left out); null if all are empty.
	UProceduralMeshComponent* MakePart(const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections, bool bCollide, bool bAtOnce, bool bShadow);
	void UpdateRoad(const FVector2D& At, bool bAtOnce);
	// The level's plant kit (see above): true if it has any grass or trees.
	bool FindKit();
	// A square's grass and trees as instanced meshes, and taking them away again.
	void PlacePlants(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh);
	void DropPlants(const FIntVector& Key);
	// [stage 6] A square's sheep and cattle (RideMesh::Animals), as instances of the kit actor tagged
	// RideAnimals' models (Tools/ue_build_rideuk.py), held with its plants (dropped with them).
	void PlaceAnimals(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh);

	std::shared_ptr<TorqueWorld::RidePlan> Plan;
	std::shared_ptr<FTorqueRideJobs> Jobs;
	// Squares in place (null for one off the world's edge, which has nothing to show).
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Tiles;
	// What stands on a square (walls, buildings, trees), where anything does: it comes and goes with its square.
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> TileExtras;
	// [stage 5] A square's water (RideMesh::Water: the sea, lakes, rivers), in M_RideWater, never collided with.
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> TileWater;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WaterMaterial;
	void DropWater(const FIntVector& Key);
	// [stage 5] The words on a square's signs (RideMesh::Labels), let go with it.
	TMap<FIntVector, TArray<TWeakObjectPtr<class UTextRenderComponent>>> TileWords;
	void DropWords(const FIntVector& Key);
	TSet<FIntVector> Asked;      // being made on a worker
	TArray<FIntVector> Want;     // as of the last look
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UProceduralMeshComponent>> RoadParts;   // by piece
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LandMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RoadMaterial;
	// The hand-built ride's scanned stone, for dry stone walls and for stone houses (RideMesh::Stone, Masonry).
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LampMaterial;   // a tunnel lamp's glowing face (M_RideLamp)
	// A road piece's tunnel lights (by piece, as RoadParts), let go with it.
	TMap<int32, TArray<TWeakObjectPtr<class UPointLightComponent>>> RoadLights;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> StoneMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MasonryMaterial;
	// [stage 5] Rendered and limewashed walls (M_RideRender), and the stone tinted to each part of the
	// country's own (an instance of M_RideStone or M_RideMasonry per tint, made the first time it's needed).
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RenderMaterial;
	UPROPERTY(Transient) TMap<uint32, TObjectPtr<UMaterialInterface>> Tinted;
	UMaterialInterface* TintedOf(UMaterialInterface* Base, uint32 Which, const TorqueWorld::Tint& By);
	// The kit: the grass's species and the trees', and which of them each of the plan's kinds uses
	// (RidePlan::PlantName: for a tree kind, its near models and its far ones).
	UPROPERTY(Transient) TArray<FTorqueFoliageSpecies> Kit;
	TArray<TArray<int32>> KindNear, KindFar;
	// [stage 6] A few birds overhead near the rider: buzzards circling, red kites where kites live,
	// gulls by the sea. Each wheels round a point of its own until the rider is far past it, then
	// another is found ahead.
	struct FRideBird
	{
		TWeakObjectPtr<UProceduralMeshComponent> Part;
		FVector Centre = FVector::ZeroVector;   // the ride's frame: x east, y up, z south (m)
		double Radius = 60.0, Speed = 9.0, Angle = 0.0, Bank = 0.4, Drift = 0.0;
		int32 Kind = 0;
	};
	TArray<FRideBird> Birds;
	FVector2D BirdsLookedAt = FVector2D(1e12, 1e12);
	FVector2D BirdsGoing = FVector2D(1.0, 0.0);   // (the way the rider is going, for where to send a bird)
	void TickBirds(float DeltaSeconds);
	void SendBird(FRideBird& Bird, const FVector2D& At, uint32 Salt);
	UPROPERTY(Transient) TArray<FTorqueFoliageSpecies> AnimalKit;
	TArray<TArray<int32>> AnimalModels;   // by RidePlan::EAnimal: indices into AnimalKit
	FVector AnimalFrame = FVector(0.0, 1.0, 1.0);
	int32 AnimalsShown = 0;
	FVector GrassFrame = FVector(0.0, 1.0, 1.0), TreeFrame = FVector(0.0, 1.0, 1.0);   // yaw, mirror, scale
	int32 KitTrees = 0;   // (where in Kit the trees start)
	float CullScale = 1.0f;
	// Each square's instanced plants (the components are the actor's own, so this needn't hold them).
	TMap<FIntVector, TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>> TilePlants;
	int32 PlantsShown = 0;
	FVector2D LookedAt = FVector2D(1e12, 1e12);
	double SinceLook = 0.0;
	bool bSaidReady = false;
};
