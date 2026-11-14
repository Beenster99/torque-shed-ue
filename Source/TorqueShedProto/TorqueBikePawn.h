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
#include "TorqueLights.h"   // [lights] the lamps: a member below
#include "TorqueBikePawn.generated.h"

class UStaticMeshComponent;
class UMeshComponent;
class UPoseableMeshComponent;
class UCameraComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
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
	// Torque Shed's telemetry_overlay.gd (beenster, Nov 12: tyre and engine temperatures and more):
	// per wheel [0 front, 1 rear].
	float Steer = 0.0f;                          // degrees, + right
	float SlipRatio[2] = {0.0f, 0.0f};           // kappa
	float SlipAngle[2] = {0.0f, 0.0f};           // degrees
	float GripUsed[2] = {0.0f, 0.0f};            // 1 = the limit
	float LoadN[2] = {0.0f, 0.0f};
	bool bContact[2] = {true, true};
	float SuspMm[2] = {0.0f, 0.0f}, TravelMm[2] = {0.0f, 0.0f};
	bool bBottomed[2] = {false, false};
	float TyreSurface[2] = {20.0f, 20.0f}, TyreCarcass[2] = {20.0f, 20.0f}, TyreC[2] = {20.0f, 20.0f};
	int8 TyreBand[2] = {0, 0};                   // -1 cold, 0 working, 1 overheated
	float DiscC[2] = {20.0f, 20.0f};
	float EngineC = 20.0f, OilC = 20.0f, AirC = 15.0f, RoadC = 15.0f;
	int8 EngineBand = 0;                         // -1 cold, 0 warm, 1 hot
	bool bLiquid = true, bFan = false;
	float PitchDeg = 0.0f, RakeDeg = 0.0f, TrailMm = 0.0f, TrailStandMm = 0.0f;
	float PowerMult = 1.0f;                      // what the heat leaves of the engine's power
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
	// [lights] The headlight, main beam, fog lights, indicators and stop lamp (TorqueLights.h), for the
	// HUD's tell-tales and the co-op snapshot.
	const FTorqueLights& GetLights() const { return Lights; }
	// The engine still being started (the starter cranking it over), for the speedo's "Starting...".
	bool IsStarting() const { return EngineCatchIn > 0.0; }
	bool EngineRunning() const;   // on, or catching (left idling when you get off)
	// The telemetry overlay's samples, oldest first (ATorqueHUD draws them as strips).
	const TArray<FTorqueTelemetry>& GetTelemetry() const { return Telemetry; }
	// [info] For the info panel's riders: how fast it's going (m/s, our sim or a co-op rider's
	// snapshots), and whether its rider is off.
	double SpeedMs() const;
	bool RiderCrashed() const;
	// [info] The pointer is out (M) to click a place in the info panel: the mouse moves it, not the view.
	bool IsPointing() const { return bPointing; }
	void SetPointing(bool bOn) { bPointing = bOn; }

	// Off the bike (Torque Shed's get off, F): stopped, the rider steps off to its left and walks about
	// in first person; the bike stands on its side stand (leaned over on it), else its centre stand
	// (the back wheel lifted), its engine left as it was, ticking over if it was running. F by the
	// bike gets back on. Parked: a bike seen standing with nobody on it (ours, or in co-op another
	// rider's), its stand down and the rider hidden.
	bool IsOnFoot() const { return bOnFoot; }
	void SetParkedLook(bool bParked);
	// [co-op] Riding a mate's bike on the road (beenster, build 141): a bike standing parked with its
	// rider off it, ours or as their snapshots say. Their parked bike nearest our feet (within reach),
	// if any. The host swaps the two bikes over (ATorquePlayerController::ServerTakeBike); then on
	// each rider's machine the bike now theirs is taken over where it stands, its engine as it was
	// (TakeOver: on foot where they stood, and the one who asked gets on), and the one let go of
	// stops being ridden here (LetOver).
	bool IsParkedUp() const { return !Sim->IsCrashed && (bOnFoot || bParkedLook) && !bPushing; }
	ATorqueBikePawn* MateBikeInReach() const;
	FVector FeetAt() const { return FootPos; }
	double FeetYaw() const { return FootYaw; }
	void TakeOver(const FVector& Feet, double Yaw, bool bRide);
	void LetOver();
	double TakeAskedAt = -100.0;   // when we last asked the host for a mate's bike (world seconds)
	// [gestures] The rider's nod (R3 or the Y key tapped) and wave (R3 or the U key held: the left hand off the bar):
	// what they're doing now, and a co-op rider's as their snapshots say (TorqueNetBike), so the
	// others see it. A nod starts on the bit's rising edge and runs its own course.
	bool IsNodding() const;
	bool IsWaving() const { return bWaving; }
	void SetNetGestures(bool bNod, bool bWave);
	// For TorqueShedTest: "off" or "on" as F does, "push" / "letgo" as holding F by it and F again,
	// "walk:<cm>" steps that far ahead (on foot, the bike alongside if it's being wheeled).
	FString TestFoot(const FString& What);
	// [washride] For TorqueShedTest: put the bike at (X, Y) world cm facing Yaw, stopped, on the ground there
	// (bPlace false: left where it is); says where it is and what its wheels stand on.
	FString TestPlaceAt(double X, double Y, double Yaw, bool bPlace = true);

	// Co-op: to start slot Slot (0 the front, then two abreast, a row every 7 m back).
	void PlaceForSlot(int32 Slot);
	// [yard] Ridden in the shed's yard (the concrete pad outside the roller door, Tools/shed_yard.py)
	// rather than on Buttertubs: the yard's route and walls, no finish, a crashed rider put back on
	// where he fell, and Backspace parks it and walks back in (Shed/TorqueYard.h).
	bool InYard() const { return bYard; }
	// The shed bay whose bike this is (-1 the Speed Twin model). The yard's spawner sets it before
	// BeginPlay (SpawnActorDeferred): on the host, a joined rider's bike isn't theirs to ask yet.
	int32 GetRideBay() const { return RideBay; }
	void SetYardBay(int32 Bay) { YardBay = Bay; }
	// [yard] Where it starts (Unreal cm, yaw degrees): where the bike stood in the shed when the rider
	// got on it there. Set by the host before BeginPlay and sent with the bike, so the rider's own
	// machine (which runs the bike) starts it there too, not at the door.
	void SetYardStart(const FVector& At, double Yaw) { bYardStart = true; YardStartAt = At; YardStartYaw = (float)Yaw; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated) bool bYardStart = false;
	UPROPERTY(Replicated) FVector YardStartAt = FVector::ZeroVector;
	UPROPERTY(Replicated) float YardStartYaw = 0.0f;
	// Back to the start of the road, the clock reset (Home, or the pause menu).
	void RestartRide();

	// For TorqueShedTest: the bike's riding spec and what it's doing now (speed, distance from the
	// start, gear, revs, lean, steering, the wheels' spin and the suspension), one line each; and
	// the hands-off robot rider (-TorqueAutoRide's) switched on or off for this bike.
	FString TestRideLog() const;
	void TestAutoRide(bool bOn) { bTestAutoRide = bOn; }
	// The test's stop: brakes on while the robot steers (bOn), then the robot off, the brakes left on.
	void TestStop(bool bOn)
	{
		bTestStopping = bOn;
		bTestAutoRide = bOn;
		if (!bOn) { Throttle = 0.0; FrontBrake = 0.8; RearBrake = 0.4; }
	}
	double TestSpeedMph() const;
	// Hold a lever for a check of the rider's fingers on it: Side 0 the clutch, 1 the front brake;
	// Pull 0..1 with the fingers over it, below 0 to let it go back to the riding.
	void TestLever(int32 Side, double Pull) { TestLeverHold[FMath::Clamp(Side, 0, 1)] = Pull; }
	// The rider's right hand rolled to this throttle (0..1) for the pose only, the bike's own throttle untouched (below 0: none).
	void TestGripThrottle(double Roll) { TestGripThrottleHold = Roll; }
	// The stage 3 controls for TorqueShedTest, as the keys work them: What "gear" (Arg +1 up, -1 down, as
	// X / Z: the box goes manual), "ignition" (as I), "clutch" (Arg 1 held, 0 let go, as Left Alt) or "state"
	// (nothing). Says the gear, the engine, the clutch and the speed after it.
	FString TestControl(const FString& What, int32 Arg);
	// [lights] For TorqueShedTest: What "state" (just say), "dipped", "main", "fog", "off" (the headlight
	// switches as the keys leave them), "cycle" (the pad's Triangle tap), "left", "right", "hazard",
	// "cancel" (indicators off), "brake" / "nobrake" (the stop lamp held on), "night" / "day" (the clock
	// at 23:00, or the level's own light: the host's), then the lamps shown at once and described.
	FString TestLights(const FString& What);
	// Measured manoeuvres for the physics checks (TorqueShedTest's brakestop and lean verbs). The robot
	// rides the road up to Mph and holds it on a straight, then: Kind 0 brakes as hard as a road test
	// does (front full, rear 0.6, no weight forward) to a stop; Kind 1 holds the steer input full at
	// that speed for 6 s on a level test pad (the road's edge, banks and ditches are no part of it) and
	// comes back to where the bend began. bFlat puts a brake stop on the pad too (else it's on the
	// road, its grade allowed for in the log). Riding realism is the road tests' 0.5 while measuring.
	// Kind 2 and 3 are knocks, on the test pad: Kind 2 rides at Mph into a drystone wall stood across
	// its path 12 m ahead (as collision.json's wall boxes), Kind 3 into a stand-in bike of the same
	// build stopped across it (seen as a debug outline only), the robot holding the line and the speed
	// until it hits. The log has the hit's speed and whether the rider came off.
	// Kind 4 is a launch on the test pad, as SimTest/realism_test's: rolling at Mph (10) it stops, stands
	// a second, then full throttle with the weight forward on the automatic box for 10 s; the log has
	// 0-60 mph (and 0-30, 0-100 km/h) and the quarter of the way's speed.
	// TestMoveBusy until it's done, then TestMoveResult says what it measured.
	void TestStartMove(int32 Kind, double Mph, bool bFlat = false);
	bool TestMoveBusy() const { return TestMove >= 0; }
	const FString& TestMoveResult() const { return TestMoveText; }
	// And the camera: 0 chase, 1 first person, 2 chin, 4 drive-by, 3 the showroom orbit held at Yaw degrees round
	// from straight ahead (90: side on from the bike's right).
	// Dist and Fov: the orbit's distance (cm) and field of view (degrees), 0 for the showroom's own.
	// Rise: the orbit's height above what it looks at (cm; 0 for the showroom's own, below 0 looks up
	// from under); At: 0 the bike, 1 the rider's left hand, 2 the right hand.
	void TestCamera(int32 Mode, double Yaw, double Dist = 0.0, double Fov = 0.0, double Rise = 0.0, int32 At = 0)
	{
		OrbitRise = Rise;
		OrbitAt = FMath::Clamp(At, 0, 2);
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
	FString RouteName = TEXT("Buttertubs");   // on the ride's line of the HUD

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
	UPROPERTY(Transient) UMeshComponent* RiderHelmet = nullptr;
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
	// [Ride UK] the ride's cattle grids handed to the sim once its plan is there.
	bool bRideGridsSet = false;
	double NavWas = -1.0;   // [Ride UK] the sat nav's last place along the ride's road
	// What the bike bumps into besides the ground (Sim/BikeContacts.h): the ride's walls, buildings,
	// gates and props from Data/Buttertubs/collision.json (Tools/make_collision.py), and the other
	// riders' bikes, gathered each frame. HitsHeard: the sim's knock count already sounded.
	TUniquePtr<TorqueSim::ContactWorld> Contacts;
	int32 HitsHeard = 0;
	void LoadContacts();
	void GatherOtherBikes();
	// [furniture] The ride's own boxes, and the yard's solid kit added to them (TorqueFurniture.h):
	// rebuilt whenever the kit's moved (its ContactVersion changes).
	TArray<TorqueSim::ContactBox> BaseBoxes;
	int32 FurnitureSeen = 0;
	TUniquePtr<FSonyPad> Pad;          // a PlayStation pad read directly (Unreal reads Xbox pads itself)
	FRiderRig RiderRig;                // the skinned rider, when the level script has imported it
	// The rider as their MetaHuman (Profile page; Shed/TorqueAvatar.h), in the rider rig's place: it
	// reaches for the joints RiderRig was posed to on this bike (seat, grips, pegs), the helmet on its
	// head. The rig's own kit body is hidden then. -TorqueKitRider keeps the kit rider.
	FTorqueAvatar RiderAvatar;
	int32 RiderAvatarFor = -2;
	TArray<uint8> RiderAvatarLook;
	// [co-op, foot] Someone else off their bike: their MetaHuman standing and walking where their
	// snapshot says they are (NetBike), shown only while they're off it. Ours is first person.
	TUniquePtr<FTorqueAvatar> NetWalker;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> NetWalkerRoot = nullptr;
	TWeakObjectPtr<const APlayerState> NetWalkerFor;   // [co-op] whose body NetWalker is (bikes can be swapped)
	FVector NetWalkerWas = FVector::ZeroVector;
	bool bNetWalkerShown = false, bNetWalkerTried = false;
	void ShowNetWalker(bool bOn, const FVector& Feet, double Yaw, float Dt);
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
	// [yard, co-op] SetYardBay's: the bay to draw, before anyone asks whose bike it is. Set by the host
	// before BeginPlay (the yard's spawner, and ATorqueGameMode on the road from the rider's player
	// state) and sent with the bike, so every machine draws each rider's own bike from the start.
	UPROPERTY(Replicated) int32 YardBay = INDEX_NONE;
	bool bYard = false;           // [yard] InYard
	FBox2D YardOpen = FBox2D(ForceInit);   // [yard] the open concrete (sim x, z) a fallen rider goes back on
	void BackOnWheels();          // after a crash or Triangle: back on the road (the yard: where he fell)
	TArray<float> SoundFloat;
	TArray<int16> SoundPcm;
	int32 LastGear = 1;
	bool bManual = false;            // gears changed by hand (the first press of a shift button)
	bool bAbs = true;                // the ABS switched on; off lets the rear lock
	bool bHasAbs[2] = {true, true};  // the bike has ABS at the front, the rear (a catalogue bike's ECU may not)
	double GearNoteFor = 0.0;
	// A word on the HUD for a moment: "Engine off", "Out of petrol!" (EngineNote, for EngineNoteFor s).
	FString EngineNote;
	double EngineNoteFor = 0.0;
	bool bEngineWas = true;   // the sim's engine running last frame (to hear it catch or stop)
	// Performance: P shows frame, game, render and GPU times; O (or -TorqueProfile) logs them each
	// second to Saved/Profiling/torque_perf_<time>.csv.
	bool bPerfShow = false, bPerfLog = false;
	double PerfClock = 0.0, PerfSum = 0.0, PerfWorst = 0.0, PerfRun = 0.0;
	int32 PerfFrames = 0;
	FString PerfText, PerfFile;
	void UpdatePerf(float Dt);
	TArray<FHudLine> HudLines;
	FTorqueDash Dash;   // [dash]
	FTorqueLights Lights;   // [lights]
	void StepLights(float Dt, bool bRemote);
	void LightsNote(const FString& Text, bool bOk);
	// [dash] The trip check: every 10 s of riding, a log line with the dash's trip next to speed x
	// time, the route's progress and how far the clock says (TripLog).
	double TripLogClock = 0.0, TripSpeedKm = 0.0, TripLogTime = 0.0;
	void TripLog(const TCHAR* Why);
	bool bShowHelp = true;
	bool bPointing = false;   // [info] the pointer out to pick a place in the info panel (M)
	bool bHudLines = false;   // the riding info lines top left (F2); a clean screen to start with (beenster, Nov 12)
	// Weather: 0 sunny, 1 overcast, 2 wet (-TorqueWeather=sunny|overcast|wet; V or D-pad right cycles).
	// ATorqueGameState owns it (the host's, in co-op) and lights the world; the bike takes the grip.
	int32 Weather = 0;
	double HostOnlyNoteFor = 0.0;      // "the host sets the weather", on a joined rider's HUD
	void ApplyWeather();
	// The heat's air, road and wetness from the weather and the time of day (BikeHeat ambient_c,
	// road_c); bSoak: the whole bike at the shed's temperature (a ride starting from the shed).
	void WeatherHeat(bool bSoak);
	double Accum = 0.0;
	FVector PrevPos, CurPos;          // Unreal cm, the sim body origin before and after the last step
	FQuat PrevRot, CurRot;

	// The route, sim metres.
	TArray<FVector> Line;             // (x, y, z) sim
	double RouteLengthM = 3000.0;     // route.json length_m
	double RouteFinishM = 3000.0;     // route.json finish_m: where a planned ride's last pin is (its length where it has none)
	int32 Near = 0;
	int32 StartIndex = 0;             // where the ride starts on Line (-TorqueStartKm)
	double RideTime = 0.0;
	bool bFinished = false;

	// Smoothed rider inputs from keys, pad and wheel.
	double Throttle = 0, FrontBrake = 0, RearBrake = 0, Lean = 0, Shift = 0;
	double CrashedFor = 0.0;
	// The respawn ghost's look (beenster, Nov 11: "only show the green mesh and then fade to solid"):
	// first the bike and rider drawn only as a green wireframe of their own polygons (M_RideGhostMatrix
	// in place of every material; Tools/ue_build_ghost.py), then for the last GHOST_FADE s their own
	// materials under a green fill (M_RideGhostFill, the overlay) clearing to solid. Never blinks.
	// Only meshes with no overlay get the fill, and only it is taken off again.
	UPROPERTY(Transient) UMaterialInstanceDynamic* GhostMid = nullptr;       // the wire
	UPROPERTY(Transient) UMaterialInstanceDynamic* GhostFillMid = nullptr;   // the fade's fill
	bool bGhostTried = false;    // looked for the materials (once)
	int32 GhostMode = 0;         // 0 solid, 1 wire only, 2 fading to solid
	// The meshes given the wire and their own materials (GhostSavedMats[GhostMatStart[i]..] for
	// GhostComps[i]), held here so nothing frees them meanwhile.
	UPROPERTY(Transient) TArray<UMeshComponent*> GhostComps;
	UPROPERTY(Transient) TArray<UMaterialInterface*> GhostSavedMats;
	TArray<int32> GhostMatStart;
	TArray<int32> GhostLodWas;   // each wired mesh's forced LOD before (the wire uses the coarsest)
	TArray<TWeakObjectPtr<USceneComponent>> GhostHairHidden;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> GhostNaniteOff;   // drawn without Nanite for the ghost
	void UpdateGhostLook();
	void GhostRestore();

	// Camera.
	// 0 third person (chase), 1 first person (rider's eyes), 2 chin camera, 3 showroom orbit, 4 drive-by.
	// The Camera key cycles first person, third person, drive-by (beenster); 2 and 3 are the tests'
	// (-TorqueCamera=, TestCamera). Falling off switches to drive-by till you're back on (bCrashCam).
	int32 CameraMode = 0;
	static constexpr int32 CameraModes = 5;
	static constexpr int32 CamChase = 0, CamFirst = 1, CamDriveBy = 4;
	void NextCamera();
	bool bCrashCam = false;           // drive-by because you fell off: back to CrashCamFrom when you're on again
	int32 CrashCamFrom = 0;
	// Third person, set by the player (FTorqueProfile::ChaseDist/Height/Tilt, saved with the character).
	// Pad: R1 held with the right stick (up / down height, left / right distance) and the D-pad (tilt).
	bool AdjustChase(const APlayerController* PC, float Dt, double PadX, double PadY, bool bPadHeld, int32 PadTilt);
	double ChaseSaveIn = -1.0;        // seconds till the changed camera is saved (-1: nothing to save)
	double CamNoteFor = 0.0;          // the view's name on screen after a change
	FString CamNote;
	// Drive-by: a camera standing at the roadside ahead, turning to watch you go past, then moving on
	// down the road ahead of you (or, falling off, watching where you're sliding to).
	void PlaceDriveBy(bool bCrash);
	void UpdateDriveBy(float Dt);
	bool bDriveByPlaced = false;
	// First person on any bike (beenster): how far down the view looks and how wide, set so the
	// grips (and so the gloves on them) sit near the bottom of the picture whatever the bars' height.
	void FirstPersonAim(const FVector& EyeLocal, double& OutPitch, double& OutFovAdd) const;
	double FpPitch = -21.0, FpFovAdd = 0.0;
	bool bFpAimInit = false;
	FVector DriveByPos = FVector::ZeroVector, DriveByLook = FVector::ZeroVector;
	double DriveByFov = 40.0, DriveByBlocked = 0.0, DriveBySide = 1.0;
	double OrbitYaw = 40.0;           // showroom: degrees round from straight ahead, to the right
	double OrbitDist = 270.0, OrbitFov = 40.0, OrbitRise = 0.0;
	int32 OrbitAt = 0;   // TestCamera's: 0 the bike, 1 the left hand, 2 the right   // the showroom's distance (cm) and field of view
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
	// [foot] Off the bike: where you stand (world cm, at your feet) and look; where the bike's parked
	// (sim frame: the ground under it, its heading, which stand, how far it leans or is lifted); the
	// catalogue bike's stands (their parts, and each stand surface's place as drawn, folded).
	bool bOnFoot = false, bParkedLook = false;
	FVector FootPos = FVector::ZeroVector;
	double FootYaw = 0.0, FootPitch = 0.0, OnFootFor = 0.0, FootNoteFor = 0.0;
	bool bMouseTaken = false;   // ReadInput has put the game in charge of the mouse (FInputModeGameOnly)
	double FootBlockLogAt = -10.0;   // OnFootFor when a stopped step was last logged
	double FootMouseLogAt = -10.0;   // OnFootFor when the mouse's look was last logged
	FString FootNote;
	TorqueSim::V3 ParkGround;
	double ParkHeading = 0.0, ParkLean = 0.0, ParkLift = 0.0;
	int32 ParkStand = 0;   // 0 none, 1 side stand, 2 centre stand
	FString SideStandPart, CentreStandPart;
	TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>> StandRest;
	void ChooseStand();
	bool GetOff();
	bool GetOn();
	void HoldParked();
	void StepFoot(APlayerController* PC, double Dt, double MoveX, double MoveY, double PadLookX = 0.0, double PadLookY = 0.0, bool bPadRun = false);
	void PoseStands(int32 Stand);
	// Wheeling it on foot (hold F by it): it follows alongside, upright, the stand up; F lets go.
	bool bPushing = false, bUseArmed = false;
	double UseHeld = 0.0;
	// Riding, Triangle / Y (beenster's pad layout): stopped, a tap gets off; moving, held a moment is
	// back on the road; crashed, a tap is back on the road at once.
	bool bPadUseRideArmed = false;
	double PadUseRideHeld = 0.0;
	// [gestures] When the nod began and the wave last started or stopped (world seconds), and R3's hold.
	static constexpr double NodTime = 0.7, WaveEase = 0.3, WaveHoldPad = 0.3;
	double NodAt = -100.0, WaveAt = -100.0, PadR3Held = 0.0;
	bool bWaving = false, bNetNodWas = false;
	void StartNod();
	void SetWaving(bool bWave);
	double GestureNow() const;
	// The View / Create button: tapped, the next camera; held with the right stick or D-pad up and
	// down, the third-person camera's distance, height and tilt (whether it did, so the let-go isn't a tap).
	bool bPadViewWas = false, bPadViewUsed = false;
	void TakeHoldOfBike();
	void LetGoBike();
	void FollowPusher();
	FString CatalogueSpec;             // the spec, as logged
	int32 GroupParts[6] = {0, 0, 0, 0, 0, 0};   // its meshes on the bike, steering, fork lowers, front wheel, swingarm, rear wheel
	bool bTestAutoRide = false;
	bool bTestStopping = false;   // TestStop: the robot brakes instead of holding its speed
	double TestGripThrottleHold = -1.0;   // TestGripThrottle's roll (below 0: none)
	double TestLeverHold[2] = {-1.0, -1.0};   // TestLever's hold, per side (below 0: none)
	bool bTestClutch = false;   // TestControl's clutch, held
	// The pad's starter (LB + RB held): how long both have been held, and whether it has fired this hold.
	double PadStartHeld = 0.0;
	bool bPadStartDone = false;
	void PressIgnition();   // the key (I): off, or the starter
	// TestStartMove's manoeuvre: which (-1 none), its phase (0 getting up to speed, 1 measuring), the
	// speed it's held at (m/s), and what it has measured so far.
	int32 TestMove = -1, TestPhase = 0, TestFromNear = 0;
	double TestMps = 0.0, TestClock = 0.0, TestSteady = 0.0, TestDist = 0.0, TestV0 = 0.0, TestSide = 1.0;
	double TestNose = 0.0, TestLeanSum = 0.0, TestGSum = 0.0, TestLeanPeak = 0.0, TestGPeak = 0.0, TestSlide = 0.0, TestLateLean = 0.0;
	int32 TestSamples = 0, TestLateSamples = 0;
	bool bTestRobotWas = false, bTestTarmac = false, bTestManualWas = false;
	double TestT30 = -1.0, TestT60 = -1.0, TestT100k = -1.0, TestStill = 0.0;
	// The test pad: while set, the wheels see a level tarmac plane at TestPadY (sim metres) and nothing
	// else, so a manoeuvre measures the bike as SimTest/realism_test's flat ground does.
	bool bTestFlat = false, bTestWantFlat = false;
	double TestPadY = 0.0, TestY0 = 0.0, TestAssistWas = 0.6;
	FString TestMoveText;
	void TestMoveStep();
	void HoldTestSpeed(double V);   // the robot's throttle and back brake holding TestMps
	double TestFirstHit = 0.0, TestPreHit = 0.0;   // the first knock's speed into it, and the speed the step before
	bool TestFirstBike = false;
	// Kind 2 and 3's wall or stopped bike (only these, on the pad), the hits before it, and where it is.
	TorqueSim::ContactWorld TestContacts;
	TUniquePtr<TorqueSim::BikeSim> TestParked;
	int32 TestHitsWas = 0;
	double TestFace = 0.0, TestAfterHit = -1.0;
	FVector2D TestAt = FVector2D::ZeroVector, TestFwd = FVector2D::ZeroVector;
	// Where its rider sits (pawn local cm): the seat's surface under the hips, the pegs (0 left, 1
	// right) and the grips' centres, from the layout (as Torque Shed sits its rider: BikeRig's hip
	// and peg) and the grips as drawn, with geometry mode's offsets. FitRiderToBike turns them into
	// the rig's pose input, once the rig is built: SeatFit, PegFit and GripFit against the Speed
	// Twin the rig was made on, and the back leant so the hands reach the bars.
	bool bRiderOnLayout = false, bRiderFitted = false;
	FVector RiderSeat = FVector::ZeroVector, RiderPeg[2] = {FVector::ZeroVector, FVector::ZeroVector};
	double RiderTankRearX = 1.0e9;   // the drawn tank's back edge along x (pawn cm), for the MetaHuman's seat
	// [rider pose] Where the knees go (BikeRig's knee, 0 left, 1 right), the tank's half-width along
	// x (FBikeFrame::TankHalfW), and how far the feet are down off the pegs (the last pose's).
	FVector RiderKnee[2] = {FVector::ZeroVector, FVector::ZeroVector};
	TArray<FVector2D> RiderTankHalfW;
	double RiderFeetDown = 0.0;
	// [rider pose] Stopped on a bike too tall to reach the road from (beenster, MX): the bike leant over
	// onto the left foot about its tyres' contact line, deg (left side down), the right foot on its peg.
	double StopLeanDeg = 0.0;
	double RiderKeepOnPeg[2] = {0.0, 0.0};
	double RiderToes[2] = {0.0, 0.0};   // [rider pose] how far each heel is up off the road at a stop (on the toes), cm
	double RiderStyleBackDeg = -100.0;   // [rider pose] the style's back lean (FBikeFrame::BackDeg)
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
	// [rider pose] A catalogue bike's drawn clutch and brake levers (they swing to the bar as pulled), as
	// first seen on their groups.
	TWeakObjectPtr<UStaticMeshComponent> CatLever[2];
	FTransform CatLeverRest[2];
	bool bCatLeverRest[2] = {false, false};
	double PullAwayLogT = -1.0, PullAwayLogNext = 0.0;   // [rider pose] the clutch hand logged for 3 s after pulling away from a stop
	double ClutchFingers = 0.0;        // [rider pose] two fingers over the clutch lever (it's in, being let out, a shift, stopped)
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
	// [Ride UK stage 7] The road through the pad: a cattle grid's bars, the knock of an iron cover or a
	// bridge's joint, grit and grass, a tyre sliding. A PlayStation pad over HID (FSonyPad), an Xbox
	// pad through the engine's force feedback.
	void UpdateRumble(float Dt);
	float RumbleKnock = 0.0f;
	int32 RumbleSurfWas[2] = {0, 0};
	uint64 RumbleHandle[2] = {0, 0};
	void LoadRoute();
	void BuildModel();
	bool BuildCatalogueModel();   // true if the bike is all catalogue parts and was drawn so
	// SideM: metres to the right of the line (co-op start slots).
	void Respawn(int32 LineIndex, double SideM = 0.0);
	void PlaceOnGround(TorqueSim::V3 Pos, double Heading);   // Respawn's second half: upright there, on the ground
	void Remount();                   // the thrown-off rider back on the bike
	void ReadInput(float Dt);
	void StepSim();
	void UpdateVisuals(double Alpha);
	void UpdateCamera(float Dt);
	void ShowRider(bool bShow);
	void UpdateProgress();
	void DrawHud();
};
