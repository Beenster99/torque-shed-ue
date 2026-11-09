// Torque Shed's sound rules (scripts/sfx.gd's tables and choices), in plain C++ with no Unreal types
// so SimTest can check them (SimTest/sfx_test.cpp): which loop each job and tool makes, the sound
// when a job's done, machines' start and stop sounds, fades and detune, each room's acoustics, how
// much a wall or a doorway takes off, footsteps by floor and parts landing by material.
//
// The rooms are the Unreal shed's, in Unreal cm as Tools/ue_build_shed.py lays them out: keep
// RoomAt in step with it.
#pragma once

#include <cstring>
#include <string>

namespace TorqueSfxRules
{
struct FPair
{
	const char* Key;
	const char* Value;
};

inline const char* Lookup(const FPair* Table, int Count, const std::string& Key)
{
	for (int I = 0; I < Count; ++I)
		if (Key == Table[I].Key) return Table[I].Value;
	return nullptr;
}
#define TORQUE_SFX_LOOKUP(Table, Key) Lookup(Table, (int)(sizeof(Table) / sizeof(Table[0])), Key)

// sfx.gd WORK_LOOPS: the loop each kind of work makes while you hold the button.
inline const char* WorkLoop(const std::string& Action)
{
	static const FPair T[] = {{"unbolt", "ratchet"}, {"bolt", "ratchet"}, {"clean", "scrub"}, {"clean_many", "scrub"},
		{"repair", "grinder"}, {"paint", "spray"}, {"pour", "pour"}, {"drain", "pour"}, {"tip", "pour"}, {"wash", "jet"},
		{"repair_many", "grinder"}, {"adjust_chain", "ratchet"}, {"lube", "spray"}, {"tyre_fit", "changer"}, {"tyre_remove", "changer"}};
	const char* V = TORQUE_SFX_LOOKUP(T, Action);
	return V ? V : "";
}

// sfx.gd TOOL_LOOPS: the loop a tool makes in someone else's hands while they work.
inline const char* ToolLoop(const std::string& Tool)
{
	static const FPair T[] = {{"wrench", "ratchet"}, {"brush", "scrub"}, {"repair", "grinder"}, {"spray", "spray"},
		{"lance", "jet"}, {"gun", "spray_gun"}, {"polisher", "polisher"}};
	const char* V = TORQUE_SFX_LOOKUP(T, Tool);
	return V ? V : "";
}

// sfx.gd DONE_SOUNDS: the one-shot when a job is done (putting a part down plays an impact).
inline const char* DoneSound(const std::string& Action)
{
	static const FPair T[] = {{"unbolt", "clink"}, {"bolt", "clink"}, {"remove", "pickup"}, {"install", "clunk"},
		{"take", "pickup"}, {"place", "clunk"}, {"drop", "clunk"}, {"set_down", "clunk"}, {"bin", "scrap"},
		{"order_part", "clunk"}, {"set_stand", "clunk"}, {"adjust_chain", "clink"}, {"tyre_remove", "bead_pop"},
		{"coffee_tray_take", "pickup"}, {"coffee_tray_down", "clunk"}};
	const char* V = TORQUE_SFX_LOOKUP(T, Action);
	return V ? V : "";
}

// sfx.gd LOOP_TAILS: a machine's start-up and run-down either side of its loop.
inline bool LoopTails(const std::string& Loop, std::string& Up, std::string& Down)
{
	static const char* const T[][3] = {{"grinder", "grinder_up", "grinder_down"}, {"jet", "jet_on", "jet_off"},
		{"spray_gun", "gun_on", "gun_off"}, {"door", "door_start", "door_stop"}, {"washer", "pump_on", "pump_off"},
		{"blast", "blast_on", "blast_off"}, {"compressor", "comp_on", "comp_off"}};
	for (const auto& R : T)
		if (Loop == R[0])
		{
			Up = R[1];
			Down = R[2];
			return true;
		}
	return false;
}

// sfx.gd LOOP_FADE_IN: seconds a machine's loop takes to come in (its start sound covers it).
inline double LoopFadeIn(const std::string& Loop)
{
	static const char* const N[] = {"shaker", "grinder", "jet", "spray_gun", "compressor", "blast", "fan"};
	static const double S[] = {0.5, 0.45, 0.15, 0.12, 0.6, 0.1, 1.4};
	for (int I = 0; I < 7; ++I)
		if (Loop == N[I]) return S[I];
	return 0.0;
}

// sfx.gd LOOP_DETUNE: loops that get a small random pitch each time they start.
inline bool LoopDetunes(const std::string& Loop)
{
	static const char* const N[] = {"ratchet", "scrub", "grinder", "polisher", "spray", "spray_gun", "jet", "washer",
		"blast", "changer", "pour"};
	for (const char* S : N)
		if (Loop == S) return true;
	return false;
}

// sfx.gd: the ear defenders, the walls and the far bus.
constexpr double MuffleCutoff = 520.0, MuffleDb = -12.0;
constexpr double WallDb = -7.0, DoorwayDb = -3.5, FarCutoff = 1100.0;
constexpr double OneShotMax = 8.0;
constexpr int PhoneRings = 3;
constexpr double PhoneGap = 2.2;

struct FPreset
{
	double Size, Damp, Wet, Pre, Hp, Spread;
};

// sfx.gd ROOM_PRESETS, by kind of room.
inline FPreset RoomPreset(const std::string& Kind)
{
	if (Kind == "paint") return {0.22, 0.12, 0.26, 5.0, 0.15, 0.6};
	if (Kind == "storage") return {0.32, 0.7, 0.12, 8.0, 0.1, 0.7};
	if (Kind == "dyno") return {0.38, 0.25, 0.22, 10.0, 0.1, 0.7};
	if (Kind == "sales") return {0.62, 0.18, 0.22, 24.0, 0.15, 1.0};
	if (Kind == "office") return {0.28, 0.75, 0.1, 6.0, 0.1, 0.6};
	if (Kind == "outside") return {0.15, 0.85, 0.035, 0.0, 0.2, 1.0};
	return {0.55, 0.35, 0.2, 18.0, 0.1, 0.8};   // "shed"
}

// sfx.gd ROOM_KIND: each room's acoustics. The Unreal shed's mixing bay (Torque Shed falls back to
// the shed's) gets the paint shop's: it's the same small tiled room.
inline std::string RoomKind(const std::string& Room)
{
	if (Room.empty()) return "outside";
	if (Room == "paint" || Room == "mixing") return "paint";
	if (Room == "storage") return "storage";
	if (Room == "dyno") return "dyno";
	return "shed";
}

// Which room a point is in (Unreal cm), "" outside: Workshop.room_at for the Unreal shed's rooms,
// as Tools/ue_build_shed.py builds them (the shell, Storage, PaintShop, PaintBay, Dyno).
inline std::string RoomAt(double X, double Y, double Z)
{
	if (Z > 420.0) return "";
	auto In = [X, Y](double X0, double X1, double Y0, double Y1) { return X >= X0 && X <= X1 && Y >= Y0 && Y <= Y1; };
	if (In(-650.0, 650.0, -550.0, 550.0)) return "shed";
	if (In(680.0, 1400.0, -520.0, 520.0)) return "paint";
	if (In(1400.0, 1700.0, -520.0, 520.0)) return "mixing";
	if (In(-1500.0, -650.0, -1100.0, -280.0)) return "storage";
	if (In(-450.0, 450.0, -1300.0, -550.0)) return "dyno";
	return "";
}

// The shed's own rectangle (cm), for the slap back off its wall when you're outside.
constexpr double ShedX0 = -650.0, ShedX1 = 650.0, ShedY0 = -550.0, ShedY1 = 550.0, ShedHeight = 600.0;

// sfx.gd occlusion_db: nothing in the same room, a little through an open doorway, more through a wall.
inline double OcclusionDb(const std::string& Room, const std::string& Listener, bool bDoorOpen)
{
	if (Room == Listener) return 0.0;
	auto Pair = [&Room, &Listener](const char* A, const char* B) { return (Room == A && Listener == B) || (Room == B && Listener == A); };
	if (Pair("shed", "paint") || Pair("shed", "storage") || Pair("shed", "dyno")) return DoorwayDb;
	if (Pair("shed", "") && bDoorOpen) return DoorwayDb;
	if (Pair("paint", "mixing")) return DoorwayDb;   // the Unreal mixing bay's doorway off the paint shop
	return WallDb;
}

// sfx.gd impact_material: what a part's material sounds like when it lands.
inline std::string ImpactMaterial(const std::string& M)
{
	if (M == "metal" || M == "plastic" || M == "rubber" || M == "soft" || M == "cardboard") return M;
	if (M == "electronics" || M == "carbon" || M == "mixed" || M == "wood") return "plastic";
	if (M == "fabric" || M == "leather") return "soft";
	return "metal";
}

// sfx.gd step_surface for the Unreal shed's rooms (no epoxy floor or sales office yet).
inline std::string StepSurface(const std::string& Room, const std::string& Ground)
{
	if (Room == "shed" || Room == "storage" || Room == "dyno") return "concrete";
	if (Room == "paint" || Room == "mixing") return "tile";
	if (Ground == "grass") return "grass";
	if (Ground == "dirt" || Ground == "gravel") return "gravel";
	return "tarmac";
}
} // namespace TorqueSfxRules
