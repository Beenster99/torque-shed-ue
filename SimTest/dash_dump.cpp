// Writes a dash part's code-drawn pieces as triangles, for Tools/dash_pictures.py to draw a picture of
// the unit as the rider sees it (there is no engine or Blender in the cloud to render one):
//     dash_dump <repo root> <style> <dashboard id>  >  unit.txt
// Each line: tag, then a triangle's three points and three normals (fitting's frame: x forward, y up,
// z right, metres). The first line is the rider's eye.
#include "BikeParts.h"
#include "BikeLayout.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace TorqueFit;

static bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::stringstream S;
	S << F.rdbuf();
	Out = S.str();
	return true;
}

int main(int argc, char** argv)
{
	if (argc < 4) return 2;
	const std::string R = argv[1];
	std::string PartsText, LayoutText, IndexText, Error;
	PartsDb Db;
	BikeLayout Lay;
	Json Index;
	if (!ReadFile(R + "/Data/Parts/parts.json", PartsText) || !ReadFile(R + "/Data/Fitment/layout.json", LayoutText) ||
		!ReadFile(R + "/Data/Parts/Blender/index.json", IndexText) || !Db.LoadFromText(PartsText, Error) || !Lay.Load(LayoutText, Db, Error) ||
		!Json::Parse(IndexText, Index, &Error))
	{
		std::fprintf(stderr, "can't load: %s\n", Error.c_str());
		return 1;
	}
	const Json* Known = Index.Get("parts");
	BikeParts Placer(Lay, Known ? *Known : Index);
	PartsMap B = Lay.ReferenceBuild(argv[2]);
	B["dashboard"] = argv[3];
	const Layout L = Lay.Solve(B);
	// (BikeParts::EyeAt: the rider's eye over the hips.)
	const V3 Eye = FromGodot(V3(0.0, L.Stats.SeatHeight - L.Stats.RearRadius + 0.88, ToGodot(L.Hip).z - 0.02));
	std::printf("eye %.5f %.5f %.5f\n", Eye.x, Eye.y, Eye.z);
	for (const FittedPart& P : Placer.Fit(B, L))
	{
		if (P.Slot != "dashboard" || !P.Made) continue;
		for (const PartSurface& S : P.Made->Surfaces)
			for (size_t K = 0; K + 2 < S.Idx.size(); K += 3)
			{
				std::printf("%s%s", S.Material.c_str(), P.Gauge.rfind("screen_", 0) == 0 ? ":screen" : "");
				for (int I = 0; I < 3; ++I) std::printf(" %.5f %.5f %.5f", S.Pos[S.Idx[K + I]].x, S.Pos[S.Idx[K + I]].y, S.Pos[S.Idx[K + I]].z);
				for (int I = 0; I < 3; ++I) std::printf(" %.3f %.3f %.3f", S.Nrm[S.Idx[K + I]].x, S.Nrm[S.Idx[K + I]].y, S.Nrm[S.Idx[K + I]].z);
				std::printf("\n");
			}
	}
	return 0;
}
