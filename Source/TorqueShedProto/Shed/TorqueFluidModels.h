// [fluids] The cans and bottles fluids come in, drawn so you can see how full they are: plastic jerry
// cans and race fuel jugs, oil bottles with a clear level strip down the side of the label, brake
// fluid bottles, the clear bleed bottle, the oil drain pan, and the chain lube's aerosol (steel: its
// level is in its info line). Built at run time from lathed and lofted shapes (no imported models),
// in the parts' colours, no makers' names on them.
//
// The plastic is see-through (M_FluidShell) and the fluid inside is a solid cut off flat at its
// surface (M_FluidLiquid): the game tips that surface as the can's carried about, swung or put down,
// a damped slosh that settles, and moves it up and down with the level (ATorqueShedItems::Tick). Until
// Tools/ue_build_fluids.py has made the two materials, the cans are drawn opaque.
#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace TorqueFluidModels
{
// A drawn can or bottle's fluid: its liquid mesh and material, how high the surface stands for each
// fill, and its slosh.
struct FVessel
{
	TWeakObjectPtr<UStaticMeshComponent> Liquid;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	TArray<float> HeightAt;                // the surface's height (cm, the vessel's frame) at fill 0, 1/32 .. 1
	FVector2D Axis = FVector2D::ZeroVector; // the liquid's middle across (x, y), the vessel's frame
	double Freq = 1.6;                     // how fast it sloshes (Hz): a big can slower than a bottle
	// The surface's slope (world x, y: how far it rises a centimetre along each) and how fast it's
	// changing; the vessel's last place and speed (to feel it accelerate).
	FVector2D Tilt = FVector2D::ZeroVector, TiltRate = FVector2D::ZeroVector;
	FVector LastPos = FVector::ZeroVector, LastVel = FVector::ZeroVector;
	bool bStarted = false;
	float Shown = -1.0f;                   // the fill drawn (eased towards what's in it as it pours)
	double Clock = 0.0;
};

// Builds Part's can or bottle (a fluid's part id: "semi_10w40", "drain_pan") under Holder, its bottom
// centre at the holder's origin, x its front, z up. Made gets every mesh. False for any other part.
bool Make(AActor* Owner, USceneComponent* Holder, FName Part, TArray<UStaticMeshComponent*>& Made, FVessel& Out);

// Each frame: how full it is (0 .. 1), the fluid's own colour and age (old oil goes black), the frame time.
void Update(FVessel& V, float Fill, const FLinearColor& Colour, float Age, float Dt);

// The fluid's colour as it shows: its own (parts.json "liquid"), darkened as it ages.
FLinearColor AgedColour(FName Kind, const FLinearColor& Colour, float Age);
}
