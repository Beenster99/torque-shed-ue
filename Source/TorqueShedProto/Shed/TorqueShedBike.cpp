#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueWashJob.h"

#include "Shed/TorqueShedCatalogue.h"

#include "Shed/TorqueBikeBadge.h"
#include "Shed/TorqueBikeLift.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueEffectPaint.h"
#include "Shed/TorqueShedParts.h"
#include "TorqueBikePawn.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueShedBikeImpl
{
bool LoadShedJson(const FString& Rel, TSharedPtr<FJsonObject>& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), Rel))) return false;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

// The model's frame (metres, x forward, y up, z right) to Unreal local cm (x forward, y right, z up).
FVector ShedModelToLocal(const FVector& P) { return FVector(P.X * 100.0, P.Z * 100.0, P.Y * 100.0); }

// Not FNames: those mustn't be made while the program is still starting (a monolithic build).
const TCHAR* const SlotPrefix = TEXT("Slot:");
const TCHAR* const OptionPrefix = TEXT("Option:");

FName TagAfter(const UActorComponent* C, const FString& Prefix)
{
	if (!C) return NAME_None;
	for (const FName& T : C->ComponentTags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(Prefix)) return FName(*S.Mid(Prefix.Len()));
	}
	return NAME_None;
}
} // namespace TorqueShedBikeImpl
using namespace TorqueShedBikeImpl;

namespace TorqueShedModel
{
bool LoadFrame(FTransform& Fix, TMap<FName, FVector>& GroupOrigins)
{
	TSharedPtr<FJsonObject> Assets, Spec;
	if (!LoadShedJson(TEXT("Data/Bike/SpeedTwin/ue_assets.json"), Assets) ||
		!LoadShedJson(TEXT("Data/Bike/SpeedTwin/speedtwin.json"), Spec))
		return false;
	double Yaw = 0.0, Mirror = 1.0, Scale = 1.0;
	const TSharedPtr<FJsonObject>* Frame = nullptr;
	if (Assets->TryGetObjectField(TEXT("frame"), Frame))
	{
		Yaw = (*Frame)->GetNumberField(TEXT("yaw"));
		Mirror = (*Frame)->GetNumberField(TEXT("mirror"));
		Scale = (*Frame)->GetNumberField(TEXT("scale"));
	}
	// The same as the ridden bike (ATorqueBikePawn::BuildModel).
	Fix = FTransform(FRotator(0.0, Yaw, 0.0), FVector::ZeroVector, FVector(Scale, Scale * Mirror, Scale));
	GroupOrigins.Reset();
	const TSharedPtr<FJsonObject>* Origins = nullptr;
	if (Spec->TryGetObjectField(TEXT("group_origins"), Origins))
	{
		for (const auto& KV : (*Origins)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!KV.Value.IsValid() || !KV.Value->TryGetArray(A) || A->Num() < 3) continue;
			GroupOrigins.Add(FName(*KV.Key), ShedModelToLocal(FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber())));
		}
	}
	return true;
}

FName SlotTag(const UActorComponent* C) { return TagAfter(C, SlotPrefix); }
FName OptionTag(const UActorComponent* C) { return TagAfter(C, OptionPrefix); }

FName PartTag(const UActorComponent* C) { return TagAfter(C, TEXT("Part:")); }

bool IsBodywork(FName Part)
{
	static const TCHAR* const Names[] = {TEXT("tank"), TEXT("side_panel_l"), TEXT("side_panel_r"),
		TEXT("front_mudguard"), TEXT("tail"), TEXT("hugger")};
	for (const TCHAR* N : Names)
		if (Part == FName(N)) return true;
	return false;
}

FString PartLabel(FName Part) { return Part.ToString().Replace(TEXT("_"), TEXT(" ")); }

void ApplyLooks(const UObject* WorldContext, const TArray<UStaticMeshComponent*>& Parts)
{
	ApplyLooks(WorldContext, UTorqueBuildSubsystem::BikeFor(WorldContext), Parts);
}

