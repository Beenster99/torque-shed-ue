#include "TorqueSettings.h"

#include "TorquePlayerState.h"
#include "SonyPad.h"
#include "GameFramework/Pawn.h"

#include "AudioDevice.h"
#include "Components/ShapeComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GenericPlatform/GenericApplication.h"
#include "Scalability.h"
#include "Widgets/SWindow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace
{
const TCHAR* Section = TEXT("TorqueSettings");

void SetCVar(const TCHAR* Name, float Value, EConsoleVariableFlags By = ECVF_SetByGameSetting)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, By);
}
void SetCVar(const TCHAR* Name, int32 Value, EConsoleVariableFlags By = ECVF_SetByGameSetting)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, By);
}
// A setting that either changes a console variable or leaves it as the game started with it: the
// starting value is kept the first time, to put back.
void SetCVarDefault(const TCHAR* Name, bool bChange, const FString& Value)
{
	static TMap<FString, FString> Started;
	IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!V) return;
	const FString* Was = Started.Find(Name);
	if (!Was) Was = &Started.Add(Name, V->GetString());
	if (bChange) V->Set(*Value, ECVF_SetByGameSetting);
	else if (V->GetString() != *Was) V->Set(**Was, ECVF_SetByGameSetting);
}
} // namespace

FTorqueSettings& FTorqueSettings::Get()
{
	static FTorqueSettings S;
	if (!S.bLoaded) S.Load();
	return S;
}

