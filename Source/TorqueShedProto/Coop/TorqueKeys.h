// Torque Shed's Key bindings (settings.gd ACTIONS, keybind_tab.gd): every action you can rebind, with
// its key or mouse button. Each action has two slots (W and Up both throttle, say). Two actions clash
// when they share a key and a place they're used: on foot, or riding. Only what differs from the
// defaults is saved (Saved/Config/<platform>/GameUserSettings.ini, [TorqueKeys]), so new defaults
// reach everything else, as Torque Shed does.
//
// The pad buttons (an Xbox pad through Unreal, a PlayStation pad read directly) stay as they are.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;

enum class ETorqueKey : uint8
{
	// On foot.
	WalkForward, WalkBack, WalkLeft, WalkRight, Run, Crouch, Jump,
	Work, AltWork, Use, NextTool, PrevTool, Tool1, Tool2, Tool3, Tool4, Tool5, Tool6, SprayFinish, GeometryMode,
	// Riding.
	Throttle, FrontBrake, RearBrake, LeanLeft, LeanRight, WeightBack, WeightForward, ShiftUp, ShiftDown, AutoShift, Abs, Clutch, Ignition,
	Camera, CamCloser, CamFarther, CamHigher, CamLower, CamTiltUp, CamTiltDown, LookBack, BackOnRoad, Restart, RealismDown, RealismUp, Weather, Horn, BackToShed, Telemetry,
	// Both.
	Help, Perf, PerfLog,
	Count
};

struct FTorqueKeys
{
	enum : uint8 { Foot = 1, Ride = 2 };
	static constexpr int32 Slots = 2;

	struct FAction
	{
		const TCHAR* Id;      // the ini key
		const TCHAR* Label;   // what it does, as the Key bindings page lists it
		uint8 Where;          // Foot, Ride or both
		FKey Default[Slots];
	};
	static const FAction& Action(ETorqueKey A);

	// The key in a slot (an invalid FKey when it's empty).
	static FKey Get(ETorqueKey A, int32 Slot);
	static void Set(ETorqueKey A, int32 Slot, const FKey& Key);
	static void ResetAll();
	static void Save();

	// Held, or pressed this frame, on either slot.
	static bool Down(const APlayerController* PC, ETorqueKey A);
	static bool Just(const APlayerController* PC, ETorqueKey A);

	// "W / Up", for the help lines and the Key bindings page.
	static FString Text(ETorqueKey A);
	static FString KeyName(const FKey& Key);
	// The first other action that shares one of its keys in a place both are used ("" for none).
	static FString Clash(ETorqueKey A);
	// Keys a binding can take: the keyboard and the mouse's buttons and wheel (not the pads, not Esc).
	static bool Bindable(const FKey& Key);

private:
	static TArray<FKey>& Table();   // Count * Slots, loaded the first time
};
