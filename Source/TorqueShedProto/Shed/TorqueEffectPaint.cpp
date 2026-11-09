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
bool Apply(UMeshComponent* C, const TorquePaintMix::FPaintLook& L, float Grime, const FTransform& LocalToPart)
{
	UMaterialInterface* Mat = TEPI::Base();
	if (!C || !Mat) return false;
	UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
	if (!M || M->Parent != Mat)
	{
		TEPI::Tidy();
		if (!TEPI::Kept().Contains(C)) TEPI::Kept().Add(C, MakeShared<TStrongObjectPtr<UMaterialInterface>>(C->GetMaterial(0)));
		M = UMaterialInstanceDynamic::Create(Mat, C);
		C->SetMaterial(0, M);
	}
	const TorquePaintMix::FPaintSpec& S = L.Spec;
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
	// The mask: the pattern in the part's frame (paint_mix.gd masked()), its colour, and the tape.
	M->SetScalarParameterValue(TEXT("Pattern"), (float)L.Pattern);
	M->SetVectorParameterValue(TEXT("MaskColor"), L.MaskColour);
	M->SetScalarParameterValue(TEXT("MaskMetal"), L.MaskMetal);
	M->SetScalarParameterValue(TEXT("MaskRough"), L.MaskRough);
	M->SetScalarParameterValue(TEXT("Tape"), L.bTape ? 1.0f : 0.0f);
	M->SetVectorParameterValue(TEXT("TapeColor"), L.TapeColour);
	const FVector X = LocalToPart.TransformVector(FVector::XAxisVector), Y = LocalToPart.TransformVector(FVector::YAxisVector),
		Z = LocalToPart.TransformVector(FVector::ZAxisVector), T = LocalToPart.GetLocation();
	M->SetVectorParameterValue(TEXT("PartRow0"), FLinearColor((float)X.X, (float)Y.X, (float)Z.X, 0.0f));
	M->SetVectorParameterValue(TEXT("PartRow1"), FLinearColor((float)X.Y, (float)Y.Y, (float)Z.Y, 0.0f));
	M->SetVectorParameterValue(TEXT("PartRow2"), FLinearColor((float)X.Z, (float)Y.Z, (float)Z.Z, 0.0f));
	M->SetVectorParameterValue(TEXT("PartOffset"), FLinearColor((float)T.X, (float)T.Y, (float)T.Z, 0.0f));
	return true;
}

void Restore(UMeshComponent* C)
{
	if (!C) return;
	TSharedPtr<TStrongObjectPtr<UMaterialInterface>> Was;
	if (!TEPI::Kept().RemoveAndCopyValue(C, Was) || !Was.IsValid()) return;
	C->SetMaterial(0, Was->Get());
}

FBox BoxIn(const UMeshComponent* C, const FTransform& Frame)
{
	if (!C) return FBox(ForceInit);
	return C->CalcBounds(C->GetComponentTransform().GetRelativeTransform(Frame)).GetBox();
}

FTransform PartFrame(const UMeshComponent* C, const FTransform& Frame, const FVector& Middle)
{
	if (!C) return FTransform::Identity;
	return C->GetComponentTransform().GetRelativeTransform(Frame) * FTransform(-Middle);
}
}
