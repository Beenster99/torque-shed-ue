// A Blender part's mesh as Tools/import_blender_parts.py writes it (Data/Parts/Blender/<kind>/<id>.glb,
// its shape keys in <id>.morph.bin), and the one thing the fitting core does to one: mix its shape
// keys and bend it onto a bike's lines (the Godot game's BlenderParts.morphed). Plain C++, so the
// game (into a dynamic mesh) and the tests deform a part the same way.
#pragma once

#include "SimMath.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
using TorqueSim::V3;

struct PartSurface
{
	std::string Material;            // its surface tag, with any "__<slot>" colour slot
	std::vector<V3> Pos, Nrm;
	std::vector<double> U, V;
	std::vector<unsigned> Idx;       // triangles
};

struct ShapeKey
{
	std::string Name;
	// Per surface: the vertices it moves and by how much.
	struct Moves { size_t Surface = 0; std::vector<unsigned> Vert; std::vector<V3> DPos, DNrm; };
	std::vector<Moves> Surfaces;
};

struct PartMesh
{
	std::vector<PartSurface> Surfaces;
	std::vector<ShapeKey> Keys;

	// Our glb (one mesh, a primitive per surface). False with Error on anything else.
	bool LoadGlb(const std::string& Bytes, std::string& Error);
	// The shape keys: the .morph.bin and its description from index.json ("shape_keys").
	bool LoadMorph(const std::string& Bytes, const std::string& ShapeKeysJson, std::string& Error);

	// A point in the glb's space to the bike's (ours: x forward, y up, z right).
	using Warp = std::function<V3(const V3&)>;
	// The part with its keys mixed by Weights (any not named stay at 0; negative weights too) and
	// every vertex then moved by WarpFn. Normals follow the warp (by finite differences).
	PartMesh Deformed(const std::map<std::string, double>& Weights, const Warp& WarpFn) const;
	size_t Triangles() const;
};
} // namespace TorqueFit
