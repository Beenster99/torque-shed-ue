#include "TorqueSession.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystem.h"

const TCHAR* UTorqueSession::ShedMap = TEXT("/Game/Shed/Shed");
const TCHAR* UTorqueSession::RideMap = TEXT("/Game/Maps/Buttertubs");

namespace
{
const FName HostKey(TEXT("TORQUE_HOST"));
const TCHAR* ConfigSection = TEXT("TorqueCoop");

IOnlineSessionPtr SessionsFor(const UWorld* World)
{
	return World ? Online::GetSessionInterface(World) : nullptr;
}
} // namespace

void UTorqueSession::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GConfig) GConfig->GetString(ConfigSection, TEXT("LastAddress"), LastAddress, GGameUserSettingsIni);
	if (GEngine)
	{
		NetFailHandle = GEngine->OnNetworkFailure().AddUObject(this, &UTorqueSession::OnNetworkFailure);
		TravelFailHandle = GEngine->OnTravelFailure().AddUObject(this, &UTorqueSession::OnTravelFailure);
	}
	// This machine's address on the network, for the host to tell friends.
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		bool bCanBind = false;
		TSharedRef<FInternetAddr> Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind);
		if (Addr->IsValid()) LocalAddr = Addr->ToString(false);
	}
}

void UTorqueSession::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetFailHandle);
		GEngine->OnTravelFailure().Remove(TravelFailHandle);
	}
	if (IOnlineSessionPtr Sessions = SessionsFor(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}
	Super::Deinitialize();
}

UTorqueSession* UTorqueSession::Get(const UObject* WorldContext)
{
	const UGameInstance* GI = UGameplayStatics::GetGameInstance(WorldContext);
	return GI ? GI->GetSubsystem<UTorqueSession>() : nullptr;
}

FString UTorqueSession::FirstMap()
{
	return FPackageName::DoesPackageExist(ShedMap) ? FString(ShedMap) : FString(RideMap);
}

FString UTorqueSession::PlayerName() const
{
	FString Name;
	if (!FParse::Value(FCommandLine::Get(), TEXT("TorqueName="), Name) || Name.IsEmpty()) Name = FPlatformProcess::UserName(false);
	if (Name.IsEmpty()) Name = TEXT("Rider");
	// It travels in a URL: no separators.
	for (TCHAR& C : Name.GetCharArray())
	{
		if (C == TEXT('?') || C == TEXT('=') || C == TEXT('#') || C == TEXT(' ') || C == TEXT('/')) C = TEXT('_');
	}
	return Name.Left(20);
}

FString UTorqueSession::NameOption() const
{
	// The game mode reads it (ATorqueGameMode::InitNewPlayer); Unreal's own Name= is replaced by the
	// online subsystem's nickname.
	return FString(TEXT("?TorqueName=")) + PlayerName();
}

void UTorqueSession::Travel(const FString& Url)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !GEngine) return;
	UE_LOG(LogTemp, Log, TEXT("Torque net: travel to %s"), *Url);
	GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
}

void UTorqueSession::RideSolo()
{
	Status.Reset();
	LeaveSession();
	// The menu is already over the map the ride starts on: no need to load it again, just swap the
	// menu's camera for the bike (or the walker).
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && World->GetNetMode() == NM_Standalone &&
		UWorld::RemovePIEPrefix(World->GetMapName()) == FPackageName::GetShortName(FirstMap()))
	{
		AGameModeBase* Mode = World->GetAuthGameMode();
		APlayerController* PC = World->GetFirstPlayerController();
		if (Mode && PC)
		{
			Mode->OptionsString += TEXT("?play");   // so the game mode stops choosing the menu
			if (APawn* Old = PC->GetPawn())
			{
				PC->UnPossess();
				Old->Destroy();
			}
			Mode->RestartPlayer(PC);
			return;
		}
	}
	Travel(FirstMap() + TEXT("?play") + NameOption());
}

void UTorqueSession::Host()
{
	Status.Reset();
	LeaveSession();
	// The game mode advertises the ride once the map is up and listening (AdvertiseHostedGame).
	Travel(FirstMap() + TEXT("?listen?play") + NameOption());
}

void UTorqueSession::JoinAddress(const FString& Address)
{
	FString Addr = Address.TrimStartAndEnd();
	if (Addr.IsEmpty())
	{
		Status = TEXT("Type the host's address first (for example 192.168.1.20)");
		return;
	}
	Status.Reset();
	LastAddress = Addr;
	if (GConfig)
	{
		GConfig->SetString(ConfigSection, TEXT("LastAddress"), *LastAddress, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	LeaveSession();
	Travel(Addr + NameOption());   // the port is 7777 unless the address says otherwise
}

void UTorqueSession::JoinLan(int32 Index)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Sessions = SessionsFor(World);
	if (!Sessions || !Search.IsValid() || !Search->SearchResults.IsValidIndex(Index)) return;
	Status.Reset();
	LeaveSession();
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UTorqueSession::OnJoined));
	if (!Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[Index]))
		Status = TEXT("Couldn't join that ride");
}

