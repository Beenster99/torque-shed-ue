// Torque Shed's sounds as files (Data/Audio/TS, made by Tools/import_ts_sounds.py): the manifest
// says what each sound plays (its recordings, or what stands in, or its synthesised variants) and
// whether it loops; takes are read the first time they're wanted and kept. Picking a take is
// Sfx.pick: at random, never the same one twice running. Plain C++ (SimTest/sfx_test.cpp).
//
// HornSynth is horn_audio.gd's synthesised horns (disc, snail and air trumpets, the klaxon, bulb,
// beeper, goose and the musical horn), for horns without recordings.
#pragma once

#include "SfxMix.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace TorqueSfxBank
{
using TorqueSfxMix::FClip;

struct FSoundDef
{
	std::vector<std::string> Files;   // relative to Data/Audio/TS
	bool bLoop = false;
	std::string From;                 // "recording", "synth", "stand_in:<sound>", "silent"
	std::string Horn;                 // a recorded horn: "hold", "once" or "repeat"
	double LoopFrom = 0.0;            // a held horn's steady note loops from here (s)
};

class FBank
{
public:
	using FReader = std::function<bool(const std::string& Rel, std::vector<uint8_t>& Out)>;
	bool Load(const std::string& ManifestText, FReader Reader);
	bool Loaded() const { return !Defs.empty(); }
	const FSoundDef* Def(const std::string& Sound) const;
	size_t Count() const { return Defs.size(); }
	std::vector<std::string> Names() const;
	// A take of Sound picked at random, never the one picked last (null: nothing to play).
	std::shared_ptr<const FClip> Pick(const std::string& Sound, std::mt19937& Rng);
	// Every take of Sound, read now.
	std::vector<std::shared_ptr<const FClip>> Takes(const std::string& Sound);
	// A file by its path (traffic loops, engine recordings, place beds).
	std::shared_ptr<const FClip> File(const std::string& Rel);
	// Reads every take of every sound but the place beds (a worker can call it at start-up).
	int Preload();

private:
	std::map<std::string, FSoundDef> Defs;
	std::map<std::string, std::shared_ptr<const FClip>> Files;
	std::map<std::string, int> LastPick;
	FReader Read;
	mutable std::mutex Lock;
	std::shared_ptr<const FClip> FileLocked(const std::string& Rel);
};

// horn_audio.gd's synthesis, run at its own 22050 Hz and handed on at the mixer's rate.
class FHornSynth : public TorqueSfxMix::FGenerator
{
public:
	FHornSynth(const std::string& Style, const std::vector<double>& Tones, double Loudness, uint32_t Seed = 7);
	void SetPressed(bool bOn) { bPressedNext = bOn; }
	bool Render(float* Out, int N) override;
	bool Sounding() const { return Env > 0.02; }

private:
	static constexpr double MixRate = 22050.0;
	std::string Style;
	std::vector<double> Tones;
	double Loudness = 0.6;
	std::atomic<bool> bPressedNext{false};
	bool bPressed = false;
	double Held = 0.0, Honk = 0.0, Env = 0.0, Lp = 0.0, Quiet = 0.0;
	std::vector<double> Phases;
	std::mt19937 Rng;
	double Frac = 0.0, Prev = 0.0, Next = 0.0;
	double Uni(double A, double B) { return std::uniform_real_distribution<double>(A, B)(Rng); }
	double Advance(int N, double Hz);
	double Saw(int N, double Hz) { return Advance(N, Hz) * 2.0 - 1.0; }
	double Square(int N, double Hz) { return Advance(N, Hz) < 0.5 ? 1.0 : -1.0; }
	double Melody(double T);
	double Sample();   // one sample at MixRate
};
} // namespace TorqueSfxBank
