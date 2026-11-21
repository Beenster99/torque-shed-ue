#include "TorqueLeafFall.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

static TAutoConsoleVariable<float> CVarTreeLeafFallM(
	TEXT("torque.TreeLeafFallM"), 200.0f,
	TEXT("While leaves are down (autumn, winter, early spring), our broadleaves keep their fallen-leaf offset out to this many metres ")
	TEXT("(0: only as far as the wind's sway, as in 164)."));
static TAutoConsoleVariable<int32> CVarTreeSeasonModels(
	TEXT("torque.TreeSeasonModels"), 1,
	TEXT("Our broadleaves drawn as their half-bare or bare models (SM_Tree_<kind>_<n>_half/_bare) while leaves are down (1, build 167), ")
	TEXT("or always in full leaf, their fallen leaves only moved away (0, as in 164-166)."));

namespace
{
struct FLeafy
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Ism;
	TWeakObjectPtr<UStaticMesh> Full;   // (its model in full leaf)
	int32 BaseCm = 0;
};
TArray<FLeafy>& Registered()
{
	static TArray<FLeafy> List;
	return List;
}
float DropNow = 0.0f;
int32 ReachApplied = -1;   // (the reach last applied, cm: 0 while no leaves are down)
int32 BandApplied = -1;    // (the season model last applied: 0 full, 1 half, 2 bare)

int32 Reach()
{
	if (DropNow <= 0.02f) return 0;
	return int32(FMath::Clamp(CVarTreeLeafFallM.GetValueOnGameThread(), 0.0f, 2000.0f) * 100.0f);
}

// [build 167] Round 166 at Whaley Bridge, winter forced: no difference with the offset out to 200 m or not,
// and "the near crowns still look like faceted blobs". Past a few tens of metres Nanite draws a crown from
// simplified clusters, whose triangles span many leaves, so a fallen leaf can't be moved away there. So
// while leaves are down a broadleaf is drawn as its model with those leaves taken out (Tools/make_trees.py
// SEASON_CUTS): all of them gone from a drop of 0.9 (late November to the end of March); before that the
// leaves are only moved away, as in 166.
int32 Band()
{
	if (CVarTreeSeasonModels.GetValueOnGameThread() == 0) return 0;
	return DropNow >= 0.9f ? 2 : 0;   // (no half-bare models made yet: Tools/make_trees.py SEASON_CUTS)
}

const TCHAR* const Suffix[3] = {TEXT(""), TEXT("_half"), TEXT("_bare")};

// The model in full leaf of a model (itself, or the one it is the half-bare or bare model of).
UStaticMesh* FullOf(UStaticMesh* Mesh)
{
	if (!Mesh) return nullptr;
	const FString Name = Mesh->GetName();
	for (int32 B = 1; B < 3; ++B)
		if (Name.EndsWith(Suffix[B]))
		{
			const FString Base = Name.LeftChop(FCString::Strlen(Suffix[B]));
			const FString Path = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName()) / Base + TEXT(".") + Base;
			if (UStaticMesh* Full = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)))
				return Full;
			return Mesh;
		}
	return Mesh;
}

// A full model's season model for a band (looked for once, beside it; the full one when there is none:
// the evergreens, and models made before 167).
UStaticMesh* SeasonModel(UStaticMesh* Full, int32 B)
{
	if (!Full || B <= 0) return Full;
	struct FFound { TWeakObjectPtr<UStaticMesh> M[3]; bool bLooked[3] = {false, false, false}; };
	static TMap<TWeakObjectPtr<UStaticMesh>, FFound> Found;
	FFound& F = Found.FindOrAdd(Full);
	if (!F.bLooked[B])
	{
		F.bLooked[B] = true;
		const FString Name = Full->GetName() + Suffix[B];
		const FString Path = FPackageName::GetLongPackagePath(Full->GetOutermost()->GetName()) / Name + TEXT(".") + Name;
		F.M[B] = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	UStaticMesh* Got = F.M[B].Get();
	return Got ? Got : Full;
}

void Set(UInstancedStaticMeshComponent* Ism, int32 BaseCm, int32 ReachCm)
{
	// (0 is "never off" for this setting, so a base of 0 stays 0)
	const int32 Want = BaseCm <= 0 ? BaseCm : FMath::Max(BaseCm, ReachCm);
	if (Ism->WorldPositionOffsetDisableDistance == Want) return;
	Ism->WorldPositionOffsetDisableDistance = Want;
	if (Ism->IsRegistered()) Ism->MarkRenderStateDirty();
}

void SetModel(UInstancedStaticMeshComponent* Ism, UStaticMesh* Full, int32 B)
{
	UStaticMesh* Want = SeasonModel(Full, B);
	if (Want && Ism->GetStaticMesh() != Want) Ism->SetStaticMesh(Want);
}
}

namespace TorqueLeafFall
{
void Apply(UInstancedStaticMeshComponent* Ism, int32 BaseCm, bool bLeafFall)
{
	if (!Ism) return;
	TArray<FLeafy>& List = Registered();
	const int32 At = List.IndexOfByPredicate([Ism](const FLeafy& L) { return L.Ism.Get() == Ism; });
	if (!bLeafFall)
	{
		if (At != INDEX_NONE) List.RemoveAtSwap(At);
		Ism->WorldPositionOffsetDisableDistance = BaseCm;
		return;
	}
	UStaticMesh* Full = FullOf(Ism->GetStaticMesh());
	if (At != INDEX_NONE)
	{
		List[At].BaseCm = BaseCm;
		List[At].Full = Full;
	}
	else
	{
		// (now and then, the ones gone with their squares or levels)
		if (List.Num() > 0 && List.Num() % 256 == 0) List.RemoveAllSwap([](const FLeafy& L) { return !L.Ism.IsValid(); });
		List.Add(FLeafy{Ism, Full, BaseCm});
	}
	Ism->WorldPositionOffsetDisableDistance = BaseCm;
	Set(Ism, BaseCm, Reach());
	SetModel(Ism, Full, Band());
}

void SetDrop(float Drop)
{
	DropNow = Drop;
	const int32 R = Reach();
	const int32 B = Band();
	if (R == ReachApplied && B == BandApplied) return;
	ReachApplied = R;
	BandApplied = B;
	TArray<FLeafy>& List = Registered();
	List.RemoveAllSwap([](const FLeafy& L) { return !L.Ism.IsValid(); });
	for (const FLeafy& L : List)
	{
		Set(L.Ism.Get(), L.BaseCm, R);
		SetModel(L.Ism.Get(), L.Full.Get(), B);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque trees: fallen leaves drawn out to %d m, %s models, on %d leafy tree parts (drop %.2f)"),
		R / 100, B == 2 ? TEXT("bare") : B == 1 ? TEXT("half-bare") : TEXT("full"), List.Num(), Drop);
}

bool IsLeafyTreeName(const FString& Name)
{
	if (Name.Contains(TEXT("TreeFar")) || Name.Contains(TEXT("_imp")) || Name.Contains(TEXT("_scan"))) return false;
	if (!Name.Contains(TEXT("SM_Tree_"))) return false;
	for (const TCHAR* Evergreen : {TEXT("spruce"), TEXT("pine"), TEXT("holly"), TEXT("hedge")})
		if (Name.Contains(Evergreen)) return false;
	return true;
}

bool IsSeasonModelName(const FString& Name)
{
	return Name.EndsWith(TEXT("_half")) || Name.EndsWith(TEXT("_bare"));
}
}
