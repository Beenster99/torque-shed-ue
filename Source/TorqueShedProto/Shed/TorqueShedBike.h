// The Speed Twin standing in the shed on a rear paddock stand: the same parts as the ridden bike,
// drawn as the build has them (UTorqueBuildSubsystem + the fitment API), still, and solid to a
// look trace so the walker can point at a part. Redrawn whenever a part is swapped. Until the
// level script has imported the model (Data/Bike/SpeedTwin/ue_assets.json) it's a grey-box bike.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedBike.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
struct FTorqueBuild;

// What the shed's actors share about the bike model: the importer's turn of the glTF frame and
// where each moving group's parts are modelled from (pawn-local cm).
namespace TorqueShedModel
{
bool LoadFrame(FTransform& Fix, TMap<FName, FVector>& GroupOrigins);
// Parts tagged with the slot they belong to read back with this ("" when none).
FName SlotTag(const UActorComponent* C);
FName OptionTag(const UActorComponent* C);
FName PartTag(const UActorComponent* C);
// Bodywork that takes paint (tank, side panels, mudguard, tail, hugger).
bool IsBodywork(FName Part);
// A part's name for people: "side_panel_l" -> "side panel l".
FString PartLabel(FName Part);
// Dirt and paint from the build onto the parts' finishes (the bike's materials take Grime and Color).
void ApplyLooks(const UObject* WorldContext, const TArray<UStaticMeshComponent*>& Parts);
// A slot's name for people: "exhaust" -> "Exhaust".
FString SlotLabel(FName Slot);
// Makes the parts of a build under Parent; only the parts in OnlySlot when it's set.
int32 MakeParts(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName OnlySlot,
	TArray<UStaticMeshComponent*>& Out);
}

UCLASS()
class ATorqueShedBike : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedBike();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The slot a part of this bike belongs to (NAME_None for the frame, tank and the rest).
	FName SlotOf(const UPrimitiveComponent* C) const;

	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* Root;
	// The bike, pitched nose down a little by the stand under the swingarm.
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* BikeRoot;
	// Keeps the walker from walking through the bike.
	UPROPERTY(VisibleAnywhere, Category = "Bike") UBoxComponent* Blocker;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Parts;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Stand;
	UPROPERTY() UStaticMesh* ShapeCube = nullptr;
	UPROPERTY() UStaticMesh* ShapeCylinder = nullptr;

private:
	void Rebuild();
	void Relook();
	void BuildGreyBox();
	void BuildStand();
	FDelegateHandle Watch, WatchLooks;
};
