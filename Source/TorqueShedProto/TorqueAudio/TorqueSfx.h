// Torque Shed's sound effects in the Unreal port: scripts/sfx.gd's API on Torque Shed's own sounds
// (Data/Audio/TS, Tools/import_ts_sounds.py), mixed through its buses (SfxMix) and played as one
// stereo stream.
//
//   PlayAt / PlayOn / PlayUi       one-shots: a different take each time (never the same twice
//                                  running), a touch of pitch and level, the acoustics of where it is
//   Impact / Footstep / JobDone    a part landing by material and surface, a step by floor, a job's sound
//   MakeLoop / SetLoop             loops (a tool at work, a machine) with their start and stop sounds,
//                                  fade in, detune, picked up at a random point
//   SetMuffled                     the ear defenders
//   StartHorn / SetHorn            a bike's horn: its recordings (held, once, repeating) or synthesised
//
// It also does on its own, from what it can see (so the shed's code needs few calls): every
// walker's footsteps and the tool loop in their hands (yours by the job, other players' by the tool),
// each room's reverb and the walls between rooms, the slap back off the shed wall outside, the
// shed's ambience (room tone, the lights' hum, the compressor cycling, the roof creaking, the wind
// outside muffled indoors), the dyno room (fan, drum, the engine on the rig, its switches), the
// mixing bay's shaker and the radio's switch, and the sound of each thing you ask the shed to do
// (UTorqueBuildSubsystem::OnRequestDone).
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "TorqueAudio/EngineSynth.h"
#include "TorqueAudio/SfxBank.h"
#include "TorqueAudio/SfxMix.h"
#include "TorqueAudio/SfxRules.h"
#include "TorqueSfx.generated.h"

struct FTorqueShedRequest;

// The one stream everything's mixed into, handed to Unreal's mixer.
UCLASS()
class UTorqueSfxSynth : public USynthComponent
{
	GENERATED_BODY()
public:
	std::shared_ptr<TorqueSfxMix::FMixer> Mixer;
protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
};

UCLASS()
class UTorqueSfx : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UTorqueSfx* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// --- sfx.gd's API --------------------------------------------------------------------------------
	int32 PlayAt(const FString& Sound, const FVector& At, float Db = 0.0f, float Seconds = 0.0f);
	int32 PlayOn(const FString& Sound, USceneComponent* Source, const FVector& Offset = FVector::ZeroVector, float Db = 0.0f, float Seconds = 0.0f);
	void PlayUi(const FString& Sound, float Db = 0.0f);
	// Material: a part's ("metal", "carbon", ...) or "cardboard"; Surface "concrete", "wood", "steel", "ground".
	int32 Impact(const FString& Material, const FString& Surface, float Strength, const FVector& At);
	int32 Footstep(const FVector& Feet, bool bRunning, float Db = -14.0f);
	// The sound of something you asked the shed to do, done: HeldBefore is what was in your hands.
	void JobDone(const FTorqueShedRequest& R, int32 HeldBefore, const FVector& At);

	int32 MakeLoop(USceneComponent* Parent, const FVector& Offset, float Db = 0.0f, float UnitCm = 400.0f);
	int32 MakeLoopAt(const FVector& At, float Db = 0.0f, float UnitCm = 400.0f);
	void SetLoop(int32 Loop, const FString& Sound);   // "" stops it (with its run-down)
	void SetLoopPitch(int32 Loop, float Pitch);
	void SetLoopDb(int32 Loop, float Db);
	FString LoopSound(int32 Loop) const;
	void FreeLoop(int32 Loop);

	void SetMuffled(bool bOn);
	bool IsMuffled() const { return bMuffled; }

	// A horn on Source: Sound its recordings ("horn_air"; "" or missing to synthesise), Style and
	// Tones the synthesis (horn_audio.gd), Loudness the part's.
	int32 StartHorn(USceneComponent* Source, const FString& Sound, const FString& Style, const TArray<double>& Tones, float Loudness);
	void SetHorn(int32 Horn, bool bPressed);
	// The horn on bay Bay's bike (its horn slot's part in Data/Parts/parts.json, worn ones quieter);
	// the stock disc horn when there's no shed bike (INDEX_NONE). 0 if the bike has no horn.
	int32 StartBikeHorn(USceneComponent* Source, int32 Bay);

	// Bay Bay's engine sound (Torque Shed's EngineSynth recipe from its engine, head, exhaust, air
	// filter, gearbox and plugs; EngineSound plays it): shared by every bike with the same recipe and
	// baking in the background, idle first. Null if the bay has no engine.
	static std::shared_ptr<TorqueEngine::FBank> BikeEngine(const UObject* WorldContext, int32 Bay);
	// A sound made on the spot (a kick start), riding along on Source like PlayOn.
	int32 PlayMadeOn(const std::vector<float>& Samples, int32 SampleRate, USceneComponent* Source, const FVector& Offset, float Db = 0.0f);

	// The room the listener's in ("" outside) and how many voices are sounding (tests).
	const FString& ListenerRoom() const { return ListenerRoomName; }
	int32 VoicesSounding() const { return Mixer ? Mixer->Voices() : 0; }
	int32 SoundsKnown() const;

