#include "Shed/TorqueAvatar.h"

#include "Shed/TorqueProfile.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
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

// One more mesh of the MetaHuman (face, clothes) following the body, or a groom (hair, brows,
// beard) bound to one of them. Materials are the Blueprint component's overrides, by slot ("" keeps
// the mesh's own).
struct FPart { FString Name, Mesh, Groom, Binding, AttachTo, Cloth, Static, Bone; TArray<FString> Materials; int32 Lod = -1; };
struct FEntry { FString Name, Blueprint, BodyMesh; TArray<FString> BodyMaterials; TArray<FPart> Parts; double HeightCm = 0.0; };

TArray<FString> Strings(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
	TArray<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (O->TryGetArrayField(Field, Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr) Out.Add(V->AsString());
	return Out;
}

void ApplyMaterials(UMeshComponent* C, const TArray<FString>& Paths)
{
	for (int32 I = 0; I < Paths.Num(); ++I)
		if (!Paths[I].IsEmpty())
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Paths[I])) C->SetMaterial(I, M);
}

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
		E.BodyMaterials = Strings(*O, TEXT("body_materials"));
		(*O)->TryGetNumberField(TEXT("height_cm"), E.HeightCm);
		const TArray<TSharedPtr<FJsonValue>>* PArr = nullptr;
		if ((*O)->TryGetArrayField(TEXT("parts"), PArr))
			for (const TSharedPtr<FJsonValue>& PV : *PArr)
			{
				const TSharedPtr<FJsonObject>* PO = nullptr;
				if (!PV->TryGetObject(PO)) continue;
				FPart P;
				(*PO)->TryGetStringField(TEXT("name"), P.Name);
				(*PO)->TryGetStringField(TEXT("mesh"), P.Mesh);
				(*PO)->TryGetStringField(TEXT("groom"), P.Groom);
				(*PO)->TryGetStringField(TEXT("binding"), P.Binding);
				(*PO)->TryGetStringField(TEXT("attach"), P.AttachTo);
				(*PO)->TryGetStringField(TEXT("cloth"), P.Cloth);
				(*PO)->TryGetStringField(TEXT("static"), P.Static);
				(*PO)->TryGetStringField(TEXT("bone"), P.Bone);
				(*PO)->TryGetNumberField(TEXT("lod"), P.Lod);
				P.Materials = Strings(*PO, TEXT("materials"));
				E.Parts.Add(P);
			}
		if (!E.Blueprint.IsEmpty() || !E.BodyMesh.IsEmpty()) List.Add(E);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: %d MetaHuman avatars listed"), List.Num());
	return List;
}
// Call O's Blueprint function Fn with one argument, set by Set on its first parameter (by name, so
// the game needn't link the hair or cloth modules). False when there's no such function.
bool CallWithOne(UObject* O, const TCHAR* Fn, TFunctionRef<void(FProperty*, void*)> Set)
{
	UFunction* F = O ? O->FindFunction(FName(Fn)) : nullptr;
	if (!F) return false;
	TArray<uint8> Buf;
	Buf.SetNumZeroed(FMath::Max<int32>(F->ParmsSize, 1));
	F->InitializeStruct(Buf.GetData());
	for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		if (!It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
		{
			Set(*It, It->ContainerPtrToValuePtr<void>(Buf.GetData()));
			break;
		}
	O->ProcessEvent(F, Buf.GetData());
	F->DestroyStruct(Buf.GetData());
	return true;
}

// Where Bone sits at rest in On's mesh space (false when it has no such bone).
bool BoneRest(const USkinnedMeshComponent* On, FName BoneName, FTransform& Rest)
{
	Rest = FTransform::Identity;
	const USkinnedAsset* SA = On ? On->GetSkinnedAsset() : nullptr;
	if (!SA || BoneName.IsNone()) return false;
	const FReferenceSkeleton& Ref = SA->GetRefSkeleton();
	int32 B = Ref.FindBoneIndex(BoneName);
	if (B == INDEX_NONE) return false;
	while (B != INDEX_NONE)
	{
		Rest = Rest * Ref.GetRefBonePose()[B];
		B = Ref.GetParentIndex(B);
	}
	return true;
}

// The MetaHuman's other meshes on Driver, following its pose, and its grooms bound to them. No
// Blueprint: its Anim Blueprints come from the MetaHuman plugin, which is editor-only. Grooms are
// made by class name, so the game needn't link the hair module (and skips them without it).
void AddParts(AActor* Owner, UPoseableMeshComponent* Driver, const FEntry& E, TArray<TWeakObjectPtr<USceneComponent>>& Out,
	UPoseableMeshComponent*& OutFace)
{
	TMap<FString, USceneComponent*> ByName;
	for (const FPart& P : E.Parts)
	{
		if (P.Mesh.IsEmpty()) continue;
		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *P.Mesh);
		if (!M) { UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no mesh %s"), *E.Name, *P.Mesh); continue; }
		// The face is posed too (copying the body's neck and head, plus the profile's face edits);
		// the rest follow the body as their leader.
		const bool bFace = !OutFace && (P.Name.Contains(TEXT("Face")) || M->GetRefSkeleton().FindBoneIndex(TEXT("FACIAL_C_FacialRoot")) != INDEX_NONE);
		USkinnedMeshComponent* C = nullptr;
		if (bFace)
		{
			UPoseableMeshComponent* F = NewObject<UPoseableMeshComponent>(Owner);
			F->SetSkinnedAssetAndUpdate(M);
			F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			OutFace = F;
			C = F;
		}
		else
		{
			USkeletalMeshComponent* S = NewObject<USkeletalMeshComponent>(Owner);
			S->SetSkeletalMeshAsset(M);
			C = S;
		}
		C->SetupAttachment(Driver);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();   // made at run time: precache before the render state
		C->RegisterComponent();
		ApplyMaterials(C, P.Materials);
		if (!bFace) C->SetLeaderPoseComponent(Driver, true);
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: %s %s"), *E.Name, bFace ? TEXT("face") : TEXT("mesh"), *P.Mesh);
		ByName.Add(P.Name, C);
		Out.Add(C);
	}
	// Hair cards kept as a static mesh, made in the face's space: on its head bone, offset by the
	// bone's rest so it sits where it was made and turns with the head.
	for (const FPart& P : E.Parts)
	{
		if (P.Static.IsEmpty()) continue;
		UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *P.Static);
		if (!SM) { UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no static mesh %s"), *E.Name, *P.Static); continue; }
		USceneComponent** To = ByName.Find(P.AttachTo);
		USkinnedMeshComponent* On = To ? Cast<USkinnedMeshComponent>(*To) : nullptr;
		if (!On) On = Driver;
		const FName BoneName(*P.Bone);
		FTransform Rest;
		const bool bOnBone = BoneRest(On, BoneName, Rest);
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetStaticMesh(SM);
		C->SetupAttachment(On, bOnBone ? BoneName : NAME_None);
		C->SetRelativeTransform(bOnBone ? Rest.Inverse() : FTransform::Identity);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(true);
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();   // made at run time: precache before the render state
		C->RegisterComponent();
		ApplyMaterials(C, P.Materials);
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: hair cards %s on %s%s"), *E.Name, *P.Static, *On->GetName(),
			bOnBone ? *FString::Printf(TEXT(", bone %s"), *P.Bone) : TEXT(""));
		Out.Add(C);
	}
	// Clothes kept as a Chaos outfit or cloth asset: a cloth component posed by the body, not simulated
	// (cheaper, and steady on the bike).
	UClass* ClothClass = FindObject<UClass>(nullptr, TEXT("/Script/ChaosClothAssetEngine.ChaosClothComponent"));
	for (const FPart& P : E.Parts)
	{
		if (P.Cloth.IsEmpty()) continue;
		if (!ClothClass || !ClothClass->IsChildOf(USkinnedMeshComponent::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no Chaos cloth module in this build, %s skipped"), *E.Name, *P.Name);
			break;
		}
		UObject* A = StaticLoadObject(UObject::StaticClass(), nullptr, *P.Cloth);
		if (!A) { UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no clothes %s"), *E.Name, *P.Cloth); continue; }
		USkinnedMeshComponent* C = NewObject<USkinnedMeshComponent>(Owner, ClothClass);
		const bool bSet = CallWithOne(C, TEXT("SetAsset"), [A](FProperty* Pr, void* V)
		{
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Pr))
				if (A->IsA(OP->PropertyClass)) OP->SetObjectPropertyValue(V, A);
		});
		if (!bSet)
			if (USkinnedAsset* SA = Cast<USkinnedAsset>(A)) C->SetSkinnedAssetAndUpdate(SA);
		CallWithOne(C, TEXT("SetEnableSimulation"), [](FProperty* Pr, void* V)
		{
			if (FBoolProperty* B = CastField<FBoolProperty>(Pr)) B->SetPropertyValue(V, false);
		});
		C->SetupAttachment(Driver);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();   // made at run time: precache before the render state
		C->RegisterComponent();
		C->SetLeaderPoseComponent(Driver, true);
		ApplyMaterials(C, P.Materials);
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: clothes %s (%s)"), *E.Name, *P.Cloth, C->GetSkinnedAsset() ? TEXT("shown") : TEXT("asset not taken"));
		ByName.Add(P.Name, C);
		Out.Add(C);
	}
	UClass* GroomClass = FindObject<UClass>(nullptr, TEXT("/Script/HairStrandsCore.GroomComponent"));
	UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: %d parts; hair module %s, cloth module %s"), *E.Name, E.Parts.Num(),
		GroomClass ? TEXT("loaded") : TEXT("missing"), ClothClass ? TEXT("loaded") : TEXT("missing"));
	for (const FPart& P : E.Parts)
	{
		if (P.Groom.IsEmpty()) continue;
		if (!GroomClass) { UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no hair module in this build, %s skipped"), *E.Name, *P.Name); break; }
		UObject* G = StaticLoadObject(UObject::StaticClass(), nullptr, *P.Groom);
		if (!G) continue;
		// Rigid on the head bone (made in the face's space, so offset by the bone's rest), not
		// bound to the skin: short hair barely deforms, and a binding needs the skin cache and costs
		// a GPU pass per avatar. The asset and LOD are set through the component's own functions
		// before it's registered, so it builds its hair groups and precaches its shaders.
		USceneComponent** To = ByName.Find(P.AttachTo);
		USkinnedMeshComponent* On = To ? Cast<USkinnedMeshComponent>(*To) : nullptr;
		if (!On) On = Driver;
		FTransform Rest;
		const bool bOnBone = BoneRest(On, FName(TEXT("head")), Rest);
		USceneComponent* C = NewObject<USceneComponent>(Owner, GroomClass);
		C->SetupAttachment(On, bOnBone ? FName(TEXT("head")) : NAME_None);
		C->SetRelativeTransform(bOnBone ? Rest.Inverse() : FTransform::Identity);
		const bool bSet = CallWithOne(C, TEXT("SetGroomAsset"), [G](FProperty* Pr, void* V)
		{
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Pr))
				if (G->IsA(OP->PropertyClass)) OP->SetObjectPropertyValue(V, G);
		});
		if (!bSet)
			if (FObjectPropertyBase* Pr = FindFProperty<FObjectPropertyBase>(C->GetClass(), TEXT("GroomAsset")))
				Pr->SetObjectPropertyValue_InContainer(C, G);
		// Cards, not strands, where the groom has them: strands are the GPU cost.
		if (P.Lod >= 0)
		{
			const int32 Lod = P.Lod;
			CallWithOne(C, TEXT("SetForcedLOD"), [Lod](FProperty* Pr, void* V)
			{
				if (FIntProperty* I = CastField<FIntProperty>(Pr)) I->SetPropertyValue(V, Lod);
			});
		}
		if (UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(C))
		{
			PC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PC->SetGenerateOverlapEvents(false);
		}
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();   // made at run time: precache before the render state
		C->RegisterComponent();
		if (UMeshComponent* MC = Cast<UMeshComponent>(C)) ApplyMaterials(MC, P.Materials);
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: groom %s on %s%s, LOD %d (asset %s)"), *E.Name, *P.Name, *On->GetName(),
			bOnBone ? TEXT(", bone head") : TEXT(""), P.Lod, bSet ? TEXT("set") : TEXT("by property"));
		Out.Add(C);
	}
}
} // namespace TorqueAvatarMath

