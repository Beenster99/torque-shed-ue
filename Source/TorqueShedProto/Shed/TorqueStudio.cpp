#include "Shed/TorqueStudio.h"

#include "Shed/TorqueStudioRig.h"

#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

namespace TorqueStudioImpl
{
TAutoConsoleVariable<int32> CVarMeter(TEXT("torque.StudioMeter"), 1,
	TEXT("The 3D preview windows meter their backdrop and hold the exposure there (1), or keep the rig's fixed exposure (0)."));
TAutoConsoleVariable<float> CVarStudioStops(TEXT("torque.StudioStops"), 0.0f,
	TEXT("Extra stops on every 3D preview's exposure, on top of the meter (for tuning the look by eye)."));
// The last trim any preview's meter settled on: the previews film through the same renderer, so one that
// can't read its corners (the profile page's face and helmet views) starts from another's.
float GSharedTrim = 0.0f;
bool bHasSharedTrim = false;
} // namespace TorqueStudioImpl

namespace TStudioI = TorqueStudioImpl;
namespace TStudioRig = TorqueStudioRig;

void TorqueStudio::Isolate(USceneCaptureComponent2D* Capture, float ExposureBias)
{
	if (!Capture) return;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// None of the level's air, sky or sky light: the studio is a closed room. (The sky light isn't held to
	// lighting channels, so it has to go here; up where the studios hang the clouds can be about too.)
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetLensFlares(false);
	// The level's post-process materials (the object highlighter's outline, any weather on the lens) are
	// added to every view whatever it overrides: not in the studio.
	Capture->ShowFlags.SetPostProcessMaterial(false);

	// Post-processing: every setting the studio's own. A fresh FPostProcessSettings holds the engine's
	// defaults; with every override on, the level's post-process volumes (TorqueSky moves the level's
	// exposure, its range and speed and the local exposure with the weather and the time of day;
	// TorqueSettings its bloom, grain and vignette) have nothing left to blend in.
	FPostProcessSettings PP;
	for (TFieldIterator<FBoolProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
		if (It->GetName().StartsWith(TEXT("bOverride_"))) It->SetPropertyValue_InContainer(&PP, true);
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.AutoExposureApplyPhysicalCameraExposure = 0;
	PP.AutoExposureBias = ExposureBias;
	PP.LocalExposureHighlightContrastScale = 1.0f;
	PP.LocalExposureShadowContrastScale = 1.0f;
	PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	PP.ReflectionMethod = EReflectionMethod::ScreenSpace;
	PP.BloomIntensity = 0.15f;
	PP.VignetteIntensity = 0.0f;
	PP.FilmGrainIntensity = 0.0f;
	PP.LensFlareIntensity = 0.0f;
	PP.SceneFringeIntensity = 0.0f;
	PP.DepthOfFieldFocalDistance = 0.0f;   // (0: no depth of field)
	PP.WeightedBlendables.Array.Reset();
	Capture->PostProcessSettings = PP;
	Capture->PostProcessBlendWeight = 1.0f;
}

UTextureRenderTarget2D* TorqueStudio::MakeTarget(UObject* Outer, int32 W, int32 H)
{
	UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Outer);
	T->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	T->ClearColor = FLinearColor(0.02f, 0.02f, 0.025f, 1.0f);
	// Its own gamma, not GEngine's DisplayGamma (which the Brightness setting moves).
	T->TargetGamma = 2.2f;
	T->InitAutoFormat(W, H);
	T->UpdateResourceImmediate(true);
	return T;
}

void TorqueStudio::MakeLights(AActor* Owner, USceneComponent* Parent, TArray<URectLightComponent*>& Out)
{
	Out.Reset();
	if (!Owner) return;
	for (int32 I = 0; I < TStudioRig::NumLights; ++I)
	{
		const TStudioRig::FSoftBox& Box = TStudioRig::Lights[I];
		URectLightComponent* L = Owner->CreateDefaultSubobject<URectLightComponent>(FName(*FString::Printf(TEXT("Studio%s"), ANSI_TO_TCHAR(Box.Name))));
		if (!L) continue;
		if (Parent) L->SetupAttachment(Parent);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetCastShadows(Box.bShadows);
		L->SetLightColor(FLinearColor(Box.R, Box.G, Box.B));
		L->LightingChannels.bChannel0 = false;
		L->LightingChannels.bChannel1 = false;
		L->LightingChannels.bChannel2 = true;
		L->SetBarnDoorAngle(88.0f);
		L->SetBarnDoorLength(0.0f);
		Out.Add(L);
	}
}

