// [places stage 6] The famous places' own models (the places plan's level 3): Data/RideUK/Sites/<id>.glb,
// made by Tools/make_ride_sites.py and read once at start (TorqueWorldSubsystem), each a few parts in the
// ride's own materials (the scanned stone and masonry, limewash, roofs, brick, the night's lights), so a
// model sits in the world as the recipes and the buildings round it do. PlacesTile (WorldRidePlaces.cpp)
// draws a place's model where its site note names one, in place of its kind's recipe.
#include "WorldRide.h"

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
std::mutex SiteModelLock;
std::vector<std::shared_ptr<const SiteModel>>& SiteModelList()
{
	static std::vector<std::shared_ptr<const SiteModel>> List;
	return List;
}

uint32_t GlbWord(const std::string& B, size_t At)
{
	uint32_t V = 0;
	std::memcpy(&V, B.data() + At, 4);   // (glb is little endian, as every machine the game runs on)
	return V;
}

// One accessor's numbers as floats (Width each: 1 scalar, 2 VEC2, 3 VEC3, 4 VEC4), normalised integers made 0..1.
bool ReadGlbAccessor(const TorqueFit::Json& Root, const std::string& Bin, int Index, int Width, std::vector<float>& Out, std::string* Why)
{
	Out.clear();
	const TorqueFit::Json* Accs = Root.Get("accessors");
	const TorqueFit::Json* Views = Root.Get("bufferViews");
	if (Accs == nullptr || Views == nullptr || !Accs->IsArray() || !Views->IsArray() || Index < 0 || size_t(Index) >= Accs->A.size())
	{
		if (Why) *Why = "no accessor " + std::to_string(Index);
		return false;
	}
	const TorqueFit::Json& A = Accs->A[size_t(Index)];
	const int ViewIndex = int(A.Num("bufferView", -1.0));
	if (ViewIndex < 0 || size_t(ViewIndex) >= Views->A.size())
	{
		if (Why) *Why = "accessor " + std::to_string(Index) + " has no buffer view";
		return false;
	}
	const TorqueFit::Json& V = Views->A[size_t(ViewIndex)];
	const int Comp = int(A.Num("componentType"));
	const size_t Count = size_t(A.Num("count"));
	const std::string Type = A.Str("type");
	const int Have = Type == "SCALAR" ? 1 : Type == "VEC2" ? 2 : Type == "VEC3" ? 3 : Type == "VEC4" ? 4 : 0;
	const size_t Size = Comp == 5126 || Comp == 5125 ? 4 : Comp == 5123 || Comp == 5122 ? 2 : Comp == 5121 || Comp == 5120 ? 1 : 0;
	if (Have == 0 || Size == 0 || Have < std::min(Width, 3))
	{
		if (Why) *Why = "accessor " + std::to_string(Index) + " is " + Type + " of " + std::to_string(Comp);
		return false;
	}
	const size_t Stride = V.Num("byteStride") > 0.0 ? size_t(V.Num("byteStride")) : Size * size_t(Have);
	const size_t Start = size_t(V.Num("byteOffset")) + size_t(A.Num("byteOffset"));
	if (Count == 0 || Start + Stride * (Count - 1) + Size * size_t(Have) > Bin.size())
	{
		if (Why) *Why = "accessor " + std::to_string(Index) + " runs past the buffer";
		return Count == 0;
	}
	const bool Norm = A.Flag("normalized");
	Out.resize(Count * size_t(Width), 1.0f);
	for (size_t I = 0; I < Count; ++I)
	{
		const char* Row = Bin.data() + Start + Stride * I;
		for (int K = 0; K < std::min(Width, Have); ++K)
		{
			float F = 0.0f;
			switch (Comp)
			{
			case 5126: std::memcpy(&F, Row + 4 * K, 4); break;
			case 5125: { uint32_t U = 0; std::memcpy(&U, Row + 4 * K, 4); F = float(U); break; }
			case 5123: { uint16_t U = 0; std::memcpy(&U, Row + 2 * K, 2); F = Norm ? float(U) / 65535.0f : float(U); break; }
			case 5122: { int16_t U = 0; std::memcpy(&U, Row + 2 * K, 2); F = Norm ? std::max(float(U) / 32767.0f, -1.0f) : float(U); break; }
			case 5121: { const uint8_t U = static_cast<uint8_t>(Row[K]); F = Norm ? float(U) / 255.0f : float(U); break; }
			default: { const int8_t U = static_cast<int8_t>(Row[K]); F = Norm ? std::max(float(U) / 127.0f, -1.0f) : float(U); break; }
			}
			Out[I * size_t(Width) + size_t(K)] = F;
		}
	}
	return true;
}

