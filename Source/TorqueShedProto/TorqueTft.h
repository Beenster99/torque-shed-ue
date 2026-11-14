// A bike's TFT screen, the phone sat nav's screen and the HUD's copies of them, in the game: the plain
// C++ screens (Dash/TftScreen.h draws them as lists of triangles and text) painted onto render targets
// that light the screen's face on the bike (the fitting tags it: FDashGauge "screen_<shape>_<mm>x<mm>")
// and are shown on the HUD as an overlay the rider places and sizes, with the sat nav's map in a
// tile where the turn-by-turn box was.
//
// It also holds what the rider does with the screen (Dash/TftMenu.h): its views, the ride mode and
// its settings, which the pawn puts on the sim (Sim/RideModes.h).
//
// The map is drawn from what the ride world knows (World/WorldRoam.h: ATorqueRideWorld::RoamNow and
// GuideNow), rebuilt on a worker when the ride's area grows.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dash/TftMap.h"
#include "Dash/TftMenu.h"
#include "Dash/TftScreen.h"
#include "Shed/TorqueShedCatalogue.h"   // FDashGauge

#include <memory>

#include "TorqueTft.generated.h"

class UCanvas;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

// What the rider has set, kept on this machine ([TorqueTft] in GameUserSettings.ini).
struct FTorqueTftPrefs
{
	int32 Design = 0;
	bool bNorthUp = false;
	int32 Zoom = 0;
	bool bOverlay = true;        // the TFT shown on the HUD
	bool bMapTile = true;        // the map tile on the HUD
	// Where each sits on the HUD: its bottom right corner as a share of the screen, and its size
	// times the usual. A corner below 0: where the game puts it.
	float OverlayX = -1.0f, OverlayY = -1.0f, OverlayScale = 1.0f;
	float TileX = -1.0f, TileY = -1.0f, TileScale = 1.0f;
	FString Mode;                // the ride mode last used ("sport")
	TMap<FString, int32> Sets;   // the rider's changes to the modes ("sport.traction" -> 2)
	static FTorqueTftPrefs& Get();
	void Save() const;
};

UCLASS()
class UTorqueTftComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	// The bike's screens, from the fitting's tags: lights each one's face. Spec: the bike's riding
	// spec (what its dash can set).
	void Hook(const TArray<UStaticMeshComponent*>& Meshes, const TArray<TorqueShedCatalogue::FDashGauge>& Gauges, const TorqueSim::BikeSpec& Spec);
	// The bike this machine rides: it takes the rider's own settings and shows on the HUD. (Called
	// whenever the pawn knows; only the first does anything.)
	void Own();
	// True once after the ride mode or one of its settings changed: the pawn puts Set() on the sim.
	bool TakeModeDirty();
	bool HasTft() const { return bTft; }
	bool HasPhone() const { return bPhone; }
	bool HasMap() const { return bTft || bPhone; }

	// Each frame: Ride is the ride's side of the screen's state (speed, revs, lamps, sat nav words),
	// which the pawn fills; At and Forward the bike in the world; DayLight 0 night .. 1 day.
	void Step(float Dt, const TorqueTft::State& Ride, const FVector& At, const FVector& Forward, double SpeedMs, float DayLight);
	// A press on the screen's buttons: what it changed (TorqueTft::Menu's flags).
	int32 Press(TorqueTft::Press P, double SpeedMs, double Throttle);
	const TorqueTft::Menu& GetMenu() const { return Menu; }
	// Says a line across the foot of the screen for a moment ("Cruise set 60").
	void Notice(const FString& Words);

	// The HUD: the screen and the map tile where the rider has them, and (arranging) their frames.
	// PlainTop: the top of the HUD's plain speedo box (screen pixels), for the tile's usual place.
	void DrawOnHud(UCanvas* Canvas, float PlainTop);
	bool OverlayOn() const;
	bool MapTileOn() const;
	// Arranging the HUD: on and off (saved as it goes off), which of the two is being moved, a nudge
	// (steps right and down) and a size step.
	void ToggleOverlay();
	void ToggleArrange();
	bool Arranging() const { return bArrange; }
	void ArrangeNext();
	void ArrangeMove(int32 Dx, int32 Dy);
	void ArrangeSize(int32 Step);

	// For the test verbs: what the screens are and show.
	FString Describe() const;

private:
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> ScreenRT;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> PhoneRT;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> TileRT;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScreenMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PhoneMat;
	TWeakObjectPtr<UStaticMeshComponent> ScreenFace, PhoneFace;
	bool bTft = false, bPhone = false, bOwnBike = false, bArrange = false;
	int32 ArrangeWhat = 0;   // 0 the screen, 1 the map tile
	TorqueTft::Shape ScreenShape = TorqueTft::Shape::Wide;
	TorqueTft::Menu Menu;
	TorqueSim::BikeSpec::AidCaps Caps;
	TorqueTft::State Shown;
	TorqueTft::MapScene Scene;
	std::shared_ptr<const TorqueTft::MapData> Map;
	double ScreenIn = 0.0, PhoneIn = 0.0, TileIn = 0.0;
	float Bright = -1.0f;

	bool bModeDirty = false;
	double WorldIn = 0.0;
	std::shared_ptr<const void> GuideHeld;   // the ride's guide the scene's route was copied from
	bool bRerouting = false, bOffRoute = false;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PageBase;

	void TakeWorld(const FVector& At, const FVector& Forward, double SpeedMs);
	void KeepPrefs() const;
	void PaintTo(UTextureRenderTarget2D* Target, const TorqueTft::Paint& List);
	UMaterialInstanceDynamic* Light(UStaticMeshComponent* Face, UTextureRenderTarget2D* Target);
	FBox2D OverlayBox(const UCanvas* Canvas) const;
	FBox2D TileBox(const UCanvas* Canvas, float PlainTop) const;
};
