#include "RiderRig.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
// The middle joint of a two-bone limb from Root to Tip (lengths A then B), bending towards Pole.
FVector RigMidJoint(const FVector& Root, const FVector& Tip, double A, double B, const FVector& Pole)
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
FQuat AlignFrames(const FVector& A0, const FVector& B0, const FVector& A1, const FVector& B1)
{
	if (A0.IsNearlyZero() || A1.IsNearlyZero()) return FQuat::Identity;
	if (FVector::CrossProduct(A0, B0).IsNearlyZero() || FVector::CrossProduct(A1, B1).IsNearlyZero())
		return FQuat::FindBetweenVectors(A0, A1);
	const FQuat Q0 = FRotationMatrix::MakeFromXY(A0, B0).ToQuat();
	const FQuat Q1 = FRotationMatrix::MakeFromXY(A1, B1).ToQuat();
	return Q1 * Q0.Inverse();
}

// Where the middle joint bends to in the rest pose, square to the line from root to tip.
FVector PoleOf(const FVector& Root, const FVector& Mid, const FVector& Tip)
{
	const FVector D = (Tip - Root).GetSafeNormal();
	const FVector M = Mid - Root;
	return (M - D * FVector::DotProduct(M, D)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
}
} // namespace

int32 FRiderRig::Find(const TCHAR* Name) const
{
	for (int32 I = 0; I < Bones.Num(); ++I)
		if (Bones[I].Name == FName(Name)) return I;
	return INDEX_NONE;
}

FQuat FRiderRig::ToComp(const FQuat& LocalDelta) const
{
	// The same turn seen in the component's space (which the importer may have turned or mirrored).
	FMatrix M = Rot3 * FQuatRotationMatrix(LocalDelta) * Rot3Inv;
	M.RemoveScaling();
	return M.ToQuat();
}

