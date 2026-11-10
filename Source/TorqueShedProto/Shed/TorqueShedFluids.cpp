// [fluids] Fluids in the shed, as Torque Shed keeps them (shed_state.gd's fluid{type, litres, age}
// on tanks, engines, master cylinders and the cans and bottles they come in; its pour, drain, lube
// and tip actions; shed_pump.gd's petrol pump on the front wall outside). Part of
// UTorqueBuildSubsystem: the host checks and does each request, everyone sees the levels.
//
// The UE shed has no money, so the pump's petrol is free: its display counts the litres of each
// sale, from lifting the nozzle to hanging it back up.
#include "Shed/TorqueBuildSubsystem.h"

#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"

namespace TorqueShedFluidsImpl
{
const FName Tank(TEXT("tank")), AuxTank(TEXT("aux_tank")), Engine(TEXT("engine")), Chain(TEXT("chain"));
const FName FrontMaster(TEXT("front_master")), RearMaster(TEXT("rear_master"));
const FName Petrol(TEXT("petrol")), Oil(TEXT("oil")), BrakeFluid(TEXT("brake_fluid")), ChainLube(TEXT("chain_lube"));
const FName Unleaded(TEXT("unleaded_95")), Semi(TEXT("semi_10w40")), Mineral(TEXT("mineral_20w50")), Dot4(TEXT("dot4"));

// Litres a second poured from a can, or drained out of a bike (shed_state POUR_RATE).
float PourRate(FName Kind)
{
	if (Kind == Petrol) return 2.0f;
	if (Kind == Oil) return 0.8f;
	if (Kind == BrakeFluid) return 0.15f;
	return 1.0f;
}

FString Litres(float L, float Cap)
{
	if (Cap < 1.0f) return FString::Printf(TEXT("%.2f of %.2f L"), L, Cap);
	// A sump of 2.5 L reads 2.5, a tank of 17 L reads 17.
	const FString CapText = FMath::IsNearlyEqual(Cap, FMath::RoundToFloat(Cap), 0.05f) ? FString::Printf(TEXT("%.0f"), Cap) : FString::Printf(TEXT("%.1f"), Cap);
	return FString::Printf(TEXT("%.1f of %s L"), L, *CapText);
}

FString Lower(const FString& S) { return S.ToLower(); }
}
namespace TSFI = TorqueShedFluidsImpl;

bool UTorqueBuildSubsystem::IsFluidAct(ETorqueShedAct A)
{
	return A == ETorqueShedAct::Pour || A == ETorqueShedAct::Drain || A == ETorqueShedAct::Lube || A == ETorqueShedAct::Tip
		|| A == ETorqueShedAct::PumpNozzle || A == ETorqueShedAct::PumpFill || A == ETorqueShedAct::RideFuel;
}

// --- Reading the levels -------------------------------------------------------------------------------

float UTorqueBuildSubsystem::FuelOf(int32 Bay) const
{
	float L = 0.0f;
	for (const FName& Slot : {TSFI::Tank, TSFI::AuxTank})
		if (const FTorqueItem* It = Items.Find(ItemOn(Bay, Slot))) L += It->Litres;
	return L;
}

float UTorqueBuildSubsystem::FuelCapacityOf(int32 Bay) const
{
	float L = 0.0f;
	for (const FName& Slot : {TSFI::Tank, TSFI::AuxTank})
		if (const FTorqueItem* It = Items.Find(ItemOn(Bay, Slot))) L += TorqueShedParts::FluidCapacity(It->Part);
	return L;
}

