#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"

struct GridConfig;
struct FluidPropertyConfig;
struct MultigridLevel;
struct MultiBlockMesh;

// initialize and allocate GridConfig variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f);

// allocate memory for coefficient matrix
void allocateCoefficients(std::unordered_map<std::string, Coefficients>& coefficients, const FVMesh& mesh);
void allocateCoefficients(Coefficients& coeff, int nr, int nz);
void allocateCoefficients(Coefficients& coeff, const FVMesh& mesh);

void allocateResiduals(std::unordered_map<std::string, ConfigResidual>& configResiduals, const FVMesh& mesh);

// allocate memory for simple algorithm
void allocateSimple(Config& config, VariablesSimple& vars, FVMesh& mesh, const SolverFieldOption& option);

// initialize and allocate cell variables
void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars);

// copy coefficients
void copyCoefficients(Coefficients& dst, const Coefficients& src, int N, cudaStream_t stream);

// allocate variables used for the multigrid method
void allocateMultigridLevel(MultigridLevel& level);

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
