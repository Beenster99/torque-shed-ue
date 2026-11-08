// The shed's bikes as the players have built them: one per bay (Bays of them, some bays empty),
// each with what's fitted in every slot, its road dirt, its paint and the parts last taken off it.
// It lives on the game instance, so it survives riding out of the shed (ServerTravel to
// Buttertubs) and coming back. The fitment API (TorqueFitApi.h) says what fits and turns a build
// into the bike's parts; the shed's bikes and the ridden bikes all draw from it.
//
// Co-op (Coop/, the shed's net layer) works like the old Torque Shed's shed_state: the host owns
// the shed. On the host the actions below change the state, save it and fire OnHostChange so the
// net layer sends it to everyone. On a client the net layer binds Forward: the actions hand it a
// request for the host instead (dirt and paint also show at once here; the host's answer comes
// back through ApplyFromHost). The shed's actors only read this, so they look the same either way.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Shed/TorqueFitApi.h"
#include "TorqueBuildSubsystem.generated.h"

class APawn;

// A part taken off a bike: which slot it came out of.
struct FTorqueRemoved
{
	FName Slot, Part;
};

// One bay's bike.
struct FTorqueShedBike
{
	bool bExists = false;
	FTorqueBuild Build;
	TMap<FName, float> Dirt;             // model part -> 0 clean .. 1 filthy (missing: as it came)
	TMap<FName, FLinearColor> Paint;     // bodywork part -> its colour (missing: factory)
	TArray<FTorqueRemoved> Removed;      // newest first: what lies on the bay's bench
	bool bRode = false;                  // ridden out: comes back dirtier
};

// What a player asks of the shed (an action, as the host applies it).
enum class ETorqueShedAct : uint8
{
	Fit,        // Slot <- Part
	Clean,      // Part loses Amount of dirt
	Paint,      // Part <- Colour
	NewBike,    // an empty bay gets a bike: BikeId
	ClearBike,  // the bay is emptied
	RodeOut,    // the bike was ridden out: dirtier when it comes back
};

struct FTorqueShedRequest
{
	ETorqueShedAct Kind = ETorqueShedAct::Fit;
	int32 Bike = 0;
	FName Slot, Part, BikeId;
	FLinearColor Colour = FLinearColor::White;
	float Amount = 0.0f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueBikeEvent, int32 /*Bike*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueActivityEvent, const FString& /*What*/);
DECLARE_DELEGATE_RetVal_OneParam(bool, FTorqueShedForward, const FTorqueShedRequest&);
DECLARE_DELEGATE_RetVal_OneParam(int32, FTorqueBikeOfPawn, const APawn*);

