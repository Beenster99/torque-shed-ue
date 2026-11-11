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

// [hands] A hand round a tool's handle (world): the bar through the fist from the little finger to
// the index (Axis), the way the knuckles point (Aim), the bar's radius (cm); fingers on a trigger
// (TriggerFingers from the index, pulled by TriggerPull 0..1); bIndexTop: the index up on a spray head.
struct FTorqueHandGrip
{
	bool bOn = false;
	FVector At = FVector::ZeroVector, Axis = FVector::UpVector, Aim = FVector::ForwardVector;
	double Radius = 1.5;
	int32 TriggerFingers = 0;
	double TriggerPull = 0.0;
	bool bIndexTop = false;
};

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
	// [hands] The hands round the tool's handles (0 left, 1 right), fingers wrapped; for the right
	// hand it takes over from ToolGrip.
	FTorqueHandGrip Grip[2];
};

// What a MetaHuman riding a bike reaches for (the parent's space, cm): the joints the bike's rider
// rig was posed to for that bike's seat, bars and pegs (FRiderRig::Last), so the avatar sits, holds
// and stands where any rider of that bike would, with its own arms and legs.
struct FTorqueRideTargets
{
	FVector Hip = FVector::ZeroVector, Neck = FVector::ZeroVector;
	// The seat's top under the rider's hips (the hips are then sat on it by this body's own hip joints),
	// and the tank's back edge along x (the crotch kept behind it); both in the parent's space.
	FVector Seat = FVector::ZeroVector;
	bool bSeat = false;
	double TankRearX = 1.0e9;
	FVector Wrist[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector Grip[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector Ankle[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FQuat Look = FQuat::Identity;            // the head's turn, in the parent's space
	double LeverFingers[2] = {0.0, 0.0}, LeverPull[2] = {0.0, 0.0};
	double Limp = 0.0;                       // off the bike: hands loose
	double HandOff[2] = {0.0, 0.0};          // [gestures] one hand off its grip (the wave): to Wrist, 0..1
	// [rider pose] On a catalogue bike: its knees (BikeRig's, where the tank's knee pads are), pegs and
	// tank's half-width along x ((x, half-width), parent's space), and how far the feet are down off
	// the pegs (0 on them, 1 on the road, as the rider rig's ankles). Without them (bOnLayout false)
	// the knees go forward of the hips, outside the hips' width.
	bool bOnLayout = false;
	FVector Knee[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector Peg[2] = {FVector::ZeroVector, FVector::ZeroVector};
	TArray<FVector2D> TankHalfW;
	double FeetDown = 0.0;
	bool bLaidBack = false;                  // feet forward (a cruiser's or chopper's): arms may reach further
};

class FTorqueAvatar
{
public:
	~FTorqueAvatar();
	// Avatars listed in Data/Avatars/avatars.json (0 when none: keep the shape body).
	static int32 Count();
	static FString NameOf(int32 Index);
	// A test (-TorqueHairAlone): avatar Index's grooms on their own, on no head, at At on Parent (the
	// groom's own space, so the hair floats where that head would be), to see if the asset draws.
	static void SpawnLooseHair(AActor* Owner, USceneComponent* Parent, int32 Index, const FVector& At);   // its name in avatars.json (wrapped to the list), "" when none
	// Make avatar Index (wrapped to the list) for Owner, its feet at Parent's origin, facing Parent's +x.
	bool Build(AActor* Owner, USceneComponent* Parent, int32 Index);
	void Pose(const FTorqueAvatarInput& In, double Dt);
	// The player's profile on it: height, and the face sliders on its face's joints (TorqueProfile.h).
	void SetLook(const FTorqueProfile& Look);
	FVector HeadWorld() const;
	// A bone of the posed body in the world (ZeroVector when there's no avatar or no such bone).
	FVector BoneWorld(FName BoneName) const;
	// On a bike: seated, hands on the grips, feet on the pegs (FTorqueRideTargets).
	void PoseRiding(const FTorqueRideTargets& T);
	// [rider pose] A test's switch back to the riding pose as it was before the fit per bike
	// (riderpose:old), for before and after pictures from the same run.
	static bool bOldRidePose;
	// What the last PoseRiding came to, for the test log: the back's lean forward of upright, each
	// knee's and elbow's angle, how far each hand and ankle fell short, and each knee's distance out.
	struct FRideReport { double BackDeg = 0.0, KneeDeg[2] = {0.0, 0.0}, ElbowDeg[2] = {0.0, 0.0}, HandShort[2] = {0.0, 0.0}, AnkleShort[2] = {0.0, 0.0}, KneeOut[2] = {0.0, 0.0}; };
	FRideReport RideReport;
	// The head joint's place and its turn from rest in the parent's space, as last posed (riding).
	bool HeadInParent(FVector& At, FQuat& Turn) const;
	// Showing the body (and clothes), the head (face) and the hair: a helmet over it, or the rider's
	// own first-person view, hides some.
	void SetShown(bool bBody, bool bHead, bool bHair);
	double StandingHeight() const;   // cm, about: the head bone's height plus the crown, at its scale
	// [hands] Where a gripped bar ended up after the last Pose (world): the target, or as near as
	// the arm reached; false when that hand wasn't gripping.
	bool GripReached(int32 Hand, FVector& At) const;
	// [hands] Between the eyes, as last posed (world): the first-person camera's spot.
	bool EyeWorld(FVector& At) const;
	// [hands] Your own body seen from inside it: the face and hair hidden from you (the owner), still
	// seen by the others and still casting their shadows.
	void SetOwnerView(bool bFirstPerson);
	void Destroy();
	bool IsValid() const { return Driver != nullptr; }
	FString Name;

private:
	UPoseableMeshComponent* Driver = nullptr;
	TWeakObjectPtr<AActor> Spawned;   // the MetaHuman Blueprint, when one is listed (older avatars.json)
	TArray<TWeakObjectPtr<USceneComponent>> Parts;   // its face, clothes and grooms, when listed as parts
	TArray<TWeakObjectPtr<USceneComponent>> Hair;    // the grooms among them
	FVector RideHead = FVector::ZeroVector;
	FQuat RideHeadTurn = FQuat::Identity;
	bool bRode = false;
	// The face, posed too: the body's bones it shares copied over each frame, its own (FACIAL_*)
	// at rest moved by the profile's face sliders (SetLook).
	UPoseableMeshComponent* FaceMesh = nullptr;
	TArray<int32> FaceFromDriver;     // per face bone: the body's bone of that name, or INDEX_NONE
	TArray<FTransform> FaceLocal;     // per face bone: its local transform when it isn't the body's
	int32 FaceHead = INDEX_NONE;
	double FaceScale = 1.0;
	void SetupFace();
	void PoseFace();
	void TouchHair();   // the grooms told their place each frame (they draw where they were made otherwise)
	double HairLogAt = 0.0;
	struct FBone { FName Name; int32 Index = INDEX_NONE; int32 Parent = INDEX_NONE; FVector Rest; FQuat RestQ; };
	TArray<FBone> Bones;              // the driven bones, parents first (Parent indexes into Bones)
	int32 Find(const TCHAR* BoneName) const;
	int32 Pelvis = INDEX_NONE, Spine[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE}, Neck = INDEX_NONE, Head = INDEX_NONE;
	int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE}, LowerArm[2] = {INDEX_NONE, INDEX_NONE}, HandBone[2] = {INDEX_NONE, INDEX_NONE};
	int32 Thigh[2] = {INDEX_NONE, INDEX_NONE}, Calf[2] = {INDEX_NONE, INDEX_NONE}, FootBone[2] = {INDEX_NONE, INDEX_NONE};
	FVector BallRest[2] = {FVector::ZeroVector, FVector::ZeroVector};   // the balls of the feet at rest (component space)
	bool bBall[2] = {false, false};
	int32 Finger[2][5][3];   // thumb, index, middle, ring, pinky; 01..03 (INDEX_NONE without)
	// The body's own axes in its component space (found from the rest pose), and sizes.
	FVector Up = FVector::UpVector, Fwd = FVector::ForwardVector, Right = FVector::RightVector;
	double ArmLen = 55.0, LegLen = 85.0;
	double RestHeight = 175.0;        // cm, as built
	double BaseScale = 1.0;           // to avatars.json's height_cm
	double PosedFor = 0.0;
	bool bPoseLogged = false;
	double Phase = 0.0, Walk = 0.0, Hands = 0.0, Lean = 0.0, Gripping = 0.0;   // smoothed
	// [hands] Holding a bar: each hand's weight on it (smoothed), where the bar came to, the eyes.
	double GripW[2] = {0.0, 0.0};
	FVector GripAt[2] = {FVector::ZeroVector, FVector::ZeroVector};
	bool bGripReached[2] = {false, false};
	FVector EyeRest = FVector::ZeroVector;   // the eyes from the head joint at rest (component space)
	FVector HeadAtC = FVector::ZeroVector;   // the head joint and its turn as last posed (component space)
	FQuat HeadTurnC = FQuat::Identity;
	bool bPosed = false;
	// A hand's frame on a bar (component space): its turn, its wrist, the palm's way (the riding grips
	// and the tools share it), and its fingers and thumb wrapped round the bar, joint by joint.
	void HandRest(int32 S, FVector& Aim0, FVector& Across0, FVector& Palm0) const;
	void HandOnBar(int32 S, const FVector& Bar, const FVector& Across1, const FVector& Aim1, double AimOff, double PalmOff,
		FQuat& HandQ, FVector& WristAt, FVector& Palm1) const;
	void WrapFingers(int32 S, TArray<FQuat>& D, TArray<bool>& Set, const FQuat& Hq, const FVector& WAct, const FVector& Bar,
		const FVector& Axis, const FVector& Aim1, const FVector& Palm1, double GripR, const double FingerK[5], double ThumbOff) const;
};
