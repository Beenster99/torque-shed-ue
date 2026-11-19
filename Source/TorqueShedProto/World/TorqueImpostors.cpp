#include "World/TorqueImpostors.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "StaticMeshAttributes.h"
#include "TextureResource.h"

namespace TImp
{
TAutoConsoleVariable<int32> CVarOn(TEXT("torque.RideImpostors"), 1,
	TEXT("Ride UK: far trees of a scanned kind are impostors the game photographs from the scan at ride start (1, build 157) or the old far models (0). Read as the ride starts; -TorqueImpostors=0|1 overrides (1 also ignores the crash mark)."));
TAutoConsoleVariable<int32> CVarViewsPerTick(TEXT("torque.RideImpostorViewsPerTick"), 2,
	TEXT("Ride UK impostors: views of a tree photographed per frame while baking (each 3 captures of 128 px; 1-8). Read as the ride starts."));
TAutoConsoleVariable<float> CVarNormal01(TEXT("torque.RideImpostorNormal01"), 0.0f,
	TEXT("Ride UK impostors: 1 if the normal capture comes out 0..1 rather than -1..1 (the trees look flat or wrongly lit). For impostors made after the change."));
TAutoConsoleVariable<float> CVarGamma(TEXT("torque.RideImpostorGamma"), 1.0f,
	TEXT("Ride UK impostors: power on the captured base colour (1 as captured; >1 darker). For impostors made after the change."));
TAutoConsoleVariable<float> CVarBrightness(TEXT("torque.RideImpostorBrightness"), 1.0f,
	TEXT("Ride UK impostors: base colour multiplier. For impostors made after the change."));

constexpr int32 Frames = 8;          // views a side: 64 (the same hemi-octahedral layout as M_RideImpostor)
constexpr int32 Cell = 128;          // each view's pixels, captured straight at this size
constexpr int32 AtlasPx = Frames * Cell;
constexpr float FovDeg = 6.0f;       // a narrow view from far off: near enough orthographic
constexpr int32 WarmTicks = 20;      // frames for a new tree's textures and Nanite pages to stream in
constexpr int32 Sources = 3;         // colour, normal, depth
const TCHAR* MasterPath = TEXT("/Game/RideUK/Impostors/M_RideImpostor.M_RideImpostor");
// (the empty-space depth the depth atlas starts with: beyond any tree's MaskDepth, so a card not yet
// photographed draws nothing)
constexpr float EmptyDepth = 1.0e7f;

FString MarkPath() { return FPaths::ProjectSavedDir() / TEXT("TorqueImpostorBake.lock"); }
// ([build 157] the photographed atlases are kept in Saved/Impostors, one file a scan, named by what they
// depend on: the scan (path, size, triangles) and this layout's version. A changed scan or layout makes
// a new name, so it is photographed again.)
constexpr int32 CacheVersion = 1;
constexpr uint32 CacheMagic = 0x504D4954;   // 'TIMP'

FString CacheFileOf(const UStaticMesh* Scan, float Radius, float Height)
{
	const FString Key = FString::Printf(TEXT("%s|v%d|%d|%d|%.1f|%.1f|%d"), *Scan->GetPathName(), CacheVersion, Frames, Cell, Radius, Height, Scan->GetNumTriangles(0));
	return FPaths::ProjectSavedDir() / TEXT("Impostors") / FString::Printf(TEXT("%s_%s.imp"), *Scan->GetName(), *FMD5::HashAnsiString(*Key).Left(12));
}

UTexture2D* MakeTexture(UObject* Outer, EPixelFormat Format, const uint8* Data, int64 Bytes)
{
	UTexture2D* T = UTexture2D::CreateTransient(AtlasPx, AtlasPx, Format);
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
	T->SRGB = false;
	T->Filter = TF_Bilinear;
	T->NeverStream = true;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
	const bool bFits = Dst && Mip.BulkData.GetBulkDataSize() == Bytes;
	if (bFits) FMemory::Memcpy(Dst, Data, Bytes);
	Mip.BulkData.Unlock();
	if (!bFits) return nullptr;
	T->UpdateResource();
	return T;
}

// The direction from the tree towards the camera for view (I, J): the inverse of the material's mapping
// (Tools/ue_tree_impostors.py frame_dir, kept the same).
FVector ViewDir(int32 I, int32 J)
{
	const double Ax = (double(I) / (Frames - 1)) * 2.0 - 1.0;
	const double Ay = (double(J) / (Frames - 1)) * 2.0 - 1.0;
	const double Px = (Ax + Ay) * 0.5, Py = (Ax - Ay) * 0.5;
	const double Z = FMath::Max(0.0, 1.0 - FMath::Abs(Px) - FMath::Abs(Py));
	return FVector(Px, Py, Z).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
}

ETextureRenderTargetFormat FormatOf(int32 Source)
{
	return Source == 2 ? RTF_R32f : RTF_RGBA16f;
}

ESceneCaptureSource CaptureOf(int32 Source)
{
	return Source == 0 ? SCS_BaseColor : Source == 1 ? SCS_Normal : SCS_SceneDepth;
}

UTextureRenderTarget2D* MakeTarget(UObject* Outer, int32 Px, int32 Source)
{
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer, NAME_None, RF_Transient);
	RT->RenderTargetFormat = FormatOf(Source);
	RT->ClearColor = Source == 2 ? FLinearColor(EmptyDepth, EmptyDepth, EmptyDepth, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	RT->bAutoGenerateMips = false;
	RT->TargetGamma = 1.0f;
	RT->InitAutoFormat(Px, Px);
	RT->UpdateResourceImmediate(true);
	return RT;
}

// The card: as tall as the scan and two radii wide in its x-z plane; the material turns it to face
// the camera about the scan's centre. A second, crossing quad in y-z only widens its bounds (the
// material folds it to a line), so a card turned sideways is never culled early.
UStaticMesh* MakeCard(UObject* Outer, float R, float MinZ, float MaxZ, UMaterialInterface* Mat)
{
	FMeshDescription Desc;
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	UVs.SetNumChannels(1);
	const FName SlotName(TEXT("Impostor"));
	const FPolygonGroupID Group = Desc.CreatePolygonGroup();
	SlotNames[Group] = SlotName;
	UStaticMesh* Made = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
#if WITH_EDITORONLY_DATA
	Made->GetStaticMaterials().Add(FStaticMaterial(Mat, SlotName, SlotName));
#else
	Made->GetStaticMaterials().Add(FStaticMaterial(Mat, SlotName));
#endif
	Made->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	auto Quad = [&](const FVector3f& Side, const FVector3f& N)
	{
		const FVector3f Corners[4] = { -Side + FVector3f(0, 0, MinZ), Side + FVector3f(0, 0, MinZ), Side + FVector3f(0, 0, MaxZ), -Side + FVector3f(0, 0, MaxZ) };
		const FVector2f Uv[4] = { FVector2f(0, 1), FVector2f(1, 1), FVector2f(1, 0), FVector2f(0, 0) };
		FVertexInstanceID Inst[4];
		for (int32 K = 0; K < 4; ++K)
		{
			const FVertexID Point = Desc.CreateVertex();
			Positions[Point] = Corners[K];
			Inst[K] = Desc.CreateVertexInstance(Point);
			Normals[Inst[K]] = N;
			Tangents[Inst[K]] = Side.GetSafeNormal();
			Signs[Inst[K]] = 1.0f;
			UVs.Set(Inst[K], 0, Uv[K]);
		}
		TArray<FVertexInstanceID, TFixedAllocator<3>> A, B;
		A.Add(Inst[0]); A.Add(Inst[1]); A.Add(Inst[2]);
		B.Add(Inst[0]); B.Add(Inst[2]); B.Add(Inst[3]);
		Desc.CreateTriangle(Group, A);
		Desc.CreateTriangle(Group, B);
	};
	Quad(FVector3f(R, 0, 0), FVector3f(0, -1, 0));
	Quad(FVector3f(0, R, 0), FVector3f(1, 0, 0));
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
} // namespace TImp

void UTorqueImpostors::Start(AActor* InOwner)
{
	Owner = InOwner;
	int32 Forced = -1;
	FParse::Value(FCommandLine::Get(), TEXT("TorqueImpostors="), Forced);
	bOn = Forced >= 0 ? Forced != 0 : TImp::CVarOn.GetValueOnGameThread() != 0;
	if (bOn && Forced != 1 && IFileManager::Get().FileExists(*TImp::MarkPath()))
	{
		FString Was;
		FFileHelper::LoadFileToString(Was, *TImp::MarkPath());
		UE_LOG(LogTemp, Warning, TEXT("Torque impostors: OFF: the last tree bake did not finish (%s; the game closed or crashed during it). Far trees are the old models. -TorqueImpostors=1 tries again."), *Was.TrimStartAndEnd());
		bOn = false;
	}
	if (bOn)
	{
		Master = LoadObject<UMaterialInterface>(nullptr, TImp::MasterPath);
		if (!Master)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque impostors: OFF: no %s (run Tools/ue_build_rideuk.py). Far trees are the old models."), TImp::MasterPath);
			bOn = false;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s"), bOn ? TEXT("on (far trees photographed from the scans at ride start)") : TEXT("off"));
}

FTorqueImpostor* UTorqueImpostors::Entry(UStaticMesh* Scan)
{
	if (!bOn || !Scan || !Owner) return nullptr;
	for (FTorqueImpostor& E : Entries)
		if (E.Scan == Scan) return E.Card ? &E : nullptr;
	FTorqueImpostor& E = Entries.AddDefaulted_GetRef();
	E.Scan = Scan;
	const FBoxSphereBounds B = Scan->GetBounds();
	E.Centre = B.Origin;
	E.Radius = FMath::Max(float(B.SphereRadius), 10.0f);
	const float MinZ = float(B.Origin.Z - B.BoxExtent.Z), MaxZ = float(B.Origin.Z + B.BoxExtent.Z);
	if (FirstAskAt <= 0.0) FirstAskAt = FPlatformTime::Seconds();
	bAllLogged = false;
	bAllReady = false;
	E.CacheFile = TImp::CacheFileOf(Scan, E.Radius, MaxZ - MinZ);
	E.bFromCache = Load(E);
	if (!E.bFromCache)
	{
		E.Colour = TImp::MakeTarget(this, TImp::AtlasPx, 0);
		E.Normal = TImp::MakeTarget(this, TImp::AtlasPx, 1);
		E.Depth = TImp::MakeTarget(this, TImp::AtlasPx, 2);
	}
	E.bBaked = E.bFromCache;
	E.Material = UMaterialInstanceDynamic::Create(Master, this);
	if (E.Material)
	{
		const float Dist = E.Radius / FMath::Tan(FMath::DegreesToRadians(TImp::FovDeg * 0.5f));
		E.Material->SetTextureParameterValue(TEXT("TexC"), E.bFromCache ? static_cast<UTexture*>(E.CachedColour.Get()) : static_cast<UTexture*>(E.Colour.Get()));
		E.Material->SetTextureParameterValue(TEXT("TexN"), E.bFromCache ? static_cast<UTexture*>(E.CachedNormal.Get()) : static_cast<UTexture*>(E.Normal.Get()));
		E.Material->SetTextureParameterValue(TEXT("TexD"), E.bFromCache ? static_cast<UTexture*>(E.CachedDepth.Get()) : static_cast<UTexture*>(E.Depth.Get()));
		E.Material->SetScalarParameterValue(TEXT("R"), E.Radius);
		E.Material->SetScalarParameterValue(TEXT("Frames"), float(TImp::Frames));
		E.Material->SetScalarParameterValue(TEXT("MinZ"), MinZ);
		E.Material->SetScalarParameterValue(TEXT("MaxZ"), MaxZ);
		// (the depth pictures run from the far-off camera: the tree is within a radius of Dist)
		E.Material->SetScalarParameterValue(TEXT("MaskDepth"), Dist / E.Radius + 1.5f);
		E.Material->SetScalarParameterValue(TEXT("NormalEncoded01"), TImp::CVarNormal01.GetValueOnGameThread());
		E.Material->SetScalarParameterValue(TEXT("ColourGamma"), TImp::CVarGamma.GetValueOnGameThread());
		E.Material->SetScalarParameterValue(TEXT("Brightness"), TImp::CVarBrightness.GetValueOnGameThread());
		E.Material->SetVectorParameterValue(TEXT("Centre"), FLinearColor(float(E.Centre.X), float(E.Centre.Y), float(E.Centre.Z), 0.0f));
		// ([seasonal trees TEST, Nov 19] the card's kind from its scan's name: pines and spruces keep their
		// needles; the rest turn their own autumn colour. Tools/ue_tree_season.py AUTUMN, the same colours.)
		{
			const FString Name = E.Scan->GetName().ToLower();
			struct FAutumn { const TCHAR* Word; FLinearColor Colour; };
			static const FAutumn Autumns[] = {
				{TEXT("beech"), FLinearColor(0.45f, 0.17f, 0.03f)}, {TEXT("birch"), FLinearColor(0.62f, 0.48f, 0.06f)},
				{TEXT("maple"), FLinearColor(0.55f, 0.36f, 0.05f)}, {TEXT("sycamore"), FLinearColor(0.55f, 0.36f, 0.05f)},
				{TEXT("alder"), FLinearColor(0.16f, 0.15f, 0.05f)}, {TEXT("willow"), FLinearColor(0.45f, 0.42f, 0.10f)},
				{TEXT("rowan"), FLinearColor(0.55f, 0.12f, 0.03f)}, {TEXT("hazel"), FLinearColor(0.50f, 0.40f, 0.07f)}};
			FLinearColor Autumn(0.30f, 0.17f, 0.04f);   // (oak, and any kind not listed)
			for (const FAutumn& A : Autumns)
				if (Name.Contains(A.Word)) { Autumn = A.Colour; break; }
			const bool bEvergreen = Name.Contains(TEXT("pine")) || Name.Contains(TEXT("spruce"));
			E.Material->SetScalarParameterValue(TEXT("SeasonOn"), bEvergreen ? 0.0f : 1.0f);
			E.Material->SetVectorParameterValue(TEXT("AutumnColour"), Autumn);
		}
		E.Card = TImp::MakeCard(this, E.Radius, MinZ, MaxZ, E.Material);
	}
	if (!E.Card)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque impostors: %s: no card made; its far trees are not drawn"), *Scan->GetName());
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s %s (radius %.0f cm, height %.0f cm)"), *Scan->GetName(),
		E.bFromCache ? TEXT("ready from Saved/Impostors") : TEXT("queued to be photographed"), E.Radius, MaxZ - MinZ);
	return &E;
}

UStaticMesh* UTorqueImpostors::Card(UStaticMesh* Scan)
{
	FTorqueImpostor* E = Entry(Scan);
	return E ? E->Card.Get() : nullptr;
}

UMaterialInterface* UTorqueImpostors::Material(UStaticMesh* Scan)
{
	FTorqueImpostor* E = Entry(Scan);
	return E ? E->Material.Get() : nullptr;
}

int32 UTorqueImpostors::Baked() const
{
	int32 N = 0;
	for (const FTorqueImpostor& E : Entries) N += E.bBaked ? 1 : 0;
	return N;
}

bool UTorqueImpostors::bAllReady = false;

bool UTorqueImpostors::Load(FTorqueImpostor& E)
{
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *E.CacheFile, FILEREAD_Silent)) return false;
	FMemoryReader Ar(File);
	uint32 Magic = 0;
	int32 Version = 0, Px = 0, RawBytes = 0, PackedBytes = 0;
	Ar << Magic << Version << Px << RawBytes << PackedBytes;
	const int64 Pixels = int64(TImp::AtlasPx) * TImp::AtlasPx;
	const int64 Expect = Pixels * (8 + 8 + 2);   // (colour and normal half-float RGBA, depth half-float R)
	if (Magic != TImp::CacheMagic || Version != TImp::CacheVersion || Px != TImp::AtlasPx || RawBytes != Expect || PackedBytes <= 0 || Ar.Tell() + PackedBytes > File.Num())
		return false;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawBytes);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawBytes, File.GetData() + Ar.Tell(), PackedBytes)) return false;
	E.CachedColour = TImp::MakeTexture(this, PF_FloatRGBA, Raw.GetData(), Pixels * 8);
	E.CachedNormal = TImp::MakeTexture(this, PF_FloatRGBA, Raw.GetData() + Pixels * 8, Pixels * 8);
	E.CachedDepth = TImp::MakeTexture(this, PF_R16F, Raw.GetData() + Pixels * 16, Pixels * 2);
	return E.CachedColour && E.CachedNormal && E.CachedDepth;
}

