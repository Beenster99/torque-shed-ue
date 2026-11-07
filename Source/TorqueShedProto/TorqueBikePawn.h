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
#include "TorqueBikePawn.generated.h"

namespace TorqueSim { class BikeSim; }
class UStaticMeshComponent;
class UCameraComponent;

UCLASS()
class ATorqueBikePawn : public APawn
{
	GENERATED_BODY()

public:
	ATorqueBikePawn();
	virtual ~ATorqueBikePawn() override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

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

private:
	TUniquePtr<TorqueSim::BikeSim> Sim;
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
	int32 CameraMode = 0;             // 0 chase, 1 onboard
	FVector CamPos = FVector::ZeroVector;
	FVector CamLookAt = FVector::ZeroVector;
	bool bCamInit = false;

	// Model geometry (Unreal local cm), read from speedtwin.json.
	bool bHaveModel = false;
	FVector SteerPoint = FVector::ZeroVector, SteerDir = FVector::UpVector, SwingPivot = FVector::ZeroVector;
	FVector RearRest = FVector::ZeroVector;

	void LoadRoute();
	void BuildModel();
	void Respawn(int32 LineIndex);
	void ReadInput(float Dt);
	void StepSim();
	void UpdateVisuals(double Alpha);
	void UpdateCamera(float Dt);
	void UpdateProgress();
	void DrawHud();
};
