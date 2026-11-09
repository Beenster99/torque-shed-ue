// Your own MetaHuman for the Profile page (Shed/TorqueProfile.h): built from your profile, standing
// high above the level in the sun, filmed by a scene capture that sees only it. The pause menu draws
// the picture: a portrait on the profile badge, and the whole figure or the face beside the Profile
// page's sliders.
//
// One per local player, made the first time the menu wants it and kept (hidden, not filming) after.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueProfile.h"
#include "TorqueProfilePreview.generated.h"

class APlayerController;
class UCanvas;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPointLightComponent;

UCLASS(NotPlaceable, Transient)
class ATorqueProfilePreview : public AActor
{
	GENERATED_BODY()

public:
	ATorqueProfilePreview();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The local player's preview, made if needed (nullptr when there are no avatars in this build).
	static ATorqueProfilePreview* For(APlayerController* PC);
	// Film this frame (and the next while it's wanted): bFace frames the head, else the whole figure.
	void Want(bool bFace, int32 Colour);
	// The picture, drawn into the canvas box; nothing when there's no avatar.
	void Draw(UCanvas* Canvas, float X, float Y, float W, float H, bool bPortrait) const;
	bool HasAvatar() const { return Avatar.IsValid(); }
	const FString& AvatarName() const { return Avatar.Name; }
	double Height() const { return Avatar.StandingHeight(); }

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* Feet = nullptr;
	UPROPERTY() USceneCaptureComponent2D* Capture = nullptr;
	UPROPERTY() UTextureRenderTarget2D* Target = nullptr;
	UPROPERTY() UPointLightComponent* Key = nullptr;
	UPROPERTY() UPointLightComponent* Fill = nullptr;
	FTorqueAvatar Avatar;
	int32 BuiltFor = -2;
	TArray<uint8> LookOn;
	bool bFaceView = false;
	double WantFor = 0.0;      // seconds left filming
	double Turn = 0.0;         // the slow turntable
	double Sway = 0.0;
};
