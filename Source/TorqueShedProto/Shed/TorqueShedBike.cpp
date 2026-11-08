#include "Shed/TorqueShedBike.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
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
} // namespace

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

FString SlotLabel(FName Slot)
{
	FString S = Slot.ToString().Replace(TEXT("_"), TEXT(" "));
	if (S.Len() > 0) S[0] = FChar::ToUpper(S[0]);
	return S;
}

int32 MakeParts(AActor* Owner, USceneComponent* Parent, const FTorqueBuild& Build, FName OnlySlot,
	TArray<UStaticMeshComponent*>& Out)
{
	FTransform Fix;
	TMap<FName, FVector> Origins;
	if (!LoadFrame(Fix, Origins)) return 0;
	TArray<FTorqueResolvedPart> Resolved;
	TorqueFit::ResolveModel(Build, Resolved);
	int32 Made = 0;
	for (const FTorqueResolvedPart& P : Resolved)
	{
		if (!OnlySlot.IsNone() && P.Slot != OnlySlot) continue;
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
		if (!P.Slot.IsNone()) C->ComponentTags.Add(FName(*(FString(SlotPrefix) + P.Slot.ToString())));
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
	PrimaryActorTick.bCanEverTick = false;
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
		Watch = B->OnChanged.AddUObject(this, &ATorqueShedBike::Rebuild);
}

void ATorqueShedBike::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnChanged.Remove(Watch);
	Super::EndPlay(Reason);
}

FName ATorqueShedBike::SlotOf(const UPrimitiveComponent* C) const
{
	return TorqueShedModel::SlotTag(C);
}

void ATorqueShedBike::Rebuild()
{
	for (UStaticMeshComponent* C : Parts)
		if (C) C->DestroyComponent();
	Parts.Reset();
	const int32 Made = TorqueShedModel::MakeParts(this, BikeRoot, UTorqueBuildSubsystem::BuildFor(this), NAME_None, Parts);
	if (Made == 0) BuildGreyBox();
	UE_LOG(LogTemp, Log, TEXT("Torque: shed bike drawn with %d parts"), Parts.Num());
}

void ATorqueShedBike::BuildGreyBox()
{
	// No imported model: blocks the size of the Speed Twin, with the two swappable slots tagged so
	// the walker can still try a swap (it changes the build; these shapes don't change).
	struct FBlock { const TCHAR* Part; const TCHAR* Slot; bool bCylinder; FVector At, Size; FRotator Rot; };
	const FBlock Blocks[] = {
		{TEXT("front_tyre"), nullptr, true, FVector(70.6, 0, 0), FVector(60, 12, 60), FRotator(0, 0, 90)},
		{TEXT("rear_tyre"), nullptr, true, FVector(-70.6, 0, 0), FVector(62, 16, 62), FRotator(0, 0, 90)},
		{TEXT("engine"), nullptr, false, FVector(0, 0, 10), FVector(45, 40, 40), FRotator::ZeroRotator},
		{TEXT("tank"), nullptr, false, FVector(10, 0, 52), FVector(55, 32, 22), FRotator::ZeroRotator},
		{TEXT("seat"), nullptr, false, FVector(-38, 0, 52), FVector(55, 26, 10), FRotator::ZeroRotator},
		{TEXT("silencers"), TEXT("exhaust"), true, FVector(-55, 20, 5), FVector(10, 10, 60), FRotator(-70, 0, 0)},
		{TEXT("handlebar"), TEXT("handlebars"), true, FVector(40, 0, 66), FVector(3, 3, 76), FRotator(0, 0, 90)},
	};
	for (const FBlock& B : Blocks)
	{
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
