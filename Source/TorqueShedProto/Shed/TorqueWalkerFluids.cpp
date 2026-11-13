// [fluids] The walker with fluids (Torque Shed's walker.gd _fluid_plan and shed_pump.gd): the petrol
// pump's nozzle (right mouse lifts it off the pump and hangs it back; in your hand, holding right
// mouse fills the bike you're at, a squirt at a time, till it clicks off and dings), and a can or
// bottle in your hands (pour it into the tank, engine or brake reservoir you're looking at, right
// mouse drains into it; the chain lube on a chain; at the general waste bin it's tipped away).
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "EngineUtils.h"

namespace
{
const FName FluidChain(TEXT("chain"));
const FName FluidChainLube(TEXT("chain_lube"));
const FName FluidGeneralBin(TEXT("general"));

FString FluidLitresText(float L)
{
	return L < 1.0f ? FString::Printf(TEXT("%.2f L"), L) : FString::Printf(TEXT("%.1f L"), L);
}
}

bool ATorqueWalker::FluidInput(bool bRight, bool bLeft)
{
	// The pump (beenster, build 141): left mouse (Cross) lifts the nozzle and, held at a bike, fills
	// it (PumpPlan's Plan); right mouse (R, Square) also lifts it, and hangs it back up from anywhere.
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	const int32 Me = B->Me();
	FString WhyNot;
	if (B->PumpHeldBy() == Me)
	{
		// The hose only stretches so far (shed_pump.gd HOSE): walk off with it and it pulls out of
		// your hand and swings back to the pump.
		const FVector Root = UTorqueBuildSubsystem::PumpHoseRoot();
		const double Away = FVector::Dist2D(GetActorLocation(), Root);
		if (Away > UTorqueBuildSubsystem::PumpHose + 60.0)
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::PumpNozzle;
			R.Item = 0;
			if (B->Request(R, WhyNot)) Say(TEXT("The hose wouldn't stretch any further: the nozzle's back on the pump."), false);
			Doing(FString(), 0.0);
			return bRight || bLeft;
		}
		// Left mouse hangs it up only at the pump itself (at a bike it fills: the work button's Plan).
		const bool bAtPump = Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Pump;
		if (!bRight && !(bLeft && bAtPump)) return false;
		// Right mouse anywhere, left at the pump: hang it back up.
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PumpNozzle;
		R.Item = 0;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		Doing(FString(), 0.0);
		bWaitRelease = true;
		return true;
	}
	if (!bRight && !bLeft) return false;
	// Left (or right) mouse at the pump: lift the nozzle off it.
	if (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Pump)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PumpNozzle;
		R.Item = 1;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		bWaitRelease = true;
		return true;
	}
	return false;
}

bool ATorqueWalker::PumpFilling() const
{
	return (bWorkHeld || ActHeldFor > 0.0) && Plan.bValid && Plan.Request.Kind == ETorqueShedAct::PumpFill;
}

bool ATorqueWalker::PumpFlowing() const
{
	return PumpFilling() && Plan.Blocked.IsEmpty();
}

void ATorqueWalker::PumpPlan()
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	const int32 Me = B->Me();
	if (B->PumpHeldBy() != Me)
	{
		// Looking at the pump with the nozzle on it: left mouse lifts it (FluidInput), shown on the HUD.
		return;
	}
	// The nozzle in your hand: the bike you're looking at (any of its parts, or its bay's floor).
	int32 Bay = INDEX_NONE;
	if (Target == ETorqueLook::BikePart) Bay = TargetBay;
	else if (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bay) Bay = TargetPlace->Index;
	if (Bay == INDEX_NONE || !B->Exists(Bay)) return;
	const double Step = 0.5;
	const float Space = B->FuelSpace(Bay);
	const FString Name = B->Bike(Bay).Name;
	// On the work button (left mouse, Cross), held: a squirt each half second till it clicks off.
	Plan = MakeOne(ETorqueShedAct::PumpFill, Step, FString::Printf(TEXT("fill %s's tank (%s of %s)"), *Name,
		*FluidLitresText(B->FuelOf(Bay)), *FluidLitresText(B->FuelCapacityOf(Bay))));
	Plan.bRepeat = true;
	Plan.Request.Bay = Bay;
	Plan.Request.Amount = FMath::Clamp(UTorqueBuildSubsystem::PumpRate * float(Step), 0.01f, FMath::Max(Space, 0.01f));
	Plan.Blocked = B->Check(Me, Plan.Request);
}

