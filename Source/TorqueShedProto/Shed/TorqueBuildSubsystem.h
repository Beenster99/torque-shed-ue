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

private:
	FTorqueBuild Build;
};
