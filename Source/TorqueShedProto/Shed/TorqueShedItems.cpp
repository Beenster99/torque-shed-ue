#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueBoothHooks.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueEffectPaint.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueWalker.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
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

// A drawn item's holder and everything on it (a delivery's labels and writing too):
// DestroyComponent(true) would only promote them to the actor, left hanging where the box was.
void DestroyHolder(USceneComponent* H)
{
	if (!H) return;
	TArray<USceneComponent*> Kids;
	H->GetChildrenComponents(true, Kids);
	for (USceneComponent* K : Kids)
		if (K) K->DestroyComponent();
	H->DestroyComponent();
}

// --- Deliveries (the props thread's Data/Props/deliveries.json) -----------------------------------------
// A boxed part comes in cardboard, or a wooden crate for the heavy kinds and anything long; the box
// whose shape is nearest, scaled to the part's size; a made-up courier's label on the lid (and the
// front of a crate) with the part and a tracking number; a sticky note saying who it's for.
// Each with its native size and the exact spots on its surface (deliveries.json "anchors", in its own
// frame, cm: x right, y front, z up, from its floor centre) for the lid label, a crate's front label
// and the sticky note: clear of a crate's battens and a box's tape.
struct FBoxVariant { const TCHAR* Id; FVector Size, Lid, Front, Note; };
const FBoxVariant CardboardBoxes[] = {
	{TEXT("box_cardboard"), FVector(45, 40, 35), FVector(10.5, 11.0, 34.8), FVector::ZeroVector, FVector(-11.25, 20.0, 28.0)},
	{TEXT("box_cardboard_long"), FVector(95, 32, 30), FVector(35.5, 8.0, 29.8), FVector::ZeroVector, FVector(-23.75, 16.0, 23.0)},
	{TEXT("box_cardboard_flat"), FVector(70, 60, 25), FVector(23.0, 21.0, 24.8), FVector::ZeroVector, FVector(-17.5, 30.0, 18.0)}};
const FBoxVariant Crates[] = {
	{TEXT("crate_wood"), FVector(75, 65, 65), FVector(20.5, 23.5, 65.0), FVector(20.5, 32.5, 22.0), FVector(-24.7, 32.5, 52.5)},
	{TEXT("crate_wood_long"), FVector(150, 60, 55), FVector(58.0, 21.0, 55.0), FVector(58.0, 30.0, 22.0), FVector(-62.2, 30.0, 42.5)},
	{TEXT("crate_wood_flat"), FVector(100, 85, 45), FVector(33.0, 33.5, 45.0), FVector(33.0, 42.5, 22.0), FVector(-37.2, 42.5, 32.5)}};
constexpr double Proud = 0.05;   // how far the label and note stand off the surface
const TCHAR* const CrateKinds[] = {TEXT("frame"), TEXT("engine"), TEXT("gearbox"), TEXT("head"), TEXT("wheel"), TEXT("tyre"), TEXT("fork"),
	TEXT("swingarm"), TEXT("exhaust"), TEXT("fairing"), TEXT("tank")};
constexpr double CrateOverCm = 75.0;
const FVector2D LabelSize(16.0, 10.0), NoteSize(7.6, 7.6);
constexpr int32 Couriers = 8;

UStaticMesh* PropMesh(const FString& Id)
{
	// Loaded once each; a prop not imported yet is remembered as missing.
	static TMap<FString, TWeakObjectPtr<UStaticMesh>> Found;
	static TSet<FString> Missing;
	if (Missing.Contains(Id)) return nullptr;
	if (const TWeakObjectPtr<UStaticMesh>* M = Found.Find(Id); M && M->IsValid()) return M->Get();
	const FString Path = FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), *Id, *Id, *Id);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Mesh) Found.Add(Id, Mesh);
	else Missing.Add(Id);
	return Mesh;
}

// How far apart two boxes' shapes are, whatever their sizes.
double ShapeGap(const FVector& A, const FVector& B)
{
	const FVector L(FMath::Loge(A.X / B.X), FMath::Loge(A.Y / B.Y), FMath::Loge(A.Z / B.Z));
	const double Mean = (L.X + L.Y + L.Z) / 3.0;
	return FMath::Square(L.X - Mean) + FMath::Square(L.Y - Mean) + FMath::Square(L.Z - Mean);
}

