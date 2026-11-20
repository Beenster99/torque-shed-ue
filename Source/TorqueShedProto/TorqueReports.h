#pragma once

#include "CoreMinimal.h"

// Crash and freeze reports (beenster, build 155: "my friends game crashed - can we create 'freeze logs'
// to help you fix?"). In a played game (not the editor):
// - a watchdog thread notices the game thread stop for FreezeSeconds and writes a freeze report: the
//   build, how long, the game thread's call stack, and a copy of the log so far;
// - at start, a crash from last time (UE's Saved/Crashes folder, or a log that never closed) is copied
//   into the same reports folder.
// All of it goes in Saved/TorqueReports: Settings > Gameplay > Crash and freeze reports opens it, to zip
// up and send.
namespace TorqueReports
{
void Start();
void Stop();
FString Folder();
int32 Count();            // reports in the folder
void OpenFolder();
}
