#include "SonyPad.h"

#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
constexpr uint16 SONY = 0x054C;
bool IsDualSense(uint16 Pid) { return Pid == 0x0CE6 || Pid == 0x0DF2; }
bool IsDualShock4(uint16 Pid) { return Pid == 0x05C4 || Pid == 0x09CC || Pid == 0x0BA0; }

float StickAxis(uint8 V) { return FMath::Clamp((float(V) - 127.5f) / 127.5f, -1.0f, 1.0f); }

uint32 Hat(uint8 H)
{
	using namespace ESonyButton;
	switch (H & 0x0F)
	{
	case 0: return DUp;
	case 1: return DUp | DRight;
	case 2: return DRight;
	case 3: return DRight | DDown;
	case 4: return DDown;
	case 5: return DDown | DLeft;
	case 6: return DLeft;
	case 7: return DLeft | DUp;
	default: return 0;
	}
}

// The common part of every report: sticks at S, then (DualSense) triggers and buttons, or
// (DualShock 4) buttons then triggers.
bool Parse(const uint8* R, int32 Len, bool bDualSense, bool bBluetooth, FSonyPadState& Out)
{
	using namespace ESonyButton;
	int32 S = -1;   // offset of LX
	bool bSimple = false;
	if (bDualSense)
	{
		if (R[0] == 0x01 && Len >= 10) { S = 1; bSimple = bBluetooth; }  // USB, or Bluetooth's simple report
		else if (R[0] == 0x31 && Len >= 12) S = 2;                       // Bluetooth, full report
	}
	else
	{
		if (R[0] == 0x01 && Len >= 10) S = 1;          // USB, and Bluetooth simple
		else if (R[0] == 0x11 && Len >= 12) S = 3;     // Bluetooth, full report
	}
	if (S < 0) return false;
	Out.LX = StickAxis(R[S]);
	Out.LY = -StickAxis(R[S + 1]);
	Out.RX = StickAxis(R[S + 2]);
	Out.RY = -StickAxis(R[S + 3]);
	uint8 B0, B1, B2;
	if (bDualSense && !bSimple)
	{
		Out.L2 = R[S + 4] / 255.0f;
		Out.R2 = R[S + 5] / 255.0f;
		B0 = R[S + 7]; B1 = R[S + 8]; B2 = R[S + 9];
	}
	else
	{
		B0 = R[S + 4]; B1 = R[S + 5]; B2 = R[S + 6];
		Out.L2 = R[S + 7] / 255.0f;
		Out.R2 = R[S + 8] / 255.0f;
	}
	uint32 Bt = Hat(B0);
	if (B0 & 0x10) Bt |= Square;
	if (B0 & 0x20) Bt |= Cross;
	if (B0 & 0x40) Bt |= Circle;
	if (B0 & 0x80) Bt |= Triangle;
	if (B1 & 0x01) Bt |= L1;
	if (B1 & 0x02) Bt |= R1;
	if (B1 & 0x10) Bt |= Create;
	if (B1 & 0x20) Bt |= Options;
	if (B1 & 0x40) Bt |= L3;
	if (B1 & 0x80) Bt |= R3;
	if (B2 & 0x01) Bt |= PS;
	Out.Buttons = Bt;
	return true;
}
} // namespace

class FSonyPad::FWorker : public FRunnable
{
public:
	FThreadSafeBool bStop;
	mutable FCriticalSection Lock;
	FSonyPadState State;

	virtual uint32 Run() override
	{
#if PLATFORM_WINDOWS
		while (!bStop)
		{
			if (!Open())
			{
				// Look again for a pad every couple of seconds.
				for (int32 I = 0; I < 20 && !bStop; ++I) FPlatformProcess::Sleep(0.1f);
				continue;
			}
			ReadLoop();
			Close();
		}
#endif
		return 0;
	}

private:
#if PLATFORM_WINDOWS
	HANDLE Device = INVALID_HANDLE_VALUE;
	bool bDualSense = true;
	int32 ReportLen = 64;

