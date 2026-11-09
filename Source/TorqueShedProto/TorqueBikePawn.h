// The ridden bike in Unreal: holds the plain C++ riding sim (Sim/BikeSim), steps it at a fixed
// 120 Hz, lets it see the world through line traces, and draws it, a chase camera and a small HUD.
//
// Frames. The sim works in metres with y up and z south (Torque Shed's region frame, relative to
// the route's origin). Unreal is centimetres with x east, y south, z up, so a sim point (x, y, z)
// is Unreal (100 x, 100 z, 100 y). Body space in the sim is x right, y up, -z forward; in Unreal
// it is x forward, y right, z up.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Sim/BikeSim.h"   // the full type: the code Unreal generates for this class deletes it
#include "RiderRig.h"
#include "SonyPad.h"
#include "TorqueAudio/RideSound.h"   // the full type, as for the sim
#include "TorqueBikePawn.generated.h"

class UStaticMeshComponent;
class UPoseableMeshComponent;
class UCameraComponent;
class UStaticMesh;
class UMaterialInterface;
class USoundWaveProcedural;
class UAudioComponent;
class UTorqueNetBikeComponent;

// A part of the Speed Twin model moved by its slot's offset from geometry mode (the bay's saved fit).
// On a part that turns with a group (the bars with the steering), the offset is in that group's frame,
// so it's set once; a wheel's part spins, so its offset (in the fork's or swingarm's frame) is put
// back on it every frame, on top of the spin.
struct FTorqueRideFit
{
	TWeakObjectPtr<UStaticMeshComponent> Part;
	FTransform Rest;                      // its place on its group, without the fit
	USceneComponent* Spin = nullptr;      // the wheel's pivot it spins on (null: set once)
	USceneComponent* Carrier = nullptr;   // the fork lowers or the swingarm that carry that wheel
	FTransform Move;                      // the offset, in the carrier's frame
};

struct FHudLine
{
	FString Text;
	FColor Colour = FColor::White;
	float Size = 1.0f;   // times the large font at 1080p
};

UCLASS()
class ATorqueBikePawn : public APawn
{
	GENERATED_BODY()

public:
	ATorqueBikePawn();
	virtual ~ATorqueBikePawn() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	const TArray<FHudLine>& GetHudLines() const { return HudLines; }

	// Co-op: to start slot Slot (0 the front, then two abreast, a row every 7 m back).
	void PlaceForSlot(int32 Slot);
	// Back to the start of the road, the clock reset (Home, or the pause menu).
	void RestartRide();

	// For TorqueShedTest: the bike's riding spec and what it's doing now (speed, distance from the
	// start, gear, revs, lean, steering, the wheels' spin and the suspension), one line each; and
	// the hands-off robot rider (-TorqueAutoRide's) switched on or off for this bike.
	FString TestRideLog() const;
	void TestAutoRide(bool bOn) { bTestAutoRide = bOn; }

	// Riding realism: 0 full sim .. 1 easy (as Torque Shed's slider). [ and ] change it in game.
	UPROPERTY(EditAnywhere, Category = "Ride", meta = (ClampMin = "0", ClampMax = "1"))
	float Assist = 0.6f;

	// The route written by Tools/export_buttertubs.py, relative to the project folder.
	UPROPERTY(EditAnywhere, Category = "Ride")
	FString RouteFile = TEXT("Data/Buttertubs/route.json");

	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* Root;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UStaticMeshComponent* Body;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UStaticMeshComponent* Tank;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UStaticMeshComponent* Rider;
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* FrontPivot;
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* RearPivot;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UStaticMeshComponent* FrontWheel;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UStaticMeshComponent* RearWheel;
	UPROPERTY(VisibleAnywhere, Category = "Bike") UCameraComponent* Camera;
	// The Speed Twin model's moving groups (see Tools/make_speedtwin.py).
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* SteerGroup;
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* FrontHub;
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* SwingGroup;

	// The model's parts, made at BeginPlay from Data/Bike/SpeedTwin/ue_assets.json.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> ModelParts;
	// The clutch (0) and front brake (1) levers, which the rider's fingers pull.
	UPROPERTY(Transient) UStaticMeshComponent* LeverParts[2];

