#include "Shed/TorquePaintBayRoom.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Shed/TorqueToolModels.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
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
// The light booth (paint_bay_room.gd BOOTH, BOOTH_Y, BOOTH_LIGHTS): three bays across the daylight
// canopy's corner, x 1460 .. 1680, their backs on the north wall (y -518), open to the room at -458,
// 92 to 190 cm up over a cabinet.
constexpr double BoothX0 = 1460.0, BoothX1 = 1680.0, BoothBack = -518.0, BoothFront = -458.0, BoothLow = 92.0, BoothHigh = 190.0;
struct FBoothLight { const TCHAR* Name; FLinearColor Colour; float Lumens; };
const FBoothLight BoothLamps[] = {
	{TEXT("DAYLIGHT"), FLinearColor(0.97f, 0.98f, 1.0f), 520.0f},
	{TEXT("SHOP LIGHT"), FLinearColor(0.86f, 1.0f, 0.9f), 430.0f},
	{TEXT("SUNSET"), FLinearColor(1.0f, 0.55f, 0.28f), 560.0f},
};
const FLinearColor Neutral(0.42f, 0.42f, 0.43f);   // a viewing booth's mid grey
const FLinearColor StickWood(0.72f, 0.58f, 0.4f);
const FLinearColor RackSteel(0.6f, 0.61f, 0.63f);

// How a bottle on the racks looks: the metallic tints and the flakes shine, a pearl has a soft
// sheen, a candy dye's glossy.
void BottleLook(const TorquePaintMix::FTint& T, float& OutMetal, float& OutRough)
{
	using TorquePaintMix::ETintKind;
	OutMetal = 0.0f;
	OutRough = 0.35f;
	switch (T.Kind)
	{
	case ETintKind::Alu:
	case ETintKind::Sparkle:
	case ETintKind::Flake:     OutMetal = 0.8f; OutRough = 0.3f; break;
	case ETintKind::Pearl:     OutMetal = 0.45f; OutRough = 0.22f; break;
	case ETintKind::Chameleon: OutMetal = 0.6f; OutRough = 0.18f; break;
	case ETintKind::Candy:     OutRough = 0.08f; break;
	default: break;
	}
}
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
		float BottleMetal = 0.0f, BottleRough = 0.35f;
		TPBRI::BottleLook(TPM::Tints()[I], BottleMetal, BottleRough);
		UStaticMeshComponent* Body = Piece(Root, CylinderMesh, Foot + FVector(0.0, 0.0, TPBRI::BottleHeight * 0.5),
			FVector(TPBRI::BottleRadius * 2.0, TPBRI::BottleRadius * 2.0, TPBRI::BottleHeight), FRotator::ZeroRotator, TPM::Tints()[I].Colour,
			BottleMetal, BottleRough);
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
	// The stirring stick: leaning in the tin on the scale, going round while it's stirred.
	Stick = NewObject<USceneComponent>(this);
	Stick->SetupAttachment(Root);
	Stick->RegisterComponent();
	Stick->SetRelativeLocation(TorquePaintBayLayout::ScaleAt + FVector(0.0, 0.0, TPBRI::PanHeight + 1.0));
	Piece(Stick, CubeMesh, FVector(2.2, 0.0, 13.0), FVector(0.6, 2.0, 28.0), FRotator(-14.0, 0.0, 0.0), TPBRI::StickWood, 0.0f, 0.7f);
	Stick->SetVisibility(false, true);
	BuildBooth(Root);
	BuildEffectsRack(Root);
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

