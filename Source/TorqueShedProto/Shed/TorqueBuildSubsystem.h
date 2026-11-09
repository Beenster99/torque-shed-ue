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
#include "Shed/TorquePaintBay.h"
#include "Shed/TorqueShedPower.h"
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
	// Its paint job (shed_state's paint_quality, primer, primer_top, coat, coat_q): how well the
	// colour went on (-1: as the factory left it), grey primer (how good, and whether it's the top
	// coat, the colour kept under it), and a protective coat over it (clear lacquer, an engine enamel).
	float PaintQ = -1.0f;
	float Primer = 0.0f;
	bool bPrimerTop = false;
	FName Coat;
	float CoatQ = 0.0f;
	// Paint mixed in the mixing bay, sprayed from the gun's cup (paint_bay.gd's mix, mix_coats,
	// mix_under, flat): the recipe's code and name, its effect, how many coats of it, the colour
	// that was under the first, and a lacquer's flattener over it (0 gloss .. 0.4 matt).
	FString MixCode, MixName;
	FName MixEffect;
	int32 MixCoats = 0;
	FLinearColor MixUnder = FLinearColor(0.5f, 0.5f, 0.5f);
	float Flat = 0.0f;
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
	// Wheeled out of its bay (shed_state's bike "xf" and "pusher"): where it stands (world cm, on
	// the dyno rig or on its stand somewhere), and who's wheeling it now (0: no one).
	bool bAway = false;
	FTransform Xf;
	int32 Pusher = 0;
	// Its last two dyno runs (the latest last), and how many it's had.
	TArray<FTorqueDynoRun> DynoRuns;
	int32 DynoN = 0;
	// Its fit from geometry mode (shed_state's bike "geo": Fit/BikeGeometry text, "" for none):
	// the rig's range handles and each slot's free offset, how it's drawn. Looks only.
	FString Geo;
};

// The dyno room's kit (shed_state's dyno): the rig's four straps, the cooling fan, the lights,
// and how hot the engine on the rig has got from run after run (it fades without the fan).
struct FTorqueDynoRoom
{
	bool Straps[4] = {false, false, false, false};
	bool bFan = false;
	bool bLights = true;
	float Heat = 0.0f;
	int32 StrapsDone() const { int32 N = 0; for (bool B : Straps) N += B ? 1 : 0; return N; }
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
	GrabBike,                            // take hold of bay Bay's bike to wheel it about (hold F)
	MoveBike,                            // ... wheeling it: it's at Place now
	ReleaseBike,                         // ... let go at Place: into its bay, onto the dyno rig, or on its stand
	DynoStrap,                           // strap Item (0 .. 3) on the dyno rig done up or undone
	DynoFan,                             // the dyno room's cooling fan on or off
	DynoLights,                          // its lights on or off
	DynoRun,                             // a run on the dyno for the bike on the rig (Bay)
	GeoSave,                             // geometry mode's fit (Text) kept with bay Bay's bike
	Prime,                               // Item: grey primer (bGun: the spray gun, on a booth hook)
	Coat,                                // Item: a protective coat, Finish its id (Coats()): a lacquer or an engine enamel
	// The paint mixing bay (paint_bay.gd's paint_* actions, TorquePaintBay.cpp). Item, Colour,
	// Amount, Tilt and Text say which tin, base, tint, how much and what name, as each notes.
	PaintBench,                          // step up to the bench (Item 1) or away from it (Item 0)
	PaintTinNew,                         // a litre of base Colour on the scale
	PaintPour,                           // Amount grams of tint Colour poured, the bottle tipped to Tilt
	PaintPourStop,                       // the bottle back on the rack
	PaintStir,                           // the tin on the scale stirred by Amount (of an even mix)
	PaintTare,                           // the scale zeroed
	PaintAddBase,                        // 250 g more base, to bring an over-pour back
	PaintTinEmpty,                       // the tin on the scale tipped away
	PaintTest,                           // a test card sprayed from it
	PaintShake,                          // it into the shaker
	PaintTinSave,                        // named Text, onto the tin shelf and the swatch wall
	PaintRecipe,                         // recipe Item's card on the bench (Colour 1: match it by eye)
	PaintTarget,                         // the card and the colour to match put away
	PaintBuy,                            // Item tins or bottles more of stock Colour (FTorquePaintBay::Stock)
	PaintTinTake,                        // tin Item off the shelf into your hands
	PaintTinPut,                         // the tin in your hands onto shelf slot Item
	PaintTinBench,                       // ... onto the scale
	PaintTinBin,                         // ... into the bin
	PaintCup,                            // the gun's cup filled from it
	PaintHook,                           // booth hook Item's tip set to Amount cm off the floor
	Radio,                               // the shed radio on (Item 1) or off (Item 0), tuned to station Colour
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
	FString Text;                 // GeoSave: the fit (Fit/BikeGeometry text)
	bool bGun = false;            // Paint, Prime, Coat: with the spray gun (it runs off the booth's air line)
	float Amount = 0.0f;          // the mixing bay: grams poured, how much stirred
	float Tilt = 0.0f;            // ... how far the bottle's tipped
};

DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueBikeEvent, int32 /*Bay*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTorqueActivityEvent, const FString& /*What*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FTorqueTellEvent, int32 /*Peer*/, const FString& /*Text*/, bool /*bOk*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FTorqueRequestEvent, const FTorqueShedRequest& /*Request*/, int32 /*HeldBefore*/);
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
	static constexpr int32 StartBikes = 2;   // a new shed: a project bike in each of the first two bays
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
	// For TorqueShedTest's "restyle" only (host): every part of a catalogue bike that isn't Style's
	// reference part (the frame too) thrown away, then the bike finished as Style (TestFinish), so
	// one bay rides each style in turn. Says what it did.
	FString TestRestyle(int32 Bay, const FString& Style);
	// A test shortcut (UTorqueShedTest georeload): the shed saved to its slot, forgotten, and loaded
	// back from the slot, as the game coming back up would. Says what came back.
	FString TestReload();
	// The bike's parts as fitting's layout reads them (Godot slot -> catalogue id, "none"); false if
	// any isn't a catalogue part (a Speed Twin's own), or there's no layout. Such a bike is drawn
	// from the Blender parts (TorqueShedCatalogue).
	bool LayoutParts(int32 Bay, TMap<FString, FString>& Out) const;
	// The bay a fitted item is on (through its engine), or INDEX_NONE.
	int32 BayOf(int32 ItemId) const;

	// --- Wheeling bikes and the dyno room (shed_state's bike xf, pusher and dyno) ----------
	// Where a bay's bike stands: its bay's paddock stand, or wherever it was wheeled to.
	static FTransform HomeOf(int32 Bay);
	FTransform BikeTransform(int32 Bay) const;
	bool IsAway(int32 Bay) const { return Exists(Bay) && Bikes[Bay].bAway; }
	// The bay whose bike Peer is wheeling, or INDEX_NONE.
	int32 PushedBy(int32 Peer) const;
	// The dyno rig (dyno_room.gd rig_transform: on the drum, nose to the far wall), its strap posts.
	static FTransform RigTransform();
	static FVector StrapPost(int32 Point);
	bool OnDyno(int32 Bay) const;
	int32 DynoBay() const;   // the bike on the rig (not being wheeled), or INDEX_NONE
	bool Strapped() const { return DynoBay() != INDEX_NONE && Dyno.StrapsDone() == 4; }
	const FTorqueDynoRoom& DynoRoom() const { return Dyno; }
	// The shed radio everyone hears (shed_state.gd radio): on or off, and its station
	// (TorqueShedRadio::Stations). Each player's game streams it itself (ATorqueShedRadio).
	bool RadioOn() const { return bRadioOn; }
	int32 RadioStation() const { return RadioTuned; }
	bool HasDynoRun(int32 Bay) const { return Exists(Bay) && Bikes[Bay].DynoRuns.Num() > 0; }
	const FTorqueDynoRun& DynoRunOf(int32 Bay) const;
	// The game's clock everyone shares (the server's world time), that runs are played back by.
	double SharedNow() const;
	// The bike's numbers, as Torque Shed works them out (power, torque, mass).
	FTorqueEngineNumbers NumbersOf(int32 Bay) const { return TorqueShedPower::Numbers(*this, Bay); }

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
	// How a repainted part's finish looks (bike_model.gd _apply_finish): gloss is wet-look, matte
	// soaks up the light, metallic has flake under the lacquer, chrome is a tinted mirror, patina is
	// faded old paint; primer is flat grey, and a lacquer's flattener takes the shine off. The colour
	// as the finish shows it, its metal and roughness. False for a part as it came (the model's own).
	static bool FinishLook(const FTorqueItem& It, FLinearColor& OutColour, float& OutMetallic, float& OutRoughness);
	// A part sprayed with an effect paint from the mixing bay (pearl, candy, flake, chameleon): its
	// spec with the colour it shows and its finish's roughness (TorqueEffectPaint.h). False for any
	// other paint, or under primer.
	static bool EffectLook(const FTorqueItem& It, TorquePaintMix::FPaintSpec& Out);
	bool EffectOf(int32 Bay, FName Slot, TorquePaintMix::FPaintSpec& Out) const;
	bool FinishOf(int32 Bay, FName Slot, FLinearColor& OutColour, float& OutMetallic, float& OutRoughness) const;
	static constexpr float RideDirt = 0.3f;    // added by each ride (shed_state ROAD_DIRT)
	static constexpr float JetClean = 0.07f;   // the pressure washer, each pass (JET_CLEAN)
	static constexpr float MaxPaintRust = 0.2f;
	// The spray can's colours and finishes (shed_state PAINTS, PAINT_NAMES, FINISHES).
	static const TArray<FLinearColor>& Paints();
	static FString PaintName(int32 Index);
	static const TArray<FName>& Finishes();
	static FString FinishName(FName Finish);
	// The spray gun's protective coats (shed_state COATS, COAT_ORDER): clear and satin clear
	// lacquer, black and silver engine enamel. The lacquers go on with a rattle can too.
	static const TArray<FName>& Coats();
	static FString CoatName(FName Coat);
	static bool IsLacquer(FName Coat);
	// Grey primer, as it shows (shed_state PRIMER_COLOR).
	static FLinearColor PrimerColour() { return FLinearColor(0.42f, 0.43f, 0.44f); }
	// The colour a part shows: grey while primer is its top coat, else its paint.
	static FLinearColor ShownColour(const FTorqueItem& It) { return It.bPrimerTop ? PrimerColour() : It.Colour; }
	// Painted or primed by someone (not as it came): the model takes its colour.
	static bool Repainted(const FTorqueItem& It) { return !It.Finish.IsNone() || It.bPrimerTop; }
	// "Paint job: a good coat (91%), over primer, lacquered", or "" for a factory finish.
	FString PaintJobText(const FTorqueItem& It) const;
	// shed_state PRIMER_BONUS, LACQUER_BONUS; the booth_shop's finish (no dust in the wet paint).
	static constexpr float PrimerBonus = 0.08f;
	static constexpr float LacquerBonus = 0.06f;
	static constexpr float BoothFinish = 0.12f;
	// How well a coat goes on by hand until the spray job's coverage game is ported (SprayJob): a
	// steady hold lays a decent coat, the booth makes it a good one.
	static constexpr float HandCoat = 0.82f;
	// Paint mixed in the mixing bay: Paint with this Finish and bGun sprays from the gun's cup.
	static FName CupFinish() { return FName(TEXT("cup")); }

	// --- The paint mixing bay (TorquePaintBay.h) ---------------------------------------------
	const FTorquePaintBay& PaintBay() const { return Paint; }
	// How high booth hook n's tip is set (cm off the floor), as everyone sees it.
	float HookTipOf(int32 Hook) const;
	static bool IsPaintBayAct(ETorqueShedAct A);

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
	// This machine's player's request went through (done here, or sent to the host), with what was in
	// their hands before it: the sound of the job (TorqueAudio/TorqueSfx, sfx.gd job_done).
	FTorqueRequestEvent OnRequestDone;
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
	FTorqueDynoRoom Dyno;
	bool bRadioOn = false;
	int32 RadioTuned = 0;
	FTorquePaintBay Paint;
	double DynoBusyUntil = 0.0;      // the host's: no new run until this one is done (SharedNow)
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
	double LastTickAt = 0.0;   // FPlatformTime::Seconds when TickSave last ran
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
	// A project bike from the part list: a style's reference bike (any style when Style is empty),
	// every part aged. False (the bay left as it was) without the fitting layout.
	bool NewProjectBike(int32 Bay, float Age, const FString& Style = FString());
	// A loaded shed's Speed Twins become project bikes and its loose Speed Twin parts go. How many.
	int32 DropSpeedTwins();
	int32 FreeBay() const;
	int32 FreeDelivery() const;
	FString DoApply(int32 Peer, const FTorqueShedRequest& R);
	// The mixing bay's requests and the gun's cup (TorquePaintBay.cpp).
	FString CheckPaintBay(int32 Peer, const FTorqueShedRequest& R) const;
	FString ApplyPaintBay(int32 Peer, const FTorqueShedRequest& R);
	FString CheckCup(int32 Peer, const FTorqueItem& It) const;
	float SprayFromCup(int32 Peer, FTorqueItem& It);
	// After a change: redraw what changed, save, tell the net layer.
	void RebuildBuild(int32 Bay);
	// What's on a bay (and how it looks), and the loose parts, as text: the host's shed coming in
	// redraws only what changed.
	FString BaySignature(int32 Bay, bool bLooks) const;
	FString LooseSignature() const;
};