void UTorqueImpostors::Save(FTorqueImpostor& E)
{
	FTextureRenderTargetResource* C = E.Colour ? E.Colour->GameThread_GetRenderTargetResource() : nullptr;
	FTextureRenderTargetResource* N = E.Normal ? E.Normal->GameThread_GetRenderTargetResource() : nullptr;
	FTextureRenderTargetResource* D = E.Depth ? E.Depth->GameThread_GetRenderTargetResource() : nullptr;
	if (!C || !N || !D) return;
	// (reading back waits for the GPU once per tree: the copies just queued included)
	TArray<FFloat16Color> Cs, Ns;
	TArray<FLinearColor> Ds;
	if (!C->ReadFloat16Pixels(Cs) || !N->ReadFloat16Pixels(Ns) || !D->ReadLinearColorPixels(Ds)) return;
	const int64 Pixels = int64(TImp::AtlasPx) * TImp::AtlasPx;
	if (Cs.Num() != Pixels || Ns.Num() != Pixels || Ds.Num() != Pixels) return;
	int64 Covered = 0;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(Pixels * 18);
	FMemory::Memcpy(Raw.GetData(), Cs.GetData(), Pixels * 8);
	FMemory::Memcpy(Raw.GetData() + Pixels * 8, Ns.GetData(), Pixels * 8);
	FFloat16* Dh = reinterpret_cast<FFloat16*>(Raw.GetData() + Pixels * 16);
	const float Dist = E.Radius / FMath::Tan(FMath::DegreesToRadians(TImp::FovDeg * 0.5f));
	for (int64 P = 0; P < Pixels; ++P)
	{
		const float Depth = Ds[P].R;
		Covered += (FMath::IsFinite(Depth) && Depth < Dist + 1.5f * E.Radius) ? 1 : 0;
		Dh[P] = FFloat16(FMath::IsFinite(Depth) ? FMath::Min(Depth, 65000.0f) : 65000.0f);
	}
	const double Coverage = 100.0 * double(Covered) / double(Pixels);
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s: the tree covers %.1f%% of its atlas"), *E.Scan->GetName(), Coverage);
	if (Covered == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque impostors: %s: the pictures are empty: not kept (photographed again next ride)"), *E.Scan->GetName());
		return;
	}
	int32 PackedBytes = FCompression::CompressMemoryBound(NAME_Zlib, int32(Raw.Num()));
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(PackedBytes);
	if (!FCompression::CompressMemory(NAME_Zlib, Packed.GetData(), PackedBytes, Raw.GetData(), int32(Raw.Num()))) return;
	TArray<uint8> File;
	FMemoryWriter Ar(File);
	uint32 Magic = TImp::CacheMagic;
	int32 Version = TImp::CacheVersion, Px = TImp::AtlasPx, RawBytes = int32(Raw.Num());
	Ar << Magic << Version << Px << RawBytes << PackedBytes;
	File.Append(Packed.GetData(), PackedBytes);
	if (FFileHelper::SaveArrayToFile(File, *E.CacheFile))
		UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s: kept in %s (%.1f MB)"), *E.Scan->GetName(), *FPaths::GetCleanFilename(E.CacheFile), File.Num() / 1048576.0);
}

