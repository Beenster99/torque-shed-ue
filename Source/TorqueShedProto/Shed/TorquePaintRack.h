// The paint rack on the shed wall: a board of colour cards (grey box for spray cans). Look at a
// card and press E (Cross) to pick that colour; then E on a piece of bodywork (tank, side panels,
// mudguard, tail, hugger) paints it. The actor's +x faces into the room; the cards run along +y.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorquePaintRack.generated.h"

class UStaticMeshComponent;

struct FTorquePaint
{
	const TCHAR* Name;
	FLinearColor Colour;
};

UCLASS()
class ATorquePaintRack : public AActor
{
	GENERATED_BODY()

public:
	ATorquePaintRack();
	virtual void BeginPlay() override;

	static const TArray<FTorquePaint>& Paints();
	// Which paint a card is (INDEX_NONE for the board).
	int32 PaintOf(const UStaticMeshComponent* C) const { return Cards.IndexOfByKey(C); }

	UPROPERTY(VisibleAnywhere, Category = "Paint") USceneComponent* Root;
	UPROPERTY(VisibleAnywhere, Category = "Paint") UStaticMeshComponent* Board;
	UPROPERTY(VisibleAnywhere, Category = "Paint") TArray<UStaticMeshComponent*> Cards;
};
