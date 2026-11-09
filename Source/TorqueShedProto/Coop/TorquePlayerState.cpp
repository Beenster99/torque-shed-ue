#include "TorquePlayerState.h"

#include "HandsState.h"
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
	DOREPLIFETIME(ATorquePlayerState, RiderStyle);
	DOREPLIFETIME(ATorquePlayerState, Bay);
	DOREPLIFETIME_CONDITION(ATorquePlayerState, Hands, COND_SkipOwner);
}

void ATorquePlayerState::CopyProperties(APlayerState* Into)
{
	// Seamless travel between the shed and the road keeps who rides which bike and their colour.
	Super::CopyProperties(Into);
	if (ATorquePlayerState* T = Cast<ATorquePlayerState>(Into))
	{
		T->Colour = Colour;
		T->RiderStyle = RiderStyle;
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

void ATorquePlayerState::ServerSetRiderStyle_Implementation(uint8 NewStyle)
{
	RiderStyle = (uint8)FMath::Clamp<int32>(NewStyle, 0, FTorqueSettings::RiderStyles().Num() - 1);
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

namespace
{
TorqueHands::FSendState SendStateOf(const FTorqueHandsNet& H)
{
	TorqueHands::FSendState S;
	S.Key = TorqueHands::MakeKey(H.Tool, H.Colour, H.Finish, H.Look, H.Bay, GetTypeHash(H.Slot), H.Item, GetTypeHash(H.Place), H.bAway);
	S.Work = H.Work;
	return S;
}
} // namespace

void ATorquePlayerState::OfferHands(const FTorqueHandsNet& Now, float Dt)
{
	Hands = Now;
	SinceHands += Dt;
	if (!TorqueHands::ShouldSend(SendStateOf(SentHands), SendStateOf(Now), SinceHands, bHandsSent)) return;
	SentHands = Now;
	SinceHands = 0.0;
	bHandsSent = true;
	if (HasAuthority())
	{
		ForceNetUpdate();   // the host's own: straight out
	}
	else
	{
		ServerSetHands(Now);
	}
}

void ATorquePlayerState::ServerSetHands_Implementation(const FTorqueHandsNet& NewHands)
{
	Hands = NewHands;
	Hands.Work = (uint8)FMath::Min<int32>(Hands.Work, 16);
	ForceNetUpdate();
}

void ATorquePlayerState::OnRep_Hands(const FTorqueHandsNet& Before)
{
	// For the two-copy tests: another player's tool and starting or stopping work, in the log.
	if (Hands.Tool != Before.Tool || (Hands.Work > 0) != (Before.Work > 0) || Hands.bAway != Before.bAway)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque net: hands: %s holds tool %d%s%s (aimed at %d bay %d %s item %d)"), *GetPlayerName(), Hands.Tool,
			Hands.Work > 0 ? TEXT(", working") : TEXT(""), Hands.bAway ? TEXT(", away") : TEXT(""), Hands.Look, Hands.Bay,
			*Hands.Slot.ToString(), Hands.Item);
	}
}
