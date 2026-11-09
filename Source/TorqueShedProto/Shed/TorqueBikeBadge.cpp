#include "Shed/TorqueBikeBadge.h"

#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedPick.h"
#include "Fit/BikeBadge.h"
#include "CanvasItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineFontServices.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace TorqueBikeBadgeImpl
{
namespace BA = TorqueFit::BadgeArt;

const TCHAR* const BadgeTag = TEXT("Badge");

std::string BadgeStd(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

FString BadgeUe(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

UMaterialInterface* BadgeMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Mat;
	static bool bTried = false;
	if (!Mat.IsValid() && !bTried)
	{
		bTried = true;
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedBadge.M_ShedBadge"));
		if (!Mat.IsValid())
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedBadge (run Tools/ue_build_shed.py): tank badges aren't drawn"));
	}
	return Mat.Get();
}

// A picture as a texture: each pixel white as far as it's covered, its alpha the same (sRGB, as
// M_ShedBadge samples it; the material takes the red as its cover).
UTexture2D* BadgeTexture(const BA::Mask& M)
{
	if (M.W <= 0 || M.H <= 0) return nullptr;
	UTexture2D* T = UTexture2D::CreateTransient(M.W, M.H, PF_B8G8R8A8);
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Trilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	uint8* Px = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (Px)
	{
		for (int32 I = 0; I < M.W * M.H; ++I)
		{
			const uint8 A = M.A[(size_t)I];
			Px[I * 4 + 0] = A;
			Px[I * 4 + 1] = A;
			Px[I * 4 + 2] = A;
			Px[I * 4 + 3] = A;
		}
	}
	Mip.BulkData.Unlock();
	T->UpdateResource();
	T->AddToRoot();   // kept for the game's life (a handful of logos)
	return T;
}

// Lettering FontId's file (Data/Fonts/Badge), "" if it isn't there.
FString FontPath(const std::string& FontId)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Fonts/Badge") / BadgeUe(BA::FontOf(FontId).File));
	static TMap<FString, bool> Seen;
	if (const bool* Had = Seen.Find(Path)) return *Had ? Path : FString();
	const bool bThere = FPaths::FileExists(Path);
	if (!bThere) UE_LOG(LogTemp, Warning, TEXT("Torque: no badge lettering %s"), *Path);
	Seen.Add(Path, bThere);
	return bThere ? Path : FString();
}

// Lettering at Size points (96 dpi) from the font file at Path: each file's font made once and kept
// for the game's life (the engine's font cache knows it by it).
FSlateFontInfo FontAt(const FString& Path, float Size)
{
	static TMap<FString, TSharedPtr<const FCompositeFont>>* Fonts = new TMap<FString, TSharedPtr<const FCompositeFont>>();
	TSharedPtr<const FCompositeFont>& F = Fonts->FindOrAdd(Path);
	if (!F.IsValid()) F = MakeShared<FStandaloneCompositeFont>(NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	return FSlateFontInfo(F, Size);
}

// The brand name lettered (white on black, its border round it), as BikeBadge.text_texture: kept
// for the last few names asked for. OutAspect: its width over its height.
UTextureRenderTarget2D* BrandTexture(UObject* WorldContext, const FString& Text, const std::string& FontId, double& OutAspect)
{
	OutAspect = 1.0;
	struct FEntry
	{
		FString Key;
		UTextureRenderTarget2D* RT = nullptr;
		double Aspect = 1.0;
	};
	static TArray<FEntry> Cache;
	const BA::Lettering& L = BA::FontOf(FontId);
	const FString Key = BadgeUe(L.Id) + TEXT("|") + Text;
	for (int32 I = 0; I < Cache.Num(); ++I)
		if (Cache[I].Key == Key && IsValid(Cache[I].RT))
		{
			OutAspect = Cache[I].Aspect;
			return Cache[I].RT;
		}
	if (!WorldContext || !WorldContext->GetWorld()) return nullptr;
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::IsInitialized() ? FEngineFontServices::Get().GetFontMeasure() : nullptr;
	if (!Measure.IsValid()) return nullptr;
	const FString Path = FontPath(FontId);
	if (Path.IsEmpty()) return nullptr;
	// TEXT_PX tall letters (a font's size is in points at 96 dpi).
	const FSlateFontInfo Font = FontAt(Path, (float)BA::TextPx * 0.75f);
	const FVector2D Size(Measure->Measure(Text, Font));
	const float Tall = (float)Measure->GetMaxCharacterHeight(Font);
	const int32 Pad = (int32)FMath::CeilToFloat(Tall * (float)BA::Border);
	const int32 W = FMath::Clamp((int32)FMath::CeilToDouble(Size.X) + Pad * 2, 4, 4096);
	const int32 H = FMath::Clamp((int32)FMath::CeilToFloat(Tall) + Pad * 2, 4, 1024);
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RT->RenderTargetFormat = RTF_RGBA8_SRGB;
	RT->ClearColor = FLinearColor::Black;
	RT->bAutoGenerateMips = false;
	RT->AddressX = TA_Clamp;
	RT->AddressY = TA_Clamp;
	RT->InitAutoFormat(W, H);
	RT->UpdateResourceImmediate(true);
	UCanvas* C = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(WorldContext, RT, C, CanvasSize, Ctx);
	if (C)
	{
		FCanvasTextItem Item(FVector2D((double)Pad, (double)Pad), FText::FromString(Text), Font, FLinearColor::White);
		Item.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Item);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(WorldContext, Ctx);
	RT->AddToRoot();
	FEntry E;
	E.Key = Key;
	E.RT = RT;
	E.Aspect = (double)W / (double)FMath::Max(1, H);
	Cache.Add(E);
	// The last 24 names kept (typing one makes one for every letter).
	while (Cache.Num() > 24)
	{
		if (Cache[0].RT) Cache[0].RT->RemoveFromRoot();
		Cache.RemoveAt(0);
	}
	OutAspect = E.Aspect;
	return RT;
}

// Godot's bike model frame (m: x right, y up, z back) and the bike's (cm: x forward, y right, z up).
BA::V3 ToGodot(const FVector& P) { return BA::V3(P.Y / 100.0, P.Z / 100.0, -P.X / 100.0); }
FVector3f FromGodot(const BA::V3& P, double Scale) { return FVector3f((float)(-P.z * Scale), (float)(P.x * Scale), (float)(P.y * Scale)); }

// A decal's skin as a static mesh (the bike's frame, cm).
UStaticMesh* SkinMesh(UObject* Outer, const BA::Skin& S, UMaterialInterface* Mat)
{
	const int32 N = (int32)S.Pos.size();
	if (N < 3) return nullptr;
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	const FName SlotName(TEXT("Badge"));
	SlotNames[Group] = SlotName;
	TArray<FVertexInstanceID> Inst;
	TArray<FVector3f> Pos, Nrm;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector3f P = FromGodot(S.Pos[(size_t)I], 100.0);
		const FVector3f Nv = FromGodot(S.Nrm[(size_t)I], 1.0).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		const FVertexID V = MD.CreateVertex();
		Positions[V] = P;
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		Normals[VI] = Nv;
		// The tangent along the picture's u (along the bike), the binormal down it.
		FVector3f T = FVector3f::ForwardVector - Nv * FVector3f::DotProduct(FVector3f::ForwardVector, Nv);
		T = T.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Tangents[VI] = T;
		Signs[VI] = 1.0f;
		UVs.Set(VI, 0, FVector2f((float)S.U[(size_t)I], (float)S.V[(size_t)I]));
		Colours[VI] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		Inst.Add(VI);
		Pos.Add(P);
		Nrm.Add(Nv);
	}
	int32 Tris = 0;
	for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
	{
		const int32 A = (int32)S.Idx[T], B = (int32)S.Idx[T + 1], C = (int32)S.Idx[T + 2];
		if (A >= N || B >= N || C >= N) continue;
		// Unreal's front face is -(P1 - P0) x (P2 - P0) (TorqueShedCatalogue's BuildMesh): wound to face out.
		const FVector3f Face = -FVector3f::CrossProduct(Pos[B] - Pos[A], Pos[C] - Pos[A]);
		const bool bAsIs = FVector3f::DotProduct(Face, Nrm[A] + Nrm[B] + Nrm[C]) >= 0.0f;
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Inst[A]);
		Tri.Add(Inst[bAsIs ? B : C]);
		Tri.Add(Inst[bAsIs ? C : B]);
		MD.CreateTriangle(Group, Tri);
		++Tris;
	}
	if (Tris == 0) return nullptr;
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer ? Outer : GetTransientPackage());
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, SlotName));
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

