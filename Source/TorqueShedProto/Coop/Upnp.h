// Asking the home router to open the game's port (UPnP IGD), as Torque Shed does: the host's
// friends then join over the internet without anyone forwarding a port by hand.
//
// Plain C++ (tested in SimTest/upnp_test.cpp): the messages and the parsing. The sockets and HTTP
// are in TorqueRouter.cpp.
//
// 1. Search: a UDP multicast to 239.255.255.250:1900; routers answer with a LOCATION header.
// 2. Fetch that XML description, find the WANIPConnection (or WANPPPConnection) service's control URL.
// 3. SOAP calls to it: AddPortMapping, GetExternalIPAddress, DeletePortMapping when hosting ends.
#pragma once

#include <cctype>
#include <cstdio>
#include <string>

namespace TorqueUpnp
{
inline const char* SearchAddress() { return "239.255.255.250"; }
inline int SearchPort() { return 1900; }

inline std::string Lower(std::string S)
{
	for (char& C : S) C = (char)std::tolower((unsigned char)C);
	return S;
}

inline std::string Trim(const std::string& S)
{
	size_t A = 0, B = S.size();
	while (A < B && std::isspace((unsigned char)S[A])) ++A;
	while (B > A && std::isspace((unsigned char)S[B - 1])) --B;
	return S.substr(A, B - A);
}

inline std::string SearchMessage()
{
	return "M-SEARCH * HTTP/1.1\r\n"
		   "HOST: 239.255.255.250:1900\r\n"
		   "MAN: \"ssdp:discover\"\r\n"
		   "MX: 2\r\n"
		   "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
		   "\r\n";
}

// The LOCATION header of a search answer ("" if none).
inline std::string ParseLocation(const std::string& Reply)
{
	const std::string L = Lower(Reply);
	size_t At = 0;
	while (At < L.size())
	{
		size_t End = L.find('\n', At);
		if (End == std::string::npos) End = L.size();
		if (L.compare(At, 9, "location:") == 0) return Trim(Reply.substr(At + 9, End - At - 9));
		At = End + 1;
	}
	return std::string();
}

// "http://192.168.1.1:5000/desc.xml" -> "http://192.168.1.1:5000".
inline std::string BaseOf(const std::string& Url)
{
	const size_t Scheme = Url.find("://");
	const size_t Path = Url.find('/', Scheme == std::string::npos ? 0 : Scheme + 3);
	return Path == std::string::npos ? Url : Url.substr(0, Path);
}

// The text inside the first <Tag>...</Tag> at or after From (namespace prefixes on the tag allowed).
inline std::string TagText(const std::string& Xml, const std::string& Tag, size_t From = 0, size_t* After = nullptr)
{
	size_t At = From;
	while ((At = Xml.find('<', At)) != std::string::npos)
	{
		size_t Close = Xml.find('>', At);
		if (Close == std::string::npos) break;
		std::string Name = Xml.substr(At + 1, Close - At - 1);
		const size_t Space = Name.find_first_of(" \t\r\n/");
		if (Space != std::string::npos) Name = Name.substr(0, Space);
		const size_t Colon = Name.find(':');
		const std::string Local = Colon == std::string::npos ? Name : Name.substr(Colon + 1);
		if (Local == Tag)
		{
			const std::string EndTag = "</" + Name + ">";
			const size_t End = Xml.find(EndTag, Close + 1);
			if (End == std::string::npos) return std::string();
			if (After) *After = End + EndTag.size();
			return Trim(Xml.substr(Close + 1, End - Close - 1));
		}
		At = Close + 1;
	}
	return std::string();
}

struct FService
{
	std::string Type;        // urn:schemas-upnp-org:service:WANIPConnection:1
	std::string ControlUrl;  // absolute
	bool Valid() const { return !Type.empty() && !ControlUrl.empty(); }
};

// The connection service to talk to, from the router's description. WANIPConnection:2 or :1
// first, else WANPPPConnection:1 (DSL routers).
inline FService ParseDescription(const std::string& Xml, const std::string& DescUrl)
{
	std::string Base = TagText(Xml, "URLBase");
	if (Base.empty()) Base = BaseOf(DescUrl);
	if (!Base.empty() && Base.back() == '/') Base.pop_back();
	FService Best;
	int BestRank = 0;
	size_t At = 0, After = 0;
	for (;;)
	{
		const size_t Start = Xml.find("<service>", At);
		if (Start == std::string::npos) break;
		const size_t End = Xml.find("</service>", Start);
		if (End == std::string::npos) break;
		const std::string Block = Xml.substr(Start, End - Start);
		const std::string Type = TagText(Block, "serviceType", 0, &After);
		std::string Control = TagText(Block, "controlURL");
		int Rank = 0;
		if (Type.find(":WANIPConnection:2") != std::string::npos) Rank = 3;
		else if (Type.find(":WANIPConnection:1") != std::string::npos) Rank = 2;
		else if (Type.find(":WANPPPConnection:1") != std::string::npos) Rank = 1;
		if (Rank > BestRank && !Control.empty())
		{
			if (Control.compare(0, 4, "http") != 0) Control = Base + (Control[0] == '/' ? "" : "/") + Control;
			Best.Type = Type;
			Best.ControlUrl = Control;
			BestRank = Rank;
		}
		At = End + 10;
	}
	return Best;
}

inline std::string Envelope(const std::string& ServiceType, const std::string& Action, const std::string& Args)
{
	return "<?xml version=\"1.0\"?>\r\n"
		   "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		   "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
		   "<u:" + Action + " xmlns:u=\"" + ServiceType + "\">" + Args + "</u:" + Action + ">"
		   "</s:Body></s:Envelope>\r\n";
}

// The SOAPAction header's value.
inline std::string SoapAction(const std::string& ServiceType, const std::string& Action)
{
	return "\"" + ServiceType + "#" + Action + "\"";
}

inline std::string AddPortMapping(const std::string& ServiceType, int Port, const std::string& LocalIp, const std::string& What)
{
	const std::string P = std::to_string(Port);
	return Envelope(ServiceType, "AddPortMapping",
		"<NewRemoteHost></NewRemoteHost><NewExternalPort>" + P + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>" + P + "</NewInternalPort><NewInternalClient>" + LocalIp + "</NewInternalClient>"
		"<NewEnabled>1</NewEnabled><NewPortMappingDescription>" + What + "</NewPortMappingDescription>"
		"<NewLeaseDuration>0</NewLeaseDuration>");
}

inline std::string DeletePortMapping(const std::string& ServiceType, int Port)
{
	return Envelope(ServiceType, "DeletePortMapping",
		"<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(Port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>");
}

inline std::string GetExternalIp(const std::string& ServiceType)
{
	return Envelope(ServiceType, "GetExternalIPAddress", "");
}

// A SOAP fault in an answer (the router said no), with its error code if there is one.
inline bool IsFault(const std::string& Reply, std::string* Code = nullptr)
{
	if (Reply.find("Fault>") == std::string::npos) return false;
	if (Code) *Code = TagText(Reply, "errorCode");
	return true;
}

// Addresses that aren't reachable from the internet: private, loopback, link-local, and the ones
// an internet provider shares between many homes (carrier-grade NAT, 100.64/10).
inline bool IsPrivate(const std::string& Ip)
{
	int A = -1, B = -1, C = -1, D = -1;
	if (std::sscanf(Ip.c_str(), "%d.%d.%d.%d", &A, &B, &C, &D) != 4) return true;
	if (A == 10 || A == 127 || A == 0) return true;
	if (A == 192 && B == 168) return true;
	if (A == 169 && B == 254) return true;
	if (A == 172 && B >= 16 && B <= 31) return true;
	if (A == 100 && B >= 64 && B <= 127) return true;
	return false;
}
} // namespace TorqueUpnp
