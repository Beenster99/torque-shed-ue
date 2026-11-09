#include "Shed/TorqueProfilePreview.h"

#include "CanvasItem.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace TorqueProfilePreviewSpot
{
// High over the level's origin, in the open: nothing above to shade it, nothing near to film.
const FVector Where(0.0, 0.0, 150000.0);
} // namespace TorqueProfilePreviewSpot

ATorqueProfilePreview::ATorqueProfilePreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;   // the pause menu is where it's shown
	SetReplicates(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Feet = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	Feet->SetupAttachment(Root);
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Key = CreateDefaultSubobject<UPointLightComponent>(TEXT("Key"));
	Key->SetupAttachment(Root);
	Fill = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fill"));
	Fill->SetupAttachment(Root);
	for (UPointLightComponent* L : {Key, Fill})
	{
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetAttenuationRadius(900.0f);
		L->SetCastShadows(L == Key);
	}
	Key->SetIntensity(90.0f);
	Key->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));
	Fill->SetIntensity(30.0f);
	Fill->SetLightColor(FLinearColor(0.8f, 0.88f, 1.0f));
}

ATorqueProfilePreview* ATorqueProfilePreview::For(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || FTorqueAvatar::Count() == 0) return nullptr;
	UWorld* World = PC->GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<ATorqueProfilePreview> It(World); It; ++It)
		if (It->GetOwner() == PC) return *It;
	FActorSpawnParameters SP;
	SP.Owner = PC;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.ObjectFlags |= RF_Transient;
	return World->SpawnActor<ATorqueProfilePreview>(TorqueProfilePreviewSpot::Where, FRotator::ZeroRotator, SP);
}

void ATorqueProfilePreview::EndPlay(const EEndPlayReason::Type Reason)
{
	Avatar.Destroy();
	Super::EndPlay(Reason);
}

void ATorqueProfilePreview::Want(bool bFace, int32 Colour)
{
	bFaceView = bFace;
	WantFor = 0.25;
	// Built (again) for the avatar the profile picks, then given its height and face.
	const FTorqueProfile& Mine = FTorqueProfile::Mine();
	const int32 Index = Mine.AvatarFor(Colour);
	if (Index != BuiltFor)
	{
		BuiltFor = Index;
		LookOn.Reset();
		Avatar.Build(this, Feet, Index);
		Capture->ShowOnlyActors.Reset();
		Capture->ShowOnlyActors.Add(this);
	}
	const TArray<uint8> Now = Mine.Pack();
	if (Avatar.IsValid() && Now != LookOn)
	{
		LookOn = Now;
		Avatar.SetLook(Mine);
	}
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
		Target->ClearColor = FLinearColor(0.02f, 0.02f, 0.025f, 1.0f);
		Target->InitAutoFormat(768, 1024);
		Target->UpdateResourceImmediate(true);
		Capture->TextureTarget = Target;
	}
}

void ATorqueProfilePreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	WantFor -= DeltaSeconds;
	const bool bOn = WantFor > 0.0 && Avatar.IsValid();
	Capture->bCaptureEveryFrame = bOn;
	SetActorHiddenInGame(!bOn);
	if (!bOn) return;

	// Standing at ease, turning slowly one way and back to show the face from the side.
	Sway += DeltaSeconds * 0.35;
	Turn = 28.0 * FMath::Sin(Sway);
	Feet->SetRelativeRotation(FRotator(0.0, 180.0 + Turn, 0.0));   // facing the camera, on -x
	FTorqueAvatarInput In;
	In.Facing = Feet->GetComponentRotation();
	In.LookPitch = bFaceView ? 2.0 : -4.0;
	Avatar.Pose(In, DeltaSeconds);

	// The camera on -x, looking back at the face or the whole figure (the target is 3:4).
	const FVector Base = GetActorLocation();
	const double Tall = FMath::Max(Avatar.StandingHeight(), 120.0);
	FVector Aim;
	double Dist;
	if (bFaceView)
	{
		Aim = Avatar.HeadWorld() + FVector(0.0, 0.0, 6.0);
		Dist = 110.0;
		Capture->FOVAngle = 17.0f;
	}
	else
	{
		Aim = Base + FVector(0.0, 0.0, Tall * 0.52);
		Dist = 520.0;
		Capture->FOVAngle = 19.0f;
	}
	const FVector Eye = Aim + FVector(-Dist, 0.0, bFaceView ? 2.0 : 10.0);
	Capture->SetWorldLocationAndRotation(Eye, (Aim - Eye).Rotation());
	Key->SetWorldLocation(Aim + FVector(-170.0, -120.0, 110.0));
	Fill->SetWorldLocation(Aim + FVector(-150.0, 160.0, -20.0));
}

void ATorqueProfilePreview::Draw(UCanvas* Canvas, float X, float Y, float W, float H, bool bPortrait) const
{
	if (!Canvas || !Target || !Target->GetResource() || !Avatar.IsValid()) return;
	FCanvasTileItem Tile(FVector2D(X, Y), Target->GetResource(), FVector2D(W, H), FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	if (bPortrait)
	{
		// The middle of the picture, square.
		Tile.UV0 = FVector2D(0.0, 0.125);
		Tile.UV1 = FVector2D(1.0, 0.875);
	}
	Canvas->DrawItem(Tile);
}
