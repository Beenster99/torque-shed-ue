// Hands-off tests of the shed, for runs on the PC with nobody at the controls:
//   -TorqueShedAction=loop
// runs Torque Shed's loop end to end on bay 1's bike (see LoopSteps in the .cpp): unbolt the
// exhaust with the wrench, take it off, restore it on the repair bench, spray it, pressure wash it
// in the wash bay, put it on the workbench, order a slip-on from the parts computer, unpack the
// box, fit it and bolt it up, ride out (Buttertubs, showing the slip-on) and come back. Or give the
// steps yourself, separated by commas. Each runs about two seconds after the last (four after a
// map has loaded), through the walker's own targeting, plan and request, as a player's would, and
// logs "Torque test: ..." lines:
//   loop                  the steps above
//   fab                   (alone) a scratch build that needs fabrication: a softail frame set up in bay 3,
//                         a four fitted to it (jobs owed), the grinder and welder making them
//   bay:<n>               the steps after work on bay n's bike (1-4; 1 to start with)
//   tool:<tool>           hands, wrench, brush, repair, spray or lance
//   crouch:<0|1>          crouch (C) or stand
//   spray:<colour>[:<finish>]  the spray can's paint: 0-15 or a name (Red, Teal...); gloss, matte, metallic, chrome, patina
//   part:<slot>           stand by the bay's bike and look at that slot's part (exhaust, tank, front_wheel...)
//   item:<what>           look at the newest part off the bikes that is <what>: a part id, a kind or a slot
//   place:<name>          look at a place: bench_0-7, repair_0-2, washer_0-1, changer, stand, general,
//                         plastic, metal (the bins), computer, bay_0-3
//   floor:<x>:<y>         look at the floor there (world cm; the wash bay is round -900:100)
//   door                  stand in the roller door
//   look:<what>           stand back and look, for a screenshot: bench, washer, repair, computer,
//                         changer, stand, bins, bike, deliveries, washbay, or any place name
//   act[:alt]             what the work button (or the second button) does to what's looked at, at once
//   hold:<seconds>        hold the work button that long (the pressure washer's passes)
//   f                     press F at what's looked at (unpack a box, the parts computer, ride out)
//   order:<part>|bike     order from the parts computer (delivered outside the roller door)
//   rideout               stand in the roller door and press F (loads Buttertubs with the bike as built)
//   back                  on the road: back to the shed (as Backspace)
//   log                   log every bay's bike and the loose parts
//   shot                  a screenshot (HighResShot 1, into Saved/Screenshots)
//   wait:<seconds>        wait longer
//   quit
// Every bay's bike is logged at every map load, so the log shows it survive riding out and coming
// back. It lives on the game instance, so it carries on across the map changes.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TorqueShedTest.generated.h"

class ATorqueWalker;

UCLASS()
class UTorqueShedTest : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Step(float Dt);
	bool RunStep(const FString& S);
	ATorqueWalker* Walker() const;
	void LogBuild(const TCHAR* Where) const;

	TArray<FString> Steps;
	int32 Next = 0;
	int32 TestBay = 0;
	int32 Tries = 0;
	bool bAimLost = false;   // the last look missed what it was after: the next act is skipped
	double Wait = 0.0;
	TWeakObjectPtr<UWorld> SeenWorld;
	FTSTicker::FDelegateHandle Ticker;
};
