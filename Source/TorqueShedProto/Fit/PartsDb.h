// The parts catalogue: the Godot game's data/parts.json, copied unchanged to Data/Parts/parts.json
// by Tools/import_parts.py. Kinds (frame, fork, handlebars, exhaust, ...) of parts by id; each part's
// fields are read by name, since they differ by kind (as parts_db.gd does).
#pragma once

#include "FitJson.h"

#include <string>
#include <vector>

namespace TorqueFit
{
class PartsDb
{
public:
	bool LoadFromText(const std::string& Text, std::string& Error);
	bool Loaded() const { return Root.IsObject(); }

	std::vector<std::string> Kinds() const;
	std::vector<std::string> Ids(const std::string& Kind) const;
	size_t Count() const;
	// The part's whole entry, or nullptr.
	const Json* Find(const std::string& Kind, const std::string& Id) const;
	double Number(const std::string& Kind, const std::string& Id, const std::string& Field, double Default = 0.0) const;
	std::string Text(const std::string& Kind, const std::string& Id, const std::string& Field) const;
	bool HasStyle(const std::string& Kind, const std::string& Id, const std::string& Style) const;

private:
	Json Root;
};
} // namespace TorqueFit