	bool Open()
	{
		GUID HidGuid;
		HidD_GetHidGuid(&HidGuid);
		HDEVINFO Set = SetupDiGetClassDevsW(&HidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (Set == INVALID_HANDLE_VALUE) return false;
		bool bFound = false;
		SP_DEVICE_INTERFACE_DATA Iface;
		Iface.cbSize = sizeof(Iface);
		for (DWORD I = 0; !bFound && SetupDiEnumDeviceInterfaces(Set, nullptr, &HidGuid, I, &Iface); ++I)
		{
			DWORD Need = 0;
			SetupDiGetDeviceInterfaceDetailW(Set, &Iface, nullptr, 0, &Need, nullptr);
			if (Need == 0) continue;
			TArray<uint8> Buf;
			Buf.SetNumZeroed(Need);
			auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(Buf.GetData());
			Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(Set, &Iface, Detail, Need, nullptr, nullptr)) continue;
			HANDLE H = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (H == INVALID_HANDLE_VALUE) continue;
			HIDD_ATTRIBUTES Attr;
			Attr.Size = sizeof(Attr);
			if (HidD_GetAttributes(H, &Attr) && Attr.VendorID == SONY &&
				(IsDualSense(Attr.ProductID) || IsDualShock4(Attr.ProductID)))
			{
				PHIDP_PREPARSED_DATA Pre = nullptr;
				HIDP_CAPS Caps;
				FMemory::Memzero(Caps);
				if (HidD_GetPreparsedData(H, &Pre))
				{
					HidP_GetCaps(Pre, &Caps);
					HidD_FreePreparsedData(Pre);
				}
				// Only the gamepad's own collection (usage page 1, usage 5: game pad).
				if (Caps.UsagePage == 0x01 && Caps.Usage == 0x05 && Caps.InputReportByteLength > 0)
				{
					Device = H;
					bDualSense = IsDualSense(Attr.ProductID);
					ReportLen = Caps.InputReportByteLength;
					// Over Bluetooth a pad sends a short report until something reads its calibration;
					// asking for that feature report switches it to the full one.
					uint8 Feature[64] = {0};
					Feature[0] = 0x05;
					HidD_GetFeature(H, Feature, 41);
					bFound = true;
					UE_LOG(LogTemp, Log, TEXT("Torque: %s connected (report %d bytes)"),
						bDualSense ? TEXT("DualSense") : TEXT("DualShock 4"), ReportLen);
					continue;
				}
			}
			CloseHandle(H);
		}
		SetupDiDestroyDeviceInfoList(Set);
		return bFound;
	}

	void ReadLoop()
	{
		TArray<uint8> Buf;
		Buf.SetNumZeroed(FMath::Max(ReportLen, 128));
		OVERLAPPED Ov;
		FMemory::Memzero(Ov);
		Ov.hEvent = CreateEventW(nullptr, true, false, nullptr);
		while (!bStop)
		{
			ResetEvent(Ov.hEvent);
			DWORD Got = 0;
			if (!ReadFile(Device, Buf.GetData(), ReportLen, &Got, &Ov))
			{
				if (GetLastError() != ERROR_IO_PENDING) break;   // unplugged
				const DWORD W = WaitForSingleObject(Ov.hEvent, 200);
				if (W == WAIT_TIMEOUT)
				{
					CancelIo(Device);
					GetOverlappedResult(Device, &Ov, &Got, true);
					continue;
				}
				if (!GetOverlappedResult(Device, &Ov, &Got, false)) break;
			}
			FSonyPadState S;
			if (Parse(Buf.GetData(), (int32)Got, bDualSense, ReportLen != 64, S))
			{
				S.bConnected = true;
				FScopeLock L(&Lock);
				State = S;
			}
		}
		CloseHandle(Ov.hEvent);
	}

	void Close()
	{
		if (Device != INVALID_HANDLE_VALUE) CloseHandle(Device);
		Device = INVALID_HANDLE_VALUE;
		FScopeLock L(&Lock);
		State = FSonyPadState();
	}
#endif
};

FSonyPad::FSonyPad()
{
	Worker = new FWorker();
	Thread = FRunnableThread::Create(Worker, TEXT("TorqueSonyPad"), 0, TPri_AboveNormal);
}

FSonyPad::~FSonyPad()
{
	if (Worker) Worker->bStop = true;
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
	}
	delete Worker;
}

FSonyPadState FSonyPad::Get() const
{
	FScopeLock L(&Worker->Lock);
	return Worker->State;
}
