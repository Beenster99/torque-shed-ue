// The bike as the player has built it in the shed: which part is fitted in each slot. It lives on
// the game instance, so it survives riding out of the shed (OpenLevel / ServerTravel to
// Buttertubs) and coming back. The fitment API (TorqueFitApi.h) says what fits and turns a build
// into the bike's parts; the shed's bike and the ridden bike both draw from it.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Shed/TorqueFitApi.h"
#include "TorqueBuildSubsystem.generated.h"

UCLASS()
class UTorqueBuildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	const FTorqueBuild& Get() const { return Build; }

	// The option fitted in a slot (the stock one when nothing has been changed).
	FName FittedIn(FName Slot) const;
	// Fit a part in a slot, if the fitment API allows it; tells everyone watching.
	bool Fit(FName Slot, FName PartId, FText& WhyNot);
	// The next option for the slot after the fitted one (wrapping round), for a quick swap.
	FName NextOption(FName Slot) const;

	// The build of whoever is asking, from any world object (a default build without a game instance).
	static const FTorqueBuild& BuildFor(const UObject* WorldContext);
	static UTorqueBuildSubsystem* Of(const UObject* WorldContext);

	FSimpleMulticastDelegate OnChanged;

	// How the bike looks, beside what's fitted (the strip / clean / paint loop, first slice):
	// road dirt per model part (0 clean .. 1 filthy) and bodywork painted a new colour.
	// The bike starts lightly ridden-in; every ride out adds more; a wash takes it off.
	float DirtOf(FName Part) const;
	// Takes Amount of dirt off; bTell false batches several (then call TellLooks once).
	void Clean(FName Part, float Amount, bool bTell = true);
	void TellLooks() { OnLooks.Broadcast(); }
	bool PaintOf(FName Part, FLinearColor& Out) const;
	void SetPaint(FName Part, const FLinearColor& Colour);
	// Riding out marks the bike to come back dirtier; the shed's bike calls Arrived with its parts.
	void RodeOut() { bRode = true; }
	void Arrived(const TArray<FName>& PartsOnBike);
	// Dirt or paint changed: redraw the finishes (cheap; the parts stay).
	FSimpleMulticastDelegate OnLooks;

	static constexpr float StartDirt = 0.25f;  // a stock part when the game starts: ridden, not neglected
	static constexpr float RideDirt = 0.3f;    // added by each ride

private:
	FTorqueBuild Build;
	TMap<FName, float> Dirt;
	TMap<FName, FLinearColor> Paint;
	bool bRode = false;
};
