#include "Shed/TorqueAvatar.h"

#include "Shed/TorqueProfile.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
struct FPart { FString Name, Mesh, Groom, Binding, AttachTo, Cloth, Static, Bone, CardsMesh, FitMesh, CardsTexture; TArray<FString> Materials, CardsMaterials; FLinearColor HairColour = FLinearColor(0.045f, 0.028f, 0.016f); bool bHairColour = false; int32 Lod = -1; bool bLooseOnly = false; };
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
				(*PO)->TryGetStringField(TEXT("cards_mesh"), P.CardsMesh);
				(*PO)->TryGetStringField(TEXT("fit_mesh"), P.FitMesh);
				(*PO)->TryGetStringField(TEXT("cards_texture"), P.CardsTexture);
				const TArray<TSharedPtr<FJsonValue>>* Rgb = nullptr;
				if ((*PO)->TryGetArrayField(TEXT("hair_colour"), Rgb) && Rgb->Num() >= 3)
				{
					P.HairColour = FLinearColor((float)(*Rgb)[0]->AsNumber(), (float)(*Rgb)[1]->AsNumber(), (float)(*Rgb)[2]->AsNumber(), 1.0f);
					P.bHairColour = true;
				}
				(*PO)->TryGetBoolField(TEXT("loose_only"), P.bLooseOnly);
				P.CardsMaterials = Strings(*PO, TEXT("cards_materials"));
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
bool BoneRestOf(const USkinnedAsset* SA, FName BoneName, FTransform& Rest);
bool BoneRest(const USkinnedMeshComponent* On, FName BoneName, FTransform& Rest)
{
	return BoneRestOf(On ? On->GetSkinnedAsset() : nullptr, BoneName, Rest);
}
bool BoneRestOf(const USkinnedAsset* SA, FName BoneName, FTransform& Rest)
{
	Rest = FTransform::Identity;
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

// For the hair log: every int, float, bool or enum property and no-argument getter of O whose
// name has one of Words, with its value ("Name=Value, ..."), read by reflection.
FString DescribeByName(UObject* O, std::initializer_list<const TCHAR*> Words)
{
	if (!O) return TEXT("-");
	auto Wanted = [&Words](const FString& N) { for (const TCHAR* W : Words) if (N.Contains(W)) return true; return false; };
	TArray<FString> Out;
	for (TFieldIterator<FProperty> It(O->GetClass()); It; ++It)
	{
		if (!Wanted(It->GetName())) continue;
		FString V;
		It->ExportTextItem_Direct(V, It->ContainerPtrToValuePtr<void>(O), nullptr, nullptr, PPF_None);
		if (V.Len() > 300) V = V.Left(300) + TEXT("...");
		Out.Add(It->GetName() + TEXT("=") + V);
	}
	for (TFieldIterator<UFunction> It(O->GetClass()); It; ++It)
	{
		UFunction* F = *It;
		if (!Wanted(F->GetName()) || !(F->GetName().StartsWith(TEXT("Get")) || F->GetName().StartsWith(TEXT("Is"))) || F->NumParms != 1 || !F->GetReturnProperty()) continue;
		FProperty* Ret = F->GetReturnProperty();
		if (!CastField<FNumericProperty>(Ret) && !CastField<FBoolProperty>(Ret) && !CastField<FEnumProperty>(Ret)) continue;
		TArray<uint8> Buf;
		Buf.SetNumZeroed(FMath::Max<int32>(F->ParmsSize, 1));
		F->InitializeStruct(Buf.GetData());
		O->ProcessEvent(F, Buf.GetData());
		FString V;
		Ret->ExportTextItem_Direct(V, Ret->ContainerPtrToValuePtr<void>(Buf.GetData()), nullptr, nullptr, PPF_None);
		F->DestroyStruct(Buf.GetData());
		Out.Add(F->GetName() + TEXT("()=") + V);
	}
	return FString::Join(Out, TEXT(", "));
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
	// bone's rest so it sits where it was made and turns with the head. Each groom with a cards mesh
	// is drawn this way too, with the game's M_HairCards (round 129: grooms drew only far away,
	// bald close up, whatever their LOD). -TorqueGroomHair draws the grooms instead, to compare.
	const bool bHairMesh = !FParse::Param(FCommandLine::Get(), TEXT("TorqueGroomHair"))
		|| !FindObject<UClass>(nullptr, TEXT("/Script/HairStrandsCore.GroomComponent"));
	TArray<FPart> Statics;
	for (const FPart& P : E.Parts)
	{
		if (!P.Static.IsEmpty()) Statics.Add(P);
		else if (bHairMesh && !P.bLooseOnly && !P.Groom.IsEmpty() && !P.CardsMesh.IsEmpty())
		{
			FPart Q = P;
			Q.Static = P.CardsMesh;
			Q.Materials = P.CardsMaterials;
			Q.Bone = TEXT("head");
			Statics.Add(Q);
		}
	}
	for (const FPart& P : Statics)
	{
		if (P.Static.IsEmpty()) continue;
		UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *P.Static);
		if (!SM) { UE_LOG(LogTemp, Warning, TEXT("Torque: avatar %s: no static mesh %s"), *E.Name, *P.Static); continue; }
		USceneComponent** To = ByName.Find(P.AttachTo);
		USkinnedMeshComponent* On = To ? Cast<USkinnedMeshComponent>(*To) : nullptr;
		if (!On) On = Driver;
		const FName BoneName(*P.Bone);
		FTransform Rest;
		bool bOnBone = BoneRest(On, BoneName, Rest);
		// Hair made for another avatar's head (shared): placed by that head's rest, as it was made.
		if (bOnBone && !P.FitMesh.IsEmpty())
			if (const USkinnedAsset* Fit = LoadObject<USkinnedAsset>(nullptr, *P.FitMesh))
			{
				FTransform FitRest;
				if (BoneRestOf(Fit, BoneName, FitRest)) Rest = FitRest;
			}
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
		// The card atlas (its opacity channel: -TorqueHairMask=R|G|B|A, default G) and the hair colour.
		if (!P.CardsTexture.IsEmpty() && C->GetNumMaterials() > 0 && C->GetMaterial(0))
		{
			UTexture* Atlas = LoadObject<UTexture>(nullptr, *P.CardsTexture);
			FString MaskName(TEXT("G"));
			FParse::Value(FCommandLine::Get(), TEXT("TorqueHairMask="), MaskName);
			const FLinearColor Mask(MaskName == TEXT("R") ? 1.f : 0.f, MaskName == TEXT("G") ? 1.f : 0.f, MaskName == TEXT("B") ? 1.f : 0.f, MaskName == TEXT("A") ? 1.f : 0.f);
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(C->GetMaterial(0), C);
			if (Atlas) MID->SetTextureParameterValue(FName(TEXT("Atlas")), Atlas);
			MID->SetVectorParameterValue(FName(TEXT("Mask")), Mask);
			if (P.bHairColour) MID->SetVectorParameterValue(FName(TEXT("Colour")), P.HairColour);
			for (int32 Slot = 0; Slot < C->GetNumMaterials(); ++Slot) C->SetMaterial(Slot, MID);
			UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: hair cards atlas %s%s, mask %s, colour %s"), *E.Name, *P.CardsTexture,
				Atlas ? TEXT("") : TEXT(" (NOT FOUND)"), *MaskName, *P.HairColour.ToString());
		}
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: hair cards %s on %s%s; bounds %s r %.0f cm; material %s"), *E.Name, *P.Static, *On->GetName(),
			bOnBone ? *FString::Printf(TEXT(", bone %s"), *P.Bone) : TEXT(""), *C->Bounds.Origin.ToCompactString(), C->Bounds.SphereRadius,
			C->GetNumMaterials() > 0 && C->GetMaterial(0) ? *C->GetMaterial(0)->GetPathName() : TEXT("none"));
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
	// Cards at every distance (also in DefaultEngine.ini): close up a groom switches to strands,
	// which these MetaHumans don't have, so the hair vanished near the camera (round 125).
	static bool bCardsSet = false;
	if (!bCardsSet && GroomClass)
	{
		bCardsSet = true;
		IConsoleVariable* Cards = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.UseCardsInsteadOfStrands"));
		if (Cards) Cards->Set(1, ECVF_SetByCode);
		// Manual LOD (round 128: r.HairStrands.LODMode was 1, auto, so the forced cards LOD was
		// ignored and close up the groom picked its strands LODs, which don't draw here).
		// -TorqueHairAutoLod keeps auto, for comparison.
		if (!FParse::Param(FCommandLine::Get(), TEXT("TorqueHairAutoLod")))
			if (IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.LODMode")))
				Mode->Set(0, ECVF_SetByCode);
		// The hair settings this build runs with, for the near/far question (round 127).
		TArray<FString> HairVars;
		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
			[&HairVars](const TCHAR* VarName, IConsoleObject* Obj)
			{
				const FString N(VarName);
				if (IConsoleVariable* V = Obj ? Obj->AsVariable() : nullptr)
					if (N.Contains(TEXT("LOD")) || N.Contains(TEXT("Cards")) || N.Contains(TEXT("Cull")) || N.Contains(TEXT("Strands.Enable")) || N.Contains(TEXT("Visibility")))
						HairVars.Add(N + TEXT("=") + V->GetString());
			}), TEXT("r.HairStrands."));
		HairVars.Sort();
		UE_LOG(LogTemp, Log, TEXT("Torque: hair settings: %s"), *FString::Join(HairVars, TEXT(", ")));
		UE_LOG(LogTemp, Log, TEXT("Torque: hair as cards everywhere: %s"), Cards ? *FString::Printf(TEXT("r.HairStrands.UseCardsInsteadOfStrands = %d"), Cards->GetInt()) : TEXT("no such setting"));
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: %d parts; hair module %s, cloth module %s"), *E.Name, E.Parts.Num(),
		GroomClass ? TEXT("loaded") : TEXT("missing"), ClothClass ? TEXT("loaded") : TEXT("missing"));
	for (const FPart& P : E.Parts)
	{
		// -TorqueOwnHair: each avatar's own groom instead of the shared stopgap one.
		static const bool bOwnHair = FParse::Param(FCommandLine::Get(), TEXT("TorqueOwnHair"));
		if (P.Groom.IsEmpty() || (bOwnHair ? !P.FitMesh.IsEmpty() : P.bLooseOnly) || (bHairMesh && !P.CardsMesh.IsEmpty())) continue;
		if (FParse::Param(FCommandLine::Get(), TEXT("TorqueNoGroom"))) { UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: groom %s left off (-TorqueNoGroom)"), *E.Name, *P.Name); continue; }
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
		// Bound (the default when there's a binding): the groom is made for MetaHuman's template head,
		// and its binding fits it onto this character's own head, which can be bigger (unbound, the
		// cards sit inside the scalp: the bald avatars of rounds 104-121). -TorqueHairRigid keeps it
		// rigid on the head bone instead. -TorqueHairScale=S scales a rigid groom about the head.
		UObject* BindingObj = P.Binding.IsEmpty() ? nullptr : StaticLoadObject(UObject::StaticClass(), nullptr, *P.Binding);
		const bool bBind = BindingObj && P.FitMesh.IsEmpty() && !FParse::Param(FCommandLine::Get(), TEXT("TorqueHairRigid"));
		// Another avatar's groom (FitMesh, its face): placed by that face's head rest, so it sits on
		// this head as it does on its own.
		const USkinnedAsset* FitFace = P.FitMesh.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *P.FitMesh);
		FTransform Rest;
		bool bOnBone = !bBind && BoneRest(On, FName(TEXT("head")), Rest);   // this head has the bone
		if (bOnBone && FitFace) bOnBone = BoneRestOf(FitFace, FName(TEXT("head")), Rest);   // placed as on its own face
		double HairScale = 1.0;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueHairScale="), HairScale);
		USceneComponent* C = NewObject<USceneComponent>(Owner, GroomClass);
		C->SetupAttachment(On, bOnBone ? FName(TEXT("head")) : NAME_None);
		// Rigid: offset by the bone's rest, then scaled about the head bone.
		C->SetRelativeTransform(bOnBone ? Rest.Inverse() * FTransform(FQuat::Identity, FVector::ZeroVector, FVector(HairScale)) : FTransform::Identity);
		const bool bSet = CallWithOne(C, TEXT("SetGroomAsset"), [G](FProperty* Pr, void* V)
		{
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Pr))
				if (G->IsA(OP->PropertyClass)) OP->SetObjectPropertyValue(V, G);
		});
		bool bBound = false;
		if (bBind)
		{
			bBound = CallWithOne(C, TEXT("SetBindingAsset"), [BindingObj](FProperty* Pr, void* V)
			{
				if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Pr))
					if (BindingObj->IsA(OP->PropertyClass)) OP->SetObjectPropertyValue(V, BindingObj);
			});
			if (!bBound)
				if (FObjectPropertyBase* BP = FindFProperty<FObjectPropertyBase>(C->GetClass(), TEXT("BindingAsset")))
				{
					BP->SetObjectPropertyValue_InContainer(C, BindingObj);
					bBound = true;
				}
		}
		if (!bSet)
			if (FObjectPropertyBase* Pr = FindFProperty<FObjectPropertyBase>(C->GetClass(), TEXT("GroomAsset")))
				Pr->SetObjectPropertyValue_InContainer(C, G);
		// Cards, not strands, where the groom has them: strands are the GPU cost. -TorqueHairLod=N
		// forces another LOD for every groom (-1: the groom picks its own), to find one that draws.
		int32 LodWanted = P.Lod;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueHairLod="), LodWanted);
		if (LodWanted >= 0)
		{
			const int32 Lod = LodWanted;
			CallWithOne(C, TEXT("SetForcedLOD"), [Lod](FProperty* Pr, void* V)
			{
				if (FIntProperty* I = CastField<FIntProperty>(Pr)) I->SetPropertyValue(V, Lod);
			});
		}
		// Cards on this component too (round 128: bUseCards was False on every groom).
		if (!FParse::Param(FCommandLine::Get(), TEXT("TorqueHairAutoLod")))
		{
			if (FBoolProperty* UC = FindFProperty<FBoolProperty>(C->GetClass(), TEXT("bUseCards")))
				UC->SetPropertyValue_InContainer(C, true);
			CallWithOne(C, TEXT("SetUseCards"), [](FProperty* Pr, void* V)
			{
				if (FBoolProperty* B = CastField<FBoolProperty>(Pr)) B->SetPropertyValue(V, true);
			});
		}
		if (UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(C))
		{
			PC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PC->SetGenerateOverlapEvents(false);
		}
		// Its materials: the listed ones, but never one from the MetaHuman plugin (editor-only, so
		// not in the game: a groom with one draws nothing); those slots get the hair module's own
		// default hair material, which always draws. Set before registering, for the PSO precache.
		if (UMeshComponent* MC = Cast<UMeshComponent>(C))
		{
			ApplyMaterials(MC, P.Materials);
			static UMaterialInterface* const HairDefault = LoadObject<UMaterialInterface>(nullptr, TEXT("/HairStrands/Materials/HairDefaultMaterial.HairDefaultMaterial"));
			for (int32 Slot = 0; Slot < FMath::Max(MC->GetNumMaterials(), 1); ++Slot)
			{
				UMaterialInterface* M = MC->GetMaterial(Slot);
				if (HairDefault && (!M || M->GetPathName().StartsWith(TEXT("/MetaHuman"))))
					MC->SetMaterial(Slot, HairDefault);
			}
		}
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();   // made at run time: precache before the render state
		C->RegisterComponent();
		if (LodWanted >= 0)
		{
			const int32 LodAgain = LodWanted;   // again once registered, in case registering reset it
			CallWithOne(C, TEXT("SetForcedLOD"), [LodAgain](FProperty* Pr, void* V)
			{
				if (FIntProperty* I = CastField<FIntProperty>(Pr)) I->SetPropertyValue(V, LodAgain);
			});
		}
		UMaterialInterface* GroomMat = nullptr;
		FBoxSphereBounds GroomBounds;
		if (UPrimitiveComponent* GP = Cast<UPrimitiveComponent>(C))
		{
			GroomMat = GP->GetNumMaterials() > 0 ? GP->GetMaterial(0) : nullptr;
			GroomBounds = GP->Bounds;
		}
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: groom %s %s on %s%s, LOD %d (asset %s); bounds %s r %.0f cm (head at %s); material %s"), *E.Name, *P.Name,
			bBound ? TEXT("bound") : (bBind ? TEXT("NOT bound") : TEXT("rigid")), *On->GetName(),
			bOnBone ? TEXT(", bone head") : TEXT(""), LodWanted, bSet ? TEXT("set") : TEXT("by property"), *GroomBounds.Origin.ToCompactString(),
			GroomBounds.SphereRadius, *On->GetSocketLocation(FName(TEXT("head"))).ToCompactString(), GroomMat ? *GroomMat->GetPathName() : TEXT("none"));
		Out.Add(C);
	}
}
} // namespace TorqueAvatarMath

