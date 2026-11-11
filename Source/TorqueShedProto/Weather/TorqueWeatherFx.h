// The weather you see around you (live weather plan, stage 3), one per machine, spawned by
// ATorqueGameState: falling rain around the camera, slanting with the wind and streaking past as you
// ride; rain splashing on the ground; and spray thrown up off wet roads by every bike's tyres
// (yours and the others' in co-op). Out of the rain (under the shed roof, a bridge) it stops.
//
// Everything is camera-facing sprites on instanced meshes with the spray game's M_SprayMist (each
// sprite's colour, opacity, softness and seed in its custom data), so it needs no assets of its own.
// How many: Settings > Graphics > Effects (or the quality preset's).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueWeatherFx.generated.h"

class UInstancedStaticMeshComponent;

UCLASS(NotPlaceable, Transient)
class ATorqueWeatherFx : public AActor
{
	GENERATED_BODY()

public:
	ATorqueWeatherFx();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	// For the log and the tests: drops falling now, splashes and spray in the air.
	int32 DropsShown() const { return Shown; }
	int32 BitsShown() const { return Bits.Num(); }
	// 0 under a roof .. 1 out under the sky (eased).
	float OpenToSky() const { return Open; }

private:
	// Rain: a fixed pool of streaks in a box around the camera, each wrapping round as it leaves the box.
	struct FDrop
	{
		FVector Pos = FVector::ZeroVector;
		float Fall = 900.0f;   // cm/s
		float Seed = 0.0f;
	};
	TArray<FDrop> Drops;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Streaks = nullptr;
	int32 Shown = 0;
	bool bStreaksHidden = true;

	// Splashes and tyre spray: short-lived soft sprites, simulated here, drawn back to front.
	struct FBit
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 0.5f, Size0 = 5.0f, Size1 = 10.0f, Alpha = 0.2f, Seed = 0.0f, Drag = 1.0f, Gravity = 0.0f;
		float Soft = 0.2f;     // 0 a crisp droplet .. 1 a soft puff of mist
		bool bSpray = false;   // (the spray's whiter colour)
	};
	TArray<FBit> Bits;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Puffs = nullptr;
	int32 PuffPool = 0;
	double SplashDebt = 0.0;

	// Where the camera is and how it's moving; and how open to the sky it is (0 under a roof .. 1 out).
	FVector Eye = FVector::ZeroVector, EyeVel = FVector::ZeroVector, LastEye = FVector::ZeroVector;
	bool bHaveEye = false;
	float Open = 1.0f, OpenTarget = 1.0f;
	double ShelterCheckIn = 0.0;

	// The bikes' wheels last frame, for their speed (remote bikes have no simulation here).
	TMap<TWeakObjectPtr<AActor>, FVector> LastWheels;
	TMap<TWeakObjectPtr<AActor>, double> SprayDebt;

	void Make();
	bool bMade = false;
	void TickRain(float Dt, double Show, const FVector& Wind);
	void TickSplashes(float Dt, double Show);
	void TickSpray(float Dt, double Wet);
	void SimulateBits(float Dt);
	void DrawBits();
	bool GroundBelow(const FVector& At, FVector& Hit) const;
};
