#include "Shed/TorqueShedCatalogue.h"

#include "Fit/BikeGeometry.h"
#include "Fit/BikeLayout.h"
#include "Fit/BikeParts.h"
#include "Fit/FitJson.h"
#include "Fit/PartMesh.h"
#include "Fit/PartsDb.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

#include <algorithm>

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
	// An empty choice ("none", a slot's option left off) has no glb: nothing to draw, nothing to warn of.
	const FString Base = FPaths::GetBaseFilename(Key).ToLower();
	if (Key.IsEmpty() || Base.IsEmpty() || Base == TEXT("none")) return false;
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
UMaterialInterface* MaterialFor(const FString& Tag, bool& bPaint, FString& OutFinish, bool& bOutFallback)
{
	const TorqueFit::Json* Idx = Index();
	const TorqueFit::Json* Finishes = Idx ? Idx->Get("tag_finish") : nullptr;
	std::string Finish = Finishes ? Finishes->Str(Std(Tag)) : std::string();
	if (Finish.empty()) Finish = "steel_brushed";
	bPaint = Finish == "paint" || Finish == "paint_2";
	if (bPaint) Finish = "paint_silver";
	static TMap<FString, UMaterialInterface*> Loaded;
	static TSet<FString> FellBack;   // finishes with no MI of their own (the brushed steel stands in)
	const FString Name = Ue(Finish);
	OutFinish = Name;
	if (UMaterialInterface** Found = Loaded.Find(Name))
	{
		bOutFallback = FellBack.Contains(Name);
		return *Found;
	}
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Bike/Materials/MI_%s.MI_%s"), *Name, *Name));
	if (!M && Name.StartsWith(TEXT("cat_")))
	{
		// A catalogue finish the level script hasn't made yet (Tools/ue_build_level.py, from index.json's
		// "finishes"): the Speed Twin's finish the tag had before.
		static const TMap<FString, FString> Before = {{TEXT("cat_powder"), TEXT("black_satin")}, {TEXT("cat_polished_alloy"), TEXT("alu_brushed")},
			{TEXT("cat_cast_alloy"), TEXT("alu_cast")}, {TEXT("cat_brushed_steel"), TEXT("steel_brushed")}, {TEXT("cat_raw_steel"), TEXT("steel_brushed")},
			{TEXT("cat_black_metal"), TEXT("black_satin")}, {TEXT("cat_anodised"), TEXT("alu_brushed")}, {TEXT("cat_titanium"), TEXT("header")},
			{TEXT("cat_carbon"), TEXT("black_gloss")}, {TEXT("cat_rubber"), TEXT("rubber")}, {TEXT("cat_plastic"), TEXT("black_satin")},
			{TEXT("cat_fabric"), TEXT("leather")}};
		if (const FString* Old = Before.Find(Name))
			M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Bike/Materials/MI_%s.MI_%s"), **Old, **Old));
	}
	bOutFallback = M == nullptr;
	if (!M)
	{
		FellBack.Add(Name);
		M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bike/Materials/MI_steel_brushed.MI_steel_brushed"));
	}
	if (M) M->AddToRoot();   // cached across map travel: unrooted, GC frees it when the shed unloads and the cache dangles
	Loaded.Add(Name, M);
	return M;
}

// -TorqueCatalogueDebug=<mode>, to tell geometry from material on the PC: unlit (the engine's unlit
// level colouring material), default (the engine's default lit material), flip (every surface
// wound the other way from the one its normals pick), fullbuild (the static mesh's full build, not
// the fast one). Empty for none.
const FString& DebugMode()
{
	static FString Mode;
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueCatalogueDebug="), Mode);
		Mode = Mode.ToLower();
		if (!Mode.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: catalogue bikes drawn in debug mode '%s'"), *Mode);
		}
	}
	return Mode;
}

// Ours (metres: x forward, y up, z right) to the bike's cm (x forward, y right, z up).
FVector3f ToUe(const TorqueSim::V3& P) { return FVector3f((float)(P.x * 100.0), (float)(P.z * 100.0), (float)(P.y * 100.0)); }

