#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"
#include "boundary_struct.h"

// momentum rhs
__global__
void createMomentumPressureRhs(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
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
	BoundaryFieldDevice pBC
);

__global__
void getCorrectionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	double* D
);