void UTorqueImpostors::Mark(bool bOnNow)
{
	if (bOnNow == bMarked) return;
	bMarked = bOnNow;
	if (bOnNow)
		FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")), *TImp::MarkPath());
	else
		IFileManager::Get().Delete(*TImp::MarkPath(), false, true, true);
}

bool UTorqueImpostors::MakeRig()
{
	if (Tree) return true;
	if (bRigFailed || !Owner || !Owner->GetRootComponent()) return false;
	const int32 Slots = FMath::Clamp(TImp::CVarViewsPerTick.GetValueOnGameThread(), 1, 8);
	Tree = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	Tree->SetMobility(EComponentMobility::Movable);
	Tree->SetupAttachment(Owner->GetRootComponent());
	Tree->SetUsingAbsoluteLocation(true);
	Tree->SetUsingAbsoluteRotation(true);
	Tree->SetUsingAbsoluteScale(true);
	Tree->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tree->SetCanEverAffectNavigation(false);
	Tree->SetCastShadow(false);
	Tree->bAffectDistanceFieldLighting = false;
	Tree->bAffectDynamicIndirectLighting = false;
	Tree->bVisibleInRayTracing = false;
	Tree->bReceivesDecals = false;
	Tree->SetVisibleInSceneCaptureOnly(true);   // (the rider never sees it)
	Tree->SetWorldLocationAndRotation(Owner->GetActorLocation() + FVector(0.0, 0.0, 100000.0), FQuat::Identity);
	Tree->RegisterComponent();
	for (int32 Slot = 0; Slot < Slots; ++Slot)
		for (int32 Source = 0; Source < TImp::Sources; ++Source)
		{
			USceneCaptureComponent2D* Cap = NewObject<USceneCaptureComponent2D>(Owner, NAME_None, RF_Transient);
			Cap->SetupAttachment(Owner->GetRootComponent());
			Cap->SetUsingAbsoluteLocation(true);
			Cap->SetUsingAbsoluteRotation(true);
			Cap->bCaptureEveryFrame = false;
			Cap->bCaptureOnMovement = false;
			Cap->bAlwaysPersistRenderingState = true;
			Cap->ProjectionType = ECameraProjectionMode::Perspective;
			Cap->FOVAngle = TImp::FovDeg;
			Cap->CaptureSource = TImp::CaptureOf(Source);
			Cap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Cap->ShowOnlyComponents.Add(Tree);
			// (base colour, normals and depth need no lighting effects: the shop preview's switches and more)
			Cap->ShowFlags.SetFog(false);
			Cap->ShowFlags.SetVolumetricFog(false);
			Cap->ShowFlags.SetAtmosphere(false);
			Cap->ShowFlags.SetMotionBlur(false);
			Cap->ShowFlags.SetSkyLighting(false);
			Cap->ShowFlags.SetLensFlares(false);
			Cap->ShowFlags.SetBloom(false);
			Cap->ShowFlags.SetDynamicShadows(false);
			Cap->ShowFlags.SetAmbientOcclusion(false);
			Cap->ShowFlags.SetScreenSpaceReflections(false);
			Cap->ShowFlags.SetTranslucency(false);
			for (const TCHAR* Name : { TEXT("GlobalIllumination"), TEXT("Lumen"), TEXT("LumenGlobalIllumination"), TEXT("LumenReflections"),
			                           TEXT("VirtualShadowMaps"), TEXT("ContactShadows"), TEXT("DistanceFieldAO"), TEXT("Cloud"), TEXT("TemporalAA"),
			                           TEXT("AntiAliasing"), TEXT("EyeAdaptation") })
			{
				const int32 Flag = FEngineShowFlags::FindIndexByName(Name);
				if (Flag != INDEX_NONE) Cap->ShowFlags.SetSingleFlag(uint32(Flag), false);
			}
			FPostProcessSettings& PP = Cap->PostProcessSettings;
			PP.bOverride_DynamicGlobalIlluminationMethod = true;
			PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
			PP.bOverride_ReflectionMethod = true;
			PP.ReflectionMethod = EReflectionMethod::None;
			UTextureRenderTarget2D* Shot = TImp::MakeTarget(this, TImp::Cell, Source);
			Cap->TextureTarget = Shot;
			Cap->RegisterComponent();
			Captures.Add(Cap);
			Shots.Add(Shot);
		}
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: camera rig made (%d views a frame)"), Slots);
	return true;
}

