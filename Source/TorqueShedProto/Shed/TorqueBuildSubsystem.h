// The shed, as Torque Shed's shed_state.gd keeps it: every part is an item (a part id from the
// catalogue, its dirt, rust, wear and paint, whether it's still boxed, and where it is: on a bike,
// on an engine, in someone's hands, on the bench or a machine, on the floor, or on a wheel), and
// each bay's bike has slots holding item ids, each bolted up or not. It lives on the game
// instance, so it survives riding out of the shed (ServerTravel to Buttertubs) and coming back.
//
// Everything a player does is a request: unbolt, bolt, remove, install, take, drop, place, bin,
// clean, repair, paint, wash, order a part or a project bike, unpack, start a build from a frame,
// fit and lever off tyres. The host owns the shed (shed_state's is_authority): Check says why a
// request can't be done, Apply does it, saves, tells the player who did it (OnTell) and fires
// OnHostChange so the net layer sends the shed to everyone (SaveShed / LoadShedFromHost). On a
// client the net layer binds Forward and Request hands requests to it. The shed's actors only
// read the state, so they look the same on every machine.
//
// The one bike model so far is the Speed Twin; its own parts are items "speed_twin_<slot>". A bay's
// build for drawing (Get) shows a catalogue part as the Speed Twin's fit option for it where the
// model has one; slots with nothing in them aren't drawn (ShowsModelPart).
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Shed/TorqueFitApi.h"
#include "TorqueBuildSubsystem.generated.h"

class APawn;
class UTorqueShedSave;

// Where an item is (shed_state's "at"), and what Ref means there.
enum class ETorqueAt : uint8
{
	Bike,     // fitted to the bike in bay Ref
	Engine,   // fitted to the engine block with item id Ref
	Held,     // in the hands of player Ref
	Spot,     // on a place in the shed (Spot: "bench_2", "repair_0", "changer", "stand")
	Floor,    // on the floor, the yard or a box pile (Place, world cm)
	Wheel,    // a tyre on the wheel with item id Ref (off the bike)
};

struct FTorqueItem
{
	int32 Id = 0;
	FName Part, Kind;
	float Rust = 0.0f, Dirt = 0.0f, Wear = 0.0f;   // 0 .. 1
	FLinearColor Colour = FLinearColor(0.5f, 0.5f, 0.5f);
	FName Finish;                // NAME_None: as it came; else "gloss", "matte", "metallic", "chrome", "patina"
	bool bBoxed = false;         // as delivered: unpack it first
	ETorqueAt At = ETorqueAt::Floor;
	int32 Ref = 0;
	FName Spot;
	FTransform Place;            // on the floor: where (world cm)
	// An engine block: what's fitted to it, which travels with it.
	TMap<FName, int32> Slots;
	TMap<FName, bool> Bolted;
	int32 Tyre = 0;              // a wheel off a bike: the tyre on it
};

// One bay's bike.
struct FTorqueShedBike
{
	bool bExists = false;
	int32 Id = 0;
	FString Name;
	FName Model;                 // the model it's drawn as (the Speed Twin)
	TMap<FName, int32> Slots;    // bike slots -> item id (0: empty)
	TMap<FName, bool> Bolted;
	bool bRode = false;          // ridden out: comes back dirtier
	// Fabrication (shed_state fab_owed / fab_done): jobs a part fitted here needs made before the
	// bike rides ("engine mounts made for a V-twin"), and those made already.
	TArray<FString> FabOwed, FabDone;
};