// Nothing to undo here: the owner calls Destroy in EndPlay, and a destructor run by the garbage
// collector mustn't destroy actors or components.
FTorqueAvatar::~FTorqueAvatar() = default;

int32 FTorqueAvatar::Count() { return TorqueAvatarMath::Entries().Num(); }

FString FTorqueAvatar::NameOf(int32 Index)
{
	const TArray<TorqueAvatarMath::FEntry>& List = TorqueAvatarMath::Entries();
	if (List.Num() == 0) return FString();
	return List[((Index % List.Num()) + List.Num()) % List.Num()].Name.Replace(TEXT("_"), TEXT(" "));
}

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
	for (TWeakObjectPtr<USceneComponent>& P : Parts)
		if (USceneComponent* C = P.Get()) C->DestroyComponent();
	Parts.Reset();
	Hair.Reset();
	bRode = false;
	FaceMesh = nullptr;
	FaceFromDriver.Reset();
	FaceLocal.Reset();
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
	// The fingers (riding: wrapped round the grips, two over a lever), when the mesh has them.
	static const TCHAR* FingerNames[5] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
	for (int32 S = 0; S < 2; ++S)
		for (int32 F = 0; F < 5; ++F)
			for (int32 J = 0; J < 3; ++J)
			{
				const FName FN(*FString::Printf(TEXT("%s_%02d_%s"), FingerNames[F], J + 1, S == 0 ? TEXT("l") : TEXT("r")));
				const int32 I = Ref.FindBoneIndex(FN);
				if (I == INDEX_NONE) continue;
				FBone B; B.Name = FN; B.Index = I; B.Rest = RefCS[I].GetLocation(); B.RestQ = RefCS[I].GetRotation();
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
	for (int32 S = 0; S < 2; ++S)
		for (int32 F = 0; F < 5; ++F)
			for (int32 J = 0; J < 3; ++J)
				Finger[S][F][J] = Find(*FString::Printf(TEXT("%s_%02d_%s"), FingerNames[F], J + 1, S == 0 ? TEXT("l") : TEXT("r")));

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
	Driver->PrecachePSOs();
	Driver->RegisterComponent();
	if (AActor* A = Spawned.Get())
	{
		TArray<USkinnedMeshComponent*> Skins;
		A->GetComponents(Skins);
		for (USkinnedMeshComponent* C : Skins)
			if (C != Driver) C->SetLeaderPoseComponent(Driver, true);
	}
	else
	{
		ApplyMaterials(Driver, E.BodyMaterials);
		AddParts(Owner, Driver, E, Parts, FaceMesh);
		for (const TWeakObjectPtr<USceneComponent>& C : Parts)
			if (C.IsValid() && (C->GetClass()->GetName().Contains(TEXT("Groom")) || C->IsA<UStaticMeshComponent>())) Hair.Add(C);
		if (FaceMesh) SetupFace();
	}
	// Its height: the top of its head (the face mesh's bounds, else the head bone and a crown), and
	// scaled to the height avatars.json gives it (the builds can come out all the template's size).
	RestHeight = FVector::DotProduct(Bones[Head].Rest, Up) + 20.0;
	if (const USkeletalMesh* FaceAsset = FaceMesh ? Cast<USkeletalMesh>(FaceMesh->GetSkinnedAsset()) : nullptr)
	{
		const FBox Box = FaceAsset->GetImportedBounds().GetBox();
		double Top = 0.0;
		for (int32 C = 0; C < 8; ++C)
		{
			const FVector Corner((C & 1) ? Box.Max.X : Box.Min.X, (C & 2) ? Box.Max.Y : Box.Min.Y, (C & 4) ? Box.Max.Z : Box.Min.Z);
			Top = FMath::Max(Top, FVector::DotProduct(Corner, Up));
		}
		if (Top > RestHeight - 30.0 && Top < RestHeight + 30.0) RestHeight = Top;
	}
	BaseScale = E.HeightCm > 100.0 && RestHeight > 100.0 ? FMath::Clamp(E.HeightCm / RestHeight, 0.85, 1.15) : 1.0;
	Driver->SetRelativeScale3D(FVector(BaseScale));
	UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s (%s, %d bones driven, legs %.0f cm, arms %.0f cm, %.0f cm tall, shown at %.0f cm)"), *Name,
		*Mesh->GetName(), Bones.Num(), LegLen, ArmLen, RestHeight, RestHeight * BaseScale);
	bPoseLogged = false;
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
	PoseFace();
	// Once, a second or so in: whether the pose reached the mesh (the left foot from its rest).
	PosedFor += Dt;
	if (!bPoseLogged && PosedFor > 1.5)
	{
		bPoseLogged = true;
		const FVector Asked = P[FootBone[0]];
		const FVector Got = Driver->GetBoneTransform(Bones[FootBone[0]].Index, FTransform::Identity).GetLocation();
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s posing: walk %.2f, left foot asked %s, mesh has %s (rest %s)"), *Name, Walk,
			*Asked.ToCompactString(), *Got.ToCompactString(), *Bones[FootBone[0]].Rest.ToCompactString());
	}
}

