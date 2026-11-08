// UTorqueSession's Epic Online Services half: sign in, host with a friend code, join by code.
// Everything goes through Unreal's online subsystem interfaces, asked for the "EOS" subsystem by
// name, so this compiles and does nothing until EOS is switched on (Docs/coop.md): Null stays the
// default for the LAN list.
#include "TorqueSession.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
const FName EosName(TEXT("EOS"));
const FName CodeKey(TEXT("TORQUE_CODE"));
const FName EosHostKey(TEXT("TORQUE_HOST"));

// Six letters and digits a friend can read out without mixing up (no 0/O, 1/I).
FString NewFriendCode()
{
	static const TCHAR* Chars = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	const int32 N = FCString::Strlen(Chars);
	FString Code;
	for (int32 I = 0; I < 6; ++I) Code.AppendChar(Chars[FMath::RandRange(0, N - 1)]);
	return Code;
}
} // namespace

bool UTorqueSession::HasEos() const
{
	return Eos() != nullptr;
}

IOnlineSubsystem* UTorqueSession::Eos() const
{
	bool bUse = false;
	if (!GConfig || !GConfig->GetBool(TEXT("TorqueCoop"), TEXT("bUseEOS"), bUse, GGameIni) || !bUse) return nullptr;
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World ? Online::GetSubsystem(World, EosName) : nullptr;
}

void UTorqueSession::EosLogin(TFunction<void(bool)> Then)
{
	IOnlineSubsystem* E = Eos();
	IOnlineIdentityPtr Identity = E ? E->GetIdentityInterface() : nullptr;
	if (!Identity)
	{
		Then(false);
		return;
	}
	if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		Then(true);
		return;
	}
	if (PendingLogin)
	{
		// Already signing in: run both when it's done.
		TFunction<void(bool)> First = MoveTemp(PendingLogin);
		PendingLogin = [First, Then](bool bOk) { First(bOk); Then(bOk); };
		return;
	}
	PendingLogin = MoveTemp(Then);
	bTriedPortal = false;
	Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
	LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0, FOnLoginCompleteDelegate::CreateUObject(this, &UTorqueSession::OnEosLogin));
	// Signed in before on this PC: no questions. Otherwise Epic's sign-in page opens once.
	FOnlineAccountCredentials Remembered;
	Remembered.Type = TEXT("persistentauth");
	UE_LOG(LogTemp, Log, TEXT("Torque net: signing in to Epic"));
	if (!Identity->Login(0, Remembered))
	{
		bTriedPortal = true;
		FOnlineAccountCredentials Portal;
		Portal.Type = TEXT("accountportal");
		if (!Identity->Login(0, Portal))
		{
			Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
			TFunction<void(bool)> Done = MoveTemp(PendingLogin);
			PendingLogin = nullptr;
			if (Done) Done(false);
		}
	}
}

void UTorqueSession::OnEosLogin(int32 LocalUserNum, bool bOk, const FUniqueNetId& UserId, const FString& Error)
{
	(void)UserId;
	IOnlineSubsystem* E = Eos();
	IOnlineIdentityPtr Identity = E ? E->GetIdentityInterface() : nullptr;
	if (!bOk && !bTriedPortal && Identity)
	{
		// Nothing remembered: Epic's sign-in page.
		bTriedPortal = true;
		FOnlineAccountCredentials Portal;
		Portal.Type = TEXT("accountportal");
		if (Identity->Login(LocalUserNum, Portal)) return;
	}
	if (Identity) Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
	UE_LOG(LogTemp, Log, TEXT("Torque net: Epic sign-in %s %s"), bOk ? TEXT("done") : TEXT("failed:"), *Error);
	if (!bOk) Status = FString(TEXT("Couldn't sign in to Epic")) + (Error.IsEmpty() ? FString() : FString(TEXT(": ")) + Error);
	TFunction<void(bool)> Done = MoveTemp(PendingLogin);
	PendingLogin = nullptr;
	if (Done) Done(bOk);
}