int SiteLayerOf(const std::string& Material)
{
	std::string M = Material;
	for (char& C : M) C = char(std::tolower(static_cast<unsigned char>(C)));
	const auto Is = [&M](const char* Prefix) { return M.rfind(Prefix, 0) == 0; };
	if (Is("stone")) return SiteModel::Stone;
	if (Is("masonry")) return SiteModel::Masonry;
	if (Is("render")) return SiteModel::Render;
	if (Is("roof")) return SiteModel::Roofing;
	if (Is("brick")) return SiteModel::Brickwork;
	if (Is("glow")) return SiteModel::Glow;
	return SiteModel::Painted;
}
} // namespace

size_t SiteModel::Triangles() const
{
	size_t N = 0;
	for (const Part& P : Parts) N += P.Idx.size() / 3;
	return N;
}

bool ReadSiteModel(const std::string& Bytes, SiteModel& Out, std::string* Why)
{
	Out.Parts.clear();
	Out.Reach = Out.Tall = 0.0;
	if (Bytes.size() < 20 || GlbWord(Bytes, 0) != 0x46546C67u || GlbWord(Bytes, 4) != 2u)
	{
		if (Why) *Why = "not a glb (glTF 2 binary)";
		return false;
	}
	const size_t JsonLen = GlbWord(Bytes, 12);
	if (GlbWord(Bytes, 16) != 0x4E4F534Au || 20 + JsonLen > Bytes.size())
	{
		if (Why) *Why = "no JSON chunk";
		return false;
	}
	TorqueFit::Json Root;
	std::string Error;
	if (!TorqueFit::Json::Parse(Bytes.substr(20, JsonLen), Root, &Error) || !Root.IsObject())
	{
		if (Why) *Why = "bad JSON: " + Error;
		return false;
	}
	std::string Bin;
	const size_t BinAt = 20 + JsonLen;
	if (BinAt + 8 <= Bytes.size() && GlbWord(Bytes, BinAt + 4) == 0x004E4942u)
	{
		const size_t BinLen = GlbWord(Bytes, BinAt);
		if (BinAt + 8 + BinLen > Bytes.size())
		{
			if (Why) *Why = "BIN chunk runs past the file";
			return false;
		}
		Bin = Bytes.substr(BinAt + 8, BinLen);
	}
	const TorqueFit::Json* Meshes = Root.Get("meshes");
	const TorqueFit::Json* Mats = Root.Get("materials");
	const TorqueFit::Json* Nodes = Root.Get("nodes");
	if (Meshes == nullptr || !Meshes->IsArray())
	{
		if (Why) *Why = "no meshes";
		return false;
	}
	// (the nodes that hold a mesh, by name; with no nodes, each mesh by its own name)
	std::vector<std::pair<int, std::string>> Use;
	if (Nodes != nullptr && Nodes->IsArray())
		for (const TorqueFit::Json& N : Nodes->A)
			if (N.Get("mesh") != nullptr) Use.push_back({int(N.Num("mesh")), N.Str("name")});
	if (Use.empty())
		for (size_t M = 0; M < Meshes->A.size(); ++M) Use.push_back({int(M), Meshes->A[M].Str("name")});
	for (const auto& U : Use)
	{
		if (U.first < 0 || size_t(U.first) >= Meshes->A.size()) continue;
		const TorqueFit::Json* Prims = Meshes->A[size_t(U.first)].Get("primitives");
		if (Prims == nullptr || !Prims->IsArray()) continue;
		const bool Drop = U.second.find("#drop") != std::string::npos, Far = U.second.find("#far") != std::string::npos;
		for (const TorqueFit::Json& Pr : Prims->A)
		{
			if (int(Pr.Num("mode", 4.0)) != 4) continue;   // (triangles only)
			const TorqueFit::Json* Attrs = Pr.Get("attributes");
			if (Attrs == nullptr || Attrs->Get("POSITION") == nullptr) continue;
			SiteModel::Part P;
			P.Drop = Drop;
			P.Far = Far;
			const int Mat = int(Pr.Num("material", -1.0));
			if (Mats != nullptr && Mats->IsArray() && Mat >= 0 && size_t(Mat) < Mats->A.size()) P.Layer = SiteLayerOf(Mats->A[size_t(Mat)].Str("name"));
			if (!ReadGlbAccessor(Root, Bin, int(Attrs->Num("POSITION")), 3, P.Pos, Why)) return false;
			const size_t Verts = P.Pos.size() / 3;
			if (Attrs->Get("NORMAL") != nullptr && !ReadGlbAccessor(Root, Bin, int(Attrs->Num("NORMAL")), 3, P.Nrm, Why)) return false;
			if (Attrs->Get("TEXCOORD_0") != nullptr && !ReadGlbAccessor(Root, Bin, int(Attrs->Num("TEXCOORD_0")), 2, P.Uv, Why)) return false;
			std::vector<float> Rgba;
			if (Attrs->Get("COLOR_0") != nullptr && !ReadGlbAccessor(Root, Bin, int(Attrs->Num("COLOR_0")), 4, Rgba, Why)) return false;
			std::vector<float> Tris;
			if (Pr.Get("indices") != nullptr)
			{
				if (!ReadGlbAccessor(Root, Bin, int(Pr.Num("indices")), 1, Tris, Why)) return false;
			}
			else
				for (size_t I = 0; I < Verts; ++I) Tris.push_back(float(I));
			if (P.Nrm.size() != Verts * 3) P.Nrm.assign(Verts * 3, 0.0f);
			if (P.Uv.size() != Verts * 2) P.Uv.assign(Verts * 2, 0.0f);
			P.Col.assign(Verts * 3, 1.0f);
			if (Rgba.size() == Verts * 4)
			{
				P.Alpha.resize(Verts);
				for (size_t I = 0; I < Verts; ++I)
				{
					for (int K = 0; K < 3; ++K) P.Col[I * 3 + size_t(K)] = Rgba[I * 4 + size_t(K)];
					P.Alpha[I] = Rgba[I * 4 + 3];
				}
			}
			// (glTF's front faces wind one way, the ride's the other: each triangle turned round)
			for (size_t T = 0; T + 2 < Tris.size(); T += 3)
			{
				const uint32_t A = uint32_t(Tris[T]), B = uint32_t(Tris[T + 1]), C = uint32_t(Tris[T + 2]);
				if (A >= Verts || B >= Verts || C >= Verts)
				{
					if (Why) *Why = "an index past the vertices in " + U.second;
					return false;
				}
				P.Idx.insert(P.Idx.end(), {A, C, B});
			}
			if (P.Idx.empty()) continue;
			// (its middle across from its box, its foot its lowest point)
			double X0 = 1e18, X1 = -1e18, Z0 = 1e18, Z1 = -1e18, Y0 = 1e18, Y1 = -1e18;
			for (size_t I = 0; I < Verts; ++I)
			{
				X0 = std::fmin(X0, P.Pos[I * 3]); X1 = std::fmax(X1, P.Pos[I * 3]);
				Y0 = std::fmin(Y0, P.Pos[I * 3 + 1]); Y1 = std::fmax(Y1, P.Pos[I * 3 + 1]);
				Z0 = std::fmin(Z0, P.Pos[I * 3 + 2]); Z1 = std::fmax(Z1, P.Pos[I * 3 + 2]);
				Out.Reach = std::fmax(Out.Reach, std::hypot(double(P.Pos[I * 3]), double(P.Pos[I * 3 + 2])));
			}
			P.MidX = 0.5 * (X0 + X1);
			P.MidZ = 0.5 * (Z0 + Z1);
			P.X0 = X0; P.X1 = X1; P.Z0 = Z0; P.Z1 = Z1;
			P.Foot = Y0;
			Out.Tall = std::fmax(Out.Tall, Y1);
			Out.Parts.push_back(std::move(P));
		}
	}
	if (Out.Parts.empty())
	{
		if (Why) *Why = "no triangles";
		return false;
	}
	return true;
}

