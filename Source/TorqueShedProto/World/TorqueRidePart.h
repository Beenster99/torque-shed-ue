// [perf, build 148] A piece of the ride's world (a square of ground, what stands on it, its water, a
// piece of road, a bird) as ATorqueRideWorld::MakePart makes it.
//
// Unreal draws a procedural mesh by its dynamic path: every section is gathered again every frame,
// for the view and for every shadow view, with no cached draw commands. With a town's buildings,
// walls and roads in view that kept the render thread at 26-58 ms a frame in build 147 (Whaley
// Bridge, Buxworth, Taxal), whatever the graphics preset. So the triangles are also built into a
// static mesh at run time (UStaticMesh::BuildFromMeshDescriptions, the fast build, as the shed's
// catalogue bikes are), drawn by a static mesh component on this one (Look): its draws are cached and
// instanced like any placed mesh's. The procedural mesh stays only for what collides (the finest
// ground and the road: the bike's wheels and ground trace use it), hidden, so the renderer never
// sees it; one that collides with nothing holds no sections at all.
//
// Build 148 measured no render gain from it (Whaley Bridge: render 4.9 ms with, 4.6 without) and the
// builds cost the game thread in towns, so since 149 it is off: torque.RideStaticParts 1 (or
// -TorqueRideStatic=1) turns it on for comparing.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "TorqueRidePart.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
namespace TorqueWorld
{
struct RideMesh;
}

UCLASS()
class URidePart : public UProceduralMeshComponent
{
	GENERATED_BODY()
public:
	// What draws the part (null: the procedural mesh draws itself, the old way).
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Look;

	// The part's look goes with it.
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	// [perf, build 154] Its shaders (PSOs) asked for when it is registered, as a static mesh's are:
	// Unreal's procedural mesh asks for none, so each ride material's first sight compiled them on the
	// render thread (rounds 151-153: 620-750 ms hitches after "PSO PRECACHING MISS" for M_RideSign,
	// M_RideBrick, M_RideRoad...). torque.RidePartPrecache=0 turns it off.
	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FMaterialInterfacePSOPrecacheParamsList& OutParams) override;

	// Whether parts are drawn as static meshes now (torque.RideStaticParts, -TorqueRideStatic=).
	static bool StaticLooks();

	// The sections' triangles as one static mesh, a material slot per section in their order (null
	// meshes and those with under 3 points or 3 indices left out, as MakePart leaves them out); null
	// if none is left or the build fails. Points are the part's own (metres, the ride's frame:
	// x east, y up, z south), turned into Unreal's centimetres as MakePart turns them.
	static UStaticMesh* BuildLook(UObject* Outer, const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections);
	// [house kit] The same with levels of detail: Lods[0] the closest, each the same sections in the same order
	// (a section empty at a level keeps its slot with one hidden speck of a triangle); ScreenSizes the screen
	// size each level takes over at (one per level, the first 1). Null if the closest has nothing or the build fails.
	static UStaticMesh* BuildLookLods(UObject* Outer, const TArray<TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>>& Lods, const TArray<float>& ScreenSizes);
};