bool ATorqueWalker::FluidPlan(int32 Held)
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	const FTorqueItem* Can = B->Item(Held);
	if (!Can) return false;
	const int32 Me = B->Me();
	const FName CanKind = TorqueShedParts::PartKind(Can->Part);
	const FString CanName = TorqueShedParts::ContainerName(Can->Part).ToLower();
	const FTorqueItem* Thing = B->Item(TargetItem);

	// The general waste: what's in it tipped away for recycling (an empty one goes in the bin as usual).
	if (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bin)
	{
		if (Can->Litres <= 0.0f || TargetPlace->Name != FluidGeneralBin) return false;
		Plan = MakeOne(ETorqueShedAct::Tip, 1.5, FString::Printf(TEXT("tip the %s away (%s)"),
			*TorqueShedParts::FluidName(CanKind).ToLower(), *FluidLitresText(Can->Litres)));
		Plan.Request.Bin = TargetPlace->Name;
		Plan.Blocked = B->Check(Me, Plan.Request);
		return true;
	}

	// The chain lube: sprayed along the chain looked at (on the bike, crouched, or loose).
	if (CanKind == FluidChainLube)
	{
		if (Thing && Thing->Kind == FluidChain && (Target == ETorqueLook::BikePart || Target == ETorqueLook::Item))
		{
			Plan = MakeOne(ETorqueShedAct::Lube, 2.5, FString::Printf(TEXT("lube the chain (%d%%)"), FMath::RoundToInt(Thing->Lube * 100.0f)));
			Plan.Request.Item = TargetItem;
			Plan.Request.Amount = 1.0f;
			Plan.Blocked = B->Check(Me, Plan.Request);
			return true;
		}
		return Target == ETorqueLook::BikePart;   // (a can of lube isn't fitted to a bike)
	}

	// What it goes into: the bike's tank, engine or brake reservoir looked at (shed_state
	// fluid_holder), an engine off the bike, or a loose tank or reservoir.
	int32 Into = 0;
	if (Target == ETorqueLook::BikePart && TargetBay != INDEX_NONE)
		Into = B->FluidHolder(TargetBay, TargetEngine, TargetSlot, CanKind);
	else if (Target == ETorqueLook::Item && Thing)
	{
		if (TargetEngine != 0) Into = B->FluidHolder(INDEX_NONE, TargetEngine, TargetSlot, CanKind);
		else if (!TorqueShedParts::IsFluid(Thing->Part) && TorqueShedParts::FluidCapacity(Thing->Part) > 0.0f) Into = TargetItem;
	}
	const FTorqueItem* IntoIt = B->Item(Into);
	if (!IntoIt) return Target == ETorqueLook::BikePart;
	const FName Kind = TorqueShedParts::FluidKind(IntoIt->Part);
	const FString What = TorqueShedParts::FluidName(Kind).ToLower();
	const float Cap = TorqueShedParts::FluidCapacity(IntoIt->Part);
	const FString Level = FString::Printf(TEXT("it has %s of %s"), *FluidLitresText(IntoIt->Litres), *FluidLitresText(Cap));
	FString Where = TEXT("it");
	if (Kind == FName(TEXT("petrol"))) Where = TEXT("the tank");
	else if (Kind == FName(TEXT("oil"))) Where = TEXT("the engine");
	else if (Kind == FName(TEXT("brake_fluid"))) Where = TEXT("the brake reservoir");

	if (!TorqueShedParts::ComesEmpty(Can->Part) || Can->Litres > 0.001f)
	{
		Plan = MakeOne(ETorqueShedAct::Pour, UTorqueBuildSubsystem::PourTime(*Can, *IntoIt),
			FString::Printf(TEXT("pour %s into %s (%s)"), *What, *Where, *Level));
		Plan.Request.Item = Into;
		Plan.Blocked = B->Check(Me, Plan.Request);
	}
	FPlan& DrainTo = Plan.bValid ? AltPlan : Plan;
	DrainTo = MakeOne(ETorqueShedAct::Drain, UTorqueBuildSubsystem::PourTime(*IntoIt, *Can),
		FString::Printf(TEXT("drain %s's %s into the %s (%s)"), *Where, *What, *CanName, *Level));
	DrainTo.Request.Item = Into;
	DrainTo.Blocked = B->Check(Me, DrainTo.Request);
	return true;
}

// --- For the tests (UTorqueShedTest pump: and fluid: steps) -------------------------------------------

