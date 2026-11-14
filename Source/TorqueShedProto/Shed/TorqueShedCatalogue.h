// A bike of catalogue parts (any of Torque Shed's styles), drawn as the Godot game's BikeModel does:
// fitting's layout solver (Fit/BikeLayout) lays the bike out from its parts, Fit/BikeParts places
// each part's Blender glb (Data/Parts/Blender) on it and bends it to the bike's lines, and each
// surface becomes a static mesh built at run time, tagged with its Godot slot (Slot:<slot>, and
// Part:<glb>) like the Speed Twin's parts, so the walker points at it and ApplyLooks dirties it.
// Slots fitting hasn't ported yet (fairings, luggage, stands...) aren't drawn.
#pragma once

#include "CoreMinimal.h"

#include <vector>

class AActor;
class USceneComponent;
class UStaticMeshComponent;

namespace TorqueFit
{
class BikeLayout;
struct FittedPart;
struct PartMesh;
}

namespace TorqueShedCatalogue
{
// Fitting's layout solver, loaded once on Data/Parts/parts.json and Data/Fitment/layout.json
// (null if it won't load).
const TorqueFit::BikeLayout* Layout();

// Draws the bike with these parts (Godot slot -> catalogue id, "none" for empty) under Parent, in
// the bike's frame (x forward, z up, cm, the origin at the rear axle's height in the middle of the
// wheelbase). OutAxleHeight: how high the origin is off the ground (cm). Returns the meshes made.
// Geo: the bike's fit from geometry mode (Fit/BikeGeometry text): its rig's range handles lay the
// bike out (the slots' free offsets are the caller's, FTorqueBikeFitView).
// OutNodes: for each mesh made, what it moves with as the bike rides (FittedPart.Node: None the
// bike itself, steer, front_hub, front_wheel, rear_hub, rear_wheel, swingarm). OutFrame: where the
// laid-out bike's axles, steering head and swingarm pivot are, in the same frame.
struct FBikeFrame
{
	bool bValid = false;
	FVector FrontAxle = FVector::ZeroVector, RearAxle = FVector::ZeroVector;
	FVector SteerPoint = FVector::ZeroVector, SteerDir = FVector::UpVector;   // a point on the steering axis, and up it
	FVector SwingPivot = FVector::ZeroVector;
	FVector Seat = FVector::ZeroVector;   // where the rider's hips sit on the seat (BikeRig's hip)
	FVector Peg = FVector::ZeroVector;    // the right footpeg (BikeRig's peg; the left is its mirror)
	FVector Grip = FVector::ZeroVector;   // the right grip as BikeRig has it (the drawn bars' may differ)
	bool bLaidBack = false;               // cruiser or chopper seating (bike_model.gd _laid_back)
	// [rider pose] The right knee as BikeRig places it (the tank's knee pads are drawn there; the
	// left is its mirror), and the tank's half-width along it: (x, half-width) pairs, front to back,
	// pawn cm, so a rider's knees stay outside its flanks.
	FVector Knee = FVector::ZeroVector;
	TArray<FVector2D> TankHalfW;
	double BackDeg = -100.0;              // [rider pose] the style's back lean, hips to neck, degrees forward of upright (beenster's marks; < -90: none)
};
// [dash] OutGauges: the clocks' working pieces among the meshes made (FittedPart::Gauge), for the
// ridden bike to drive: in the meshes' own space (the bike's frame, cm), as the meshes are made.
struct FDashGauge
{
	FName What;                   // speed, rpm, led0 .. led5, readout
	int32 Mesh = INDEX_NONE;      // which of Out
	FVector At = FVector::ZeroVector, Axis = FVector::UpVector, Up = FVector::ForwardVector;
};
// bOnlyFitted: leave out what the layout puts on for a slot that's empty ("none"), so a bare frame
// in the shed is only the frame (no bars, tyres, sprockets...) until those are fitted.
int32 MakeBike(AActor* Owner, USceneComponent* Parent, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out, double& OutAxleHeight, const FString& Geo = FString(),
	TArray<FName>* OutNodes = nullptr, FBikeFrame* OutFrame = nullptr, TArray<FDashGauge>* OutGauges = nullptr,
	bool bOnlyFitted = false);

// The same bike's parts as fitting has them, not drawn: each part placed and its mesh bent onto the
// bike (fitting's frame, metres), what's hidden left out. For geometry mode's gaps. False if the
// catalogue won't load.
bool DrawnParts(const TMap<FString, FString>& Parts, const FString& Geo, std::vector<TorqueFit::FittedPart>& OutParts,
	std::vector<TorqueFit::PartMesh>& OutMeshes);

// A loose catalogue part, with what's on it (an engine's own parts, a wheel's tyre): Parts maps each
// slot to its catalogue id, MainSlot the part's own. Drawn as it sits on a bike of its style (that
// bike's frame), with the same tags as a bike's; built once for each such set and kept. Returns
// the meshes made.
int32 MakePart(AActor* Owner, USceneComponent* Parent, const FString& MainSlot, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out);

// [bikes v2] A whole Bike Models V2 bike for looking at (BikesV2/README.md): Data/BikesV2/<Model>.glb
// (one mesh, a primitive per surface, fitting's frame in metres with the origin on the ground under
// the rear axle) drawn under Parent in the bike's cm, each surface on the existing bike material
// that Data/BikesV2/<Model>.json names for it, with that file's colour, metallic and roughness.
// Nothing fits, rides or saves it yet. Returns the meshes made (0 and a log line if a file is missing).
int32 MakeV2Display(AActor* Owner, USceneComponent* Parent, const FString& Model, TArray<UStaticMeshComponent*>& Out);

// Tagged on a surface that isn't paint (chrome, rubber...): ApplyLooks leaves its colour alone.
extern const TCHAR* const NoPaintTag;

// Tagged on a surface that turns with a wheel (the tyre, the rim, a disc): its road dirt lies evenly
// round the wheel (ApplyLooks), not caked on whichever side was at the bottom when it was drawn.
extern const TCHAR* const SpinsTag;
}
