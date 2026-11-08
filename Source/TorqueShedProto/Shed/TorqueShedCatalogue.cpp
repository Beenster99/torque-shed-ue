#include "Shed/TorqueShedCatalogue.h"

#include "Fit/BikeLayout.h"
#include "Fit/BikeParts.h"
#include "Fit/FitJson.h"
#include "Fit/PartMesh.h"
#include "Fit/PartsDb.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"

namespace TorqueShedCatalogueImpl
{
std::string Std(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

FString Ue(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

bool ReadBytes(const FString& Rel, std::string& Out)
{
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *FPaths::Combine(FPaths::ProjectDir(), Rel), FILEREAD_Silent)) return false;
	Out.assign(reinterpret_cast<const char*>(B.GetData()), B.Num());
	return true;
}

// Data/Parts/Blender/index.json: which glbs there are, their shape keys, and each surface tag's finish.
const TorqueFit::Json* Index()
{
	static TorqueFit::Json J;
	static bool bTried = false, bOk = false;
	if (!bTried)
	{
		bTried = true;
		std::string Text, Error;
		bOk = ReadBytes(TEXT("Data/Parts/Blender/index.json"), Text) && TorqueFit::Json::Parse(Text, J, &Error) && J.Get("parts");
		if (!bOk)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: Data/Parts/Blender/index.json didn't load (%s): catalogue bikes can't be drawn"), *Ue(Error));
		}
	}
	return bOk ? &J : nullptr;
}

// A glb with its shape keys, loaded once.
bool LoadPart(const std::string& Glb, TorqueFit::PartMesh& Out)
{
	static TMap<FString, TSharedPtr<TorqueFit::PartMesh>> Cache;
	const FString Key = Ue(Glb);
	if (const TSharedPtr<TorqueFit::PartMesh>* Found = Cache.Find(Key))
	{
		if (!Found->IsValid()) return false;
		Out = **Found;
		return true;
	}
	TSharedPtr<TorqueFit::PartMesh> M = MakeShared<TorqueFit::PartMesh>();
	const TorqueFit::Json* Idx = Index();
	const TorqueFit::Json* Parts = Idx ? Idx->Get("parts") : nullptr;
	const TorqueFit::Json* Entry = Parts && Glb.size() > 4 ? Parts->Get(Glb.substr(0, Glb.size() - 4)) : nullptr;
	std::string Bytes, MorphBytes, Error;
	bool bOk = Entry && ReadBytes(TEXT("Data/Parts/Blender/") + Key, Bytes) && M->LoadGlb(Bytes, Error);
	if (bOk && Entry->Get("morph"))
		bOk = ReadBytes(TEXT("Data/Parts/Blender/") + Ue(Entry->Str("morph")), MorphBytes) &&
			M->LoadMorph(MorphBytes, Entry->Get("shape_keys") ? Entry->Get("shape_keys")->Dump(0) : std::string("[]"), Error);
	if (!bOk)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: Blender part %s didn't load (%s)"), *Key, *Ue(Error));
		M.Reset();
	}
	Cache.Add(Key, M);
	if (!M.IsValid()) return false;
	Out = *M;
	return true;
}

// The bike's material for a surface tag ("chrome", "paint__engine"): index.json's tag_finish, then
// /Game/Bike/Materials/MI_<finish> (the level script's), paint as the Speed Twin's silver.
UMaterialInterface* MaterialFor(const FString& Tag, bool& bPaint)
{
	const TorqueFit::Json* Idx = Index();
	const TorqueFit::Json* Finishes = Idx ? Idx->Get("tag_finish") : nullptr;
	std::string Finish = Finishes ? Finishes->Str(Std(Tag)) : std::string();
	if (Finish.empty()) Finish = "steel_brushed";
	bPaint = Finish == "paint" || Finish == "paint_2";
	if (bPaint) Finish = "paint_silver";
	static TMap<FString, UMaterialInterface*> Loaded;
	const FString Name = Ue(Finish);
	if (UMaterialInterface** Found = Loaded.Find(Name)) return *Found;
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Bike/Materials/MI_%s.MI_%s"), *Name, *Name));
	if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bike/Materials/MI_steel_brushed.MI_steel_brushed"));
	Loaded.Add(Name, M);
	return M;
}

// Ours (metres: x forward, y up, z right) to the bike's cm (x forward, y right, z up).
FVector3f ToUe(const TorqueSim::V3& P) { return FVector3f((float)(P.x * 100.0), (float)(P.z * 100.0), (float)(P.y * 100.0)); }

