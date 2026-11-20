// The drone camera (beenster, Nov 18 2026: "a pretty decent camera ... not a hand held one but more
// like a drone. You can fly it about when you are off the bike, you can use zoom, change the
// exposure, have pic filters").
//
// On foot (the shed and the yard, or off the bike on a ride) the View / Create button or B (Settings >
// Controller > Key bindings: "Drone camera") sends it up from just over your head, and the same again
// brings it back. It's this player's alone and drawn by nobody: an actor that only exists while it's
// up (nothing at all when it isn't), with a camera on a steady gimbal that the view is handed to.
//
// Flying: the left stick (WASD) moves it, the right stick (the mouse) turns it and tilts the camera,
// R2 / L2 (Space / C) up and down, L3 (Shift held) fast, R3 levels the camera. It eases about as a
// drone does, slows down indoors, stops at walls, and goes no higher than 120 m over where it went up
// nor further than 200 m from it (60 m round the shed).
//
// The camera: the D-pad's up / down (the wheel) zooms, 24 to 400 mm; its left / right (Tab) picks a
// setting, Exposure (+/- 3 EV), Aperture (depth of field, focused on what's in the middle of the
// frame) or Filter (TorquePhotos), and L1 / R1 (Q / E) change it. Cross (left mouse, Enter) takes the
// photo: the frame as it's seen, without the screen's own words, saved full size to the Pictures folder
// (TorquePhotos). Triangle (G) puts thirds lines over the view, Square (H) hides the screen's words.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueDrone.generated.h"

class AHUD;
class APawn;
class APlayerController;
class APostProcessVolume;
class UCameraComponent;
class UCanvas;
struct FSonyPadState;

UCLASS()
class ATorqueDrone : public AActor
{
	GENERATED_BODY()

public:
	ATorqueDrone();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Sends it up for Pilot (the walker, or the bike's rider on foot): from where they look. False,
	// and why, when it can't go.
	static bool Launch(APawn* Pilot, FString& WhyNot);
	// The one up for this player, or null.
	static ATorqueDrone* For(const AController* Controller);
	// Brings it back: the view goes back to the pilot.
	void Land();
	// The viewfinder over everything else (ATorqueHUD), and whether this frame is the photo's (when
	// nothing at all is drawn over the view).
	void DrawHud(AHUD* Hud, UCanvas* Canvas);
	bool HidesHud() const;

	UPROPERTY(VisibleAnywhere, Category = "Drone") UCameraComponent* Camera;

	// --- For hands-off tests (UTorqueShedTest drone:...) -------------------------------------------------
	FString TestShot();
	FString TestSet(const FString& What, double Value);   // zoom (mm), ev, fstop (0: off), filter (index)
	FString TestFly(const FVector& By);                    // moved by By (cm), as flying would (walls stop it)
	FString TestStatus() const;

private:
	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<APawn> Pilot;
	FVector Home = FVector::ZeroVector;   // where it went up from
	FVector Vel = FVector::ZeroVector;
	double Yaw = 0.0, Pitch = 0.0;        // where the gimbal points (what's wanted)
	FRotator Gimbal = FRotator::ZeroRotator;   // where it points now (eased to it)
	double FocalMm = 24.0, FocalWant = 24.0;
	int32 EvThirds = 0;                   // exposure, thirds of a stop
	int32 Aperture = 0;                   // 0: off (all sharp), else an index into the f-stops
	int32 Filter = 0;
	int32 Setting = 0;                    // the one L1 / R1 change: 0 exposure, 1 aperture, 2 filter
	bool bGrid = false, bClean = false, bIndoor = false;
	double FocusCm = 100000.0;            // where it's focused (eased to what's in the middle)
	bool bFocused = false;
	uint32 PrevPad = 0;
	uint64 OpenFrame = 0;
	double Flying = 0.0;                  // seconds up
	double MaxRange = 20000.0, MaxUp = 12000.0;
	bool bAtLimit = false;
	// The photo: asked for on ShotFrame, the screen read back that frame (the HUD left off it).
	FDelegateHandle ShotHandle;
	uint64 ShotFrame = 0;
	double ShotWait = 0.0;
	bool bShotWaiting = false;
	double Flash = 0.0;
	int32 Shots = 0;
	FString LastPhoto, Note;
	double NoteFor = 0.0;
	TArray<TWeakObjectPtr<APostProcessVolume>> Volumes;
	TArray<TWeakObjectPtr<AActor>> Ignore;   // the pilot and what's on them (the drone flies through them)

	FSonyPadState PadNow() const;
	void ReadInput(float Dt);
	void Move(const FVector& Delta);
	void UpdateLens(float Dt);
	double LevelExposure() const;
	void Shoot();
	void Shot(int32 W, int32 H, const TArray<FColor>& Pixels);
	void EndShot();
	void Say(const FString& Text, double Seconds = 3.0);
	FString SettingText(int32 Which) const;
	void Step(int32 Dir);
};
