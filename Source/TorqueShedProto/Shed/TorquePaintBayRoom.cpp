#include "Shed/TorquePaintBayRoom.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Shed/TorqueToolModels.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace TorquePaintBayRoomImpl
{
// paint_bay_room.gd's sizes, in cm: a litre tin, a tint bottle, a swatch card, the test card.
constexpr double TinRadius = 5.5, TinHeight = 12.0;
constexpr double BottleRadius = 3.0, BottleHeight = 16.0;
const FVector SwatchSize(1.0, 62.0, 38.0);
const FVector CardSize(1.0, 30.0, 40.0);
constexpr double PanHeight = 3.0;     // the scale's pan over the bench top
const FLinearColor TinSteel(0.62f, 0.63f, 0.65f);
}

namespace TPBRI = TorquePaintBayRoomImpl;

ATorquePaintBayRoom::ATorquePaintBayRoom()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
}

UStaticMeshComponent* ATorquePaintBayRoom::Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm,
	const FRotator& Rot, const FLinearColor& Colour, float Metallic, float Roughness)
{
	if (!Mesh || !Parent) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Parent);
	C->SetRelativeLocationAndRotation(At, Rot);
	C->SetRelativeScale3D(SizeCm / 100.0);   // the basic shapes are a metre across
	C->RegisterComponent();
	return C;
}

