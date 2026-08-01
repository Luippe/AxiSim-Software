#pragma once

#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include "core_struct.h"

// ====================================================
// -------------------OPENFOAM-------------------------
// ====================================================
enum class MergeType {
	MERGE_POINTS = 1,
	MERGE_TOPOLOGY = 2
};

enum class BoundaryFOAMType {
	PATCH = 1,
	WALL = 2,
	WEDGE = 3,
	SYMMETRY_PLANE = 4
};

// One blockMeshDict patch: a type plus every quad that belongs to it, each quad
// spelled as 4 global vertex labels. Many faces per patch, not one -- the two
// wedge planes alone collect a face from every block in the mesh.
//
// A quad that touches the axis is emitted with a repeated label (3 distinct
// corners, one written twice), which is how blockMesh spells a collapsed face.
struct BoundaryFOAM {

	BoundaryFOAMType type = BoundaryFOAMType::PATCH;
	std::vector<std::array<int, 4>> faces;

};

struct Hex {

	std::array<int, 8> indices;
	std::array<int, 3> size;
	std::array<double, 3> grading;

};

struct BlockMeshDict {

	double scale = 1.0;
	std::vector<std::vector<Vec3>> vertices;
	std::vector<Hex> hexes;
	std::unordered_map<std::string, BoundaryFOAM> boundary;
	MergeType mergeType = MergeType::MERGE_POINTS;

};