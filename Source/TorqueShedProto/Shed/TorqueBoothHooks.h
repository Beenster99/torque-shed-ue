// The spray bay's hooks on their wires (equipment_view.gd add_hook_wire), each at the height it's
// been set to (the shed's: UTorqueBuildSubsystem::HookTipOf, so every player sees the same), and
// swinging like a pendulum from its mount on the rail: a part hung on or lifted off sets it going, a
// coat of paint nudges it, someone walking into it pushes it, and the air slowly stills it. The part
// on a hook swings with it (ATorqueShedItems asks Swing).
//
// The rails, their drop rods and the hook mounts are the level's (Tools/ue_build_shed.py paint_shop).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueBoothHooks.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class ATorqueBoothHooks : public AActor
{
	GENERATED_BODY()

public:
	ATorqueBoothHooks();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Hook n's mount on the rail (the pendulum's pivot) and how far it's swung now. False without
	// the hooks (the turn is then none).
	static bool Swing(const UObject* WorldContext, int32 Hook, FVector& OutPivot, FQuat& OutTurn);
	// A push on hook n: a swing speed in rad/s about the world's X and Y.
	void Push(int32 Hook, const FVector2D& Rate);
	// Where hook n's mount is (ue_build_shed.py BOOTH_RAIL_Z, the hanger under it).
	static FVector PivotOf(int32 Hook);

private:
	struct FHook
	{
		FVector2D Angle = FVector2D::ZeroVector;   // about X and Y, radians
		FVector2D Rate = FVector2D::ZeroVector;
		FString Sig;                               // what hangs on it, and its paint: a change sets it going
		float Shown = -1.0f;                       // the tip height drawn
	};
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) TArray<USceneComponent*> Swings;     // one per hook, at its pivot
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Wires;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Tips;
	TArray<FHook> Hooks;
	FQuat TurnOf(int32 Hook) const;
};
