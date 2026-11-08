#include "TorqueFit.h"

#include "Fitment.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <string>

namespace
{
using TorqueFit::Xf;
using TorqueSim::M3;
using TorqueSim::V3;

// One bike's data, loaded once.
struct FBikeData
{
	bool bTried = false, bOk = false;
	FString Folder;
	TorqueFit::BikeFit Fit;
	TorqueFit::Assembly Stock;
	TSharedPtr<FJsonObject> Assets;   // ue_assets.json "parts": mesh and material per model part
};

TMap<FName, TSharedPtr<FBikeData>>& Cache()
{
	static TMap<FName, TSharedPtr<FBikeData>> Bikes;
	return Bikes;
}

FString FolderFor(FName Bike)
{
	if (Bike == FName(TEXT("speed_twin_1200"))) return TEXT("SpeedTwin");
	return Bike.ToString();
}

bool ReadText(const FString& Rel, std::string& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::ProjectDir(), Rel))) return false;
	Out.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	return true;
}

std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
std::string ToStd(FName N) { return ToStd(N.ToString()); }
FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
FName ToName(const std::string& S) { return S.empty() ? NAME_None : FName(*ToF(S)); }

FBikeData* Get(FName Bike)
{
	TSharedPtr<FBikeData>& D = Cache().FindOrAdd(Bike);
	if (!D.IsValid()) D = MakeShared<FBikeData>();
	if (D->bTried) return D->bOk ? D.Get() : nullptr;
	D->bTried = true;
	D->Folder = FolderFor(Bike);
	std::string FitText, Types, Error;
	const FString Dir = FString(TEXT("Data/Bike/")) + D->Folder + TEXT("/");
	if (!ReadText(Dir + TEXT("fitment.json"), FitText) || !ReadText(TEXT("Data/Fitment/interfaces.json"), Types))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: no fitment data for %s (run Tools/make_speedtwin_fit.py)"), *Bike.ToString());
		return nullptr;
	}
	if (!D->Fit.Load(FitText, Types, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: %s"), *ToF(Error));
		return nullptr;
	}
	std::string Fixes;
	if (ReadText(Dir + TEXT("fitment_fixes.json"), Fixes) && !D->Fit.ApplyFixes(Fixes, Error))
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: author fixes not applied: %s"), *ToF(Error));
	if (!D->Fit.Assemble(TorqueFit::Build(), D->Stock, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: the stock %s doesn't assemble: %s"), *Bike.ToString(), *ToF(Error));
		return nullptr;
	}
	FString AssetText;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(AssetText, *FPaths::Combine(FPaths::ProjectDir(), Dir + TEXT("ue_assets.json"))) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AssetText), Root) && Root.IsValid())
	{
		const TSharedPtr<FJsonObject>* Parts = nullptr;
		if (Root->TryGetObjectField(TEXT("parts"), Parts)) D->Assets = *Parts;
	}
	D->bOk = true;
	return D.Get();
}

// The model frame (metres, x forward, y up, z right) and the pawn's local frame (cm, x forward,
// y right, z up) differ by swapping y and z, and by 100.
FVector ToLocal(const V3& P) { return FVector(P.x * 100.0, P.z * 100.0, P.y * 100.0); }
FVector DirToLocal(const V3& D) { return FVector(D.x, D.z, D.y); }
V3 FromLocal(const FVector& P) { return V3(P.X / 100.0, P.Z / 100.0, P.Y / 100.0); }
V3 DirFromLocal(const FVector& D) { return V3(D.X, D.Z, D.Y); }

FTransform XfToLocal(const Xf& X)
{
	// Columns of the swapped rotation: the images of local x, y (model z) and z (model y).
	const FVector C0 = DirToLocal(X.R.X), C1 = DirToLocal(X.R.Z), C2 = DirToLocal(X.R.Y);
	const FMatrix M(FPlane(C0, 0.0), FPlane(C1, 0.0), FPlane(C2, 0.0), FPlane(ToLocal(X.T), 1.0));
	return FTransform(M);
}

Xf XfFromLocal(const FTransform& T)
{
	const FMatrix M = T.ToMatrixNoScale();
	const FVector C0 = M.GetScaledAxis(EAxis::X), C1 = M.GetScaledAxis(EAxis::Y), C2 = M.GetScaledAxis(EAxis::Z);
	Xf X;
	X.R = M3(DirFromLocal(C0), DirFromLocal(C2), DirFromLocal(C1));
	X.T = FromLocal(T.GetLocation());
	return X;
}

