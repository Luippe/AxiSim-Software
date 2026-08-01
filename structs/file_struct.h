#pragma once

#include <vector>
#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include "boundary_struct.h"   // BoundaryVariable
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

	// The BoundarySegmentGroup this patch was named and typed from, or -1 for the
	// two wedge planes and for the fallback "unassigned" patch. The 0/ field files
	// have to spell the same patch names as the dict, and the sanitize + collision
	// pass that produced them is not reversible -- carrying the id is what lets the
	// field writer find each patch's boundary conditions without redoing it.
	int groupID = -1;

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

// One patch's entry in one 0/ field file.
//
// `type` is the OpenFOAM boundary-condition name. `entry` is the single keyword
// line that type needs ("value uniform 0", "gradient uniform 1e-05") and is empty
// for the types that take none -- zeroGradient, noSlip, and every constraint type.
// `note` is written as a trailing // comment, and is set whenever the AxiSim
// condition could not be reproduced exactly rather than left for the user to find
// by comparing two solvers' answers.
struct FieldPatch {

	std::string type = "zeroGradient";
	std::string entry;
	std::string note;

};

// One file under 0/: the initial condition for a field, plus its condition on
// every patch. blockMesh never reads these -- the solver does, and it insists on
// an entry for every patch in the mesh, so a patch missing here is a fatal error
// at solver startup rather than a default.
struct FoamField {

	std::string name;                          // field AND file name: U, p, T, C

	// The AxiSim variable this file exports. None for U, which is one vector field
	// built from two of them and so has its own path through the writer.
	BoundaryVariable variable = BoundaryVariable::None;

	const char* className = "volScalarField";
	std::string dimensions;                    // "[0 1 -1 0 0 0 0]"
	std::string internalField;                 // "uniform 0", "uniform (0 0 0)"
	std::string note;                          // banner comment, may be empty

	// Ordered rather than hashed, so two exports of the same case diff cleanly --
	// the same reason writeBlockMeshDict sorts its patches on the way out.
	std::map<std::string, FieldPatch> boundaryField;

};