float FlyGain(FName Slot)
{
	// Dirt.FLY_SLOTS: the parts out in front that collect dead flies at speed, and how many land on each.
	static const TMap<FName, float> Gains = {
		{TEXT("headlight"), 1.0f}, {TEXT("fairing"), 1.0f}, {TEXT("fog_lights"), 0.9f}, {TEXT("mirrors"), 0.6f},
		{TEXT("front_mudguard"), 0.7f}, {TEXT("fork"), 0.55f}, {TEXT("indicators"), 0.5f}, {TEXT("number_boards"), 0.6f},
		{TEXT("handlebars"), 0.35f}, {TEXT("dashboard"), 0.35f}, {TEXT("tank"), 0.3f}, {TEXT("front_master"), 0.3f},
		{TEXT("horn"), 0.5f}, {TEXT("cooling"), 0.45f}, {TEXT("steering_damper"), 0.3f}, {TEXT("lever_guards"), 0.3f}};
	const float* G = Gains.Find(Slot);
	return G ? *G : 0.0f;
}

void SetDirtFrame(UMaterialInstanceDynamic* M, const FTransform& PartToBike, const FVector2D& ZRange)
{
	if (!M) return;
	const FMatrix X = PartToBike.ToMatrixWithScale();
	M->SetVectorParameterValue(TEXT("Row0"), FLinearColor((float)X.M[0][0], (float)X.M[1][0], (float)X.M[2][0], 0.0f));
	M->SetVectorParameterValue(TEXT("Row1"), FLinearColor((float)X.M[0][1], (float)X.M[1][1], (float)X.M[2][1], 0.0f));
	M->SetVectorParameterValue(TEXT("Row2"), FLinearColor((float)X.M[0][2], (float)X.M[1][2], (float)X.M[2][2], 0.0f));
	M->SetVectorParameterValue(TEXT("RowOffset"), FLinearColor((float)X.M[3][0], (float)X.M[3][1], (float)X.M[3][2], 0.0f));
	M->SetVectorParameterValue(TEXT("ZRange"), FLinearColor((float)ZRange.X, (float)ZRange.Y, 0.0f, 0.0f));
}

