#include "Photo/TorquePhotos.h"

#include "Coop/TorquePlayerController.h"
#include "Shed/TorqueDecals.h"
#include "Async/Async.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "TextureResource.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shlobj.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace TorquePhotosImpl
{
// A picture's pixels, BGRA, 8 bits each.
struct FRaw
{
	TArray64<uint8> Px;
	int32 W = 0, H = 0;
	bool Ok() const { return W > 0 && H > 0 && Px.Num() == (int64)W * H * 4; }
};

IImageWrapperModule& Wrappers() { return FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName(TEXT("ImageWrapper"))); }

// A file's picture (a JPEG or a PNG), or false. (Off the game thread: the module's got on it first.)
bool Decode(IImageWrapperModule& Mod, const TArray<uint8>& Bytes, FRaw& Out)
{
	if (Bytes.Num() < 8 || Bytes.Num() > 256 * 1024 * 1024) return false;
	const EImageFormat Format = Mod.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (Format != EImageFormat::JPEG && Format != EImageFormat::PNG) return false;
	const TSharedPtr<IImageWrapper> W = Mod.CreateImageWrapper(Format);
	if (!W.IsValid() || !W->SetCompressed(Bytes.GetData(), Bytes.Num())) return false;
	if (!W->GetRaw(ERGBFormat::BGRA, 8, Out.Px)) return false;
	Out.W = (int32)W->GetWidth();
	Out.H = (int32)W->GetHeight();
	if (Out.W <= 0 || Out.H <= 0 || Out.W > 16384 || Out.H > 16384) return false;
	return Out.Ok();
}

bool Load(IImageWrapperModule& Mod, const FString& Path, FRaw& Out)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) && Decode(Mod, Bytes, Out);
}

// The box (X0, Y0, CW x CH) of In, made OW x OH: each new pixel the mean of the old ones under it
// (no shimmer from a big picture made small).
FRaw Resample(const FRaw& In, int32 X0, int32 Y0, int32 CW, int32 CH, int32 OW, int32 OH)
{
	FRaw Out;
	OW = FMath::Max(OW, 1);
	OH = FMath::Max(OH, 1);
	Out.W = OW;
	Out.H = OH;
	Out.Px.SetNumUninitialized((int64)OW * OH * 4);
	for (int32 Y = 0; Y < OH; ++Y)
	{
		const double Sy0 = Y0 + (double)Y * CH / OH, Sy1 = Y0 + (double)(Y + 1) * CH / OH;
		const int32 Ya = FMath::Clamp((int32)Sy0, 0, In.H - 1), Yb = FMath::Clamp(FMath::Max((int32)FMath::CeilToDouble(Sy1), Ya + 1), Ya + 1, In.H);
		for (int32 X = 0; X < OW; ++X)
		{
			const double Sx0 = X0 + (double)X * CW / OW, Sx1 = X0 + (double)(X + 1) * CW / OW;
			const int32 Xa = FMath::Clamp((int32)Sx0, 0, In.W - 1), Xb = FMath::Clamp(FMath::Max((int32)FMath::CeilToDouble(Sx1), Xa + 1), Xa + 1, In.W);
			uint32 Sum[4] = {0, 0, 0, 0};
			for (int32 Sy = Ya; Sy < Yb; ++Sy)
			{
				const uint8* Row = In.Px.GetData() + ((int64)Sy * In.W + Xa) * 4;
				for (int32 Sx = Xa; Sx < Xb; ++Sx, Row += 4)
				{
					Sum[0] += Row[0];
					Sum[1] += Row[1];
					Sum[2] += Row[2];
					Sum[3] += Row[3];
				}
			}
			const uint32 N = (uint32)((Yb - Ya) * (Xb - Xa));
			uint8* O = Out.Px.GetData() + ((int64)Y * OW + X) * 4;
			for (int32 C = 0; C < 4; ++C) O[C] = (uint8)((Sum[C] + N / 2) / N);
		}
	}
	return Out;
}

