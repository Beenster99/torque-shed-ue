// A bike standing in one of the shed's bays on a rear paddock stand: the same parts as the ridden
// bike, drawn as that bay's build has them (UTorqueBuildSubsystem + the fitment API), still, so
// the walker can point at a part. Redrawn whenever a part on it is swapped, by anyone in the shed.
// An empty bay shows only its stand. Until the level script has imported the model
// (Data/Bike/SpeedTwin/ue_assets.json) it's a grey-box bike.
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
void ApplyLooks(const UObject* WorldContext, int32 Bike, const TArray<UStaticMeshComponent*>& Parts);
// The same for the bike of whoever WorldContext is (a ridden pawn: its player's bay).
void ApplyLooks(const UObject* WorldContext, const TArray<UStaticMeshComponent*>& Parts);
// One spare: option Part for Slot drawn as it would be fitted to Build, under a new holder on Parent,
// shrunk (never grown) to fit inside MaxCm when that's set, and moved so its box sits centred on
// WorldBottom. Its meshes are tagged with the slot and Option:<part>. Returns how many meshes.
int32 MakeSpare(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName Slot, FName Part,
	const FVector& WorldBottom, double MaxCm, TArray<UStaticMeshComponent*>& Out, USceneComponent*& OutHolder);
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

	// Which bay this is (0 .. UTorqueBuildSubsystem::Bays - 1): the bike in it is that bay's build.
	UPROPERTY(EditAnywhere, Category = "Bike") int32 Bay = 0;

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
	void BikeChanged(int32 Bike) { if (Bike == Bay) Rebuild(); }
	void LooksChanged(int32 Bike) { if (Bike == Bay) Relook(); }
	void BuildGreyBox();
	void BuildStand();
	FDelegateHandle Watch, WatchLooks;
};
