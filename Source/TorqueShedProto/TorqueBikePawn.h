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
#include "Shed/TorqueAvatar.h"   // the full type: a member below
#include "SonyPad.h"
#include "TorqueAudio/RideSound.h"   // the full type, as for the sim
#include "TorqueDash.h"   // the dashboard: a member below
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
	// [shed] Where the walker's HUD puts it (hud.gd): 0 the corner, 1 the look panel's title, 2 its
	// lines, 3 its prompts, 4 the message across the top.
	uint8 Role = 0;
};

// What the bike was doing at one moment, for the telemetry overlay (Torque Shed's telemetry_overlay.gd).
struct FTorqueTelemetry
{
	float Speed = 0.0f;      // m/s
	float Rpm = 0.0f;
	float Throttle = 0.0f, FrontBrake = 0.0f, RearBrake = 0.0f;   // 0 .. 1
	float Lean = 0.0f;       // degrees, + right
	int32 Gear = 0;
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
	// [dash] The dashboard as Torque Shed has it (TorqueDash.h): what the dash part shows, the trip
	// computer and the petrol, for the HUD's speedo (hud.gd _draw_speedo); and how far the petrol goes.
	const FTorqueDash& GetDash() const { return Dash; }
	double DashRangeKm() const { return Sim ? Dash.RangeKm(Sim->Build) : 0.0; }
	// The engine still being started (the starter cranking it over), for the speedo's "Starting...".
	bool IsStarting() const { return EngineCatchIn > 0.0; }
	// The telemetry overlay's samples, oldest first (ATorqueHUD draws them as strips).
	const TArray<FTorqueTelemetry>& GetTelemetry() const { return Telemetry; }

	// Co-op: to start slot Slot (0 the front, then two abreast, a row every 7 m back).
	void PlaceForSlot(int32 Slot);
	// Back to the start of the road, the clock reset (Home, or the pause menu).
	void RestartRide();

