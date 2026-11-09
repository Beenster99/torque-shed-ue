#include "Shed/TorqueBuildSubsystem.h"

#include "Fit/BikeLayout.h"
#include "Fit/BikeShop.h"
#include "Fit/BikeRideSpec.h"
#include "Fit/BikeTune.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedRadio.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedSave.h"
#include "Coop/TorquePlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// (Named, not anonymous: in a unity build an anonymous namespace is shared with the other files.)
namespace TorqueBuildImpl
{
const FTorqueShedBike& NoBike()
{
	static const FTorqueShedBike Empty;
	return Empty;
}

const FTorqueBuild& NoBuild()
{
	static const FTorqueBuild Stock;
	return Stock;
}

// shed_state BIKE_NAMES: what a project bike is called when it comes in.
const TCHAR* const BikeNames[] = {TEXT("Barn Find"), TEXT("Rusty Ranger"), TEXT("Basket Case"), TEXT("Old Faithful"),
	TEXT("The Widowmaker"), TEXT("Rattler"), TEXT("Hedge Hopper"), TEXT("Midnight Special"), TEXT("Scrapyard Dog"),
	TEXT("Grandad's Pride"), TEXT("Mud Puppy"), TEXT("Iron Maiden")};

FString Lower(const FString& S) { return S.ToLower(); }

bool SpotIs(FName Spot, ETorquePlace Kind)
{
	const FTorquePlace* P = TorqueShedLayout::FindPlace(Spot);
	return P && P->Kind == Kind;
}

// How a sprayed coat came out (shed_state _job_word).
FString JobWord(float Q, float Overdone = 0.0f)
{
	FString Word = Q > 0.95f ? FString(TEXT("a perfect coat")) : (Q > 0.85f ? FString(TEXT("a good coat")) : (Q > 0.7f ? FString(TEXT("a bit patchy")) : FString(TEXT("patchy"))));
	if (Overdone > 0.15f) Word += TEXT(", with runs");
	else if (Overdone > 0.04f) Word += TEXT(", a few runs");
	return Word;
}

// How well a coat goes on this part: as the spray game scored it (the request's Amount, from
// TorqueSprayJob: coverage less runs and dry spray), or a steady hand coat when none came with it
// (UTorqueBuildSubsystem::HandCoat: a test's act); better on a hook in the spray bay, where no
// dust settles in it.
float SprayQ(const FTorqueItem& It, const FTorqueShedRequest& R)
{
	const bool bInBooth = It.At == ETorqueAt::Spot && SpotIs(It.Spot, ETorquePlace::Hook);
	const float Hand = R.Amount > 0.0f ? FMath::Clamp(R.Amount, 0.0f, 1.0f) : UTorqueBuildSubsystem::HandCoat;
	return FMath::Clamp(Hand + (bInBooth ? UTorqueBuildSubsystem::BoothFinish : 0.0f), 0.0f, 1.0f);
}

bool IsSpot(FName Spot)
{
	return SpotIs(Spot, ETorquePlace::Bench) || SpotIs(Spot, ETorquePlace::Repair) || SpotIs(Spot, ETorquePlace::Washer) ||
		SpotIs(Spot, ETorquePlace::Changer) || SpotIs(Spot, ETorquePlace::Stand) || SpotIs(Spot, ETorquePlace::Hook);
}

const FName& EngineKind() { static const FName N(TEXT("engine")); return N; }
const FName& FrameSlot() { static const FName N(TEXT("frame")); return N; }

// A slot, or a stock model part's name (the ridden bike's parts carry only that).
FName AsSlot(FName SlotOrPart)
{
	if (TorqueShedParts::Slots().Contains(SlotOrPart)) return SlotOrPart;
	return TorqueShedParts::SlotOfModelPart(SlotOrPart);
}

// Engine work needs the engine on the stand, except for service parts in the frame.
FString EngineAccess(const TMap<int32, FTorqueItem>& Items, const FTorqueShedRequest& R, FName Slot)
{
	if (!TorqueShedParts::IsEngineSlot(Slot)) return FString();
	if (R.Engine == 0)
	{
		if (TorqueShedParts::IsServiceSlot(Slot)) return FString();
		return FString::Printf(TEXT("Take the engine out and put it on the engine stand to get at the %s."), *TorqueShedParts::SlotName(Slot).ToLower());
	}
	const FTorqueItem* Block = Items.Find(R.Engine);
	if (!Block || !(Block->At == ETorqueAt::Spot && SpotIs(Block->Spot, ETorquePlace::Stand))) return TEXT("Put the engine on the engine stand first.");
	return FString();
}
std::string Std(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

FString Ue(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

// A slot or part id as fitting knows it. FNames ignore case and keep the first spelling the engine
// saw ("Engine", the module's name, for the slot "engine"), so their text is lowered for it.
std::string Id(FName Name) { return Std(Name.ToString().ToLower()); }

// Fitting's layout solver (Fit/BikeLayout: BikeRig in the Godot game), shared with the drawing.
const TorqueFit::BikeLayout* FitLayout() { return TorqueShedCatalogue::Layout(); }

// The slots Torque Shed asks the layout about when a part goes on (shed_state install).
bool IsFitSlot(FName Slot)
{
	const FString S = Slot.ToString();
	return S == TEXT("frame") || S == TEXT("engine") || S == TEXT("tank") || S == TEXT("fork");
}
// "exhaust, chain and 2 more" (shed_state _slot_list).
FString SlotList(const TArray<FName>& Slots, int32 Most = 4)
{
	FString S;
	for (int32 I = 0; I < Slots.Num() && I < Most; ++I)
		S += (I ? TEXT(", ") : TEXT("")) + TorqueShedParts::SlotName(Slots[I]).ToLower();
	if (Slots.Num() > Most) S += FString::Printf(TEXT(" and %d more"), Slots.Num() - Most);
	return S;
}

// A dyno run on disk, and back.
FTorqueShedSaveDynoRun ToSaveRun(const FTorqueDynoRun& R)
{
	FTorqueShedSaveDynoRun S;
	S.N = R.N; S.Rpm = R.Rpm; S.Nm = R.Nm; S.Hp = R.Hp;
	S.PeakHp = R.PeakHp; S.PeakHpRpm = R.PeakHpRpm; S.PeakNm = R.PeakNm; S.PeakNmRpm = R.PeakNmRpm;
	S.Redline = R.Redline; S.IdleRpm = R.IdleRpm; S.Gears = R.Gears; S.Heat = R.Heat; S.TempC = R.TempC;
	S.bFan = R.bFan; S.Mass = R.Mass;
	return S;
}

FTorqueDynoRun FromSaveRun(const FTorqueShedSaveDynoRun& S)
{
	FTorqueDynoRun R;
	R.N = S.N; R.Rpm = S.Rpm; R.Nm = S.Nm; R.Hp = S.Hp;
	R.PeakHp = S.PeakHp; R.PeakHpRpm = S.PeakHpRpm; R.PeakNm = S.PeakNm; R.PeakNmRpm = S.PeakNmRpm;
	R.Redline = S.Redline; R.IdleRpm = S.IdleRpm; R.Gears = S.Gears; R.Heat = S.Heat; R.TempC = S.TempC;
	R.bFan = S.bFan; R.Mass = S.Mass;
	R.Start = -1.0e9;   // long since played
	return R;
}

// A dyno run in the shed's bytes (with the time it started, so a joiner sees it part-way).
bool SerializeRun(FArchive& Ar, FTorqueDynoRun& R)
{
	uint8 bFan = R.bFan ? 1 : 0;
	Ar << R.N << R.Start << R.Rpm << R.Nm << R.Hp << R.PeakHp << R.PeakHpRpm << R.PeakNm << R.PeakNmRpm
		<< R.Redline << R.IdleRpm << R.Gears << R.Heat << R.TempC << bFan << R.Mass;
	R.bFan = bFan != 0;
	return !Ar.IsError() && R.Rpm.Num() <= 256 && R.Nm.Num() <= 256 && R.Hp.Num() <= 256;
}
} // namespace TorqueBuildImpl

namespace TBI = TorqueBuildImpl;

// --- Setting up, saving ------------------------------------------------------------------------

bool UTorqueBuildSubsystem::IsTestRun()
{
	// Any -Torque switch but the player's own (name, network, menu, screen messages) is a test run.
	const TCHAR* Cmd = FCommandLine::Get();
	FString Token;
	while (FParse::Token(Cmd, Token, false))
	{
		if (!Token.StartsWith(TEXT("-Torque"))) continue;
		if (Token.StartsWith(TEXT("-TorqueName")) || Token.StartsWith(TEXT("-TorqueNet")) ||
			Token.StartsWith(TEXT("-TorqueMenu")) || Token.StartsWith(TEXT("-TorqueScreenMessages")) ||
			Token.StartsWith(TEXT("-TorqueTitleTest")))   // its own slots (SlotBase), never the player's
			continue;
		return true;
	}
	return false;
}

void UTorqueBuildSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SaveSlot = SlotBase();
	if (IsTestRun()) SaveSlot = TEXT("ShedTest");
	else
	{
		// The game saved last (the title screen's Load Game picks another).
		const TArray<FSavedGame> Games = SavedGames();
		if (Games.Num() > 0) SaveSlot = Games[0].Slot;
	}
	// A test run's shed comes out the same every time.
	Rng.Initialize(SaveSlot == TEXT("ShedTest") ? 12345 : (int32)(FDateTime::Now().GetTicks() & 0x7fffffff));
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

bool UTorqueBuildSubsystem::TickSave(float FrameDt)
{
	// The time since this last ran, by the clock: a ticker on a delay is handed the frame's time,
	// not the half second between its runs, so the shaker took many times its 20 s.
	const double Now = FPlatformTime::Seconds();
	const float Dt = LastTickAt > 0.0 ? (float)FMath::Clamp(Now - LastTickAt, 0.0, 2.0) : FrameDt;
	LastTickAt = Now;
	// The host's clock for the mixing bay's shaker.
	if (!IsClient() && Paint.Tick(Dt))
	{
		MarkSave();
		OnItems.Broadcast();
		OnHostChange.Broadcast();
	}
	if (!bSaveDue) return true;
	SaveIn -= Dt;
	if (SaveIn <= 0.0) Save();   // washing changes dirt many times a second: one save after it settles
	return true;
}

FString UTorqueBuildSubsystem::SlotBase()
{
	FString Steps;
	return FParse::Value(FCommandLine::Get(), TEXT("TorqueTitleTest="), Steps) ? FString(TEXT("TitleTest")) : FString(TEXT("Shed"));
}

int32 UTorqueBuildSubsystem::GameNumber(const FString& Slot)
{
	const FString Base = SlotBase();
	if (Slot == Base) return 1;
	if (!Slot.StartsWith(Base + TEXT("_"), ESearchCase::CaseSensitive)) return 0;
	const FString Num = Slot.Mid(Base.Len() + 1);
	if (Num.IsEmpty() || !Num.IsNumeric() || Num.Contains(TEXT("."))) return 0;
	return FCString::Atoi(*Num);
}

TArray<UTorqueBuildSubsystem::FSavedGame> UTorqueBuildSubsystem::SavedGames()
{
	TArray<FSavedGame> Out;
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.sav")), true, false);
	for (const FString& File : Files)
	{
		FSavedGame G;
		G.Slot = FPaths::GetBaseFilename(File);
		G.Number = GameNumber(G.Slot);
		if (G.Number <= 0) continue;   // ShedTest and anything else that isn't a game
		G.Saved = IFileManager::Get().GetTimeStamp(*FPaths::Combine(Dir, File));
		if (const UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::LoadGameFromSlot(G.Slot, 0)))
		{
			G.BikeCount = 0;
			for (const FTorqueShedSaveBike& B : S->Bikes) G.BikeCount += B.bExists ? 1 : 0;
		}
		Out.Add(G);
	}
	Out.Sort([](const FSavedGame& A, const FSavedGame& B) { return A.Saved > B.Saved; });
	return Out;
}

FString UTorqueBuildSubsystem::NewGameSlot()
{
	TSet<int32> Used;
	for (const FSavedGame& G : SavedGames()) Used.Add(G.Number);
	int32 N = 1;
	while (Used.Contains(N)) ++N;
	return N == 1 ? SlotBase() : FString::Printf(TEXT("%s_%d"), *SlotBase(), N);
}

void UTorqueBuildSubsystem::UseGame(const FString& Slot, bool bNew)
{
	if (IsTestRun() || Slot.IsEmpty()) return;
	if (bSaveDue) Save();   // the game in use keeps what was done to it
	SaveSlot = Slot;
	Bikes.Reset();
	Items.Reset();
	if (bNew)
	{
		StartFresh();
		Save();
	}
	else
		Load();
	bSaveDue = false;
	Builds.SetNum(Bays);
	for (int32 I = 0; I < Bays; ++I) RebuildBuild(I);
	for (int32 I = 0; I < Bays; ++I) OnChanged.Broadcast(I);
	OnItems.Broadcast();
	OnHostChange.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("Torque: playing game %d (slot %s, %s)"), GameNumber(Slot), *Slot, bNew ? TEXT("new") : TEXT("loaded"));
}

bool UTorqueBuildSubsystem::DeleteGame(const FString& Slot)
{
	if (GameNumber(Slot) <= 0 || !UGameplayStatics::DoesSaveGameExist(Slot, 0)) return false;
	const bool bInUse = Slot == SaveSlot;
	if (bInUse) bSaveDue = false;   // never written back after it's gone
	const bool bOk = UGameplayStatics::DeleteGameInSlot(Slot, 0);
	UE_LOG(LogTemp, Log, TEXT("Torque: deleted game %d (slot %s): %s"), GameNumber(Slot), *Slot, bOk ? TEXT("done") : TEXT("FAILED"));
	if (bOk && bInUse)
	{
		const TArray<FSavedGame> Left = SavedGames();
		if (Left.Num() > 0) UseGame(Left[0].Slot, false);
		else
		{
			// Nothing left: a new shed in game 1's slot, saved only once something is done in it.
			SaveSlot = SlotBase();
			Bikes.Reset();
			Items.Reset();
			StartFresh();
			bSaveDue = false;
			for (int32 I = 0; I < Bays; ++I) OnChanged.Broadcast(I);
			OnItems.Broadcast();
			OnHostChange.Broadcast();
		}
	}
	return bOk;
}

void UTorqueBuildSubsystem::StartFresh()
{
	Bikes.Reset();
	Bikes.SetNum(Bays);
	Items.Reset();
	Paint = FTorquePaintBay::Starter();
	bRadioOn = false;
	RadioTuned = 0;
	LiftsUp = 0;
	bShedLights = true;   // [kit]
	bDoorUp = true;
	StandXf = StandHome();
	StandPusher = 0;
	ExtinguisherBy = 0;
	bFoaming = false;
	NextId = 1;
	Mine = 0;
	bHostCopy = false;
	// The project bikes the shed starts with (shed_state's demo bikes: a couple to get stuck into).
	for (int32 I = 0; I < StartBikes && I < Bays; ++I) NewProjectBike(I, Rng.FRandRange(0.35f, 0.6f));
	Builds.SetNum(Bays);
	for (int32 I = 0; I < Bays; ++I) RebuildBuild(I);
	UE_LOG(LogTemp, Log, TEXT("Torque: a new shed: %d project bikes, %d parts"), StartBikes, Items.Num());
}

void UTorqueBuildSubsystem::ToSave(UTorqueShedSave& S) const
{
	S.Version = UTorqueShedSave::CurrentVersion;
	S.NextId = NextId;
	S.MyBike = Mine;
	S.Bikes.Reset();
	for (const FTorqueShedBike& B : Bikes)
	{
		FTorqueShedSaveBike To;
		To.bExists = B.bExists;
		To.Id = B.Id;
		To.Name = B.Name;
		To.Model = B.Model;
		To.Slots = B.Slots;
		To.Bolted = B.Bolted;
		To.bRode = B.bRode;
		To.FabOwed = B.FabOwed;
		To.FabDone = B.FabDone;
		To.bAway = B.bAway;
		To.Xf = B.Xf;
		To.DynoN = B.DynoN;
		for (const FTorqueDynoRun& Run : B.DynoRuns) To.DynoRuns.Add(TBI::ToSaveRun(Run));
		To.Geo = B.Geo;
		To.Tune = B.Tune;
		S.Bikes.Add(To);
	}
	S.DynoStraps.Reset();
	for (bool B : Dyno.Straps) S.DynoStraps.Add(B);
	S.bDynoFan = Dyno.bFan;
	S.bDynoLights = Dyno.bLights;
	S.DynoHeat = Dyno.Heat;
	S.bRadioOn = bRadioOn;
	S.RadioStation = RadioTuned;
	S.LiftsUp = LiftsUp;
	S.bShedLights = bShedLights;   // [kit]
	S.bDoorUp = bDoorUp;
	S.bStandMoved = true;
	S.StandAt = StandXf.GetLocation();
	S.StandYaw = (float)StandXf.Rotator().Yaw;
	{
		// The paint mixing bay, as bytes (FTorquePaintBay::Serialize).
		S.PaintBay.Reset();
		FMemoryWriter PaintAr(S.PaintBay);
		const_cast<FTorquePaintBay&>(Paint).Serialize(PaintAr, false);
	}
	S.Items.Reset();
	for (const TPair<int32, FTorqueItem>& KV : Items)
	{
		const FTorqueItem& It = KV.Value;
		FTorqueShedSaveItem To;
		To.Id = It.Id;
		To.Part = It.Part;
		To.Kind = It.Kind;
		To.Rust = It.Rust;
		To.Dirt = It.Dirt;
		To.Wear = It.Wear;
		To.Colour = It.Colour;
		To.Finish = It.Finish;
		To.bBoxed = It.bBoxed;
		To.At = (uint8)It.At;
		To.Ref = It.Ref;
		To.Spot = It.Spot;
		To.Place = It.Place;
		To.Slots = It.Slots;
		To.Bolted = It.Bolted;
		To.Tyre = It.Tyre;
		To.PaintQ = It.PaintQ;
		To.Primer = It.Primer;
		To.bPrimerTop = It.bPrimerTop;
		To.Coat = It.Coat;
		To.CoatQ = It.CoatQ;
		To.MixCode = It.MixCode;
		To.MixName = It.MixName;
		To.MixEffect = It.MixEffect;
		To.MixCoats = It.MixCoats;
		To.MixUnder = It.MixUnder;
		To.Flat = It.Flat;
		To.OrderedBy = It.OrderedBy;
		To.OrderedTint = It.OrderedTint;
		To.Tape = It.Tape;
		To.Mask = It.Mask;
		To.MaskColour = It.MaskColour;
		To.MaskMetal = It.MaskMetal;
		To.MaskRough = It.MaskRough;
		To.Badge = It.Badge;
		To.Tune = It.Tune;
		To.bLightened = It.bLightened;
		S.Items.Add(To);
	}
}