void UTorqueImpostors::BeginTree(FTorqueImpostor& E)
{
	Tree->SetStaticMesh(E.Scan);
	Tree->SetForcedLodModel(1);   // (LOD 0 for a mesh that isn't Nanite)
	Tree->PrestreamTextures(10.0f, false);
	Warm = TImp::WarmTicks;
	NextView = 0;
	StartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s: photographing"), *E.Scan->GetName());
}

// Last tick's pictures into their places in the atlases: a plain copy on the GPU (render graph), in
// the same order as the captures, before this tick's captures reuse the pictures.
void UTorqueImpostors::CopyPending()
{
	if (Pending.Num() == 0) return;
	if (Entries.IsValidIndex(PendingEntry))
	{
		const FTorqueImpostor& E = Entries[PendingEntry];
		UTextureRenderTarget2D* Atlases[TImp::Sources] = { E.Colour.Get(), E.Normal.Get(), E.Depth.Get() };
		for (const FIntPoint& SlotView : Pending)
			for (int32 Source = 0; Source < TImp::Sources; ++Source)
			{
				const int32 ShotIndex = SlotView.X * TImp::Sources + Source;
				if (!Shots.IsValidIndex(ShotIndex) || !Shots[ShotIndex] || !Atlases[Source]) continue;
				FTextureRenderTargetResource* Src = Shots[ShotIndex]->GameThread_GetRenderTargetResource();
				FTextureRenderTargetResource* Dst = Atlases[Source]->GameThread_GetRenderTargetResource();
				if (!Src || !Dst) continue;
				const int32 I = SlotView.Y % TImp::Frames, J = SlotView.Y / TImp::Frames;
				const FIntVector At(I * TImp::Cell, J * TImp::Cell, 0);
				ENQUEUE_RENDER_COMMAND(TorqueImpostorCell)([Src, Dst, At](FRHICommandListImmediate& RHICmdList)
				{
					FRHITexture* SrcTex = Src->GetRenderTargetTexture();
					FRHITexture* DstTex = Dst->GetRenderTargetTexture();
					if (!SrcTex || !DstTex) return;
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef From = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SrcTex, TEXT("TorqueImpostorView")));
					FRDGTextureRef To = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DstTex, TEXT("TorqueImpostorAtlas")));
					FRHICopyTextureInfo Info;
					Info.Size = FIntVector(TImp::Cell, TImp::Cell, 1);
					Info.DestPosition = At;
					AddCopyTexturePass(GraphBuilder, From, To, Info);
					GraphBuilder.Execute();
				});
			}
	}
	Pending.Reset();
}

