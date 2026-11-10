#include "Shed/TorqueShopPreview.h"
#include "Shed/TorqueBikeBadge.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueFluidModels.h"

#include "Fit/BikeLayout.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueToolModels.h"
#include "Coop/TorqueUi.h"
#include "CanvasItem.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"

#include <string>

namespace TorqueShopPreviewImpl
{
// High over the level, well away from the profile page's studio (ATorqueProfilePreview, at 0, 0).
const FVector Where(20000.0, 0.0, 150000.0);
constexpr float Fov = 30.0f;                 // the capture's across
constexpr int32 TargetW = 1280;              // the picture's width in pixels (its height follows the box)
constexpr double SpinRate = 24.0;            // degrees a second
constexpr double StartYaw = -145.0;          // a three-quarter view from the front left
constexpr double StartPitch = 12.0;          // looking down a little
constexpr double MinZoom = 0.6, MaxZoom = 3.5;
// The studio's own fixed exposure (manual, EV100 0 and this compensation: a picture's value is a
// surface's nits x 2^Bias / 1.2). The key gives ~19 lux at the item whatever its size (see Tick), so
// a white part (albedo 0.8) in it is ~4.9 nits and lands just under 1.0 before the tone curve.
// (Auto exposure metered the dim backdrop and the level's sky light lit the item as well: the
// washed-out, cream and orange look of build 125.)
constexpr float ExposureBias = -2.0f;
// The backdrop's glow (M_ShedScreenPage's Brightness, nits for a white page): the gradient's top
// ~0.15 and the pool behind the item ~0.3 after exposure, well under the lit part.
constexpr float BackdropBrightness = 3.5f;

// The backdrop's picture: a light grey at the top falling to a dark grey at the floor, a soft
// pool of light behind the middle of the item, and the corners shaded off.
UTexture2D* MakeGradient()
{
	constexpr int32 N = 256;
	UTexture2D* T = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
	T->SRGB = true;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	FColor* Px = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Px) return nullptr;
	const FLinearColor Top(0.46f, 0.46f, 0.47f), Bottom(0.11f, 0.11f, 0.115f), Glow(0.64f, 0.64f, 0.64f);   // greys (build 101)
	for (int32 Y = 0; Y < N; ++Y)
		for (int32 X = 0; X < N; ++X)
		{
			const float U = (X + 0.5f) / N, V = (Y + 0.5f) / N;
			// Top to bottom, eased so most of the fall is under the middle (a studio sweep).
			const float Fall = FMath::SmoothStep(0.0f, 1.0f, FMath::Pow(V, 1.25f));
			FLinearColor C = FMath::Lerp(Top, Bottom, Fall);
			// The pool of light, a little above the middle.
			const float Dx = (U - 0.5f) * 1.25f, Dy = (V - 0.46f) * 1.6f;
			const float Pool = FMath::Exp(-(Dx * Dx + Dy * Dy) * 5.0f);
			C = FMath::Lerp(C, Glow, Pool * 0.55f);
			// The corners shaded.
			const float Cx = U - 0.5f, Cy = V - 0.5f;
			C *= 1.0f - 0.45f * FMath::Clamp((Cx * Cx + Cy * Cy) * 2.2f - 0.12f, 0.0f, 1.0f);
			C.A = 1.0f;
			// Written as the colour it should look (the texture's sRGB), with a little dither against banding.
			const float Dither = (((X * 7 + Y * 13) % 5) - 2) * 0.0015f;
			Px[Y * N + X] = FLinearColor(C.R + Dither, C.G + Dither, C.B + Dither, 1.0f).GetClamped().ToFColor(false);
		}
	Mip.BulkData.Unlock();
	T->UpdateResource();
	return T;
}

