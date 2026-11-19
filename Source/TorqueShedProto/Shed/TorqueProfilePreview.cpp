#include "Shed/TorqueProfilePreview.h"

#include "CanvasItem.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RiderRig.h"
#include "Shed/TorqueDecals.h"
#include "Shed/TorqueToolModels.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueProfilePreviewSpot
{
// High over the level's origin, in the open: nothing above to shade it, nothing near to film.
const FVector Where(0.0, 0.0, 150000.0);
// (Build 155, beenster: "the avatar screen is so dark". Lit now as the shop's studio is, with a fixed
// exposure; -TorqueProfileExposure=N moves it, in stops.)
constexpr float ExposureBias = -0.5f;
constexpr double SpinRate = 20.0;           // degrees a second
constexpr double MinZoom = 0.6, MaxZoom = 3.5;
constexpr float Fov = 20.0f;
} // namespace TorqueProfilePreviewSpot

namespace TPPS = TorqueProfilePreviewSpot;

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
	Capture->FOVAngle = TPPS::Fov;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetMotionBlur(false);
	// No temporal anti-aliasing: its history blended each slider move in over many frames (beenster,
	// build 160: "respond so slowly to my slider input"); the picture is drawn larger than shown instead.
	Capture->ShowFlags.SetTemporalAA(false);
	// Only its own lights (as the shop's studio): not the level's sky light, nor bounce from the level.
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetLensFlares(false);
	{
		float Bias = TPPS::ExposureBias;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueProfileExposure="), Bias);
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = 0;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = Bias;
		PP.bOverride_DynamicGlobalIlluminationMethod = true;
		PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		PP.bOverride_ReflectionMethod = true;
		PP.ReflectionMethod = EReflectionMethod::ScreenSpace;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.15f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.0f;
		Capture->PostProcessBlendWeight = 1.0f;
	}
	Key = CreateDefaultSubobject<UPointLightComponent>(TEXT("Key"));
	Fill = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fill"));
	Rim = CreateDefaultSubobject<UPointLightComponent>(TEXT("Rim"));
	Bounce = CreateDefaultSubobject<UPointLightComponent>(TEXT("Bounce"));
	for (UPointLightComponent* L : {Key, Fill, Rim, Bounce})
	{
		L->SetupAttachment(Root);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetCastShadows(L == Key);
		// Only the studio: what it shows is on channel 2 and nothing else in the level is.
		L->LightingChannels.bChannel0 = false;
		L->LightingChannels.bChannel1 = false;
		L->LightingChannels.bChannel2 = true;
	}
	Key->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));
	Fill->SetLightColor(FLinearColor(0.80f, 0.88f, 1.0f));
	Rim->SetLightColor(FLinearColor(0.95f, 0.97f, 1.0f));
	Bounce->SetLightColor(FLinearColor(1.0f, 0.98f, 0.95f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(Root);
	Backdrop->SetStaticMesh(CubeMesh.Object);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->SetGenerateOverlapEvents(false);
	Backdrop->SetCastShadow(false);
	Backdrop->SetLightingChannels(false, false, true);
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
	ATorqueProfilePreview* P = World->SpawnActor<ATorqueProfilePreview>(TPPS::Where, FRotator::ZeroRotator, SP);
	if (P && P->Backdrop)
		P->Backdrop->SetMaterial(0, TorqueToolModels::Finish(P, FLinearColor(0.32f, 0.33f, 0.35f), 0.0f, 0.85f));   // a soft grey studio card
	return P;
}

void ATorqueProfilePreview::EndPlay(const EEndPlayReason::Type Reason)
{
	Avatar.Destroy();
	Super::EndPlay(Reason);
}

void ATorqueProfilePreview::StudioLit()
{
	TArray<UPrimitiveComponent*> Prims;
	GetComponents<UPrimitiveComponent>(Prims);
	if (Prims.Num() == LitCount) return;   // (nothing new: called as the sliders move, so kept cheap)
	LitCount = Prims.Num();
	for (UPrimitiveComponent* C : Prims)
	{
		if (!C) continue;
		C->SetLightingChannels(false, false, true);
		// (Build 160, beenster: "the preview looks so low def". It stands far above the level, where
		// the texture streamer, which only thinks of the player's view, left its textures blurry, and
		// the avatar picked its LODs for a small picture: full-size textures and the finest LOD here.)
		C->bForceMipStreaming = true;
		if (USkinnedMeshComponent* S = Cast<USkinnedMeshComponent>(C)) S->SetForcedLOD(1);
	}
}

void ATorqueProfilePreview::MakeHelmet()
{
	// The helmet the rider wears (Data/Bike/SpeedTwin/ue_assets.json rider_rig), turned as on the head.
	bHelmetTried = true;
	FTransform Fit;
	UMaterialInterface* Kit = nullptr;
	UStaticMesh* Mesh = TorqueDecals::HelmetMesh(Fit, Kit);
	HelmetKitPaint = Kit;
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: profile preview: no helmet mesh (ue_assets.json rider_rig.helmet)"));
		return;
	}
	Helmet = NewObject<UStaticMeshComponent>(this, TEXT("PreviewHelmet"));
	Helmet->SetStaticMesh(Mesh);
	Helmet->SetupAttachment(Root);
	Helmet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Helmet->SetGenerateOverlapEvents(false);
	Helmet->SetRelativeTransform(Fit);
	Helmet->SetVisibility(false, true);
	Helmet->RegisterComponent();
	// Placeholder slots take the kit's paint as on the rider (TorqueHelmetLook::PaintPlaceholders' rule).
	FTorqueProfile AsItCame;
	TorqueHelmetLook::ApplyPaint(Helmet, AsItCame, Kit);
	// Centred over the feet at chest height, out of the avatar's way when it's hidden.
	const FBoxSphereBounds B = Helmet->CalcBounds(Helmet->GetComponentTransform());
	Helmet->AddWorldOffset(GetActorLocation() + FVector(0.0, 0.0, 120.0) - B.Origin);
	StudioLit();
}