void UTorqueImpostors::Tick()
{
	// ([round 156] the editor bake re-entered its own setup 157 times while a material compiled and
	// took the GPU driver down: nothing here may run twice at once)
	if (bInTick || !bOn) return;
	TGuardValue<bool> Guard(bInTick, true);
	CopyPending();
	if (Entries.IsValidIndex(SaveEntry)) Save(Entries[SaveEntry]);
	SaveEntry = -1;
	if (Current >= 0 && (!Entries.IsValidIndex(Current) || Entries[Current].bBaked)) Current = -1;
	if (Current < 0)
	{
		for (int32 K = 0; K < Entries.Num(); ++K)
			if (!Entries[K].bBaked && Entries[K].Card) { Current = K; break; }
		if (Current < 0)
		{
			Mark(false);   // (all done: no bake in progress)
			if (!bAllLogged && Entries.Num() > 0)
			{
				int32 Cached = 0;
				for (const FTorqueImpostor& Each : Entries) Cached += Each.bFromCache ? 1 : 0;
				UE_LOG(LogTemp, Display, TEXT("Torque impostors: all ready: %d trees (%d from Saved/Impostors, %d photographed), %.1f s after the first was asked for"),
					Entries.Num(), Cached, Entries.Num() - Cached, FPlatformTime::Seconds() - FirstAskAt);
				bAllLogged = true;
			}
			bAllReady = true;
			if (Tree && Tree->IsRegistered() && Tree->GetStaticMesh()) Tree->SetStaticMesh(nullptr);
			return;
		}
		if (!MakeRig())
		{
			bRigFailed = true;
			bOn = false;
			UE_LOG(LogTemp, Warning, TEXT("Torque impostors: no camera rig; impostors off"));
			return;
		}
		Mark(true);
		BeginTree(Entries[Current]);
	}
	FTorqueImpostor& E = Entries[Current];
	const double Dist = double(E.Radius) / FMath::Tan(FMath::DegreesToRadians(double(TImp::FovDeg) * 0.5));
	const FVector CentreWs = Tree->GetComponentLocation() + E.Centre;
	const int32 Slots = Captures.Num() / TImp::Sources;
	if (Warm > 0)
	{
		// (a view or two taken and thrown away while the tree streams in: the first pictures of a new
		// mesh came out empty in the editor)
		--Warm;
		if (Warm % 5 == 0)
			for (int32 Source = 0; Source < TImp::Sources; ++Source)
			{
				USceneCaptureComponent2D* Cap = Captures[Source];
				const FVector D = TImp::ViewDir(TImp::Frames / 2, TImp::Frames / 2);
				Cap->SetWorldLocationAndRotation(CentreWs + D * Dist, FRotationMatrix::MakeFromX(-D).ToQuat());
				Cap->CaptureScene();
			}
		return;
	}
	PendingEntry = Current;
	for (int32 Slot = 0; Slot < Slots && NextView < TImp::Frames * TImp::Frames; ++Slot, ++NextView)
	{
		const int32 I = NextView % TImp::Frames, J = NextView / TImp::Frames;
		const FVector D = TImp::ViewDir(I, J);
		const FQuat Look = FRotationMatrix::MakeFromX(-D).ToQuat();
		for (int32 Source = 0; Source < TImp::Sources; ++Source)
		{
			USceneCaptureComponent2D* Cap = Captures[Slot * TImp::Sources + Source];
			Cap->SetWorldLocationAndRotation(CentreWs + D * Dist, Look);
			Cap->CaptureScene();
		}
		Pending.Add(FIntPoint(Slot, NextView));
	}
	if (NextView >= TImp::Frames * TImp::Frames)
	{
		// (its last views are copied next tick, before anything else is captured)
		E.bBaked = true;
		SaveEntry = Current;
		UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s: done in %.1f s (%d of %d trees)"), *E.Scan->GetName(),
			FPlatformTime::Seconds() - StartedAt, Baked(), Entries.Num());
	}
}

void UTorqueImpostors::Stop()
{
	if (bInTick) return;
	Pending.Reset();
	Mark(false);
}