// A flat 1 m quad in the actor's YZ plane facing -x (back at a camera looking along +x), its
// texture's top at +z.
UStaticMesh* MakeQuad(UMaterialInterface* Material)
{
	const FVector P[4] = {FVector(0, -50, 50), FVector(0, 50, 50), FVector(0, -50, -50), FVector(0, 50, -50)};
	const FVector2f UV[4] = {FVector2f(0, 0), FVector2f(1, 0), FVector2f(0, 1), FVector2f(1, 1)};
	const FVector Facing(-1, 0, 0), Right(0, 1, 0);
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	const FName SlotName(TEXT("Backdrop"));
	SlotNames[Group] = SlotName;
	FVertexInstanceID Ids[4];
	for (int32 I = 0; I < 4; ++I)
	{
		const FVertexID V = MD.CreateVertex();
		Positions[V] = FVector3f(P[I]);
		Ids[I] = MD.CreateVertexInstance(V);
		Normals[Ids[I]] = FVector3f(Facing);
		Tangents[Ids[I]] = FVector3f(Right);
		Signs[Ids[I]] = 1.0f;
		Colours[Ids[I]] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		UVs.Set(Ids[I], 0, UV[I]);
	}
	// Each triangle wound to face Facing (Unreal's face: -(P1 - P0) x (P2 - P0)).
	const int32 Tris[2][3] = {{0, 1, 2}, {2, 1, 3}};
	for (const auto& Tri3 : Tris)
	{
		int32 A = Tri3[0], Bv = Tri3[1], Cv = Tri3[2];
		if (FVector::DotProduct(-FVector::CrossProduct(P[Bv] - P[A], P[Cv] - P[A]), Facing) < 0.0) Swap(Bv, Cv);
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Ids[A]);
		Tri.Add(Ids[Bv]);
		Tri.Add(Ids[Cv]);
		MD.CreateTriangle(Group, Tri);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName));
#endif
	Mesh->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	return Mesh;
}

// A paint's finish on a material of the bike master (Color, Metallic, Roughness and their 2s).
void Paint(UStaticMeshComponent* C, const FLinearColor& Colour, float Metallic, float Roughness)
{
	UMaterialInstanceDynamic* M = C ? C->CreateDynamicMaterialInstance(0) : nullptr;
	if (!M) return;
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	M->SetVectorParameterValue(TEXT("Color2"), Colour);
	if (Metallic >= 0.0f)
		for (const TCHAR* P : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(P, Metallic);
	if (Roughness >= 0.0f)
		for (const TCHAR* P : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(P, Roughness);
}

// Shown clean, as built: the catalogue's finishes carry a road grime of their own (Tools/
// import_blender_parts.py, make_speedtwin.py) that the shed sets per bike, so here it's taken off,
// with the rust and the flies.
void Clean(UStaticMeshComponent* C)
{
	UMaterialInstanceDynamic* M = C ? C->CreateDynamicMaterialInstance(0) : nullptr;
	if (!M) return;
	M->SetScalarParameterValue(TEXT("Grime"), 0.0f);
	M->SetScalarParameterValue(TEXT("Rust"), 0.0f);
	M->SetScalarParameterValue(TEXT("FlyGain"), 0.0f);
}

// How a tint's bottle looks (as the mixing bay's rack: ATorquePaintBayRoom's BottleLook).
void BottleLook(const TorquePaintMix::FTint& T, float& OutMetal, float& OutRough)
{
	using TorquePaintMix::ETintKind;
	OutMetal = 0.0f;
	OutRough = 0.35f;
	switch (T.Kind)
	{
	case ETintKind::Alu:
	case ETintKind::Sparkle:
	case ETintKind::Flake:     OutMetal = 0.8f; OutRough = 0.3f; break;
	case ETintKind::Pearl:     OutMetal = 0.45f; OutRough = 0.22f; break;
	case ETintKind::Chameleon: OutMetal = 0.6f; OutRough = 0.18f; break;
	case ETintKind::Candy:     OutRough = 0.08f; break;
	default: break;
	}
}

} // namespace TorqueShopPreviewImpl

namespace TSPI = TorqueShopPreviewImpl;

ATorqueShopPreview::ATorqueShopPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetReplicates(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Holder = CreateDefaultSubobject<USceneComponent>(TEXT("Holder"));
	Holder->SetupAttachment(Pivot);
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->FOVAngle = TSPI::Fov;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetMotionBlur(false);
	// Only the studio's lights: not the level's sky light (it isn't held to lighting channels, and
	// up here it lit the item from the whole sky and the ground) nor bounce light from the level.
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetLensFlares(false);
	{
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = 0;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = TorqueShopPreviewImpl::ExposureBias;
		PP.bOverride_DynamicGlobalIlluminationMethod = true;
		PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		PP.bOverride_ReflectionMethod = true;
		PP.ReflectionMethod = EReflectionMethod::ScreenSpace;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.15f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.0f;
		PP.bOverride_LensFlareIntensity = true;
		PP.LensFlareIntensity = 0.0f;
		Capture->PostProcessBlendWeight = 1.0f;
	}
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Key"));
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fill"));
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Rim"));
	BounceLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Bounce"));
	for (UPointLightComponent* L : {KeyLight, FillLight, RimLight, BounceLight})
	{
		L->SetupAttachment(Root);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetCastShadows(L == KeyLight);
		// Only the studio: its pieces are on channel 2 and nothing else in the level is.
		L->LightingChannels.bChannel0 = false;
		L->LightingChannels.bChannel1 = false;
		L->LightingChannels.bChannel2 = true;
	}
	KeyLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));
	FillLight->SetLightColor(FLinearColor(0.80f, 0.88f, 1.0f));
	RimLight->SetLightColor(FLinearColor(0.95f, 0.97f, 1.0f));
	BounceLight->SetLightColor(FLinearColor(1.0f, 0.98f, 0.95f));   // a neutral floor bounce (no sky light now)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	ResetView();
}