	// For TorqueShedTest: the bike's riding spec and what it's doing now (speed, distance from the
	// start, gear, revs, lean, steering, the wheels' spin and the suspension), one line each; and
	// the hands-off robot rider (-TorqueAutoRide's) switched on or off for this bike.
	FString TestRideLog() const;
	void TestAutoRide(bool bOn) { bTestAutoRide = bOn; }
	// Hold a lever for a check of the rider's fingers on it: Side 0 the clutch, 1 the front brake;
	// Pull 0..1 with the fingers over it, below 0 to let it go back to the riding.
	void TestLever(int32 Side, double Pull) { TestLeverHold[FMath::Clamp(Side, 0, 1)] = Pull; }
	// Measured manoeuvres for the physics checks (TorqueShedTest's brakestop and lean verbs). The robot
	// rides the road up to Mph and holds it on a straight, then: Kind 0 brakes as hard as a road test
	// does (front full, rear 0.6, no weight forward) to a stop; Kind 1 holds the steer input full at
	// that speed for 6 s (the ground taken as tarmac, as the bike leaves the road) and comes back to
	// where the bend began. TestMoveBusy until it's done, then TestMoveResult says what it measured.
	void TestStartMove(int32 Kind, double Mph);
	bool TestMoveBusy() const { return TestMove >= 0; }
	const FString& TestMoveResult() const { return TestMoveText; }
	// And the camera: 0 chase, 1 first person, 2 chin, 3 the showroom orbit held at Yaw degrees round
	// from straight ahead (90: side on from the bike's right).
	// Dist and Fov: the orbit's distance (cm) and field of view (degrees), 0 for the showroom's own.
	void TestCamera(int32 Mode, double Yaw, double Dist = 0.0, double Fov = 0.0)
	{
		CameraMode = FMath::Clamp(Mode, 0, CameraModes - 1);
		OrbitYaw = Yaw;
		bOrbitHeld = true;
		OrbitDist = Dist > 0.0 ? Dist : 270.0;
		OrbitFov = Fov > 0.0 ? Fov : 40.0;
	}

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
	// The rider as their MetaHuman (Profile page; Shed/TorqueAvatar.h), in the rider rig's place: it
	// reaches for the joints RiderRig was posed to on this bike (seat, grips, pegs), the helmet on its
	// head. The rig's own kit body is hidden then. -TorqueKitRider keeps the kit rider.
	FTorqueAvatar RiderAvatar;
	int32 RiderAvatarFor = -2;
	TArray<uint8> RiderAvatarLook;
	void UpdateRiderAvatar();
	void ShowRiderAvatar();
	uint32 PrevPadButtons = 0;
	bool bTrigSeen[4] = {false, false, false, false}, bTrigLive[4] = {false, false, false, false};
	double TrigRest[4] = {0, 0, 0, 0};
	double InputLogTimer = 0.0;
	int32 InputLogCount = 0;
	TUniquePtr<TorqueSound::RideSound> Sound;
	// Torque Shed's horn on this bike (TorqueAudio/TorqueSfx: its part's recordings or synthesis), held on
	// J or the d-pad's left; and the starter cranking it over before the engine catches.
	int32 HornId = 0;
	bool bHornDown = false;
	double EngineCatchIn = 0.0;
	int32 RideBay = INDEX_NONE;   // the shed bay whose bike this is (BuildCatalogueModel): its engine sound
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
	FTorqueDash Dash;   // [dash]
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
	double OrbitDist = 270.0, OrbitFov = 40.0;   // the showroom's distance (cm) and field of view
	bool bOrbitHeld = false;          // -TorqueOrbitYaw: hold the angle instead of turning
	bool bRiderShown = true;
	FVector CamPos = FVector::ZeroVector;
	FVector CamLookAt = FVector::ZeroVector;
	bool bCamInit = false;
	FVector HeadLocal = FVector(-9.0, 0.0, 128.0);   // the helmet's centre, pawn local cm (UpdateRider)
	bool bHeadShown = true;
	double LookYaw = 0.0, LookPitch = 0.0;   // where the camera looks, degrees: the stick's and the mouse's
	double StickYaw = 0.0, StickPitch = 0.0, MouseYaw = 0.0, MousePitch = 0.0, MouseStill = 0.0;
	// Settings > Gameplay > Telemetry overlay: the last few seconds, ten times a second.
	TArray<FTorqueTelemetry> Telemetry;
	double TelemetryClock = 0.0;

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
	double TestLeverHold[2] = {-1.0, -1.0};   // TestLever's hold, per side (below 0: none)
	// TestStartMove's manoeuvre: which (-1 none), its phase (0 getting up to speed, 1 measuring), the
	// speed it's held at (m/s), and what it has measured so far.
	int32 TestMove = -1, TestPhase = 0, TestFromNear = 0;
	double TestMps = 0.0, TestClock = 0.0, TestSteady = 0.0, TestDist = 0.0, TestV0 = 0.0, TestSide = 1.0;
	double TestNose = 0.0, TestLeanSum = 0.0, TestGSum = 0.0, TestLeanPeak = 0.0, TestGPeak = 0.0, TestSlide = 0.0, TestLateLean = 0.0;
	int32 TestSamples = 0, TestLateSamples = 0;
	bool bTestRobotWas = false, bTestTarmac = false;
	FString TestMoveText;
	void TestMoveStep();
	// Where its rider sits (pawn local cm): the seat's surface under the hips, the pegs (0 left, 1
	// right) and the grips' centres, from the layout (as Torque Shed sits its rider: BikeRig's hip
	// and peg) and the grips as drawn, with geometry mode's offsets. FitRiderToBike turns them into
	// the rig's pose input, once the rig is built: SeatFit, PegFit and GripFit against the Speed
	// Twin the rig was made on, and the back leant so the hands reach the bars.
	bool bRiderOnLayout = false, bRiderFitted = false;
	FVector RiderSeat = FVector::ZeroVector, RiderPeg[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector RiderGrip[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector RiderChest = FVector::ZeroVector;   // the top of the back's place against the Speed Twin's
	double RiderBackDeg = 0.0;                  // and the back's lean for it, forward of upright
	double RiderStyleDeg = 0.0;                 // the style's own lean, before the reach moved it
	bool bRiderLaidBack = false;                // cruiser or chopper seating (feet forward)
	FString RiderStyle;                         // Torque Shed's riding style (situp, scratcher...)
	double RiderReach[2] = {0.0, 0.0}, RiderArm = 0.0;        // shoulder to wrist, and the arm's length
	double RiderLegReach[2] = {0.0, 0.0}, RiderLeg = 0.0;     // hip to ankle, and the leg's length
	FTransform RiderSeatFit = FTransform::Identity, RiderPegFit[2], RiderGripFit[2];
	FVector PoseHip = FVector::ZeroVector, PoseWrist[2] = {FVector::ZeroVector, FVector::ZeroVector}, PoseAnkle[2] = {FVector::ZeroVector, FVector::ZeroVector};
	void FitRiderToBike();
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