bool UTorqueBuildSubsystem::FromSave(const UTorqueShedSave& S)
{
	if (S.Version != UTorqueShedSave::CurrentVersion) return false;
	Bikes.Reset();
	Bikes.SetNum(Bays);
	for (int32 I = 0; I < Bays && I < S.Bikes.Num(); ++I)
	{
		const FTorqueShedSaveBike& From = S.Bikes[I];
		FTorqueShedBike& To = Bikes[I];
		To.bExists = From.bExists;
		To.Id = From.Id;
		To.Name = From.Name;
		To.Model = From.Model.IsNone() ? FName(TorqueShedParts::StockModel) : From.Model;
		To.Slots = From.Slots;
		To.Bolted = From.Bolted;
		To.bRode = From.bRode;
		To.FabOwed = From.FabOwed;
		To.FabDone = From.FabDone;
		To.bAway = From.bAway;
		To.Xf = From.Xf;
		To.Pusher = 0;   // nobody's wheeling it when the shed opens
		To.DynoN = From.DynoN;
		for (const FTorqueShedSaveDynoRun& Run : From.DynoRuns) To.DynoRuns.Add(TBI::FromSaveRun(Run));
		To.Geo = TorqueGeometryMode::Clean(From.Geo);
		To.Tune = From.Tune;
	}
	Dyno = FTorqueDynoRoom();
	for (int32 I = 0; I < 4 && I < S.DynoStraps.Num(); ++I) Dyno.Straps[I] = S.DynoStraps[I];
	Dyno.bFan = S.bDynoFan;
	Dyno.bLights = S.bDynoLights;
	Dyno.Heat = S.DynoHeat;
	bRadioOn = S.bRadioOn;
	RadioTuned = FMath::Clamp(S.RadioStation, 0, FMath::Max(0, TorqueShedRadio::Stations().Num() - 1));
	LiftsUp = 0;
	for (int32 L = 0; L < 31; ++L)
		if (IsLift(L) && (S.LiftsUp & (1 << L)) != 0) LiftsUp |= 1 << L;
	bShedLights = S.bShedLights;   // [kit]
	bDoorUp = S.bDoorUp;
	StandXf = S.bStandMoved && TorqueShedLayout::OnSite(S.StandAt) ? FTransform(FRotator(0.0, S.StandYaw, 0.0), FVector(S.StandAt.X, S.StandAt.Y, 0.0)) : StandHome();
	StandPusher = 0;
	ExtinguisherBy = 0;
	bFoaming = false;
	Paint = FTorquePaintBay::Starter();
	if (S.PaintBay.Num() > 0)
	{
		FTorquePaintBay Saved;
		FMemoryReader PaintAr(S.PaintBay);
		if (Saved.Serialize(PaintAr, false)) Paint = Saved;
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: the paint mixing bay's save didn't read: a new bay"));
		}
	}
	Paint.AfterLoad();
	Items.Reset();
	for (const FTorqueShedSaveItem& From : S.Items)
	{
		FTorqueItem It;
		It.Id = From.Id;
		It.Part = From.Part;
		It.Kind = From.Kind;
		It.Rust = From.Rust;
		It.Dirt = From.Dirt;
		It.Wear = From.Wear;
		It.Colour = From.Colour;
		It.Finish = From.Finish;
		It.bBoxed = From.bBoxed;
		It.At = (ETorqueAt)FMath::Min<uint8>(From.At, (uint8)ETorqueAt::Wheel);
		It.Ref = From.Ref;
		It.Spot = From.Spot;
		It.Place = From.Place;
		It.Slots = From.Slots;
		It.Bolted = From.Bolted;
		It.Tyre = From.Tyre;
		It.PaintQ = From.PaintQ;
		It.Primer = From.Primer;
		It.bPrimerTop = From.bPrimerTop;
		It.Coat = From.Coat;
		It.CoatQ = From.CoatQ;
		It.MixCode = From.MixCode;
		It.MixName = From.MixName;
		It.MixEffect = From.MixEffect;
		It.MixCoats = From.MixCoats;
		It.MixUnder = From.MixUnder;
		It.Flat = From.Flat;
		It.OrderedBy = From.OrderedBy;
		It.OrderedTint = From.OrderedTint;
		It.Tape = FMath::Clamp(From.Tape, 0, TorquePaintMix::TapePatterns);
		It.Mask = FMath::Clamp(From.Mask, 0, TorquePaintMix::TapePatterns);
		It.MaskColour = From.MaskColour;
		It.MaskMetal = From.MaskMetal;
		It.MaskRough = From.MaskRough;
		It.Badge = From.Badge;
		It.Tune = From.Tune;
		It.bLightened = From.bLightened;
		Items.Add(It.Id, It);
	}
	NextId = FMath::Max(S.NextId, 1);
	for (const TPair<int32, FTorqueItem>& KV : Items) NextId = FMath::Max(NextId, KV.Key + 1);
	Mine = FMath::Clamp(S.MyBike, 0, Bays - 1);
	Builds.SetNum(Bays);
	for (int32 I = 0; I < Bays; ++I) RebuildBuild(I);
	return true;
}

void UTorqueBuildSubsystem::Load()
{
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!S || !FromSave(*S))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: no item shed saved in %s yet: a new shed"), *SaveSlot);
		StartFresh();
		return;
	}
	bHostCopy = false;
	// Nobody is holding anything when the shed opens: what was in someone's hands is on the floor.
	for (TPair<int32, FTorqueItem>& KV : Items)
		if (KV.Value.At == ETorqueAt::Held)
		{
			KV.Value.At = ETorqueAt::Floor;
			KV.Value.Place = FTransform(TorqueShedLayout::DropAt() + FVector(Rng.FRandRange(-60.0f, 60.0f), Rng.FRandRange(-40.0f, 40.0f), 0.0));
		}
	if (DropSpeedTwins() > 0)
	{
		Builds.SetNum(Bays);
		for (int32 I = 0; I < Bays; ++I) RebuildBuild(I);
		MarkSave();
	}
	int32 Count = 0;
	for (const FTorqueShedBike& B : Bikes) Count += B.bExists ? 1 : 0;
	UE_LOG(LogTemp, Log, TEXT("Torque: shed loaded from %s: %d bikes, %d parts"), *SaveSlot, Count, Items.Num());
}

void UTorqueBuildSubsystem::Save()
{
	bSaveDue = false;
	if (bHostCopy) return;   // someone else's shed: never over this player's own
	UTorqueShedSave* S = Cast<UTorqueShedSave>(UGameplayStatics::CreateSaveGameObject(UTorqueShedSave::StaticClass()));
	if (!S) return;
	ToSave(*S);
	if (!UGameplayStatics::SaveGameToSlot(S, SaveSlot, 0))
		UE_LOG(LogTemp, Warning, TEXT("Torque: couldn't save the shed to %s"), *SaveSlot);
}

// The shed's bytes for co-op: small, as they're sent to everyone after every change (a few KB for
// a shed full of parts). Names go in a table once; dirt, rust and wear are a byte each.
namespace TorqueBuildImpl
{
constexpr int32 BytesLayout = 15;   // bumped when the layout below changes (2: fabrication, 3: wheeling and the dyno, 4: the fit, 5: paint jobs, 6: the mixing bay, 7: booth hook heights, 8: the radio, 9: the effects rack, 10: masking tape, 11: who a delivery's for, 12: a tank's badge, 13: the bay lifts, 14: the shed's lights, roller door, engine stand and extinguisher, 15: the dyno desk's tune and lightened parts)

struct FNameTable
{
	TArray<FString> Names;
	TMap<FName, int32> Index;
	uint16 Of(FName N)
	{
		if (const int32* I = Index.Find(N)) return (uint16)*I;
		const int32 I = Names.Add(N.IsNone() ? FString() : N.ToString());
		Index.Add(N, I);
		return (uint16)I;
	}
};

uint8 Quant(float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255); }

// The dyno desk's settings on a bike or a part: how many, then each name (from the table) and value.
void WriteTune(FArchive& Ar, FNameTable& Table, const TMap<FName, float>& Tune)
{
	uint8 N = (uint8)FMath::Min(Tune.Num(), 255);
	Ar << N;
	int32 Done = 0;
	for (const TPair<FName, float>& KV : Tune)
	{
		if (Done++ >= N) break;
		uint16 Key = Table.Of(KV.Key);
		float V = KV.Value;
		Ar << Key << V;
	}
}

bool ReadTune(FArchive& Ar, const TArray<FName>& Names, TMap<FName, float>& Tune)
{
	Tune.Reset();
	uint8 N = 0;
	Ar << N;
	for (int32 I = 0; I < N && !Ar.IsError(); ++I)
	{
		uint16 Key = 0;
		float V = 0.0f;
		Ar << Key << V;
		if (!Names.IsValidIndex(Key) || !FMath::IsFinite(V)) return false;
		Tune.Add(Names[Key], V);
	}
	return !Ar.IsError();
}
float Unquant(uint8 V) { return V / 255.0f; }

void WriteSlots(FArchive& Ar, const TMap<FName, int32>& Slots, const TMap<FName, bool>& Bolted)
{
	const TArray<FName>& All = TorqueShedParts::Slots();
	uint8 N = 0;
	for (const TPair<FName, int32>& KV : Slots)
		if (KV.Value != 0 && All.Contains(KV.Key)) ++N;
	Ar << N;
	for (const TPair<FName, int32>& KV : Slots)
	{
		if (KV.Value == 0 || !All.Contains(KV.Key)) continue;
		uint8 Slot = (uint8)All.IndexOfByKey(KV.Key);
		int32 Id = KV.Value;
		uint8 bOn = Bolted.FindRef(KV.Key) ? 1 : 0;
		Ar << Slot << Id << bOn;
	}
}

bool ReadSlots(FArchive& Ar, TMap<FName, int32>& Slots, TMap<FName, bool>& Bolted, bool bBike)
{
	const TArray<FName>& All = TorqueShedParts::Slots();
	Slots.Reset();
	Bolted.Reset();
	// Every slot of its kind is there, empty unless the host's says otherwise.
	for (const FName& S : bBike ? TorqueShedParts::BikeSlots() : TorqueShedParts::EngineSlots())
	{
		Slots.Add(S, 0);
		Bolted.Add(S, false);
	}
	uint8 N = 0;
	Ar << N;
	for (int32 I = 0; I < N && !Ar.IsError(); ++I)
	{
		uint8 Slot = 0, bOn = 0;
		int32 Id = 0;
		Ar << Slot << Id << bOn;
		if (!All.IsValidIndex(Slot)) return false;
		Slots.Add(All[Slot], Id);
		Bolted.Add(All[Slot], bOn != 0);
	}
	return !Ar.IsError();
}
} // namespace TorqueBuildImpl

bool UTorqueBuildSubsystem::SaveShed(TArray<uint8>& Out) const
{
	// The body first (names gathered as it goes), then the table and the body together.
	TBI::FNameTable Table;
	TArray<uint8> Body;
	FMemoryWriter Ar(Body);
	int32 BikeCount = Bikes.Num();
	Ar << BikeCount;
	for (const FTorqueShedBike& B : Bikes)
	{
		uint8 bExists = B.bExists ? 1 : 0, bRode = B.bRode ? 1 : 0;
		Ar << bExists;
		if (!B.bExists) continue;
		int32 Id = B.Id;
		FString Name = B.Name;
		uint16 Model = Table.Of(B.Model);
		Ar << Id << Name << Model << bRode;
		TBI::WriteSlots(Ar, B.Slots, B.Bolted);
		TArray<FString> Owed = B.FabOwed, Done = B.FabDone;
		Ar << Owed << Done;
		uint8 bAway = B.bAway ? 1 : 0;
		FVector3f Where(B.Xf.GetLocation());
		float Yaw = (float)B.Xf.Rotator().Yaw;
		int32 Pusher = B.Pusher, DynoN = B.DynoN;
		Ar << bAway << Where << Yaw << Pusher << DynoN;
		uint8 Runs = (uint8)FMath::Min(B.DynoRuns.Num(), 2);
		Ar << Runs;
		for (int32 I = B.DynoRuns.Num() - Runs; I < B.DynoRuns.Num(); ++I)
		{
			FTorqueDynoRun Run = B.DynoRuns[I];
			TBI::SerializeRun(Ar, Run);
		}
		FString Geo = B.Geo;
		Ar << Geo;
		TBI::WriteTune(Ar, Table, B.Tune);
	}
	{
		// The dyno room's kit.
		uint8 Straps = 0;
		for (int32 I = 0; I < 4; ++I) Straps |= Dyno.Straps[I] ? (1 << I) : 0;
		uint8 bFan = Dyno.bFan ? 1 : 0, bLights = Dyno.bLights ? 1 : 0;
		float Heat = Dyno.Heat;
		Ar << Straps << bFan << bLights << Heat;
	}
	int32 ItemCount = Items.Num();
	Ar << ItemCount;
	for (const TPair<int32, FTorqueItem>& KV : Items)
	{
		const FTorqueItem& It = KV.Value;
		int32 Id = It.Id, Ref = It.Ref, Tyre = It.Tyre;
		uint16 Part = Table.Of(It.Part), Kind = Table.Of(It.Kind), Finish = Table.Of(It.Finish), Spot = Table.Of(It.Spot);
		uint8 Rust = TBI::Quant(It.Rust), Dirt = TBI::Quant(It.Dirt), Wear = TBI::Quant(It.Wear);
		FColor Colour = It.Colour.QuantizeRound();
		uint8 bBoxed = It.bBoxed ? 1 : 0, At = (uint8)It.At;
		Ar << Id << Part << Kind << Rust << Dirt << Wear << Colour << Finish << bBoxed << At << Ref << Spot << Tyre;
		if (It.bBoxed)
		{
			// Who the delivery's for.
			FString OrderedBy = It.OrderedBy;
			FColor OrderedTint = It.OrderedTint.QuantizeRound();
			Ar << OrderedBy << OrderedTint;
		}
		if (It.At == ETorqueAt::Floor)
		{
			FVector3f Where(It.Place.GetLocation());
			float Yaw = (float)It.Place.Rotator().Yaw;
			Ar << Where << Yaw;
		}
		// The paint job: its quality (0: as the factory left it, else 1 + 254 x quality), primer, coat.
		uint8 PaintQ = It.PaintQ < 0.0f ? 0 : (uint8)(1 + FMath::RoundToInt(FMath::Clamp(It.PaintQ, 0.0f, 1.0f) * 254.0f));
		uint8 Primer = TBI::Quant(It.Primer), CoatQ = TBI::Quant(It.CoatQ), bPrimerTop = It.bPrimerTop ? 1 : 0;
		uint16 Coat = Table.Of(It.Coat);
		Ar << PaintQ << Primer << bPrimerTop << Coat << CoatQ;
		// Paint from the mixing bay.
		FString MixCode = It.MixCode, MixName = It.MixName;
		uint16 MixEffect = Table.Of(It.MixEffect);
		uint8 MixCoats = (uint8)FMath::Clamp(It.MixCoats, 0, 255), Flat = TBI::Quant(It.Flat);
		FColor MixUnder = It.MixUnder.QuantizeRound();
		Ar << MixCode << MixName << MixEffect << MixCoats << MixUnder << Flat;
		// Masking tape on it, and a masked pattern.
		uint8 Tape = (uint8)It.Tape, Mask = (uint8)It.Mask, MaskMetal = TBI::Quant(It.MaskMetal), MaskRough = TBI::Quant(It.MaskRough);
		FColor MaskColour = It.MaskColour.QuantizeRound();
		Ar << Tape << Mask;
		if (Mask != 0) Ar << MaskColour << MaskMetal << MaskRough;
		// A tank's badge.
		uint8 bBadge = It.Badge.IsEmpty() ? 0 : 1;
		Ar << bBadge;
		if (bBadge)
		{
			FString Badge = It.Badge;
			Ar << Badge;
		}
		// The dyno desk's settings kept on it, and whether it's been lightened.
		TBI::WriteTune(Ar, Table, It.Tune);
		uint8 bLightenedB = It.bLightened ? 1 : 0;
		Ar << bLightenedB;
		uint8 bEngine = It.Kind == TBI::EngineKind() ? 1 : 0;
		Ar << bEngine;
		if (bEngine) TBI::WriteSlots(Ar, It.Slots, It.Bolted);
	}
	int32 NextIdOut = NextId;
	Ar << NextIdOut;
	const_cast<FTorquePaintBay&>(Paint).Serialize(Ar, true);
	uint8 bRadioB = bRadioOn ? 1 : 0;
	int32 RadioOut = RadioTuned;
	Ar << bRadioB << RadioOut;
	int32 LiftsOut = LiftsUp;
	Ar << LiftsOut;
	{
		// [kit] The main lights, the roller door, the engine stand, the fire extinguisher.
		uint8 bLightsB = bShedLights ? 1 : 0, bDoorB = bDoorUp ? 1 : 0, bFoamB = bFoaming ? 1 : 0;
		FVector StandAt = StandXf.GetLocation();
		double StandYaw = StandXf.Rotator().Yaw;
		int32 Pusher = StandPusher, ExtBy = ExtinguisherBy;
		Ar << bLightsB << bDoorB << StandAt << StandYaw << Pusher << ExtBy << bFoamB;
	}

	Out.Reset();
	FMemoryWriter Head(Out);
	int32 Layout = TBI::BytesLayout;
	int32 NumNames = Table.Names.Num();
	Head << Layout << NumNames;
	for (FString& N : Table.Names) Head << N;
	Out.Append(Body);
	return true;
}

