#include "Shed/TorqueShedTest.h"
#include "Shed/TorqueShedRadio.h"
#include "Shed/TorqueBikeLift.h"
#include "Shed/TorqueBoothHooks.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueSettings.h"

#include "GameFramework/PlayerState.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueWalker.h"
#include "TorqueBikePawn.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace TorqueShedTestImpl
{
// Torque Shed's loop on bay 1's bike, end to end: every tool, the repair bench, the spray can, the
// wash bay, the workbench, the parts computer, a delivery, fitting and bolting, riding out and back.
const TCHAR* const LoopSteps =
	TEXT("bay:1,restyle:cafe,log,look:bike,shot,look:screen,shot,look:board,shot,look:bench,shot,look:computer,shot,look:monitor,shot,")
	TEXT("place:radio,act,wait:8,radio,shot:ui,act,wait:6,radio,f,radio,")   // the radio: on, retuned, off
	TEXT("tool:wrench,part:exhaust,act,")                    // unbolt the exhaust
	TEXT("tool:hands,part:exhaust,act,")                     // take it off: in your hands
	TEXT("place:repair_0,act,")                              // onto the repair bench
	TEXT("tool:repair,item:exhaust,act,")                    // grind off the rust
	TEXT("tool:spray,spray:7:matte,item:exhaust,act,shot,")  // matte teal
	TEXT("tool:hands,item:exhaust,act,")                     // pick it up
	TEXT("floor:-900:100,act,")                              // down in the wash bay
	TEXT("tool:lance,item:exhaust,hold:3,")                  // pressure wash it
	TEXT("tool:hands,item:exhaust,act,place:bench_0,act,")   // onto the workbench
	TEXT("look:computer,f,computer:exhaust:slip_on,shot:ui,computer:paint:tints,shot:ui,computer:close,")   // the computer's page
	TEXT("order:slip_on,order:steel_backbone,look:deliveries,shot,item:slip_on,f,item:slip_on,act,")   // order a slip-on (and a frame: a crate), the boxes, unpack it, pick it up
	TEXT("floor:300:700,act,item:slip_on,act,")               // put it down in the yard and pick it up again
	TEXT("part:tank,act,")                                   // fit it to the bike
	TEXT("tool:wrench,part:exhaust,act,log,")                // bolt it up
	TEXT("tool:hands,rideout,wait:10,shot,back,wait:6,log");

// A scratch build that needs fabrication. In Torque Shed (bike_rig part_fit) only a frame, engine,
// tank or fork going onto a bike that already has a frame and an engine can owe jobs, so: order a
// steel backbone frame and set it up in the empty bay 3, finish the rest as the mx's reference bike
// with a 660 triple and no fork (a test shortcut: "finish"), then order an upside-down sport fork
// and fit it (owes "downtubes reshaped to clear the front tyre"), make the job with the grinder and
// welder (fabricate) and ride it out.
const TCHAR* const FabSteps =
	TEXT("bay:3,order:steel_backbone,item:steel_backbone,f,item:steel_backbone,act,")
	TEXT("place:bay_2,act,look:bike,shot,")
	TEXT("finish:mx:engine=triple_660:fork=none,log,")
	TEXT("order:usd_sport,item:usd_sport,f,item:usd_sport,act,")
	TEXT("part:frame,act,log,look:bike,shot,")
	TEXT("tool:repair,fabricate,log,")
	TEXT("tool:wrench,part:fork,act,log,look:bike,shot,look:screen,shot,")   // the fork goes on loose: bolt it up
	TEXT("tool:hands,rideout,wait:10,shot,back,wait:6,log");

// The dyno room, end to end on bay 1's bike: wheel it onto the rig, strap it down at the four posts,
// a run with the cooling fan on and one with it off (heat soaked, down on power), the big screen
// part way through the second run and after it, then the straps undone and the bike wheeled home.
const TCHAR* const DynoSteps =
	TEXT("bay:1,restyle:cafe,tool:hands,wheel:floor,wait:2,bikelog,look:bikenow,shot,wheel:home,wait:2,bikelog,")
	TEXT("wheel:rig,wait:2,bikelog,look:dynoroom,shot,")
	TEXT("strap:0,strap:1,strap:2,strap:3,look:dynoroom,shot,")
	TEXT("fan:on,dynorun,dynolog,look:dyno,shot,")
	TEXT("fan:off,dynorun:mid,look:dyno,shot,dynolog,look:dyno,shot,look:dynoroom,shot,")
	TEXT("strap:0,strap:1,strap:2,strap:3,wheel:home,wait:2,bikelog,log");

// The paint shop's spray bay on bay 1's bike: take the exhaust off, grind its rust off on the
// repair bench (too rusty to paint otherwise), hang it on the first hook and spray it with the gun
// in the booth: grey primer, gloss red, then clear lacquer, a shot after each; then the exhaust back
// on and the gun tried on it there (refused: it runs off the booth's air line) and the rattle can
// (works anywhere; the exhaust's ground back, where the tank's too rusty for paint).
const TCHAR* const BoothSteps =
	TEXT("bay:1,restyle:cafe,tool:wrench,part:exhaust,act,tool:hands,part:exhaust,act,")
	TEXT("place:repair_0,act,tool:repair,item:exhaust,act,tool:hands,item:exhaust,act,")
	TEXT("place:hook_0,act,look:booth,shot,look:boothceiling,shot,")
	TEXT("tool:gun,spray:0:primer,item:exhaust,act,log,look:hook_0,shot,")
	TEXT("spray:0:gloss,item:exhaust,act,look:hook_0,shot,")
	TEXT("spray:0:clear,item:exhaust,act,log,look:hook_0,shot,look:booth,shot,")
	TEXT("paint:hook:1:185,wait:2,look:hook:1,shot,paint:hook:1:138,wait:2,look:hook:1,shot,")
	TEXT("tool:hands,item:exhaust,act,part:tank,act,tool:wrench,part:exhaust,act,")
	TEXT("tool:gun,spray:3:gloss,part:exhaust,act,")
	TEXT("tool:spray,spray:1:gloss,part:exhaust,act,look:bike,shot,log");

// The spray mini game (TorqueSprayJob, TorqueWalkerSpray.cpp) on the exhaust hung in the booth: the
// gun's primer put on in passes all round, then red: one pass (thin, wet, with the scan on it), the
// rest, and lacquer held too far off (dry) and too close (runs), a shot after each.
const TCHAR* const SprayGameSteps =
	TEXT("bay:1,restyle:cafe,tool:wrench,part:exhaust,act,tool:hands,part:exhaust,act,")
	TEXT("place:repair_0,act,tool:repair,item:exhaust,act,tool:hands,item:exhaust,act,")
	TEXT("place:hook_0,act,tool:gun,spray:0:primer,item:exhaust,shot,spraypass:2:80,item:exhaust,shot,")
	TEXT("item:exhaust,spraypass:12:80,item:exhaust,shot,wait:3,item:exhaust,shot,")
	TEXT("spray:0:gloss,item:exhaust,spraypass:1:80,item:exhaust,shot,sprayscan:1,wait:1,shot,sprayscan:0,")
	TEXT("item:exhaust,spraypass:12:80,item:exhaust,shot,wait:3,item:exhaust,shot,")
	TEXT("spray:0:clear,item:exhaust,spraypass:1:170,item:exhaust,shot,item:exhaust,spraypass:3:35,item:exhaust,shot,")
	TEXT("item:exhaust,spraypass:12:80,wait:3,item:exhaust,shot,log");

// The Esc menu and your profile (the pages are put up for the shots: key presses don't reach a
// test run): the pause menu, the Profile page with the jaw and chin moved and the figure taller, then
// the Settings. The face isn't saved, so your own comes back next time.
const TCHAR* const MenuSteps =
	TEXT("menu:pause,wait:1,shot:ui,menu:profile,wait:2,shot:ui,face:jaw:0.8,face:chin:-0.6,face:height:0.8,wait:2,shot:ui,")
	TEXT("face:jaw:0,face:chin:0,face:height:0,menu:settings,wait:1,shot:ui,menu:close");

// The paint mixing bay (paint_bay.gd): at the bench a litre of white with phthalo blue and lemon
// yellow poured in (a green), stirred, a test card, saved as Mix 1 (its card on the swatch wall);
// that card on the bench and mixed again to it as Mix 2; a satin lacquer (10% flattener); a silver with ultramarine
// in the shaker. Then the bay's exhaust (off bay 1's bike, ground back on the repair bench) hung in
// the spray bay and sprayed with Mix 1 from the gun's cup, two coats, then the satin lacquer over it.
// Then twin stripes masked off with tape, Mix 3 (the silver) sprayed over, and the tape peeled.
const TCHAR* const MixSteps =
	TEXT("bay:1,restyle:cafe,look:paintbay,shot,bench:on,paint:newtin:W,paint:pour:PB:40,paint:pour:LY:12,paintlog,shot:ui,look:mixbench,shot,")
	TEXT("paint:stir:1,paint:test,paint:save,paintlog,look:swatches,shot,look:teststand,shot,")
	TEXT("bench:on,paint:recipe:0,paint:newtin:W,paint:pour:PB:38,paint:pour:LY:13,paint:stir:1,paint:save,paintlog,")
	TEXT("paint:newtin:L,paint:pour:FL:100,paint:stir:1,paint:save:Satin clear,")
	TEXT("paint:newtin:S,paint:pour:UB:20,paint:shake,wait:22,paintlog,paint:save,bench:off,paintlog,look:mixbench,shot,")
	TEXT("bench:on,paint:newtin:C,paint:pour:CR:30,paint:stir:1,paint:save:Candy red,paint:newtin:W,paint:pour:PW:20,paint:pour:XP:12,paint:stir:1,paint:save,paintlog,bench:off,")   // the effects rack
	TEXT("tool:wrench,part:exhaust,act,tool:hands,part:exhaust,act,place:repair_0,act,tool:repair,item:exhaust,act,")
	TEXT("tool:hands,item:exhaust,act,place:hook_0,act,")
	TEXT("paint:take:0,paint:cup,paint:put:0,paintlog,tool:gun,spray:0:cup,item:exhaust,act,item:exhaust,act,log,look:hook_0,shot,")
	TEXT("tool:hands,paint:take:2,paint:cup,paint:put:2,tool:gun,spray:0:cup,item:exhaust,act,log,paintlog,look:hook_0,shot,")
	TEXT("tool:hands,paint:tapetake,paint:pattern:2,item:exhaust,act,log,look:hook_0,shot,paint:tapeput,")   // masking tape: twin stripes
	TEXT("paint:take:3,paint:cup,paint:put:3,tool:gun,spray:0:cup,item:exhaust,act,item:exhaust,act,log,look:hook_0,shot,")
	TEXT("tool:hands,item:exhaust,act:alt,log,look:hook_0,shot");

// The bike lifts on bay 1's bike: its post, raised (the bike rides up), wheeling it off refused,
// a part off it standing (no crouching), lowered, wheeled out, onto the wash bay's lift and up on
// that, down and home.
const TCHAR* const LiftSteps =
	TEXT("bay:1,restyle:cafe,tool:hands,lifts,look:lift_0,shot,place:lift_0,f,wait:4,lifts,look:bike,shot,")
	TEXT("wheel:floor,lifts,tool:wrench,part:exhaust,act,log,")
	TEXT("place:lift_0,f,wait:4,lifts,tool:hands,wheel:floor,wait:2,bikelog,wheel:wash,wait:2,bikelog,")
	TEXT("place:lift_wash,f,wait:5,lifts,look:washbay,shot,place:lift_wash,f,wait:5,lifts,wheel:home,wait:2,bikelog,lifts,log");

// The wash game: bay 1's bike wheeled into the wash bay, the lance held on its tank and its engine (the
// muck coming off where the jet goes, the shed told), the scan on, the bike raised on the wash lift
// and its underside washed crouched, then home.   [wash]
const TCHAR* const WashGameSteps =
	TEXT("bay:1,tool:hands,wheel:wash,wait:2,bikelog,tool:lance,look:washbay,shot,washstate,")
	TEXT("part:tank,hold:3,washstate,shot,part:engine,hold:3,washstate,shot,washpass:1:60,shot,washpass:1:60,look:washbay,shot,log,")
	TEXT("tool:hands,place:lift_wash,f,wait:5,lifts,tool:lance,crouch:1,part:engine,hold:3,washstate,washpass:1:60,shot,crouch:0,look:washbay,shot,")
	TEXT("tool:hands,place:lift_wash,f,wait:5,wheel:home,wait:2,bikelog,log");

// Geometry mode on a catalogue bike: a steel backbone set up in the empty bay 3 and finished as a cafe
// racer (a test shortcut), a rig handle slid, the bars up 2 cm and the seat back 1 cm, saved; then
// the shed saved, forgotten and loaded back, the map opened again, and the fit checked on the bike.
const TCHAR* const GeoSteps =
	TEXT("bay:3,order:steel_backbone,item:steel_backbone,f,item:steel_backbone,act,")
	TEXT("place:bay_2,act,finish:cafe,log,look:bike,shot,")
	TEXT("geo:open,wait:3,geo:rig:tank_up:12,wait:3,geo:move:handlebars:0:2:0,geo:move:saddle:0:0:1,")
	TEXT("geo:select:handlebars,shot:ui,geo:save,wait:3,shot:ui,geo:exit,look:bike,shot,geo:check,")
	TEXT("georeload,wait:8,log,geo:check,look:bike,shot");

// The same on bay 1's project bike (remade as a cafe racer first, so every run's the same bike), then
// ridden out: Buttertubs shows the fit.
const TCHAR* const GeoTwinSteps =
	TEXT("bay:1,restyle:cafe,log,look:bike,shot,geo:open,wait:3,geo:move:handlebars:0:2:0,geo:move:saddle:0:0:1,")
	TEXT("geo:select:handlebars,shot:ui,geo:save,wait:3,geo:exit,look:bike,shot,geo:check,")
	TEXT("tool:hands,rideout,wait:10,shot,back,wait:6,geo:check");

// A scratch-built bike ridden: a steel backbone set up in the empty bay 3 and finished as a cafe
// racer (a test shortcut), ridden out on its own physics; its spec logged at the start, then the
// robot rider (-TorqueAutoRide's) takes it 10 s down the road, logged again (speed, gear, the
// wheels' spin, the bars' turn, the suspension), and back to the shed.
const TCHAR* const RideSteps =
	TEXT("bay:3,order:steel_backbone,item:steel_backbone,f,item:steel_backbone,act,")
	TEXT("place:bay_2,act,finish:cafe,log,look:bike,shot,")
	TEXT("tool:hands,rideout,wait:6,ridelog,robot:on,wait:10,ridelog,shot,robot:off,back,wait:6,log");

// The rider sat on four styles in turn, one bay: the scratch bike above made a cafe racer, then
// remade as a chopper, a sport bike and an MX bike (a test shortcut), each ridden out, logged at
// the start and after 10 s of the robot riding (spec, groups, the rider's seat, pegs, grips and
// pose), a shot of it on the road and one side on from its right, and back.
#define TORQUE_RIDE_STYLE(S) TEXT("restyle:") TEXT(S) TEXT(",log,look:bike,shot,tool:hands,rideout,wait:6,ridelog,shot,robot:on,wait:10,ridelog,shot,") \
	TEXT("camera:3:90:450:60,wait:1,shot,camera:0,robot:off,back,wait:6,")
const TCHAR* const RidesSteps =
	TEXT("bay:3,order:steel_backbone,item:steel_backbone,f,item:steel_backbone,act,place:bay_2,act,finish:cafe,")
	TORQUE_RIDE_STYLE("cafe") TORQUE_RIDE_STYLE("chopper") TORQUE_RIDE_STYLE("sport") TORQUE_RIDE_STYLE("mx")
	TEXT("log");
#undef TORQUE_RIDE_STYLE

FVector Flat(const FVector& V) { return FVector(V.X, V.Y, 0.0); }

// Away from a point towards the middle of the shed: where to stand to look at it.
FVector TowardsMiddle(const FVector& At) { return Flat(FVector(0.0, 120.0, 0.0) - At).GetSafeNormal(); }
} // namespace TorqueShedTestImpl