void FTorqueSettings::Load()
{
	bLoaded = true;
	if (!GConfig) return;
	const FString& Ini = GGameUserSettingsIni;
	GConfig->GetBool(Section, TEXT("Saved"), bSaved, Ini);
	GConfig->GetInt(Section, TEXT("Quality"), Quality, Ini);
	GConfig->GetInt(Section, TEXT("WindowMode"), WindowMode, Ini);
	GConfig->GetInt(Section, TEXT("ResX"), Resolution.X, Ini);
	GConfig->GetInt(Section, TEXT("ResY"), Resolution.Y, Ini);
	GConfig->GetInt(Section, TEXT("Upscaler"), Upscaler, Ini);
	GConfig->GetInt(Section, TEXT("UpscaleMode"), UpscaleMode, Ini);
	GConfig->GetInt(Section, TEXT("FrameCap"), FrameCap, Ini);
	GConfig->GetBool(Section, TEXT("VSync"), bVSync, Ini);
	GConfig->GetBool(Section, TEXT("MotionBlur"), bMotionBlur, Ini);
	GConfig->GetInt(Section, TEXT("Monitor"), Monitor, Ini);
	GConfig->GetFloat(Section, TEXT("Brightness"), Brightness, Ini);
	GConfig->GetFloat(Section, TEXT("Sharpness"), Sharpness, Ini);
	GConfig->GetInt(Section, TEXT("AntiAliasing"), AntiAliasing, Ini);
	GConfig->GetInt(Section, TEXT("Shadows"), Shadows, Ini);
	GConfig->GetInt(Section, TEXT("Textures"), Textures, Ini);
	GConfig->GetInt(Section, TEXT("GlobalIllumination"), GlobalIllumination, Ini);
	GConfig->GetInt(Section, TEXT("Reflections"), Reflections, Ini);
	GConfig->GetInt(Section, TEXT("Effects"), Effects, Ini);
	GConfig->GetInt(Section, TEXT("Foliage"), Foliage, Ini);
	GConfig->GetFloat(Section, TEXT("DrawDistance"), DrawDistance, Ini);
	GConfig->GetInt(Section, TEXT("Fog"), Fog, Ini);
	GConfig->GetInt(Section, TEXT("Clouds"), Clouds, Ini);
	GConfig->GetBool(Section, TEXT("AmbientOcclusion"), bAmbientOcclusion, Ini);
	GConfig->GetFloat(Section, TEXT("Bloom"), Bloom, Ini);
	GConfig->GetInt(Section, TEXT("PostProcess"), PostProcess, Ini);
	GConfig->GetInt(Section, TEXT("Shading"), Shading, Ini);
	GConfig->GetInt(Section, TEXT("Anisotropy"), Anisotropy, Ini);
	GConfig->GetBool(Section, TEXT("Hdr"), bHdr, Ini);
	GConfig->GetFloat(Section, TEXT("FilmGrain"), FilmGrain, Ini);
	GConfig->GetFloat(Section, TEXT("Vignette"), Vignette, Ini);
	GConfig->GetBool(Section, TEXT("LensFlares"), bLensFlares, Ini);
	GConfig->GetBool(Section, TEXT("ChromaticAberration"), bChromaticAberration, Ini);
	GConfig->GetBool(Section, TEXT("DepthOfField"), bDepthOfField, Ini);
	GConfig->GetBool(Section, TEXT("FpsCounter"), bFpsCounter, Ini);
	GConfig->GetBool(Section, TEXT("AvatarShadows"), bAvatarShadows, Ini);
	GConfig->GetFloat(Section, TEXT("Master"), Master, Ini);
	GConfig->GetFloat(Section, TEXT("Sfx"), Sfx, Ini);
	GConfig->GetFloat(Section, TEXT("Engine"), Engine, Ini);
	GConfig->GetFloat(Section, TEXT("Ambience"), Ambience, Ini);
	GConfig->GetFloat(Section, TEXT("Radio"), Radio, Ini);
	GConfig->GetFloat(Section, TEXT("Fov"), Fov, Ini);
	GConfig->GetBool(Section, TEXT("AutoShift"), bAutoShift, Ini);
	GConfig->GetBool(Section, TEXT("Telemetry"), bTelemetry, Ini);
	GConfig->GetBool(Section, TEXT("InfoPanel"), bInfoPanel, Ini);
	GConfig->GetBool(Section, TEXT("PlaceLookup"), bPlaceLookup, Ini);
	GConfig->GetInt(Section, TEXT("WorldCacheGb"), WorldCacheGb, Ini);
	GConfig->GetBool(Section, TEXT("WorldOffline"), bWorldOffline, Ini);
	WorldCacheGb = FMath::Clamp(WorldCacheGb, 1, 200);
	GConfig->GetBool(Section, TEXT("LookIntoCorner"), bLookIntoCorner, Ini);
	GConfig->GetBool(Section, TEXT("CameraRecentre"), bCameraRecentre, Ini);
	GConfig->GetInt(Section, TEXT("Units"), Units, Ini);
	GConfig->GetInt(Section, TEXT("ScreenBright"), ScreenBright, Ini);
	GConfig->GetBool(Section, TEXT("DayNight"), bDayNight, Ini);
	GConfig->GetInt(Section, TEXT("DayLength"), DayLength, Ini);
	GConfig->GetFloat(Section, TEXT("TimeOfDay"), TimeOfDay, Ini);
	GConfig->GetBool(Section, TEXT("RealClock"), bRealClock, Ini);
	GConfig->GetInt(Section, TEXT("WeatherMode"), WeatherMode, Ini);
	// Live weather became the default after these settings were saved: always sunny, the old default,
	// moves to Live once (any other choice stays as it was).
	bool bLiveOffered = false;
	GConfig->GetBool(Section, TEXT("WeatherLiveOffered"), bLiveOffered, Ini);
	if (!bLiveOffered && WeatherMode == 1) WeatherMode = WeatherLive;
	for (int32 I = 0; I < 4; ++I) GConfig->GetFloat(Section, *FString::Printf(TEXT("PadCentre%d"), I), PadCentre[I], Ini);
	GConfig->GetFloat(Section, TEXT("Realism"), Realism, Ini);
	GConfig->GetBool(Section, TEXT("OpenRouter"), bOpenRouter, Ini);
	GConfig->GetFloat(Section, TEXT("MouseLook"), MouseLook, Ini);
	MouseLook = FMath::Clamp(MouseLook, 0.1f, 10.0f);
	GConfig->GetFloat(Section, TEXT("PadLook"), PadLook, Ini);
	GConfig->GetBool(Section, TEXT("InvertLook"), bInvertLook, Ini);
	GConfig->GetFloat(Section, TEXT("StickDeadZone"), StickDeadZone, Ini);
	GConfig->GetFloat(Section, TEXT("Steering"), Steering, Ini);
	GConfig->GetString(Section, TEXT("Name"), Name, Ini);
	// A colour nobody chose (or a -TorqueName test copy, so two on one PC differ): one of the bright
	// ones, from the name, rather than everyone in black. -TorqueColour=N picks one.
	FString TestName;
	const bool bTestName = FParse::Value(FCommandLine::Get(), TEXT("TorqueName="), TestName);
	if (!GConfig->GetInt(Section, TEXT("Colour"), Colour, Ini) || bTestName)
	{
		const FString By = bTestName ? TestName : (Name.IsEmpty() ? FString(FPlatformProcess::UserName()) : Name);
		Colour = 1 + (int32)(GetTypeHash(By) % (uint32)(Colours().Num() - 1));
	}
	FParse::Value(FCommandLine::Get(), TEXT("TorqueColour="), Colour);
	Quality = FMath::Clamp(Quality, 0, 4);
	Brightness = FMath::Clamp(Brightness, 0.5f, 1.5f);
	Sharpness = FMath::Clamp(Sharpness, 0.0f, 1.0f);
	AntiAliasing = FMath::Clamp(AntiAliasing, 0, 3);
	for (int32* G : {&Shadows, &Textures, &GlobalIllumination, &Reflections, &Effects, &Foliage}) *G = FMath::Clamp(*G, -1, 4);
	DrawDistance = FMath::Clamp(DrawDistance, 0.5f, 2.0f);
	Fog = FMath::Clamp(Fog, -1, 2);
	Clouds = FMath::Clamp(Clouds, -1, 4);
	Bloom = FMath::Clamp(Bloom, 0.0f, 2.0f);
	PostProcess = FMath::Clamp(PostProcess, -1, 4);
	Shading = FMath::Clamp(Shading, -1, 4);
	Anisotropy = FMath::Clamp(Anisotropy, -1, 4);
	FilmGrain = FMath::Clamp(FilmGrain, 0.0f, 2.0f);
	Vignette = FMath::Clamp(Vignette, 0.0f, 2.0f);
	Fov = FMath::Clamp(Fov, 60.0f, 110.0f);
	Units = FMath::Clamp(Units, 0, 1);
	ScreenBright = FMath::Clamp(ScreenBright, 0, 3);
	if (!DayLengths().Contains(DayLength)) DayLength = 120;
	TimeOfDay = TimeOfDay < 0.0f ? -1.0f : FMath::Fmod(TimeOfDay, 24.0f);
	WeatherMode = FMath::Clamp(WeatherMode, 0, WeatherLive);
	FSonyPad::SetCentre(PadCentre);
	WindowMode = FMath::Clamp(WindowMode, 0, 2);
	UpscaleMode = FMath::Clamp(UpscaleMode, 0, 4);
	Colour = FMath::Clamp(Colour, 0, Colours().Num() - 1);
	GConfig->GetInt(Section, TEXT("RiderStyle"), RiderStyle, Ini);
	RiderStyle = FMath::Clamp(RiderStyle, 0, RiderStyles().Num() - 1);
}

