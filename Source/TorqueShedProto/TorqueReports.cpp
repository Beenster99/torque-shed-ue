#include "TorqueReports.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <atomic>

namespace TorqueReportsImpl
{
constexpr double FreezeSeconds = 15.0;   // the game thread silent this long: a freeze (or a very long load)

std::atomic<double> LastBeat{0.0};
FTSTicker::FDelegateHandle BeatHandle;

FString BuildStamp()
{
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/build.json")))) return Text.TrimStartAndEnd();
	return TEXT("(no Data/build.json)");
}

FString Stamp()
{
	return FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
}

// A copy of the log as it is now (it's written with others allowed to read it).
void CopyLog(const FString& To)
{
	const FString Log = FPlatformOutputDevices::GetAbsoluteLogFilename();
	if (!Log.IsEmpty() && IFileManager::Get().FileExists(*Log)) IFileManager::Get().Copy(*To, *Log, true, true, true);
}

void WriteFreeze(double Silent)
{
	const FString Dir = FPaths::Combine(TorqueReports::Folder(), TEXT("freeze_") + Stamp());
	IFileManager::Get().MakeDirectory(*Dir, true);
	FString Text = FString::Printf(TEXT("Torque Shed freeze report\nWhen: %s\nThe game thread had not ticked for %.0f s.\nBuild: %s\n\nGame thread call stack:\n"),
		*FDateTime::Now().ToString(), Silent, *BuildStamp());
	static ANSICHAR Stack[64 * 1024];
	Stack[0] = 0;
	FPlatformStackWalk::ThreadStackWalkAndDump(Stack, sizeof(Stack), 0, GGameThreadId);   // (returns void on Windows)
	if (Stack[0]) Text += ANSI_TO_TCHAR(Stack);
	else Text += TEXT("(this platform couldn't walk the game thread's stack)\n");
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, TEXT("freeze.txt")));
	CopyLog(FPaths::Combine(Dir, TEXT("log_at_freeze.log")));
}

// Watches the game thread's heartbeat from a thread of its own.
class FWatchdog : public FRunnable
{
public:
	std::atomic<bool> bStop{false};
	virtual uint32 Run() override
	{
		bool bFrozen = false;
		double FrozenSince = 0.0;
		while (!bStop.load())
		{
			FPlatformProcess::Sleep(1.0f);
			const double Beat = LastBeat.load();
			if (Beat <= 0.0) continue;   // not ticking yet
			const double Silent = FPlatformTime::Seconds() - Beat;
			if (!bFrozen && Silent >= FreezeSeconds)
			{
				bFrozen = true;
				FrozenSince = Beat;
				WriteFreeze(Silent);
			}
			else if (bFrozen && Silent < 2.0)
			{
				// It came back (a long load, or a window being dragged): say so next to the report.
				bFrozen = false;
				UE_LOG(LogTemp, Warning, TEXT("Torque reports: the game thread came back after %.0f s (a freeze report was written)"),
					FPlatformTime::Seconds() - FrozenSince);
			}
		}
		return 0;
	}
	virtual void Stop() override { bStop.store(true); }
};

FWatchdog* Dog = nullptr;
FRunnableThread* DogThread = nullptr;

