// The tank's badge on a drawn bike (the Godot game's BikeBadge.stamp): its logo and brand name, each
// in its own colour, on both sides of the tank, each a thin skin cut from the tank's own surface
// and lifted a hair off it (Fit/BikeBadge, checked against Godot by SimTest/badge_test), wearing
// M_ShedBadge (Tools/ue_build_shed.py) with its picture: the logo drawn from its shapes, the brand
// name lettered in its font (Data/Fonts/Badge, by the engine's own text drawing).
//
// Used by TorqueShedModel::ApplyLooks (the bikes in the shed and the ridden bike) and the shop's
// preview (the New bike configurator's bike).
#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UObject;
class UStaticMeshComponent;
class UTexture;

namespace TorqueBikeBadge
{
// Stamps Badge (Fit/BikeShop's badge text, "" for none) on the tank whose surfaces are Tank (a
// drawn bike's Slot:tank meshes), in place of what was stamped there before. Does nothing when
// it's the same badge already, and takes it off for "". Each call also clears away the badges of
// tanks that have gone (a bike drawn again), so call it with no tank too.
void Stamp(const TArray<UStaticMeshComponent*>& Tank, const FString& Badge);
// The tank's surfaces among a drawn bike's meshes (tagged Slot:tank).
TArray<UStaticMeshComponent*> TankOf(const TArray<UStaticMeshComponent*>& Parts);
// Logo Id's picture (white where the logo is, its alpha the same), for the configurator's list;
// null for none.
UTexture* Logo(const FString& Id);
// Text in lettering FontId (BikeBadge.FONTS, as the brand's), Px tall, centred in Box on Canvas
// (the configurator's lettering buttons). False if it can't be (the font's file missing).
bool DrawLettering(UCanvas* Canvas, const FBox2D& Box, const FString& Text, const FString& FontId, float Px, const FLinearColor& Colour);
}