void FTorqueSettings::Save()
{
	if (!GConfig) return;
	const FString& Ini = GGameUserSettingsIni;
	bSaved = true;
	GConfig->SetBool(Section, TEXT("Saved"), true, Ini);
	GConfig->SetInt(Section, TEXT("Quality"), Quality, Ini);
	GConfig->SetInt(Section, TEXT("WindowMode"), WindowMode, Ini);
	GConfig->SetInt(Section, TEXT("ResX"), Resolution.X, Ini);
	GConfig->SetInt(Section, TEXT("ResY"), Resolution.Y, Ini);
	GConfig->SetInt(Section, TEXT("Upscaler"), Upscaler, Ini);
	GConfig->SetInt(Section, TEXT("UpscaleMode"), UpscaleMode, Ini);
	GConfig->SetInt(Section, TEXT("FrameCap"), FrameCap, Ini);
	GConfig->SetBool(Section, TEXT("VSync"), bVSync, Ini);
	GConfig->SetBool(Section, TEXT("MotionBlur"), bMotionBlur, Ini);
	GConfig->SetInt(Section, TEXT("Monitor"), Monitor, Ini);
	GConfig->SetFloat(Section, TEXT("Brightness"), Brightness, Ini);
	GConfig->SetFloat(Section, TEXT("Sharpness"), Sharpness, Ini);
	GConfig->SetInt(Section, TEXT("AntiAliasing"), AntiAliasing, Ini);
	GConfig->SetInt(Section, TEXT("Shadows"), Shadows, Ini);
	GConfig->SetInt(Section, TEXT("Textures"), Textures, Ini);
	GConfig->SetInt(Section, TEXT("GlobalIllumination"), GlobalIllumination, Ini);
	GConfig->SetInt(Section, TEXT("Reflections"), Reflections, Ini);
	GConfig->SetInt(Section, TEXT("Effects"), Effects, Ini);
	GConfig->SetInt(Section, TEXT("Foliage"), Foliage, Ini);
	GConfig->SetFloat(Section, TEXT("DrawDistance"), DrawDistance, Ini);
	GConfig->SetInt(Section, TEXT("Fog"), Fog, Ini);
	GConfig->SetInt(Section, TEXT("Clouds"), Clouds, Ini);
	GConfig->SetBool(Section, TEXT("AmbientOcclusion"), bAmbientOcclusion, Ini);
	GConfig->SetFloat(Section, TEXT("Bloom"), Bloom, Ini);
	GConfig->SetInt(Section, TEXT("PostProcess"), PostProcess, Ini);
	GConfig->SetInt(Section, TEXT("Shading"), Shading, Ini);
	GConfig->SetInt(Section, TEXT("Anisotropy"), Anisotropy, Ini);
	GConfig->SetBool(Section, TEXT("Hdr"), bHdr, Ini);
	GConfig->SetFloat(Section, TEXT("FilmGrain"), FilmGrain, Ini);
	GConfig->SetFloat(Section, TEXT("Vignette"), Vignette, Ini);
	GConfig->SetBool(Section, TEXT("LensFlares"), bLensFlares, Ini);
	GConfig->SetBool(Section, TEXT("ChromaticAberration"), bChromaticAberration, Ini);
	GConfig->SetBool(Section, TEXT("DepthOfField"), bDepthOfField, Ini);
	GConfig->SetBool(Section, TEXT("FpsCounter"), bFpsCounter, Ini);
	GConfig->SetBool(Section, TEXT("AvatarShadows"), bAvatarShadows, Ini);
	GConfig->SetFloat(Section, TEXT("Master"), Master, Ini);
	GConfig->SetFloat(Section, TEXT("Sfx"), Sfx, Ini);
	GConfig->SetFloat(Section, TEXT("Engine"), Engine, Ini);
	GConfig->SetFloat(Section, TEXT("Ambience"), Ambience, Ini);
	GConfig->SetFloat(Section, TEXT("Radio"), Radio, Ini);
	GConfig->SetFloat(Section, TEXT("Fov"), Fov, Ini);
	GConfig->SetBool(Section, TEXT("AutoShift"), bAutoShift, Ini);
	GConfig->SetBool(Section, TEXT("Telemetry"), bTelemetry, Ini);
	GConfig->SetBool(Section, TEXT("InfoPanel"), bInfoPanel, Ini);
	GConfig->SetBool(Section, TEXT("PlaceLookup"), bPlaceLookup, Ini);
	GConfig->SetInt(Section, TEXT("WorldCacheGb"), WorldCacheGb, Ini);
	GConfig->SetBool(Section, TEXT("WorldOffline"), bWorldOffline, Ini);
	GConfig->SetBool(Section, TEXT("LookIntoCorner"), bLookIntoCorner, Ini);
	GConfig->SetBool(Section, TEXT("CameraRecentre"), bCameraRecentre, Ini);
	GConfig->SetInt(Section, TEXT("Units"), Units, Ini);
	GConfig->SetInt(Section, TEXT("ScreenBright"), ScreenBright, Ini);
	GConfig->SetBool(Section, TEXT("DayNight"), bDayNight, Ini);
	GConfig->SetInt(Section, TEXT("DayLength"), DayLength, Ini);
	GConfig->SetFloat(Section, TEXT("TimeOfDay"), TimeOfDay, Ini);
	GConfig->SetBool(Section, TEXT("RealClock"), bRealClock, Ini);
	GConfig->SetInt(Section, TEXT("WeatherMode"), WeatherMode, Ini);
	GConfig->SetBool(Section, TEXT("WeatherLiveOffered"), true, Ini);
	for (int32 I = 0; I < 4; ++I) GConfig->SetFloat(Section, *FString::Printf(TEXT("PadCentre%d"), I), PadCentre[I], Ini);
	GConfig->SetFloat(Section, TEXT("Realism"), Realism, Ini);
	GConfig->SetBool(Section, TEXT("OpenRouter"), bOpenRouter, Ini);
	GConfig->SetFloat(Section, TEXT("MouseLook"), MouseLook, Ini);
	GConfig->SetFloat(Section, TEXT("PadLook"), PadLook, Ini);
	GConfig->SetBool(Section, TEXT("InvertLook"), bInvertLook, Ini);
	GConfig->SetFloat(Section, TEXT("StickDeadZone"), StickDeadZone, Ini);
	GConfig->SetFloat(Section, TEXT("Steering"), Steering, Ini);
	GConfig->SetString(Section, TEXT("Name"), *Name, Ini);
	GConfig->SetInt(Section, TEXT("Colour"), Colour, Ini);
	GConfig->SetInt(Section, TEXT("RiderStyle"), RiderStyle, Ini);
	GConfig->Flush(false, Ini);
}