namespace TSTI = TorqueShedTestImpl;

void UTorqueShedTest::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Spec;
	if (!FParse::Value(FCommandLine::Get(), TEXT("TorqueShedAction="), Spec, false)) return;
	// "loop", "fab", "dyno" and "booth" stand for their steps anywhere in the list; alone, they quit at the end.
	TArray<FString> Given;
	Spec.ParseIntoArray(Given, TEXT(","), true);
	for (const FString& G : Given)
	{
		const FString T = G.TrimStartAndEnd();
		TArray<FString> Preset;
		if (T == TEXT("loop")) FString(TSTI::LoopSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("fab")) FString(TSTI::FabSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("dyno")) FString(TSTI::DynoSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("booth")) FString(TSTI::BoothSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("spraygame")) FString(TSTI::SprayGameSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("mix")) FString(TSTI::MixSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("lift")) FString(TSTI::LiftSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("washgame")) FString(TSTI::WashGameSteps).ParseIntoArray(Preset, TEXT(","), true);   // [wash]
		else if (T == TEXT("menus")) FString(TSTI::MenuSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("geo")) FString(TSTI::GeoSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("geotwin")) FString(TSTI::GeoTwinSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("ride")) FString(TSTI::RideSteps).ParseIntoArray(Preset, TEXT(","), true);
		else if (T == TEXT("rides")) FString(TSTI::RidesSteps).ParseIntoArray(Preset, TEXT(","), true);
		else Preset.Add(T);
		Steps.Append(Preset);
	}
	if (Given.Num() == 1 && Steps.Num() > 1) Steps.Add(TEXT("quit"));
	Spec = FString::Join(Steps, TEXT(","));
	UE_LOG(LogTemp, Log, TEXT("Torque test: %d shed steps: %s"), Steps.Num(), *Spec);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTorqueShedTest::Step), 0.0f);
}

void UTorqueShedTest::Deinitialize()
{
	if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Super::Deinitialize();
}

ATorqueWalker* UTorqueShedTest::Walker() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ATorqueWalker>(PC->GetPawn()) : nullptr;
}

