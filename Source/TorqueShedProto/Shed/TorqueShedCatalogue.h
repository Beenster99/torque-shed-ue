// A bike of catalogue parts (any of Torque Shed's styles), drawn as the Godot game's BikeModel does:
// fitting's layout solver (Fit/BikeLayout) lays the bike out from its parts, Fit/BikeParts places
// each part's Blender glb (Data/Parts/Blender) on it and bends it to the bike's lines, and each
// surface becomes a static mesh built at run time, tagged with its Godot slot (Slot:<slot>, and
// Part:<glb>) like the Speed Twin's parts, so the walker points at it and ApplyLooks dirties it.
// Slots fitting hasn't ported yet (fairings, luggage, stands...) aren't drawn.
#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMeshComponent;

namespace TorqueFit
{
class BikeLayout;
}

namespace TorqueShedCatalogue
{
// Fitting's layout solver, loaded once on Data/Parts/parts.json and Data/Fitment/layout.json
// (null if it won't load).
const TorqueFit::BikeLayout* Layout();

// Draws the bike with these parts (Godot slot -> catalogue id, "none" for empty) under Parent, in
// the bike's frame (x forward, z up, cm, the origin at the rear axle's height in the middle of the
// wheelbase). OutAxleHeight: how high the origin is off the ground (cm). Returns the meshes made.
int32 MakeBike(AActor* Owner, USceneComponent* Parent, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out, double& OutAxleHeight);

// Tagged on a surface that isn't paint (chrome, rubber...): ApplyLooks leaves its colour alone.
extern const TCHAR* const NoPaintTag;
}
