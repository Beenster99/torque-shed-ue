// The grass and trees on a ride's land (Ride UK stage 3: the look of the hand-built ride, made as you
// go). Tools/make_foliage.py lays Buttertubs' plants out once from its own maps; this does the same
// for any square of the world, from nothing but the cells (cover, canopy, the ground's slope and the
// roads), so every rider has the same tufts in the same places. The game draws them with Buttertubs'
// own plant and tree models (World/TorqueRideWorld: the RideUK level's kit).
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace
{
const char* const KindNames[RidePlan::PlantKinds] = {
	"carpet_green", "carpet_moor", "carpet_far_green", "carpet_far_moor", "tuft_a", "tuft_b", "tussock_a", "tussock_b", "matgrass", "far_a", "far_b",
	"rush_a", "rush_b", "heather_twig_a", "heather_twig_b", "heather_a", "heather_b", "bracken",
	"ash", "sycamore", "alder", "birch", "hawthorn", "spruce"};

enum Kind : uint16_t
{
	CarpetGreen, CarpetMoor, CarpetFarGreen, CarpetFarMoor, TuftA, TuftB, TussockA, TussockB, MatGrass, FarA, FarB, RushA, RushB,
	TwigA, TwigB, HeatherA, HeatherB, Bracken, Ash, Sycamore, Alder, Birch, Hawthorn, Spruce
};

// The cover ids the baker writes (Terrain::Cover).
enum CoverId : int { CGrass = 0, CForest = 1, CCrop = 2, CShrub = 3, CBarren = 4, CUrban = 5, CSand = 6, CWetland = 7, CWater = 8, CRock = 9 };

uint32_t Mix(uint32_t H)
{
	H ^= H >> 16; H *= 0x7feb352dU; H ^= H >> 15; H *= 0x846ca68bU; H ^= H >> 16;
	return H;
}

// A number from 0 to 1, always the same for the same grid point, layer and salt.
double Hash(int64_t I, int64_t J, uint32_t Salt)
{
	const uint32_t H = Mix(static_cast<uint32_t>(I) * 73856093u ^ Mix(static_cast<uint32_t>(J) * 19349663u ^ Salt * 83492791u));
	return double(H & 0xffffffu) / double(0x1000000);
}

double Smooth(double Lo, double Hi, double V)
{
	const double T = std::fmin(1.0, std::fmax(0.0, (V - Lo) / (Hi - Lo)));
	return T * T * (3.0 - 2.0 * T);
}

// Smooth value noise, 0..1, features about Scale metres across, and three octaves of it.
double Noise(double X, double Z, double Scale, uint32_t Salt)
{
	const double U = X / Scale, V = Z / Scale;
	const double Fu = std::floor(U), Fv = std::floor(V);
	const int64_t I = static_cast<int64_t>(Fu), J = static_cast<int64_t>(Fv);
	double Tu = U - Fu, Tv = V - Fv;
	Tu = Tu * Tu * (3.0 - 2.0 * Tu);
	Tv = Tv * Tv * (3.0 - 2.0 * Tv);
	const double A = Hash(I, J, Salt), B = Hash(I + 1, J, Salt), C = Hash(I, J + 1, Salt), D = Hash(I + 1, J + 1, Salt);
	return A + (B - A) * Tu + (C - A) * Tv + (A - B - C + D) * Tu * Tv;
}

double Fbm(double X, double Z, double Scale, uint32_t Salt)
{
	return (Noise(X, Z, Scale, Salt) * 0.57 + Noise(X, Z, Scale * 0.5, Salt + 1) * 0.29 + Noise(X, Z, Scale * 0.25, Salt + 2) * 0.14);
}
} // namespace

const char* RidePlan::PlantName(int Kind)
{
	return Kind >= 0 && Kind < PlantKinds ? KindNames[Kind] : "";
}

