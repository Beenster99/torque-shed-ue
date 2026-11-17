// [garage] The forecourt garage's jobs (beenster, Nov 17 2026: "Let's keep the modding simple. When you
// get to a petrol station there is a garage unit that you can ride your bike into, make room for 3 bikes,
// then when in this room you can use your spanner and a shop computer mounted on a wall"; and "You can't
// spanner your bike on the roadside"). Out on the ride the shed's parts are far away, so a garage keeps it
// simple: the wall computer sells a part straight into your hands, unboxed; the spanner on your bike fits
// the part in your hands and bolts it up in one go, or (hands empty) unbolts and takes off the part you
// look at. Whatever comes off is sent back to the shed and waits unboxed in its delivery area. The rules
// are the shed's own (the same part kinds, mounts, fitting's "won't fit"), done through the shed's own
// Install and Bolt so a fitted part is exactly as if fitted in the shed. Not here: wheels and tyres, the
// frame and engine swaps, and engine internals (they want the tyre changer, the bay and the engine stand).
#include "Shed/TorqueBuildSubsystem.h"

#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"

namespace TorqueGarageActsImpl
{
const FName FrameSlot(TEXT("frame"));
const FName EngineSlot(TEXT("engine"));
}
namespace TGAI = TorqueGarageActsImpl;

bool UTorqueBuildSubsystem::IsGarageAct(ETorqueShedAct A)
{
	return (uint8)A >= (uint8)ETorqueShedAct::GarageBuy && (uint8)A <= (uint8)ETorqueShedAct::GarageStow;
}

FString UTorqueBuildSubsystem::GarageOffWhy(int32 Bay, FName Slot) const
{
	if (Slot == TGAI::FrameSlot || Slot == TGAI::EngineSlot) return TEXT("Frame and engine swaps are done in the shed.");
	if (TorqueShedParts::IsTyreSlot(Slot) || !TorqueShedParts::TyreOf(Slot).IsNone()) return TEXT("Wheels and tyres are changed in the shed, on the tyre changer.");
	if (TorqueShedParts::IsEngineSlot(Slot) && !TorqueShedParts::IsServiceSlot(Slot))
		return FString::Printf(TEXT("The %s is inside the engine: that's a job for the engine stand in the shed."), *TorqueShedParts::SlotName(Slot).ToLower());
	const FHost H = HostOf(Bay, Slot, 0);
	if (!H.Slots || H.Slots->FindRef(Slot) == 0) return TEXT("That part isn't fitted.");
	return PartsOnIt(H, Slot);
}

FName UTorqueBuildSubsystem::GarageSlot(int32 Bay, FName Looked, const FTorqueItem& It, FString& Why) const
{
	const TArray<FName> Candidates = TorqueShedParts::SlotsForKind(It.Kind);
	if (Candidates.Num() == 0) { Why = TEXT("That doesn't bolt on anywhere."); return NAME_None; }
	const FHost H = HostOf(Bay, Candidates[0], 0);
	if (!H.Slots) { Why = TEXT("Fit an engine first."); return NAME_None; }
	// The slot looked at if it takes this part, else the first one free, else the first.
	FName Pick = Candidates.Contains(Looked) ? Looked : NAME_None;
	for (const FName& S : Candidates)
		if (Pick.IsNone() && H.Slots->FindRef(S) == 0) Pick = S;
	if (Pick.IsNone()) Pick = Candidates[0];
	if (Pick == TGAI::FrameSlot || Pick == TGAI::EngineSlot) { Why = TEXT("Frame and engine swaps are done in the shed."); return NAME_None; }
	if (TorqueShedParts::IsTyreSlot(Pick) || !TorqueShedParts::TyreOf(Pick).IsNone()) { Why = TEXT("Wheels and tyres are changed in the shed, on the tyre changer."); return NAME_None; }
	if (TorqueShedParts::IsEngineSlot(Pick) && !TorqueShedParts::IsServiceSlot(Pick))
	{
		Why = FString::Printf(TEXT("The %s goes inside the engine: that's a job for the engine stand in the shed."), *TorqueShedParts::SlotName(Pick).ToLower());
		return NAME_None;
	}
	// What it mounts on must be there (not counting what's in the slot now, which comes off).
	for (const FName& M : TorqueShedParts::PartMounts(Pick, It.Part))
		if (M != Pick && M != TGAI::EngineSlot && H.Slots->FindRef(M) == 0)
		{
			Why = FString::Printf(TEXT("Fit the %s first."), *TorqueShedParts::SlotName(M).ToLower());
			return NAME_None;
		}
	return Pick;
}

