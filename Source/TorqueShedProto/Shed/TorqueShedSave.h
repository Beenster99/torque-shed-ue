// The shed on disk (UTorqueBuildSubsystem): every bay's bike and every item, as shed_state.gd's
// save_snapshot. Saved on the host (the shed is the host's; clients keep a copy). The same object
// is the shed's bytes in co-op (UTorqueBuildSubsystem::SaveShed, through SaveGameToMemory).
// Slot "Shed" for play; test runs (any -Torque test switch, such as -TorqueShedAction=) use
// "ShedTest", reset at the start of every run, so a test never changes the player's shed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TorqueShedSave.generated.h"

USTRUCT()
struct FTorqueShedSaveItem
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName Part;
	UPROPERTY() FName Kind;
	UPROPERTY() float Rust = 0.0f;
	UPROPERTY() float Dirt = 0.0f;
	UPROPERTY() float Wear = 0.0f;
	UPROPERTY() FLinearColor Colour = FLinearColor(0.5f, 0.5f, 0.5f);
	UPROPERTY() FName Finish;
	UPROPERTY() bool bBoxed = false;
	UPROPERTY() uint8 At = 0;          // ETorqueAt
	UPROPERTY() int32 Ref = 0;
	UPROPERTY() FName Spot;
	UPROPERTY() FTransform Place;
	UPROPERTY() TMap<FName, int32> Slots;
	UPROPERTY() TMap<FName, bool> Bolted;
	UPROPERTY() int32 Tyre = 0;
	UPROPERTY() float PaintQ = -1.0f;   // the paint job (FTorqueItem): -1 as the factory left it
	UPROPERTY() float Primer = 0.0f;
	UPROPERTY() bool bPrimerTop = false;
	UPROPERTY() FName Coat;
	UPROPERTY() float CoatQ = 0.0f;
	UPROPERTY() FString MixCode;        // paint from the mixing bay (FTorqueItem)
	UPROPERTY() FString MixName;
	UPROPERTY() FName MixEffect;
	UPROPERTY() int32 MixCoats = 0;
	UPROPERTY() FLinearColor MixUnder = FLinearColor(0.5f, 0.5f, 0.5f);
	UPROPERTY() float Flat = 0.0f;
	UPROPERTY() FString OrderedBy;     // a delivery: who it's for (FTorqueItem)
	UPROPERTY() FLinearColor OrderedTint = FLinearColor(0.98f, 0.85f, 0.2f);
	UPROPERTY() int32 Tape = 0;         // masking tape on it, and a masked pattern (FTorqueItem)
	UPROPERTY() int32 Mask = 0;
	UPROPERTY() FLinearColor MaskColour = FLinearColor::White;
	UPROPERTY() float MaskMetal = 0.05f;
	UPROPERTY() float MaskRough = 0.15f;
};

// A dyno run (FTorqueDynoRun), the bike's last two kept.
USTRUCT()
struct FTorqueShedSaveDynoRun
{
	GENERATED_BODY()
	UPROPERTY() int32 N = 0;
	UPROPERTY() TArray<float> Rpm;
	UPROPERTY() TArray<float> Nm;
	UPROPERTY() TArray<float> Hp;
	UPROPERTY() float PeakHp = 0.0f;
	UPROPERTY() float PeakHpRpm = 0.0f;
	UPROPERTY() float PeakNm = 0.0f;
	UPROPERTY() float PeakNmRpm = 0.0f;
	UPROPERTY() float Redline = 8000.0f;
	UPROPERTY() float IdleRpm = 1000.0f;
	UPROPERTY() int32 Gears = 6;
	UPROPERTY() float Heat = 0.0f;
	UPROPERTY() float TempC = 90.0f;
	UPROPERTY() bool bFan = false;
	UPROPERTY() float Mass = 0.0f;
};

USTRUCT()
struct FTorqueShedSaveBike
{
	GENERATED_BODY()

	UPROPERTY() bool bExists = false;
	UPROPERTY() int32 Id = 0;
	UPROPERTY() FString Name;
	UPROPERTY() FName Model;
	UPROPERTY() TMap<FName, int32> Slots;
	UPROPERTY() TMap<FName, bool> Bolted;
	UPROPERTY() bool bRode = false;
	UPROPERTY() TArray<FString> FabOwed;
	UPROPERTY() TArray<FString> FabDone;
	// Wheeled out of its bay (onto the dyno, or left on its stand): where it stands.
	UPROPERTY() bool bAway = false;
	UPROPERTY() FTransform Xf;
	UPROPERTY() TArray<FTorqueShedSaveDynoRun> DynoRuns;
	UPROPERTY() int32 DynoN = 0;
	// Its fit from geometry mode (Fit/BikeGeometry text; "" for none).
	UPROPERTY() FString Geo;
};

UCLASS()
class UTorqueShedSave : public USaveGame
{
	GENERATED_BODY()

public:
	// 1 and 2 were the first prototype's build-per-bay sheds; 3 is the item shed. Older saves
	// aren't read (no one has played one): the shed starts afresh.
	static constexpr int32 CurrentVersion = 3;
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TArray<FTorqueShedSaveBike> Bikes;
	UPROPERTY() TArray<FTorqueShedSaveItem> Items;
	UPROPERTY() int32 NextId = 1;
	UPROPERTY() int32 MyBike = 0;
	// The dyno room's kit (fields added to version 3: older saves read them as their defaults).
	UPROPERTY() TArray<bool> DynoStraps;
	UPROPERTY() bool bDynoFan = false;
	UPROPERTY() bool bDynoLights = true;
	UPROPERTY() float DynoHeat = 0.0f;
	// The shed radio (added to version 3 too: an older save's is off).
	UPROPERTY() bool bRadioOn = false;
	UPROPERTY() int32 RadioStation = 0;
	// The paint mixing bay (FTorquePaintBay::Serialize's bytes; none: a new garage's bay).
	UPROPERTY() TArray<uint8> PaintBay;
};
