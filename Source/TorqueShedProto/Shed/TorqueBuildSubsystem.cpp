#include "Shed/TorqueBuildSubsystem.h"

#include "Shed/TorqueShedSave.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UTorqueBuildSubsystem::IsTestRun()
{
	// Any -Torque switch but the player's own (name, network, menu, screen messages) is a test run.
	const TCHAR* Cmd = FCommandLine::Get();
	FString Token;
	while (FParse::Token(Cmd, Token, false))
	{
		if (!Token.StartsWith(TEXT("-Torque"))) continue;
		if (Token.StartsWith(TEXT("-TorqueName")) || Token.StartsWith(TEXT("-TorqueNet")) ||
			Token.StartsWith(TEXT("-TorqueMenu")) || Token.StartsWith(TEXT("-TorqueScreenMessages")))
			continue;
		return true;
	}
	return false;
}

void UTorqueBuildSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SaveSlot = IsTestRun() ? TEXT("ShedTest") : TEXT("Shed");
	if (SaveSlot == TEXT("ShedTest"))
	{
		// A test starts from the stock bike every time, and never touches the player's slot.
		if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0)) UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
		UE_LOG(LogTemp, Log, TEXT("Torque: test run: the shed uses save slot %s, reset to stock"), *SaveSlot);
	}
	else
		Load();
	OnChanged.AddUObject(this, &UTorqueBuildSubsystem::MarkSave);
	OnLooks.AddUObject(this, &UTorqueBuildSubsystem::MarkSave);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTorqueBuildSubsystem::TickSave), 0.5f);
}

void UTorqueBuildSubsystem::Deinitialize()
{
	if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	if (bSaveDue) Save();
	Super::Deinitialize();
}

bool UTorqueBuildSubsystem::TickSave(float Dt)
{
	if (!bSaveDue) return true;
	SaveIn -= Dt;
	if (SaveIn <= 0.0) Save();   // washing changes dirt every frame: one save after it settles
	return true;
}

void UTorqueBuildSubsystem::Load()
{
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!S)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: no shed save in %s yet: the stock bike"), *SaveSlot);
		return;
	}
	if (!S->Bike.IsNone() && S->Bike != Build.Bike)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: shed save %s is for %s, not %s: ignored"), *SaveSlot, *S->Bike.ToString(), *Build.Bike.ToString());
		return;
	}
	// Only what still fits (the parts data may have changed since it was saved).
	for (const TPair<FName, FName>& KV : S->Fitted)
	{
		FText Why;
		if (TorqueFit::CanFit(Build, KV.Key, KV.Value, Why)) Build.Fitted.Add(KV.Key, KV.Value);
		else UE_LOG(LogTemp, Warning, TEXT("Torque: saved %s in %s no longer fits: %s"), *KV.Value.ToString(), *KV.Key.ToString(), *Why.ToString());
	}
	Dirt = S->Dirt;
	Paint = S->Paint;
	for (const FString& R : S->Removed)
	{
		FString A, B;
		if (R.Split(TEXT(":"), &A, &B) && Removed.Num() < RecentMax) Removed.Add({FName(*A), FName(*B)});
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: shed loaded from %s: %d parts fitted, %d painted"), *SaveSlot, Build.Fitted.Num(), Paint.Num());
}

void UTorqueBuildSubsystem::Save()
{
	bSaveDue = false;
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::CreateSaveGameObject(UTorqueShedSave::StaticClass()));
	if (!S) return;
	S->Bike = Build.Bike;
	S->Fitted = Build.Fitted;
	S->Dirt = Dirt;
	S->Paint = Paint;
	for (const FTorqueRemoved& R : Removed) S->Removed.Add(R.Slot.ToString() + TEXT(":") + R.Part.ToString());
	if (!UGameplayStatics::SaveGameToSlot(S, SaveSlot, 0))
		UE_LOG(LogTemp, Warning, TEXT("Torque: couldn't save the shed to %s"), *SaveSlot);
}

