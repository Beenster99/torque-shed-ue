// The parts bench: the parts that aren't on the bike lie on the bench top, one slot's spares after
// another along its length. Look at one and press E (Cross) to fit it; the part it replaces comes
// off the bike and lands on the bench in its place. The actor sits on the bench top, its +x along
// the bench, its +y towards the wall.
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
