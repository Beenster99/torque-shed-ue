// Effect paints and masking on parts (Torque Shed's paint_mix.gd fx pass): a part sprayed from the
// mixing bay with a pearl, candy, flake or chameleon, or with a masked stripe or tape on it, wears
// M_ShedEffectPaint (Tools/ue_build_shed.py) in place of its own material, with the spec's flip
// colours, pearl sheen, candy depth and flakes, and the stripe's pattern and colour. Plain solid,
// metallic, chrome and lacquer paints keep the part's own material, as before.
//
// Used by ATorqueShedItems (parts in the shed) and TorqueShedBike::ApplyLooks (parts on a bike).
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorquePaintMix.h"

class UMeshComponent;

namespace TorqueEffectPaint
{
// Puts the look on C's first material, keeping what was there to put back. LocalToPart: from C's
// mesh to the part's frame (x forward, y right, z up, from the middle of the part's box), where the
// masked stripes go (PartFrame). False (and C left alone) when the material isn't in the project yet.
bool Apply(UMeshComponent* C, const TorquePaintMix::FPaintLook& L, float Grime, const FTransform& LocalToPart);
// Puts back C's own material if it's wearing the effect paint (the part repainted plainly).
void Restore(UMeshComponent* C);
// C's box in Frame (a bike's or a part's own frame, in the world), and from C's mesh to Frame with
// Middle (in Frame) as its origin.
FBox BoxIn(const UMeshComponent* C, const FTransform& Frame);
FTransform PartFrame(const UMeshComponent* C, const FTransform& Frame, const FVector& Middle);
}