void ATorqueProfilePreview::Want(EView NewView, int32 Colour)
{
	View = NewView;
	WantFor = 0.25;
	// Built (again) for the avatar the profile picks, then given its height and face.
	const FTorqueProfile& Mine = FTorqueProfile::Mine();
	const int32 Index = Mine.AvatarFor(Colour);
	if (Index != BuiltFor)
	{
		BuiltFor = Index;
		LookOn.Reset();
		Avatar.Build(this, Feet, Index);
		Feet->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));   // facing the camera's start, on -x
		Capture->ShowOnlyActors.Reset();
		Capture->ShowOnlyActors.Add(this);
		LitCount = -1;
		StudioLit();
		Shown = EView::Body;
	}
	const TArray<uint8> Now = Mine.Pack();
	if (Avatar.IsValid() && Now != LookOn)
	{
		LookOn = Now;
		Avatar.SetLook(Mine);
		StudioLit();   // (a jacket put on just now too)
	}
	if (View == EView::Helmet && !bHelmetTried) MakeHelmet();
	if (Helmet)
	{
		// Your paint and decals on it, when they change.
		TArray<uint8> Paint = Now;
		Paint.RemoveAt(0, FMath::Min(2 + (int32)FTorqueProfile::NumFace, Paint.Num()));
		if (Paint != HelmetLookOn)
		{
			HelmetLookOn = Paint;
			TorqueHelmetLook::ApplyPaint(Helmet, Mine, HelmetKitPaint);
		}
		if (TorqueDecals::MyHelmet() != HelmetDecalsOn)
		{
			HelmetDecalsOn = TorqueDecals::MyHelmet();
			TorqueDecals::ApplyHelmet(Helmet, HelmetDecalsOn);
			StudioLit();   // (its decal skins too)
		}
	}
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
		Target->ClearColor = FLinearColor(0.02f, 0.02f, 0.025f, 1.0f);
		Target->InitAutoFormat(1152, 1536);   // (build 160: 768 x 1024 looked soft; drawn smaller, so smooth without TAA)
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

	// The avatar, or the helmet on its own.
	const bool bHelmetView = View == EView::Helmet && Helmet;
	const EView Show = bHelmetView ? EView::Helmet : EView::Body;
	if (Show != Shown)
	{
		Shown = Show;
		Avatar.SetShown(!bHelmetView, !bHelmetView, !bHelmetView);
		if (Helmet) Helmet->SetVisibility(bHelmetView, true);
	}

	if (bSpin && !bDrag) Yaw = FMath::Fmod(Yaw + TPPS::SpinRate * DeltaSeconds, 360.0);
	FTorqueAvatarInput In;
	In.Facing = Feet->GetComponentRotation();
	In.LookPitch = View == EView::Face ? 2.0 : -4.0;
	Avatar.Pose(In, DeltaSeconds);

	// What it looks at, and how far it stands to fit it (the target is 3:4).
	const FVector Base = GetActorLocation();
	const double Tall = FMath::Max(Avatar.StandingHeight(), 120.0);
	FVector Aim;
	double Radius;
	if (bHelmetView)
	{
		const FBoxSphereBounds B = Helmet->Bounds;
		Aim = B.Origin;
		Radius = FMath::Max(B.SphereRadius * 0.8, 12.0);
	}
	else if (View == EView::Face)   // the badge's portrait
	{
		Aim = Avatar.HeadWorld() + FVector(0.0, 0.0, 6.0);
		Radius = 17.0;
	}
	else
	{
		// (Build 160, beenster: "why cant i see them in full figure". The Profile page shows the whole
		// figure always; the wheel zooms in towards the face, which it fills at full zoom.)
		const double ZoomIn = FMath::Clamp((Zoom - 1.0) / (TPPS::MaxZoom - 1.0), 0.0, 1.0);
		const double T = ZoomIn * ZoomIn * (3.0 - 2.0 * ZoomIn);
		Aim = FMath::Lerp(Base + FVector(0.0, 0.0, Tall * 0.52), Avatar.HeadWorld() + FVector(0.0, 0.0, 6.0), T);
		Radius = FMath::Lerp(Tall * 0.5 + 6.0, 17.0, T);
	}
	const double TanV = FMath::Tan(FMath::DegreesToRadians(TPPS::Fov * 0.5)) * 4.0 / 3.0;   // (FOV is across; 3 wide by 4 high)
	const double Fit = Radius / FMath::Max(TanV, 0.01) * 1.08;
	const double Dist = FMath::Max(bHelmetView || View == EView::Face || Zoom < 1.0 ? Fit / Zoom : Fit, Radius * 0.6 + 5.0);
	// The camera starts on -x looking back at its front; Yaw takes it round, Pitch up over it.
	const FRotator ViewRot(-Pitch, Yaw, 0.0);
	const FVector Fwd = ViewRot.Vector();
	const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Z);
	const FVector Eye = Aim - Fwd * Dist;
	Capture->SetWorldLocationAndRotation(Eye, ViewRot);

	// The grey card behind, square to the view and filling the picture.
	if (Backdrop)
	{
		const double Back = Dist + Radius * 1.8 + 30.0;
		const double TanH = FMath::Tan(FMath::DegreesToRadians(TPPS::Fov * 0.5));
		Backdrop->SetWorldLocationAndRotation(Eye + Fwd * Back, ViewRot);
		Backdrop->SetWorldScale3D(FVector(0.01, 2.0 * Back * TanH * 1.15 / 100.0, 2.0 * Back * TanH * 4.0 / 3.0 * 1.15 / 100.0));
	}

	// The lights stay with the camera, as the shop's: key high on the left, fill low on the right,
	// rim behind and above, a little bounce from below. Brighter the further out they stand.
	const double L = Radius * 2.4 + 60.0;
	const double Gain = FMath::Square(L / 250.0);
	auto Place = [&](UPointLightComponent* Light, const FVector& Dir, double Candelas)
	{
		Light->SetWorldLocation(Aim + Dir.GetSafeNormal() * L);
		Light->SetIntensity((float)(Candelas * Gain));
		Light->SetAttenuationRadius((float)(L * 4.0));
		Light->SetSourceRadius((float)(L * 0.08));
	};
	Place(Key, -Fwd * 0.8 - Right * 0.75 + Up * 0.85, 120.0);
	Place(Fill, -Fwd * 0.9 + Right * 0.85 - Up * 0.1, 55.0);
	Place(Rim, Fwd * 0.9 + Right * 0.35 + Up * 0.95, 100.0);
	Place(Bounce, -Fwd * 0.6 - Up * 0.8, 25.0);
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

