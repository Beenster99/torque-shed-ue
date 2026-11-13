// The cloud guard (round 141): our own cloud material (M_RideCloud) must be a working Volume material,
// or the engine stops on an assert as soon as it draws the sky. Before the first frame of every game
// world this checks the level's Volumetric Cloud: if its material isn't Volume domain, or didn't compile
// (no shaders for this machine), it puts the engine's own cloud back, and hides the cloud if that won't
// do either. -TorqueEngineCloud puts the engine's cloud back whatever (to compare, or to be safe).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TorqueCloudGuard.generated.h"

namespace TorqueCloudGuard
{
// Check the world's clouds (again: it does nothing once a cloud has been looked at). True when the cloud
// left showing wears our own material.
bool Check(UWorld* World);
}

UCLASS()
class UTorqueCloudGuard : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldComponentsUpdated(UWorld& World) override;
	virtual void OnWorldBeginPlay(UWorld& World) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