ATorqueShopPreview* ATorqueShopPreview::For(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController()) return nullptr;
	UWorld* World = PC->GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<ATorqueShopPreview> It(World); It; ++It)
		if (It->GetOwner() == PC) return *It;
	FActorSpawnParameters SP;
	SP.Owner = PC;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.ObjectFlags |= RF_Transient;
	ATorqueShopPreview* P = World->SpawnActor<ATorqueShopPreview>(TSPI::Where, FRotator::ZeroRotator, SP);
	if (P)
	{
		P->MakeBackdrop();
		P->SetActorHiddenInGame(true);
	}
	return P;
}

void ATorqueShopPreview::MakeBackdrop()
{
	if (Backdrop) return;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedScreenPage.M_ShedScreenPage"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedScreenPage (run Tools/ue_build_shed.py): the shop preview's backdrop is plain black"));
		return;
	}
	Gradient = TSPI::MakeGradient();
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
	if (!M || !Gradient) return;
	M->SetTextureParameterValue(TEXT("Page"), Gradient);
	M->SetScalarParameterValue(TEXT("Brightness"), TSPI::BackdropBrightness);
	UStaticMesh* Quad = TSPI::MakeQuad(M);
	if (!Quad) return;
	Backdrop = NewObject<UStaticMeshComponent>(this);
	Backdrop->SetStaticMesh(Quad);
	Backdrop->SetMaterial(0, M);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->SetGenerateOverlapEvents(false);
	Backdrop->SetCastShadow(false);
	Backdrop->SetupAttachment(Root);
	Backdrop->RegisterComponent();
}

void ATorqueShopPreview::Studio(UPrimitiveComponent* C) const
{
	if (!C) return;
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetLightingChannels(false, false, true);
}

UStaticMeshComponent* ATorqueShopPreview::Piece(UStaticMesh* Mesh, const FVector& At, const FVector& SizeCm, const FLinearColor& Colour, float Metallic, float Roughness)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness));
	C->SetupAttachment(Holder);
	C->SetRelativeLocation(At);
	C->SetRelativeScale3D(SizeCm / 100.0);   // the basic shapes are a metre across
	C->RegisterComponent();
	Shown.Add(C);
	return C;
}

void ATorqueShopPreview::ResetView()
{
	Yaw = TSPI::StartYaw;
	Pitch = TSPI::StartPitch;
	Zoom = 1.0;
}

void ATorqueShopPreview::Clear()
{
	TArray<USceneComponent*> Under;
	Holder->GetChildrenComponents(true, Under);
	for (USceneComponent* C : Under)
		if (C) C->DestroyComponent();
	Shown.Reset();
	Holder->SetRelativeLocation(FVector::ZeroVector);
	bVessel = false;
	Vessel = TorqueFluidModels::FVessel();
	VesselPart = NAME_None;
}

