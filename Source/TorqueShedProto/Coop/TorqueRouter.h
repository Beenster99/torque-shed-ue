// While hosting: asks the home router to open the game's port (UPnP, Coop/Upnp.h) and finds the
// address friends over the internet join with, as Torque Shed does. If the router won't (UPnP off,
// or no router that speaks it), the host is told to forward the port by hand, and the public
// address comes from a "what's my IP" service instead.
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
		Off,        // not hosting
		Checking,   // asking the router
		Open,       // the router opened the port for us
		Closed,     // it didn't: forward the port by hand
	};

	~FTorqueRouter();

	// Start asking (does nothing if already open or checking for this port).
	void Open(int32 Port, const FString& LocalIp);
	// Hosting has ended: take the port back off the router.
	void Close();

	EState GetState() const { return State; }
	// The public address friends over the internet use ("" until known).
	const FString& PublicAddress() const { return PublicIp; }
	// Why the router said no, for the log and the menu ("" if it didn't).
	const FString& Problem() const { return Why; }

private:
	EState State = EState::Off;
	int32 MappedPort = 0, WantPort = 0;
	FString LocalIp, PublicIp, Why;
	FString ServiceType, ControlUrl;
	FSocket* Socket = nullptr;
	double SearchLeft = 0.0, ResendIn = 0.0;
	FTSTicker::FDelegateHandle Ticker;
	int32 Generation = 0;   // bumped by Close, so late answers from an earlier Open are ignored

	bool Poll(float Dt);
	void SendSearch();
	void StopSearch();
	void Describe(const FString& Location);
	void Map();
	void AskExternalIp();
	void Fail(const FString& Reason);
	void LookUpPublicIp();
	void Soap(const FString& Action, const FString& Body, TFunction<void(bool, const FString&)> Then);
};