float FTorqueSettings::ScreenPercent(int32 Mode)
{
	static const float Pct[5] = {100.0f, 66.7f, 58.0f, 50.0f, 33.3f};
	return Pct[FMath::Clamp(Mode, 0, 4)];
}

bool FTorqueSettings::HdrAvailable()
{
	const UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return G && G->SupportsHDRDisplayOutput();
}

bool FTorqueSettings::HasDlss()
{
	// NVIDIA's DLSS plugin (free, from Fab) registers these when it's in the build and the GPU can.
	// Asked every frame by the menus: keep the answer once it's yes, and look again at most every 2 s
	// while it's no (the plugin registers its cvars late, at PostEngineInit).
	static bool bFound = false;
	static double NextLook = 0.0;
	if (bFound) return true;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextLook) return false;
	NextLook = Now + 2.0;
	bFound = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")) != nullptr;
	return bFound;
}

void FTorqueSettings::Apply(UWorld* World) const
{
	if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		MoveToMonitor(Monitor);
		G->SetOverallScalabilityLevel(Quality);
		// Torque Shed's own quality rows: each Unreal scalability group the preset's, or its own.
		G->SetShadowQuality(Shadows < 0 ? Quality : Shadows);
		G->SetTextureQuality(Textures < 0 ? Quality : Textures);
		G->SetGlobalIlluminationQuality(GlobalIllumination < 0 ? Quality : GlobalIllumination);
		G->SetReflectionQuality(Reflections < 0 ? Quality : Reflections);
		G->SetVisualEffectQuality(Effects < 0 ? Quality : Effects);
		G->SetFoliageQuality(Foliage < 0 ? Quality : Foliage);
		G->SetPostProcessingQuality(PostProcess < 0 ? Quality : PostProcess);
		G->SetShadingQuality(Shading < 0 ? Quality : Shading);
		G->SetFullscreenMode(WindowMode == 0 ? EWindowMode::Fullscreen : WindowMode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		const FIntPoint Res = (Resolution.X > 0 && Resolution.Y > 0) ? Resolution : G->GetDesktopResolution();
		G->SetScreenResolution(Res);
		G->SetFrameRateLimit((float)FrameCap);
		G->SetVSyncEnabled(bVSync);
		G->ApplySettings(false);
		// Every scalability setting put back as its group's level says (Unreal only sets the ones whose
		// level changed), so the rows below start from the preset each time.
		Scalability::SetQualityLevels(Scalability::GetQualityLevels(), true);
		// HDR output, only where the screen has it (and back off when the setting is off).
		if (G->SupportsHDRDisplayOutput() && G->IsHDREnabled() != bHdr) G->EnableHDRDisplayOutput(bHdr);
	}
	const bool bDlss = Upscaler == 1 && HasDlss();
	// TSR (DLSS takes over from it when on, and needs a temporal one under it), TAA, FXAA or none.
	static const int32 AaMethods[4] = {4, 2, 1, 0};
	SetCVar(TEXT("r.AntiAliasingMethod"), bDlss ? 4 : AaMethods[FMath::Clamp(AntiAliasing, 0, 3)]);
	SetCVar(TEXT("r.ScreenPercentage"), ScreenPercent(UpscaleMode));
	// NVIDIA's DLSS 4.5 plugin (UE 5.8, v8.9): on or off; its quality is the screen percentage set
	// above (Quality 66.7, Balanced 58, Performance 50, Ultra performance 33, and 100 is DLAA).
	if (HasDlss()) SetCVar(TEXT("r.NGX.DLSS.Enable"), bDlss ? 1 : 0);
	SetCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 4 : 0);
	// Brightness: the display gamma (2.2 as it comes; higher is brighter).
	if (GEngine) GEngine->DisplayGamma = 2.2f * FMath::Clamp(Brightness, 0.5f, 1.5f);
	SetCVarDefault(TEXT("r.Tonemapper.Sharpen"), Sharpness > 0.001f, FString::SanitizeFloat(FMath::Clamp(Sharpness, 0.0f, 1.0f)));
	// Draw distance: times the view distance the preset gives (Unreal's BaseScalability.ini).
	{
		static const float ByLevel[5] = {0.4f, 0.6f, 0.8f, 1.0f, 1.0f};
		SetCVar(TEXT("r.ViewDistanceScale"), ByLevel[FMath::Clamp(Quality, 0, 4)] * FMath::Clamp(DrawDistance, 0.5f, 2.0f), ECVF_SetByScalability);
	}
	// Volumetric fog and light: the preset's, off, low (coarse) or high.
	if (Fog >= 0)
	{
		SetCVar(TEXT("r.VolumetricFog"), Fog > 0 ? 1 : 0, ECVF_SetByScalability);
		if (Fog > 0) SetCVar(TEXT("r.VolumetricFog.GridPixelSize"), Fog == 1 ? 16 : 8, ECVF_SetByScalability);
	}
	// The volumetric clouds: off, or how many steps each ray takes through them.
	SetCVarDefault(TEXT("r.VolumetricCloud"), Clouds == 0, TEXT("0"));
	{
		static const TCHAR* Steps[5] = {TEXT("128"), TEXT("256"), TEXT("512"), TEXT("768"), TEXT("1024")};
		SetCVarDefault(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), Clouds > 0, Steps[FMath::Clamp(Clouds, 0, 4)]);
	}
	// Texture filtering: the preset's, or 1x (bilinear) to 16x anisotropic.
	if (Anisotropy >= 0) SetCVar(TEXT("r.MaxAnisotropy"), 1 << FMath::Clamp(Anisotropy, 0, 4), ECVF_SetByScalability);
	// The camera effects, each on as the level has it or off.
	SetCVarDefault(TEXT("r.LensFlareQuality"), !bLensFlares, TEXT("0"));
	SetCVarDefault(TEXT("r.SceneColorFringeQuality"), !bChromaticAberration, TEXT("0"));
	SetCVarDefault(TEXT("r.DepthOfFieldQuality"), !bDepthOfField, TEXT("0"));
	// Ambient occlusion: the screen-space one and Lumen's short-range one.
	SetCVarDefault(TEXT("r.AmbientOcclusionLevels"), !bAmbientOcclusion, TEXT("0"));
	SetCVarDefault(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"), !bAmbientOcclusion, TEXT("0"));
	// Glow and bloom: off, or the level's own strength times the setting.
	SetCVarDefault(TEXT("r.BloomQuality"), Bloom < 0.005f, TEXT("0"));
	if (World)
	{
		// Bloom, film grain and the vignette: each volume's own strength (or Unreal's default), times the setting.
		struct FOwn { float BloomOwn, GrainOwn, VignetteOwn; };
		static TMap<TWeakObjectPtr<APostProcessVolume>, FOwn> Level;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			APostProcessVolume* Ppv = *It;
			FPostProcessSettings& P = Ppv->Settings;
			const FOwn* Found = Level.Find(Ppv);
			const FOwn Own = Found ? *Found
								   : Level.Add(Ppv, FOwn{P.bOverride_BloomIntensity ? P.BloomIntensity : 0.675f, P.bOverride_FilmGrainIntensity ? P.FilmGrainIntensity : 0.0f,
										 P.bOverride_VignetteIntensity ? P.VignetteIntensity : 0.4f});
			P.bOverride_BloomIntensity = true;
			P.BloomIntensity = Own.BloomOwn * FMath::Clamp(Bloom, 0.0f, 2.0f);
			P.bOverride_FilmGrainIntensity = true;
			// (up to the level's own at 1, then on to twice it, or a clear amount where the level has little)
			auto Times = [](float Base, float K, float Most) {
				return K <= 1.0f ? Base * K : FMath::Lerp(Base, FMath::Max(Base * 2.0f, Most), FMath::Min(K - 1.0f, 1.0f));
			};
			P.FilmGrainIntensity = FMath::Clamp(Times(Own.GrainOwn, FilmGrain, 0.5f), 0.0f, 1.0f);
			P.bOverride_VignetteIntensity = true;
			P.VignetteIntensity = FMath::Clamp(Times(Own.VignetteOwn, Vignette, 0.8f), 0.0f, 1.0f);
		}
		FAudioDeviceHandle Audio = World->GetAudioDevice();
		if (Audio.IsValid()) Audio->SetTransientPrimaryVolume(FMath::Clamp(Master, 0.0f, 1.0f));
		// People on foot casting shadows (and the ones who come later: ATorquePlayerController checks).
		ApplyAvatarShadows(World);
	}
}