void ApplyLooks(const UObject* WorldContext, int32 Bike, const TArray<UStaticMeshComponent*>& Parts)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(WorldContext);
	// Being pressure washed: the wash game sets the parts' dirt itself (TorqueWashJob: as it was when
	// the wash began, and the mask of what's been washed off), so it's left as it is.   [wash]
	const bool bWashing = TorqueWash::HidesGrime(WorldContext, Bike);
	// Each part's box in the bike's frame (its actor: x forward, y right, z up), for a masked stripe.
	TMap<FName, FBox> Boxes;
	for (UStaticMeshComponent* C : Parts)
	{
		if (!C || !C->GetOwner() || PartTag(C).IsNone()) continue;
		const FName Slot = SlotTag(C);
		Boxes.FindOrAdd(Slot.IsNone() ? PartTag(C) : Slot, FBox(ForceInit)) += TorqueEffectPaint::BoxIn(C, C->GetOwner()->GetActorTransform());
	}
	// The parts with a painted surface at all: one with none (a stainless exhaust, a chrome bar) that's
	// been sprayed is painted all over, as the spray game paints it (TorqueSpray::MeshesOf).   [spray]
	TSet<FName> HasPaint;
	for (UStaticMeshComponent* C : Parts)
		if (C && !PartTag(C).IsNone() && !C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag)))
			HasPaint.Add(SlotTag(C).IsNone() ? PartTag(C) : SlotTag(C));
	// The road dirt's frame (Tools/ue_bike_dirt.py): each mesh in the bike's (its actor's, unscaled),
	// and how high the bike stands, from the bottom of its tyres.
	FBox Whole(ForceInit);
	for (const TPair<FName, FBox>& Bx : Boxes) Whole += Bx.Value;
	const FVector2D ZRange = Whole.IsValid ? FVector2D(Whole.Min.Z, FMath::Max(Whole.Max.Z, Whole.Min.Z + 10.0)) : FVector2D(0.0, 120.0);
	auto Dirt = [&](UStaticMeshComponent* C, FName Which, float Grime)
	{
		UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!M || !C->GetOwner()) return;
		FTransform Frame = C->GetOwner()->GetActorTransform();
		Frame.SetScale3D(FVector::OneVector);
		SetDirtFrame(M, C->GetComponentTransform().GetRelativeTransform(Frame), ZRange);
		M->SetScalarParameterValue(TEXT("FlyGain"), FlyGain(Which));
		if (!bWashing || M->K2_GetScalarParameterValue(TEXT("HasMask")) < 0.5f) M->SetScalarParameterValue(TEXT("Grime"), Grime);
	};
	for (UStaticMeshComponent* C : Parts)
	{
		if (!C) continue;
		const FName Part = PartTag(C);
		if (Part.IsNone()) continue;
		const FName Slot = SlotTag(C);
		const FName Which = Slot.IsNone() ? Part : Slot;
		const float Grime = B ? B->DirtOf(Bike, Which) : 0.0f;
		// Sprayed with a pearl, candy, flake or chameleon from the mixing bay, or masked: the effect
		// paint (TorqueEffectPaint.h) in place of the part's own material; any other paint, its own.
		TorquePaintMix::FPaintLook Effect;
		const bool bBare = C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag));
		if (bBare && !HasPaint.Contains(Which) && B && C->GetOwner())
		{
			// Sprayed all over: its metal's material has no colour, so it's drawn in the paint material.
			FLinearColor Shown;
			float Metallic = 0.05f, Roughness = 0.15f;
			if (B->FinishOf(Bike, Which, Shown, Metallic, Roughness))
			{
				TorquePaintMix::FPaintLook Plain;
				if (!B->EffectOf(Bike, Which, Plain))
				{
					Plain.Spec.Colour = Shown;
					Plain.Spec.Metal = Metallic;
					Plain.Spec.Rough = Roughness;
				}
				const FBox Box = Boxes.FindRef(Which);
				const FTransform ToPart = TorqueEffectPaint::PartFrame(C, C->GetOwner()->GetActorTransform(), Box.IsValid ? Box.GetCenter() : FVector::ZeroVector);
				if (TorqueEffectPaint::Apply(C, Plain, Grime, ToPart))
				{
					Dirt(C, Which, Grime);
					continue;
				}
			}
		}
		if (B && C->GetOwner() && !bBare && B->EffectOf(Bike, Which, Effect))
		{
			const FBox Box = Boxes.FindRef(Which);
			const FTransform ToPart = TorqueEffectPaint::PartFrame(C, C->GetOwner()->GetActorTransform(), Box.IsValid ? Box.GetCenter() : FVector::ZeroVector);
			if (TorqueEffectPaint::Apply(C, Effect, Grime, ToPart))
			{
				Dirt(C, Which, Grime);
				continue;
			}
		}
		TorqueEffectPaint::Restore(C);
		UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0);
		if (!M) continue;
		// The bike's masters draw their road dirt by Grime (mud low down, dust on top, flies up front:
		// Tools/ue_bike_dirt.py), and their rust bloom by Rust.
		Dirt(C, Which, Grime);
		M->SetScalarParameterValue(TEXT("Rust"), B ? B->RustOf(Bike, Which) : 0.0f);
		FLinearColor Paint;
		if (!C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag)) && B && B->PaintOf(Bike, Which, Paint))
		{
			M->SetVectorParameterValue(TEXT("Color"), Paint);
			M->SetVectorParameterValue(TEXT("Color2"), Paint);
			// Repainted: its finish too (gloss, matte, metallic, chrome, patina, primer, a satin clear).
			FLinearColor Shown;
			float Metallic = 0.0f, Roughness = 0.5f;
			if (B->FinishOf(Bike, Which, Shown, Metallic, Roughness))
			{
				for (const TCHAR* Param : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(Param, Metallic);
				for (const TCHAR* Param : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(Param, Roughness);
			}
		}
	}
	// The tank's badge, if it has one (stamped again only when it's changed).
	TorqueBikeBadge::Stamp(TorqueBikeBadge::TankOf(Parts), B ? B->BadgeOf(Bike) : FString());
}