// Made to fit inside MaxPx each way (kept as it is when it does).
FRaw Fit(FRaw&& In, int32 MaxPx)
{
	if (In.W <= MaxPx && In.H <= MaxPx) return MoveTemp(In);
	const double K = (double)MaxPx / FMath::Max(In.W, In.H);
	return Resample(In, 0, 0, In.W, In.H, FMath::Max(1, FMath::RoundToInt(In.W * K)), FMath::Max(1, FMath::RoundToInt(In.H * K)));
}

FRaw Halve(const FRaw& In)
{
	FRaw Out;
	Out.W = FMath::Max(1, In.W / 2);
	Out.H = FMath::Max(1, In.H / 2);
	Out.Px.SetNumUninitialized((int64)Out.W * Out.H * 4);
	for (int32 Y = 0; Y < Out.H; ++Y)
		for (int32 X = 0; X < Out.W; ++X)
		{
			const int32 X1 = FMath::Min(2 * X, In.W - 1), X2 = FMath::Min(2 * X + 1, In.W - 1);
			const int32 Y1 = FMath::Min(2 * Y, In.H - 1), Y2 = FMath::Min(2 * Y + 1, In.H - 1);
			for (int32 C = 0; C < 4; ++C)
			{
				const uint32 S = In.Px[((int64)Y1 * In.W + X1) * 4 + C] + In.Px[((int64)Y1 * In.W + X2) * 4 + C] +
					In.Px[((int64)Y2 * In.W + X1) * 4 + C] + In.Px[((int64)Y2 * In.W + X2) * 4 + C];
				Out.Px[((int64)Y * Out.W + X) * 4 + C] = (uint8)((S + 2) / 4);
			}
		}
	return Out;
}

bool Encode(IImageWrapperModule& Mod, const FRaw& In, EImageFormat Format, int32 Quality, TArray<uint8>& Out)
{
	Out.Reset();
	const TSharedPtr<IImageWrapper> W = Mod.CreateImageWrapper(Format);
	if (!In.Ok() || !W.IsValid() || !W->SetRaw(In.Px.GetData(), In.Px.Num(), In.W, In.H, ERGBFormat::BGRA, 8)) return false;
	const TArray64<uint8> Packed = W->GetCompressed(Quality);
	if (Packed.Num() == 0 || Packed.Num() > MAX_int32) return false;
	Out.Append(Packed.GetData(), (int32)Packed.Num());
	return true;
}

bool Save(const TArray<uint8>& Bytes, const FString& Path)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(Bytes, *Path);
}

// --- The textures, loaded off the game thread ---------------------------------------------------------

struct FEntry
{
	UTexture2D* Tex = nullptr;   // (rooted while it's here)
	bool bLoading = false, bFailed = false;
	double FailedAt = 0.0;
	uint64 Used = 0;
};
TMap<FString, FEntry>& Cache()
{
	static TMap<FString, FEntry>* M = new TMap<FString, FEntry>();
	return *M;
}
FString KeyOf(const FString& Path, int32 MaxPx) { return FString::Printf(TEXT("%s|%d"), *Path, MaxPx); }
constexpr int32 MaxKept = 96;

// A texture with every mip given (mip 0 first). Game thread.
UTexture2D* MakeTexture(TArray<FRaw>& Mips)
{
	if (Mips.Num() == 0 || !Mips[0].Ok()) return nullptr;
	UTexture2D* T = UTexture2D::CreateTransient(Mips[0].W, Mips[0].H, PF_B8G8R8A8);
	FTexturePlatformData* PD = T ? T->GetPlatformData() : nullptr;
	if (!PD || PD->Mips.Num() == 0) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Trilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	{
		FTexture2DMipMap& M0 = PD->Mips[0];
		void* D = M0.BulkData.Lock(LOCK_READ_WRITE);
		if (D) FMemory::Memcpy(D, Mips[0].Px.GetData(), Mips[0].Px.Num());
		M0.BulkData.Unlock();
	}
	for (int32 I = 1; I < Mips.Num(); ++I)
	{
		FTexture2DMipMap* M = new FTexture2DMipMap();
		M->SizeX = Mips[I].W;
		M->SizeY = Mips[I].H;
		M->SizeZ = 1;
		M->BulkData.Lock(LOCK_READ_WRITE);
		void* D = M->BulkData.Realloc(Mips[I].Px.Num());
		if (D) FMemory::Memcpy(D, Mips[I].Px.GetData(), Mips[I].Px.Num());
		M->BulkData.Unlock();
		PD->Mips.Add(M);
	}
	T->UpdateResource();
	T->AddToRoot();
	return T;
}

