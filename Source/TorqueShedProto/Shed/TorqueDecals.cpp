#include "Shed/TorqueDecals.h"

#include "Shed/TorqueEffectPaint.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedPick.h"
#include "CanvasItem.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineFontServices.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace TorqueDecalsImpl
{
namespace BD = TorqueFit::Decals;
using DV3 = TorqueSim::V3;

const TCHAR* const DecalTag = TEXT("Decal");

struct FLib
{
	bool bLoaded = false;
	TArray<TorqueDecals::FLibDecal> Decals;
	TArray<TorqueDecals::FFace> Faces;
};

FLib& Lib()
{
	static FLib L;
	if (L.bLoaded) return L;
	L.bLoaded = true;
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Decals/decals.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no decal library at %s (Tools/decals/make_decals.py)"), *Path);
		return L;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("decals"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			TorqueDecals::FLibDecal D;
			D.Id = O->GetStringField(TEXT("id"));
			D.Shelf = O->GetStringField(TEXT("shelf"));
			D.Name = O->GetStringField(TEXT("name"));
			D.File = O->GetStringField(TEXT("file"));
			D.W = (int32)O->GetNumberField(TEXT("w"));
			D.H = (int32)O->GetNumberField(TEXT("h"));
			D.Colours = (int32)O->GetNumberField(TEXT("colours"));
			const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
			if (O->TryGetArrayField(TEXT("tags"), Tags))
				for (const TSharedPtr<FJsonValue>& T : *Tags)
					if (T.IsValid()) D.Tags.Add(T->AsString());
			if (!D.Id.IsEmpty() && !D.File.IsEmpty()) L.Decals.Add(D);
		}
	if (Root->TryGetArrayField(TEXT("fonts"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			TorqueDecals::FFace F;
			F.Id = O->GetStringField(TEXT("id"));
			F.File = O->GetStringField(TEXT("file"));
			F.Name = O->GetStringField(TEXT("name"));
			F.Style = O->GetStringField(TEXT("style"));
			F.bCaps = O->GetBoolField(TEXT("caps"));
			if (!F.Id.IsEmpty()) L.Faces.Add(F);
		}
	UE_LOG(LogTemp, Log, TEXT("Torque: decal library: %d pictures, %d lettering faces"), L.Decals.Num(), L.Faces.Num());
	return L;
}

UMaterialInterface* DecalMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Mat;
	static bool bTried = false;
	if (!Mat.IsValid() && !bTried)
	{
		bTried = true;
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedDecal.M_ShedDecal"));
		if (!Mat.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedDecal (run Tools/ue_build_shed.py): decals aren't drawn"));
		}
		else
			Mat->AddToRoot();
	}
	return Mat.Get();
}

// A PNG file as a texture (sRGB, as M_ShedDecal samples it), at most MaxPx across (shrunk by halves).
UTexture2D* PngTexture(const FString& Path, int32 MaxPx)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() == 0 || Bytes.Num() > 16 * 1024 * 1024) return nullptr;
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetCompressed(Bytes.GetData(), Bytes.Num())) return nullptr;
	TArray64<uint8> Raw;
	if (!Png->GetRaw(ERGBFormat::BGRA, 8, Raw)) return nullptr;
	int32 W = (int32)Png->GetWidth(), H = (int32)Png->GetHeight();
	if (W <= 0 || H <= 0 || Raw.Num() != (int64)W * (int64)H * 4) return nullptr;
	// Shrunk by halves (each pixel the mean of four) till it's small enough.
	while ((W > MaxPx || H > MaxPx) && W >= 2 && H >= 2)
	{
		const int32 NW = W / 2, NH = H / 2;
		TArray64<uint8> Half;
		Half.SetNumUninitialized((int64)NW * NH * 4);
		for (int32 Y = 0; Y < NH; ++Y)
			for (int32 X = 0; X < NW; ++X)
				for (int32 C = 0; C < 4; ++C)
				{
					const int32 Sum = Raw[((int64)(2 * Y) * W + 2 * X) * 4 + C] + Raw[((int64)(2 * Y) * W + 2 * X + 1) * 4 + C] +
						Raw[((int64)(2 * Y + 1) * W + 2 * X) * 4 + C] + Raw[((int64)(2 * Y + 1) * W + 2 * X + 1) * 4 + C];
					Half[((int64)Y * NW + X) * 4 + C] = (uint8)((Sum + 2) / 4);
				}
		Raw = MoveTemp(Half);
		W = NW;
		H = NH;
	}
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Trilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Px = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (Px) FMemory::Memcpy(Px, Raw.GetData(), Raw.Num());
	Mip.BulkData.Unlock();
	T->UpdateResource();
	T->AddToRoot();   // kept for the game's life (the library is small; own pictures a handful)
	return T;
}