// A stable number from the item id (bike_model.gd courier_for / tracking_for seed on it too).
uint32 Seed(int32 Id, uint32 Salt) { return HashCombine(GetTypeHash(Id), Salt); }

// A rotation from where its +x and +z should point.
FRotator Facing(const FVector& X, const FVector& Z) { return FRotationMatrix::MakeFromXZ(X, Z).Rotator(); }

UTextRenderComponent* Words(AActor* Owner, USceneComponent* Parent, const FString& Text, const FVector& At, const FRotator& Rot,
	float Size, const FColor& Ink)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(Owner);
	T->SetupAttachment(Parent);
	T->RegisterComponent();
	T->SetRelativeLocationAndRotation(At, Rot);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Ink);
	T->SetCastShadow(false);
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	T->SetText(FText::FromString(Text));
	return T;
}

UStaticMeshComponent* Prop(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FRotator& Rot, const FVector& Scale)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent);
	C->SetRelativeLocationAndRotation(At, Rot);
	C->SetRelativeScale3D(Scale);
	C->RegisterComponent();
	return C;
}

// The delivery for It, Size across (cm, x long, y wide, z high), its floor in the middle of Parent.
// False (nothing made) while the box models aren't in the project.
bool MakeDelivery(AActor* Owner, USceneComponent* Parent, const FTorqueItem& It, const FVector& Size, TArray<UStaticMeshComponent*>& Made)
{
	bool bCrate = Size.GetMax() > CrateOverCm;
	for (const TCHAR* K : CrateKinds) bCrate = bCrate || It.Kind == FName(K);
	const FBoxVariant* Best = nullptr;
	double BestGap = 1e30;
	for (const FBoxVariant& V : bCrate ? MakeArrayView(Crates) : MakeArrayView(CardboardBoxes))
	{
		const double Gap = ShapeGap(Size, V.Size);
		if (Gap < BestGap) { BestGap = Gap; Best = &V; }
	}
	UStaticMesh* BoxMesh = Best ? PropMesh(Best->Id) : nullptr;
	if (!BoxMesh) return false;
	// Scaled to the part's box by the model's own size (its origin's its floor centre, its front +y).
	const FVector Native = BoxMesh->GetBoundingBox().GetSize();
	const FVector Scale(Size.X / FMath::Max(Native.X, 1.0), Size.Y / FMath::Max(Native.Y, 1.0), Size.Z / FMath::Max(Native.Z, 1.0));
	Made.Add(Prop(Owner, Parent, BoxMesh, FVector::ZeroVector, FRotator::ZeroRotator, Scale));
	// The courier's label, flat on the lid near a corner, and on a crate's front too.
	const int32 Courier = (int32)(Seed(It.Id, 0x0C0021E5u) % Couriers);
	const uint32 Track = Seed(It.Id, 0x7AC4u);
	const FString Tracking = FString::Printf(TEXT("TS %03u %04u %03u"), Track % 1000u, (Track / 1000u) % 10000u, (Track / 10000000u) % 1000u);
	const FString LabelText = TorqueShedParts::PartName(It.Part) + TEXT("\n") + Tracking;
	const FColor Ink(20, 20, 20);
	UStaticMesh* LabelMesh = PropMesh(TEXT("courier_label"));
	auto Label = [&](const FVector& At, const FRotator& Rot, const FRotator& TextRot, const FVector& StripOffset)
	{
		if (UStaticMeshComponent* L = Prop(Owner, Parent, LabelMesh, At, Rot, FVector::OneVector))
		{
			Made.Add(L);   // moves, hides and is aimed at with the box
			if (UMaterialInstanceDynamic* M = L->CreateDynamicMaterialInstance(0)) M->SetScalarParameterValue(TEXT("Courier"), (float)Courier);
		}
		// The contents and tracking number over the label's blank strip (its lower quarter).
		Words(Owner, Parent, LabelText, At + StripOffset, TextRot, 0.9f, Ink);
	};
	if (LabelMesh)
	{
		// At the model's own spots, moved with its scale; the labels stay their real size.
		const FVector Lid = Best->Lid * Scale + FVector(0.0, 0.0, Proud);
		// Flat, read from the front (+y): its top away from you.
		Label(Lid, FRotator::ZeroRotator, Facing(FVector::UpVector, FVector(0.0, -1.0, 0.0)), FVector(0.0, LabelSize.Y * 0.36, 0.05));
		if (bCrate)
		{
			// On the front, standing up, its top up.
			const FVector Front = Best->Front * Scale + FVector(0.0, Proud, 0.0);
			Label(Front, Facing(FVector::ForwardVector, FVector(0.0, 1.0, 0.0)), Facing(FVector(0.0, 1.0, 0.0), FVector::UpVector),
				FVector(0.0, 0.05, -LabelSize.Y * 0.36));
		}
	}
	// The sticky note: at its spot on the front face, a few degrees off square, in the
	// colour of whoever ordered it, with their name.
	if (UStaticMesh* NoteMesh = PropMesh(TEXT("sticky_note")))
	{
		// (The note's model stands up already, facing +y like the box's front, its back on its y 0.)
		const FVector At = Best->Note * Scale + FVector(0.0, Proud, 0.0);
		const float Skew = (float)((int32)(Seed(It.Id, 0x5717u) % 9u) - 4);
		const FQuat Tilt(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(Skew));
		const FRotator Rot = Tilt.Rotator();
		if (UStaticMeshComponent* N = Prop(Owner, Parent, NoteMesh, At, Rot, FVector::OneVector))
		{
			Made.Add(N);
			if (UMaterialInstanceDynamic* M = N->CreateDynamicMaterialInstance(0)) M->SetVectorParameterValue(TEXT("Tint"), It.OrderedTint);
		}
		FString Who = It.OrderedBy;
		if (Who.IsEmpty())
			if (const UWorld* World = Owner->GetWorld())
				if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerState) Who = PC->PlayerState->GetPlayerName();
		const FRotator TextRot = (Tilt * Facing(FVector(0.0, 1.0, 0.0), FVector::UpVector).Quaternion()).Rotator();
		Words(Owner, Parent, TEXT("For ") + Who, At + FVector(0.0, 0.15, 0.0), TextRot, 1.1f, FColor(25, 25, 30));
	}
	return true;
}
const TCHAR* const ItemPrefix = TEXT("Item:");

