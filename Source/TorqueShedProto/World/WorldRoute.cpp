#include "WorldRoute.h"

#include "WorldRegion.h"
#include "../Fit/FitJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

namespace TorqueWorld
{
namespace Route
{
namespace
{
const double Pi = 3.14159265358979323846;

// RoutePlanner.CLASS_COST, and Ride UK's curvy style: the scenic costs, harder on big roads and towns.
struct StyleCosts
{
	const char* Mode;
	double Motorway, Trunk, Primary, Secondary, Tertiary, Unclassified, Residential, LivingStreet, Track;
};
const StyleCosts Table[StyleCount] = {
	{"main", 0.7, 0.75, 0.8, 0.9, 1.0, 1.15, 1.3, 1.4, 3.0},
	{"scenic", 3.0, 2.0, 1.3, 0.95, 0.9, 0.9, 1.2, 1.5, 3.0},
	{"tracks", 5.0, 3.0, 2.0, 1.4, 1.1, 0.9, 1.2, 1.5, 0.7},
	{"curvy", 6.0, 3.0, 1.25, 0.95, 0.9, 0.95, 1.7, 2.0, 3.0},
};

const StyleCosts* StyleOf(const std::string& Mode)
{
	for (const StyleCosts& S : Table)
		if (Mode == S.Mode) return &S;
	return nullptr;
}

std::string Base64Url(const std::string& In)
{
	static const char* Abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string Out;
	size_t I = 0;
	for (; I + 2 < In.size(); I += 3)
	{
		const unsigned V = (unsigned(static_cast<unsigned char>(In[I])) << 16) | (unsigned(static_cast<unsigned char>(In[I + 1])) << 8) | unsigned(static_cast<unsigned char>(In[I + 2]));
		Out += Abc[(V >> 18) & 63]; Out += Abc[(V >> 12) & 63]; Out += Abc[(V >> 6) & 63]; Out += Abc[V & 63];
	}
	if (I + 1 == In.size())
	{
		const unsigned V = unsigned(static_cast<unsigned char>(In[I])) << 16;
		Out += Abc[(V >> 18) & 63]; Out += Abc[(V >> 12) & 63];
	}
	else if (I + 2 == In.size())
	{
		const unsigned V = (unsigned(static_cast<unsigned char>(In[I])) << 16) | (unsigned(static_cast<unsigned char>(In[I + 1])) << 8);
		Out += Abc[(V >> 18) & 63]; Out += Abc[(V >> 12) & 63]; Out += Abc[(V >> 6) & 63];
	}
	return Out;
}

bool FromBase64Url(const std::string& In, std::string& Out)
{
	Out.clear();
	unsigned Acc = 0;
	int Bits = 0;
	for (char C : In)
	{
		int V = -1;
		if (C >= 'A' && C <= 'Z') V = C - 'A';
		else if (C >= 'a' && C <= 'z') V = C - 'a' + 26;
		else if (C >= '0' && C <= '9') V = C - '0' + 52;
		else if (C == '-' || C == '+') V = 62;
		else if (C == '_' || C == '/') V = 63;
		else if (C == '=') break;
		else return false;
		Acc = (Acc << 6) | unsigned(V);
		Bits += 6;
		if (Bits >= 8)
		{
			Bits -= 8;
			Out += static_cast<char>((Acc >> Bits) & 0xFF);
		}
	}
	return true;
}

// A JSON string as Godot's JSON.stringify writes one.
std::string Quoted(const std::string& S)
{
	std::string Out = "\"";
	for (char C : S)
	{
		switch (C)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(C) < 0x20)
			{
				char Buf[8];
				std::snprintf(Buf, sizeof(Buf), "\\u%04x", unsigned(static_cast<unsigned char>(C)));
				Out += Buf;
			}
			else Out += C;
		}
	}
	return Out + "\"";
}

// The first MaxName characters of a name (UTF-8: never half a character).
std::string Clip(const std::string& S, size_t Chars)
{
	size_t Count = 0, I = 0;
	for (; I < S.size(); ++I)
	{
		if ((static_cast<unsigned char>(S[I]) & 0xC0) != 0x80)
		{
			if (Count == Chars) break;
			++Count;
		}
	}
	return S.substr(0, I);
}

bool GoodPlace(double Lat, double Lon) { return std::isfinite(Lat) && std::isfinite(Lon) && std::fabs(Lat) <= 85.0 && std::fabs(Lon) <= 180.0 && !(Lat == 0.0 && Lon == 0.0); }

// Percent-decoding (and '+' as a space), for links.
std::string Decode(const std::string& S)
{
	std::string Out;
	for (size_t I = 0; I < S.size(); ++I)
	{
		if (S[I] == '%' && I + 2 < S.size() && std::isxdigit(static_cast<unsigned char>(S[I + 1])) && std::isxdigit(static_cast<unsigned char>(S[I + 2])))
		{
			Out += static_cast<char>(std::strtol(S.substr(I + 1, 2).c_str(), nullptr, 16));
			I += 2;
		}
		else Out += S[I] == '+' ? ' ' : S[I];
	}
	return Out;
}

// A number at Text[At...] ("-12.345"), its end in End; false when there is none.
bool NumberAt(const std::string& Text, size_t At, double& Value, size_t& End, bool NeedDot)
{
	size_t I = At;
	if (I < Text.size() && Text[I] == '-') ++I;
	const size_t Digits = I;
	while (I < Text.size() && std::isdigit(static_cast<unsigned char>(Text[I]))) ++I;
	if (I == Digits) return false;
	bool Dot = false;
	if (I < Text.size() && Text[I] == '.')
	{
		const size_t After = ++I;
		while (I < Text.size() && std::isdigit(static_cast<unsigned char>(Text[I]))) ++I;
		Dot = I > After;
		if (!Dot) I = After - 1;
	}
	if (NeedDot && !Dot) return false;
	Value = std::strtod(Text.substr(At, I - At).c_str(), nullptr);
	End = I;
	return true;
}

// "lat<Sep>lon" at Text[At...] (Sep a comma, maybe with spaces, or a slash), the end in End.
bool PairAt(const std::string& Text, size_t At, char Sep, double& Lat, double& Lon, size_t& End, bool NeedDot)
{
	size_t I = 0;
	if (!NumberAt(Text, At, Lat, I, NeedDot)) return false;
	while (I < Text.size() && Text[I] == ' ') ++I;
	if (I >= Text.size() || Text[I] != Sep) return false;
	++I;
	while (I < Text.size() && Text[I] == ' ') ++I;
	if (!NumberAt(Text, I, Lon, End, NeedDot)) return false;
	return GoodPlace(Lat, Lon);
}

// The value of an XML attribute in a tag's text, or "".
std::string Attribute(const std::string& Tag, const char* Name)
{
	const std::string Key = std::string(" ") + Name + "=";
	size_t At = Tag.find(Key);
	if (At == std::string::npos) return "";
	At += Key.size();
	if (At >= Tag.size()) return "";
	const char Q = Tag[At];
	if (Q != '"' && Q != '\'') return "";
	const size_t End = Tag.find(Q, At + 1);
	return End == std::string::npos ? "" : Tag.substr(At + 1, End - At - 1);
}

// Every <Name ...> tag's lat and lon in a GPX, with the name inside it if it has one.
std::vector<RoutePin> GpxPoints(const std::string& Text, const char* Name)
{
	std::vector<RoutePin> Out;
	const std::string Open = std::string("<") + Name;
	for (size_t At = Text.find(Open); At != std::string::npos; At = Text.find(Open, At + 1))
	{
		const char After = At + Open.size() < Text.size() ? Text[At + Open.size()] : '>';
		if (After != ' ' && After != '\t' && After != '\n' && After != '\r') continue;
		const size_t End = Text.find('>', At);
		if (End == std::string::npos) break;
		const std::string Tag = Text.substr(At, End - At);
		RoutePin P;
		P.Lat = std::strtod(Attribute(Tag, "lat").c_str(), nullptr);
		P.Lon = std::strtod(Attribute(Tag, "lon").c_str(), nullptr);
		if (!GoodPlace(P.Lat, P.Lon)) continue;
		// (its <name> when it has one before it closes)
		const std::string Close = std::string("</") + Name;
		const size_t Shut = Text.find(Close, End);
		const size_t NameAt = Text.find("<name>", End);
		if (Shut != std::string::npos && NameAt != std::string::npos && NameAt < Shut)
		{
			const size_t NameEnd = Text.find("</name>", NameAt);
			if (NameEnd != std::string::npos) P.Name = Clip(Text.substr(NameAt + 6, NameEnd - NameAt - 6), MaxName);
		}
		Out.push_back(P);
	}
	return Out;
}
} // namespace

