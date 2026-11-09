// Torque Shed's sound-effects mixer (scripts/sfx.gd's buses and players), in plain C++ with no
// Unreal types so SimTest can render it (SimTest/sfx_test.cpp). TorqueSfx (TorqueSfx.h) is the
// game's side: it decides what plays where, and feeds this one stereo stream to Unreal.
//
// The graph is Torque Shed's, bus for bus:
//   Room:   sounds in the listener's room, through that room's reverb (ROOM_PRESETS), with the slap
//           back off the shed's wall when you're outside
//   Far:    sounds from another room or from outside: low-passed at 1100 Hz, its own reverb
//   AmbOut: the outdoor ambience, muffled while you're indoors
//   Sfx:    all of them summed, plus the 2D sounds (UI, room tone), then the ear defenders'
//           low-pass (520 Hz) and -12 dB when muffled
// Each voice plays a clip (a variant of a sound) or a generator (a synthesised horn), at a point
// in the world with Godot's inverse-distance fall-off (unit size, max distance, 3 dB at most up
// close, the air's high-frequency loss with distance) and simple stereo panning, or in 2D.
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace TorqueSfxMix
{
constexpr int Rate = 48000;

inline double DbToLin(double Db) { return Db <= -79.0 ? 0.0 : std::pow(10.0, Db / 20.0); }
inline double LinToDb(double L) { return L <= 1e-4 ? -80.0 : 20.0 * std::log10(L); }

// A recording or a synthesised take: mono 16-bit samples at its own rate.
struct FClip
{
	std::vector<int16_t> S;
	int SampleRate = 44100;
	double Seconds() const { return SampleRate > 0 ? (double)S.size() / SampleRate : 0.0; }
};
// A mono 16-bit PCM WAV (any rate) as a clip; empty if it isn't one.
std::shared_ptr<FClip> ReadClip(const std::vector<uint8_t>& Bytes);

// A sound made as it plays (HornSynth). Render fills N mono samples at Rate; false once it's over.
struct FGenerator
{
	virtual ~FGenerator() = default;
	virtual bool Render(float* Out, int N) = 0;
};

enum class EBus : uint8_t { Room, Far, AmbOut, Sfx };

struct FVec
{
	double X = 0.0, Y = 0.0, Z = 0.0;
};

// What to play (Torque Shed's AudioStreamPlayer3D settings, in Unreal's cm).
struct FPlay
{
	std::shared_ptr<const FClip> Clip;
	std::shared_ptr<FGenerator> Gen;
	bool bLoop = false;
	double LoopFrom = 0.0;     // seconds: where a loop goes round from (a held horn's steady note)
	double Db = 0.0;
	double Pitch = 1.0;
	double StartAt = 0.0;      // seconds into the clip
	double Life = 0.0;         // seconds it may sound at most (0: as long as it lasts)
	double FadeIn = 0.0;       // seconds
	bool b3D = true;
	FVec At;
	double Unit = 400.0;       // cm: full level out to here (Godot's unit_size 4)
	double MaxDistance = 4000.0;
	double MaxDb = 3.0;
	EBus Bus = EBus::Room;
};

// Godot's AudioEffectReverb settings (sfx.gd ROOM_PRESETS).
struct FRoomPreset
{
	double Size = 0.55, Damp = 0.35, Wet = 0.2, Pre = 18.0, Hp = 0.1, Spread = 0.8;
};

// A stereo Freeverb (8 combs, 4 all-passes a side), as Godot's AudioEffectReverb.
class FReverb
{
public:
	FReverb();
	void Set(const FRoomPreset& P, double Dry);
	// In place, stereo interleaved.
	void Process(float* LR, int Frames);

private:
	struct FComb
	{
		std::vector<float> Buf;
		size_t I = 0;
		float Store = 0.0f;
	};
	struct FAllPass
	{
		std::vector<float> Buf;
		size_t I = 0;
	};
	FComb Combs[2][8];
	FAllPass Passes[2][4];
	std::vector<float> PreBuf;
	size_t PreI = 0;
	size_t PreLen = 1;
	float Feedback = 0.84f, Damp1 = 0.2f, Damp2 = 0.8f, Wet = 0.2f, DryGain = 1.0f, HpA = 0.0f, HpZ = 0.0f, HpX = 0.0f;
	FRoomPreset Shown;
	bool bSet = false;
	int SpreadShown = -1;
	void Size(int Spread);
};

// A Butterworth low-pass on a stereo stream (Godot's AudioEffectLowPassFilter), its cutoff eased.
class FLowPass
{
public:
	void Set(double Hz0, double Q = 0.7071);
	void Process(float* LR, int Frames);
	double Cutoff() const { return Hz; }

private:
	double Hz = 20000.0, Qv = 0.7071;
	double B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0;
	double Z[2][2] = {{0, 0}, {0, 0}};
};

class FMixer
{
public:
	FMixer();

	// The game thread's side. Every call takes the lock briefly.
	int Play(const FPlay& P);                     // its id (0: nothing to play)
	void Stop(int Id, double Release = 0.012);    // eased out over Release seconds, then gone
	bool Alive(int Id) const;
	void SetDb(int Id, double Db);                // the voice's level before distance
	void SetPitch(int Id, double Pitch);
	void SetAt(int Id, const FVec& At);
	void SetBus(int Id, EBus Bus);
	void SetListener(const FVec& At, const FVec& Right);
	void SetRoom(const FRoomPreset& P);
	void SetSlap(bool bOn, double DelayMs, double LevelDb);
	void SetMuffled(bool bOn);
	void SetAmbOut(double Cutoff, double Db);
	void SetVolume(double Linear);                // everything (the sound effects slider)
	int Voices() const;
	double ClipPos(int Id) const;                 // seconds into its clip (tests)

	// The audio thread's side: Frames stereo frames (interleaved L, R) at Rate.
	void Render(float* Out, int Frames);

	// For tests: the loudest sample so far.
	float Peak = 0.0f;

private:
	struct FVoice
	{
		int Id = 0;
		FPlay P;
		double Pos = 0.0;       // samples into the clip
		double Gain = 0.0;      // the level now (eased), and the release
		double Fade = 1.0;
		double FadeStep = 0.0;
		double Left = 1.0, Right = 1.0;
		double Age = 0.0;
		bool bStopping = false;
		double Release = 0.012;
		double Air = 0.0;       // the air's high-shelf state
		bool bDone = false;
	};
	mutable std::mutex Lock;
	std::vector<FVoice> Live;
	int NextId = 1;
	FVec Ear, EarRight{0.0, 1.0, 0.0};
	FReverb RoomVerb, FarVerb;
	FLowPass FarLp, AmbLp, MuffleLp;
	double AmbCut = 20000.0, AmbDb = 0.0, AmbGain = 1.0;
	bool bMuffled = false;
	double MuffleGain = 1.0, Volume = 1.0;
	// The slap back: one tap off the room bus.
	std::vector<float> Slap;
	size_t SlapI = 0;
	bool bSlap = false;
	double SlapMs = 120.0, SlapDb = -16.0;
	std::vector<float> BusBuf[4];
	std::vector<float> Mono;
	void Target(const FVoice& V, double& OutGain, double& OutL, double& OutR, double& OutAir) const;
	FVoice* Find(int Id);
	const FVoice* Find(int Id) const;
};
} // namespace TorqueSfxMix