// Last session's crash (UE's own crash folders), and a log that never closed: copied in once each.
void CollectLastTime()
{
	IFileManager& Files = IFileManager::Get();
	const FString Out = TorqueReports::Folder();
	const FString SeenFile = FPaths::Combine(Out, TEXT("seen.txt"));
	FString SeenText;
	FFileHelper::LoadFileToString(SeenText, *SeenFile);
	TArray<FString> Seen;
	SeenText.ParseIntoArrayLines(Seen);
	bool bChanged = false;

	const FString Crashes = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes"));
	TArray<FString> Dirs;
	Files.FindFiles(Dirs, *FPaths::Combine(Crashes, TEXT("*")), false, true);
	for (const FString& D : Dirs)
	{
		if (Seen.Contains(D)) continue;
		Seen.Add(D);
		bChanged = true;
		const FString From = FPaths::Combine(Crashes, D), To = FPaths::Combine(Out, TEXT("crash_") + D);
		// Only a real crash (hub, 156: a non-fatal ensure writes a crash folder too and the game carries on):
		// an ensure, or a hang the engine noticed but lived through, is a log line here, not a report.
		FString Context;
		FFileHelper::LoadFileToString(Context, *FPaths::Combine(From, TEXT("CrashContext.runtime-xml")));
		if (Context.Contains(TEXT("<IsEnsure>true</IsEnsure>")) || Context.Contains(TEXT("<CrashType>Ensure</CrashType>")))
		{
			UE_LOG(LogTemp, Log, TEXT("Torque reports: %s was an ensure (the game carried on): not a crash report"), *D);
			continue;
		}
		TArray<FString> Inside;
		Files.FindFiles(Inside, *FPaths::Combine(From, TEXT("*")), true, false);
		for (const FString& F : Inside)
			if (Files.FileSize(*FPaths::Combine(From, F)) < 300LL * 1024 * 1024) Files.Copy(*FPaths::Combine(To, F), *FPaths::Combine(From, F), true, true);
		UE_LOG(LogTemp, Warning, TEXT("Torque reports: last time's crash %s copied to %s"), *D, *To);
	}

	// The newest backed-up log is last session's: if it never says it closed, the game was killed or froze.
	const FString Logs = FPaths::ProjectLogDir();
	TArray<FString> Backups;
	// (our own game's logs only: not the web browser's cef3 logs that share the folder)
	Files.FindFiles(Backups, *FPaths::Combine(Logs, FString(FApp::GetProjectName()) + TEXT("-backup-*.log")), true, false);
	Backups.Sort();
	if (Backups.Num() > 0 && !Seen.Contains(Backups.Last()))
	{
		const FString Path = FPaths::Combine(Logs, Backups.Last());
		Seen.Add(Backups.Last());
		bChanged = true;
		FString Tail;
		if (FFileHelper::LoadFileToString(Tail, *Path))
		{
			Tail = Tail.Right(4096);
			if (!Tail.Contains(TEXT("Log file closed")))
			{
				Files.Copy(*FPaths::Combine(Out, TEXT("unclosed_") + Backups.Last()), *Path, true, true);
				UE_LOG(LogTemp, Warning, TEXT("Torque reports: last session's log %s never closed (a crash, a freeze or the game was killed): copied"), *Backups.Last());
			}
		}
	}
	if (bChanged) FFileHelper::SaveStringToFile(FString::Join(Seen, TEXT("\n")), *SeenFile);
}
} // namespace TorqueReportsImpl

namespace TRI = TorqueReportsImpl;

FString TorqueReports::Folder()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TorqueReports"));
}

int32 TorqueReports::Count()
{
	// (asked every frame the menu's up: looked at every couple of seconds)
	static double CheckedAt = -100.0;
	static int32 Last = 0;
	const double Now = FPlatformTime::Seconds();
	if (Now - CheckedAt < 2.0) return Last;
	CheckedAt = Now;
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *FPaths::Combine(Folder(), TEXT("*")), true, true);
	int32 N = 0;
	for (const FString& F : Found)
		if (F != TEXT("seen.txt")) ++N;
	Last = N;
	return N;
}

void TorqueReports::OpenFolder()
{
	const FString Dir = FPaths::ConvertRelativePathToFull(Folder());
	IFileManager::Get().MakeDirectory(*Dir, true);
	FPlatformProcess::ExploreFolder(*Dir);
}

void TorqueReports::Start()
{
	if (GIsEditor || IsRunningCommandlet() || TRI::Dog) return;
	IFileManager::Get().MakeDirectory(*Folder(), true);
	TRI::CollectLastTime();
	TRI::BeatHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) {
		TRI::LastBeat.store(FPlatformTime::Seconds());
		return true;
	}), 0.0f);
	TRI::Dog = new TRI::FWatchdog();
	TRI::DogThread = FRunnableThread::Create(TRI::Dog, TEXT("TorqueWatchdog"), 0, TPri_BelowNormal);
	UE_LOG(LogTemp, Display, TEXT("Torque reports: watching for freezes (%.0f s); reports in %s"), TRI::FreezeSeconds, *Folder());
}

void TorqueReports::Stop()
{
	if (TRI::BeatHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TRI::BeatHandle);
	TRI::BeatHandle.Reset();
	if (TRI::DogThread)
	{
		TRI::DogThread->Kill(true);
		delete TRI::DogThread;
		TRI::DogThread = nullptr;
	}
	delete TRI::Dog;
	TRI::Dog = nullptr;
}