bool UTorqueBuildSubsystem::LoadShedFromHost(const TArray<uint8>& Data)
{
	if (Data.Num() == 0) return false;
	FMemoryReader Ar(Data);
	int32 Layout = 0, NumNames = 0;
	Ar << Layout << NumNames;
	if (Layout != TBI::BytesLayout || NumNames < 0 || NumNames > 65535)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque net: the host's shed is from another version of the game (layout %d, this game %d)"), Layout, TBI::BytesLayout);
		return false;
	}
	TArray<FName> Names;
	for (int32 I = 0; I < NumNames && !Ar.IsError(); ++I)
	{
		FString N;
		Ar << N;
		Names.Add(N.IsEmpty() ? FName() : FName(*N));
	}
	auto NameAt = [&Names](uint16 I) { return Names.IsValidIndex(I) ? Names[I] : FName(); };
	TArray<FTorqueShedBike> NewBikes;
	TMap<int32, FTorqueItem> NewItems;
	int32 BikeCount = 0;
	Ar << BikeCount;
	if (BikeCount < 0 || BikeCount > 64) return false;
	NewBikes.SetNum(Bays);
	for (int32 I = 0; I < BikeCount && !Ar.IsError(); ++I)
	{
		uint8 bExists = 0;
		Ar << bExists;
		if (!bExists) continue;
		FTorqueShedBike B;
		B.bExists = true;
		uint16 Model = 0;
		uint8 bRode = 0;
		Ar << B.Id << B.Name << Model << bRode;
		B.Model = NameAt(Model);
		B.bRode = bRode != 0;
		if (!TBI::ReadSlots(Ar, B.Slots, B.Bolted, true)) return false;
		Ar << B.FabOwed << B.FabDone;
		if (Ar.IsError() || B.FabOwed.Num() > 64 || B.FabDone.Num() > 64) return false;
		uint8 bAway = 0, Runs = 0;
		FVector3f Where;
		float Yaw = 0.0f;
		Ar << bAway << Where << Yaw << B.Pusher << B.DynoN << Runs;
		B.bAway = bAway != 0;
		B.Xf = FTransform(FRotator(0.0, Yaw, 0.0), FVector(Where));
		if (Runs > 2) return false;
		for (int32 R = 0; R < Runs; ++R)
		{
			FTorqueDynoRun Run;
			if (!TBI::SerializeRun(Ar, Run)) return false;
			B.DynoRuns.Add(Run);
		}
		Ar << B.Geo;
		if (Ar.IsError() || B.Geo.Len() > TorqueGeometryMode::MaxText) return false;
		if (!TBI::ReadTune(Ar, Names, B.Tune)) return false;
		if (NewBikes.IsValidIndex(I)) NewBikes[I] = B;
	}
	FTorqueDynoRoom NewDyno;
	{
		uint8 Straps = 0, bFan = 0, bLights = 1;
		Ar << Straps << bFan << bLights << NewDyno.Heat;
		for (int32 I = 0; I < 4; ++I) NewDyno.Straps[I] = (Straps & (1 << I)) != 0;
		NewDyno.bFan = bFan != 0;
		NewDyno.bLights = bLights != 0;
	}
	int32 ItemCount = 0;
	Ar << ItemCount;
	if (ItemCount < 0 || ItemCount > 100000) return false;
	for (int32 I = 0; I < ItemCount && !Ar.IsError(); ++I)
	{
		FTorqueItem It;
		uint16 Part = 0, Kind = 0, Finish = 0, Spot = 0;
		uint8 Rust = 0, Dirt = 0, Wear = 0, bBoxed = 0, At = 0, bEngine = 0;
		FColor Colour;
		Ar << It.Id << Part << Kind << Rust << Dirt << Wear << Colour << Finish << bBoxed << At << It.Ref << Spot << It.Tyre;
		It.Part = NameAt(Part);
		It.Kind = NameAt(Kind);
		It.Finish = NameAt(Finish);
		It.Spot = NameAt(Spot);
		It.Rust = TBI::Unquant(Rust);
		It.Dirt = TBI::Unquant(Dirt);
		It.Wear = TBI::Unquant(Wear);
		It.Colour = Colour.ReinterpretAsLinear();   // (QuantizeRound wrote it as the numbers are: no sRGB step)
		It.bBoxed = bBoxed != 0;
		if (It.bBoxed)
		{
			FColor OrderedTint;
			Ar << It.OrderedBy << OrderedTint;
			It.OrderedTint = OrderedTint.ReinterpretAsLinear();
		}
		It.At = (ETorqueAt)FMath::Min<uint8>(At, (uint8)ETorqueAt::Wheel);
		if (It.At == ETorqueAt::Floor)
		{
			FVector3f Where;
			float Yaw = 0.0f;
			Ar << Where << Yaw;
			It.Place = FTransform(FRotator(0.0, Yaw, 0.0), FVector(Where));
		}
		{
			uint8 PaintQ = 0, Primer = 0, bPrimerTop = 0, CoatQ = 0;
			uint16 Coat = 0;
			Ar << PaintQ << Primer << bPrimerTop << Coat << CoatQ;
			It.PaintQ = PaintQ == 0 ? -1.0f : (PaintQ - 1) / 254.0f;
			It.Primer = TBI::Unquant(Primer);
			It.bPrimerTop = bPrimerTop != 0;
			It.Coat = NameAt(Coat);
			It.CoatQ = TBI::Unquant(CoatQ);
			uint16 MixEffect = 0;
			uint8 MixCoats = 0, Flat = 0;
			FColor MixUnder;
			Ar << It.MixCode << It.MixName << MixEffect << MixCoats << MixUnder << Flat;
			It.MixEffect = NameAt(MixEffect);
			It.MixCoats = MixCoats;
			It.MixUnder = MixUnder.ReinterpretAsLinear();
			It.Flat = TBI::Unquant(Flat);
			uint8 Tape = 0, Mask = 0;
			Ar << Tape << Mask;
			It.Tape = FMath::Min<int32>(Tape, TorquePaintMix::TapePatterns);
			It.Mask = FMath::Min<int32>(Mask, TorquePaintMix::TapePatterns);
			if (Mask != 0)
			{
				uint8 MaskMetal = 0, MaskRough = 0;
				FColor MaskColour;
				Ar << MaskColour << MaskMetal << MaskRough;
				It.MaskColour = MaskColour.ReinterpretAsLinear();
				It.MaskMetal = TBI::Unquant(MaskMetal);
				It.MaskRough = TBI::Unquant(MaskRough);
			}
			uint8 bBadge = 0;
			Ar << bBadge;
			if (bBadge)
			{
				Ar << It.Badge;
				if (It.Badge.Len() > 2000) It.Badge.Reset();   // (not a badge)
			}
			if (!TBI::ReadTune(Ar, Names, It.Tune)) return false;
			uint8 bLightenedB = 0;
			Ar << bLightenedB;
			It.bLightened = bLightenedB != 0;
		}
		Ar << bEngine;
		if (bEngine && !TBI::ReadSlots(Ar, It.Slots, It.Bolted, false)) return false;
		NewItems.Add(It.Id, It);
	}
	int32 HostNextId = 1;
	Ar << HostNextId;
	FTorquePaintBay NewPaint;
	const bool bPaintRead = NewPaint.Serialize(Ar, true);
	uint8 bRadioB = 0;
	int32 RadioIn = 0;
	Ar << bRadioB << RadioIn;
	int32 LiftsIn = 0;
	Ar << LiftsIn;
	uint8 bLightsB = 1, bDoorB = 1, bFoamB = 0;   // [kit]
	FVector StandAtIn = StandHome().GetLocation();
	double StandYawIn = 0.0;
	int32 PusherIn = 0, ExtByIn = 0;
	Ar << bLightsB << bDoorB << StandAtIn << StandYawIn << PusherIn << ExtByIn << bFoamB;
	if (Ar.IsError() || !bPaintRead)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque net: the host's shed couldn't be read (%d bytes)"), Data.Num());
		return false;
	}
	// Redraw only what changed: a bay's parts when what's on it did, else its looks; the loose
	// parts when any of them changed at all.
	TArray<FString> PartsBefore, LooksBefore;
	for (int32 I = 0; I < Bays; ++I) { PartsBefore.Add(BaySignature(I, false)); LooksBefore.Add(BaySignature(I, true)); }
	const FString LooseBefore = LooseSignature();
	Bikes = MoveTemp(NewBikes);
	Items = MoveTemp(NewItems);
	Dyno = NewDyno;
	Paint = MoveTemp(NewPaint);
	bRadioOn = bRadioB != 0;
	RadioTuned = FMath::Clamp(RadioIn, 0, FMath::Max(0, TorqueShedRadio::Stations().Num() - 1));
	LiftsUp = LiftsIn;
	bShedLights = bLightsB != 0;   // [kit]
	bDoorUp = bDoorB != 0;
	StandXf = FTransform(FRotator(0.0, StandYawIn, 0.0), StandAtIn);
	StandPusher = PusherIn;
	ExtinguisherBy = ExtByIn;
	bFoaming = bFoamB != 0;
	NextId = HostNextId;
	bHostCopy = true;
	for (int32 I = 0; I < Bays; ++I)
	{
		if (BaySignature(I, false) != PartsBefore[I])
		{
			RebuildBuild(I);
			OnChanged.Broadcast(I);
		}
		else if (BaySignature(I, true) != LooksBefore[I])
			OnLooks.Broadcast(I);
	}
	if (LooseSignature() != LooseBefore) OnItems.Broadcast();
	return true;
}

FString UTorqueBuildSubsystem::BaySignature(int32 Bay, bool bLooks) const
{
	FString Sig = Exists(Bay) ? Bikes[Bay].Name : FString(TEXT("-"));
	if (!bLooks && Exists(Bay) && !Bikes[Bay].Geo.IsEmpty()) Sig += TEXT("|geo:") + Bikes[Bay].Geo;
	for (const FName& S : TorqueShedParts::Slots())
	{
		const FTorqueItem* It = Items.Find(ItemOn(Bay, S));
		if (!It) continue;
		Sig += FString::Printf(TEXT("|%d:%s"), It->Id, *It->Part.ToString());
		if (bLooks)
			Sig += FString::Printf(TEXT(":%d:%d:%s:%s:%d:%d"), TBI::Quant(It->Dirt), TBI::Quant(It->Rust), *It->Colour.QuantizeRound().ToHex(),
				*It->Finish.ToString(), IsBolted(Bay, S) ? 1 : 0, It->bPrimerTop ? 1 : 0);
		if (bLooks && It->Flat > 0.0f) Sig += FString::Printf(TEXT(":flat%d"), TBI::Quant(It->Flat));
	}
	return Sig;
}

FString UTorqueBuildSubsystem::LooseSignature() const
{
	FString Sig;
	for (const TPair<int32, FTorqueItem>& KV : Items)
	{
		const FTorqueItem& It = KV.Value;
		if (BayOf(KV.Key) != INDEX_NONE) continue;
		const FVector P = It.Place.GetLocation();
		Sig += FString::Printf(TEXT("|%d:%d:%d:%s:%d:%d:%d:%d:%d:%s:%s"), It.Id, (int32)It.At, It.Ref, *It.Spot.ToString(), It.bBoxed ? 1 : 0,
			It.Tyre, FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y), TBI::Quant(It.Dirt) + 256 * TBI::Quant(It.Rust),
			*It.Colour.QuantizeRound().ToHex(), *It.Finish.ToString());
		if (It.bPrimerTop) Sig += TEXT(":primer");
		if (It.Flat > 0.0f) Sig += FString::Printf(TEXT(":flat%d"), TBI::Quant(It.Flat));
		for (const TPair<FName, int32>& S : It.Slots) Sig += FString::Printf(TEXT(",%d"), S.Value);
	}
	return Sig;
}

void UTorqueBuildSubsystem::LeaveHostShed()
{
	if (!bHostCopy) return;
	if (SaveSlot == TEXT("ShedTest")) StartFresh();
	else Load();
	bHostCopy = false;
	UE_LOG(LogTemp, Log, TEXT("Torque: left the host's shed: this player's own shed is back"));
	for (int32 I = 0; I < Bikes.Num(); ++I) OnChanged.Broadcast(I);
	OnItems.Broadcast();
}

// --- Making things -------------------------------------------------------------------------------

int32 UTorqueBuildSubsystem::NewItem(FName Part, float Rust, float Dirt, float Wear)
{
	FTorqueItem It;
	It.Id = NextId++;
	It.Part = Part;
	It.Kind = TorqueShedParts::PartKind(Part);
	It.Rust = FMath::Clamp(Rust, 0.0f, 1.0f);
	It.Dirt = FMath::Clamp(Dirt, 0.0f, 1.0f);
	It.Wear = FMath::Clamp(Wear, 0.0f, 1.0f);
	It.Colour = TorqueShedParts::DefaultColour(Part);
	It.At = ETorqueAt::Floor;
	It.Place = FTransform(TorqueShedLayout::DropAt());
	Items.Add(It.Id, It);
	return It.Id;
}

int32 UTorqueBuildSubsystem::AgedItem(FName Part, float Age)
{
	// shed_state _aged_item: most parts on an old bike have only a bloom of surface rust; about one
	// in seven has gone properly bad.
	const float R = Rng.FRand();
	const float RustScale = R < 0.85f ? FMath::Lerp(0.1f, 0.45f, R / 0.85f) : FMath::Lerp(0.7f, 1.1f, (R - 0.85f) / 0.15f);
	return NewItem(Part, Age * RustScale, Age * Rng.FRandRange(0.8f, 1.4f), Age * Rng.FRandRange(0.2f, 1.0f));
}

void UTorqueBuildSubsystem::NewStockBike(int32 Bay, float Age)
{
	if (!Bikes.IsValidIndex(Bay)) return;
	{
		FTorqueShedBike& B = Bikes[Bay];
		B = FTorqueShedBike();
		B.bExists = true;
		B.Id = NextId++;
		B.Name = TBI::BikeNames[Rng.RandRange(0, (int32)UE_ARRAY_COUNT(TBI::BikeNames) - 1)];
		B.Model = FName(TorqueShedParts::StockModel);
		for (const FName& S : TorqueShedParts::BikeSlots())
		{
			B.Slots.Add(S, 0);
			B.Bolted.Add(S, false);
		}
	}
	int32 Block = 0;
	for (const FName& S : TorqueShedParts::StockSlots())
	{
		if (TorqueShedParts::IsEngineSlot(S)) continue;
		const int32 Id = AgedItem(TorqueShedParts::StockPart(S), Age);
		Move(Id, ETorqueAt::Bike, Bay);
		Bikes[Bay].Slots.Add(S, Id);
		Bikes[Bay].Bolted.Add(S, true);
		if (S == TBI::EngineKind()) Block = Id;
	}
	if (Items.Contains(Block))
	{
		for (const FName& S : TorqueShedParts::EngineSlots())
		{
			Items[Block].Slots.Add(S, 0);
			Items[Block].Bolted.Add(S, false);
		}
		for (const FName& S : TorqueShedParts::StockSlots())
		{
			if (!TorqueShedParts::IsEngineSlot(S)) continue;
			const int32 Id = AgedItem(TorqueShedParts::StockPart(S), Age);
			Move(Id, ETorqueAt::Engine, Block);
			Items[Block].Slots.Add(S, Id);   // (looked up again: adding items may move the map's storage)
			Items[Block].Bolted.Add(S, true);
		}
	}
}

bool UTorqueBuildSubsystem::NewProjectBike(int32 Bay, float Age, const FString& Style)
{
	// shed_state _style_bike: a project bike built as a style's reference bike from the part list
	// (any style for "any old project bike"), every part as worn as Age.
	if (!Bikes.IsValidIndex(Bay)) return false;
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	if (!Lay || Lay->Styles().empty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no fitting layout (Data/Fitment/layout.json): no project bike for bay %d"), Bay + 1);
		return false;
	}
	const std::vector<std::string>& StyleIds = Lay->Styles();
	const std::string Want = Style.IsEmpty() ? StyleIds[Rng.RandRange(0, (int32)StyleIds.size() - 1)] : TBI::Std(Style.ToLower());
	const TorqueFit::PartsMap Ref = Lay->ReferenceBuild(Want);
	if (Ref.empty()) return false;
	TMap<FName, FName> Parts;
	for (const auto& KV : Ref) Parts.Add(FName(*TBI::Ue(KV.first)), FName(*TBI::Ue(KV.second)));
	FillBike(Bay, Parts, Age);
	UE_LOG(LogTemp, Log, TEXT("Torque: %s, a %s project bike, in bay %d"), *Bikes[Bay].Name, *TBI::Ue(Want), Bay + 1);
	return true;
}

void UTorqueBuildSubsystem::FillBike(int32 Bay, const TMap<FName, FName>& Parts, float Age)
{
	{
		FTorqueShedBike& B = Bikes[Bay];
		B = FTorqueShedBike();
		B.bExists = true;
		B.Id = NextId++;
		B.Name = TBI::BikeNames[Rng.RandRange(0, (int32)UE_ARRAY_COUNT(TBI::BikeNames) - 1)];
		B.Model = FName(TorqueShedParts::StockModel);   // (the fitment API's name for a bike; its parts are the catalogue's)
		for (const FName& S : TorqueShedParts::BikeSlots())
		{
			B.Slots.Add(S, 0);
			B.Bolted.Add(S, false);
		}
	}
	// The engine first, so the parts that go on it have a block to go on.
	TArray<FName> Order;
	Order.Add(TBI::EngineKind());
	for (const FName& S : TorqueShedParts::Slots())
		if (S != TBI::EngineKind()) Order.Add(S);
	for (const FName& S : Order)
	{
		if (ItemOn(Bay, S) != 0) continue;
		const FName* Found = nullptr;
		for (const TPair<FName, FName>& KV : Parts)
			if (KV.Key.ToString().Equals(S.ToString(), ESearchCase::IgnoreCase)) Found = &KV.Value;
		if (!Found || Found->IsNone() || *Found == FName(TEXT("none"))) continue;
		const FName Part = *Found;
		if (!TorqueShedParts::IsPart(Part) || !HostOf(Bay, S, 0).Slots) continue;
		const int32 Id = AgedItem(Part, Age);
		if (S == TBI::EngineKind())
			for (const FName& ES : TorqueShedParts::EngineSlots())
			{
				Items[Id].Slots.Add(ES, 0);
				Items[Id].Bolted.Add(ES, false);
			}
		const FHost H = HostOf(Bay, S, 0);   // (looked up again: adding an item may move the items' storage)
		Move(Id, H.Engine != 0 ? ETorqueAt::Engine : ETorqueAt::Bike, H.Engine != 0 ? H.Engine : Bay);
		H.Slots->Add(S, Id);
		H.Bolted->Add(S, true);
	}
}

FString UTorqueBuildSubsystem::NewOrderedBike(int32 Peer, int32 Bay, const FString& OrderText)
{
	// BikeCopy.order of a configured new bike, built (shed_state deliver_ready_bike): every part new,
	// coloured and finished as ordered, the fabrication its parts take done by the works as they
	// built it, and named after who ordered it ("Simon's cafe racer", owned_name).
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	if (!Lay) return TEXT("The bike can't be built: the part list's bike layout is missing.");
	TorqueFit::Shop::Order O;
	if (!TorqueFit::Shop::ParseOrder(*Lay, TBI::Std(OrderText), O)) return TEXT("That bike can't be built.");
	if (!Bikes.IsValidIndex(Bay)) return TEXT("Every bay is full. Ride a bike out or strip one down first.");
	TMap<FName, FName> Parts;
	for (const auto& KV : O.Parts) Parts.Add(FName(*TBI::Ue(KV.first)), FName(*TBI::Ue(KV.second)));
	FillBike(Bay, Parts, 0.0f);
	for (const auto& KV : TorqueFit::Shop::OrderLooks(*Lay, O))
	{
		FTorqueItem* It = Items.Find(ItemOn(Bay, FName(*TBI::Ue(KV.first))));
		if (!It) continue;
		It->Colour = FLinearColor((float)KV.second.Colour[0], (float)KV.second.Colour[1], (float)KV.second.Colour[2]);
		It->Finish = KV.second.Finish.empty() ? NAME_None : FName(*TBI::Ue(KV.second.Finish));
	}
	// The badge on the tank (BikeShop.buy_new: the tank item carries it).
	if (FTorqueItem* Tank = Items.Find(ItemOn(Bay, FName(TEXT("tank")))))
		Tank->Badge = TBI::Ue(TorqueFit::Shop::BadgeText(O.TankBadge));
	FTorqueShedBike& B = Bikes[Bay];
	for (const std::string& What : Lay->Solve(O.Parts).Fit.Work) B.FabDone.Add(TBI::Ue(What));
	FString By;
	FLinearColor Tint;
	OrdererOf(Peer, By, Tint);
	const std::string Style = Lay->DetectStyle(O.Parts);
	const FString Base = FString::Printf(TEXT("%s's %s"), By.IsEmpty() || By == TEXT("the workshop") ? TEXT("The garage") : *By,
		Style.empty() ? TEXT("bike") : *TBI::Ue(TorqueFit::Shop::StyleNoun(*Lay, Style)));
	FString Name = Base;
	for (int32 N = 2;; ++N)
	{
		bool bTaken = false;
		for (int32 I = 0; I < Bikes.Num(); ++I) bTaken = bTaken || (I != Bay && Bikes[I].bExists && Bikes[I].Name == Name);
		if (!bTaken) break;
		Name = FString::Printf(TEXT("%s %d"), *Base, N);
	}
	B.Name = Name;
	UE_LOG(LogTemp, Log, TEXT("Torque: %s, a new %s from the configurator, in bay %d"), *B.Name, *TBI::Ue(O.Style), Bay + 1);
	return FString();
}