// What a player asks of the shed (shed_state's actions).
enum class ETorqueShedAct : uint8
{
	Unbolt, Bolt, Remove, Install,      // Bay (or Engine) + Slot
	Take,                                // Item (on the floor) or Spot
	Drop,                                // what's in your hands onto the floor at Place
	Place,                               // ... onto Spot
	Bin,                                 // ... into Bin ("general", "plastic", "metal")
	Clean,                               // Items: scrubbed clean with the brush
	Repair,                              // Items: on the repair bench, rust and wear ground off
	Paint,                               // Item: sprayed Colour (an index into Paints()) and Finish
	Wash,                                // Items: the pressure washer takes some muck off each
	OrderPart,                           // Part: delivered boxed to the deliveries bay
	OrderBike,                           // a project bike, into the first empty bay
	Unpack,                              // Item: a box opened where it is
	StartBuild,                          // the frame in your hands onto bay Bay's stand
	TyreFit, TyreRemove,                 // the tyre changer (Spot "changer")
	RodeOut,                             // bay Bay's bike was ridden out
	Fabricate,                           // the grinder and welder make bay Bay's next owed job
	Count
};

struct FTorqueShedRequest
{
	ETorqueShedAct Kind = ETorqueShedAct::Take;
	int32 Bay = -1;
	int32 Engine = 0;             // an engine block's item id, for work on an engine off the bike
	FName Slot;
	int32 Item = 0;
	TArray<int32> Items;
	FName Spot, Part, Bin, Finish;
	int32 Colour = 0;
	FTransform Place;
	bool bCrouched = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueBikeEvent, int32 /*Bay*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueActivityEvent, const FString& /*What*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FTorqueTellEvent, int32 /*Peer*/, const FString& /*Text*/, bool /*bOk*/);
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
	static constexpr int32 StartBikes = 2;   // a new shed: a project Speed Twin in each of the first two bays
	int32 NumBikes() const { return Bikes.Num(); }
	bool Exists(int32 Bay) const { return Bikes.IsValidIndex(Bay) && Bikes[Bay].bExists; }
	const FTorqueShedBike& Bike(int32 Bay) const;
	// A bay's bike as the fitment API draws it (a stock Speed Twin for an empty or unknown bay).
	const FTorqueBuild& Get(int32 Bay) const;
	// Whether a part of the drawn model is on the bike (its slot has something in it).
	bool ShowsModelPart(int32 Bay, const FTorqueResolvedPart& Part) const;
	// The bike's slots: what's in one (item id, 0 for none) and whether it's bolted. Engine slots
	// are the fitted engine's; with Engine set, that engine block's (on the stand or anywhere).
	int32 ItemOn(int32 Bay, FName Slot, int32 Engine = 0) const;
	bool IsBolted(int32 Bay, FName Slot, int32 Engine = 0) const;
	// Empty slots that need a part, and parts fitted but not bolted: a bike rides with neither.
	TArray<FName> MissingSlots(int32 Bay) const;
	TArray<FName> LooseSlots(int32 Bay) const;
	bool IsComplete(int32 Bay) const;
	// The fabrication the bike still owes, the next job first (only jobs its parts still need), and
	// how long the grinder and welder take over one (shed_state fab_time: 4 s + its hours).
	TArray<FString> FabOwed(int32 Bay) const;
	static float FabTime(const FString& What);
	FString FabOwedText(int32 Bay) const;
	// For TorqueShedTest's "finish" only (host): every empty slot of a catalogue bike gets a
	// style's reference part (fitting's layout; Style empty: the bike's own style), or what Set
	// says for that slot ("none" leaves it empty), and everything on the bike is bolted up. Owed
	// fabrication stays owed. Says what it did.
	FString TestFinish(int32 Bay, const FString& Style, const TMap<FString, FString>& Set);
	// The bike's parts as fitting's layout reads them (Godot slot -> catalogue id, "none"); false if
	// any isn't a catalogue part (a Speed Twin's own), or there's no layout. Such a bike is drawn
	// from the Blender parts (TorqueShedCatalogue).
	bool LayoutParts(int32 Bay, TMap<FString, FString>& Out) const;
	// The bay a fitted item is on (through its engine), or INDEX_NONE.
	int32 BayOf(int32 ItemId) const;

	// --- The items ---------------------------------------------------------------------------
	const FTorqueItem* Item(int32 Id) const { return Items.Find(Id); }
	const TMap<int32, FTorqueItem>& AllItems() const { return Items; }
	int32 HeldBy(int32 Peer) const;
	int32 ItemAtSpot(FName Spot) const;
	// "Speed Twin exhaust"; "Condition 62%   Rust 20%   Dirt 45%   Wear 12%   factory finish".
	FString ItemTitle(int32 Id) const;
	FString ConditionText(int32 Id) const;
	static float Health(const FTorqueItem& It) { return FMath::Clamp(1.0f - 0.6f * It.Rust - 0.4f * It.Wear, 0.0f, 1.0f); }
	// How long the grinder and welder take over these (shed_state repair_time).
	float RepairTime(const TArray<int32>& Ids) const;

	// How the bike's model looks: road dirt and rust (0 .. 1) and paint, by slot (or by a stock
	// model part's name, for the ridden bike's parts that aren't tagged with a slot).
	float DirtOf(int32 Bay, FName Slot) const;
	float RustOf(int32 Bay, FName Slot) const;
	bool PaintOf(int32 Bay, FName Slot, FLinearColor& Out) const;
	static constexpr float RideDirt = 0.3f;    // added by each ride (shed_state ROAD_DIRT)
	static constexpr float JetClean = 0.07f;   // the pressure washer, each pass (JET_CLEAN)
	static constexpr float MaxPaintRust = 0.2f;
	// The spray can's colours and finishes (shed_state PAINTS, PAINT_NAMES, FINISHES).
	static const TArray<FLinearColor>& Paints();
	static FString PaintName(int32 Index);
	static const TArray<FName>& Finishes();
	static FString FinishName(FName Finish);

	// --- Requests ----------------------------------------------------------------------------
	// This machine's player (a PlayerState's player id; 1 until the walker says).
	int32 Me() const { return LocalPeer; }
	void SetLocalPeer(int32 Peer) { LocalPeer = Peer; }
	// Why Peer can't do it right now, or "" when they can (shed_state check).
	FString Check(int32 Peer, const FTorqueShedRequest& Request) const;
	// This machine's player asks: checked here, then done (the host) or sent to the host (a client).
	bool Request(const FTorqueShedRequest& Request, FString& WhyNot);
	// The host: does Peer's request if it can and saves (shed_state _apply).
	bool Apply(int32 Peer, const FTorqueShedRequest& Request, FString& WhyNot);
	// A player gone: what was in their hands goes down on the floor where they were.
	void DropHeldBy(int32 Peer, const FVector& At);
	// Riding out marks the bike to come back dirtier; the shed's bike calls Arrived when it's back.
	void RodeOut(int32 Bay);
	void Arrived(int32 Bay);

	// --- Co-op hooks -------------------------------------------------------------------------
	// Bound by the net layer on a client: requests go to the host through it (return true if sent).
	FTorqueShedForward Forward;
	// Fires on the host after any change, for the net layer to send the shed to everyone.
	FSimpleMulticastDelegate OnHostChange;
	// What the host says to the player who asked ("Fitted the clubman bars as the handlebars..."),
	// or why it couldn't (bOk false). The net layer passes other players' on to them.
	FTorqueTellEvent OnTell;
	// The whole shed as bytes (every bike and item, versioned), and the host's copy coming in
	// (sets it, no save, tells everyone). Until LeaveHostShed a client keeps the host's shed and
	// never saves it over its own.
	bool SaveShed(TArray<uint8>& Out) const;
	bool LoadShedFromHost(const TArray<uint8>& Data);
	void LeaveHostShed();
	bool IsHostCopy() const { return bHostCopy; }
	// What this machine's player is doing ("fitting the clubman bars to bay 2", "" when idle).
	void ReportActivity(const FString& What);
	const FString& Activity() const { return LastActivity; }
	FTorqueActivityEvent OnActivity;

	// --- Whose bike ---------------------------------------------------------------------------
	// This machine's player's bike: the bay they last worked on or claimed. It's the one they ride.
	int32 MyBike() const { return Mine; }
	void SetMyBike(int32 Bay);
	FTorqueBikeEvent OnMyBike;
	// Bound by the net layer: which bay a (ridden) pawn's player has. Unbound or -1: MyBike.
	FTorqueBikeOfPawn BikeOfPawn;
	// The bay of whoever is asking (a pawn: its player's; anything else: MyBike).
	static int32 BikeFor(const UObject* WorldContext);
	// That bay's build (a default build without a game instance).
	static const FTorqueBuild& BuildFor(const UObject* WorldContext);
	// Whether that bike has a part of the model on it (true without a shed: a stock bike).
	static bool RidesWith(const UObject* WorldContext, const FTorqueResolvedPart& Part);
	static UTorqueBuildSubsystem* Of(const UObject* WorldContext);

	// A bay's parts changed (redraw it), or only its dirt, rust or paint (cheap: the parts stay);
	// the loose items changed (on the bench, the floor, in someone's hands).
	FTorqueBikeEvent OnChanged;
	FTorqueBikeEvent OnLooks;
	FSimpleMulticastDelegate OnItems;

