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
#include <functional>
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

std::string ChurchKit::NameFaith(const std::string& Name) const
{
	if (Name.empty()) return std::string();
	const std::string L = " " + Lower(Name) + " ";
	for (const auto& F : FaithNames)
		for (const std::string& W : F.second)
			if (!W.empty() && L.find(W) != std::string::npos) return F.first;
	return std::string();
}

int ChurchKit::Pick(const char* LookName, bool InTown, bool Chapel, double Long, double Short, double Seed, double Seed2, double& Along, double& Across, bool& Whole,
	const std::vector<std::string>& Wants) const
{
	if (Models.empty() || Long <= 0.0 || Short <= 0.0) return -1;
	const auto Tagged = [](const ChurchModel& M, const std::string& T) { return std::find(M.Tags.begin(), M.Tags.end(), T) != M.Tags.end(); };
	const bool Meeting = !Wants.empty() && Wants[0] == "meeting";
	const bool WantCross = std::find(Wants.begin(), Wants.end(), "cross") != Wants.end();
	// (those with every tag wanted, then with the first: the region's or the town's (a meeting house from
	// anywhere); else any as ever)
	const auto With = [&](size_t Need)
	{
		return PickAmong(LookName, InTown, Chapel, Long, Short, Seed, Seed2, Along, Across, Whole, [&](const ChurchModel& M)
		{
			for (size_t K = 0; K < Need; ++K)
				if (!Tagged(M, Wants[K])) return false;
			if (!WantCross && Tagged(M, "cross")) return false;
			return Meeting ? M.Kind == "meeting" : M.Kind != "meeting";
		}, Meeting ? 4 : 3);
	};
	if (Wants.size() > 1)
		if (const int Got = With(Wants.size()); Got >= 0) return Got;
	if (!Wants.empty())
		if (const int Got = With(1); Got >= 0) return Got;
	// (never transepts where the outline has none, nor a meeting house unasked)
	return PickAmong(LookName, InTown, Chapel, Long, Short, Seed, Seed2, Along, Across, Whole, [&](const ChurchModel& M) { return M.Kind != "meeting" && !Tagged(M, "cross"); });
}