bool FRiderRig::Build(AActor* Owner, USceneComponent* Parent, const FString& BodyPath, const FString& HelmetPath,
	const TMap<FString, UMaterialInterface*>& Mats, const FTransform& HelmetFix)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *BodyPath);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no rider mesh at %s"), *BodyPath);
		return false;
	}
	FString Text;
	TSharedPtr<FJsonObject> Info;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Rider/rider.json"))) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Info) || !Info.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no Data/Rider/rider.json (run Tools/make_rider.py)"));
		return false;
	}
	auto Vec = [](const TArray<TSharedPtr<FJsonValue>>& A) {
		return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
	};

	// The skeleton's rest pose in component space, from the mesh's reference pose.
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TArray<FTransform> RefCS;
	RefCS.SetNum(Ref.GetNum());
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		RefCS[I] = P >= 0 ? Ref.GetRefBonePose()[I] * RefCS[P] : Ref.GetRefBonePose()[I];
	}

	Bones.Reset();
	TArray<FVector> CSPos;
	for (const TSharedPtr<FJsonValue>& V : Info->GetArrayField(TEXT("bones")))
	{
		const TSharedPtr<FJsonObject> B = V->AsObject();
		const FName Name(*B->GetStringField(TEXT("name")));
		const int32 RefIndex = Ref.FindBoneIndex(Name);
		if (RefIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: the rider mesh has no bone %s"), *Name.ToString());
			continue;
		}
		FBone Bone;
		Bone.Name = Name;
		Bone.Rest = Vec(B->GetArrayField(TEXT("rest")));
		Bone.RestCS = RefCS[RefIndex].GetRotation();
		Bone.RestScale = RefCS[RefIndex].GetScale3D();
		FString ParentName;
		if (B->TryGetStringField(TEXT("parent"), ParentName))
			for (int32 J = 0; J < Bones.Num(); ++J)
				if (Bones[J].Name == FName(*ParentName)) Bone.Parent = J;
		Bones.Add(Bone);
		CSPos.Add(RefCS[RefIndex].GetLocation());
	}
	if (Bones.Num() < 10) return false;

	// How the importer turned the skeleton: the axis swap (and scale, and move) that best takes the
	// bones' component-space rest positions onto the ones make_rider.py wrote.
	FVector MeanC = FVector::ZeroVector, MeanL = FVector::ZeroVector;
	for (int32 I = 0; I < Bones.Num(); ++I) { MeanC += CSPos[I]; MeanL += Bones[I].Rest; }
	MeanC /= Bones.Num(); MeanL /= Bones.Num();
	double SpreadC = 0.0, SpreadL = 0.0;
	for (int32 I = 0; I < Bones.Num(); ++I) { SpreadC += (CSPos[I] - MeanC).SizeSquared(); SpreadL += (Bones[I].Rest - MeanL).SizeSquared(); }
	const double Scale = FMath::Sqrt(SpreadL / FMath::Max(SpreadC, 1e-9));
	static const int32 Perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
	double Best = TNumericLimits<double>::Max();
	FMatrix BestM = FMatrix::Identity;
	for (const auto& Pm : Perms)
	{
		for (int32 Signs = 0; Signs < 8; ++Signs)
		{
			// Local axis k = sign_k * component axis Pm[k].
			double Sg[3];
			for (int32 K = 0; K < 3; ++K) Sg[K] = (Signs >> K) & 1 ? -1.0 : 1.0;
			auto Map = [&](const FVector& C) {
				return FVector(Sg[0] * C[Pm[0]], Sg[1] * C[Pm[1]], Sg[2] * C[Pm[2]]) * Scale;
			};
			const FVector T = MeanL - Map(MeanC);
			double Err = 0.0;
			for (int32 I = 0; I < Bones.Num(); ++I) Err += (Map(CSPos[I]) + T - Bones[I].Rest).SizeSquared();
			if (Err < Best)
			{
				Best = Err;
				// Row vectors: local = component * M, so row r says where component axis r goes.
				FMatrix M(EForceInit::ForceInitToZero);
				for (int32 K = 0; K < 3; ++K) M.M[Pm[K]][K] = Sg[K] * Scale;
				M.M[3][0] = T.X; M.M[3][1] = T.Y; M.M[3][2] = T.Z; M.M[3][3] = 1.0;
				BestM = M;
			}
		}
	}
	const double Rms = FMath::Sqrt(Best / Bones.Num());
	UE_LOG(LogTemp, Log, TEXT("Torque: rider skeleton fitted, %d bones, %.2f cm off, scale %.3f"), Bones.Num(), Rms, Scale);
	if (Rms > 3.0) UE_LOG(LogTemp, Warning, TEXT("Torque: the rider's skeleton didn't line up well (%.1f cm)"), Rms);
	CompToLocal = BestM;
	LocalToComp = BestM.Inverse();
	Rot3 = BestM; Rot3.SetOrigin(FVector::ZeroVector);
	Rot3Inv = Rot3.Inverse();

	auto Name2 = [](const TCHAR* Stem, int32 S) { return FString::Printf(TEXT("%s_%s"), Stem, S == 0 ? TEXT("l") : TEXT("r")); };
	Pelvis = Find(TEXT("pelvis"));
	Spine[0] = Find(TEXT("spine_01")); Spine[1] = Find(TEXT("spine_02")); Spine[2] = Find(TEXT("spine_03"));
	Neck = Find(TEXT("neck_01")); Head = Find(TEXT("head"));
	for (int32 S = 0; S < 2; ++S)
	{
		UpperArm[S] = Find(*Name2(TEXT("upperarm"), S)); LowerArm[S] = Find(*Name2(TEXT("lowerarm"), S));
		Hand[S] = Find(*Name2(TEXT("hand"), S)); Thigh[S] = Find(*Name2(TEXT("thigh"), S));
		Calf[S] = Find(*Name2(TEXT("calf"), S)); Foot[S] = Find(*Name2(TEXT("foot"), S));
		for (int32 I : {UpperArm[S], LowerArm[S], Hand[S], Thigh[S], Calf[S], Foot[S]})
			if (I == INDEX_NONE) return false;
		ElbowPole[S] = PoleOf(Bones[UpperArm[S]].Rest, Bones[LowerArm[S]].Rest, Bones[Hand[S]].Rest);
		KneePole[S] = PoleOf(Bones[Thigh[S]].Rest, Bones[Calf[S]].Rest, Bones[Foot[S]].Rest);
	}
	const TArray<TSharedPtr<FJsonValue>>* HandList = nullptr;
	if (Info->TryGetArrayField(TEXT("hands"), HandList))
	{
		for (const TSharedPtr<FJsonValue>& HV : *HandList)
		{
			const TSharedPtr<FJsonObject> H = HV->AsObject();
			FHand& Hd = Hands[H->GetStringField(TEXT("side")) == TEXT("r") ? 1 : 0];
			Hd.Grip = Vec(H->GetArrayField(TEXT("grip")));
			Hd.FingerAxis = Vec(H->GetArrayField(TEXT("finger_axis"))).GetSafeNormal();
			Hd.TwistAxis = Vec(H->GetArrayField(TEXT("twist_axis"))).GetSafeNormal();
			Hd.TwistDeg = H->GetNumberField(TEXT("twist_deg"));
			for (const TSharedPtr<FJsonValue>& FV : H->GetArrayField(TEXT("fingers")))
			{
				const TSharedPtr<FJsonObject> F = FV->AsObject();
				const TArray<TSharedPtr<FJsonValue>>& Bn = F->GetArrayField(TEXT("bones"));
				const TArray<TSharedPtr<FJsonValue>>& R = F->GetArrayField(TEXT("reach"));
				const TArray<TSharedPtr<FJsonValue>>& Pl = F->GetArrayField(TEXT("pull"));
				FFinger Fg;
				if (Bn.Num() >= 2) { Fg.Bone01 = Find(*Bn[0]->AsString()); Fg.Bone02 = Find(*Bn[1]->AsString()); }
				if (R.Num() >= 2) Fg.Reach = FVector2D(R[0]->AsNumber(), R[1]->AsNumber());
				if (Pl.Num() >= 2) Fg.Pull = FVector2D(Pl[0]->AsNumber(), Pl[1]->AsNumber());
				if (Fg.Bone01 != INDEX_NONE && Fg.Bone02 != INDEX_NONE) Hd.Fingers.Add(Fg);
			}
		}
	}
	if (Pelvis == INDEX_NONE || Head == INDEX_NONE || Neck == INDEX_NONE || Spine[2] == INDEX_NONE) return false;
	HelmetCentreRest = HelmetCentre = Vec(Info->GetArrayField(TEXT("helmet_centre")));

	Body = NewObject<UPoseableMeshComponent>(Owner, TEXT("RiderBody"));
	Body->SetupAttachment(Parent);
	Body->SetSkinnedAssetAndUpdate(Mesh);
	Body->SetRelativeTransform(FTransform(CompToLocal));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->bComponentUseFixedSkelBounds = true;   // it never moves far from the pose it was made in
	Body->CastShadow = true;
	// The kit: the glb's material slots are named jacket, jeans, boots, sole, gloves.
	const TArray<FName> Slots = Body->GetMaterialSlotNames();
	for (int32 I = 0; I < Slots.Num(); ++I)
		for (const TPair<FString, UMaterialInterface*>& M : Mats)
			if (M.Value && Slots[I].ToString().Contains(M.Key)) Body->SetMaterial(Body->GetMaterialIndex(Slots[I]), M.Value);
	Body->RegisterComponent();

	if (UStaticMesh* HM = LoadObject<UStaticMesh>(nullptr, *HelmetPath))
	{
		Helmet = NewObject<UStaticMeshComponent>(Owner, TEXT("RiderHelmet"));
		Helmet->SetStaticMesh(HM);
		Helmet->SetupAttachment(Parent);
		Helmet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Helmet->SetGenerateOverlapEvents(false);
		Helmet->bCastHiddenShadow = true;   // still throws its shadow when the camera's inside it
		HelmetRest = HelmetFix;
		Helmet->SetRelativeTransform(HelmetRest);
		for (int32 I = 0; I < HM->GetStaticMaterials().Num(); ++I)
		{
			const FString Slot = HM->GetStaticMaterials()[I].MaterialSlotName.ToString();
			for (const TPair<FString, UMaterialInterface*>& M : Mats)
				if (M.Value && Slot.Contains(M.Key)) Helmet->SetMaterial(I, M.Value);
		}
		Helmet->RegisterComponent();
	}
	return true;
}