FSlateFontInfo LetteringFont(const FString& Path, float Size)
{
	static TMap<FString, TSharedPtr<const FCompositeFont>>* Fonts = new TMap<FString, TSharedPtr<const FCompositeFont>>();
	TSharedPtr<const FCompositeFont>& F = Fonts->FindOrAdd(Path);
	if (!F.IsValid()) F = MakeShared<FStandaloneCompositeFont>(NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	return FSlateFontInfo(F, Size);
}

// Lettering as a picture: the letters in red (the fill), an outline and a drop shadow in green (the
// ink), on black; kept for the last few asked for. OutAspect: its width over its height.
UTextureRenderTarget2D* LetteringTexture(UObject* WorldContext, const FString& Text, const FString& FaceId, bool bOutline, bool bShadow, float& OutAspect)
{
	OutAspect = 1.0f;
	struct FEntry
	{
		FString Key;
		UTextureRenderTarget2D* RT = nullptr;
		float Aspect = 1.0f;
	};
	static TArray<FEntry> Cache;
	const FString Key = FString::Printf(TEXT("%s|%d%d|%s"), *FaceId, bOutline ? 1 : 0, bShadow ? 1 : 0, *Text);
	for (const FEntry& E : Cache)
		if (E.Key == Key && IsValid(E.RT))
		{
			OutAspect = E.Aspect;
			return E.RT;
		}
	const TorqueDecals::FFace* Face = TorqueDecals::Face(FaceId);
	if (!Face || !WorldContext || !WorldContext->GetWorld() || Text.IsEmpty()) return nullptr;
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::IsInitialized() ? FEngineFontServices::Get().GetFontMeasure() : nullptr;
	if (!Measure.IsValid()) return nullptr;
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Fonts") / Face->File);
	if (!FPaths::FileExists(Path)) return nullptr;
	constexpr float Px = 128.0f;
	FSlateFontInfo Font = LetteringFont(Path, Px * 0.75f);   // (points at 96 dpi)
	const int32 Edge = bOutline ? 8 : 0;
	const FVector2D Size(Measure->Measure(Text, Font));
	const float Tall = (float)Measure->GetMaxCharacterHeight(Font);
	const int32 ShadowPx = bShadow ? 10 : 0;
	const int32 Pad = 12 + Edge + ShadowPx;
	const int32 W = FMath::Clamp((int32)FMath::CeilToDouble(Size.X) + Pad * 2, 8, 4096);
	const int32 H = FMath::Clamp((int32)FMath::CeilToFloat(Tall) + Pad * 2, 8, 1024);
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
		const FVector2D At((double)(Pad - ShadowPx / 2), (double)(Pad - ShadowPx / 2));
		FSlateFontInfo Inked = Font;
		if (bOutline)
		{
			Inked.OutlineSettings.OutlineSize = Edge;
			Inked.OutlineSettings.OutlineColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);
		}
		if (bShadow)
		{
			FCanvasTextItem Sh(At + FVector2D((double)ShadowPx, (double)ShadowPx), FText::FromString(Text), Inked, FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
			Sh.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(Sh);
		}
		if (bOutline)
		{
			FCanvasTextItem Ol(At, FText::FromString(Text), Inked, FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
			Ol.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(Ol);
		}
		FCanvasTextItem Item(At, FText::FromString(Text), Font, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
		Item.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Item);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(WorldContext, Ctx);
	RT->AddToRoot();
	FEntry E;
	E.Key = Key;
	E.RT = RT;
	E.Aspect = (float)W / (float)FMath::Max(1, H);
	Cache.Add(E);
	while (Cache.Num() > 32)
	{
		if (Cache[0].RT) Cache[0].RT->RemoveFromRoot();
		Cache.RemoveAt(0);
	}
	OutAspect = E.Aspect;
	return RT;
}

// A decal's skin as a static mesh (the part frame, cm).
UStaticMesh* DecalSkinMesh(UObject* Outer, const BD::Skin& S, UMaterialInterface* Mat)
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
	const FName SlotName(TEXT("Decal"));
	SlotNames[Group] = SlotName;
	TArray<FVertexInstanceID> Inst;
	for (int32 I = 0; I < N; ++I)
	{
		const DV3& P = S.Pos[(size_t)I];
		const DV3& Nv = S.Nrm[(size_t)I];
		const FVertexID V = MD.CreateVertex();
		Positions[V] = FVector3f((float)P.x, (float)P.y, (float)P.z);
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		const FVector3f Nf((float)Nv.x, (float)Nv.y, (float)Nv.z);
		Normals[VI] = Nf;
		FVector3f T = FVector3f::ForwardVector - Nf * FVector3f::DotProduct(FVector3f::ForwardVector, Nf);
		Tangents[VI] = T.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Signs[VI] = 1.0f;
		UVs.Set(VI, 0, FVector2f((float)S.U[(size_t)I], (float)S.V[(size_t)I]));
		Colours[VI] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		Inst.Add(VI);
	}
	int32 Tris = 0;
	for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
	{
		const int32 A = (int32)S.Idx[T], B = (int32)S.Idx[T + 1], C = (int32)S.Idx[T + 2];
		if (A >= N || B >= N || C >= N) continue;
		// Fit/BikeDecal winds (b - a) x (c - a) out, in a right-handed sense; Unreal's front face is
		// -(P1 - P0) x (P2 - P0) (TorqueShedCatalogue's BuildMesh), so each goes in turned round.
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Inst[A]);
		Tri.Add(Inst[C]);
		Tri.Add(Inst[B]);
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

// What's drawn on each slot: its decals' text and the skins made for them.
struct FDrawn
{
	TWeakObjectPtr<UStaticMeshComponent> Holder;
	FString Key;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Skins;
};

TArray<FDrawn>& Drawn()
{
	static TArray<FDrawn> L;
	return L;
}

void DropDrawn(FDrawn& D)
{
	for (const TWeakObjectPtr<UStaticMeshComponent>& S : D.Skins)
		if (S.IsValid()) S->DestroyComponent();
	D.Skins.Reset();
}

FLinearColor Rgb(uint32 V)
{
	return FLinearColor::FromSRGBColor(FColor((uint8)((V >> 16) & 0xFF), (uint8)((V >> 8) & 0xFF), (uint8)(V & 0xFF), 255));
}
} // namespace TorqueDecalsImpl

namespace TDI = TorqueDecalsImpl;

namespace TorqueDecals
{
FString ToUe(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

std::string ToStd(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

const TArray<FLibDecal>& Library() { return TDI::Lib().Decals; }
const TArray<FFace>& Faces() { return TDI::Lib().Faces; }

const FLibDecal* Find(const FString& Id)
{
	for (const FLibDecal& D : Library())
		if (D.Id == Id) return &D;
	return nullptr;
}

const FFace* Face(const FString& Id)
{
	for (const FFace& F : Faces())
		if (F.Id == Id) return &F;
	return nullptr;
}

FString OwnFolder() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Decals")); }

TArray<FString> OwnPictures()
{
	TArray<FString> Files;
	IFileManager::Get().MakeDirectory(*OwnFolder(), true);
	IFileManager::Get().FindFiles(Files, *(OwnFolder() / TEXT("*.png")), true, false);
	TArray<FString> Out;
	for (const FString& F : Files)
	{
		const FString Name = FPaths::GetBaseFilename(F);
		// (a name the decal text can carry: letters, digits, - _ .)
		bool bOk = !Name.IsEmpty() && Name.Len() <= 60;
		for (const TCHAR C : Name) bOk = bOk && (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT('.'));
		if (bOk) Out.Add(Name);
	}
	Out.Sort();
	return Out;
}

bool TakesDecals(FName Slot)
{
	static const TSet<FName> Slots = {FName(TEXT("tank")), FName(TEXT("fairing")), FName(TEXT("side_panels")), FName(TEXT("tail")),
		FName(TEXT("front_mudguard")), FName(TEXT("rear_mudguard")), FName(TEXT("hugger")), FName(TEXT("number_boards"))};
	return Slots.Contains(Slot);
}

FString Shown(const FString& Text, const FString& FaceId)
{
	const FFace* F = Face(FaceId);
	return F && F->bCaps ? Text.ToUpper() : Text;
}

UTexture* LibraryPicture(const FString& Id)
{
	static TMap<FString, UTexture2D*> Made;
	if (UTexture2D** Had = Made.Find(Id)) return *Had;
	const FLibDecal* D = Find(Id);
	UTexture2D* T = D ? TDI::PngTexture(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Decals") / D->File), 1024) : nullptr;
	Made.Add(Id, T);
	return T;
}

UTexture* Thumb(const FString& Id)
{
	static TMap<FString, UTexture2D*> Made;
	if (UTexture2D** Had = Made.Find(Id)) return *Had;
	UTexture2D* T = Find(Id) ? TDI::PngTexture(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Decals/thumbs") / (Id + TEXT(".png"))), 128) : nullptr;
	Made.Add(Id, T);
	return T;
}

UTexture* OwnPicture(const FString& Name)
{
	static TMap<FString, UTexture2D*> Own;
	if (UTexture2D** Had = Own.Find(Name)) return *Had;
	UTexture2D* T = TDI::PngTexture(OwnFolder() / (Name + TEXT(".png")), 512);
	if (T) Own.Add(Name, T);   // (one not there yet is looked for again)
	return T;
}

UTexture* Picture(UObject* WorldContext, const TorqueFit::Decals::Decal& D, float& OutAspect, bool& bFull)
{
	namespace BD = TorqueFit::Decals;
	OutAspect = 1.0f;
	bFull = false;
	const FString Id = ToUe(D.Id);
	if (D.K == BD::Kind::Library)
	{
		if (const FLibDecal* L = Find(Id)) OutAspect = L->Aspect();
		return LibraryPicture(Id);
	}
	if (D.K == BD::Kind::Text)
		return TDI::LetteringTexture(WorldContext, Shown(ToUe(D.Text), Id), Id, D.Has(BD::Outline), D.Has(BD::Shadow), OutAspect);
	// An own picture (Saved/Decals), shrunk to 512 across; a friend's not here yet draws nothing.
	bFull = true;
	UTexture2D* T = Cast<UTexture2D>(OwnPicture(Id));
	if (T && T->GetSizeY() > 0) OutAspect = (float)T->GetSizeX() / (float)T->GetSizeY();
	return T;
}

FTransform PartFrame(const TArray<UStaticMeshComponent*>& SlotMeshes)
{
	const UStaticMeshComponent* First = nullptr;
	for (const UStaticMeshComponent* C : SlotMeshes)
		if (C && C->GetOwner())
		{
			First = C;
			break;
		}
	if (!First) return FTransform::Identity;
	FTransform Frame = First->GetOwner()->GetActorTransform();
	Frame.SetScale3D(FVector::OneVector);
	FBox Box(ForceInit);
	for (const UStaticMeshComponent* C : SlotMeshes)
		if (C) Box += TorqueEffectPaint::BoxIn(C, Frame);
	return Box.IsValid ? FTransform(Box.GetCenter()) * Frame : Frame;
}

std::vector<TorqueSim::V3> PartTriangles(const TArray<UStaticMeshComponent*>& SlotMeshes, const FTransform& Frame)
{
	std::vector<TorqueSim::V3> Out;
	for (const UStaticMeshComponent* C : SlotMeshes)
	{
		const TArray<FVector>* Pos = nullptr;
		const TArray<int32>* Tris = nullptr;
		if (!C || !C->IsVisible() || !TorqueShedPick::Triangles(C, Pos, Tris)) continue;
		const FTransform ToFrame = C->GetComponentTransform().GetRelativeTransform(Frame);
		Out.reserve(Out.size() + (size_t)Tris->Num());
		for (const int32 I : *Tris)
			if (Pos->IsValidIndex(I))
			{
				const FVector P = ToFrame.TransformPosition((*Pos)[I]);
				Out.push_back(TorqueSim::V3(P.X, P.Y, P.Z));
			}
	}
	return Out;
}

void Apply(const TArray<UStaticMeshComponent*>& SlotMeshes, const FString& Text)
{
	namespace BD = TorqueFit::Decals;
	TArray<TDI::FDrawn>& L = TDI::Drawn();
	UStaticMeshComponent* Holder = SlotMeshes.Num() > 0 ? SlotMeshes[0] : nullptr;
	// Clear away what was on meshes that have gone, and (if it's changed) on this one.
	const FString Key = FString::Printf(TEXT("%d|%s"), SlotMeshes.Num(), *Text);
	for (int32 I = L.Num() - 1; I >= 0; --I)
	{
		if (L[I].Holder.IsValid() && L[I].Holder.Get() != Holder) continue;
		if (L[I].Holder.IsValid() && L[I].Key == Key) return;   // the same as drawn
		TDI::DropDrawn(L[I]);
		L.RemoveAt(I);
	}
	if (!Holder || !Holder->GetOwner() || Text.IsEmpty()) return;
	UMaterialInterface* Mat = TDI::DecalMaterial();
	if (!Mat) return;
	const std::vector<BD::Decal> Decals = BD::Parse(ToStd(Text));
	if (Decals.empty()) return;
	const FTransform Frame = PartFrame(SlotMeshes);
	const std::vector<TorqueSim::V3> Tris = PartTriangles(SlotMeshes, Frame);
	if (Tris.size() < 3) return;
	TDI::FDrawn& Rec = L.AddDefaulted_GetRef();
	Rec.Holder = Holder;
	Rec.Key = Key;
	int32 Made = 0;
	for (const BD::Decal& D0 : Decals)
	{
		float Aspect = 1.0f;
		bool bFull = false;
		UTexture* Tex = Picture(Holder, D0, Aspect, bFull);
		if (!Tex) continue;
		std::vector<BD::Decal> Both = {D0};
		if (D0.Has(BD::Pair)) Both.push_back(BD::Mirrored(D0));
		for (const BD::Decal& D : Both)
		{
			BD::Skin S;
			if (!BD::MakeSkin(Tris, D, S)) continue;
			UStaticMesh* Mesh = TDI::DecalSkinMesh(Holder->GetOwner(), S, Mat);
			if (!Mesh) continue;
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Holder->GetOwner());
			C->SetStaticMesh(Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->SetCastShadow(false);
			C->LightingChannels = Holder->LightingChannels;
			C->bVisibleInSceneCaptureOnly = Holder->bVisibleInSceneCaptureOnly;
			C->bHiddenInSceneCapture = Holder->bHiddenInSceneCapture;
			C->ComponentTags.Add(FName(TDI::DecalTag));
			C->SetupAttachment(Holder);
			C->RegisterComponent();
			C->SetWorldTransform(Frame);
			if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			{
				M->SetTextureParameterValue(TEXT("Decal"), Tex);
				M->SetVectorParameterValue(TEXT("Color1"), TDI::Rgb(D.Rgb1));
				M->SetVectorParameterValue(TEXT("Color2"), TDI::Rgb(D.Rgb2));
				M->SetScalarParameterValue(TEXT("Filled"), D.Has(BD::Filled) ? 1.0f : 0.0f);
				M->SetScalarParameterValue(TEXT("Full"), bFull ? 1.0f : 0.0f);
				// Under the lacquer: the paint's gloss; a sticker: a satin vinyl.
				M->SetScalarParameterValue(TEXT("Roughness"), D.Has(BD::UnderLacquer) ? 0.12f : 0.42f);
			}
			Rec.Skins.Add(C);
			++Made;
		}
	}
	UE_LOG(LogTemp, Verbose, TEXT("Torque: %d decal skins drawn"), Made);
}

void ApplyBike(const TArray<UStaticMeshComponent*>& Parts, TFunctionRef<FString(FName Slot)> DecalsOf)
{
	TMap<FName, TArray<UStaticMeshComponent*>> BySlot;
	for (UStaticMeshComponent* C : Parts)
	{
		if (!C) continue;
		const FName Slot = TorqueShedModel::SlotTag(C);
		if (TakesDecals(Slot)) BySlot.FindOrAdd(Slot).Add(C);
	}
	for (const TPair<FName, TArray<UStaticMeshComponent*>>& KV : BySlot) Apply(KV.Value, DecalsOf(KV.Key));
}
}
