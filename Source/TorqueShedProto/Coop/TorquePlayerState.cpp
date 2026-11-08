#include "TorquePlayerState.h"

#include "TorqueSettings.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

void ATorquePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATorquePlayerState, Activity);
	DOREPLIFETIME(ATorquePlayerState, Colour);
	DOREPLIFETIME(ATorquePlayerState, Bay);
}

void ATorquePlayerState::CopyProperties(APlayerState* Into)
{
	// Seamless travel between the shed and the road keeps who rides which bike and their colour.
	Super::CopyProperties(Into);
	if (ATorquePlayerState* T = Cast<ATorquePlayerState>(Into))
	{
		T->Colour = Colour;
		T->Bay = Bay;
	}
}

void ATorquePlayerState::ServerSetActivity_Implementation(const FString& What)
{
	Activity = What.Left(80);
}

void ATorquePlayerState::ServerSetLook_Implementation(const FString& NewName, uint8 NewColour)
{
	Colour = (uint8)FMath::Clamp<int32>(NewColour, 0, FTorqueSettings::Colours().Num() - 1);
	const FString Clean = NewName.TrimStartAndEnd().Left(20);
	if (!Clean.IsEmpty() && Clean != GetPlayerName())
	{
		if (AGameModeBase* Mode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
			if (AController* C = GetOwningController()) Mode->ChangeName(C, Clean, false);
	}
}

void ATorquePlayerState::ServerSetBay_Implementation(int32 NewBay)
{
	Bay = FMath::Clamp(NewBay, -1, 63);
}

ATorquePlayerState* ATorquePlayerState::Mine(const UObject* WorldContext)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContext, 0);
	return PC ? PC->GetPlayerState<ATorquePlayerState>() : nullptr;
}

FColor ATorquePlayerState::ColourValue() const
{
	return FTorqueSettings::Colours()[FMath::Clamp<int32>(Colour, 0, FTorqueSettings::Colours().Num() - 1)];
}