const char* const Styles[StyleCount] = {"main", "scenic", "tracks", "curvy"};

bool IsStyle(const std::string& Mode) { return StyleOf(Mode) != nullptr; }

double ClassCost(const std::string& Mode, const std::string& Class)
{
	const StyleCosts* S = StyleOf(Mode);
	if (!S) S = &Table[0];
	if (Class == "motorway") return S->Motorway;
	if (Class == "trunk") return S->Trunk;
	if (Class == "primary") return S->Primary;
	if (Class == "secondary") return S->Secondary;
	if (Class == "tertiary") return S->Tertiary;
	if (Class == "unclassified") return S->Unclassified;
	if (Class == "residential") return S->Residential;
	if (Class == "living_street") return S->LivingStreet;
	if (Class == "track") return S->Track;
	// [OSM stage 4] (a service road, alley, drive or car park aisle: ridden only to start or finish a ride,
	// never a short cut through a car park)
	if (Class == "service") return 8.0;
	return 1.5;
}

double Twistiness(const std::vector<P2>& Line)
{
	// (resampled every 10 m, so a mapper's many little points on a straight road add nothing)
	std::vector<P2> Even;
	if (Line.size() < 2) return 0.0;
	Even.push_back(Line.front());
	double Carry = 0.0, Total = 0.0;
	for (size_t I = 1; I < Line.size(); ++I)
	{
		const P2 A = Line[I - 1], B = Line[I];
		const double Len = A.DistanceTo(B);
		Total += Len;
		double T = 10.0 - Carry;
		while (T <= Len)
		{
			Even.push_back(A + (B - A) * (T / Len));
			T += 10.0;
		}
		Carry = Len - (T - 10.0);
	}
	if (Even.size() < 3 || Total < 1.0) return 0.0;
	double Turned = 0.0;
	for (size_t I = 1; I + 1 < Even.size(); ++I)
	{
		const P2 U = Even[I] - Even[I - 1], V = Even[I + 1] - Even[I];
		double D = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
		while (D > Pi) D -= 2.0 * Pi;
		while (D < -Pi) D += 2.0 * Pi;
		const double Deg = std::fabs(D) * 180.0 / Pi;
		if (Deg >= 3.0) Turned += Deg;
	}
	return Turned / (Total / 1000.0);
}

