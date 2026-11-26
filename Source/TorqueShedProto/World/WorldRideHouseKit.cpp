// [house kit] The detailed detached houses: reading the kit, choosing a design for a footprint, laying it or
// handing it over as an instance. See WorldRideHouseKit.h.
#include "WorldRideHouseKit.h"

#include "WorldRide.h"
#include "FitJson.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex HouseKitLock;
std::atomic<bool> HouseInstancingOn{false};
std::shared_ptr<const HouseKit>& HouseKitTable()
{
	static std::shared_ptr<const HouseKit> Table;
	return Table;
}

std::vector<std::string> Words(const TorqueFit::Json* J)
{
	std::vector<std::string> Out;
	if (J && J->IsArray())
		for (const TorqueFit::Json& E : J->A) Out.push_back(E.S);
	return Out;
}

uint8_t Byte(double V)
{
	return uint8_t(std::clamp(int(std::lround(V * 255.0)), 0, 255));
}

// One piece of a house into the meshes: its points turned and stretched into place (X, Z, Y its origin; x along
// (Ux, Uz) stretched Sx, z out along (Nx, Nz) stretched Sz), the parts of at least level Need, in the colours of
// Colours (3 bytes a part, or null for the piece's own). Each section into its mesh: Into[0] plain .. [5] glass.
size_t LayHousePiece(const WarehousePiece& P, const uint8_t* Colours, double X, double Z, double Y, double Ux, double Uz, double Nx, double Nz,
	double Sx, double Sz, int Need, RideMesh* const Into[6])
{
	for (int K = 0; K < 6; ++K)
		if (Into[K]->Alpha.size() < Into[K]->Vertices()) Into[K]->Alpha.resize(Into[K]->Vertices(), 1.0f);
	std::vector<uint32_t> Where(P.Vertices(), UINT32_MAX);
	std::vector<double> Normal(P.Vertices() * 3), World(P.Vertices() * 3);
	for (size_t V = 0; V < P.Vertices(); ++V)
	{
		const size_t Part = P.PartOf[V];
		const WarehousePiece::Part& Pt = P.Parts[Part];
		if (Pt.Lod < Need) continue;
		const int S = Pt.Section < 6 ? Pt.Section : 0;
		RideMesh& Mi = *Into[S];
		const double Lx = P.Pos[V * 3] * 0.01, Ly = P.Pos[V * 3 + 1] * 0.01, Lz = P.Pos[V * 3 + 2] * 0.01;
		const double Wx = X + Lx * Sx * Ux + Lz * Sz * Nx, Wy = Y + Ly, Wz = Z + Lx * Sx * Uz + Lz * Sz * Nz;
		const double N0 = P.Nrm[V * 3] / Sx, N1 = P.Nrm[V * 3 + 1], N2 = P.Nrm[V * 3 + 2] / Sz;
		double Gx = N0 * Ux + N2 * Nx, Gy = N1, Gz = N0 * Uz + N2 * Nz;
		const double Nl = std::sqrt(Gx * Gx + Gy * Gy + Gz * Gz);
		if (Nl > 1e-9) { Gx /= Nl; Gy /= Nl; Gz /= Nl; } else { Gx = 0.0; Gy = 1.0; Gz = 0.0; }
		World[V * 3] = Wx; World[V * 3 + 1] = Wy; World[V * 3 + 2] = Wz;
		Normal[V * 3] = Gx; Normal[V * 3 + 1] = Gy; Normal[V * 3 + 2] = Gz;
		Where[V] = uint32_t(Mi.Vertices());
		const double Px = Wx - Mi.OriginX, Pz = Wz - Mi.OriginZ;
		Mi.Pos.push_back(float(Px)); Mi.Pos.push_back(float(Wy)); Mi.Pos.push_back(float(Pz));
		Mi.Nrm.push_back(float(Gx)); Mi.Nrm.push_back(float(Gy)); Mi.Nrm.push_back(float(Gz));
		// (UVs metres of surface, as the warehouse kit's: a roof's along it level and up it; a flat top's east and
		// south; a wall's along it and down it)
		const double Hl = std::sqrt(Gx * Gx + Gz * Gz);
		const double Tx = Hl > 1e-6 ? -Gz / Hl : 1.0, Tz = Hl > 1e-6 ? Gx / Hl : 0.0;
		double U, W;
		if (S == 2 && std::fabs(Gy) > 0.2)
		{
			double Sx2 = Gy * Tz, Sy2 = Gz * Tx - Gx * Tz, Sz2 = -Gy * Tx;   // (N x T)
			if (Sy2 < 0.0) { Sx2 = -Sx2; Sy2 = -Sy2; Sz2 = -Sz2; }
			U = Px * Tx + Pz * Tz;
			W = -(Px * Sx2 + Wy * Sy2 + Pz * Sz2);
		}
		else if (std::fabs(Gy) > 0.7) { U = Px; W = Pz; }
		else { U = Px * Tx + Pz * Tz; W = -Wy; }
		Mi.Uv.push_back(float(U)); Mi.Uv.push_back(float(W));
		const uint8_t* C = Colours ? Colours + Part * 3 : nullptr;
		Mi.Col.push_back((C ? C[0] : Pt.R) / 255.0f); Mi.Col.push_back((C ? C[1] : Pt.G) / 255.0f); Mi.Col.push_back((C ? C[2] : Pt.B) / 255.0f);
		Mi.Alpha.push_back(S == 5 ? 0.0f : Pt.Code / 255.0f);
	}
	size_t Laid = 0;
	for (size_t T = 0; T + 2 < P.Idx.size(); T += 3)
	{
		const uint16_t A = P.Idx[T], B = P.Idx[T + 1], C = P.Idx[T + 2];
		if (Where[A] == UINT32_MAX || Where[B] == UINT32_MAX || Where[C] == UINT32_MAX) continue;
		const int S = P.Parts[P.PartOf[A]].Section < 6 ? P.Parts[P.PartOf[A]].Section : 0;
		// (wound to face the way its corners' normals do: the export wrote them so, and a turn keeps it)
		const double* Pa = &World[size_t(A) * 3];
		const double* Pb = &World[size_t(B) * 3];
		const double* Pc = &World[size_t(C) * 3];
		const double E1[3] = {Pb[0] - Pa[0], Pb[1] - Pa[1], Pb[2] - Pa[2]}, E2[3] = {Pc[0] - Pa[0], Pc[1] - Pa[1], Pc[2] - Pa[2]};
		const double F[3] = {E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]};
		double Dot = 0.0;
		for (const uint16_t Vi : {A, B, C})
			for (int K = 0; K < 3; ++K) Dot += F[K] * Normal[size_t(Vi) * 3 + size_t(K)];
		if (Dot >= 0.0) Into[S]->Idx.insert(Into[S]->Idx.end(), {Where[A], Where[B], Where[C]});
		else Into[S]->Idx.insert(Into[S]->Idx.end(), {Where[A], Where[C], Where[B]});
		++Laid;
	}
	return Laid;
}
} // namespace

