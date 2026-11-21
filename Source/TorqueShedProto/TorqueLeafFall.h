// [build 165] The trees' fallen leaves, near and mid distance. M_Tree (Tools/ue_build_level.py tree_season)
// drops a leaf by moving it into a speck at its middle (World Position Offset), and the game turns that
// offset off past a short distance (the wind's sway, 30 m in Ride UK, 25 m in Buttertubs) to save vertex
// work. In 164 that left every near broadleaf past 30 m with a full crown of dropped leaves painted twig
// dark: "the big broadleaf crowns NEAR the road are still full dark green and lumpy, not bare".
//
// So the components whose leaves fall (our own near broadleaves and the gardens) register here with their
// usual distance; while the season has leaves down (MPC LeafDrop above a trace, set by ATorqueGameState)
// they keep their offset out to torque.TreeLeafFallM (200 m: Ride UK's near trees end there, and past it
// M_Tree paints a dropped leaf in the far woods' winter grey instead). Shadows stay Rigid, so the longer
// reach doesn't redraw shadow pages.
#pragma once

#include "CoreMinimal.h"

class UInstancedStaticMeshComponent;

namespace TorqueLeafFall
{
// Set the component's World Position Offset distance (cm) from its usual one and the season, and keep
// it in step as the season changes (bLeafFall), or forget it (a pooled component reused for something else).
void Apply(UInstancedStaticMeshComponent* Ism, int32 BaseCm, bool bLeafFall);
// The season's leaf drop (MPC LeafDrop) as just set: re-applies the registered components when leaves
// start or stop falling, or the reach changes.
void SetDrop(float Drop);
// Whether a tree model (by its asset name) drops its leaves: our own near broadleaves, not the far
// lumps (M_Tree greys those), not the evergreens.
bool IsLeafyTreeName(const FString& Name);
}
