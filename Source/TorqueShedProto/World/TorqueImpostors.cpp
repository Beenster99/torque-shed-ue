#include "World/TorqueImpostors.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
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
	E.Colour = TImp::MakeTarget(this, TImp::AtlasPx, 0);
	E.Normal = TImp::MakeTarget(this, TImp::AtlasPx, 1);
	E.Depth = TImp::MakeTarget(this, TImp::AtlasPx, 2);
	E.Material = UMaterialInstanceDynamic::Create(Master, this);
	if (E.Material)
	{
		const float Dist = E.Radius / FMath::Tan(FMath::DegreesToRadians(TImp::FovDeg * 0.5f));
		E.Material->SetTextureParameterValue(TEXT("TexC"), E.Colour);
		E.Material->SetTextureParameterValue(TEXT("TexN"), E.Normal);
		E.Material->SetTextureParameterValue(TEXT("TexD"), E.Depth);
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
		E.Card = TImp::MakeCard(this, E.Radius, MinZ, MaxZ, E.Material);
	}
	if (!E.Card)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque impostors: %s: no card made; its far trees are not drawn"), *Scan->GetName());
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("Torque impostors: %s queued (radius %.0f cm, height %.0f cm)"), *Scan->GetName(), E.Radius, MaxZ - MinZ);
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
	if (Current >= 0 && (!Entries.IsValidIndex(Current) || Entries[Current].bBaked)) Current = -1;
	if (Current < 0)
	{
		for (int32 K = 0; K < Entries.Num(); ++K)
			if (!Entries[K].bBaked && Entries[K].Card) { Current = K; break; }
		if (Current < 0)
		{
			Mark(false);   // (all done: no bake in progress)
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