void TorqueStudio::PlaceLights(const TArray<URectLightComponent*>& Lights, const FVector& Centre, const FVector& Fwd, const FVector& Right,
	const FVector& Up, double RadiusCm)
{
	const double D = TStudioRig::DistanceCm(RadiusCm);
	const double G = TStudioRig::Gain(D);
	for (int32 I = 0; I < Lights.Num() && I < TStudioRig::NumLights; ++I)
	{
		URectLightComponent* L = Lights[I];
		if (!L) continue;
		const TStudioRig::FSoftBox& Box = TStudioRig::Lights[I];
		const FVector Dir = (-Fwd * Box.Toward + Right * Box.Right + Up * Box.Up).GetSafeNormal();
		const FVector At = Centre + Dir * D;
		// A rect light shines along its +x; its width along y, its height along z. Turned to the subject
		// and kept upright (the strips stand tall); the top light looks straight down, its width across
		// the picture.
		const FVector Face = (Centre - At).GetSafeNormal();
		const FVector Across = FMath::Abs(Face | Up) > 0.95 ? Right : (Up ^ Face).GetSafeNormal();
		const FRotator Turn = FRotationMatrix::MakeFromXY(Face, Across).Rotator();
		L->SetWorldLocationAndRotation(At, Turn);
		L->SetIntensity((float)(Box.Candelas * G));
		L->SetAttenuationRadius((float)(D * 4.0));
		L->SetSourceWidth((float)(D * Box.Width));
		L->SetSourceHeight((float)(D * Box.Height));
	}
}

void TorqueStudio::Enrol(USceneComponent* Under)
{
	if (!Under) return;
	TArray<USceneComponent*> All;
	Under->GetChildrenComponents(true, All);
	for (USceneComponent* C : All)
	{
		UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C);
		if (!P) continue;
		const FLightingChannels& Ch = P->LightingChannels;
		if (Ch.bChannel0 || Ch.bChannel1 || !Ch.bChannel2) P->SetLightingChannels(false, false, true);
	}
}

double TorqueStudio::ReadCorners(UTextureRenderTarget2D* Target2D)
{
	if (!Target2D) return -1.0;
	FTextureRenderTargetResource* R = Target2D->GameThread_GetRenderTargetResource();
	const int32 W = Target2D->SizeX, H = Target2D->SizeY;
	if (!R || W < 32 || H < 32) return -1.0;
	// A band along the top (4-10% down), of which the outer corners (8-22% in from each side) are read:
	// the subject is framed into the middle, so they're clear of it at the start view.
	const int32 Y0 = FMath::Max(1, (int32)(H * 0.04)), Y1 = FMath::Max(Y0 + 2, (int32)(H * 0.10));
	TArray<FColor> Px;
	if (!R->ReadPixels(Px, FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect(0, Y0, W, Y1)) || Px.Num() != W * (Y1 - Y0)) return -1.0;
	TArray<float> Grey;
	Grey.Reserve(Px.Num() / 3);
	for (int32 Y = 0; Y < Y1 - Y0; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const float U = (X + 0.5f) / W;
			if (!((U > 0.08f && U < 0.22f) || (U > 0.78f && U < 0.92f))) continue;
			const FColor& C = Px[Y * W + X];
			Grey.Add((0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B) / 255.0f);
		}
	if (Grey.Num() == 0) return -1.0;
	Grey.Sort();
	return Grey[Grey.Num() / 2];
}

void TorqueStudio::FMeter::Tick(float DeltaSeconds, bool bOn, bool bMayRead, USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target2D, float Base)
{
	if (!Capture) return;
	if (!bOn)
	{
		bWasOn = false;
		return;
	}
	if (!bWasOn)
	{
		// Opened: read soon (a few frames in, once it has filmed), and settle again.
		bWasOn = true;
		bSettled = false;
		Reads = 0;
		Next = 0.2f;
	}
	const bool bMeter = TStudioI::CVarMeter.GetValueOnGameThread() != 0;
	if (!bMeter) Trim = 0.0f;
	else if (!bOwnTrim && TStudioI::bHasSharedTrim) Trim = TStudioI::GSharedTrim;
	Next -= DeltaSeconds;
	if (bMeter && bMayRead && Next <= 0.0f)
	{
		const double Read = ReadCorners(Target2D);
		if (Read < 0.0)
		{
			Next = 1.0f;
		}
		else
		{
			LastRead = Read;
			++Reads;
			const bool bClose = FMath::Abs(Read - Target) <= TStudioRig::MeterTolerance;
			if (!bClose) Trim = (float)FMath::Clamp(Trim + TStudioRig::MeterStep(Read, Target), TStudioRig::TrimMin, TStudioRig::TrimMax);
			if (bClose || Reads >= 8)
			{
				if (!bSettled)
				{
					UE_LOG(LogTemp, Log, TEXT("Torque studio (%s): backdrop reads %.3f (wants %.3f) after %d reads: exposure %+.2f stops (trim %+.2f)%s"),
						Who, Read, Target, Reads, Base + Trim, Trim, FMath::Abs(Trim) > 1.0f ? TEXT(": something outside the studio is moving its picture") : TEXT(""));
				}
				bSettled = true;
				bOwnTrim = true;
				TStudioI::GSharedTrim = Trim;
				TStudioI::bHasSharedTrim = true;
				Reads = 0;
				Next = 15.0f;   // and a look now and then while it's up
			}
			else
			{
				Next = 0.12f;   // (a couple of frames for the new exposure to be filmed)
			}
		}
	}
	const float Want = Base + Trim + TStudioI::CVarStudioStops.GetValueOnGameThread();
	if (Capture->PostProcessSettings.AutoExposureBias != Want) Capture->PostProcessSettings.AutoExposureBias = Want;
}
