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
	ConfigSolver config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundarySolverDevice bc
);

// ---------------pressure correction----------------
__global__
void computePressureGradient(
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* p,
	double* gradPZ,
	double* gradPR
);

// Weighted least-squares cell gradient of a field (BC-aware). Provided as an
// alternative to the Green-Gauss computePressureGradient for comparison; LSQ is
// less sensitive on the small, irregular cells near the axis.
__global__
void computePressureGradientLSQ(
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* p,
	double* gradZ,
	double* gradR
);

__global__
void createPPCoeff(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
);

__global__
void createPPRhs(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients ppCoeff,
	VariablesSimple simple
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
	ConfigSolver config,
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