int HouseKit::Pick(const std::string& Set, double Front, double Deep, double Seed, bool Boards, double Fit) const
{
	std::vector<std::pair<int, double>> Fits;
	double Sum = 0.0;
	for (size_t I = 0; I < Designs.size(); ++I)
	{
		const HouseDesign& D = Designs[I];
		if (D.Set != Set || D.Weight <= 0.0 || D.L <= 1.0 || D.W <= 1.0) continue;
		if (D.Boards() && !Boards) continue;
		const double Off = std::fmax(std::fabs(std::log(Front / D.L)), std::fabs(std::log(Deep / D.W)));
		if (Off > Fit) continue;
		// (the nearer its size the likelier; the boarded ones, where they may stand, as likely as the rest together)
		const double W = D.Weight * (1.0 - 0.7 * Off / Fit) * (D.Boards() ? 3.0 : 1.0);
		Fits.push_back({int(I), W});
		Sum += W;
	}
	double At = Seed * Sum;
	for (const auto& F : Fits)
	{
		At -= F.second;
		if (At <= 0.0) return F.first;
	}
	return Fits.empty() ? -1 : Fits.back().first;
}

bool SetHouseKit(const std::string& JsonText, const std::string& Bin)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(JsonText, Root) || !Root.IsObject()) return false;
	auto Kit = std::make_shared<HouseKit>();
	std::map<std::string, int> Named;
	if (!ReadKitPieces(Bin, Kit->Pieces, Named)) return false;
	const TorqueFit::Json* Rows = Root.Get("designs");
	if (!Rows || !Rows->IsArray()) return false;
	for (const TorqueFit::Json& Row : Rows->A)
	{
		HouseDesign D;
		D.Name = Row.Str("name");
		D.Label = Row.Str("label");
		D.Set = Row.Str("set");
		D.L = Row.Num("L"); D.W = Row.Num("W"); D.H = Row.Num("H");
		D.Floors = int(Row.Num("floors", 2.0));
		D.Weight = Row.Num("weight", 1.0);
		D.Classes = Words(Row.Get("classes"));
		D.Regions = Words(Row.Get("regions"));
		D.Roles = Words(Row.Get("roles"));
		for (const std::string& P : Words(Row.Get("pieces")))
		{
			const auto It = Named.find(P);
			if (It != Named.end()) D.Pieces.push_back(It->second);
		}
		if (D.Pieces.empty() || D.Roles.empty()) continue;
		// (all its pieces carry the same parts, one per role)
		bool Same = true;
		for (const int P : D.Pieces) Same = Same && Kit->Pieces[size_t(P)].Parts.size() == D.Roles.size();
		if (!Same) continue;
		const WarehousePiece& First = Kit->Pieces[size_t(D.Pieces[0])];
		if (const TorqueFit::Json* Cws = Row.Get("colourways"); Cws && Cws->IsArray())
			for (const TorqueFit::Json& Cw : Cws->A)
			{
				std::vector<uint8_t> Colours(D.Roles.size() * 3);
				for (size_t K = 0; K < D.Roles.size(); ++K)
				{
					const WarehousePiece::Part& Pt = First.Parts[K];
					Colours[K * 3] = Pt.R; Colours[K * 3 + 1] = Pt.G; Colours[K * 3 + 2] = Pt.B;
					const TorqueFit::Json* C = Cw.Get(D.Roles[K] == "render2" ? "render" : D.Roles[K]);
					if (C && C->IsArray() && C->A.size() == 3)
						for (size_t J = 0; J < 3; ++J) Colours[K * 3 + J] = Byte(C->A[J].N);
				}
				D.Colours.push_back(std::move(Colours));
			}
		if (D.Colours.empty())
		{
			std::vector<uint8_t> Own(D.Roles.size() * 3);
			for (size_t K = 0; K < D.Roles.size(); ++K) { Own[K * 3] = First.Parts[K].R; Own[K * 3 + 1] = First.Parts[K].G; Own[K * 3 + 2] = First.Parts[K].B; }
			D.Colours.push_back(std::move(Own));
		}
		Kit->Designs.push_back(std::move(D));
	}
	if (Kit->Designs.empty()) return false;
	std::lock_guard<std::mutex> Hold(HouseKitLock);
	HouseKitTable() = std::move(Kit);
	return true;
}

