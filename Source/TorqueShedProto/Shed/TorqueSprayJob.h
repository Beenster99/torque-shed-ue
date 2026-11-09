// One coat being sprayed on one part, by hand: Torque Shed's spray mini game (spray_job.gd, with
// the wet paint drawn on the part as spray_marks.gd does it). The part's surface is covered in
// points (its own triangles, TorqueShedPick), and the coat is painted on the part as you go, so
// you see the paint land, the gaps you've missed and where it's run because you overdid it.
//
// How it lays on depends on how you hold it. The fan widens and thins with distance: in the sweet
// spot (Near to Far away, wider with skill) a steady pass lays an even coat; too close and it
// piles on in a tight spot and runs; too far and it lands dry and rough, and past Out it doesn't
// reach at all. Sweep too fast and it goes on thin; linger and it runs. Paint only lands on the
// surfaces facing the gun, so the job is all of the part's outside a spray could get to (not the
// faces down on the floor or shut inside it): you walk round it for the far side and crouch for
// underneath.
//
// The space round the part is split into little cubes, each holding how many coats have landed
// there for each of the six ways a surface in it can face, and how much of that landed dry. Those
// go up to the GPU as two textures (each a grid of the cubes' slices), which M_SprayCoat reads as
// an overlay on the part's meshes: wet and glossy where the fan has been, thin where it's only had
// a dusting, rough and speckled where it landed dry, sagging into drips where it's too thick.
//
// The coats, in order: an optional grey primer, the colour (an engine enamel is a colour too) and
// an optional clear lacquer. The finished coat goes to the host as the usual shed request (Paint,
// Prime or Coat), with how good it came out as its Amount and how much ran as its Tilt.
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueBuildSubsystem.h"   // FTorqueShedRequest, FTorqueItem
#include "UObject/StrongObjectPtr.h"

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;
class UWorld;