UTorqueBuildSubsystem::FFluidLevels UTorqueBuildSubsystem::FluidsOf(int32 Bay) const
{
	FFluidLevels F;
	if (!Exists(Bay)) return F;
	auto Level = [](const FTorqueItem& It) { return FMath::Clamp(It.Litres / FMath::Max(TorqueShedParts::FluidCapacity(It.Part), 0.01f), 0.0f, 1.0f); };
	F.FuelLitres = FuelOf(Bay);
	F.FuelCapacity = FuelCapacityOf(Bay);
	if (const FTorqueItem* T = Items.Find(ItemOn(Bay, TSFI::Tank)))
	{
		F.bTank = true;
		F.FuelAge = T->FluidAge;
	}
	if (const FTorqueItem* E = Items.Find(ItemOn(Bay, TSFI::Engine)))
	{
		F.bEngine = true;
		F.Oil = Level(*E);
		F.OilAge = E->FluidAge;
	}
	if (const FTorqueItem* M = Items.Find(ItemOn(Bay, TSFI::FrontMaster)))
	{
		F.bFrontBrake = true;
		F.FrontBrake = Level(*M);
		F.BrakeAge = M->FluidAge;
	}
	if (const FTorqueItem* M = Items.Find(ItemOn(Bay, TSFI::RearMaster)))
	{
		F.bRearBrake = true;
		F.RearBrake = Level(*M);
		F.BrakeAge = FMath::Max(F.BrakeAge, M->FluidAge);
	}
	if (const FTorqueItem* C = Items.Find(ItemOn(Bay, TSFI::Chain)))
	{
		F.bChain = true;
		F.Lube = C->Lube;
	}
	return F;
}

FString UTorqueBuildSubsystem::FluidText(const FTorqueItem& It) const
{
	if (It.Kind == TSFI::Chain)
	{
		// shed_state chain_text's lube half.
		const TCHAR* Oiled = It.Lube > 0.8f ? TEXT("freshly lubed") : (It.Lube > 0.3f ? TEXT("fine") : TEXT("dry"));
		return FString::Printf(TEXT("Lube %d%% (%s)"), FMath::RoundToInt(It.Lube * 100.0f), Oiled);
	}
	const float Cap = TorqueShedParts::FluidCapacity(It.Part);
	if (Cap <= 0.0f) return FString();
	const FName K = TorqueShedParts::FluidKind(It.Part);
	const FString What = TorqueShedParts::FluidName(K);
	if (It.Litres <= 0.001f)
		return FString::Printf(TEXT("%s: empty (holds %s L)"), *What, *(Cap < 1.0f ? FString::Printf(TEXT("%.2f"), Cap) : FString::Printf(TEXT("%.0f"), Cap)));
	const FString KindName = !It.FluidType.IsNone() ? TorqueShedParts::PartName(It.FluidType) : What;
	const TCHAR* State = It.FluidAge < 0.25f ? TEXT("fresh") : (It.FluidAge < 0.6f ? TEXT("used") : (K == TSFI::Petrol ? TEXT("stale") : TEXT("old and dirty")));
	return FString::Printf(TEXT("%s: %s  -  %s, %s"), *What, *TSFI::Litres(It.Litres, Cap), *KindName, State);
}

int32 UTorqueBuildSubsystem::FluidHolder(int32 Bay, int32 EngineId, FName Slot, FName Kind) const
{
	// shed_state fluid_holder: the tank, the engine, or the master cylinder looked at (else the front one).
	if (EngineId != 0) return Kind == TSFI::Oil ? EngineId : 0;
	FName On = Slot;
	if (Kind == TSFI::Petrol) On = TSFI::Tank;
	else if (Kind == TSFI::Oil) On = TSFI::Engine;
	else if (Kind == TSFI::BrakeFluid)
	{
		if (On != TSFI::FrontMaster && On != TSFI::RearMaster) On = Slot.ToString().StartsWith(TEXT("rear_")) ? TSFI::RearMaster : TSFI::FrontMaster;
	}
	else return 0;
	return ItemOn(Bay, On);
}

float UTorqueBuildSubsystem::PourTime(const FTorqueItem& From, const FTorqueItem& Into)
{
	const float Room = TorqueShedParts::FluidCapacity(Into.Part) - Into.Litres;
	const float Moved = FMath::Min(From.Litres, FMath::Max(Room, 0.0f));
	const FName K = TorqueShedParts::FluidKind(TorqueShedParts::IsFluid(Into.Part) ? From.Part : Into.Part);
	return FMath::Clamp(Moved / TSFI::PourRate(K), 0.6f, 5.0f);
}

// --- The petrol pump (shed_pump.gd) ---------------------------------------------------------------------

