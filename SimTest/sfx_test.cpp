// Checks the shed's sounds (TorqueAudio/SfxMix, SfxBank, SfxRules) against Torque Shed: every file
// in Data/Audio/TS's manifest reads, the synthesised takes kept the game's loudness (sfx_synth.gd
// LEVELS), takes never repeat twice running, the rooms and walls follow sfx.gd, and a little scene
// renders cleanly (a sound through a wall is quieter and duller than in the room, the ear defenders
// muffle, a loop goes round without stopping, a one-shot ends, the synthesised horn sounds while
// held and stops). Writes sfx_test.wav to listen to.
#include "SfxBank.h"
#include "SfxMix.h"
#include "SfxRules.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace TorqueSfxMix;

static int Failures = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("%s %s\n", Ok ? "  ok  " : "  FAIL", What);
	if (!Ok) ++Failures;
}

static const std::string Root = TORQUE_DATA "/Audio/TS/";

static bool ReadFile(const std::string& Path, std::vector<uint8_t>& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	Out.assign(std::istreambuf_iterator<char>(F), std::istreambuf_iterator<char>());
	return true;
}

static double Peak(const FClip& C)
{
	int M = 0;
	for (int16_t S : C.S) M = std::max(M, std::abs((int)S));
	return M / 32767.0;
}

static double Rms(const FClip& C)
{
	double Sum = 0.0;
	for (int16_t S : C.S) Sum += (double)S * S;
	return std::sqrt(Sum / std::max<size_t>(C.S.size(), 1)) / 32767.0;
}

static double RenderRms(FMixer& M, int Frames, std::vector<float>* Keep = nullptr)
{
	std::vector<float> B((size_t)Frames * 2);
	double Sum = 0.0;
	for (int Done = 0; Done < Frames; Done += 480)
	{
		const int N = std::min(480, Frames - Done);
		M.Render(B.data() + (size_t)Done * 2, N);
	}
	for (float X : B) Sum += (double)X * X;
	if (Keep) Keep->insert(Keep->end(), B.begin(), B.end());
	return std::sqrt(Sum / B.size());
}

static void WriteWav(const std::string& Path, const std::vector<float>& LR)
{
	std::ofstream F(Path, std::ios::binary);
	auto U32 = [&F](uint32_t V) { F.put((char)(V & 255)); F.put((char)((V >> 8) & 255)); F.put((char)((V >> 16) & 255)); F.put((char)(V >> 24)); };
	auto U16 = [&F](uint16_t V) { F.put((char)(V & 255)); F.put((char)(V >> 8)); };
	const uint32_t Bytes = (uint32_t)LR.size() * 2;
	F.write("RIFF", 4); U32(36 + Bytes); F.write("WAVE", 4); F.write("fmt ", 4); U32(16); U16(1); U16(2); U32(Rate); U32(Rate * 4); U16(4); U16(16);
	F.write("data", 4); U32(Bytes);
	for (float X : LR) U16((uint16_t)(int16_t)std::lround(std::max(-1.0f, std::min(1.0f, X)) * 32767.0f));
}