std::vector<double> EdgeCosts(const Region& G, const std::string& Mode)
{
	std::vector<double> ByClass;
	for (const std::string& Class : G.Classes) ByClass.push_back(ClassCost(Mode, Class));
	std::vector<double> Out(G.Edges.size(), 1.0);
	for (size_t E = 0; E < G.Edges.size(); ++E)
		Out[E] = size_t(G.Edges[E].Class) < ByClass.size() ? ByClass[size_t(G.Edges[E].Class)] : 1.5;
	if (Mode != "curvy") return Out;
	// Curvy: the buildings by 100 m square, to know a built-up stretch.
	std::unordered_map<long long, int> Built;
	const auto Key = [](double X, double Z) { return static_cast<long long>(std::floor(X / 100.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 100.0)); };
	for (const Building& B : G.Buildings) ++Built[Key(B.X, B.Z)];
	for (size_t E = 0; E < G.Edges.size(); ++E)
	{
		const RoadEdge& Edge = G.Edges[E];
		if (Edge.Length < 1.0) continue;
		// (a twisty road is the point: up to almost half off for 300 degrees a km; a straight one costs more)
		const double Twist = Twistiness(Edge.Line);
		double Factor = std::fmin(1.25, std::fmax(0.55, 1.2 - Twist / 450.0));
		// (through a village or a town: more, by how built up it is)
		int Squares = 0, Busy = 0;
		for (size_t I = 0; I < Edge.Line.size(); ++I)
		{
			if (I > 0 && Edge.Line[I].DistanceTo(Edge.Line[I - 1]) < 1.0) continue;
			++Squares;
			const auto It = Built.find(Key(Edge.Line[I].X, Edge.Line[I].Z));
			if (It != Built.end() && It->second >= 6) ++Busy;
		}
		if (Squares > 0) Factor *= 1.0 + 1.2 * double(Busy) / double(Squares);
		Out[E] *= Factor;
	}
	return Out;
}

