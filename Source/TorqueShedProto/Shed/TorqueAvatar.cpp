#include "Shed/TorqueAvatar.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// (A named namespace, not an anonymous one: unity builds put this file next to RiderRig.cpp.)
namespace TorqueAvatarMath
{
// The middle joint of a two-bone limb from Root to Tip (lengths A then B), bending towards Pole.
FVector MidJoint(const FVector& Root, const FVector& Tip, double A, double B, const FVector& Pole)
{
	FVector D = Tip - Root;
	const double Len = FMath::Clamp(D.Size(), FMath::Abs(A - B) + 0.1, (A + B) * 0.999);
	D = D.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const double Along = (A * A - B * B + Len * Len) / (2.0 * Len);
	const double Out = FMath::Sqrt(FMath::Max(A * A - Along * Along, 0.0));
	FVector Side = Pole - D * FVector::DotProduct(Pole, D);
	Side = Side.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	return Root + D * Along + Side * Out;
}

// The turn taking direction A0 to A1 and the plane (A0, B0) to (A1, B1): a bone's aim and its hinge.
FQuat Align(const FVector& A0, const FVector& B0, const FVector& A1, const FVector& B1)
{
	if (A0.IsNearlyZero() || A1.IsNearlyZero()) return FQuat::Identity;
	if (FVector::CrossProduct(A0, B0).IsNearlyZero() || FVector::CrossProduct(A1, B1).IsNearlyZero())
		return FQuat::FindBetweenVectors(A0, A1);
	const FQuat Q0 = FRotationMatrix::MakeFromXY(A0, B0).ToQuat();
	const FQuat Q1 = FRotationMatrix::MakeFromXY(A1, B1).ToQuat();
	return Q1 * Q0.Inverse();
}

// A turn taking From towards To by Frac of the way.
FQuat Toward(const FVector& From, const FVector& To, double Frac)
{
	return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenVectors(From, To), Frac);
}

struct FEntry { FString Name, Blueprint, BodyMesh; };

const TArray<FEntry>& Entries()
{
	static TArray<FEntry> List;
	static bool bRead = false;
	if (bRead) return List;
	bRead = true;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Avatars/avatars.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		return List;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("avatars"), Arr)) return List;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V->TryGetObject(O)) continue;
		FEntry E;
		(*O)->TryGetStringField(TEXT("name"), E.Name);
		(*O)->TryGetStringField(TEXT("blueprint"), E.Blueprint);
		(*O)->TryGetStringField(TEXT("body"), E.BodyMesh);
		if (!E.Blueprint.IsEmpty() || !E.BodyMesh.IsEmpty()) List.Add(E);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: %d MetaHuman avatars listed"), List.Num());
	return List;
}
} // namespace TorqueAvatarMath

// Nothing to undo here: the owner calls Destroy in EndPlay, and a destructor run by the garbage
// collector mustn't destroy actors or components.
FTorqueAvatar::~FTorqueAvatar() = default;

int32 FTorqueAvatar::Count() { return TorqueAvatarMath::Entries().Num(); }

int32 FTorqueAvatar::Find(const TCHAR* BoneName) const
{
	for (int32 I = 0; I < Bones.Num(); ++I)
		if (Bones[I].Name == FName(BoneName)) return I;
	return INDEX_NONE;
}

void FTorqueAvatar::Destroy()
{
	if (AActor* A = Spawned.Get()) A->Destroy();
	Spawned.Reset();
	if (Driver) Driver->DestroyComponent();
	Driver = nullptr;
	Bones.Reset();
}

