// Starting, hosting and joining rides, and moving everyone between the shed and the road.
//
// Co-op is a listen server: the host's game is also the server, so there's nothing else to run.
// Friends join by the host's IP address (port 7777, which the host's router must forward for a ride
// over the internet), or on the same network from a list the menu finds by itself (Unreal's Null
// online subsystem, LAN). Epic Online Services can later replace the Null subsystem so friends join
// without port forwarding: the calls here (create, find, join a session) are the ones EOS uses too.
// See Docs/coop.md.
//
// One per game (a GameInstanceSubsystem), so it lives across map changes.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TorqueSession.generated.h"

class FOnlineSessionSearch;
class UNetDriver;

UCLASS()
class UTorqueSession : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UTorqueSession* Get(const UObject* WorldContext);

	// The maps. The game starts in the shed when it's been built (Tools/ue_build_shed.py), else on
	// the road.
	static const TCHAR* ShedMap;
	static const TCHAR* RideMap;
	static FString FirstMap();

	// From the start menu.
	void RideSolo();
	void Host();
	void JoinAddress(const FString& Address);
	void JoinLan(int32 Index);
	void FindLanGames();
	// Back at the menu: leave any ride hosted or joined before.
	void LeaveSession();

	// From the shed's "ride out" (TorqueFlow::RideOut) and back: in co-op the host takes everyone
	// (a seamless server travel); a joined rider waits for the host; alone, just go.
	static void StartRide(UObject* WorldContext);
	static void GoToShed(UObject* WorldContext);

	// The game mode, when a host's world is up: advertise the ride on the network.
	void AdvertiseHostedGame();

	struct FLanGame
	{
		FString Host;
		int32 Riders = 0;
		int32 MaxRiders = 0;
		int32 PingMs = 0;
	};
	const TArray<FLanGame>& GetLanGames() const { return LanGames; }
	bool IsSearching() const { return bSearching; }

	// The rider's name (-TorqueName=, else the Windows account name) and this machine's address on
	// the network, for telling friends.
	FString PlayerName() const;
	FString LocalAddress() const { return LocalAddr; }
	// The last address joined (kept in Saved/Config, GameUserSettings).
	FString LastAddress;
	// What went wrong last (lost connection, couldn't join...), for the menu. Empty if nothing.
	FString Status;

private:
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FLanGame> LanGames;
	bool bSearching = false;
	FString LocalAddr;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, NetFailHandle, TravelFailHandle;

	void OnCreated(FName SessionName, bool bOk);
	void OnFound(bool bOk);
	void OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Failure, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error);
	void Travel(const FString& Url);
	FString NameOption() const;
};
