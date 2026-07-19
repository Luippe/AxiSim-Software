#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"

struct GridConfig;
struct FluidPropertyConfig;
struct MultigridLevel;
struct MultiBlockMesh;

// initialize and allocate GridConfig variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f);

// Flatten the mesh's cell/face connectivity into the CSR layout every face-path
// consumer expects:
//
//   cell n owns slots [faceStart[n], faceStart[n + 1])
//   slot k holds the cell across that face, or < 0 for a boundary slot
//
// THE single definition of that walk. The multigrid hierarchy indexes level 0's
// slots by number (GridLevel::fineSlotToCoarseSlot), so a second, separately
// written walk that ordered slots differently would make the coarse operator
// scatter into the wrong entries -- silently, since the residual still drops.
void buildCellFaceCSR(const FVMesh& mesh, std::vector<int>& faceStart, std::vector<int>& faceNeighbor);

// allocate memory for coefficient matrix
void allocateCoefficients(std::unordered_map<std::string, Coefficients>& coefficients, const FVMesh& mesh);
void allocateCoefficients(Coefficients& coeff, int nr, int nz);
void allocateCoefficients(Coefficients& coeff, const FVMesh& mesh);

// Face-path allocation for a level that has no FVMesh behind it (a multigrid
// coarse level, whose connectivity comes from the agglomeration map).
void allocateCoefficients(
	Coefficients& coeff,
	int nCells,
	const std::vector<int>& faceStart,
	const std::vector<int>& faceNeighbor
);

void allocateResiduals(std::unordered_map<std::string, ConfigResidual>& configResiduals, const FVMesh& mesh);

// Greedy multicolor ordering of a cell graph in CSR form, uploaded ready for the
// Gauss-Seidel sweeps in linear_solver.cu. Frees any previous coloring first.
// Takes the CSR rather than an FVMesh so a multigrid level -- which has
// connectivity but no mesh behind it -- can be colored too.
void buildMeshColoring(
	MeshColoring& coloring,
	int nCells,
	const std::vector<int>& faceStart,
	const std::vector<int>& faceNeighbor
);

void buildMeshColoring(MeshColoring& coloring, const FVMesh& mesh);

// allocate memory for simple algorithm
void allocateSimple(Config& config, VariablesSimple& vars, FVMesh& mesh, const SolverFieldOption& option);

// initialize and allocate cell variables
void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars);

// copy coefficients
void copyCoefficients(Coefficients& dst, const Coefficients& src, int N, cudaStream_t stream);

// allocate variables used for the multigrid method
void allocateMultigridLevel(MultigridLevel& level);

// free the device memory owned by a multigrid level (mirror of allocateMultigridLevel)
void freeMultigridLevel(MultigridLevel& level);

void free_GridConfig(GridConfig& g);

FVMeshDevice createFVMeshDevice(const FVMesh& fvMesh);

// Multi-block structured path (structs/multiblock.h): assemble the packed mesh
// from blocks + interfaces, then upload through the same path as the FVMesh overload.
FVMeshDevice createFVMeshDevice(const MultiBlockMesh& mb);

BoundarySolverDevice createBoundarySolverDevice(
	const std::vector<BoundarySegmentGroup>& boundaryGroups,
	const SolverFieldOption& option
);

// free device memory held by the boundary field / solver structs
void freeBoundaryFieldDevice(BoundaryFieldDevice& d);
void freeBoundarySolverDevice(BoundarySolverDevice& dBC);