FTransform UTorqueBuildSubsystem::PumpAt()
{
	// On the outside face of the front wall (ue_build_shed.py: 550 + the 30 cm wall), west of the
	// roller door, its face out to the yard (the props' front is +y).
	return FTransform(FRotator::ZeroRotator, FVector(-360.0, 580.0, 0.0));
}

FVector UTorqueBuildSubsystem::PumpHoseRoot()
{
	// Out of the bottom of the cabinet, on its left (petrol_pump's case runs 60 .. 150 cm up).
	return PumpAt().TransformPosition(FVector(-15.0, 15.0, 60.0));
}

FVector UTorqueBuildSubsystem::PumpHolster()
{
	return PumpAt().TransformPosition(FVector(28.0, 17.0, 100.0));
}

FVector UTorqueBuildSubsystem::FillerOf(int32 Bay) const
{
	// About where the cap is on a bike standing where it's parked (ShedPump.FILLER_GUESS): over the
	// front of the tank, a metre up.
	return BikeTransform(Bay).TransformPosition(FVector(18.0, 0.0, 100.0));
}

bool UTorqueBuildSubsystem::PumpReaches(int32 Bay) const
{
	return Exists(Bay) && FVector::Dist(FillerOf(Bay), PumpHoseRoot()) <= PumpHose;
}

float UTorqueBuildSubsystem::FillTanks(int32 Bay, float Want)
{
	float Put = 0.0f;
	for (const FName& Slot : {TSFI::Tank, TSFI::AuxTank})
	{
		FTorqueItem* It = Items.Find(ItemOn(Bay, Slot));
		if (!It || Want - Put <= 0.0f) continue;
		const float Cap = TorqueShedParts::FluidCapacity(It->Part);
		const float Have = It->Litres;
		const float Add = FMath::Clamp(Want - Put, 0.0f, FMath::Max(Cap - Have, 0.0f));
		if (Add <= 0.0f) continue;
		// Fresh pump fuel freshens what was there; mostly pump fuel, it's pump fuel.
		It->FluidAge = It->FluidAge * Have / (Have + Add);
		if (It->FluidType.IsNone() || Add > Have) It->FluidType = TSFI::Unleaded;
		It->Litres = Have + Add;
		Put += Add;
	}
	return Put;
}

void UTorqueBuildSubsystem::ReportRideFuel(int32 Bay, float Litres)
{
	if (!Exists(Bay) || !FMath::IsFinite(Litres)) return;
	// Each quarter litre burnt (and never more than the tank had), so co-op isn't sent a shed a frame.
	if (RideFuelSent >= 0.0f && FMath::Abs(RideFuelSent - Litres) < 0.25f && Litres > 0.05f) return;
	if (Litres >= FuelOf(Bay) - 0.01f) return;
	RideFuelSent = Litres;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::RideFuel;
	R.Bay = Bay;
	R.Amount = Litres;
	FString Why;
	Request(R, Why);
}

// --- Starting fluids ------------------------------------------------------------------------------------

void UTorqueBuildSubsystem::NewItemFluid(FTorqueItem& It) const
{
	// shed_state _new_item: a can or bottle comes full of its grade (unless it's one that comes empty);
	// a tank, an engine or a master cylinder comes dry. A chain comes lubed.
	It.FluidType = NAME_None;
	It.Litres = 0.0f;
	It.FluidAge = 0.0f;
	It.Lube = 1.0f;
	if (TorqueShedParts::IsFluid(It.Part) && !TorqueShedParts::ComesEmpty(It.Part))
	{
		It.FluidType = TorqueShedParts::FluidGrade(It.Part);
		It.Litres = TorqueShedParts::FluidCapacity(It.Part);
	}
}