int32 UTorqueBuildSubsystem::DropSpeedTwins()
{
	// Every bike is made from the part list now (the Speed Twin model is out of the game): a saved
	// bike with any of the Speed Twin's own parts on it becomes a project bike in its bay, and any
	// Speed Twin part left about the shed goes.
	int32 Bikes2 = 0;
	for (int32 Bay = 0; Bay < Bikes.Num(); ++Bay)
	{
		if (!Bikes[Bay].bExists) continue;
		bool bTwin = false;
		for (const FName& S : TorqueShedParts::Slots())
			if (const FTorqueItem* It = Items.Find(ItemOn(Bay, S))) bTwin = bTwin || TorqueShedParts::IsStock(It->Part);
		if (!bTwin) continue;
		for (const FName& S : TorqueShedParts::BikeSlots())
			if (const int32 Id = Bikes[Bay].Slots.FindRef(S)) EraseItem(Id);
		NewProjectBike(Bay, Rng.FRandRange(0.35f, 0.6f));
		++Bikes2;
	}
	TArray<int32> Loose;
	for (const TPair<int32, FTorqueItem>& KV : Items)
		if (TorqueShedParts::IsStock(KV.Value.Part)) Loose.Add(KV.Key);
	for (int32 Id : Loose)
	{
		// (Off an engine or a wheel that stays: clear its place there first.)
		for (TPair<int32, FTorqueItem>& KV : Items)
		{
			for (TPair<FName, int32>& Sl : KV.Value.Slots)
				if (Sl.Value == Id) Sl.Value = 0;
			if (KV.Value.Tyre == Id) KV.Value.Tyre = 0;
		}
		for (FTorqueShedBike& B : Bikes)
			for (TPair<FName, int32>& Sl : B.Slots)
				if (Sl.Value == Id) Sl.Value = 0;
		EraseItem(Id);
	}
	if (Bikes2 > 0 || Loose.Num() > 0)
		UE_LOG(LogTemp, Log, TEXT("Torque: the saved shed's Speed Twins are project bikes now (%d bikes, %d loose Speed Twin parts gone)"), Bikes2, Loose.Num());
	return Bikes2 + Loose.Num();
}

void UTorqueBuildSubsystem::Move(int32 Id, ETorqueAt At, int32 Ref, FName Spot)
{
	FTorqueItem* It = Items.Find(Id);
	if (!It) return;
	It->At = At;
	It->Ref = Ref;
	It->Spot = Spot;
	if (At != ETorqueAt::Floor) It->Place = FTransform::Identity;
}

void UTorqueBuildSubsystem::EraseItem(int32 Id)
{
	const FTorqueItem* It = Items.Find(Id);
	if (!It) return;
	TArray<int32> Gone;
	Gone.Add(Id);
	if (It->Tyre != 0) Gone.Add(It->Tyre);
	for (const TPair<FName, int32>& KV : It->Slots)
		if (KV.Value != 0) Gone.Add(KV.Value);
	for (int32 G : Gone) Items.Remove(G);
}

int32 UTorqueBuildSubsystem::FreeBay() const
{
	for (int32 I = 0; I < Bikes.Num(); ++I)
		if (!Bikes[I].bExists) return I;
	return INDEX_NONE;
}

int32 UTorqueBuildSubsystem::FreeDelivery() const
{
	for (int32 I = 0; I < 42; ++I)
	{
		const FVector At = TorqueShedLayout::DeliveryAt(I);
		bool bTaken = false;
		for (const TPair<int32, FTorqueItem>& KV : Items)
			if (KV.Value.At == ETorqueAt::Floor && FVector::Dist(KV.Value.Place.GetLocation(), At) < 40.0) { bTaken = true; break; }
		if (!bTaken) return I;
	}
	return INDEX_NONE;
}

// --- Reads ---------------------------------------------------------------------------------------

const FTorqueShedBike& UTorqueBuildSubsystem::Bike(int32 Bay) const
{
	return Bikes.IsValidIndex(Bay) ? Bikes[Bay] : TBI::NoBike();
}

const FTorqueBuild& UTorqueBuildSubsystem::Get(int32 Bay) const
{
	return Builds.IsValidIndex(Bay) ? Builds[Bay] : TBI::NoBuild();
}

void UTorqueBuildSubsystem::RebuildBuild(int32 Bay)
{
	if (Builds.Num() < Bays) Builds.SetNum(Bays);
	if (!Builds.IsValidIndex(Bay)) return;
	FTorqueBuild Build;
	if (Exists(Bay))
	{
		Build.Bike = Bikes[Bay].Model.IsNone() ? FName(TorqueShedParts::StockModel) : Bikes[Bay].Model;
		// A catalogue part shows as the model's fit option for it, where it has one (the front tyre
		// before the back: the Speed Twin's tyre options are pairs).
		for (const FName& S : TorqueShedParts::Slots())
		{
			const FTorqueItem* It = Items.Find(ItemOn(Bay, S));
			if (!It || TorqueShedParts::IsStock(It->Part)) continue;
			FName FitSlot, Option;
			if (TorqueShedParts::ModelOption(It->Part, It->Kind, FitSlot, Option) && !Build.Fitted.Contains(FitSlot))
				Build.Fitted.Add(FitSlot, Option);
		}
	}
	Builds[Bay] = Build;
}

bool UTorqueBuildSubsystem::ShowsModelPart(int32 Bay, const FTorqueResolvedPart& Part) const
{
	if (!Exists(Bay)) return false;
	return ItemOn(Bay, TorqueShedParts::SlotOfModelPart(Part)) != 0;
}

UTorqueBuildSubsystem::FHost UTorqueBuildSubsystem::HostOf(int32 Bay, FName Slot, int32 Engine)
{
	FHost H;
	if (Engine != 0)
	{
		FTorqueItem* E = Items.Find(Engine);
		if (!E || E->Kind != TBI::EngineKind()) return H;
		H.Slots = &E->Slots;
		H.Bolted = &E->Bolted;
		H.Engine = Engine;
		H.Bay = E->At == ETorqueAt::Bike ? E->Ref : INDEX_NONE;
		return H;
	}
	if (!Exists(Bay)) return H;
	if (TorqueShedParts::IsEngineSlot(Slot))
	{
		const int32 Block = Bikes[Bay].Slots.FindRef(TBI::EngineKind());
		FTorqueItem* E = Items.Find(Block);
		if (!E) return H;
		H.Slots = &E->Slots;
		H.Bolted = &E->Bolted;
		H.Engine = Block;
		H.Bay = Bay;
		return H;
	}
	H.Slots = &Bikes[Bay].Slots;
	H.Bolted = &Bikes[Bay].Bolted;
	H.Bay = Bay;
	return H;
}

int32 UTorqueBuildSubsystem::ItemOn(int32 Bay, FName Slot, int32 Engine) const
{
	const FHost H = HostOf(Bay, Slot, Engine);
	return H.Slots ? H.Slots->FindRef(Slot) : 0;
}

bool UTorqueBuildSubsystem::IsBolted(int32 Bay, FName Slot, int32 Engine) const
{
	const FHost H = HostOf(Bay, Slot, Engine);
	return H.Bolted ? H.Bolted->FindRef(Slot) : false;
}

TArray<FName> UTorqueBuildSubsystem::MissingSlots(int32 Bay) const
{
	TArray<FName> Out;
	if (!Exists(Bay)) return Out;
	for (const FName& S : TorqueShedParts::BikeSlots())
		if (ItemOn(Bay, S) == 0 && !TorqueShedParts::MayBeEmpty(S)) Out.Add(S);
	if (ItemOn(Bay, TBI::EngineKind()) != 0)
		for (const FName& S : TorqueShedParts::EngineSlots())
			if (ItemOn(Bay, S) == 0) Out.Add(S);
	return Out;
}

TArray<FName> UTorqueBuildSubsystem::LooseSlots(int32 Bay) const
{
	TArray<FName> Out;
	if (!Exists(Bay)) return Out;
	for (const FName& S : TorqueShedParts::Slots())
		if (ItemOn(Bay, S) != 0 && !IsBolted(Bay, S)) Out.Add(S);
	return Out;
}

bool UTorqueBuildSubsystem::IsComplete(int32 Bay) const
{
	return Exists(Bay) && MissingSlots(Bay).Num() == 0 && LooseSlots(Bay).Num() == 0 && FabOwed(Bay).Num() == 0;
}

// --- Fabrication (shed_state fab_owed, BikeRig.part_fit) ---------------------------------------------

bool UTorqueBuildSubsystem::LayoutParts(int32 Bay, TMap<FString, FString>& Out) const
{
	Out.Reset();
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	if (!Lay || !Exists(Bay)) return false;
	for (const std::string& SlotText : Lay->Slots())
	{
		const FString Slot = TBI::Ue(SlotText);
		const FTorqueItem* It = Items.Find(ItemOn(Bay, FName(*Slot)));
		if (It && (TorqueShedParts::IsStock(It->Part) || !TorqueShedParts::IsPart(It->Part))) return false;
		Out.Add(Slot, It ? It->Part.ToString().ToLower() : FString(TEXT("none")));
	}
	return true;
}

TMap<FName, float> UTorqueBuildSubsystem::TuneOf(int32 Bay) const
{
	TMap<FName, float> Out;
	if (!Exists(Bay)) return Out;
	for (const TorqueFit::TuneTab& Tab : TorqueFit::TuneTabs())
		for (const TorqueFit::TuneRow& Row : Tab.Rows)
		{
			std::string Home, Field;
			if (Row.Key == "chain" || !TorqueFit::TuneHome(Row.Key, Home, Field)) continue;
			const TMap<FName, float>* Holder = nullptr;
			if (Home == "bike") Holder = &Bikes[Bay].Tune;
			else if (const FTorqueItem* It = Items.Find(ItemOn(Bay, FName(*TBI::Ue(Home))))) Holder = &It->Tune;
			if (const float* V = Holder ? Holder->Find(FName(*TBI::Ue(Field))) : nullptr) Out.Add(FName(*TBI::Ue(Row.Key)), *V);
		}
	return Out;
}

float UTorqueBuildSubsystem::ChainSlackOf(int32 Bay) const
{
	const FTorqueItem* Chain = Items.Find(ItemOn(Bay, FName(TEXT("chain"))));
	const float* Slack = Chain ? Chain->Tune.Find(FName(TEXT("slack"))) : nullptr;
	return Slack ? *Slack : (float)TorqueFit::ChainSlackStock;
}

TArray<FName> UTorqueBuildSubsystem::LightenedOf(int32 Bay) const
{
	TArray<FName> Out;
	if (!Exists(Bay)) return Out;
	for (const TPair<FName, int32>& KV : Bikes[Bay].Slots)
		if (const FTorqueItem* It = Items.Find(KV.Value))
			if (It->bLightened) Out.Add(KV.Key);
	return Out;
}

bool UTorqueBuildSubsystem::WithTune(int32 Bay, TFunctionRef<void(const TorqueFit::BikeTune&)> Fn) const
{
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	TMap<FString, FString> Parts;
	if (!Lay || !LayoutParts(Bay, Parts)) return false;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TBI::Std(KV.Key)] = TBI::Std(KV.Value);
	TorqueFit::TuneMap Tune;
	for (const TPair<FName, float>& KV : TuneOf(Bay)) Tune[TBI::Id(KV.Key)] = KV.Value;
	double EcuHealth = 1.0;
	if (const FTorqueItem* Ecu = Items.Find(ItemOn(Bay, FName(TEXT("ecu")))))
		EcuHealth = TorqueFit::PartHealth(Ecu->Wear, Ecu->Rust, Ecu->Dirt);
	const TorqueFit::BikeTune T(*Lay, TorqueFit::RideParts(*Lay, Map), Tune, EcuHealth);
	Fn(T);
	return true;
}

bool UTorqueBuildSubsystem::FitTier(int32 Bay, FName Slot, FName Part, FString& OutTier, FString& OutReason, TArray<FString>& OutWork) const
{
	OutWork.Reset();
	TMap<FString, FString> Parts;
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	if (!Lay || !LayoutParts(Bay, Parts) || TorqueShedParts::IsStock(Part)) return false;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TBI::Std(KV.Key)] = TBI::Std(KV.Value);
	std::vector<std::string> Done;
	for (const FString& D : Bikes[Bay].FabDone) Done.push_back(TBI::Std(D));
	const TorqueFit::PartTier T = Lay->PartFit(Map, TBI::Id(Slot), TBI::Id(Part), Done);
	OutTier = TBI::Ue(T.Tier);
	OutReason = TBI::Ue(T.Reason);
	for (const TorqueFit::FabJob& J : T.Work) OutWork.Add(TBI::Ue(J.What));
	return true;
}

TArray<FString> UTorqueBuildSubsystem::FabOwed(int32 Bay) const
{
	TArray<FString> Out;
	if (!Exists(Bay) || Bikes[Bay].FabOwed.Num() == 0) return Out;
	// A job whose part has come off again isn't owed any more: only what the bike's parts need now.
	TMap<FString, FString> Parts;
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	if (!Lay || !LayoutParts(Bay, Parts)) return Bikes[Bay].FabOwed;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TBI::Std(KV.Key)] = TBI::Std(KV.Value);
	const TorqueFit::Layout L = Lay->Solve(Map);
	for (const FString& What : Bikes[Bay].FabOwed)
		for (const std::string& Need : L.Fit.Work)
			if (TBI::Ue(Need) == What)
			{
				Out.Add(What);
				break;
			}
	return Out;
}

float UTorqueBuildSubsystem::FabTime(const FString& What)
{
	return 4.0f + (float)TorqueFit::BikeLayout::FabJobFor(TBI::Std(What)).Hours;
}

FString UTorqueBuildSubsystem::TestReload()
{
	if (IsClient() || bHostCopy) return TEXT("only the host's own shed can");
	Save();
	Bikes.Reset();
	Items.Reset();
	Load();
	Builds.SetNum(Bays);
	for (int32 I = 0; I < Bays; ++I) RebuildBuild(I);
	for (int32 I = 0; I < Bays; ++I) OnChanged.Broadcast(I);
	OnItems.Broadcast();
	int32 Count = 0, Fitted = 0;
	for (const FTorqueShedBike& B : Bikes)
	{
		Count += B.bExists ? 1 : 0;
		Fitted += B.bExists && !B.Geo.IsEmpty() ? 1 : 0;
	}
	return FString::Printf(TEXT("saved to %s and loaded back: %d bikes (%d with a fit), %d parts"), *SaveSlot, Count, Fitted, Items.Num());
}

FString UTorqueBuildSubsystem::TestRestyle(int32 Bay, const FString& Style)
{
	if (IsClient()) return TEXT("only the host can");
	if (!Exists(Bay)) return TEXT("there's no bike in that bay");
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	TMap<FString, FString> Parts;
	if (!Lay || !LayoutParts(Bay, Parts)) return TEXT("it isn't a catalogue bike");
	const TorqueFit::PartsMap Ref = Lay->ReferenceBuild(TBI::Std(Style.ToLower()));
	if (Ref.empty()) return TEXT("no such style: ") + Style;
	// The engine's own parts first, then the bike's, the engine block last (it takes what's left on it).
	TArray<FName> Order;
	for (const FName& S : TorqueShedParts::Slots())
		if (S != TBI::EngineKind() && TorqueShedParts::IsEngineSlot(S)) Order.Add(S);
	for (const FName& S : TorqueShedParts::Slots())
		if (S != TBI::EngineKind() && !TorqueShedParts::IsEngineSlot(S)) Order.Add(S);
	Order.Add(TBI::EngineKind());
	int32 Gone = 0;
	for (const FName& S : Order)
	{
		const int32 Id = ItemOn(Bay, S);
		const FTorqueItem* It = Items.Find(Id);
		if (!It) continue;
		const TorqueFit::PartsMap::const_iterator Want = Ref.find(TBI::Id(S));
		if (Want != Ref.end() && Want->second == TBI::Std(It->Part.ToString().ToLower())) continue;
		const FHost H = HostOf(Bay, S, 0);
		if (!H.Slots) continue;
		H.Slots->Remove(S);
		H.Bolted->Remove(S);
		EraseItem(Id);
		++Gone;
	}
	return FString::Printf(TEXT("%d parts thrown away; "), Gone) + TestFinish(Bay, Style, TMap<FString, FString>());
}

FString UTorqueBuildSubsystem::TestFinish(int32 Bay, const FString& Style, const TMap<FString, FString>& Set)
{
	if (IsClient()) return TEXT("only the host can");
	if (!Exists(Bay)) return TEXT("there's no bike in that bay");
	const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
	TMap<FString, FString> Parts;
	if (!Lay || !LayoutParts(Bay, Parts)) return TEXT("it isn't a catalogue bike");
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TBI::Std(KV.Key)] = TBI::Std(KV.Value);
	const std::string RefStyle = Style.IsEmpty() ? Lay->StyleFor(Map) : TBI::Std(Style.ToLower());
	TorqueFit::PartsMap Ref = Lay->ReferenceBuild(RefStyle);
	for (const TPair<FString, FString>& KV : Set) Ref[TBI::Std(KV.Key.ToLower())] = TBI::Std(KV.Value.ToLower());
	// The engine first, so the parts that go on it have a block to go on.
	TArray<FName> Order;
	Order.Add(TBI::EngineKind());
	for (const FName& S : TorqueShedParts::Slots())
		if (S != TBI::EngineKind()) Order.Add(S);
	int32 Added = 0;
	for (const FName& S : Order)
	{
		if (ItemOn(Bay, S) != 0) continue;
		const TorqueFit::PartsMap::const_iterator Found = Ref.find(TBI::Id(S));
		if (Found == Ref.end() || Found->second.empty() || Found->second == "none") continue;
		const FName Part(*TBI::Ue(Found->second));
		if (!TorqueShedParts::IsPart(Part)) continue;
		if (!HostOf(Bay, S, 0).Slots) continue;
		const int32 Id = NewItem(Part, 0.0f, 0.0f, 0.0f);
		const FHost H = HostOf(Bay, S, 0);   // (looked up again: adding an item may move the items' storage)
		Move(Id, H.Engine != 0 ? ETorqueAt::Engine : ETorqueAt::Bike, H.Engine != 0 ? H.Engine : Bay);
		H.Slots->Add(S, Id);
		H.Bolted->Add(S, true);
		++Added;
	}
	int32 Bolted = 0;
	for (const FName& S : TorqueShedParts::Slots())
	{
		const FHost H = HostOf(Bay, S, 0);
		if (!H.Slots || H.Slots->FindRef(S) == 0 || H.Bolted->FindRef(S)) continue;
		H.Bolted->Add(S, true);
		++Bolted;
	}
	RebuildBuild(Bay);
	OnChanged.Broadcast(Bay);
	OnItems.Broadcast();
	MarkSave();
	OnHostChange.Broadcast();
	const TArray<FName> Missing = MissingSlots(Bay);
	FString Left;
	for (const FName& S : Missing) Left += (Left.IsEmpty() ? TEXT("") : TEXT(", ")) + S.ToString().ToLower();
	const FString Owed = FabOwedText(Bay);
	return FString::Printf(TEXT("%d parts from the %s reference bike, %d more bolted up; missing: %s; owed: %s; complete: %s"),
		Added, *TBI::Ue(RefStyle), Bolted, Left.IsEmpty() ? TEXT("nothing") : *Left, Owed.IsEmpty() ? TEXT("nothing") : *Owed,
		IsComplete(Bay) ? TEXT("yes") : TEXT("no"));
}

FString UTorqueBuildSubsystem::FabOwedText(int32 Bay) const
{
	return FString::Join(FabOwed(Bay), TEXT(", "));
}

int32 UTorqueBuildSubsystem::BayOf(int32 ItemId) const
{
	const FTorqueItem* It = Items.Find(ItemId);
	for (int32 Guard = 0; It && Guard < 4; ++Guard)
	{
		if (It->At == ETorqueAt::Bike) return It->Ref;
		if (It->At != ETorqueAt::Engine && It->At != ETorqueAt::Wheel) return INDEX_NONE;
		It = Items.Find(It->Ref);
	}
	return INDEX_NONE;
}

// --- Wheeling bikes and the dyno room -------------------------------------------------------------

FTransform UTorqueBuildSubsystem::HomeOf(int32 Bay)
{
	// The bays' bikes face the roller door (+y), as the level script stands them.
	return FTransform(FRotator(0.0, 90.0, 0.0), TorqueShedLayout::BayAt(FMath::Clamp(Bay, 0, Bays - 1)));
}

FTransform UTorqueBuildSubsystem::BikeTransform(int32 Bay) const
{
	return IsAway(Bay) ? Bikes[Bay].Xf : HomeOf(Bay);
}

