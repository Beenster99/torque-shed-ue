// While hosting: asks the home router to open the game's port (UPnP, Coop/Upnp.h) and finds the
// address friends over the internet join with, as Torque Shed does. If the router won't (UPnP off,
// or no router that speaks it), the host is told to forward the port by hand, and the public
// address comes from a "what's my IP" service instead.
//
// Careful with the player's router:
// - only when the player hosts for real (UTorqueSession decides: not on test runs, and only with
//   Settings > Gameplay > "Open my router for internet friends" on);
// - the mapping has a two hour lease, renewed every hour while hosting, so a crash can't leave the
//   port open for ever (routers that only take permanent ones get one, and the next point covers it);
// - it's removed when hosting ends and when the game closes (waiting up to two seconds for that);
// - a mapping of ours left behind (the game was killed) is noted in GameUserSettings.ini and
//   removed at the next start-up (CleanUpLeftover).
//
// Owned by UTorqueSession; everything runs on the game thread (a socket polled by a ticker, and
// Unreal's HTTP requests).
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FSocket;

class FTorqueRouter : public TSharedFromThis<FTorqueRouter>
{
public:
	enum class EState : uint8
	{
		Off,        // not hosting (or not asked)
		Checking,   // asking the router
		Open,       // the router opened the port for us
		Closed,     // it didn't: forward the port by hand
	};

	~FTorqueRouter();

	// Start asking (does nothing if already open or checking for this port).
	void Open(int32 Port, const FString& LocalIp);
	// Not asking (a test run, or the setting off): say why in the join instructions.
	void Skip(const FString& Reason);
	// Hosting has ended: take the port back off the router. bWait: block (at most two seconds)
	// until the router has answered, for the game closing.
	void Close(bool bWait = false);
	// At start-up: if a mapping of ours was left behind last time, remove it.
	void CleanUpLeftover(const FString& LocalIp);

	EState GetState() const { return State; }
	// The public address friends over the internet use ("" until known).
	const FString& PublicAddress() const { return PublicIp; }
	// Why the router said no, or why it wasn't asked ("" if neither).
	const FString& Problem() const { return Why; }

private:
	EState State = EState::Off;
	bool bCleanUp = false;      // this search is to remove a leftover, not to open
	bool bPermanent = false;    // the router only took a lease of 0
	int32 MappedPort = 0, WantPort = 0;
	FString LocalIp, PublicIp, Why;
	FString ServiceType, ControlUrl;
	FSocket* Socket = nullptr;
	double SearchLeft = 0.0, ResendIn = 0.0;
	FTSTicker::FDelegateHandle Ticker, RenewTicker;
	int32 Generation = 0;   // bumped by Close, so late answers from an earlier Open are ignored

	bool StartSearch();
	bool Poll(float Dt);
	void SendSearch();
	void StopSearch();
	void Describe(const FString& Location);
	void Map(int32 LeaseSeconds);
	bool Renew(float Dt);
	void AskExternalIp();
	void RemoveLeftover();
	void Fail(const FString& Reason);
	void LookUpPublicIp();
	TSharedRef<class IHttpRequest, ESPMode::ThreadSafe> Soap(const FString& Action, const FString& Body, TFunction<void(bool, const FString&)> Then);
	static void Remember(int32 Port, const FString& Ip);   // the leftover note in GameUserSettings.ini
};