// Nothing to undo here: the owner calls Destroy in EndPlay, and a destructor run by the garbage
// collector mustn't destroy actors or components.
FTorqueAvatar::~FTorqueAvatar() = default;

int32 FTorqueAvatar::Count() { return TorqueAvatarMath::Entries().Num(); }

void FTorqueAvatar::SpawnLooseHair(AActor* Owner, USceneComponent* Parent, int32 Index, const FVector& At)
{
	using namespace TorqueAvatarMath;
	const TArray<FEntry>& List = Entries();
	UClass* GroomClass = FindObject<UClass>(nullptr, TEXT("/Script/HairStrandsCore.GroomComponent"));
	if (!Owner || !Parent || List.Num() == 0 || !GroomClass) return;
	const FEntry& E = List[((Index % List.Num()) + List.Num()) % List.Num()];
	static UMaterialInterface* const HairDefault = LoadObject<UMaterialInterface>(nullptr, TEXT("/HairStrands/Materials/HairDefaultMaterial.HairDefaultMaterial"));
	for (const FPart& P : E.Parts)
	{
		if (P.Groom.IsEmpty()) continue;
		UObject* G = StaticLoadObject(UObject::StaticClass(), nullptr, *P.Groom);
		if (!G) continue;
		USceneComponent* C = NewObject<USceneComponent>(Owner, GroomClass);
		C->SetupAttachment(Parent);
		C->SetRelativeLocation(At);
		CallWithOne(C, TEXT("SetGroomAsset"), [G](FProperty* Pr, void* V)
		{
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Pr))
				if (G->IsA(OP->PropertyClass)) OP->SetObjectPropertyValue(V, G);
		});
		if (UMeshComponent* MC = Cast<UMeshComponent>(C))
			for (int32 Slot = 0; Slot < FMath::Max(MC->GetNumMaterials(), 1); ++Slot)
			{
				UMaterialInterface* M = MC->GetMaterial(Slot);
				if (HairDefault && (!M || M->GetPathName().StartsWith(TEXT("/MetaHuman")))) MC->SetMaterial(Slot, HairDefault);
			}
		if (UPrimitiveComponent* PP = Cast<UPrimitiveComponent>(C)) PP->PrecachePSOs();
		C->RegisterComponent();
		const FBoxSphereBounds B = C->Bounds;
		UE_LOG(LogTemp, Log, TEXT("Torque: loose hair %s %s at %s: bounds %s r %.0f cm"), *E.Name, *P.Groom, *C->GetComponentLocation().ToCompactString(),
			*B.Origin.ToCompactString(), B.SphereRadius);
	}
}

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
	for (int32 S = 0; S < 2; ++S)
	{
		const int32 I = Ref.FindBoneIndex(FName(S == 0 ? TEXT("ball_l") : TEXT("ball_r")));
		bBall[S] = I != INDEX_NONE;
		if (!bBall[S]) continue;
		BallRest[S] = RefCS[I].GetLocation();
		Toes += BallRest[S] - (Bones[FootBone[0]].Rest + Bones[FootBone[1]].Rest) * 0.5;
	}
	Fwd = (Toes - Up * FVector::DotProduct(Toes, Up)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	Right = Bones[Thigh[1]].Rest - Bones[Thigh[0]].Rest;
	Right = (Right - Up * FVector::DotProduct(Right, Up) - Fwd * FVector::DotProduct(Right, Fwd)).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	ArmLen = (Bones[LowerArm[0]].Rest - Bones[UpperArm[0]].Rest).Size() + (Bones[HandBone[0]].Rest - Bones[LowerArm[0]].Rest).Size();
	EyeRest = Fwd * 9.5 + Up * 7.0;   // [hands] the eyes from the head joint, till the face's eye joints say (SetupFace)
	bPosed = false;
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
	Gripping += ((In.bToolInHand && !In.Grip[1].bOn ? 1.0 : 0.0) - Gripping) * FMath::Min(1.0, Dt * 8.0);
	const FTransform CT = Driver->GetComponentTransform();
	const FVector GripLocal = CT.InverseTransformPosition(In.ToolGrip);
	// [hands] The hands on the tool's handles (HandOnBar): each hand's turn, wrist and the bar, here
	// in component space; eased on as a tool's taken up, let go at once when it's put away.
	FQuat BarQ[2];
	FVector BarWrist[2], BarC[2], BarAxisC[2], BarAimC[2], BarPalm[2];
	double BarR[2] = {1.5, 1.5};
	for (int32 S = 0; S < 2; ++S)
	{
		const FTorqueHandGrip& G = In.Grip[S];
		bGripReached[S] = false;
		if (!G.bOn) { GripW[S] = 0.0; continue; }
		GripW[S] += (1.0 - GripW[S]) * FMath::Min(1.0, Dt * 8.0);
		const double Scale = FMath::Max(CT.GetScale3D().X, 0.01);
		BarC[S] = CT.InverseTransformPosition(G.At);
		BarAxisC[S] = CT.InverseTransformVectorNoScale(G.Axis).GetSafeNormal(UE_SMALL_NUMBER, Up);
		FVector Aim = CT.InverseTransformVectorNoScale(G.Aim);
		Aim = (Aim - BarAxisC[S] * FVector::DotProduct(Aim, BarAxisC[S])).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
		BarAimC[S] = Aim;
		BarR[S] = G.Radius / Scale;
		// The knuckles a little past the bar and the bones a finger's thickness behind its surface
		// (for the riding grips' 1.6 cm bar: 1.5 and 3 cm).
		HandOnBar(S, BarC[S], BarAxisC[S], Aim, FMath::Min(BarR[S], 2.0) * 0.9, BarR[S] + 1.4, BarQ[S], BarWrist[S], BarPalm[S]);
	}
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
				// [hands] Or round a handle, the wrist where the hand needs it.
				const double OnBar = In.Grip[S].bOn ? GripW[S] : 0.0;
				if (OnBar > 0.0) W = FMath::Lerp(W, BarWrist[S], OnBar);
				const double Up2 = FMath::Max3(Hands, OnTool, OnBar);
				const double Lu = (E0 - S0).Size(), Lf = (W0 - E0).Size();
				const FVector Pole = (-Fwd * (1.0 - Up2) + Right * (Sign * 0.6) - Up * (0.4 + Up2)).GetSafeNormal();
				const FVector E = MidJoint(P[I], W, Lu, Lf, Pole);
				const FVector Hinge0 = FVector::CrossProduct(W0 - S0, -Fwd);
				const FVector Hinge = FVector::CrossProduct(W - P[I], Pole);
				D[I] = Align(E0 - S0, Hinge0, E - P[I], Hinge); Set[I] = true;
				D[LowerArm[S]] = Align(W0 - E0, Hinge0, W - E, Hinge); Set[LowerArm[S]] = true;
				D[HandBone[S]] = D[LowerArm[S]]; Set[HandBone[S]] = true;
				if (OnBar > 0.0)
				{
					// [hands] The hand turned onto the handle and the fingers round it: on a trigger
					// (eased open to hook it, curled as it's pulled), the index up on a spray head.
					const FTorqueHandGrip& G = In.Grip[S];
					D[HandBone[S]] = FQuat::Slerp(D[LowerArm[S]], BarQ[S], OnBar);
					const FVector WAct = E + D[LowerArm[S]].RotateVector(W0 - E0);
					double K[5] = {0.0, 1.0, 1.0, 1.0, 1.0};
					const double Pull = FMath::Clamp(G.TriggerPull, 0.0, 1.0);
					if (G.TriggerFingers >= 4)
						for (int32 F = 1; F < 5; ++F) K[F] = FMath::Lerp(0.88, 1.0, Pull);
					else
						for (int32 F = 1; F <= G.TriggerFingers && F < 5; ++F) K[F] = FMath::Lerp(0.45, 0.8, Pull);
					if (G.bIndexTop) K[1] = 0.3 + 0.3 * Pull;   // pressing the spray head
					for (int32 F = 1; F < 5; ++F) K[F] *= OnBar;
					WrapFingers(S, D, Set, D[HandBone[S]], WAct, BarC[S], BarAxisC[S], BarAimC[S], BarPalm[S], BarR[S] + 1.0, K, 1.0 - OnBar);
					// Where the bar is now (the tool goes with the hand when the arm can't reach).
					GripAt[S] = CT.TransformPosition(BarC[S] + (WAct - BarWrist[S]));
					bGripReached[S] = true;
				}
			}
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		const FTransform T(D[I] * Bones[I].RestQ, P[I]);
		Driver->SetBoneTransformByName(Bones[I].Name, T, EBoneSpaces::ComponentSpace);
	}
	PoseFace();
	HeadAtC = P[Head];
	HeadTurnC = D[Head];
	bPosed = true;
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
	// [hands] Where the eyes are from the head joint (the face's eye joints at rest), for the
	// first-person camera; kept to the default when they're missing or look wrong.
	{
		TArray<FTransform> RestCS;
		RestCS.SetNum(Num);
		for (int32 I = 0; I < Num; ++I)
		{
			const int32 Par = Ref.GetParentIndex(I);
			RestCS[I] = Par >= 0 ? Ref.GetRefBonePose()[I] * RestCS[Par] : Ref.GetRefBonePose()[I];
		}
		const int32 L = Ref.FindBoneIndex(TEXT("FACIAL_L_Eye")), R = Ref.FindBoneIndex(TEXT("FACIAL_R_Eye"));
		if (L != INDEX_NONE && R != INDEX_NONE && Head != INDEX_NONE)
		{
			const FVector Off = (RestCS[L].GetLocation() + RestCS[R].GetLocation()) * 0.5 - Bones[Head].Rest;
			if (Off.Size() > 4.0 && Off.Size() < 20.0 && FVector::DotProduct(Off, Fwd) > 2.0) EyeRest = Off;
		}
		UE_LOG(LogTemp, Log, TEXT("Torque: avatar %s: eyes %s from the head joint%s"), *Name, *EyeRest.ToCompactString(),
			L != INDEX_NONE && R != INDEX_NONE ? TEXT(" (the face's eye joints)") : TEXT(" (no eye joints: a guess)"));
	}
	PoseFace();
}