void Trim()
{
	TMap<FString, FEntry>& C = Cache();
	int32 Have = 0;
	for (const TPair<FString, FEntry>& KV : C)
		if (KV.Value.Tex) ++Have;
	while (Have > MaxKept)
	{
		FString Oldest;
		uint64 Least = MAX_uint64;
		for (const TPair<FString, FEntry>& KV : C)
			if (KV.Value.Tex && KV.Value.Used < Least)
			{
				Least = KV.Value.Used;
				Oldest = KV.Key;
			}
		if (Oldest.IsEmpty() || Least + 120 > GFrameCounter) break;   // (nothing unused for a couple of seconds)
		if (FEntry* E = C.Find(Oldest))
		{
			E->Tex->RemoveFromRoot();   // (gone once nothing draws it)
		}
		C.Remove(Oldest);
		--Have;
	}
}

// Loads Path (fit to MaxPx, mips made) off the game thread into Key's entry. ThumbOf: Path is a
// photo whose small copy is to be written to ThumbOf as it's made.
void LoadAsync(const FString& Key, const FString& Path, int32 MaxPx, const FString& ThumbOf)
{
	IImageWrapperModule* Mod = &Wrappers();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Mod, Key, Path, MaxPx, ThumbOf]()
	{
		TSharedPtr<TArray<FRaw>> Mips = MakeShared<TArray<FRaw>>();
		FRaw Img;
		if (Load(*Mod, Path, Img))
		{
			Img = Fit(MoveTemp(Img), MaxPx);
			if (!ThumbOf.IsEmpty())
			{
				TArray<uint8> Jpg;
				if (Encode(*Mod, Img, EImageFormat::JPEG, 85, Jpg)) Save(Jpg, ThumbOf);
			}
			Mips->Add(MoveTemp(Img));
			while ((*Mips).Last().W > 1 || (*Mips).Last().H > 1) Mips->Add(Halve((*Mips).Last()));
		}
		AsyncTask(ENamedThreads::GameThread, [Key, Mips]()
		{
			FEntry* E = Cache().Find(Key);
			if (!E) return;
			E->bLoading = false;
			E->Tex = MakeTexture(*Mips);
			E->bFailed = E->Tex == nullptr;
			E->FailedAt = FPlatformTime::Seconds();
			Trim();
		});
	});
}

UTexture2D* Ask(const FString& Path, int32 MaxPx, const FString& ThumbOf, bool bRetry)
{
	const FString Key = KeyOf(Path, MaxPx);
	FEntry& E = Cache().FindOrAdd(Key);
	E.Used = GFrameCounter;
	if (E.Tex) return E.Tex;
	if (E.bLoading) return nullptr;
	if (E.bFailed && !(bRetry && FPlatformTime::Seconds() - E.FailedAt > 3.0)) return nullptr;
	E.bFailed = false;
	E.bLoading = true;
	LoadAsync(Key, Path, MaxPx, ThumbOf);
	return nullptr;
}

FString MonthName(int32 M)
{
	static const TCHAR* Names[] = {TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"),
		TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec")};
	return Names[FMath::Clamp(M, 1, 12) - 1];
}
} // namespace TorquePhotosImpl

namespace TPI = TorquePhotosImpl;