private:
	std::shared_ptr<TorqueSfxMix::FMixer> Mixer;
	UPROPERTY(Transient) UTorqueSfxSynth* Synth = nullptr;
	UPROPERTY(Transient) AActor* Speaker = nullptr;
	std::mt19937 Rng{1234};
	double Uni(double A, double B) { return std::uniform_real_distribution<double>(A, B)(Rng); }
	bool bShed = false;          // the shed's map (its rooms, its ambience)
	bool bMuffled = false;
	FVector Ear = FVector::ZeroVector;
	FString ListenerRoomName;
	TorqueSfxRules::FPreset Shown{0.55, 0.35, 0.2, 18.0, 0.1, 0.8};

	struct FFollow   // a one-shot riding along on something
	{
		int32 Voice = 0;
		TWeakObjectPtr<USceneComponent> Source;
		FVector Offset = FVector::ZeroVector;
	};
	TArray<FFollow> Follows;

	struct FLoop
	{
		TWeakObjectPtr<USceneComponent> Parent;
		bool bParented = false;
		FVector Offset = FVector::ZeroVector;   // in the parent's frame, or the world point
		float BaseDb = 0.0f, Pitch = 1.0f, Detune = 1.0f, UnitCm = 400.0f;
		FString Sound;
		int32 Voice = 0;
		FVector At = FVector::ZeroVector;
	};
	TMap<int32, FLoop> Loops;
	int32 NextLoop = 1;
	double RouteIn = 0.0;

	struct FHorn
	{
		TWeakObjectPtr<USceneComponent> Source;
		FString Sound, Style, Mode;
		TArray<double> Tones;
		float Loudness = 0.6f;
		bool bPressed = false;
		int32 Voice = 0;
		std::shared_ptr<TorqueSfxBank::FHornSynth> Synth;
	};
	TMap<int32, FHorn> Horns;
	int32 NextHorn = 1;

	// Walkers: their strides and the loop in their hands.
	struct FWalkerSound
	{
		FVector Last = FVector::ZeroVector;
		bool bSeen = false;
		double Stride = 0.0;
		int32 Loop = 0;
	};
	TMap<uint32, FWalkerSound> Walkers;   // by the walker's UniqueID

	// The shed's ambience (sfx_ambience.gd).
	int32 ToneVoice = 0, HumVoice = 0, WindVoice = 0;
	double ToneLevel = 0.0, HumLevel = 0.0, WindLevel = 0.0, AmbCut = 20000.0, AmbDb = 0.0;
	int32 CompLoop = 0;
	bool bCompOn = false;
	double CompTimer = 30.0, CreakTimer = 20.0;
	bool bDynoLitSeen = true, bLitKnown = false;

	// The dyno room (dyno_room.gd) and the rest of the kit it watches.
	int32 FanLoop = 0, RollerLoop = 0;
	float FanSpeed = 0.0f, FanPush = 0.0f;
	bool bFanSeen = false, bLightsSeen = true, bKitKnown = false, bRunSeen = false, bRadioSeen = false, bRadioKnown = false;
	int32 DynoGearSeen = 0;
	std::shared_ptr<TorqueSfxMix::FGenerator> DynoEngine;
	int32 DynoVoice = 0;
	int32 ShakerLoop = 0;
	FDelegateHandle RequestHandle;

	void EnsureSpeaker();
	int32 Play(const FString& Sound, const TorqueSfxMix::FPlay& Base, bool bRandomise);
	TorqueSfxMix::FVec V(const FVector& P) const { return {P.X, P.Y, P.Z}; }
	FString RoomAt(const FVector& P) const;
	TorqueSfxMix::EBus BusFor(const FVector& P) const;
	double Occlusion(const FVector& P) const;
	FVector LoopPos(const FLoop& L) const;
	void Route(FLoop& L);
	void TickListener(float Dt);
	void TickWalkers(float Dt);
	void TickAmbience(float Dt);
	void TickKit(float Dt);
	void TickHorns();
	void Requested(const FTorqueShedRequest& R, int32 HeldBefore);
	int32 Bed(const FString& Sound, TorqueSfxMix::EBus Bus);
	void Fade(int32 Voice, double& Level, double Target, float Dt, double Db);
};
