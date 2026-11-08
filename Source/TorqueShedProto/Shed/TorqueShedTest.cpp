#include "Shed/TorqueShedTest.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorquePartsBench.h"
#include "Shed/TorquePartsStore.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorquePaintRack.h"
#include "Shed/TorqueWalker.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UTorqueShedTest::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Spec;
	if (!FParse::Value(FCommandLine::Get(), TEXT("TorqueShedAction="), Spec, false)) return;
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
	FString Text;
	const UTorqueBuildSubsystem* B = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueBuildSubsystem>() : nullptr;
	if (B)
		for (const FName& Slot : TorqueFit::Slots(B->Get().Bike))
			Text += FString::Printf(TEXT(" %s=%s"), *Slot.ToString(), *B->FittedIn(Slot).ToString());
	UE_LOG(LogTemp, Log, TEXT("Torque test: %s, build:%s"), Where, *Text);
}

bool UTorqueShedTest::Step(float Dt)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->HasBegunPlay()) return true;
	if (SeenWorld.Get() != World)
	{
		// A new map: let it settle (the bike and bench draw at BeginPlay, the camera exposes).
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

namespace
{
// The biggest part of a component list matching a tag test, and where it is.
UStaticMeshComponent* Biggest(const TArray<UStaticMeshComponent*>& List, TFunctionRef<bool(const UStaticMeshComponent*)> Want)
{
	UStaticMeshComponent* Best = nullptr;
	double BestVol = -1.0;
	for (UStaticMeshComponent* C : List)
	{
		if (!C || !Want(C)) continue;
		const double Vol = C->Bounds.GetBox().GetVolume();
		if (Vol > BestVol) { BestVol = Vol; Best = C; }
	}
	return Best;
}
} // namespace

bool UTorqueShedTest::RunStep(const FString& S)
{
	TArray<FString> P;
	S.ParseIntoArray(P, TEXT(":"), true);
	if (P.Num() == 0) return true;
	UWorld* World = GetGameInstance()->GetWorld();
	const FString& Verb = P[0];
	static int32 Tries = 0;

	if (Verb == TEXT("wait")) { Wait = P.Num() > 1 ? FCString::Atod(*P[1]) : 2.0; UE_LOG(LogTemp, Log, TEXT("Torque test: wait %.1f s"), Wait); return true; }
	if (Verb == TEXT("quit"))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque test: quit"));
		if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
		return true;
	}
	if (Verb == TEXT("shot"))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque test: screenshot (Saved/Screenshots)"));
		if (APlayerController* PC = World->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("HighResShot 1"));
		return true;
	}
	if (Verb == TEXT("back"))
	{
		const bool bOk = TorqueFlow::BackToShed(World);
		UE_LOG(LogTemp, Log, TEXT("Torque test: back to the shed: %s"), bOk ? TEXT("going") : TEXT("refused (no shed map, or a client)"));
		return true;
	}

	// The rest need the walker in the shed.
	ATorqueWalker* W = Walker();
	if (!W)
	{
		if (++Tries < 20) return false;
		UE_LOG(LogTemp, Warning, TEXT("Torque test: %s skipped: no walker (not in the shed?)"), *S);
		Tries = 0;
		return true;
	}
	Tries = 0;
	ATorqueShedBike* Bike = nullptr;
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It) { Bike = *It; break; }
	ATorquePartsBench* Bench = nullptr;
	for (TActorIterator<ATorquePartsBench> It(World); It; ++It) { Bench = *It; break; }
	AActor* Door = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->ActorHasTag(TEXT("RideOut"))) { Door = *It; break; }

	// Aim at a bike part: from the side of the bike it's on, 130 cm out.
	// Aim at a bike part: from the side of the bike it's on, 130 cm out, at a point part way out
	// along it (the middle of a handlebar is behind the clocks; its ends, by the grips, aren't).
	auto AimBike = [W, Bike](UStaticMeshComponent* C) -> FString
	{
		const FVector Right = Bike->GetActorRightVector();
		const double Off = FVector::DotProduct(C->Bounds.Origin - Bike->GetActorLocation(), Right);
		const double Sign = Off < -2.0 ? -1.0 : 1.0;
		const double Half = FVector::DotProduct(C->Bounds.BoxExtent, Right.GetAbs());
		const FVector At = C->Bounds.Origin + Right * Sign * 0.6 * Half;
		return W->TestLookAt(At, Right * Sign, 130.0);
	};
	// Aim at the bench: from the room side, 100 cm out.
	auto AimBench = [W, Bench, Bike](UStaticMeshComponent* C) -> FString
	{
		const FVector At = C->Bounds.Origin;
		FVector Out = Bench->GetActorRightVector();
		if (Bike && FVector::DotProduct(Bike->GetActorLocation() - Bench->GetActorLocation(), Out) < 0.0) Out = -Out;
		return W->TestLookAt(At, Out, 100.0);
	};

	if (Verb == TEXT("swap") && P.Num() >= 2)
	{
		const FName Slot(*P[1]);
		UStaticMeshComponent* C = Bike ? Biggest(Bike->Parts, [Slot](const UStaticMeshComponent* X) { return TorqueShedModel::SlotTag(X) == Slot; }) : nullptr;
		if (!C) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: no %s part on the shed bike"), *S, *Slot.ToString()); return true; }
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: looking at %s"), *S, *AimBike(C));
		UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World);
		// Choose the part (as Q / R would), then one press of E: it ends on that part, or the log
		// says why it won't fit and the bike stays as it was.
		if (P.Num() >= 3) W->TestChoose(Slot, FName(*P[2]));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(false));
		if (P.Num() >= 3 && B)
			UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s now has %s (%s)"), *S, *Slot.ToString(), *B->FittedIn(Slot).ToString(),
				B->FittedIn(Slot) == FName(*P[2]) ? TEXT("as asked") : TEXT("not that part"));
		return true;
	}
	if (Verb == TEXT("bench") && P.Num() >= 3)
	{
		const FName Slot(*P[1]), Part(*P[2]);
		auto Match = [Slot, Part](const UStaticMeshComponent* X)
			{ return TorqueShedModel::SlotTag(X) == Slot && TorqueShedModel::OptionTag(X) == Part; };
		// On the bench (recently removed) first, else in the parts store, looked at from in front.
		UStaticMeshComponent* C = Bench ? Biggest(Bench->Items, Match) : nullptr;
		FString Seen;
		if (C) Seen = AimBench(C);
		else
			for (TActorIterator<ATorquePartsStore> It(World); It && !C; ++It)
				if ((C = Biggest(It->Items, Match)) != nullptr)
					Seen = W->TestLookAt(C->Bounds.Origin, It->GetActorForwardVector(), 100.0) + TEXT(" (store)");
		if (!C) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: no such spare on the bench or in the store (fitted already, or not imported)"), *S); return true; }
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: looking at %s"), *S, *Seen);
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(false));
		return true;
	}
	if (Verb == TEXT("rideout"))
	{
		if (!Door) { UE_LOG(LogTemp, Warning, TEXT("Torque test: rideout: no RideOut point in the level")); return true; }
		const FVector D = Door->GetActorLocation();
		const FVector In = Bike ? (Bike->GetActorLocation() - D).GetSafeNormal2D() : FVector(0, -1, 0);
		// In the doorway, looking out.
		UE_LOG(LogTemp, Log, TEXT("Torque test: rideout: at the door, seeing %s"), *W->TestLookAt(D - In * 300.0, In, 300.0));
		LogBuild(TEXT("riding out"));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(true));
		Wait = 3.0;
		return true;
	}
	// The bike's dirt and paint, as the shed draws them.
	auto Looks = [World, Bike](const FString& Only) -> FString
	{
		const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World);
		if (!B || !Bike) return TEXT("no bike");
		double Sum = 0.0;
		int32 N = 0;
		FString Painted, One;
		for (const UStaticMeshComponent* C : Bike->Parts)
		{
			const FName Part = TorqueShedModel::PartTag(C);
			if (Part.IsNone()) continue;
			Sum += B->DirtOf(Part);
			++N;
			FLinearColor Col;
			if (B->PaintOf(Part, Col)) Painted += FString::Printf(TEXT(" %s=%s"), *Part.ToString(), *Col.ToFColor(true).ToHex());
			if (Part.ToString() == Only) One = FString::Printf(TEXT(", %s %.0f%%"), *Only, 100.0f * B->DirtOf(Part));
		}
		return FString::Printf(TEXT("dirt %.0f%% on average over %d parts%s; painted:%s"), N ? 100.0 * Sum / N : 0.0, N, *One,
			Painted.IsEmpty() ? TEXT(" none") : *Painted);
	};
	// A bike part by its model name.
	auto PartNamed = [Bike](const FString& Name) -> UStaticMeshComponent*
	{
		if (!Bike) return nullptr;
		for (UStaticMeshComponent* C : Bike->Parts)
			if (TorqueShedModel::PartTag(C).ToString() == Name) return C;
		return nullptr;
	};
	if (Verb == TEXT("dirt"))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s"), *S, *Looks(P.Num() > 1 ? P[1] : FString()));
		return true;
	}
	if (Verb == TEXT("wash") && P.Num() >= 2)
	{
		UStaticMeshComponent* C = PartNamed(P[1]);
		if (!C) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: no part %s on the shed bike"), *S, *P[1]); return true; }
		const double Secs = P.Num() > 2 ? FCString::Atod(*P[2]) : 3.0;
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: looking at %s; before: %s"), *S, *AimBike(C), *Looks(P[1]));
		W->TestHoldWash(Secs);
		Wait = Secs + 0.5;   // the next step (dirt:<part>) shows what came off
		return true;
	}
	if (Verb == TEXT("paint") && P.Num() >= 3)
	{
		ATorquePaintRack* Rack = nullptr;
		for (TActorIterator<ATorquePaintRack> It(World); It; ++It) { Rack = *It; break; }
		int32 Index = INDEX_NONE;
		for (int32 I = 0; I < ATorquePaintRack::Paints().Num(); ++I)
			if (P[1].Replace(TEXT("_"), TEXT(" ")).Equals(ATorquePaintRack::Paints()[I].Name, ESearchCase::IgnoreCase) || P[1] == FString::FromInt(I)) Index = I;
		UStaticMeshComponent* Part = PartNamed(P[2]);
		if (!Rack || !Rack->Cards.IsValidIndex(Index) || !Part)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: no paint rack, no such paint (0-%d or a name), or no part %s"), *S, ATorquePaintRack::Paints().Num() - 1, *P[2]);
			return true;
		}
		UStaticMeshComponent* Card = Rack->Cards[Index];
		Card->UpdateBounds();
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: at the rack seeing %s"), *S, *W->TestLookAt(Card->Bounds.Origin, Rack->GetActorForwardVector(), 110.0));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(false));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: at the bike seeing %s"), *S, *AimBike(Part));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(false));
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s"), *S, *Looks(P[2]));
		return true;
	}
	if (Verb == TEXT("look") && P.Num() >= 2)
	{
		FString Seen;
		if (P[1] == TEXT("bike") && Bike)
			Seen = W->TestLookAt(Bike->GetActorLocation() + FVector(0, 0, 60), FVector(1.0, 1.4, 0.0), 260.0);
		else if (P[1] == TEXT("bench") && Bench && Bike)
			Seen = W->TestLookAt(Bench->GetActorLocation(), (Bike->GetActorLocation() - Bench->GetActorLocation()), 220.0);
		else if (P[1] == TEXT("door") && Door)
			Seen = W->TestLookAt(Door->GetActorLocation() + FVector(0, 0, 150), FVector(1.0, -1.0, 0.0), 450.0);   // from inside, clear of the bike
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: seeing %s"), *S, *Seen);
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("Torque test: don't know the step '%s'"), *S);
	return true;
}