void UTorqueShedTest::LogBuild(const TCHAR* Where) const
{
	const UTorqueBuildSubsystem* B = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueBuildSubsystem>() : nullptr;
	if (!B)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s, no shed"), Where);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque test: %s, the shed:"), Where);
	for (int32 Bay = 0; Bay < B->NumBikes(); ++Bay)
	{
		if (!B->Exists(Bay))
		{
			UE_LOG(LogTemp, Log, TEXT("Torque test:   bay %d empty"), Bay + 1);
			continue;
		}
		const FTorqueShedBike& Bike = B->Bike(Bay);
		FString Text;
		for (const TPair<FName, int32>& KV : Bike.Slots)
		{
			if (KV.Value == 0) continue;
			const FTorqueItem* It = B->Item(KV.Value);
			Text += FString::Printf(TEXT(" %s=%s%s"), *KV.Key.ToString(), It ? *It->Part.ToString() : TEXT("?"), Bike.Bolted.FindRef(KV.Key) ? TEXT("") : TEXT("(loose)"));
		}
		const TArray<FName> Missing = B->MissingSlots(Bay);
		FString MissingText;
		for (const FName& S : Missing) MissingText += TEXT(" ") + S.ToString();
		UE_LOG(LogTemp, Log, TEXT("Torque test:   bay %d %s%s%s:%s%s"), Bay + 1, *Bike.Name, Bay == B->MyBike() ? TEXT(", mine") : TEXT(""),
			B->IsComplete(Bay) ? TEXT(", ready to ride") : TEXT(""), *Text, Missing.Num() ? *(TEXT("   missing:") + MissingText) : TEXT(""));
	}
	FString Loose;
	for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
	{
		const FTorqueItem& It = KV.Value;
		if (It.At == ETorqueAt::Bike || It.At == ETorqueAt::Engine || It.At == ETorqueAt::Wheel) continue;
		const FString At = It.At == ETorqueAt::Held ? FString(TEXT("held")) : (It.At == ETorqueAt::Spot ? It.Spot.ToString() : FString(TEXT("floor")));
		Loose += FString::Printf(TEXT(" %d:%s@%s%s(rust %.0f%% dirt %.0f%%%s)"), It.Id, *It.Part.ToString(), *At, It.bBoxed ? TEXT(",boxed") : TEXT(""),
			100.0f * It.Rust, 100.0f * It.Dirt, It.Finish.IsNone() ? TEXT("") : *FString::Printf(TEXT(" %s"), *It.Finish.ToString()));
		const FString Job = B->PaintJobText(It);   // the paint job, primer and coat, once it's been sprayed
		if (!Job.IsEmpty()) Loose += TEXT(" [") + Job + TEXT("]");
	}
	UE_LOG(LogTemp, Log, TEXT("Torque test:   loose:%s"), Loose.IsEmpty() ? TEXT(" none") : *Loose);
}

bool UTorqueShedTest::Step(float Dt)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->HasBegunPlay()) return true;
	if (SeenWorld.Get() != World)
	{
		// A new map: let it settle (the bikes and parts draw at BeginPlay, the camera exposes).
		SeenWorld = World;
		Wait = FMath::Max(Wait, 4.0);
		LogBuild(*FString::Printf(TEXT("in %s"), *World->GetMapName()));
	}
	Wait -= Dt;
	StepGame += Dt;
	++StepFrames;
	if (Wait > 0.0) return true;
	// The last step and the wait after it: real time against game time (the hub's run timed out
	// once with every step fine: this says where the time went).
	const double RealNow = FPlatformTime::Seconds();
	if (RunRealAt == 0.0) RunRealAt = RealNow;
	if (!StepWas.IsEmpty())
	{
		const double Real = RealNow - StepRealAt;
		if (Real > StepGame + 3.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque test: slow step: '%s' took %.1f s real, %.1f s game, %d frames (%.0f fps)"), *StepWas, Real, StepGame, StepFrames,
				StepFrames / FMath::Max(Real, 0.001));
		}
		SlowSteps.Add(TPair<double, FString>(Real, StepWas));
	}
	StepRealAt = RealNow;
	StepGame = 0.0;
	StepFrames = 0;
	StepWas.Reset();
	// The run's time and its slowest steps: at the end, or before a "quit" ends it (a preset run alone).
	const bool bEnd = Next >= Steps.Num();
	if (bEnd || Steps[Next].TrimStartAndEnd() == TEXT("quit"))
	{
		SlowSteps.Sort([](const TPair<double, FString>& X, const TPair<double, FString>& Y) { return X.Key > Y.Key; });
		FString Slowest;
		for (int32 I = 0; I < SlowSteps.Num() && I < 5; ++I)
			Slowest += FString::Printf(TEXT("%s'%s' %.1f s"), I == 0 ? TEXT("") : TEXT(", "), *SlowSteps[I].Value, SlowSteps[I].Key);
		UE_LOG(LogTemp, Log, TEXT("Torque test: all shed steps done in %.0f s real; slowest: %s"), RealNow - RunRealAt, *Slowest);
	}
	if (bEnd) return false;   // stop ticking
	const FString S = Steps[Next].TrimStartAndEnd();
	StepWas = S;
	Wait = 2.0;
	if (RunStep(S)) ++Next;
	else Wait = 1.0;   // not ready (no walker yet): try again in a second, but not for ever
	return true;
}