void UTorqueBuildSubsystem::FillFluids(int32 Bay, float Age)
{
	// shed_state _fill_fluids: an old bike's tank has a splash in it, its oil and brake fluid are down
	// and old, and its chain's dry, the more so the older it is. (A test run's bikes are ready to ride:
	// the riding tests mustn't run out of petrol.)
	if (IsTestRun())
	{
		ReadyToRide(Bay, 1.0f);
		return;
	}
	struct FFill { FName Slot, Type; float Share; };
	const FFill Fills[] = {
		{TSFI::Tank, TSFI::Unleaded, Rng.FRandRange(0.05f, 0.5f) * (1.0f - Age * 0.6f)},
		{TSFI::Engine, Age > 0.5f ? TSFI::Mineral : TSFI::Semi, Rng.FRandRange(0.55f, 1.0f) * (1.0f - Age * 0.4f)},
		{TSFI::FrontMaster, TSFI::Dot4, Rng.FRandRange(0.5f, 1.0f) * (1.0f - Age * 0.3f)},
		{TSFI::RearMaster, TSFI::Dot4, Rng.FRandRange(0.5f, 1.0f) * (1.0f - Age * 0.3f)},
	};
	for (const FFill& F : Fills)
		if (FTorqueItem* It = Items.Find(ItemOn(Bay, F.Slot)))
		{
			It->FluidType = F.Type;
			It->Litres = FMath::RoundToFloat(TorqueShedParts::FluidCapacity(It->Part) * F.Share * 100.0f) / 100.0f;
			It->FluidAge = FMath::Clamp(Age * Rng.FRandRange(0.9f, 1.3f), 0.0f, 1.0f);
		}
	if (FTorqueItem* C = Items.Find(ItemOn(Bay, TSFI::Chain))) C->Lube = FMath::Clamp(Rng.FRandRange(0.0f, 0.6f) * (1.0f - Age), 0.0f, 1.0f);
}

void UTorqueBuildSubsystem::ReadyToRide(int32 Bay, float FuelShare)
{
	// shed_state _ready_to_ride: the oil and brake fluid filled to the mark and fresh, the chain lubed,
	// and at least FuelShare of a tank (top_up_fuel: a tank with more in it is left as it is).
	for (const FName& Slot : {TSFI::Engine, TSFI::FrontMaster, TSFI::RearMaster})
		if (FTorqueItem* It = Items.Find(ItemOn(Bay, Slot)))
		{
			if (It->FluidType.IsNone()) It->FluidType = Slot == TSFI::Engine ? TSFI::Semi : TSFI::Dot4;
			It->Litres = TorqueShedParts::FluidCapacity(It->Part);
			It->FluidAge = 0.0f;
		}
	if (FTorqueItem* C = Items.Find(ItemOn(Bay, TSFI::Chain))) C->Lube = 1.0f;
	for (const FName& Slot : {TSFI::Tank, TSFI::AuxTank})
		if (FTorqueItem* It = Items.Find(ItemOn(Bay, Slot)))
		{
			const float WantL = FMath::CeilToFloat(TorqueShedParts::FluidCapacity(It->Part) * FuelShare * 100.0f) / 100.0f;
			if (It->Litres >= WantL) continue;
			if (It->FluidType.IsNone()) It->FluidType = TSFI::Unleaded;
			// What's put in is fresh: the old petrol left in it is that much less of the tank.
			It->FluidAge = It->FluidAge * It->Litres / FMath::Max(WantL, 0.01f);
			It->Litres = WantL;
		}
}

void UTorqueBuildSubsystem::FluidsForOldSave()
{
	// shed_state _brim_old_tanks: a shed from before fluids comes back with every bike ready to ride
	// on a full tank, so nobody loads in stranded. Parts off the bikes are dry, cans there are none.
	int32 N = 0;
	for (int32 Bay = 0; Bay < Bikes.Num(); ++Bay)
		if (Exists(Bay))
		{
			ReadyToRide(Bay, 1.0f);
			++N;
		}
	UE_LOG(LogTemp, Log, TEXT("Torque: a shed saved before fluids: %d bikes filled up and ready to ride"), N);
	MarkSave();
}

// --- Moving fluid -----------------------------------------------------------------------------------------