// What's stamped on each tank: the badge (its key) and its decals. A tank that's gone (its bike drawn
// again) takes its decals with it, as they're cleared away on the next stamp.
struct FStamped
{
	TWeakObjectPtr<UStaticMeshComponent> Tank;
	FString Key;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Decals;
};

TArray<FStamped>& Stamped()
{
	static TArray<FStamped> L;
	return L;
}

void DropDecals(FStamped& S)
{
	for (const TWeakObjectPtr<UStaticMeshComponent>& D : S.Decals)
		if (D.IsValid()) D->DestroyComponent();
	S.Decals.Reset();
}

// Clears away what was on Holder (and on tanks that have gone).
void Clear(const UStaticMeshComponent* Holder)
{
	TArray<FStamped>& L = Stamped();
	for (int32 I = L.Num() - 1; I >= 0; --I)
		if (!L[I].Tank.IsValid() || L[I].Tank.Get() == Holder)
		{
			DropDecals(L[I]);
			L.RemoveAt(I);
		}
}

FString KeyOn(const UStaticMeshComponent* Holder)
{
	for (const FStamped& S : Stamped())
		if (S.Tank.Get() == Holder) return S.Key;
	return FString();
}
} // namespace TorqueBikeBadgeImpl

namespace TBBI = TorqueBikeBadgeImpl;

