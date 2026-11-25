// The 3D preview windows' photo studio (see Shed/TorqueStudioRig.h for the rig's numbers and why).
// Every preview (ATorqueShopPreview: the parts computer and the new-bike configurator; ATorqueProfilePreview:
// the profile page, the badge and the helmet decals) films its subject with these, so it looks the same
// at noon or midnight, in sun or rain, in the shed or out on a ride, at any Brightness or graphics setting:
//  - Isolate: the capture's post-processing is entirely its own (every setting overridden, so nothing of
//    the level's post-process volumes gets through: not its exposure, local exposure, grading, grain or
//    outline material), fixed manual exposure, and none of the level's sky light, fog, sky or clouds.
//  - MakeLights / PlaceLights: the soft boxes, on the studio's own lighting channel.
//  - Enrol: puts everything shown on that channel (the level's sun and lamps can't reach it, and only the
//    studio's lights reach it).
//  - FMeter: reads the backdrop now and then and trims the exposure to hold it at its grey, so whatever
//    else changes in the renderer the studio stays a studio (the trim is logged: it should stay near 0).
// SimTest/studio_test.cpp checks every scene capture in the game is either one of these or on its list of
// captures that are meant to see the game (the TVs' rider cam, the impostor baker).
#pragma once

#include "CoreMinimal.h"

class AActor;
class URectLightComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;

namespace TorqueStudio
{
// The capture's settings: call once from the preview's constructor. ExposureBias in stops (the rig's
// ShopExposureBias or ProfileExposureBias).
void Isolate(USceneCaptureComponent2D* Capture, float ExposureBias);
// Its picture's render target: made the same way for every preview (8-bit sRGB, its own fixed gamma so
// the game's Brightness slider doesn't bend the capture's tone curve).
UTextureRenderTarget2D* MakeTarget(UObject* Outer, int32 W, int32 H);

// The rig's soft boxes, as default subobjects of Owner under Parent (call from the constructor).
void MakeLights(AActor* Owner, USceneComponent* Parent, TArray<URectLightComponent*>& Out);
// Stands them round Centre for a subject of RadiusCm seen by a camera looking along Fwd (Right, Up its axes).
void PlaceLights(const TArray<URectLightComponent*>& Lights, const FVector& Centre, const FVector& Fwd, const FVector& Right,
	const FVector& Up, double RadiusCm);

// Every primitive under Under onto the studio's lighting channel only (cheap when they already are).
void Enrol(USceneComponent* Under);

// Holds the picture's exposure: Tick it every frame the preview is up.
struct FMeter
{
	const TCHAR* Who = TEXT("preview");
	double Target = 0.5;        // the backdrop's top corners, as displayed (0-1)
	float Trim = 0.0f;          // stops added to the base exposure
	// Every frame: bOn when the capture is filming, bMayRead when the backdrop's top corners should be
	// clear of the subject. Puts Base + Trim on the capture.
	void Tick(float DeltaSeconds, bool bOn, bool bMayRead, USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target2D, float Base);
	// The last reading (0-1, -1 before the first), for the test log.
	double LastRead = -1.0;

private:
	bool bWasOn = false;
	bool bSettled = false;
	bool bOwnTrim = false;      // found its own (till then it borrows the last any preview found)
	int32 Reads = 0;
	float Next = 0.0f;
};

// Reads the backdrop's top corners of a picture (the median grey, 0-1 as displayed; -1 if it can't).
double ReadCorners(UTextureRenderTarget2D* Target2D);
} // namespace TorqueStudio
