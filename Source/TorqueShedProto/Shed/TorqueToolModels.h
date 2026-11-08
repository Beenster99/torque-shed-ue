// The tool in your hand, as Torque Shed's ToolModels (tool_models.gd) draws it: a chrome
// combination spanner, a wooden wire brush, a blue angle grinder, an aerosol can in the spray
// colour and the pressure washer's gun and lance, built from the same primitives at the same sizes
// (boxes, cylinders, spheres, capsules, tori and ring segments), each material's pieces one mesh.
// Bare hands draw nothing. The walker holds it at Torque Shed's spot in front of the camera.
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

// Torque Shed's hold of a tool in front of the camera (walker.gd _set_tool_view), as a transform
// relative to the camera in Unreal's frame.
FTransform HoldOf(int32 Tool);

// One of the bike's finishes as a plain colour (the bike master's parameters), for sparks and
// spray: Emissive > 0 glows.
UMaterialInstanceDynamic* Finish(UObject* Outer, const FLinearColor& Colour, float Metallic, float Roughness, float Emissive = 0.0f);
}
