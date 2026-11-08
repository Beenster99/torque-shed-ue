#include "Shed/TorqueBuildSubsystem.h"

#include "Shed/TorqueShedSave.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
const FTorqueShedBike& NoBike()
{
	static const FTorqueShedBike Empty;
	return Empty;
}

FString RemovedToString(const FTorqueRemoved& R) { return R.Slot.ToString() + TEXT(":") + R.Part.ToString(); }

bool RemovedFromString(const FString& Text, FTorqueRemoved& Out)
{
	FString A, B;
	if (!Text.Split(TEXT(":"), &A, &B)) return false;
	Out.Slot = FName(*A);
	Out.Part = FName(*B);
	return true;
}
} // namespace

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
		// A test starts from a new shed every time, and never touches the player's slot.
		if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0)) UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
		UE_LOG(LogTemp, Log, TEXT("Torque: test run: the shed uses save slot %s, a new shed"), *SaveSlot);
		StartFresh();
	}
	else
		Load();
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

void UTorqueBuildSubsystem::StartFresh()
{
	Bikes.Reset();
	Bikes.SetNum(Bays);
	for (int32 I = 0; I < StartBikes && I < Bays; ++I) Bikes[I].bExists = true;   // FTorqueBuild's default bike
	Mine = 0;
}

void UTorqueBuildSubsystem::Load()
{
	StartFresh();
	bHostCopy = false;
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!S)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: no shed save in %s yet: a new shed"), *SaveSlot);
		return;
	}
	TArray<FTorqueShedSaveBike> Saved = S->Bikes;
	if (S->Version < 2)
	{
		// The one-bike shed: it goes in bay 0, beside a new bike in bay 1.
		FTorqueShedSaveBike Old;
		Old.bExists = true;
		Old.Bike = S->Bike;
		Old.Fitted = S->Fitted;
		Old.Dirt = S->Dirt;
		Old.Paint = S->Paint;
		Old.Removed = S->Removed;
		Saved.Reset();
		Saved.Add(Old);
		for (int32 I = 1; I < StartBikes; ++I) { FTorqueShedSaveBike New; New.bExists = true; Saved.Add(New); }
	}
	int32 Fitted = 0;
	for (int32 I = 0; I < Bays && I < Saved.Num(); ++I)
	{
		const FTorqueShedSaveBike& From = Saved[I];
		FTorqueShedBike& To = Bikes[I];
		To = FTorqueShedBike();
		To.bExists = From.bExists;
		if (!To.bExists) continue;
		if (!From.Bike.IsNone()) To.Build.Bike = From.Bike;
		// Only what still fits (the parts data may have changed since it was saved).
		for (const TPair<FName, FName>& KV : From.Fitted)
		{
			FText Why;
			if (TorqueFit::CanFit(To.Build, KV.Key, KV.Value, Why)) { To.Build.Fitted.Add(KV.Key, KV.Value); ++Fitted; }
			else UE_LOG(LogTemp, Warning, TEXT("Torque: bay %d: saved %s in %s no longer fits: %s"), I + 1, *KV.Value.ToString(), *KV.Key.ToString(), *Why.ToString());
		}
		To.Dirt = From.Dirt;
		To.Paint = From.Paint;
		To.bRode = From.bRode;
		for (const FString& R : From.Removed)
		{
			FTorqueRemoved Off;
			if (RemovedFromString(R, Off) && To.Removed.Num() < RecentMax) To.Removed.Add(Off);
		}
	}
	Mine = FMath::Clamp(S->MyBike, 0, Bays - 1);
	UE_LOG(LogTemp, Log, TEXT("Torque: shed loaded from %s: %d bikes, %d parts fitted"), *SaveSlot,
		Bikes.FilterByPredicate([](const FTorqueShedBike& B) { return B.bExists; }).Num(), Fitted);
}

void UTorqueBuildSubsystem::Save()
{
	bSaveDue = false;
	if (bHostCopy) return;   // someone else's shed: never over this player's own
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::CreateSaveGameObject(UTorqueShedSave::StaticClass()));
	if (!S) return;
	S->Version = 2;
	S->MyBike = Mine;
	for (const FTorqueShedBike& From : Bikes)
	{
		FTorqueShedSaveBike To;
		To.bExists = From.bExists;
		To.Bike = From.Build.Bike;
		To.Fitted = From.Build.Fitted;
		To.Dirt = From.Dirt;
		To.Paint = From.Paint;
		To.bRode = From.bRode;
		for (const FTorqueRemoved& R : From.Removed) To.Removed.Add(RemovedToString(R));
		S->Bikes.Add(To);
	}
	if (!UGameplayStatics::SaveGameToSlot(S, SaveSlot, 0))
		UE_LOG(LogTemp, Warning, TEXT("Torque: couldn't save the shed to %s"), *SaveSlot);
}

