#include "RadioStream.h"

#include <algorithm>
#include <cstring>

// minimp3 (lieff/minimp3, CC0): a small, self-contained MP3 decoder, compiled here only.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4005 4100 4127 4189 4244 4245 4267 4310 4389 4456 4457 4458 4459 4701 4703 4706)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define MINIMP3_IMPLEMENTATION
#include "ThirdParty/minimp3/minimp3.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace TorqueRadio
{
struct FIcyMp3Decoder::FState
{
	mp3dec_t Dec;
	int16_t Frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

FIcyMp3Decoder::FIcyMp3Decoder()
	: State(new FState())
{
	mp3dec_init(&State->Dec);
}

FIcyMp3Decoder::~FIcyMp3Decoder() = default;

void FIcyMp3Decoder::SetMetaInt(int MetaInt)
{
	Every = std::max(0, MetaInt);
	ToMeta = Every;
	Part = EPart::Audio;
}

void FIcyMp3Decoder::Push(const uint8_t* Data, size_t Num)
{
	if (!Data) return;
	bPushed = true;
	// Drop what has been decoded now and then, so the buffer doesn't grow for ever.
	if (Read > 65536 && Read * 2 > Audio.size())
	{
		Audio.erase(Audio.begin(), Audio.begin() + (std::ptrdiff_t)Read);
		Read = 0;
	}
	if (Every <= 0)
	{
		Audio.insert(Audio.end(), Data, Data + Num);
		return;
	}
	size_t I = 0;
	while (I < Num)
	{
		if (Part == EPart::Audio)
		{
			const size_t Take = std::min(Num - I, (size_t)ToMeta);
			Audio.insert(Audio.end(), Data + I, Data + I + Take);
			I += Take;
			ToMeta -= (int)Take;
			if (ToMeta == 0) Part = EPart::Length;
		}
		else if (Part == EPart::Length)
		{
			// The block's length in 16s (0: nothing said this time).
			MetaLeft = Data[I++] * 16;
			Meta.clear();
			if (MetaLeft > 0) Part = EPart::Meta;
			else
			{
				Part = EPart::Audio;
				ToMeta = Every;
			}
		}
		else
		{
			const size_t Take = std::min(Num - I, (size_t)MetaLeft);
			Meta.append((const char*)Data + I, Take);
			I += Take;
			MetaLeft -= (int)Take;
			if (MetaLeft == 0)
			{
				OnMeta();
				Part = EPart::Audio;
				ToMeta = Every;
			}
		}
	}
}

void FIcyMp3Decoder::OnMeta()
{
	// StreamTitle='Artist - Song';StreamUrl='...';  (padded with zeros)
	std::string M = Meta;
	const size_t Zero = M.find('\0');
	if (Zero != std::string::npos) M.resize(Zero);
	const std::string Key = "StreamTitle='";
	const size_t From = M.find(Key);
	if (From == std::string::npos) return;
	const size_t Start = From + Key.size();
	size_t End = M.find("';", Start);
	if (End == std::string::npos) End = M.rfind('\'');
	if (End == std::string::npos || End < Start) return;
	std::string T = M.substr(Start, End - Start);
	while (!T.empty() && (T.back() == ' ' || T.back() == '\t')) T.pop_back();
	while (!T.empty() && (T.front() == ' ' || T.front() == '\t')) T.erase(T.begin());
	if (!T.empty() && T != Title)
	{
		Title = T;
		bNewTitle = true;
	}
}

bool FIcyMp3Decoder::TakeTitle(std::string& Out)
{
	if (!bNewTitle) return false;
	bNewTitle = false;
	Out = Title;
	return true;
}

int FIcyMp3Decoder::Decode(std::vector<int16_t>& Pcm, int MaxFrames)
{
	int Made = 0;
	while (Made < MaxFrames)
	{
		const size_t Have = Audio.size() - Read;
		// minimp3 wants a few frames to find its footing at the start; after that, one will do.
		if (Have < (Frames == 0 ? (size_t)16384 : (size_t)1441)) break;
		mp3dec_frame_info_t Info;
		const int Got = mp3dec_decode_frame(&State->Dec, Audio.data() + Read, (int)std::min<size_t>(Have, 1 << 20), State->Frame, &Info);
		if (Info.frame_bytes <= 0) break;   // nothing whole in there yet
		Read += (size_t)Info.frame_bytes;
		if (Got <= 0)
		{
			Junk += (size_t)Info.frame_bytes;   // skipped junk or a frame it couldn't use
			continue;
		}
		if (Rate == 0)
		{
			Rate = Info.hz;
			Chans = Info.channels >= 2 ? 2 : 1;
		}
		++Frames;
		const int In = Info.channels >= 2 ? 2 : 1;
		const size_t At = Pcm.size();
		Pcm.resize(At + (size_t)Got * (size_t)Chans);
		int16_t* Out = Pcm.data() + At;
		for (int S = 0; S < Got; ++S)
		{
			if (In == Chans)
				for (int C = 0; C < Chans; ++C) Out[S * Chans + C] = State->Frame[S * In + C];
			else if (Chans == 2)
				Out[S * 2] = Out[S * 2 + 1] = State->Frame[S];
			else
				Out[S] = (int16_t)((State->Frame[S * 2] + State->Frame[S * 2 + 1]) / 2);
		}
		Made += Got;
	}
	return Made;
}
}