namespace TorquePhotos
{
FString Folder()
{
	static FString Dir;
	if (!Dir.IsEmpty()) return Dir;
#if PLATFORM_WINDOWS
	// The Pictures folder as Windows has it (moved to OneDrive or another drive, that one).
	WCHAR Known[MAX_PATH + 1] = {0};
	if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, Known)) && Known[0] != 0)
	{
		Dir = FString(Known);
		FPaths::NormalizeDirectoryName(Dir);
		Dir = Dir / TEXT("Torque Shed");
	}
#endif
	if (Dir.IsEmpty()) Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Photos"));
	UE_LOG(LogTemp, Log, TEXT("Torque photos: kept in %s"), *Dir);
	return Dir;
}

FString ThumbFolder() { return Folder() / TEXT("thumbs"); }

TArray<FString> List()
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(Folder() / TEXT("*")), true, false);
	TArray<TPair<FDateTime, FString>> Dated;
	for (const FString& N : Names)
	{
		const FString Ext = FPaths::GetExtension(N).ToLower();
		if (Ext != TEXT("jpg") && Ext != TEXT("jpeg") && Ext != TEXT("png")) continue;
		const FString Path = Folder() / N;
		Dated.Emplace(IFileManager::Get().GetTimeStamp(*Path), Path);
	}
	Dated.Sort([](const TPair<FDateTime, FString>& A, const TPair<FDateTime, FString>& B) { return A.Key > B.Key; });
	TArray<FString> Out;
	for (const TPair<FDateTime, FString>& D : Dated) Out.Add(D.Value);
	return Out;
}

FString When(const FString& Path)
{
	const FDateTime Utc = IFileManager::Get().GetTimeStamp(*Path);
	if (Utc == FDateTime::MinValue()) return FString();
	const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow());
	return FString::Printf(TEXT("%d %s %d, %02d:%02d"), Local.GetDay(), *TPI::MonthName(Local.GetMonth()), Local.GetYear(), Local.GetHour(), Local.GetMinute());
}

void SaveShot(int32 W, int32 H, TArray<FColor>&& Pixels, TFunction<void(const FString&)> Done)
{
	if (W <= 0 || H <= 0 || Pixels.Num() != W * H)
	{
		if (Done) Done(FString());
		return;
	}
	// Named by when it was taken (a second one in the same second gets " 2").
	const FDateTime Now = FDateTime::Now();
	const FString Base = FString::Printf(TEXT("Torque Shed %04d-%02d-%02d %02d-%02d-%02d"), Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(),
		Now.GetMinute(), Now.GetSecond());
	FString Path = Folder() / (Base + TEXT(".jpg"));
	for (int32 N = 2; IFileManager::Get().FileExists(*Path) && N < 100; ++N) Path = Folder() / FString::Printf(TEXT("%s %d.jpg"), *Base, N);
	const FString ThumbPath = ThumbFolder() / FPaths::GetCleanFilename(Path);
	IImageWrapperModule* Mod = &TPI::Wrappers();
	TSharedPtr<TPI::FRaw> Img = MakeShared<TPI::FRaw>();
	Img->W = W;
	Img->H = H;
	Img->Px.SetNumUninitialized((int64)W * H * 4);
	FMemory::Memcpy(Img->Px.GetData(), Pixels.GetData(), (SIZE_T)W * H * 4);   // (FColor is B, G, R, A)
	Pixels.Empty();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Mod, Img, Path, ThumbPath, Done]()
	{
		for (int64 I = 3; I < Img->Px.Num(); I += 4) Img->Px[I] = 255;
		TArray<uint8> Jpg;
		const bool bOk = TPI::Encode(*Mod, *Img, EImageFormat::JPEG, 95, Jpg) && TPI::Save(Jpg, Path);
		if (bOk)
		{
			TPI::FRaw Small = TPI::Fit(MoveTemp(*Img), 384);
			if (TPI::Encode(*Mod, Small, EImageFormat::JPEG, 85, Jpg)) TPI::Save(Jpg, ThumbPath);
		}
		const FString Said = bOk ? Path : FString();
		AsyncTask(ENamedThreads::GameThread, [Done, Said]() { if (Done) Done(Said); });
	});
}

