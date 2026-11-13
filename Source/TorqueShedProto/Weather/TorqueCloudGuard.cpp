#include "Weather/TorqueCloudGuard.h"

#include "Components/VolumetricCloudComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RHI.h"

namespace
{
// Still compiling or failed to compile, by whichever form this engine has (false when it has neither:
// the domain check still stands).
template <typename T>
bool CompileFailed(T* Mat, UWorld* World)
{
	if constexpr (requires(T* X, ERHIFeatureLevel::Type F) { X->IsCompilingOrHadCompileError(F); })
	{
		return Mat->IsCompilingOrHadCompileError(World->GetFeatureLevel());
	}
	else if constexpr (requires(T* X, EShaderPlatform P) { X->IsCompilingOrHadCompileError(P); })
	{
		return Mat->IsCompilingOrHadCompileError(GShaderPlatformForFeatureLevel[World->GetFeatureLevel()]);
	}
	else
	{
		return false;
	}
}

// Why a cloud material won't do (empty when it will).
FString WhyNot(UMaterialInterface* MatIf, UWorld* World)
{
	if (!MatIf) return TEXT("none");
	UMaterial* Mat = MatIf->GetMaterial();
	if (!Mat) return TEXT("no base material");
	if (Mat->MaterialDomain != MD_Volume) return FString::Printf(TEXT("domain %d, not Volume"), (int32)Mat->MaterialDomain.GetValue());
	// Without "Used with Volumetric Cloud" the cook has none of the cloud's shaders for it: the cloud
	// shadow pass falls back to the default surface material and asserts (round 142).
	if (!Mat->GetUsageByFlag(MATUSAGE_VolumetricCloud)) return TEXT("not marked Used with Volumetric Cloud");
	if (CompileFailed(Mat, World)) return TEXT("not compiled (no shaders, or an error)");
	return FString();
}

FObjectPropertyBase* MaterialProp(UVolumetricCloudComponent* Cloud)
{
	return CastField<FObjectPropertyBase>(Cloud->GetClass()->FindPropertyByName(FName(TEXT("Material"))));
}

TSet<TWeakObjectPtr<UVolumetricCloudComponent>> Checked;
} // namespace

bool TorqueCloudGuard::Check(UWorld* World)
{
	if (!World) return false;
	bool bOurs = false;
	const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("TorqueEngineCloud"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		UVolumetricCloudComponent* Cloud = It->FindComponentByClass<UVolumetricCloudComponent>();
		if (!Cloud) continue;
		FObjectPropertyBase* Prop = MaterialProp(Cloud);
		UMaterialInterface* Mat = Prop ? Cast<UMaterialInterface>(Prop->GetObjectPropertyValue_InContainer(Cloud)) : nullptr;
		const bool bRide = Mat && Mat->GetName().Contains(TEXT("M_RideCloud"));
		if (Checked.Contains(Cloud))
		{
			bOurs |= bRide && Cloud->IsVisible();
			continue;
		}
		Checked.Add(Cloud);
		const FString Why = bForce && bRide ? FString(TEXT("-TorqueEngineCloud")) : WhyNot(Mat, World);
		if (Why.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("Torque clouds: %s is a working Volume material"), *GetNameSafe(Mat));
			bOurs |= bRide;
			continue;
		}
		UE_LOG(LogTemp, Warning, TEXT("Torque clouds: %s won't do (%s): the engine's own cloud instead"), *GetNameSafe(Mat), *Why);
		UMaterialInterface* EngineMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
		const FString EngineWhy = WhyNot(EngineMat, World);
		if (Prop && EngineWhy.IsEmpty())
		{
			Prop->SetObjectPropertyValue_InContainer(Cloud, EngineMat);
			Cloud->MarkRenderStateDirty();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque clouds: the engine's cloud won't do either (%s): no clouds drawn"), *EngineWhy);
			Cloud->SetHiddenInGame(true);
			Cloud->SetVisibility(false);
		}
	}
	return bOurs;
}

void UTorqueCloudGuard::OnWorldComponentsUpdated(UWorld& World)
{
	Super::OnWorldComponentsUpdated(World);
	TorqueCloudGuard::Check(&World);
}

void UTorqueCloudGuard::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	TorqueCloudGuard::Check(&World);
}

bool UTorqueCloudGuard::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
