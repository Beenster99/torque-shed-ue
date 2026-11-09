// Checks Fit/BikeBadge (the tank badge's pictures, placement and decal skins) against the original
// game, dumped by Tools/make_godot_reference.py --badge (SimTest/data/godot_badge.json): each
// logo's picture, block by block, and a few badges stamped on a made-up tank (where each picture
// goes and every vertex of each decal's skin, both sides). Then the badge's text round trip.
#include "BikeBadge.h"
#include "BikeShop.h"
#include "FitJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace TorqueFit;

namespace
{
int Failures = 0;
int Checks = 0;

void Expect(bool Ok, const std::string& What)
{
	++Checks;
	if (!Ok || What.rfind("  ", 0) != 0 || getenv("BADGE_ALL")) std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What.c_str());
	if (!Ok) ++Failures;
}

bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::stringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

std::string Fmt(const char* F, double A, double B)
{
	char Buf[160];
	std::snprintf(Buf, sizeof(Buf), F, A, B);
	return Buf;
}

void Rgb(const Json* A, double Out[3])
{
	if (!A || !A->IsArray() || A->A.size() < 3) return;
	for (int I = 0; I < 3; ++I) Out[I] = A->A[(size_t)I].N;
}

Shop::Badge BadgeOf(const Json& J)
{
	Shop::Badge B;
	B.Logo = J.Str("logo");
	B.Brand = J.Str("brand");
	B.Font = J.Str("brand_font");
	Rgb(J.Get("logo_color"), B.LogoColour);
	Rgb(J.Get("brand_color"), B.BrandColour);
	B.LogoSize = J.Num("logo_size", 1.0);
	B.LogoX = J.Num("logo_x", 0.0);
	B.LogoY = J.Num("logo_y", 0.0);
	B.BrandSize = J.Num("brand_size", 1.0);
	B.BrandX = J.Num("brand_x", 0.0);
	B.BrandY = J.Num("brand_y", 0.0);
	return B;
}

// A skin's vertices, each (position, uv), sorted, to compare whatever order the triangles are in.
using FVert = std::tuple<long long, long long, long long, long long, long long>;
FVert Key(double X, double Y, double Z, double U, double V)
{
	auto R = [](double D) { return (long long)std::llround(D * 20000.0); };
	return FVert(R(X), R(Y), R(Z), R(U), R(V));
}
} // namespace

