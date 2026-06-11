#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"

struct GridConfig;
struct FluidPropertyConfig;

// initialize and allocate GridConfig variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f);

// allocate memory for coefficient matrix
void allocateCoefficients(ConfigSolver& config, Coefficients& coeff);

// allocate memory for simple algorithm
void allocateSimple(ConfigSolver& config, VariablesSimple& vars, FVMesh& mesh);

// initialize and allocate cell variables
void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars);

void free_GridConfig(GridConfig& g);

FVMeshDevice createFVMeshDevice(const FVMesh& fvMesh);

BoundarySolverDevice createBoundarySolverDevice(
	const std::vector<BoundarySegmentGroup>& boundaryGroups,
	const SolverFieldOption& option
);