void FTorqueAvatar::SetupFace()
{
	const FReferenceSkeleton& Ref = FaceMesh->GetSkinnedAsset()->GetRefSkeleton();
	const FReferenceSkeleton& Body = Driver->GetSkinnedAsset()->GetRefSkeleton();
	const int32 Num = Ref.GetNum();
	FaceFromDriver.SetNum(Num);
	FaceLocal.SetNum(Num);
	for (int32 I = 0; I < Num; ++I)
	{
		FaceFromDriver[I] = Body.FindBoneIndex(Ref.GetBoneName(I));
		FaceLocal[I] = Ref.GetRefBonePose()[I];
	}
	FaceHead = Ref.FindBoneIndex(TEXT("head"));
	FaceScale = 1.0;
	PoseFace();
}

void FTorqueAvatar::PoseFace()
{
	if (!FaceMesh || !Driver || FaceMesh->BoneSpaceTransforms.Num() != FaceLocal.Num()) return;
	const TArray<FTransform>& From = Driver->BoneSpaceTransforms;
	for (int32 I = 0; I < FaceLocal.Num(); ++I)
	{
		const int32 D = FaceFromDriver[I];
		FTransform T = D != INDEX_NONE && From.IsValidIndex(D) ? From[D] : FaceLocal[I];
		if (I == FaceHead) T.SetScale3D(T.GetScale3D() * FaceScale);
		FaceMesh->BoneSpaceTransforms[I] = T;
	}
	FaceMesh->MarkRefreshTransformDirty();
}

