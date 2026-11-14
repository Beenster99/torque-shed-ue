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

	/** For test shots (look:avatars): every puppet stands still at its start, turned round to face
	 *  back the way the row was spawned from, so a camera in front sees faces, fronts, legs and feet. */
	void Hold(bool bOn) { bHold = bOn; }
	/** Where puppet I (0 is A) stands, in the world; false when there's no such puppet. */
	bool PuppetAt(int32 I, FVector& Out) const;
	int32 PuppetCount() const { return Puppets.Num(); }

private:
	struct FPuppet
	{
		TUniquePtr<FTorqueAvatar> Body;
		USceneComponent* Root = nullptr;
		FVector Start = FVector::ZeroVector;
		double Clock = 0.0;
	};
	TArray<FPuppet> Puppets;
	bool bHold = false;
};