static_assert(UTorqueBuildSubsystem::WashLift == TorqueShedLayout::WashLiftId, "the wash bay lift's id, in both");

FString UTorqueBuildSubsystem::LiftName(int32 Lift)
{
	return Lift == WashLift ? FString(TEXT("The wash bay lift")) : FString::Printf(TEXT("Bay %d's lift"), Lift + 1);
}

int32 UTorqueBuildSubsystem::LiftOf(int32 Bay) const
{
	// shed_state.gd lift_of: within half a metre of a lift's middle.
	if (!Exists(Bay)) return INDEX_NONE;
	const FVector At = BikeTransform(Bay).GetLocation();
	for (int32 L = 0; L <= WashLift; ++L)
		if (IsLift(L) && FVector::Dist2D(At, TorqueShedLayout::LiftXf(L).GetLocation()) < 50.0) return L;
	return INDEX_NONE;
}

int32 UTorqueBuildSubsystem::BikeOnLift(int32 Lift, int32 Besides) const
{
	for (int32 I = 0; I < Bikes.Num(); ++I)
		if (I != Besides && LiftOf(I) == Lift) return I;
	return INDEX_NONE;
}

int32 UTorqueBuildSubsystem::PushedBy(int32 Peer) const
{
	if (Peer == 0) return INDEX_NONE;
	for (int32 I = 0; I < Bikes.Num(); ++I)
		if (Bikes[I].bExists && Bikes[I].Pusher == Peer) return I;
	return INDEX_NONE;
}

FTransform UTorqueBuildSubsystem::RigTransform()
{
	// dyno_room.gd CENTER (0, 0, 55.6), facing the far wall: through the doorway behind bays 2 and 3.
	return FTransform(FRotator(0.0, -90.0, 0.0), FVector(0.0, -940.0, 0.0));
}

FVector UTorqueBuildSubsystem::StrapPost(int32 Point)
{
	// STRAP_POSTS, in rig space (Godot x right, z back): front left, front right, rear left, rear right.
	static const FVector2D Posts[4] = {FVector2D(-0.95, -0.6), FVector2D(0.95, -0.6), FVector2D(-0.95, 0.8), FVector2D(0.95, 0.8)};
	const FVector2D P = Posts[FMath::Clamp(Point, 0, 3)];
	// Torque Shed's room frame is the shed's turned about: its x is our x, its z (back) our y.
	return RigTransform().GetLocation() + FVector(P.X * 100.0, P.Y * 100.0, 0.0);
}

bool UTorqueBuildSubsystem::OnDyno(int32 Bay) const
{
	return IsAway(Bay) && FVector::Dist2D(Bikes[Bay].Xf.GetLocation(), RigTransform().GetLocation()) < 30.0;
}

int32 UTorqueBuildSubsystem::DynoBay() const
{
	for (int32 I = 0; I < Bikes.Num(); ++I)
		if (OnDyno(I) && Bikes[I].Pusher == 0) return I;
	return INDEX_NONE;
}

const FTorqueDynoRun& UTorqueBuildSubsystem::DynoRunOf(int32 Bay) const
{
	static const FTorqueDynoRun None;
	return HasDynoRun(Bay) ? Bikes[Bay].DynoRuns.Last() : None;
}

double UTorqueBuildSubsystem::SharedNow() const
{
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World) return 0.0;
	if (const AGameStateBase* GS = World->GetGameState()) return GS->GetServerWorldTimeSeconds();
	return World->GetTimeSeconds();
}

int32 UTorqueBuildSubsystem::HeldBy(int32 Peer) const
{
	for (const TPair<int32, FTorqueItem>& KV : Items)
		if (KV.Value.At == ETorqueAt::Held && KV.Value.Ref == Peer) return KV.Key;
	return 0;
}

int32 UTorqueBuildSubsystem::ItemAtSpot(FName Spot) const
{
	if (Spot.IsNone()) return 0;
	for (const TPair<int32, FTorqueItem>& KV : Items)
		if (KV.Value.At == ETorqueAt::Spot && KV.Value.Spot == Spot) return KV.Key;
	return 0;
}

FString UTorqueBuildSubsystem::ItemTitle(int32 Id) const
{
	const FTorqueItem* It = Items.Find(Id);
	return It ? TorqueShedParts::PartName(It->Part) : FString();
}

FString UTorqueBuildSubsystem::ConditionText(int32 Id) const
{
	const FTorqueItem* It = Items.Find(Id);
	if (!It) return FString();
	if (It->bBoxed) return TEXT("Brand new, still in its box");
	FString PaintText = TEXT("factory finish");
	if (It->bPrimerTop) PaintText = TEXT("grey primer");
	else if (!It->Finish.IsNone())
	{
		const FLinearColor Colour = It->Colour;
		const int32 Idx = Paints().IndexOfByPredicate([Colour](const FLinearColor& C) { return C.Equals(Colour, 0.01f); });
		PaintText = FString::Printf(TEXT("%s %s"), *FinishName(It->Finish), *PaintName(Idx).ToLower());
	}
	FString Text = FString::Printf(TEXT("Condition %d%%   Rust %d%%   Dirt %d%%   Wear %d%%   %s"), FMath::RoundToInt(Health(*It) * 100.0f),
		FMath::RoundToInt(It->Rust * 100.0f), FMath::RoundToInt(It->Dirt * 100.0f), FMath::RoundToInt(It->Wear * 100.0f), *PaintText);
	const FString Job = PaintJobText(*It);
	if (!Job.IsEmpty()) Text += TEXT("   ") + Job;
	return Text;
}

FString UTorqueBuildSubsystem::PaintJobText(const FTorqueItem& It) const
{
	// shed_state finish_text and coat_text.
	FString Text;
	if (It.bPrimerTop)
	{
		Text = FString::Printf(TEXT("In primer (%d%%): ready for its colour coat"), FMath::RoundToInt(It.Primer * 100.0f));
		if (It.PaintQ >= 0.0f) Text += FString::Printf(TEXT(", the old paint job (%d%%) under it"), FMath::RoundToInt(It.PaintQ * 100.0f));
	}
	else if (It.PaintQ >= 0.0f)
	{
		Text = FString::Printf(TEXT("Paint job: %s (%d%%)"), *TBI::JobWord(It.PaintQ), FMath::RoundToInt(It.PaintQ * 100.0f));
		if (!It.MixName.IsEmpty())
			Text += FString::Printf(TEXT(", %s (%s, %d %s)"), *It.MixName, *TorquePaintMix::EffectName(It.MixEffect).ToLower(), It.MixCoats,
				It.MixCoats == 1 ? TEXT("coat") : TEXT("coats"));
		if (It.Primer > 0.0f) Text += TEXT(", over primer");
		if (IsLacquer(It.Coat)) Text += TEXT(", lacquered");
		if (It.Flat >= 0.08f) Text += TEXT(", ") + TorquePaintMix::SheenName(It.Flat) + TEXT(" lacquer");
		if (It.Mask != 0) Text += TEXT(", masked ") + FString(TorquePaintMix::PatternName(It.Mask)).ToLower();
		if (It.Tape != 0) Text += TEXT(", taped for a ") + FString(TorquePaintMix::PatternName(It.Tape)).ToLower() + TEXT(" (right mouse peels it)");
	}
	if (!It.Coat.IsNone())
	{
		const FString CoatText = FString::Printf(TEXT("Protective coat: %s, %s (%d%%)"), *CoatName(It.Coat).ToLower(), *TBI::JobWord(It.CoatQ),
			FMath::RoundToInt(It.CoatQ * 100.0f));
		Text = Text.IsEmpty() ? CoatText : Text + TEXT("   ") + CoatText;
	}
	return Text;
}

float UTorqueBuildSubsystem::RepairTime(const TArray<int32>& Ids) const
{
	float Worst = 0.0f;
	for (int32 Id : Ids)
		if (const FTorqueItem* It = Items.Find(Id)) Worst = FMath::Max(Worst, It->Rust + It->Wear);
	return 2.0f + 4.0f * Worst + 0.6f * FMath::Max(0, Ids.Num() - 1);
}

float UTorqueBuildSubsystem::DirtOf(int32 Bay, FName Slot) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	return It ? It->Dirt : 0.0f;
}

float UTorqueBuildSubsystem::RustOf(int32 Bay, FName Slot) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	return It ? It->Rust : 0.0f;
}

FString UTorqueBuildSubsystem::BadgeOf(int32 Bay) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(FName(TEXT("tank")))));
	return It ? It->Badge : FString();
}

bool UTorqueBuildSubsystem::PaintOf(int32 Bay, FName Slot, FLinearColor& Out) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	if (!It) return false;
	if (Repainted(*It))
	{
		float Metallic = 0.0f, Roughness = 0.5f;
		Out = ShownColour(*It);
		FinishLook(*It, Out, Metallic, Roughness);
		return true;
	}
	// A catalogue part the model has no shape for yet: the stock part tinted the catalogue colour.
	FName FitSlot, Option;
	if (!TorqueShedParts::IsStock(It->Part) && !TorqueShedParts::ModelOption(It->Part, It->Kind, FitSlot, Option))
	{
		Out = It->Colour;
		return true;
	}
	return false;
}

bool UTorqueBuildSubsystem::FinishLook(const FTorqueItem& It, FLinearColor& OutColour, float& OutMetallic, float& OutRoughness)
{
	if (!Repainted(It)) return false;
	FLinearColor C = ShownColour(It);
	if (It.bPrimerTop)
	{
		OutColour = C;
		OutMetallic = 0.0f;
		OutRoughness = 0.7f;
		return true;
	}
	const FName F = It.Finish.IsNone() ? FName(TEXT("gloss")) : It.Finish;
	float Metal = 0.05f, Rough = 0.12f;
	if (F == FName(TEXT("matte")))
	{
		Metal = 0.0f;
		Rough = 0.9f;
		C = C * 0.92f;   // Color.darkened(0.08)
	}
	else if (F == FName(TEXT("metallic")))
	{
		Metal = 0.8f;
		Rough = 0.32f;
		C = C + (FLinearColor::White - C) * 0.08f;   // Color.lightened(0.08)
	}
	else if (F == FName(TEXT("chrome")))
	{
		Metal = 1.0f;
		Rough = 0.1f;
		C = FMath::Lerp(C, FLinearColor(0.97f, 0.97f, 0.99f), 0.7f);
	}
	else if (F == FName(TEXT("patina")))
	{
		Metal = 0.15f;
		Rough = 0.78f;
		FLinearColor Hsv = C.LinearRGBToHSV();
		Hsv.G *= 0.55f;
		Hsv.B *= 0.8f;
		C = Hsv.HSVToLinearRGB();
	}
	// A lacquer's flattener (the mixing bay's satin and matt clears): the shine taken off.
	if (It.Flat > 0.0f) Rough = FMath::Lerp(Rough, 0.8f, FMath::Clamp(It.Flat / 0.4f, 0.0f, 1.0f));
	C.A = 1.0f;
	OutColour = C;
	OutMetallic = Metal;
	OutRoughness = Rough;
	return true;
}

bool UTorqueBuildSubsystem::EffectLook(const FTorqueItem& It, TorquePaintMix::FPaintLook& Out)
{
	namespace TPM = TorquePaintMix;
	if (!Repainted(It)) return false;
	Out = TPM::FPaintLook();
	// An effect paint from the mixing bay (pearl, candy, flake, chameleon): its spec.
	static const FName Effects[] = {FName(TEXT("pearl")), FName(TEXT("candy")), FName(TEXT("flake")), FName(TEXT("chameleon"))};
	bool bEffect = false;
	for (const FName& E : Effects) bEffect = bEffect || It.MixEffect == E;
	TPM::FTinMix Mix;
	bEffect = bEffect && !It.bPrimerTop && !It.MixCode.IsEmpty() && TPM::ParseCode(It.MixCode, Mix);
	const bool bMasked = It.Mask != 0 && !It.bPrimerTop;
	const bool bTaped = It.Tape != 0;
	if (!bEffect && !bMasked && !bTaped) return false;
	if (bEffect)
	{
		Out.Spec = TPM::Spec(Mix, It.MixName);
		Out.Spec.Colour = It.Colour;   // as sprayed (a candy's coats deepen it), not lightened as a plain metallic is
		// A satin or matt clear over it takes the shine off, as it does a plain paint.
		if (It.Flat > 0.0f) Out.Spec.Rough = FMath::Lerp(Out.Spec.Rough, 0.8f, FMath::Clamp(It.Flat / 0.4f, 0.0f, 1.0f));
	}
	else
	{
		// A plain paint under a stripe or tape: as its finish draws it, no effects.
		FLinearColor Shown = ShownColour(It);
		float Metallic = 0.05f, Roughness = 0.12f;
		FinishLook(It, Shown, Metallic, Roughness);
		Out.Spec.Colour = Shown;
		Out.Spec.Metal = Metallic;
		Out.Spec.Rough = Roughness;
		Out.Spec.Flake = 0.0f;
	}
	Out.Spec.Colour.A = 1.0f;
	// paint_mix.gd fx_pass: the masked colour, or (taped, not sprayed over yet) the tape where the
	// stripe will be.
	if (bMasked)
	{
		Out.Pattern = It.Mask;
		Out.MaskColour = It.MaskColour;
		Out.MaskMetal = It.MaskMetal;
		Out.MaskRough = It.MaskRough;
	}
	if (bTaped)
	{
		if (Out.Pattern == 0)
		{
			Out.Pattern = It.Tape;
			Out.MaskColour = Out.Spec.Colour;
			Out.MaskMetal = Out.Spec.Metal;
			Out.MaskRough = Out.Spec.Rough;
		}
		Out.bTape = true;
		Out.TapeColour = TPM::TapeColourOn(Out.Spec.Colour);
	}
	return true;
}

bool UTorqueBuildSubsystem::EffectOf(int32 Bay, FName Slot, TorquePaintMix::FPaintLook& Out) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	return It && EffectLook(*It, Out);
}

bool UTorqueBuildSubsystem::FinishOf(int32 Bay, FName Slot, FLinearColor& OutColour, float& OutMetallic, float& OutRoughness) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	return It && FinishLook(*It, OutColour, OutMetallic, OutRoughness);
}

const TArray<FLinearColor>& UTorqueBuildSubsystem::Paints()
{
	static const TArray<FLinearColor> L = {
		FLinearColor(0.78f, 0.09f, 0.08f), FLinearColor(0.1f, 0.35f, 0.8f), FLinearColor(0.95f, 0.6f, 0.05f), FLinearColor(0.1f, 0.6f, 0.3f),
		FLinearColor(0.55f, 0.15f, 0.7f), FLinearColor(0.9f, 0.9f, 0.9f), FLinearColor(0.06f, 0.06f, 0.06f), FLinearColor(0.0f, 0.6f, 0.7f),
		FLinearColor(0.72f, 0.73f, 0.75f), FLinearColor(0.8f, 0.62f, 0.2f), FLinearColor(0.95f, 0.85f, 0.1f), FLinearColor(0.95f, 0.4f, 0.6f),
		FLinearColor(0.42f, 0.06f, 0.1f), FLinearColor(0.45f, 0.28f, 0.14f), FLinearColor(0.9f, 0.85f, 0.7f), FLinearColor(0.35f, 0.4f, 0.18f)};
	return L;
}

FString UTorqueBuildSubsystem::PaintName(int32 Index)
{
	static const TCHAR* const Names[] = {TEXT("Red"), TEXT("Blue"), TEXT("Orange"), TEXT("Green"), TEXT("Purple"), TEXT("White"),
		TEXT("Black"), TEXT("Teal"), TEXT("Silver"), TEXT("Gold"), TEXT("Yellow"), TEXT("Pink"), TEXT("Burgundy"), TEXT("Brown"),
		TEXT("Cream"), TEXT("Olive")};
	return Index >= 0 && Index < (int32)UE_ARRAY_COUNT(Names) ? FString(Names[Index]) : FString(TEXT("Custom"));
}

const TArray<FName>& UTorqueBuildSubsystem::Finishes()
{
	static const TArray<FName> L = {FName(TEXT("gloss")), FName(TEXT("matte")), FName(TEXT("metallic")), FName(TEXT("chrome")), FName(TEXT("patina"))};
	return L;
}

FString UTorqueBuildSubsystem::FinishName(FName Finish)
{
	if (Finish == FName(TEXT("matte"))) return TEXT("Matte");
	if (Finish == FName(TEXT("metallic"))) return TEXT("Metallic flake");
	if (Finish == FName(TEXT("chrome"))) return TEXT("Chrome");
	if (Finish == FName(TEXT("patina"))) return TEXT("Old patina");
	return TEXT("Gloss");
}

const TArray<FName>& UTorqueBuildSubsystem::Coats()
{
	static const TArray<FName> L = {FName(TEXT("clear")), FName(TEXT("satin_clear")), FName(TEXT("enamel_black")), FName(TEXT("enamel_silver"))};
	return L;
}

FString UTorqueBuildSubsystem::CoatName(FName Coat)
{
	if (Coat == FName(TEXT("clear"))) return TEXT("Clear lacquer");
	if (Coat == FName(TEXT("satin_clear"))) return TEXT("Satin clear lacquer");
	if (Coat == FName(TEXT("enamel_black"))) return TEXT("Black engine enamel");
	if (Coat == FName(TEXT("enamel_silver"))) return TEXT("Silver engine enamel");
	return Coat.ToString();
}

bool UTorqueBuildSubsystem::IsLacquer(FName Coat)
{
	return Coat == FName(TEXT("clear")) || Coat == FName(TEXT("satin_clear"));
}

// --- The rules (shed_state check) ------------------------------------------------------------------

FString UTorqueBuildSubsystem::PartsOnIt(const FHost& Host, FName Slot) const
{
	if (!Host.Slots) return FString();
	for (const FName& Dep : TorqueShedParts::Dependents(Slot))
	{
		// The tyre comes off with its wheel.
		const bool bTyreOfWheel = TorqueShedParts::TyreOf(Slot) == Dep;
		if (Host.Slots->FindRef(Dep) != 0 && !bTyreOfWheel)
			return FString::Printf(TEXT("Take the %s off first."), *TBI::Lower(TorqueShedParts::SlotName(Dep)));
	}
	// Parts that bolt onto this one as well as their own mounts (fairing-mount mirrors).
	for (const TPair<FName, int32>& KV : *Host.Slots)
	{
		const FTorqueItem* On = Items.Find(KV.Value);
		if (!On) continue;
		if (TorqueShedParts::PartMounts(KV.Key, On->Part).Contains(Slot) && !TorqueShedParts::MountsOf(KV.Key).Contains(Slot))
			return FString::Printf(TEXT("Take the %s off first."), *TBI::Lower(TorqueShedParts::SlotName(KV.Key)));
	}
	return FString();
}

