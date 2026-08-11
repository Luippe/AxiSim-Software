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
	FluidPropertyConfig fluid,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundarySolverDevice bc
);

// ---------------pressure correction----------------
__global__
void createPPCoeff(
	FluidPropertyConfig fluid,
	FVMeshDevice mesh,
	Coefficients coeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
);

__global__
void createPPRhs(
	FluidPropertyConfig fluid,
	FVMeshDevice mesh,
	Coefficients ppCoeff,
	VariablesSimple simple,
	int applyNonOrtho
);

// ---------------------update variables----------------
__global__
void updateMomentumPressure(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
);

__global__
void updateMassFlux(
	FluidPropertyConfig fluid,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC,
	int applyNonOrtho
);

// D = V / aP for both momentum equations in one pass. Fused because the two
// share the cell volume and differ only in which diagonal they divide it by.
__global__
void getCorrectionCoefficients(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	double* DU,
	double* DV
);