std::string CodeOf(const RouteRequest& R)
{
	if (R.Pins.size() < 2) return "";
	// (keys in order, no spaces: as Godot's JSON.stringify writes {"p", "m", "r"})
	std::string Text = "{\"m\":" + Quoted(IsStyle(R.Mode) ? R.Mode : "main") + ",\"p\":[";
	for (size_t I = 0; I < R.Pins.size(); ++I)
	{
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "[%lld,%lld,", static_cast<long long>(std::llround(R.Pins[I].Lat * 1e5)), static_cast<long long>(std::llround(R.Pins[I].Lon * 1e5)));
		Text += (I > 0 ? "," : "") + std::string(Buf) + Quoted(Clip(R.Pins[I].Name, MaxName)) + "]";
	}
	Text += "],\"r\":\"\"}";
	return std::string(CodePrefix) + Base64Url(Text);
}

bool FromCode(const std::string& Text, RouteRequest& Out)
{
	const size_t At = Text.find(CodePrefix);
	if (At == std::string::npos) return false;
	size_t End = At + 4;
	while (End < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[End])) || Text[End] == '-' || Text[End] == '_')) ++End;
	std::string Raw;
	if (End == At + 4 || !FromBase64Url(Text.substr(At + 4, End - At - 4), Raw)) return false;
	TorqueFit::Json J;
	if (!TorqueFit::Json::Parse(Raw, J) || !J.IsObject()) return false;
	const TorqueFit::Json* P = J.Get("p");
	if (!P || !P->IsArray() || P->A.size() < 2 || P->A.size() > size_t(MaxPinsAnyway)) return false;
	RouteRequest R;
	for (const TorqueFit::Json& Row : P->A)
	{
		if (!Row.IsArray() || Row.A.size() < 3 || !Row.A[0].IsNumber() || !Row.A[1].IsNumber()) return false;
		RoutePin Pin;
		Pin.Lat = Row.A[0].N / 1e5;
		Pin.Lon = Row.A[1].N / 1e5;
		Pin.Name = Clip(Row.A[2].IsString() ? Row.A[2].S : "", MaxName);
		if (!GoodPlace(Pin.Lat, Pin.Lon) || std::fabs(Pin.Lat) > 80.0) return false;
		R.Pins.push_back(Pin);
	}
	const std::string Mode = J.Str("m", "main");
	R.Mode = IsStyle(Mode) ? Mode : "main";
	Out = R;
	return true;
}

