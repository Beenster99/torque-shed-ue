// The shop's 3D preview (Torque Shed's PartPreview): whatever the parts computer has picked, on a
// turntable in a little photo studio high over the level, filmed by a scene capture that sees only
// the studio. A soft gradient backdrop behind it, its own key, fill and rim lights (lighting
// channel 2, so the level's sun and lamps don't reach it). Drawn into a box on the computer's page
// worked with the mouse alone (no buttons over the picture).
//
// Mouse: drag to turn it (and tip it up or down), the wheel zooms, right-click stops or starts the
// turning. One per local player, made the first time the shop wants it and kept (hidden, not
// filming) after. The New Bike Configurator shows its bikes here too (ShowBike).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShopPreview.generated.h"

class AHUD;
class APlayerController;
class UCanvas;
class UMaterialInterface;
class UPointLightComponent;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class UTextureRenderTarget2D;

// How a slot of a previewed bike is finished: its colour, and (when >= 0) metallic and roughness.
struct FTorqueShopFinish
{
	FLinearColor Colour = FLinearColor::White;
	float Metallic = -1.0f;
	float Roughness = -1.0f;
};

UCLASS(NotPlaceable, Transient)
class ATorqueShopPreview : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShopPreview();
	virtual void Tick(float DeltaSeconds) override;

	// The local player's preview, made if needed.
	static ATorqueShopPreview* For(APlayerController* PC);

	// What to show. Each only rebuilds when it changes, so call it every frame it's drawn.
	void ShowPart(FName Part);           // a catalogue part as it comes out of its box
	void ShowPaint(int32 Stock);         // a tin of base or a bottle of tint (FTorquePaintBay stock index)
	void ShowKit(FName Model);           // a piece of shed or yard kit (TorqueFurniture's model)
	// A whole bike: slot -> catalogue part id (TorqueShedCatalogue::MakeBike's map), each slot's paint,
	// and the badge on its tank (Fit/BikeShop's badge text; a new badge is stamped on the bike that's
	// there, not a new bike built).
	void ShowBike(const TMap<FString, FString>& Parts, const TMap<FName, FTorqueShopFinish>& Finishes = TMap<FName, FTorqueShopFinish>(),
		const FString& Badge = FString());
	void ShowNothing();
	// The project bike's styles (the fitting layout's), to page through their reference bikes.
	static TArray<FString> BikeStyles();
	static TMap<FString, FString> StyleBike(const FString& Style);

	// The picture in Box, and keeps it filming. (Buttons is left empty: no buttons over it since build 101.)
	void Draw(AHUD* Hud, UCanvas* Canvas, const FBox2D& Box, const FVector2D& Mouse);
	// Input. Press: the left mouse went down (true when it was on the picture: a drag or a button).
	bool Press(const FVector2D& Mouse);
	// Every frame the page is up: where the mouse is and whether the left button is held (drags).
	void Hold(const FVector2D& Mouse, bool bLeftDown);
	bool Wheel(const FVector2D& Mouse, int32 Dir);      // Dir +1 (down) zooms out, -1 in; true when over it
	bool RightPress(const FVector2D& Mouse);            // stops or starts the turning; true when over it
	bool IsTurning() const { return bSpin; }
	FString Showing() const { return Key; }              // for the test log: "part:<id>", "paint:<n>", "bike:..."

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* Pivot = nullptr;        // turns (the turntable)
	UPROPERTY() USceneComponent* Holder = nullptr;       // what's shown, centred on the pivot
	UPROPERTY() USceneCaptureComponent2D* Capture = nullptr;
	UPROPERTY() UStaticMeshComponent* Backdrop = nullptr;
	UPROPERTY() UPointLightComponent* KeyLight = nullptr;
	UPROPERTY() UPointLightComponent* FillLight = nullptr;
	UPROPERTY() UPointLightComponent* RimLight = nullptr;
	UPROPERTY() UPointLightComponent* BounceLight = nullptr;   // low and in front: lifts the shadows
	UPROPERTY() UTextureRenderTarget2D* Target = nullptr;
	UPROPERTY() UTexture2D* Gradient = nullptr;
	UPROPERTY() UStaticMesh* Cube = nullptr;
	UPROPERTY() UStaticMesh* Cylinder = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Shown;

	FString Key;                // what's built
	double Radius = 50.0;       // its bounding sphere (cm)
	double Yaw = 0.0, Pitch = 0.0, Zoom = 1.0;
	bool bSpin = true;
	double WantFor = 0.0;       // seconds left filming
	// The mouse: dragging since a press on the picture.
	bool bDrag = false;
	FVector2D DragLast = FVector2D::ZeroVector;
	FVector2D DragFrom = FVector2D::ZeroVector;
	// Where the last Draw put the picture and its buttons.
	enum class EButton : uint8 { Spin, ZoomOut, ZoomIn, Reset };
	struct FButton { FBox2D Box; EButton What; };
	TArray<FButton> Buttons;
	FBox2D PictureBox = FBox2D(ForceInit);

	void Clear();
	void Rebuild(const FString& NewKey, TFunctionRef<void()> Make);
	void Framed();              // centres what's made on the pivot and sizes the view to it
	void ResetView();
	void MakeBackdrop();
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm, const FLinearColor& Colour, float Metallic, float Roughness);
	void Studio(UPrimitiveComponent* C) const;   // only the studio's lights, no shadows on the backdrop
};