namespace TorqueBikeBadge
{
TArray<UStaticMeshComponent*> TankOf(const TArray<UStaticMeshComponent*>& Parts)
{
	TArray<UStaticMeshComponent*> Out;
	for (UStaticMeshComponent* C : Parts)
		if (C && TorqueShedModel::SlotTag(C) == FName(TEXT("tank"))) Out.Add(C);
	return Out;
}

UTexture* Logo(const FString& Id)
{
	static TMap<FString, UTexture2D*> Made;
	if (UTexture2D** Had = Made.Find(Id)) return *Had;
	UTexture2D* T = TBBI::BadgeTexture(TorqueFit::BadgeArt::LogoMask(TBBI::BadgeStd(Id)));
	Made.Add(Id, T);
	return T;
}

bool DrawLettering(UCanvas* Canvas, const FBox2D& Box, const FString& Text, const FString& FontId, float Px, const FLinearColor& Colour)
{
	if (!Canvas || Text.IsEmpty()) return false;
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::IsInitialized() ? FEngineFontServices::Get().GetFontMeasure() : nullptr;
	const FString Path = TBBI::FontPath(TBBI::BadgeStd(FontId));
	if (!Measure.IsValid() || Path.IsEmpty()) return false;
	const FSlateFontInfo Font = TBBI::FontAt(Path, FMath::Max(1.0f, Px * 0.75f));   // (points at 96 dpi)
	FVector2D Size(Measure->Measure(Text, Font));
	// Too wide for the box: smaller till it fits.
	const double Room = Box.GetSize().X;
	FSlateFontInfo Fitted = Font;
	if (Size.X > Room && Size.X > 0.0)
	{
		Fitted.Size = FMath::Max(1.0f, (float)(Font.Size * Room / Size.X));
		Size = FVector2D(Measure->Measure(Text, Fitted));
	}
	FCanvasTextItem Item(FVector2D(Box.Min.X + (Room - Size.X) * 0.5, Box.Min.Y + (Box.GetSize().Y - Size.Y) * 0.5), FText::FromString(Text), Fitted, Colour);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
	return true;
}

void Stamp(const TArray<UStaticMeshComponent*>& Tank, const FString& Badge)
{
	namespace BA = TorqueFit::BadgeArt;
	UStaticMeshComponent* Holder = Tank.Num() > 0 ? Tank[0] : nullptr;
	if (!Holder || !Holder->GetOwner())
	{
		TBBI::Clear(nullptr);
		return;
	}
	const TorqueFit::Shop::Badge B = TorqueFit::Shop::ParseBadge(TBBI::BadgeStd(Badge));
	// The badge and the tank it's on (a different tank needs it cut again).
	const FString Key = FString::Printf(TEXT("%08x_%d"), GetTypeHash(TBBI::BadgeUe(TorqueFit::Shop::BadgeText(B))), Tank.Num());
	if (!B.Empty() && TBBI::KeyOn(Holder) == Key) return;
	TBBI::Clear(Holder);
	if (B.Empty()) return;
	UMaterialInterface* Mat = TBBI::BadgeMaterial();
	if (!Mat) return;
	// The tank's triangles in the bike's frame (what the tank is drawn under), in Godot's.
	const USceneComponent* FrameOf = Holder->GetAttachParent();
	const FTransform Frame = FrameOf ? FrameOf->GetComponentTransform() : Holder->GetOwner()->GetActorTransform();
	std::vector<BA::V3> Faces;
	for (const UStaticMeshComponent* C : Tank)
	{
		const TArray<FVector>* Pos = nullptr;
		const TArray<int32>* Tris = nullptr;
		if (!C || !C->IsVisible() || !TorqueShedPick::Triangles(C, Pos, Tris)) continue;
		const FTransform ToFrame = C->GetComponentTransform().GetRelativeTransform(Frame);
		for (const int32 I : *Tris)
			if (Pos->IsValidIndex(I)) Faces.push_back(TBBI::ToGodot(ToFrame.TransformPosition((*Pos)[I])));
	}
	if (Faces.size() < 3) return;
	const BA::Box3 Box = BA::BoundsOf(Faces);
	const double MidX = (Box.Min.x + Box.Max.x) * 0.5;
	// The brand lettered (how wide it is decides how big it goes), as BikeBadge.brand_text has it.
	double Aspect = 1.0;
	UTexture* BrandTex = nullptr;
	if (!B.Brand.empty())
	{
		FString Shown = TBBI::BadgeUe(B.Brand);
		if (BA::FontOf(B.Font).Caps) Shown = Shown.ToUpper();
		BrandTex = TBBI::BrandTexture(Holder, Shown, B.Font, Aspect);
	}
	UTexture* LogoTex = B.Logo.empty() ? nullptr : Logo(TBBI::BadgeUe(B.Logo));
	TBBI::FStamped& Record = TBBI::Stamped().AddDefaulted_GetRef();
	Record.Tank = Holder;
	Record.Key = Key;
	int32 Made = 0;
	for (const double Side : {1.0, -1.0})
		for (const BA::Pic& P : BA::Place(B, Box, Aspect))
		{
			UTexture* Tex = P.bLogo ? LogoTex : BrandTex;
			if (!Tex) continue;
			BA::Skin S;
			if (!BA::DecalSkin(Faces, Side, MidX, P, S)) continue;
			UStaticMesh* Mesh = TBBI::SkinMesh(Holder->GetOwner(), S, Mat);
			if (!Mesh) continue;
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Holder->GetOwner());
			C->SetStaticMesh(Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->SetCastShadow(false);
			C->LightingChannels = Holder->LightingChannels;
			C->bVisibleInSceneCaptureOnly = Holder->bVisibleInSceneCaptureOnly;
			C->bHiddenInSceneCapture = Holder->bHiddenInSceneCapture;
			C->ComponentTags.Add(FName(TBBI::BadgeTag));
			C->SetupAttachment(Holder);
			C->RegisterComponent();
			C->SetWorldTransform(Frame);
			if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			{
				const double* Rgb = P.bLogo ? B.LogoColour : B.BrandColour;
				M->SetTextureParameterValue(TEXT("Badge"), Tex);
				M->SetVectorParameterValue(TEXT("Color"), FLinearColor((float)Rgb[0], (float)Rgb[1], (float)Rgb[2], 1.0f));
				M->SetScalarParameterValue(TEXT("Roughness"), 0.3f);
			}
			Record.Decals.Add(C);
			++Made;
		}
	UE_LOG(LogTemp, Verbose, TEXT("Torque: badge stamped on the tank, %d decals"), Made);
}
}