FString UTorqueBuildSubsystem::CheckGarage(int32 Peer, const FTorqueShedRequest& R) const
{
	const int32 Held = HeldBy(Peer);
	const FTorqueItem* HeldIt = Items.Find(Held);
	switch (R.Kind)
	{
	case ETorqueShedAct::GarageBuy:
		if (Held != 0) return TEXT("Your hands are full: fit that first, or send it back to the shed.");
		if (!TorqueShedParts::IsPart(R.Part) || TorqueShedParts::IsStock(R.Part)) return TEXT("Unknown part.");
		if (TorqueShedParts::IsFluid(R.Part)) return TEXT("Oil and fluids are in the shed's stores.");
		if (TorqueShedParts::SlotsForKind(TorqueShedParts::PartKind(R.Part)).Num() == 0) return TEXT("That's not sold here.");
		return FString();
	case ETorqueShedAct::GarageStow:
	{
		if (!HeldIt) return TEXT("You're not holding a part.");
		FVector Spot;
		if (!FreeDelivery(HeldIt->Part, Spot)) return TEXT("The shed's delivery area is full: bring some boxes in when you're back.");
		return FString();
	}
	case ETorqueShedAct::GarageOff:
	{
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (Held != 0) return TEXT("Your hands are full: fit that first, or send it back to the shed.");
		const FString Why = GarageOffWhy(R.Bay, R.Slot);
		if (!Why.IsEmpty()) return Why;
		const FTorqueItem* On = Items.Find(ItemOn(R.Bay, R.Slot));
		FVector Spot;
		if (On && !FreeDelivery(On->Part, Spot)) return TEXT("The shed's delivery area is full: bring some boxes in when you're back.");
		return FString();
	}
	case ETorqueShedAct::GarageFit:
	{
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (!HeldIt) return TEXT("You're not holding a part: buy one at the computer.");
		if (HeldIt->bBoxed) return TEXT("Unpack it first.");
		FString Why;
		const FName S = GarageSlot(R.Bay, R.Slot, *HeldIt, Why);
		if (S.IsNone()) return Why;
		if (ItemOn(R.Bay, S) != 0)
		{
			const FString Off = GarageOffWhy(R.Bay, S);
			if (!Off.IsEmpty()) return Off;
			const FTorqueItem* On = Items.Find(ItemOn(R.Bay, S));
			FVector Spot;
			if (On && !FreeDelivery(On->Part, Spot)) return TEXT("The shed's delivery area is full: bring some boxes in when you're back.");
		}
		if (S == FName(TEXT("tank")) || S == FName(TEXT("fork")))
		{
			// The tank and fork: what fitting's layout makes of it (won't fit stops it).
			FString Tier, Reason;
			TArray<FString> Work;
			if (FitTier(R.Bay, S, HeldIt->Part, Tier, Reason, Work) && Tier == TEXT("won't fit"))
				return Reason.IsEmpty() ? FString(TEXT("That won't fit this bike.")) : Reason;
		}
		return FString();
	}
	default:
		return TEXT("That can't be done here.");
	}
}

void UTorqueBuildSubsystem::SendHome(int32 Id)
{
	FTorqueItem* It = Items.Find(Id);
	if (!It) return;
	FVector Spot = TorqueShedLayout::DeliveryAt(0);
	FreeDelivery(It->Part, Spot);   // (Check found it room)
	It->bBoxed = false;
	Move(Id, ETorqueAt::Floor, 0);
	It->Place = FTransform(FRotator(0.0, Rng.FRandRange(-8.0f, 8.0f), 0.0), Spot);
}

FString UTorqueBuildSubsystem::ApplyGarage(int32 Peer, const FTorqueShedRequest& R)
{
	FString Text;
	switch (R.Kind)
	{
	case ETorqueShedAct::GarageBuy:
	{
		const int32 Id = NewItem(R.Part, 0.0f, 0.0f, 0.0f);
		Items[Id].bBoxed = false;
		Move(Id, ETorqueAt::Held, Peer);
		Text = FString::Printf(TEXT("Bought %s. Hold the spanner on your bike to fit it."), *TorqueShedParts::PartName(R.Part));
		OnItems.Broadcast();
		break;
	}
	case ETorqueShedAct::GarageStow:
	{
		const int32 Held = HeldBy(Peer);
		Text = FString::Printf(TEXT("The %s is on its way back to the shed's delivery area."), *ItemTitle(Held));
		SendHome(Held);
		OnItems.Broadcast();
		break;
	}
	case ETorqueShedAct::GarageOff:
	{
		const int32 Id = ItemOn(R.Bay, R.Slot);
		const FString Title = ItemTitle(Id);
		const FHost H = HostOf(R.Bay, R.Slot, 0);
		H.Slots->Add(R.Slot, 0);
		H.Bolted->Add(R.Slot, false);
		SendHome(Id);
		Text = FString::Printf(TEXT("Took off the %s. It's gone back to the shed's delivery area."), *Title);
		RebuildBuild(R.Bay);
		OnChanged.Broadcast(R.Bay);
		OnItems.Broadcast();
		break;
	}
	case ETorqueShedAct::GarageFit:
	{
		FString Why;
		const int32 Held = HeldBy(Peer);
		const FName S = GarageSlot(R.Bay, R.Slot, Items[Held], Why);
		FString Was;
		if (const int32 Old = ItemOn(R.Bay, S))
		{
			// What's there now comes off first, back to the shed.
			Was = ItemTitle(Old);
			const FHost H = HostOf(R.Bay, S, 0);
			H.Slots->Add(S, 0);
			H.Bolted->Add(S, false);
			SendHome(Old);
		}
		// Then the shed's own Install and Bolt (the fabrication a part owes, a wheel's tyre...).
		FTorqueShedRequest In;
		In.Kind = ETorqueShedAct::Install;
		In.Bay = R.Bay;
		In.Slot = S;
		In.bCrouched = true;
		const FString Fitted = DoApply(Peer, In);
		FTorqueShedRequest Bolt = In;
		Bolt.Kind = ETorqueShedAct::Bolt;
		DoApply(Peer, Bolt);
		Text = FString::Printf(TEXT("Fitted and bolted up the %s"), *ItemTitle(ItemOn(R.Bay, S)));
		if (!Was.IsEmpty()) Text += FString::Printf(TEXT("; the %s has gone back to the shed"), *Was);
		Text += TEXT(".");
		// (fabrication owed, as the shed's Install says it)
		const int32 FabAt = Fitted.Find(TEXT(" It needs fabrication"));
		if (FabAt != INDEX_NONE) Text += Fitted.Mid(FabAt);
		break;
	}
	default:
		break;
	}
	MarkSave();
	OnHostChange.Broadcast();
	return Text;
}