void UTorqueBuildSubsystem::LeaveHostShed()
{
	if (!bHostCopy) return;
	if (SaveSlot == TEXT("ShedTest")) { StartFresh(); bHostCopy = false; }
	else Load();
	UE_LOG(LogTemp, Log, TEXT("Torque: left the host's shed: this player's own shed is back"));
	for (int32 I = 0; I < Bikes.Num(); ++I) OnChanged.Broadcast(I);
}

// --- Reads ---------------------------------------------------------------------------------------

const FTorqueBuild& UTorqueBuildSubsystem::Get(int32 Bike) const
{
	return Bikes.IsValidIndex(Bike) ? Bikes[Bike].Build : NoBike().Build;
}

const FTorqueShedBike& UTorqueBuildSubsystem::Snapshot(int32 Bike) const
{
	return Bikes.IsValidIndex(Bike) ? Bikes[Bike] : NoBike();
}

const TArray<FTorqueRemoved>& UTorqueBuildSubsystem::RecentlyRemoved(int32 Bike) const
{
	return Snapshot(Bike).Removed;
}

FName UTorqueBuildSubsystem::FittedIn(int32 Bike, FName Slot) const
{
	const FTorqueBuild& B = Get(Bike);
	if (const FName* F = B.Fitted.Find(Slot)) return *F;
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(B.Bike, Slot);
	for (const FTorqueFitOption& O : Opts)
		if (O.bStock) return O.PartId;
	return Opts.Num() > 0 ? Opts[0].PartId : NAME_None;
}

FName UTorqueBuildSubsystem::NextOption(int32 Bike, FName Slot) const
{
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Get(Bike).Bike, Slot);
	if (Opts.Num() == 0) return NAME_None;
	const FName Now = FittedIn(Bike, Slot);
	int32 At = 0;
	for (int32 I = 0; I < Opts.Num(); ++I)
		if (Opts[I].PartId == Now) At = I;
	return Opts[(At + 1) % Opts.Num()].PartId;
}

float UTorqueBuildSubsystem::DirtOf(int32 Bike, FName Part) const
{
	if (const float* D = Snapshot(Bike).Dirt.Find(Part)) return *D;
	// Parts that came with the bike start dirty; fitted options (opt_...) come out of the box clean.
	return Part.ToString().StartsWith(TEXT("opt_")) ? 0.0f : StartDirt;
}

bool UTorqueBuildSubsystem::PaintOf(int32 Bike, FName Part, FLinearColor& Out) const
{
	if (const FLinearColor* C = Snapshot(Bike).Paint.Find(Part)) { Out = *C; return true; }
	return false;
}

// --- Actions -------------------------------------------------------------------------------------

bool UTorqueBuildSubsystem::Ask(const FTorqueShedRequest& Request, FText& WhyNot)
{
	if (!IsClient()) return Apply(Request, WhyNot);
	// A client: check what can be checked here, show dirt and paint at once, and ask the host.
	if (Request.Kind != ETorqueShedAct::NewBike && !Exists(Request.Bike))
	{
		WhyNot = FText::FromString(TEXT("There's no bike in that bay."));
		return false;
	}
	if (Request.Kind == ETorqueShedAct::Fit && !TorqueFit::CanFit(Get(Request.Bike), Request.Slot, Request.Part, WhyNot))
		return false;
	if (Request.Kind == ETorqueShedAct::Clean)
	{
		FTorqueShedBike& B = Bikes[Request.Bike];
		B.Dirt.Add(Request.Part, FMath::Clamp(DirtOf(Request.Bike, Request.Part) - Request.Amount, 0.0f, 1.0f));
	}
	else if (Request.Kind == ETorqueShedAct::Paint)
	{
		Bikes[Request.Bike].Paint.Add(Request.Part, Request.Colour);
		OnLooks.Broadcast(Request.Bike);
	}
	if (!Forward.Execute(Request))
	{
		WhyNot = FText::FromString(TEXT("The host didn't get that."));
		return false;
	}
	return true;
}