std::vector<RoutePin> PlacesInLink(const std::string& Text)
{
	const std::string T = Decode(Text);
	std::vector<RoutePin> Out;
	const auto Add = [&Out](double Lat, double Lon)
	{
		RoutePin P;
		P.Lat = Lat;
		P.Lon = Lon;
		if (Out.empty() || GroundMetres(Out.back().Lat, Out.back().Lon, Lat, Lon) > 1.0) Out.push_back(P);
	};
	double Lat = 0.0, Lon = 0.0;
	size_t End = 0;
	// A Google directions link: /dir/lat,lon/lat,lon/... (its stops, in order).
	const size_t Dir = T.find("/dir/");
	if (Dir != std::string::npos)
	{
		size_t At = Dir + 5;
		while (At < T.size() && T[At] != '@' && T[At] != '?' && T[At] != '#')
		{
			const size_t Slash = T.find('/', At);
			const std::string Part = T.substr(At, (Slash == std::string::npos ? T.size() : Slash) - At);
			if (!Part.empty() && Part[0] != '@' && PairAt(Part, 0, ',', Lat, Lon, End, true)) Add(Lat, Lon);
			if (Slash == std::string::npos) break;
			At = Slash + 1;
		}
		if (Out.size() >= 2) return Out;
		Out.clear();
	}
	// An OpenStreetMap directions link: route=lat,lon;lat,lon
	const size_t RouteAt = T.find("route=");
	if (RouteAt != std::string::npos)
	{
		size_t At = RouteAt + 6;
		while (PairAt(T, At, ',', Lat, Lon, End, true))
		{
			Add(Lat, Lon);
			if (End >= T.size() || T[End] != ';') break;
			At = End + 1;
		}
		if (Out.size() >= 2) return Out;
		Out.clear();
	}
	// saddr= and daddr= (Google's and Apple's older links)
	{
		const size_t S = T.find("saddr="), D = T.find("daddr=");
		if (S != std::string::npos && D != std::string::npos)
		{
			double Lat2 = 0.0, Lon2 = 0.0;
			if (PairAt(T, S + 6, ',', Lat, Lon, End, true) && PairAt(T, D + 6, ',', Lat2, Lon2, End, true))
			{
				Add(Lat, Lon);
				Add(Lat2, Lon2);
				return Out;
			}
		}
	}
	// One place, as MapLink.parse finds it, in its order.
	const auto Found = [&]() -> bool
	{
		for (size_t At = T.find("!3d"); At != std::string::npos; At = T.find("!3d", At + 1))
		{
			size_t E1 = 0;
			if (NumberAt(T, At + 3, Lat, E1, false) && T.compare(E1, 3, "!4d") == 0 && NumberAt(T, E1 + 3, Lon, End, false) && GoodPlace(Lat, Lon)) return true;
		}
		for (size_t At = T.find('@'); At != std::string::npos; At = T.find('@', At + 1))
			if (PairAt(T, At + 1, ',', Lat, Lon, End, false)) return true;
		for (const char* Key : {"q=", "ll=", "sll=", "query=", "center=", "daddr=", "destination="})
			for (size_t At = T.find(Key); At != std::string::npos; At = T.find(Key, At + 1))
			{
				if (At == 0 || (T[At - 1] != '?' && T[At - 1] != '&')) continue;
				if (PairAt(T, At + std::strlen(Key), ',', Lat, Lon, End, false)) return true;
			}
		for (size_t At = T.find("#map="); At != std::string::npos; At = T.find("#map=", At + 1))
		{
			const size_t Slash = T.find('/', At);
			if (Slash != std::string::npos && PairAt(T, Slash + 1, '/', Lat, Lon, End, false)) return true;
		}
		const size_t M = T.find("mlat=");
		if (M != std::string::npos)
		{
			size_t E1 = 0;
			const size_t Ml = T.find("mlon=", M);
			if (Ml != std::string::npos && NumberAt(T, M + 5, Lat, E1, false) && NumberAt(T, Ml + 5, Lon, End, false) && GoodPlace(Lat, Lon)) return true;
		}
		// (plain "lat, lon" typed)
		for (size_t At = 0; At < T.size(); ++At)
		{
			if (At > 0 && (std::isdigit(static_cast<unsigned char>(T[At - 1])) || T[At - 1] == '.' || T[At - 1] == '-')) continue;
			if (PairAt(T, At, ',', Lat, Lon, End, true) && std::fabs(Lat) < 100.0) return true;
		}
		return false;
	};
	if (Found()) Add(Lat, Lon);
	return Out;
}

