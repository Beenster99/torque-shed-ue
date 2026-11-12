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
	float Reach = 0.0f;   // and how much further out than at arm's length (cm: the wheel, Q / E)
	FString Label;        // a shelving unit's label plate ("Brakes", "Barn find"): shed_state.gd's "label"
	uint8 Channel = 0;    // [tv] a TV's channel (TorqueShedTv::EChannel: a bought TV shows them as the wall's do)
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
	double Sit = 0.0;      // a seat: how high you sit on it, cm (0: not one)
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

// [label] A shelving unit's label plate (equipment.gd _build_shelf: 0.7 x 0.14 m, 12 cm under its
// top, on its front): the longest label (LabelMax letters, as label_dialog.gd), whether a model has
// one, what the piece is called with it ("Shelving unit: Brakes", shed_state.gd furniture_name) and
// a label cleaned up as the host keeps it.
constexpr int32 LabelMax = 24;
bool HasLabel(const FModel& M);
// [tv] A TV (equipment.gd's "tv" kind): it shows a channel, right mouse at it changes it, as the wall's.
bool IsTv(const FModel& M);
FString NameOf(const FTorqueFurniture& F);
FString CleanLabel(const FString& Text);

// The model's mesh (null when it isn't imported: the hub's PC runs Tools/ue_import_props.py).
UStaticMesh* Mesh(FName Model);
// How the glTF importer laid the props down (ue_import_props.py importer_frame): the engine stand's
// model is drawn at its transform as it is, so this is zero; kept in one place in case it isn't.
constexpr double ImportYaw = 0.0;

// Where a piece is while Pawn moves it: a big one on the floor in front of them, its front (the end
// you push from) towards them, turned by Turn; a small one in their hands at chest height; a wall one
// held up in front of them.
FTransform MovedXf(const APawn* Pawn, const FModel& M, double Turn, double Reach = 0.0);
// Where it goes down when Pawn lets go of it (the client works it out, the host checks it): a wall
// one against the wall they face, a small one on whatever they look at (a bench, a shelf, the
// floor), a big one where it is. False with why not.
bool PutDownXf(const APawn* Pawn, const FModel& M, double Turn, FTransform& Out, FString& WhyNot, double Reach = 0.0, int32 Self = 0);
// How far out a moved piece can be pushed past arm's length, and in (cm), and a step of the wheel.
constexpr double ReachMin = -30.0, ReachMax = 220.0, ReachStep = 25.0;
// A big piece on the floor snapped as it's placed (beenster, Nov 10): square to the walls when it's
// near square, then flush against a wall (or a bench or machine) it's within a hand of, and butted up
// and lined up with another piece of kit that's square too. Self: its own id (not snapped to itself).
FTransform Snapped(const UWorld* World, const FModel& M, const FTransform& Xf, int32 Self);

// [trolley] A trolley (equipment.gd's "trolley" kind: the parts trolley, the flatbed) carries what's
// put on it (beenster, Nov 12: "the shed trolleys should carry boxes and stacked boxes"): something on
// the floor whose bottom centre At is over its deck, off the floor, rides along when it's moved.
bool IsTrolley(const FModel& M);
bool OnTrolley(const FModel& M, const FTransform& TrolleyXf, const FVector& At);
// Where a trolley being moved is drawn now (its mover's, snapped), or where it stands.
FTransform DrawnXf(const UObject* WorldContext, const FTorqueFurniture& F);

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
	AActor* Rides() const { return RideHolder; }   // the Road-tagged holder of the ridden pieces

private:
	UPROPERTY(Transient) TMap<int32, UStaticMeshComponent*> Drawn;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> FixedDrawn;
	UPROPERTY(Transient) TArray<UTextRenderComponent*> WordsDrawn;
	// [label] The words on each labelled shelving unit's plate, by piece id.
	UPROPERTY(Transient) TMap<int32, UTextRenderComponent*> LabelsDrawn;
	TMap<int32, FString> LabelsText;   // what each says
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	// The pieces the bike rides on are drawn by an actor of their own, tagged Road (the bike's ground
	// trace only stands its wheels on what's tagged Road or Land).
	UPROPERTY(Transient) AActor* RideHolder = nullptr;
	TMap<int32, FName> DrawnModel;
	TSet<FName> Missing;   // models not imported, warned about once
	UStaticMeshComponent* Make(FName Model, const FTransform& Xf, bool bMovable);
	void Paint();
	void DrawLabels();
};