int32 MakeSpare(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName Slot, FName Part,
	const FVector& WorldBottom, double MaxCm, TArray<UStaticMeshComponent*>& Out, USceneComponent*& OutHolder)
{
	FTorqueBuild Trial = Build;
	Trial.Fitted.Add(Slot, Part);
	USceneComponent* Holder = NewObject<USceneComponent>(Owner);
	Holder->SetupAttachment(Parent);
	Holder->RegisterComponent();
	OutHolder = nullptr;
	TArray<UStaticMeshComponent*> Made;
	MakeParts(Owner, Holder, Trial, Slot, Made);
	if (Made.Num() == 0)   // nothing to show (no model yet, or an option that takes parts off)
	{
		Holder->DestroyComponent();
		return 0;
	}
	OutHolder = Holder;
	auto BoxOf = [&Made]()
	{
		FBox Box(ForceInit);
		for (UStaticMeshComponent* C : Made)
		{
			C->UpdateBounds();
			Box += C->Bounds.GetBox();
		}
		return Box;
	};
	FBox Box = BoxOf();
	if (MaxCm > 0.0)
	{
		const double Big = Box.GetSize().GetMax();
		if (Big > MaxCm)
		{
			Holder->SetRelativeScale3D(FVector(MaxCm / Big));
			Box = BoxOf();
		}
	}
	const FVector Centre = Box.GetCenter();
	Holder->SetWorldLocation(Holder->GetComponentLocation() + FVector(WorldBottom.X - Centre.X, WorldBottom.Y - Centre.Y, WorldBottom.Z + 1.0 - Box.Min.Z));
	for (UStaticMeshComponent* C : Made)
	{
		C->ComponentTags.Add(FName(*FString::Printf(TEXT("Option:%s"), *Part.ToString())));
		Out.Add(C);
	}
	return Made.Num();
}

FString SlotLabel(FName Slot)
{
	FString S = Slot.ToString().Replace(TEXT("_"), TEXT(" "));
	if (S.Len() > 0) S[0] = FChar::ToUpper(S[0]);
	return S;
}

int32 MakeParts(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName OnlySlot,
	TArray<UStaticMeshComponent*>& Out)
{
	return MakePartsWhere(Owner, Parent, Build, [OnlySlot](const FTorqueResolvedPart& P) { return OnlySlot.IsNone() || P.Slot == OnlySlot; }, Out);
}

int32 MakePartsWhere(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build,
	TFunctionRef<bool(const FTorqueResolvedPart&)> Keep, TArray<UStaticMeshComponent*>& Out)
{
	FTransform Fix;
	TMap<FName, FVector> Origins;
	if (!LoadFrame(Fix, Origins)) return 0;
	TArray<FTorqueResolvedPart> Resolved;
	TorqueFit::ResolveModel(Build, Resolved);
	int32 Made = 0;
	for (const FTorqueResolvedPart& P : Resolved)
	{
		if (!Keep(P)) continue;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *P.Mesh);
		if (!Mesh) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetStaticMesh(Mesh);
		// Picked by the walker's own look test against each part's box (TorqueWalker), not by
		// collision: the bike's meshes are imported without any.
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetupAttachment(Parent);
		// Parts of the moving groups are modelled about the group's origin (the axles, the swingarm
		// pivot); standing still, each group sits at its origin.
		const FVector* At = Origins.Find(P.Group);
		const FTransform Group(At ? *At : FVector::ZeroVector);
		C->SetRelativeTransform(Fix * P.Delta * Group);
		if (!P.Material.IsEmpty())
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *P.Material)) C->SetMaterial(0, M);
		const FName Slot = TorqueShedParts::SlotOfModelPart(P);
		if (!Slot.IsNone()) C->ComponentTags.Add(FName(*(FString(SlotPrefix) + Slot.ToString())));
		C->ComponentTags.Add(FName(*(TEXT("Part:") + P.Name.ToString())));
		C->RegisterComponent();
		Out.Add(C);
		++Made;
	}
	return Made;
}
} // namespace TorqueShedModel