float UTorqueBuildSubsystem::Transfer(FTorqueItem& From, FTorqueItem& Into)
{
	// shed_state _transfer: as much as fits; the mix takes the type of whichever there's more of, and
	// its age is the average.
	const float Room = TorqueShedParts::FluidCapacity(Into.Part) - Into.Litres;
	const float Moved = FMath::Min(From.Litres, FMath::Max(Room, 0.0f));
	if (Moved <= 0.0f) return 0.0f;
	const float Before = Into.Litres;
	const FName SrcType = TorqueShedParts::FluidGrade(!From.FluidType.IsNone() ? From.FluidType : From.Part);
	if (Before <= 0.001f || Moved >= Before) Into.FluidType = SrcType;
	Into.FluidAge = (Into.FluidAge * Before + From.FluidAge * Moved) / (Before + Moved);
	Into.Litres = Before + Moved;
	From.Litres -= Moved;
	if (From.Litres <= 0.0005f)
	{
		From.Litres = 0.0f;
		From.FluidType = NAME_None;
		From.FluidAge = 0.0f;
	}
	return Moved;
}

FString UTorqueBuildSubsystem::CheckFluid(int32 Peer, const FTorqueShedRequest& R) const
{
	const int32 Held = HeldBy(Peer);
	const FTorqueItem* Can = Items.Find(Held);
	switch (R.Kind)
	{
	case ETorqueShedAct::Pour:
	case ETorqueShedAct::Drain:
	{
		if (!Can || !TorqueShedParts::IsFluid(Can->Part)) return TEXT("You need a can or bottle of it in your hands.");
		const FTorqueItem* Into = Items.Find(R.Item);
		if (!Into || TorqueShedParts::IsFluid(Into->Part) || TorqueShedParts::FluidCapacity(Into->Part) <= 0.0f) return TEXT("That doesn't take any fluid.");
		if (Into->bBoxed) return TEXT("Unpack it first.");
		const FName K = TorqueShedParts::FluidKind(Into->Part), CanK = TorqueShedParts::PartKind(Can->Part);
		if (K != CanK)
			return FString::Printf(TEXT("That takes %s, not %s."), *TorqueShedParts::FluidName(K).ToLower(), *TorqueShedParts::FluidName(CanK).ToLower());
		const float Room = TorqueShedParts::FluidCapacity(Into->Part) - Into->Litres;
		const float CanRoom = TorqueShedParts::FluidCapacity(Can->Part) - Can->Litres;
		if (R.Kind == ETorqueShedAct::Pour)
		{
			if (Can->Litres <= 0.001f) return TEXT("It's empty.");
			if (Room <= 0.005f) return TEXT("It's already full.");
			return FString();
		}
		// Draining an engine on a bike: the drain plug's underneath (unless the bike's up on its lift).
		const int32 OnBay = BayOf(R.Item);
		if (K == TSFI::Oil && OnBay != INDEX_NONE && !R.bCrouched && !LiftRaised(OnBay)) return TEXT("Crouch down to get at the drain plug (C).");
		if (Into->Litres <= 0.001f) return TEXT("There's nothing to drain.");
		if (CanRoom <= 0.005f)
			return FString::Printf(TEXT("Your %s is full. Tip it away at the general waste bin."), *TorqueShedParts::ContainerName(Can->Part).ToLower());
		return FString();
	}
	case ETorqueShedAct::Lube:
	{
		if (!Can || Can->Kind != TSFI::ChainLube) return TEXT("You need a can of chain lube in your hands.");
		if (Can->Litres < LubeUse * 0.5f) return TEXT("The can's empty.");
		const FTorqueItem* C = Items.Find(R.Item);
		if (!C || C->Kind != TSFI::Chain) return TEXT("Aim at a chain.");
		const int32 OnBay = BayOf(R.Item);
		if (OnBay != INDEX_NONE && !R.bCrouched && !LiftRaised(OnBay)) return TEXT("Crouch down (C) to get at the chain.");
		if (C->Lube > 0.95f) return TEXT("It's freshly lubed.");
		return FString();
	}
	case ETorqueShedAct::Tip:
		if (!Can || !TorqueShedParts::IsFluid(Can->Part) || Can->Litres <= 0.0f) return TEXT("Nothing to tip away.");
		if (!R.Bin.IsNone() && R.Bin != FName(TEXT("general"))) return TEXT("Tip it away at the general waste bin.");
		return FString();
	case ETorqueShedAct::PumpNozzle:
		if (R.Item != 0)
		{
			if (PumpBy == Peer) return TEXT("You've got the nozzle already.");
			if (PumpBy != 0) return TEXT("Someone else has the nozzle.");
			if (Held != 0) return TEXT("Put the part down first.");
			if (PushedBy(Peer) != INDEX_NONE || StandPusher == Peer || FurnitureMovedBy(Peer) != 0) return TEXT("Let go of what you're moving first.");
			if (ExtinguisherBy == Peer) return TEXT("Put the fire extinguisher back first (F).");
			if (Paint.Held.Contains(Peer) || Paint.TapeBy == Peer) return TEXT("Put down what you're carrying first.");
			return FString();
		}
		return PumpBy == Peer ? FString() : FString(TEXT("You're not holding the nozzle."));
	case ETorqueShedAct::PumpFill:
	{
		if (PumpBy != Peer) return TEXT("Lift the nozzle off the pump first.");
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (Bikes[R.Bay].Pusher != 0) return TEXT("Someone is wheeling that bike about.");
		if (ItemOn(R.Bay, TSFI::Tank) == 0) return TEXT("There's no tank fitted to fill.");
		if (FuelSpace(R.Bay) < 0.05f) return TEXT("The tank's full.");
		if (!PumpReaches(R.Bay)) return TEXT("The hose won't reach that far. Wheel the bike up to the pump.");
		if (!(R.Amount > 0.0f && R.Amount <= 5.0f)) return TEXT("How much?");
		return FString();
	}
	case ETorqueShedAct::RideFuel:
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (!(R.Amount >= 0.0f && R.Amount < 1000.0f)) return TEXT("How much?");
		return FString();
	default:
		return TEXT("That isn't a fluid job.");
	}
}

