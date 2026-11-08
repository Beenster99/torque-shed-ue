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

// A loose catalogue part, with what's on it (an engine's own parts, a wheel's tyre): Parts maps each
// slot to its catalogue id, MainSlot the part's own. Drawn as it sits on a bike of its style (that
// bike's frame), with the same tags as a bike's; built once for each such set and kept. Returns
// the meshes made.
int32 MakePart(AActor* Owner, USceneComponent* Parent, const FString& MainSlot, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out);

// Tagged on a surface that isn't paint (chrome, rubber...): ApplyLooks leaves its colour alone.
extern const TCHAR* const NoPaintTag;
}