bool UTorqueShedTest::RunStep(const FString& S)
{
	TArray<FString> P;
	S.ParseIntoArray(P, TEXT(":"), true);
	if (P.Num() == 0) return true;
	UWorld* World = GetGameInstance()->GetWorld();
	const FString& Verb = P[0];
	// What's in hand is hidden from look: until the next step that isn't a shot or a wait, so a
	// stand-back shot shows the bike, not the part held up in front of the camera.
	if (Verb != TEXT("shot") && Verb != TEXT("wait"))
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) It->bHideHeld = Verb == TEXT("look");
	auto Said = [&S](const FString& What) { UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s"), *S, *What); };
	auto Warn = [&S](const FString& What) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: %s"), *S, *What); };

	if (Verb == TEXT("wait")) { Wait = P.Num() > 1 ? FCString::Atod(*P[1]) : 2.0; Said(FString::Printf(TEXT("%.1f s"), Wait)); return true; }
	if (Verb == TEXT("quit"))
	{
		Said(TEXT("quitting"));
		if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
		return true;
	}
	if (Verb == TEXT("set") || Verb == TEXT("key"))
	{
		// [settings] "set:FIELD:VALUE" (set:fov:100, set:timeofday:19.5, set:telemetry:on, set:units:metric)
		// changes a setting for the run; "key:F3" is the telemetry key (keys don't reach a test run).
		ATorquePlayerController* PC = Cast<ATorquePlayerController>(World->GetFirstPlayerController());
		if (!PC) { Warn(TEXT("no player controller")); return true; }
		if (Verb == TEXT("key"))
		{
			if (P.Num() > 1 && P[1].Equals(TEXT("F3"), ESearchCase::IgnoreCase))
				Said(PC->TestSet(TEXT("telemetry"), FTorqueSettings::Get().bTelemetry ? TEXT("off") : TEXT("on")));
			else Warn(TEXT("key what? key:F3"));
		}
		else if (P.Num() >= 3) Said(PC->TestSet(P[1], P[2].ToLower()));
		else Warn(TEXT("set what? set:fov:100, set:timeofday:19.5, set:telemetry:on"));
		return true;
	}
	if (Verb == TEXT("menu") || Verb == TEXT("face"))
	{
		// The Esc menu's pages for a screenshot (key presses don't reach the game in a test run):
		// "menu:pause", "menu:profile", "menu:settings", "menu:close"; "face:SLIDER:V" sets a face
		// slider on your profile (its name's first word: jaw, chin, nose, ...; or height, avatar).
		ATorquePlayerController* PC = Cast<ATorquePlayerController>(World->GetFirstPlayerController());
		if (!PC) { Warn(TEXT("no player controller")); return true; }
		if (Verb == TEXT("menu")) Said(PC->TestMenu(P.Num() > 2 ? (P[1] + TEXT(":") + P[2]).ToLower() : P.Num() > 1 ? P[1].ToLower() : FString(TEXT("pause"))));
		else if (P.Num() >= 3) Said(PC->TestFace(P[1].ToLower(), FCString::Atof(*P[2])));
		else Warn(TEXT("face what? face:jaw:0.8, face:height:-0.5, face:avatar:2"));
		return true;
	}
	if (Verb == TEXT("shot"))
	{
		// "shot:ui": the viewport as the player sees it, HUD and menus included (HighResShot
		// leaves them out).
		if (P.Num() > 1 && P[1] == TEXT("ui"))
		{
			Said(TEXT("screenshot with the HUD (Saved/Screenshots)"));
			FScreenshotRequest::RequestScreenshot(FString(), true, true);
		}
		else
		{
			Said(TEXT("screenshot (Saved/Screenshots)"));
			if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("HighResShot 1"));
		}
		return true;
	}
	if (Verb == TEXT("log")) { LogBuild(*S); return true; }
	if (Verb == TEXT("bay") && P.Num() >= 2)
	{
		TestBay = FMath::Clamp(FCString::Atoi(*P[1]) - 1, 0, UTorqueBuildSubsystem::Bays - 1);
		Said(FString::Printf(TEXT("working on bay %d"), TestBay + 1));
		return true;
	}
	if (Verb == TEXT("back"))
	{
		const bool bOk = TorqueFlow::BackToShed(World);
		Said(bOk ? TEXT("going back to the shed") : TEXT("refused (no shed map, or a client)"));
		return true;
	}

	// On the road: our own bike's spec and what it's doing ("ridelog"), and the robot rider on or off.
	if (Verb == TEXT("ridelog") || Verb == TEXT("robot") || Verb == TEXT("camera") || Verb == TEXT("lever") || Verb == TEXT("brakestop") || Verb == TEXT("lean"))
	{
		ATorqueBikePawn* Bike = nullptr;
		for (TActorIterator<ATorqueBikePawn> It(World); It; ++It)
			if (It->IsLocallyControlled()) { Bike = *It; break; }
		if (!Bike)
		{
			if (++Tries < 20) return false;
			Warn(TEXT("skipped: no bike of ours (not on the road?)"));
			Tries = 0;
			return true;
		}
		Tries = 0;
		if (Verb == TEXT("lever"))
		{
			// lever:clutch:1 / lever:brake:0.5 hold it (fingers over, pulled that far); lever:off lets both go.
			const FString Which = P.Num() > 1 ? P[1].ToLower() : FString(TEXT("off"));
			if (Which == TEXT("off"))
			{
				Bike->TestLever(0, -1.0);
				Bike->TestLever(1, -1.0);
				Said(TEXT("levers let go"));
				return true;
			}
			const int32 LeverSide = Which == TEXT("clutch") ? 0 : 1;
			const double Pull = P.Num() > 2 ? FCString::Atod(*P[2]) : 1.0;
			Bike->TestLever(LeverSide, Pull);
			Said(FString::Printf(TEXT("%s held, pulled %.2f"), LeverSide == 0 ? TEXT("clutch") : TEXT("brake"), Pull));
			return true;
		}
		// brakestop[:MPH][:flat] (default 60): the robot holds MPH on a straight, then stops as hard as a
		// road test does (on the road, or :flat on the level test pad); lean:max[:MPH] (default 45): a
		// full-lean bend held 6 s at MPH on the test pad. Each waits until
		// it's done (up to about two minutes) and logs what it measured.
		if (Verb == TEXT("brakestop") || Verb == TEXT("lean"))
		{
			const bool bBrake = Verb == TEXT("brakestop");
			const int32 MphAt = bBrake ? 1 : 2;
			const FString MphArg = P.Num() > MphAt && P[MphAt].ToLower() != TEXT("flat") ? P[MphAt] : FString();
			const double Mph = MphArg.IsEmpty() ? (bBrake ? 60.0 : 45.0) : FCString::Atod(*MphArg);
			const bool bFlat = P.Last().ToLower() == TEXT("flat");
			if (!bMoveStarted)
			{
				Bike->TestStartMove(bBrake ? 0 : 1, Mph, bFlat);
				bMoveStarted = true;
				Said(FString::Printf(TEXT("getting up to %.0f mph on a straight"), Mph));
				Tries = 0;
				return false;
			}
			if (Bike->TestMoveBusy())
			{
				Tries = 0;   // (it takes as long as it takes; the bike gives up by itself)
				return false;
			}
			bMoveStarted = false;
			Said(Bike->TestMoveResult());
			return true;
		}
		if (Verb == TEXT("camera"))
		{
			const int32 Mode = P.Num() > 1 ? FCString::Atoi(*P[1]) : 0;
			const double Yaw = P.Num() > 2 ? FCString::Atod(*P[2]) : 40.0;
			const double Dist = P.Num() > 3 ? FCString::Atod(*P[3]) : 0.0, Fov = P.Num() > 4 ? FCString::Atod(*P[4]) : 0.0;
			const double Rise = P.Num() > 5 ? FCString::Atod(*P[5]) : 0.0;
			const FString AtName = P.Num() > 6 ? P[6].ToLower() : FString(TEXT("bike"));
			const int32 AtIndex = AtName == TEXT("lhand") ? 1 : AtName == TEXT("rhand") ? 2 : 0;
			Bike->TestCamera(Mode, Yaw, Dist, Fov, Rise, AtIndex);
			Said(FString::Printf(TEXT("camera %d, %.0f deg round, %.0f cm out, %.0f deg view, %.0f cm up, at %s"), Mode, Yaw, Dist, Fov, Rise, *AtName));
			return true;
		}
				if (Verb == TEXT("robot"))
		{
			const bool bOn = P.Num() < 2 || P[1] != TEXT("off");
			Bike->TestAutoRide(bOn);
			Said(bOn ? TEXT("the robot rides") : TEXT("the robot's off"));
			return true;
		}
		TArray<FString> Lines;
		Bike->TestRideLog().ParseIntoArray(Lines, TEXT("\n"), true);
		for (const FString& L : Lines) Said(L);
		return true;
	}

	// The rest need the walker in the shed.
	ATorqueWalker* W = Walker();
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World);
	if (!W || !B)
	{
		if (Tries == 0) Warn(TEXT("waiting for the walker (up to 20 s)"));
		if (++Tries < 20) return false;
		Warn(TEXT("skipped: no walker (not in the shed?)"));
		Tries = 0;
		return true;
	}
	Tries = 0;
	if (Verb == TEXT("item") || Verb == TEXT("place") || Verb == TEXT("floor") || Verb == TEXT("door") || Verb == TEXT("look")) bAimLost = false;

	if (Verb == TEXT("tool") && P.Num() >= 2)
	{
		static const TCHAR* const Names[] = {TEXT("hands"), TEXT("wrench"), TEXT("brush"), TEXT("repair"), TEXT("spray"), TEXT("lance"), TEXT("gun")};
		int32 Index = INDEX_NONE;
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Names); ++I)
			if (P[1].Equals(Names[I], ESearchCase::IgnoreCase)) Index = I;
		if (Index == INDEX_NONE) { Warn(TEXT("no such tool (hands, wrench, brush, repair, spray, lance, gun)")); return true; }
		W->TestTool((ETorqueTool)Index);
		Said(TEXT("in hand"));
		return true;
	}
	if (Verb == TEXT("crouch"))
	{
		const bool bOn = P.Num() < 2 || P[1] != TEXT("0");
		W->TestCrouch(bOn);
		Said(bOn ? TEXT("crouching") : TEXT("standing"));
		return true;
	}
	if (Verb == TEXT("spray") && P.Num() >= 2)
	{
		int32 Colour = FCString::Atoi(*P[1]);
		for (int32 I = 0; I < UTorqueBuildSubsystem::Paints().Num(); ++I)
			if (P[1].Equals(UTorqueBuildSubsystem::PaintName(I), ESearchCase::IgnoreCase)) Colour = I;
		const FName Finish = P.Num() > 2 ? FName(*P[2].ToLower()) : FName(TEXT("gloss"));
		W->TestSpray(Colour, Finish);
		Said(FString::Printf(TEXT("loaded: %s, colour %s"), *W->SprayFinishText(), *UTorqueBuildSubsystem::PaintName(Colour)));
		return true;
	}
	if (Verb == TEXT("spraypass"))
	{
		// spraypass:PASSES:CM: steady passes of the spray game's controls over the item looked at,
		// from CM off; the coverage and its faults after.
		const int32 Passes = P.Num() > 1 ? FMath::Max(FCString::Atoi(*P[1]), 1) : 5;
		const double Distance = P.Num() > 2 ? FMath::Clamp(FCString::Atod(*P[2]), 10.0, 300.0) : 80.0;
		Said(W->TestSprayPasses(Passes, Distance));
		LogBuild(TEXT("after it"));
		return true;
	}
	if (Verb == TEXT("washpass"))   // [wash] washpass:PASSES:CM sweeps the lance over the whole bike
	{
		Said(W->TestWashPasses(P.Num() > 1 ? FCString::Atoi(*P[1]) : 1, P.Num() > 2 ? FCString::Atod(*P[2]) : 60.0));
		return true;
	}
	if (Verb == TEXT("washstate"))   // [wash] how the wash game on the wash bay's bike is going
	{
		const FString State = W->TestWashState();
		Said(State.IsEmpty() ? FString(TEXT("no wash going")) : State);
		return true;
	}
	if (Verb == TEXT("sprayscan"))
	{
		const bool bOn = P.Num() < 2 || P[1] != TEXT("0");
		W->TestSprayScan(bOn);
		Said(bOn ? TEXT("scan on") : TEXT("scan off"));
		return true;
	}
	if (Verb == TEXT("part") && P.Num() >= 2)
	{
		// The biggest mesh of that slot on the bay's bike, looked at from beside the bike.
		const FName Slot(*P[1]);
		const UStaticMeshComponent* Best = nullptr;
		const ATorqueShedBike* Bike = nullptr;
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		{
			if (It->Bay != TestBay) continue;
			Bike = *It;
			double BestVol = -1.0;
			for (const UStaticMeshComponent* C : It->Parts)
			{
				if (!C || !C->IsVisible() || TorqueShedModel::SlotTag(C) != Slot) continue;
				const double Vol = C->Bounds.GetBox().GetVolume();
				if (Vol > BestVol) { BestVol = Vol; Best = C; }
			}
		}
		if (!Best) { Warn(FString::Printf(TEXT("bay %d's bike has nothing drawn in that slot"), TestBay + 1)); return true; }
		// Each of the slot's meshes (the biggest first), from beside the bike on its side first, then
		// round it, aiming at the mesh's own surface on the side looked from, until the walker's look
		// lands on that slot.
		TArray<const UStaticMeshComponent*> Meshes;
		for (const UStaticMeshComponent* C : Bike->Parts)
			if (C && C->IsVisible() && TorqueShedModel::SlotTag(C) == Slot) Meshes.Add(C);
		Meshes.Sort([](const UStaticMeshComponent& X, const UStaticMeshComponent& Y) { return X.Bounds.GetBox().GetVolume() > Y.Bounds.GetBox().GetVolume(); });
		FString Seen, Candidates;
		const double Distances[] = {90.0, 130.0, 60.0};
		for (const UStaticMeshComponent* C : Meshes)
		{
			FVector Out = TSTI::Flat(C->Bounds.Origin - Bike->GetActorLocation()).GetSafeNormal();
			if (Out.IsNearlyZero()) Out = FVector(1.0, 0.0, 0.0);
			const FVector Sides[] = {Out, FVector(1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), (Out + FVector(0.0, 1.0, 0.0)).GetSafeNormal(),
				(Out - FVector(0.0, 1.0, 0.0)).GetSafeNormal(), FVector(0.0, 1.0, 0.0), FVector(0.0, -1.0, 0.0)};
			for (const double D : Distances)
				for (const FVector& Away : Sides)
				{
					if (Away.IsNearlyZero()) continue;
					FVector At = C->Bounds.Origin;
					TorqueShedPick::SurfacePoint(C, Away, At);
					Seen = W->TestLookAt(At, Away, D);
					if (W->TestTargetSlot() == Slot)
					{
						bAimLost = false;
						Said(TEXT("looking at ") + Seen);
						return true;
					}
					if (Candidates.IsEmpty()) Candidates = FString::Printf(TEXT("from %.0f cm along (%.1f, %.1f) at %s: %s"), D, Away.X, Away.Y,
						*C->GetStaticMesh()->GetName(), *W->TestCandidates());
				}
		}
		bAimLost = true;
		Warn(TEXT("couldn't get a look at it: last saw ") + Seen + TEXT(". First try went into: ") + Candidates);
		return true;
	}
	if (Verb == TEXT("item") && P.Num() >= 2)
	{
		// The newest part off the bikes matching a part id, a kind or a slot.
		const FName What(*P[1]);
		const FName Kind = TorqueShedParts::KindOf(What);
		int32 Found = 0;
		for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
		{
			const FTorqueItem& It = KV.Value;
			if (It.At != ETorqueAt::Spot && It.At != ETorqueAt::Floor) continue;
			if (It.Part == What || It.Kind == What || It.Kind == Kind || It.Part == TorqueShedParts::StockPart(What)) Found = FMath::Max(Found, It.Id);
		}
		FVector At;
		bool bDrawn = false;
		TArray<const UStaticMeshComponent*> Meshes;
		for (TActorIterator<ATorqueShedItems> It(World); It && !bDrawn; ++It)
		{
			bDrawn = It->WhereIs(Found, At);
			if (bDrawn) It->MeshesOf(Found, Meshes);
		}
		if (Found == 0 || !bDrawn) { Warn(Found == 0 ? TEXT("no such part off the bikes") : TEXT("it isn't drawn")); return true; }
		// From the middle of the shed first, then round it, aiming at the part's own surface (a
		// frame's box is mostly air), until the walker's look lands on the item.
		Meshes.Sort([](const UStaticMeshComponent& X, const UStaticMeshComponent& Y) { return X.Bounds.GetBox().GetVolume() > Y.Bounds.GetBox().GetVolume(); });
		const FVector Middle = TSTI::TowardsMiddle(At);
		const FVector Sides[] = {Middle, (Middle + FVector(0.0, 1.0, 0.0)).GetSafeNormal(), (Middle - FVector(0.0, 1.0, 0.0)).GetSafeNormal(),
			FVector(1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, -1.0, 0.0)};
		const double Distances[] = {90.0, 130.0, 60.0};
		FString Seen, Candidates;
		for (const double D : Distances)
			for (const FVector& Away : Sides)
			{
				if (Away.IsNearlyZero()) continue;
				FVector Aim = At;
				for (const UStaticMeshComponent* C : Meshes)
					if (TorqueShedPick::SurfacePoint(C, Away, Aim)) break;
				Seen = W->TestLookAt(Aim, Away, D);
				if (W->TestTargetItem() == Found)
				{
					bAimLost = false;
					Said(TEXT("looking at ") + Seen);
					return true;
				}
				if (Candidates.IsEmpty()) Candidates = FString::Printf(TEXT("from %.0f cm along (%.1f, %.1f): %s"), D, Away.X, Away.Y, *W->TestCandidates());
			}
		bAimLost = true;
		Warn(FString::Printf(TEXT("couldn't get a look at item %d: last saw "), Found) + Seen + TEXT(". First try went into: ") + Candidates);
		return true;
	}
	if (Verb == TEXT("finish"))
	{
		// A test shortcut, not a player's move: the rest of the bike from its style's reference parts.
		// finish[:<style>][:<slot>=<part>|none]...
		FString Style;
		TMap<FString, FString> Set;
		for (int32 I = 1; I < P.Num(); ++I)
		{
			FString Slot, Part;
			if (P[I].Split(TEXT("="), &Slot, &Part)) Set.Add(Slot, Part);
			else Style = P[I];
		}
		Said(TEXT("finished from the catalogue (test shortcut): ") + B->TestFinish(TestBay, Style, Set));
		return true;
	}
	if (Verb == TEXT("restyle") && P.Num() >= 2)
	{
		Said(TEXT("remade from the catalogue (test shortcut): ") + B->TestRestyle(TestBay, P[1]));
		return true;
	}
		if (Verb == TEXT("fabricate"))
	{
		// Each job the bike owes, made with the repair tool on its frame (the grinder and welder).
		const int32 Jobs = B->FabOwed(TestBay).Num();
		TArray<FString> More;
		for (int32 J = 0; J < Jobs; ++J)
		{
			More.Add(TEXT("part:frame"));
			More.Add(TEXT("act"));
		}
		Steps.Insert(More, Next + 1);   // after this step
		Said(FString::Printf(TEXT("%d jobs owed: %s"), Jobs, *B->FabOwedText(TestBay)));
		return true;
	}
	if (Verb == TEXT("place") && P.Num() >= 2)
	{
		const FTorquePlace* Place = TorqueShedLayout::FindPlace(FName(*P[1]));
		if (!Place) { Warn(TEXT("no such place")); return true; }
		const FVector At = Place->Kind == ETorquePlace::Bay ? Place->Put + FVector(110.0, 40.0, 0.0) : Place->Box.GetCenter() + FVector(0.0, 0.0, Place->Box.GetExtent().Z);
		Said(TEXT("looking at ") + W->TestLookAt(At, TSTI::TowardsMiddle(At), Place->Kind == ETorquePlace::Bay ? 120.0 : 90.0));
		return true;
	}
	// Carrying a part (the wheel and R): carry:reach:<cm>, carry:wheel:<+-clicks>, carry:turn:<deg>,
	// carry:turntap:<quarters>, carry:aim:<place> or carry:aim:<x>:<y> (stand 2.2 m back facing it,
	// so the wheel pushes the drop box towards it), carry:show (say where the box is). Each says
	// the plan, the drop and the box (blue, red or hidden).
	if (Verb == TEXT("carry") && P.Num() >= 2)
	{
		if (P[1] == TEXT("aim") && P.Num() >= 3)
		{
			FVector At;
			if (P.Num() >= 4) At = FVector(FCString::Atod(*P[2]), FCString::Atod(*P[3]), 0.0);
			else
			{
				const FTorquePlace* Place = TorqueShedLayout::FindPlace(FName(*P[2]));
				if (!Place) { Warn(TEXT("no such place")); return true; }
				At = Place->Box.GetCenter();
				At.Z = 0.0;
			}
			Said(TEXT("facing ") + W->TestLookAt(At + FVector(0.0, 0.0, 60.0), TSTI::TowardsMiddle(At), 220.0));
			bAimLost = false;
			Said(W->TestCarry(TEXT("show"), 0.0));
			return true;
		}
		const double Value = P.Num() >= 3 ? FCString::Atod(*P[2]) : 0.0;
		const FString Out = W->TestCarry(P[1].ToLower(), Value);
		if (Out.StartsWith(TEXT("not "))) Warn(Out);
		else Said(Out);
		return true;
	}
	if (Verb == TEXT("floor") && P.Num() >= 3)
	{
		const FVector At(FCString::Atod(*P[1]), FCString::Atod(*P[2]), 0.0);
		Said(TEXT("looking at ") + W->TestLookAt(At, TSTI::TowardsMiddle(At), 100.0));
		return true;
	}
	if (Verb == TEXT("door") || Verb == TEXT("rideout"))
	{
		AActor* Door = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
			if (It->ActorHasTag(TEXT("RideOut"))) { Door = *It; break; }
		if (!Door) { Warn(TEXT("no RideOut point in this map")); return true; }
		// In the opening, looking out.
		const FVector Out = Door->GetActorLocation() + FVector(0.0, 300.0, 120.0);
		Said(TEXT("looking at ") + W->TestLookAt(Out, FVector(0.0, -1.0, 0.0), 300.0));
		if (Verb == TEXT("rideout")) Said(W->TestInteract());
		return true;
	}
	if (Verb == TEXT("look") && P.Num() >= 2)
	{
		// Stand back from something and look at it, for a screenshot.
		const FString What = P[1].ToLower();
		if (What == TEXT("player"))
		{
			// Co-op: stand back about 3 m from the nearest other player and face them, so a screenshot
			// takes in all of them and the name tag over their head.
			const APawn* Nearest = nullptr;
			double Best = 1e30;
			for (TActorIterator<APawn> It(W->GetWorld()); It; ++It)
			{
				if (*It == W || !It->GetPlayerState()) continue;
				const double D = FVector::DistSquared(It->GetActorLocation(), W->GetActorLocation());
				if (D < Best) { Best = D; Nearest = *It; }
			}
			AController* C = W->GetController();
			if (!Nearest || !C) { Warn(TEXT("look:player: no other player here")); return true; }
			const FVector Them = Nearest->GetActorLocation();
			// From where we are now, or round to the first clear side (a wall or a bench in the way).
			const FVector Back = FVector(W->GetActorLocation() - Them).GetSafeNormal2D();
			FVector Away = Back.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : Back;
			FCollisionQueryParams Ignore(TEXT("TorqueLookPlayer"), false, W);
			Ignore.AddIgnoredActor(Nearest);
			for (int32 Turn = 0; Turn < 8; ++Turn)
			{
				const FVector Try = Away.RotateAngleAxis(45.0 * ((Turn + 1) / 2) * (Turn % 2 ? 1.0 : -1.0), FVector::UpVector);
				if (!W->GetWorld()->LineTraceTestByChannel(Them, Them + Try * 340.0, ECC_Visibility, Ignore)) { Away = Try; break; }
			}
			Said(FString::Printf(TEXT("facing %s from 3 m: %s"), *Nearest->GetPlayerState()->GetPlayerName(),
				*W->TestLookAt(Them + FVector(0.0, 0.0, 50.0), Away, 300.0)));
			return true;
		}
		FVector At = FVector::ZeroVector, Away = FVector::ZeroVector;
		double Distance = 220.0;
		FName PlaceName = NAME_None;
		if (What == TEXT("bench")) PlaceName = TEXT("bench_3");
		else if (What == TEXT("washer")) PlaceName = TEXT("washer_0");
		else if (What == TEXT("repair")) PlaceName = TEXT("repair_1");
		else if (What == TEXT("bins")) PlaceName = TEXT("plastic");
		else if (What == TEXT("hook") && P.Num() >= 3)
		{
			// look:hook:N: the part hung on spray booth hook N (1 .. 10), wherever it's wound to.
			const int32 Hook = FCString::Atoi(*P[2]) - 1;
			const FVector Pivot = ATorqueBoothHooks::PivotOf(Hook);
			At = FVector(Pivot.X, Pivot.Y, FMath::Max(B ? B->HookTipOf(Hook) - 25.0 : 120.0, 40.0));
			Away = TSTI::TowardsMiddle(At);
			Distance = 170.0;
			Said(FString::Printf(TEXT("hook %d's tip at %.0f cm"), Hook + 1, B ? B->HookTipOf(Hook) : 0.0f));
		}
		else if (TorqueShedLayout::FindPlace(FName(*What))) PlaceName = FName(*What);
		if (!PlaceName.IsNone())
		{
			const FTorquePlace* Place = TorqueShedLayout::FindPlace(PlaceName);
			At = Place->Box.GetCenter() + FVector(0.0, 0.0, Place->Box.GetExtent().Z);
			Away = TSTI::TowardsMiddle(At);
		}
		else if (What == TEXT("bike"))
		{
			At = TorqueShedLayout::BayAt(TestBay) + FVector(0.0, 0.0, 70.0);
			Away = FVector(0.6, 1.0, 0.0).GetSafeNormal();
			Distance = 300.0;
		}
		else if (What == TEXT("paintbay")) { At = FVector(1565.0, 0.0, 120.0); Away = FVector(-0.35, -1.0, 0.0).GetSafeNormal(); Distance = 420.0; }
		// The scale and its read-out on the bench's front edge, from where you'd stand at the bench.
		else if (What == TEXT("mixbench")) { At = FVector(1645.0, -40.0, 90.0); Away = FVector(-1.0, -0.15, 0.0).GetSafeNormal(); Distance = 95.0; }
		// The swatch wall's first cards (the newest top left, SwatchAt), from across the room.
		else if (What == TEXT("swatches")) { At = FVector(1433.0, -250.0, 195.0); Away = FVector(1.0, 0.25, 0.0).GetSafeNormal(); Distance = 240.0; }
		else if (What == TEXT("teststand")) { At = TorquePaintBayLayout::TestCardAt; Away = FVector(0.6, 0.6, 0.0).GetSafeNormal(); Distance = 110.0; }
		else if (What == TEXT("bikenow"))
		{
			// The bike wherever it is now (mid-floor, the rig), from its front quarter.
			const UTorqueBuildSubsystem* NowShed = W->GetWorld()->GetGameInstance()->GetSubsystem<UTorqueBuildSubsystem>();
			At = (NowShed ? NowShed->BikeTransform(TestBay).GetLocation() : TorqueShedLayout::BayAt(TestBay)) + FVector(0.0, 0.0, 70.0);
			Away = FVector(0.6, 1.0, 0.0).GetSafeNormal();
			Distance = 300.0;
		}
		else if (What == TEXT("screen"))
		{
			// The bay's screen on the back wall, whole (ATorqueShedScreens), from the middle of the shed.
			const double ScreenX[4] = {-504.0, -232.0, 232.0, 504.0};
			At = FVector(ScreenX[FMath::Clamp(TestBay, 0, 3)], -543.0, 245.0);
			Away = FVector(0.0, 1.0, 0.0);
			Distance = 260.0;
		}
		else if (What == TEXT("board")) { At = FVector(-643.0, 60.0, 255.0); Away = FVector(1.0, 0.0, 0.0); Distance = 300.0; }
		else if (What == TEXT("monitor")) { At = FVector(613.5, 190.0, 110.0); Away = FVector(-1.0, 0.0, 0.0); Distance = 110.0; }
		else if (What == TEXT("booth")) { At = FVector(1160.0, 0.0, 110.0); Away = FVector(-1.0, 0.25, 0.0).GetSafeNormal(); Distance = 430.0; }
		// The hall from the back wall (between bays 2 and 3) to the open roller door: the daylight in
		// the doorway and the dust in it.
		else if (What == TEXT("hall")) { At = FVector(0.0, 520.0, 150.0); Away = FVector(0.0, -1.0, 0.0); Distance = 1000.0; }
		// The shed's back west corner (behind bay 1), from the middle of the floor: how dark it goes.
		else if (What == TEXT("corner")) { At = FVector(-620.0, -520.0, 200.0); Away = FVector(1.0, 1.0, 0.0).GetSafeNormal(); Distance = 600.0; }
		// The hex LED ceiling over the spray bay, looking up from inside the paint shop's door.
		else if (What == TEXT("boothceiling")) { At = FVector(1080.0, -40.0, 335.0); Away = FVector(-1.0, 0.35, 0.0).GetSafeNormal(); Distance = 330.0; }
		else if (What == TEXT("dyno")) { At = FVector(0.0, -1290.0, 225.0); Away = FVector(0.35, 1.0, 0.0).GetSafeNormal(); Distance = 250.0; }
		else if (What == TEXT("dynoroom")) { At = FVector(0.0, -980.0, 80.0); Away = FVector(-0.45, 1.0, 0.0).GetSafeNormal(); Distance = 360.0; }
		else if (What == TEXT("deliveries")) { At = TorqueShedLayout::DeliveryAt(0) + FVector(110.0, 50.0, 30.0); Away = FVector(-0.5, -1.0, 0.0).GetSafeNormal(); Distance = 350.0; }
		else if (What == TEXT("washbay")) { At = FVector(-930.0, 150.0, 60.0); Away = FVector(1.0, 0.4, 0.0).GetSafeNormal(); Distance = 300.0; }
		else { Warn(TEXT("look at what? player, bench, washer, repair, computer, changer, stand, bins, bike, screen, board, monitor, dyno, dynoroom, booth, boothceiling, hall, corner, deliveries, washbay, or a place name")); return true; }
		// A view for a shot: what the crosshair lands on doesn't matter (a screen or the bike's middle).
		const FString Seen = W->TestLookAt(At, Away, Distance);
		Said(Seen.Contains(TEXT("nothing")) ? FString::Printf(TEXT("framing the %s"), *What) : TEXT("looking at ") + Seen);
		return true;
	}
	if (Verb == TEXT("act"))
	{
		if (bAimLost)
		{
			Warn(TEXT("skipped: the look before it missed"));
			bAimLost = false;
			return true;
		}
		Said(W->TestAct(P.Num() > 1 && P[1] == TEXT("alt")));
		LogBuild(TEXT("after it"));
		return true;
	}
	if (Verb == TEXT("hold") && P.Num() >= 2)
	{
		const double Seconds = FMath::Max(FCString::Atod(*P[1]), 0.1);
		W->TestHoldAct(Seconds);
		Wait = Seconds + 1.0;
		Said(FString::Printf(TEXT("holding the work button on %s for %.1f s"), *W->Describe(), Seconds));
		return true;
	}
	if (Verb == TEXT("f"))
	{
		Said(W->TestInteract());
		return true;
	}
	if (Verb == TEXT("order") && P.Num() >= 2)
	{
		Said(W->TestOrder(FName(*P[1])));
		LogBuild(TEXT("after the order"));
		return true;
	}
	if (Verb == TEXT("computer"))
	{
		// The parts computer's page: "computer" opens it, "computer:KIND[:PART[:order]]" picks a kind
		// (bikes, paint:bases, paint:tints or a part kind) and a model and orders it, "computer:close".
		if (P.Num() >= 2 && P[1] == TEXT("close"))
		{
			Said(W->TestComputer(NAME_None, NAME_None, false) + TEXT(" (closing)"));
			W->TestCloseComputer();
			return true;
		}
		// (paint:bases has a colon in it: put it back together)
		FString KindText = P.Num() >= 2 ? P[1] : FString();
		int32 Next2 = 2;
		if (KindText == TEXT("paint") && P.Num() >= 3) { KindText += TEXT(":") + P[2]; Next2 = 3; }
		const FName KindName = KindText.IsEmpty() ? NAME_None : FName(*KindText);
		const FName PartName = P.Num() > Next2 ? FName(*P[Next2]) : NAME_None;
		const bool bOrder = P.Num() > Next2 + 1 && P[Next2 + 1] == TEXT("order");
		Said(W->TestComputer(KindName, PartName, bOrder));
		if (bOrder) LogBuild(TEXT("after the order"));
		return true;
	}
	if (Verb == TEXT("newbike"))
	{
		// The parts computer's New bike aisle: "newbike[:STYLE[:SLOT=PART]...][:order]" configures a
		// style's bike (cafe to start), puts those parts on, colours it and orders it built
		// (logo=ID, brand=NAME and font=ID put a badge on its tank).
		const FString Style = P.Num() >= 2 && !P[1].Contains(TEXT("=")) && P[1] != TEXT("order") ? P[1] : FString();
		TArray<FString> Swaps;
		bool bOrder = false;
		for (int32 I = 1; I < P.Num(); ++I)
		{
			if (P[I] == TEXT("order")) bOrder = true;
			else if (P[I].Contains(TEXT("="))) Swaps.Add(P[I]);
		}
		Said(W->TestNewBike(Style, Swaps, bOrder));
		if (bOrder) LogBuild(TEXT("after the order"));
		return true;
	}
	if (Verb == TEXT("wheel") && P.Num() >= 2)
	{
		int32 Where = 0;
		if (P[1] == TEXT("rig")) Where = 1;
		else if (P[1] == TEXT("floor")) Where = 2;
		else if (P[1] == TEXT("wash")) Where = 3;
		Said(W->TestWheel(TestBay, Where));
		LogBuild(TEXT("after it"));
		return true;
	}
	if (Verb == TEXT("bench") && P.Num() >= 2)
	{
		// Step up to the mixing bench (its view) or back from it.
		Said(W->TestBench(P[1] == TEXT("on")));
		return true;
	}
	if (Verb == TEXT("paintlog"))
	{
		Said(B->PaintBay().Summary());
		return true;
	}
	if (Verb == TEXT("paint") && P.Num() >= 2)
	{
		// The mixing bay's requests, as the bench's rows and the tins' keys make them.
		const FString& What = P[1];
		const FTorquePaintBay& Bay = B->PaintBay();
		FTorqueShedRequest R;
		if (What == TEXT("pour") && P.Num() >= 4)
		{
			Said(W->TestPour(P[2], FCString::Atof(*P[3])));
			return true;
		}
		if (What == TEXT("stir"))
		{
			// An even mix: stirred in the bench's flushes until it's done (or a turn's worth, given less).
			float Left = P.Num() > 2 ? FCString::Atof(*P[2]) : 1.0f;
			FString Last;
			while (Left > 0.0f)
			{
				R.Kind = ETorqueShedAct::PaintStir;
				R.Amount = FMath::Min(Left, 0.25f);
				Last = W->TestPaint(R);
				Left -= 0.25f;
			}
			Said(Last);
			return true;
		}
		if (What == TEXT("newtin") && P.Num() >= 3) { R.Kind = ETorqueShedAct::PaintTinNew; R.Colour = TorquePaintMix::BaseIndex(P[2]); }
		else if (What == TEXT("tare")) R.Kind = ETorqueShedAct::PaintTare;
		else if (What == TEXT("addbase")) R.Kind = ETorqueShedAct::PaintAddBase;
		else if (What == TEXT("test")) R.Kind = ETorqueShedAct::PaintTest;
		else if (What == TEXT("shake")) R.Kind = ETorqueShedAct::PaintShake;
		else if (What == TEXT("empty")) R.Kind = ETorqueShedAct::PaintTinEmpty;
		else if (What == TEXT("save"))
		{
			R.Kind = ETorqueShedAct::PaintTinSave;
			R.Text = P.Num() > 2 ? P[2] : FString::Printf(TEXT("Mix %d"), Bay.Mixed + 1);
		}
		else if (What == TEXT("recipe") && P.Num() >= 3)
		{
			// The n-th card on the swatch wall (newest first); "eye" after it: match it by eye.
			const TArray<const FTorquePaintRecipe*> Cards = Bay.Swatches();
			const int32 N = FCString::Atoi(*P[2]);
			if (!Cards.IsValidIndex(N)) { Warn(TEXT("no such card")); return true; }
			R.Kind = ETorqueShedAct::PaintRecipe;
			R.Item = Cards[N]->Id;
			R.Colour = P.Num() > 3 && P[3] == TEXT("eye") ? 1 : 0;
		}
		else if (What == TEXT("target")) R.Kind = ETorqueShedAct::PaintTarget;
		else if (What == TEXT("buy") && P.Num() >= 3)
		{
			R.Kind = ETorqueShedAct::PaintBuy;
			R.Item = 1;
			const int32 Base = TorquePaintMix::BaseIndex(P[2]);
			R.Colour = Base != INDEX_NONE ? FTorquePaintBay::BaseStock(Base) : FTorquePaintBay::TintStock(TorquePaintMix::TintIndex(P[2]));
		}
		else if (What == TEXT("take") && P.Num() >= 3) { R.Kind = ETorqueShedAct::PaintTinTake; R.Item = Bay.TinInSlot(FCString::Atoi(*P[2])); }
		else if (What == TEXT("put") && P.Num() >= 3) { R.Kind = ETorqueShedAct::PaintTinPut; R.Item = FCString::Atoi(*P[2]); }
		else if (What == TEXT("tobench")) R.Kind = ETorqueShedAct::PaintTinBench;
		else if (What == TEXT("bin")) R.Kind = ETorqueShedAct::PaintTinBin;
		else if (What == TEXT("cup")) R.Kind = ETorqueShedAct::PaintCup;
		else if (What == TEXT("tapetake")) R.Kind = ETorqueShedAct::PaintTapeTake;
		else if (What == TEXT("tapeput")) R.Kind = ETorqueShedAct::PaintTapePut;
		else if (What == TEXT("pattern") && P.Num() >= 3) { R.Kind = ETorqueShedAct::PaintTapePattern; R.Colour = FCString::Atoi(*P[2]); }
		else if (What == TEXT("hook") && P.Num() >= 4)
		{
			// paint:hook:N:CM: spray booth hook N (1 .. 10) wound to CM off the floor.
			R.Kind = ETorqueShedAct::PaintHook;
			R.Item = FCString::Atoi(*P[2]) - 1;
			R.Amount = FCString::Atof(*P[3]);
		}
		else { Warn(TEXT("paint what? newtin:W, pour:PB:40, stir, tare, addbase, test, shake, empty, save[:name], recipe:N[:eye], target, buy:CODE, take:N, put:N, tobench, bin, cup, hook:N:CM, tapetake, pattern:N, tapeput")); return true; }
		Said(W->TestPaint(R));
		return true;
	}
	if (Verb == TEXT("radio"))
	{
		// What the shed radio's playing (its station and song, or why not).
		Said(ATorqueShedRadio::NowPlaying(World).Replace(TEXT("\n"), TEXT(" / ")));
		return true;
	}
	if (Verb == TEXT("lifts"))
	{
		// Each lift: up or down as the shed has it, how high it's got, and the test bay's bike's height.
		FString Line;
		for (int32 L = 0; L <= UTorqueBuildSubsystem::WashLift; ++L)
		{
			if (!UTorqueBuildSubsystem::IsLift(L)) continue;
			const ATorqueBikeLift* Lift = ATorqueBikeLift::Find(World, L);
			Line += FString::Printf(TEXT("%s%s %s %.0f cm"), Line.IsEmpty() ? TEXT("") : TEXT("; "), *UTorqueBuildSubsystem::LiftName(L),
				B->LiftUp(L) ? TEXT("up") : TEXT("down"), Lift ? Lift->Height() : -1.0);
		}
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
			if (It->Bay == TestBay)
				Line += FString::Printf(TEXT("; bay %d's bike at z %.1f (on lift %d)"), TestBay + 1, It->GetActorLocation().Z, B->LiftOf(TestBay));
		Said(Line);
		return true;
	}
	if (Verb == TEXT("bikelog"))
	{
		// Where the bike's ended up once it's settled (after a wait), and how it's standing.
		Said(W->BikeWhere(TestBay));
		return true;
	}
	if (Verb == TEXT("strap") && P.Num() >= 2)
	{
		// One of the rig's four posts, looked at from outside the rig (the bike behind it), and the
		// work button: done up if it was undone, undone if it was done up.
		const int32 Point = FMath::Clamp(FCString::Atoi(*P[1]), 0, 3);
		const FName Name(*FString::Printf(TEXT("dyno_strap_%d"), Point));
		const FTorquePlace* Place = TorqueShedLayout::FindPlace(Name);
		if (!Place) { Warn(TEXT("no such strap post")); return true; }
		const FVector At = Place->Box.GetCenter() + FVector(0.0, 0.0, Place->Box.GetExtent().Z * 0.6);
		const FVector Away(At.X < 0.0 ? -1.0 : 1.0, 0.0, 0.0);
		const FString Seen = W->TestLookAt(At, Away, 75.0);
		if (W->TestTargetPlace() != Name) { Warn(TEXT("couldn't get a look at it: saw ") + Seen); return true; }
		Said(W->TestAct());
		Said(FString::Printf(TEXT("%d of 4 straps done up"), B->DynoRoom().StrapsDone()));
		return true;
	}
	if (Verb == TEXT("fan") || Verb == TEXT("lights") || Verb == TEXT("switch"))
	{
		// fan:on|off, lights:on|off: the switch by the dyno room's door, flicked (F) if it isn't so
		// already; switch:fan|lights flicks it either way.
		const bool bFan = Verb == TEXT("fan") || (Verb == TEXT("switch") && P.Num() >= 2 && P[1] == TEXT("fan"));
		const bool bNow = bFan ? B->DynoRoom().bFan : B->DynoRoom().bLights;
		if (Verb != TEXT("switch") && P.Num() >= 2 && (P[1] == TEXT("on")) == bNow)
		{
			Said(bNow ? TEXT("already on") : TEXT("already off"));
			return true;
		}
		const FName Name = bFan ? FName(TEXT("dyno_fan")) : FName(TEXT("dyno_lights"));
		const FTorquePlace* Place = TorqueShedLayout::FindPlace(Name);
		if (!Place) { Warn(TEXT("no such switch")); return true; }
		const FString Seen = W->TestLookAt(Place->Box.GetCenter(), FVector(0.0, -1.0, 0.0), 70.0);
		if (W->TestTargetPlace() != Name) { Warn(TEXT("couldn't get a look at it: saw ") + Seen); return true; }
		Said(W->TestInteract());
		const bool bAfter = bFan ? B->DynoRoom().bFan : B->DynoRoom().bLights;
		Said(bAfter ? TEXT("now on") : TEXT("now off"));
		return true;
	}
	if (Verb == TEXT("shedlights") || Verb == TEXT("rollerdoor") || Verb == TEXT("extinguisher"))
	{
		// [kit] shedlights:on|off, rollerdoor:up|down: F at the switch boxes right of the roller door
		// (if it isn't so already); extinguisher:take|spray|stop|put: F at it, the work button held
		// or let go, F to put it back.
		const FString Arg = P.Num() >= 2 ? P[1] : FString();
		if (Verb == TEXT("extinguisher") && (Arg == TEXT("spray") || Arg == TEXT("stop")))
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::ExtinguisherSpray;
			R.Item = Arg == TEXT("spray") ? 1 : 0;
			FString WhyNot;
			if (!B->Request(R, WhyNot)) Warn(WhyNot);
			else Said(Arg == TEXT("spray") ? TEXT("spraying") : TEXT("stopped"));
			return true;
		}
		if (Verb == TEXT("extinguisher") && Arg == TEXT("put"))
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::Extinguisher;
			R.Item = 0;
			FString WhyNot;
			if (!B->Request(R, WhyNot)) Warn(WhyNot);
			else Said(TEXT("put back"));
			return true;
		}
		const bool bLights = Verb == TEXT("shedlights");
		if (!bLights && Verb == TEXT("rollerdoor") && (Arg == TEXT("up")) == B->DoorUp()) { Said(TEXT("already so")); return true; }
		if (bLights && (Arg == TEXT("on")) == B->ShedLightsOn()) { Said(TEXT("already so")); return true; }
		const FName Name = Verb == TEXT("extinguisher") ? FName(TEXT("extinguisher")) : (bLights ? FName(TEXT("shed_lights")) : FName(TEXT("door_button")));
		const FTorquePlace* Place = TorqueShedLayout::FindPlace(Name);
		if (!Place) { Warn(TEXT("no such place")); return true; }
		const FVector Away = Verb == TEXT("extinguisher") ? FVector(1.0, 0.0, 0.0) : FVector(0.0, -1.0, 0.0);
		const FString Seen = W->TestLookAt(Place->Box.GetCenter(), Away, 80.0);
		if (W->TestTargetPlace() != Name) { Warn(TEXT("couldn't get a look at it: saw ") + Seen); return true; }
		Said(W->TestInteract());
		Said(FString::Printf(TEXT("lights %s, door %s, extinguisher held by %d"), B->ShedLightsOn() ? TEXT("on") : TEXT("off"),
			B->DoorUp() ? TEXT("up") : TEXT("down"), B->ExtinguisherHeldBy()));
		return true;
	}
	if (Verb == TEXT("standmove") && P.Num() >= 3)
	{
		// [kit] standmove:<x>:<y>[:<yaw>]: take hold of the engine stand and let go of it there.
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::GrabStand;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) { Warn(WhyNot); return true; }
		R.Kind = ETorqueShedAct::ReleaseStand;
		R.Place = FTransform(FRotator(0.0, P.Num() >= 4 ? FCString::Atod(*P[3]) : 0.0, 0.0), FVector(FCString::Atod(*P[1]), FCString::Atod(*P[2]), 0.0));
		if (!B->Request(R, WhyNot)) { Warn(WhyNot); return true; }
		Said(FString::Printf(TEXT("engine stand at (%.0f, %.0f)"), B->StandTransform().GetLocation().X, B->StandTransform().GetLocation().Y));
		return true;
	}
	if (Verb == TEXT("dynorun"))
	{
		// F at the dyno desk, then wait out the run (dynorun:mid stops about 60% of the way in, for a
		// shot of the graph growing).
		const FTorquePlace* Place = TorqueShedLayout::FindPlace(TEXT("dyno_desk"));
		if (!Place) { Warn(TEXT("no dyno desk")); return true; }
		const FVector At = Place->Box.GetCenter() + FVector(0.0, 0.0, Place->Box.GetExtent().Z);
		const FString Seen = W->TestLookAt(At, FVector(-1.0, 0.0, 0.0), 90.0);
		if (W->TestTargetPlace() != FName(TEXT("dyno_desk"))) { Warn(TEXT("couldn't get a look at the desk: saw ") + Seen); return true; }
		const int32 Bay = B->DynoBay();
		const int32 Before = Bay != INDEX_NONE && B->HasDynoRun(Bay) ? B->DynoRunOf(Bay).N : 0;
		Said(W->TestInteract());
		if (Bay == INDEX_NONE || !B->HasDynoRun(Bay) || B->DynoRunOf(Bay).N == Before) { Warn(TEXT("no run started")); return true; }
		const float Length = TorqueShedPower::RunTime(B->DynoRunOf(Bay));
		const bool bMid = P.Num() >= 2 && P[1] == TEXT("mid");
		Wait = bMid ? Length * 0.6 : Length + 1.0;
		Said(FString::Printf(TEXT("run %d started, %.1f s long"), B->DynoRunOf(Bay).N, Length));
		return true;
	}
	if (Verb == TEXT("dynolog"))
	{
		// The rig's bike's runs, once the latest has finished playing.
		float Rpm = 0.0f, Progress = 0.0f;
		int32 Gear = 0;
		if (ATorqueDynoRoom::Live(World, Rpm, Gear, Progress)) return false;   // still running: try again in a second
		const int32 Bay = B->DynoBay();
		if (Bay == INDEX_NONE || !B->HasDynoRun(Bay)) { Warn(TEXT("no runs on the rig's bike")); return true; }
		for (const FTorqueDynoRun& Run : B->Bike(Bay).DynoRuns)
		{
			FString Line = FString::Printf(TEXT("bay %d run %d: %.1f hp @ %.0f rpm, %.1f Nm @ %.0f rpm, limiter %.0f rpm, engine %.0f C, heat %.2f"),
				Bay + 1, Run.N, Run.PeakHp, Run.PeakHpRpm, Run.PeakNm, Run.PeakNmRpm, Run.Redline, Run.TempC, Run.Heat);
			Line += Run.bFan ? TEXT(", fan on") : TEXT(", fan off");
			Said(Line);
		}
		return true;
	}
	if (Verb == TEXT("georeload"))
	{
		Said(B->TestReload());
		const bool bOk = TorqueFlow::BackToShed(World);
		Said(bOk ? TEXT("opening the shed again") : TEXT("couldn't open the shed again (no shed map, or a client)"));
		return true;
	}
	if (Verb == TEXT("geo") && P.Num() >= 2)
	{
		const FString& What = P[1];
		ATorqueGeometryMode* G = ATorqueGeometryMode::For(W->GetController());
		if (What == TEXT("open"))
		{
			FString Why;
			if (G) Said(TEXT("already open"));
			else if (ATorqueGeometryMode::Open(W, TestBay, Why)) Said(FString::Printf(TEXT("geometry mode open on bay %d"), TestBay + 1));
			else Warn(TEXT("refused: ") + Why);
			return true;
		}
		if (What == TEXT("check"))
		{
			const FString Now = B->Exists(TestBay) ? B->Bike(TestBay).Geo : FString();
			const bool bSame = !GeoSent.IsEmpty() && Now == GeoSent;
			if (bSame) Said(TEXT("Geo survived: yes, the bay's fit is the one saved: ") + Now);
			else Warn(FString::Printf(TEXT("Geo survived: NO. Sent %s, the bay has %s"), GeoSent.IsEmpty() ? TEXT("nothing") : *GeoSent,
				Now.IsEmpty() ? TEXT("nothing") : *Now));
			// And drawn with it: each moved slot's first part, from where it'd be without the fit.
			for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
			{
				if (It->Bay != TestBay) continue;
				TSet<FName> Seen;
				for (int32 I = 0; I < It->Parts.Num() && I < It->FitView.Rest.Num(); ++I)
				{
					const UStaticMeshComponent* C = It->Parts[I];
					const FName Slot = C ? TorqueShedModel::SlotTag(C) : NAME_None;
					if (Slot.IsNone() || Seen.Contains(Slot) || !Now.Contains(FString::Printf(TEXT("\"%s\""), *Slot.ToString()))) continue;
					Seen.Add(Slot);
					const FVector D = C->GetRelativeLocation() - It->FitView.Rest[I].GetLocation();
					Said(FString::Printf(TEXT("%s drawn moved by (%.1f, %.1f, %.1f) cm (bike's frame: forward, right, up)"), *Slot.ToString(), D.X, D.Y, D.Z));
				}
			}
			return true;
		}
		if (!G) { Warn(TEXT("geometry mode isn't open (geo:open first)")); return true; }
		if (What == TEXT("rig") && P.Num() >= 4) { Said(G->TestRig(P[2], FCString::Atod(*P[3]))); return true; }
		if (What == TEXT("move") && P.Num() >= 6)
		{
			Said(G->TestMove(FName(*P[2]), FVector(FCString::Atod(*P[3]), FCString::Atod(*P[4]), FCString::Atod(*P[5]))));
			return true;
		}
		if (What == TEXT("select") && P.Num() >= 3) { Said(G->TestSelect(FName(*P[2]))); return true; }
		if (What == TEXT("save")) { GeoSent = G->TestSave(); Said(TEXT("saved: ") + GeoSent); return true; }
		if (What == TEXT("exit")) { G->TestExit(); Said(TEXT("left geometry mode")); return true; }
		Warn(TEXT("geo what? open, rig, move, select, save, exit, check"));
		return true;
	}
	Warn(TEXT("unknown step (see TorqueShedTest.h)"));
	return true;
}