void SetLooks(UStaticMeshComponent* C, float Dirt, float Rust, const FLinearColor* Colour, const FTorqueItem* Finished = nullptr,
	const FTransform& ToPart = FTransform::Identity, bool bOverBare = false)
{
	// Sprayed with a pearl, candy, flake or chameleon from the mixing bay, or masked: the effect paint.
	TorquePaintMix::FPaintLook Effect;
	if (Colour && Finished && UTorqueBuildSubsystem::EffectLook(*Finished, Effect) && TorqueEffectPaint::Apply(C, Effect, Dirt, ToPart)) return;
	// A part with no painted surface of its own (a stainless exhaust, a chrome bar) sprayed all over:
	// its metal's material has no colour to set, so it's drawn in the paint material, plain.
	if (bOverBare && Colour && Finished)
	{
		TorquePaintMix::FPaintLook Plain;
		FLinearColor Shown = *Colour;
		float Metallic = 0.05f, Roughness = 0.15f;
		if (!UTorqueBuildSubsystem::FinishLook(*Finished, Shown, Metallic, Roughness) && Finished->bPrimerTop) Roughness = 0.75f;
		Plain.Spec.Colour = Shown;
		Plain.Spec.Metal = Metallic;
		Plain.Spec.Rough = Roughness;
		if (TorqueEffectPaint::Apply(C, Plain, Dirt, ToPart)) return;
	}
	TorqueEffectPaint::Restore(C);
	UMaterialInstanceDynamic* M = C ? C->CreateDynamicMaterialInstance(0) : nullptr;
	if (!M) return;
	M->SetScalarParameterValue(TEXT("Grime"), Dirt);
	M->SetScalarParameterValue(TEXT("Rust"), Rust);
	if (!Colour) return;
	FLinearColor Shown = *Colour;
	float Metallic = 0.0f, Roughness = 0.5f;
	const bool bFinish = Finished && UTorqueBuildSubsystem::FinishLook(*Finished, Shown, Metallic, Roughness);
	M->SetVectorParameterValue(TEXT("Color"), Shown);
	M->SetVectorParameterValue(TEXT("Color2"), Shown);
	if (bFinish)
	{
		// Repainted: its finish (gloss, matte, metallic, chrome, patina, primer, a satin clear).
		for (const TCHAR* Param : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(Param, Metallic);
		for (const TCHAR* Param : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(Param, Roughness);
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

int32 ATorqueShedItems::MakeNewPart(AActor* Outer, USceneComponent* Holder, FName Part, UStaticMesh* Cube, TArray<UStaticMeshComponent*>& Made)
{
	// Draw's unboxed path for a part fresh from the shop: its home slot, the catalogue's Blender part
	// (or the stock part of that kind), tinted the catalogue's colour.
	const int32 Before = Made.Num();
	if (!Outer || !Holder || !TorqueShedParts::IsPart(Part)) return 0;
	const FName Kind = TorqueShedParts::PartKind(Part);
	const TArray<FName> KindSlots = TorqueShedParts::SlotsForKind(Kind);
	const FName Home = TorqueShedParts::IsStock(Part) ? TorqueShedParts::StockSlot(Part) : (KindSlots.Num() > 0 ? KindSlots[0] : NAME_None);
	const bool bCatalogue = !TorqueShedParts::IsStock(Part);
	if (bCatalogue && !Home.IsNone())
	{
		TMap<FString, FString> Parts;
		Parts.Add(Home.ToString().ToLower(), Part.ToString().ToLower());
		TorqueShedCatalogue::MakePart(Outer, Holder, Home.ToString().ToLower(), Parts, Made);
	}
	FName FitSlot, Option;
	const bool bOption = !bCatalogue && TorqueShedParts::ModelOption(Part, Kind, FitSlot, Option);
	if (Made.Num() == Before)
	{
		FTorqueBuild Build;
		Build.Bike = FName(TorqueShedParts::StockModel);
		if (bOption) Build.Fitted.Add(FitSlot, Option);
		TorqueShedModel::MakePartsWhere(Outer, Holder, Build,
			[Home](const FTorqueResolvedPart& P) { return TorqueShedParts::SlotOfModelPart(P) == Home; }, Made);
	}
	const FLinearColor Colour = TorqueShedParts::DefaultColour(Part);
	if (Made.Num() == Before)
	{
		if (!Cube) return 0;
		// No shape for it yet: a block the size of its kind, as the shed draws it.
		const FVector Size = TSI::KindSize(Kind);
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Outer);
		C->SetStaticMesh(Cube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Holder);
		C->SetRelativeLocation(FVector(0.0, 0.0, Size.Z * 0.5));
		C->SetRelativeScale3D(Size / 100.0);
		C->RegisterComponent();
		TSI::SetLooks(C, 0.0f, 0.0f, &Colour);
		Made.Add(C);
		return Made.Num() - Before;
	}
	const bool bTint = bCatalogue;   // (as Draw: a catalogue part shows the catalogue's colour)
	for (int32 I = Before; I < Made.Num(); ++I)
	{
		UStaticMeshComponent* C = Made[I];
		const bool bPaint = bTint && !C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag));
		TSI::SetLooks(C, 0.0f, 0.0f, bPaint ? &Colour : nullptr);
	}
	return Made.Num() - Before;
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

bool ATorqueShedItems::SizeOf(int32 Item, FVector& Out) const
{
	for (const FDrawn& D : Drawn)
		if (D.Item == Item && !D.Size.IsNearlyZero())
		{
			Out = D.Size;
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
		TSI::DestroyHolder(D.Holder.Get());
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
	// [fluids] Forget the cans no longer drawn (binned, boxed, gone).
	TSet<int32> Shown;
	for (const FDrawn& D : Drawn) Shown.Add(D.Item);
	for (auto VIt = Vessels.CreateIterator(); VIt; ++VIt)
		if (!Shown.Contains(VIt.Key())) VIt.RemoveCurrent();
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
	TorqueFluidModels::FVessel Vessel;
	bool bVessel = false;
	if (It->bBoxed)
	{
		// As delivered: the props' box or crate, labelled, or a plain cardboard block until they're in.
		const FVector BoxSize = TSI::KindSize(It->Kind) + FVector(8.0);
		if (!TSI::MakeDelivery(this, Holder, *It, BoxSize, Made)) Block(BoxSize, TSI::Cardboard);
	}
	else if (TorqueShedParts::IsFluid(It->Part) && TorqueFluidModels::Make(this, Holder, It->Part, Made, Vessel))
	{
		// [fluids] A can or bottle: see-through, with its fluid in it (Tick moves the level and the slosh).
		bVessel = true;
	}
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
		{
			// The parts on an engine or the tyre on a wheel look as they are.
			auto OwnOf = [&](UStaticMeshComponent* C) -> const FTorqueItem*
			{
				const FName Slot = TorqueShedModel::SlotTag(C);
				if (Slot == Home) return It;
				const int32 SubId = Slot == TorqueShedParts::TyreOf(Home) ? It->Tyre : It->Slots.FindRef(Slot);
				const FTorqueItem* Sub = B->Item(SubId);
				return Sub ? Sub : It;
			};
			// Each part's box in the holder's frame (the bike's: x forward, z up), for a masked stripe.
			const FTransform Frame = Holder->GetComponentTransform();
			TMap<const FTorqueItem*, FBox> Boxes;
			for (UStaticMeshComponent* C : Made) Boxes.FindOrAdd(OwnOf(C), FBox(ForceInit)) += TorqueEffectPaint::BoxIn(C, Frame);
			// Which parts have a painted surface at all (else spraying them paints the lot, as the spray
			// game does: TorqueSpray::MeshesOf).
			TSet<const FTorqueItem*> HasPaint;
			for (UStaticMeshComponent* C : Made)
				if (!C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag))) HasPaint.Add(OwnOf(C));
			for (UStaticMeshComponent* C : Made)
			{
				const FTorqueItem* Own = OwnOf(C);
				const bool bBare = C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag));
				const bool bOverBare = bBare && !HasPaint.Contains(Own) && UTorqueBuildSubsystem::Repainted(*Own);
				const bool bOwnTint = (Own == It ? bTint : UTorqueBuildSubsystem::Repainted(*Own)) && (!bBare || bOverBare);
				const FLinearColor OwnShown = UTorqueBuildSubsystem::ShownColour(*Own);   // grey while it's in primer
				const FBox Box = Boxes.FindRef(Own);
				TSI::SetLooks(C, Own->Dirt, Own->Rust, bOwnTint ? &OwnShown : nullptr, UTorqueBuildSubsystem::Repainted(*Own) ? Own : nullptr,
					TorqueEffectPaint::PartFrame(C, Frame, Box.IsValid ? Box.GetCenter() : FVector::ZeroVector), bOverBare);
			}
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
		TSI::DestroyHolder(Holder);
		return;
	}
	FDrawn D;
	D.Item = Id;
	D.Holder = Holder;
	D.Centre = Box.GetCenter();
	D.Offset = FVector(D.Centre.X, D.Centre.Y, Box.Min.Z);
	D.Size = Box.GetSize();   // (the holder is at the origin, unturned, while it's made)
	if (It->At == ETorqueAt::Held) D.HeldBy = It->Ref;
	if (It->At == ETorqueAt::Spot)
		if (const FTorquePlace* HookP = TorqueShedLayout::FindPlace(It->Spot); HookP && HookP->Kind == ETorquePlace::Hook)
		{
			D.Hook = HookP->Index;
			D.Height = Box.GetSize().Z;
		}
	if (It->At == ETorqueAt::Spot)
		if (const FTorquePlace* StandP = TorqueShedLayout::FindPlace(It->Spot); StandP && StandP->Kind == ETorquePlace::Stand)
			D.bOnStand = true;   // [kit]
	if (It->At == ETorqueAt::Spot)
		if (const FTorquePlace* ChangerP = TorqueShedLayout::FindPlace(It->Spot); ChangerP && ChangerP->Kind == ETorquePlace::Changer)
			D.bOnChanger = true;   // [tyre]
	Drawn.Add(D);
	if (bVessel)
	{
		// The same can redrawn (something in the shed changed): its level and slosh carry on.
		if (const TorqueFluidModels::FVessel* Was = Vessels.Find(Id))
		{
			Vessel.Shown = Was->Shown;
			Vessel.Tilt = Was->Tilt;
			Vessel.TiltRate = Was->TiltRate;
			Vessel.Clock = Was->Clock;
		}
		Vessels.Add(Id, Vessel);
	}
	else Vessels.Remove(Id);
	// Where it sits: on its place, on the floor where it was put, or in someone's hands (Tick).
	FTransform Rest;
	if (It->At == ETorqueAt::Spot)
	{
		const FTorquePlace* P = TorqueShedLayout::FindPlace(It->Spot);
		Rest = FTransform(P ? P->Put : TorqueShedLayout::DropAt());
		// Along the workbench (it runs north-south).
		if (P && P->Kind == ETorquePlace::Bench) Rest.SetRotation(FRotator(0.0, 90.0, 0.0).Quaternion());
		// Hung on a spray bay hook: its top at the hook's tip, along the rail, never down on the grate.
		if (P && P->Kind == ETorquePlace::Hook) Rest = HungAt(D.Hook, D.Height);
		// [kit] On the engine stand: turned as the stand is.
		if (P && P->Kind == ETorquePlace::Stand) Rest.SetRotation(TorqueShedLayout::StandNow().GetRotation());
		// [tyre] On the tyre changer: flat on its turntable (the holder's placed below, as Tick does).
		if (D.bOnChanger)
		{
			const FTransform Flat = OnChanger(D, ChangerSpin);
			Holder->SetWorldLocationAndRotation(Flat.GetLocation(), Flat.GetRotation());
			return;
		}
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
		// Out in front at chest height, a little to the right: in view, not in the way. Turned as
		// R has turned it (walker.gd hold_turn), the way it'll go down.
		const FRotator Aim = W->GetBaseAimRotation();
		const FRotator Yaw(0.0, Aim.Yaw, 0.0);
		Out = FTransform(FRotator(0.0, Aim.Yaw + W->HeldTurn(), 0.0), W->GetPawnViewLocation() + Yaw.RotateVector(FVector(60.0, 18.0, -45.0)));
		return true;
	}
	return false;
}

