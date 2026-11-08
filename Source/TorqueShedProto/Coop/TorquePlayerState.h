// Each player in co-op, as everyone sees them: name (Unreal's own), the colour they ride in, and
// what they're doing in the shed ("fitting clubman bars to bay 2"), drawn over them by ATorqueHUD.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TorquePlayerState.generated.h"

UCLASS()
class ATorquePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* Into) override;

	UPROPERTY(Replicated) FString Activity;
	UPROPERTY(Replicated) uint8 Colour = 0;
	// The shed bay this player last worked on or claimed (-1 none): which bike they ride out on.
	UPROPERTY(Replicated) int32 Bay = -1;

	// From the owning player's game; the host passes it on to everyone.
	UFUNCTION(Server, Reliable) void ServerSetActivity(const FString& What);
	UFUNCTION(Server, Reliable) void ServerSetLook(const FString& NewName, uint8 NewColour);
	UFUNCTION(Server, Reliable) void ServerSetBay(int32 NewBay);

	// This player's own state, from any of their objects.
	static ATorquePlayerState* Mine(const UObject* WorldContext);
	FColor ColourValue() const;
};
