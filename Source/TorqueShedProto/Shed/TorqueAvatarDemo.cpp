#include "Shed/TorqueAvatarDemo.h"

#include "Shed/TorqueAvatar.h"
#include "Components/SceneComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ATorqueAvatarDemo::ATorqueAvatarDemo()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ATorqueAvatarDemo::~ATorqueAvatarDemo() = default;

void ATorqueAvatarDemo::BeginPlay()
{
	Super::BeginPlay();
	const int32 Count = FTorqueAvatar::Count();
	UE_LOG(LogTemp, Log, TEXT("Torque: avatar demo with %d avatars"), Count);
	for (int32 I = 0; I < Count; ++I)
	{
		FPuppet Pup;
		Pup.Root = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Puppet%d"), I));
		Pup.Root->SetupAttachment(RootComponent);
		Pup.Root->RegisterComponent();
		// Side by side, 1.2 m apart, each a second behind the last.
		Pup.Start = FVector(0.0, (I - (Count - 1) * 0.5) * 120.0, 0.0);
		Pup.Root->SetRelativeLocation(Pup.Start);
		Pup.Clock = -1.0 * I;
		Pup.Body = MakeUnique<FTorqueAvatar>();
		if (!Pup.Body->Build(this, Pup.Root, I)) continue;
		// -TorqueHairAlone: each avatar's grooms also on their own, standing where the puppet starts
		// (in the groom's own space: the hair floats at head height), to see which assets draw.
		if (FParse::Param(FCommandLine::Get(), TEXT("TorqueHairAlone")))
			FTorqueAvatar::SpawnLooseHair(this, RootComponent, I, Pup.Start);
		Puppets.Add(MoveTemp(Pup));
	}
}

void ATorqueAvatarDemo::EndPlay(const EEndPlayReason::Type Reason)
{
	for (FPuppet& Pup : Puppets)
		if (Pup.Body) Pup.Body->Destroy();
	Puppets.Reset();
	Super::EndPlay(Reason);
}

void ATorqueAvatarDemo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = DeltaSeconds;
	for (FPuppet& Pup : Puppets)
	{
		Pup.Clock += Dt;
		const double T = FMath::Fmod(FMath::Max(Pup.Clock, 0.0), 17.0);
		// 0-5 s walk 1.5 m/s away; 5-6 turn; 6-8.5 hurry 3 m/s back; 8.5-9.5 turn; 9.5-12 crouch;
		// 12-17 stand working, looking up and down.
		double Along = 0.0, Speed = 0.0, Yaw = 0.0, Crouch = 0.0;
		bool bWork = false;
		if (T < 5.0) { Along = 150.0 * T; Speed = 150.0; }
		else if (T < 6.0) { Along = 750.0; Yaw = 180.0 * (T - 5.0); }
		else if (T < 8.5) { Along = 750.0 - 300.0 * (T - 6.0); Speed = -300.0; Yaw = 180.0; }
		else if (T < 9.5) { Yaw = 180.0 + 180.0 * (T - 8.5); }
		else if (T < 12.0) { Crouch = 1.0; }
		else { bWork = true; }
		const FRotator Facing(0.0, GetActorRotation().Yaw + Yaw, 0.0);
		Pup.Root->SetRelativeLocationAndRotation(Pup.Start + FVector(Along, 0.0, 0.0), FRotator(0.0, Yaw, 0.0));
		FTorqueAvatarInput In;
		In.Velocity = GetActorRotation().RotateVector(FVector(Speed, 0.0, 0.0));
		In.Facing = Facing;
		In.LookPitch = 25.0 * FMath::Sin(Pup.Clock * 0.9);
		In.Crouch = Crouch;
		In.bWorking = bWork;
		Pup.Body->Pose(In, Dt);
	}
}
