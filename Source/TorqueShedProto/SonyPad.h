// Reads a PlayStation pad (DualSense, DualSense Edge, DualShock 4) straight from Windows HID, over
// USB or Bluetooth, so it works without DS4Windows or Steam. Unreal on Windows only reads Xbox-style
// (XInput) pads by itself. A background thread keeps the latest state; the game reads it each frame.
#pragma once

#include "CoreMinimal.h"

struct FSonyPadState
{
	bool bConnected = false;
	float LX = 0, LY = 0, RX = 0, RY = 0;   // -1 .. 1, +y is up
	float L2 = 0, R2 = 0;                   // 0 .. 1
	uint32 Buttons = 0;                     // ESonyButton bits
};

namespace ESonyButton
{
enum : uint32
{
	Square = 1 << 0, Cross = 1 << 1, Circle = 1 << 2, Triangle = 1 << 3,
	L1 = 1 << 4, R1 = 1 << 5, Create = 1 << 6, Options = 1 << 7, L3 = 1 << 8, R3 = 1 << 9,
	DUp = 1 << 10, DRight = 1 << 11, DDown = 1 << 12, DLeft = 1 << 13, PS = 1 << 14,
};
}

class FSonyPad
{
public:
	FSonyPad();
	~FSonyPad();
	FSonyPadState Get() const;

private:
	class FWorker;
	FWorker* Worker = nullptr;
	class FRunnableThread* Thread = nullptr;
};