	// The rider (Tools/make_rider.py): a skinned body in riding kit and a helmet, posed every frame:
	// hands on the grips, feet on the pegs (or down when stopped), weight and lean as you ride.
	UPROPERTY(Transient) UPoseableMeshComponent* RiderBody = nullptr;
	// The skinned rider and helmet hang off this: on the bike it sits on Root, and in a crash it's
	// let go so the rider is thrown clear and slides along the road on his back.
	UPROPERTY(VisibleAnywhere, Category = "Bike") USceneComponent* RiderRoot = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* RiderHelmet = nullptr;
	// Until the level script has imported it, a rider made of simple shapes.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> RiderParts;
	// A dial and needles drawn over the clock: speed, and revs on a small dial below.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> ClockParts;
	UPROPERTY() UStaticMesh* ShapeCube = nullptr;
	UPROPERTY() UStaticMesh* ShapeCylinder = nullptr;
	UPROPERTY() UStaticMesh* ShapeSphere = nullptr;

	// Co-op (Coop/TorqueNetBike): sends this bike to the others, or draws someone else's from theirs.
	UPROPERTY(VisibleAnywhere, Category = "Net") UTorqueNetBikeComponent* NetBike = nullptr;

	// Sound: made by TorqueSound::RideSound and fed to Unreal as it plays.
	UPROPERTY(Transient) USoundWaveProcedural* SoundWave = nullptr;
	UPROPERTY(Transient) UAudioComponent* SoundAudio = nullptr;

private:
	friend class UTorqueNetBikeComponent;   // reads our bike to send it, or sets someone else's
	TUniquePtr<TorqueSim::BikeSim> Sim;
	TUniquePtr<FSonyPad> Pad;          // a PlayStation pad read directly (Unreal reads Xbox pads itself)
	FRiderRig RiderRig;                // the skinned rider, when the level script has imported it
	uint32 PrevPadButtons = 0;
	bool bTrigSeen[4] = {false, false, false, false}, bTrigLive[4] = {false, false, false, false};
	double TrigRest[4] = {0, 0, 0, 0};
	double InputLogTimer = 0.0;
	int32 InputLogCount = 0;
	TUniquePtr<TorqueSound::RideSound> Sound;
	TArray<float> SoundFloat;
	TArray<int16> SoundPcm;
	int32 LastGear = 1;
	bool bManual = false;            // gears changed by hand (the first press of a shift button)
	bool bAbs = true;                // the ABS switched on; off lets the rear lock
	bool bHasAbs[2] = {true, true};  // the bike has ABS at the front, the rear (a catalogue bike's ECU may not)
	double GearNoteFor = 0.0;
	// Performance: P shows frame, game, render and GPU times; O (or -TorqueProfile) logs them each
	// second to Saved/Profiling/torque_perf_<time>.csv.
	bool bPerfShow = false, bPerfLog = false;
	double PerfClock = 0.0, PerfSum = 0.0, PerfWorst = 0.0, PerfRun = 0.0;
	int32 PerfFrames = 0;
	FString PerfText, PerfFile;
	void UpdatePerf(float Dt);
	TArray<FHudLine> HudLines;
	bool bShowHelp = true;
	// Weather: 0 sunny, 1 overcast, 2 wet (-TorqueWeather=sunny|overcast|wet; V or D-pad right cycles).
	// ATorqueGameState owns it (the host's, in co-op) and lights the world; the bike takes the grip.
	int32 Weather = 0;
	double HostOnlyNoteFor = 0.0;      // "the host sets the weather", on a joined rider's HUD
	void ApplyWeather();
	double Accum = 0.0;
	FVector PrevPos, CurPos;          // Unreal cm, the sim body origin before and after the last step
	FQuat PrevRot, CurRot;

	// The route, sim metres.
	TArray<FVector> Line;             // (x, y, z) sim
	double RouteLengthM = 3000.0;     // route.json length_m
	int32 Near = 0;
	int32 StartIndex = 0;             // where the ride starts on Line (-TorqueStartKm)
	double RideTime = 0.0;
	bool bFinished = false;

	// Smoothed rider inputs from keys, pad and wheel.
	double Throttle = 0, FrontBrake = 0, RearBrake = 0, Lean = 0, Shift = 0;
	double CrashedFor = 0.0;