FName UTorqueBuildSubsystem::InstallSlot(const FTorqueShedRequest& R, const FTorqueItem& It, FString& Error) const
{
	TArray<FName> Candidates = TorqueShedParts::SlotsForKind(It.Kind);
	if (Candidates.Num() == 0) { Error = TEXT("That doesn't bolt on anywhere."); return NAME_None; }
	const bool bOnEngine = TorqueShedParts::IsEngineSlot(Candidates[0]);
	if (R.Engine != 0 && !bOnEngine) { Error = TEXT("That doesn't go on an engine."); return NAME_None; }
	const FHost H = HostOf(R.Bay, Candidates[0], R.Engine);
	if (!H.Slots) { Error = bOnEngine ? TEXT("Fit an engine first.") : TEXT("That bike has gone."); return NAME_None; }
	// The slot looked at first, then the same side (front or rear), then the rest.
	const FName Looked = R.Slot;
	const FString LookedText = Looked.ToString();
	const FString Side = LookedText.StartsWith(TEXT("front_")) ? TEXT("front_") : (LookedText.StartsWith(TEXT("rear_")) ? TEXT("rear_") : TEXT(""));
	auto Rank = [Looked, Side](const FName& S)
	{
		if (S == Looked) return 0;
		if (!Side.IsEmpty() && S.ToString().StartsWith(Side)) return 1;
		return 2;
	};
	Candidates.StableSort([&Rank](const FName& A, const FName& B) { return Rank(A) < Rank(B); });
	FName MissingMount;
	for (const FName& S : Candidates)
	{
		if (H.Slots->FindRef(S) != 0) continue;
		bool bOk = true;
		for (const FName& M : TorqueShedParts::PartMounts(S, It.Part))
		{
			if (M == TBI::EngineKind() && bOnEngine) continue;
			if (H.Slots->FindRef(M) == 0)
			{
				bOk = false;
				if (MissingMount.IsNone()) MissingMount = M;
			}
		}
		if (bOk) return S;
	}
	if (!MissingMount.IsNone()) Error = FString::Printf(TEXT("Fit the %s first."), *TBI::Lower(TorqueShedParts::SlotName(MissingMount)));
	else Error = FString::Printf(TEXT("There's already a %s fitted."), *TBI::Lower(TorqueShedParts::KindName(It.Kind)));
	return NAME_None;
}

FString UTorqueBuildSubsystem::Check(int32 Peer, const FTorqueShedRequest& R) const
{
	const int32 Held = HeldBy(Peer);
	const FTorqueItem* HeldIt = Items.Find(Held);
	const ETorqueShedAct A = R.Kind;
	if (IsPaintBayAct(A)) return CheckPaintBay(Peer, R);
	// A tin of paint in your hands is a full pair of hands.
	if ((A == ETorqueShedAct::Take || A == ETorqueShedAct::Remove || A == ETorqueShedAct::GrabBike) && Paint.Held.Contains(Peer))
		return TEXT("Put the tin of paint down first.");
	if ((A == ETorqueShedAct::Take || A == ETorqueShedAct::Remove || A == ETorqueShedAct::GrabBike) && Paint.TapeBy == Peer)
		return TEXT("Hang the masking tape back on its hook first.");
	// [kit] The extinguisher or the engine stand in your hands is a full pair of hands too.
	if ((A == ETorqueShedAct::Take || A == ETorqueShedAct::Remove || A == ETorqueShedAct::GrabBike || A == ETorqueShedAct::GrabStand
		|| A == ETorqueShedAct::PaintTapeTake) && ExtinguisherBy == Peer)
		return TEXT("Put the fire extinguisher back first (F).");
	if ((A == ETorqueShedAct::Take || A == ETorqueShedAct::Remove || A == ETorqueShedAct::GrabBike || A == ETorqueShedAct::Extinguisher)
		&& StandPusher == Peer && A != ETorqueShedAct::ReleaseStand)
		return TEXT("Let go of the engine stand first (F).");
	if (IsKitAct(A)) return CheckKit(Peer, R);
	const bool bSlotWork = A == ETorqueShedAct::Unbolt || A == ETorqueShedAct::Bolt || A == ETorqueShedAct::Remove;
	// (Not with the bike up on a lift: then the low parts are at chest height.)
	if (bSlotWork && R.Engine == 0 && TorqueShedParts::IsLowSlot(R.Slot) && !R.bCrouched && !LiftRaised(R.Bay))
		return TEXT("Crouch down to reach it (C).");
	if (bSlotWork || A == ETorqueShedAct::Install)
	{
		if (R.Engine != 0)
		{
			const FTorqueItem* E = Items.Find(R.Engine);
			if (!E || E->Kind != TBI::EngineKind()) return TEXT("That engine has gone.");
		}
		else if (!Exists(R.Bay))
			return TEXT("That bike has gone.");
	}
	switch (A)
	{
	case ETorqueShedAct::Unbolt:
	case ETorqueShedAct::Bolt:
	case ETorqueShedAct::Remove:
	{
		const FHost H = HostOf(R.Bay, R.Slot, R.Engine);
		const int32 On = H.Slots ? H.Slots->FindRef(R.Slot) : 0;
		if (On == 0) return TEXT("That part isn't fitted.");
		const bool bBolted = H.Bolted && H.Bolted->FindRef(R.Slot);
		if (A != ETorqueShedAct::Bolt && R.Engine == 0 && TorqueShedParts::IsTyreSlot(R.Slot))
			return TEXT("The tyre comes off with its wheel: take the wheel off and change the tyre on the tyre changer.");
		if (A == ETorqueShedAct::Unbolt && !bBolted) return TEXT("It's already unbolted.");
		if (A == ETorqueShedAct::Bolt && bBolted) return TEXT("It's already bolted up.");
		if (A == ETorqueShedAct::Remove)
		{
			const FString Access = TBI::EngineAccess(Items, R, R.Slot);
			if (!Access.IsEmpty()) return Access;
			if (Held != 0) return TEXT("Your hands are full.");
			if (bBolted) return TEXT("Unbolt it with the wrench first.");
			const FString First = PartsOnIt(H, R.Slot);
			if (!First.IsEmpty()) return First;
		}
		return FString();
	}
	case ETorqueShedAct::Install:
	{
		if (!HeldIt) return TEXT("You're not holding a part.");
		if (HeldIt->bBoxed) return TEXT("Unpack it first.");
		if (HeldIt->Kind == FName(TEXT("tyre")) && R.Engine == 0)
			return TEXT("Tyres go on at the tyre changer: take the wheel off and put it on the changer by the workbench.");
		FString Error;
		const FName Where = InstallSlot(R, *HeldIt, Error);
		if (Where.IsNone()) return Error;
		if (R.Engine == 0 && TorqueShedParts::IsLowSlot(Where) && !R.bCrouched) return TEXT("Crouch down to reach it (C).");
		if (R.Engine == 0 && TBI::IsFitSlot(Where))
		{
			// The frame, engine, tank and fork: what fitting's layout makes of it (won't fit stops it).
			FString Tier, Reason;
			TArray<FString> Work;
			if (FitTier(R.Bay, Where, HeldIt->Part, Tier, Reason, Work) && Tier == TEXT("won't fit"))
				return Reason.IsEmpty() ? FString(TEXT("That won't fit this bike.")) : Reason;
		}
		return TBI::EngineAccess(Items, R, Where);
	}
	case ETorqueShedAct::Take:
		if (Held != 0) return TEXT("Your hands are full.");
		if (R.Item != 0)
		{
			const FTorqueItem* It = Items.Find(R.Item);
			if (!It || It->At != ETorqueAt::Floor) return TEXT("Nothing there.");
		}
		else if (ItemAtSpot(R.Spot) == 0)
			return TEXT("Nothing there.");
		return FString();
	case ETorqueShedAct::Drop:
		if (!HeldIt) return TEXT("You're not holding a part.");
		if (!TorqueShedLayout::OnSite(R.Place.GetLocation())) return TEXT("You can't put it there.");
		return FString();
	case ETorqueShedAct::Place:
	{
		if (!HeldIt) return TEXT("You're not holding a part.");
		if (!TBI::IsSpot(R.Spot)) return TEXT("You can't put it there.");
		if (ItemAtSpot(R.Spot) != 0) return TEXT("That spot is taken.");
		if (TBI::SpotIs(R.Spot, ETorquePlace::Stand) && (HeldIt->Kind != TBI::EngineKind() || HeldIt->bBoxed))
			return TEXT("Only an engine goes on the engine stand.");
		if ((TBI::SpotIs(R.Spot, ETorquePlace::Washer) || TBI::SpotIs(R.Spot, ETorquePlace::Repair) || TBI::SpotIs(R.Spot, ETorquePlace::Changer) ||
			TBI::SpotIs(R.Spot, ETorquePlace::Hook)) && HeldIt->bBoxed)
			return TEXT("Unpack it first.");
		if (TBI::SpotIs(R.Spot, ETorquePlace::Changer) && HeldIt->Kind != FName(TEXT("wheel")))
			return TEXT("Only a wheel goes on the tyre changer. Fit tyres to a wheel on it.");
		return FString();
	}
	case ETorqueShedAct::Bin:
	{
		if (!HeldIt) return TEXT("You're not holding a part.");
		const FName Which = R.Bin.IsNone() ? FName(TEXT("general")) : R.Bin;
		if (!TorqueShedParts::Bins().Contains(Which)) return TEXT("That's not a bin.");
		if (Which == FName(TEXT("general"))) return FString();
		if (HeldIt->bBoxed) return TEXT("Unpack it first.");
		if (HeldIt->Kind == TBI::EngineKind())
			for (const TPair<FName, int32>& KV : HeldIt->Slots)
				if (KV.Value != 0) return TEXT("Strip the engine down first: there are parts still on it.");
		const FName Want = TorqueShedParts::BinFor(HeldIt->Part);
		if (Want != Which)
		{
			const FString Where = Want == FName(TEXT("general")) ? FString(TEXT("general waste")) : TorqueShedParts::BinName(Want).ToLower();
			return FString::Printf(TEXT("That's %s: %s."), *TorqueShedParts::MaterialName(HeldIt->Part).ToLower(), *Where);
		}
		return FString();
	}
	case ETorqueShedAct::Clean:
		for (int32 Id : R.Items)
		{
			const FTorqueItem* It = Items.Find(Id);
			if (It && It->At != ETorqueAt::Held && It->Dirt >= 0.02f && !It->bBoxed) return FString();
		}
		return TEXT("It's already clean.");
	case ETorqueShedAct::Repair:
	{
		if (R.Items.Num() == 0) return TEXT("Nothing on it needs restoring.");
		bool bAny = false;
		for (int32 Id : R.Items)
		{
			const FTorqueItem* It = Items.Find(Id);
			if (!It) continue;
			if (!(It->At == ETorqueAt::Spot && TBI::SpotIs(It->Spot, ETorquePlace::Repair))) return TEXT("Put the part on a repair machine first.");
			if (It->bBoxed) return TEXT("Unpack it first.");
			if (It->Rust >= 0.02f || It->Wear >= 0.02f) bAny = true;
		}
		return bAny ? FString() : FString(TEXT("It's already in top condition."));
	}
	case ETorqueShedAct::Paint:
	case ETorqueShedAct::Prime:
	case ETorqueShedAct::Coat:
	{
		// shed_state check "paint" / "prime" and _coat_check.
		const FTorqueItem* It = Items.Find(R.Item);
		if (!It) return TEXT("That part has gone.");
		if (It->At == ETorqueAt::Held) return TEXT("Put it down first.");
		if (It->bBoxed) return TEXT("Unpack it first.");
		const bool bInBooth = It->At == ETorqueAt::Spot && TBI::SpotIs(It->Spot, ETorquePlace::Hook);
		if (R.Kind == ETorqueShedAct::Coat)
		{
			if (!Coats().Contains(R.Finish)) return TEXT("That's not a coat the spray gun has.");
			const bool bCanLacquer = IsLacquer(R.Finish) && !R.bGun && It->PaintQ >= 0.0f;
			if (IsLacquer(R.Finish) && It->bPrimerTop) return TEXT("Lacquer goes over the colour, not bare primer. Spray the colour coat on first.");
			if (IsLacquer(R.Finish) && !R.bGun && It->PaintQ < 0.0f && !bInBooth) return TEXT("Lacquer from a can goes over fresh paint. Spray its colour on first.");
			if (!bInBooth && !bCanLacquer) return TEXT("Protective coats go on with the spray gun in the paint shop: hang it on a hook there.");
			if (It->Rust > MaxPaintRust) return TEXT("Too rusty to coat. Get the rust off it first.");
			return FString();
		}
		if (R.bGun && !bInBooth) return TEXT("The spray gun runs off the paint booth's air line. Hang the part in the booth.");
		if (R.Kind == ETorqueShedAct::Paint && R.Finish == CupFinish())
		{
			// Paint mixed in the mixing bay, from the gun's cup.
			if (!R.bGun) return TEXT("Mixed paint goes on with the spray gun: fill its cup from the tin.");
			if (It->Rust > MaxPaintRust) return TEXT("Too rusty to paint. Grind it back on the repair bench first.");
			return CheckCup(Peer, *It);
		}
		if (It->Rust > MaxPaintRust)
			return R.Kind == ETorqueShedAct::Prime ? FString(TEXT("Too rusty to prime. Grind it back on the repair bench first."))
				: FString(TEXT("Too rusty to paint. Grind it back on the repair bench first."));
		if (R.Kind == ETorqueShedAct::Prime)
			return It->bPrimerTop ? FString(TEXT("It's already in primer. Spray its colour on.")) : FString();
		const int32 Idx = FMath::Clamp(R.Colour, 0, Paints().Num() - 1);
		const FName Finish = Finishes().Contains(R.Finish) ? R.Finish : FName(TEXT("gloss"));
		if (!It->bPrimerTop && It->Finish == Finish && It->Colour.Equals(Paints()[Idx], 0.001f))
			return FString::Printf(TEXT("It's already %s %s."), *FinishName(Finish).ToLower(), *PaintName(Idx).ToLower());
		return FString();
	}
	case ETorqueShedAct::Wash:
		if (!TorqueShedLayout::InWashBay(R.Place.GetLocation())) return TEXT("Pressure washing is done in the wash bay. Wheel it in there first.");
		for (int32 Id : R.Items)
		{
			const FTorqueItem* It = Items.Find(Id);
			if (It && It->Dirt >= 0.005f && !It->bBoxed) return FString();
		}
		return TEXT("It's already clean.");
	case ETorqueShedAct::OrderPart:
		if (!TorqueShedParts::IsPart(R.Part) || TorqueShedParts::IsStock(R.Part)) return TEXT("Unknown part.");
		if (TorqueShedParts::SlotsForKind(TorqueShedParts::PartKind(R.Part)).Num() == 0) return TEXT("That's not sold here yet.");
		if (FreeDelivery() == INDEX_NONE) return TEXT("The delivery area outside is full. Bring some boxes in first.");
		return FString();
	case ETorqueShedAct::OrderBike:
		if (FreeBay() == INDEX_NONE) return TEXT("Every bay is full. Ride a bike out or strip one down first.");
		if (!R.Text.IsEmpty())
		{
			// The configurator's order: one the host can read, of a style it knows.
			const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
			TorqueFit::Shop::Order O;
			if (!Lay || R.Text.Len() > 8000 || !TorqueFit::Shop::ParseOrder(*Lay, TBI::Std(R.Text), O)) return TEXT("That bike can't be built.");
		}
		return FString();
	case ETorqueShedAct::Unpack:
	{
		const FTorqueItem* It = Items.Find(R.Item);
		if (!It || !It->bBoxed) return TEXT("Nothing to unpack.");
		if (It->At == ETorqueAt::Held) return TEXT("Put it down first.");
		return FString();
	}
	case ETorqueShedAct::StartBuild:
		if (!HeldIt || HeldIt->Kind != TBI::FrameSlot()) return TEXT("Hold a frame to start a bike from scratch.");
		if (HeldIt->bBoxed) return TEXT("Unpack it first.");
		if (R.Bay < 0 || R.Bay >= Bays) return TEXT("Take it to one of the bays.");
		if (Exists(R.Bay)) return FString::Printf(TEXT("There's already a bike in bay %d."), R.Bay + 1);
		return FString();
	case ETorqueShedAct::TyreFit:
	case ETorqueShedAct::TyreRemove:
	{
		if (!TBI::SpotIs(R.Spot, ETorquePlace::Changer)) return TEXT("Use the tyre changer.");
		const FTorqueItem* Wheel = Items.Find(ItemAtSpot(R.Spot));
		if (!Wheel) return TEXT("Put a wheel on the tyre changer first.");
		if (A == ETorqueShedAct::TyreFit)
		{
			if (!HeldIt || HeldIt->Kind != FName(TEXT("tyre"))) return TEXT("Hold a tyre to fit it.");
			if (HeldIt->bBoxed) return TEXT("Unpack it first.");
			if (Wheel->Tyre != 0) return TEXT("There's already a tyre on that wheel. Lever it off first.");
		}
		else
		{
			if (Wheel->Tyre == 0) return TEXT("There's no tyre on that wheel.");
			if (Held != 0) return TEXT("Your hands are full.");
		}
		return FString();
	}
	case ETorqueShedAct::RodeOut:
		return Exists(R.Bay) ? FString() : FString(TEXT("That bike has gone."));
	case ETorqueShedAct::Fabricate:
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (Held != 0) return TEXT("Put the part down first.");
		if (FabOwed(R.Bay).Num() == 0) return TEXT("It doesn't need any fabrication.");
		return FString();
	case ETorqueShedAct::GrabBike:
	{
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		const FTorqueShedBike& B = Bikes[R.Bay];
		if (B.Pusher != 0 && B.Pusher != Peer) return TEXT("Someone's already wheeling it.");
		if (PushedBy(Peer) != INDEX_NONE) return TEXT("You're already wheeling a bike.");
		if (LiftRaised(R.Bay)) return TEXT("It's up on the lift. Lower the lift first (F on the button post).");
		if (Held != 0) return TEXT("Put down what you're holding first.");
		// shed_state _wheel_check: it rolls on both wheels and tyres.
		static const TCHAR* const Rolling[] = {TEXT("front_wheel"), TEXT("rear_wheel"), TEXT("front_tyre"), TEXT("rear_tyre")};
		for (const TCHAR* Slot : Rolling)
			if (ItemOn(R.Bay, FName(Slot)) == 0) return TEXT("It won't roll without both wheels and tyres.");
		if (OnDyno(R.Bay) && Dyno.StrapsDone() > 0) return TEXT("Undo the straps first (hold the work button at each one).");
		if (OnDyno(R.Bay) && SharedNow() < DynoBusyUntil) return TEXT("It's mid-run. Wait for it to finish.");
		return FString();
	}
	case ETorqueShedAct::MoveBike:
	case ETorqueShedAct::ReleaseBike:
		if (!Exists(R.Bay) || Bikes[R.Bay].Pusher != Peer) return TEXT("You're not holding that bike.");
		if (!TorqueShedLayout::OnSite(R.Place.GetLocation())) return TEXT("You can't put it there.");
		if (A == ETorqueShedAct::ReleaseBike)
			for (int32 L = 0; L <= WashLift; ++L)
				if (IsLift(L) && LiftUp(L) && TorqueShedLayout::OntoLift(L, R.Place.GetLocation()))
					return L == WashLift ? FString(TEXT("The wash bay lift is up. Lower it first, then wheel the bike on."))
						: FString::Printf(TEXT("Bay %d's lift is up. Lower it first, then wheel the bike on."), L + 1);
		return FString();
	case ETorqueShedAct::ScrapBike:
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		if (Bikes[R.Bay].Pusher != 0) return TEXT("Someone's wheeling it.");
		if (OnDyno(R.Bay) && SharedNow() < DynoBusyUntil) return TEXT("It's mid-run. Wait for it to finish.");
		return FString();
	case ETorqueShedAct::Lift:
	{
		if (!IsLift(R.Item)) return TEXT("There's no lift there.");
		if (R.Colour != 0 && R.Colour != 1) return TEXT("Up or down?");
		const int32 On = BikeOnLift(R.Item);
		if (On != INDEX_NONE && Bikes[On].Pusher != 0)
			return R.Item == WashLift ? FString(TEXT("Someone's moving the bike on the wash bay lift.")) : FString::Printf(TEXT("Someone's moving the bike in bay %d."), R.Item + 1);
		return FString();
	}
	case ETorqueShedAct::DynoStrap:
	{
		const int32 On = DynoBay();
		if (R.Item < 0 || R.Item >= 4) return TEXT("There's no strap there.");
		if (On == INDEX_NONE) return TEXT("Wheel a bike onto the rig first.");
		if (Bikes[On].Pusher != 0) return TEXT("Let go of the bike first.");
		if (SharedNow() < DynoBusyUntil) return TEXT("It's mid-run. Wait for it to finish.");
		return FString();
	}
	case ETorqueShedAct::DynoFan:
	case ETorqueShedAct::DynoLights:
		return FString();
	case ETorqueShedAct::PaintTapeTake:
		if (Held != 0 || Paint.Held.Contains(Peer)) return TEXT("Your hands are full.");
		if (Paint.TapeBy == Peer) return TEXT("You've got the masking tape.");
		if (Paint.TapeBy != 0) return TEXT("Someone else has the masking tape.");
		return FString();
	case ETorqueShedAct::PaintTapePut:
		return Paint.TapeBy == Peer ? FString() : FString(TEXT("You're not holding the tape."));
	case ETorqueShedAct::PaintTapePattern:
		if (Paint.TapeBy != Peer) return TEXT("You're not holding the tape.");
		if (R.Colour < 1 || R.Colour > TorquePaintMix::TapePatterns) return TEXT("There's no pattern like that.");
		return FString();
	case ETorqueShedAct::PaintTape:
	{
		// paint_bay.gd paint_tape: on a painted part, put down (or on a bike), not taped already.
		if (Paint.TapeBy != Peer) return TEXT("Take the masking tape off its hook by the mixing bay door.");
		const FTorqueItem* It = Items.Find(R.Item);
		if (!It) return TEXT("That part has gone.");
		if (It->At == ETorqueAt::Held || It->bBoxed) return TEXT("Put it down (and unpack it) first.");
		if (It->PaintQ < 0.0f || It->bPrimerTop || It->Finish.IsNone()) return TEXT("Paint it first: the tape keeps its colour where the stripe goes.");
		if (It->Tape != 0) return TEXT("It's taped already. Spray it, or peel the tape off.");
		return FString();
	}
	case ETorqueShedAct::PaintPeel:
	{
		const FTorqueItem* It = Items.Find(R.Item);
		return It && It->Tape != 0 ? FString() : FString(TEXT("There's no tape on it."));
	}
	case ETorqueShedAct::Radio:
		if (TorqueShedRadio::Stations().Num() == 0) return TEXT("The radio has no stations.");
		if (!TorqueShedRadio::Stations().IsValidIndex(R.Colour)) return TEXT("No such station.");
		return FString();
	case ETorqueShedAct::DynoRun:
	{
		if (!Exists(R.Bay) || !OnDyno(R.Bay)) return TEXT("Wheel a bike onto the dyno first.");
		if (Bikes[R.Bay].Pusher != 0) return TEXT("Let go of it so it can be strapped down.");
		if (SharedNow() < DynoBusyUntil) return TEXT("It's mid-run. Wait for it to finish.");
		if (!Strapped()) return FString::Printf(TEXT("Strap it down first: %d of the 4 straps are done up."), Dyno.StrapsDone());
		const TArray<FName> Missing = MissingSlots(R.Bay);
		if (Missing.Num() > 0) return FString::Printf(TEXT("It won't run. Missing: %s."), *TBI::SlotList(Missing));
		const TArray<FName> Loose = LooseSlots(R.Bay);
		if (Loose.Num() > 0) return FString::Printf(TEXT("Bolt it up first: %s."), *TBI::SlotList(Loose));
		return FString();
	}
	case ETorqueShedAct::GeoSave:
		return TorqueGeometryMode::CheckSave(*this, R);
	case ETorqueShedAct::Tune:
	{
		// shed_state check "tune": the bike, the setting, the part that keeps it, what its parts allow.
		if (!Exists(R.Bay)) return TEXT("That bike has gone.");
		const std::string Key = TBI::Std(R.Text);
		std::string Home, Field;
		if (!TorqueFit::TuneHome(Key, Home, Field)) return TEXT("There's nothing like that to adjust.");
		if (Home != "bike" && ItemOn(R.Bay, FName(*TBI::Ue(Home))) == 0)
			return FString::Printf(TEXT("There's no %s fitted."), *TorqueShedParts::SlotName(FName(*TBI::Ue(Home))).ToLower());
		if (R.Item == 0 && !FMath::IsFinite(R.Amount)) return TEXT("Pick a setting.");
		FString Why;
		if (!WithTune(R.Bay, [&Why, &Key](const TorqueFit::BikeTune& T) { Why = TBI::Ue(T.Block(Key)); }))
			return TEXT("The dyno desk can't read this bike's parts.");
		return Why;
	}
	case ETorqueShedAct::Lighten:
	{
		const FTorqueItem* It = Items.Find(R.Item);
		if (!It) return TEXT("That part has gone.");
		if (It->bBoxed) return TEXT("Unpack it first.");
		if (It->bLightened) return TEXT("It's already been drilled and trimmed.");
		const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
		const std::string Id = TBI::Id(It->Part);
		const TorqueFit::Json* P = Lay ? Lay->Part(Id) : nullptr;
		if (!P) return TEXT("There's nothing to trim off that.");
		if (P->Flag("lightweight")) return TEXT("It's already as light as it goes.");
		const TorqueFit::BikeTune T(*Lay, TorqueFit::PartsMap(), TorqueFit::TuneMap());
		if (T.LightenCut(Id) <= 0.0) return TEXT("There's nothing to trim off that.");
		return FString();
	}
	default:
		return TEXT("The shed doesn't know how to do that.");
	}
}

