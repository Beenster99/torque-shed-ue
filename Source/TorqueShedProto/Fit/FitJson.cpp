#include "FitJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace TorqueFit
{
namespace
{
struct FReader
{
	const std::string& T;
	size_t I = 0;
	std::string Err;

	explicit FReader(const std::string& Text) : T(Text) {}

	void SkipSpace()
	{
		while (I < T.size() && (T[I] == ' ' || T[I] == '\t' || T[I] == '\n' || T[I] == '\r')) ++I;
	}
	bool Fail(const char* What)
	{
		if (Err.empty())
		{
			char Buf[96];
			std::snprintf(Buf, sizeof(Buf), "%s at byte %zu", What, I);
			Err = Buf;
		}
		return false;
	}
	bool Literal(const char* Word)
	{
		size_t K = 0;
		while (Word[K])
		{
			if (I + K >= T.size() || T[I + K] != Word[K]) return Fail("bad literal");
			++K;
		}
		I += K;
		return true;
	}
	static void PutUtf8(std::string& Out, unsigned Cp)
	{
		if (Cp < 0x80) Out += static_cast<char>(Cp);
		else if (Cp < 0x800)
		{
			Out += static_cast<char>(0xC0 | (Cp >> 6));
			Out += static_cast<char>(0x80 | (Cp & 0x3F));
		}
		else if (Cp < 0x10000)
		{
			Out += static_cast<char>(0xE0 | (Cp >> 12));
			Out += static_cast<char>(0x80 | ((Cp >> 6) & 0x3F));
			Out += static_cast<char>(0x80 | (Cp & 0x3F));
		}
		else
		{
			Out += static_cast<char>(0xF0 | (Cp >> 18));
			Out += static_cast<char>(0x80 | ((Cp >> 12) & 0x3F));
			Out += static_cast<char>(0x80 | ((Cp >> 6) & 0x3F));
			Out += static_cast<char>(0x80 | (Cp & 0x3F));
		}
	}
	bool Hex4(unsigned& Out)
	{
		if (I + 4 > T.size()) return Fail("short \\u escape");
		Out = 0;
		for (int K = 0; K < 4; ++K)
		{
			const char C = T[I++];
			Out <<= 4;
			if (C >= '0' && C <= '9') Out |= static_cast<unsigned>(C - '0');
			else if (C >= 'a' && C <= 'f') Out |= static_cast<unsigned>(C - 'a' + 10);
			else if (C >= 'A' && C <= 'F') Out |= static_cast<unsigned>(C - 'A' + 10);
			else return Fail("bad \\u escape");
		}
		return true;
	}
	bool ReadString(std::string& Out)
	{
		if (T[I] != '"') return Fail("expected a string");
		++I;
		while (I < T.size())
		{
			const char C = T[I++];
			if (C == '"') return true;
			if (C != '\\')
			{
				Out += C;
				continue;
			}
			if (I >= T.size()) break;
			const char E = T[I++];
			switch (E)
			{
			case '"': Out += '"'; break;
			case '\\': Out += '\\'; break;
			case '/': Out += '/'; break;
			case 'b': Out += '\b'; break;
			case 'f': Out += '\f'; break;
			case 'n': Out += '\n'; break;
			case 'r': Out += '\r'; break;
			case 't': Out += '\t'; break;
			case 'u':
			{
				unsigned Cp = 0;
				if (!Hex4(Cp)) return false;
				if (Cp >= 0xD800 && Cp < 0xDC00 && I + 6 <= T.size() && T[I] == '\\' && T[I + 1] == 'u')
				{
					I += 2;
					unsigned Lo = 0;
					if (!Hex4(Lo)) return false;
					Cp = 0x10000 + ((Cp - 0xD800) << 10) + (Lo - 0xDC00);
				}
				PutUtf8(Out, Cp);
				break;
			}
			default: return Fail("bad escape");
			}
		}
		return Fail("unterminated string");
	}
	bool ReadValue(Json& Out, int Depth)
	{
		if (Depth > 64) return Fail("nested too deep");
		SkipSpace();
		if (I >= T.size()) return Fail("unexpected end");
		const char C = T[I];
		if (C == '{')
		{
			Out.Kind = Json::Object;
			++I;
			SkipSpace();
			if (I < T.size() && T[I] == '}') { ++I; return true; }
			while (true)
			{
				SkipSpace();
				std::string Key;
				if (I >= T.size() || !ReadString(Key)) return Fail("expected a key");
				SkipSpace();
				if (I >= T.size() || T[I] != ':') return Fail("expected ':'");
				++I;
				Out.O.emplace_back(Key, Json());
				if (!ReadValue(Out.O.back().second, Depth + 1)) return false;
				SkipSpace();
				if (I < T.size() && T[I] == ',') { ++I; continue; }
				if (I < T.size() && T[I] == '}') { ++I; return true; }
				return Fail("expected ',' or '}'");
			}
		}
		if (C == '[')
		{
			Out.Kind = Json::Array;
			++I;
			SkipSpace();
			if (I < T.size() && T[I] == ']') { ++I; return true; }
			while (true)
			{
				Out.A.emplace_back();
				if (!ReadValue(Out.A.back(), Depth + 1)) return false;
				SkipSpace();
				if (I < T.size() && T[I] == ',') { ++I; continue; }
				if (I < T.size() && T[I] == ']') { ++I; return true; }
				return Fail("expected ',' or ']'");
			}
		}
		if (C == '"')
		{
			Out.Kind = Json::String;
			return ReadString(Out.S);
		}
		if (C == 't') { Out.Kind = Json::Bool; Out.B = true; return Literal("true"); }
		if (C == 'f') { Out.Kind = Json::Bool; Out.B = false; return Literal("false"); }
		if (C == 'n') { Out.Kind = Json::Null; return Literal("null"); }
		if (C == 'N') { Out.Kind = Json::Number; Out.N = std::nan(""); return Literal("NaN"); }   // Python's json writes these
		if (C == '-' || (C >= '0' && C <= '9'))
		{
			const char* Start = T.c_str() + I;
			char* End = nullptr;
			const double V = std::strtod(Start, &End);
			if (End == Start) return Fail("bad number");
			I += static_cast<size_t>(End - Start);
			Out.Kind = Json::Number;
			Out.N = V;
			return true;
		}
		return Fail("unexpected character");
	}
};

void DumpString(std::string& Out, const std::string& S)
{
	Out += '"';
	for (const char C : S)
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
				std::snprintf(Buf, sizeof(Buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(C)));
				Out += Buf;
			}
			else Out += C;
		}
	}
	Out += '"';
}