bool FromGpx(const std::string& Text, RouteRequest& Out, int MostPins)
{
	std::vector<RoutePin> Way = GpxPoints(Text, "rtept");
	if (Way.size() < 2) Way = GpxPoints(Text, "trkpt");
	if (Way.size() < 2) Way = GpxPoints(Text, "wpt");
	if (Way.size() < 2) return false;
	MostPins = std::max(2, std::min(MostPins, MaxPinsAnyway));
	RouteRequest R;
	R.Mode = "main";
	if (int(Way.size()) <= MostPins)
	{
		R.Pins = Way;
		Out = R;
		return true;
	}
	// The ends, then again and again the point furthest off the line between the pins either side of it
	// (Douglas-Peucker, a pin at a time), in a flat frame at the way's start.
	const double Lat0 = Way.front().Lat, Lon0 = Way.front().Lon;
	std::vector<P2> Flat;
	for (const RoutePin& P : Way) Flat.push_back(P2((P.Lon - Lon0) * 111320.0 * std::cos(Lat0 * Pi / 180.0), (P.Lat - Lat0) * 110574.0));
	std::vector<size_t> Kept = {0, Way.size() - 1};
	while (int(Kept.size()) < MostPins)
	{
		double Worst = -1.0;
		size_t Pick = 0, Slot = 0;
		for (size_t K = 0; K + 1 < Kept.size(); ++K)
		{
			const P2 A = Flat[Kept[K]], B = Flat[Kept[K + 1]];
			const P2 Ab = B - A;
			const double L2 = Ab.X * Ab.X + Ab.Z * Ab.Z;
			for (size_t I = Kept[K] + 1; I < Kept[K + 1]; ++I)
			{
				const P2 Ap = Flat[I] - A;
				const double T = L2 > 1e-9 ? std::fmin(1.0, std::fmax(0.0, (Ap.X * Ab.X + Ap.Z * Ab.Z) / L2)) : 0.0;
				const double D = Flat[I].DistanceTo(A + Ab * T);
				if (D > Worst) { Worst = D; Pick = I; Slot = K + 1; }
			}
		}
		if (Worst < 30.0) break;   // (the rest lie along it)
		Kept.insert(Kept.begin() + long(Slot), Pick);
	}
	for (size_t I : Kept) R.Pins.push_back(Way[I]);
	if (R.Pins.front().Name.empty()) R.Pins.front().Name = "Start";
	Out = R;
	return true;
}

