// The parts bench: the last few parts taken off the bike lie on the bench top (the rest of the
// spares are on the parts store's shelves, ATorquePartsStore). Look at one and press E (Cross) to
// fit it back; the part it replaces lands on the bench in its place. The actor sits on the bench
// top, its +x along the bench.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorquePartsBench.generated.h"

class UStaticMeshComponent;

UCLASS()
class ATorquePartsBench : public AActor
{
	GENERATED_BODY()

public:
	ATorquePartsBench();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The bench top's length along +x (cm); the spares are spread over it.
	UPROPERTY(EditAnywhere, Category = "Bench") float Length = 340.0f;

	UPROPERTY(VisibleAnywhere, Category = "Bench") USceneComponent* Root;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Items;
	UPROPERTY(Transient) TArray<USceneComponent*> Holders;

private:
	void Rebuild();
	FDelegateHandle Watch;
};