void ATorqueShopPreview::Rebuild(const FString& NewKey, TFunctionRef<void()> Make)
{
	if (NewKey == Key) return;
	// Another bike in place of a bike (a part or colour changed in the configurator) keeps the view
	// as it was (part_preview.gd show_bike); anything else starts at the start view.
	const bool bKeepView = Key.StartsWith(TEXT("bike:")) && NewKey.StartsWith(TEXT("bike:"));
	Key = NewKey;
	Clear();
	TorqueBikeBadge::Stamp(TArray<UStaticMeshComponent*>(), FString());   // (the last bike's badge goes with it)
	Make();
	for (UStaticMeshComponent* C : Shown) Studio(C);
	Framed();
	// A new item starts at the start view (turning on or paused as it was).
	if (!bKeepView) ResetView();
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(this);
}

void ATorqueShopPreview::Framed()
{
	// Its box with the turntable square, centred on the pivot.
	Pivot->SetRelativeRotation(FRotator::ZeroRotator);
	FBox Box(ForceInit);
	for (UStaticMeshComponent* C : Shown)
		if (C)
		{
			C->UpdateBounds();
			Box += C->Bounds.GetBox();
		}
	if (!Box.IsValid)
	{
		Radius = 50.0;
		return;
	}
	const FVector Off = Box.GetCenter() - Pivot->GetComponentLocation();
	Holder->SetRelativeLocation(Holder->GetRelativeLocation() - Off);
	Radius = FMath::Max(Box.GetExtent().Size(), 5.0);
}

void ATorqueShopPreview::ShowNothing()
{
	Rebuild(FString(), [] {});
}

void ATorqueShopPreview::ShowPart(FName Part)
{
	Rebuild(TEXT("part:") + Part.ToString(), [this, Part]
	{
		TArray<UStaticMeshComponent*> Made;
		// A fluid: its can or bottle as the shed draws it, see-through with the fluid in it (Tick keeps
		// its level), full as it's sold (empty for the drain pan and the bleed bottle).
		if (TorqueShedParts::IsFluid(Part) && TorqueFluidModels::Make(this, Holder, Part, Made, Vessel))
		{
			bVessel = true;
			VesselPart = Part;
			Shown.Append(Made);
			return;
		}
		ATorqueShedItems::MakeNewPart(this, Holder, Part, Cube, Made);
		Shown.Append(Made);
	});
}

void ATorqueShopPreview::ShowKit(FName Model)
{
	Rebuild(TEXT("kit:") + Model.ToString(), [this, Model]
	{
		// The piece's own model with its own materials (none when it isn't imported: an empty view).
		UStaticMesh* Mesh = TorqueFurniture::Mesh(Model);
		if (!Mesh) return;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(Holder);
		C->SetRelativeRotation(FRotator(0.0, TorqueFurniture::ImportYaw, 0.0));
		C->RegisterComponent();
		Shown.Add(C);
	});
}

void ATorqueShopPreview::ShowPaint(int32 Stock)
{
	Rebuild(FString::Printf(TEXT("paint:%d"), Stock), [this, Stock]
	{
		namespace TPM = TorquePaintMix;
		const int32 NB = TPM::Bases().Num();
		const FLinearColor Steel(0.62f, 0.63f, 0.65f);
		if (Stock >= 0 && Stock < NB)
		{
			// A litre tin (paint_bay_room.gd: 11 cm across, 12 high): steel, its lid pressed in, a band
			// round it in the base's colour, the paint's own swatch on the lid.
			const TPM::FBase& B = TPM::Bases()[Stock];
			const double R = 5.5, H = 12.0;
			Piece(Cylinder, FVector(0.0, 0.0, H * 0.5), FVector(R * 2.0, R * 2.0, H), Steel, 0.9f, 0.3f);
			Piece(Cylinder, FVector(0.0, 0.0, H + 0.15), FVector(R * 2.0 - 0.6, R * 2.0 - 0.6, 0.3), Steel * 0.92f, 0.9f, 0.22f);
			Piece(Cylinder, FVector(0.0, 0.0, H * 0.48), FVector(R * 2.0 + 0.12, R * 2.0 + 0.12, H * 0.62), B.Colour, B.Metal, 0.3f);
			Piece(Cylinder, FVector(0.0, 0.0, H + 0.32), FVector(R * 1.1, R * 1.1, 0.06), B.Colour, B.Metal, 0.25f);
			// The handle's lugs.
			for (const double Side : {-1.0, 1.0})
				Piece(Cube, FVector(0.0, Side * (R + 0.2), H - 1.2), FVector(0.8, 0.5, 1.4), Steel, 0.9f, 0.3f);
		}
		else if (Stock >= NB && Stock - NB < TPM::Tints().Num())
		{
			// A tint's bottle (3 cm across, 16 high) in its colour, a black cap.
			const TPM::FTint& T = TPM::Tints()[Stock - NB];
			float Metal = 0.0f, Rough = 0.35f;
			TSPI::BottleLook(T, Metal, Rough);
			const double R = 3.0, H = 16.0;
			Piece(Cylinder, FVector(0.0, 0.0, H * 0.5), FVector(R * 2.0, R * 2.0, H), T.Colour, Metal, Rough);
			Piece(Cylinder, FVector(0.0, 0.0, H + 0.4), FVector(R * 1.3, R * 1.3, 0.8), T.Colour * 0.8f, Metal, Rough);
			Piece(Cylinder, FVector(0.0, 0.0, H + 1.6), FVector(R * 1.1, R * 1.1, 1.8), FLinearColor(0.03f, 0.03f, 0.03f), 0.0f, 0.5f);
			// A white label round it.
			Piece(Cylinder, FVector(0.0, 0.0, H * 0.42), FVector(R * 2.0 + 0.08, R * 2.0 + 0.08, H * 0.35), FLinearColor(0.85f, 0.85f, 0.83f), 0.0f, 0.6f);
		}
	});
}

