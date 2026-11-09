// What you see of the spray can's or spray gun's spray (Torque Shed's spray_fx.gd, made richer): the
// atomised paint streaming out of the nozzle and widening to the fan, a soft overspray cloud that
// billows off the part and drifts down and away on the booth's air, fine droplets bouncing back
// off the surface, a haze at the nozzle, and on the surface a ring showing the fan's footprint,
// coloured by how far off you're holding it (green the sweet spot, red too close: it'll run, blue
// too far: it'll go on dry, grey out of reach). Aiming with the spray controls up and the trigger
// off, only the footprint shows. The wet paint building up on the part is the job's coat
// (TorqueSprayJob, M_SprayCoat).
//
// The particles are camera-facing sprites on one instanced mesh (M_SprayMist: soft, lit by the
// room, each its own colour and shape by its custom data), simulated here: a few thousand at most,
// sorted back to front each frame. Every walker has one, so you see the others' spray in co-op.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Shed/TorqueSprayJob.h"
#include "TorqueSprayFx.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

// The spray this frame.
struct FTorqueSprayShot
{
	bool bFiring = false;          // paint's coming out
	bool bAiming = false;          // the spray controls are up: the footprint shows where it would land
	bool bGun = false;             // the gun's wider, softer fan (else the can's tighter cone)
	FVector Nozzle = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	bool bHit = false;             // it meets a surface At, facing Normal
	FVector At = FVector::ZeroVector, Normal = FVector::UpVector;
	double Radius = 10.0;          // the fan's footprint (cm)
	double Dry = 0.0;              // how much of it lands dry (held too far off)
	TorqueSpray::EZone Zone = TorqueSpray::EZone::Good;
	FLinearColor Colour = FLinearColor(0.8f, 0.1f, 0.1f);
	bool bClear = false;           // lacquer: a faint, glassy mist
};

UCLASS()
class UTorqueSprayFx : public USceneComponent
{
	GENERATED_BODY()

public:
	UTorqueSprayFx();
	// Each frame, with the spray as it is (nothing on: what's in the air drifts and fades) and the
	// eye it's seen from.
	void Update(float Dt, const FTorqueSprayShot& Shot, const FVector& Eye);
	bool Busy() const { return Bits.Num() > 0 || bRingShown; }
	// The flat sprite the bits are drawn on (the engine's plane, held by the walker so it's cooked).
	void SetMesh(UStaticMesh* Mesh) { Plane = Mesh; }

private:
	struct FBit
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 1.0f;
		float Size0 = 1.0f, Size1 = 1.0f;     // cm across, start and end
		float Alpha = 0.1f, Stretch = 1.0f;   // how long along its flight, to across
		float Soft = 0.5f, Seed = 0.0f, Spin = 0.0f;
		FLinearColor Colour = FLinearColor::White;
		uint8 Kind = 0;                       // 0 jet, 1 overspray mist, 2 bounce-back droplet, 3 nozzle haze
		bool bLands = false;                  // a jet bit that lands on the surface at the end of its life
		FVector Normal = FVector::UpVector;   // ... facing this way
	};
	TArray<FBit> Bits;
	double Debt[4] = {0, 0, 0, 0};
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
	void Emit(float Dt, const FTorqueSprayShot& Shot);
	void Simulate(float Dt);
	void Draw(const FVector& Eye);
	void DrawRing(const FTorqueSprayShot& Shot);
};