void ATorquePaintBayRoom::BuildBooth(USceneComponent* Root)
{
	using namespace TorquePaintBayRoomImpl;
	const double CX = (BoothX0 + BoothX1) * 0.5, CY = (BoothBack + BoothFront) * 0.5, W = BoothX1 - BoothX0, D = BoothFront - BoothBack;
	const double MidZ = (BoothLow + BoothHigh) * 0.5, H = BoothHigh - BoothLow;
	// The cabinet under it (solid: it's walked up to), the bays' floor, back, top and dividers.
	if (UStaticMeshComponent* Cab = Piece(Root, CubeMesh, FVector(CX, CY, BoothLow * 0.5), FVector(W, D, BoothLow), FRotator::ZeroRotator,
		FLinearColor(0.2f, 0.21f, 0.23f), 0.2f, 0.5f))
	{
		Cab->SetCollisionProfileName(TEXT("BlockAll"));
		Cab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	Piece(Root, CubeMesh, FVector(CX, CY, BoothLow + 1.0), FVector(W, D, 2.0), FRotator::ZeroRotator, Neutral, 0.0f, 0.85f);
	Piece(Root, CubeMesh, FVector(CX, BoothBack + 1.0, MidZ), FVector(W, 2.0, H), FRotator::ZeroRotator, Neutral, 0.0f, 0.85f);
	Piece(Root, CubeMesh, FVector(CX, CY, BoothHigh + 1.0), FVector(W, D, 2.0), FRotator::ZeroRotator, Neutral, 0.0f, 0.85f);
	for (int32 I = 0; I <= 3; ++I)
		Piece(Root, CubeMesh, FVector(BoothX0 + I * W / 3.0, CY, MidZ), FVector(2.0, D, H), FRotator::ZeroRotator, Neutral, 0.0f, 0.85f);
	for (int32 I = 0; I < 3; ++I)
	{
		const FBoothLight& Spec = BoothLamps[I];
		const double BX = BoothX0 + (I + 0.5) * W / 3.0;
		// The lamp under the top's front edge, glowing its colour, and its light on the back.
		if (UStaticMeshComponent* Lamp = Piece(Root, CubeMesh, FVector(BX, BoothFront - 8.0, BoothHigh - 1.0), FVector(W / 3.0 * 0.6, 4.0, 1.2),
			FRotator::ZeroRotator, Spec.Colour, 0.0f, 0.4f))
			Lamp->SetMaterial(0, TorqueToolModels::Finish(this, Spec.Colour, 0.0f, 0.4f, 3.0f));
		USpotLightComponent* L = NewObject<USpotLightComponent>(this);
		L->SetupAttachment(Root);
		const FVector From(BX, BoothFront - 10.0, BoothHigh - 3.0), To(BX, BoothBack + 4.0, MidZ);
		L->SetRelativeLocationAndRotation(From, (To - From).Rotation());
		L->SetIntensityUnits(ELightUnits::Lumens);
		L->SetIntensity(Spec.Lumens);
		L->SetLightColor(Spec.Colour);
		L->SetAttenuationRadius(140.0f);
		L->SetInnerConeAngle(18.0f);
		L->SetOuterConeAngle(42.0f);
		L->SetCastShadows(false);
		L->RegisterComponent();
		BoothLights.Add(L);
		// The test card, clipped to the back of the bay, facing the room.
		UStaticMeshComponent* Card = Piece(Root, CubeMesh, FVector(BX, BoothBack + 4.0, MidZ), FVector(1.0, 22.0, 32.0), FRotator(0.0, 90.0, 0.0),
			FLinearColor::White, 0.0f, 0.3f);
		if (Card) Card->SetVisibility(false);
		BoothCards.Add(Card);
		// Its name over it.
		UTextRenderComponent* Name = NewObject<UTextRenderComponent>(this);
		Name->SetupAttachment(Root);
		Name->RegisterComponent();
		Name->SetRelativeLocationAndRotation(FVector(BX, BoothFront + 0.5, BoothHigh + 6.0), FRotator(0.0, 90.0, 0.0));
		Name->SetHorizontalAlignment(EHTA_Center);
		Name->SetVerticalAlignment(EVRTA_TextCenter);
		Name->SetWorldSize(5.0f);
		Name->SetTextRenderColor(FColor(235, 235, 240));
		Name->SetCastShadow(false);
		Name->SetText(FText::FromString(Spec.Name));
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: mixing bay light booth: %d bays"), BoothCards.Num());
}

void ATorquePaintBayRoom::BuildEffectsRack(USceneComponent* Root)
{
	// Torque Shed's effects rack (paint_bay.gd EFFECTS_RACK): two steel shelves of six over the
	// shaker, beside the tints, each with a lip, ends either side and its name over it.
	namespace TPBL = TorquePaintBayLayout;
	using TPBRI::RackSteel;
	const double X0 = 1674.0, X1 = 1700.0, Y0 = TPBL::EffectsY0, Y1 = TPBL::EffectsY1;
	const double CX = (X0 + X1) * 0.5, CY = (Y0 + Y1) * 0.5, W = Y1 - Y0;
	for (const double Z : {TPBL::EffectsLow, TPBL::EffectsHigh})
	{
		Piece(Root, CubeMesh, FVector(CX, CY, Z - 1.5), FVector(X1 - X0, W, 3.0), FRotator::ZeroRotator, RackSteel, 0.85f, 0.35f);
		Piece(Root, CubeMesh, FVector(X0 - 0.5, CY, Z), FVector(1.0, W, 5.5), FRotator::ZeroRotator, RackSteel, 0.85f, 0.35f);
	}
	for (const double Y : {Y0, Y1})
		Piece(Root, CubeMesh, FVector(CX, Y, (TPBL::EffectsLow - 6.0 + TPBL::EffectsHigh + 24.0) * 0.5),
			FVector(X1 - X0, 2.0, TPBL::EffectsHigh + 30.0 - TPBL::EffectsLow), FRotator::ZeroRotator, RackSteel, 0.85f, 0.35f);
	UTextRenderComponent* Name = NewObject<UTextRenderComponent>(this);
	Name->SetupAttachment(Root);
	Name->RegisterComponent();
	Name->SetRelativeLocationAndRotation(FVector(1698.0, CY, TPBL::EffectsHigh + 33.0), FRotator(0.0, 180.0, 0.0));
	Name->SetHorizontalAlignment(EHTA_Center);
	Name->SetVerticalAlignment(EVRTA_TextCenter);
	Name->SetWorldSize(8.0f);
	Name->SetTextRenderColor(FColor(30, 30, 36));
	Name->SetCastShadow(false);
	Name->SetText(FText::FromString(TEXT("EFFECTS")));
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
	// The stirring stick in the tin on the scale: round while it's being stirred (the tin's stir goes up).
	if (Stick)
	{
		const FTorqueTin* OnScale = P.BenchTinPtr();
		Stick->SetVisibility(OnScale != nullptr, true);
		if (OnScale)
		{
			if (OnScale->Id == LastStirTin && OnScale->Mix.Stir > LastStir + 0.0001f) StirringFor = 0.35;
			LastStirTin = OnScale->Id;
			LastStir = OnScale->Mix.Stir;
			StirringFor = FMath::Max(StirringFor - DeltaSeconds, 0.0);
			if (StirringFor > 0.0) StickAngle = FMath::Fmod(StickAngle + 540.0 * DeltaSeconds, 360.0);
			Stick->SetRelativeRotation(FRotator(0.0, StickAngle, 0.0));
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
	for (UStaticMeshComponent* Card : BoothCards)
	{
		if (!Card) continue;
		Card->SetVisibility(P.bTest);
		if (P.bTest) Tint(Card, P.Test.Colour, P.Test.Metal, P.Test.Rough);
	}
}
