#include "TorqueRouter.h"

#include "Upnp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString FromStd(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
constexpr double SearchSeconds = 3.0;
} // namespace

FTorqueRouter::~FTorqueRouter()
{
	StopSearch();
}

void FTorqueRouter::Open(int32 Port, const FString& InLocalIp)
{
	if ((State == EState::Open || State == EState::Checking) && WantPort == Port) return;
	WantPort = Port;
	LocalIp = InLocalIp;
	PublicIp.Reset();
	Why.Reset();
	State = EState::Checking;
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueNoUpnp")))
	{
		Fail(TEXT("not asked (-TorqueNoUpnp)"));
		return;
	}
	if (!LocalIp.IsEmpty() && !TorqueUpnp::IsPrivate(ToStd(LocalIp)))
	{
		// A PC with a public address of its own: no router in the way.
		State = EState::Open;
		PublicIp = LocalIp;
		return;
	}
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		Fail(TEXT("no sockets"));
		return;
	}
	StopSearch();
	Socket = Sockets->CreateSocket(NAME_DGram, TEXT("Torque UPnP search"), FNetworkProtocolTypes::IPv4);
	if (!Socket)
	{
		Fail(TEXT("couldn't make a socket"));
		return;
	}
	Socket->SetNonBlocking(true);
	Socket->SetMulticastTtl(2);
	TSharedRef<FInternetAddr> Any = Sockets->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	Any->SetAnyAddress();
	Any->SetPort(0);
	Socket->Bind(*Any);
	SearchLeft = SearchSeconds;
	ResendIn = 0.0;
	UE_LOG(LogTemp, Log, TEXT("Torque net: asking the router to open UDP port %d"), Port);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FTorqueRouter::Poll), 0.05f);
}

void FTorqueRouter::SendSearch()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Socket || !Sockets) return;
	TSharedRef<FInternetAddr> To = Sockets->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	bool bValid = false;
	To->SetIp(UTF8_TO_TCHAR(TorqueUpnp::SearchAddress()), bValid);
	To->SetPort(TorqueUpnp::SearchPort());
	const std::string Msg = TorqueUpnp::SearchMessage();
	int32 Sent = 0;
	Socket->SendTo(reinterpret_cast<const uint8*>(Msg.data()), (int32)Msg.size(), Sent, *To);
}

bool FTorqueRouter::Poll(float Dt)
{
	if (!Socket) return false;
	ResendIn -= Dt;
	if (ResendIn <= 0.0)
	{
		SendSearch();   // twice or three times: the first can be lost
		ResendIn = 1.0;
	}
	uint32 Pending = 0;
	while (Socket && Socket->HasPendingData(Pending))
	{
		TArray<uint8> Buf;
		Buf.SetNumZeroed(FMath::Clamp<int32>((int32)Pending, 1, 8192) + 1);
		int32 Read = 0;
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		TSharedRef<FInternetAddr> From = Sockets->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		if (!Socket->RecvFrom(Buf.GetData(), Buf.Num() - 1, Read, *From) || Read <= 0) break;
		const std::string Reply(reinterpret_cast<const char*>(Buf.GetData()), (size_t)Read);
		const std::string Location = TorqueUpnp::ParseLocation(Reply);
		if (!Location.empty())
		{
			StopSearch();
			Describe(FromStd(Location));
			return false;
		}
	}
	SearchLeft -= Dt;
	if (SearchLeft <= 0.0)
	{
		StopSearch();
		Fail(TEXT("no router answered (UPnP may be switched off on it)"));
		return false;
	}
	return true;
}

void FTorqueRouter::StopSearch()
{
	if (Ticker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		Ticker.Reset();
	}
	if (Socket)
	{
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) Sockets->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FTorqueRouter::Describe(const FString& Location)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Location);
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(5.0f);
	TWeakPtr<FTorqueRouter> Weak = AsShared();
	const int32 Gen = Generation;
	Req->OnProcessRequestComplete().BindLambda([Weak, Gen, Location](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk) {
		TSharedPtr<FTorqueRouter> Self = Weak.Pin();
		if (!Self || Self->Generation != Gen) return;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
		{
			Self->Fail(TEXT("the router's description couldn't be read"));
			return;
		}
		const TorqueUpnp::FService S = TorqueUpnp::ParseDescription(ToStd(Resp->GetContentAsString()), ToStd(Location));
		if (!S.Valid())
		{
			Self->Fail(TEXT("the router doesn't offer port opening"));
			return;
		}
		Self->ServiceType = FromStd(S.Type);
		Self->ControlUrl = FromStd(S.ControlUrl);
		Self->Map();
	});
	Req->ProcessRequest();
}

