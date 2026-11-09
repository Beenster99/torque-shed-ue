#include "SfxBank.h"

#include "FitJson.h"

#include <cmath>

namespace TorqueSfxBank
{
using TorqueFit::Json;

bool FBank::Load(const std::string& ManifestText, FReader Reader)
{
	Json M;
	if (!Json::Parse(ManifestText, M) || !M.IsObject()) return false;
	const Json* Sounds = M.Get("sounds");
	if (!Sounds || !Sounds->IsObject()) return false;
	std::lock_guard<std::mutex> G(Lock);
	Read = std::move(Reader);
	Defs.clear();
	for (const auto& KV : Sounds->O)
	{
		FSoundDef D;
		if (const Json* F = KV.second.Get("files"))
			for (const Json& X : F->A)
				if (X.IsString()) D.Files.push_back(X.S);
		D.bLoop = KV.second.Flag("loop");
		D.From = KV.second.Str("from");
		D.Horn = KV.second.Str("horn");
		D.LoopFrom = KV.second.Num("loop_from", 0.0);
		Defs[KV.first] = D;
	}
	return !Defs.empty();
}

const FSoundDef* FBank::Def(const std::string& Sound) const
{
	std::lock_guard<std::mutex> G(Lock);
	auto It = Defs.find(Sound);
	return It == Defs.end() ? nullptr : &It->second;
}

std::vector<std::string> FBank::Names() const
{
	std::lock_guard<std::mutex> G(Lock);
	std::vector<std::string> Out;
	for (const auto& KV : Defs) Out.push_back(KV.first);
	return Out;
}

std::shared_ptr<const FClip> FBank::FileLocked(const std::string& Rel)
{
	auto It = Files.find(Rel);
	if (It != Files.end()) return It->second;
	std::shared_ptr<const FClip> C;
	std::vector<uint8_t> Bytes;
	if (Read && Read(Rel, Bytes))
	{
		std::shared_ptr<FClip> Made = TorqueSfxMix::ReadClip(Bytes);
		if (!Made->S.empty()) C = Made;
	}
	Files[Rel] = C;   // a missing file is remembered too, so it isn't looked for every time
	return C;
}

std::shared_ptr<const FClip> FBank::File(const std::string& Rel)
{
	std::lock_guard<std::mutex> G(Lock);
	return FileLocked(Rel);
}

std::vector<std::shared_ptr<const FClip>> FBank::Takes(const std::string& Sound)
{
	std::lock_guard<std::mutex> G(Lock);
	std::vector<std::shared_ptr<const FClip>> Out;
	auto It = Defs.find(Sound);
	if (It == Defs.end()) return Out;
	for (const std::string& F : It->second.Files)
		if (std::shared_ptr<const FClip> C = FileLocked(F)) Out.push_back(C);
	return Out;
}

std::shared_ptr<const FClip> FBank::Pick(const std::string& Sound, std::mt19937& Rng)
{
	std::vector<std::shared_ptr<const FClip>> List = Takes(Sound);
	if (List.empty()) return nullptr;
	if (List.size() == 1) return List[0];
	std::lock_guard<std::mutex> G(Lock);
	auto Found = LastPick.find(Sound);
	const int Last = Found == LastPick.end() ? -1 : Found->second;
	int V = std::uniform_int_distribution<int>(0, (int)List.size() - 2)(Rng);
	if (Last >= 0 && V >= Last) ++V;
	LastPick[Sound] = V;
	return List[(size_t)V];
}

int FBank::Preload()
{
	int N = 0;
	for (const std::string& S : Names()) N += (int)Takes(S).size();
	return N;
}

// --- Horns ------------------------------------------------------------------------------------------

namespace
{
// horn_audio.gd TUNE: La Cucaracha, note (index into the tones, -1 a rest) and seconds.
const double Tune[][2] = {{0, 0.13}, {0, 0.13}, {0, 0.13}, {3, 0.38}, {4, 0.42}, {-1, 0.12},
	{0, 0.13}, {0, 0.13}, {0, 0.13}, {3, 0.38}, {4, 0.5}, {-1, 0.4}};
constexpr double TwoPi = 6.283185307179586;
}

FHornSynth::FHornSynth(const std::string& InStyle, const std::vector<double>& InTones, double InLoudness, uint32_t Seed)
	: Style(InStyle), Tones(InTones), Loudness(InLoudness), Rng(Seed)
{
	while (Phases.size() < std::max<size_t>(Tones.size(), 4)) Phases.push_back(Uni(0.0, 1.0));
}

double FHornSynth::Advance(int N, double Hz)
{
	double& Ph = Phases[(size_t)N];
	Ph += Hz / MixRate;
	Ph -= std::floor(Ph);
	return Ph;
}

double FHornSynth::Melody(double T)
{
	double Total = 0.0;
	for (const auto& Nt : Tune) Total += Nt[1];
	double At = std::fmod(T, Total);
	for (const auto& Nt : Tune)
	{
		if (At < Nt[1])
		{
			const int Idx = (int)Nt[0];
			if (Idx < 0 || Idx >= (int)Tones.size() || At > Nt[1] - 0.02) return 0.0;
			return Saw(Idx, Tones[(size_t)Idx]);
		}
		At -= Nt[1];
	}
	return 0.0;
}

double FHornSynth::Sample()
{
	const bool bNow = bPressedNext.load();
	if (bNow)
	{
		if (!bPressed && (Style == "bulb" || Style == "beep")) Honk = Style == "bulb" ? 0.36 : 0.3;
		Held += 1.0 / MixRate;
	}
	else
		Held = 0.0;
	bPressed = bNow;
	Honk = std::max(Honk - 1.0 / MixRate, 0.0);
	const bool bOn = bPressed || Honk > 0.0;
	const double T = Held;
	double Gate = bOn ? 1.0 : 0.0;
	double S = 0.0;
	const double F0 = Tones.empty() ? 420.0 : Tones[0];
	if (Style == "melody")
		S = bPressed ? Melody(T) : 0.0;
	else if (Style == "klaxon")
	{
		// AOOGA: the pitch swoops up and the motor rasps.
		const double Swoop = std::min(std::max(T / 0.45, 0.0), 1.0);
		S = Saw(0, F0 * (0.55 + 0.45 * std::sqrt(Swoop))) * (0.7 + 0.3 * std::sin(T * TwoPi * 31.0));
	}
	else if (Style == "bulb")
	{
		const double K = 1.0 - Honk / 0.36;
		S = Saw(0, F0 * (1.15 + (0.85 - 1.15) * K)) * 0.8 + Uni(-0.2, 0.2);
	}
	else if (Style == "beep")
	{
		const double K = 0.3 - Honk;
		Gate *= (K < 0.11 || (K > 0.17 && K < 0.28)) ? 1.0 : 0.0;
		S = Square(0, F0);
	}
	else if (Style == "goose")
	{
		const double Wob = 1.0 + 0.04 * std::sin(T * TwoPi * 7.0) + 0.02 * Uni(-1.0, 1.0);
		S = Saw(0, F0 * Wob) * 0.6 + Square(1, F0 * 2.0 * Wob) * 0.25;
	}
	else if (Style == "air" || Style == "snail")
	{
		const double Vib = 1.0 + 0.006 * std::sin(T * TwoPi * 5.0);
		for (size_t N = 0; N < Tones.size(); ++N) S += Saw((int)N, Tones[N] * Vib) / Tones.size();
	}
	else
		for (size_t N = 0; N < Tones.size(); ++N) S += Square((int)N, Tones[N]) * 0.75 / Tones.size();
	Env += (Gate - Env) * (Gate > Env ? 0.02 : 0.006);
	const double Bright = (Style == "air" || Style == "snail" || Style == "beep") ? 0.5 : 0.3;
	Lp += (S - Lp) * Bright;
	return Lp * Env * 0.35 * std::min(std::max(Loudness, 0.2), 1.3);
}

bool FHornSynth::Render(float* Out, int N)
{
	const double Step = MixRate / TorqueSfxMix::Rate;
	for (int I = 0; I < N; ++I)
	{
		Frac += Step;
		while (Frac >= 1.0)
		{
			Frac -= 1.0;
			Prev = Next;
			Next = Sample();
		}
		Out[I] = (float)(Prev + (Next - Prev) * Frac);
	}
	// It goes once it's been silent a while with the button up (TorqueSfx makes a new one each press).
	Quiet = (!bPressedNext.load() && Env < 0.001 && Honk <= 0.0) ? Quiet + (double)N / TorqueSfxMix::Rate : 0.0;
	return Quiet < 0.25;
}
} // namespace TorqueSfxBank
