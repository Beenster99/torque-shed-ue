// Hands-off tests of the shed, for runs on the PC with nobody at the controls:
//   -TorqueShedAction=swap:handlebars:clubman_bar,bench:exhaust:peashooter_twins,shot,rideout,wait:8,shot,back,look:bike,shot,quit
// Each step runs about two seconds after the last (and four after a map has loaded), through the
// walker's own targeting and the same code as E and F, and logs "Torque test: ..." lines:
//   bay:<n>               the steps after work on bay n's bike and table (1-4; 1 to start with)
//   new:<n>[:<bike>]      start a new bike (speed_twin_1200 unless named) in empty bay n
//   swap:<slot>[:<part>]  stand by the bike, look at that slot's part and press E until <part> is on
//                         (once, without a part); a part that won't fit logs its reason
//   bench:<slot>:<part>   stand at the bench, look at that spare and press E
//   rideout               stand in the roller door and press F (loads Buttertubs with the build)
//   back                  on the road: back to the shed (as Backspace)
//   wash:<part>[:<s>]     look at that part (tank, front_rim, silencers...) and hold wash for <s> (3) seconds
//   dirt[:<part>]         log the bike's dirt (average, and that part's) and its paint
//   paint:<paint>:<part>  pick a paint off the rack (a name, spaces as underscores: Carnival_Red, or 0-7), then E on the part
//   look:bike|bench|door  stand and look there (for a screenshot)
//   shot                  a screenshot (HighResShot 1, into Saved/Screenshots)
//   wait:<seconds>        wait longer
//   quit
// Every bay's build is logged at every map load, so the log shows it survive riding out and coming back.
// It lives on the game instance, so it carries on across the map changes.
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
	double Wait = 0.0;
	TWeakObjectPtr<UWorld> SeenWorld;
	FTSTicker::FDelegateHandle Ticker;
};