namespace
{
// The parts Avatar shadows turned off, to turn back on (and only those: some never cast one).
TArray<TWeakObjectPtr<UPrimitiveComponent>>& Darkened()
{
	static TArray<TWeakObjectPtr<UPrimitiveComponent>> D;
	return D;
}
void Darken(AActor* Actor, int32 Depth)
{
	if (!Actor || Depth > 4) return;
	TArray<UPrimitiveComponent*> Parts;
	Actor->GetComponents<UPrimitiveComponent>(Parts);
	for (UPrimitiveComponent* Part : Parts)
		if (Part && Part->CastShadow && !Part->IsA<UShapeComponent>())
		{
			Part->SetCastShadow(false);
			Darkened().Add(Part);
		}
	TArray<AActor*> Attached;
	Actor->GetAttachedActors(Attached);
	for (AActor* A : Attached) Darken(A, Depth + 1);
}
} // namespace

void FTorqueSettings::ApplyAvatarShadows(UWorld* World) const
{
	if (bAvatarShadows)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Part : Darkened())
			if (UPrimitiveComponent* P = Part.Get()) P->SetCastShadow(true);
		Darkened().Reset();
		return;
	}
	if (!World) return;
	for (TActorIterator<ACharacter> It(World); It; ++It) Darken(*It, 0);
}

