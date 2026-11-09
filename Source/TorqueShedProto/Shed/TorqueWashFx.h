// What you see of the pressure washer's jet (Torque Shed's water_spray.gd, made richer): a glassy
// pencil of water out of the nozzle breaking into a tight flat fan of fast streaks, a fine mist
// hanging all along it and billowing off where it lands, droplets and spray bouncing back off the
// surface (and dripping off it), and on the bike a ring showing the jet's footprint, coloured by how
// far off you're holding it (green the sweet spot, amber very close, blue too far, grey out of reach).
// The bay filling with mist the longer anyone washes is ATorqueWashMist.
//
// The water is camera-facing sprites on one instanced mesh (the spray game's M_SprayMist, each its
// own colour, opacity, softness and seed by its custom data), simulated here, sorted back to front.
// Every walker has one, so you see the others' jets in co-op.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Shed/TorqueWashJob.h"
#include "TorqueWashFx.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

// The jet this frame.
struct FTorqueWashShot
{
	bool bFiring = false;          // water's coming out
	bool bRing = false;            // show the footprint (the lance is up, aimed at the bike)
	FVector Nozzle = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	bool bHit = false;             // it meets a surface At, facing Normal
	FVector At = FVector::ZeroVector, Normal = FVector::UpVector;
	double Radius = TorqueWash::JetRadius;
	TorqueWash::EZone Zone = TorqueWash::EZone::Good;
	bool bMuddy = false;           // it's landing on muck: the splash comes off brown
};

UCLASS()
class UTorqueWashFx : public USceneComponent
{
	GENERATED_BODY()

public:
	UTorqueWashFx();
	void Update(float Dt, const FTorqueWashShot& Shot, const FVector& Eye);
	bool Busy() const { return Bits.Num() > 0 || bRingShown; }
	void SetMesh(UStaticMesh* Mesh) { Plane = Mesh; }

private:
	struct FBit
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 1.0f;
		float Size0 = 1.0f, Size1 = 1.0f;
		float Alpha = 0.1f, Stretch = 1.0f;
		float Soft = 0.5f, Seed = 0.0f, Spin = 0.0f;
		FLinearColor Colour = FLinearColor::White;
		uint8 Kind = 0;   // 0 jet streak, 1 mist, 2 splash droplet, 3 the glassy core, 4 rebound spray, 5 drip
		bool bLands = false;
		FVector Normal = FVector::UpVector;
	};
	TArray<FBit> Bits;
	double Debt[6] = {0, 0, 0, 0, 0, 0};
	double Clock = 0.0;
	bool bRingShown = false;
	bool bMade = false;
	UPROPERTY(Transient) UInstancedStaticMeshComponent* Sprites = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Ring = nullptr;
	UPROPERTY(Transient) UMaterialInstanceDynamic* RingMaterial = nullptr;
	UPROPERTY(Transient) UStaticMesh* Plane = nullptr;
	int32 Pool = 0;
	TArray<FTransform> Xfs;
	TArray<float> Data;
	void Make();
	void Emit(float Dt, const FTorqueWashShot& Shot);
	void Simulate(float Dt);
	void Draw(const FVector& Eye);
	void DrawRing(const FTorqueWashShot& Shot);
};