void DumpValue(std::string& Out, const Json& V, int Indent, int Level)
{
	const auto Newline = [&](int L) {
		if (Indent <= 0) return;
		Out += '\n';
		Out.append(static_cast<size_t>(Indent * L), ' ');
	};
	switch (V.Kind)
	{
	case Json::Null: Out += "null"; break;
	case Json::Bool: Out += V.B ? "true" : "false"; break;
	case Json::Number:
	{
		char Buf[40];
		if (std::isfinite(V.N) && V.N == std::floor(V.N) && std::fabs(V.N) < 1e15) std::snprintf(Buf, sizeof(Buf), "%.1f", V.N);
		else std::snprintf(Buf, sizeof(Buf), "%.9g", V.N);
		Out += Buf;
		break;
	}
	case Json::String: DumpString(Out, V.S); break;
	case Json::Array:
	{
		// Short arrays of numbers (vectors) stay on one line.
		bool Flat = V.A.size() <= 4;
		for (const Json& E : V.A) Flat = Flat && E.Kind == Json::Number;
		Out += '[';
		for (size_t K = 0; K < V.A.size(); ++K)
		{
			if (K) Out += Flat ? ", " : ",";
			if (!Flat) Newline(Level + 1);
			DumpValue(Out, V.A[K], Indent, Level + 1);
		}
		if (!Flat && !V.A.empty()) Newline(Level);
		Out += ']';
		break;
	}
	case Json::Object:
		Out += '{';
		for (size_t K = 0; K < V.O.size(); ++K)
		{
			if (K) Out += ',';
			Newline(Level + 1);
			DumpString(Out, V.O[K].first);
			Out += ": ";
			DumpValue(Out, V.O[K].second, Indent, Level + 1);
		}
		if (!V.O.empty()) Newline(Level);
		Out += '}';
		break;
	}
}
} // namespace

const Json* Json::Get(const std::string& Key) const
{
	if (Kind != Object) return nullptr;
	for (const auto& KV : O)
		if (KV.first == Key) return &KV.second;
	return nullptr;
}

Json* Json::GetMutable(const std::string& Key)
{
	if (Kind != Object) return nullptr;
	for (auto& KV : O)
		if (KV.first == Key) return &KV.second;
	return nullptr;
}

double Json::Num(const std::string& Key, double Default) const
{
	const Json* V = Get(Key);
	return V && V->Kind == Number ? V->N : Default;
}

std::string Json::Str(const std::string& Key, const std::string& Default) const
{
	const Json* V = Get(Key);
	return V && V->Kind == String ? V->S : Default;
}

bool Json::Flag(const std::string& Key, bool Default) const
{
	const Json* V = Get(Key);
	return V && V->Kind == Bool ? V->B : Default;
}

Json Json::MakeNumber(double V) { Json J; J.Kind = Number; J.N = V; return J; }
Json Json::MakeString(const std::string& V) { Json J; J.Kind = String; J.S = V; return J; }
Json Json::MakeBool(bool V) { Json J; J.Kind = Bool; J.B = V; return J; }
Json Json::MakeArray() { Json J; J.Kind = Array; return J; }
Json Json::MakeObject() { Json J; J.Kind = Object; return J; }

Json& Json::Set(const std::string& Key, const Json& V)
{
	if (Kind != Object) { *this = MakeObject(); }
	if (Json* Have = GetMutable(Key)) *Have = V;
	else O.emplace_back(Key, V);
	return *this;
}

Json& Json::Push(const Json& V)
{
	if (Kind != Array) { *this = MakeArray(); }
	A.push_back(V);
	return *this;
}

bool Json::Parse(const std::string& Text, Json& Out, std::string* Error)
{
	Out = Json();
	FReader R(Text);
	bool Ok = R.ReadValue(Out, 0);
	if (Ok)
	{
		R.SkipSpace();
		if (R.I != Text.size()) Ok = R.Fail("trailing text");
	}
	if (!Ok && Error) *Error = R.Err;
	return Ok;
}

std::string Json::Dump(int Indent) const
{
	std::string Out;
	DumpValue(Out, *this, Indent, 0);
	return Out;
}
} // namespace TorqueFit