	// Camera.
	int32 CameraMode = 0;             // 0 chase, 1 first person (rider's eyes), 2 chin camera, 3 showroom orbit
	static constexpr int32 CameraModes = 4;
	double OrbitYaw = 40.0;           // showroom: degrees round from straight ahead, to the right
	bool bOrbitHeld = false;          // -TorqueOrbitYaw: hold the angle instead of turning
	bool bRiderShown = true;
	FVector CamPos = FVector::ZeroVector;
	FVector CamLookAt = FVector::ZeroVector;
	bool bCamInit = false;
	FVector HeadLocal = FVector(-9.0, 0.0, 128.0);   // the helmet's centre, pawn local cm (UpdateRider)
	bool bHeadShown = true;
	double LookYaw = 0.0, LookPitch = 0.0;   // right stick, degrees

	// Model geometry (Unreal local cm), read from speedtwin.json.
	bool bHaveModel = false;
	FVector SteerPoint = FVector::ZeroVector, SteerDir = FVector::UpVector, SwingPivot = FVector::ZeroVector;
	FVector RearRest = FVector::ZeroVector;
	// A catalogue bike (any of Torque Shed's styles, BuildCatalogueModel): its parts on the same
	// groups as the Speed Twin model's (wheels spinning, bars and fork steering, the fork lowers and
	// the swingarm on the suspension), its geometry above from the layout, and its own riding spec.
	bool bCatalogueModel = false;
	FString CatalogueSpec;             // the spec, as logged
	int32 GroupParts[6] = {0, 0, 0, 0, 0, 0};   // its meshes on the bike, steering, fork lowers, front wheel, swingarm, rear wheel
	bool bTestAutoRide = false;
	FTransform PartFix;                // the importer's turn of the model's frame
	FVector LeverPivot[2] = {FVector::ZeroVector, FVector::ZeroVector}, LeverAxis[2] = {FVector::UpVector, FVector::UpVector};
	FTransform LeverBase[2];           // each lever's rest placement (the importer's fix, then where its socket put it)
	FTransform GripDelta[2] = {FTransform::Identity, FTransform::Identity};   // fitted grips vs stock, for the rider's hands
	// Geometry mode's fit on the model: the wheels' parts (put back each frame), and the offsets of the
	// levers' and grips' slots (in the steering frame) for the levers as they're pulled and the hands.
	TArray<FTorqueRideFit> RideFit;
	FTransform LeverFit[2] = {FTransform::Identity, FTransform::Identity};
	FTransform GripFitMove[2] = {FTransform::Identity, FTransform::Identity};
	UStaticMeshComponent* GripParts[2] = {nullptr, nullptr};   // (in ModelParts)
	FTransform SeatFitMove = FTransform::Identity;             // the saddle's offset: the rider's hips
	FTransform PegFitMove = FTransform::Identity;              // the rearsets': the rider's feet
	void ApplyRideFit(const TArray<FName>& PartSlots, const TArray<FName>& PartGroups, const TArray<FTransform>& PartRest);
	double LeverPullRad[2] = {0.0, 0.0};
	double SinceShift = 10.0;          // seconds since the last gear change (the clutch's pull)
	double BrakeFingers = 0.0;         // two fingers over the front brake lever
	// What the rider's body follows, smoothed: the bike's lean (degrees, + right) and its speeding up
	// (m/s/s, - braking).
	double BodyLean = 0.0, LongAcc = 0.0, LastSpeed = 0.0;

	// Thrown off in a crash (world cm): the pelvis's path, falling and sliding, and the turn from the
	// riding pose at the moment of the crash to lying on his back.
	bool bThrown = false;
	double ThrownFor = 0.0;
	FVector ThrowPelvis = FVector::ZeroVector, ThrowVel = FVector::ZeroVector;
	FQuat ThrowRot0 = FQuat::Identity, ThrowLie = FQuat::Identity;
	void ThrowRider(float Dt);

	void BuildRider(UMaterialInterface* const* Mats);
	bool BuildRiderRig();
	void UpdateRider();
	void UpdateClock();
	void ShowHead(bool bShow);
	void StartSound();
	void UpdateSound(float Dt);
	void LoadRoute();
	void BuildModel();
	bool BuildCatalogueModel();   // true if the bike is all catalogue parts and was drawn so
	// SideM: metres to the right of the line (co-op start slots).
	void Respawn(int32 LineIndex, double SideM = 0.0);
	void Remount();                   // the thrown-off rider back on the bike
	void ReadInput(float Dt);
	void StepSim();
	void UpdateVisuals(double Alpha);
	void UpdateCamera(float Dt);
	void ShowRider(bool bShow);
	void UpdateProgress();
	void DrawHud();
};