namespace TorqueAvatarMath
{
// A face slider's joints: any FACIAL_* bone whose name (lower case) has one of Any and none of
// Not; Sides only the left and right ones. Each is moved by Move (cm, in the body's up / forward /
// out-to-its-side axes) or scaled by 1 + Scale, times the slider's -1 .. 1.
struct FFaceEdit
{
	int32 Slider;
	const TCHAR* Any[5];
	const TCHAR* Not[3];
	bool bSides;
	FVector Move;      // x up, y forward, z out to the side
	double Scale;
};
const FFaceEdit FaceEdits[] = {
	{1, {TEXT("brow"), TEXT("forehead")}, {}, false, FVector(0.5, 0.0, 0.0), 0.0},
	{2, {TEXT("_eye"), TEXT("eyelid"), TEXT("eyesack"), TEXT("eyecorner"), TEXT("eyelash")}, {TEXT("brow")}, true, FVector(0.0, 0.0, 0.35), 0.0},
	{3, {TEXT("nosetip")}, {}, false, FVector(0.0, 0.6, 0.0), 0.0},
	{4, {TEXT("nostril"), TEXT("nosewing")}, {}, true, FVector(0.0, 0.0, 0.3), 0.0},
	{5, {TEXT("noseupper"), TEXT("nosebridge")}, {}, false, FVector(0.0, 0.35, 0.0), 0.0},
	{6, {TEXT("cheekbone"), TEXT("cheekouter"), TEXT("cheekupper")}, {}, true, FVector(0.0, 0.15, 0.35), 0.0},
	{7, {TEXT("lip")}, {TEXT("corner"), TEXT("rotation")}, false, FVector(0.0, 0.2, 0.0), 0.0},
	{8, {TEXT("jaw"), TEXT("masseter")}, {}, true, FVector(0.0, 0.0, 0.5), 0.0},
	{9, {TEXT("chin")}, {}, false, FVector(-0.4, 0.4, 0.0), 0.0},
	{10, {TEXT("_ear")}, {}, true, FVector::ZeroVector, 0.15},
};
} // namespace TorqueAvatarMath