FName UTorqueBuildSubsystem::FittedIn(FName Slot) const
{
	if (const FName* F = Build.Fitted.Find(Slot)) return *F;
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Build.Bike, Slot);
	for (const FTorqueFitOption& O : Opts)
		if (O.bStock) return O.PartId;
	return Opts.Num() > 0 ? Opts[0].PartId : NAME_None;
}

bool UTorqueBuildSubsystem::Fit(FName Slot, FName PartId, FText& WhyNot)
{
	if (!TorqueFit::CanFit(Build, Slot, PartId, WhyNot)) return false;
	const FName Off = FittedIn(Slot);
	Build.Fitted.Add(Slot, PartId);
	// What came off goes on the bench, newest first; what went on comes off it.
	Removed.RemoveAll([Slot, PartId, Off](const FTorqueRemoved& R) { return R.Slot == Slot && (R.Part == PartId || R.Part == Off); });
	if (!Off.IsNone() && Off != PartId) Removed.Insert({Slot, Off}, 0);
	if (Removed.Num() > RecentMax) Removed.SetNum(RecentMax);
	UE_LOG(LogTemp, Log, TEXT("Torque: fitted %s in %s"), *PartId.ToString(), *Slot.ToString());
	OnChanged.Broadcast();
	return true;
}

FName UTorqueBuildSubsystem::NextOption(FName Slot) const
{
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Build.Bike, Slot);
	if (Opts.Num() == 0) return NAME_None;
	const FName Now = FittedIn(Slot);
	int32 At = 0;
	for (int32 I = 0; I < Opts.Num(); ++I)
		if (Opts[I].PartId == Now) At = I;
	return Opts[(At + 1) % Opts.Num()].PartId;
}

UTorqueBuildSubsystem* UTorqueBuildSubsystem::Of(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UTorqueBuildSubsystem>() : nullptr;
}

const FTorqueBuild& UTorqueBuildSubsystem::BuildFor(const UObject* WorldContext)
{
	static const FTorqueBuild Default;
	const UTorqueBuildSubsystem* S = Of(WorldContext);
	return S ? S->Build : Default;
}

float UTorqueBuildSubsystem::DirtOf(FName Part) const
{
	if (const float* D = Dirt.Find(Part)) return *D;
	// Parts that came with the bike start dirty; fitted options (opt_...) come out of the box clean.
	return Part.ToString().StartsWith(TEXT("opt_")) ? 0.0f : StartDirt;
}

void UTorqueBuildSubsystem::Clean(FName Part, float Amount, bool bTell)
{
	const float Now = DirtOf(Part);
	const float After = FMath::Clamp(Now - Amount, 0.0f, 1.0f);
	if (After == Now) return;
	Dirt.Add(Part, After);
	if (bTell) OnLooks.Broadcast();
}

bool UTorqueBuildSubsystem::PaintOf(FName Part, FLinearColor& Out) const
{
	if (const FLinearColor* C = Paint.Find(Part)) { Out = *C; return true; }
	return false;
}

void UTorqueBuildSubsystem::SetPaint(FName Part, const FLinearColor& Colour)
{
	Paint.Add(Part, Colour);
	UE_LOG(LogTemp, Log, TEXT("Torque: painted %s %s"), *Part.ToString(), *Colour.ToString());
	OnLooks.Broadcast();
}

void UTorqueBuildSubsystem::Arrived(const TArray<FName>& PartsOnBike)
{
	if (!bRode) return;
	bRode = false;
	for (const FName& P : PartsOnBike) Dirt.Add(P, FMath::Min(DirtOf(P) + RideDirt, 1.0f));
	UE_LOG(LogTemp, Log, TEXT("Torque: back from a ride, %d parts dirtier"), PartsOnBike.Num());
	OnLooks.Broadcast();
}
