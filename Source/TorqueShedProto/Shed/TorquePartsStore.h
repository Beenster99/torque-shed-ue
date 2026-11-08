// The parts store: galvanised shelving along a wall with a bay for every slot the bike has options
// for (handlebars, exhaust, saddle, mudguard...), labelled, holding that slot's spares (every option
// not on the bike; big ones shrunk to fit the bay). Look at one and press E (Cross) to fit it.
// On the bike itself, Q / R choose from the same options. The actor stands on the floor at the
// middle of the unit's foot; +x faces into the room, the bays run along +y.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorquePartsStore.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
class UTextRenderComponent;

UCLASS()
class ATorquePartsStore : public AActor
{
	GENERATED_BODY()

public:
	ATorquePartsStore();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditAnywhere, Category = "Store") float Width = 300.0f;
	UPROPERTY(EditAnywhere, Category = "Store") float Height = 200.0f;
	UPROPERTY(EditAnywhere, Category = "Store") float Depth = 45.0f;
	UPROPERTY(EditAnywhere, Category = "Store") int32 Levels = 4;

	UPROPERTY(VisibleAnywhere, Category = "Store") USceneComponent* Root;
	UPROPERTY(VisibleAnywhere, Category = "Store") UBoxComponent* Blocker;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Items;
	UPROPERTY(Transient) TArray<USceneComponent*> Holders;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Frame;
	UPROPERTY(Transient) TArray<UTextRenderComponent*> Labels;
	UPROPERTY() UStaticMesh* ShapeCube = nullptr;

private:
	void BuildFrame(int32 Columns);
	void Rebuild();
	FDelegateHandle Watch;
	int32 FrameColumns = 0;
};