UTexture2D* Image(const FString& Path, int32 MaxPx) { return TPI::Ask(Path, MaxPx, FString(), false); }

UTexture2D* Thumb(const FString& PhotoPath)
{
	const FString ThumbPath = ThumbFolder() / FPaths::GetCleanFilename(PhotoPath);
	// (The small copy when it's there; else it's made from the photo as it's loaded.)
	const FString Key = TPI::KeyOf(ThumbPath, 384);
	if (const TPI::FEntry* E = TPI::Cache().Find(Key); E && (E->Tex || E->bLoading)) return TPI::Ask(ThumbPath, 384, FString(), false);
	if (IFileManager::Get().FileExists(*ThumbPath)) return TPI::Ask(ThumbPath, 384, FString(), false);
	// Made from the photo, kept under the small copy's name.
	TPI::FEntry& E = TPI::Cache().FindOrAdd(Key);
	E.Used = GFrameCounter;
	if (E.bFailed) return nullptr;
	E.bLoading = true;
	TPI::LoadAsync(Key, PhotoPath, 384, ThumbPath);
	return nullptr;
}

bool Failed(const FString& Path)
{
	for (const TPair<FString, TPI::FEntry>& KV : TPI::Cache())
		if (KV.Value.bFailed && KV.Key.StartsWith(Path + TEXT("|"))) return true;
	return false;
}

void Forget(const FString& Path)
{
	for (auto It = TPI::Cache().CreateIterator(); It; ++It)
		if (It.Key().StartsWith(Path + TEXT("|")) && !It.Value().bLoading)
		{
			if (It.Value().Tex) It.Value().Tex->RemoveFromRoot();
			It.RemoveCurrent();
		}
}

bool Delete(const FString& Path)
{
	if (!FPaths::IsUnderDirectory(Path, Folder())) return false;
	const FString ThumbPath = ThumbFolder() / FPaths::GetCleanFilename(Path);
	if (!IFileManager::Get().Delete(*Path, false, true, true)) return false;
	IFileManager::Get().Delete(*ThumbPath, false, true, true);
	Forget(Path);
	Forget(ThumbPath);
	return true;
}

// --- Prints ---------------------------------------------------------------------------------------------

void MakePrint(const FString& PhotoPath, TFunction<void(const FString&)> Done)
{
	IImageWrapperModule* Mod = &TPI::Wrappers();
	const FString Shared = TorqueDecals::SharedFolder();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Mod, PhotoPath, Shared, Done]()
	{
		TSharedPtr<TArray<uint8>> Png = MakeShared<TArray<uint8>>();
		FString Id;
		TPI::FRaw Img;
		if (TPI::Load(*Mod, PhotoPath, Img))
		{
			// 7 x 5, its middle: a wide photo loses its sides, a tall one its top and bottom.
			constexpr double Aspect = TorquePhotos::PrintW / TorquePhotos::PrintH;
			int32 CW = Img.W, CH = Img.H;
			if ((double)Img.W / Img.H > Aspect) CW = FMath::Max(1, FMath::RoundToInt(Img.H * Aspect));
			else CH = FMath::Max(1, FMath::RoundToInt(Img.W / Aspect));
			const int32 X0 = (Img.W - CW) / 2, Y0 = (Img.H - CH) / 2;
			// The shared picture (as an own decal picture's: a PNG no bigger than 512, named by what's in it).
			TPI::FRaw Small = TPI::Resample(Img, X0, Y0, CW, CH, 512, FMath::RoundToInt(512 / Aspect));
			for (int64 I = 3; I < Small.Px.Num(); I += 4) Small.Px[I] = 255;
			if (TPI::Encode(*Mod, Small, EImageFormat::PNG, 0, *Png) && Png->Num() <= TorqueDecals::MaxSharedBytes)
			{
				Id = FString::Printf(TEXT("p%08x"), FCrc::MemCrc32(Png->GetData(), Png->Num()));
				// This machine's sharp copy, 1400 x 1000.
				TPI::FRaw Sharp = TPI::Resample(Img, X0, Y0, CW, CH, 1400, 1000);
				for (int64 I = 3; I < Sharp.Px.Num(); I += 4) Sharp.Px[I] = 255;
				TArray<uint8> Jpg;
				if (TPI::Encode(*Mod, Sharp, EImageFormat::JPEG, 92, Jpg)) TPI::Save(Jpg, Shared / (Id + TEXT("_print.jpg")));
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Done, Id, Png]()
		{
			// Kept as the decals keep a shared picture (and checked the same way).
			const bool bOk = !Id.IsEmpty() && TorqueDecals::TakeShared(Id, *Png);
			if (!bOk)
			{
				UE_LOG(LogTemp, Warning, TEXT("Torque photos: couldn't make a print"));
			}
			if (Done) Done(bOk ? Id : FString());
		});
	});
}

