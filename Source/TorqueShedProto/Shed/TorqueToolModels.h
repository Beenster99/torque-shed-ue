// The tool in your hand: a chrome combination spanner, a wooden wire brush, a blue angle grinder,
// an aerosol can in the spray colour, the gravity-feed spray gun and the pressure washer's gun and
// lance. Bare hands draw nothing.
//
// [hands] Detailed models when Data/Tools has them (Tools/props/make_hand_tools.py: <id>.tmesh, read
// at run time, and tools.json: their material slots, nozzle, first-person hold and where each hand
// grips them), the shed props' materials (MI_Prop_<key>) on them where those are in. Without the
// data, Torque Shed's ToolModels (tool_models.gd), built from the same primitives at the same sizes
// (boxes, cylinders, spheres, capsules, tori and ring segments), held at Torque Shed's spot.
#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace TorqueToolModels
{
// The tools, as the walker's ETorqueTool: hands, wrench, brush, repair (the grinder), spray, lance, gun.
enum ETool : int32 { Hands = 0, Wrench, Brush, Repair, Spray, Lance, Gun };

// Builds the tool under Parent (in Unreal's frame, cm: x forward, z up), the can tinted CanColour.
// OutNozzle: where the work comes off it (the grinder's disc, the can's nozzle, the lance's tip),
// in Parent's frame. Returns the meshes made (none for hands).
int32 Make(AActor* Owner, USceneComponent* Parent, int32 Tool, const FLinearColor& CanColour,
	TArray<UStaticMeshComponent*>& Out, FVector& OutNozzle);

// How the tool is held in front of the camera, as a transform relative to the camera in Unreal's
// frame: tools.json's first-person hold for a detailed model (its origin is the right hand's grip),
// else Torque Shed's (walker.gd _set_tool_view).
FTransform HoldOf(int32 Tool);

// [hands] Where a hand holds a tool (cm, the tool's frame): the bar through the fist, from the
// little finger to the index (Axis), the way the knuckles point (Aim), the bar's radius; Trigger:
// fingers on a trigger (from the index); bIndexTop: the index up on a spray head.
struct FGrip
{
	bool bOn = false;
	FVector At = FVector::ZeroVector, Axis = FVector::UpVector, Aim = FVector::ForwardVector;
	double Radius = 1.5;
	int32 Trigger = 0;
	bool bIndexTop = false;
};
// Hand 0 the left, 1 the right. False (no grip) for bare hands, the old models, or no such hand.
bool GripOf(int32 Tool, int32 Hand, FGrip& Out);
// Builds every detailed tool's meshes now (at the start, so taking a tool up doesn't hitch).
void Warm();
// Whether the detailed model (Data/Tools) is the one drawn for this tool.
bool HasModel(int32 Tool);

// One of the bike's finishes as a plain colour (the bike master's parameters), for sparks and
// spray: Emissive > 0 glows.
UMaterialInstanceDynamic* Finish(UObject* Outer, const FLinearColor& Colour, float Metallic, float Roughness, float Emissive = 0.0f);
}