std::shared_ptr<const HouseKit> HouseKitNow()
{
	std::lock_guard<std::mutex> Hold(HouseKitLock);
	return HouseKitTable();
}

void SetHouseInstancing(bool On)
{
	HouseInstancingOn.store(On);
}

bool HouseInstancing()
{
	return HouseInstancingOn.load();
}

size_t HouseLookMesh(const HouseKit& Kit, int Design, int Cw, int Lod, RideMesh& Out)
{
	if (Design < 0 || size_t(Design) >= Kit.Designs.size()) return 0;
	const HouseDesign& D = Kit.Designs[size_t(Design)];
	const uint8_t* Colours = D.Colours[size_t(std::clamp(Cw, 0, int(D.Colours.size()) - 1))].data();
	RideMesh* const Into[6] = {&Out, &RideMesh::SubOf(Out.Masonry, Out.OriginX, Out.OriginZ), &Out.RoofingOf(), &Out.BrickworkOf(), &Out.RenderOf(), &Out.NightOf()};
	size_t Tris = 0;
	for (const int P : D.Pieces)
		Tris += LayHousePiece(Kit.Pieces[size_t(P)], Colours, Out.OriginX, Out.OriginZ, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, Lod, Into);
	return Tris;
}

size_t PlaceHouse(const HouseKit& Kit, int Design, int Cw, double X, double Z, double Y, double Ux, double Uz, double Nx, double Nz,
	double Sx, double Sz, RideMesh* const Into[6])
{
	if (Design < 0 || size_t(Design) >= Kit.Designs.size()) return 0;
	const HouseDesign& D = Kit.Designs[size_t(Design)];
	const int C = std::clamp(Cw, 0, int(D.Colours.size()) - 1);
	size_t Tris = 0;
	for (const int P : D.Pieces) Tris += Kit.Pieces[size_t(P)].Idx.size() / 3;
	if (HouseInstancing())
	{
		RideMesh& Out = *Into[0];
		RideKitPart K;
		K.Kit = 1;
		K.Piece = Design * 8 + C;
		K.X = float(X - Out.OriginX);
		K.Z = float(Z - Out.OriginZ);
		K.Y = float(Y);
		K.Ax = float(Ux); K.Az = float(Uz); K.Nx = float(Nx); K.Nz = float(Nz);
		K.Sx = float(Sx); K.Sz = float(Sz); K.Sy = 1.0f;
		Out.KitParts.push_back(K);
		return Tris;
	}
	size_t Laid = 0;
	for (const int P : D.Pieces) Laid += LayHousePiece(Kit.Pieces[size_t(P)], D.Colours[size_t(C)].data(), X, Z, Y, Ux, Uz, Nx, Nz, Sx, Sz, 0, Into);
	return Laid;
}
} // namespace TorqueWorld
