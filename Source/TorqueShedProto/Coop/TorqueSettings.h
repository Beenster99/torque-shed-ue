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
	int32 Upscaler = 1;         // 0 TSR (Unreal's), 1 DLSS (TSR when NVIDIA's plugin is missing)
	int32 UpscaleMode = 1;      // 0 native, 1 quality, 2 balanced, 3 performance, 4 ultra performance
	int32 FrameCap = 0;         // frames per second, 0 for no cap
	bool bVSync = false;
	bool bMotionBlur = true;
	// Torque Shed's Graphics tab, the rest of it (graphics_tab.gd), as Unreal does each.
	int32 Monitor = -1;         // the screen the game is on, -1 wherever it opens
	float Brightness = 1.0f;    // 0.5 .. 1.5 (the display gamma, 2.2 at 1)
	float Sharpness = 0.0f;     // 0 .. 1 (Unreal's tonemapper sharpen)
	int32 AntiAliasing = 0;     // 0 TSR, 1 TAA, 2 FXAA, 3 off (DLSS does its own when it's on)
	// Unreal's scalability groups, each -1 for the quality preset's or 0 low .. 4 cinematic.
	int32 Shadows = -1, Textures = -1, GlobalIllumination = -1, Reflections = -1, Effects = -1, Foliage = -1;
	float DrawDistance = 1.0f;  // 0.5 .. 2, times the preset's
	int32 Fog = -1;             // volumetric fog and light: -1 the preset's, 0 off, 1 low, 2 high
	int32 Clouds = -1;          // -1 the preset's, 0 off, 1 low .. 4 ultra
	bool bAmbientOcclusion = true;
	float Bloom = 1.0f;         // glow and bloom: 0 off .. 2, times the level's own
	bool bFpsCounter = false;   // the frame rate at the top of the screen
	bool bAvatarShadows = true; // people on foot cast shadows
	// The rest of a PC game's graphics page.
	int32 PostProcess = -1, Shading = -1;   // Unreal's post-processing and shading groups, as the others
	int32 Anisotropy = -1;      // texture filtering: -1 the preset's, 0 bilinear (1x), 1 2x, 2 4x, 3 8x, 4 16x
	bool bHdr = false;          // HDR output, on a screen that has it
	float FilmGrain = 1.0f;     // 0 off .. 2, times the level's own
	float Vignette = 1.0f;      // 0 off .. 2, times the level's own darkening at the corners
	bool bLensFlares = true, bChromaticAberration = true, bDepthOfField = true;
	static bool HdrAvailable();

	// Sound, 0 .. 1: everything, the sound effects (tools, knocks, the shed's machines), the engines,
	// the world around (wind, birds, the shed's hum) and the shed radio.
	float Master = 1.0f, Sfx = 1.0f, Engine = 1.0f, Ambience = 1.0f, Radio = 1.0f;

	// Gameplay.
	float Realism = 0.6f;       // riding realism: 0 full sim .. 1 easy (Torque Shed's slider)
	bool bOpenRouter = true;    // hosting asks the router to open the port (UPnP), as Torque Shed always did
	float Fov = 80.0f;          // field of view, degrees: on foot as it is, the bike's cameras moved by as much
	bool bAutoShift = true;     // the gearbox changes itself (G / R3 while riding)
	bool bTelemetry = false;    // live strips of what the bike is doing, while riding (F3)
	bool bLookIntoCorner = true;    // the rider's head, and a first-person view, turn into a bend
	bool bCameraRecentre = true;    // the chase camera swings back behind the bike after you look round
	int32 Units = 1;            // 0 metric (km/h, km), 1 imperial (mph, miles)
	// Time and weather (the host's rule in co-op; ATorqueGameState).
	bool bDayNight = false;     // the clock runs
	int32 DayLength = 120;      // real minutes for a whole day
	float TimeOfDay = -1.0f;    // hours, -1 for the level's own light
	// 0 changes by itself, 1 always sunny, 2 always overcast, 3 always wet roads, 4 live (the real
	// weather where you ride; the default, as beenster chose: the host can switch it off any time).
	int32 WeatherMode = 4;
	static constexpr int32 WeatherLive = 4;
	static const TArray<int32>& DayLengths();
	FString SpeedText(double MetresPerSecond) const;   // "52 mph"
	FString DistanceText(double Metres) const;         // "1.25 miles"
	static FString RealismWord(float V);
	static FString RealismText(float V);

	// Controller.
	float MouseLook = 1.0f;     // times the default
	float PadLook = 1.0f;
	bool bInvertLook = false;
	// The PlayStation pad's sticks' centres (Recalibrate, in Controller check): LX, LY, RX, RY.
	float PadCentre[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float StickDeadZone = 0.08f;   // the bike's lean stick (0.03 .. 0.25)
	float Steering = 1.0f;      // how quickly a pushed stick leans the bike, times the default

	// Character.
	FString Name;               // empty: the Windows account name
	int32 Colour = 0;           // index into Colours(): the rider's jacket and the name over them
	int32 RiderStyle = 0;       // index into RiderStyles(): how you sit on the bike (Torque Shed's Rider style)

	// Whether the player has ever saved settings: until then the game keeps the project's defaults
	// (so a fresh install, and test runs, look as before).
	bool bSaved = false;

	static FTorqueSettings& Get();
	void Save();
	// Puts the graphics and sound settings on the game (call after changing them).
	void Apply(UWorld* World) const;
	// The cameras' field of view for one the game set at Base degrees (the walker's 80 is the setting).
	float FovFor(float Base) const { return FMath::Clamp(Base + (Fov - 80.0f), 40.0f, 140.0f); }
	// Avatar shadows off: everyone on foot (and what they carry) stops casting shadows; on, back as they were.
	void ApplyAvatarShadows(UWorld* World) const;

	static const TArray<FColor>& Colours();
	static const TArray<FString>& ColourNames();
	FColor RiderColour() const;
	// Torque Shed's riding styles (rider_style.gd STYLES / INFO), in its Profile tab's order: the id
	// the rider pose reads, the name and the line under it. Only looks: the bike rides the same.
	struct FRiderStyle { const TCHAR* Id; const TCHAR* Name; const TCHAR* Desc; };
	static const TArray<FRiderStyle>& RiderStyles();
	// The style a ridden pawn's rider sits in: this machine's own from Settings (or
	// -TorqueRiderStyle=), another player's from their ATorquePlayerState. An id from RiderStyles().
	static FString RiderStyleFor(const class APawn* Pawn);
	// Screen percentage for the upscaler mode.
	static float ScreenPercent(int32 Mode);
	static bool HasDlss();
	// The screens (Torque Shed's Monitor row): how many, and one's name for the menu.
	static int32 MonitorCount();
	static FString MonitorName(int32 Index);
	static void MoveToMonitor(int32 Index);

private:
	void Load();
	bool bLoaded = false;
};