TorqueFit::Build ToBuild(const FTorqueBuild& B)
{
	TorqueFit::Build Out;
	for (const TPair<FName, FName>& KV : B.Fitted) Out.Fitted[ToStd(KV.Key)] = ToStd(KV.Value);
	for (const TPair<FName, FTorqueFitAdjust>& KV : B.Adjust)
	{
		TorqueFit::Adjust A;
		A.Slide = KV.Value.Slide;
		A.TurnDeg = KV.Value.TurnDeg;
		A.Hole = KV.Value.Hole;
		A.Free = KV.Value.bFree;
		A.FreeXf = XfFromLocal(KV.Value.Free);
		Out.Adjusts[ToStd(KV.Key)] = A;
	}
	return Out;
}

void MeshAssets(const FBikeData& D, const std::string& Mesh, FString& MeshPath, FString& MaterialPath)
{
	MeshPath.Reset();
	MaterialPath.Reset();
	const TSharedPtr<FJsonObject>* Where = nullptr;
	if (!D.Assets.IsValid() || !D.Assets->TryGetObjectField(ToF(Mesh), Where)) return;
	(*Where)->TryGetStringField(TEXT("mesh"), MeshPath);
	(*Where)->TryGetStringField(TEXT("material"), MaterialPath);
}

FTorqueFitMesh FitMesh(const FBikeData& D, const std::string& Mesh, const std::string& Group)
{
	FTorqueFitMesh M;
	M.ModelPart = ToName(Mesh);
	M.Group = ToName(Group);
	MeshAssets(D, Mesh, M.Mesh, M.Material);
	return M;
}
} // namespace

