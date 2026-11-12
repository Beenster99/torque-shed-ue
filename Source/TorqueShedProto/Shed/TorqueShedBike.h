// A bike standing in one of the shed's bays on a rear paddock stand: the same parts as the ridden
// bike, drawn as that bay's bike has them (UTorqueBuildSubsystem + the fitment API), still, so
// the walker can point at a part. Only the slots with a part in them are drawn; each part is
// tagged with its slot (Torque Shed's slot names). Redrawn whenever a part goes on or comes off,
// by anyone in the shed. An empty bay shows only its stand. Until the level script has imported the model
// (Data/Bike/SpeedTwin/ue_assets.json) it's a grey-box bike.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "Shed/TorqueGeometryMode.h"
#include "TorqueShedBike.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
struct FTorqueBuild;
struct FTorqueResolvedPart;

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
// Dirt, rust and paint from the bay's parts onto their finishes (the bike's materials take Grime,
// Rust and Color), by each part's slot tag (or, untagged, its stock model part's slot).
void ApplyLooks(const UObject* WorldContext, int32 Bike, const TArray<UStaticMeshComponent*>& Parts);
// The road dirt in a part's material (Tools/ue_bike_dirt.py): how many dead flies a slot's part
// collects up front (Dirt.FLY_SLOTS), and its mesh's frame in the bike's and the bike's height.
float FlyGain(FName Slot);
void SetDirtFrame(UMaterialInstanceDynamic* M, const FTransform& PartToBike, const FVector2D& ZRange);
// The same for the bike of whoever WorldContext is (a ridden pawn: its player's bay).
void ApplyLooks(const UObject* WorldContext, const TArray<UStaticMeshComponent*>& Parts);
// One spare: option Part for Slot drawn as it would be fitted to Build, under a new holder on Parent,
// shrunk (never grown) to fit inside MaxCm when that's set, and moved so its box sits centred on
// WorldBottom. Its meshes are tagged with the slot and Option:<part>. Returns how many meshes.
int32 MakeSpare(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName Slot, FName Part,
	const FVector& WorldBottom, double MaxCm, TArray<UStaticMeshComponent*>& Out, USceneComponent*& OutHolder);
// A slot's name for people: "exhaust" -> "Exhaust".
FString SlotLabel(FName Slot);
// Makes the parts of a build under Parent; only the parts in OnlySlot (a fit slot) when it's set.
int32 MakeParts(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName OnlySlot,
	TArray<UStaticMeshComponent*>& Out);
// The same, keeping the parts Keep says to. Each is tagged Slot:<its slot> (TorqueShedParts) and
// Part:<model part>.
int32 MakePartsWhere(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build,
	TFunctionRef<bool(const FTorqueResolvedPart&)> Keep, TArray<UStaticMeshComponent*>& Out);
}

UCLASS()
class ATorqueShedBike : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedBike();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Where it is drawn while someone here wheels it (the walker's own, ahead of the host's word),
	// or false to follow the shed.
	void Preview(const FTransform& Xf) { PreviewXf = Xf; bPreview = true; }
	void EndPreview() { bPreview = false; }

	// Which bay this is (0 .. UTorqueBuildSubsystem::Bays - 1): the bike in it is that bay's build.
	UPROPERTY(EditAnywhere, Category = "Bike") int32 Bay = 0;

	// Where its front wheel is as drawn (cm): the axle's x along the bike from its middle, and the
	// tyre's radius. A bike lift's wheel vice slides to it (ATorqueBikeLift).
	double FrontAxleX = 70.6;
	double FrontRadius = 30.0;

	// The slot a part of this bike belongs to ("frame" for the frame and what's part of it).
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

	// Its parts as drawn, and its fit from geometry mode shown on them (the bay's saved fit, or
	// geometry mode's draft while it's open on this bike).
	FTorqueBikeFitView FitView;
	// Geometry mode's draft fit drawn in place of the saved one (null: back to the saved one).
	void Preview(const FString* Geo);
	// Geometry mode open on it: out of its bay it stands level on both wheels, not leaned on its stand.
	bool bHeldLevel = false;

private:
	TOptional<FString> PreviewGeo;
	FString FitText() const;
	void Rebuild();
	void Relook();
	void BikeChanged(int32 Bike) { if (Bike == Bay) Rebuild(); }
	void LooksChanged(int32 Bike) { if (Bike == Bay) Relook(); }
	void BuildGreyBox();
	void BuildStand();
	// Puts the paddock stand under the swingarm of the bike as drawn: its rear axle RearX along the
	// bike from its middle, AxleZ off the floor (cm).
	void PoseStand(double RearX, double AxleZ);
	FDelegateHandle Watch, WatchLooks;
	// Wheeled out of its bay (UTorqueBuildSubsystem::BikeTransform): off the paddock stand, level on
	// its wheels, gliding to where the shed says it is.
	bool bPreview = false;
	FTransform PreviewXf;
	bool bShownAway = false;
	void Follow(float Dt);
	// How it stands (beenster, Nov 12: "bikes need to sit on their sidestands when you get off them,
	// wherever you are"): 0 on the paddock stand in its bay, 1 leaned on its side stand, 2 up on its
	// centre stand, 3 upright on its wheels (being wheeled, on a lift or the dyno, or no stand fitted).
	// -1: not posed yet (Rebuild sets it back, so a rebuilt bike is posed again, not left pitched
	// nose down off its paddock stand).
	int32 ShownPose = -1;
	int32 WantedPose() const;
	void ApplyPose(int32 Pose);
	// From the last Rebuild: the bike's origin height (its rear axle, cm), its swingarm pivot (model
	// cm) and its stands (catalogue part ids, "" for none) with their surfaces as drawn (folded).
	double RearAxleZ = 30.5;
	FVector PaddockLoc = FVector(0.0, 0.0, 32.5);
	FVector SwingPivotCm = FVector(-20.0, 0.0, 10.0);
	FString SideStandId, CentreStandId;
	TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>> StandRest;
};
