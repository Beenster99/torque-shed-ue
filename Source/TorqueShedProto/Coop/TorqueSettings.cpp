#include "TorqueSettings.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
const TCHAR* Section = TEXT("TorqueSettings");

void SetCVar(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, ECVF_SetByGameSetting);
}
void SetCVar(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, ECVF_SetByGameSetting);
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
	GConfig->GetFloat(Section, TEXT("Master"), Master, Ini);
	GConfig->GetFloat(Section, TEXT("Engine"), Engine, Ini);
	GConfig->GetFloat(Section, TEXT("Ambience"), Ambience, Ini);
	GConfig->GetFloat(Section, TEXT("MouseLook"), MouseLook, Ini);
	GConfig->GetFloat(Section, TEXT("PadLook"), PadLook, Ini);
	GConfig->GetBool(Section, TEXT("InvertLook"), bInvertLook, Ini);
	GConfig->GetFloat(Section, TEXT("StickDeadZone"), StickDeadZone, Ini);
	GConfig->GetFloat(Section, TEXT("Steering"), Steering, Ini);
	GConfig->GetString(Section, TEXT("Name"), Name, Ini);
	GConfig->GetInt(Section, TEXT("Colour"), Colour, Ini);
	Quality = FMath::Clamp(Quality, 0, 4);
	WindowMode = FMath::Clamp(WindowMode, 0, 2);
	UpscaleMode = FMath::Clamp(UpscaleMode, 0, 4);
	Colour = FMath::Clamp(Colour, 0, Colours().Num() - 1);
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
	GConfig->SetFloat(Section, TEXT("Master"), Master, Ini);
	GConfig->SetFloat(Section, TEXT("Engine"), Engine, Ini);
	GConfig->SetFloat(Section, TEXT("Ambience"), Ambience, Ini);
	GConfig->SetFloat(Section, TEXT("MouseLook"), MouseLook, Ini);
	GConfig->SetFloat(Section, TEXT("PadLook"), PadLook, Ini);
	GConfig->SetBool(Section, TEXT("InvertLook"), bInvertLook, Ini);
	GConfig->SetFloat(Section, TEXT("StickDeadZone"), StickDeadZone, Ini);
	GConfig->SetFloat(Section, TEXT("Steering"), Steering, Ini);
	GConfig->SetString(Section, TEXT("Name"), *Name, Ini);
	GConfig->SetInt(Section, TEXT("Colour"), Colour, Ini);
	GConfig->Flush(false, Ini);
}

float FTorqueSettings::ScreenPercent(int32 Mode)
{
	static const float Pct[5] = {100.0f, 66.7f, 58.0f, 50.0f, 33.3f};
	return Pct[FMath::Clamp(Mode, 0, 4)];
}

bool FTorqueSettings::HasDlss()
{
	// NVIDIA's DLSS plugin (free, from Fab) registers these when it's in the build and the GPU can.
	return IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")) != nullptr;
}

void FTorqueSettings::Apply(UWorld* World) const
{
	if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		G->SetOverallScalabilityLevel(Quality);
		G->SetFullscreenMode(WindowMode == 0 ? EWindowMode::Fullscreen : WindowMode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		const FIntPoint Res = (Resolution.X > 0 && Resolution.Y > 0) ? Resolution : G->GetDesktopResolution();
		G->SetScreenResolution(Res);
		G->SetFrameRateLimit((float)FrameCap);
		G->SetVSyncEnabled(bVSync);
		G->ApplySettings(false);
	}
	const bool bDlss = Upscaler == 1 && HasDlss();
	SetCVar(TEXT("r.AntiAliasingMethod"), 4);   // TSR (DLSS takes over from it when on)
	SetCVar(TEXT("r.ScreenPercentage"), ScreenPercent(UpscaleMode));
	if (HasDlss())
	{
		SetCVar(TEXT("r.NGX.DLSS.Enable"), bDlss ? 1 : 0);
		// The plugin's own quality setting: 2 ultra quality .. -2 ultra performance.
		static const int32 DlssQuality[5] = {2, 1, 0, -1, -2};
		SetCVar(TEXT("r.NGX.DLSS.Quality"), DlssQuality[FMath::Clamp(UpscaleMode, 0, 4)]);
		SetCVar(TEXT("r.NGX.DLAA.Enable"), bDlss && UpscaleMode == 0 ? 1 : 0);
	}
	SetCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 4 : 0);
	if (World)
	{
		if (FAudioDeviceHandle Audio = World->GetAudioDevice()) Audio->SetTransientPrimaryVolume(FMath::Clamp(Master, 0.0f, 1.0f));
	}
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
