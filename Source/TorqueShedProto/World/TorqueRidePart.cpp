#include "World/TorqueRidePart.h"
#include "World/WorldRide.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "LocalVertexFactory.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

namespace TRPart
{
// (off since build 149: build 148 measured no render gain at Whaley Bridge, render 4.9 ms against 4.6
// without, while building the meshes cost the game thread in towns)
TAutoConsoleVariable<int32> CVarPrecache(TEXT("torque.RidePartPrecache"), 1,
	TEXT("Ride UK: the world's pieces ask for their shaders (PSOs) when they are put in the scene (1, build 154) or not (0, as until 153)."));
TAutoConsoleVariable<int32> CVarStaticParts(TEXT("torque.RideStaticParts"), 0,
	TEXT("Ride UK: draw the world's pieces as static meshes built at run time (1, cached draws) or as procedural meshes (0, the old way). Pieces made after the change follow it."));
} // namespace TRPart

void URidePart::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// (the whole actor going takes the look with it anyway)
	if (!bDestroyingHierarchy && IsValid(Look) && !Look->IsBeingDestroyed()) Look->DestroyComponent();
	Look = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void URidePart::CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FMaterialInterfacePSOPrecacheParamsList& OutParams)
{
	Super::CollectPSOPrecacheData(BasePrecachePSOParams, OutParams);
	if (TRPart::CVarPrecache.GetValueOnGameThread() == 0) return;
	// (the procedural mesh draws its sections with the local vertex factory, one material each)
	const FPSOPrecacheVertexFactoryData LocalVF(&FLocalVertexFactory::StaticType);
	TSet<UMaterialInterface*> SeenMats;
	for (const FMaterialInterfacePSOPrecacheParams& Have : OutParams) SeenMats.Add(Have.MaterialInterface);
	const int32 Sections = GetNumSections();
	for (int32 Section = 0; Section < Sections; ++Section)
	{
		UMaterialInterface* Mat = GetMaterial(Section);
		if (!Mat || SeenMats.Contains(Mat)) continue;
		SeenMats.Add(Mat);
		FMaterialInterfacePSOPrecacheParams& Params = OutParams.AddDefaulted_GetRef();
		Params.Priority = EPSOPrecachePriority::High;
		Params.MaterialInterface = Mat;
		Params.VertexFactoryDataList.Add(LocalVF);
		Params.PSOPrecacheParams = BasePrecachePSOParams;
	}
}

bool URidePart::StaticLooks()
{
	static int32 FromCommandLine = -1;
	if (FromCommandLine == -1)
	{
		int32 Given = 1;
		FromCommandLine = FParse::Value(FCommandLine::Get(), TEXT("TorqueRideStatic="), Given) ? (Given != 0 ? 1 : 0) : 2;
		if (FromCommandLine != 2) TRPart::CVarStaticParts->Set(FromCommandLine, ECVF_SetByCommandline);
	}
	return TRPart::CVarStaticParts.GetValueOnGameThread() != 0;
}