void RidePlan::PlantsTile(double X0, double Z0, double Size, int Cells, const RideMesh& Mesh, std::vector<RidePlant>& Out) const
{
	Out.clear();
	if (!Land || Cells < 1) return;
	const int N = Cells + 1;
	if (Mesh.Vertices() < size_t(N) * size_t(N)) return;
	const double Step = Size / Cells;
	const bool Fine = Step <= 4.5;
	if (Step > 8.5) return;

	const double Keep = std::fmin(1.0, std::fmax(0.0, GrassDensity));
	if (!(Fine && Keep > 0.0) && !(RealTrees && Land->HasTrees())) return;

	// The ground drawn: its height and its tilt anywhere on the square, from the mesh's own points.
	const auto GridY = [&Mesh, N](int I, int J) { return double(Mesh.Pos[(size_t(J) * N + I) * 3 + 1]); };
	const auto Surface = [&](double X, double Z, double& Y, double& Nx, double& Nz)
	{
		const double U = std::fmin(std::fmax((X - X0) / Step, 0.0), Cells - 1e-6), V = std::fmin(std::fmax((Z - Z0) / Step, 0.0), Cells - 1e-6);
		const int I = int(U), J = int(V);
		const double Tu = U - I, Tv = V - J;
		const double A = GridY(I, J), B = GridY(I + 1, J), C = GridY(I, J + 1), D = GridY(I + 1, J + 1);
		// (the same two triangles LandTile draws: A B C and B D C)
		if (Tu + Tv <= 1.0)
		{
			Y = A + (B - A) * Tu + (C - A) * Tv;
			Nx = -(B - A) / Step; Nz = -(C - A) / Step;
		}
		else
		{
			Y = D + (C - D) * (1.0 - Tu) + (B - D) * (1.0 - Tv);
			Nx = -(D - C) / Step; Nz = -(D - B) / Step;
		}
		const double Len = std::sqrt(Nx * Nx + 1.0 + Nz * Nz);
		Nx /= Len; Nz /= Len;
	};

	// How far each point of a 4 m grid over the square (and a little past it) is from the nearest
	// road's edge, from the roads' own points (no further than Reach: past that, "far").
	constexpr double Reach = 150.0, Cell = 4.0;
	const int G = int(std::ceil(Size / Cell)) + 1;
	std::vector<float> Edge(size_t(G) * G, float(Reach));
	{
		const double Bx0 = X0 - Reach, Bz0 = Z0 - Reach, Bx1 = X0 + Size + Reach, Bz1 = Z0 + Size + Reach;
		for (int W = 0; W < RoadCount(); ++W)
		{
			const std::vector<RoadPoint>& Pts = RoadOf(W);
			for (size_t K = 0; K < Pts.size(); ++K)
			{
				const RoadPoint& P = Pts[K];
				// (the point and every 2 m on to the next: a side road's points can be far apart)
				const P2 Next = K + 1 < Pts.size() ? Pts[K + 1].At : P.At;
				if (std::fmax(P.At.X, Next.X) < Bx0 || std::fmin(P.At.X, Next.X) > Bx1 || std::fmax(P.At.Z, Next.Z) < Bz0 || std::fmin(P.At.Z, Next.Z) > Bz1) continue;
				const int Bits = std::max(1, int(std::ceil(P.At.DistanceTo(Next) / 2.0)));
				for (int Bit = 0; Bit < Bits; ++Bit)
				{
					const P2 At = P.At + (Next - P.At) * (double(Bit) / Bits);
					const int I0 = std::max(0, int(std::floor((At.X - Reach - X0) / Cell))), I1 = std::min(G - 1, int(std::ceil((At.X + Reach - X0) / Cell)));
					const int J0 = std::max(0, int(std::floor((At.Z - Reach - Z0) / Cell))), J1 = std::min(G - 1, int(std::ceil((At.Z + Reach - Z0) / Cell)));
					for (int J = J0; J <= J1; ++J)
						for (int I = I0; I <= I1; ++I)
						{
							const double D = std::hypot(X0 + I * Cell - At.X, Z0 + J * Cell - At.Z) - P.Half;
							float& E = Edge[size_t(J) * G + I];
							if (D < E) E = float(D);
						}
				}
			}
		}
	}
	const auto RoadEdge = [&](double X, double Z)
	{
		const double U = std::fmin(std::fmax((X - X0) / Cell, 0.0), G - 1.000001), V = std::fmin(std::fmax((Z - Z0) / Cell, 0.0), G - 1.000001);
		const int I = int(U), J = int(V);
		const double Tu = U - I, Tv = V - J;
		const double A = Edge[size_t(J) * G + I], B = Edge[size_t(J) * G + I + 1], C = Edge[size_t(J + 1) * G + I], D = Edge[size_t(J + 1) * G + I + 1];
		return A + (B - A) * Tu + (C - A) * Tv + (A - B - C + D) * Tu * Tv;
	};

	const auto Put = [&](uint16_t What, double X, double Z, double Yaw, double Scale, double Down, bool Tilt, double Height, double Pick, bool Far)
	{
		double Y = 0.0, Nx = 0.0, Nz = 0.0;
		Surface(X, Z, Y, Nx, Nz);
		RidePlant P;
		P.Kind = What;
		P.Far = Far;
		P.X = float(X - Mesh.OriginX);
		// (sunk a little more on a slope, so an upright plant's downhill side doesn't float)
		const double Slope = std::sqrt(Nx * Nx + Nz * Nz);
		P.Y = float(Y - Down - (Tilt ? 0.0 : std::fmin(Slope, 0.7) * 0.12 * Scale * (Height > 0.0 ? 4.0 : 1.0)));
		P.Z = float(Z - Mesh.OriginZ);
		P.Yaw = float(Yaw);
		P.Scale = float(Scale);
		P.Nx = Tilt ? float(Nx) : 0.0f;
		P.Nz = Tilt ? float(Nz) : 0.0f;
		P.Height = float(Height);
		P.Pick = float(Pick);
		Out.push_back(P);
	};

	// Points on a jittered grid Spacing apart over the square; Each(X, Z, Edge, Hash) for every one
	// within Within of a road's edge. A staggered, barely jittered grid for mats laid edge to edge.
	const auto Scatter = [&](double Spacing, double Within, uint32_t Salt, double Jitter, auto&& Each)
	{
		const int64_t I0 = static_cast<int64_t>(std::ceil(X0 / Spacing)), I1 = static_cast<int64_t>(std::ceil((X0 + Size) / Spacing));
		const int64_t J0 = static_cast<int64_t>(std::ceil(Z0 / Spacing)), J1 = static_cast<int64_t>(std::ceil((Z0 + Size) / Spacing));
		for (int64_t J = J0; J < J1; ++J)
			for (int64_t I = I0; I < I1; ++I)
			{
				const double Stagger = Jitter < 1.0 ? double(J & 1) * 0.5 : 0.0;
				const double X = (double(I) + Stagger + Hash(I, J, Salt) * Jitter) * Spacing, Z = (double(J) + Hash(I, J, Salt + 101u) * Jitter) * Spacing;
				if (X < X0 || X >= X0 + Size || Z < Z0 || Z >= Z0 + Size) continue;
				double D = RoadEdge(X, Z);
				if (D > Within) continue;
				if (D < 1.5)
				{
					// (right by a road's edge the 4 m grid is too coarse a guide: how far from it exactly)
					NearOpts Opts;
					NearHit Hit;
					D = NearestOf(X, Z, Opts, Hit) ? std::fmin(D + 1.0, Hit.Dist - Hit.Half) : D;
				}
				Each(X, Z, D, Hash(I, J, Salt + 202u), Hash(I, J, Salt + 303u), Hash(I, J, Salt + 404u));
			}
	};

	// What the ground is like at a point: its cover, steepness, and the moor's patches.
	struct Here
	{
		int Cover = -1;
		double Slope = 0.0, Wet = 0.0, Molinia = 0.0, Heath = 0.0, Dry = 0.0, Speckle = 0.0;
		bool Ok = true;   // grass grows here (not water, sand, a town's hard ground, sheer rock)
	};
	// The cover and the moor's patches change over metres, not centimetres: worked out once on a 2 m
	// grid over the square and read back from the nearest point of it (the noise is the slow part).
	constexpr double Fg = 2.0;
	const int F = int(std::ceil(Size / Fg)) + 1;
	struct Field { int Cover = -1; float Wet = 0.0f, Molinia = 0.0f, Heath = 0.0f, Dry = 0.0f; };
	std::vector<Field> Fields(size_t(F) * F);
	for (int J = 0; J < F; ++J)
		for (int I = 0; I < F; ++I)
		{
			const double X = X0 + I * Fg, Z = Z0 + J * Fg;
			Field& Fd = Fields[size_t(J) * F + I];
			Fd.Cover = Land->Cover(X, Z);
			Fd.Molinia = float(Fbm(X, Z, 45.0, 10u));
			Fd.Heath = float(Fbm(X, Z, 30.0, 20u));
			Fd.Dry = float(Fbm(X, Z, 60.0, 30u));
			const double WetBase = Fd.Cover == CWetland ? 0.75 : Fd.Cover == CShrub ? 0.4 : 0.2;
			Fd.Wet = float(WetBase + (Fbm(X, Z, 18.0, 50u) - 0.5) * 0.6);
		}
	const auto Look = [&](double X, double Z)
	{
		const int I = std::min(F - 1, std::max(0, int(std::lround((X - X0) / Fg)))), J = std::min(F - 1, std::max(0, int(std::lround((Z - Z0) / Fg))));
		const Field& Fd = Fields[size_t(J) * F + I];
		Here H;
		H.Cover = Fd.Cover;
		double Y = 0.0, Nx = 0.0, Nz = 0.0;
		Surface(X, Z, Y, Nx, Nz);
		const double Ny = std::sqrt(std::fmax(0.0, 1.0 - Nx * Nx - Nz * Nz));
		H.Slope = Ny > 1e-6 ? std::sqrt(Nx * Nx + Nz * Nz) / Ny : 10.0;
		H.Molinia = Fd.Molinia;
		H.Heath = Fd.Heath;
		H.Dry = Fd.Dry;
		H.Speckle = Noise(X, Z, 3.0, 40u);
		H.Wet = std::fmin(1.0, std::fmax(0.0, double(Fd.Wet) + (H.Slope < 0.05 ? 0.1 : 0.0)));
		H.Ok = H.Cover != CWater && H.Cover != CSand && H.Cover != CRock && H.Cover != CUrban && H.Slope < 1.1;
		return H;
	};
	const double Pi2 = 6.283185307179586;

	if (Fine && Keep > 0.0)
	{
		// The carpet: grass mats edge to edge, fine ones near the road and coarse ones further out.
		struct Band { double Spacing, Reach, Clear; uint16_t Green, Moor; uint32_t Salt; };
		for (const Band& B : {Band{1.45, 48.0, 1.1, CarpetGreen, CarpetMoor, 1000u}, Band{3.0, 140.0, 2.1, CarpetFarGreen, CarpetFarMoor, 2000u}})
			Scatter(B.Spacing, B.Reach, B.Salt, 0.25, [&](double X, double Z, double D, double H1, double H2, double H3)
			{
				if (D < B.Clear) return;
				const Here H = Look(X, Z);
				if (!(H.Ok || H.Cover == CUrban) || H.Cover == CForest) return;
				if (H.Cover == CUrban && H2 > 0.4) return;   // (gardens and greens, not the yards and lanes)
				if (H1 > Keep) return;
				const bool Moor = (Smooth(0.4, 0.6, H.Molinia + 0.3 * H.Dry - 0.15) * (D > 4.0 ? 1.0 : 0.0) + (H.Cover == CShrub ? 0.3 : 0.0)) > H2;
				Put(Moor ? B.Moor : B.Green, X, Z, H3 * Pi2, 0.9 + 0.25 * Hash(int64_t(X * 10.0), int64_t(Z * 10.0), 7u), 0.02, true, 0.0, 0.0, false);
			});
		// ...and a closer row along the verge, so the grass runs up to the tarmac's edge.
		Scatter(0.85, 1.5, 3000u, 0.2, [&](double X, double Z, double D, double H1, double, double H3)
		{
			if (D < 0.75 || H1 > Keep) return;
			const Here H = Look(X, Z);
			if (H.Cover == CWater || H.Cover == CRock) return;
			Put(CarpetGreen, X, Z, H3 * Pi2, 0.75 + 0.15 * H1, 0.02, true, 0.0, 0.0, false);
		});

		// Grass clumps, the main cover, within 58 m of a road.
		Scatter(0.6, 58.0, 4000u, 1.0, [&](double X, double Z, double D, double H1, double H2, double H3)
		{
			if (D < 0.4) return;
			const Here H = Look(X, Z);
			if (!H.Ok || H.Cover == CForest) return;
			double P = (1.0 - 0.6 * Smooth(12.0, 58.0, D)) * (0.6 + 0.4 * H.Speckle);
			P *= 1.0 - 0.7 * Smooth(0.55, 0.85, H.Wet);
			const double Heath = (H.Cover == CShrub ? 1.0 : 0.0) * Smooth(0.6, 0.72, H.Heath) * (H.Wet < 0.6 ? 1.0 : 0.0);
			P *= 1.0 - 0.75 * Heath;
			if (H.Cover == CCrop) P *= 0.6;
			if (H1 >= P * Keep) return;
			const bool Verge = D < 2.5;
			const double Mol = Smooth(0.45, 0.65, H.Molinia) * (D > 3.0 ? 1.0 : 0.0) * (H.Cover == CShrub ? 1.0 : 0.45);
			const double Mat = Smooth(0.55, 0.7, H.Dry) * (1.0 - Mol) * (D > 2.0 ? 1.0 : 0.0);
			uint16_t What = H2 < 0.5 ? TuftA : TuftB;
			if (!Verge && H2 < Mol) What = H3 < 0.5 ? TussockA : TussockB;
			else if (!Verge && H2 < Mol + Mat) What = MatGrass;
			const double Scale = (0.75 + 0.55 * Hash(int64_t(X * 10.0), int64_t(Z * 10.0), 8u)) * (Verge ? 0.7 : 1.0);   // (the verge is cut and grazed)
			Put(What, X, Z, H3 * Pi2, Scale, 0.03, false, 0.0, 0.0, false);
		});

		// Coarse clumps for the middle distance, out to 140 m.
		Scatter(2.1, 140.0, 5000u, 1.0, [&](double X, double Z, double D, double H1, double H2, double H3)
		{
			if (D < 0.8) return;
			const Here H = Look(X, Z);
			if (!H.Ok || H.Cover == CForest) return;
			if (H1 >= 0.85 * (1.0 - 0.8 * Smooth(0.6, 0.85, H.Wet)) * Keep) return;
			Put(H2 < 0.5 ? FarA : FarB, X, Z, H3 * Pi2, 0.8 + 0.5 * H2, 0.03, false, 0.0, 0.0, false);
		});

		// Soft rush where it is wet, and the odd clump anywhere, as in every Dales pasture.
		Scatter(1.4, 95.0, 6000u, 1.0, [&](double X, double Z, double D, double H1, double H2, double H3)
		{
			if (D < 1.2) return;
			const Here H = Look(X, Z);
			if (!H.Ok) return;
			if (H1 >= (Smooth(0.5, 0.8, H.Wet) * 0.9 + 0.02) * Keep) return;
			Put(H2 < 0.5 ? RushA : RushB, X, Z, H3 * Pi2, 0.75 + 0.5 * H2, 0.05, false, 0.0, 0.0, false);
		});

		// Heather in patches on the moor (shrub) and a little on rough grazing; twiggy bushes near the road.
		Scatter(0.85, 75.0, 7000u, 1.0, [&](double X, double Z, double D, double H1, double H2, double H3)
		{
			if (D < 1.5) return;
			const Here H = Look(X, Z);
			if (!H.Ok) return;
			const double Heath = ((H.Cover == CShrub ? 1.0 : 0.0) + (H.Cover == CGrass ? 0.25 : 0.0)) * Smooth(0.6, 0.7, H.Heath) * (1.0 - Smooth(0.5, 0.75, H.Wet));
			if (H1 >= Heath * Keep) return;
			const bool A = H2 < 0.6;
			const uint16_t What = D < 22.0 ? (A ? TwigA : TwigB) : (A ? HeatherA : HeatherB);
			Put(What, X, Z, H3 * Pi2, 0.75 + 0.55 * Hash(int64_t(X * 10.0), int64_t(Z * 10.0), 9u), 0.04, false, 0.0, 0.0, false);
		});

		// Bracken on the steeper, drier slopes and under the woods' edges.
		Scatter(1.1, 105.0, 8000u, 1.0, [&](double X, double Z, double D, double H1, double H2, double H3)
		{
			if (D < 2.0) return;
			const Here H = Look(X, Z);
			if (!(H.Ok || H.Cover == CForest)) return;
			const double Steep = H.Cover == CForest ? 0.5 : Smooth(0.25, 0.45, H.Slope);
			const double P = Steep * (1.0 - Smooth(0.4, 0.6, H.Wet)) * Smooth(0.4, 0.6, Fbm(X, Z, 35.0, 60u)) * 0.9;
			if (H1 >= P * Keep) return;
			Put(Bracken, X, Z, H3 * Pi2, 0.8 + 0.45 * H2, 0.03, false, 0.0, 0.0, false);
		});
	}

	// Trees where the map's canopy says there are some, as tall as it says: broadleaved ones (ash and
	// sycamore mostly, alder in the wet, birch now and then, hawthorn where they are short) or spruce.
	if (RealTrees && Land->HasTrees())
	{
		const double Grid = Fine ? 7.0 : 14.0;
		const int64_t Gx0 = static_cast<int64_t>(std::ceil(X0 / Grid)), Gx1 = static_cast<int64_t>(std::ceil((X0 + Size) / Grid));
		const int64_t Gz0 = static_cast<int64_t>(std::ceil(Z0 / Grid)), Gz1 = static_cast<int64_t>(std::ceil((Z0 + Size) / Grid));
		for (int64_t Gz = Gz0; Gz < Gz1; ++Gz)
			for (int64_t Gx = Gx0; Gx < Gx1; ++Gx)
			{
				const double X = (double(Gx) + Hash(Gx, Gz, 11u) * 0.8) * Grid, Z = (double(Gz) + Hash(Gx, Gz, 12u) * 0.8) * Grid;
				if (X < X0 || X >= X0 + Size || Z < Z0 || Z >= Z0 + Size) continue;
				const int Trees = Land->TreesAt(X, Z);
				if (Trees <= 0) continue;
				const double Tall = double(Trees & 31) * 2.0, Share = double((Trees >> 5) & 7) / 8.0;
				if (Tall < 3.0 || Hash(Gx, Gz, 13u) > Share * (Fine ? 0.8 : 1.0) * 1.2) continue;
				if (RoadEdge(X, Z) < 5.0) continue;
				const int Leaf = (Trees >> 8) & 3;
				const double Roll = Hash(Gx, Gz, 14u);
				uint16_t What = Ash;
				const bool Needle = Leaf == 2 || (Leaf == 3 && Roll < 0.5);
				if (Needle) What = Spruce;
				else if (Tall < 6.5) What = Hawthorn;
				else
				{
					const Here H = Look(X, Z);
					const double Wet = H.Wet + (H.Cover == CWetland ? 0.3 : 0.0);
					What = Roll < 0.1 + 0.4 * Smooth(0.45, 0.8, Wet) ? Alder : Roll < 0.55 ? Ash : Roll < 0.94 ? Sycamore : Birch;
				}
				const double Height = Tall * (0.8 + 0.35 * Hash(Gx, Gz, 15u));
				Put(What, X, Z, Hash(Gx, Gz, 17u) * Pi2, 1.0, 0.15, false, Height, Hash(Gx, Gz, 16u), !Fine);
			}
	}
}
} // namespace TorqueWorld