FString UTorqueBuildSubsystem::ApplyFluid(int32 Peer, const FTorqueShedRequest& R, TSet<int32>& LooksOf, bool& bItems)
{
	FTorqueItem* Can = Items.Find(HeldBy(Peer));
	auto Touch = [this, &LooksOf, &bItems](int32 Id)
	{
		const int32 On = BayOf(Id);
		if (On == INDEX_NONE) bItems = true;
		else LooksOf.Add(On);
	};
	switch (R.Kind)
	{
	case ETorqueShedAct::Pour:
	case ETorqueShedAct::Drain:
	{
		FTorqueItem* Into = Items.Find(R.Item);
		if (!Can || !Into) return FString();
		const bool bPour = R.Kind == ETorqueShedAct::Pour;
		const float Moved = bPour ? Transfer(*Can, *Into) : Transfer(*Into, *Can);
		const FString What = TorqueShedParts::FluidName(TorqueShedParts::FluidKind(Into->Part)).ToLower();
		Touch(R.Item);
		bItems = true;   // (the can in your hands: its level)
		const FString Amount = Moved < 1.0f ? FString::Printf(TEXT("%.2f L"), Moved) : FString::Printf(TEXT("%.1f L"), Moved);
		const float Cap = TorqueShedParts::FluidCapacity(Into->Part);
		if (bPour)
			return FString::Printf(TEXT("Poured in %s of %s: %s"), *Amount, *What, *TSFI::Litres(Into->Litres, Cap));
		return FString::Printf(TEXT("Drained %s of %s into the %s"), *Amount, *What, *TorqueShedParts::ContainerName(Can->Part).ToLower());
	}
	case ETorqueShedAct::Lube:
	{
		FTorqueItem* C = Items.Find(R.Item);
		if (!Can || !C) return FString();
		// How well it went on: coverage along the chain, less any flung off where it was overdone.
		const float Q = FMath::Clamp(R.Amount > 0.0f ? R.Amount : 1.0f, 0.0f, 1.0f);
		const float Waste = FMath::Clamp(R.Tilt, 0.0f, 1.0f);
		Can->Litres = FMath::Max(0.0f, Can->Litres - LubeUse * (1.0f + Waste * 2.0f));
		C->Lube = FMath::Max(C->Lube, FMath::Lerp(0.5f, 1.0f, Q));
		C->LubeType = !Can->FluidType.IsNone() ? Can->FluidType : Can->Part;
		Touch(R.Item);
		bItems = true;
		return FString::Printf(TEXT("Lubed the chain with %s"), *TorqueShedParts::PartName(C->LubeType).ToLower());
	}
	case ETorqueShedAct::Tip:
		if (!Can) return FString();
		Can->Litres = 0.0f;
		Can->FluidAge = 0.0f;
		Can->FluidType = NAME_None;
		bItems = true;
		return TEXT("Tipped it away for recycling");
	case ETorqueShedAct::PumpNozzle:
		if (R.Item != 0)
		{
			// A new sale: the display goes back to nought.
			PumpBy = Peer;
			PumpLitres = 0.0f;
			return TEXT("Lifted the nozzle off the pump. Hold the work button on a bike's tank to fill it.");
		}
		PumpBy = 0;
		return PumpLitres < 0.05f ? FString(TEXT("Hung the nozzle back on the pump."))
			: FString::Printf(TEXT("Hung the nozzle back up: %.2f litres."), PumpLitres);
	case ETorqueShedAct::PumpFill:
	{
		const float Put = FillTanks(R.Bay, R.Amount);
		PumpLitres += Put;
		LooksOf.Add(R.Bay);
		if (FuelSpace(R.Bay) < 0.01f)
			// The nozzle's sensor in the filler neck: the petrol's reached it, and it clicks off.
			return FString::Printf(TEXT("Click: the nozzle cut off. %s is brimmed (%.2f litres)."), *Bikes[R.Bay].Name, PumpLitres);
		return FString();
	}
	case ETorqueShedAct::RideFuel:
	{
		// What the dash says is left, kept in the main tank first (it's topped up from the auxiliary).
		float Left = FMath::Min(R.Amount, FuelOf(R.Bay));
		for (const FName& Slot : {TSFI::Tank, TSFI::AuxTank})
			if (FTorqueItem* It = Items.Find(ItemOn(R.Bay, Slot)))
			{
				It->Litres = FMath::Min(Left, TorqueShedParts::FluidCapacity(It->Part));
				Left -= It->Litres;
			}
		LooksOf.Add(R.Bay);   // (the bay TV's fuel bar)
		return FString();
	}
	default:
		return FString();
	}
}

