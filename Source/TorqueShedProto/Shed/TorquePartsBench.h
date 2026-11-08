// A bay's parts table: the last few parts taken off that bay's bike lie on it (the rest of the
// spares are on the parts store's shelves, ATorquePartsStore). Look at one and press E (Cross) to
// fit it back; the part it replaces lands here in its place. Everyone in the shed sees the same.
// The actor sits on the table top, its +x along the table.
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
	// The bay whose bike's parts lie here.
	UPROPERTY(EditAnywhere, Category = "Bench") int32 Bay = 0;

	UPROPERTY(VisibleAnywhere, Category = "Bench") USceneComponent* Root;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Items;
	UPROPERTY(Transient) TArray<USceneComponent*> Holders;

private:
	void Rebuild();
	void BikeChanged(int32 Bike) { if (Bike == Bay) Rebuild(); }
	FDelegateHandle Watch;
};
