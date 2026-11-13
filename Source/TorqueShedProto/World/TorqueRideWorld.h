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
struct DiaryRide;
struct RideMesh;
struct Tint;
struct RoamArea;
struct RideGuide;
}
struct FTorqueRideJobs;
struct FTorqueDash;

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
	// The road's surface at a point of the ride's road (Unreal cm; RidePlan::SurfaceAt): Kind 0 tarmac,
	// 1 slick (paint, tar), 2 grit, 3 steel, 4 a gravel road, and how far it stands over the mark a ray hits there (m). False
	// with no ride.
	static bool SurfaceUnder(const FVector& At, int32& Kind, double& Lift);
	// The cattle grids across the ride's roads, in the ride's frame (metres: x east, y up, z south): each
	// its middle, and its way along the road (x, z) with its width across (y). False with no ride.
	static bool CattleGrids(TArray<TPair<FVector, FVector>>& Out);
	// Where to put a bike back after a fall (beenster, build 142: off on a side road, it always went back
	// to the ride's road): the middle of the nearest road of any kind to At (the ride's frame, metres), a
	// little to its left as the UK keeps, facing along it the way nearer Heading (sim: 0 faces -z, + left).
	// Which is 0 for the ride's own road, a side road's number otherwise. False with no ride, or no road
	// within 80 m.
	static bool RoadBack(const FVector& At, double Heading, FVector& OutPos, double& OutHeading, int32& Which);
	// The sat nav at a place (Unreal cm; RidePlan::NavAt) into the dash's nav fields; clears them with
	// no ride. Was: the last call's place along the ride's road (keeps to it where the road passes itself).
	static void NavFor(const FVector& At, FTorqueDash& Dash, double& Was);
	// [ride anywhere] The world loaded round the ride and the way the sat nav is guiding the riders
	// (World/WorldRoam.h), for the maps (the TFT's, the HUD's); null with no ride. Read on the game
	// thread; each is a snapshot to keep, replaced (its Version up) when the area grows or the way
	// re-routes, and OnRoamChanged is broadcast then.
	static std::shared_ptr<const TorqueWorld::RoamArea> RoamNow();
	static std::shared_ptr<const TorqueWorld::RideGuide> GuideNow();
	DECLARE_MULTICAST_DELEGATE(FOnRoamChanged);
	static FOnRoamChanged OnRoamChanged;
	// [OSM stage 1] A test run's limit on the HUD in place of the road's (the "limit:" test verb), for a
	// picture of each: 20..70, -1 the national limit; INDEX_NONE (the default) the road's own.
	static int32 TestLimit;
	// [OSM stage 2] The credits the prop kit's others' models ask for (Data/RideUK/prop_kit.json's
	// "credit" of each kind drawn from a Fab model), for the credits page; empty with none.
	static TArray<FString> PropCredits;
	// [stage 7] The ride diary: a photo (the screen as it is, without the HUD) pinned where the rider
	// is, saved with the ride in Saved/RideDiary. What happened, for a note on the screen.
	static FString TakePhoto();
	// Where the diary keeps its rides and photos.
	static FString DiaryFolder();

private:
	static TWeakObjectPtr<ATorqueRideWorld> Current;
	double Heard[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
	// [stage 7] This ride's diary (World/WorldRideDiary), stepped as the rider goes, saved now and then.
	std::shared_ptr<TorqueWorld::DiaryRide> Diary;
	double SinceDiary = 0.0, SinceDiarySaved = 0.0;
	void SaveDiary();
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
	std::shared_ptr<const TorqueWorld::RoamArea> Roam;
	std::shared_ptr<const TorqueWorld::RideGuide> Guide;
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
	UPROPERTY(Transient) TMap<int64, TObjectPtr<UProceduralMeshComponent>> RoadParts;   // by piece
	// [stage 8] The landmark bridges' hero pieces (RidePlan::HeroBridges), all ride long.
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> HeroParts;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LandMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RoadMaterial;
	// The hand-built ride's scanned stone, for dry stone walls and for stone houses (RideMesh::Stone, Masonry).
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LampMaterial;   // a tunnel lamp's glowing face (M_RideLamp)
	// [stage 7] What lights up after dark (RideMesh::Night: lit windows, lamp heads, cat's eyes; M_RideNight),
	// and the street lamps' real lights on the nearest squares, on only after dark.
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NightMaterial;
	TMap<FIntVector, TArray<TWeakObjectPtr<class UPointLightComponent>>> StreetLights;
	void DropStreetLights(const FIntVector& Key);
	bool bStreetLightsOn = false;
	// A road piece's tunnel lights (by piece, as RoadParts), let go with it.
	TMap<int64, TArray<TWeakObjectPtr<class UPointLightComponent>>> RoadLights;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> StoneMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MasonryMaterial;
	// [stage 5] Rendered and limewashed walls (M_RideRender), and the stone tinted to each part of the
	// country's own (an instance of M_RideStone or M_RideMasonry per tint, made the first time it's needed).
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RenderMaterial;
	// [OSM stage 3] Roofs (M_RideRoof: slate, stone slate, tile, thatch or sheet by the vertex alpha) and brick walls (M_RideBrick).
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RoofMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BrickMaterial;
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
	// [OSM stage 2] The prop kit (the TorqueFoliage actor tagged RideProps: SM_Prop_<kind>_<n>, made by
	// Tools/ue_build_rideuk.py from Data/RideUK/prop_kit.json), which of its models each kind of prop
	// (RidePlan::EProp) uses, and each kind's height in life (cm), turn (degrees) and drop (cm) from the
	// kit file. A kind with none keeps its code-made shape.
	UPROPERTY(Transient) TArray<FTorqueFoliageSpecies> PropKit;
	TArray<TArray<int32>> PropModels;
	TArray<FVector> PropFix;
	FVector PropFrame = FVector(0.0, 1.0, 1.0);
	void FindProps();
	void PlaceProps(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh);
	int32 PropsShown = 0;
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
	// [ride anywhere] Squares to make again (their ground has come since), and where the ground round
	// the rider was last fetched.
	TSet<FIntVector> Stale;
	FVector2D GroundAt = FVector2D(1e12, 1e12);
	bool bFetchingGround = false;
	bool bSaidReady = false;
};