int main()
{
	std::vector<uint8_t> Text;
	Check(ReadFile(Root + "manifest.json", Text), "Data/Audio/TS/manifest.json reads");
	TorqueSfxBank::FBank Bank;
	Check(Bank.Load(std::string(Text.begin(), Text.end()), [](const std::string& Rel, std::vector<uint8_t>& Out) { return ReadFile(Root + Rel, Out); }),
		"the manifest parses");
	std::printf("  %zu sounds\n", Bank.Count());

	// Every take of every sound reads as a WAV.
	int Takes = 0, Bad = 0;
	for (const std::string& S : Bank.Names())
	{
		const TorqueSfxBank::FSoundDef* D = Bank.Def(S);
		const size_t Got = Bank.Takes(S).size();
		Takes += (int)Got;
		if (Got != D->Files.size()) { ++Bad; std::printf("  missing a take of %s\n", S.c_str()); }
		if (D->From != "silent" && D->Files.empty()) { ++Bad; std::printf("  %s has nothing to play\n", S.c_str()); }
	}
	std::printf("  %d takes\n", Takes);
	Check(Bad == 0 && Takes > 300, "every take of every sound reads");
	Check(Bank.Def("ratchet") && Bank.Def("ratchet")->From == "recording" && Bank.Def("ratchet")->bLoop, "the ratchet is Simon's recording, looped");
	Check(Bank.Def("light_switch") && Bank.Def("light_switch")->Files.size() == 4, "the light switch has its four recordings");
	Check(Bank.Def("step_tile") && Bank.Def("step_tile")->Files.size() == 8, "footsteps have eight variants");
	Check(Bank.Def("horn_air") && Bank.Def("horn_air")->Horn == "hold" && Bank.Def("horn_air")->LoopFrom == 0.25, "a held horn loops from 0.25 s");
	Check(Bank.Def("horn_ship") && Bank.Def("horn_ship")->Horn == "once", "the ship's horn plays its whole blast");

	// sfx_synth.gd LEVELS: peaks for one-shots, RMS for steady loops.
	struct FLevel { const char* Sound; bool bRms; double Want; };
	const FLevel Levels[] = {{"clink", false, 0.6}, {"clunk", false, 0.75}, {"pickup", false, 0.45}, {"impact_metal_concrete", false, 0.8},
		{"step_concrete", false, 0.5}, {"step_grass", false, 0.35}, {"grinder", true, 0.17}, {"scrub", true, 0.12}, {"compressor", true, 0.16},
		{"tone_shed", true, 0.04}, {"shaker", true, 0.13}};
	bool bLevels = true;
	for (const FLevel& L : Levels)
	{
		for (const auto& C : Bank.Takes(L.Sound))
		{
			const double Got = L.bRms ? Rms(*C) : Peak(*C);
			if (std::abs(Got - L.Want) > (L.bRms ? 0.03 : 0.01)) { bLevels = false; std::printf("  %s: %.3f, want %.3f\n", L.Sound, Got, L.Want); }
		}
	}
	Check(bLevels, "the synthesised takes keep Torque Shed's loudness");

	// Picking: never the same take twice running.
	std::mt19937 Rng(5);
	bool bRepeat = false;
	std::shared_ptr<const FClip> Last;
	for (int I = 0; I < 200; ++I)
	{
		auto C = Bank.Pick("clink", Rng);
		if (C == Last) bRepeat = true;
		Last = C;
	}
	Check(!bRepeat, "a take is never picked twice running");

	// The rules.
	using namespace TorqueSfxRules;
	Check(RoomAt(0, 0, 100) == "shed" && RoomAt(1000, 0, 100) == "paint" && RoomAt(1550, 0, 100) == "mixing" && RoomAt(0, -900, 100) == "dyno" &&
		RoomAt(-1000, -700, 100) == "storage" && RoomAt(0, 1500, 0).empty(), "the rooms are where the level builds them");
	Check(OcclusionDb("shed", "shed", false) == 0.0 && OcclusionDb("dyno", "shed", false) == DoorwayDb && OcclusionDb("dyno", "paint", false) == WallDb &&
		OcclusionDb("", "shed", true) == DoorwayDb && OcclusionDb("", "shed", false) == WallDb, "walls and doorways take off what sfx.gd says");
	Check(std::string(WorkLoop("unbolt")) == "ratchet" && std::string(ToolLoop("gun")) == "spray_gun" && std::string(DoneSound("bin")) == "scrap", "the work loops and done sounds");
	Check(ImpactMaterial("carbon") == "plastic" && ImpactMaterial("leather") == "soft" && ImpactMaterial("steel") == "metal", "materials land as sfx.gd's");

	// A scene: the grinder in the room, then through a wall; a clunk; the ear defenders.
	std::vector<float> Wav;
	FMixer M;
	M.SetListener({0, 0, 160}, {0, 1, 0});
	const FPreset P = RoomPreset("shed");
	M.SetRoom({P.Size, P.Damp, P.Wet, P.Pre, P.Hp, P.Spread});
	FPlay G;
	G.Clip = Bank.Pick("grinder", Rng);
	G.bLoop = true;
	G.At = {150, 0, 100};
	G.FadeIn = LoopFadeIn("grinder");
	const int Id = M.Play(G);
	const double InRoom = RenderRms(M, Rate * 3, &Wav);
	Check(M.Alive(Id) && M.ClipPos(Id) >= 0.0, "the grinder's loop goes round and keeps going");
	M.SetDb(Id, WallDb);
	M.SetBus(Id, EBus::Far);
	const double Through = RenderRms(M, Rate * 2, &Wav);
	std::printf("  grinder in the room %.4f, through a wall %.4f\n", InRoom, Through);
	Check(Through < InRoom * 0.6 && Through > InRoom * 0.05, "through a wall it's quieter");
	M.SetBus(Id, EBus::Room);
	M.SetDb(Id, 0.0);
	M.SetMuffled(true);
	RenderRms(M, Rate / 2, &Wav);
	const double Muffled = RenderRms(M, Rate, &Wav);
	Check(Muffled < InRoom * 0.4, "the ear defenders make it dull and quieter");
	M.SetMuffled(false);
	M.Stop(Id, 0.05);
	RenderRms(M, Rate / 5, &Wav);
	Check(!M.Alive(Id) && M.Voices() == 0, "a stopped loop goes");
	FPlay C;
	C.Clip = Bank.Pick("clunk", Rng);
	C.At = {100, 50, 0};
	C.Db = -4.0;   // as job_done plays it
	const int Cl = M.Play(C);
	RenderRms(M, Rate * 2, &Wav);
	Check(!M.Alive(Cl) && M.Voices() == 0, "a one-shot ends by itself");
	FPlay Far = C;
	Far.At = {5000, 0, 0};
	RenderRms(M, Rate * 4);   // the room's tail dies away
	const double Quiet = RenderRms(M, Rate / 4);
	M.Play(Far);
	Check(RenderRms(M, Rate / 4) <= Quiet + 1e-6, "nothing is heard past its max distance");

	// The synthesised horn: sounds while held, stops after.
	auto Horn = std::make_shared<TorqueSfxBank::FHornSynth>("disc", std::vector<double>{410.0, 520.0}, 0.7);
	FPlay H;
	H.Gen = Horn;
	H.b3D = false;
	H.Bus = EBus::Sfx;
	Horn->SetPressed(true);
	const int Hid = M.Play(H);
	const double Honk = RenderRms(M, Rate, &Wav);
	Horn->SetPressed(false);
	RenderRms(M, Rate, &Wav);
	std::printf("  horn held %.4f\n", Honk);
	Check(Honk > 0.02 && !M.Alive(Hid), "the twin-tone horn sounds while held and stops after");
	Check(M.Peak < 1.0f, "nothing clips");
	WriteWav("sfx_test.wav", Wav);

	std::printf(Failures ? "sfx_test: %d FAILED\n" : "sfx_test: all passed\n", Failures);
	return Failures ? 1 : 0;
}