UCLASS()
class UTorqueBuildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Loads the shed's save (or, on a test run, starts the test slot afresh) and saves changes
	// a couple of seconds after they're made, and on the way out.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// The save slot in use: "Shed", or "ShedTest" on a test run.
	const FString& SlotName() const { return SaveSlot; }
	static bool IsTestRun();

	// --- The bays --------------------------------------------------------------------------
	static constexpr int32 Bays = 4;
	static constexpr int32 StartBikes = 2;   // a new shed: a Speed Twin in each of the first two bays
	int32 NumBikes() const { return Bikes.Num(); }
	bool Exists(int32 Bike) const { return Bikes.IsValidIndex(Bike) && Bikes[Bike].bExists; }
	// A bay's build (a stock Speed Twin for an empty or unknown bay).
	const FTorqueBuild& Get(int32 Bike) const;
	// The option fitted in a slot (the stock one when nothing has been changed).
	FName FittedIn(int32 Bike, FName Slot) const;
	// The next option for the slot after the fitted one (wrapping round), for a quick swap.
	FName NextOption(int32 Bike, FName Slot) const;
	// The parts taken off most recently (newest first, at most RecentMax): what lies on the bench.
	const TArray<FTorqueRemoved>& RecentlyRemoved(int32 Bike) const;
	static constexpr int32 RecentMax = 4;

	// How a bike looks beside what's fitted: road dirt per model part (0 clean .. 1 filthy) and
	// bodywork painted a new colour. A bike starts lightly ridden-in; every ride adds more.
	float DirtOf(int32 Bike, FName Part) const;
	bool PaintOf(int32 Bike, FName Part, FLinearColor& Out) const;
	static constexpr float StartDirt = 0.25f;  // a stock part when the game starts: ridden, not neglected
	static constexpr float RideDirt = 0.3f;    // added by each ride

	// --- Actions (forwarded to the host on a client) -----------------------------------------
	// Fit a part in a slot, if the fitment API allows it (checked here first, so a refusal shows
	// at once). On a client, true means the host has been asked.
	bool Fit(int32 Bike, FName Slot, FName PartId, FText& WhyNot);
	// Takes Amount of dirt off; bTell false batches several (then call TellLooks once).
	void Clean(int32 Bike, FName Part, float Amount, bool bTell = true);
	void TellLooks(int32 Bike);
	void SetPaint(int32 Bike, FName Part, const FLinearColor& Colour);
	// Start a project in an empty bay (BikeId: a bike the fitment data knows), or empty a bay.
	bool NewBike(int32 Bike, FName BikeId);
	void ClearBike(int32 Bike);
	// Riding out marks the bike to come back dirtier; the shed's bike calls Arrived with its parts.
	void RodeOut(int32 Bike);
	void Arrived(int32 Bike, const TArray<FName>& PartsOnBike);

	// The host: does a request (from this machine or a client's) and saves. False with WhyNot
	// when it can't be done (a part that doesn't fit, a bay that's empty).
	bool Apply(const FTorqueShedRequest& Request, FText& WhyNot);

	// --- Co-op hooks -------------------------------------------------------------------------
	// Bound by the net layer on a client: actions go to the host through it (return true if sent).
	FTorqueShedForward Forward;
	// Fires on the host after any change it has applied, for the net layer to send that bike.
	FTorqueBikeEvent OnHostChange;
	// A whole bay as it is, and the host's copy of one coming in (sets it, no save, tells everyone).
	const FTorqueShedBike& Snapshot(int32 Bike) const;
	void ApplyFromHost(int32 Bike, const FTorqueShedBike& FromHost);
	// What this machine's player is doing ("fitting clubman bars to bay 2", "" when idle).
	void ReportActivity(const FString& What);
	const FString& Activity() const { return LastActivity; }
	FTorqueActivityEvent OnActivity;
	// Leaving someone else's shed: put this player's own shed back (from their save). Until then
	// a client holds the host's bays and never saves them over its own.
	void LeaveHostShed();
	bool IsHostCopy() const { return bHostCopy; }

	// --- Whose bike ---------------------------------------------------------------------------
	// This machine's player's bike: the bay they last worked on or claimed. It's the one they ride.
	int32 MyBike() const { return Mine; }
	void SetMyBike(int32 Bike);
	FTorqueBikeEvent OnMyBike;
	// Bound by the net layer: which bay a (ridden) pawn's player has. Unbound or -1: MyBike.
	FTorqueBikeOfPawn BikeOfPawn;
	// The bay of whoever is asking (a pawn: its player's; anything else: MyBike).
	static int32 BikeFor(const UObject* WorldContext);
	// That bay's build (a default build without a game instance).
	static const FTorqueBuild& BuildFor(const UObject* WorldContext);
	static UTorqueBuildSubsystem* Of(const UObject* WorldContext);

	// A bay's parts changed (redraw it), or only its dirt or paint (cheap: the parts stay).
	FTorqueBikeEvent OnChanged;
	FTorqueBikeEvent OnLooks;

private:
	TArray<FTorqueShedBike> Bikes;
	int32 Mine = 0;
	FString LastActivity;
	bool bHostCopy = false;

	FString SaveSlot;
	bool bSaveDue = false;
	double SaveIn = 0.0;
	FTSTicker::FDelegateHandle Ticker;
	bool IsClient() const { return Forward.IsBound(); }
	void StartFresh();
	void Load();
	void Save();
	void Changed(int32 Bike, bool bParts);
	bool Ask(const FTorqueShedRequest& Request, FText& WhyNot);
	void MarkSave() { bSaveDue = true; SaveIn = 2.0; }
	bool TickSave(float Dt);
};