FString ATorqueWalker::FluidLog() const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("no shed");
	FString Out;
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
	{
		if (!B->Exists(Bay)) continue;
		const UTorqueBuildSubsystem::FFluidLevels F = B->FluidsOf(Bay);
		auto Pc = [](bool bHas, float V) { return bHas ? FString::Printf(TEXT("%.0f%%"), V * 100.0f) : FString(TEXT("-")); };
		Out += FString::Printf(TEXT("[bay %d %s: fuel %s, oil %s (age %.2f), brake F %s R %s, chain lube %s] "), Bay + 1, *B->Bike(Bay).Name,
			F.bTank ? *FString::Printf(TEXT("%.2f / %.1f L"), F.FuelLitres, F.FuelCapacity) : TEXT("-"),
			*Pc(F.bEngine, F.Oil), F.OilAge, *Pc(F.bFrontBrake, F.FrontBrake), *Pc(F.bRearBrake, F.RearBrake), *Pc(F.bChain, F.Lube));
	}
	const int32 Me = B->Me();
	const FTorqueItem* Can = B->Item(B->HeldBy(Me));
	if (Can && TorqueShedParts::IsFluid(Can->Part))
		Out += FString::Printf(TEXT("[in hand: item %d %s, %.2f of %.2f L] "), Can->Id, *Can->Part.ToString(), Can->Litres, TorqueShedParts::FluidCapacity(Can->Part));
	const int32 By = B->PumpHeldBy();
	Out += FString::Printf(TEXT("[pump: nozzle %s, sale %.2f L]"), By == 0 ? TEXT("hung up") : (By == Me ? TEXT("in my hand") : TEXT("in someone else's hand")), B->PumpSale());
	return Out;
}