FTransform ATorqueShedItems::HungAt(int32 Hook, double Height) const
{
	const FVector At = TorqueShedLayout::HookAt(Hook);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const double Tip = B ? (double)B->HookTipOf(Hook) : TorqueShedLayout::HookTip;
	const FVector Still(At.X, At.Y, FMath::Max(Tip - Height, TorqueShedLayout::HookFloor));
	FVector Pivot;
	FQuat Turn;
	ATorqueBoothHooks::Swing(this, Hook, Pivot, Turn);
	return FTransform(Turn * FRotator(0.0, 90.0, 0.0).Quaternion(), Pivot + Turn.RotateVector(Still - Pivot));
}

FTransform ATorqueShedItems::OnChanger(const FDrawn& D, double Spin) const
{
	// workshop.gd: a wheel lies flat on the tyre changer's turntable. Its axle is its box's thinnest
	// side: turned upright, then about the upright by Spin, the box's middle over the place and its
	// underside on it.
	const FTorquePlace* P = TorqueShedLayout::FindPlace(FName(TEXT("changer")));
	const FVector Put = P ? P->Put : TorqueShedLayout::DropAt();
	FQuat Lay = FQuat::Identity;
	double Thick = D.Size.Z;
	if (D.Size.Y <= D.Size.X && D.Size.Y < D.Size.Z)
	{
		Lay = FRotator(0.0, 0.0, 90.0).Quaternion();
		Thick = D.Size.Y;
	}
	else if (D.Size.X < D.Size.Y && D.Size.X < D.Size.Z)
	{
		Lay = FRotator(90.0, 0.0, 0.0).Quaternion();
		Thick = D.Size.X;
	}
	const FQuat Turn = FQuat(FVector::UpVector, Spin) * Lay;
	return FTransform(Turn, Put + FVector(0.0, 0.0, Thick * 0.5) - Turn.RotateVector(D.Centre));
}

