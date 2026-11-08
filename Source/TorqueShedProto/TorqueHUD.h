// Readable on-screen speed, progress and controls, sized to the screen (the engine's debug text is
// tiny at 4K). The lines come from the bike pawn each frame.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TorqueHUD.generated.h"

UCLASS()
class ATorqueHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	void DrawRiderNames();
};
