// A small JSON reader and writer for the fitment system's data (plain C++, no exceptions), so the
// same code runs in the game and in the standalone FitTest.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace TorqueFit
{
struct Json
{
	enum EKind { Null, Bool, Number, String, Array, Object };
	EKind Kind = Null;
	bool B = false;
	double N = 0.0;
	std::string S;
	std::vector<Json> A;
	std::vector<std::pair<std::string, Json>> O;   // in file order

	bool IsNull() const { return Kind == Null; }
	bool IsObject() const { return Kind == Object; }
	bool IsArray() const { return Kind == Array; }
	bool IsNumber() const { return Kind == Number; }
	bool IsString() const { return Kind == String; }

	// Object member, or nullptr.
	const Json* Get(const std::string& Key) const;
	Json* GetMutable(const std::string& Key);
	// Typed reads with a fallback.
	double Num(const std::string& Key, double Default = 0.0) const;
	std::string Str(const std::string& Key, const std::string& Default = std::string()) const;
	bool Flag(const std::string& Key, bool Default = false) const;
	size_t Size() const { return Kind == Array ? A.size() : (Kind == Object ? O.size() : 0); }

	// Building.
	static Json MakeNumber(double V);
	static Json MakeString(const std::string& V);
	static Json MakeBool(bool V);
	static Json MakeArray();
	static Json MakeObject();
	Json& Set(const std::string& Key, const Json& V);   // object: replace or append
	Json& Push(const Json& V);                         // array: append

	static bool Parse(const std::string& Text, Json& Out, std::string* Error = nullptr);
	std::string Dump(int Indent = 1) const;
};
} // namespace TorqueFit