// One surface as a static mesh (its triangles turned for Unreal's left-handed frame).
// Which way a surface is wound is chosen from its own normals: Unreal takes a triangle's face as
// -(P1 - P0) x (P2 - P0), and the winding that agrees with most of the vertex normals is kept, so
// a part comes out right whichever way its glb (or a code-drawn shell) was wound. bOutFlipped:
// the glb's own order was turned round (as the axis swap needs).
// The finishes' detail (normal and detail maps, Tools/make_bike_textures.py) is laid on by UV 0 in
// metres, as on the Speed Twin. A catalogue surface's own UVs are its glb's, one flat projection
// that stretches down the sides and isn't bent with the part, so bBoxUv lays them again as Torque
// Shed's bike shader does (triplanar): each vertex mapped in metres on the plane its normal faces
// most, the tangent along that plane's u so the detail's bumps light the right way. Decals keep
// their own (their coverage map is laid out on them). UV 1's v is the road grime, as
// make_speedtwin.py writes it: by how low the point sits on the bike (bWheel: an even coat, as
// a spinning wheel takes it).
UStaticMesh* BuildMesh(AActor* Owner, const TorqueFit::PartSurface& S, UMaterialInterface* Material, TArray<FVector>& OutPos,
	TArray<int32>& OutTris, bool& bOutFlipped, bool bBoxUv = false, bool bWheel = false)
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
	// The bike masters darken by vertex colour R (baked occlusion on the Speed Twin's glbs): leave it
	// unset and a runtime mesh can come out black, so every vertex is explicitly white (no occlusion).
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
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
		Colours[VI] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		float U = I < (int32)S.U.size() ? (float)S.U[I] : 0.0f;
		float Vv = I < (int32)S.V.size() ? (float)S.V[I] : 0.0f;
		// (Unreal's frame, cm: the plane's u and v directions, for the tangent.)
		FVector3f UDir = FMath::Abs(N.Z) < 0.9f ? FVector3f::CrossProduct(FVector3f::UpVector, N) : FVector3f::ForwardVector;
		FVector3f VDir = FVector3f::CrossProduct(N, UDir);
		if (bBoxUv)
		{
			const FVector3f AbsN(FMath::Abs(N.X), FMath::Abs(N.Y), FMath::Abs(N.Z));
			const FVector3f M = P / 100.0f;   // metres
			if (AbsN.Z >= AbsN.X && AbsN.Z >= AbsN.Y) { U = M.X; Vv = M.Y; UDir = FVector3f::ForwardVector; VDir = FVector3f::RightVector; }       // faces up or down
			else if (AbsN.Y >= AbsN.X) { U = M.X; Vv = -M.Z; UDir = FVector3f::ForwardVector; VDir = -FVector3f::UpVector; }                  // faces the side
			else { U = M.Y; Vv = -M.Z; UDir = FVector3f::RightVector; VDir = -FVector3f::UpVector; }                                            // faces ahead or back
		}
		const FVector3f T = (UDir - N * FVector3f::DotProduct(UDir, N)).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector);
		Tangents[VI] = T;
		Signs[VI] = FVector3f::DotProduct(FVector3f::CrossProduct(N, T), VDir) >= 0.0f ? 1.0f : -1.0f;
		UVs.Set(VI, 0, FVector2f(U, Vv));
		const float Height = P.Z / 100.0f;   // metres above the axles
		const float Grime = bWheel ? 0.35f : FMath::Pow(FMath::Clamp((0.28f - Height) / 0.38f, 0.0f, 1.0f), 1.3f);
		UVs.Set(VI, 1, FVector2f(0.0f, Grime));
		Instances.Add(VI);
	}
	// Turned round (A, C, B), how many triangles face the way their vertex normals do.
	int32 Agree = 0, Count = 0;
	for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
	{
		const int32 A = (int32)S.Idx[T], B = (int32)S.Idx[T + 1], C = (int32)S.Idx[T + 2];
		if (A >= NumVerts || B >= NumVerts || C >= NumVerts || A == B || B == C || A == C) continue;
		const FVector3f Face = -FVector3f::CrossProduct(FVector3f(OutPos[C] - OutPos[A]), FVector3f(OutPos[B] - OutPos[A]));
		const FVector3f Avg = Normals[Instances[A]] + Normals[Instances[B]] + Normals[Instances[C]];
		++Count;
		if (FVector3f::DotProduct(Face, Avg) >= 0.0f) ++Agree;
	}
	bOutFlipped = Agree * 2 >= Count;
	if (DebugMode() == TEXT("flip")) bOutFlipped = !bOutFlipped;
	for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
	{
		const int32 A = (int32)S.Idx[T], B = (int32)S.Idx[T + 1], C = (int32)S.Idx[T + 2];
		if (A >= NumVerts || B >= NumVerts || C >= NumVerts || A == B || B == C || A == C) continue;
		const int32 Second = bOutFlipped ? C : B, Third = bOutFlipped ? B : C;
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Instances[A]);
		Tri.Add(Instances[Second]);
		Tri.Add(Instances[Third]);
		MD.CreateTriangle(Group, Tri);
		OutTris.Add(A);
		OutTris.Add(Second);
		OutTris.Add(Third);
	}
	if (OutTris.Num() == 0) return nullptr;
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Owner ? static_cast<UObject*>(Owner) : GetTransientPackage());
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName));   // the imported-slot-name argument only exists in editor builds
#endif
	// A runtime mesh never gets the editor's UV-density pass: mark it initialised for texture streaming,
	// or every draw trips UVChannelData.bInitialized ensures (and hitches) in a game build.
	Mesh->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = DebugMode() != TEXT("fullbuild");
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	return Mesh;
}