void FTorqueSettings::MoveToMonitor(int32 Index)
{
	// Torque Shed's Monitor row: the window to that screen (fullscreen follows the window's screen).
	if (Index < 0 || !GEngine || !GEngine->GameViewport) return;
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	if (!Metrics.MonitorInfo.IsValidIndex(Index)) return;
	TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
	if (!Window.IsValid()) return;
	const FPlatformRect& Work = Metrics.MonitorInfo[Index].WorkArea;
	const FVector2D Pos = Window->GetPositionInScreen();
	if (Pos.X >= Work.Left && Pos.X < Work.Right && Pos.Y >= Work.Top && Pos.Y < Work.Bottom) return;   // there already
	if (Window->GetWindowMode() != EWindowMode::Windowed) Window->SetWindowMode(EWindowMode::Windowed);
	Window->MoveWindowTo(FVector2D(Work.Left + 40, Work.Top + 40));
}

namespace
{
// The screens' names for the menu, looked up again every few seconds (it's asked every frame).
const TArray<FString>& Screens()
{
	static TArray<FString> Names;
	static double At = -100.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - At > 3.0)
	{
		At = Now;
		Names.Reset();
		FDisplayMetrics Metrics;
		FDisplayMetrics::RebuildDisplayMetrics(Metrics);
		for (int32 I = 0; I < Metrics.MonitorInfo.Num(); ++I)
		{
			const FMonitorInfo& M = Metrics.MonitorInfo[I];
			Names.Add(FString::Printf(TEXT("Screen %d: %d x %d%s"), I + 1, M.NativeWidth, M.NativeHeight, M.bIsPrimary ? TEXT(" (main)") : TEXT("")));
		}
	}
	return Names;
}
} // namespace