void ATorqueProfilePreview::DrawWorked(UCanvas* Canvas, float X, float Y, float W, float H)
{
	PictureBox = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	Draw(Canvas, X, Y, W, H, false);
}

bool ATorqueProfilePreview::Press(const FVector2D& Mouse)
{
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse)) return false;
	bDrag = true;
	DragLast = Mouse;
	return true;
}

void ATorqueProfilePreview::Hold(const FVector2D& Mouse, bool bLeftDown)
{
	if (!bDrag) return;
	if (!bLeftDown)
	{
		bDrag = false;
		return;
	}
	const FVector2D D = Mouse - DragLast;
	DragLast = Mouse;
	// Across turns it, up and down tips the view (as the shop's).
	Yaw = FMath::Fmod(Yaw - D.X * 0.45, 360.0);
	Pitch = FMath::Clamp(Pitch + D.Y * 0.3, -15.0, 60.0);
}

bool ATorqueProfilePreview::Wheel(const FVector2D& Mouse, int32 Dir)
{
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse) || Dir == 0) return false;
	Zoom = FMath::Clamp(Zoom * (Dir < 0 ? 1.12 : 1.0 / 1.12), TPPS::MinZoom, TPPS::MaxZoom);
	return true;
}

bool ATorqueProfilePreview::RightPress(const FVector2D& Mouse)
{
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse)) return false;
	bSpin = !bSpin;
	return true;
}

FString ATorqueProfilePreview::Describe() const
{
	static const TCHAR* Views[] = {TEXT("body"), TEXT("face"), TEXT("helmet")};
	return FString::Printf(TEXT("profile preview: %s view, %s, turned %.0f deg, tipped %.0f, zoom %.2f, %s%s"), Views[(int32)View],
		Avatar.IsValid() ? *Avatar.Name : TEXT("no avatar"), Yaw, Pitch, Zoom, bSpin ? TEXT("turning") : TEXT("still"),
		View == EView::Helmet && !Helmet ? TEXT(", NO helmet mesh") : TEXT(""));
}
