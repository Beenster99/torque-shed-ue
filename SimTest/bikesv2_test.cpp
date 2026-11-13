// [bikes v2] Every Bike Models V2 bike in Data/BikesV2/show.json loads with the game's own glb
// reader (Fit/PartMesh), has a look for each of its surfaces (Data/BikesV2/<model>.json), stands on
// the ground and is the size its file says. Plain C++, no engine.
#include "FitJson.h"
#include "PartMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using TorqueFit::Json;

namespace
{
bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::stringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

std::string Root()
{
	for (const char* R : {".", "..", "../..", "../../.."})
	{
		std::ifstream F(std::string(R) + "/Data/BikesV2/show.json");
		if (F) return R;
	}
	return ".";
}
} // namespace

int main()
{
	const std::string R = Root();
	std::string Text, Error;
	Json Show;
	if (!ReadFile(R + "/Data/BikesV2/show.json", Text) || !Json::Parse(Text, Show, &Error) || !Show.Get("bikes"))
	{
		std::printf("FAIL can't read Data/BikesV2/show.json under %s (%s)\n", R.c_str(), Error.c_str());
		return 1;
	}
	int Bad = 0;
	for (const Json& B : Show.Get("bikes")->A)
	{
		const std::string Model = B.Str("model");
		std::string Bytes, InfoText;
		TorqueFit::PartMesh M;
		Json Info;
		if (!ReadFile(R + "/Data/BikesV2/" + Model + ".glb", Bytes) || !M.LoadGlb(Bytes, Error) ||
			!ReadFile(R + "/Data/BikesV2/" + Model + ".json", InfoText) || !Json::Parse(InfoText, Info, &Error))
		{
			std::printf("FAIL %s doesn't load (%s)\n", Model.c_str(), Error.c_str());
			++Bad;
			continue;
		}
		const Json* Looks = Info.Get("surfaces");
		double LoX = 1e9, HiX = -1e9, LoY = 1e9, HiY = -1e9, HalfZ = 0.0;
		for (const TorqueFit::PartSurface& S : M.Surfaces)
		{
			if (!Looks || !Looks->Get(S.Material))
			{
				std::printf("FAIL %s: surface %s has no look\n", Model.c_str(), S.Material.c_str());
				++Bad;
			}
			for (const TorqueSim::V3& P : S.Pos)
			{
				LoX = std::min(LoX, P.x);
				HiX = std::max(HiX, P.x);
				LoY = std::min(LoY, P.y);
				HiY = std::max(HiY, P.y);
				HalfZ = std::max(HalfZ, std::fabs(P.z));
			}
		}
		// (Fitting's frame: x forward from the rear axle, y up from the ground, z to the right, metres.)
		const double Wheelbase = Info.Num("wheelbase_mm") / 1000.0;
		const bool bSize = M.Triangles() > 1000 && M.Triangles() < 200000 && std::fabs(LoY) < 0.01 && HiY > 0.9 && HiY < 1.6 &&
			LoX < -0.25 && LoX > -0.45 && HiX > Wheelbase + 0.2 && HiX < Wheelbase + 0.45 && HalfZ > 0.3 && HalfZ < 0.55;
		if (!bSize) ++Bad;
		std::printf("%s %s: %zu surfaces, %zu triangles, %.2f m long, %.2f m high, %.2f m wide\n", bSize ? "ok  " : "FAIL", Model.c_str(),
			M.Surfaces.size(), M.Triangles(), HiX - LoX, HiY - LoY, HalfZ * 2.0);
	}
	std::printf(Bad ? "bikesv2_test: %d FAILED\n" : "bikesv2_test: all passed\n", Bad);
	return Bad ? 1 : 0;
}