FString ATorqueWalker::TestFluid(const FString& Verb, const TArray<FString>& Args, int32 Bay)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("not ready: no shed");
	const int32 Me = B->Me();
	const FString What = Args.Num() > 0 ? Args[0] : FString(TEXT("log"));
	Note.Reset();
	if (What == TEXT("log") || What == TEXT("state")) return FluidLog();

	if (Verb == TEXT("pump"))
	{
		if (What == TEXT("lift"))
		{
			if (B->PumpHeldBy() == Me) return TEXT("already holding the nozzle: ") + FluidLog();
			// From the yard, in front of the cabinet, looking at it.
			const FTorquePlace* Place = TorqueShedLayout::FindPlace(FName(TEXT("petrol_pump")));
			if (!Place) return TEXT("not there: no petrol_pump place");
			const FString Seen = TestLookAt(Place->Box.GetCenter(), FVector(0.2, 1.0, 0.0).GetSafeNormal(), 110.0);
			FindTarget();
			if (!(Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Pump))
				return TEXT("not looking at the pump: ") + Seen + TEXT(" (went into: ") + TestCandidates() + TEXT(")");
			FluidInput(false, true);
			bWaitRelease = false;
			return FString::Printf(TEXT("left mouse at the pump: %s; %s"), Note.IsEmpty() ? TEXT("-") : *Note, *FluidLog());
		}
		if (What == TEXT("hang"))
		{
			if (B->PumpHeldBy() != Me) return TEXT("not holding the nozzle: ") + FluidLog();
			Plan = FPlan();   // not at a bike: right mouse hangs it up
			FluidInput(true);
			bWaitRelease = false;
			return FString::Printf(TEXT("right mouse with the nozzle: %s; %s"), Note.IsEmpty() ? TEXT("-") : *Note, *FluidLog());
		}
		if (What == TEXT("burn"))
		{
			// As if it had been ridden: Litres burnt out of its tank (the ride's own report to the shed).
			if (!B->Exists(Bay)) return TEXT("can't burn: no bike in that bay");
			const float Before = B->FuelOf(Bay);
			const float Burn = Args.Num() > 1 ? FMath::Max(0.0f, (float)FCString::Atod(*Args[1])) : 5.0f;
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::RideFuel;
			R.Bay = Bay;
			R.Amount = FMath::Max(0.0f, Before - Burn);
			FString WhyNot;
			if (!B->Request(R, WhyNot)) return TEXT("can't burn: ") + WhyNot;
			return FString::Printf(TEXT("burnt bay %d's petrol: %.2f -> %.2f L; %s"), Bay + 1, Before, B->FuelOf(Bay), *FluidLog());
		}
		if (What == TEXT("fill"))
		{
			if (B->PumpHeldBy() != Me) return TEXT("can't fill: not holding the nozzle (pump:lift first)");
			if (!B->Exists(Bay)) return TEXT("can't fill: no bike in that bay");
			const float Before = B->FuelOf(Bay);
			const double Seconds = Args.Num() > 1 ? FMath::Clamp(FCString::Atod(*Args[1]), 0.5, 120.0) : 10.0;
			// Stand by the filler on the pump's side and hold left mouse, a squirt each half second.
			const FVector Filler = B->FillerOf(Bay);
			FVector Side = (UTorqueBuildSubsystem::PumpHoseRoot() - Filler).GetSafeNormal2D();
			if (Side.IsNearlyZero()) Side = FVector(0.0, 1.0, 0.0);
			const FString Seen = TestLookAt(Filler, Side, 90.0);
			int32 Squirts = 0;
			FString Stop = TEXT("held for the time");
			for (double T = 0.0; T < Seconds; T += 0.5)
			{
				ReAim();
				FindTarget();
				MakePlan();
				if (!Plan.bValid || Plan.Request.Kind != ETorqueShedAct::PumpFill)
				{
					Stop = TEXT("no fill plan looking at ") + Describe();
					break;
				}
				if (!Plan.Blocked.IsEmpty())
				{
					Stop = Plan.Blocked;
					break;
				}
				if (!Do(Plan)) { Stop = Note; break; }
				++Squirts;
			}
			const float After = B->FuelOf(Bay);
			const FString Out = FString::Printf(TEXT("filled bay %d (looking at %s): %d squirts, fuel %.2f -> %.2f of %.1f L, stopped: %s%s; %s"),
				Bay + 1, *Seen, Squirts, Before, After, B->FuelCapacityOf(Bay), *Stop, Note.IsEmpty() ? TEXT("") : *(TEXT(" [") + Note + TEXT("]")), *FluidLog());
			return After > Before + 0.01f ? Out : TEXT("failed to fill: ") + Out;
		}
		return TEXT("not a pump step: ") + What;
	}

	// fluid:
	if (What == TEXT("carry"))
	{
		if (Args.Num() < 2) return TEXT("not said what: fluid:carry:<oil|petrol|brake_fluid|chain_lube|part id>");
		FString Want = Args[1];
		if (Want == TEXT("brake")) Want = TEXT("brake_fluid");
		else if (Want == TEXT("lube")) Want = TEXT("chain_lube");
		else if (Want == TEXT("pan")) Want = TEXT("drain_pan");
		const FName WantName(*Want);
		if (const FTorqueItem* Held = B->Item(B->HeldBy(Me)))
		{
			if (Held->Part == WantName || (TorqueShedParts::PartKind(Held->Part) == WantName && !TorqueShedParts::ComesEmpty(Held->Part)))
				return TEXT("already holding it: ") + FluidLog();
			return TEXT("can't: hands full (fluid:drop first)");
		}
		// The fullest one lying about (a part id, or a kind's that doesn't come empty); else order one.
		auto Find = [&]()
		{
			int32 Best = 0;
			float BestL = -1.0f;
			for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
			{
				const FTorqueItem& It = KV.Value;
				if (!TorqueShedParts::IsFluid(It.Part) || (It.At != ETorqueAt::Floor && It.At != ETorqueAt::Spot)) continue;
				const bool bMatch = It.Part == WantName || (TorqueShedParts::PartKind(It.Part) == WantName && !TorqueShedParts::ComesEmpty(It.Part));
				if (bMatch && It.Litres > BestL) { BestL = It.Litres; Best = KV.Key; }
			}
			return Best;
		};
		int32 Id = Find();
		FString Ordered;
		if (Id == 0)
		{
			FName Part = WantName;
			if (!TorqueShedParts::IsFluid(Part))
				for (const FName& P : TorqueShedParts::PartsOfKind(WantName))
					if (!TorqueShedParts::ComesEmpty(P)) { Part = P; break; }
			if (!TorqueShedParts::IsFluid(Part)) return TEXT("not a fluid: ") + Want;
			Ordered = TestOrder(Part) + TEXT("; ");
			Id = Find();
			if (Id == 0) return TEXT("not delivered: ") + Ordered;
		}
		const FTorqueItem* It = B->Item(Id);
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Take;
		if (It->At == ETorqueAt::Floor) R.Item = Id;
		else R.Spot = It->Spot;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) return TEXT("can't take it: ") + WhyNot;
		return Ordered + FString::Printf(TEXT("took item %d (%s); "), Id, *It->Part.ToString()) + FluidLog();
	}
	if (What == TEXT("set"))
	{
		// A test shortcut: fluid:set:<petrol|oil|brake|lube>:<0 .. 1> on the test bay's bike.
		if (Args.Num() < 3) return TEXT("not said what: fluid:set:<petrol|oil|brake|lube>:<level 0 .. 1>");
		return B->TestSetFluid(Bay, Args[1], (float)FCString::Atod(*Args[2])) + TEXT("; ") + FluidLog();
	}
	if (What == TEXT("drop"))
	{
		if (B->HeldBy(Me) == 0) return TEXT("nothing in hand");
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Drop;
		R.Place = CarryDrop();
		FString WhyNot;
		if (!B->Request(R, WhyNot)) return TEXT("can't put it down: ") + WhyNot;
		return TEXT("put it down");
	}
	if (What == TEXT("slosh"))
	{
		const int32 Held = B->HeldBy(Me);
		if (Held == 0) return TEXT("not holding a can");
		const double Kick = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 0.0;
		FString Out = TEXT("not drawn: no shed items");
		for (TActorIterator<ATorqueShedItems> It(GetWorld()); It; ++It)
			if (!It->TestSlosh(Held, FVector2D(Kick, Kick * 0.4), Out)) Out = TEXT("not drawn as a can: ") + Out;
		return Out;
	}
	return TEXT("not a fluid step: ") + What;
}
