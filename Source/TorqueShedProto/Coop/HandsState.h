// What a player's hands are doing in the shed, for everyone else to see (ATorquePlayerState's
// Hands): the tool held, how far through a job (0 not working), the spray colour and finish, and
// what they're aimed at. The owner offers it every frame; it goes to the host only when it has
// changed in a way the others would see, at most ten times a second for a job's progress, and at
// once for a new tool, starting or stopping work, or a new target.
//
// Plain C++ with no Unreal types, so SimTest can check it (SimTest/net_test.cpp).
#pragma once

#include <cmath>
#include <cstdint>

namespace TorqueHands
{
// A job's progress as it travels: 0 not working, 1..16 working (so a job just begun is still 1).
inline uint8_t QuantiseWork(double Fraction, bool bWorking)
{
	if (!bWorking) return 0;
	const double F = Fraction < 0.0 ? 0.0 : (Fraction > 1.0 ? 1.0 : Fraction);
	const int Step = 1 + (int)std::floor(F * 15.0 + 0.5);
	return (uint8_t)(Step > 16 ? 16 : Step);
}
inline double WorkFraction(uint8_t Work) { return Work == 0 ? 0.0 : (Work - 1) / 15.0; }

// The parts of the state that matter for when to send: Key is everything but the progress (tool,
// colour, finish, target...), compared whole.
struct FSendState
{
	uint64_t Key = 0;
	uint8_t Work = 0;
};

constexpr double ProgressGap = 0.1;   // seconds between progress-only updates
constexpr double Refresh = 2.0;       // and an unchanged state again now and then (a lost one heals)

// Whether to send Now, given what was Sent SinceSent seconds ago.
inline bool ShouldSend(const FSendState& Sent, const FSendState& Now, double SinceSent, bool bEverSent)
{
	if (!bEverSent) return true;
	if (Now.Key != Sent.Key) return true;
	if ((Now.Work == 0) != (Sent.Work == 0)) return true;   // started or stopped
	if (Now.Work != Sent.Work) return SinceSent >= ProgressGap;
	return SinceSent >= Refresh;
}

// A key from small whole values (each 0..255) and two hashes (a slot's and a place's names).
inline uint64_t MakeKey(uint8_t Tool, uint8_t Colour, uint8_t Finish, uint8_t Look, int Bay, uint32_t SlotHash, int Item, uint32_t PlaceHash, bool bFlags)
{
	uint64_t K = 1469598103934665603ull;
	auto Mix = [&K](uint64_t V) { K = (K ^ V) * 1099511628211ull; };
	Mix(Tool); Mix(Colour); Mix(Finish); Mix(Look); Mix((uint64_t)(int64_t)Bay); Mix(SlotHash); Mix((uint64_t)(int64_t)Item); Mix(PlaceHash); Mix(bFlags ? 1 : 0);
	return K;
}
}
