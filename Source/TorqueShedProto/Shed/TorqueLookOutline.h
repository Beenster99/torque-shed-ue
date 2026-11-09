// A thin, soft line round what the walker looks at (beenster, Nov 9 2026: "a very subtle outline
// highlight line for each part we are looking at"). What's looked at is drawn into custom depth
// with a stencil, and M_ShedOutline (Tools/ue_build_shed.py), a post-process that only the local
// walker carries, draws a pale line just outside that shape, fainter where something in front hides it.
//
// [co-op] Only the player on this machine is outlined: the marks are this machine's render state,
// and only this player's walker carries the post-process, so each player sees only their own look's outline.
// (Another player's work still glows faintly in their colour, as before.)
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueShedLayout.h"

class AActor;
class UPostProcessComponent;
class UPrimitiveComponent;
class UWorld;

struct FTorqueLookOutline
{
	// The stencil value the outline material looks for (any non-zero stencil draws a line).
	static constexpr int32 Stencil = 1;

	// Puts M_ShedOutline on a post-process of its own on Owner (unbound, full weight), once. Not on
	// the walker's camera: its weight is the white rooms' exposure blend, 0 in the main shed, which
	// hid the line there in builds 101-103. False when the level script hasn't made the material
	// (no outline then; nothing is marked).
	bool Attach(AActor* Owner);
	// Outlines exactly these: what was outlined and isn't any more is unmarked.
	void Show(const TArray<UPrimitiveComponent*>& Now);
	void Clear() { Show(TArray<UPrimitiveComponent*>()); }
	int32 Count() const { return Shown.Num(); }
	// Places whose own meshes are things that move (the engine stand, the extinguisher, the tape
	// roll, a paint tin): the only places outlined. Fixed kit isn't.
	static bool Movable(ETorquePlace Kind);
	// The level's own meshes that make up a place (a machine, a bin, the computer, a hook, the
	// radio): the ones mostly inside its box, found once per place. None for a bay's floor.
	const TArray<UPrimitiveComponent*>& PlaceMeshes(UWorld* World, const FTorquePlace* Place);

private:
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Shown;
	const FTorquePlace* CachedPlace = nullptr;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedMeshes;
	TArray<UPrimitiveComponent*> PlaceOut;
	TWeakObjectPtr<UPostProcessComponent> Post;
	bool bNoMaterial = false;
};