void UTorqueSession::CreateEosSession()
{
	IOnlineSubsystem* E = Eos();
	IOnlineSessionPtr Sessions = E ? E->GetSessionInterface() : nullptr;
	if (!Sessions || Sessions->GetNamedSession(NAME_GameSession)) return;   // already (after a map change)
	FriendCode = NewFriendCode();
	FOnlineSessionSettings S;
	S.NumPublicConnections = 8;
	S.bShouldAdvertise = true;
	S.bIsLANMatch = false;
	S.bAllowJoinInProgress = true;
	S.bUsesPresence = false;
	S.bAllowJoinViaPresence = false;
	S.bUseLobbiesIfAvailable = false;
	S.Set(CodeKey, FriendCode, EOnlineDataAdvertisementType::ViaOnlineService);
	S.Set(EosHostKey, PlayerName(), EOnlineDataAdvertisementType::ViaOnlineService);
	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(EosCreateHandle);
	EosCreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UTorqueSession::OnEosCreated));
	if (!Sessions->CreateSession(0, NAME_GameSession, S)) FriendCode.Reset();
}

void UTorqueSession::OnEosCreated(FName SessionName, bool bOk)
{
	(void)SessionName;
	if (IOnlineSubsystem* E = Eos())
		if (IOnlineSessionPtr Sessions = E->GetSessionInterface()) Sessions->ClearOnCreateSessionCompleteDelegate_Handle(EosCreateHandle);
	if (!bOk) FriendCode.Reset();
	UE_LOG(LogTemp, Log, TEXT("Torque net: EOS session %s, friend code %s"), bOk ? TEXT("up") : TEXT("failed"),
		FriendCode.IsEmpty() ? TEXT("none") : *FriendCode);
}

void UTorqueSession::JoinByCode(const FString& Code)
{
	SearchCode = Code.TrimStartAndEnd().ToUpper();
	if (SearchCode.Len() != 6)
	{
		Status = TEXT("A friend code is six letters and digits");
		return;
	}
	if (!HasEos())
	{
		Status = TEXT("Friend codes need Epic Online Services, which isn't switched on");
		return;
	}
	Status.Reset();
	LeaveSession();
	TWeakObjectPtr<UTorqueSession> Self(this);
	EosLogin([Self](bool bOk) {
		if (!Self.IsValid() || !bOk) return;
		IOnlineSubsystem* E = Self->Eos();
		IOnlineSessionPtr Sessions = E ? E->GetSessionInterface() : nullptr;
		if (!Sessions) return;
		Self->CodeSearch = MakeShared<FOnlineSessionSearch>();
		Self->CodeSearch->bIsLanQuery = false;
		Self->CodeSearch->MaxSearchResults = 10;
		Self->CodeSearch->QuerySettings.Set(CodeKey, Self->SearchCode, EOnlineComparisonOp::Equals);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(Self->EosFindHandle);
		Self->EosFindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(Self.Get(), &UTorqueSession::OnCodeFound));
		if (!Sessions->FindSessions(0, Self->CodeSearch.ToSharedRef())) Self->Status = TEXT("Couldn't look for that ride");
	});
}

void UTorqueSession::OnCodeFound(bool bOk)
{
	IOnlineSubsystem* E = Eos();
	IOnlineSessionPtr Sessions = E ? E->GetSessionInterface() : nullptr;
	if (!Sessions) return;
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(EosFindHandle);
	const FOnlineSessionSearchResult* Found = nullptr;
	if (bOk && CodeSearch.IsValid())
	{
		for (const FOnlineSessionSearchResult& R : CodeSearch->SearchResults)
		{
			FString Code;
			if (R.IsValid() && R.Session.SessionSettings.Get(CodeKey, Code) && Code == SearchCode)
			{
				Found = &R;
				break;
			}
		}
	}
	if (!Found)
	{
		Status = FString::Printf(TEXT("No ride with friend code %s (has the host started it?)"), *SearchCode);
		return;
	}
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(EosJoinHandle);
	EosJoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UTorqueSession::OnEosJoined));
	if (!Sessions->JoinSession(0, NAME_GameSession, *Found)) Status = TEXT("Couldn't join that ride");
}

void UTorqueSession::OnEosJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSubsystem* E = Eos();
	IOnlineSessionPtr Sessions = E ? E->GetSessionInterface() : nullptr;
	if (!Sessions) return;
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(EosJoinHandle);
	FString Addr;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(SessionName, Addr) || Addr.IsEmpty())
	{
		Status = TEXT("Couldn't join that ride (it may have ended)");
		return;
	}
	// An EOS P2P address (EOS:<host's id>:GameNetDriver:...): the relay finds the host, no ports.
	Travel(Addr + NameOption());
}

void UTorqueSession::LeaveEosSession()
{
	FriendCode.Reset();
	IOnlineSubsystem* E = Eos();
	IOnlineSessionPtr Sessions = E ? E->GetSessionInterface() : nullptr;
	if (Sessions && Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
}