void ATorqueShopPreview::ShowBike(const TMap<FString, FString>& Parts, const TMap<FName, FTorqueShopFinish>& Finishes, const FString& Badge)
{
	// The key: every slot's part and finish, in slot order.
	TArray<FString> Slots;
	Parts.GetKeys(Slots);
	Slots.Sort();
	FString Sig = TEXT("bike:");
	for (const FString& S : Slots) Sig += S + TEXT("=") + Parts[S] + TEXT(";");
	TArray<FName> Painted;
	Finishes.GetKeys(Painted);
	Painted.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	for (const FName& S : Painted)
	{
		const FTorqueShopFinish& F = Finishes[S];
		Sig += FString::Printf(TEXT("|%s=%s,%.2f,%.2f"), *S.ToString(), *F.Colour.ToString(), F.Metallic, F.Roughness);
	}
	Rebuild(Sig, [this, &Parts, &Finishes]
	{
		TArray<UStaticMeshComponent*> Made;
		double AxleHeight = 0.0;
		TorqueShedCatalogue::MakeBike(this, Holder, Parts, Made, AxleHeight);
		for (UStaticMeshComponent* C : Made)
		{
			if (!C) continue;
			Shown.Add(C);
			TSPI::Clean(C);
			if (C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag))) continue;
			const FName Slot = TorqueShedModel::SlotTag(C);
			if (const FTorqueShopFinish* F = Finishes.Find(Slot))
				TSPI::Paint(C, F->Colour, F->Metallic, F->Roughness);
			else if (const FString* Part = Parts.Find(Slot.ToString().ToLower()))
				TSPI::Paint(C, TorqueShedParts::DefaultColour(FName(**Part)), -1.0f, -1.0f);   // as the shed shows a catalogue part
		}
	});
	// The badge on its tank (again only when it's changed, or the bike has).
	TorqueBikeBadge::Stamp(TorqueBikeBadge::TankOf(Shown), Badge);
}

TArray<FString> ATorqueShopPreview::BikeStyles()
{
	TArray<FString> Out;
	if (const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout())
		for (const std::string& S : Lay->Styles()) Out.Add(FString(UTF8_TO_TCHAR(S.c_str())));
	return Out;
}

TMap<FString, FString> ATorqueShopPreview::StyleBike(const FString& Style)
{
	TMap<FString, FString> Out;
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return Out;
	const std::string Want(TCHAR_TO_UTF8(*Style.ToLower()));
	for (const auto& KV : Lay->ReferenceBuild(Want))
		if (!KV.second.empty() && KV.second != "none") Out.Add(FString(UTF8_TO_TCHAR(KV.first.c_str())), FString(UTF8_TO_TCHAR(KV.second.c_str())));
	return Out;
}

void ATorqueShopPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	WantFor -= DeltaSeconds;
	const bool bOn = WantFor > 0.0 && Shown.Num() > 0 && Target;
	Capture->bCaptureEveryFrame = bOn;
	SetActorHiddenInGame(!bOn);
	if (!bOn) return;

	if (bSpin && !bDrag) Yaw = FMath::Fmod(Yaw + TSPI::SpinRate * DeltaSeconds, 360.0);
	Pivot->SetRelativeRotation(FRotator(0.0, Yaw, 0.0));
	if (bVessel)
	{
		const float Fill = TorqueShedParts::ComesEmpty(VesselPart) ? 0.0f : 1.0f;
		const FLinearColor Liquid = TorqueFluidModels::AgedColour(TorqueShedParts::PartKind(VesselPart), TorqueShedParts::LiquidColour(VesselPart), 0.0f);
		TorqueFluidModels::Update(Vessel, Fill, Liquid, 0.0f, DeltaSeconds);
	}

	// The camera looks at the pivot from far enough to fit the item's sphere in the picture's height.
	const double Aspect = FMath::Max((double)Target->SizeX / FMath::Max((double)Target->SizeY, 1.0), 0.2);
	const double TanH = FMath::Tan(FMath::DegreesToRadians(TSPI::Fov * 0.5));
	const double TanV = TanH / Aspect;
	const double Fit = Radius / FMath::Max(FMath::Min(TanH, TanV), 0.01);
	const double Dist = FMath::Max(Fit * 1.06 / Zoom, Radius * 0.45 + 5.0);
	const FRotator View(-Pitch, 0.0, 0.0);
	const FVector Fwd = View.Vector();
	const FVector Right = FRotationMatrix(View).GetScaledAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(View).GetScaledAxis(EAxis::Z);
	const FVector Centre = Pivot->GetComponentLocation();
	const FVector Eye = Centre - Fwd * Dist;
	Capture->SetWorldLocationAndRotation(Eye, View);

	// The backdrop behind it, square to the view and filling the picture.
	if (Backdrop)
	{
		const double Back = Dist + Radius * 1.8 + 20.0;
		const double W = 2.0 * Back * TanH * 1.08, H = 2.0 * Back * TanV * 1.08;
		Backdrop->SetWorldLocationAndRotation(Eye + Fwd * Back, View);
		Backdrop->SetWorldScale3D(FVector(1.0, W / 100.0, H / 100.0));
	}

	// The lights stay with the camera (the item turns under them): key high on the left, fill low on
	// the right, rim behind and above. Brighter the further out they stand, so it's lit the same at any size.
	const double L = Radius * 2.4 + 60.0;
	const double Gain = FMath::Square(L / 250.0);
	auto Place = [&](UPointLightComponent* Light, const FVector& Dir, double Candelas)
	{
		Light->SetWorldLocation(Centre + Dir.GetSafeNormal() * L);
		Light->SetIntensity((float)(Candelas * Gain));
		Light->SetAttenuationRadius((float)(L * 4.0));
		Light->SetSourceRadius((float)(L * 0.08));   // soft boxes: broad highlights on paint and chrome
	};
	Place(KeyLight, -Fwd * 0.8 - Right * 0.75 + Up * 0.85, 120.0);
	Place(FillLight, -Fwd * 0.9 + Right * 0.85 - Up * 0.1, 45.0);
	Place(RimLight, Fwd * 0.9 + Right * 0.35 + Up * 0.95, 100.0);
	Place(BounceLight, -Fwd * 0.6 - Up * 0.8, 22.0);
}