bool FTorqueAvatar::GripReached(int32 Hand, FVector& At) const
{
	if (!Driver || Hand < 0 || Hand > 1 || !bGripReached[Hand]) return false;
	At = GripAt[Hand];
	return true;
}

bool FTorqueAvatar::EyeWorld(FVector& At) const
{
	if (!Driver || !bPosed || Head == INDEX_NONE) return false;
	At = Driver->GetComponentTransform().TransformPosition(HeadAtC + HeadTurnC.RotateVector(EyeRest));
	return true;
}

void FTorqueAvatar::SetOwnerView(bool bFirstPerson)
{
	for (const TWeakObjectPtr<USceneComponent>& P : Parts)
	{
		UPrimitiveComponent* C = Cast<UPrimitiveComponent>(P.Get());
		if (!C || (C != FaceMesh && !Hair.Contains(P))) continue;
		C->SetOwnerNoSee(bFirstPerson);
		C->SetCastHiddenShadow(true);
	}
}

void FTorqueAvatar::TouchHair()
{
	// Round 126: the hair drew only where each groom was first made (a mass floating at the line-up's
	// start, puppets "growing" hair as they walked through it). A groom placed on a moving head, not
	// bound to the skin, isn't sent its new place by the head's bones moving: send it every frame.
	// -TorqueHairRecreate rebuilds its render state every frame instead (a heavier test);
	// -TorqueHairLog logs each groom every 2 s.
	static const bool bRecreate = FParse::Param(FCommandLine::Get(), TEXT("TorqueHairRecreate"));
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("TorqueHairLog"));
	const double Now = FPlatformTime::Seconds();
	const bool bLogNow = bLog && Now - HairLogAt > 2.0;
	if (bLogNow) HairLogAt = Now;
	for (const TWeakObjectPtr<USceneComponent>& P : Hair)
	{
		USceneComponent* C = P.Get();
		if (!C || !C->IsRegistered()) continue;
		C->UpdateComponentToWorld();
		if (bRecreate) C->MarkRenderStateDirty();
		else C->MarkRenderTransformDirty();
		if (bLogNow)
		{
			const UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(C);
			UE_LOG(LogTemp, Log, TEXT("Torque: hair %s %s: visible %d, render state %d, at %s, bounds %s r %.0f, head %s, last rendered %.1f s ago"),
				*Name, *C->GetName(), C->IsVisible() ? 1 : 0, C->IsRenderStateCreated() ? 1 : 0, *C->GetComponentLocation().ToCompactString(),
				PC ? *PC->Bounds.Origin.ToCompactString() : TEXT("-"), PC ? PC->Bounds.SphereRadius : 0.0,
				FaceMesh ? *FaceMesh->GetSocketLocation(FName(TEXT("head"))).ToCompactString() : TEXT("-"),
				PC && PC->GetWorld() ? PC->GetWorld()->GetTimeSeconds() - PC->GetLastRenderTime() : -1.0);
			// What it's drawing: its LOD settings and getters, and its asset's.
			FObjectPropertyBase* AssetProp = FindFProperty<FObjectPropertyBase>(C->GetClass(), TEXT("GroomAsset"));
			UObject* GA = AssetProp ? AssetProp->GetObjectPropertyValue_InContainer(C) : nullptr;
			UE_LOG(LogTemp, Log, TEXT("Torque: hair %s %s LOD: %s; asset: %s"), *Name, *C->GetName(),
				*TorqueAvatarMath::DescribeByName(C, {TEXT("LOD"), TEXT("Lod"), TEXT("Cards"), TEXT("Geometry")}),
				*TorqueAvatarMath::DescribeByName(GA, {TEXT("LOD"), TEXT("Lod"), TEXT("Cards"), TEXT("Geometry"), TEXT("Strip")}));
		}
	}
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
	TouchHair();
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

