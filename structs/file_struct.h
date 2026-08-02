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

	// 8 GLOBAL vertex labels, in OpenFOAM's canonical hex order. A corner shared
	// with a neighbouring block carries that neighbour's label too -- that sharing
	// is the only thing that makes the seam between them an interior face. A block
	// on the axis repeats a label, collapsing the cell to a prism.
	std::array<int, 8> indices;
	std::array<int, 3> size;
	std::array<double, 3> grading;

};

struct BlockMeshDict {

	double scale = 1.0;

	// The dict's whole `vertices` list, already welded: one entry per distinct
	// corner in the mesh, not 8 per block. Hex::indices and BoundaryFOAM::faces
	// index straight into it.
	std::vector<Vec3> vertices;

	std::vector<Hex> hexes;
	std::unordered_map<std::string, BoundaryFOAM> boundary;
	MergeType mergeType = MergeType::MERGE_POINTS;

};

// One patch of a PolyMesh, as constant/polyMesh/boundary spells it: a CONTIGUOUS
// run of the face list rather than a list of faces. That is the whole reason
// PolyMesh::faces is ordered the way it is.
struct FoamPolyPatch {

	std::string name;
	BoundaryFOAMType type = BoundaryFOAMType::PATCH;

	// The BoundarySegmentGroup this patch was named and typed from, or -1 for the
	// two wedge planes and the "unassigned" fallback -- the same role, and for the
	// same reason, as BoundaryFOAM::groupID.
	int groupID = -1;

	int startFace = 0;
	int nFaces = 0;

};

// A mesh written straight into constant/polyMesh, skipping blockMesh entirely.
//
// This is the export path for a mesh blockMeshDict cannot spell. A dict is a list
// of HEXES: it can only describe a mesh that decomposes into structured blocks, so
// AxiSim's unstructured (gmsh-triangulated) meshes have no dict at all. polyMesh is
// OpenFOAM's own on-disk format and is face-based like AxiSim's own FVMesh, so the
// triangles go out as the arbitrary polyhedra they revolve into with nothing lost.
//
// The layout is not free-form -- OpenFOAM reads these files as written and checks
// the invariants rather than repairing them:
//
//   - every face is wound so its normal points OUT of faceOwner, i.e. from owner
//     towards neighbour;
//   - INTERNAL faces come first, each with owner < neighbour, sorted by owner and
//     then by neighbour ("upper-triangular order");
//   - BOUNDARY faces follow, grouped into the contiguous runs the patches name.
//
// Face point counts vary: the wedge revolve turns a triangle edge into a quad, or
// into a triangle when one of its endpoints sits on the axis. An edge lying ON the
// axis sweeps nothing and is left out entirely, which is exactly what blockMesh
// does to a block face on the axis.
struct PolyMesh {

	std::vector<Vec3> points;

	// Point labels per face, in the order described above.
	std::vector<std::vector<int>> faces;

	// Parallel to `faces`.
	std::vector<int> faceOwner;

	// INTERNAL faces only -- boundary faces have no neighbour, and OpenFOAM sizes
	// the file itself to tell the two apart. So this doubles as the internal count.
	std::vector<int> faceNeighbour;

	// In startFace order, covering every face from nInternalFaces() to faces.size()
	// with no gap: a boundary face in no patch is a hole the solver aborts on.
	std::vector<FoamPolyPatch> patches;

	int nCells = 0;

	int nInternalFaces() const {
		return (int)faceNeighbour.size();
	}

};

// Convection scheme for the exported divSchemes, mirroring the solver's own
// ConvectionScheme. Mirrored rather than reused because solver_struct.h reaches
// into setting.cuh and gpu_utils.h, and none of the case writers want that.
enum class FoamConvection {
	Upwind = 0,
	Linear = 1,
	LinearUpwind = 2,
	Quick = 3
};

// Everything OUTSIDE the mesh that the case dictionaries need, flattened out of
// Solver and FluidPropertyConfig at the call site. The mesh carries the geometry
// and the boundary conditions; this carries the physics and the run control.
struct FoamCaseSetup {

	// Base SI, exactly as the solver stores them.
	double rho = 998.0;        // kg/m^3
	double mu  = 1.0518e-3;    // Pa.s
	double D   = 3.0277e-9;    // mass diffusivity, m^2/s
	double cp  = 4180.0;       // J/(kg.K)
	double k   = 0.6;          // W/(m.K)

	bool solveEnergy = false;
	bool solveConcentration = false;

	bool transient = false;
	bool secondOrderTime = false;
	double dt = 0.1;
	double tEnd = 2.0;

	// Iteration cap for a steady run. Only a cap: residualControl is what actually
	// stops SIMPLE, and it normally trips long before this.
	//
	// This value is a FLOOR, not the Solver tab's "Maximum Outer Iterations"
	// verbatim -- foamCaseSetupFromSolver raises it to AxiSim's cap when AxiSim
	// asks for more and never lowers it. A short AxiSim run is a deliberate choice
	// about AxiSim; copying it across would stop the reference short of converged
	// and turn the comparison into a measurement of the reference's own error.
	int steadyIterations = 2000;

	FoamConvection convection = FoamConvection::Upwind;
	bool leastSquaresGradient = true;

	// Solver tab -> Add Convection Term. simpleFoam and pimpleFoam always assemble
	// div(phi,U), so this cannot be honoured -- it exists so writeFvSchemes can say
	// so in the dict instead of exporting a case that quietly solves a different
	// equation set than the one AxiSim ran.
	bool addConvection = true;

	// ConfigSimple::useNonOrthCorrector -> nNonOrthogonalCorrectors. AxiSim's flag
	// is one extra deferred corrector pass, so it maps to 1 rather than a count.
	bool nonOrthCorrector = false;

	double momentumRelaxation = 0.7;
	double pressureRelaxation = 0.3;

	// AxiSim under-relaxes temperature with momentumRelaxation but leaves
	// concentration at 1.0 (solver.cpp, the two underRelaxEquation calls). Both are
	// written out separately rather than sharing one "(T|Conc)" entry so the dict
	// says what AxiSim actually did.
	double concentrationRelaxation = 1.0;

	// OpenFOAM's incompressible solvers are written in kinematic terms: rho is
	// divided out of the whole equation set and never appears in a dictionary.
	// This is also why the exported p is p/rho rather than Pa.
	double nu() const { return rho != 0.0 ? mu / rho : 0.0; }

	// The energy equation's counterpart to D. scalarTransport takes a diffusivity,
	// not a conductivity, so k has to be divided through by rho*cp on the way out.
	double alpha() const {
		return (rho != 0.0 && cp != 0.0) ? k / (rho * cp) : 0.0;
	}
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