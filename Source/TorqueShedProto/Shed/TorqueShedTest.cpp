#include "Shed/TorqueShedTest.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorqueWalker.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace TorqueShedTestImpl
{
// Torque Shed's loop on bay 1's bike, end to end: every tool, the repair bench, the spray can, the
// wash bay, the workbench, the parts computer, a delivery, fitting and bolting, riding out and back.
const TCHAR* const LoopSteps =
	TEXT("bay:1,log,look:bike,shot,look:bench,shot,look:computer,shot,")
	TEXT("tool:wrench,part:exhaust,act,")                    // unbolt the exhaust
	TEXT("tool:hands,part:exhaust,act,")                     // take it off: in your hands
	TEXT("place:repair_0,act,")                              // onto the repair bench
	TEXT("tool:repair,item:exhaust,act,")                    // grind off the rust
	TEXT("tool:spray,spray:7:matte,item:exhaust,act,shot,")  // matte teal
	TEXT("tool:hands,item:exhaust,act,")                     // pick it up
	TEXT("floor:-900:100,act,")                              // down in the wash bay
	TEXT("tool:lance,item:exhaust,hold:3,")                  // pressure wash it
	TEXT("tool:hands,item:exhaust,act,place:bench_0,act,")   // onto the workbench
	TEXT("order:slip_on,item:slip_on,f,item:slip_on,act,")   // order a slip-on, unpack it, pick it up
	TEXT("part:tank,act,")                                   // fit it to the bike
	TEXT("tool:wrench,part:exhaust,act,log,")                // bolt it up
	TEXT("tool:hands,rideout,wait:10,shot,back,wait:6,log,quit");

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
	if (Spec.TrimStartAndEnd() == TEXT("loop")) Spec = TSTI::LoopSteps;
	Spec.ParseIntoArray(Steps, TEXT(","), true);
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
	if (Wait > 0.0) return true;
	if (Next >= Steps.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("Torque test: all shed steps done"));
		return false;   // stop ticking
	}
	const FString S = Steps[Next].TrimStartAndEnd();
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
	auto Said = [&S](const FString& What) { UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s"), *S, *What); };
	auto Warn = [&S](const FString& What) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: %s"), *S, *What); };

	if (Verb == TEXT("wait")) { Wait = P.Num() > 1 ? FCString::Atod(*P[1]) : 2.0; Said(FString::Printf(TEXT("%.1f s"), Wait)); return true; }
	if (Verb == TEXT("quit"))
	{
		Said(TEXT("quitting"));
		if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
		return true;
	}
	if (Verb == TEXT("shot"))
	{
		Said(TEXT("screenshot (Saved/Screenshots)"));
		if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("HighResShot 1"));
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

	// The rest need the walker in the shed.
	ATorqueWalker* W = Walker();
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World);
	if (!W || !B)
	{
		if (++Tries < 20) return false;
		Warn(TEXT("skipped: no walker (not in the shed?)"));
		Tries = 0;
		return true;
	}
	Tries = 0;
	if (Verb == TEXT("item") || Verb == TEXT("place") || Verb == TEXT("floor") || Verb == TEXT("door") || Verb == TEXT("look")) bAimLost = false;

	if (Verb == TEXT("tool") && P.Num() >= 2)
	{
		static const TCHAR* const Names[] = {TEXT("hands"), TEXT("wrench"), TEXT("brush"), TEXT("repair"), TEXT("spray"), TEXT("lance")};
		int32 Index = INDEX_NONE;
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Names); ++I)
			if (P[1].Equals(Names[I], ESearchCase::IgnoreCase)) Index = I;
		if (Index == INDEX_NONE) { Warn(TEXT("no such tool (hands, wrench, brush, repair, spray, lance)")); return true; }
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
		Said(FString::Printf(TEXT("%s %s"), *UTorqueBuildSubsystem::FinishName(Finish), *UTorqueBuildSubsystem::PaintName(Colour)));
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
		for (TActorIterator<ATorqueShedItems> It(World); It && !bDrawn; ++It) bDrawn = It->WhereIs(Found, At);
		if (Found == 0 || !bDrawn) { Warn(Found == 0 ? TEXT("no such part off the bikes") : TEXT("it isn't drawn")); return true; }
		Said(TEXT("looking at ") + W->TestLookAt(At, TSTI::TowardsMiddle(At), 90.0));
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
		FVector At = FVector::ZeroVector, Away = FVector::ZeroVector;
		double Distance = 220.0;
		FName PlaceName = NAME_None;
		if (What == TEXT("bench")) PlaceName = TEXT("bench_3");
		else if (What == TEXT("washer")) PlaceName = TEXT("washer_0");
		else if (What == TEXT("repair")) PlaceName = TEXT("repair_1");
		else if (What == TEXT("bins")) PlaceName = TEXT("plastic");
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
		else if (What == TEXT("deliveries")) { At = TorqueShedLayout::DeliveryAt(0) + FVector(110.0, 50.0, 30.0); Away = FVector(-0.5, -1.0, 0.0).GetSafeNormal(); Distance = 350.0; }
		else if (What == TEXT("washbay")) { At = FVector(-930.0, 150.0, 60.0); Away = FVector(1.0, 0.4, 0.0).GetSafeNormal(); Distance = 300.0; }
		else { Warn(TEXT("look at what? bench, washer, repair, computer, changer, stand, bins, bike, deliveries, washbay, or a place name")); return true; }
		Said(TEXT("looking at ") + W->TestLookAt(At, Away, Distance));
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
	Warn(TEXT("unknown step (see TorqueShedTest.h)"));
	return true;
}
