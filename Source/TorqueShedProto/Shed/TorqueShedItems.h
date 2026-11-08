// Every part in the shed that isn't on a bike: on the workbench, the repair bench, in the parts
// washer, on the tyre changer or the engine stand, on the floor, in the boxes stacked in the
// deliveries bay, and in each player's hands (Torque Shed's Workshop draws them all from
// shed_state's items). Drawn from UTorqueBuildSubsystem, so everyone in a co-op shed sees the same
// parts, and sees what each of the others is carrying. One in the shed level (the level script
// places it; the walker makes one if it's missing).
//
// A part the Speed Twin model has is drawn as that model part (its own, or its fit option for a
// catalogue part); one it hasn't is a block the size of its kind, in the catalogue's colour.
// Boxed parts are cardboard boxes. Every mesh is tagged Item:<id> for the walker to point at.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedItems.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class ATorqueShedItems : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedItems();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// The item a mesh shows (0 for none).
	static int32 ItemOf(const UActorComponent* C);
	// All the meshes, for the walker's look test.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Meshes;
	// Where an item is drawn now (its box's centre), if it's drawn.
	bool WhereIs(int32 Item, FVector& Out) const;

	UPROPERTY() UStaticMesh* ShapeCube = nullptr;

private:
	// One drawn item: a holder with its meshes under it.
	struct FDrawn
	{
		int32 Item = 0;
		TWeakObjectPtr<USceneComponent> Holder;
		FVector Offset = FVector::ZeroVector;   // holder location from where the part sits (its bottom centre)
		FVector Centre = FVector::ZeroVector;   // the meshes' box centre, from the holder
		int32 HeldBy = 0;                       // a peer, if it's in someone's hands
	};
	TArray<FDrawn> Drawn;
	void Rebuild();
	void Draw(int32 Item);
	bool HandOf(int32 Peer, FTransform& Out) const;
	FDelegateHandle WatchItems, WatchBikes;
};
