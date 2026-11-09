// Pressure washing a bike by hand: Torque Shed's wash mini game (wash_job.gd, with the muck drawn as
// dirt_layer.gd's wash mask does it). The bike in the wash bay is covered in points (each part's own
// triangles, TorqueShedPick), one for each side of its surface the jet can get to, each on a part.
//
// Where the jet lands it washes the muck off the surfaces facing it (a surface side on to it barely
// gets wet, one facing away not at all, so you go round the bike, and raise it on the wash bay's
// lift and crouch for underneath), as wide as the distance makes it and weaker the further off you
// are; caked muck takes longer than a film of dust. What's washed off shows on the bike there and
// then, exactly where the jet went: the space round the bike is split into little cubes, each
// holding how much has been washed off the surfaces in it facing each of six ways, sent up to the GPU
// as two small atlases that M_WashDirt (an overlay on every part) reads to draw the muck still on it.
// X lights up the muck still to do.
//
// The shed hears every moment how much of each part's muck has come off (a Wash request whose
// Amounts are each part's dirt left as a share of what it last heard), so the whole crew see it come
// clean and two of you can wash the same bike at once. A part is clean once all of it is: a cue, and
// it's called clean to the shed (Amount 0). The whole bike clean: a bigger one.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;
class UTorqueBuildSubsystem;
class UWorld;
struct FTorqueShedRequest;

namespace TorqueWash
{
// wash_job.gd, in cm and seconds.
constexpr double SampleCell = 4.5;          // the surface is sampled this far apart
constexpr int32 MaxPoints = 9000;
constexpr double JetRadius = 12.0;       // the jet's footprint at DRef (a fan tip: a good wide stripe)
constexpr double DRef = 60.0;
constexpr double WashRate = 14.0;         // washed off a second in the middle of the jet (less for caked muck)
constexpr double JetFalloff = 0.35;      // weaker to the edge of the footprint
constexpr double Caked = 1.0;         // how much longer caked muck takes than a film of dust (see Grip)
constexpr double NearDist = 15.0;         // closer than this the footprint's tiny
constexpr double FarDist = 120.0;         // further than this it starts to lose its bite
constexpr double OutDist = 260.0;     // and past this it doesn't reach at all
constexpr double Skim = 0.1;          // how square on the jet has to meet a surface: none at all this side on,
constexpr double FaceOn = 0.5;        // all of it by this (cosines)
constexpr float DoneShare = 0.975f;    // this much of a part clean and it's done
constexpr float Specks = 0.02f;       // or all but this share of its patches (at least SpecksMin)
constexpr int32 SpecksMin = 3;
constexpr float CreditSpan = 0.3f;    // a patch this much dirtier than clean counts for nothing
constexpr float CleanLevel = 0.04f;        // a patch this clean or cleaner counts as clean (Dirt.CLEAN)
constexpr double WayOut = 40.0;       // clear of the bike this far out from a point, and the jet can get to it
// The mask's cubes.
constexpr double MinVoxel = 3.0;
constexpr int32 MaskMaxDim = 64;

enum class EZone : uint8 { Close, Good, Far, Out };

// How the jet lands at a distance: its zone, how wide, how much bite it has (0..1).
struct FJetShape
{
	EZone Zone = EZone::Out;
	double Radius = JetRadius, Power = 0.0;
};
FJetShape JetAt(double Dist);

// The jet this frame: where it meets anything (bHit), whether that's the bike (bBike, on part Part).
struct FJet
{
	bool bHit = false, bBike = false;
	FVector At = FVector::ZeroVector, Normal = FVector::UpVector;
	double Dist = 0.0, Radius = TorqueWash::JetRadius;
	EZone Zone = EZone::Out;
	int32 Part = INDEX_NONE;
};

struct FPartStat
{
	int32 Part = INDEX_NONE;
	FString Name;
	float Clean = 0.0f;
	bool bDone = false;
};

// How the wash is going: the whole bike, each part (dirtiest first), and each side as seen from where
// you are (this side, the far side, the top, underneath; -1 for none).
struct FStats
{
	float Clean = 0.0f;
	TArray<FPartStat> Parts;
	float Sides[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
};

struct FCue
{
	bool bBike = false;   // the whole bike's clean (else a part)
	FString Name;
};

// Whether a wash game is drawing bay Bay's muck itself (the parts' own grime is held off).
bool HidesGrime(const UObject* WorldContext, int32 Bay);
// How hard muck as thick as Level (0..1) holds on: the jet's rate is divided by this.
inline double Grip(double Level) { return 0.4 + Caked * Level; }
// How much of the jet lands on a surface it meets at Facing (cosine): none side on, all square on.
inline double Take(double Facing) { return FMath::Clamp((Facing - Skim) / (FaceOn - Skim), 0.0, 1.0); }
const TCHAR* ZoneWord(EZone Z);
}

class FTorqueWashJob
{
public:
	FTorqueWashJob();
	~FTorqueWashJob();