void FTorqueRouter::Soap(const FString& Action, const FString& Body, TFunction<void(bool, const FString&)> Then)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(ControlUrl);
	Req->SetVerb(TEXT("POST"));
	Req->SetTimeout(5.0f);
	Req->SetHeader(TEXT("Content-Type"), TEXT("text/xml; charset=\"utf-8\""));
	Req->SetHeader(TEXT("SOAPAction"), FromStd(TorqueUpnp::SoapAction(ToStd(ServiceType), ToStd(Action))));
	Req->SetContentAsString(Body);
	Req->OnProcessRequestComplete().BindLambda([Then](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk) {
		const FString Text = Resp.IsValid() ? Resp->GetContentAsString() : FString();
		std::string Code;
		const bool bFault = TorqueUpnp::IsFault(ToStd(Text), &Code);
		const bool bGood = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 && !bFault;
		Then(bGood, bGood ? Text : (bFault ? FString::Printf(TEXT("error %s"), *FromStd(Code)) : FString(TEXT("no answer"))));
	});
	Req->ProcessRequest();
}

void FTorqueRouter::Map()
{
	TWeakPtr<FTorqueRouter> Weak = AsShared();
	const int32 Gen = Generation;
	const int32 Port = WantPort;
	Soap(TEXT("AddPortMapping"), FromStd(TorqueUpnp::AddPortMapping(ToStd(ServiceType), Port, ToStd(LocalIp), "Torque Shed")),
		[Weak, Gen, Port](bool bOk, const FString& Answer) {
			TSharedPtr<FTorqueRouter> Self = Weak.Pin();
			if (!Self || Self->Generation != Gen) return;
			if (!bOk)
			{
				Self->Fail(FString::Printf(TEXT("the router said no (%s)"), *Answer));
				return;
			}
			Self->MappedPort = Port;
			Self->State = EState::Open;
			UE_LOG(LogTemp, Log, TEXT("Torque net: the router opened UDP port %d"), Port);
			Self->AskExternalIp();
		});
}

void FTorqueRouter::AskExternalIp()
{
	TWeakPtr<FTorqueRouter> Weak = AsShared();
	const int32 Gen = Generation;
	Soap(TEXT("GetExternalIPAddress"), FromStd(TorqueUpnp::GetExternalIp(ToStd(ServiceType))), [Weak, Gen](bool bOk, const FString& Answer) {
		TSharedPtr<FTorqueRouter> Self = Weak.Pin();
		if (!Self || Self->Generation != Gen) return;
		const std::string Ip = bOk ? TorqueUpnp::TagText(ToStd(Answer), "NewExternalIPAddress") : std::string();
		if (!Ip.empty() && !TorqueUpnp::IsPrivate(Ip))
		{
			Self->PublicIp = FromStd(Ip);
			UE_LOG(LogTemp, Log, TEXT("Torque net: friends over the internet join %s"), *Self->PublicIp);
			return;
		}
		// Behind a second router, or the provider's shared address (carrier-grade NAT): ask outside.
		Self->LookUpPublicIp();
	});
}

void FTorqueRouter::Fail(const FString& Reason)
{
	State = EState::Closed;
	Why = Reason;
	UE_LOG(LogTemp, Log, TEXT("Torque net: the router didn't open the port: %s. Forward UDP port %d to %s by hand for friends over the internet"),
		*Reason, WantPort, LocalIp.IsEmpty() ? TEXT("this PC") : *LocalIp);
	LookUpPublicIp();
}

void FTorqueRouter::LookUpPublicIp()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueNoUpnp"))) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TEXT("https://api.ipify.org"));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(6.0f);
	TWeakPtr<FTorqueRouter> Weak = AsShared();
	const int32 Gen = Generation;
	Req->OnProcessRequestComplete().BindLambda([Weak, Gen](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk) {
		TSharedPtr<FTorqueRouter> Self = Weak.Pin();
		if (!Self || Self->Generation != Gen || !bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		const FString Ip = Resp->GetContentAsString().TrimStartAndEnd();
		if (Ip.Len() <= 45 && !TorqueUpnp::IsPrivate(ToStd(Ip))) Self->PublicIp = Ip;
	});
	Req->ProcessRequest();
}

void FTorqueRouter::Close()
{
	++Generation;
	StopSearch();
	if (MappedPort > 0 && !ControlUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Torque net: closing UDP port %d on the router"), MappedPort);
		Soap(TEXT("DeletePortMapping"), FromStd(TorqueUpnp::DeletePortMapping(ToStd(ServiceType), MappedPort)), [](bool, const FString&) {});
	}
	MappedPort = 0;
	State = EState::Off;
	PublicIp.Reset();
	Why.Reset();
}