void ATorqueShopPreview::Draw(AHUD* Hud, UCanvas* Canvas, const FBox2D& Box, const FVector2D& Mouse)
{
	Buttons.Reset();
	PictureBox = Box;
	if (!Hud || !Canvas || !Box.bIsValid || !GEngine) return;
	const FVector2D Size = Box.GetSize();
	if (Size.X < 8.0 || Size.Y < 8.0) return;
	WantFor = 0.25;
	if (!Backdrop) MakeBackdrop();

	// The picture: a target the box's shape.
	const int32 Want_W = TSPI::TargetW;
	const int32 Want_H = FMath::Clamp((int32)(TSPI::TargetW * Size.Y / Size.X + 0.5), 64, 2048);
	if (!Target || Target->SizeX != Want_W || FMath::Abs((int32)Target->SizeY - Want_H) > 4)
	{
		if (!Target)
		{
			Target = NewObject<UTextureRenderTarget2D>(this);
			Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
			Target->ClearColor = FLinearColor(0.02f, 0.02f, 0.025f, 1.0f);
		}
		Target->InitAutoFormat(Want_W, Want_H);
		Target->UpdateResourceImmediate(true);
		Capture->TextureTarget = Target;
	}
	const float S = TorqueUi::Scale(Canvas);
	const float BX0 = (float)Box.Min.X, BY0 = (float)Box.Min.Y, BX1 = (float)Box.Max.X, BY1 = (float)Box.Max.Y;
	const float BW = BX1 - BX0, BHt = BY1 - BY0;
	const FLinearColor Frame(1.0f, 1.0f, 1.0f, 0.08f);
	Hud->DrawRect(FLinearColor(0.03f, 0.035f, 0.045f, 1.0f), BX0, BY0, BW, BHt);
	if (Shown.Num() > 0 && Target->GetResource())
	{
		FCanvasTileItem Tile(Box.Min, Target->GetResource(), Size, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Tile);
	}
	// A thin frame.
	Hud->DrawRect(Frame, BX0, BY0, BW, 1.0f);
	Hud->DrawRect(Frame, BX0, BY1 - 1.0f, BW, 1.0f);
	Hud->DrawRect(Frame, BX0, BY0, 1.0f, BHt);
	Hud->DrawRect(Frame, BX1 - 1.0f, BY0, 1.0f, BHt);

	// No buttons over the picture (beenster, build 101): the mouse works it, as Torque Shed's PartPreview.
	const float Pad = 8.0f * S;
	// How to work it, faint, bottom left.
	const FString Hint = TEXT("Drag to turn  |  wheel to zoom  |  right-click to pause");
	const float HintPx = 13.0f * S;
	if ((float)TorqueUi::Measure(Hint, HintPx).X < BW - 2.0f * Pad)
		TorqueUi::Text(Canvas, Hint, BX0 + Pad, BY1 - Pad - HintPx * 1.3f, HintPx, FLinearColor(TorqueUi::Ink.R, TorqueUi::Ink.G, TorqueUi::Ink.B, 0.6f));
}

bool ATorqueShopPreview::Press(const FVector2D& Mouse)
{
	for (const FButton& B : Buttons)
		if (B.Box.IsInside(Mouse))
		{
			switch (B.What)
			{
			case EButton::Spin: bSpin = !bSpin; break;
			case EButton::ZoomOut: Zoom = FMath::Clamp(Zoom / 1.25, TSPI::MinZoom, TSPI::MaxZoom); break;
			case EButton::ZoomIn: Zoom = FMath::Clamp(Zoom * 1.25, TSPI::MinZoom, TSPI::MaxZoom); break;
			case EButton::Reset: ResetView(); break;
			}
			return true;
		}
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse)) return false;
	bDrag = true;
	DragFrom = DragLast = Mouse;
	return true;
}

void ATorqueShopPreview::Hold(const FVector2D& Mouse, bool bLeftDown)
{
	if (!bDrag) return;
	if (!bLeftDown)
	{
		bDrag = false;
		return;
	}
	const FVector2D D = Mouse - DragLast;
	DragLast = Mouse;
	// Across turns it, up and down tips the view (never under the floor, never straight down).
	Yaw = FMath::Fmod(Yaw + D.X * 0.45, 360.0);
	Pitch = FMath::Clamp(Pitch + D.Y * 0.3, -15.0, 70.0);
}

bool ATorqueShopPreview::Wheel(const FVector2D& Mouse, int32 Dir)
{
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse) || Dir == 0) return false;
	Zoom = FMath::Clamp(Zoom * (Dir < 0 ? 1.12 : 1.0 / 1.12), TSPI::MinZoom, TSPI::MaxZoom);
	return true;
}

bool ATorqueShopPreview::RightPress(const FVector2D& Mouse)
{
	if (!PictureBox.bIsValid || !PictureBox.IsInside(Mouse)) return false;
	bSpin = !bSpin;
	return true;
}