FVector FTorqueAvatar::BoneWorld(FName BoneName) const
{
	if (!Driver || Driver->GetBoneIndex(BoneName) == INDEX_NONE) return FVector::ZeroVector;
	return Driver->GetBoneLocationByName(BoneName, EBoneSpaces::WorldSpace);
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

void FTorqueAvatar::HandRest(int32 S, FVector& Aim0, FVector& Across0, FVector& Palm0) const
{
	// The hand's own frame at rest: wrist to middle knuckle, and across the knuckles from pinky to
	// index; the palm's side is Aim x Across for the right hand, the other way for the left (mirrored).
	const double Side = S == 0 ? -1.0 : 1.0;
	const FVector W0 = Bones[HandBone[S]].Rest, E0 = Bones[LowerArm[S]].Rest;
	const int32 Knuckle = Finger[S][2][0], IndexK = Finger[S][1][0], PinkyK = Finger[S][4][0];
	Aim0 = Knuckle != INDEX_NONE ? Bones[Knuckle].Rest - W0 : W0 - E0;
	Across0 = (IndexK != INDEX_NONE && PinkyK != INDEX_NONE) ? Bones[IndexK].Rest - Bones[PinkyK].Rest : Right * Side;
	Across0 -= Aim0.GetSafeNormal() * FVector::DotProduct(Across0, Aim0.GetSafeNormal());
	Palm0 = (FVector::CrossProduct(Aim0, Across0) * Side).GetSafeNormal();
}

void FTorqueAvatar::HandOnBar(int32 S, const FVector& Bar, const FVector& Across1, const FVector& Aim1, double AimOff, double PalmOff,
	FQuat& HandQ, FVector& WristAt, FVector& Palm1) const
{
	// Turned so the knuckles point along Aim1 with the index along Across1, and placed so the bar
	// sits across the palm just short of the knuckles: AimOff short of them, PalmOff palm-side of the bones.
	FVector Aim0, Across0, Palm0;
	HandRest(S, Aim0, Across0, Palm0);
	HandQ = TorqueAvatarMath::Align(Aim0, Across0, Aim1, Across1);
	Palm1 = HandQ.RotateVector(Palm0);
	const FVector KnuckleAt = Bar + Aim1 * AimOff - Palm1 * PalmOff;
	WristAt = KnuckleAt - HandQ.RotateVector(Aim0);
}

void FTorqueAvatar::WrapFingers(int32 S, TArray<FQuat>& D, TArray<bool>& Set, const FQuat& Hq, const FVector& WAct, const FVector& Bar,
	const FVector& Axis, const FVector& Aim1, const FVector& Palm1, double GripR, const double FingerK[5], double ThumbOff) const
{
	// Joint by joint, each finger bone turns (about the bar's line, towards the palm) until its far
	// end touches the grip's surface (GripR from the bar's line), so the fingers follow the bar
	// whatever the hand's size; FingerK opens each finger (1 wrapped, 0 straight). The thumb tucks
	// round the other side to meet the fingertips (ThumbOff 1: left at rest).
	FVector Aim0, Across0, Palm0;
	HandRest(S, Aim0, Across0, Palm0);
	const FVector W0 = Bones[HandBone[S]].Rest;
	const FVector Curl = Hq.RotateVector(FVector::CrossProduct(Aim0.GetSafeNormal(), Palm0).GetSafeNormal());
	const FVector Ax = Axis.GetSafeNormal();
	auto FromBar = [&](const FVector& X) { const FVector V = X - Bar; return (V - Ax * FVector::DotProduct(V, Ax)).Size(); };
	for (int32 F = 1; F < 5; ++F)
	{
		const double K = FingerK[F];
		const int32 B0 = Finger[S][F][0];
		if (B0 == INDEX_NONE) continue;
		FQuat Par = Hq;
		FVector Pj = WAct + Hq.RotateVector(Bones[B0].Rest - W0);
		for (int32 J = 0; J < 3; ++J)
		{
			const int32 B = Finger[S][F][J];
			if (B == INDEX_NONE) break;
			const int32 Bn = J < 2 ? Finger[S][F][J + 1] : INDEX_NONE;
			const int32 Bp = J > 0 ? Finger[S][F][J - 1] : INDEX_NONE;
			const FVector Seg0 = Bn != INDEX_NONE ? Bones[Bn].Rest - Bones[B].Rest
				: (Bp != INDEX_NONE ? (Bones[B].Rest - Bones[Bp].Rest) * 0.8 : FVector::ZeroVector);
			const FVector Seg = Par.RotateVector(Seg0);   // the bone as its parent has turned it
			double Deg = J == 2 ? 70.0 : 90.0;   // as far as it bends when the bar's out of reach
			for (double T = 0.0; T <= 110.0; T += 3.0)
				if (FromBar(Pj + FQuat(Curl, FMath::DegreesToRadians(T)).RotateVector(Seg)) <= GripR) { Deg = T; break; }
			const FQuat Q(Curl, FMath::DegreesToRadians(Deg * K));
			D[B] = Q * Par; Set[B] = true;
			Pj += Q.RotateVector(Seg);
			Par = D[B];
		}
	}
	// The thumb: round the inside of the bar from the wrist side, its tip under the bar.
	const int32 T0 = Finger[S][0][0], T1 = Finger[S][0][1], T2 = Finger[S][0][2];
	if (T0 == INDEX_NONE || T2 == INDEX_NONE) return;
	const FVector P0 = WAct + Hq.RotateVector(Bones[T0].Rest - W0);
	const FVector TipRest = Bones[T2].Rest + (Bones[T2].Rest - Bones[T1 != INDEX_NONE ? T1 : T0].Rest) * 0.8;
	const FVector TipNow = Hq.RotateVector(TipRest - Bones[T0].Rest);
	FVector Goal = Bar - Aim1 * (GripR * 0.7) + Palm1 * (GripR * 0.7);
	Goal += Ax * FVector::DotProduct(P0 - Bar, Ax);   // where along the bar the thumb is
	FQuat Q = FQuat::FindBetweenVectors(TipNow, Goal - P0);
	const double Ang = Q.GetAngle();
	if (Ang > FMath::DegreesToRadians(80.0)) Q = FQuat::Slerp(FQuat::Identity, Q, FMath::DegreesToRadians(80.0) / Ang);
	Q = FQuat::Slerp(Q, FQuat::Identity, FMath::Clamp(ThumbOff, 0.0, 1.0));
	for (int32 J = 0; J < 3; ++J)
		if (Finger[S][0][J] != INDEX_NONE) { D[Finger[S][0][J]] = Q * Hq; Set[Finger[S][0][J]] = true; }
}

bool FTorqueAvatar::bOldRidePose = false;

void FTorqueAvatar::PoseRiding(const FTorqueRideTargets& Tg)
{
	using namespace TorqueAvatarMath;
	if (!Driver) return;
	const bool bFit = !bOldRidePose;   // [rider pose] the fit per bike (riderpose:old turns it off for a test's before picture)
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
	FVector Hip = ToC(Tg.Hip);
	const FVector Spine0 = Bones[Neck != INDEX_NONE ? Neck : Head].Rest - Bones[Pelvis].Rest;
	FQuat Back = FQuat::FindBetweenVectors(Spine0, ToC(Tg.Neck) - Hip);
	const FQuat LookC = ToCQ(Tg.Look);
	const double Off = FMath::Clamp(Tg.Limp, 0.0, 1.0);
	// Sat on the seat by this body's own hips, not the rider rig's pelvis (a MetaHuman's pelvis bone
	// sits higher above its seat bones than the kit rider's): the hip joints 9 cm above the seat's top,
	// right over the rig's seat point; then moved back if that leaves the crotch (9 cm ahead of the
	// hip joints) within 2 cm of the tank's back edge. The back then leans from where the hips are.
	if (Tg.bSeat && Thigh[0] != INDEX_NONE && Thigh[1] != INDEX_NONE)
	{
		const FVector HipJoints0 = (Bones[Thigh[0]].Rest + Bones[Thigh[1]].Rest) * 0.5 - Bones[Pelvis].Rest;
		const FQuat PelvisQ = FQuat::Slerp(FQuat::Identity, Back, 0.15);
		FVector Sat = ToC(Tg.Seat) + Up * 9.0 - PelvisQ.RotateVector(HipJoints0);
		const FVector Crotch = Rel.TransformPosition(Sat + PelvisQ.RotateVector(HipJoints0) + Fwd * 9.0);
		const double Over = Crotch.X - (Tg.TankRearX - 2.0);
		if (Over > 0.0) Sat -= Rel.InverseTransformVector(FVector(Over, 0.0, 0.0));
		Hip = FMath::Lerp(Sat, Hip, Off);
		Back = FQuat::FindBetweenVectors(Spine0, ToC(Tg.Neck) - Hip);
	}

	// The hands round the grips: each hand's frame from its own bones (wrist to middle knuckle, and
	// across the knuckles from pinky to index, which lies along the bar), turned so the fingers point
	// forward and down past the bar with the index inboard, and placed so the bar sits across the
	// palm just short of the knuckles. Off the bike (Limp), the rider rig's wrist.
	FQuat HandQ[2];
	FVector Aim0[2], Palm0[2], WristAt[2], BarAt[2], BarAxis[2], AimOn[2], PalmOn[2];
	// [gestures] Each hand's own let-go (the wave's), on top of Limp; the back is still fitted to the
	// wrists on the bars (WristFit), so a wave doesn't sit the rider up.
	double HandOffS[2];
	FVector WristFit[2];
	for (int32 S = 0; S < 2; ++S)
	{
		const double Side = S == 0 ? -1.0 : 1.0;   // the left side is -Right
		FVector Across0;
		HandRest(S, Aim0[S], Across0, Palm0[S]);
		const FVector Bar = ToC(Tg.Grip[S]);
		const FVector Across1 = -Right * Side;   // the index towards the bike's middle
		FVector Aim1 = Bar - ToC(Tg.Wrist[S]);
		Aim1 -= Across1 * FVector::DotProduct(Aim1, Across1);
		Aim1 = (Aim1.Size() > 2.0 ? Aim1.GetSafeNormal() : FVector::ZeroVector) + (Fwd * 0.8 - Up * 0.6) * 0.5;
		Aim1 = (Aim1 - Across1 * FVector::DotProduct(Aim1, Across1)).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
		FVector Palm1, GripWrist;
		HandOnBar(S, Bar, Across1, Aim1, 1.5, 3.0, HandQ[S], GripWrist, Palm1);   // bar 1.5 cm short of the knuckles, 3 cm palm-side of the bones
		HandOffS[S] = FMath::Max(Off, FMath::Clamp(Tg.HandOff[S], 0.0, 1.0));
		WristAt[S] = FMath::Lerp(GripWrist, ToC(Tg.Wrist[S]), HandOffS[S]);
		WristFit[S] = Tg.HandOff[S] > 0.0 ? FMath::Lerp(GripWrist, ToC(Tg.Wrist[S]), Off) : WristAt[S];
		BarAt[S] = Bar;
		BarAxis[S] = Across1.GetSafeNormal();
		AimOn[S] = Aim1;
		PalmOn[S] = Palm1;
	}
	const FQuat BackRig = Back;   // the head keeps the rider rig's gaze, however far the back leans
	const FVector Pitch = FVector::CrossProduct(Right, Up).GetSafeNormal();   // turning about Right takes Up towards this
	const double PitchSign = FVector::DotProduct(Pitch, Fwd) >= 0.0 ? 1.0 : -1.0;
	double ArmReach[2];
	for (int32 S = 0; S < 2; ++S)
		ArmReach[S] = (Bones[LowerArm[S]].Rest - Bones[UpperArm[S]].Rest).Size() + (Bones[HandBone[S]].Rest - Bones[LowerArm[S]].Rest).Size();
	if (!bFit)
	{
		// (Before the fit per bike.) Low, far bars (clip-ons): lean the back further forward, from the
		// hips, until the shoulders are near enough for both hands to reach (at most 35 degrees more).
		// Shoulders estimated as turned with the whole back.
		const FVector Pelvis0 = Bones[Pelvis].Rest;
		for (int32 Step = 0; Step < 12; ++Step)
		{
			double Short = 0.0;
			for (int32 S = 0; S < 2; ++S)
			{
				const FVector Sh = Hip + Back.RotateVector(Bones[UpperArm[S]].Rest - Pelvis0);
				Short = FMath::Max(Short, (WristFit[S] - Sh).Size() - ArmReach[S] * 0.97);
			}
			if (Short <= 0.0 || Off > 0.5) break;
			Back = FQuat(Right, PitchSign * FMath::DegreesToRadians(3.0)) * Back;
		}
	}
	else if (Off <= 0.5)
	{
		// [rider pose] The back leant about the hips, as little as it takes from the rider rig's lean
		// (the riding style's, braking and accelerating), until both hands reach their grips with the
		// elbows a little bent (the wrist at most 0.88 of the arm away from the shoulder, 0.95 on a
		// cruiser's or chopper's bars, which the arms reach out to) and not cramped (at least 0.6, or it
		// sits up). The shoulders are found as the spine's shares carry them (the pelvis 0.15 of the
		// turn, then 0.4, 0.75 and the top of the spine all of it), which lean less than the turn.
		const bool bChain = Spine[0] != INDEX_NONE && Spine[1] != INDEX_NONE && Spine[2] != INDEX_NONE;
		auto ShoulderOf = [&](const FQuat& Bk, int32 Sd) {
			if (!bChain) return Hip + Bk.RotateVector(Bones[UpperArm[Sd]].Rest - Bones[Pelvis].Rest);
			FVector At = Hip + FQuat::Slerp(FQuat::Identity, Bk, 0.15).RotateVector(Bones[Spine[0]].Rest - Bones[Pelvis].Rest);
			At += FQuat::Slerp(FQuat::Identity, Bk, 0.4).RotateVector(Bones[Spine[1]].Rest - Bones[Spine[0]].Rest);
			At += FQuat::Slerp(FQuat::Identity, Bk, 0.75).RotateVector(Bones[Spine[2]].Rest - Bones[Spine[1]].Rest);
			return At + Bk.RotateVector(Bones[UpperArm[Sd]].Rest - Bones[Spine[2]].Rest);
		};
		// With the style's own lean (beenster's marked poses, which sit up with the arms near straight)
		// the back goes as near it as the hands allow, the wrists up to 0.97 of the arm away.
		const bool bStyleLean = Tg.bOnLayout && Tg.BackDeg > -90.0;
		// The back's lean as it's drawn (hips to neck, the spine's shares carrying the neck).
		auto DrawnLean = [&](const FQuat& Bk) {
			FVector At = Hip + Bk.RotateVector(Spine0);
			if (bChain && Neck != INDEX_NONE)
			{
				At = Hip + FQuat::Slerp(FQuat::Identity, Bk, 0.15).RotateVector(Bones[Spine[0]].Rest - Bones[Pelvis].Rest);
				At += FQuat::Slerp(FQuat::Identity, Bk, 0.4).RotateVector(Bones[Spine[1]].Rest - Bones[Spine[0]].Rest);
				At += FQuat::Slerp(FQuat::Identity, Bk, 0.75).RotateVector(Bones[Spine[2]].Rest - Bones[Spine[1]].Rest);
				At += Bk.RotateVector(Bones[Neck].Rest - Bones[Spine[2]].Rest);
			}
			const FVector V = At - Hip;
			return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(V, Fwd), FVector::DotProduct(V, Up)));
		};
		const double MaxR = bStyleLean ? 0.97 : (Tg.bLaidBack ? 0.95 : 0.88), MinR = 0.6;
		FQuat Best = Back;
		double BestCost = TNumericLimits<double>::Max();
		for (int32 K = 0; K <= 75 && (BestCost > 0.0 || bStyleLean); ++K)
			for (int32 Way = 0; Way < (K == 0 ? 1 : 2) && (BestCost > 0.0 || bStyleLean); ++Way)
			{
				const double TurnDeg = (Way == 0 ? 1.0 : -1.0) * K;
				const FQuat Q = FQuat(Right, PitchSign * FMath::DegreesToRadians(TurnDeg)) * Back;
				// No further than 65 degrees forward of upright, nor 20 back (hips to neck).
				const FVector Spine1 = Q.RotateVector(Spine0);
				const double LeanDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Spine1, Fwd), FVector::DotProduct(Spine1, Up)));
				if (LeanDeg > 65.0 || LeanDeg < -20.0) continue;
				double Far = 0.0, Near = 10.0;
				for (int32 S = 0; S < 2; ++S)
				{
					const double Ratio = (WristFit[S] - ShoulderOf(Q, S)).Size() / FMath::Max(ArmReach[S], 1.0);
					Far = FMath::Max(Far, Ratio);
					Near = FMath::Min(Near, Ratio);
				}
				const double Miss = (Far <= MaxR && Near >= MinR) ? 0.0 : FMath::Max(Far - MaxR, 0.0) * 10.0 + FMath::Max(MinR - Near, 0.0);
				const double Cost = bStyleLean ? Miss * 100.0 + FMath::Abs(DrawnLean(Q) - Tg.BackDeg) * 0.01 : Miss + (Miss > 0.0 ? K * 0.001 : 0.0);
				if (Cost < BestCost) { BestCost = Cost; Best = Q; }
			}
		Back = Best;
		// [rider pose] Flat wrists (beenster): each hand turned round its grip until the knuckles
		// carry on the forearm's line, the back of the hand in line with the forearm, and then rolled
		// on round it as far as the rider rig's hand rolls the throttle. A few passes, since where
		// the wrist sits by the bar moves with the hand's turn.
		double RollRad[2] = {0.0, 0.0};
		for (int32 S = 0; S < 2; ++S)
		{
			FVector Was = BarAt[S] - ToC(Tg.WristUnrolled[S]), Now = BarAt[S] - ToC(Tg.Wrist[S]);
			Was -= BarAxis[S] * FVector::DotProduct(Was, BarAxis[S]);
			Now -= BarAxis[S] * FVector::DotProduct(Now, BarAxis[S]);
			if (HandOffS[S] < 0.01 && !Tg.WristUnrolled[S].IsZero() && Was.Size() > 1.0 && Now.Size() > 1.0)
				RollRad[S] = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(Was, Now), BarAxis[S]), FVector::DotProduct(Was, Now));
		}
		for (int32 Pass = 0; Pass < 3; ++Pass)
			for (int32 S = 0; S < 2; ++S)
			{
				const double Side = S == 0 ? -1.0 : 1.0;
				const double Lu = (Bones[LowerArm[S]].Rest - Bones[UpperArm[S]].Rest).Size();
				const double Lf = (Bones[HandBone[S]].Rest - Bones[LowerArm[S]].Rest).Size();
				const FVector Pole = (-Fwd * 0.25 + Right * (Side * 0.6) - Up * 0.75).GetSafeNormal();
				const FVector Elbow = MidJoint(ShoulderOf(Back, S), WristFit[S], Lu, Lf, Pole);
				FVector Along = WristFit[S] - Elbow;
				Along -= BarAxis[S] * FVector::DotProduct(Along, BarAxis[S]);
				if (Along.Size() < 1.0) continue;
				Along = FQuat(BarAxis[S].GetSafeNormal(), RollRad[S]).RotateVector(Along.GetSafeNormal());
				FVector Palm1, GripWrist;
				HandOnBar(S, BarAt[S], BarAxis[S], Along, 1.5, 3.0, HandQ[S], GripWrist, Palm1);
				WristAt[S] = FMath::Lerp(GripWrist, ToC(Tg.Wrist[S]), HandOffS[S]);
				WristFit[S] = Tg.HandOff[S] > 0.0 ? FMath::Lerp(GripWrist, ToC(Tg.Wrist[S]), Off) : WristAt[S];
				AimOn[S] = Along;
				PalmOn[S] = Palm1;
			}
	}

	// [rider pose] The tank's half-width at x along it (parent's space; 0 off its ends).
	auto TankHalfAt = [&Tg](double X) {
		const TArray<FVector2D>& W = Tg.TankHalfW;
		if (W.Num() < 2 || X > W[0].X || X < W.Last().X) return 0.0;
		for (int32 J = 1; J < W.Num(); ++J)
			if (X >= W[J].X)
			{
				const double Span = W[J - 1].X - W[J].X;
				return Span > UE_SMALL_NUMBER ? FMath::Lerp(W[J].Y, W[J - 1].Y, (X - W[J].X) / Span) : W[J].Y;
			}
		return 0.0;
	};
	FVector KneeGoal[2] = {FVector::ZeroVector, FVector::ZeroVector}, AnkleAim[2] = {FVector::ZeroVector, FVector::ZeroVector};
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
				// The arm reaches the wrist the hand needs on the bar (as far as it can); the elbows
				// bend down and out.
				const FVector S0 = Bones[I].Rest, E0 = Bones[LowerArm[S]].Rest, W0 = Bones[HandBone[S]].Rest;
				const FVector W = WristAt[S];
				const double Lu = (E0 - S0).Size(), Lf = (W0 - E0).Size();
				const FVector Pole = bFit ? (-Fwd * 0.25 + Right * (Side * 0.6) - Up * 0.75).GetSafeNormal()
					: (-Fwd * 0.3 + Right * (Side * 0.7) - Up * 0.6).GetSafeNormal();
				const FVector E = MidJoint(P[I], W, Lu, Lf, Pole);
				const FVector Hinge0 = FVector::CrossProduct(W0 - S0, -Fwd);
				const FVector Hinge = FVector::CrossProduct(W - P[I], Pole);
				D[I] = Align(E0 - S0, Hinge0, E - P[I], Hinge); Set[I] = true;
				D[LowerArm[S]] = Align(W0 - E0, Hinge0, W - E, Hinge); Set[LowerArm[S]] = true;
				D[HandBone[S]] = FQuat::Slerp(HandQ[S], D[LowerArm[S]], HandOffS[S]); Set[HandBone[S]] = true;
				// The fingers wrapped round the bar (WrapFingers). The index and middle lift onto a
				// lever when they're on it.
				const FVector WAct = E + D[LowerArm[S]].RotateVector(W0 - E0);   // where the wrist got to
				const double Loose = 1.0 - HandOffS[S] * 0.7;
				double K[5] = {0.0, Loose, Loose, Loose, Loose};
				const double On = FMath::Clamp(Tg.LeverFingers[S], 0.0, 1.0), Pull = FMath::Clamp(Tg.LeverPull[S], 0.0, 1.0);
				for (int32 F = 1; F <= 2; ++F) K[F] *= FMath::Lerp(1.0, 0.35 + 0.45 * Pull, On);
				WrapFingers(S, D, Set, D[HandBone[S]], WAct, BarAt[S], BarAxis[S], AimOn[S], PalmOn[S], 2.6, K, HandOffS[S]);   // the grip's radius plus half a finger
			}
			else if (I == Thigh[S])
			{
				const FVector H0 = Bones[I].Rest, K0 = Bones[Calf[S]].Rest, A0 = Bones[FootBone[S]].Rest;
				const double Lt = (K0 - H0).Size(), Ls = (A0 - K0).Size();
				const FVector Hinge0 = FVector::CrossProduct(A0 - H0, Fwd);
				FVector A = ToC(Tg.Ankle[S]);
				FQuat FootQ = FQuat::Identity;
				FVector K, Hinge;
				if (!bFit)
				{
					// (Before the fit per bike.) Ankles where the rider rig's are; knees forward and a little out.
					const FVector Pole = (Fwd + Right * (Side * 0.25) + Up * 0.2).GetSafeNormal();
					K = MidJoint(P[I], A, Lt, Ls, Pole);
					Hinge = FVector::CrossProduct(A - P[I], Pole);
				}
				else
				{
					// [rider pose] On a catalogue bike, the ball of the foot on the peg (the rider rig's
					// ankle is its kit rider's, whose feet aren't this body's): toes down on rearsets behind
					// the hips, level under them, up a little on forward controls, and turned out a little.
					// Down at a stop, the rig's ankle (on the road) and the foot level.
					const double Down = FMath::Clamp(Tg.FeetDown, 0.0, 1.0);
					if (Tg.bOnLayout && bBall[S] && Down < 1.0)
					{
						// The foot's pitch from the shin (beenster's marked poses): the sole about 86 degrees
						// from the shin, toes down on rearsets and level under a near-upright shin, and
						// 108 degrees on forward controls, the toes pointing on ahead. The shin moves with
						// the pitch (the ball stays on the peg), so a few passes.
						const double Yr = FMath::DegreesToRadians(8.0);
						const FQuat ToesOut = FQuat::FindBetweenVectors(Fwd, Fwd * FMath::Cos(Yr) + Right * (Side * FMath::Sin(Yr)));
						// The ball joint's height over the sole at rest, the boot's sole on the peg's top.
						const double Sole = FMath::Clamp(FVector::DotProduct(BallRest[S], Up), 0.5, 5.0);
						const FVector BallAt = ToC(Tg.Peg[S] + FVector(0.0, Side * 1.5, 1.2)) + Up * Sole;
						const double SoleToShin = Tg.bLaidBack ? 108.0 : 86.0;
						double PitchDeg = 0.0;
						FQuat OnPeg = ToesOut;
						FVector AnkleOnPeg = BallAt - OnPeg.RotateVector(BallRest[S] - A0);
						for (int32 Pass = 0; Pass < 4; ++Pass)
						{
							FVector Reach = AnkleOnPeg - P[I];
							const double Span = FMath::Clamp(Reach.Size(), FMath::Abs(Lt - Ls) + 0.1, (Lt + Ls) * 0.999);
							Reach = Reach.GetSafeNormal(UE_SMALL_NUMBER, -Up);
							const double Mid = (Lt * Lt - Ls * Ls + Span * Span) / (2.0 * Span);
							const FVector Bend = (Fwd - Reach * FVector::DotProduct(Fwd, Reach)).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
							const FVector KneeSide = P[I] + Reach * Mid + Bend * FMath::Sqrt(FMath::Max(Lt * Lt - Mid * Mid, 0.0));
							const FVector ShinUp = KneeSide - AnkleOnPeg;
							const double ShinDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(ShinUp, Fwd), FVector::DotProduct(ShinUp, Up)));
							PitchDeg = FMath::Clamp(ShinDeg + SoleToShin - 90.0, -45.0, 50.0);
							const double Pr = FMath::DegreesToRadians(PitchDeg);
							OnPeg = ToesOut * FQuat::FindBetweenVectors(Fwd, Fwd * FMath::Cos(Pr) - Up * FMath::Sin(Pr));
							AnkleOnPeg = BallAt - OnPeg.RotateVector(BallRest[S] - A0);
						}
						A = FMath::Lerp(AnkleOnPeg, A, Down);
						FootQ = FQuat::Slerp(OnPeg, FQuat::Identity, Down);
					}
					// The knee on the circle the thigh and shin allow, as near as it comes to a nearly
					// level thigh (12 degrees down from the hip joint, as people sit a bike: beenster), as
					// far out as the bike's knee point (the tank's knee pads; without one, 6 cm out from
					// the hip), and outside the tank's flank.
					{
						const double Dn = FMath::DegreesToRadians(12.0);
						FVector Goal = Rel.TransformPosition(P[I] + Fwd * (Lt * FMath::Cos(Dn)) - Up * (Lt * FMath::Sin(Dn)));
						if (Tg.bOnLayout) Goal.Y = Tg.Knee[S].Y;
						else Goal = Rel.TransformPosition(ToC(Goal) + Right * (Side * 6.0));
						KneeGoal[S] = ToC(Goal);
					}
					FVector Dir = A - P[I];
					const double Len = FMath::Clamp(Dir.Size(), FMath::Abs(Lt - Ls) + 0.1, (Lt + Ls) * 0.999);
					Dir = Dir.GetSafeNormal(UE_SMALL_NUMBER, -Up);
					const double Along = (Lt * Lt - Ls * Ls + Len * Len) / (2.0 * Len);
					const double Out = FMath::Sqrt(FMath::Max(Lt * Lt - Along * Along, 0.0));
					const FVector Centre = P[I] + Dir * Along;
					const FVector E1 = FVector::CrossProduct(Dir, Right).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
					const FVector E2 = FVector::CrossProduct(Dir, E1);
					const FVector MidLine = Rel.TransformPosition(P[Pelvis]);
					double BestKnee = TNumericLimits<double>::Max();
					K = Centre + E1 * Out;
					for (int32 J = 0; J < 72; ++J)
					{
						const double Ang = J * (2.0 * PI / 72.0);
						const FVector Kj = Centre + (E1 * FMath::Cos(Ang) + E2 * FMath::Sin(Ang)) * Out;
						const FVector Kp = Rel.TransformPosition(Kj);
						if ((Kp.Y - MidLine.Y) * Side < 0.0) continue;   // never across the bike's middle
						const double Half = TankHalfAt(Kp.X);
						const double Inside = Half > 0.0 ? FMath::Max(Half + 5.0 - FMath::Abs(Kp.Y), 0.0) : 0.0;   // a knee's half-thickness off the flank
						const double Cost = (Kj - KneeGoal[S]).SizeSquared() + 40.0 * Inside * Inside;
						if (Cost < BestKnee) { BestKnee = Cost; K = Kj; }
					}
					Hinge = FVector::CrossProduct(A - P[I], K - Centre);
				}
				AnkleAim[S] = A;
				D[I] = Align(K0 - H0, Hinge0, K - P[I], Hinge); Set[I] = true;
				D[Calf[S]] = Align(A0 - K0, Hinge0, A - K, Hinge); Set[Calf[S]] = true;
				D[FootBone[S]] = FootQ; Set[FootBone[S]] = true;   // level, or on the peg
			}
		}
	}
	for (int32 I = 0; I < N; ++I)
		Driver->SetBoneTransformByName(Bones[I].Name, FTransform(D[I] * Bones[I].RestQ, P[I]), EBoneSpaces::ComponentSpace);
	PoseFace();
	RideHead = Rel.TransformPosition(P[Head]);
	RideHeadTurn = RelQ * D[Head] * RelQ.Inverse();
	bRode = true;

	// What it came to, for the test log.
	{
		auto AngleAt = [&P](int32 A, int32 B, int32 C) {
			const FVector U = (P[A] - P[B]).GetSafeNormal(), V = (P[C] - P[B]).GetSafeNormal();
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(U, V), -1.0, 1.0)));
		};
		const double Scale = Rel.GetScale3D().Z;
		const FVector Spine1 = Neck != INDEX_NONE ? P[Neck] - P[Pelvis] : Spine0;
		RideReport.BackDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Spine1, Fwd), FVector::DotProduct(Spine1, Up)));
		for (int32 S = 0; S < 2; ++S)
		{
			RideReport.KneeDeg[S] = AngleAt(Thigh[S], Calf[S], FootBone[S]);
			RideReport.ElbowDeg[S] = AngleAt(UpperArm[S], LowerArm[S], HandBone[S]);
			RideReport.HandShort[S] = (P[HandBone[S]] - WristAt[S]).Size() * Scale;
			RideReport.AnkleShort[S] = (P[FootBone[S]] - AnkleAim[S]).Size() * Scale;
			RideReport.KneeOut[S] = FMath::Abs(Rel.TransformPosition(P[Calf[S]]).Y);
		}
	}
}