std::string Gazetteer::Fold(const std::string& Text)
{
	// (lower case, the accents of the Latin letters UK names use taken off, other punctuation as spaces)
	static const std::map<std::string, char> Accents = {
		{"\xc3\xa0", 'a'}, {"\xc3\xa1", 'a'}, {"\xc3\xa2", 'a'}, {"\xc3\xa4", 'a'}, {"\xc3\xa8", 'e'}, {"\xc3\xa9", 'e'}, {"\xc3\xaa", 'e'},
		{"\xc3\xab", 'e'}, {"\xc3\xac", 'i'}, {"\xc3\xad", 'i'}, {"\xc3\xae", 'i'}, {"\xc3\xaf", 'i'}, {"\xc3\xb2", 'o'}, {"\xc3\xb3", 'o'},
		{"\xc3\xb4", 'o'}, {"\xc3\xb6", 'o'}, {"\xc3\xb9", 'u'}, {"\xc3\xba", 'u'}, {"\xc3\xbb", 'u'}, {"\xc3\xbc", 'u'}, {"\xc3\xbd", 'y'},
		{"\xc5\xb5", 'w'}, {"\xc5\xb7", 'y'}, {"\xc3\xbf", 'y'}, {"\xc3\xa7", 'c'}, {"\xc3\xb1", 'n'},
		{"\xc3\x80", 'a'}, {"\xc3\x81", 'a'}, {"\xc3\x82", 'a'}, {"\xc3\x89", 'e'}, {"\xc3\x8a", 'e'}, {"\xc3\x8e", 'i'}, {"\xc3\x94", 'o'},
		{"\xc3\x9b", 'u'}, {"\xc5\xb4", 'w'}, {"\xc5\xb6", 'y'}};
	std::string Out;
	for (size_t I = 0; I < Text.size(); ++I)
	{
		const unsigned char C = static_cast<unsigned char>(Text[I]);
		if (C >= 0x80 && I + 1 < Text.size())
		{
			const auto It = Accents.find(Text.substr(I, 2));
			if (It != Accents.end()) { Out += It->second; ++I; continue; }
			while (I + 1 < Text.size() && (static_cast<unsigned char>(Text[I + 1]) & 0xC0) == 0x80) ++I;
			continue;
		}
		if (C == '\'') continue;   // (St Mary's: st marys)
		if (std::isalnum(C)) Out += static_cast<char>(std::tolower(C));
		else if (!Out.empty() && Out.back() != ' ') Out += ' ';
	}
	while (!Out.empty() && Out.back() == ' ') Out.pop_back();
	return Out;
}

bool Gazetteer::Load(const std::string& Text)
{
	Rows.clear();
	Keys.clear();
	size_t At = 0;
	while (At < Text.size())
	{
		size_t End = Text.find('\n', At);
		if (End == std::string::npos) End = Text.size();
		std::string Line = Text.substr(At, End - At);
		At = End + 1;
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();
		if (Line.empty() || Line[0] == '#') continue;
		std::vector<std::string> F;
		size_t From = 0;
		for (size_t Tab = Line.find('\t'); ; Tab = Line.find('\t', From))
		{
			F.push_back(Line.substr(From, (Tab == std::string::npos ? Line.size() : Tab) - From));
			if (Tab == std::string::npos) break;
			From = Tab + 1;
		}
		if (F.size() < 4) continue;
		Place P;
		P.Name = F[0];
		P.Lat = std::strtod(F[1].c_str(), nullptr);
		P.Lon = std::strtod(F[2].c_str(), nullptr);
		P.Kind = F[3];
		P.Population = F.size() > 4 ? std::atoi(F[4].c_str()) : 0;
		if (P.Name.empty() || !GoodPlace(P.Lat, P.Lon)) continue;
		Rows.push_back(P);
		Keys.push_back(Fold(P.Name));
	}
	return !Rows.empty();
}

std::vector<Place> Gazetteer::Search(const std::string& Query, size_t Limit) const
{
	const std::string Q = Fold(Query);
	std::vector<Place> First, Then;
	if (Q.empty() || Limit == 0) return First;
	for (size_t I = 0; I < Rows.size() && First.size() < Limit; ++I)
	{
		const std::string& K = Keys[I];
		if (K.compare(0, Q.size(), Q) == 0) First.push_back(Rows[I]);
		else if (Then.size() < Limit && K.find(" " + Q) != std::string::npos) Then.push_back(Rows[I]);
	}
	for (const Place& P : Then)
	{
		if (First.size() >= Limit) break;
		First.push_back(P);
	}
	return First;
}

const Place* Gazetteer::Near(double Lat, double Lon, double Reach) const
{
	const Place* Nearest = nullptr;
	double Best = 1e18;
	for (const Place& P : Rows)
	{
		if (P.Kind == "hill" || P.Kind == "station") continue;
		const double D = GroundMetres(Lat, Lon, P.Lat, P.Lon);
		if (D < Reach && (P.Kind == "city" || P.Kind == "town" || P.Kind == "village")) return &P;   // (the biggest first)
		if (D < Best) { Best = D; Nearest = &P; }
	}
	return Nearest;
}

