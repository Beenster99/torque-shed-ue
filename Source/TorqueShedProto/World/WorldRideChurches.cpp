// [churches stage 3] The church kit: reading it, choosing a church for a building, building it into a
// square's meshes. See WorldRideChurches.h.
#include "WorldRideChurches.h"

#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "FitJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex ChurchKitLock;
std::shared_ptr<const ChurchKit>& ChurchKitTable()
{
	static std::shared_ptr<const ChurchKit> Table;
	return Table;
}

// A little-endian reader over the kit's bytes.
struct Bytes
{
	const std::string& S;
	size_t At = 0;
	bool Bad = false;
	bool Has(size_t N) { if (At + N > S.size()) Bad = true; return !Bad; }
	template <typename T> T Get()
	{
		T V{};
		if (!Has(sizeof(T))) return V;
		std::memcpy(&V, S.data() + At, sizeof(T));
		At += sizeof(T);
		return V;
	}
	template <typename T> void Many(std::vector<T>& Out, size_t N)
	{
		if (!Has(N * sizeof(T))) return;
		Out.resize(N);
		if (N) std::memcpy(Out.data(), S.data() + At, N * sizeof(T));
		At += N * sizeof(T);
	}
};

std::string Lower(const std::string& S)
{
	std::string L = S;
	for (char& C : L) C = char(std::tolower(static_cast<unsigned char>(C)));
	return L;
}

std::vector<std::string> Words(const TorqueFit::Json* J)
{
	std::vector<std::string> Out;
	if (J && J->IsArray())
		for (const TorqueFit::Json& W : J->A)
			if (W.IsString()) Out.push_back(Lower(W.S));
	return Out;
}
} // namespace

int ChurchKit::NameKind(const std::string& Name) const
{
	if (Name.empty()) return 0;
	const std::string L = Lower(Name);
	for (const std::string& W : SkipNames)
		if (!W.empty() && L.find(W) != std::string::npos) return 2;
	for (const std::string& W : ChapelNames)
		if (!W.empty() && L.find(W) != std::string::npos) return 1;
	return 0;
}

int ChurchKit::Pick(const char* LookName, bool InTown, bool Chapel, double Long, double Short, double Seed, double Seed2, double& Along, double& Across, bool& Whole) const
{
	if (Models.empty() || Long <= 0.0 || Short <= 0.0) return -1;
	const std::string Look = LookName ? LookName : "";
	const auto RegionHas = [this, &Look](const std::string& Region)
	{
		for (const auto& R : Looks)
			if (R.first == Region) return std::find(R.second.begin(), R.second.end(), Look) != R.second.end();
		return false;
	};
	const bool TownOnes = InTown && Seed2 < TownShare;
	const auto KindFits = [Chapel](const ChurchModel& M) { return Chapel ? (M.Kind == "chapel" || M.Kind == "tin") : M.Kind == "church"; };
	// (the region's own, or in a town now and then the town ones; then the region's stretched further; then
	// the region's and the town's; where none of those fits, any)
	for (int Pass = 0; Pass < 4; ++Pass)
	{
		const double Lo = Pass == 1 || Pass == 2 ? StretchLo * 0.85 : StretchLo, Hi = Pass == 1 || Pass == 2 ? StretchHi * 1.2 : StretchHi;
		std::vector<std::pair<double, int>> Fits;   // (cost, model; a whole footprint's fit as the model's index + Models.size())
		for (size_t I = 0; I < Models.size(); ++I)
		{
			const ChurchModel& M = Models[I];
			if (!KindFits(M) || M.NaveL <= 0.0 || M.NaveW <= 0.0) continue;
			const bool Mine = Pass == 0 ? (TownOnes ? M.Region == "town" : RegionHas(M.Region)) : Pass == 1 ? RegionHas(M.Region) : Pass == 2 ? RegionHas(M.Region) || M.Region == "town" : true;
			if (!Mine) continue;
			for (int Way = 0; Way < 2; ++Way)
			{
				const double A = Long / (Way ? M.MaxX - M.MinX : M.NaveL), C = Short / (Way ? M.MaxZ - M.MinZ : M.NaveW);
				if (A < Lo || A > Hi || C < Lo || C > Hi) continue;
				Fits.push_back({std::fabs(std::log(A)) + std::fabs(std::log(C)), int(I + (Way ? Models.size() : 0))});
			}
		}
		if (Fits.empty()) continue;
		std::sort(Fits.begin(), Fits.end());
		double Sum = 0.0;
		size_t N = 0;
		const auto Of = [this](int F) { return size_t(F) % Models.size(); };
		for (; N < Fits.size() && Fits[N].first <= Fits[0].first + Spread; ++N) Sum += Models[Of(Fits[N].second)].Weight;
		double At = std::clamp(Seed, 0.0, 0.999999) * Sum;
		int Chosen = Fits[0].second;
		for (size_t K = 0; K < N; ++K)
		{
			const double W = Models[Of(Fits[K].second)].Weight;
			if (At < W) { Chosen = Fits[K].second; break; }
			At -= W;
		}
		Whole = size_t(Chosen) >= Models.size();
		const ChurchModel& M = Models[Of(Chosen)];
		Along = Long / (Whole ? M.MaxX - M.MinX : M.NaveL);
		Across = Short / (Whole ? M.MaxZ - M.MinZ : M.NaveW);
		return int(Of(Chosen));
	}
	return -1;
}