// --- Doing it (shed_state _apply) -------------------------------------------------------------------

void UTorqueBuildSubsystem::OrdererOf(int32 Peer, FString& OutName, FLinearColor& OutTint) const
{
	// Their name and the colour they ride in (the jacket, the name over them), from their player state.
	OutName = TEXT("the workshop");
	OutTint = FLinearColor(0.98f, 0.85f, 0.2f);   // deliveries.json's first sticky note colour
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS) return;
	const APlayerState* Found = nullptr;
	for (const APlayerState* PS : GS->PlayerArray)
		if (PS && PS->GetPlayerId() == Peer) Found = PS;
	if (!Found && GS->PlayerArray.Num() == 1) Found = GS->PlayerArray[0];   // alone: it's you
	if (!Found) return;
	OutName = Found->GetPlayerName();
	if (const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(Found)) OutTint = FLinearColor::FromSRGBColor(TPS->ColourValue());
}

bool UTorqueBuildSubsystem::Request(const FTorqueShedRequest& R, FString& WhyNot)
{
	WhyNot = Check(Me(), R);
	if (!WhyNot.IsEmpty()) return false;
	const int32 HeldBefore = HeldBy(Me());   // [sound] for OnRequestDone
	if (IsClient())
	{
		if (Forward.Execute(R)) { OnRequestDone.Broadcast(R, HeldBefore); return true; }
		WhyNot = TEXT("The host didn't get that: try again.");
		return false;
	}
	if (!Apply(Me(), R, WhyNot)) return false;
	OnRequestDone.Broadcast(R, HeldBefore);
	return true;
}

bool UTorqueBuildSubsystem::Apply(int32 Peer, const FTorqueShedRequest& R, FString& WhyNot)
{
	if (IsClient()) { WhyNot = TEXT("Only the host changes the shed."); return false; }
	WhyNot = Check(Peer, R);
	if (!WhyNot.IsEmpty())
	{
		OnTell.Broadcast(Peer, WhyNot, false);
		return false;
	}
	const FString Text = DoApply(Peer, R);
	if (!Text.IsEmpty()) OnTell.Broadcast(Peer, Text, true);
	return true;
}

