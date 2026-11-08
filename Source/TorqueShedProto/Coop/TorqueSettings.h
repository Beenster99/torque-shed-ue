// The player's settings (the Settings menu: graphics, sound, controller, character), kept in
// Saved/Config/<platform>/GameUserSettings.ini under [TorqueSettings], with the window and
// scalability settings in Unreal's own UGameUserSettings beside them.
//
// One per game, read anywhere with FTorqueSettings::Get().
#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FTorqueSettings
{
	// Graphics.
	int32 Quality = 3;          // Unreal's scalability: 0 low, 1 medium, 2 high, 3 epic, 4 cinematic
	int32 WindowMode = 0;       // 0 fullscreen, 1 borderless window, 2 window
	FIntPoint Resolution = FIntPoint(0, 0);   // 0 x 0: the desktop's
	int32 Upscaler = 0;         // 0 TSR (Unreal's), 1 DLSS (when NVIDIA's plugin is in the build)
	int32 UpscaleMode = 1;      // 0 native, 1 quality, 2 balanced, 3 performance, 4 ultra performance
	int32 FrameCap = 0;         // frames per second, 0 for no cap
	bool bVSync = false;
	bool bMotionBlur = true;

	// Sound, 0 .. 1.
	float Master = 1.0f, Engine = 1.0f, Ambience = 1.0f;

	// Controller.
	float MouseLook = 1.0f;     // times the default
	float PadLook = 1.0f;
	bool bInvertLook = false;
	float StickDeadZone = 0.08f;   // the bike's lean stick (0.03 .. 0.25)
	float Steering = 1.0f;      // how quickly a pushed stick leans the bike, times the default

	// Character.
	FString Name;               // empty: the Windows account name
	int32 Colour = 0;           // index into Colours(): the rider's jacket and the name over them

	// Whether the player has ever saved settings: until then the game keeps the project's defaults
	// (so a fresh install, and test runs, look as before).
	bool bSaved = false;

	static FTorqueSettings& Get();
	void Save();
	// Puts the graphics and sound settings on the game (call after changing them).
	void Apply(UWorld* World) const;

	static const TArray<FColor>& Colours();
	static const TArray<FString>& ColourNames();
	FColor RiderColour() const;
	// Screen percentage for the upscaler mode.
	static float ScreenPercent(int32 Mode);
	static bool HasDlss();

private:
	void Load();
	bool bLoaded = false;
};