private:
	TArray<FTorqueShedBike> Bikes;
	TMap<int32, FTorqueItem> Items;
	TArray<FTorqueBuild> Builds;     // per bay, for drawing
	int32 NextId = 1;
	int32 LocalPeer = 1;
	int32 Mine = 0;
	FString LastActivity;
	bool bHostCopy = false;
	FRandomStream Rng;

	FString SaveSlot;
	bool bSaveDue = false;
	double SaveIn = 0.0;
	FTSTicker::FDelegateHandle Ticker;
	bool IsClient() const { return Forward.IsBound(); }
	void StartFresh();
	void Load();
	void Save();
	void ToSave(UTorqueShedSave& S) const;
	bool FromSave(const UTorqueShedSave& S);
	bool TickSave(float Dt);
	void MarkSave() { bSaveDue = true; SaveIn = 2.0; }

	// shed_state's host_of: the slots of a bike, or of an engine (its own, or the one in the bike).
	struct FHost
	{
		TMap<FName, int32>* Slots = nullptr;
		TMap<FName, bool>* Bolted = nullptr;
		int32 Bay = INDEX_NONE;      // the bike's bay (or the bay the engine is in)
		int32 Engine = 0;            // the engine block, for an engine slot
	};
	FHost HostOf(int32 Bay, FName Slot, int32 Engine);
	FHost HostOf(int32 Bay, FName Slot, int32 Engine) const { return const_cast<UTorqueBuildSubsystem*>(this)->HostOf(Bay, Slot, Engine); }
	FString PartsOnIt(const FHost& Host, FName Slot) const;
	FName InstallSlot(const FTorqueShedRequest& R, const FTorqueItem& It, FString& Error) const;
	// What fitting's BikeLayout says of part Part in Slot on bay Bay's bike (BikeRig.part_tier, for the
	// frame, engine, tank and fork): false when it can't judge (a Speed Twin part on it: those
	// aren't in the catalogue), else the tier ("bolts on", "fabricate", "won't fit"), why, and the jobs.
	bool FitTier(int32 Bay, FName Slot, FName Part, FString& OutTier, FString& OutReason, TArray<FString>& OutWork) const;

	int32 NewItem(FName Part, float Rust, float Dirt, float Wear);
	int32 AgedItem(FName Part, float Age);
	void Move(int32 Id, ETorqueAt At, int32 Ref, FName Spot = NAME_None);
	void EraseItem(int32 Id);
	void NewStockBike(int32 Bay, float Age);
	int32 FreeBay() const;
	int32 FreeDelivery() const;
	FString DoApply(int32 Peer, const FTorqueShedRequest& R);
	// After a change: redraw what changed, save, tell the net layer.
	void RebuildBuild(int32 Bay);
	// What's on a bay (and how it looks), and the loose parts, as text: the host's shed coming in
	// redraws only what changed.
	FString BaySignature(int32 Bay, bool bLooks) const;
	FString LooseSignature() const;
};