bool FTorqueAvatar::Build(AActor* Owner, USceneComponent* Parent, int32 Index)
{
	using namespace TorqueAvatarMath;
	Destroy();
	const TArray<FEntry>& List = Entries();
	if (!Owner || !Parent || List.Num() == 0) return false;
	const FEntry& E = List[((Index % List.Num()) + List.Num()) % List.Num()];
	Name = E.Name;

	// The MetaHuman's Blueprint, carried by the walker with its feet at Parent's origin.
	USkeletalMesh* Mesh = nullptr;
	USkeletalMeshComponent* BodyComp = nullptr;
	if (!E.Blueprint.IsEmpty())
	{
		UClass* Cls = LoadClass<AActor>(nullptr, *E.Blueprint);
		UWorld* World = Owner->GetWorld();
		if (Cls && World)
		{
			FActorSpawnParameters SP;
			SP.Owner = Owner;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* A = World->SpawnActor<AActor>(Cls, Parent->GetComponentTransform(), SP);
			if (A)
			{
				A->SetReplicates(false);
				A->SetActorEnableCollision(false);
				A->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				Spawned = A;
				// Its body: the component called Body, or the skinned mesh with the most bones.
				TArray<USkeletalMeshComponent*> Skins;
				A->GetComponents(Skins);
				int32 Most = -1;
				for (USkeletalMeshComponent* S : Skins)
				{
					USkeletalMesh* M = S->GetSkeletalMeshAsset();
					if (!M) continue;
					if (S->GetName().Contains(TEXT("Body"))) { BodyComp = S; break; }
					if (M->GetRefSkeleton().GetNum() > Most) { Most = M->GetRefSkeleton().GetNum(); BodyComp = S; }
				}
				if (BodyComp) Mesh = BodyComp->GetSkeletalMeshAsset();
			}
		}
		if (!Mesh) UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: Blueprint %s didn't spawn a body"), *Name, *E.Blueprint);
	}
	if (!Mesh && !E.BodyMesh.IsEmpty()) Mesh = LoadObject<USkeletalMesh>(nullptr, *E.BodyMesh);
	if (!Mesh) { Destroy(); return false; }

	// The rest pose in component space.
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TArray<FTransform> RefCS;
	RefCS.SetNum(Ref.GetNum());
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		RefCS[I] = P >= 0 ? Ref.GetRefBonePose()[I] * RefCS[P] : Ref.GetRefBonePose()[I];
	}
	// The bones it moves (the UE5 mannequin's names, which MetaHuman bodies use), parents first.
	const TCHAR* Want[] = {TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_03"), TEXT("spine_05"), TEXT("neck_01"), TEXT("head"),
		TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r")};
	Bones.Reset();
	for (const TCHAR* W : Want)
	{
		const int32 I = Ref.FindBoneIndex(FName(W));
		if (I == INDEX_NONE) continue;
		FBone B;
		B.Name = FName(W);
		B.Index = I;
		B.Rest = RefCS[I].GetLocation();
		B.RestQ = RefCS[I].GetRotation();
		Bones.Add(B);
	}
	// spine_03 and spine_05 stand in for spine_02/03 on a mannequin with three spine bones.
	if (Find(TEXT("spine_05")) == INDEX_NONE)
		for (const TCHAR* W : {TEXT("spine_02"), TEXT("spine_03")})
		{
			const int32 I = Ref.FindBoneIndex(FName(W));
			if (I == INDEX_NONE || Find(W) != INDEX_NONE) continue;
			FBone B; B.Name = FName(W); B.Index = I; B.Rest = RefCS[I].GetLocation(); B.RestQ = RefCS[I].GetRotation();
			Bones.Add(B);
		}
	// Parents among the driven bones: the nearest driven ancestor.
	Bones.Sort([](const FBone& A, const FBone& B) { return A.Index < B.Index; });   // the ref skeleton lists parents first
	for (FBone& B : Bones)
		for (int32 P = Ref.GetParentIndex(B.Index); P != INDEX_NONE && B.Parent == INDEX_NONE; P = Ref.GetParentIndex(P))
			for (int32 J = 0; J < Bones.Num(); ++J)
				if (Bones[J].Index == P) { B.Parent = J; break; }

	Pelvis = Find(TEXT("pelvis"));
	Spine[0] = Find(TEXT("spine_01"));
	Spine[1] = Find(TEXT("spine_03")) != INDEX_NONE && Find(TEXT("spine_05")) != INDEX_NONE ? Find(TEXT("spine_03")) : Find(TEXT("spine_02"));
	Spine[2] = Find(TEXT("spine_05")) != INDEX_NONE ? Find(TEXT("spine_05")) : Find(TEXT("spine_03"));
	Neck = Find(TEXT("neck_01"));
	Head = Find(TEXT("head"));
	const TCHAR* Side[2] = {TEXT("l"), TEXT("r")};
	for (int32 S = 0; S < 2; ++S)
	{
		UpperArm[S] = Find(*FString::Printf(TEXT("upperarm_%s"), Side[S]));
		LowerArm[S] = Find(*FString::Printf(TEXT("lowerarm_%s"), Side[S]));
		HandBone[S] = Find(*FString::Printf(TEXT("hand_%s"), Side[S]));
		Thigh[S] = Find(*FString::Printf(TEXT("thigh_%s"), Side[S]));
		Calf[S] = Find(*FString::Printf(TEXT("calf_%s"), Side[S]));
		FootBone[S] = Find(*FString::Printf(TEXT("foot_%s"), Side[S]));
	}
	for (int32 S = 0; S < 2; ++S)
		if (UpperArm[S] == INDEX_NONE || LowerArm[S] == INDEX_NONE || HandBone[S] == INDEX_NONE || Thigh[S] == INDEX_NONE
			|| Calf[S] == INDEX_NONE || FootBone[S] == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: the body hasn't the mannequin's limb bones"), *Name);
			Destroy();
			return false;
		}
	if (Pelvis == INDEX_NONE || Head == INDEX_NONE) { Destroy(); return false; }

	// Its own axes: up the back, forward the way the toes point, right from the left hip to the right.
	Up = (Bones[Head].Rest - Bones[Pelvis].Rest).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	FVector Toes = FVector::ZeroVector;
	for (const TCHAR* W : {TEXT("ball_l"), TEXT("ball_r")})
	{
		const int32 I = Ref.FindBoneIndex(FName(W));
		if (I != INDEX_NONE) Toes += RefCS[I].GetLocation() - (Bones[FootBone[0]].Rest + Bones[FootBone[1]].Rest) * 0.5;
	}
	Fwd = (Toes - Up * FVector::DotProduct(Toes, Up)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	Right = Bones[Thigh[1]].Rest - Bones[Thigh[0]].Rest;
	Right = (Right - Up * FVector::DotProduct(Right, Up) - Fwd * FVector::DotProduct(Right, Fwd)).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	ArmLen = (Bones[LowerArm[0]].Rest - Bones[UpperArm[0]].Rest).Size() + (Bones[HandBone[0]].Rest - Bones[LowerArm[0]].Rest).Size();
	LegLen = (Bones[Calf[0]].Rest - Bones[Thigh[0]].Rest).Size() + (Bones[FootBone[0]].Rest - Bones[Calf[0]].Rest).Size();

	// The poseable copy: in the Blueprint body's own space (hidden, the parts follow it), or shown
	// on its own, turned to face Parent's +x.
	Driver = NewObject<UPoseableMeshComponent>(Owner, *FString::Printf(TEXT("Avatar_%s"), *Name.Replace(TEXT(" "), TEXT("_"))));
	Driver->SetupAttachment(BodyComp ? (USceneComponent*)BodyComp : Parent);
	Driver->SetSkinnedAssetAndUpdate(Mesh);
	Driver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Driver->SetGenerateOverlapEvents(false);
	Driver->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (BodyComp)
	{
		Driver->SetHiddenInGame(true);
		Driver->CastShadow = false;
	}
	else
	{
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
		Driver->SetRelativeRotation(FRotator(0.0, -Yaw, 0.0));
	}
	Driver->RegisterComponent();
	if (AActor* A = Spawned.Get())
	{
		TArray<USkinnedMeshComponent*> Parts;
		A->GetComponents(Parts);
		for (USkinnedMeshComponent* C : Parts)
			if (C != Driver) C->SetLeaderPoseComponent(Driver, true);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s (%s, %d bones driven, legs %.0f cm, arms %.0f cm)"), *Name,
		*Mesh->GetName(), Bones.Num(), LegLen, ArmLen);
	return true;
}

void FTorqueAvatar::Pose(const FTorqueAvatarInput& In, double Dt)
{
	using namespace TorqueAvatarMath;
	if (!Driver) return;
	const int32 N = Bones.Num();
	TArray<FQuat> D;   // each bone's turn from rest, component space
	TArray<FVector> P;  // each bone's joint now
	TArray<bool> Set;
	D.Init(FQuat::Identity, N);
	P.SetNumZeroed(N);
	Set.Init(false, N);
	auto Fk = [&](int32 I) {
		const int32 Par = Bones[I].Parent;
		if (Par == INDEX_NONE) { if (!Set[I]) P[I] = Bones[I].Rest; return; }
		P[I] = P[Par] + D[Par].RotateVector(Bones[I].Rest - Bones[Par].Rest);
		if (!Set[I]) D[I] = D[Par];
	};

	// How it moves: forward speed in the body's frame, and the stride's phase.
	const FVector Flat(In.Velocity.X, In.Velocity.Y, 0.0);
	const FVector Ahead = In.Facing.Vector();
	const double Speed = Flat.Size();
	const double Fore = FVector::DotProduct(Flat, FVector(Ahead.X, Ahead.Y, 0.0).GetSafeNormal());
	const double WalkWant = In.bInAir ? 0.0 : FMath::Clamp(Speed / 120.0, 0.0, 1.0);
	Walk += (WalkWant - Walk) * FMath::Min(1.0, Dt * 8.0);
	const double Stride = FMath::Clamp(70.0 + Speed * 0.17, 70.0, 165.0);   // cm per step pair, longer when hurrying
	Phase = FMath::Fmod(Phase + 2.0 * PI * Speed / FMath::Max(Stride, 1.0) * Dt * (Fore < -10.0 ? -1.0 : 1.0), 2.0 * PI);
	const double HandsWant = In.bCarrying ? 1.0 : In.bWorking ? 0.7 : 0.0;
	Hands += (HandsWant - Hands) * FMath::Min(1.0, Dt * 6.0);
	Gripping += ((In.bToolInHand ? 1.0 : 0.0) - Gripping) * FMath::Min(1.0, Dt * 8.0);
	const FVector GripLocal = Driver->GetComponentTransform().InverseTransformPosition(In.ToolGrip);
	const double LeanWant = FMath::Clamp(Speed / 550.0, 0.0, 1.0) * 8.0 + In.Crouch * 18.0;   // degrees forward
	Lean += (LeanWant - Lean) * FMath::Min(1.0, Dt * 4.0);
	const double Crouch = FMath::Clamp(In.Crouch, 0.0, 1.0);

	// Pelvis: lower when crouched, bobbing twice a stride.
	const double Bob = Walk * (1.5 + 2.0 * FMath::Clamp(Speed / 550.0, 0.0, 1.0)) * FMath::Abs(FMath::Sin(Phase));
	P[Pelvis] = Bones[Pelvis].Rest - Up * (Crouch * LegLen * 0.42 + Bob);
	D[Pelvis] = FQuat(Up, FMath::DegreesToRadians(4.0 * Walk * FMath::Sin(Phase)));   // hips turn with the step
	Set[Pelvis] = true;
	// The back leans forward with speed and crouching; the head looks where the player does.
	const double LeanRad = FMath::DegreesToRadians(Lean);
	const FQuat Bend = FQuat::FindBetweenVectors(Up, Up * FMath::Cos(LeanRad) + Fwd * FMath::Sin(LeanRad));
	const double PitchRad = FMath::DegreesToRadians(FMath::Clamp(In.LookPitch, -70.0, 70.0)) - LeanRad;
	const FQuat Nod = FQuat::FindBetweenVectors(Fwd, Fwd * FMath::Cos(PitchRad) + Up * FMath::Sin(PitchRad));
	const FQuat Counter = FQuat(Up, FMath::DegreesToRadians(-4.0 * Walk * FMath::Sin(Phase)));   // shoulders against the hips
	for (int32 I = 0; I < N; ++I)
	{
		if (I == Spine[0]) { D[I] = FQuat::Slerp(FQuat::Identity, Bend, 0.4); Set[I] = true; }
		else if (I == Spine[1]) { D[I] = FQuat::Slerp(FQuat::Identity, Bend, 0.75) * FQuat::Slerp(FQuat::Identity, Counter, 0.5); Set[I] = true; }
		else if (I == Spine[2]) { D[I] = Bend * Counter; Set[I] = true; }
		else if (I == Neck) { D[I] = FQuat::Slerp(FQuat::Identity, Nod, 0.4) * Bend * Counter; Set[I] = true; }
		else if (I == Head) { D[I] = Nod * Bend * Counter; Set[I] = true; }
		Fk(I);
		for (int32 S = 0; S < 2; ++S)
		{
			const double Sign = S == 0 ? -1.0 : 1.0;   // left, right
			if (I == Thigh[S])
			{
				// Feet: forward and back through the stride, lifted on the way forward; planted
				// under the hips when standing (and crouched, so the knees bend).
				const double Th = Phase + (S == 0 ? 0.0 : PI);
				const FVector A0 = Bones[FootBone[S]].Rest, K0 = Bones[Calf[S]].Rest, H0 = Bones[I].Rest;
				const FVector A = A0 + Fwd * (Walk * Stride * 0.25 * FMath::Sin(Th))
					+ Up * (Walk * FMath::Max(0.0, FMath::Cos(Th)) * (6.0 + 6.0 * FMath::Clamp(Speed / 550.0, 0.0, 1.0)))
					+ Right * (Sign * Crouch * 6.0);
				const double Lt = (K0 - H0).Size(), Ls = (A0 - K0).Size();
				const FVector KneeTo = Fwd + Right * (Sign * 0.15 * Crouch);
				const FVector K = MidJoint(P[I], A, Lt, Ls, KneeTo);
				// The knee's hinge from the line hip-to-ankle and the way the knee points (steady
				// when the leg is nearly straight, where the bones' own cross product isn't).
				const FVector Hinge0 = FVector::CrossProduct(A0 - H0, Fwd);
				const FVector Hinge = FVector::CrossProduct(A - P[I], KneeTo);
				D[I] = Align(K0 - H0, Hinge0, K - P[I], Hinge); Set[I] = true;
				D[Calf[S]] = Align(A0 - K0, Hinge0, A - K, Hinge); Set[Calf[S]] = true;
				// The foot stays level, toe up a touch as the heel strikes.
				D[FootBone[S]] = FQuat(Right, FMath::DegreesToRadians(-10.0 * Walk * FMath::Max(0.0, FMath::Sin(Th)))) ;
				Set[FootBone[S]] = true;
			}
			else if (I == UpperArm[S])
			{
				// Arms: down from the A-pose to hang by the sides, swinging against the legs; out in
				// front, elbows bent, when carrying or working.
				const FVector S0 = Bones[I].Rest, E0 = Bones[LowerArm[S]].Rest, W0 = Bones[HandBone[S]].Rest;
				const double Swing = Walk * (12.0 + 10.0 * FMath::Clamp(Speed / 550.0, 0.0, 1.0)) * FMath::Sin(Phase + (S == 0 ? PI : 0.0));
				const FVector Hang = P[I] - Up * (ArmLen * 0.93) + Right * (Sign * 6.0) + Fwd * (3.0 + Swing);
				const FVector Hold = P[I] - Up * (ArmLen * 0.45) + Fwd * (ArmLen * 0.55) - Right * (Sign * 8.0);
				FVector W = FMath::Lerp(Hang, Hold, Hands);
				// The right hand holds the tool where the tool is (the wrist a hand's width short of the grip).
				const double OnTool = S == 1 ? Gripping : 0.0;
				if (OnTool > 0.0)
					W = FMath::Lerp(W, GripLocal - (GripLocal - P[I]).GetSafeNormal() * 7.0, OnTool);
				const double Up2 = FMath::Max(Hands, OnTool);
				const double Lu = (E0 - S0).Size(), Lf = (W0 - E0).Size();
				const FVector Pole = (-Fwd * (1.0 - Up2) + Right * (Sign * 0.6) - Up * (0.4 + Up2)).GetSafeNormal();
				const FVector E = MidJoint(P[I], W, Lu, Lf, Pole);
				const FVector Hinge0 = FVector::CrossProduct(W0 - S0, -Fwd);
				const FVector Hinge = FVector::CrossProduct(W - P[I], Pole);
				D[I] = Align(E0 - S0, Hinge0, E - P[I], Hinge); Set[I] = true;
				D[LowerArm[S]] = Align(W0 - E0, Hinge0, W - E, Hinge); Set[LowerArm[S]] = true;
				D[HandBone[S]] = D[LowerArm[S]]; Set[HandBone[S]] = true;
			}
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		const FTransform T(D[I] * Bones[I].RestQ, P[I]);
		Driver->SetBoneTransformByName(Bones[I].Name, T, EBoneSpaces::ComponentSpace);
	}
}
