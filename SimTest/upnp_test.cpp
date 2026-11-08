// Tests Coop/Upnp.h against answers shaped like real routers' (a BT Smart Hub style IGD v1, a
// FritzBox-style DSL router with only WANPPPConnection, and an IGD v2 with a URLBase).
#include "Upnp.h"

#include <cstdio>

static int Failures = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Failures;
}

int main()
{
	using namespace TorqueUpnp;
	std::printf("Search answer\n");
	const std::string Reply = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
							  "USN: uuid:abc::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nEXT:\r\nSERVER: Linux UPnP/1.0\r\n"
							  "Location: http://192.168.1.254:49152/IGDdevicedesc.xml\r\n\r\n";
	Check(ParseLocation(Reply) == "http://192.168.1.254:49152/IGDdevicedesc.xml", "LOCATION found, any case");
	Check(ParseLocation("HTTP/1.1 200 OK\r\n\r\n").empty(), "no LOCATION: empty");
	Check(BaseOf("http://192.168.1.254:49152/IGDdevicedesc.xml") == "http://192.168.1.254:49152", "base URL");

	std::printf("IGD v1 description\n");
	const std::string V1 = R"(<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device>
<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><deviceList><device>
<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><serviceList><service>
<serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><controlURL>/upnp/control/WANCommonIFC1</controlURL></service>
</serviceList><deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList>
<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
<controlURL>/upnp/control/WANIPConn1</controlURL><eventSubURL>/upnp/event/WANIPConn1</eventSubURL></service>
</serviceList></device></deviceList></device></deviceList></device></root>)";
	FService S = ParseDescription(V1, "http://192.168.1.254:49152/IGDdevicedesc.xml");
	Check(S.Type == "urn:schemas-upnp-org:service:WANIPConnection:1", "WANIPConnection:1 picked");
	Check(S.ControlUrl == "http://192.168.1.254:49152/upnp/control/WANIPConn1", "control URL made absolute");

	std::printf("DSL router: WANPPPConnection only, relative URL without a slash\n");
	const std::string Ppp = R"(<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>
<controlURL>igdupnp/control/WANPPPConn1</controlURL></service></serviceList></device></root>)";
	S = ParseDescription(Ppp, "http://192.168.178.1:49000/igddesc.xml");
	Check(S.Type == "urn:schemas-upnp-org:service:WANPPPConnection:1", "WANPPPConnection:1 picked");
	Check(S.ControlUrl == "http://192.168.178.1:49000/igdupnp/control/WANPPPConn1", "control URL joined with a slash");

	std::printf("IGD v2 with URLBase, v1 and v2 both offered\n");
	const std::string V2 = R"(<root><URLBase>http://10.0.0.1:1780/</URLBase><device><serviceList>