UTexture2D* PrintPicture(UObject* WorldContext, const FString& Id)
{
	if (!TorqueDecals::IsSharedId(Id)) return nullptr;
	const FString Sharp = TorqueDecals::SharedFolder() / (Id + TEXT("_print.jpg"));
	const FString Shared = TorqueDecals::SharedFolder() / (Id + TEXT(".png"));
	if (!Failed(Sharp))
	{
		if (UTexture2D* T = TPI::Ask(Sharp, 1400, FString(), false)) return T;
		if (!Failed(Sharp)) return nullptr;   // (still loading)
	}
	if (UTexture2D* T = TPI::Ask(Shared, 512, FString(), true)) return T;
	// A friend's print not here yet: asked for from the host once (it comes as a decal picture does).
	if (Failed(Shared))
		if (ATorquePlayerController* PC = WorldContext && WorldContext->GetWorld() ? Cast<ATorquePlayerController>(WorldContext->GetWorld()->GetFirstPlayerController()) : nullptr)
			PC->WantDecalPicture(Id);
	return nullptr;
}

UStaticMesh* PrintMesh()
{
	static TWeakObjectPtr<UStaticMesh> Made;
	if (Made.IsValid()) return Made.Get();
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
	const FName PhotoSlot(TEXT("Photo")), PaperSlot(TEXT("Paper"));
	const FPolygonGroupID PhotoGroup = MD.CreatePolygonGroup();
	const FPolygonGroupID PaperGroup = MD.CreatePolygonGroup();
	SlotNames[PhotoGroup] = PhotoSlot;
	SlotNames[PaperGroup] = PaperSlot;
	// A face: its four corners in order round it, its outward normal, the UV at each corner.
	auto Face = [&](const FPolygonGroupID Group, const FVector3f (&P)[4], const FVector3f& N, const FVector2f (&UV)[4])
	{
		FVertexInstanceID Inst[4];
		const FVector3f Tan = FMath::Abs(N.X) > 0.9f ? FVector3f(0.0f, 1.0f, 0.0f) : FVector3f(1.0f, 0.0f, 0.0f);
		for (int32 I = 0; I < 4; ++I)
		{
			const FVertexID V = MD.CreateVertex();
			Positions[V] = P[I];
			Inst[I] = MD.CreateVertexInstance(V);
			Normals[Inst[I]] = N;
			Tangents[Inst[I]] = Tan;
			Signs[Inst[I]] = 1.0f;
			UVs.Set(Inst[I], 0, UV[I]);
			Colours[Inst[I]] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		}
		// Unreal draws a triangle's front where -(P1 - P0) x (P2 - P0) points: each wound to face N.
		const bool bFlip = FVector3f::DotProduct(-FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]), N) < 0.0f;
		for (int32 K = 0; K < 2; ++K)
		{
			const int32 A = 0, B = K == 0 ? 1 : 2, C = K == 0 ? 2 : 3;
			TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
			Tri.Add(Inst[A]);
			Tri.Add(Inst[bFlip ? C : B]);
			Tri.Add(Inst[bFlip ? B : C]);
			MD.CreateTriangle(Group, Tri);
		}
	};
	const float HW = (float)(PrintW * 0.5), HH = (float)(PrintH * 0.5), Th = (float)PrintT;
	const FVector2f U00(0.0f, 0.0f), U10(1.0f, 0.0f), U11(1.0f, 1.0f), U01(0.0f, 1.0f);
	// The face (+y, out of the wall): seen from in front, +x is to the right and +z up, so the
	// picture's left edge is at -x and its top row (v 0) at +z.
	{
		const FVector3f P[4] = {FVector3f(-HW, Th, HH), FVector3f(HW, Th, HH), FVector3f(HW, Th, -HH), FVector3f(-HW, Th, -HH)};
		const FVector2f UV[4] = {U00, U10, U11, U01};
		Face(PhotoGroup, P, FVector3f(0.0f, 1.0f, 0.0f), UV);
	}
	// The back, on the wall, and the four edges: paper.
	const FVector2f UV[4] = {U00, U10, U11, U01};
	{
		const FVector3f P[4] = {FVector3f(-HW, 0.0f, HH), FVector3f(HW, 0.0f, HH), FVector3f(HW, 0.0f, -HH), FVector3f(-HW, 0.0f, -HH)};
		Face(PaperGroup, P, FVector3f(0.0f, -1.0f, 0.0f), UV);
	}
	{
		const FVector3f P[4] = {FVector3f(-HW, 0.0f, HH), FVector3f(HW, 0.0f, HH), FVector3f(HW, Th, HH), FVector3f(-HW, Th, HH)};
		Face(PaperGroup, P, FVector3f(0.0f, 0.0f, 1.0f), UV);
	}
	{
		const FVector3f P[4] = {FVector3f(-HW, 0.0f, -HH), FVector3f(HW, 0.0f, -HH), FVector3f(HW, Th, -HH), FVector3f(-HW, Th, -HH)};
		Face(PaperGroup, P, FVector3f(0.0f, 0.0f, -1.0f), UV);
	}
	{
		const FVector3f P[4] = {FVector3f(HW, 0.0f, HH), FVector3f(HW, 0.0f, -HH), FVector3f(HW, Th, -HH), FVector3f(HW, Th, HH)};
		Face(PaperGroup, P, FVector3f(1.0f, 0.0f, 0.0f), UV);
	}
	{
		const FVector3f P[4] = {FVector3f(-HW, 0.0f, HH), FVector3f(-HW, 0.0f, -HH), FVector3f(-HW, Th, -HH), FVector3f(-HW, Th, HH)};
		Face(PaperGroup, P, FVector3f(-1.0f, 0.0f, 0.0f), UV);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), TEXT("SM_TorquePhotoPrint"));
	UMaterialInterface* Paper = PaperMaterial();
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Paper, PhotoSlot, PhotoSlot));
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Paper, PaperSlot, PaperSlot));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Paper, PhotoSlot));
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Paper, PaperSlot));
#endif
	for (FStaticMaterial& M : Mesh->GetStaticMaterials()) M.UVChannelData = FMeshUVChannelInfo(1.0f);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = true;   // (looked at and taken down by its box)
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque photos: the print's mesh wouldn't build"));
		return nullptr;
	}
	Mesh->AddToRoot();   // (made once, kept for the game)
	Made = Mesh;
	return Mesh;
}

