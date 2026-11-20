// [level crossings] Checks every type's layout (World/WorldLevelCrossing) and, given a path, writes them as JSON for
// Tools/props/render_lx_sheet.py, so the cloud's preview renders show the game's own layout.
//
//     lx_test [out.json]
#include "WorldLevelCrossing.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace TorqueWorld;

static int Fails = 0;
static void Check(bool Ok, const char* What, int Type)
{
	if (!Ok)
	{
		++Fails;
		std::printf("FAIL %s (%s)\n", What, LevelX::TypeName(Type));
	}
}

int main(int Argc, char** Argv)
{
	FILE* Json = Argc > 1 ? std::fopen(Argv[1], "w") : nullptr;
	if (Json) std::fprintf(Json, "{\"crossings\": [\n");
	bool First = true;
	// Each type square across a 6.4 m road, then skewed 35 degrees across a 4 m one.
	for (int Skew = 0; Skew < 2; ++Skew)
		for (int Type = 0; Type < LevelX::Types; ++Type)
		{
			LevelX::Spot S;
			S.Mid = P2(Type * 60.0, Skew * 70.0);
			S.Road = P2(0.0, -1.0);   // (north)
			const double Ang = (Skew ? 35.0 : 90.0) * 3.14159265 / 180.0;   // (between the road and the line)
			S.Rail = P2(std::sin(Ang), -std::cos(Ang));
			S.Half = Skew ? 2.0 : 3.2;
			S.Key = 0x9e3779b9u * uint32_t(Type + 1 + Skew * 7);
			const std::vector<LevelX::Piece> Pieces = LevelX::Layout(Type, S);
			const double Far = LevelX::StandOff(S);
			int Booms = 0, Units = 0, Gates = 0, Paint = 0;
			for (const LevelX::Piece& P : Pieces)
			{
				// Nothing on the road but its paint (and a boom or gate's machine at its kerb is off it).
				const P2 D = P.At - S.Mid;
				const double V = D.X * S.Road.Z - D.Z * S.Road.X;
				const double U = D.X * S.Road.X + D.Z * S.Road.Z;
				if (P.Role != LevelX::Paint) Check(std::fabs(V) > S.Half + 0.1 || std::fabs(U) > Far + 20.0, "a piece stands on the road", Type);
				// Nothing nearer the line than CLEAR - 1 (the fence's line) but the paint.
				const P2 Rn(-S.Rail.Z, S.Rail.X);
				const double Perp = std::fabs(D.X * Rn.X + D.Z * Rn.Z);
				if (P.Role != LevelX::Paint) Check(Perp > LevelX::Clear - 1.05, "a piece is too near the rails", Type);
				Booms += P.Role == LevelX::Boom;
				Units += P.Role == LevelX::Unit;
				Gates += P.Role == LevelX::Gate;
				Paint += P.Role == LevelX::Paint;
				if (P.Role == LevelX::Boom) Check(P.Length > 1.5 && P.Length < 9.5, "a boom's length", Type);
				if (Json)
				{
					std::fprintf(Json, "%s{\"type\": %d, \"skew\": %d, \"mesh\": \"%s\", \"role\": %d, \"approach\": %d, \"x\": %.3f, \"z\": %.3f, \"up\": %.3f, "
						"\"fx\": %.4f, \"fz\": %.4f, \"sx\": %.4f, \"sz\": %.4f, \"length\": %.3f, \"width\": %.3f, \"skirted\": %d, \"offside\": %d, \"row\": %d, "
						"\"mid\": [%.3f, %.3f], \"road\": [%.4f, %.4f], \"rail\": [%.4f, %.4f], \"half\": %.3f}",
						First ? "" : ",\n", Type, Skew, P.Mesh.c_str(), P.Role, P.Approach, P.At.X, P.At.Z, P.Up, P.Front.X, P.Front.Z, P.Shut.X, P.Shut.Z,
						P.Length, P.Width, P.Skirted ? 1 : 0, P.Offside ? 1 : 0, P.Row, S.Mid.X, S.Mid.Z, S.Road.X, S.Road.Z, S.Rail.X, S.Rail.Z, S.Half);
					First = false;
				}
			}
			const bool Lit = Type == LevelX::Lights || Type == LevelX::HalfBarrier || Type == LevelX::FullBarrier;
			Check(Units == (Lit ? 4 : 0), "light units", Type);
			Check(Booms == (Type == LevelX::HalfBarrier ? 2 : Type == LevelX::FullBarrier ? 4 : 0), "booms", Type);
			Check(Gates == (Type == LevelX::Gated ? 4 : 0), "gates", Type);
			Check(Paint == 2, "paint each way", Type);
			std::printf("%-18s skew %d: %3d pieces, stand-off %.1f m\n", LevelX::TypeName(Type), Skew, int(Pieces.size()), Far);
		}
	Check(LevelX::BoomMesh(3.4) == "lx_boom_4" && LevelX::BoomMesh(12.0) == "lx_boom_9" && LevelX::GateMesh(3.3) == "lx_gate_35", "kit picks", 0);
	Check(std::fabs(LevelX::MeshLength("lx_gate_35") - 3.5) < 1e-9 && std::fabs(LevelX::MeshLength("lx_boom_7") - 7.0) < 1e-9, "kit lengths", 0);
	Check(LevelX::TypeFor("primary", 3.5, 1) == LevelX::FullBarrier && LevelX::TypeFor("track", 1.5, 0) == LevelX::Gated, "types by road", 0);
	if (Json)
	{
		std::fprintf(Json, "\n]}\n");
		std::fclose(Json);
	}
	std::printf(Fails ? "%d FAILED\n" : "lx_test: all passed\n", Fails);
	return Fails ? 1 : 0;
}
