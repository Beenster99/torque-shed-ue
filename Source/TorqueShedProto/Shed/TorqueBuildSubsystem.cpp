#include "Shed/TorqueBuildSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

FName UTorqueBuildSubsystem::FittedIn(FName Slot) const
{
	if (const FName* F = Build.Fitted.Find(Slot)) return *F;
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Build.Bike, Slot);
	for (const FTorqueFitOption& O : Opts)
		if (O.bStock) return O.PartId;
	return Opts.Num() > 0 ? Opts[0].PartId : NAME_None;
}

bool UTorqueBuildSubsystem::Fit(FName Slot, FName PartId, FText& WhyNot)
{
	if (!TorqueFit::CanFit(Build, Slot, PartId, WhyNot)) return false;
	Build.Fitted.Add(Slot, PartId);
	UE_LOG(LogTemp, Log, TEXT("Torque: fitted %s in %s"), *PartId.ToString(), *Slot.ToString());
	OnChanged.Broadcast();
	return true;
}

FName UTorqueBuildSubsystem::NextOption(FName Slot) const
{
	const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Build.Bike, Slot);
	if (Opts.Num() == 0) return NAME_None;
	const FName Now = FittedIn(Slot);
	int32 At = 0;
	for (int32 I = 0; I < Opts.Num(); ++I)
		if (Opts[I].PartId == Now) At = I;
	return Opts[(At + 1) % Opts.Num()].PartId;
}

UTorqueBuildSubsystem* UTorqueBuildSubsystem::Of(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UTorqueBuildSubsystem>() : nullptr;
}

const FTorqueBuild& UTorqueBuildSubsystem::BuildFor(const UObject* WorldContext)
{
	static const FTorqueBuild Default;
	const UTorqueBuildSubsystem* S = Of(WorldContext);
	return S ? S->Build : Default;
}