FString FTorqueSettings::MonitorName(int32 Index)
{
	if (Index < 0) return TEXT("Where it opens");
	return Screens().IsValidIndex(Index) ? Screens()[Index] : FString::Printf(TEXT("Screen %d"), Index + 1);
}

int32 FTorqueSettings::MonitorCount()
{
	return Screens().Num();
}

const TArray<int32>& FTorqueSettings::DayLengths()
{
	// As Torque Shed's Daylight.DAY_LENGTHS: real minutes for a day.
	static const TArray<int32> L = {10, 20, 30, 60, 120, 240, 480, 1440};
	return L;
}

FString FTorqueSettings::SpeedText(double MetresPerSecond) const
{
	const double V = FMath::Abs(MetresPerSecond);
	return Units == 0 ? FString::Printf(TEXT("%.0f km/h"), V * 3.6) : FString::Printf(TEXT("%.0f mph"), V * 2.23694);
}

FString FTorqueSettings::DistanceText(double Metres) const
{
	return Units == 0 ? FString::Printf(TEXT("%.2f km"), Metres / 1000.0) : FString::Printf(TEXT("%.2f miles"), Metres / 1609.344);
}

const TArray<FColor>& FTorqueSettings::Colours()
{
	static const TArray<FColor> C = {
		FColor(30, 30, 32),     // black
		FColor(190, 40, 35),    // red
		FColor(35, 85, 170),    // blue
		FColor(40, 120, 60),    // racing green
		FColor(230, 170, 30),   // yellow
		FColor(235, 235, 230),  // white
		FColor(230, 110, 30),   // orange
		FColor(120, 70, 40),    // brown leather
	};
	return C;
}