namespace TRPart
{
// (a free function, not the class's own: no local can shadow one of the component classes' many members)
UStaticMesh* BuildRideLook(UObject* Outer, const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections)
{
	int32 AllPoints = 0, AllTriangles = 0, Kept = 0;
	for (const TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>& Each : Sections)
	{
		const TorqueWorld::RideMesh* M = Each.Key;
		const int32 Count = M ? int32(M->Vertices()) : 0;
		if (Count < 3 || M->Idx.size() < 3) continue;
		AllPoints += Count;
		AllTriangles += int32(M->Idx.size() / 3);
		++Kept;
	}
	if (Kept == 0) return nullptr;

	FMeshDescription Desc;
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	UVs.SetNumChannels(2);   // [real water] (UV1: the water's depth and flow, the land's shore)
	Desc.ReserveNewVertices(AllPoints);
	Desc.ReserveNewVertexInstances(AllPoints);
	Desc.ReserveNewTriangles(AllTriangles);
	Desc.ReserveNewPolygonGroups(Kept);

	UStaticMesh* Made = NewObject<UStaticMesh>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	int32 Slot = 0, Triangles = 0;
	TArray<FVertexInstanceID> Instances;
	for (const TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>& Each : Sections)
	{
		const TorqueWorld::RideMesh* M = Each.Key;
		const int32 Count = M ? int32(M->Vertices()) : 0;
		if (Count < 3 || M->Idx.size() < 3) continue;
		const FName SlotName(*FString::Printf(TEXT("Part%d"), Slot));
		const FPolygonGroupID Group = Desc.CreatePolygonGroup();
		SlotNames[Group] = SlotName;
#if WITH_EDITORONLY_DATA
		Made->GetStaticMaterials().Add(FStaticMaterial(Each.Value, SlotName, SlotName));
#else
		Made->GetStaticMaterials().Add(FStaticMaterial(Each.Value, SlotName));   // (the imported-slot-name argument only exists in editor builds)
#endif
		// (a runtime mesh never gets the editor's UV-density pass: marked as done, or game builds trip
		// UVChannelData ensures, as the shed's catalogue bikes did)
		Made->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
		++Slot;

		const bool bColoured = M->Col.size() == M->Pos.size();
		const bool bAlpha = M->Alpha.size() == size_t(Count);
		const bool bNormals = M->Nrm.size() == M->Pos.size();
		const bool bUvs = M->Uv.size() >= size_t(Count) * 2;
		Instances.Reset(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			const size_t V = size_t(I) * 3;
			// The ride's frame is metres, x east, y up, z south; Unreal's centimetres, x east, y south, z up.
			const FVertexID Point = Desc.CreateVertex();
			Positions[Point] = FVector3f(M->Pos[V] * 100.0f, M->Pos[V + 2] * 100.0f, M->Pos[V + 1] * 100.0f);
			const FVertexInstanceID Inst = Desc.CreateVertexInstance(Point);
			const FVector3f N = bNormals ? FVector3f(M->Nrm[V], M->Nrm[V + 2], M->Nrm[V + 1]).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector) : FVector3f::UpVector;
			Normals[Inst] = N;
			// (the procedural mesh's tangent was +x; here +x made square to the normal, or +y where the
			// normal is +x itself)
			FVector3f T = FVector3f::ForwardVector - N * N.X;
			if (T.SizeSquared() < 1e-4f) T = FVector3f::RightVector - N * N.Y;
			Tangents[Inst] = T.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector);
			Signs[Inst] = 1.0f;
			UVs.Set(Inst, 0, bUvs ? FVector2f(M->Uv[size_t(I) * 2], M->Uv[size_t(I) * 2 + 1]) : FVector2f::ZeroVector);
			UVs.Set(Inst, 1, M->Uv1.size() >= size_t(Count) * 2 ? FVector2f(M->Uv1[size_t(I) * 2], M->Uv1[size_t(I) * 2 + 1]) : FVector2f::ZeroVector);
			// The procedural mesh kept the colours as they are, linear (CreateMeshSection_LinearColor with
			// no sRGB conversion): the materials read them as masks and the road's own colour. The static
			// mesh build stores a colour sRGB-encoded (ToFColor(true)), so it's given the colour whose
			// encoding is the procedural mesh's byte: the same bytes reach the material.
			if (bColoured)
			{
				const FLinearColor Linear(M->Col[V], M->Col[V + 1], M->Col[V + 2], bAlpha ? M->Alpha[size_t(I)] : 1.0f);
				const FLinearColor Decoded(Linear.ToFColor(false));
				Colours[Inst] = FVector4f(Decoded.R, Decoded.G, Decoded.B, Decoded.A);
			}
			else
				Colours[Inst] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
			Instances.Add(Inst);
		}
		// The triangles in the procedural mesh's own order (the same front faces).
		for (size_t K = 0; K + 2 < M->Idx.size(); K += 3)
		{
			const uint32 A = M->Idx[K], B = M->Idx[K + 1], C = M->Idx[K + 2];
			if (A >= uint32(Count) || B >= uint32(Count) || C >= uint32(Count) || A == B || B == C || A == C) continue;
			TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
			Tri.Add(Instances[int32(A)]);
			Tri.Add(Instances[int32(B)]);
			Tri.Add(Instances[int32(C)]);
			Desc.CreateTriangle(Group, Tri);
			++Triangles;
		}
	}
	if (Triangles == 0) return nullptr;
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	Params.bMarkPackageDirty = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&Desc);
	if (!Made->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	return Made;
}
} // namespace TRPart

