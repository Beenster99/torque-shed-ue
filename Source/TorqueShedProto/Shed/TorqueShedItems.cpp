#include "Shed/TorqueShedItems.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueWalker.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueShedItemsImpl
{
// The size of a part's box (cm: long, wide, high) by its kind, for a boxed part and for a part the
// model has no shape for.
FVector KindSize(FName Kind)
{
	struct FSize { const TCHAR* Kind; double X, Y, Z; };
	static const FSize Sizes[] = {
		{TEXT("frame"), 140, 40, 60}, {TEXT("engine"), 60, 50, 50}, {TEXT("wheel"), 70, 20, 70}, {TEXT("tyre"), 70, 20, 70},
		{TEXT("fork"), 80, 25, 25}, {TEXT("swingarm"), 70, 30, 20}, {TEXT("exhaust"), 90, 25, 25}, {TEXT("tank"), 60, 35, 30},
		{TEXT("fairing"), 60, 60, 50}, {TEXT("panniers"), 50, 30, 40}, {TEXT("top_box"), 45, 40, 35}, {TEXT("tail"), 60, 30, 25},
		{TEXT("saddle"), 60, 30, 15}, {TEXT("handlebars"), 80, 15, 15}, {TEXT("shock"), 40, 15, 15}, {TEXT("gearbox"), 35, 30, 25},
		{TEXT("head"), 35, 30, 20}, {TEXT("cooling"), 40, 10, 30}, {TEXT("front_mudguard"), 50, 20, 15}, {TEXT("side_panels"), 35, 10, 30},
	};
	const FString K = Kind.ToString();
	for (const FSize& S : Sizes)
		if (K == S.Kind) return FVector(S.X, S.Y, S.Z);
	return FVector(30, 25, 20);
}

const FLinearColor Cardboard(0.52f, 0.37f, 0.22f);
const TCHAR* const ItemPrefix = TEXT("Item:");

void SetLooks(UStaticMeshComponent* C, float Dirt, float Rust, const FLinearColor* Colour)
{
	UMaterialInstanceDynamic* M = C ? C->CreateDynamicMaterialInstance(0) : nullptr;
	if (!M) return;
	M->SetScalarParameterValue(TEXT("Grime"), Dirt);
	M->SetScalarParameterValue(TEXT("Rust"), Rust);
	if (Colour)
	{
		M->SetVectorParameterValue(TEXT("Color"), *Colour);
		M->SetVectorParameterValue(TEXT("Color2"), *Colour);
	}
}
} // namespace TorqueShedItemsImpl

namespace TSI = TorqueShedItemsImpl;

ATorqueShedItems::ATorqueShedItems()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ShapeCube = Cube.Object;
	Tags.Add(TEXT("ShedItems"));
}

void ATorqueShedItems::BeginPlay()
{
	Super::BeginPlay();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		WatchItems = B->OnItems.AddUObject(this, &ATorqueShedItems::Rebuild);
		// An engine on the stand or a wheel on the changer changes when its parts do.
		WatchBikes = B->OnChanged.AddLambda([this](int32) { Rebuild(); });
	}
	Rebuild();
}

void ATorqueShedItems::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		B->OnItems.Remove(WatchItems);
		B->OnChanged.Remove(WatchBikes);
	}
	Super::EndPlay(Reason);
}

int32 ATorqueShedItems::ItemOf(const UActorComponent* C)
{
	if (!C) return 0;
	for (const FName& T : C->ComponentTags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(TSI::ItemPrefix)) return FCString::Atoi(*S.Mid(FCString::Strlen(TSI::ItemPrefix)));
	}
	return 0;
}

bool ATorqueShedItems::WhereIs(int32 Item, FVector& Out) const
{
	for (const FDrawn& D : Drawn)
		if (D.Item == Item && D.Holder.IsValid())
		{
			Out = D.Holder->GetComponentTransform().TransformPosition(D.Centre);
			return true;
		}
	return false;
}

void ATorqueShedItems::MeshesOf(int32 Item, TArray<const UStaticMeshComponent*>& Out) const
{
	for (const UStaticMeshComponent* C : Meshes)
		if (C && ItemOf(C) == Item) Out.Add(C);
}

