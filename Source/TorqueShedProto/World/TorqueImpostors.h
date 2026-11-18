// [perf, build 157] Impostors for Ride UK's far trees, made inside the game at ride start.
//
// Round 153's far trees (Buttertubs' low-poly models) looked "ridiculous" (beenster), and the full
// scanned trees cost ~9 ms of GPU past 120-200 m. An impostor draws a far tree as one camera-facing card
// showing pictures of the real scan from 64 directions (a hemi-octahedral 8 x 8 atlas, blended by
// M_RideImpostor). Rounds 154-156 tried to take those pictures in the editor, and the editor bake
// crashed beenster's GPU twice (155: an orthographic capture; 156: the bake script re-entering its own
// setup while the material compiled). So since 157 the game takes them itself, while riding, with the
// same kind of scene capture the shop's previews already use every day:
//
//  - a far tree of a scanned kind asks Card()/Material() for its scan: the first ask makes the card (a
//    runtime static mesh as tall and wide as the scan), three atlases (render targets) and a material
//    instance reading them, and queues the scan for its pictures. Until they are taken the atlas's depth
//    says "nothing here", so the card draws nothing (no black cards);
//  - Tick() takes torque.RideImpostorViewsPerTick views a frame (each a colour, a normal and a depth
//    picture of 128 px) of one tree at a time, 1 km above the ride, seen by nothing else;
//  - a file in Saved/ marks a bake in progress. If the game dies during one, the next start leaves
//    impostors off (the old far models instead) and says so in the log; -TorqueImpostors=1 tries again.
//
// Knobs: torque.RideImpostors 0 (or -TorqueImpostors=0) = the old far models; torque.RideImpostorViewsPerTick;
// torque.RideImpostorNormal01 / RideImpostorGamma / RideImpostorBrightness for the look.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TorqueImpostors.generated.h"

class AActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

USTRUCT()
struct FTorqueImpostor
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Scan;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Card;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Colour;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Normal;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Depth;
	FVector Centre = FVector::ZeroVector;   // (the scan's bounds, in its own space)
	float Radius = 0.0f;
	bool bBaked = false;
};

UCLASS()
class UTorqueImpostors : public UObject
{
	GENERATED_BODY()
public:
	// Once, as the ride starts: whether impostors are on this time (cvar, command line, the crash mark).
	void Start(AActor* InOwner);
	bool IsOn() const { return bOn; }
	// The card and material that draw this scan far off (made and queued on the first ask); null if off.
	UStaticMesh* Card(UStaticMesh* Scan);
	UMaterialInterface* Material(UStaticMesh* Scan);
	// Every frame from the ride world's Tick: a few views of the tree being baked.
	void Tick();
	// The ride ends: the bake was not a crash, so the mark goes.
	void Stop();

	int32 Baked() const;
	int32 Queued() const { return Entries.Num(); }

private:
	FTorqueImpostor* Entry(UStaticMesh* Scan);
	bool MakeRig();
	void BeginTree(FTorqueImpostor& E);
	void CopyPending();
	void Mark(bool bOnNow);

	UPROPERTY(Transient) TObjectPtr<AActor> Owner;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> Master;
	UPROPERTY(Transient) TArray<FTorqueImpostor> Entries;
	// The rig: the tree being photographed, and per view slot a capture for colour, normal and depth,
	// each with a picture of its own (so nothing depends on when Unreal renders a capture in the frame).
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Tree;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> Shots;

	bool bOn = false;
	bool bInTick = false;     // (re-entry guard: nothing in Tick may run Tick again)
	bool bRigFailed = false;
	bool bMarked = false;
	int32 Current = -1;       // the entry being baked
	int32 Warm = 0;           // ticks left to let the tree's textures and Nanite pages stream in
	int32 NextView = 0;       // its next view (0..63)
	// The views captured last tick, copied into the atlases this tick: (slot, view)
	TArray<FIntPoint> Pending;
	int32 PendingEntry = -1;
	double StartedAt = 0.0;
};
