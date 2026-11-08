#include "Shed/TorqueBuildSubsystem.h"

#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedSave.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
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

bool IsSpot(FName Spot)
{
	return SpotIs(Spot, ETorquePlace::Bench) || SpotIs(Spot, ETorquePlace::Repair) || SpotIs(Spot, ETorquePlace::Washer) ||
		SpotIs(Spot, ETorquePlace::Changer) || SpotIs(Spot, ETorquePlace::Stand);
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

bool UTorqueBuildSubsystem::TickSave(float Dt)
{
	if (!bSaveDue) return true;
	SaveIn -= Dt;
	if (SaveIn <= 0.0) Save();   // washing changes dirt many times a second: one save after it settles
	return true;
}

void UTorqueBuildSubsystem::StartFresh()
{
	Bikes.Reset();
	Bikes.SetNum(Bays);
	Items.Reset();
	NextId = 1;
	Mine = 0;
	bHostCopy = false;
	// The project bikes the shed starts with (shed_state's demo bikes: a couple to get stuck into).
	for (int32 I = 0; I < StartBikes && I < Bays; ++I) NewStockBike(I, Rng.FRandRange(0.35f, 0.6f));
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
		S.Bikes.Add(To);
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
	}
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
constexpr int32 BytesLayout = 1;   // bumped when the layout below changes

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
		if (It.At == ETorqueAt::Floor)
		{
			FVector3f Where(It.Place.GetLocation());
			float Yaw = (float)It.Place.Rotator().Yaw;
			Ar << Where << Yaw;
		}
		uint8 bEngine = It.Kind == TBI::EngineKind() ? 1 : 0;
		Ar << bEngine;
		if (bEngine) TBI::WriteSlots(Ar, It.Slots, It.Bolted);
	}
	int32 NextIdOut = NextId;
	Ar << NextIdOut;

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
		if (NewBikes.IsValidIndex(I)) NewBikes[I] = B;
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
		It.Colour = FLinearColor::FromSRGBColor(Colour);
		It.bBoxed = bBoxed != 0;
		It.At = (ETorqueAt)FMath::Min<uint8>(At, (uint8)ETorqueAt::Wheel);
		if (It.At == ETorqueAt::Floor)
		{
			FVector3f Where;
			float Yaw = 0.0f;
			Ar << Where << Yaw;
			It.Place = FTransform(FRotator(0.0, Yaw, 0.0), FVector(Where));
		}
		Ar << bEngine;
		if (bEngine && !TBI::ReadSlots(Ar, It.Slots, It.Bolted, false)) return false;
		NewItems.Add(It.Id, It);
	}
	int32 HostNextId = 1;
	Ar << HostNextId;
	if (Ar.IsError())
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
	for (const FName& S : TorqueShedParts::Slots())
	{
		const FTorqueItem* It = Items.Find(ItemOn(Bay, S));
		if (!It) continue;
		Sig += FString::Printf(TEXT("|%d:%s"), It->Id, *It->Part.ToString());
		if (bLooks)
			Sig += FString::Printf(TEXT(":%d:%d:%s:%s:%d"), TBI::Quant(It->Dirt), TBI::Quant(It->Rust), *It->Colour.QuantizeRound().ToHex(),
				*It->Finish.ToString(), IsBolted(Bay, S) ? 1 : 0);
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
	return Exists(Bay) && MissingSlots(Bay).Num() == 0 && LooseSlots(Bay).Num() == 0;
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
	FString Paint = TEXT("factory finish");
	if (!It->Finish.IsNone())
	{
		const FLinearColor Colour = It->Colour;
		const int32 Idx = Paints().IndexOfByPredicate([Colour](const FLinearColor& C) { return C.Equals(Colour, 0.01f); });
		Paint = FString::Printf(TEXT("%s %s"), *FinishName(It->Finish), *PaintName(Idx).ToLower());
	}
	return FString::Printf(TEXT("Condition %d%%   Rust %d%%   Dirt %d%%   Wear %d%%   %s"), FMath::RoundToInt(Health(*It) * 100.0f),
		FMath::RoundToInt(It->Rust * 100.0f), FMath::RoundToInt(It->Dirt * 100.0f), FMath::RoundToInt(It->Wear * 100.0f), *Paint);
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

bool UTorqueBuildSubsystem::PaintOf(int32 Bay, FName Slot, FLinearColor& Out) const
{
	const FTorqueItem* It = Items.Find(ItemOn(Bay, TBI::AsSlot(Slot)));
	if (!It) return false;
	if (!It->Finish.IsNone())
	{
		Out = It->Colour;
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
	const bool bSlotWork = A == ETorqueShedAct::Unbolt || A == ETorqueShedAct::Bolt || A == ETorqueShedAct::Remove;
	if (bSlotWork && R.Engine == 0 && TorqueShedParts::IsLowSlot(R.Slot) && !R.bCrouched)
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
		if ((TBI::SpotIs(R.Spot, ETorquePlace::Washer) || TBI::SpotIs(R.Spot, ETorquePlace::Repair) || TBI::SpotIs(R.Spot, ETorquePlace::Changer)) && HeldIt->bBoxed)
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
	{
		const FTorqueItem* It = Items.Find(R.Item);
		if (!It) return TEXT("That part has gone.");
		if (It->At == ETorqueAt::Held) return TEXT("Put it down first.");
		if (It->bBoxed) return TEXT("Unpack it first.");
		if (It->Rust > MaxPaintRust) return TEXT("Too rusty to paint. Grind it back on the repair bench first.");
		const int32 Idx = FMath::Clamp(R.Colour, 0, Paints().Num() - 1);
		const FName Finish = Finishes().Contains(R.Finish) ? R.Finish : FName(TEXT("gloss"));
		if (It->Finish == Finish && It->Colour.Equals(Paints()[Idx], 0.001f))
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
	default:
		return TEXT("The shed doesn't know how to do that.");
	}
}

// --- Doing it (shed_state _apply) -------------------------------------------------------------------

bool UTorqueBuildSubsystem::Request(const FTorqueShedRequest& R, FString& WhyNot)
{
	WhyNot = Check(Me(), R);
	if (!WhyNot.IsEmpty()) return false;
	if (IsClient())
	{
		if (Forward.Execute(R)) return true;
		WhyNot = TEXT("The host didn't get that: try again.");
		return false;
	}
	return Apply(Me(), R, WhyNot);
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
		Text = FString::Printf(TEXT("Fitted the %s as the %s. Bolt it up with the wrench."), *ItemTitle(Held), *TorqueShedParts::SlotName(S).ToLower());
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
		FTorqueItem& It = Items[R.Item];
		const int32 Idx = FMath::Clamp(R.Colour, 0, Paints().Num() - 1);
		It.Colour = Paints()[Idx];
		It.Finish = Finishes().Contains(R.Finish) ? R.Finish : FName(TEXT("gloss"));
		Text = FString::Printf(TEXT("Sprayed the %s %s %s"), *ItemTitle(R.Item), *FinishName(It.Finish).ToLower(), *PaintName(Idx).ToLower());
		Touch(R.Item);
		break;
	}
	case ETorqueShedAct::Wash:
		// No message: it's a pass of the jet, many a second while the trigger is held.
		for (int32 Id : R.Items)
		{
			FTorqueItem* It = Items.Find(Id);
			if (!It || It->bBoxed || It->Dirt <= 0.0f) continue;
			It->Dirt = FMath::Max(0.0f, It->Dirt - JetClean);
			Touch(Id);
		}
		break;
	case ETorqueShedAct::OrderPart:
	{
		const int32 Cell = FreeDelivery();
		const int32 Id = NewItem(R.Part, 0.0f, 0.0f, 0.0f);
		Items[Id].bBoxed = true;
		Items[Id].At = ETorqueAt::Floor;
		Items[Id].Place = FTransform(FRotator(0.0, Rng.FRandRange(-8.0f, 8.0f), 0.0), TorqueShedLayout::DeliveryAt(Cell));
		Text = FString::Printf(TEXT("Ordered %s. The box is in the delivery area outside the roller door."), *TorqueShedParts::PartName(R.Part));
		bItems = true;
		break;
	}
	case ETorqueShedAct::OrderBike:
	{
		const int32 Bay = FreeBay();
		NewStockBike(Bay, Rng.FRandRange(0.35f, 0.9f));
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