namespace TorqueSpray
{
// spray_job.gd, in cm and seconds.
constexpr double Cell = 3.0;          // the coverage samples the surface this far apart
constexpr int32 MaxPoints = 5000;
constexpr float Full = 0.8f;          // coats for a full film (wet paint flows out to fill a thin patch)
constexpr float Done = 0.9f;          // this much covered and the coat's on
constexpr float AutoDone = 0.995f;    // all of it: done there and then, without letting go
constexpr double Flow = 0.9;          // of the tool's stock rate: steady passes, half overlapping, make a good coat
constexpr double DRef = 80.0;         // the fan's stock width and flow are at this distance
constexpr double Near = 50.0;         // closer than this it piles on and runs
constexpr double Far = 130.0;         // further than this it starts to land dry
constexpr double DrySpan = 55.0;      // all of it dry this far past Far
constexpr double Out = 110.0;         // and none of it reaches this far past Far
constexpr double Slow = 15.0;         // sweeping slower than this (cm/s) and it piles on
constexpr double Fast = 220.0;        // faster than this and it goes on thin
constexpr double WayOut = 60.0;       // clear of the part this far out from a point, and a spray can get to it
// walker.gd's tools: the can's round cone and the gun's wider, even fan; coats a second where it's
// pointed; coats before a patch runs.
constexpr double CanRadius = 10.0, GunRadius = 13.0;
constexpr double CanRate = 10.0, GunRate = 7.0;
constexpr double CanRuns = 4.0, GunRuns = 5.5;
constexpr double CanFalloff = 0.35, GunFalloff = 0.15;
// The spray controls (F with the can or gun on a part): a moment to get set, slower, careful
// passes, a slower look and walk, and a slight hand sway that holding your breath steadies.
constexpr double Settle = 1.2;
constexpr double ModeFlow = 0.38;
constexpr double ModeWalk = 0.6;
constexpr double ModeLook = 0.45;
constexpr double Sway = 0.0035;       // radians at rest
constexpr double Breath = 4.0;        // seconds you can hold your breath to steady your hand
// spray_marks.gd: the cubes of the mask.
constexpr double Voxel = 2.0;
constexpr int32 MaxDim = 64;
constexpr float CoatMax = 8.0f;       // coats a cube's byte can hold
constexpr double Skim = 0.08;         // how square on the fan has to meet a surface for any to land
constexpr double FaceOn = 0.55;       // ... and for all of it to
constexpr double DryFor = 1.6;        // seconds after the coat's done that the wet look is let go of

enum class EZone : uint8 { Close, Good, Far, Out };
enum class EPace : uint8 { Slow, Good, Fast };
enum class ELayer : uint8 { Primer, Paint, Clear };
enum class ECue : uint8 { Runs, Dry, Ready, Done };

struct FCue
{
	ECue Kind = ECue::Ready;
	FString Text;
};

// How the fan lands at a distance: its zone, how wide, coats a second at its middle, how much dry.
struct FFan
{
	EZone Zone = EZone::Out;
	double Radius = 0.0, Flow = 0.0, Dry = 0.0;
};

// The jet this frame: where it meets a surface (bHit), how far off, how wide, how fast it's going.
struct FJet
{
	bool bHit = false;
	FVector At = FVector::ZeroVector, Normal = FVector::UpVector;
	double Dist = 0.0, Radius = 0.0, Speed = 0.0, Dry = 0.0;
	EZone Zone = EZone::Out;
	EPace Pace = EPace::Good;
};

// How the coat's going over all of the part a spray can get to, as fractions of it. Sides: how much
// of the side facing you, the far side, the top and underneath is covered (-1: none of the part).
struct FStats
{
	float Covered = 0.0f, Thin = 0.0f, Runs = 0.0f, Dry = 0.0f, Even = 1.0f, Coats = 0.0f;
	int32 Seen = 0;
	float Sides[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
};

// How a coat looks going on: its colour, roughness and metal; a clear lacquer (bKeep: over stripes
// or effects, only a faint gloss so they show through); and what missed bits show (Under, its alpha
// how much it hides the part as drawn) and how rough that is.
struct FLook
{
	FLinearColor Colour = FLinearColor(0.8f, 0.1f, 0.1f);
	float Rough = 0.15f, Metal = 0.0f;
	bool bClear = false, bKeep = false;
	FLinearColor Under = FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
	float UnderRough = 0.85f;
};

// A step of the part's paint job for the HUD: primer, colour, lacquer.
struct FStep
{
	FString Name;
	enum EState : uint8 { Todo, Now, DoneStep, Skip } State = Todo;
	bool bOptional = false;
	float Q = 0.0f;
};

ELayer LayerOf(const FTorqueShedRequest& R);
// How the coat in R looks going on over It, sprayed by Peer (whose gun cup it may be).
FLook LookFor(const UTorqueBuildSubsystem& B, int32 Peer, const FTorqueShedRequest& R, const FTorqueItem& It);
// The colour of the mist for a spray: the paint, grey primer, a clear coat.
FLinearColor MistColour(const UTorqueBuildSubsystem& B, int32 Peer, const FTorqueShedRequest& R);
// The meshes an item is drawn with now (a loose part's, or its slot's on bay Bay's bike): the shed
// redraws them whenever it changes, so look them up again each frame.
void MeshesOf(UWorld* World, int32 Item, int32 Bay, FName Slot, TArray<UStaticMeshComponent*>& Out);
// "SPRAYING  ·  GLOSS RED" names, and the cue words.
const TCHAR* ZoneWord(EZone Z);
}

class FTorqueSprayJob
{
public:
	FTorqueSprayJob();
	~FTorqueSprayJob();

	FString Key;                   // the same coat on the same part (see ATorqueWalker::SprayKey)
	FTorqueShedRequest Request;    // what goes to the host when it's done
	int32 Item = 0;
	int32 Bay = INDEX_NONE;        // a bike part: its bay and slot (else a loose one)
	FName Slot;
	TorqueSpray::ELayer Layer = TorqueSpray::ELayer::Paint;
	bool bLive = false;            // the trigger's on it
	TorqueSpray::FJet Jet;
	TorqueSpray::FStats Last;      // Stats() as of the last step
	TArray<TorqueSpray::FCue> Cues;
	// The tool: fan radius at DRef, flow (coats a second at DRef), falloff to the fan's edge, coats
	// before it runs, and how much wider the sweet spot is.
	double Radius = TorqueSpray::CanRadius, Rate = TorqueSpray::CanRate, Falloff = TorqueSpray::CanFalloff;
	double RunLevel = TorqueSpray::CanRuns, Band = 1.0;