bool SetChurchKit(const std::string& JsonText, const std::string& Bin)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(JsonText, Root) || !Root.IsObject()) return false;
	auto Kit = std::make_shared<ChurchKit>();
	if (const TorqueFit::Json* L = Root.Get("looks"))
		for (const auto& KV : L->O) Kit->Looks.push_back({KV.first, Words(&KV.second)});
	Kit->TownShare = Root.Num("town_share", Kit->TownShare);
	Kit->Spread = Root.Num("pick", Kit->Spread);
	if (const TorqueFit::Json* S = Root.Get("stretch"); S && S->IsArray() && S->A.size() == 2) { Kit->StretchLo = S->A[0].N; Kit->StretchHi = S->A[1].N; }
	if (const TorqueFit::Json* S = Root.Get("area"); S && S->IsArray() && S->A.size() == 2) { Kit->AreaLo = S->A[0].N; Kit->AreaHi = S->A[1].N; }
	Kit->ChapelNames = Words(Root.Get("chapel_names"));
	Kit->SkipNames = Words(Root.Get("skip_names"));
	// (the triangles: "TSCK", version 1, then each model)
	Bytes R{Bin};
	if (Bin.size() < 12 || Bin.compare(0, 4, "TSCK") != 0) return false;
	R.At = 4;
	if (R.Get<uint32_t>() != 1) return false;
	const uint32_t Count = R.Get<uint32_t>();
	for (uint32_t I = 0; I < Count && !R.Bad; ++I)
	{
		ChurchModel M;
		const uint16_t NameLen = R.Get<uint16_t>();
		if (!R.Has(NameLen)) break;
		M.Name.assign(Bin.data() + R.At, NameLen);
		R.At += NameLen;
		float F[7];
		for (float& V : F) V = R.Get<float>();
		M.NaveL = F[0]; M.NaveW = F[1]; M.MinX = F[2]; M.MaxX = F[3]; M.MinZ = F[4]; M.MaxZ = F[5]; M.Top = F[6];
		const uint16_t Parts = R.Get<uint16_t>();
		for (uint16_t P = 0; P < Parts; ++P)
		{
			ChurchModel::Part Pt;
			Pt.Section = R.Get<uint8_t>(); Pt.A = R.Get<uint8_t>(); Pt.B = R.Get<uint8_t>(); Pt.C = R.Get<uint8_t>(); Pt.Code = R.Get<uint8_t>();
			M.Parts.push_back(Pt);
		}
		const uint32_t Verts = R.Get<uint32_t>(), Tris = R.Get<uint32_t>();
		if (Verts > 65535 || R.Bad) { R.Bad = true; break; }
		M.Pos.resize(size_t(Verts) * 3);
		M.Nrm.resize(size_t(Verts) * 3);
		M.PartOf.resize(Verts);
		if (!R.Has(size_t(Verts) * 10)) break;
		for (uint32_t V = 0; V < Verts; ++V)
		{
			for (int K = 0; K < 3; ++K) M.Pos[size_t(V) * 3 + size_t(K)] = R.Get<int16_t>();
			for (int K = 0; K < 3; ++K) M.Nrm[size_t(V) * 3 + size_t(K)] = R.Get<int8_t>();
			M.PartOf[V] = R.Get<uint8_t>();
		}
		R.Many(M.Idx, size_t(Tris) * 3);
		for (uint8_t& P : M.PartOf) P = std::min<uint8_t>(P, uint8_t(M.Parts.empty() ? 0 : M.Parts.size() - 1));
		for (uint16_t& Ix : M.Idx) if (Ix >= Verts) Ix = 0;
		Kit->Models.push_back(std::move(M));
	}
	if (R.Bad || Kit->Models.size() != Count) return false;
	// (the rules' rows name the models: their kind, region and weight)
	if (const TorqueFit::Json* Rows = Root.Get("churches"); Rows && Rows->IsArray())
		for (const TorqueFit::Json& Row : Rows->A)
			for (ChurchModel& M : Kit->Models)
				if (M.Name == Row.Str("name"))
				{
					M.Label = Row.Str("label");
					M.Kind = Row.Str("kind", "church");
					M.Region = Row.Str("region");
					M.Weight = std::max(0.0, Row.Num("weight", 1.0));
				}
	Kit->Models.erase(std::remove_if(Kit->Models.begin(), Kit->Models.end(), [](const ChurchModel& M) { return M.Kind.empty() || M.Weight <= 0.0; }), Kit->Models.end());
	std::lock_guard<std::mutex> Hold(ChurchKitLock);
	ChurchKitTable() = std::move(Kit);
	return true;
}

