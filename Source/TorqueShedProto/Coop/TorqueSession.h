// Starting, hosting and joining rides, and moving everyone between the shed and the road.
//
// Co-op is a listen server: the host's game is also the server, so there's nothing else to run.
// As in Torque Shed: friends type the host's address (or a host name) and join. Hosting asks the
// router to open the port itself (UPnP, TorqueRouter), and the pause menu shows the host exactly
// which address to give friends. On the same network the menu also lists rides by itself (Unreal's
// Null online subsystem, LAN). See Docs/coop.md.
//
// Epic Online Services (TorqueSessionEos.cpp), once switched on ([TorqueCoop] bUseEOS=True in
// DefaultGame.ini, the OnlineSubsystemEOS plugin and the product's IDs: Docs/coop.md): the host also
// gets a six-letter friend code, and friends join with it from anywhere, through EOS's relay (P2P),
// with no port forwarding. LAN and join-by-address stay as they are.
//
// One per game (a GameInstanceSubsystem), so it lives across map changes.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TorqueSession.generated.h"

class FOnlineSessionSearch;
class FTorqueRouter;
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
	// EOS: join the ride whose host shows this code.
	void JoinByCode(const FString& Code);
	// Whether EOS is switched on and its plugin loaded (the menu offers friend codes).
	bool HasEos() const;
	// The code friends join this host's ride with (empty until EOS has made the session).
	FString FriendCode;
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

	// The rider's name (-TorqueName=, else Settings > Character, else the Windows account name) and
	// this machine's address on the network, for telling friends.
	FString PlayerName() const;
	FString LocalAddress() const { return LocalAddr; }
	// Every address this PC has on its networks (wired and wireless), for friends at home.
	TArray<FString> LocalAddresses() const;
	// The port the game listens on (7777 unless the URL says otherwise).
	static int32 GamePort();

	// What to tell friends, line by line, for the pause menu and the HUD while hosting (as Torque
	// Shed's Esc menu): the address at home, whether the router opened the port, and the address over
	// the internet. Empty when not hosting.
	TArray<FString> JoinInstructions() const;
	bool IsHosting() const;
	// The address a friend over the internet types ("" until known).
	FString InternetAddress() const;
	// The last address joined (kept in Saved/Config, GameUserSettings).
	FString LastAddress;
	// What went wrong last (lost connection, couldn't join...), for the menu. Empty if nothing.
	FString Status;

private:
	TSharedPtr<FTorqueRouter> Router;
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FLanGame> LanGames;
	bool bSearching = false;
	FString LocalAddr;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, NetFailHandle, TravelFailHandle;

	// EOS (TorqueSessionEos.cpp).
	class IOnlineSubsystem* Eos() const;
	void EosLogin(TFunction<void(bool)> Then);
	void OnEosLogin(int32 LocalUserNum, bool bOk, const FUniqueNetId& UserId, const FString& Error);
	void CreateEosSession();
	void OnEosCreated(FName SessionName, bool bOk);
	void OnCodeFound(bool bOk);
	void OnEosJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void LeaveEosSession();
	TFunction<void(bool)> PendingLogin;
	bool bTriedPortal = false;
	FString SearchCode;
	TSharedPtr<FOnlineSessionSearch> CodeSearch;
	FDelegateHandle LoginHandle, EosCreateHandle, EosFindHandle, EosJoinHandle;

	void OnCreated(FName SessionName, bool bOk);
	void OnFound(bool bOk);
	void OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Failure, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error);
	void Travel(const FString& Url);
	FString NameOption() const;
};