namespace TRPart
{
UStaticMesh* BuildRideLookLods(UObject* Outer, const TArray<TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>>& Lods, const TArray<float>& ScreenSizes)
{
	if (Lods.Num() == 0) return nullptr;
	const int32 Slots = Lods[0].Num();
	int32 Closest = 0;
	for (const TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>& Each : Lods[0])
		if (Each.Key) Closest += int32(Each.Key->Idx.size() / 3);
	if (Closest == 0 || Slots == 0) return nullptr;
	UStaticMesh* Made = NewObject<UStaticMesh>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	// (one material slot per section, named alike at every level, so each level's sections find theirs)
	for (int32 Slot = 0; Slot < Slots; ++Slot)
	{
		const FName SlotName(*FString::Printf(TEXT("Part%d"), Slot));
#if WITH_EDITORONLY_DATA
		Made->GetStaticMaterials().Add(FStaticMaterial(Lods[0][Slot].Value, SlotName, SlotName));
#else
		Made->GetStaticMaterials().Add(FStaticMaterial(Lods[0][Slot].Value, SlotName));
#endif
		Made->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	}
	TArray<FMeshDescription> Descs;
	Descs.SetNum(Lods.Num());
	for (int32 Lod = 0; Lod < Lods.Num(); ++Lod)
	{
		FMeshDescription& Desc = Descs[Lod];
		FStaticMeshAttributes Attr(Desc);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		UVs.SetNumChannels(2);
		TArray<FVertexInstanceID> Instances;
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			const FPolygonGroupID Group = Desc.CreatePolygonGroup();
			SlotNames[Group] = FName(*FString::Printf(TEXT("Part%d"), Slot));
			const TorqueWorld::RideMesh* M = Slot < Lods[Lod].Num() ? Lods[Lod][Slot].Key : nullptr;
			const int32 Count = M ? int32(M->Vertices()) : 0;
			int32 Made3 = 0;
			if (Count >= 3 && M->Idx.size() >= 3)
			{
				const bool bColoured = M->Col.size() == M->Pos.size();
				const bool bAlpha = M->Alpha.size() == size_t(Count);
				const bool bNormals = M->Nrm.size() == M->Pos.size();
				const bool bUvs = M->Uv.size() >= size_t(Count) * 2;
				Instances.Reset(Count);
				for (int32 I = 0; I < Count; ++I)
				{
					const size_t V = size_t(I) * 3;
					const FVertexID Point = Desc.CreateVertex();
					Positions[Point] = FVector3f(M->Pos[V] * 100.0f, M->Pos[V + 2] * 100.0f, M->Pos[V + 1] * 100.0f);
					const FVertexInstanceID Inst = Desc.CreateVertexInstance(Point);
					const FVector3f N = bNormals ? FVector3f(M->Nrm[V], M->Nrm[V + 2], M->Nrm[V + 1]).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector) : FVector3f::UpVector;
					Normals[Inst] = N;
					FVector3f T = FVector3f::ForwardVector - N * N.X;
					if (T.SizeSquared() < 1e-4f) T = FVector3f::RightVector - N * N.Y;
					Tangents[Inst] = T.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector);
					Signs[Inst] = 1.0f;
					UVs.Set(Inst, 0, bUvs ? FVector2f(M->Uv[size_t(I) * 2], M->Uv[size_t(I) * 2 + 1]) : FVector2f::ZeroVector);
					UVs.Set(Inst, 1, FVector2f::ZeroVector);
					if (bColoured)
					{
						const FLinearColor Linear(M->Col[V], M->Col[V + 1], M->Col[V + 2], bAlpha ? M->Alpha[size_t(I)] : 1.0f);
						const FLinearColor Decoded(Linear.ToFColor(false));
						Colours[Inst] = FVector4f(Decoded.R, Decoded.G, Decoded.B, Decoded.A);
					}
					else
						Colours[Inst] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
					Instances.Add(Inst);
				}
				for (size_t K = 0; K + 2 < M->Idx.size(); K += 3)
				{
					const uint32 A = M->Idx[K], B = M->Idx[K + 1], C = M->Idx[K + 2];
					if (A >= uint32(Count) || B >= uint32(Count) || C >= uint32(Count) || A == B || B == C || A == C) continue;
					TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
					Tri.Add(Instances[int32(A)]);
					Tri.Add(Instances[int32(B)]);
					Tri.Add(Instances[int32(C)]);
					Desc.CreateTriangle(Group, Tri);
					++Made3;
				}
			}
			if (Made3 == 0)
			{
				// (a speck under the ground in the middle, so the slot is there at every level)
				TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
				for (int32 K = 0; K < 3; ++K)
				{
					const FVertexID Point = Desc.CreateVertex();
					Positions[Point] = FVector3f(K == 1 ? 0.1f : 0.0f, K == 2 ? 0.1f : 0.0f, -40.0f);
					const FVertexInstanceID Inst = Desc.CreateVertexInstance(Point);
					Normals[Inst] = FVector3f::UpVector;
					Tangents[Inst] = FVector3f::ForwardVector;
					Signs[Inst] = 1.0f;
					UVs.Set(Inst, 0, FVector2f::ZeroVector);
					UVs.Set(Inst, 1, FVector2f::ZeroVector);
					Colours[Inst] = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
					Tri.Add(Inst);
				}
				Desc.CreateTriangle(Group, Tri);
			}
		}
	}
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	Params.bMarkPackageDirty = false;
	TArray<const FMeshDescription*> Pointers;
	for (const FMeshDescription& Desc : Descs) Pointers.Add(&Desc);
	if (!Made->BuildFromMeshDescriptions(Pointers, Params)) return nullptr;
	if (FStaticMeshRenderData* LookData = Made->GetRenderData())
		for (int32 Lod = 0; Lod < LookData->LODResources.Num() && Lod < MAX_STATIC_MESH_LODS; ++Lod)
			LookData->ScreenSize[Lod].Default = Lod < ScreenSizes.Num() ? ScreenSizes[Lod] : (Lod == 0 ? 1.0f : 0.0f);
	return Made;
}
} // namespace TRPart

UStaticMesh* URidePart::BuildLook(UObject* Outer, const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections)
{
	return TRPart::BuildRideLook(Outer, Sections);
}

UStaticMesh* URidePart::BuildLookLods(UObject* Outer, const TArray<TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>>& Lods, const TArray<float>& ScreenSizes)
{
	return TRPart::BuildRideLookLods(Outer, Lods, ScreenSizes);
}