double FRiderRig::DrawnSpineDeg() const
{
	if (!Body || Pelvis == INDEX_NONE || Neck == INDEX_NONE) return 0.0;
	const FVector A = CompToLocal.TransformPosition(Body->GetBoneLocationByName(Bones[Pelvis].Name, EBoneSpaces::ComponentSpace));
	const FVector B = CompToLocal.TransformPosition(Body->GetBoneLocationByName(Bones[Neck].Name, EBoneSpaces::ComponentSpace));
	return FMath::RadiansToDegrees(FMath::Atan2(B.X - A.X, B.Z - A.Z));
}

bool FRiderRig::BoneRest(const TCHAR* Name, FVector& Out) const
{
	const int32 I = Find(Name);
	if (I == INDEX_NONE) return false;
	Out = Bones[I].Rest;
	return true;
}

void FRiderRig::Pose(const FRiderPoseInput& In)
{
	if (!Body) return;
	const int32 N = Bones.Num();
	TArray<FQuat> D;   // each bone's turn from rest, pawn local
	TArray<FVector> P;  // each bone's joint now, pawn local
	D.Init(FQuat::Identity, N);
	P.SetNumZeroed(N);
	TArray<bool> Set;
	Set.Init(false, N);
	auto Fk = [&](int32 I) {
		const int32 Par = Bones[I].Parent;
		if (Par == INDEX_NONE) { P[I] = Bones[I].Rest; return; }
		P[I] = P[Par] + D[Par].RotateVector(Bones[I].Rest - Bones[Par].Rest);
		if (!Set[I]) D[I] = D[Par];
	};

	// The back: the whole spine turns so its top goes where the chest is put, shared up the bones.
	const FVector Hip = In.SeatFit.TransformPosition(Bones[Pelvis].Rest) + FVector(0.0, In.HipShift, 0.0);
	const FVector Top0 = Bones[Neck].Rest - Bones[Pelvis].Rest;
	const FVector Top1 = Bones[Neck].Rest + In.ChestShift - Hip;
	const FQuat Back = FQuat::FindBetweenVectors(Top0, Top1);
	const FQuat Look(In.Look);

	for (int32 I = 0; I < N; ++I)
	{
		if (I == Pelvis)
		{
			P[I] = Hip;
			D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.15);
			Set[I] = true;
			continue;
		}
		if (I == Spine[0]) { D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.4); Set[I] = true; }
		else if (I == Spine[1]) { D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.75); Set[I] = true; }
		else if (I == Spine[2] || I == Neck) { D[I] = Back; Set[I] = true; }
		else if (I == Head) { D[I] = Look * FQuat::Slerp(FQuat::Identity, Back, 0.3); Set[I] = true; }
		Fk(I);
		for (int32 S = 0; S < 2; ++S)
		{
			if (I == UpperArm[S])
			{
				// The hands stay on the grips, which turn with the bars; the elbows bend to suit. The
				// right hand also rolls about its grip with the throttle, and both follow fitted bars.
				const FHand& Hd = Hands[S];
				const FQuat Twist(Hd.TwistAxis, FMath::DegreesToRadians(Hd.TwistDeg * FMath::Clamp(In.Throttle, 0.0, 1.0)));
				const FVector W0 = Bones[Hand[S]].Rest, E0 = Bones[LowerArm[S]].Rest, S0 = Bones[I].Rest;
				const FVector Wt = In.GripFit[S].TransformPosition(Hd.Grip + Twist.RotateVector(W0 - Hd.Grip));
				const double Off = FMath::Clamp(In.Limp, 0.0, 1.0);
				const FVector W = FMath::Lerp(In.BarPivot + In.BarTurn.RotateVector(Wt - In.BarPivot), In.HandsOff[S], Off);
				const double Lu = (E0 - S0).Size(), Lf = (W0 - E0).Size();
				const FVector E = RigMidJoint(P[I], W, Lu, Lf, ElbowPole[S]);
				const FVector Hinge0 = FVector::CrossProduct(E0 - S0, W0 - E0), Hinge = FVector::CrossProduct(E - P[I], W - E);
				D[I] = AlignFrames(E0 - S0, Hinge0, E - P[I], Hinge); Set[I] = true;
				D[LowerArm[S]] = AlignFrames(W0 - E0, Hinge0, W - E, Hinge); Set[LowerArm[S]] = true;
				D[Hand[S]] = FQuat::Slerp(In.BarTurn * In.GripFit[S].GetRotation() * Twist, D[LowerArm[S]], Off); Set[Hand[S]] = true;
				// Index and middle fingers: off the grip and over the lever, then pulling it in.
				const double On = FMath::Clamp(In.LeverFingers[S], 0.0, 1.0), Pull = FMath::Clamp(In.LeverPull[S], 0.0, 1.0);
				for (const FFinger& F : Hd.Fingers)
				{
					const FVector2D Ang = FMath::Lerp(F.Reach, F.Pull, Pull) * On * (1.0 - Off);
					D[F.Bone01] = D[Hand[S]] * FQuat(Hd.FingerAxis, FMath::DegreesToRadians(Ang.X)); Set[F.Bone01] = true;
					D[F.Bone02] = D[Hand[S]] * FQuat(Hd.FingerAxis, FMath::DegreesToRadians(Ang.X + Ang.Y)); Set[F.Bone02] = true;
				}
			}
			else if (I == Thigh[S])
			{
				// Feet on the pegs, or down on the road as the bike stops.
				const FVector A0 = Bones[Foot[S]].Rest, K0 = Bones[Calf[S]].Rest, H0 = Bones[I].Rest;
				const FVector Down = S == 0 ? In.FootDownL : In.FootDownR;
				const FVector A = FMath::Lerp(In.PegFit[S].TransformPosition(A0), Down, FMath::Clamp(In.FeetDown, 0.0, 1.0));
				const double Lt = (K0 - H0).Size(), Ls = (A0 - K0).Size();
				const FVector Pole = (KneePole[S] + FVector(0.0, 0.0, 0.6) * In.FeetDown).GetSafeNormal();
				const FVector K = RigMidJoint(P[I], A, Lt, Ls, Pole);
				const FVector Hinge0 = FVector::CrossProduct(K0 - H0, A0 - K0), Hinge = FVector::CrossProduct(K - P[I], A - K);
				D[I] = AlignFrames(K0 - H0, Hinge0, K - P[I], Hinge); Set[I] = true;
				D[Calf[S]] = AlignFrames(A0 - K0, Hinge0, A - K, Hinge); Set[Calf[S]] = true;
				D[Foot[S]] = FQuat::Identity; Set[Foot[S]] = true;   // the foot stays level
			}
		}
	}

	PosedSpineDeg = FMath::RadiansToDegrees(FMath::Atan2(P[Neck].X - P[Pelvis].X, P[Neck].Z - P[Pelvis].Z));

	// Hand the pose to the mesh, parents first.
	for (int32 I = 0; I < N; ++I)
	{
		const FTransform T(ToComp(D[I]) * Bones[I].RestCS, LocalToComp.TransformPosition(P[I]), Bones[I].RestScale);
		Body->SetBoneTransformByName(Bones[I].Name, T, EBoneSpaces::ComponentSpace);
	}

	// The helmet rides on the head.
	const FVector HeadRest = Bones[Head].Rest;
	HelmetCentre = P[Head] + D[Head].RotateVector(HelmetCentreRest - HeadRest);
	if (Helmet)
		Helmet->SetRelativeTransform(HelmetRest * FTransform(-HeadRest) * FTransform(D[Head], P[Head]));
}