std::shared_ptr<const ChurchKit> ChurchKitNow()
{
	std::lock_guard<std::mutex> Hold(ChurchKitLock);
	return ChurchKitTable();
}

void BuildChurch(const ChurchModel& M, bool Whole, double X, double Y, double Z, double Low, double Dx, double Dz, double Along, double Across, double Up, bool Sooted,
	RideMesh& Out, RideMesh& Washed, RideMesh& Houses, RideMesh& Roofs, RideMesh& Bricks)
{
	using namespace RideSceneBits;
	RideMesh* Into[5] = {&Out, &Houses, &Roofs, &Bricks, &Washed};
	// (a mesh's alpha is all or nothing: the vertices before ours keep the 1 they would have had)
	for (RideMesh* Mi : Into)
		if (Mi->Alpha.size() < Mi->Vertices()) Mi->Alpha.resize(Mi->Vertices(), 1.0f);
	std::vector<uint32_t> Where(M.Vertices());   // (where each of the model's vertices went)
	// (model +x to (Dx, Dz), its +z, south, to (-Dz, Dx): a turn, no mirror)
	const double Cx = -Dz, Cz = Dx;
	// (fitted whole: its footprint's middle, not its nave's, on the building's)
	const double Ox = Whole ? -(M.MinX + M.MaxX) * 0.5 : 0.0, Oz = Whole ? -(M.MinZ + M.MaxZ) * 0.5 : 0.0;
	for (size_t V = 0; V < M.Vertices(); ++V)
	{
		const ChurchModel::Part& P = M.Parts[M.PartOf[V]];
		const int S = P.Section < 5 ? P.Section : 0;
		RideMesh& Mi = *Into[S];
		const double Lx = (M.Pos[V * 3] * 0.01 + Ox) * Along, Ly = M.Pos[V * 3 + 1] * 0.01 * Up, Lz = (M.Pos[V * 3 + 2] * 0.01 + Oz) * Across;
		const double Wx = X + Lx * Dx + Lz * Cx, Wy = Y + Ly, Wz = Z + Lx * Dz + Lz * Cz;
		// (the normal under the stretch: over each way's stretch, then turned)
		const double Nx0 = M.Nrm[V * 3] / Along, Ny0 = M.Nrm[V * 3 + 1] / Up, Nz0 = M.Nrm[V * 3 + 2] / Across;
		double Nx = Nx0 * Dx + Nz0 * Cx, Ny = Ny0, Nz = Nx0 * Dz + Nz0 * Cz;
		const double Nl = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
		if (Nl > 1e-9) { Nx /= Nl; Ny /= Nl; Nz /= Nl; } else { Nx = 0.0; Ny = 1.0; Nz = 0.0; }
		Where[V] = uint32_t(Mi.Vertices());
		const double Px = Wx - Mi.OriginX, Pz = Wz - Mi.OriginZ;
		Mi.Pos.push_back(float(Px)); Mi.Pos.push_back(float(Wy)); Mi.Pos.push_back(float(Pz));
		Mi.Nrm.push_back(float(Nx)); Mi.Nrm.push_back(float(Ny)); Mi.Nrm.push_back(float(Nz));
		// (UVs metres of surface, as the houses': a roof's along its eaves and up its slope; a wall's along it and down)
		const double Hl = std::sqrt(Nx * Nx + Nz * Nz);
		const double Tx = Hl > 1e-6 ? -Nz / Hl : 1.0, Tz = Hl > 1e-6 ? Nx / Hl : 0.0;
		double U, W;
		if (S == ChurchKit::Roofing)
		{
			double Sx = Ny * Tz, Sy = Nz * Tx - Nx * Tz, Sz = -Ny * Tx;   // (N x T)
			if (Sy < 0.0) { Sx = -Sx; Sy = -Sy; Sz = -Sz; }
			U = Px * Tx + Pz * Tz;
			W = -(Px * Sx + Wy * Sy + Pz * Sz);
		}
		else if (std::fabs(Ny) > 0.7) { U = Px; W = Pz; }
		else { U = Px * Tx + Pz * Tz; W = -Wy; }
		Mi.Uv.push_back(float(U)); Mi.Uv.push_back(float(W));
		float R = P.A / 255.0f, G = P.B / 255.0f, B = P.C / 255.0f;
		if (S == ChurchKit::Masonry && Sooted) R *= 0.5f;
		Mi.Col.push_back(R); Mi.Col.push_back(G); Mi.Col.push_back(B);
		Mi.Alpha.push_back(P.Code / 255.0f);
	}
	// (on a slope, the plinth it stands on: dressed stone round its whole footprint, half a metre out, down to its lowest ground)
	if (Y - Low > 0.4)
	{
		const double Ax0 = (M.MinX + Ox - 0.5) * Along, Ax1 = (M.MaxX + Ox + 0.5) * Along, Az0 = (M.MinZ + Oz - 0.5) * Across, Az1 = (M.MaxZ + Oz + 0.5) * Across;
		const auto Corner = [&](double A, double C, double H) { return At3(X + A * Dx + C * Cx, H, Z + A * Dz + C * Cz); };
		const P3 Mid = At3(X, Y - 0.3, Z);
		const double Top = Y - 0.35;
		Rgb Mask;
		Mask.R = Sooted ? 0.24f : 0.44f;
		Mask.G = 0.8f;
		Mask.B = 0.3f;
		const double Us[5] = {Ax0, Ax1, Ax1, Ax0, Ax0}, Cs[5] = {Az0, Az0, Az1, Az1, Az0};
		for (int K = 0; K < 4; ++K)
		{
			const P3 Q0 = Corner(Us[K], Cs[K], Low - 0.4), Q1 = Corner(Us[K + 1], Cs[K + 1], Low - 0.4), Q2 = Corner(Us[K + 1], Cs[K + 1], Top), Q3 = Corner(Us[K], Cs[K], Top);
			const P3 Away = At3(Mid.X + ((Q0.X + Q1.X) * 0.5 - Mid.X) * 50.0, Mid.Y, Mid.Z + ((Q0.Z + Q1.Z) * 0.5 - Mid.Z) * 50.0);
			Quad(Houses, Q0, Q1, Q2, Q3, Away, Mask, false, 0.06f);
		}
		Quad(Houses, Corner(Ax0, Az0, Top), Corner(Ax1, Az0, Top), Corner(Ax1, Az1, Top), Corner(Ax0, Az1, Top), At3(X, Y + 100.0, Z), Mask, false, 0.06f);
	}
	for (size_t T = 0; T + 2 < M.Idx.size(); T += 3)
	{
		const uint16_t A = M.Idx[T], B = M.Idx[T + 1], C = M.Idx[T + 2];
		const int S = M.Parts[M.PartOf[A]].Section < 5 ? M.Parts[M.PartOf[A]].Section : 0;
		// (a triangle's corners all of one part, so of one mesh)
		Into[S]->Idx.insert(Into[S]->Idx.end(), {Where[A], Where[B], Where[C]});
	}
}
} // namespace TorqueWorld
