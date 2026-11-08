// The MetaHuman avatars on show, for checking them on one PC without a co-op game: started by
// -TorqueAvatarDemo, a row of them in front of you in the shed, each walking up and back (the second
// leg hurrying), turning, crouching, then standing working with their hands out in front, nodding
// up and down as they look about.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Shed/TorqueAvatar.h"
#include "TorqueAvatarDemo.generated.h"

UCLASS()
class ATorqueAvatarDemo : public AActor
{
	GENERATED_BODY()

public:
	ATorqueAvatarDemo();
	virtual ~ATorqueAvatarDemo() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FPuppet
	{
		TUniquePtr<FTorqueAvatar> Body;
		USceneComponent* Root = nullptr;
		FVector Start = FVector::ZeroVector;
		double Clock = 0.0;
	};
	TArray<FPuppet> Puppets;
};
