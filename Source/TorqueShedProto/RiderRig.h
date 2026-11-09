// The rider: a skinned body in riding kit and a helmet (Tools/make_rider.py, imported by
// Tools/ue_build_level.py), posed every frame from a few riding inputs. Plain C++, no UObject:
// the pawn owns the components and keeps them alive.
//
// The mesh is made in the riding pose, so posing only moves it a little from where it was made.
// Everything is worked out in the pawn's local frame (cm, x forward, y right, z up) as rotations
// of each bone away from its rest pose, then handed to the poseable mesh in its component space.
#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;

struct FRiderPoseInput
{
	FVector ChestShift = FVector::ZeroVector;  // where the top of the back goes from rest, cm
	double HipShift = 0.0;                     // the seat of the pants across the seat, cm
	FQuat BarTurn = FQuat::Identity;           // the handlebars' turn about the steering axis
	FVector BarPivot = FVector::ZeroVector;    // a point on the steering axis
	FVector FootDownL = FVector::ZeroVector;   // ankles when stopped with the feet down
	FVector FootDownR = FVector::ZeroVector;
	double FeetDown = 0.0;                     // 0 on the pegs, 1 down on the road
	FRotator Look = FRotator::ZeroRotator;     // the head's turn, where you look
	double Throttle = 0.0;                     // 0..1: the right hand rolls the grip back
	// Fingers on a lever (0 on the grip, 1 hooked over the lever) and how far it's pulled (0..1):
	// index 0 is the left hand on the clutch, 1 the right on the front brake.
	double LeverFingers[2] = {0.0, 0.0};
	double LeverPull[2] = {0.0, 0.0};
	// Off the bike (0..1): the hands let go of the grips and go to HandsOff (wrists, left and right).
	double Limp = 0.0;
	FVector HandsOff[2] = {FVector::ZeroVector, FVector::ZeroVector};
	// Where the fitted grips sit against the stock ones (pawn local cm, left and right), from the
	// fitment system's grips_l / grips_r placement: the hands follow other bars.
	FTransform GripFit[2] = {FTransform::Identity, FTransform::Identity};
	// The same for the seat and the pegs (pawn local cm), from geometry mode's offsets of the saddle
	// and the rearsets: the hips sit on a moved seat and the feet go to moved pegs.
	FTransform SeatFit = FTransform::Identity;
	FTransform PegFit[2] = {FTransform::Identity, FTransform::Identity};
};

class FRiderRig
{
public:
	// Load the rig (rider.json for the bones, the meshes from their asset paths), add the body and
	// helmet under Parent. HelmetFix is the importer's turn of the glTF frame, as the bike's parts get.
	bool Build(AActor* Owner, USceneComponent* Parent, const FString& BodyPath, const FString& HelmetPath,
		const TMap<FString, UMaterialInterface*>& Mats, const FTransform& HelmetFix);
	void Pose(const FRiderPoseInput& In);
	bool IsValid() const { return Body != nullptr; }
	FVector PelvisRest() const { return Pelvis != INDEX_NONE ? Bones[Pelvis].Rest : FVector(-34.0, 0.0, 61.5); }
	// Read only, for fitting the pose to another bike: a bone's joint at rest (pawn local cm; false
	// if the mesh has no such bone), and the grip centre each hand holds at rest (0 left, 1 right).
	bool BoneRest(const TCHAR* Name, FVector& Out) const;
	FVector GripRest(int32 Side) const { return Hands[FMath::Clamp(Side, 0, 1)].Grip; }
	// The back's lean forward of upright (degrees, pelvis to neck seen from the side): as the last
	// Pose put it, and as the mesh's bones have it now (what's drawn), for the test log.
	double PosedSpineDeg = 0.0;
	double DrawnSpineDeg() const;

	UPoseableMeshComponent* Body = nullptr;
	UStaticMeshComponent* Helmet = nullptr;
	FVector HelmetCentre = FVector::ZeroVector;   // now, pawn local cm (the first-person eye is by it)

private:
	struct FBone
	{
		FName Name;
		int32 Parent = INDEX_NONE;   // into Bones
		FVector Rest;                // pawn local cm
		FQuat RestCS;                // rotation in the mesh's component space
		FVector RestScale = FVector::OneVector;
	};
	TArray<FBone> Bones;
	FMatrix CompToLocal = FMatrix::Identity;   // the body's component space to pawn local
	FMatrix LocalToComp = FMatrix::Identity;
	FMatrix Rot3 = FMatrix::Identity, Rot3Inv = FMatrix::Identity;   // CompToLocal without the move
	FTransform HelmetRest;
	FVector HelmetCentreRest = FVector::ZeroVector;

	// Bones by job (indices into Bones; INDEX_NONE if the mesh lacks one).
	int32 Pelvis = INDEX_NONE, Spine[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE}, Neck = INDEX_NONE, Head = INDEX_NONE;
	int32 UpperArm[2], LowerArm[2], Hand[2], Thigh[2], Calf[2], Foot[2];
	// The hands (rider.json "hands"): the grip's centre, the axis fingers and the throttle turn
	// about, and for the index and middle fingers their bones and their turns (knuckle, middle joint,
	// degrees) hooked over the lever at rest and pulled.
	struct FFinger { int32 Bone01 = INDEX_NONE, Bone02 = INDEX_NONE; FVector2D Reach = FVector2D::ZeroVector, Pull = FVector2D::ZeroVector; };
	struct FHand { FVector Grip = FVector::ZeroVector, FingerAxis = FVector::RightVector, TwistAxis = FVector::RightVector; double TwistDeg = 0.0; TArray<FFinger> Fingers; };
	FHand Hands[2];
	FVector ElbowPole[2], KneePole[2];

	int32 Find(const TCHAR* Name) const;
	FQuat ToComp(const FQuat& LocalDelta) const;
};
