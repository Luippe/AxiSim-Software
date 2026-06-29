#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"
#include "boundary_struct.h"

// momentum rhs
// Adds the pressure-gradient body force to the momentum RHS. grad(p) must be
// precomputed into simple.gradPZ/gradPR (same scheme as the rest of the solve).
__global__
void createMomentumPressureRhs(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple
);

__global__
void computeFaceMassFluxRhieChow(
	Config config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundarySolverDevice bc
);

// ---------------pressure correction----------------
__global__
void createPPCoeff(
	Config config,
	FVMeshDevice mesh,
	Coefficients coeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
);

__global__
void createPPRhs(
	Config config,
	FVMeshDevice mesh,
	Coefficients ppCoeff,
	VariablesSimple simple,
	int applyNonOrtho
);

// ---------------------update variables----------------
__global__
void updateVelocity(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
);


__global__
void updatePressure(
	FVMeshDevice mesh,
	VariablesSimple simple
);

__global__
void updateMassFlux(
	Config config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC,
	int applyNonOrtho
);

__global__
void getCorrectionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	double* D
);