// A drawn surface on Parent: tagged with its slot and glb (the walker and ApplyLooks find it by
// them), and paint in the catalogue part's colour as Torque Shed's solid gloss (plastic duller),
// not the Speed Twin's metallic silver.
UStaticMeshComponent* AddSurface(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Mat,
	const FString& Slot, const FString& Glb, bool bPaint, const FString& PartId)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Parent);
	C->SetRelativeTransform(FTransform::Identity);
	if (Mat) C->SetMaterial(0, Mat);
	C->ComponentTags.Add(FName(*(TEXT("Slot:") + Slot)));
	C->ComponentTags.Add(FName(*(TEXT("Part:") + Glb)));
	if (!bPaint) C->ComponentTags.Add(FName(TorqueShedCatalogue::NoPaintTag));
	C->RegisterComponent();
	if (bPaint)
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0))
		{
			const FLinearColor Colour = TorqueShedParts::DefaultColour(FName(*PartId));
			M->SetVectorParameterValue(TEXT("Color"), Colour);
			M->SetVectorParameterValue(TEXT("Color2"), Colour);
			const bool bPlastic = TorqueShedParts::BinFor(FName(*PartId)) == FName(TEXT("plastic"));
			for (const TCHAR* Param : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(Param, 0.0f);
			for (const TCHAR* Param : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(Param, bPlastic ? 0.45f : 0.3f);
			M->SetScalarParameterValue(TEXT("RoughVar"), 0.05f);
		}
	}
	return C;
}

// A surface's material, with -TorqueCatalogueDebug's plain materials in place of it.
UMaterialInterface* SurfaceMaterial(const FString& Tag, bool& bPaint, FString& OutFinish, bool& bOutFallback)
{
	UMaterialInterface* Mat = MaterialFor(Tag, bPaint, OutFinish, bOutFallback);
	const FString& Debug = DebugMode();
	if (Debug == TEXT("unlit") || Debug == TEXT("default"))
	{
		UMaterialInterface* Plain = Debug == TEXT("unlit") && GEngine ? GEngine->LevelColorationUnlitMaterial.Get() : nullptr;
		Mat = Plain ? Plain : UMaterial::GetDefaultMaterial(MD_Surface);
		bPaint = false;
	}
	return Mat;
}