ATorqueShedBike::ATorqueShedBike()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	BikeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BikeRoot"));
	BikeRoot->SetupAttachment(Root);
	// The model's origin is at axle height (30 cm front, 31 cm rear tyre) in the middle of the
	// 1413 mm wheelbase. The stand lifts the rear tyre about 4 cm, so the bike pitches nose down
	// about 2 degrees about the front tyre's contact patch.
	BikeRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, 32.5), FRotator(-2.0, 0.0, 0.0));

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(Root);
	Blocker->SetBoxExtent(FVector(110.0, 45.0, 60.0));
	Blocker->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ShapeCube = Cube.Object;
	ShapeCylinder = Cylinder.Object;
	Tags.Add(TEXT("ShedBike"));
}

void ATorqueShedBike::BeginPlay()
{
	Super::BeginPlay();
	BuildStand();
	Rebuild();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		Watch = B->OnChanged.AddUObject(this, &ATorqueShedBike::BikeChanged);
		WatchLooks = B->OnLooks.AddUObject(this, &ATorqueShedBike::LooksChanged);
		// Back from a ride: the bike comes in dirtier (the host's to do; clients hear it from the host).
		B->Arrived(Bay);
	}
}

void ATorqueShedBike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Follow(DeltaSeconds);
}

void ATorqueShedBike::Follow(float Dt)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	{
		// [yard] Being ridden in the yard (Shed/TorqueYard.h): it's out there, so not in its bay too.
		bool bRidden = false;
		for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It && !bRidden; ++It)
			bRidden = It->InYard() && It->GetRideBay() == Bay;
		if (bRidden != IsHidden())
		{
			SetActorHiddenInGame(bRidden);
			SetActorEnableCollision(!bRidden);
		}
	}
	const bool bAway = B->IsAway(Bay);
	if (bAway != bShownAway)
	{
		// Off the paddock stand (hidden while it is away): level on its wheels, axle height 30.5 cm.
		bShownAway = bAway;
		for (UStaticMeshComponent* C : Stand)
			if (C) C->SetVisibility(!bAway);
		BikeRoot->SetRelativeLocationAndRotation(bAway ? FVector(0.0, 0.0, 30.5) : FVector(0.0, 0.0, 32.5), FRotator(bAway ? 0.0 : -2.0, 0.0, 0.0));
	}
	// Being wheeled: it mustn't shove the one wheeling it about.
	const bool bPushed = B->Exists(Bay) && B->Bike(Bay).Pusher != 0;
	if (bPushed != (Blocker->GetCollisionEnabled() == ECollisionEnabled::NoCollision))
		Blocker->SetCollisionEnabled(bPushed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	if (!bPushed) bPreview = false;
	FTransform Want = bPreview ? PreviewXf : B->BikeTransform(Bay);
	// Standing on a bike lift (in its bay, or the wash bay's): up with the platform
	// (workshop_upgrades.gd _ride_lifts; not while it's being wheeled).
	if (!bPreview && !bPushed) Want.AddToTranslation(FVector(0.0, 0.0, ATorqueBikeLift::HeightAt(this, Want.GetLocation())));
	const FVector At = GetActorLocation();
	const double Gap = FVector::Dist(At, Want.GetLocation());
	if (Gap < 0.05 && GetActorRotation().Equals(Want.Rotator(), 0.05)) return;
	// The walker's own preview is followed at once; the shed's word (10 a second over the net) glides.
	if (bPreview || Gap > 400.0) SetActorLocationAndRotation(Want.GetLocation(), Want.GetRotation());
	else
	{
		const float K = FMath::Clamp(Dt * 12.0f, 0.0f, 1.0f);
		FVector To = FMath::Lerp(At, Want.GetLocation(), (double)K);
		// Going up or down with a lift: with the platform, not lagging behind it.
		if (FMath::Abs(At.Z - Want.GetLocation().Z) < 5.0) To.Z = Want.GetLocation().Z;
		SetActorLocationAndRotation(To, FQuat::Slerp(GetActorQuat(), Want.GetRotation(), K));
	}
}

