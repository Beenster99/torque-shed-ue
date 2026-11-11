// The shed radio's decoder (TorqueAudio/RadioStream): an MP3 in Icecast framing, fed in odd-sized
// pieces as a download brings it, must come out as the right sound with the station's song titles;
// and dropping a station mid-stream, over and over (beenster clicking through stations, build 134
// froze the PC), must cost next to nothing and never wait.
//
//   radio_test                 the tests on SimTest/data/radio_tone.mp3 (6 s: 440 Hz left, 660 Hz right)
//   radio_test STREAM METAINT  also decodes a captured stream (e.g. curl -H "Icy-MetaData: 1" a SomaFM
//                              station for a few seconds; METAINT is its icy-metaint header)
#include "RadioStream.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int Fails = 0;
void Check(bool Ok, const char* What)
{
	std::printf("%s  %s\n", Ok ? "PASS" : "FAIL", What);
	if (!Ok) ++Fails;
}

std::vector<uint8_t> Load(const std::string& Path)
{
	std::ifstream F(Path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
}

// The MP3 as an Icecast server sends it: MetaInt audio bytes, a length byte, the metadata (padded to 16s).
std::vector<uint8_t> Icy(const std::vector<uint8_t>& Mp3, int MetaInt, const std::vector<std::string>& Titles)
{
	std::vector<uint8_t> Out;
	size_t At = 0, Block = 0;
	while (At < Mp3.size())
	{
		const size_t Take = std::min(Mp3.size() - At, (size_t)MetaInt);
		Out.insert(Out.end(), Mp3.begin() + (std::ptrdiff_t)At, Mp3.begin() + (std::ptrdiff_t)(At + Take));
		At += Take;
		if (Take < (size_t)MetaInt) break;
		// Every other block says nothing (length 0), as servers do between song changes.
		if (Block % 2 == 1 || Titles.empty())
			Out.push_back(0);
		else
		{
			std::string M = "StreamTitle='" + Titles[(Block / 2) % Titles.size()] + "';StreamUrl='';";
			const size_t Len = (M.size() + 15) / 16;
			M.resize(Len * 16, '\0');
			Out.push_back((uint8_t)Len);
			Out.insert(Out.end(), M.begin(), M.end());
		}
		++Block;
	}
	return Out;
}

struct FResult
{
	std::vector<int16_t> Pcm;
	std::vector<std::string> Titles;
	int Rate = 0, Chans = 0;
};

// Fed in pieces of 1 .. MaxPiece bytes, decoding a little after each, like the game's frames.
FResult Run(const std::vector<uint8_t>& Stream, int MetaInt, size_t MaxPiece, unsigned Seed)
{
	FResult R;
	TorqueRadio::FIcyMp3Decoder D;
	D.SetMetaInt(MetaInt);
	size_t At = 0;
	unsigned S = Seed;
	while (At < Stream.size())
	{
		S = S * 1664525u + 1013904223u;
		const size_t Piece = std::min(Stream.size() - At, 1 + (size_t)(S >> 8) % MaxPiece);
		D.Push(Stream.data() + At, Piece);
		At += Piece;
		D.Decode(R.Pcm, 2048);
		std::string T;
		if (D.TakeTitle(T)) R.Titles.push_back(T);
	}
	while (D.Decode(R.Pcm, 1 << 20) > 0) {}
	R.Rate = D.SampleRate();
	R.Chans = D.Channels();
	return R;
}

// The tone's pitch on one channel, from its rising zero crossings.
double Pitch(const FResult& R, int Chan)
{
	if (R.Chans <= 0 || R.Rate <= 0) return 0.0;
	const size_t N = R.Pcm.size() / (size_t)R.Chans;
	int Crossings = 0;
	size_t First = 0, Last = 0;
	for (size_t I = (size_t)R.Rate / 4 + 1; I + (size_t)R.Rate / 4 < N; ++I)
	{
		const int A = R.Pcm[(I - 1) * (size_t)R.Chans + (size_t)Chan], B = R.Pcm[I * (size_t)R.Chans + (size_t)Chan];
		if (A < 0 && B >= 0)
		{
			if (Crossings == 0) First = I;
			Last = I;
			++Crossings;
		}
	}
	return Crossings > 1 ? (Crossings - 1) * (double)R.Rate / (double)(Last - First) : 0.0;
}
}

