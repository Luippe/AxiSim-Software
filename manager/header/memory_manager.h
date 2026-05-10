#pragma once
#include "solver_struct.h"

struct GridConfig;
struct FluidPropertyConfig;

// initialize and allocate GridConfig variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f);

// allocate memory for coefficient matrix
void allocateCoefficients(ConfigSolver& config, Coefficients& coeff, BoundaryConditionConfig& bc, CellStoreType type);

// allocate memory for simple algorithm
void allocateSimple(ConfigSolver& config, VariablesSimple& vars, BoundaryConditionConfig& bc);

// initialize and allocate cell variables
void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars);

void free_GridConfig(GridConfig& g);