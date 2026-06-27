#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"

struct GridConfig;
struct FluidPropertyConfig;
struct MultigridLevel;

// initialize and allocate GridConfig variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f);

// allocate memory for coefficient matrix
void allocateCoefficients(Coefficients& coeff, int nr, int nz);
void allocateCoefficients(Coefficients& coeff, const FVMesh& mesh);

// allocate memory for simple algorithm
void allocateSimple(ConfigSolver& config, VariablesSimple& vars, FVMesh& mesh, const SolverFieldOption& option);

// initialize and allocate cell variables
void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars);

// allocate variables used for the multigrid method
void allocateMultigridLevel(MultigridLevel& level);

void free_GridConfig(GridConfig& g);

FVMeshDevice createFVMeshDevice(const FVMesh& fvMesh);

BoundarySolverDevice createBoundarySolverDevice(
	const std::vector<BoundarySegmentGroup>& boundaryGroups,
	const SolverFieldOption& option
);