void ATorqueShedBike::Relook()
{
	TorqueShedModel::ApplyLooks(this, Bay, Parts);
}

void ATorqueShedBike::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		B->OnChanged.Remove(Watch);
		B->OnLooks.Remove(WatchLooks);
	}
	Super::EndPlay(Reason);
}

FName ATorqueShedBike::SlotOf(const UPrimitiveComponent* C) const
{
	return TorqueShedModel::SlotTag(C);
}

void ATorqueShedBike::Preview(const FString* Geo)
{
	if (Geo) PreviewGeo = *Geo;
	else if (!PreviewGeo.IsSet()) return;   // nothing previewed: as drawn already
	else PreviewGeo.Reset();
	Rebuild();
}

FString ATorqueShedBike::FitText() const
{
	if (PreviewGeo.IsSet()) return PreviewGeo.GetValue();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	return B && B->Exists(Bay) ? B->Bike(Bay).Geo : FString();
}

void ATorqueShedBike::Rebuild()
{
	for (UStaticMeshComponent* C : Parts)
		if (C) C->DestroyComponent();
	Parts.Reset();
	FitView = FTorqueBikeFitView();
	const FString Geo = FitText();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (B && !B->Exists(Bay))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: bay %d is empty"), Bay + 1);   // just the stand
		return;
	}
	// A bike of catalogue parts (any style): laid out and drawn from the Blender parts, its origin at
	// its own rear axle's height (the stand lifts it a little more).
	TMap<FString, FString> CatalogueParts;
	if (B && B->LayoutParts(Bay, CatalogueParts))
	{
		double AxleHeight = 31.0;
		const int32 Drawn = TorqueShedCatalogue::MakeBike(this, BikeRoot, CatalogueParts, Parts, AxleHeight, Geo);
		if (Drawn > 0)
		{
			BikeRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, AxleHeight + 1.5), FRotator(-2.0, 0.0, 0.0));
			FitView.Capture(Parts);
			FitView.Show(Parts, Geo);
			Relook();
			UE_LOG(LogTemp, Log, TEXT("Torque: bay %d: %s drawn from the catalogue with %d surfaces"), Bay + 1, *B->Bike(Bay).Name, Drawn);
			return;
		}
	}
	BikeRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, 32.5), FRotator(-2.0, 0.0, 0.0));
	const FTorqueBuild& Build = B ? B->Get(Bay) : UTorqueBuildSubsystem::BuildFor(this);
	const int32 InBay = Bay;
	const int32 Made = TorqueShedModel::MakePartsWhere(this, BikeRoot, Build,
		[B, InBay](const FTorqueResolvedPart& P) { return !B || B->ShowsModelPart(InBay, P); }, Parts);
	if (Made == 0) BuildGreyBox();
	FitView.Capture(Parts);
	FitView.Show(Parts, Geo);
	Relook();
	UE_LOG(LogTemp, Log, TEXT("Torque: bay %d: %s drawn with %d parts"), Bay + 1, *Build.Bike.ToString(), Parts.Num());
}