bool UTorqueBuildSubsystem::Apply(const FTorqueShedRequest& R, FText& WhyNot)
{
	if (R.Kind == ETorqueShedAct::NewBike)
	{
		if (!Bikes.IsValidIndex(R.Bike)) { WhyNot = FText::FromString(TEXT("There's no such bay.")); return false; }
		if (Bikes[R.Bike].bExists) { WhyNot = FText::FromString(TEXT("That bay has a bike in it already.")); return false; }
		if (R.BikeId.IsNone() || TorqueFit::Slots(R.BikeId).Num() == 0)
		{
			WhyNot = FText::FromString(FString::Printf(TEXT("There's no %s to build yet."), *R.BikeId.ToString()));
			return false;
		}
		FTorqueShedBike New;
		New.bExists = true;
		New.Build.Bike = R.BikeId;
		Bikes[R.Bike] = New;
		UE_LOG(LogTemp, Log, TEXT("Torque: bay %d: a new %s"), R.Bike + 1, *R.BikeId.ToString());
		Changed(R.Bike, true);
		return true;
	}
	if (!Exists(R.Bike)) { WhyNot = FText::FromString(TEXT("There's no bike in that bay.")); return false; }
	FTorqueShedBike& B = Bikes[R.Bike];
	switch (R.Kind)
	{
	case ETorqueShedAct::Fit:
	{
		if (!TorqueFit::CanFit(B.Build, R.Slot, R.Part, WhyNot)) return false;
		const FName Off = FittedIn(R.Bike, R.Slot);
		const FName Slot = R.Slot, On = R.Part;
		B.Build.Fitted.Add(Slot, On);
		// What came off goes on the bench, newest first; what went on comes off it.
		B.Removed.RemoveAll([Slot, On, Off](const FTorqueRemoved& X) { return X.Slot == Slot && (X.Part == On || X.Part == Off); });
		if (!Off.IsNone() && Off != On) B.Removed.Insert({Slot, Off}, 0);
		if (B.Removed.Num() > RecentMax) B.Removed.SetNum(RecentMax);
		UE_LOG(LogTemp, Log, TEXT("Torque: bay %d: fitted %s in %s"), R.Bike + 1, *On.ToString(), *Slot.ToString());
		Changed(R.Bike, true);
		return true;
	}
	case ETorqueShedAct::Clean:
	{
		const float Now = DirtOf(R.Bike, R.Part);
		const float After = FMath::Clamp(Now - R.Amount, 0.0f, 1.0f);
		if (After != Now) B.Dirt.Add(R.Part, After);
		Changed(R.Bike, false);
		return true;
	}
	case ETorqueShedAct::Paint:
		B.Paint.Add(R.Part, R.Colour);
		UE_LOG(LogTemp, Log, TEXT("Torque: bay %d: painted %s %s"), R.Bike + 1, *R.Part.ToString(), *R.Colour.ToString());
		Changed(R.Bike, false);
		return true;
	case ETorqueShedAct::ClearBike:
		B = FTorqueShedBike();
		UE_LOG(LogTemp, Log, TEXT("Torque: bay %d emptied"), R.Bike + 1);
		Changed(R.Bike, true);
		return true;
	case ETorqueShedAct::RodeOut:
		B.bRode = true;
		MarkSave();
		if (!bHostCopy) OnHostChange.Broadcast(R.Bike);
		return true;
	default:
		return false;
	}
}

void UTorqueBuildSubsystem::Changed(int32 Bike, bool bParts)
{
	if (bParts) OnChanged.Broadcast(Bike);
	else OnLooks.Broadcast(Bike);
	MarkSave();
	OnHostChange.Broadcast(Bike);
}

bool UTorqueBuildSubsystem::Fit(int32 Bike, FName Slot, FName PartId, FText& WhyNot)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::Fit;
	R.Bike = Bike;
	R.Slot = Slot;
	R.Part = PartId;
	return Ask(R, WhyNot);
}

void UTorqueBuildSubsystem::Clean(int32 Bike, FName Part, float Amount, bool bTell)
{
	if (!Exists(Bike) || Amount <= 0.0f) return;
	if (IsClient())
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Clean;
		R.Bike = Bike;
		R.Part = Part;
		R.Amount = Amount;
		FText Why;
		Ask(R, Why);   // the net layer batches these (washing asks every frame)
		if (bTell) OnLooks.Broadcast(Bike);
		return;
	}
	// The host: change it now and tell once (TellLooks) when washing several parts in a frame.
	const float Now = DirtOf(Bike, Part);
	const float After = FMath::Clamp(Now - Amount, 0.0f, 1.0f);
	if (After == Now) return;
	Bikes[Bike].Dirt.Add(Part, After);
	if (bTell) Changed(Bike, false);
}

