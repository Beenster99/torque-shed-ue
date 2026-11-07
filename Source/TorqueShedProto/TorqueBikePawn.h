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
#include "SonyPad.h"
#include "TorqueAudio/RideSound.h"   // the full type, as for the sim
#include "TorqueBikePawn.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class UStaticMesh;
class UMaterialInterface;
class USoundWaveProcedural;
class UAudioComponent;

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

	// The rider, made of simple shapes and posed every frame: hands on the grips, feet on the pegs
	// (or down when stopped), weight and lean as you ride.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> RiderParts;
	// A dial and needles drawn over the clock: speed, and revs on a small dial below.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> ClockParts;
	UPROPERTY() UStaticMesh* ShapeCube = nullptr;
	UPROPERTY() UStaticMesh* ShapeCylinder = nullptr;
	UPROPERTY() UStaticMesh* ShapeSphere = nullptr;

	// Sound: made by TorqueSound::RideSound and fed to Unreal as it plays.
	UPROPERTY(Transient) USoundWaveProcedural* SoundWave = nullptr;
	UPROPERTY(Transient) UAudioComponent* SoundAudio = nullptr;

private:
	TUniquePtr<TorqueSim::BikeSim> Sim;
	TUniquePtr<FSonyPad> Pad;          // a PlayStation pad read directly (Unreal reads Xbox pads itself)
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
	bool bAbs = true;                // the Speed Twin's ABS; off lets the rear lock
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
	double Accum = 0.0;
	FVector PrevPos, CurPos;          // Unreal cm, the sim body origin before and after the last step
	FQuat PrevRot, CurRot;

	// The route, sim metres.
	TArray<FVector> Line;             // (x, y, z) sim
	int32 Near = 0;
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

	void BuildRider(UMaterialInterface* const* Mats);
	void UpdateRider();
	void UpdateClock();
	void ShowHead(bool bShow);
	void StartSound();
	void UpdateSound(float Dt);
	void LoadRoute();
	void BuildModel();
	void Respawn(int32 LineIndex);
	void ReadInput(float Dt);
	void StepSim();
	void UpdateVisuals(double Alpha);
	void UpdateCamera(float Dt);
	void ShowRider(bool bShow);
	void UpdateProgress();
	void DrawHud();
};