	int32 Bay = INDEX_NONE;
	TorqueWash::FJet Jet;
	TArray<TorqueWash::FCue> Cues;
	bool bBikeDone = false;

	// Starts on bay Bay's bike as it's drawn now. False if there's nothing of it to wash.
	bool Setup(UWorld* World, int32 InBay);
	// Each frame: finds the bike's meshes (redrawn whenever the shed changes) and keeps the muck on
	// them, moving with the bike (the lift raises it). False if the bike has gone.
	bool Attach(UWorld* World);
	// Takes the muck off the meshes (the job's done with): the bike's own grime shows again.
	void Detach();
	// One frame of washing from Eye along Dir (world); the jet meets the world at WorldAt (bWorldHit).
	// Not bFiring it only works out where the jet would land.
	void Step(double Dt, const FVector& Eye, const FVector& Dir, bool bFiring, bool bWorldHit, const FVector& WorldAt, const FVector& WorldNormal);
	TorqueWash::FStats Stats() const;
	float PartClean(int32 Part) const;
	bool PartDone(int32 Part) const { return Done.IsValidIndex(Part) && Done[Part]; }
	FString PartName(int32 Part) const { return Names.IsValidIndex(Part) ? Names[Part] : FString(); }
	int32 PartCount() const { return Items.Num(); }
	// What's been washed off since the last time, for the shed: a Wash request (Items, Amounts), or false.
	bool TakeUpdate(FTorqueShedRequest& Out);
	// The shed's dirt as it stands (someone else washing, the parts washer): shown here too.
	void Resync(const UTorqueBuildSubsystem& B);
	void Tick(double Dt);                         // the scan's pulse, the flashes, uploads to the GPU
	void SetScan(bool bOn) { bScanOn = bOn; }
	bool Owns(const UStaticMeshComponent* C) const;
	FVector Middle() const;                       // the bike's middle (world)
	// For the tests: sweep the jet over the bike in rows from each side (and from above, and from
	// below when it's raised), Distance cm off, as a player would, Passes times. Returns the steps
	// that landed on the bike.
	int32 TestSweep(int32 Passes, double Distance, bool bUnder);

private:
	struct FShown
	{
		TWeakObjectPtr<UStaticMeshComponent> C;
		TStrongObjectPtr<UMaterialInstanceDynamic> M;
		int32 Part = INDEX_NONE;
	};
	TWeakObjectPtr<UWorld> WorldOf;
	FTransform Model = FTransform::Identity;      // the bike actor's frame, unscaled
	// Each part (by slot): its item, name, the muck on it when the wash began (evened up by the
	// shed's since), how much of it the shed last heard was left (a share), done, flashing.
	TArray<int32> Items;
	TArray<FName> Slots;
	TArray<FString> Names;
	TArray<float> Start, Sent, PatMean, Flash;
	TArray<int32> PartN;
	TArray<bool> Done, Dinged;
	TArray<int32> ToCallClean;
	// The points (model cm), the way each faces out, its part, cube, how much muck it's laid on with
	// (its share of its part's, by where it is on the bike: low down and underneath most).
	TArray<FVector> Points, Normals;
	TArray<int32> PointPart, Vox;
	TArray<float> Pat;
	double ZLo = 0.0, ZHi = 100.0;
	// The mask (model cm): how much has been washed off each cube's surfaces facing each way (0..1).
	double Vx = TorqueWash::MinVoxel;
	FVector Origin = FVector::ZeroVector;
	FIntVector Dims = FIntVector(1, 1, 1);
	int32 Grid = 1;
	FIntPoint AtlasSize = FIntPoint(1, 1);
	TArray<float> Washed;
	TArray<uint8> Occupied;
	TArray<uint8> BytesA, BytesB;
	int32 DirtyLo = MAX_int32, DirtyHi = -1;
	double SinceUpload = 0.0;
	TStrongObjectPtr<UTexture2D> TexA, TexB;
	TArray<FShown> Shown;
	bool bHolding = false;                        // HidesGrime says so (the overlay's on)
	bool bScanOn = false;
	double ScanAmt = 0.0, ScanT = 0.0;
	FVector Eye = FVector(TNumericLimits<double>::Max());
	FVector Mid = FVector::ZeroVector;
	mutable TorqueWash::FStats Cached;
	mutable double StatsAge = 1.0;

	int32 IndexOf(const FVector& P) const;
	float WashedFacing(int32 I, const FVector& N) const;
	float Remaining(int32 Point) const;           // the muck left on a point, 0..1
	float Credit(int32 Point) const;
	bool ClearWay(const FVector& P, const FVector& D) const;
	void Store(int32 I);
	void Upload(bool bAll);
	void Wash(const FVector& At, const FVector& Along, double Reach, double Amount);
	void CheckDone(const TSet<int32>& Touched);
	bool CleanEnough(int32 Part, float CleanShare) const;
	void Hold(bool bOn);
	void Relook();
	UMaterialInstanceDynamic* MidFor(UStaticMeshComponent* C, int32 Part);
	void ApplyParams();
	static UMaterialInterface* DirtMaterial();
};
