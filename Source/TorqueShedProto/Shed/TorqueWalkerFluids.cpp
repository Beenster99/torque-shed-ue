// [fluids] The walker with fluids (Torque Shed's walker.gd _fluid_plan and shed_pump.gd): the petrol
// pump's nozzle (F lifts it off the pump and hangs it back; in your hand, the work button fills the
// bike you're at, a squirt at a time, till it clicks off), and a can or bottle in your hands (pour it
// into the tank, engine or brake reservoir you're looking at, right mouse drains into it; the chain
// lube on a chain; at the general waste bin it's tipped away).
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"

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

bool ATorqueWalker::FluidInput(bool bF)
{
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
			return bF;
		}
		if (!bF) return false;
		// F anywhere: hang it back up.
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PumpNozzle;
		R.Item = 0;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		Doing(FString(), 0.0);
		return true;
	}
	if (!bF) return false;
	// F at the pump: lift the nozzle off it.
	if (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Pump)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::PumpNozzle;
		R.Item = 1;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		return true;
	}
	return false;
}

void ATorqueWalker::PumpPlan()
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	const int32 Me = B->Me();
	if (B->PumpHeldBy() != Me)
	{
		// Looking at the pump with the nozzle on it: F lifts it (FluidInput), shown on the HUD.
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
	const FString Level = FString::Printf(TEXT("%s of %s"), *FluidLitresText(IntoIt->Litres), *FluidLitresText(Cap));
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