void UTorqueBuildSubsystem::DropFluidOf(int32 Peer)
{
	if (PumpBy != Peer || Peer == 0) return;
	// Whoever had the nozzle has gone: it's back on the pump.
	PumpBy = 0;
	MarkSave();
	OnHostChange.Broadcast();
}

FString UTorqueBuildSubsystem::TestSetFluid(int32 Bay, const FString& Kind, float Level)
{
	if (!Exists(Bay)) return TEXT("no bike in that bay");
	const float L = FMath::Clamp(Level, 0.0f, 1.0f);
	TArray<FName> Slots;
	if (Kind == TEXT("petrol") || Kind == TEXT("fuel")) Slots = {TSFI::Tank, TSFI::AuxTank};
	else if (Kind == TEXT("oil")) Slots = {TSFI::Engine};
	else if (Kind == TEXT("brake") || Kind == TEXT("brake_fluid")) Slots = {TSFI::FrontMaster, TSFI::RearMaster};
	else if (Kind == TEXT("lube") || Kind == TEXT("chain_lube") || Kind == TEXT("chain")) Slots = {TSFI::Chain};
	else return TEXT("not a fluid: ") + Kind;
	int32 Set = 0;
	for (const FName& Slot : Slots)
		if (FTorqueItem* It = Items.Find(ItemOn(Bay, Slot)))
		{
			if (Slot == TSFI::Chain) It->Lube = L;
			else It->Litres = TorqueShedParts::FluidCapacity(It->Part) * L;
			++Set;
		}
	if (Set == 0) return FString::Printf(TEXT("bay %d's bike has nothing that holds %s"), Bay + 1, *Kind);
	OnLooks.Broadcast(Bay);
	MarkSave();
	OnHostChange.Broadcast();
	return FString::Printf(TEXT("set bay %d's %s to %.0f%% (%d part%s)"), Bay + 1, *Kind, L * 100.0f, Set, Set == 1 ? TEXT("") : TEXT("s"));
}