void SetSiteModels(std::vector<std::shared_ptr<const SiteModel>> List)
{
	std::lock_guard<std::mutex> Hold(SiteModelLock);
	SiteModelList() = std::move(List);
}

std::shared_ptr<const SiteModel> SiteModelOf(const std::string& Id)
{
	if (Id.empty()) return nullptr;
	std::lock_guard<std::mutex> Hold(SiteModelLock);
	for (const std::shared_ptr<const SiteModel>& M : SiteModelList())
		if (M && M->Id == Id) return M;
	return nullptr;
}

void PutSitePart(RideMesh& Out, const SiteModel::Part& P, double X, double Y, double Z, double Turn)
{
	const double C = std::cos(Turn), S = std::sin(Turn);
	const size_t Verts = P.Pos.size() / 3, V0 = Out.Vertices();
	if (Verts == 0) return;
	// ("empty for 1 everywhere" made real before a part with its own alpha joins; and kept so after)
	// (a part all of whose alpha is 1 leaves a mesh with none as it is)
	bool AllOne = true;
	for (const float A : P.Alpha) AllOne = AllOne && A >= 0.999f;
	if (!AllOne && Out.Alpha.empty()) Out.Alpha.assign(V0, 1.0f);
	const bool Coloured = !Out.Col.empty() || V0 == 0;
	Out.Pos.reserve(Out.Pos.size() + Verts * 3);
	for (size_t I = 0; I < Verts; ++I)
	{
		const double Px = P.Pos[I * 3], Py = P.Pos[I * 3 + 1], Pz = P.Pos[I * 3 + 2];
		Out.Pos.push_back(float(X + Px * C - Pz * S - Out.OriginX));
		Out.Pos.push_back(float(Y + Py));
		Out.Pos.push_back(float(Z + Px * S + Pz * C - Out.OriginZ));
		const double Nx = P.Nrm[I * 3], Ny = P.Nrm[I * 3 + 1], Nz = P.Nrm[I * 3 + 2];
		Out.Nrm.push_back(float(Nx * C - Nz * S));
		Out.Nrm.push_back(float(Ny));
		Out.Nrm.push_back(float(Nx * S + Nz * C));
		Out.Uv.push_back(P.Uv[I * 2]);
		Out.Uv.push_back(P.Uv[I * 2 + 1]);
		if (Coloured)
		{
			Out.Col.push_back(P.Col[I * 3]);
			Out.Col.push_back(P.Col[I * 3 + 1]);
			Out.Col.push_back(P.Col[I * 3 + 2]);
		}
	}
	if (!Out.Alpha.empty())
	{
		if (P.Alpha.empty()) Out.Alpha.resize(Out.Vertices(), 1.0f);
		else Out.Alpha.insert(Out.Alpha.end(), P.Alpha.begin(), P.Alpha.end());
	}
	Out.Idx.reserve(Out.Idx.size() + P.Idx.size());
	for (const uint32_t I : P.Idx) Out.Idx.push_back(uint32_t(V0) + I);
}
} // namespace TorqueWorld
