#include "TorqueLeafFall.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarTreeLeafFallM(
	TEXT("torque.TreeLeafFallM"), 200.0f,
	TEXT("While leaves are down (autumn, winter, early spring), our broadleaves keep their fallen-leaf offset out to this many metres ")
	TEXT("(0: only as far as the wind's sway, as in 164)."));

namespace
{
struct FLeafy
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Ism;
	int32 BaseCm = 0;
};
TArray<FLeafy>& Registered()
{
	static TArray<FLeafy> List;
	return List;
}
float DropNow = 0.0f;
int32 ReachApplied = -1;   // (the reach last applied, cm: 0 while no leaves are down)

int32 Reach()
{
	if (DropNow <= 0.02f) return 0;
	return int32(FMath::Clamp(CVarTreeLeafFallM.GetValueOnGameThread(), 0.0f, 2000.0f) * 100.0f);
}

void Set(UInstancedStaticMeshComponent* Ism, int32 BaseCm, int32 ReachCm)
{
	// (0 is "never off" for this setting, so a base of 0 stays 0)
	const int32 Want = BaseCm <= 0 ? BaseCm : FMath::Max(BaseCm, ReachCm);
	if (Ism->WorldPositionOffsetDisableDistance == Want) return;
	Ism->WorldPositionOffsetDisableDistance = Want;
	if (Ism->IsRegistered()) Ism->MarkRenderStateDirty();
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
	if (At != INDEX_NONE) List[At].BaseCm = BaseCm;
	else
	{
		// (now and then, the ones gone with their squares or levels)
		if (List.Num() > 0 && List.Num() % 256 == 0) List.RemoveAllSwap([](const FLeafy& L) { return !L.Ism.IsValid(); });
		List.Add(FLeafy{Ism, BaseCm});
	}
	Ism->WorldPositionOffsetDisableDistance = BaseCm;
	Set(Ism, BaseCm, Reach());
}

void SetDrop(float Drop)
{
	DropNow = Drop;
	const int32 R = Reach();
	if (R == ReachApplied) return;
	ReachApplied = R;
	TArray<FLeafy>& List = Registered();
	List.RemoveAllSwap([](const FLeafy& L) { return !L.Ism.IsValid(); });
	for (const FLeafy& L : List) Set(L.Ism.Get(), L.BaseCm, R);
	UE_LOG(LogTemp, Log, TEXT("Torque trees: fallen leaves drawn out to %d m on %d leafy tree parts (drop %.2f)"), R / 100, List.Num(), Drop);
}

bool IsLeafyTreeName(const FString& Name)
{
	if (Name.Contains(TEXT("TreeFar")) || Name.Contains(TEXT("_imp")) || Name.Contains(TEXT("_scan"))) return false;
	if (!Name.Contains(TEXT("SM_Tree_"))) return false;
	for (const TCHAR* Evergreen : {TEXT("spruce"), TEXT("pine"), TEXT("holly"), TEXT("hedge")})
		if (Name.Contains(Evergreen)) return false;
	return true;
}
}
