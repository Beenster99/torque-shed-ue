// Tyre smoke and darkies (beenster, Nov 19 2026: "LOTS of smoke to burnouts, so much that you can get
// disoriented in it. Also can we have some darkies laid on the road when we wheelspin or skid"), one per
// machine, spawned by ATorqueGameState beside the weather's effects, for every bike there (ours and the
// co-op riders', from their snapshots: ATorqueBikePawn::TyreScrub).
//
// Smoke: a tyre sliding on tarmac heats up; once hot it pours out smoke, a lot of it in a burnout. Each
// puff is a soft camera-facing sprite on one instanced mesh (M_TyreSmoke: Tools/ue_build_spray.py; the
// rain's M_RainSprite until it's built), growing from half a metre to four to eight metres across
// and hanging in the air for 9 to 15 seconds, drifting on the wind. With the camera inside the
// cloud, a veil the same colour lies over the view, as thick as the smoke round the lens, so a long
// burnout fogs out everything (the puffs right by the lens fade out into it rather than flicker).
//
// Darkies: the black rubber a tyre lays on the road, flat in it (decals, M_TyreMark), never geometry
// standing on it: a strip along the way the tyre slid, darker the more it slid, a dark patch where it
// spun on the spot.
//
// The budget (PERFORMANCE FIRST): a fixed number of smoke puffs (by Settings > Graphics > Effects, at
// most 300) and of darkies (at most 220, a pool: the oldest fade away as new ones are laid, and ones
// left far behind go). torque.RideTyreSmoke / torque.RideTyreMarks 0 turn either off.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueTyreFx.generated.h"

class UInstancedStaticMeshComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;

UCLASS(NotPlaceable, Transient)
class ATorqueTyreFx : public AActor
{
	GENERATED_BODY()

public:
	ATorqueTyreFx();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	// For the log and the tests: puffs of smoke in the air, darkies on the road, how thick the smoke is
	// round the camera (0 clear .. 1 can't see).
	int32 PuffsShown() const { return Puffs.Num(); }
	int32 MarksShown() const;
	float SmokeAtEye() const { return Veil; }

private:
	// --- Smoke.
	struct FPuff
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 8.0f, Size0 = 40.0f, Size1 = 350.0f, Alpha = 0.4f, Seed = 0.0f;
		float Drag = 0.9f;   // per second: how fast its first fling dies into the air's drift
	};
	TArray<FPuff> Puffs;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Smoke = nullptr;
	int32 PuffPool = 0;        // instances: the veil (0) and the puffs after it
	bool bOwnSmoke = false;    // M_TyreSmoke (it draws the veil as a flat fill); else the rain's sprite
	float Veil = 0.0f;         // how thick the smoke round the camera is, eased
	int32 DrawnSlots = 0;

	// --- Exhaust flames ([beenster, build 160] a sports engine's pops and bangs: ATorqueBikePawn's
	// TakeExhaustFlames / ExhaustTips). Each a few glowing sprites stretched out of the pipe, and a quick
	// flash of light; a fixed handful at once.
	struct FFlame
	{
		TWeakObjectPtr<AActor> Bike;
		float Age = 0.0f, Life = 0.1f, Len = 35.0f, Seed = 0.0f;
		bool bBang = false;
	};
	TArray<FFlame> Flames;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> FlameSprites = nullptr;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FlameLight = nullptr;
	int32 FlameDrawn = 0;
	void TickFlames(float Dt);

	// --- Darkies.
	struct FMark
	{
		UDecalComponent* Decal = nullptr;            // (kept alive by Keep)
		UMaterialInstanceDynamic* Mid = nullptr;
		FVector From = FVector::ZeroVector, To = FVector::ZeroVector, Up = FVector::UpVector;
		FVector Along = FVector::ForwardVector;
		double HalfWidth = 7.0, Laid = 0.0;   // cm; metres of tread slid over it
		double Born = 0.0;                    // world seconds
		float Opacity = 0.0f, Shown = -1.0f, Fade = 1.0f, EndsShown = -1.0f;
		bool bLive = false, bOpen = false, bFading = false;
	};
	TArray<FMark> Marks;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Keep;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MarkMaterial = nullptr;
	int32 MarkPool = 0;
	int32 MarksLaid = 0;          // (for the log)
	bool bTestMarksLaid = false;  // the smoke test's strip and circle

	// Each bike's tyres: how hot they are from sliding (0..1), the smoke owed, and the mark being laid.
	struct FTyreState
	{
		double Heat = 0.0, Debt = 0.0;
		int32 Mark = INDEX_NONE;
	};
	struct FBikeTyres { FTyreState T[2]; };
	TMap<TWeakObjectPtr<AActor>, FBikeTyres> Bikes;

	FVector Eye = FVector::ZeroVector;
	FVector EyeFwd = FVector::ForwardVector;
	FQuat EyeRot = FQuat::Identity;
	double LogClock = 0.0;
	int32 Emitted = 0, EmittedLogged = 0, PeakPuffs = 0;   // (for the log)
	double HottestTyre = 0.0;
	bool bMade = false;

	void Make();
	// A tyre's scrub this frame (ATorqueBikePawn::TyreScrub's, copied).
	struct FScrub
	{
		double SlideMs = 0.0, RoadMs = 0.0, HalfWidthCm = 7.0, RadiusCm = 30.0, SpinMs = 0.0;
		bool bTarmac = true;
		FVector At = FVector::ZeroVector, Fwd = FVector::ForwardVector, Up = FVector::UpVector;
	};
	void TickBikes(float Dt, double Wet, const FVector& Wind);
	void EmitSmoke(FTyreState& St, const FScrub& In, float Dt, double Wet, const FVector& Wind);
	void AddPuff(const FPuff& P);
	void LayMark(FTyreState& St, const FScrub& In, float Dt, double Wet, AActor* Bike);
	void CloseMark(FTyreState& St);
	int32 TakeMark();
	void PlaceMark(FMark& M);
	void TickMarks(float Dt);
	void SimulatePuffs(float Dt, const FVector& Wind);
	void DrawPuffs();
};