namespace TorqueFit
{
TArray<FName> Slots(FName Bike)
{
	TArray<FName> Out;
	if (FBikeData* D = Get(Bike))
		for (const std::string& S : D->Fit.Slots()) Out.Add(ToName(S));
	return Out;
}

TArray<FTorqueFitOption> Options(FName Bike, FName Slot)
{
	TArray<FTorqueFitOption> Out;
	FBikeData* D = Get(Bike);
	if (!D) return Out;
	const std::vector<Option>* List = D->Fit.OptionsFor(ToStd(Slot));
	if (!List) return Out;
	for (const Option& O : *List)
	{
		FTorqueFitOption F;
		F.PartId = ToName(O.Id);
		F.Name = FText::FromString(ToF(O.Name));
		F.bStock = O.Stock;
		F.CatalogueKind = ToName(O.CatKind);
		F.CatalogueId = ToName(O.CatId);
		if (O.Stock)
		{
			// The stock parts are the stock units of this slot.
			for (const Unit& U : D->Fit.StockUnits())
				if (U.Slot == O.Slot)
					for (const std::string& M : U.Meshes) F.Meshes.Add(FitMesh(*D, M, U.Group));
		}
		else
		{
			for (const Unit& U : O.Units)
			{
				for (const Unit& S : D->Fit.StockUnits())
					if (S.Id == U.Id)
						for (const std::string& M : S.Meshes) F.ReplacesModelParts.Add(ToName(M));
				for (const std::string& M : U.Meshes) F.Meshes.Add(FitMesh(*D, M, U.Group));
			}
		}
		Out.Add(F);
	}
	return Out;
}

bool CanFit(const FTorqueBuild& InBuild, FName Slot, FName PartId, FText& WhyNot)
{
	FBikeData* D = Get(InBuild.Bike);
	if (!D)
	{
		WhyNot = FText::FromString(TEXT("no fitment data for this bike"));
		return false;
	}
	std::string Why;
	const bool Ok = D->Fit.CanFit(ToBuild(InBuild), ToStd(Slot), ToStd(PartId), Why);
	WhyNot = FText::FromString(ToF(Why));
	return Ok;
}

bool ResolveModel(const FTorqueBuild& InBuild, TArray<FTorqueResolvedPart>& Out, TArray<FTorqueFitClash>* Clashes)
{
	Out.Reset();
	if (Clashes) Clashes->Reset();
	FBikeData* D = Get(InBuild.Bike);
	if (!D) return false;
	if (!D->Assets.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: no ue_assets.json for %s yet (run Tools/ue_build_level.py)"), *InBuild.Bike.ToString());
		return false;
	}
	Assembly A;
	std::string Error;
	if (!D->Fit.Assemble(ToBuild(InBuild), A, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque fit: %s"), *ToF(Error));
		return false;
	}
	for (const std::string& N : A.Notes) UE_LOG(LogTemp, Log, TEXT("Torque fit: %s"), *ToF(N));
	for (const Placed& P : A.Units)
	{
		const V3 To = D->Fit.GroupOrigin(P.U.Group);
		for (const std::string& M : P.U.Meshes)
		{
			const MeshInfo* Info = D->Fit.FindMesh(M);
			const V3 From = D->Fit.GroupOrigin(Info ? Info->Group : P.U.Group);
			// Its vertices are relative to the group it was modelled in; it hangs from the unit's group.
			const Xf Delta = Xf::Translate(-To) * P.World * Xf::Translate(From);
			FTorqueResolvedPart R;
			R.Name = ToName(M);
			R.Slot = ToName(P.U.Slot);
			R.Unit = ToName(P.U.Id);
			R.Group = ToName(P.U.Group);
			R.Delta = XfToLocal(Delta);
			MeshAssets(*D, M, R.Mesh, R.Material);
			if (R.Mesh.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Torque fit: no imported mesh for %s (re-run Tools/ue_build_level.py)"), *R.Name.ToString());
				continue;
			}
			Out.Add(R);
		}
	}
	if (Clashes)
	{
		for (const Clash& C : D->Fit.Clearance(A, &D->Stock))
		{
			FTorqueFitClash F;
			F.A = ToName(C.A);
			F.B = ToName(C.B);
			F.Case = ToF(C.Case);
			F.DepthMm = C.DepthMm;
			Clashes->Add(F);
		}
	}
	return Out.Num() > 0;
}

FTorqueFitAdjust ConstrainDrag(const FTorqueBuild& InBuild, FName UnitName, const FVector& DragCm, double TurnDeg)
{
	const FTorqueFitAdjust* Have = InBuild.Adjust.Find(UnitName);
	FTorqueFitAdjust Result = Have ? *Have : FTorqueFitAdjust();
	FBikeData* D = Get(InBuild.Bike);
	if (!D) return Result;
	Assembly A;
	std::string Error;
	const TorqueFit::Build B = ToBuild(InBuild);
	if (!D->Fit.Assemble(B, A, Error)) return Result;
	auto It = B.Adjusts.find(ToStd(UnitName));
	const Adjust Now = It != B.Adjusts.end() ? It->second : Adjust();
	const Adjust New = D->Fit.ConstrainDrag(A, ToStd(UnitName), Now, FromLocal(DragCm), TurnDeg);
	Result.Slide = New.Slide;
	Result.TurnDeg = New.TurnDeg;
	Result.Hole = New.Hole;
	return Result;
}

bool AuthorFix(const FTorqueBuild& InBuild, FName UnitName, FString& Error)
{
	FBikeData* D = Get(InBuild.Bike);
	if (!D) { Error = TEXT("no fitment data for this bike"); return false; }
	Assembly A;
	std::string Err;
	if (!D->Fit.Assemble(ToBuild(InBuild), A, Err)) { Error = ToF(Err); return false; }
	Json Fix;
	if (!D->Fit.AuthorFix(A, ToStd(UnitName), Fix, Err)) { Error = ToF(Err); return false; }
	// Merge into fitment_fixes.json: one fix per unit and option.
	const FString Rel = FString(TEXT("Data/Bike/")) + D->Folder + TEXT("/fitment_fixes.json");
	std::string Text;
	Json File;
	if (!ReadText(Rel, Text) || !Json::Parse(Text, File, &Err) || !File.IsObject()) File = Json::MakeObject();
	Json List = Json::MakeArray();
	if (const Json* Old = File.Get("fixes"))
		for (const Json& F : Old->A)
			if (!(F.Str("unit") == Fix.Str("unit") && F.Str("option") == Fix.Str("option"))) List.Push(F);
	List.Push(Fix);
	File.Set("about", Json::MakeString("Author fixes from geometry mode: each replaces a unit's mount frame (per option). Loaded on top of fitment.json."));
	File.Set("fixes", List);
	const std::string Out = File.Dump(1) + "\n";
	if (!FFileHelper::SaveStringToFile(ToF(Out), *FPaths::Combine(FPaths::ProjectDir(), Rel), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Error = TEXT("couldn't write ") + Rel;
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque fit: saved %s's fit to %s"), *UnitName.ToString(), *Rel);
	Reload();
	return true;
}

FTorqueBuild BuildFromCommandLine()
{
	FTorqueBuild B;
	FString Spec;
	if (!FParse::Value(FCommandLine::Get(), TEXT("TorqueBuild="), Spec, false)) return B;
	TArray<FString> Items;
	Spec.ParseIntoArray(Items, TEXT(","));
	for (const FString& Item : Items)
	{
		FString SlotPart, OptionPart;
		if (Item.Split(TEXT(":"), &SlotPart, &OptionPart)) B.Fitted.Add(FName(*SlotPart.TrimStartAndEnd()), FName(*OptionPart.TrimStartAndEnd()));
	}
	return B;
}

void Reload()
{
	Cache().Reset();
}
} // namespace TorqueFit
