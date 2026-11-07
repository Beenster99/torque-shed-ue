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
	FVector ElbowPole[2], KneePole[2];

	int32 Find(const TCHAR* Name) const;
	FQuat ToComp(const FQuat& LocalDelta) const;
};