	// Starts on Item drawn by its meshes now. Ignore: actors whose collision isn't in the way of a
	// spray (the part's own, the walker). False if there's nothing to spray.
	bool Setup(UWorld* World, int32 InItem, const FString& InKey, const FTorqueShedRequest& R, int32 InBay, FName InSlot,
		const TorqueSpray::FLook& InLook, double InRunLevel, const TArray<const AActor*>& Ignore);
	// Each frame: finds the part's meshes (redrawn whenever the shed changes) and keeps the coat on
	// them, moving with the part (a hook swings it). False if the part has gone.
	bool Attach(UWorld* World);
	void SetTool(double InRadius, double InRate, double InFalloff, double InBand);
	FVector2D Sweet() const;                     // [near, far] for this tool and skill
	TorqueSpray::FFan FanAt(double Dist) const;
	// Where the line from Eye along Dir first meets the part's own surface (its points): false if not.
	bool SurfaceHit(const FVector& Eye, const FVector& Dir, double Width, FVector& OutAt, FVector& OutNormal, double& OutT) const;
	// One frame of spraying from Eye along Dir, meeting a surface at At (bHit false: nothing). Not
	// bFiring it only works out where the fan would land, how far off and how fast (aiming). Returns
	// the cubes it coated.
	int32 Step(double Dt, const FVector& Eye, const FVector& Dir, bool bHit, const FVector& At, const FVector& Normal, bool bFiring);
	TorqueSpray::FStats Stats() const;
	float Quality(const TorqueSpray::FStats& S) const;
	bool Ready() const;          // enough covered to call it a coat: letting go finishes it
	bool Complete() const;       // all of it: done without letting go
	FTorqueShedRequest Result() const;
	// Takes the coat off the part's meshes (the job's being dropped).
	void Detach();
	// Done (the wet coat flashes and dries off) or given up (bFlash false: off now).
	void Finish(bool bFlash);
	bool Gone() const { return bGone; }
	bool Drying() const { return bDrying; }
	void Tick(double Dt);        // the scan's pulse, the flash, drying, uploads to the GPU
	void SetScan(bool bOn) { bScanOn = bOn; }
	int32 PaintedCount() const;
	bool Reaches(const FVector& WorldAt, double Margin) const;
	TArray<TorqueSpray::FStep> Steps(const UTorqueBuildSubsystem& B) const;
	// The meshes the coat's on now (the walker's highlight leaves them be).
	const TArray<TWeakObjectPtr<UStaticMeshComponent>>& Meshes() const { return Shown; }
	FTransform ModelTransform() const { return Model; }

private:
	// The coverage points (model cm) and the way each faces out; their cube in the mask.
	TArray<FVector> Points, Normals;
	TArray<int32> Vox;
	int32 Dropped = 0;
	// The mask (model cm).
	double Vx = TorqueSpray::Voxel;
	FVector Origin = FVector::ZeroVector;
	FIntVector Dims = FIntVector(1, 1, 1);
	int32 Grid = 1;                                // slices across the atlas
	FIntPoint AtlasSize = FIntPoint(1, 1);
	TArray<float> Coats, DryAmt, Landed;           // Coats: cube * 6 + facing
	TArray<uint8> Occupied;
	TArray<uint8> BytesA, BytesB;                  // the atlases, BGRA
	int32 DirtyLo = MAX_int32, DirtyHi = -1;       // the slices changed since the last upload
	double SinceUpload = 0.0;
	TStrongObjectPtr<UTexture2D> TexA, TexB;
	// The part's frame: what its meshes hang off (the item's holder or the bike), and each of its
	// meshes as it was when the job began, by mesh asset (the meshes come and go, their shapes don't).
	FTransform Model = FTransform::Identity;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Shown;
	TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Mids;
	TorqueSpray::FLook Look;
	FVector LastAt = FVector(TNumericLimits<double>::Max());
	FVector Eye = FVector(TNumericLimits<double>::Max());
	FVector Middle = FVector::ZeroVector;
	double Speed = 0.0;
	bool bBegun = false, bScanOn = false, bDrying = false, bGone = false;
	double ScanAmt = 0.0, ScanT = 0.0, FlashAmt = 0.0, Wet = 1.0, Dried = 0.0;
	TMap<int32, float> Told;   // the cues said, and when (by kind)

	int32 IndexOf(const FVector& P) const;
	float CoatsFacing(int32 I, const FVector& N) const;
	float DryOf(int32 I) const;
	void Store(int32 I);
	bool ClearWay(const FVector& P, const FVector& D) const;
	bool OpenSide(UWorld* World, const FVector& P, const FVector& N, bool bLookRound, const TArray<const AActor*>& Ignore) const;
	int32 Paint(const FVector& At, const FVector& Along, double Reach, double Amount, double Dryness, double InFalloff);
	void Note();
	void Upload(bool bAll);
	void ApplyParams(bool bAll);
	UMaterialInstanceDynamic* MidFor(UStaticMeshComponent* C);
	static UMaterialInterface* CoatMaterial();
};
