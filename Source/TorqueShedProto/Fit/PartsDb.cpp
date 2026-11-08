#include "PartsDb.h"

namespace TorqueFit
{
bool PartsDb::LoadFromText(const std::string& InText, std::string& Error)
{
	Json Parsed;
	if (!Json::Parse(InText, Parsed, &Error)) return false;
	if (!Parsed.IsObject())
	{
		Error = "parts.json: the top level is not an object of kinds";
		return false;
	}
	for (const auto& KV : Parsed.O)
	{
		if (!KV.second.IsObject())
		{
			Error = "parts.json: kind " + KV.first + " is not an object of parts";
			return false;
		}
	}
	Root = std::move(Parsed);
	return true;
}

std::vector<std::string> PartsDb::Kinds() const
{
	std::vector<std::string> Out;
	for (const auto& KV : Root.O) Out.push_back(KV.first);
	return Out;
}

std::vector<std::string> PartsDb::Ids(const std::string& Kind) const
{
	std::vector<std::string> Out;
	if (const Json* K = Root.Get(Kind))
		for (const auto& KV : K->O) Out.push_back(KV.first);
	return Out;
}

size_t PartsDb::Count() const
{
	size_t N = 0;
	for (const auto& KV : Root.O) N += KV.second.O.size();
	return N;
}

const Json* PartsDb::Find(const std::string& Kind, const std::string& Id) const
{
	const Json* K = Root.Get(Kind);
	return K ? K->Get(Id) : nullptr;
}

double PartsDb::Number(const std::string& Kind, const std::string& Id, const std::string& Field, double Default) const
{
	const Json* P = Find(Kind, Id);
	return P ? P->Num(Field, Default) : Default;
}

std::string PartsDb::Text(const std::string& Kind, const std::string& Id, const std::string& Field) const
{
	const Json* P = Find(Kind, Id);
	return P ? P->Str(Field) : std::string();
}

bool PartsDb::HasStyle(const std::string& Kind, const std::string& Id, const std::string& Style) const
{
	const Json* P = Find(Kind, Id);
	const Json* Styles = P ? P->Get("styles") : nullptr;
	if (!Styles || !Styles->IsArray()) return false;
	for (const Json& S : Styles->A)
		if (S.IsString() && S.S == Style) return true;
	return false;
}
} // namespace TorqueFit