int ChurchKit::PickAmong(const char* LookName, bool InTown, bool Chapel, double Long, double Short, double Seed, double Seed2, double& Along, double& Across, bool& Whole,
	const std::function<bool(const ChurchModel&)>& Allowed, int Passes) const
{
	const std::string Look = LookName ? LookName : "";
	const auto RegionHas = [this, &Look](const std::string& Region)
	{
		for (const auto& R : Looks)
			if (R.first == Region) return std::find(R.second.begin(), R.second.end(), Look) != R.second.end();
		return false;
	};
	const bool TownOnes = InTown && Seed2 < TownShare;
	const auto KindFits = [Chapel](const ChurchModel& M) { return M.Kind == "meeting" || (Chapel ? (M.Kind == "chapel" || M.Kind == "tin") : M.Kind == "church"); };
	// (the region's own, or in a town now and then the town ones; then the region's stretched further; then
	// the region's and the town's; where none of those fits, any)
	for (int Pass = 0; Pass < Passes; ++Pass)
	{
		const double Lo = Pass == 1 || Pass == 2 ? StretchLo * 0.85 : StretchLo, Hi = Pass == 1 || Pass == 2 ? StretchHi * 1.2 : StretchHi;
		std::vector<std::pair<double, int>> Fits;   // (cost, model; a whole footprint's fit as the model's index + Models.size())
		for (size_t I = 0; I < Models.size(); ++I)
		{
			const ChurchModel& M = Models[I];
			if (!KindFits(M) || !Allowed(M) || M.NaveL <= 0.0 || M.NaveW <= 0.0) continue;
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
	if (const TorqueFit::Json* F = Root.Get("faith_names"))
		for (const auto& KV : F->O) Kit->FaithNames.push_back({KV.first, Words(&KV.second)});
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
					M.Tags = Words(Row.Get("tags"));
					if (const TorqueFit::Json* D = Row.Get("door"); D && D->IsArray() && D->A.size() == 4)
					{
						M.HasDoor = true;
						for (size_t K = 0; K < 4; ++K) M.Door[K] = D->A[K].N;
					}
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
// [churches stage 5] The churchyards on a square: see WorldRide.h (Churchyard).
void RidePlan::ChurchyardsTile(double X0, double Z0, double Size, const LocalLook& Look, const std::vector<ChurchDoor>& Doors, RideMesh& Out, RideMesh& Washed,
	RideMesh& Houses, RideMesh& RoofOut, RideMesh& BrickOut) const
{
	using namespace RideSceneBits;
	if (Churchyards.empty()) return;
	const std::shared_ptr<const ChurchKit> Kit = ChurchKitNow();
	const auto Inside = [X0, Z0, Size](double X, double Z) { return X >= X0 && X < X0 + Size && Z >= Z0 && Z < Z0 + Size; };
	const auto ModelNamed = [&Kit](const char* Name) -> const ChurchModel*
	{
		if (!Kit) return nullptr;
		for (const ChurchModel& M : Kit->Models)
			if (M.Name == Name) return &M;
		return nullptr;
	};
	const ChurchModel* Lych = ModelNamed(Look.TileShare > 0.4f ? "SM_Church_yard_lychgate_tile" : "SM_Church_yard_lychgate_slate");
	const ChurchModel* OldYew = ModelNamed("SM_Church_yard_yew");
	const ChurchModel* IrishYew = ModelNamed("SM_Church_yard_yew_irish");
	const auto Hash = [](double X, double Z, uint32_t Salt)
	{
		uint32_t V = static_cast<uint32_t>(std::lround(X * 4.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Z * 4.0)) * 19349663u ^ (Salt * 2654435761u);
		V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
		return double(V & 0xffffff) / double(0x1000000);
	};
	static const Rgb Coping = {0.33f, 0.31f, 0.27f}, Gravel = {0.24f, 0.225f, 0.19f};
	// (the masonry's alpha, moss, as the garden walls': every vertex or none)
	const auto Stone = [&Houses](const std::function<void()>& Draw)
	{
		const size_t From = Houses.Vertices();
		Draw();
		if (!Houses.Alpha.empty() && Houses.Alpha.size() < Houses.Vertices())
		{
			Houses.Alpha.resize(From, 1.0f);
			Houses.Alpha.resize(Houses.Vertices(), 0.06f);
		}
	};
	// (a stretch of path, Half wide, from A to B a metre at a time on the ground)
	const auto PathRun = [&](const P2& A, const P2& B, double Half)
	{
		const double L = A.DistanceTo(B);
		if (L < 0.05) return;
		const P2 U = (B - A) * (1.0 / L), S(U.Z, -U.X);
		const int Steps = std::max(1, int(std::ceil(L)));
		for (int K = 0; K < Steps; ++K)
		{
			const P2 Pa = A + U * (L * K / Steps), Pb = A + U * (L * (K + 1) / Steps);
			const P2 Pm = (Pa + Pb) * 0.5;
			if (!Inside(Pm.X, Pm.Z)) continue;
			// (each corner on its own ground, 5 cm up, so it follows the ground's slope without steps)
			const P2 Ea = Pa - U * 0.05, Eb = Pb + U * 0.05;   // (a little overlap, so no seam shows)
			const P2 Q[4] = {Ea + S * Half, Eb + S * Half, Eb - S * Half, Ea - S * Half};
			double H[4];
			bool Bad = false;
			for (int Ci = 0; Ci < 4; ++Ci)
			{
				H[Ci] = Ground(Q[Ci].X, Q[Ci].Z);
				Bad = Bad || std::isnan(H[Ci]);
			}
			if (Bad) continue;
			const Rgb C = Shade(Gravel, 0.9 + 0.2 * Hash(Pm.X, Pm.Z, 61u));
			Quad(Out, At3(Q[0].X, H[0] + 0.05, Q[0].Z), At3(Q[1].X, H[1] + 0.05, Q[1].Z), At3(Q[2].X, H[2] + 0.05, Q[2].Z), At3(Q[3].X, H[3] + 0.05, Q[3].Z),
				At3(Pm.X, (H[0] + H[2]) * 0.5 + 100.0, Pm.Z), C);
		}
	};
	for (const Churchyard& Y : Churchyards)
	{
		if (Y.MaxX < X0 - 10.0 || Y.MinX > X0 + Size + 10.0 || Y.MaxZ < Z0 - 10.0 || Y.MinZ > Z0 + Size + 10.0) continue;
		const bool Sooted = Hash(Y.Church.X, Y.Church.Z, 94u) < Look.Soot;
		Rgb Mask;
		Mask.R = float((Sooted ? 0.22 : 0.44) + 0.12 * Hash(Y.Church.X, Y.Church.Z, 62u));
		Mask.G = 0.8f;
		Mask.B = 0.3f;
		const double WallH = 1.05 + 0.3 * Hash(Y.Church.X, Y.Church.Z, 63u);
		// (its wall, in pieces of 3 m at most stepping with the ground, coped)
		for (const auto& Run : Y.Walls)
		{
			const double L = Run.first.DistanceTo(Run.second);
			const int Steps = std::max(1, int(std::ceil(L / 3.0)));
			const P2 U = (Run.second - Run.first) * (1.0 / std::fmax(L, 1e-6));
			for (int K = 0; K < Steps; ++K)
			{
				const P2 Pa = Run.first + U * (L * K / Steps), Pb = Run.first + U * (L * (K + 1) / Steps), Pm = (Pa + Pb) * 0.5;
				if (!Inside(Pm.X, Pm.Z)) continue;
				const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
				if (std::isnan(Ga) || std::isnan(Gb)) continue;
				const double Foot = std::fmin(Ga, Gb) - 0.3, Head = std::fmax(Ga, Gb) + WallH;
				const double Half = Pa.DistanceTo(Pb) * 0.5 + 0.12;   // (a little past each end: the corners closed)
				Stone([&] { Box(Houses, Pm.X, Pm.Z, U, Half, 0.25, Foot, Head, Mask); });
				Box(Out, Pm.X, Pm.Z, U, Half + 0.02, 0.3, Head, Head + 0.12, Shade(Coping, 0.9 + 0.2 * Hash(Pm.X, Pm.Z, 64u)));
			}
		}
		// (its gate: a lychgate over it, else two stone piers)
		const P2 Side(Y.Out.Z, -Y.Out.X);
		if (Inside(Y.Gate.X, Y.Gate.Z))
		{
			const double G = Ground(Y.Gate.X, Y.Gate.Z);
			if (!std::isnan(G))
			{
				if (Y.Lych && Lych)
				{
					double Low = G;
					for (double Su : {-1.6, 1.6})
						for (double Sv : {-1.8, 1.8})
						{
							const double Gc = Ground(Y.Gate.X + Y.Out.X * Su + Side.X * Sv, Y.Gate.Z + Y.Out.Z * Su + Side.Z * Sv);
							if (!std::isnan(Gc)) Low = std::fmin(Low, Gc);
						}
					BuildChurch(*Lych, true, Y.Gate.X, G, Y.Gate.Z, Low, Y.Out.X, Y.Out.Z, 1.0, 1.0, 1.0, Sooted, Out, Washed, Houses, RoofOut, BrickOut);
				}
				else
					for (double Way : {-1.0, 1.0})
					{
						const P2 P = Y.Gate + Side * (Way * 1.75);
						Stone([&] { Box(Houses, P.X, P.Z, Y.Out, 0.3, 0.3, G - 0.3, G + WallH + 0.45, Mask); });
						Box(Out, P.X, P.Z, Y.Out, 0.36, 0.36, G + WallH + 0.45, G + WallH + 0.6, Coping);
					}
			}
		}
		// (the path: from the road's side of the gate to the church, then round it to its door)
		PathRun(Y.Gate + Y.Out * 1.5, Y.PathEnd, 0.75);
		const ChurchDoor* Door = nullptr;
		for (const ChurchDoor& D : Doors)
			if (D.Middle.DistanceTo(Y.Church) < 25.0 && (!Door || D.Middle.DistanceTo(Y.Church) < Door->Middle.DistanceTo(Y.Church))) Door = &D;
		if (Door)
		{
			// (along the walk, 1.5 m out of the church as built, the shorter way round from the path's end to
			// the point by the door, then out to the door)
			const double Hu = Door->HalfA + 1.5, Hv = Door->HalfC + 1.5, Cs = Door->Along.X, Sn = -Door->Along.Z;
			const P2 Mid = Door->Box;
			const auto LocalOf = [&](const P2& P) { const double Qx = P.X - Mid.X, Qz = P.Z - Mid.Z; return P2(Qx * Cs - Qz * Sn, Qx * Sn + Qz * Cs); };
			const auto WorldOf = [&](double U, double V) { return P2(Mid.X + U * Cs + V * Sn, Mid.Z - U * Sn + V * Cs); };
			// (a point's way round the walk's ring, 0 to its length, from the -u, -v corner along +u)
			const double Ring = 4.0 * (Hu + Hv);
			const auto RoundOf = [&](const P2& L)
			{
				const double U = std::clamp(L.X, -Hu, Hu), V = std::clamp(L.Z, -Hv, Hv);
				const double Du = Hu - std::fabs(U), Dv = Hv - std::fabs(V);
				if (Dv <= Du) return V < 0.0 ? U + Hu : 2.0 * Hu + 2.0 * Hv + (Hu - U);
				return U > 0.0 ? 2.0 * Hu + (V + Hv) : 4.0 * Hu + 2.0 * Hv + (Hv - V);
			};
			const auto PointOf = [&](double T)
			{
				T = std::fmod(std::fmod(T, Ring) + Ring, Ring);
				if (T < 2.0 * Hu) return WorldOf(-Hu + T, -Hv);
				T -= 2.0 * Hu;
				if (T < 2.0 * Hv) return WorldOf(Hu, -Hv + T);
				T -= 2.0 * Hv;
				if (T < 2.0 * Hu) return WorldOf(Hu - T, Hv);
				T -= 2.0 * Hu;
				return WorldOf(-Hu, Hv - T);
			};
			const double Ta = RoundOf(LocalOf(Y.PathEnd)), Tb = RoundOf(LocalOf(Door->Door));
			double Span = Tb - Ta;
			if (Span > Ring * 0.5) Span -= Ring;
			if (Span < -Ring * 0.5) Span += Ring;
			// (its corners on the way, each a stretch to itself)
			std::vector<double> Bends{Ta};
			const double Corners[4] = {0.0, 2.0 * Hu, 2.0 * Hu + 2.0 * Hv, 4.0 * Hu + 2.0 * Hv};
			std::vector<double> Between;
			for (double C : Corners)
				for (double Lap : {-Ring, 0.0, Ring})
				{
					const double At = C + Lap;
					if ((Span > 0.0 && At > Ta && At < Ta + Span) || (Span < 0.0 && At < Ta && At > Ta + Span)) Between.push_back(At);
				}
			std::sort(Between.begin(), Between.end(), [Span](double A, double B) { return Span > 0.0 ? A < B : A > B; });
			Bends.insert(Bends.end(), Between.begin(), Between.end());
			Bends.push_back(Ta + Span);
			PathRun(Y.PathEnd, PointOf(Ta), 0.6);
			for (size_t K = 0; K + 1 < Bends.size(); ++K) PathRun(PointOf(Bends[K]), PointOf(Bends[K + 1]), 0.6);
			PathRun(PointOf(Ta + Span), Door->Door, 0.6);
		}
		// (its yews, each its own size and turn)
		for (const auto& Yw : Y.Yews)
		{
			if (!Inside(Yw.first.X, Yw.first.Z)) continue;
			const ChurchModel* M = Yw.second == 0 ? OldYew : IrishYew;
			const double G = Ground(Yw.first.X, Yw.first.Z);
			if (!M || std::isnan(G)) continue;
			const double Turn = Hash(Yw.first.X, Yw.first.Z, 65u) * 6.283185307179586, Big = (Yw.second == 0 ? 0.65 : 0.85) + 0.3 * Hash(Yw.first.X, Yw.first.Z, 66u);
			BuildChurch(*M, true, Yw.first.X, G - 0.1, Yw.first.Z, G - 0.1, std::cos(Turn), std::sin(Turn), Big, Big, Big * (Yw.second == 0 ? 1.0 : 1.1), false, Out, Washed, Houses, RoofOut, BrickOut);
		}
	}
	// (every mesh's alpha whole: a church built before these took some)
	for (RideMesh* Mi : {&Out, &Washed, &RoofOut, &BrickOut})
		if (!Mi->Alpha.empty() && Mi->Alpha.size() < Mi->Vertices()) Mi->Alpha.resize(Mi->Vertices(), 1.0f);
	if (!Houses.Alpha.empty() && Houses.Alpha.size() < Houses.Vertices()) Houses.Alpha.resize(Houses.Vertices(), 0.06f);
}

} // namespace TorqueWorld