// One surface as a static mesh (its triangles turned for Unreal's left-handed frame).
UStaticMesh* BuildMesh(AActor* Owner, const TorqueFit::PartSurface& S, UMaterialInterface* Material, TArray<FVector>& OutPos, TArray<int32>& OutTris)
{
	const int32 NumVerts = (int32)S.Pos.size();
	if (NumVerts == 0 || S.Idx.size() < 3) return nullptr;
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(2);
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	MD.ReserveNewVertices(NumVerts);
	MD.ReserveNewVertexInstances(NumVerts);
	MD.ReserveNewTriangles((int32)S.Idx.size() / 3);
	MD.ReserveNewPolygonGroups(1);
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	const FName SlotName(TEXT("Surface"));
	SlotNames[Group] = SlotName;
	TArray<FVertexInstanceID> Instances;
	Instances.Reserve(NumVerts);
	OutPos.Reset();
	OutTris.Reset();
	for (int32 I = 0; I < NumVerts; ++I)
	{
		const FVector3f P = ToUe(S.Pos[I]);
		const FVertexID V = MD.CreateVertex();
		Positions[V] = P;
		OutPos.Add(FVector(P));
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		FVector3f N = I < (int32)S.Nrm.size() ? FVector3f((float)S.Nrm[I].x, (float)S.Nrm[I].z, (float)S.Nrm[I].y) : FVector3f::UpVector;
		N = N.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Normals[VI] = N;
		// Any tangent across the normal (the parts' finishes have no tangent-space detail to line up).
		const FVector3f Across = FMath::Abs(N.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		Tangents[VI] = FVector3f::CrossProduct(Across, N).GetSafeNormal();
		Signs[VI] = 1.0f;
		const float U = I < (int32)S.U.size() ? (float)S.U[I] : 0.0f;
		const float Vv = I < (int32)S.V.size() ? (float)S.V[I] : 0.0f;
		UVs.Set(VI, 0, FVector2f(U, Vv));
		UVs.Set(VI, 1, FVector2f(0.0f, 0.0f));
		Instances.Add(VI);
	}
	for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
	{
		const int32 A = (int32)S.Idx[T], B = (int32)S.Idx[T + 1], C = (int32)S.Idx[T + 2];
		if (A >= NumVerts || B >= NumVerts || C >= NumVerts || A == B || B == C || A == C) continue;
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Instances[A]);
		Tri.Add(Instances[C]);
		Tri.Add(Instances[B]);
		MD.CreateTriangle(Group, Tri);
		OutTris.Add(A);
		OutTris.Add(C);
		OutTris.Add(B);
	}
	if (OutTris.Num() == 0) return nullptr;
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Owner);
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName));   // the imported-slot-name argument only exists in editor builds
#endif
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	return Mesh;
}
} // namespace TorqueShedCatalogueImpl

namespace TSCI = TorqueShedCatalogueImpl;

namespace TorqueShedCatalogue
{
const TCHAR* const NoPaintTag = TEXT("NoPaint");

const TorqueFit::BikeLayout* Layout()
{
	static TorqueFit::PartsDb Db;
	static TorqueFit::BikeLayout Lay;
	static bool bTried = false, bOk = false;
	if (!bTried)
	{
		bTried = true;
		std::string PartsText, LayoutText, Error;
		bOk = TSCI::ReadBytes(TEXT("Data/Parts/parts.json"), PartsText) && TSCI::ReadBytes(TEXT("Data/Fitment/layout.json"), LayoutText) &&
			Db.LoadFromText(PartsText, Error) && Lay.Load(LayoutText, Db, Error);
		if (!bOk)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: the bike layout didn't load (%s): every part bolts on, catalogue bikes aren't drawn"), *TSCI::Ue(Error));
		}
	}
	return bOk ? &Lay : nullptr;
}

int32 MakeBike(AActor* Owner, USceneComponent* Parent, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out, double& OutAxleHeight)
{
	OutAxleHeight = 31.0;
	const TorqueFit::BikeLayout* Lay = Layout();
	const TorqueFit::Json* Idx = TSCI::Index();
	if (!Lay || !Idx || !Owner || !Parent) return 0;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TSCI::Std(KV.Key)] = TSCI::Std(KV.Value);
	const TorqueFit::Layout L = Lay->Solve(Map);
	OutAxleHeight = L.Stats.RearRadius * 100.0;
	TorqueFit::BikeParts Placer(*Lay, *Idx->Get("parts"));
	Placer.SetMeshSource([](const std::string& Glb, TorqueFit::PartMesh& M) { return TSCI::LoadPart(Glb, M); });
	int32 Made = 0;
	for (const TorqueFit::FittedPart& P : Placer.Fit(Map, L))
	{
		if (P.Hidden) continue;   // inside a full fairing's shell (the plain headlight), as Godot hides it
		TorqueFit::PartMesh Glb;
		if (!TSCI::LoadPart(P.Glb, Glb)) continue;
		const TorqueFit::PartMesh Drawn = TorqueFit::BikeParts::Draw(Glb, P);
		for (const TorqueFit::PartSurface& S : Drawn.Surfaces)
		{
			// A "tag__slot" surface belongs to that slot (a head glb's barrels are the engine's).
			const FString Material = TSCI::Ue(S.Material);
			FString Tag = Material, Slot = TSCI::Ue(P.Slot);
			const int32 At = Material.Find(TEXT("__"));
			if (At != INDEX_NONE)
			{
				Tag = Material.Left(At);
				Slot = Material.Mid(At + 2);
			}
			bool bPaint = false;
			UMaterialInterface* Mat = TSCI::MaterialFor(Tag, bPaint);
			TArray<FVector> Pos;
			TArray<int32> Tris;
			UStaticMesh* Mesh = TSCI::BuildMesh(Owner, S, Mat, Pos, Tris);
			if (!Mesh) continue;
			TorqueShedPick::AddShape(Mesh, Pos, Tris);
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
			C->SetStaticMesh(Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->SetupAttachment(Parent);
			C->SetRelativeTransform(FTransform::Identity);
			if (Mat) C->SetMaterial(0, Mat);
			C->ComponentTags.Add(FName(*(TEXT("Slot:") + Slot)));
			C->ComponentTags.Add(FName(*(TEXT("Part:") + TSCI::Ue(P.Glb))));
			if (!bPaint) C->ComponentTags.Add(FName(NoPaintTag));
			C->RegisterComponent();
			// Paint takes the catalogue part's colour until it's sprayed (ApplyLooks).
			if (bPaint)
			{
				const FString PartId = Parts.FindRef(Slot);
				if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0))
				{
					const FLinearColor Colour = TorqueShedParts::DefaultColour(FName(*PartId));
					M->SetVectorParameterValue(TEXT("Color"), Colour);
					M->SetVectorParameterValue(TEXT("Color2"), Colour);
				}
			}
			Out.Add(C);
			++Made;
		}
	}
	return Made;
}
}
