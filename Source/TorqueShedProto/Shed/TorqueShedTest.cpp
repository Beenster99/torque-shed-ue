#include "Shed/TorqueShedTest.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorquePartsBench.h"
#include "Shed/TorqueShedBike.h"
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
	auto AimBike = [W, Bike](UStaticMeshComponent* C) -> FString
	{
		const FVector At = C->Bounds.Origin;
		const FVector Right = Bike->GetActorRightVector();
		const double Sign = FVector::DotProduct(At - Bike->GetActorLocation(), Right) < 0.0 ? -1.0 : 1.0;
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
		const int32 Most = P.Num() >= 3 && B ? TorqueFit::Options(B->Get().Bike, Slot).Num() : 1;
		for (int32 I = 0; I < Most; ++I)
		{
			if (P.Num() >= 3 && B && B->FittedIn(Slot) == FName(*P[2])) break;
			UE_LOG(LogTemp, Log, TEXT("Torque test: %s"), *W->TestPress(false));
			// The bike has been redrawn: look again at the slot's new part.
			if (UStaticMeshComponent* Again = Biggest(Bike->Parts, [Slot](const UStaticMeshComponent* X) { return TorqueShedModel::SlotTag(X) == Slot; }))
				AimBike(Again);
		}
		if (P.Num() >= 3 && B)
			UE_LOG(LogTemp, Log, TEXT("Torque test: %s: %s now has %s (%s)"), *S, *Slot.ToString(), *B->FittedIn(Slot).ToString(),
				B->FittedIn(Slot) == FName(*P[2]) ? TEXT("as asked") : TEXT("NOT what was asked"));
		return true;
	}
	if (Verb == TEXT("bench") && P.Num() >= 3)
	{
		const FName Slot(*P[1]), Part(*P[2]);
		UStaticMeshComponent* C = Bench ? Biggest(Bench->Items, [Slot, Part](const UStaticMeshComponent* X)
			{ return TorqueShedModel::SlotTag(X) == Slot && TorqueShedModel::OptionTag(X) == Part; }) : nullptr;
		if (!C) { UE_LOG(LogTemp, Warning, TEXT("Torque test: %s: no such spare on the bench (fitted already, or not imported)"), *S); return true; }
		UE_LOG(LogTemp, Log, TEXT("Torque test: %s: looking at %s"), *S, *AimBench(C));
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
