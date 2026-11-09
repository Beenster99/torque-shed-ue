// A thin, soft line round what the walker looks at (beenster, Nov 9 2026: "a very subtle outline
// highlight line for each part we are looking at"). What's looked at is drawn into custom depth
// with a stencil, and M_ShedOutline (Tools/ue_build_shed.py), a post-process on the walker's own
// camera, draws a pale line just outside that shape, fainter where something in front hides it.
//
// [co-op] Only the player on this machine is outlined: the marks are this machine's render state,
// and the line is on this player's camera, so each player sees only their own look's outline.
// (Another player's work still glows faintly in their colour, as before.)
#pragma once

#include "CoreMinimal.h"

class UCameraComponent;
class UPrimitiveComponent;
class UWorld;
struct FTorquePlace;

struct FTorqueLookOutline
{
	// The stencil value the outline material looks for (any non-zero stencil draws a line).
	static constexpr int32 Stencil = 1;

	// Puts M_ShedOutline on the camera's post-process, once. False when the level script hasn't
	// made it (no outline then; nothing is marked).
	bool Attach(UCameraComponent* Camera);
	// Outlines exactly these: what was outlined and isn't any more is unmarked.
	void Show(const TArray<UPrimitiveComponent*>& Now);
	void Clear() { Show(TArray<UPrimitiveComponent*>()); }
	int32 Count() const { return Shown.Num(); }
	// The level's own meshes that make up a place (a machine, a bin, the computer, a hook, the
	// radio): the ones mostly inside its box, found once per place. None for a bay's floor.
	const TArray<UPrimitiveComponent*>& PlaceMeshes(UWorld* World, const FTorquePlace* Place);

private:
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Shown;
	const FTorquePlace* CachedPlace = nullptr;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedMeshes;
	TArray<UPrimitiveComponent*> PlaceOut;
	TWeakObjectPtr<UCameraComponent> AttachedTo;
	bool bNoMaterial = false;
};