// A "tag__slot" surface belongs to that slot (a head glb's barrels are the engine's).
void SplitTag(const std::string& Material, const std::string& PartSlot, FString& Tag, FString& Slot)
{
	const FString M = Ue(Material);
	Tag = M;
	Slot = Ue(PartSlot);
	const int32 At = M.Find(TEXT("__"));
	if (At != INDEX_NONE)
	{
		Tag = M.Left(At);
		Slot = M.Mid(At + 2);
	}
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

bool DrawnParts(const TMap<FString, FString>& Parts, const FString& Geo, std::vector<TorqueFit::FittedPart>& OutParts,
	std::vector<TorqueFit::PartMesh>& OutMeshes)
{
	OutParts.clear();
	OutMeshes.clear();
	const TorqueFit::BikeLayout* Lay = Layout();
	const TorqueFit::Json* Idx = TSCI::Index();
	if (!Lay || !Idx) return false;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TSCI::Std(KV.Key)] = TSCI::Std(KV.Value);
	TorqueFit::BikeGeo Fitted;
	if (!Geo.IsEmpty()) TorqueFit::Geo::FromText(TSCI::Std(Geo), Fitted);
	const TorqueFit::Layout L = Lay->Solve(Map, Fitted.Rig);
	TorqueFit::BikeParts Placer(*Lay, *Idx->Get("parts"));
	Placer.SetMeshSource([](const std::string& Glb, TorqueFit::PartMesh& M) { return TSCI::LoadPart(Glb, M); });
	for (const TorqueFit::FittedPart& P : Placer.Fit(Map, L))
	{
		if (P.Hidden) continue;
		TorqueFit::PartMesh Glb;
		if (!TSCI::LoadPart(P.Glb, Glb)) continue;
		TorqueFit::PartMesh Drawn = TorqueFit::BikeParts::Draw(Glb, P);
		Drawn.Surfaces.erase(std::remove_if(Drawn.Surfaces.begin(), Drawn.Surfaces.end(), [&P](const TorqueFit::PartSurface& S)
			{ return std::find(P.HiddenTags.begin(), P.HiddenTags.end(), S.Material) != P.HiddenTags.end(); }), Drawn.Surfaces.end());
		OutParts.push_back(P);
		OutMeshes.push_back(std::move(Drawn));
	}
	return true;
}

