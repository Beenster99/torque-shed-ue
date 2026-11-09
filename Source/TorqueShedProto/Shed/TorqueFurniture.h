// The shed's movable kit and the yard's (beenster, Nov 9 2026: "take a look in torque shed at all of the
// items which are shed items and those which are for outside - we should add those too"): Torque
// Shed's equipment (equipment.gd: shelving, benches, washers, machines, trolleys, seats, TVs), its
// trinkets (trinkets.gd) and its yard kit (ramps, things to jump, walls and fences, cones, posts and
// flags), each one a piece you look at (outlined), hold F on to take hold of and push about, turn with
// R and put down with F, as Torque Shed's furniture is (shed_state.gd furniture, "mover").
//
// What there is: Data/Furniture/models.json (every model, its footprint and how the bike meets it).
// Where the yard's are: Data/Furniture/yard.json (Tools/yard_layout.py: Torque Shed's test area laid
// out on the pad, with the kit a new shed starts with, the pieces built into it, and its markings).
// The models are the props' (Data/Props, /Game/Shed/Props/<id>/SM_<id>, Tools/props/make_props.py and
// make_yard.py).
//
// The pieces are the shed's (UTorqueBuildSubsystem::Furniture, the GrabFurniture, TurnFurniture,
// ReleaseFurniture and OrderFurniture requests, checked and applied in TorqueFurniture.cpp), so the
// host decides and every player's game draws the same: ATorqueFurnishings reads them each frame and
// draws each piece where it stands, or in front of whoever's moving it. The ridden bike rides on
// what's tagged Road (a ramp, a tabletop, a bus's roof) and bumps into the solid ones (ContactBoxes).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <vector>

#include "TorqueFurniture.generated.h"

class APawn;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;

namespace TorqueSim { struct ContactBox; }

// One movable piece (the yard's built-in pieces aren't: TorqueFurniture::Yard().Fixed).
struct FTorqueFurniture
{
	int32 Id = 0;
	FName Model;
	FTransform Xf;        // where it stands: on the floor (or the wall, or a bench top), turned
	int32 Mover = 0;      // the peer moving it, 0: no one
	float Turn = 0.0f;    // while it's moved, how far it's turned from square on to its mover (deg)
};

namespace TorqueFurniture
{
struct FModel
{
	FName Id;
	FName Kind;            // equipment.gd's kinds, "trinket", or "fixture" (the yard's own)
	FString Name, Desc;
	FVector Size = FVector(100.0);   // width (its x), height, depth (its front-back), cm
	bool bYard = false;    // yard kit: lives outside
	bool bWall = false;    // hangs on a wall
	bool bRide = false;    // the bike rides on it
	bool bSolid = false;   // the bike bumps into it
	bool bSold = false;    // on the parts computer's Shed aisle
	bool Small() const { return FMath::Max3(Size.X, Size.Y, Size.Z) < 70.0; }   // carried in your hands
};
const TMap<FName, FModel>& Models();
const FModel* Find(FName Model);
// The kinds in Torque Shed's order and their headings ("Engine stands", "Jump ramps"...).
const TArray<TPair<FName, FString>>& Kinds();
TArray<FName> SoldOfKind(FName Kind);

struct FPlaced
{
	FName Model;
	FTransform Xf;
};
struct FMark
{
	FVector2D At, Size;   // a painted rectangle: its middle, its length (along its yaw) and width, cm
	double Yaw = 0.0;
	FLinearColor Colour;
};
struct FWord
{
	FString Text;
	FVector2D At;
	double Height = 100.0, Yaw = 0.0;
	FLinearColor Colour;
};
struct FYard
{
	TArray<FPlaced> Start, Fixed;
	TArray<FMark> Marks;
	TArray<FWord> Words;
	FBox2D Drop = FBox2D(ForceInit);   // the yard kit drop-off
};
const FYard& Yard();
// The few pieces a new shed has indoors beside its own fixed kit.
const TArray<FPlaced>& ShedStart();

// The model's mesh (null when it isn't imported: the hub's PC runs Tools/ue_import_props.py).
UStaticMesh* Mesh(FName Model);
// How the glTF importer laid the props down (ue_import_props.py importer_frame): the engine stand's
// model is drawn at its transform as it is, so this is zero; kept in one place in case it isn't.
constexpr double ImportYaw = 0.0;

// Where a piece is while Pawn moves it: a big one on the floor in front of them, its front (the end
// you push from) towards them, turned by Turn; a small one in their hands at chest height; a wall one
// held up in front of them.
FTransform MovedXf(const APawn* Pawn, const FModel& M, double Turn);
// Where it goes down when Pawn lets go of it (the client works it out, the host checks it): a wall
// one against the wall they face, a small one on whatever they look at (a bench, a shelf, the
// floor), a big one where it is. False with why not.
bool PutDownXf(const APawn* Pawn, const FModel& M, double Turn, FTransform& Out, FString& WhyNot);

// The ridden bike's contact boxes for the solid pieces (sim frame: Sim/BikeContacts.h), and a
// number that changes whenever they might have (the bike rebuilds its boxes then).
int32 ContactVersion();
void Touch();
void ContactBoxes(const UObject* WorldContext, std::vector<TorqueSim::ContactBox>& Out);
}

// Draws every piece, the movable ones and the yard's own, and the yard's markings.
UCLASS()
class ATorqueFurnishings : public AActor
{
	GENERATED_BODY()

public:
	ATorqueFurnishings();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	static void SpawnMissing(UWorld* World);

	// The movable piece a mesh draws (0: none, or a fixed one).
	int32 PieceOf(const UPrimitiveComponent* C) const;
	// Every movable piece's mesh, by piece id.
	const TMap<int32, UStaticMeshComponent*>& Pieces() const { return Drawn; }

private:
	UPROPERTY(Transient) TMap<int32, UStaticMeshComponent*> Drawn;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> FixedDrawn;
	UPROPERTY(Transient) TArray<UTextRenderComponent*> WordsDrawn;
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	// The pieces the bike rides on are drawn by an actor of their own, tagged Road (the bike's ground
	// trace only stands its wheels on what's tagged Road or Land).
	UPROPERTY(Transient) AActor* RideHolder = nullptr;
	TMap<int32, FName> DrawnModel;
	TSet<FName> Missing;   // models not imported, warned about once
	UStaticMeshComponent* Make(FName Model, const FTransform& Xf, bool bMovable);
	void Paint();
};