void UTorqueSession::OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Sessions = SessionsFor(World);
	if (!Sessions) return;
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	FString Addr;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(SessionName, Addr) || Addr.IsEmpty())
	{
		Status = TEXT("Couldn't join that ride (it may have ended)");
		return;
	}
	// LAN replies sometimes come without the game's port.
	if (Addr.EndsWith(TEXT(":0"))) Addr = Addr.LeftChop(2) + TEXT(":7777");
	Travel(Addr + NameOption());
}

void UTorqueSession::FindLanGames()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Sessions = SessionsFor(World);
	if (!Sessions || bSearching) return;
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 20;
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UTorqueSession::OnFound));
	bSearching = Sessions->FindSessions(0, Search.ToSharedRef());
}

void UTorqueSession::OnFound(bool bOk)
{
	bSearching = false;
	LanGames.Reset();
	if (!bOk || !Search.IsValid()) return;
	for (const FOnlineSessionSearchResult& R : Search->SearchResults)
	{
		FLanGame G;
		G.Host = R.Session.OwningUserName;
		FString Named;
		if (R.Session.SessionSettings.Get(HostKey, Named) && !Named.IsEmpty()) G.Host = Named;
		G.MaxRiders = R.Session.SessionSettings.NumPublicConnections;
		G.Riders = FMath::Max(G.MaxRiders - R.Session.NumOpenPublicConnections, 1);
		G.PingMs = R.PingInMs;
		LanGames.Add(G);
	}
}

void UTorqueSession::AdvertiseHostedGame()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Sessions = SessionsFor(World);
	if (!Sessions || Sessions->GetNamedSession(NAME_GameSession)) return;   // already (after a map change)
	FOnlineSessionSettings S;
	S.NumPublicConnections = 8;
	S.bShouldAdvertise = true;
	S.bIsLANMatch = true;           // EOS: false, with lobbies and presence (Docs/coop.md)
	S.bAllowJoinInProgress = true;
	S.bUsesPresence = false;
	S.bAllowJoinViaPresence = false;
	S.bUseLobbiesIfAvailable = false;
	S.Set(HostKey, PlayerName(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UTorqueSession::OnCreated));
	Sessions->CreateSession(0, NAME_GameSession, S);
}

void UTorqueSession::OnCreated(FName SessionName, bool bOk)
{
	UE_LOG(LogTemp, Log, TEXT("Torque net: hosting %s %s; friends join %s (port 7777)"), *SessionName.ToString(),
		bOk ? TEXT("advertised on the network") : TEXT("(not advertised; joining by address still works)"),
		LocalAddr.IsEmpty() ? TEXT("by this PC's address") : *LocalAddr);
}

void UTorqueSession::LeaveSession()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSessionPtr Sessions = SessionsFor(World);
	if (Sessions && Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
}

void UTorqueSession::StartRide(UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return;
	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_Client)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque net: the host starts the ride"));
		return;
	}
	if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
	{
		// Everyone comes too: the game mode's seamless travel keeps each rider connected.
		World->ServerTravel(FString(RideMap) + TEXT("?listen?play"), false);
		return;
	}
	if (UTorqueSession* Self = Get(WorldContext)) Self->Travel(FString(RideMap) + TEXT("?play") + Self->NameOption());
}

void UTorqueSession::GoToShed(UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !FPackageName::DoesPackageExist(ShedMap)) return;
	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_Client) return;
	if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
	{
		World->ServerTravel(FString(ShedMap) + TEXT("?listen?play"), false);
		return;
	}
	if (UTorqueSession* Self = Get(WorldContext)) Self->Travel(FString(ShedMap) + TEXT("?play") + Self->NameOption());
}

void UTorqueSession::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Failure, const FString& Error)
{
	(void)NetDriver;
	const bool bHost = World && World->GetNetMode() == NM_ListenServer;
	if (Failure == ENetworkFailure::NetDriverListenFailure)
		Status = TEXT("Couldn't host: port 7777 is in use (is another copy of the game hosting?)");
	else if (Failure == ENetworkFailure::ConnectionTimeout)
		Status = bHost ? FString() : TEXT("The host stopped answering (timed out)");
	else if (!bHost)
		Status = FString(TEXT("Lost the connection to the host")) + (Error.IsEmpty() ? FString() : FString(TEXT(": ")) + Error);
	UE_LOG(LogTemp, Warning, TEXT("Torque net: network failure %s: %s"), ENetworkFailure::ToString(Failure), *Error);
	// A rider dropping out doesn't end the host's ride.
	if (!bHost || Failure == ENetworkFailure::NetDriverListenFailure) LeaveSession();
}

void UTorqueSession::OnTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error)
{
	(void)World;
	Status = FString(TEXT("Couldn't join: ")) + (Error.IsEmpty() ? FString(ETravelFailure::ToString(Failure)) : Error);
	UE_LOG(LogTemp, Warning, TEXT("Torque net: travel failure %s: %s"), ETravelFailure::ToString(Failure), *Error);
	LeaveSession();
}