FString UTorqueBuildSubsystem::DoApply(int32 Peer, const FTorqueShedRequest& R)
{
	FString Text;
	const int32 Held = HeldBy(Peer);
	// What changed, for who to redraw: bays whose parts changed, bays whose looks changed, and
	// whether anything off the bikes moved.
	TSet<int32> PartsOf, LooksOf;
	bool bItems = false;
	auto Touch = [this, &LooksOf, &bItems](int32 Id)
	{
		const int32 On = BayOf(Id);
		if (On == INDEX_NONE) bItems = true;
		else LooksOf.Add(On);
	};
	if (IsKitAct(R.Kind))
	{
		// [kit] The shed's lights, roller door, engine stand and extinguisher (TorqueShedKit.cpp).
		Text = ApplyKit(Peer, R);
		if (R.Kind == ETorqueShedAct::ReleaseStand) OnItems.Broadcast();   // an engine on it sits where it stopped
		if (R.Kind != ETorqueShedAct::ExtinguisherSpray) MarkSave();
		OnHostChange.Broadcast();
		return Text;
	}
	if (IsPaintBayAct(R.Kind))
	{
		// The mixing bay (TorquePaintBay.cpp): its tins, bench, shaker and cups.
		Text = ApplyPaintBay(Peer, R);
		if (R.Kind != ETorqueShedAct::PaintHook) OnItems.Broadcast();   // a hook's height is read as it's drawn
		MarkSave();
		OnHostChange.Broadcast();
		return Text;
	}
	switch (R.Kind)
	{
	case ETorqueShedAct::Unbolt:
	case ETorqueShedAct::Bolt:
	{
		const FHost H = HostOf(R.Bay, R.Slot, R.Engine);
		const bool bOn = R.Kind == ETorqueShedAct::Bolt;
		H.Bolted->Add(R.Slot, bOn);
		Text = FString::Printf(TEXT("%s %s"), *TorqueShedParts::SlotName(R.Slot), bOn ? TEXT("bolted up") : TEXT("unbolted"));
		if (bOn && R.Engine == 0 && IsComplete(R.Bay)) Text += TEXT(". The bike is ready to ride!");
		Touch(H.Slots->FindRef(R.Slot));
		break;
	}
	case ETorqueShedAct::Remove:
	{
		const FHost H = HostOf(R.Bay, R.Slot, R.Engine);
		const int32 Id = H.Slots->FindRef(R.Slot);
		const int32 FromBay = H.Bay;
		H.Slots->Add(R.Slot, 0);
		H.Bolted->Add(R.Slot, false);
		Move(Id, ETorqueAt::Held, Peer);
		const FName TyreSlot = TorqueShedParts::TyreOf(R.Slot);
		if (R.Engine == 0 && !TyreSlot.IsNone())
		{
			// The tyre stays on its wheel: take them both to the tyre changer.
			const int32 Tyre = H.Slots->FindRef(TyreSlot);
			if (Tyre != 0)
			{
				H.Slots->Add(TyreSlot, 0);
				H.Bolted->Add(TyreSlot, false);
				Items[Id].Tyre = Tyre;
				Move(Tyre, ETorqueAt::Wheel, Id);
			}
		}
		Text = FString::Printf(TEXT("Removed the %s"), *ItemTitle(Id));
		if (FromBay != INDEX_NONE) PartsOf.Add(FromBay);
		bItems = true;
		if (R.Engine == 0 && R.Slot == TBI::FrameSlot() && Bikes.IsValidIndex(R.Bay))
		{
			// Nothing left of it: the bay is free for the next project.
			Text += FString::Printf(TEXT(". Bay %d is free."), R.Bay + 1);
			Bikes[R.Bay] = FTorqueShedBike();
		}
		break;
	}
	case ETorqueShedAct::Install:
	{
		FString Error;
		const FName S = InstallSlot(R, Items[Held], Error);
		// A part that needs fabrication goes on, and the bike owes the jobs (see FabOwed).
		FString Fab;
		if (R.Engine == 0 && TBI::IsFitSlot(S))
		{
			FString Tier, Reason;
			TArray<FString> Work;
			if (FitTier(R.Bay, S, Items[Held].Part, Tier, Reason, Work) && Tier == TEXT("fabricate") && Work.Num() > 0)
			{
				for (const FString& W : Work) Bikes[R.Bay].FabOwed.AddUnique(W);
				Fab = FString::Printf(TEXT(" It needs fabrication before it'll ride: %s. Take the grinder and welder to it."), *FString::Join(Work, TEXT(", ")));
			}
		}
		const FHost H = HostOf(R.Bay, S, R.Engine);
		H.Slots->Add(S, Held);
		H.Bolted->Add(S, false);
		if (TorqueShedParts::IsEngineSlot(S)) Move(Held, ETorqueAt::Engine, H.Engine);
		else Move(Held, ETorqueAt::Bike, H.Bay);
		const FName TyreSlot = TorqueShedParts::TyreOf(S);
		const int32 Tyre = Items[Held].Tyre;
		if (Tyre != 0 && !TyreSlot.IsNone() && H.Slots->FindRef(TyreSlot) == 0)
		{
			// A wheel goes back on with its tyre seated on it.
			H.Slots->Add(TyreSlot, Tyre);
			H.Bolted->Add(TyreSlot, true);
			Move(Tyre, ETorqueAt::Bike, H.Bay);
			Items[Held].Tyre = 0;
		}
		Text = FString::Printf(TEXT("Fitted the %s as the %s. Bolt it up with the wrench."), *ItemTitle(Held), *TorqueShedParts::SlotName(S).ToLower()) + Fab;
		if (H.Bay != INDEX_NONE) PartsOf.Add(H.Bay);
		bItems = true;
		break;
	}
	case ETorqueShedAct::Take:
		Move(R.Item != 0 ? R.Item : ItemAtSpot(R.Spot), ETorqueAt::Held, Peer);
		bItems = true;
		break;
	case ETorqueShedAct::Drop:
		Move(Held, ETorqueAt::Floor, 0);
		Items[Held].Place = R.Place;
		bItems = true;
		break;
	case ETorqueShedAct::Place:
		Move(Held, ETorqueAt::Spot, 0, R.Spot);
		if (TBI::SpotIs(R.Spot, ETorquePlace::Stand)) Text = TEXT("Engine on the stand. Strip it down or build it up.");
		else if (TBI::SpotIs(R.Spot, ETorquePlace::Washer)) Text = TEXT("Into the parts washer");
		else if (TBI::SpotIs(R.Spot, ETorquePlace::Hook)) Text = TEXT("Hung in the spray bay. Spray it with the spray gun (7): primer, colour, then lacquer.");
		else if (TBI::SpotIs(R.Spot, ETorquePlace::Changer))
			Text = Items[Held].Tyre != 0 ? TEXT("Wheel clamped on the tyre changer. Lever the old tyre off, or lift the wheel off again.")
				: TEXT("Wheel clamped on the tyre changer. Hold a tyre that fits it and work it on.");
		bItems = true;
		break;
	case ETorqueShedAct::Bin:
	{
		const FName Which = R.Bin.IsNone() ? FName(TEXT("general")) : R.Bin;
		Text = Which == FName(TEXT("general")) ? FString::Printf(TEXT("Binned the %s"), *ItemTitle(Held))
			: FString::Printf(TEXT("Recycled the %s in the %s bin"), *ItemTitle(Held), *Which.ToString());
		EraseItem(Held);
		bItems = true;
		break;
	}
	case ETorqueShedAct::Clean:
	{
		int32 N = 0;
		for (int32 Id : R.Items)
		{
			FTorqueItem* It = Items.Find(Id);
			if (!It || It->At == ETorqueAt::Held || It->Dirt < 0.02f || It->bBoxed) continue;
			It->Dirt = 0.0f;
			++N;
			Touch(Id);
		}
		Text = FString::Printf(TEXT("Scrubbed %d part%s clean"), N, N == 1 ? TEXT("") : TEXT("s"));
		break;
	}
	case ETorqueShedAct::Repair:
	{
		int32 N = 0, Last = 0;
		for (int32 Id : R.Items)
		{
			FTorqueItem* It = Items.Find(Id);
			if (!It || (It->Rust <= 0.0f && It->Wear <= 0.0f)) continue;
			It->Rust = 0.0f;
			It->Wear = 0.0f;
			++N;
			Last = Id;
			Touch(Id);
		}
		Text = N == 1 ? FString::Printf(TEXT("Restored the %s"), *ItemTitle(Last)) : FString::Printf(TEXT("Restored %d parts"), N);
		break;
	}
	case ETorqueShedAct::Paint:
	{
		// shed_state _apply "paint": fresh paint covers any coat that was on it; in the booth no
		// dust settles in it, and over a good primer it keys on and comes out better.
		FTorqueItem& It = Items[R.Item];
		const bool bPrimed = It.bPrimerTop;
		float CoatsQ = 1.0f;
		FString What;
		if (R.Finish == CupFinish())
		{
			// Mixed paint from the gun's cup: its colour and effect, coats building up (TorquePaintBay.cpp).
			CoatsQ = SprayFromCup(Peer, It);
			const FTorquePaintCup* Cup = Paint.Cups.Find(Peer);
			const TorquePaintMix::FPaintSpec Mixed = Cup ? Cup->Spec : TorquePaintMix::FPaintSpec();
			if (Mixed.IsLacquer()) What = FString::Printf(TEXT("with %s clear lacquer"), *TorquePaintMix::SheenName(Mixed.Flat));
			else What = FString::Printf(TEXT("%s (%s, coat %d)"), *Mixed.Name, *TorquePaintMix::EffectName(Mixed.Effect).ToLower(), It.MixCoats);
		}
		else
		{
			const int32 Idx = FMath::Clamp(R.Colour, 0, Paints().Num() - 1);
			// Over masking tape the colour under it stays where the stripe goes (shed_state "paint").
			if (It.Tape != 0 && !It.bPrimerTop) MaskUnderTape(It);
			else It.Mask = 0;
			It.Colour = Paints()[Idx];
			It.Finish = Finishes().Contains(R.Finish) ? R.Finish : FName(TEXT("gloss"));
			It.MixCode.Reset();
			It.MixName.Reset();
			It.MixEffect = NAME_None;
			It.MixCoats = 0;
			It.Flat = 0.0f;
			What = FinishName(It.Finish).ToLower() + TEXT(" ") + PaintName(Idx).ToLower();
		}
		It.Coat = NAME_None;
		It.CoatQ = 0.0f;
		float Q = TBI::SprayQ(It, R);
		if (bPrimed) Q = FMath::Min(1.0f, Q + PrimerBonus * It.Primer);
		It.bPrimerTop = false;
		Q *= CoatsQ;
		It.PaintQ = Q;
		Text = FString::Printf(TEXT("Sprayed the %s %s"), *ItemTitle(R.Item), *What);
		if (bPrimed) Text += TEXT(" over primer");
		Text += TEXT(": ") + TBI::JobWord(Q, R.Tilt);
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::Prime:
	{
		// shed_state _apply "prime": grey primer, the colour that was on it kept underneath for now.
		FTorqueItem& It = Items[R.Item];
		const float Q = TBI::SprayQ(It, R);
		It.Coat = NAME_None;
		It.CoatQ = 0.0f;
		It.Primer = Q;
		It.bPrimerTop = true;
		It.Mask = 0;
		Text = FString::Printf(TEXT("Primed the %s: %s"), *ItemTitle(R.Item), *TBI::JobWord(Q, R.Tilt));
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::Coat:
	{
		// shed_state _coat_apply: clear lacquer over a paint job brings it up (once); an enamel paints it.
		FTorqueItem& It = Items[R.Item];
		const float Q = TBI::SprayQ(It, R);
		if (IsLacquer(R.Finish))
		{
			if (It.PaintQ >= 0.0f && !IsLacquer(It.Coat)) It.PaintQ = FMath::Min(1.0f, It.PaintQ + LacquerBonus * Q);
		}
		else
		{
			const bool bSilver = R.Finish == FName(TEXT("enamel_silver"));
			It.Colour = Paints()[FMath::Clamp(bSilver ? 8 : 6, 0, Paints().Num() - 1)];
			It.Finish = bSilver ? FName(TEXT("metallic")) : FName(TEXT("gloss"));
			const float Under = It.bPrimerTop ? It.Primer : 0.0f;
			It.PaintQ = FMath::Min(1.0f, Q + PrimerBonus * Under);
			It.bPrimerTop = false;
		}
		It.Coat = R.Finish;
		It.CoatQ = Q;
		Text = FString::Printf(TEXT("Gave the %s a coat of %s: %s"), *ItemTitle(R.Item).ToLower(), *CoatName(R.Finish).ToLower(), *TBI::JobWord(Q, R.Tilt));
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::Wash:
		// No message: it's a pass of the jet, many a second while the trigger is held.
		// With Amounts (the wash game, TorqueWashJob): each part's dirt times its share left, as the jet
		// actually took it off; without, a timed pass off everything near.
		for (int32 I = 0; I < R.Items.Num(); ++I)
		{
			FTorqueItem* It = Items.Find(R.Items[I]);
			if (!It || It->bBoxed || It->Dirt <= 0.0f) continue;
			if (R.Amounts.IsValidIndex(I))
			{
				const float K = FMath::Clamp(R.Amounts[I], 0.0f, 1.0f);
				It->Dirt = K <= 0.0f || It->Dirt * K < 0.005f ? 0.0f : It->Dirt * K;
			}
			else It->Dirt = FMath::Max(0.0f, It->Dirt - JetClean);
			Touch(R.Items[I]);
		}
		break;
	case ETorqueShedAct::OrderPart:
	{
		const int32 Cell = FreeDelivery();
		const int32 Id = NewItem(R.Part, 0.0f, 0.0f, 0.0f);
		Items[Id].bBoxed = true;
		Items[Id].At = ETorqueAt::Floor;
		Items[Id].Place = FTransform(FRotator(0.0, Rng.FRandRange(-8.0f, 8.0f), 0.0), TorqueShedLayout::DeliveryAt(Cell));
		OrdererOf(Peer, Items[Id].OrderedBy, Items[Id].OrderedTint);
		Text = FString::Printf(TEXT("Ordered %s. The box is in the delivery area outside the roller door."), *TorqueShedParts::PartName(R.Part));
		bItems = true;
		break;
	}
	case ETorqueShedAct::OrderBike:
	{
		const int32 Bay = FreeBay();
		if (!R.Text.IsEmpty())
		{
			// A new bike from the configurator, built and ready to ride.
			const FString Why = NewOrderedBike(Peer, Bay, R.Text);
			if (!Why.IsEmpty())
			{
				Text = Why;
				break;
			}
			Text = FString::Printf(TEXT("%s is in bay %d, built new and ready to ride."), *Bikes[Bay].Name, Bay + 1);
			PartsOf.Add(Bay);
			LooksOf.Add(Bay);
			break;
		}
		if (!NewProjectBike(Bay, Rng.FRandRange(0.35f, 0.9f)))
		{
			Text = TEXT("No project bike could be found (the part list's bike layout is missing).");
			break;
		}
		Text = FString::Printf(TEXT("%s has been delivered to bay %d"), *Bikes[Bay].Name, Bay + 1);
		PartsOf.Add(Bay);
		break;
	}
	case ETorqueShedAct::Unpack:
		Items[R.Item].bBoxed = false;
		Text = FString::Printf(TEXT("Unpacked the %s"), *ItemTitle(R.Item));
		bItems = true;
		break;
	case ETorqueShedAct::StartBuild:
	{
		int32 Number = 1;
		for (const FTorqueShedBike& Other : Bikes)
			if (Other.bExists && Other.Name.StartsWith(TEXT("Scratch build"))) ++Number;
		FTorqueShedBike& B = Bikes[R.Bay];
		B = FTorqueShedBike();
		B.bExists = true;
		B.Id = NextId++;
		B.Name = FString::Printf(TEXT("Scratch build %d"), Number);
		B.Model = FName(TorqueShedParts::StockModel);
		for (const FName& S : TorqueShedParts::BikeSlots())
		{
			B.Slots.Add(S, 0);
			B.Bolted.Add(S, false);
		}
		B.Slots.Add(TBI::FrameSlot(), Held);
		B.Bolted.Add(TBI::FrameSlot(), true);
		Move(Held, ETorqueAt::Bike, R.Bay);
		Text = FString::Printf(TEXT("%s is on the paddock stand in bay %d. Fit the forks, shock and the rest to it, then bolt it all up."), *B.Name, R.Bay + 1);
		PartsOf.Add(R.Bay);
		bItems = true;
		break;
	}
	case ETorqueShedAct::TyreFit:
	{
		const int32 Wheel = ItemAtSpot(R.Spot);
		Items[Wheel].Tyre = Held;
		Move(Held, ETorqueAt::Wheel, Wheel);
		Text = TEXT("Tyre on. Lift the wheel off and fit it to a bike.");
		bItems = true;
		break;
	}
	case ETorqueShedAct::TyreRemove:
	{
		const int32 Wheel = ItemAtSpot(R.Spot);
		const int32 Tyre = Items[Wheel].Tyre;
		Items[Wheel].Tyre = 0;
		Move(Tyre, ETorqueAt::Held, Peer);
		Text = TEXT("Broke the bead and levered the tyre off");
		bItems = true;
		break;
	}
	case ETorqueShedAct::RodeOut:
		Bikes[R.Bay].bRode = true;
		break;
	case ETorqueShedAct::Fabricate:
	{
		const FString What = FabOwed(R.Bay)[0];
		FTorqueShedBike& B = Bikes[R.Bay];
		B.FabOwed.Remove(What);
		B.FabDone.AddUnique(What);
		const FString Left = FabOwedText(R.Bay);
		Text = FString::Printf(TEXT("Made the %s. %s"), *What,
			Left.IsEmpty() ? (IsComplete(R.Bay) ? TEXT("That's the fabrication done: the bike is ready to ride!") : TEXT("That's the fabrication done.")) : *FString::Printf(TEXT("Still to make: %s."), *Left));
		break;
	}
	case ETorqueShedAct::GrabBike:
	{
		FTorqueShedBike& B = Bikes[R.Bay];
		if (!B.bAway) B.Xf = HomeOf(R.Bay);
		B.bAway = true;
		B.Pusher = Peer;
		Text = FString::Printf(TEXT("Got hold of %s. F to let go."), *B.Name);
		break;
	}
	case ETorqueShedAct::MoveBike:
		Bikes[R.Bay].Xf = FTransform(FRotator(0.0, R.Place.Rotator().Yaw, 0.0), FVector(R.Place.GetLocation().X, R.Place.GetLocation().Y, 0.0));
		break;
	case ETorqueShedAct::ReleaseBike:
	{
		// shed_state _park: back into its bay, square on the dyno's drum, or on its stand where it is.
		FTorqueShedBike& B = Bikes[R.Bay];
		B.Pusher = 0;
		const FVector At(R.Place.GetLocation().X, R.Place.GetLocation().Y, 0.0);
		B.Xf = FTransform(FRotator(0.0, R.Place.Rotator().Yaw, 0.0), At);
		const int32 OnRig = DynoBay();
		// Wheeled onto a lift (its own bay's, another empty bay's, the wash bay's): square on it,
		// its front tyre in the lift's wheel vice (ATorqueBikeLift slides the vice to it).
		int32 Onto = INDEX_NONE;
		for (int32 L = 0; L <= WashLift && Onto == INDEX_NONE; ++L)
			if (IsLift(L) && TorqueShedLayout::OntoLift(L, At) && (L == R.Bay || BikeOnLift(L, R.Bay) == INDEX_NONE)) Onto = L;
		if (FVector::Dist2D(At, HomeOf(R.Bay).GetLocation()) < 90.0 || Onto == R.Bay)
		{
			B.bAway = false;
			B.Xf = HomeOf(R.Bay);
			Text = FString::Printf(TEXT("%s is on the paddock stand in bay %d"), *B.Name, R.Bay + 1);
		}
		else if (FVector::Dist2D(At, RigTransform().GetLocation()) < 100.0 && (OnRig == INDEX_NONE || OnRig == R.Bay))
		{
			B.Xf = RigTransform();
			for (bool& Strap : Dyno.Straps) Strap = false;
			Text = FString::Printf(TEXT("%s is on the dyno. Strap it down at the four posts (hold the work button), then run it at the desk."), *B.Name);
		}
		else if (Onto != INDEX_NONE)
		{
			// wash.gd LIFT_SNAP: square on the lift, locked in its wheel vice.
			B.Xf = TorqueShedLayout::LiftXf(Onto);
			Text = FString::Printf(TEXT("%s is locked onto %s. F on the button post raises it."), *B.Name,
				Onto == WashLift ? TEXT("the wash bay lift") : *FString::Printf(TEXT("bay %d's lift"), Onto + 1));
		}
		else Text = FString::Printf(TEXT("%s is on its stand."), *B.Name);
		break;
	}
	case ETorqueShedAct::ScrapBike:
	{
		// shed_state.gd remove_bike: every part on it goes (an engine with what's bolted to it, a
		// wheel with its tyre), then the bike; the bay is free for the next project.
		const FString Name = Bikes[R.Bay].Name;
		int32 Gone = 0;
		for (const FName& S : TorqueShedParts::BikeSlots())
			if (const int32 Id = Bikes[R.Bay].Slots.FindRef(S))
			{
				EraseItem(Id);
				++Gone;
			}
		if (OnDyno(R.Bay))
			for (bool& Strap : Dyno.Straps) Strap = false;
		Bikes[R.Bay] = FTorqueShedBike();
		PartsOf.Add(R.Bay);
		bItems = true;
		Text = FString::Printf(TEXT("Scrapped %s and the %d parts on it. Bay %d is free."), *Name, Gone, R.Bay + 1);
		break;
	}
	case ETorqueShedAct::Lift:
		if (R.Colour != 0) LiftsUp |= 1 << R.Item;
		else LiftsUp &= ~(1 << R.Item);
		Text = FString::Printf(TEXT("%s going %s"), *LiftName(R.Item), R.Colour != 0 ? TEXT("up") : TEXT("down"));
		break;
	case ETorqueShedAct::DynoStrap:
	{
		bool& Strap = Dyno.Straps[R.Item];
		Strap = !Strap;
		if (Dyno.StrapsDone() == 4) Text = FString::Printf(TEXT("Strapped down: %s is ready for a run."), *Bikes[DynoBay()].Name);
		else if (Strap) Text = FString::Printf(TEXT("Strap done up (%d of 4)"), Dyno.StrapsDone());
		else Text = FString::Printf(TEXT("Strap undone (%d of 4 done up)"), Dyno.StrapsDone());
		break;
	}
	case ETorqueShedAct::DynoFan:
		Dyno.bFan = !Dyno.bFan;
		Text = Dyno.bFan ? TEXT("Cooling fan on") : TEXT("Cooling fan off");
		break;
	case ETorqueShedAct::DynoLights:
		Dyno.bLights = !Dyno.bLights;
		Text = Dyno.bLights ? TEXT("Dyno room lights on") : TEXT("Dyno room lights off");
		break;
	case ETorqueShedAct::PaintTapeTake:
		Paint.TapeBy = Peer;
		Text = FString::Printf(TEXT("Masking tape: %s. R picks the pattern; hold the work button on a painted part to tape it."),
			*FString(TorquePaintMix::PatternName(Paint.TapePattern)).ToLower());
		bItems = true;
		break;
	case ETorqueShedAct::PaintTapePut:
		Paint.TapeBy = 0;
		bItems = true;
		break;
	case ETorqueShedAct::PaintTapePattern:
		Paint.TapePattern = R.Colour;
		Text = FString::Printf(TEXT("Masking tape: %s"), *FString(TorquePaintMix::PatternName(R.Colour)).ToLower());
		break;
	case ETorqueShedAct::PaintTape:
	{
		FTorqueItem& It = Items[R.Item];
		It.Tape = Paint.TapePattern;
		Text = FString::Printf(TEXT("Taped a %s on the %s. Now spray the second colour over it."),
			*FString(TorquePaintMix::PatternName(It.Tape)).ToLower(), *ItemTitle(R.Item));
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::PaintPeel:
	{
		FTorqueItem& It = Items[R.Item];
		It.Tape = 0;
		Text = It.Mask != 0 ? FString(TEXT("Peeled the tape off: a crisp line.")) : FString(TEXT("Peeled the tape off: nothing sprayed over it."));
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::Radio:
		UE_LOG(LogTemp, Log, TEXT("Torque radio: request from player %d: %s, station %d"), Peer, R.Item != 0 ? TEXT("on") : TEXT("off"), R.Colour);
		bRadioOn = R.Item != 0;
		RadioTuned = R.Colour;
		Text = bRadioOn ? FString::Printf(TEXT("Radio: %s"), *TorqueShedRadio::Stations()[RadioTuned].Name) : FString(TEXT("Radio off"));
		break;
	case ETorqueShedAct::DynoRun:
	{
		FTorqueShedBike& B = Bikes[R.Bay];
		FTorqueDynoRun Run = TorqueShedPower::DynoRun(NumbersOf(R.Bay));
		// Heat: hotter every run, far hotter without the fan, and a hot engine fades over the sweep.
		Dyno.Heat = FMath::Clamp(Dyno.Heat + (Dyno.bFan ? TorqueShedPower::FanHeat : TorqueShedPower::NoFanHeat), 0.0f, 1.0f);
		TorqueShedPower::HeatRun(Run, Dyno.Heat);
		Run.bFan = Dyno.bFan;
		if (!Dyno.bFan && Dyno.Heat > 0.6f)
			if (FTorqueItem* Block = Items.Find(ItemOn(R.Bay, FName(TEXT("engine"))))) Block->Wear = FMath::Min(1.0f, Block->Wear + 0.004f);
		B.DynoN += 1;
		Run.N = B.DynoN;
		Run.Start = SharedNow();
		B.DynoRuns.Add(Run);
		while (B.DynoRuns.Num() > 2) B.DynoRuns.RemoveAt(0);
		DynoBusyUntil = Run.Start + TorqueShedPower::RunTime(Run);
		Text = FString::Printf(TEXT("Dyno run %d on %s..."), B.DynoN, *B.Name);
		if (!Dyno.bFan && Dyno.Heat > 0.5f) Text += TEXT(" It's running hot: switch the fan on.");
		break;
	}
	case ETorqueShedAct::GeoSave:
		Text = TorqueGeometryMode::ApplySave(Bikes[R.Bay], R.Text);
		PartsOf.Add(R.Bay);
		break;
	case ETorqueShedAct::Tune:
	{
		// shed_state _apply "tune": kept where TUNE_HOME says, within what the parts allow; the
		// chain's slack on the chain. Says nothing (the desk shows it).
		const std::string Key = TBI::Std(R.Text);
		std::string Home, Field;
		TorqueFit::TuneHome(Key, Home, Field);
		TMap<FName, float>* Holder = nullptr;
		if (Home == "bike") Holder = &Bikes[R.Bay].Tune;
		else if (FTorqueItem* It = Items.Find(ItemOn(R.Bay, FName(*TBI::Ue(Home))))) Holder = &It->Tune;
		if (!Holder) break;
		const FName FieldName(*TBI::Ue(Field));
		double Lo = 0.0, Hi = 0.0;
		if (Key == "chain") TorqueFit::TuneLimits(Key, Lo, Hi);
		else WithTune(R.Bay, [&Lo, &Hi, &Key](const TorqueFit::BikeTune& T) { T.Range(Key, Lo, Hi); });
		if (R.Item != 0) Holder->Remove(FieldName);
		else Holder->Add(FieldName, (float)FMath::Clamp((double)R.Amount, Lo, Hi));
		break;
	}
	case ETorqueShedAct::Lighten:
	{
		FTorqueItem& It = Items[R.Item];
		const TorqueFit::BikeLayout* Lay = TBI::FitLayout();
		const std::string Id = TBI::Id(It.Part);
		const TorqueFit::BikeTune T(*Lay, TorqueFit::PartsMap(), TorqueFit::TuneMap());
		const TorqueFit::Json* P = Lay->Part(Id);
		const double Kg = (P ? P->Num("mass", 0.0) : 0.0) * T.LightenCut(Id);
		It.bLightened = true;
		It.Wear = FMath::Min(1.0f, It.Wear + (float)TorqueFit::LightenWear);
		Text = FString::Printf(TEXT("Drilled and trimmed the %s: %.1f kg lighter, but a little weaker"), *ItemTitle(R.Item), Kg);
		Touch(R.Item);
		break;
	}
	default:
		break;
	}
	for (int32 Bay : PartsOf)
	{
		RebuildBuild(Bay);
		OnChanged.Broadcast(Bay);
	}
	for (int32 Bay : LooksOf)
		if (!PartsOf.Contains(Bay)) OnLooks.Broadcast(Bay);
	if (bItems) OnItems.Broadcast();
	MarkSave();
	OnHostChange.Broadcast();
	return Text;
}

void UTorqueBuildSubsystem::DropHeldBy(int32 Peer, const FVector& At)
{
	if (IsClient()) return;
	DropKitOf(Peer);   // [kit]
	if (Paint.Held.Contains(Peer) || Paint.BenchBy == Peer || Paint.Cups.Contains(Peer))
	{
		// The mixing bay: their tin back on the shelf, away from the bench, the cup put away.
		Paint.PutBack(Peer);
		Paint.Cups.Remove(Peer);
		OnItems.Broadcast();
		MarkSave();
		OnHostChange.Broadcast();
	}
	const int32 Id = HeldBy(Peer);
	if (Id == 0) return;
	Move(Id, ETorqueAt::Floor, 0);
	const FVector Floor(At.X, At.Y, 0.0);
	Items[Id].Place = FTransform(TorqueShedLayout::OnSite(Floor) ? Floor : TorqueShedLayout::DropAt());
	OnItems.Broadcast();
	MarkSave();
	OnHostChange.Broadcast();
}

void UTorqueBuildSubsystem::RodeOut(int32 Bay)
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::RodeOut;
	R.Bay = Bay;
	FString Why;
	Request(R, Why);
}

void UTorqueBuildSubsystem::Arrived(int32 Bay)
{
	// The host's: a client's copy comes back from it.
	if (IsClient() || !Exists(Bay) || !Bikes[Bay].bRode) return;
	Bikes[Bay].bRode = false;
	TArray<int32> On;
	for (const TPair<int32, FTorqueItem>& KV : Items)
		if (BayOf(KV.Key) == Bay) On.Add(KV.Key);
	for (int32 Id : On) Items[Id].Dirt = FMath::Min(Items[Id].Dirt + RideDirt, 1.0f);
	UE_LOG(LogTemp, Log, TEXT("Torque: bay %d back from a ride, %d parts dirtier"), Bay + 1, On.Num());
	OnLooks.Broadcast(Bay);
	MarkSave();
	OnHostChange.Broadcast();
}

// --- Co-op and whose bike ----------------------------------------------------------------------------

void UTorqueBuildSubsystem::ReportActivity(const FString& What)
{
	if (What == LastActivity) return;
	LastActivity = What;
	OnActivity.Broadcast(What);
}

void UTorqueBuildSubsystem::SetMyBike(int32 Bay)
{
	if (Bay < 0 || Bay >= Bays || Bay == Mine) return;
	Mine = Bay;
	if (!bHostCopy) MarkSave();
	OnMyBike.Broadcast(Bay);
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
	return S ? S->Get(BikeFor(WorldContext)) : TBI::NoBuild();
}

bool UTorqueBuildSubsystem::RidesWith(const UObject* WorldContext, const FTorqueResolvedPart& Part)
{
	const UTorqueBuildSubsystem* S = Of(WorldContext);
	const int32 Bay = BikeFor(WorldContext);
	if (!S || !S->Exists(Bay)) return true;
	return S->ShowsModelPart(Bay, Part);
}