void FTorqueAvatar::SetLook(const FTorqueProfile& Look)
{
	using namespace TorqueAvatarMath;
	if (!Driver) return;
	Driver->SetRelativeScale3D(FVector(BaseScale * Look.HeightScale()));
	if (!FaceMesh) return;
	FaceScale = 1.0 + 0.06 * FMath::Clamp((double)Look.Face[0], -1.0, 1.0);
	const FReferenceSkeleton& Ref = FaceMesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 Num = Ref.GetNum();
	TArray<FTransform> RestCS;
	RestCS.SetNum(Num);
	TArray<FVector> Moved;   // each joint's move, its parents' included (component space)
	Moved.Init(FVector::ZeroVector, Num);
	for (int32 I = 0; I < Num; ++I)
	{
		const int32 Par = Ref.GetParentIndex(I);
		RestCS[I] = Par >= 0 ? Ref.GetRefBonePose()[I] * RestCS[Par] : Ref.GetRefBonePose()[I];
		FaceLocal[I] = Ref.GetRefBonePose()[I];
		if (Par >= 0) Moved[I] = Moved[Par];
		const FString Low = Ref.GetBoneName(I).ToString().ToLower();
		if (!Low.StartsWith(TEXT("facial_"))) continue;
		const double SideSign = Low.Contains(TEXT("_l_")) ? -1.0 : Low.Contains(TEXT("_r_")) ? 1.0 : 0.0;
		FVector Own = FVector::ZeroVector;
		double Grow = 1.0;
		for (const FFaceEdit& Ed : FaceEdits)
		{
			const double V = FMath::Clamp((double)Look.Face[Ed.Slider], -1.0, 1.0);
			if (V == 0.0 || (Ed.bSides && SideSign == 0.0)) continue;
			bool bAny = false, bNot = false;
			for (const TCHAR* W : Ed.Any) if (W && Low.Contains(W)) bAny = true;
			for (const TCHAR* W : Ed.Not) if (W && Low.Contains(W)) bNot = true;
			if (!bAny || bNot) continue;
			Own += (Up * Ed.Move.X + Fwd * Ed.Move.Y + Right * (SideSign * Ed.Move.Z)) * V;
			Grow *= 1.0 + Ed.Scale * V;
		}
		Moved[I] += Own;
		if (Par >= 0 && !Moved[I].IsNearlyZero())
		{
			// Its place in its parent's (moved, unturned) frame.
			const FVector At = RestCS[I].GetLocation() + Moved[I];
			const FVector ParAt = RestCS[Par].GetLocation() + Moved[Par];
			FaceLocal[I].SetTranslation(RestCS[Par].GetRotation().UnrotateVector(At - ParAt) / RestCS[Par].GetScale3D());
		}
		if (Grow != 1.0) FaceLocal[I].SetScale3D(FaceLocal[I].GetScale3D() * Grow);
	}
	PoseFace();
}

