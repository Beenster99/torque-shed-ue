#include "Shed/TorqueEffectPaint.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace TorqueEffectPaintImpl
{
UMaterialInterface* Base()
{
	static UMaterialInterface* M = nullptr;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedEffectPaint.M_ShedEffectPaint"));
		if (M) M->AddToRoot();   // kept across map travel
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedEffectPaint (run Tools/ue_build_shed.py): effect paints show as plain colours"));
		}
	}
	return M;
}

// What each part wore before the effect paint went on, to put back (held, as nothing else does).
TMap<TWeakObjectPtr<UMeshComponent>, TSharedPtr<TStrongObjectPtr<UMaterialInterface>>>& Kept()
{
	static TMap<TWeakObjectPtr<UMeshComponent>, TSharedPtr<TStrongObjectPtr<UMaterialInterface>>> L;
	return L;
}

void Tidy()
{
	for (auto It = Kept().CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
}
}

namespace TEPI = TorqueEffectPaintImpl;

namespace TorqueEffectPaint
{
bool Apply(UMeshComponent* C, const TorquePaintMix::FPaintSpec& S, float Grime)
{
	UMaterialInterface* Base = TEPI::Base();
	if (!C || !Base) return false;
	UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
	if (!M || M->Parent != Base)
	{
		TEPI::Tidy();
		if (!TEPI::Kept().Contains(C)) TEPI::Kept().Add(C, MakeShared<TStrongObjectPtr<UMaterialInterface>>(C->GetMaterial(0)));
		M = UMaterialInstanceDynamic::Create(Base, C);
		C->SetMaterial(0, M);
	}
	M->SetVectorParameterValue(TEXT("Color"), S.Colour);
	M->SetScalarParameterValue(TEXT("Metallic"), S.Metal);
	M->SetScalarParameterValue(TEXT("Roughness"), S.Rough);
	M->SetScalarParameterValue(TEXT("Flip"), S.Flip);
	M->SetVectorParameterValue(TEXT("FlipA"), S.FlipA);
	M->SetVectorParameterValue(TEXT("FlipB"), S.FlipB);
	M->SetScalarParameterValue(TEXT("Pearl"), S.Pearl);
	M->SetVectorParameterValue(TEXT("PearlTint"), S.PearlTint);
	M->SetScalarParameterValue(TEXT("Depth"), FMath::Clamp(S.Depth, 0.0f, 1.5f));
	M->SetScalarParameterValue(TEXT("Flake"), S.Flake);
	M->SetVectorParameterValue(TEXT("FlakeColour"), S.FlakeColour);
	M->SetScalarParameterValue(TEXT("Grime"), Grime);
	return true;
}

void Restore(UMeshComponent* C)
{
	if (!C) return;
	TSharedPtr<TStrongObjectPtr<UMaterialInterface>> Was;
	if (!TEPI::Kept().RemoveAndCopyValue(C, Was) || !Was.IsValid()) return;
	C->SetMaterial(0, Was->Get());
}
}
