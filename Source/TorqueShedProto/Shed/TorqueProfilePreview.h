// Your own MetaHuman and helmet for the Profile page (Shed/TorqueProfile.h): built from your profile,
// standing high above the level in a little photo studio of its own (as the shop's preview,
// ATorqueShopPreview: its own lights on lighting channel 2, the level's sun and sky kept out, a fixed
// exposure, a grey backdrop), filmed by a scene capture that sees only it. The pause menu draws the
// picture: a portrait on the profile badge, and beside the Profile page's rows the whole figure, the
// face, or your helmet on its own in your paint.
//
// Worked with the mouse as the shop's preview is (build 155, beenster): drag to turn it (and tip
// the view up or down), the wheel zooms, right-click stops or starts the turning.
//
// One per local player, made the first time the menu wants it and kept (hidden, not filming) after.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueProfile.h"
#include "Shed/TorqueStudio.h"
#include "TorqueProfilePreview.generated.h"

class APlayerController;
class UCanvas;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class URectLightComponent;

UCLASS(NotPlaceable, Transient)
class ATorqueProfilePreview : public AActor
{
	GENERATED_BODY()

public:
	ATorqueProfilePreview();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	enum class EView : uint8 { Body, Face, Helmet };

	// The local player's preview, made if needed (nullptr when there are no avatars in this build).
	static ATorqueProfilePreview* For(APlayerController* PC);
	// Film this frame (and the next while it's wanted): the whole figure, the head, or the helmet.
	void Want(EView View, int32 Colour);
	void Want(bool bFace, int32 Colour) { Want(bFace ? EView::Face : EView::Body, Colour); }
	// The picture, drawn into the canvas box; nothing when there's no avatar. bPortrait: the middle,
	// square (the badge).
	void Draw(UCanvas* Canvas, float X, float Y, float W, float H, bool bPortrait) const;
	// The big picture on the Profile page, which the mouse works (Press / Hold / Wheel / RightPress).
	void DrawWorked(UCanvas* Canvas, float X, float Y, float W, float H);
	bool Press(const FVector2D& Mouse);             // true when it was on the picture (a drag starts)
	void Hold(const FVector2D& Mouse, bool bLeftDown);
	bool Wheel(const FVector2D& Mouse, int32 Dir);  // Dir +1 (down) zooms out, -1 in; true when over it
	bool RightPress(const FVector2D& Mouse);        // stops or starts the turning; true when over it
	bool IsDragging() const { return bDrag; }

	// [helmet decals] Settings > Profile > Helmet decals (ATorqueDecalMode::OpenHelmet): the helmet on
	// its own, worked by the decal editor. The helmet (made and got ready to cut decals from; null if
	// there's none), its look lit again after new decal skins, the view turned and zoomed by the editor,
	// and the ray under the mouse through the picture (and a point back onto it) as last drawn.
	UStaticMeshComponent* EditHelmet();
	void Relit() { LitCount = -1; StudioLit(); }
	void TurnBy(double DYaw, double DPitch);
	void ZoomBy(int32 Dir);
	void SetSpin(bool bOn) { bSpin = bOn; }
	bool ScreenRay(const FVector2D& Mouse, FVector& OutFrom, FVector& OutDir) const;
	bool ToScreen(const FVector& World, FVector2D& Out) const;
	const FBox2D& Picture() const { return PictureBox; }
	// The decals the preview last drew from the saved helmet's (so it draws them again after an edit).
	void ForgetDecals() { HelmetDecalsOn = TEXT("\x1"); }

	bool HasAvatar() const { return Avatar.IsValid(); }
	const FString& AvatarName() const { return Avatar.Name; }
	double Height() const { return Avatar.StandingHeight(); }
	FString Describe() const;   // for the test log

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* Feet = nullptr;
	UPROPERTY() USceneCaptureComponent2D* Capture = nullptr;
	UPROPERTY() UTextureRenderTarget2D* Target = nullptr;
	UPROPERTY() TArray<URectLightComponent*> Lights;   // the studio's soft boxes (TorqueStudioRig's order)
	UPROPERTY() UStaticMeshComponent* Backdrop = nullptr;
	UPROPERTY() UStaticMeshComponent* Helmet = nullptr;     // your helmet on its own (the Helmet view)
	UPROPERTY() UMaterialInterface* HelmetKitPaint = nullptr;
	FTorqueAvatar Avatar;
	int32 BuiltFor = -2;
	TArray<uint8> LookOn;
	TArray<uint8> HelmetLookOn;
	FString HelmetDecalsOn;
	bool bHelmetTried = false;
	EView View = EView::Body;
	EView Shown = EView::Body;   // what's showing now (the avatar or the helmet)
	TorqueStudio::FMeter Meter;   // holds the exposure (TorqueStudio)
	float BaseExposure = 0.0f;
	double WantFor = 0.0;      // seconds left filming
	// The view: turned round it (degrees, 0 from the front), tipped down, zoomed, turning by itself.
	double Yaw = 0.0, Pitch = 6.0, Zoom = 1.0;
	bool bSpin = true;
	bool bDrag = false;
	FVector2D DragLast = FVector2D::ZeroVector;
	FBox2D PictureBox = FBox2D(ForceInit);

	void MakeHelmet();
	void StudioLit();          // everything it shows lit only by its own lights (sharp: finest LOD, full textures)
	int32 LitCount = -1;       // how many parts it last did that for
};
