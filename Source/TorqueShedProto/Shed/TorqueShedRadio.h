// The workshop radio on its shelf over the workbench (Torque Shed's shed_radio.gd): a wooden
// cabinet with a cloth grille, a dial whose needle glows and moves with the station, two knobs and
// an aerial, the station and the song over it. Its switch and station are the shed's
// (UTorqueBuildSubsystem::RadioOn / RadioStation, a Radio request), so everyone in the session hears
// the same station; each player's game streams it itself (the engine's media player, as
// radio_stream.gd did with its own decoder). No signal: it hisses instead of going quiet.
//
// The song playing comes from the station's own "now playing" (Icecast metadata), read every so
// often from the start of the stream.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "TorqueShedRadio.generated.h"

class IHttpRequest;
class UAudioComponent;
class UMediaPlayer;
class UMediaSoundComponent;
class USoundWaveProcedural;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

namespace TorqueShedRadio
{
struct FStation
{
	FString Name, Genre, Url;
};
// The stations it tunes to, in dial order (Torque Shed's data/radio.json: SomaFM's, listener
// supported and free to listen to).
const TArray<FStation>& Stations();
// Its base's centre on the shelf (ue_build_shed.py workbench: Radio_Shelf), its front facing +x.
const FVector At(-638.0, -145.0, 161.5);
}

// The song title reader's mailbox: the HTTP thread writes, the game thread reads.
struct FTorqueRadioPeek
{
	FCriticalSection Lock;
	TArray<uint8> Bytes;
	int32 MetaInt = -1;   // the stream's "icy-metaint" (-1: not heard yet)
	bool bEnded = false;
};

UCLASS()
class ATorqueShedRadio : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedRadio();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float DeltaSeconds) override;

	// "Groove Salad  ·  Ambient and downtempo", then the song (or "Tuning in...", "No signal");
	// "Off" when it's off. For the look panel.
	static FString NowPlaying(const UObject* WorldContext);

private:
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY(Transient) UMediaPlayer* Player = nullptr;
	UPROPERTY(Transient) UMediaSoundComponent* Speaker = nullptr;
	UPROPERTY(Transient) USoundWaveProcedural* HissWave = nullptr;
	UPROPERTY(Transient) UAudioComponent* Hiss = nullptr;
	UPROPERTY(Transient) UStaticMeshComponent* Needle = nullptr;
	UPROPERTY(Transient) UTextRenderComponent* Display = nullptr;

	FString Sig;               // "on|station" as last tuned
	int32 Station = 0;
	bool bOn = false;
	FString Status;            // "Tuning in...", "On air", "No signal"
	FString Title;             // the song, as the station last said
	FString Shown;             // what the display over it says
	double NeedleX = 8.6;      // the dial's front, from the radio's base (cm)
	double NeedleY0 = 5.0;     // the needle's sweep, station 0 at -NeedleY0, the last at -(NeedleY0 + NeedleSpan) (cm)
	double NeedleSpan = 14.0;
	double NeedleZ = 17.0;     // its middle's height and its length (cm)
	double NeedleH = 6.0;
	double TunedAt = 0.0, RetryAt = 0.0, PeekAt = 0.0, PeekStarted = 0.0;
	bool bLive = false;
	uint32 HissSeed = 12345;
	float HissLow = 0.0f;
	TSharedPtr<FTorqueRadioPeek, ESPMode::ThreadSafe> Peek;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PeekRequest;

	void Sync();
	void Tune();
	void StopAll();
	void Fail(const TCHAR* Why);
	void UpdateLooks();
	void FeedHiss(float Dt);
	void StartPeek();
	void ReadPeek();
	void StopPeek();
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, const FVector& Where, const FVector& SizeCm, const FRotator& Rot,
		const FLinearColor& Colour, float Metallic, float Roughness, float Emissive = 0.0f);
};