<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/ctl/IPConn1</controlURL></service>
<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType><controlURL>/ctl/IPConn</controlURL></service>
</serviceList></device></root>)";
	S = ParseDescription(V2, "http://10.0.0.1:5000/rootDesc.xml");
	Check(S.Type == "urn:schemas-upnp-org:service:WANIPConnection:2", "v2 preferred");
	Check(S.ControlUrl == "http://10.0.0.1:1780/ctl/IPConn", "URLBase used");
	Check(!ParseDescription("<root></root>", "http://x/").Valid(), "no service: not valid");

	std::printf("SOAP\n");
	const std::string Add = AddPortMapping("urn:schemas-upnp-org:service:WANIPConnection:1", 7777, "192.168.1.20", "Torque Shed");
	Check(Add.find("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">") != std::string::npos, "AddPortMapping body");
	Check(Add.find("<NewExternalPort>7777</NewExternalPort><NewProtocol>UDP</NewProtocol>") != std::string::npos, "port and UDP");
	Check(Add.find("<NewInternalClient>192.168.1.20</NewInternalClient>") != std::string::npos, "this PC as the client");
	Check(SoapAction("urn:schemas-upnp-org:service:WANIPConnection:1", "AddPortMapping") ==
			  "\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"", "SOAPAction header");
	const std::string IpReply = R"(<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>
<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"><NewExternalIPAddress>81.2.3.4</NewExternalIPAddress>
</u:GetExternalIPAddressResponse></s:Body></s:Envelope>)";
	Check(TagText(IpReply, "NewExternalIPAddress") == "81.2.3.4", "external address read");
	Check(!IsFault(IpReply), "an answer isn't a fault");
	const std::string Fault = R"(<s:Envelope><s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>
<UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>718</errorCode><errorDescription>ConflictInMappingEntry</errorDescription></UPnPError>
</detail></s:Fault></s:Body></s:Envelope>)";
	std::string Code;
	Check(IsFault(Fault, &Code) && Code == "718", "fault and its code");

	Check(Add.find("<NewLeaseDuration>7200</NewLeaseDuration>") != std::string::npos, "a two hour lease by default");
	Check(AddPortMapping("x", 7777, "1.2.3.4", "T", 0).find("<NewLeaseDuration>0</NewLeaseDuration>") != std::string::npos,
		"lease 0 for routers that want it");
	const std::string Entry = R"(<s:Envelope><s:Body><u:GetSpecificPortMappingEntryResponse><NewInternalPort>7777</NewInternalPort>
<NewInternalClient>192.168.1.20</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>Torque Shed</NewPortMappingDescription>
<NewLeaseDuration>0</NewLeaseDuration></u:GetSpecificPortMappingEntryResponse></s:Body></s:Envelope>)";
	Check(IsOurMapping(Entry, { "192.168.1.20" }), "our mapping left behind is recognised");
	Check(!IsOurMapping(Entry, { "192.168.1.21" }), "another PC's isn't ours");
	Check(IsOurMapping(Entry, { "172.17.0.1", "10.8.0.2", "192.168.1.20" }), "ours whichever of this PC's addresses it points at");
	auto Named = [&](const std::string& Name) {
		std::string E = Entry;
		E.replace(E.find("Torque Shed</"), 11, Name);
		return E;
	};
	Check(IsOurMapping(Named("Torque She"), { "192.168.1.20" }), "the old name cut to ten characters by the router");
	Check(IsOurMapping(Named("TorqueShed"), { "192.168.1.20" }) && IsOurMapping(Named("TorqueS"), { "192.168.1.20" }), "the new name, whole or cut");
	Check(!IsOurMapping(Named("Torque"), { "192.168.1.21" }), "a cut name on another PC still isn't ours");
	Check(!IsOurMapping(Named("Steam"), { "192.168.1.20" }) && !IsOurMapping(Named(""), { "192.168.1.20" }) && !IsOurMapping(Named("Tor"), { "192.168.1.20" }),
		"other names (or almost none) aren't ours");
	Check(IsOurMapping(Named("TorqueShedServer"), { "192.168.1.20" }), "a longer name starting with ours counts (a router that adds to it)");
	Check(std::string(MappingName()).size() <= 10, "our name fits in ten characters");
	Check(AddressFacing("192.168.1.1", { "172.17.0.1", "10.8.0.2", "192.168.1.20", "192.168.56.1" }) == "192.168.1.20", "the address on the router's network");
	Check(AddressFacing("10.0.0.138", { "192.168.1.20", "10.0.0.5" }) == "10.0.0.5", "on a 10. network too");
	Check(AddressFacing("192.168.1.1", { "10.8.0.2" }).empty() && AddressFacing("junk", { "192.168.1.20" }).empty(), "none when nothing is near");
	Check(GetSpecificPortMappingEntry("x", 7777).find("<NewExternalPort>7777</NewExternalPort>") != std::string::npos, "entry query");

	std::printf("Addresses\n");
	Check(IsPrivate("192.168.1.20") && IsPrivate("10.1.2.3") && IsPrivate("172.20.0.1") && IsPrivate("127.0.0.1"), "private ones");
	Check(IsPrivate("100.70.1.2"), "carrier-grade NAT counts as private");
	Check(!IsPrivate("81.2.3.4") && !IsPrivate("172.32.0.1") && !IsPrivate("100.128.0.1"), "public ones");
	Check(IsPrivate("not an ip") && IsPrivate(""), "junk isn't public");

	std::printf(Failures ? "upnp_test: %d FAILED\n" : "upnp_test: all passed\n", Failures);
	return Failures ? 1 : 0;
}
