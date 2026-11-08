// A co-op player's body in the shed as a MetaHuman (made on the PC by Tools/ue_metahumans.py and
// listed in Data/Avatars/avatars.json), moved in code: no animation assets and no Anim Blueprint.
//
// The assembled MetaHuman Blueprint (body, face, hair, clothes) is spawned and carried by the
// walker. A hidden poseable copy of its body is posed every frame (legs stepping with the walk,
// arms swinging, crouching, the head looking where the player looks, hands out in front when
// carrying or working), and every skinned part of the Blueprint follows it as its leader pose. With
// only a body mesh listed, the poseable copy is shown itself.
//
// Plain C++, no UObject: the walker owns it, and its components belong to the walker's actor (or
// to the spawned Blueprint), which keep them alive.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class USceneComponent;
class UPoseableMeshComponent;
class USkeletalMesh;

struct FTorqueAvatarInput
{
	FVector Velocity = FVector::ZeroVector;   // world cm/s
	FRotator Facing = FRotator::ZeroRotator;  // the body's yaw (world)
	double LookPitch = 0.0;                   // degrees, + up
	double Crouch = 0.0;                      // 0 standing .. 1 crouched
	bool bCarrying = false;                   // a part in both hands
	bool bWorking = false;                    // a tool at work in front
	bool bInAir = false;
};

class FTorqueAvatar
{
public:
	~FTorqueAvatar();
	// Avatars listed in Data/Avatars/avatars.json (0 when none: keep the shape body).
	static int32 Count();
	// Make avatar Index (wrapped to the list) for Owner, its feet at Parent's origin, facing Parent's +x.
	bool Build(AActor* Owner, USceneComponent* Parent, int32 Index);
	void Pose(const FTorqueAvatarInput& In, double Dt);
	void Destroy();
	bool IsValid() const { return Driver != nullptr; }
	FString Name;

private:
	UPoseableMeshComponent* Driver = nullptr;
	TWeakObjectPtr<AActor> Spawned;   // the MetaHuman Blueprint, when there is one
	struct FBone { FName Name; int32 Index = INDEX_NONE; int32 Parent = INDEX_NONE; FVector Rest; FQuat RestQ; };
	TArray<FBone> Bones;              // the driven bones, parents first (Parent indexes into Bones)
	int32 Find(const TCHAR* BoneName) const;
	int32 Pelvis = INDEX_NONE, Spine[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE}, Neck = INDEX_NONE, Head = INDEX_NONE;
	int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE}, LowerArm[2] = {INDEX_NONE, INDEX_NONE}, HandBone[2] = {INDEX_NONE, INDEX_NONE};
	int32 Thigh[2] = {INDEX_NONE, INDEX_NONE}, Calf[2] = {INDEX_NONE, INDEX_NONE}, FootBone[2] = {INDEX_NONE, INDEX_NONE};
	// The body's own axes in its component space (found from the rest pose), and sizes.
	FVector Up = FVector::UpVector, Fwd = FVector::ForwardVector, Right = FVector::RightVector;
	double ArmLen = 55.0, LegLen = 85.0;
	double Phase = 0.0, Walk = 0.0, Hands = 0.0, Lean = 0.0;   // smoothed
};