void ATorquePaintBayRoom::Tint(UStaticMeshComponent* C, const FLinearColor& Colour, float Metallic, float Roughness)
{
	UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr;
	if (!M) return;
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	M->SetVectorParameterValue(TEXT("Color2"), Colour);
	for (const TCHAR* P : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(P, Metallic);
	for (const TCHAR* P : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(P, Roughness);
}

void ATorquePaintBayRoom::BeginPlay()
{
	Super::BeginPlay();
	namespace TPM = TorquePaintMix;
	USceneComponent* Root = GetRootComponent();
	// The rack's bottles, each its tint, a black cap on top.
	for (int32 I = 0; I < TPM::Tints().Num(); ++I)
	{
		const FVector Foot = TorquePaintBayLayout::RackAt(I);
		UStaticMeshComponent* Body = Piece(Root, CylinderMesh, Foot + FVector(0.0, 0.0, TPBRI::BottleHeight * 0.5),
			FVector(TPBRI::BottleRadius * 2.0, TPBRI::BottleRadius * 2.0, TPBRI::BottleHeight), FRotator::ZeroRotator, TPM::Tints()[I].Colour,
			TPM::Tints()[I].Kind == TPM::ETintKind::Alu || TPM::Tints()[I].Kind == TPM::ETintKind::Sparkle ? 0.8f : 0.0f, 0.35f);
		if (Body)
			Piece(Body, CylinderMesh, FVector(0.0, 0.0, 56.0), FVector(55.0, 55.0, 12.0), FRotator::ZeroRotator, FLinearColor(0.03f, 0.03f, 0.03f), 0.0f, 0.5f);
		Bottles.Add(Body);
	}
	// The bottle being poured: tipped over the tin from its neck, a thin stream under it.
	PourBottle = NewObject<USceneComponent>(this);
	PourBottle->SetupAttachment(Root);
	PourBottle->RegisterComponent();
	PourBody = Piece(PourBottle, CylinderMesh, FVector(0.0, 0.0, -TPBRI::BottleHeight * 0.5),
		FVector(TPBRI::BottleRadius * 2.0, TPBRI::BottleRadius * 2.0, TPBRI::BottleHeight), FRotator::ZeroRotator, FLinearColor::White, 0.0f, 0.35f);
	PourStream = Piece(Root, CylinderMesh, FVector::ZeroVector, FVector(0.6, 0.6, 10.0), FRotator::ZeroRotator, FLinearColor::White, 0.0f, 0.1f);
	PourBottle->SetVisibility(false, true);
	if (PourStream) PourStream->SetVisibility(false);
	// A pool of tins: a steel can, and the paint in it just under the rim.
	for (int32 I = 0; I < FTorquePaintBay::MaxTins + 4; ++I)
	{
		USceneComponent* T = NewObject<USceneComponent>(this);
		T->SetupAttachment(Root);
		T->RegisterComponent();
		Piece(T, CylinderMesh, FVector(0.0, 0.0, TPBRI::TinHeight * 0.5), FVector(TPBRI::TinRadius * 2.0, TPBRI::TinRadius * 2.0, TPBRI::TinHeight),
			FRotator::ZeroRotator, TPBRI::TinSteel, 0.9f, 0.35f);
		TinPaint.Add(Piece(T, CylinderMesh, FVector(0.0, 0.0, TPBRI::TinHeight - 0.8), FVector(TPBRI::TinRadius * 1.9, TPBRI::TinRadius * 1.9, 0.4),
			FRotator::ZeroRotator, FLinearColor::White, 0.0f, 0.2f));
		T->SetVisibility(false, true);
		TinRoots.Add(T);
	}
	// The swatch wall's cards, facing into the room (+x), and the test card on the stand.
	for (int32 I = 0; I < FTorquePaintBay::MaxSwatches; ++I)
	{
		UStaticMeshComponent* S = Piece(Root, CubeMesh, TorquePaintBayLayout::SwatchAt(I), TPBRI::SwatchSize, FRotator::ZeroRotator, FLinearColor::White, 0.0f, 0.3f);
		if (S) S->SetVisibility(false);
		Swatches.Add(S);
	}
	TestCard = Piece(Root, CubeMesh, TorquePaintBayLayout::TestCardAt, TPBRI::CardSize, FRotator(0.0, 25.0, 0.0), FLinearColor::White, 0.0f, 0.3f);
	if (TestCard) TestCard->SetVisibility(false);
	// The scale's read-out on the front of the bench, facing whoever's at it.
	ScaleText = NewObject<UTextRenderComponent>(this);
	ScaleText->SetupAttachment(Root);
	ScaleText->RegisterComponent();
	// On the read-out on the bench's front edge (ue_build_shed.py MixBay_ScaleDisplay: x 1632, z 77 .. 92).
	ScaleText->SetRelativeLocationAndRotation(FVector(1631.5, -40.0, 84.5), FRotator(0.0, 180.0, 0.0));
	ScaleText->SetHorizontalAlignment(EHTA_Center);
	ScaleText->SetVerticalAlignment(EVRTA_TextCenter);
	ScaleText->SetWorldSize(9.0f);
	ScaleText->SetTextRenderColor(FColor(150, 255, 160));
	ScaleText->SetCastShadow(false);   // (the engine's text material is unlit: it glows like the LCD)
	ScaleText->SetText(FText::FromString(TEXT("0 g")));
}

FVector ATorquePaintBayRoom::HeldAt(int32 Peer, FRotator& OutRot) const
{
	// In a player's hands: in front of them at waist height, a little to the right.
	OutRot = FRotator::ZeroRotator;
	if (UWorld* World = GetWorld())
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const APlayerState* PS = It->GetPlayerState();
			if (!PS || PS->GetPlayerId() != Peer) continue;
			const FRotator Yaw(0.0, It->GetActorRotation().Yaw, 0.0);
			OutRot = Yaw;
			const FVector Feet = It->GetActorLocation() - FVector(0.0, 0.0, It->GetSimpleCollisionHalfHeight());
			return Feet + Yaw.Vector() * 35.0 + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * 18.0 + FVector(0.0, 0.0, 95.0);
		}
	return FVector(0.0, 0.0, -1000.0);
}

void ATorquePaintBayRoom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	namespace TPM = TorquePaintMix;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	const FTorquePaintBay& P = B->PaintBay();
	ShakeClock += DeltaSeconds;
	// The scale.
	if (ScaleText)
	{
		const FString Reading = P.BenchTinPtr() ? FString::Printf(TEXT("%s g%s"), *TPM::GramsText(P.ScaleGrams()), P.Tared() ? TEXT(" T") : TEXT(""))
			: FString(TEXT("0 g"));
		ScaleText->SetText(FText::FromString(Reading));
	}
	// The bottle being poured, over the tin, tipped as far as it is.
	const bool bPouring = P.PourTint != INDEX_NONE && P.Tilt > 0.0f && P.BenchTinPtr();
	for (int32 I = 0; I < Bottles.Num(); ++I)
		if (Bottles[I]) Bottles[I]->SetVisibility(!(bPouring && P.PourTint == I), true);
	if (PourBottle)
	{
		PourBottle->SetVisibility(bPouring, true);
		if (bPouring)
		{
			const FVector Neck = TorquePaintBayLayout::ScaleAt + FVector(-2.0, 4.0, TPBRI::PanHeight + TPBRI::TinHeight + 6.0);
			PourBottle->SetRelativeLocationAndRotation(Neck, FRotator(0.0, 0.0, 180.0 - 120.0 * P.Tilt));
			Tint(PourBody, TPM::Tints()[FMath::Clamp(P.PourTint, 0, TPM::Tints().Num() - 1)].Colour, 0.0f, 0.35f);
		}
	}
	if (PourStream)
	{
		const bool bFlow = bPouring && TPM::PourRate(P.Tilt) > 0.0f;
		PourStream->SetVisibility(bFlow);
		if (bFlow)
		{
			const double Drop = 6.0;
			const double Width = FMath::Clamp(0.3 + 0.05 * TPM::PourRate(P.Tilt), 0.3, 1.6);
			PourStream->SetRelativeLocation(TorquePaintBayLayout::ScaleAt + FVector(0.0, 0.0, TPBRI::PanHeight + TPBRI::TinHeight + Drop * 0.5));
			PourStream->SetRelativeScale3D(FVector(Width, Width, Drop) / 100.0);
			Tint(PourStream, TPM::Tints()[FMath::Clamp(P.PourTint, 0, TPM::Tints().Num() - 1)].Colour, 0.0f, 0.1f);
		}
	}
	// The tins, where each is (the shaker's jiggles), the paint at the top its colour.
	int32 Used = 0;
	for (const TPair<int32, FTorqueTin>& KV : P.Tins)
	{
		if (!TinRoots.IsValidIndex(Used)) break;
		const FTorqueTin& T = KV.Value;
		FVector At = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		switch (T.At)
		{
		case ETorqueTinAt::Bench: At = TorquePaintBayLayout::ScaleAt + FVector(0.0, 0.0, TPBRI::PanHeight); break;
		case ETorqueTinAt::Shelf: At = TorquePaintBayLayout::SlotAt(T.Ref); break;
		case ETorqueTinAt::Shaker:
			At = TorquePaintBayLayout::ShakerAt + FVector(0.0, 0.0, 8.0) + FVector(FMath::Sin(ShakeClock * 47.0), FMath::Cos(ShakeClock * 53.0), 0.0) * 0.6;
			break;
		case ETorqueTinAt::Held: At = HeldAt(T.Ref, Rot); break;
		}
		USceneComponent* Root = TinRoots[Used];
		Root->SetVisibility(true, true);
		Root->SetRelativeLocationAndRotation(At, Rot);
		const TPM::FPaintSpec S = T.Spec();
		const FString Sig = S.Code + FString::Printf(TEXT("%.2f"), S.Stir);
		if (TinSigs.Num() < TinRoots.Num()) TinSigs.SetNum(TinRoots.Num());
		if (TinPaint.IsValidIndex(Used) && TinPaint[Used] && TinSigs[Used] != Sig)
		{
			// An unstirred tin shows its base with streaks: the colour part way there.
			const TPM::FPaintSpec Even = TPM::Spec(T.Mix);
			const FLinearColor Base = TPM::Bases()[FMath::Clamp(T.Mix.Base, 0, TPM::Bases().Num() - 1)].Colour;
			Tint(TinPaint[Used], FMath::Lerp(Base, Even.Colour, 0.35f + 0.65f * T.Mix.Stir), S.Metal, S.Rough);
			TinSigs[Used] = Sig;
		}
		++Used;
	}
	for (int32 I = Used; I < TinRoots.Num(); ++I)
		if (TinRoots[I]) TinRoots[I]->SetVisibility(false, true);
	// The swatch wall and the test card: redrawn only when they change.
	FString Now = FString::Printf(TEXT("%d:%d:%s"), P.Recipes.Num(), P.TestN, P.bTest ? *P.Test.Code : TEXT("-"));
	if (P.Recipes.Num() > 0) Now += P.Recipes.Last().Code;
	if (Now == Shown) return;
	Shown = Now;
	const TArray<const FTorquePaintRecipe*> Cards = P.Swatches();
	for (int32 I = 0; I < Swatches.Num(); ++I)
	{
		if (!Swatches[I]) continue;
		const bool bOn = Cards.IsValidIndex(I);
		Swatches[I]->SetVisibility(bOn);
		if (bOn) Tint(Swatches[I], Cards[I]->Colour, Cards[I]->Effect == FName(TEXT("metallic")) ? 0.7f : 0.0f, 0.3f);
	}
	if (TestCard)
	{
		TestCard->SetVisibility(P.bTest);
		if (P.bTest) Tint(TestCard, P.Test.Colour, P.Test.Metal, P.Test.Rough);
	}
}