void UTorqueBuildSubsystem::TellLooks(int32 Bike)
{
	if (IsClient()) OnLooks.Broadcast(Bike);
	else Changed(Bike, false);
}

void UTorqueBuildSubsystem::SetPaint(int32 Bike, FName Part, const FLinearColor& Colour)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::Paint;
	R.Bike = Bike;
	R.Part = Part;
	R.Colour = Colour;
	FText Why;
	Ask(R, Why);
}

bool UTorqueBuildSubsystem::NewBike(int32 Bike, FName BikeId)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::NewBike;
	R.Bike = Bike;
	R.BikeId = BikeId;
	FText Why;
	if (Ask(R, Why)) return true;
	UE_LOG(LogTemp, Warning, TEXT("Torque: bay %d: no new bike: %s"), Bike + 1, *Why.ToString());
	return false;
}

void UTorqueBuildSubsystem::ClearBike(int32 Bike)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::ClearBike;
	R.Bike = Bike;
	FText Why;
	Ask(R, Why);
}

void UTorqueBuildSubsystem::RodeOut(int32 Bike)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::RodeOut;
	R.Bike = Bike;
	FText Why;
	Ask(R, Why);
}

void UTorqueBuildSubsystem::Arrived(int32 Bike, const TArray<FName>& PartsOnBike)
{
	// The host's: a client's copy comes back from it.
	if (IsClient() || !Exists(Bike) || !Bikes[Bike].bRode) return;
	FTorqueShedBike& B = Bikes[Bike];
	B.bRode = false;
	for (const FName& P : PartsOnBike) B.Dirt.Add(P, FMath::Min(DirtOf(Bike, P) + RideDirt, 1.0f));
	UE_LOG(LogTemp, Log, TEXT("Torque: bay %d back from a ride, %d parts dirtier"), Bike + 1, PartsOnBike.Num());
	Changed(Bike, false);
}

// --- Co-op ---------------------------------------------------------------------------------------

void UTorqueBuildSubsystem::ApplyFromHost(int32 Bike, const FTorqueShedBike& FromHost)
{
	if (Bike < 0 || Bike >= Bays) return;
	if (Bikes.Num() < Bays) Bikes.SetNum(Bays);
	bHostCopy = true;
	FTorqueShedBike& B = Bikes[Bike];
	// Only redraw the parts when they changed; dirt and paint are cheap.
	const bool bParts = B.bExists != FromHost.bExists || B.Build.Bike != FromHost.Build.Bike ||
		!B.Build.Fitted.OrderIndependentCompareEqual(FromHost.Build.Fitted) || B.Removed.Num() != FromHost.Removed.Num();
	B = FromHost;
	if (bParts) OnChanged.Broadcast(Bike);
	else OnLooks.Broadcast(Bike);
}

void UTorqueBuildSubsystem::ReportActivity(const FString& What)
{
	if (What == LastActivity) return;
	LastActivity = What;
	OnActivity.Broadcast(What);
}

void UTorqueBuildSubsystem::SetMyBike(int32 Bike)
{
	if (Bike < 0 || Bike >= Bays || Bike == Mine) return;
	Mine = Bike;
	if (!bHostCopy) MarkSave();
	OnMyBike.Broadcast(Bike);
}

UTorqueBuildSubsystem* UTorqueBuildSubsystem::Of(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UTorqueBuildSubsystem>() : nullptr;
}

int32 UTorqueBuildSubsystem::BikeFor(const UObject* WorldContext)
{
	const UTorqueBuildSubsystem* S = Of(WorldContext);
	if (!S) return 0;
	if (const APawn* P = Cast<APawn>(WorldContext))
		if (S->BikeOfPawn.IsBound())
		{
			const int32 B = S->BikeOfPawn.Execute(P);
			if (B >= 0) return B;
		}
	return S->Mine;
}

const FTorqueBuild& UTorqueBuildSubsystem::BuildFor(const UObject* WorldContext)
{
	const UTorqueBuildSubsystem* S = Of(WorldContext);
	return S ? S->Get(BikeFor(WorldContext)) : NoBike().Build;
}