void ATorqueShedBike::BuildGreyBox()
{
	// No imported model: blocks the size of the Speed Twin, tagged with their slots so the walker
	// can still work on them; a slot with nothing in it isn't drawn.
	struct FBlock { const TCHAR* Part; const TCHAR* Slot; bool bCylinder; FVector At, Size; FRotator Rot; };
	const FBlock Blocks[] = {
		{TEXT("front_tyre"), TEXT("front_tyre"), true, FVector(70.6, 0, 0), FVector(60, 12, 60), FRotator(0, 0, 90)},
		{TEXT("rear_tyre"), TEXT("rear_tyre"), true, FVector(-70.6, 0, 0), FVector(62, 16, 62), FRotator(0, 0, 90)},
		{TEXT("crankcase"), TEXT("engine"), false, FVector(0, 0, 10), FVector(45, 40, 40), FRotator::ZeroRotator},
		{TEXT("tank"), TEXT("tank"), false, FVector(10, 0, 52), FVector(55, 32, 22), FRotator::ZeroRotator},
		{TEXT("seat"), TEXT("saddle"), false, FVector(-38, 0, 52), FVector(55, 26, 10), FRotator::ZeroRotator},
		{TEXT("silencers"), TEXT("exhaust"), true, FVector(-55, 20, 5), FVector(10, 10, 60), FRotator(-70, 0, 0)},
		{TEXT("handlebar"), TEXT("handlebars"), true, FVector(40, 0, 66), FVector(3, 3, 76), FRotator(0, 0, 90)},
	};
	const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
	for (const FBlock& B : Blocks)
	{
		if (Sub && B.Slot && Sub->ItemOn(Bay, FName(B.Slot)) == 0) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(B.bCylinder ? ShapeCylinder : ShapeCube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(BikeRoot);
		C->SetRelativeLocationAndRotation(B.At, B.Rot);
		C->SetRelativeScale3D(B.Size / 100.0);
		if (B.Slot) C->ComponentTags.Add(FName(*(FString(TEXT("Slot:")) + B.Slot)));
		C->ComponentTags.Add(FName(*(FString(TEXT("Part:")) + B.Part)));
		C->RegisterComponent();
		Parts.Add(C);
	}
}

void ATorqueShedBike::BuildStand()
{
	// A rear paddock stand: two uprights either side of the tyre with bobbin cups, a cross tube at
	// the floor and a handle reaching back. Grey-box steel.
	struct FTube { FVector At, Size; FRotator Rot; };
	const double X = -66.0;   // just ahead of the rear axle, under the swingarm bobbins
	const FTube Tubes[] = {
		{FVector(X, -17, 14), FVector(3, 3, 28), FRotator::ZeroRotator},
		{FVector(X, 17, 14), FVector(3, 3, 28), FRotator::ZeroRotator},
		{FVector(X, 0, 2), FVector(3, 3, 40), FRotator(0, 0, 90)},
		{FVector(X - 35, 0, 18), FVector(3, 3, 80), FRotator(60, 0, 0)},
		{FVector(X, -17, 30), FVector(5, 5, 4), FRotator::ZeroRotator},
		{FVector(X, 17, 30), FVector(5, 5, 4), FRotator::ZeroRotator},
		{FVector(X, -17, 2), FVector(6, 6, 6), FRotator(0, 0, 90)},   // wheels
		{FVector(X, 17, 2), FVector(6, 6, 6), FRotator(0, 0, 90)},
	};
	for (const FTube& T : Tubes)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(ShapeCylinder);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Root);
		C->SetRelativeLocationAndRotation(T.At, T.Rot);
		C->SetRelativeScale3D(T.Size / 100.0);
		C->RegisterComponent();
		Stand.Add(C);
	}
}
