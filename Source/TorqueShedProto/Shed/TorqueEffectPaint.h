// Effect paints on parts (Torque Shed's paint_mix.gd spec): a part sprayed from the mixing bay with
// a pearl, candy, flake or chameleon wears M_ShedEffectPaint (Tools/ue_build_shed.py) in place of
// its own material, with the spec's flip colours, pearl sheen, candy depth and flakes. Solid,
// metallic, chrome and lacquer paints keep the part's own material, as before.
//
// Used by ATorqueShedItems (parts in the shed) and TorqueShedBike::ApplyLooks (parts on a bike).
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorquePaintMix.h"

class UMeshComponent;

namespace TorqueEffectPaint
{
// Puts the effect paint on C's first material, keeping what was there to put back. False (and C
// left alone) when the material isn't in the project yet.
bool Apply(UMeshComponent* C, const TorquePaintMix::FPaintSpec& S, float Grime);
// Puts back C's own material if it's wearing the effect paint (the part repainted solid).
void Restore(UMeshComponent* C);
}
