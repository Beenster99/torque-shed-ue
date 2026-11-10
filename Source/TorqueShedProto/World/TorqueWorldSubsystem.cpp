#include "World/TorqueWorldSubsystem.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include "World/CellStore.h"
#include "World/HeightTiles.h"
#include "World/WorldFiles.h"
#include "World/WorldLoad.h"

#include <atomic>

struct FTorqueWorldShared
{
	TorqueWorld::CellStore Cells;
	TorqueWorld::HeightStore Heights;
	std::atomic<bool> bStop{false};   // the game is closing: every wait gives up
};

namespace TWSI
{
// One request's answer, filled on the game thread and read by the worker that asked.
struct FAnswer
{
	FCriticalSection Lock;
	bool bDone = false;
	int32 Code = 0;
	TArray<uint8> Bytes;
};

static std::string ToStd(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

// GETs a URL (redirects followed): the HTTP code, 0 for no answer, and the body. Blocks, so never
// on the game thread, which is the one that has to send it.
static int BlockingGet(const std::shared_ptr<FTorqueWorldShared>& Shared, const std::string& Url, std::string& Body)
{
	Body.clear();
	if (IsInGameThread())
	{
		UE_LOG(LogTemp, Error, TEXT("Torque world: a fetch was asked for on the game thread and refused (%s)"), UTF8_TO_TCHAR(Url.c_str()));
		return 0;
	}
	if (Shared->bStop) return 0;
	TSharedRef<FAnswer, ESPMode::ThreadSafe> Answer = MakeShared<FAnswer, ESPMode::ThreadSafe>();
	const FString Address(UTF8_TO_TCHAR(Url.c_str()));
	const FString Agent(UTF8_TO_TCHAR(TorqueWorld::CellStore::UserAgent));
	AsyncTask(ENamedThreads::GameThread, [Answer, Address, Agent]()
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Address);
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("User-Agent"), Agent);
		Req->SetTimeout(120.0f);
		Req->OnProcessRequestComplete().BindLambda([Answer](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			FScopeLock Hold(&Answer->Lock);
			if (bConnected && Response.IsValid())
			{
				Answer->Code = Response->GetResponseCode();
				Answer->Bytes = Response->GetContent();
			}
			Answer->bDone = true;
		});
		if (!Req->ProcessRequest())
		{
			FScopeLock Hold(&Answer->Lock);
			Answer->bDone = true;
		}
	});
	// (a little longer than the request's own limit, in case the game thread was slow to send it)
	const double GiveUpAt = FPlatformTime::Seconds() + 150.0;
	for (;;)
	{
		{
			FScopeLock Hold(&Answer->Lock);
			if (Answer->bDone)
			{
				if (Answer->Bytes.Num() > 0) Body.assign(reinterpret_cast<const char*>(Answer->Bytes.GetData()), size_t(Answer->Bytes.Num()));
				return int(Answer->Code);
			}
		}
		if (Shared->bStop || FPlatformTime::Seconds() > GiveUpAt) return 0;
		FPlatformProcess::Sleep(0.005f);
	}
}
} // namespace TWSI

void UTorqueWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Shared = std::make_shared<FTorqueWorldShared>();
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World")));
	Shared->Cells.Dir = TWSI::ToStd(FPaths::Combine(Root, TEXT("cells")));
	Shared->Heights.Disk = TWSI::ToStd(FPaths::Combine(Root, TEXT("heights")));
	// (a weak hold: a fetch asked for after the game has closed finds nothing to ask)
	std::weak_ptr<FTorqueWorldShared> Weak = Shared;
	Shared->Cells.Get = [Weak](const std::string& Url, std::string& Body)
	{
		const std::shared_ptr<FTorqueWorldShared> Held = Weak.lock();
		return Held ? TWSI::BlockingGet(Held, Url, Body) : 0;
	};
	FString Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueWorldTest="), Spec, false)) StartTest(Spec);
}

void UTorqueWorldSubsystem::Deinitialize()
{
	if (Shared) Shared->bStop = true;
	if (TestRun.IsValid()) TestRun.Wait();
	Shared.reset();
	Super::Deinitialize();
}

TorqueWorld::CellStore& UTorqueWorldSubsystem::Cells()
{
	return Shared->Cells;
}

TorqueWorld::HeightStore& UTorqueWorldSubsystem::Heights()
{
	return Shared->Heights;
}

void UTorqueWorldSubsystem::StartTest(const FString& Spec)
{
	// Hawes market place to Leyburn market place, unless four numbers say otherwise.
	double Ends[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	TArray<FString> Given;
	Spec.ParseIntoArray(Given, TEXT(","), true);
	if (Given.Num() == 4)
		for (int32 I = 0; I < 4; ++I) Ends[I] = FCString::Atod(*Given[I].TrimStartAndEnd());
	const bool bQuit = FParse::Param(FCommandLine::Get(), TEXT("TorqueWorldTestQuit"));
	const std::string ReportPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("world_test.json"))));
	UE_LOG(LogTemp, Display, TEXT("Torque world test: from %.4f, %.4f to %.4f, %.4f"), Ends[0], Ends[1], Ends[2], Ends[3]);
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	const double LatA = Ends[0], LonA = Ends[1], LatB = Ends[2], LonB = Ends[3];
	TestRun = Async(EAsyncExecution::Thread, [Held, LatA, LonA, LatB, LonB, ReportPath, bQuit]()
	{
		const double StartedAt = FPlatformTime::Seconds();
		const TorqueWorld::RideCheck R = TorqueWorld::CheckRide(Held->Cells, Held->Heights, LatA, LonA, LatB, LonB, [](const std::string& Step)
		{
			UE_LOG(LogTemp, Display, TEXT("Torque world test: %s"), UTF8_TO_TCHAR(Step.c_str()));
		});
		const double Took = FPlatformTime::Seconds() - StartedAt;
		if (Held->bStop) return;   // (the game closed under it: no verdict)
		TorqueWorld::Files::Write(ReportPath, R.Dump());
		UE_LOG(LogTemp, Display, TEXT("Torque world test: %d cells, %d junctions, %d roads; %.1f km by road (%.1f km straight); %d of %d points have ground, %.0f m to %.0f m, %.0f m of climbing, steepest 1 in %.1f; %d tiles, %d failed; %.1f s"),
			R.Cells, R.Junctions, R.Roads, R.Km, R.CrowKm, R.WithGround, R.Points, R.Low, R.High, R.Climb, R.Steepest > 0.0 ? 1.0 / R.Steepest : 0.0, R.Tiles, R.TilesFailed, Took);
		if (R.Ok) { UE_LOG(LogTemp, Display, TEXT("Torque world test: PASS")); }
		else { UE_LOG(LogTemp, Error, TEXT("Torque world test: FAIL: %s"), UTF8_TO_TCHAR(R.Why.c_str())); }
		if (bQuit) AsyncTask(ENamedThreads::GameThread, []() { FPlatformMisc::RequestExit(false, TEXT("TorqueWorldTest")); });
	});
}
