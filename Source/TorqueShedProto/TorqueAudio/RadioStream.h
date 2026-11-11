// The shed radio's decoder (Torque Shed's radio_stream.gd, which decoded the stream itself): the
// raw bytes of an Icecast/SHOUTcast MP3 stream in, the station's "now playing" lines and 16-bit PCM
// out. Plain C++ (no engine), so SimTest's radio_test drives it too.
//
// Why not the engine's media player: closing a Windows Media Foundation player while it is still
// opening or starting a stream blocks the game thread, and with the game fullscreen that froze
// beenster's whole PC (build 134, clicking through stations quickly). Here a station change is just
// dropping a download and this decoder, which never waits on anything.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TorqueRadio
{
class FIcyMp3Decoder
{
public:
	FIcyMp3Decoder();
	~FIcyMp3Decoder();
	FIcyMp3Decoder(const FIcyMp3Decoder&) = delete;
	FIcyMp3Decoder& operator=(const FIcyMp3Decoder&) = delete;

	// The stream's "icy-metaint" header: metadata every MetaInt audio bytes (0: none). Set it
	// before the first Push.
	void SetMetaInt(int MetaInt);
	// The stream's next bytes, as they came.
	void Push(const uint8_t* Data, size_t Num);
	// Decodes up to MaxFrames samples per channel more onto Pcm (interleaved, Channels() wide).
	// Returns the samples per channel added: 0 when it needs more bytes.
	int Decode(std::vector<int16_t>& Pcm, int MaxFrames);
	// The first frame's format (0 until a frame has decoded); later frames are made to match.
	int SampleRate() const { return Rate; }
	int Channels() const { return Chans; }
	// The latest "StreamTitle" the station sent, once (false when nothing new).
	bool TakeTitle(std::string& Out);
	// Whether anything has been pushed yet (SetMetaInt goes before that).
	bool Started() const { return bPushed; }
	// Audio bytes waiting to be decoded.
	size_t Pending() const { return Audio.size() - Read; }
	// Frames decoded and bytes skipped as junk, for tests and the log.
	int FramesDecoded() const { return Frames; }
	size_t JunkSkipped() const { return Junk; }

private:
	struct FState;
	std::unique_ptr<FState> State;
	int Every = 0;              // icy-metaint
	int ToMeta = 0;             // audio bytes left before the next metadata block
	enum class EPart { Audio, Length, Meta } Part = EPart::Audio;
	int MetaLeft = 0;           // metadata bytes still to read
	std::string Meta;
	std::vector<uint8_t> Audio;
	size_t Read = 0;
	int Rate = 0, Chans = 0;
	int Frames = 0;
	size_t Junk = 0;
	std::string Title;
	bool bNewTitle = false;
	bool bPushed = false;
	void OnMeta();
};
}
