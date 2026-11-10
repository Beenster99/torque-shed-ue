// A bike lift (Torque Shed's workshop_upgrades.gd _build_lift, drawn as a workshop's
// electro-hydraulic motorcycle lift table): a low base frame on the floor, a scissor (an inner and
// an outer arm each side, crossing on a pin; fixed pivots at the front, rollers running in tracks at
// the back) pushed up by a hydraulic ram from the power pack under it, the platform (a rubber mat
// on a painted table with a box skirt, checker plate wings each side, a rear wheel drop-out panel,
// a wheel vice that slides to a bike's front tyre and closes on it), a ramp at the back that the bike
// is wheeled up and that folds up as a stop when it rises, and a control box on a stand beside it.
// F on the control box raises or lowers it (a Lift request: TorqueShedLayout place "lift_N"). The
// wash bay's (bWash) is a narrow grated runway, so the jet gets through to underneath.
//
// Whether it's up is the shed's (UTorqueBuildSubsystem::LiftUp); every player's game runs its own
// lifts towards that, so they go up and down smoothly for everyone. Bikes standing on one ride it
// (ATorqueShedBike asks HeightAt): down, its platform is LowTop() off the floor.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Shed/TorqueShedLayout.h"
#include "TorqueBikeLift.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTorqueBuildSubsystem;

UCLASS()
class ATorqueBikeLift : public AActor
{
	GENERATED_BODY()

public:
	ATorqueBikeLift();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float DeltaSeconds) override;

	// A lift at Xf (x along the bike, nose first; its origin on the floor under the bike's middle),
	// raised and lowered as lift LiftId (a bay, or UTorqueBuildSubsystem::WashLift).
	static ATorqueBikeLift* Spawn(UWorld* World, int32 InLiftId, const FTransform& Xf, bool bInWash);
	// Every lift the shed has (the bays' and the wash bay's), where TorqueShedLayout puts them, that
	// isn't in this world yet.
	static void SpawnMissing(UWorld* World);
	// How far up the lift a thing at At is standing on is right now: 0 off a lift, or with it down
	// (workshop_upgrades.gd lift_height_at: within half a metre of a lift's middle).
	static double HeightAt(const UObject* WorldContext, const FVector& At);
	static ATorqueBikeLift* Find(const UObject* WorldContext, int32 InLiftId);

	UPROPERTY(EditAnywhere, Category = "Lift") int32 LiftId = 0;
	UPROPERTY(EditAnywhere, Category = "Lift") bool bWash = false;
	// Where the control box's stand is, in the lift's frame (TorqueShedLayout::LiftPostLocal).
	UPROPERTY(EditAnywhere, Category = "Lift") FVector PostLocal = FVector(-160.0, -125.0, 0.0);

	double Height() const { return H; }
	double LowTop() const { return 17.0; }                // the platform's top when it's down
	double Top() const { return bWash ? 125.0 : 90.0; }   // Wash.LIFT_TOP, GarageUpgrades.LIFT_TOP
	static constexpr double Speed = 30.0;                 // cm a second (LIFT_SPEED)
	static constexpr double ArmLength = 150.0;            // the scissor arms, pivot to pivot

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) USceneComponent* Deck = nullptr;
	// The parts that move as it goes up and down (Pose), and the vice's jaws (Clamp).
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> InnerArms;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> OuterArms;
	UPROPERTY(Transient) UStaticMeshComponent* Pin = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* LowRoller = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* HighRoller = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* RamTube = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* RamBarrel = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* RamRod = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Ramp = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Jaws;
	// The platform's top, its wings and the ramp: ground to a ridden bike while the lift is down (Pose).
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> GroundParts;
	// The wheel vice (its jaws' middle; the stop is in front of it), sliding along the platform to
	// meet the front tyre of the bike on it.
	UPROPERTY(Transient) USceneComponent* Vice = nullptr;
	double H = 0.0;
	double JawGap = 1.0;    // the vice: 1 open, 0 closed on a tyre
	double ViceX = 0.0;     // where along the platform the vice is (cm from its middle)
	bool bSeen = false;     // the shed's word on it has been read once (no click for the start)
	bool bWantUp = false;
	bool bMoving = false;
	int32 Motor = INDEX_NONE;

	// Its sizes (cm): the platform's length and width, the scissor's half spacing.
	double DeckLength() const { return TorqueShedLayout::LiftDeckLength; }
	double DeckWidth() const { return bWash ? 34.0 : 70.0; }
	double ArmY() const { return bWash ? 9.0 : 22.0; }

	UStaticMeshComponent* Piece(USceneComponent* Parent, const FVector& Where, const FVector& SizeCm, const FLinearColor& Colour,
		float Metallic, float Roughness, bool bSolid = false, float Emissive = 0.0f, bool bCylinder = false);
	// A cylinder (Diameter across) from A to B, in the root's frame.
	static void Span(UStaticMeshComponent* C, const FVector& A, const FVector& B, double Diameter);
	void Label(const FString& Text, const FVector& Where, float Size);
	void Pose();
	void PoseJaws();
	// Where the vice wants to be: at the front tyre of the bike on it, or back at rest.
	double ViceWant(const UTorqueBuildSubsystem* B) const;
	double ViceRest() const { return DeckLength() * 0.5 - 40.0; }
};
