// Readable on-screen speed, progress and controls, sized to the screen (the engine's debug text is
// tiny at 4K). The lines come from the bike pawn each frame.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TorqueBikePawn.h"   // FHudLine
#include "TorqueHUD.generated.h"

UCLASS()
class ATorqueHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	void DrawRiderNames();
	// On foot in the shed: Torque Shed's crosshair and work ring, tool bar, spray picker and carrying line.
	void DrawWalkerCues(const class ATorqueWalker* Walker, float S);
	// [shed] On foot: hud.gd's look panel (roles 1-3 of the lines) and the message across the top (4).
	void DrawLookPanel(const TArray<FHudLine>& Lines, float S);
	float NameLogClock = 2.0f;
	// Settings: the frame rate at the top of the screen, and the telemetry overlay's strips.
	void DrawFrameRate();
	void DrawTelemetry(const ATorqueBikePawn* Bike, float S);
	float FpsTime = 0.0f, FpsShown = 0.0f;
	int32 FpsFrames = 0;
};