const TArray<FString>& FTorqueSettings::ColourNames()
{
	static const TArray<FString> N = {TEXT("Black"), TEXT("Red"), TEXT("Blue"), TEXT("Racing green"), TEXT("Yellow"),
		TEXT("White"), TEXT("Orange"), TEXT("Brown leather")};
	return N;
}

FColor FTorqueSettings::RiderColour() const
{
	return Colours()[FMath::Clamp(Colour, 0, Colours().Num() - 1)];
}

// As Torque Shed's main_menu.gd realism_word / realism_text.
FString FTorqueSettings::RealismWord(float V)
{
	if (V < 0.15f) return TEXT("full sim");
	if (V < 0.45f) return TEXT("mostly sim");
	if (V < 0.8f) return TEXT("assisted");
	return TEXT("easy");
}

FString FTorqueSettings::RealismText(float V)
{
	if (V < 0.15f) return TEXT("full sim: nothing saves the front, slides can high-side");
	if (V < 0.45f) return TEXT("mostly sim: the rider barely saves the front");
	if (V < 0.8f) return TEXT("the rider protects the front and smooths the throttle");
	return TEXT("easy: the rider also balances the bike for you");
}

const TArray<FTorqueSettings::FRiderStyle>& FTorqueSettings::RiderStyles()
{
	static const TArray<FRiderStyle> S = {
		{TEXT("situp"), TEXT("Sit-up adventure"), TEXT("Straight back, tall in the saddle, and neutral through the corners.")},
		{TEXT("scratcher"), TEXT("Knee-down scratcher"), TEXT("Tucked in and hanging off: the further over you go, the lower the inside knee.")},
		{TEXT("naked"), TEXT("Elbows-out naked"), TEXT("Leant forward over the tank with the elbows out, and a bit of knee out in the corners.")},
		{TEXT("rossi"), TEXT("Rossi leg out"), TEXT("Braking hard into a corner, the inside leg comes off the peg and dangles forward and out.")},
	};
	return S;
}

FString FTorqueSettings::RiderStyleFor(const APawn* Pawn)
{
	const TArray<FRiderStyle>& Styles = RiderStyles();
	const bool bMine = !Pawn || Pawn->IsLocallyControlled() || Pawn->GetNetMode() == NM_Standalone;
	if (bMine)
	{
		static FString Style;
		static const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("TorqueRiderStyle="), Style);   // (asked every frame)
		if (bAsked)
			for (const FRiderStyle& R : Styles)
				if (Style.Equals(R.Id, ESearchCase::IgnoreCase)) return R.Id;
		return Styles[FMath::Clamp(Get().RiderStyle, 0, Styles.Num() - 1)].Id;
	}
	const ATorquePlayerState* PS = Pawn->GetPlayerState<ATorquePlayerState>();
	return Styles[FMath::Clamp<int32>(PS ? PS->RiderStyle : 0, 0, Styles.Num() - 1)].Id;
}

float FTorqueSettings::ScreenBrightTimes(int32 Step)
{
	static const float Times[4] = {0.3f, 0.55f, 1.0f, 1.45f};
	return Times[FMath::Clamp(Step, 0, 3)];
}

const TCHAR* FTorqueSettings::ScreenBrightName(int32 Step)
{
	static const TCHAR* const Names[4] = {TEXT("dim"), TEXT("low"), TEXT("normal"), TEXT("bright")};
	return Names[FMath::Clamp(Step, 0, 3)];
}