FVector FTorqueAvatar::HeadWorld() const
{
	return Driver && Head != INDEX_NONE ? Driver->GetSocketLocation(Bones[Head].Name) : FVector::ZeroVector;
}

double FTorqueAvatar::StandingHeight() const
{
	if (!Driver || Head == INDEX_NONE) return 0.0;
	return RestHeight * Driver->GetRelativeScale3D().Z;
}

void FTorqueAvatar::SetShown(bool bBody, bool bHead, bool bHair)
{
	if (!Driver) return;
	Driver->SetVisibility(bBody, false);
	for (const TWeakObjectPtr<USceneComponent>& P : Parts)
	{
		USceneComponent* C = P.Get();
		if (!C) continue;
		const bool bIsHair = Hair.Contains(P);
		C->SetVisibility(bIsHair ? (bHead && bHair) : (C == FaceMesh ? bHead : bBody), false);
	}
}

bool FTorqueAvatar::HeadInParent(FVector& At, FQuat& Turn) const
{
	if (!Driver || !bRode) return false;
	At = RideHead;
	Turn = RideHeadTurn;
	return true;
}

void FTorqueAvatar::PoseRiding(const FTorqueRideTargets& Tg)
{
	using namespace TorqueAvatarMath;
	if (!Driver) return;
	const int32 N = Bones.Num();
	TArray<FQuat> D;
	TArray<FVector> P;
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
	// The parent's space to this mesh's component space (its turn to face +x, and its scale).
	const FTransform Rel = Driver->GetRelativeTransform();
	const FQuat RelQ = Rel.GetRotation();
	auto ToC = [&Rel](const FVector& V) { return Rel.InverseTransformPosition(V); };
	auto ToCQ = [&RelQ](const FQuat& Q) { return RelQ.Inverse() * Q * RelQ; };

	// The back from the hips towards the rider's neck, shared up the spine as the rider rig does.
	const FVector Hip = ToC(Tg.Hip);
	FQuat Back = FQuat::FindBetweenVectors(Bones[Neck != INDEX_NONE ? Neck : Head].Rest - Bones[Pelvis].Rest, ToC(Tg.Neck) - Hip);
	const FQuat LookC = ToCQ(Tg.Look);
	const double Off = FMath::Clamp(Tg.Limp, 0.0, 1.0);

	// The hands round the grips: each hand's frame from its own bones (wrist to middle knuckle, and
	// across the knuckles from pinky to index, which lies along the bar), turned so the fingers point
	// forward and down past the bar with the index inboard, and placed so the bar sits across the
	// palm just short of the knuckles. Off the bike (Limp), the rider rig's wrist.
	FQuat HandQ[2];
	FVector Aim0[2], Palm0[2], WristAt[2];
	for (int32 S = 0; S < 2; ++S)
	{
		const double Side = S == 0 ? -1.0 : 1.0;   // the left side is -Right
		const FVector W0 = Bones[HandBone[S]].Rest, E0 = Bones[LowerArm[S]].Rest;
		const int32 Knuckle = Finger[S][2][0], IndexK = Finger[S][1][0], PinkyK = Finger[S][4][0];
		Aim0[S] = Knuckle != INDEX_NONE ? Bones[Knuckle].Rest - W0 : W0 - E0;
		FVector Across0 = (IndexK != INDEX_NONE && PinkyK != INDEX_NONE) ? Bones[IndexK].Rest - Bones[PinkyK].Rest : Right * Side;
		Across0 -= Aim0[S].GetSafeNormal() * FVector::DotProduct(Across0, Aim0[S].GetSafeNormal());
		// Palm side: Aim x Across for the right hand, the other way for the left (mirrored).
		Palm0[S] = (FVector::CrossProduct(Aim0[S], Across0) * Side).GetSafeNormal();
		const FVector Bar = ToC(Tg.Grip[S]);
		const FVector Across1 = -Right * Side;   // the index towards the bike's middle
		FVector Aim1 = Bar - ToC(Tg.Wrist[S]);
		Aim1 -= Across1 * FVector::DotProduct(Aim1, Across1);
		Aim1 = (Aim1.Size() > 2.0 ? Aim1.GetSafeNormal() : FVector::ZeroVector) + (Fwd * 0.8 - Up * 0.6) * 0.5;
		Aim1 = (Aim1 - Across1 * FVector::DotProduct(Aim1, Across1)).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
		HandQ[S] = Align(Aim0[S], Across0, Aim1, Across1);
		const FVector Palm1 = HandQ[S].RotateVector(Palm0[S]);
		const FVector KnuckleAt = Bar + Aim1 * 1.5 - Palm1 * 3.0;   // bar 1.5 cm short of the knuckles, 3 cm palm-side of the bones
		WristAt[S] = FMath::Lerp(KnuckleAt - HandQ[S].RotateVector(Aim0[S]), ToC(Tg.Wrist[S]), Off);
	}
	const FQuat BackRig = Back;   // the head keeps the rider rig's gaze, however far the back leans
	// Low, far bars (clip-ons): lean the back further forward, from the hips, until the shoulders are
	// near enough for both hands to reach (at most 35 degrees more). Shoulders estimated as turned
	// with the whole back, which the upper spine nearly is.
	{
		const FVector Pitch = FVector::CrossProduct(Right, Up).GetSafeNormal();   // turning about Right takes Up towards this
		const double Sign = FVector::DotProduct(Pitch, Fwd) >= 0.0 ? 1.0 : -1.0;
		const FVector Pelvis0 = Bones[Pelvis].Rest;
		for (int32 Step = 0; Step < 12; ++Step)
		{
			double Short = 0.0;
			for (int32 S = 0; S < 2; ++S)
			{
				const FVector S0 = Bones[UpperArm[S]].Rest;
				const double Reach = ((Bones[LowerArm[S]].Rest - S0).Size() + (Bones[HandBone[S]].Rest - Bones[LowerArm[S]].Rest).Size()) * 0.97;
				const FVector Sh = Hip + Back.RotateVector(S0 - Pelvis0);
				Short = FMath::Max(Short, (WristAt[S] - Sh).Size() - Reach);
			}
			if (Short <= 0.0 || Off > 0.5) break;
			Back = FQuat(Right, Sign * FMath::DegreesToRadians(3.0)) * Back;
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		if (I == Pelvis) { P[I] = Hip; D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.15); Set[I] = true; continue; }
		if (I == Spine[0]) { D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.4); Set[I] = true; }
		else if (I == Spine[1]) { D[I] = FQuat::Slerp(FQuat::Identity, Back, 0.75); Set[I] = true; }
		else if (I == Spine[2] || I == Neck) { D[I] = Back; Set[I] = true; }
		else if (I == Head) { D[I] = LookC * FQuat::Slerp(FQuat::Identity, BackRig, 0.3); Set[I] = true; }
		Fk(I);
		for (int32 S = 0; S < 2; ++S)
		{
			const double Side = S == 0 ? -1.0 : 1.0;   // the left side is -Right
			if (I == UpperArm[S])
			{
				// The arm reaches the wrist the hand needs on the bar (as far as it can).
				const FVector S0 = Bones[I].Rest, E0 = Bones[LowerArm[S]].Rest, W0 = Bones[HandBone[S]].Rest;
				const FVector W = WristAt[S];
				const double Lu = (E0 - S0).Size(), Lf = (W0 - E0).Size();
				const FVector Pole = (-Fwd * 0.3 + Right * (Side * 0.7) - Up * 0.6).GetSafeNormal();
				const FVector E = MidJoint(P[I], W, Lu, Lf, Pole);
				const FVector Hinge0 = FVector::CrossProduct(W0 - S0, -Fwd);
				const FVector Hinge = FVector::CrossProduct(W - P[I], Pole);
				D[I] = Align(E0 - S0, Hinge0, E - P[I], Hinge); Set[I] = true;
				D[LowerArm[S]] = Align(W0 - E0, Hinge0, W - E, Hinge); Set[LowerArm[S]] = true;
				D[HandBone[S]] = FQuat::Slerp(HandQ[S], D[LowerArm[S]], Off); Set[HandBone[S]] = true;
				// The fingers round the grip; the index and middle over a lever when they're on it.
				// Each finger turns about the bar's line, towards the palm.
				const FVector Curl0 = FVector::CrossProduct(Aim0[S].GetSafeNormal(), Palm0[S]).GetSafeNormal();
				const FVector Curl = D[HandBone[S]].RotateVector(Curl0);
				static const double WrapDeg[3] = {55.0, 80.0, 45.0};
				for (int32 F = 0; F < 5; ++F)
				{
					double K = 1.0 - Off * 0.7;
					if (F == 1 || F == 2)
					{
						const double On = FMath::Clamp(Tg.LeverFingers[S], 0.0, 1.0), Pull = FMath::Clamp(Tg.LeverPull[S], 0.0, 1.0);
						K *= FMath::Lerp(1.0, 0.35 + 0.45 * Pull, On);
					}
					double Sum = 0.0;
					for (int32 J = 0; J < 3; ++J)
					{
						const int32 B = Finger[S][F][J];
						if (B == INDEX_NONE) continue;
						Sum += (F == 0 ? 0.45 : 1.0) * WrapDeg[J] * K;
						D[B] = FQuat(Curl, FMath::DegreesToRadians(Sum)) * D[HandBone[S]]; Set[B] = true;
					}
				}
			}
			else if (I == Thigh[S])
			{
				// Ankles where the rider's are (on the pegs, or down at a stop); knees forward and a little out.
				const FVector H0 = Bones[I].Rest, K0 = Bones[Calf[S]].Rest, A0 = Bones[FootBone[S]].Rest;
				const FVector A = ToC(Tg.Ankle[S]);
				const double Lt = (K0 - H0).Size(), Ls = (A0 - K0).Size();
				const FVector Pole = (Fwd + Right * (Side * 0.25) + Up * 0.2).GetSafeNormal();
				const FVector K = MidJoint(P[I], A, Lt, Ls, Pole);
				const FVector Hinge0 = FVector::CrossProduct(A0 - H0, Fwd);
				const FVector Hinge = FVector::CrossProduct(A - P[I], Pole);
				D[I] = Align(K0 - H0, Hinge0, K - P[I], Hinge); Set[I] = true;
				D[Calf[S]] = Align(A0 - K0, Hinge0, A - K, Hinge); Set[Calf[S]] = true;
				D[FootBone[S]] = FQuat::Identity; Set[FootBone[S]] = true;   // level on the peg
			}
		}
	}
	for (int32 I = 0; I < N; ++I)
		Driver->SetBoneTransformByName(Bones[I].Name, FTransform(D[I] * Bones[I].RestQ, P[I]), EBoneSpaces::ComponentSpace);
	PoseFace();
	RideHead = Rel.TransformPosition(P[Head]);
	RideHeadTurn = RelQ * D[Head] * RelQ.Inverse();
	bRode = true;
}

