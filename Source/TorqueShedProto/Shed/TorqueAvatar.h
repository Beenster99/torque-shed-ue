// A co-op player's body in the shed as a MetaHuman (made on the PC by Tools/ue_metahumans.py and
// listed in Data/Avatars/avatars.json), moved in code: no animation assets and no Anim Blueprint.
//
// The assembled MetaHuman's meshes (body, face, clothes) and grooms are listed as parts and made on
// the walker; its Blueprint isn't loaded, because its Anim Blueprints need the editor-only MetaHuman
// plugin. A poseable copy of its body is posed every frame (legs stepping with the walk,
// arms swinging, crouching, the head looking where the player looks, hands out in front when
// carrying or working), shown as the body, and every other part follows it as its leader pose.
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
struct FTorqueProfile;

struct FTorqueAvatarInput
{
	FVector Velocity = FVector::ZeroVector;   // world cm/s
	FRotator Facing = FRotator::ZeroRotator;  // the body's yaw (world)
	double LookPitch = 0.0;                   // degrees, + up
	double Crouch = 0.0;                      // 0 standing .. 1 crouched
	bool bCarrying = false;                   // a part in both hands
	bool bWorking = false;                    // a tool at work in front
	bool bInAir = false;
	// The tool in their right hand (world): the grip the walker's tool view puts it at, the hand
	// reaching to hold it there.
	bool bToolInHand = false;
	FVector ToolGrip = FVector::ZeroVector;
};

class FTorqueAvatar
{
public:
	~FTorqueAvatar();
	// Avatars listed in Data/Avatars/avatars.json (0 when none: keep the shape body).
	static int32 Count();
	static FString NameOf(int32 Index);   // its name in avatars.json (wrapped to the list), "" when none
	// Make avatar Index (wrapped to the list) for Owner, its feet at Parent's origin, facing Parent's +x.
	bool Build(AActor* Owner, USceneComponent* Parent, int32 Index);
	void Pose(const FTorqueAvatarInput& In, double Dt);
	// The player's profile on it: height, and the face sliders on its face's joints (TorqueProfile.h).
	void SetLook(const FTorqueProfile& Look);
	FVector HeadWorld() const;
	double StandingHeight() const;   // cm, about: the head bone's height plus the crown, at its scale
	void Destroy();
	bool IsValid() const { return Driver != nullptr; }
	FString Name;

private:
	UPoseableMeshComponent* Driver = nullptr;
	TWeakObjectPtr<AActor> Spawned;   // the MetaHuman Blueprint, when one is listed (older avatars.json)
	TArray<TWeakObjectPtr<USceneComponent>> Parts;   // its face, clothes and grooms, when listed as parts
	// The face, posed too: the body's bones it shares copied over each frame, its own (FACIAL_*)
	// at rest moved by the profile's face sliders (SetLook).
	UPoseableMeshComponent* FaceMesh = nullptr;
	TArray<int32> FaceFromDriver;     // per face bone: the body's bone of that name, or INDEX_NONE
	TArray<FTransform> FaceLocal;     // per face bone: its local transform when it isn't the body's
	int32 FaceHead = INDEX_NONE;
	double FaceScale = 1.0;
	void SetupFace();
	void PoseFace();
	struct FBone { FName Name; int32 Index = INDEX_NONE; int32 Parent = INDEX_NONE; FVector Rest; FQuat RestQ; };
	TArray<FBone> Bones;              // the driven bones, parents first (Parent indexes into Bones)
	int32 Find(const TCHAR* BoneName) const;
	int32 Pelvis = INDEX_NONE, Spine[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE}, Neck = INDEX_NONE, Head = INDEX_NONE;
	int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE}, LowerArm[2] = {INDEX_NONE, INDEX_NONE}, HandBone[2] = {INDEX_NONE, INDEX_NONE};
	int32 Thigh[2] = {INDEX_NONE, INDEX_NONE}, Calf[2] = {INDEX_NONE, INDEX_NONE}, FootBone[2] = {INDEX_NONE, INDEX_NONE};
	// The body's own axes in its component space (found from the rest pose), and sizes.
	FVector Up = FVector::UpVector, Fwd = FVector::ForwardVector, Right = FVector::RightVector;
	double ArmLen = 55.0, LegLen = 85.0;
	double RestHeight = 175.0;        // cm, as built
	double BaseScale = 1.0;           // to avatars.json's height_cm
	double PosedFor = 0.0;
	bool bPoseLogged = false;
	double Phase = 0.0, Walk = 0.0, Hands = 0.0, Lean = 0.0, Gripping = 0.0;   // smoothed
};