UMaterialInterface* PaperMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Base;
	static TWeakObjectPtr<UMaterialInstanceDynamic> Paper;
	if (Paper.IsValid()) return Paper.Get();
	if (!Base.IsValid())
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedDecal.M_ShedDecal"));
		if (!Base.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque photos: no M_ShedDecal (run Tools/ue_build_shed.py): prints are drawn plain"));
			return nullptr;
		}
		Base->AddToRoot();
	}
	// Its own picture at full colour (Full), no picture given: white. Satin, as a print is.
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base.Get(), GetTransientPackage());
	if (!M) return Base.Get();
	M->SetScalarParameterValue(TEXT("Full"), 1.0f);
	M->SetScalarParameterValue(TEXT("Wear"), 0.0f);
	M->SetScalarParameterValue(TEXT("Roughness"), 0.55f);
	M->AddToRoot();
	Paper = M;
	return M;
}

UMaterialInterface* PrintMaterial(UObject* Outer, UTexture2D* Picture)
{
	UMaterialInterface* Paper = PaperMaterial();
	UMaterialInstanceDynamic* PaperDyn = Cast<UMaterialInstanceDynamic>(Paper);
	UMaterialInterface* Base = PaperDyn ? PaperDyn->Parent.Get() : Paper;
	if (!Base || !Picture) return Paper;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Outer);
	if (!M) return Paper;
	M->SetTextureParameterValue(TEXT("Decal"), Picture);
	M->SetScalarParameterValue(TEXT("Full"), 1.0f);
	M->SetScalarParameterValue(TEXT("Wear"), 0.0f);
	M->SetScalarParameterValue(TEXT("Roughness"), 0.32f);   // a glossy print
	return M;
}