int32 MakeBike(AActor* Owner, USceneComponent* Parent, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out, double& OutAxleHeight, const FString& Geo, TArray<FName>* OutNodes, FBikeFrame* OutFrame,
	TArray<FDashGauge>* OutGauges, bool bOnlyFitted)
{
	OutAxleHeight = 31.0;
	const TorqueFit::BikeLayout* Lay = Layout();
	const TorqueFit::Json* Idx = TSCI::Index();
	if (!Lay || !Idx || !Owner || !Parent) return 0;
	TorqueFit::PartsMap Map;
	for (const TPair<FString, FString>& KV : Parts) Map[TSCI::Std(KV.Key)] = TSCI::Std(KV.Value);
	TorqueFit::BikeGeo Fitted;
	if (!Geo.IsEmpty()) TorqueFit::Geo::FromText(TSCI::Std(Geo), Fitted);
	const TorqueFit::Layout L = Lay->Solve(Map, Fitted.Rig);
	OutAxleHeight = L.Stats.RearRadius * 100.0;
	if (OutFrame)
	{
		OutFrame->bValid = true;
		OutFrame->FrontAxle = FVector(TSCI::ToUe(L.FrontAxle));
		OutFrame->RearAxle = FVector(TSCI::ToUe(L.RearAxle));
		OutFrame->SteerPoint = FVector(TSCI::ToUe(L.Head));
		FVector Dir = FVector(TSCI::ToUe(L.SteerAxis)).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		OutFrame->SteerDir = Dir.Z < 0.0 ? -Dir : Dir;
		OutFrame->SwingPivot = FVector(TSCI::ToUe(L.Pivot));
		OutFrame->Seat = FVector(TSCI::ToUe(L.Hip));
		const FVector Peg(TSCI::ToUe(L.Peg));
		OutFrame->Peg = FVector(Peg.X, FMath::Abs(Peg.Y), Peg.Z);
		const FVector Grip(TSCI::ToUe(L.GripR));
		OutFrame->Grip = FVector(Grip.X, FMath::Abs(Grip.Y), Grip.Z);
		// Feet forward: the pegs 20 cm ahead of the swingarm pivot, or a cruiser's or chopper's frame.
		const auto Frm = Map.find("frame");
		const TorqueFit::Json* FramePart = Frm != Map.end() ? Lay->Part(Frm->second) : nullptr;
		const std::string FrameStyle = FramePart ? FramePart->Str("style") : std::string();
		OutFrame->bLaidBack = L.Peg.x > L.Pivot.x + 0.2 || FrameStyle == "chopper" || FrameStyle == "cruiser";
	}
	TorqueFit::BikeParts Placer(*Lay, *Idx->Get("parts"));
	Placer.SetMeshSource([](const std::string& Glb, TorqueFit::PartMesh& M) { return TSCI::LoadPart(Glb, M); });
	int32 Made = 0;
	TMap<FString, int32> Summary;   // what each surface's finish became, for the log
	int32 Turned = 0, Kept = 0;     // surfaces wound the other way from their glb, or as it
	for (const TorqueFit::FittedPart& P : Placer.Fit(Map, L))
	{
		if (P.Hidden) continue;   // inside a full fairing's shell (the plain headlight), as Godot hides it
		if (bOnlyFitted && P.Slot != "frame")
		{
			// Only what's fitted: the layout dresses an empty slot with a stand-in (bars, tyres...).
			const auto Fitted = Map.find(P.Slot);
			if (Fitted != Map.end() && (Fitted->second.empty() || Fitted->second == "none")) continue;
		}
		TorqueFit::PartMesh Glb;
		if (!TSCI::LoadPart(P.Glb, Glb)) continue;
		const TorqueFit::PartMesh Drawn = TorqueFit::BikeParts::Draw(Glb, P);
		for (const TorqueFit::PartSurface& S : Drawn.Surfaces)
		{
			// Surfaces behind a full fairing's side panels, as Godot hides them (_hide_covered).
			if (std::find(P.HiddenTags.begin(), P.HiddenTags.end(), S.Material) != P.HiddenTags.end()) continue;
			FString Tag, Slot;
			TSCI::SplitTag(S.Material, P.Slot, Tag, Slot);
			bool bPaint = false, bFallback = false;
			FString Finish;
			UMaterialInterface* Mat = TSCI::SurfaceMaterial(Tag, bPaint, Finish, bFallback);
			FString Line = Tag + TEXT(" -> ") + (Mat ? Mat->GetName() : FString(TEXT("no material"))) + (bFallback ? TEXT(" (fallback for ") + Finish + TEXT(")") : FString());
			if (S.Nrm.size() < S.Pos.size()) Line += TEXT(", no normals");
			if (bPaint)
			{
				const FLinearColor Colour = TorqueShedParts::DefaultColour(FName(*Parts.FindRef(Slot)));
				Line += FString::Printf(TEXT(", %s colour %.2f %.2f %.2f"), *Slot, Colour.R, Colour.G, Colour.B);
			}
			Summary.FindOrAdd(Line) += 1;
			TArray<FVector> Pos;
			TArray<int32> Tris;
			bool bFlipped = false;
			const bool bBoxUv = Tag != TEXT("decal") && !Finish.StartsWith(TEXT("decal"));
			const bool bWheel = P.Node == "front_wheel" || P.Node == "rear_wheel";
			UStaticMesh* Mesh = TSCI::BuildMesh(Owner, S, Mat, Pos, Tris, bFlipped, bBoxUv, bWheel);
			if (!Mesh) continue;
			++(bFlipped ? Turned : Kept);
			TorqueShedPick::AddShape(Mesh, Pos, Tris);
			UStaticMeshComponent* C = TSCI::AddSurface(Owner, Parent, Mesh, Mat, Slot, TSCI::Ue(P.Glb), bPaint, Parts.FindRef(Slot));
			Out.Add(C);
			if (OutNodes) OutNodes->Add(P.Node.empty() ? NAME_None : FName(*TSCI::Ue(P.Node)));
			if (OutGauges && !P.Gauge.empty())
			{
				// (Ours to Unreal as the vertices go: x, z, y; points in cm.)
				FDashGauge G;
				G.What = FName(*TSCI::Ue(P.Gauge));
				G.Mesh = Out.Num() - 1;
				G.At = FVector(TSCI::ToUe(P.GaugeAt));
				G.Axis = FVector(P.GaugeAxis.x, P.GaugeAxis.z, P.GaugeAxis.y).GetSafeNormal();
				G.Up = FVector(P.GaugeUp.x, P.GaugeUp.z, P.GaugeUp.y).GetSafeNormal();
				OutGauges->Add(G);
			}
			++Made;
		}
	}
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: catalogue bike's %d surfaces (%d wound the other way from their glb, %d as it) by finish:"), Made, Turned, Kept);
	}
	for (const TPair<FString, int32>& KV : Summary)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque:   %d x %s"), KV.Value, *KV.Key);
	}
	return Made;
}

