// The moorland ground cover: grass, rushes, heather, moss and bracken, a million or so Nanite plants
// placed by Tools/make_foliage.py. The level holds one of these with the species' meshes and
// material (set by Tools/ue_build_level.py); at BeginPlay it reads Data/Buttertubs/Foliage/
// instances.bin and fills one instanced component per species per 256 m tile.
//
// Performance knobs: Density (or -TorqueFoliage=0.5 on the command line, or the console variable
// torque.FoliageDensity before the level loads) keeps that share of the plants; each species has its
// own cull distance, all scaled by -TorqueFoliageCull=0.7 (or torque.FoliageCull); wind stops beyond
// WpoDisableDistance; only the taller plants cast shadows, which don't re-render as they sway; and
// the plants stay out of Lumen's and the ray tracer's scene.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueFoliage.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;

USTRUCT(BlueprintType)
struct FTorqueFoliageSpecies
{
	GENERATED_BODY()

	// As in foliage.json; the file's species index is this entry's index in Species.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") TObjectPtr<UStaticMesh> Mesh = nullptr;
	// Optional: the mesh's own material when empty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") TObjectPtr<UMaterialInterface> Material = nullptr;
	// Unreal cm from the camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") float CullDistance = 6000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") bool bCastShadow = true;
	// Multiplies each plant's size (1 for our own meshes; set it for a mesh from elsewhere, such as Megascans).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") float ScaleMultiplier = 1.0f;
	// True for a mesh that is already z up in Unreal's frame (a Megascans plant, say), so the glTF
	// importer's frame isn't applied to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") bool bNativeMesh = false;
	// Several meshes for the species (scanned variants, say): each plant takes one of them, and Mesh
	// is ignored. VariantMaterials[i], if set, goes on Variants[i].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") TArray<TObjectPtr<UStaticMesh>> Variants;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") TArray<TObjectPtr<UMaterialInterface>> VariantMaterials;
	// Share of this species' plants kept (a bigger mesh needs fewer to cover the same ground).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0", ClampMax = "1"))
	float Keep = 1.0f;
};

UCLASS()
class ATorqueFoliage : public AActor
{
	GENERATED_BODY()

public:
	ATorqueFoliage();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
	FString InstancesFile = TEXT("Data/Buttertubs/Foliage/instances.bin");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
	TArray<FTorqueFoliageSpecies> Species;

	// How the glTF importer turned the glb frame into Unreal's (found from the road by the level script).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") float FrameYaw = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") float FrameMirror = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage") float FrameScale = 1.0f;

	// Share of the plants kept, 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0", ClampMax = "1"))
	float Density = 1.0f;

	// Beyond this (cm) the wind stops moving the plants: saves the vertex work on distant ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
	int32 WpoDisableDistance = 2500;

	// Make the plants now (in the editor, to look at them; Play does it by itself).
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Foliage")
	void Rebuild();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Foliage")
	void Clear();

	UPROPERTY(VisibleAnywhere, Category = "Foliage") USceneComponent* Root;
	UPROPERTY(Transient) TArray<UInstancedStaticMeshComponent*> Parts;

	int32 NumPlants = 0;
};
