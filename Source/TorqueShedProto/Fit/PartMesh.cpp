#include "PartMesh.h"

#include "FitJson.h"

#include <cstdint>
#include <cstring>

namespace TorqueFit
{
namespace
{
uint32_t U32(const std::string& B, size_t At)
{
	uint32_t V = 0;
	std::memcpy(&V, B.data() + At, 4);
	return V;
}

// One accessor of our glbs: float VEC2/VEC3 or uint16/uint32 SCALAR, tightly packed.
bool ReadAccessor(const Json& J, const std::string& Bin, size_t Index, std::vector<double>& Out, size_t& Width, std::string& Error)
{
	const Json* Accs = J.Get("accessors");
	const Json* Views = J.Get("bufferViews");
	if (!Accs || !Views || Index >= Accs->A.size()) { Error = "glb: accessor out of range"; return false; }
	const Json& A = Accs->A[Index];
	const size_t ViewIndex = static_cast<size_t>(A.Num("bufferView", -1.0));
	if (ViewIndex >= Views->A.size()) { Error = "glb: accessor without a buffer view"; return false; }
	const Json& View = Views->A[ViewIndex];
	const std::string Type = A.Str("type");
	Width = Type == "SCALAR" ? 1 : Type == "VEC2" ? 2 : Type == "VEC3" ? 3 : Type == "VEC4" ? 4 : 0;
	const int Comp = static_cast<int>(A.Num("componentType", 0.0));
	const size_t Count = static_cast<size_t>(A.Num("count", 0.0));
	const size_t Size = Comp == 5126 || Comp == 5125 ? 4 : Comp == 5123 ? 2 : Comp == 5121 ? 1 : 0;
	if (!Width || !Size) { Error = "glb: an accessor type this reader doesn't take"; return false; }
	const size_t Start = static_cast<size_t>(View.Num("byteOffset", 0.0) + A.Num("byteOffset", 0.0));
	const size_t Stride = View.Num("byteStride", 0.0) > 0.0 ? static_cast<size_t>(View.Num("byteStride", 0.0)) : Size * Width;
	if (Count && Start + Stride * (Count - 1) + Size * Width > Bin.size()) { Error = "glb: accessor runs past the buffer"; return false; }
	Out.resize(Count * Width);
	for (size_t I = 0; I < Count; ++I)
		for (size_t C = 0; C < Width; ++C)
		{
			const char* P = Bin.data() + Start + I * Stride + C * Size;
			double V = 0.0;
			if (Comp == 5126) { float F; std::memcpy(&F, P, 4); V = F; }
			else if (Comp == 5125) { uint32_t U; std::memcpy(&U, P, 4); V = U; }
			else if (Comp == 5123) { uint16_t U; std::memcpy(&U, P, 2); V = U; }
			else { V = static_cast<unsigned char>(*P); }
			Out[I * Width + C] = V;
		}
	return true;
}
} // namespace

bool PartMesh::LoadGlb(const std::string& B, std::string& Error)
{
	Surfaces.clear();
	if (B.size() < 28 || B.compare(0, 4, "glTF") != 0) { Error = "not a glb"; return false; }
	const size_t JsonLen = U32(B, 12);
	if (20 + JsonLen + 8 > B.size()) { Error = "glb: truncated"; return false; }
	Json J;
	if (!Json::Parse(B.substr(20, JsonLen), J, &Error)) return false;
	const size_t BinAt = 20 + JsonLen;
	const size_t BinLen = U32(B, BinAt);
	if (BinAt + 8 + BinLen > B.size()) { Error = "glb: truncated binary chunk"; return false; }
	const std::string Bin = B.substr(BinAt + 8, BinLen);
	const Json* Meshes = J.Get("meshes");
	const Json* Mats = J.Get("materials");
	if (!Meshes || Meshes->A.empty()) { Error = "glb: no mesh"; return false; }
	const Json* Prims = Meshes->A[0].Get("primitives");
	if (!Prims) { Error = "glb: no primitives"; return false; }
	for (const Json& P : Prims->A)
	{
		PartSurface S;
		const Json* Attr = P.Get("attributes");
		if (!Attr || !Attr->Get("POSITION")) { Error = "glb: a primitive without positions"; return false; }
		std::vector<double> Buf;
		size_t W = 0;
		if (!ReadAccessor(J, Bin, static_cast<size_t>(Attr->Num("POSITION")), Buf, W, Error) || W != 3) return false;
		for (size_t I = 0; I + 2 < Buf.size(); I += 3) S.Pos.push_back(V3(Buf[I], Buf[I + 1], Buf[I + 2]));
		if (Attr->Get("NORMAL"))
		{
			if (!ReadAccessor(J, Bin, static_cast<size_t>(Attr->Num("NORMAL")), Buf, W, Error) || W != 3) return false;
			for (size_t I = 0; I + 2 < Buf.size(); I += 3) S.Nrm.push_back(V3(Buf[I], Buf[I + 1], Buf[I + 2]));
		}
		S.Nrm.resize(S.Pos.size());
		if (Attr->Get("TEXCOORD_0"))
		{
			if (!ReadAccessor(J, Bin, static_cast<size_t>(Attr->Num("TEXCOORD_0")), Buf, W, Error) || W != 2) return false;
			for (size_t I = 0; I + 1 < Buf.size(); I += 2) { S.U.push_back(Buf[I]); S.V.push_back(Buf[I + 1]); }
		}
		S.U.resize(S.Pos.size());
		S.V.resize(S.Pos.size());
		if (P.Get("indices"))
		{
			if (!ReadAccessor(J, Bin, static_cast<size_t>(P.Num("indices")), Buf, W, Error) || W != 1) return false;
			for (double D : Buf) S.Idx.push_back(static_cast<unsigned>(D));
		}
		else
			for (unsigned I = 0; I < S.Pos.size(); ++I) S.Idx.push_back(I);
		const size_t Mi = static_cast<size_t>(P.Num("material", -1.0));
		S.Material = (Mats && Mi < Mats->A.size()) ? Mats->A[Mi].Str("name") : std::string("raw_steel");
		Surfaces.push_back(std::move(S));
	}
	return true;
}

bool PartMesh::LoadMorph(const std::string& Bytes, const std::string& KeysJson, std::string& Error)
{
	Keys.clear();
	Json J;
	if (!Json::Parse(KeysJson, J, &Error)) return false;
	if (!J.IsArray()) { Error = "shape_keys: not a list"; return false; }
	for (const Json& K : J.A)
	{
		ShapeKey Key;
		Key.Name = K.Str("name");
		if (const Json* Surfs = K.Get("surfaces"))
			for (const Json& S : Surfs->A)
			{
				ShapeKey::Moves M;
				M.Surface = static_cast<size_t>(S.Num("surface"));
				const size_t N = static_cast<size_t>(S.Num("count"));
				const size_t At = static_cast<size_t>(S.Num("offset"));
				if (At + N * 28 > Bytes.size() || M.Surface >= Surfaces.size()) { Error = "morph.bin: a key runs past the file"; return false; }
				for (size_t I = 0; I < N; ++I)
				{
					uint32_t V;
					std::memcpy(&V, Bytes.data() + At + I * 4, 4);
					if (V >= Surfaces[M.Surface].Pos.size()) { Error = "morph.bin: a vertex out of range"; return false; }
					M.Vert.push_back(V);
				}
				auto Vec = [&](size_t Base, size_t I) {
					float F[3];
					std::memcpy(F, Bytes.data() + Base + I * 12, 12);
					return V3(F[0], F[1], F[2]);
				};
				for (size_t I = 0; I < N; ++I) M.DPos.push_back(Vec(At + N * 4, I));
				for (size_t I = 0; I < N; ++I) M.DNrm.push_back(Vec(At + N * 16, I));
				Key.Surfaces.push_back(std::move(M));
			}
		Keys.push_back(std::move(Key));
	}
	return true;
}

PartMesh PartMesh::Deformed(const std::map<std::string, double>& Weights, const Warp& WarpFn) const
{
	PartMesh Out;
	Out.Surfaces = Surfaces;
	for (const ShapeKey& K : Keys)
	{
		auto It = Weights.find(K.Name);
		if (It == Weights.end() || It->second == 0.0) continue;
		for (const ShapeKey::Moves& M : K.Surfaces)
		{
			PartSurface& S = Out.Surfaces[M.Surface];
			for (size_t I = 0; I < M.Vert.size(); ++I)
			{
				S.Pos[M.Vert[I]] += M.DPos[I] * It->second;
				S.Nrm[M.Vert[I]] += M.DNrm[I] * It->second;
			}
		}
	}
	if (!WarpFn) return Out;
	const double H = 0.002;
	for (PartSurface& S : Out.Surfaces)
		for (size_t I = 0; I < S.Pos.size(); ++I)
		{
			const V3 P = S.Pos[I];
			const V3 N = S.Nrm[I].Normalized();
			// The warp's Jacobian by central differences; normals take its inverse transpose.
			const V3 Jx = (WarpFn(P + V3(H, 0, 0)) - WarpFn(P - V3(H, 0, 0))) / (2.0 * H);
			const V3 Jy = (WarpFn(P + V3(0, H, 0)) - WarpFn(P - V3(0, H, 0))) / (2.0 * H);
			const V3 Jz = (WarpFn(P + V3(0, 0, H)) - WarpFn(P - V3(0, 0, H))) / (2.0 * H);
			// Inverse transpose of [Jx Jy Jz] applied to N is the cofactor matrix times N (up to scale).
			const V3 Cx = Jy.Cross(Jz), Cy = Jz.Cross(Jx), Cz = Jx.Cross(Jy);
			V3 Nn = Cx * N.x + Cy * N.y + Cz * N.z;
			if (Cx.Dot(Jx) < 0.0) Nn = -Nn;   // a mirroring warp keeps normals facing out
			S.Pos[I] = WarpFn(P);
			S.Nrm[I] = Nn.Normalized();
		}
	return Out;
}

size_t PartMesh::Triangles() const
{
	size_t N = 0;
	for (const PartSurface& S : Surfaces) N += S.Idx.size() / 3;
	return N;
}
} // namespace TorqueFit