int32 MakePart(AActor* Owner, USceneComponent* Parent, const FString& MainSlot, const TMap<FString, FString>& Parts,
	TArray<UStaticMeshComponent*>& Out)
{
	const TorqueFit::BikeLayout* Lay = Layout();
	const TorqueFit::Json* Idx = TSCI::Index();
	const FString MainId = Parts.FindRef(MainSlot);
	if (!Lay || !Idx || !Owner || !Parent || MainId.IsEmpty()) return 0;
	// Built once for each part (with what's on it) and kept: the loose parts are all drawn again
	// whenever one of them moves.
	struct FSurfaceMade
	{
		UStaticMesh* Mesh = nullptr;
		UMaterialInterface* Mat = nullptr;
		FString Slot, Glb;
		bool bPaint = false;
	};
	static TMap<FString, TArray<FSurfaceMade>> Cache;
	TArray<FString> Keys;
	Parts.GetKeys(Keys);
	Keys.Sort();
	FString Key;
	for (const FString& K : Keys) Key += K + TEXT("=") + Parts.FindRef(K) + TEXT(";");
	TArray<FSurfaceMade>* Made = Cache.Find(Key);
	if (!Made)
	{
		Made = &Cache.Add(Key);
		// As it sits on a bike of its own style (its style's reference bike with it, and what's on it, fitted).
		const std::vector<std::string> Styles = Lay->StylesOf(TSCI::Std(MainId));
		const std::string Style = !Styles.empty() ? Styles[0] : (!Lay->Styles().empty() ? Lay->Styles()[0] : std::string());
		TorqueFit::PartsMap Map = Lay->ReferenceBuild(Style);
		for (const TPair<FString, FString>& KV : Parts) Map[TSCI::Std(KV.Key)] = TSCI::Std(KV.Value);
		const TorqueFit::Layout L = Lay->Solve(Map);
		TorqueFit::BikeParts Placer(*Lay, *Idx->Get("parts"));
		Placer.SetMeshSource([](const std::string& Glb, TorqueFit::PartMesh& M) { return TSCI::LoadPart(Glb, M); });
		for (const TorqueFit::FittedPart& P : Placer.Fit(Map, L))
		{
			if (P.Hidden) continue;
			TorqueFit::PartMesh Glb;
			if (!TSCI::LoadPart(P.Glb, Glb)) continue;
			const TorqueFit::PartMesh Drawn = TorqueFit::BikeParts::Draw(Glb, P);
			for (const TorqueFit::PartSurface& S : Drawn.Surfaces)
			{
				FString Tag, Slot;
				TSCI::SplitTag(S.Material, P.Slot, Tag, Slot);
				if (!Parts.Contains(Slot)) continue;   // the rest of the bike it was laid out on
				FSurfaceMade M;
				FString Finish;
				bool bFallback = false;
				M.Mat = TSCI::SurfaceMaterial(Tag, M.bPaint, Finish, bFallback);
				TArray<FVector> Pos;
				TArray<int32> Tris;
				bool bFlipped = false;
				const bool bBoxUv = Tag != TEXT("decal") && !Finish.StartsWith(TEXT("decal"));
				M.Mesh = TSCI::BuildMesh(nullptr, S, M.Mat, Pos, Tris, bFlipped, bBoxUv, P.Node == "front_wheel" || P.Node == "rear_wheel");
				if (!M.Mesh) continue;
				M.Mesh->AddToRoot();   // kept in the cache across map travel
				TorqueShedPick::AddShape(M.Mesh, Pos, Tris);
				M.Slot = Slot;
				M.Glb = TSCI::Ue(P.Glb);
				Made->Add(M);
			}
		}
		{
			UE_LOG(LogTemp, Log, TEXT("Torque: loose %s drawn from the catalogue with %d surfaces"), *MainId, Made->Num());
		}
	}
	for (const FSurfaceMade& M : *Made)
		Out.Add(TSCI::AddSurface(Owner, Parent, M.Mesh, M.Mat, M.Slot, M.Glb, M.bPaint, Parts.FindRef(M.Slot)));
	return Made->Num();
}

}