void ATorqueShedItems::Rebuild()
{
	for (FDrawn& D : Drawn)
		if (USceneComponent* H = D.Holder.Get()) H->DestroyComponent(true);
	for (UStaticMeshComponent* C : Meshes)
		if (C) C->DestroyComponent();
	Drawn.Reset();
	Meshes.Reset();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
	{
		const ETorqueAt At = KV.Value.At;
		if (At == ETorqueAt::Spot || At == ETorqueAt::Floor || At == ETorqueAt::Held) Draw(KV.Key);
	}
}

void ATorqueShedItems::Draw(int32 Id)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const FTorqueItem* It = B ? B->Item(Id) : nullptr;
	if (!It) return;
	USceneComponent* Holder = NewObject<USceneComponent>(this);
	Holder->SetupAttachment(RootComponent);
	Holder->RegisterComponent();
	Holder->SetWorldTransform(FTransform::Identity);
	TArray<UStaticMeshComponent*> Made;
	auto Block = [this, Holder, &Made](const FVector& Size, const FLinearColor& Colour)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(ShapeCube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Holder);
		C->SetRelativeLocation(FVector(0.0, 0.0, Size.Z * 0.5));
		C->SetRelativeScale3D(Size / 100.0);
		C->RegisterComponent();
		TSI::SetLooks(C, 0.0f, 0.0f, &Colour);
		Made.Add(C);
	};
	if (It->bBoxed)
		Block(TSI::KindSize(It->Kind) + FVector(8.0), TSI::Cardboard);
	else
	{
		// The model's own part, its fit option for a catalogue part, or the stock part of that kind
		// tinted the catalogue's colour; an engine with what's on it, a wheel with its tyre.
		const TArray<FName> KindSlots = TorqueShedParts::SlotsForKind(It->Kind);
		const FName Home = TorqueShedParts::IsStock(It->Part) ? TorqueShedParts::StockSlot(It->Part)
			: (KindSlots.Num() > 0 ? KindSlots[0] : NAME_None);
		TSet<FName> Show;
		Show.Add(Home);
		for (const TPair<FName, int32>& KV : It->Slots)
			if (KV.Value != 0) Show.Add(KV.Key);
		if (It->Tyre != 0) Show.Add(TorqueShedParts::TyreOf(Home));
		// A catalogue part (any style's) is its own Blender part, with what's on it that's catalogue too.
		const bool bCatalogue = !TorqueShedParts::IsStock(It->Part) && TorqueShedParts::IsPart(It->Part);
		if (bCatalogue)
		{
			TMap<FString, FString> Parts;
			Parts.Add(Home.ToString().ToLower(), It->Part.ToString().ToLower());
			for (const TPair<FName, int32>& KV : It->Slots)
				if (const FTorqueItem* Sub = B->Item(KV.Value))
					if (!TorqueShedParts::IsStock(Sub->Part)) Parts.Add(KV.Key.ToString().ToLower(), Sub->Part.ToString().ToLower());
			if (const FTorqueItem* Tyre = B->Item(It->Tyre))
				if (!TorqueShedParts::IsStock(Tyre->Part)) Parts.Add(TorqueShedParts::TyreOf(Home).ToString().ToLower(), Tyre->Part.ToString().ToLower());
			TorqueShedCatalogue::MakePart(this, Holder, Home.ToString().ToLower(), Parts, Made);
		}
		FName FitSlot, Option;
		const bool bOption = !bCatalogue && TorqueShedParts::ModelOption(It->Part, It->Kind, FitSlot, Option);
		if (Made.Num() == 0)
		{
			FTorqueBuild Build;
			Build.Bike = FName(TorqueShedParts::StockModel);
			if (bOption) Build.Fitted.Add(FitSlot, Option);
			TorqueShedModel::MakePartsWhere(this, Holder, Build,
				[&Show](const FTorqueResolvedPart& P) { return Show.Contains(TorqueShedParts::SlotOfModelPart(P)); }, Made);
		}
		const bool bTint = UTorqueBuildSubsystem::Repainted(*It) || (!TorqueShedParts::IsStock(It->Part) && !bOption);
		if (Made.Num() == 0) Block(TSI::KindSize(It->Kind), UTorqueBuildSubsystem::ShownColour(*It));
		else
			for (UStaticMeshComponent* C : Made)
			{
				// The parts on an engine or the tyre on a wheel look as they are.
				const FTorqueItem* Own = It;
				const FName Slot = TorqueShedModel::SlotTag(C);
				if (Slot != Home)
				{
					const int32 SubId = Slot == TorqueShedParts::TyreOf(Home) ? It->Tyre : It->Slots.FindRef(Slot);
					if (const FTorqueItem* Sub = B->Item(SubId)) Own = Sub;
				}
				const bool bOwnTint = (Own == It ? bTint : UTorqueBuildSubsystem::Repainted(*Own)) && !C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag));
				const FLinearColor OwnShown = UTorqueBuildSubsystem::ShownColour(*Own);   // grey while it's in primer
				TSI::SetLooks(C, Own->Dirt, Own->Rust, bOwnTint ? &OwnShown : nullptr);
			}
	}
	FBox Box(ForceInit);
	for (UStaticMeshComponent* C : Made)
	{
		C->ComponentTags.Add(FName(*FString::Printf(TEXT("%s%d"), TSI::ItemPrefix, Id)));
		C->UpdateBounds();
		Box += C->Bounds.GetBox();
		Meshes.Add(C);
	}
	if (!Box.IsValid)
	{
		Holder->DestroyComponent();
		return;
	}
	FDrawn D;
	D.Item = Id;
	D.Holder = Holder;
	D.Centre = Box.GetCenter();
	D.Offset = FVector(D.Centre.X, D.Centre.Y, Box.Min.Z);
	if (It->At == ETorqueAt::Held) D.HeldBy = It->Ref;
	Drawn.Add(D);
	// Where it sits: on its place, on the floor where it was put, or in someone's hands (Tick).
	FTransform Rest;
	if (It->At == ETorqueAt::Spot)
	{
		const FTorquePlace* P = TorqueShedLayout::FindPlace(It->Spot);
		Rest = FTransform(P ? P->Put : TorqueShedLayout::DropAt());
		// Along the workbench (it runs north-south).
		if (P && P->Kind == ETorquePlace::Bench) Rest.SetRotation(FRotator(0.0, 90.0, 0.0).Quaternion());
		// Hung on a spray bay hook: its top at the hook's tip, along the rail, never down on the grate.
		if (P && P->Kind == ETorquePlace::Hook)
			Rest = FTransform(FRotator(0.0, 90.0, 0.0), FVector(P->Put.X, P->Put.Y, FMath::Max(P->Put.Z - Box.GetSize().Z, TorqueShedLayout::HookFloor)));
	}
	else if (It->At == ETorqueAt::Floor)
		Rest = It->Place;
	if (D.HeldBy == 0)
		Holder->SetWorldLocationAndRotation(Rest.GetLocation() - Rest.GetRotation().RotateVector(D.Offset), Rest.GetRotation());
	else
		Tick(0.0f);
}

bool ATorqueShedItems::HandOf(int32 Peer, FTransform& Out) const
{
	for (TActorIterator<ATorqueWalker> W(GetWorld()); W; ++W)
	{
		const APlayerState* PS = W->GetPlayerState();
		if (!PS || PS->GetPlayerId() != Peer) continue;
		// Out in front at chest height, a little to the right: in view, not in the way.
		const FRotator Aim = W->GetBaseAimRotation();
		const FRotator Yaw(0.0, Aim.Yaw, 0.0);
		Out = FTransform(Yaw, W->GetPawnViewLocation() + Yaw.RotateVector(FVector(60.0, 18.0, -45.0)));
		return true;
	}
	return false;
}

void ATorqueShedItems::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (const FDrawn& D : Drawn)
	{
		USceneComponent* H = D.Holder.Get();
		if (!H || D.HeldBy == 0) continue;
		FTransform Hand;
		if (bHideHeld || !HandOf(D.HeldBy, Hand))
		{
			H->SetVisibility(false, true);
			continue;
		}
		H->SetVisibility(true, true);
		H->SetWorldLocationAndRotation(Hand.GetLocation() - Hand.GetRotation().RotateVector(D.Centre), Hand.GetRotation());
	}
}