std::vector<const Place*> Gazetteer::InBox(double South, double West, double North, double East, const std::string& Kinds, size_t Limit) const
{
	std::vector<const Place*> Out;
	const std::string Spaced = " " + Kinds + " ";
	for (const Place& P : Rows)
	{
		if (Out.size() >= Limit) break;
		if (P.Lat < South || P.Lat > North || P.Lon < West || P.Lon > East) continue;
		if (Spaced.find(" " + P.Kind + " ") == std::string::npos) continue;
		Out.push_back(&P);
	}
	return Out;
}

std::vector<RouteRequest> Loops(double Lat, double Lon, double Km, const std::string& Mode, int Count)
{
	std::vector<RouteRequest> Out;
	if (!GoodPlace(Lat, Lon) || Km < 5.0 || Count < 1) return Out;
	// Out to two turning points and back, a triangle with the start: its sides about a third of the
	// way each, less a quarter for the roads' winding.
	const double Side = Km * 1000.0 / 3.0 / 1.3;
	const double MetresLat = 110574.0, MetresLon = 111320.0 * std::cos(Lat * Pi / 180.0);
	for (int K = 0; K < Count; ++K)
	{
		// (headings spread round the compass, the first to the north east)
		const double Heading = (45.0 + 360.0 * K / Count) * Pi / 180.0;
		RouteRequest R;
		R.Mode = IsStyle(Mode) ? Mode : "curvy";
		RoutePin Start;
		Start.Lat = Lat;
		Start.Lon = Lon;
		Start.Name = "Start";
		R.Pins.push_back(Start);
		for (int Corner = 0; Corner < 2; ++Corner)
		{
			const double H = Heading + (Corner == 0 ? -Pi / 6.0 : Pi / 6.0);
			RoutePin P;
			P.Lat = Lat + std::cos(H) * Side / MetresLat;
			P.Lon = Lon + std::sin(H) * Side / MetresLon;
			R.Pins.push_back(P);
		}
		Start.Name = "Back";
		R.Pins.push_back(Start);
		Out.push_back(R);
	}
	return Out;
}

bool ReadFamousRoads(const std::string& Text, std::vector<FamousRoad>& Out)
{
	Out.clear();
	TorqueFit::Json J;
	if (!TorqueFit::Json::Parse(Text, J) || !J.IsArray()) return false;
	for (const TorqueFit::Json& Row : J.A)
	{
		FamousRoad F;
		F.Name = Row.Str("name");
		F.About = Row.Str("about");
		const std::string Mode = Row.Str("mode", "curvy");
		F.Journey.Mode = IsStyle(Mode) ? Mode : "curvy";
		const TorqueFit::Json* Pins = Row.Get("pins");
		if (F.Name.empty() || !Pins || !Pins->IsArray()) continue;
		for (const TorqueFit::Json& P : Pins->A)
		{
			if (!P.IsArray() || P.A.size() < 2) continue;
			RoutePin Pin;
			Pin.Lat = P.A[0].N;
			Pin.Lon = P.A[1].N;
			Pin.Name = P.A.size() > 2 && P.A[2].IsString() ? Clip(P.A[2].S, MaxName) : "";
			if (GoodPlace(Pin.Lat, Pin.Lon)) F.Journey.Pins.push_back(Pin);
		}
		if (F.Journey.Pins.size() >= 2) Out.push_back(F);
	}
	return !Out.empty();
}

double GroundMetres(double Lat0, double Lon0, double Lat1, double Lon1)
{
	const double K = 40075016.686 / 360.0;
	const double Dx = (Lon1 - Lon0) * K * std::cos((Lat0 + Lat1) * 0.5 * Pi / 180.0), Dy = (Lat1 - Lat0) * K;
	return std::sqrt(Dx * Dx + Dy * Dy);
}
} // namespace Route
} // namespace TorqueWorld