// --- Filters ---------------------------------------------------------------------------------------------

namespace
{
const TCHAR* const FilterNames[] = {TEXT("Natural"), TEXT("Vivid"), TEXT("Black & white"), TEXT("Noir"), TEXT("Sepia"), TEXT("Vintage film"),
	TEXT("Golden"), TEXT("Teal & orange"), TEXT("Faded")};

FVector4 Grey(double V) { return FVector4(V, V, V, 1.0); }
}

int32 NumFilters() { return UE_ARRAY_COUNT(FilterNames); }

FString FilterName(int32 Filter) { return FilterNames[FMath::Clamp(Filter, 0, NumFilters() - 1)]; }

void ApplyFilter(int32 Filter, FPostProcessSettings& P)
{
	auto Sat = [&P](double V) { P.bOverride_ColorSaturation = true; P.ColorSaturation = Grey(V); };
	auto Con = [&P](double V) { P.bOverride_ColorContrast = true; P.ColorContrast = Grey(V); };
	auto Gain = [&P](double R, double G, double B) { P.bOverride_ColorGain = true; P.ColorGain = FVector4(R, G, B, 1.0); };
	auto Lift = [&P](double V) { P.bOverride_ColorOffset = true; P.ColorOffset = FVector4(V, V, V, 0.0); };
	auto Vignette = [&P](float V) { P.bOverride_VignetteIntensity = true; P.VignetteIntensity = V; };
	auto Grain = [&P](float V) { P.bOverride_FilmGrainIntensity = true; P.FilmGrainIntensity = V; };
	switch (Filter)
	{
	case 1:   // Vivid
		Sat(1.28);
		Con(1.08);
		break;
	case 2:   // Black & white
		Sat(0.0);
		Con(1.12);
		break;
	case 3:   // Noir: hard black and white, dark corners, grain
		Sat(0.0);
		Con(1.45);
		Vignette(0.85f);
		Grain(0.35f);
		break;
	case 4:   // Sepia: toned brown
		Sat(0.0);
		Con(1.05);
		Gain(1.12, 0.98, 0.74);
		Vignette(0.5f);
		break;
	case 5:   // Vintage film: softer, warm, lifted blacks, grain
		Sat(0.72);
		Con(0.9);
		Gain(1.06, 1.0, 0.86);
		Lift(0.025);
		Vignette(0.6f);
		Grain(0.45f);
		break;
	case 6:   // Golden: a warm evening light
		Sat(1.1);
		Gain(1.1, 1.0, 0.82);
		break;
	case 7:   // Teal & orange: cool shadows, warm highlights
		Sat(1.12);
		Con(1.06);
		P.bOverride_ColorGainShadows = true;
		P.ColorGainShadows = FVector4(0.86, 1.0, 1.1, 1.0);
		P.bOverride_ColorGainHighlights = true;
		P.ColorGainHighlights = FVector4(1.1, 1.0, 0.86, 1.0);
		break;
	case 8:   // Faded: flat and pale
		Sat(0.8);
		Con(0.82);
		Lift(0.04);
		break;
	default:  // Natural
		break;
	}
}
} // namespace TorquePhotos