int main()
{
	std::string Text;
	if (!ReadFile("SimTest/data/godot_badge.json", Text))
	{
		std::printf("no SimTest/data/godot_badge.json (run from the repo root)\n");
		return 1;
	}
	Json G;
	if (!Json::Parse(Text, G))
	{
		std::printf("godot_badge.json doesn't parse\n");
		return 1;
	}

	std::printf("Logos:\n");
	const Json* Logos = G.Get("logos");
	for (const auto& L : Shop::Logos())
	{
		const Json* E = Logos ? Logos->Get(L.first) : nullptr;
		Expect(E != nullptr, "Godot has logo " + L.first);
		if (!E) continue;
		const BadgeArt::Mask M = BadgeArt::LogoMask(L.first);
		Expect(M.W == (int)E->Num("w") && M.H == (int)E->Num("h"), "  " + L.first + " size");
		const Json* Blocks = E->Get("blocks");
		double Worst = 0.0, Total = 0.0, Ink = 0.0;
		for (int By = 0; By < 32; ++By)
			for (int Bx = 0; Bx < 32; ++Bx)
			{
				double S = 0.0;
				for (int Y = 0; Y < 8; ++Y)
					for (int X = 0; X < 8; ++X) S += M.At(Bx * 8 + X, By * 8 + Y) / 255.0;
				const double Want = Blocks ? Blocks->A[(size_t)(By * 32 + Bx)].N : 0.0;
				Worst = std::max(Worst, std::fabs(S - Want));
				Total += std::fabs(S - Want);
				Ink += Want;
			}
		// Each 8 x 8 block within a few pixels' worth (Godot's bilinear shrink isn't quite a box filter),
		// and all of it within 1 %.
		Expect(Worst <= 4.0 && Total <= Ink * 0.01, Fmt(("  " + L.first + ": worst block off by %.2f px, %.3f %% in all").c_str(), Worst, 100.0 * Total / std::max(Ink, 1.0)));
	}

	std::printf("Badges on the made-up tank:\n");
	std::vector<BadgeArt::V3> Faces;
	if (const Json* F = G.Get("faces"))
		for (size_t I = 0; I + 2 < F->A.size(); I += 3) Faces.push_back({F->A[I].N, F->A[I + 1].N, F->A[I + 2].N});
	const BadgeArt::Box3 Box = BadgeArt::BoundsOf(Faces);
	const double MidX = (Box.Min.x + Box.Max.x) * 0.5;
	const Json* Badges = G.Get("badges");
	for (size_t Bi = 0; Badges && Bi < Badges->A.size(); ++Bi)
	{
		const Json& E = Badges->A[Bi];
		const Shop::Badge B = BadgeOf(*E.Get("badge"));
		const std::string Name = "badge " + std::to_string(Bi) + " (" + (B.Logo.empty() ? std::string("no logo") : B.Logo) + ", " +
			(B.Brand.empty() ? std::string("no brand") : "\"" + B.Brand + "\"") + ")";
		double Aspect = 1.0;
		if (const Json* Px = E.Get("brand_px")) Aspect = Px->A[0].N / std::max(1.0, Px->A[1].N);
		if (const Json* Bt = E.Get("brand_text"))
			Expect(BadgeArt::BrandText(Shop::CleanBadge(B)) == Bt->S, "  " + Name + " brand text " + Bt->S);
		const std::vector<BadgeArt::Pic> Pics = BadgeArt::Place(B, Box, Aspect);
		const Json* Nodes = E.Get("nodes");
		const size_t N = Nodes ? Nodes->A.size() : 0;
		Expect(N == Pics.size() * 2, Name + ": " + std::to_string(Pics.size()) + " pictures, both sides");
		if (N != Pics.size() * 2) continue;
		for (size_t K = 0; K < N; ++K)
		{
			const Json& Nd = Nodes->A[K];
			const BadgeArt::Pic& P = Pics[K % Pics.size()];
			const double Side = K < Pics.size() ? 1.0 : -1.0;
			const std::string What = "  " + Name + (P.bLogo ? " logo" : " brand") + (Side > 0.0 ? " right" : " left");
			const Json* R = Nd.Get("rect");
			const double Off = std::max({std::fabs(P.X - R->A[0].N), std::fabs(P.Y - R->A[1].N), std::fabs(P.W - R->A[2].N), std::fabs(P.H - R->A[3].N)});
			Expect(Off < 1e-5, What + Fmt(" place (off by %.2g m)%.0s", Off, 0.0));
			BadgeArt::Skin S;
			const bool bMade = BadgeArt::DecalSkin(Faces, Side, MidX, P, S);
			const Json* Pos = Nd.Get("pos");
			const Json* Nrm = Nd.Get("nrm");
			const Json* Uv = Nd.Get("uv");
			const size_t NV = Pos->A.size() / 3;
			Expect(bMade && S.Pos.size() == NV, What + " skin of " + std::to_string(NV) + " vertices (ours " + std::to_string(S.Pos.size()) + ")");
			if (!bMade || S.Pos.size() != NV) continue;
			// Every vertex where Godot has it, with its picture's place on it and its normal.
			std::vector<std::pair<FVert, size_t>> Want, Got;
			for (size_t I = 0; I < NV; ++I)
			{
				Want.push_back({Key(Pos->A[I * 3].N, Pos->A[I * 3 + 1].N, Pos->A[I * 3 + 2].N, Uv->A[I * 2].N, Uv->A[I * 2 + 1].N), I});
				Got.push_back({Key(S.Pos[I].x, S.Pos[I].y, S.Pos[I].z, S.U[I], S.V[I]), I});
			}
			std::sort(Want.begin(), Want.end());
			std::sort(Got.begin(), Got.end());
			size_t Same = 0;
			double WorstN = 0.0;
			for (size_t I = 0; I < NV; ++I)
			{
				if (Want[I].first != Got[I].first) continue;
				++Same;
				const size_t Wi = Want[I].second, Gi = Got[I].second;
				const BadgeArt::V3 WN(Nrm->A[Wi * 3].N, Nrm->A[Wi * 3 + 1].N, Nrm->A[Wi * 3 + 2].N);
				WorstN = std::max(WorstN, (WN - S.Nrm[Gi]).Length());
			}
			Expect(Same == NV, What + " vertices and picture where Godot has them (" + std::to_string(Same) + " of " + std::to_string(NV) + ")");
			Expect(WorstN < 1e-3, What + Fmt(" normals (worst off by %.2g)%.0s", WorstN, 0.0));
			// Each triangle faces out of the tank.
			int Inward = 0;
			for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
			{
				const BadgeArt::V3 &A = S.Pos[S.Idx[T]], &Bv = S.Pos[S.Idx[T + 1]], &C = S.Pos[S.Idx[T + 2]];
				const BadgeArt::V3 Avg = S.Nrm[S.Idx[T]] + S.Nrm[S.Idx[T + 1]] + S.Nrm[S.Idx[T + 2]];
				if ((Bv - A).Cross(C - A).Dot(Avg) <= 0.0) ++Inward;
			}
			Expect(Inward == 0, What + " triangles face out (" + std::to_string(Inward) + " don't)");
		}
		// As the tank item keeps it, and back.
		const Shop::Badge Clean = Shop::CleanBadge(B);
		const Shop::Badge Back = Shop::ParseBadge(Shop::BadgeText(B));
		Expect(Back.Logo == Clean.Logo && Back.Brand == Clean.Brand && Back.Font == Clean.Font && std::fabs(Back.BrandSize - Clean.BrandSize) < 1e-9 &&
			std::fabs(Back.LogoX - Clean.LogoX) < 1e-9 && std::fabs(Back.BrandColour[2] - Clean.BrandColour[2]) < 1e-3,
			"  " + Name + " text round trip");
	}
	Expect(Shop::BadgeText(Shop::Badge()).empty() && Shop::ParseBadge("").Empty() && Shop::ParseBadge("nonsense").Empty(), "no badge: no text, and back");

	std::printf("%d checks: %s\n", Checks, Failures == 0 ? "all passed" : (std::to_string(Failures) + " FAILED").c_str());
	return Failures == 0 ? 0 : 1;
}