void ATorqueShedItems::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickVessels(DeltaSeconds);
	// [tyre] The tyre changer's turntable turns while anyone works a tyre on or off (2.2 rad/s, workshop.gd).
	bool bTurning = false;
	for (TActorIterator<ATorqueWalker> W(GetWorld()); W && !bTurning; ++W) bTurning = W->TurningChanger();
	if (bTurning) ChangerSpin = FMath::Fmod(ChangerSpin + DeltaSeconds * 2.2, UE_DOUBLE_TWO_PI);
	for (const FDrawn& D : Drawn)
	{
		USceneComponent* H = D.Holder.Get();
		if (H && D.HeldBy == 0 && D.bOnChanger)
		{
			if (bTurning)
			{
				const FTransform Flat = OnChanger(D, ChangerSpin);
				H->SetWorldLocationAndRotation(Flat.GetLocation(), Flat.GetRotation());
			}
			continue;
		}
		if (H && D.HeldBy == 0 && D.Hook != INDEX_NONE)
		{
			// On a booth hook: at its height, swinging with it.
			const FTransform Rest = HungAt(D.Hook, D.Height);
			H->SetWorldLocationAndRotation(Rest.GetLocation() - Rest.GetRotation().RotateVector(D.Offset), Rest.GetRotation());
			continue;
		}
		if (H && D.HeldBy == 0 && D.bOnStand)
		{
			// [kit] On the engine stand: where it is now, rolling with it while someone wheels it.
			if (const FTorquePlace* P = TorqueShedLayout::FindPlace(FName(TEXT("stand"))))
			{
				const FQuat Turn = TorqueShedLayout::StandNow().GetRotation();
				H->SetWorldLocationAndRotation(P->Put - Turn.RotateVector(D.Offset), Turn);
			}
			continue;
		}
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

void ATorqueShedItems::TickVessels(float DeltaSeconds)
{
	// [fluids] Each can's level (what's in it, eased as it pours) and its fluid's slosh as it's carried,
	// in the fluid's colour, darker as it ages.
	if (Vessels.Num() == 0 || DeltaSeconds <= 0.0f) return;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	for (TPair<int32, TorqueFluidModels::FVessel>& KV : Vessels)
	{
		const FTorqueItem* Can = B->Item(KV.Key);
		if (!Can) continue;
		const float Cap = TorqueShedParts::FluidCapacity(Can->Part);
		const float Fill = Cap > 0.0f ? Can->Litres / Cap : 0.0f;
		const FName Kind = TorqueShedParts::PartKind(Can->Part);
		const FName Source = Can->FluidType.IsNone() ? Can->Part : Can->FluidType;
		const FLinearColor Colour = TorqueFluidModels::AgedColour(Kind, TorqueShedParts::LiquidColour(Source), Can->FluidAge);
		TorqueFluidModels::Update(KV.Value, Fill, Colour, Can->FluidAge, DeltaSeconds);
	}
}

bool ATorqueShedItems::TestSlosh(int32 Item, const FVector2D& Kick, FString& Out)
{
	TorqueFluidModels::FVessel* V = Vessels.Find(Item);
	if (!V)
	{
		Out = FString::Printf(TEXT("item %d isn't drawn as a can"), Item);
		return false;
	}
	V->TiltRate += Kick;
	const double Deg = FMath::RadiansToDegrees(FMath::Atan(V->Tilt.Size()));
	Out = FString::Printf(TEXT("slosh: level shown %.2f, surface tilt %.1f deg (x %.3f, y %.3f), swinging %.2f/s, liquid %s%s"),
		V->Shown, Deg, V->Tilt.X, V->Tilt.Y, V->TiltRate.Size(), V->Liquid.IsValid() ? TEXT("drawn") : TEXT("none (opaque can)"),
		Kick.IsNearlyZero() ? TEXT("") : TEXT(", kicked"));
	return true;
}