int main(int Argc, char** Argv)
{
	const std::vector<uint8_t> Tone = Load(TORQUE_SIMTEST "/data/radio_tone.mp3");
	Check(Tone.size() > 50000, "the test tone loads");
	const std::vector<std::string> Titles = { "Torque Shed - Test One", "Somebody - Caf\xc3\xa9 Racer" };

	// 1. Icecast framing in odd pieces: right length, format, pitches and titles.
	{
		const FResult R = Run(Icy(Tone, 8000, Titles), 8000, 5000, 7);
		const double Secs = R.Chans > 0 && R.Rate > 0 ? (double)R.Pcm.size() / R.Chans / R.Rate : 0.0;
		std::printf("      %.3f s at %d Hz, %d channels, left %.1f Hz, right %.1f Hz, %zu titles\n", Secs, R.Rate, R.Chans, Pitch(R, 0), Pitch(R, 1), R.Titles.size());
		Check(R.Rate == 44100 && R.Chans == 2, "44.1 kHz stereo");
		Check(std::fabs(Secs - 6.0) < 0.1, "all 6 s of it decode");
		Check(std::fabs(Pitch(R, 0) - 440.0) < 2.0 && std::fabs(Pitch(R, 1) - 660.0) < 2.0, "the left is 440 Hz and the right 660 Hz (no metadata leaked into the sound)");
		Check(R.Titles.size() >= 2 && R.Titles[0] == Titles[0] && R.Titles[1] == Titles[1], "the song titles come through, UTF-8 and all");
	}
	// 2. Byte by byte, and a metadata block split across pieces.
	{
		const FResult R = Run(Icy(Tone, 3000, Titles), 3000, 1, 1);
		Check(R.Pcm.size() > 44100 * 2 * 5 && R.Titles.size() >= 2 && R.Titles[0] == Titles[0], "fed a byte at a time, the same");
	}
	// 3. No metadata (icy-metaint 0), and joined mid-frame (a stream starts wherever it starts).
	{
		const std::vector<uint8_t> Mid(Tone.begin() + 777, Tone.end());
		const FResult R = Run(Mid, 0, 4096, 3);
		const double Secs = R.Chans > 0 && R.Rate > 0 ? (double)R.Pcm.size() / R.Chans / R.Rate : 0.0;
		Check(Secs > 5.5 && std::fabs(Pitch(R, 0) - 440.0) < 2.0, "no metadata and joined mid-frame: still plays");
	}
	// 4. Junk is not sound: random bytes decode to nothing (and don't crash).
	{
		std::vector<uint8_t> Junk(200000);
		unsigned S = 99;
		for (uint8_t& B : Junk) { S = S * 1664525u + 1013904223u; B = (uint8_t)(S >> 24); }
		const FResult R = Run(Junk, 0, 8192, 5);
		Check(R.Pcm.size() < 44100, "random bytes make (almost) no sound");
	}
	// 5. Clicking through stations: 5000 stations started and dropped mid-stream, each fed and decoded a
	// little first. The game does this on its game thread, so it must be quick and never wait.
	{
		const std::vector<uint8_t> Stream = Icy(Tone, 8000, Titles);
		const auto T0 = std::chrono::steady_clock::now();
		double Worst = 0.0;
		size_t Made = 0;
		for (int I = 0; I < 5000; ++I)
		{
			const auto A = std::chrono::steady_clock::now();
			TorqueRadio::FIcyMp3Decoder D;
			D.SetMetaInt(8000);
			const size_t Cut = 20000 + (size_t)(I * 37) % 20000;
			D.Push(Stream.data(), Cut);
			std::vector<int16_t> Pcm;
			D.Decode(Pcm, 4608);
			Made += Pcm.size();
			const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - A).count();
			if (Ms > Worst) Worst = Ms;
		}
		const double Total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		std::printf("      5000 station changes in %.0f ms, the slowest %.2f ms\n", Total, Worst);
		Check(Made > 0, "each one decoded something before it was dropped");
		Check(Worst < 20.0, "no station change takes more than 20 ms (a frame)");
	}
	// 6. A captured live stream, when given.
	if (Argc >= 3)
	{
		const std::vector<uint8_t> Live = Load(Argv[1]);
		const FResult R = Run(Live, std::atoi(Argv[2]), 16384, 11);
		const double Secs = R.Chans > 0 && R.Rate > 0 ? (double)R.Pcm.size() / R.Chans / R.Rate : 0.0;
		std::printf("      %s: %.1f s at %d Hz, %d ch; titles:", Argv[1], Secs, R.Rate, R.Chans);
		for (const std::string& T : R.Titles) std::printf(" [%s]", T.c_str());
		std::printf("\n");
		Check(Secs > 1.0, "the captured stream decodes");
	}
	std::printf(Fails == 0 ? "radio_test: all passed\n" : "radio_test: %d FAILED\n", Fails);
	return Fails == 0 ? 0 : 1;
}
