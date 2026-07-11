#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"

// reduction kernel
void reduction(int N, int threadsPerBlock, size_t shmem, cudaStream_t stream, double* tmpA, double* tmpB, double* in, double* store);

// computational helper functions
__device__
void addNeighborCoeff(
	int n,
	int nb,
	FVMeshDevice mesh,
	double aNb,
	Coefficients coeff
);

__device__
void phiGradientGreenGauss(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
);

__device__
void phiGradientLeastSquare(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
);


__device__
double interpolateFieldToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice fieldBC,
	const double* phi
);

__device__
double nonOrthoScalarDiffusionFlux(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	const double* gradPhiZ,
	const double* gradPhiR,
	double gamma
);

__device__
void getOutwardNormalForCell(
	FVMeshDevice mesh,
	int cellID,
	int faceID,
	double& normalZ,
	double& normalR
);

__device__
double interpolateNormalCorrectionCoeffToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple
);

__global__
void computeGradient(
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	double* phi,
	double* gradZ,
	double* gradR,
	GradientScheme scheme
);

__device__
double getDistanceCellToCell(
	const FVMeshDevice& mesh,
	int owner,
	int neighbor,
	double normalZ,
	double normalR
);

__device__
double getDistanceCellToFace(
	const FVMeshDevice& mesh,
	int cellID,
	int faceID,
	double normalZ,
	double normalR
);


// boolean helper functions
__device__
bool isDirichletType(uint8_t type);

__device__
bool isNeumannType(uint8_t type);

__device__
bool isFullyDevelopedType(uint8_t type);

__device__
bool isMichaelisMentenType(uint8_t type);

__device__
bool isHillType(uint8_t type);

__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

__global__ void
addDiffusionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	int applyNonOrtho,
	double constVar
);

__global__
void addUTransientCoefficient(Config config, Coefficients uCoeff, VariablesSimple simple, double dt);

__global__
void addVTransientCoefficient(Config config, Coefficients vCoeff, VariablesSimple simple, double dt);

// fluxScale multiplies the face MASS flux (mDot = rho*u*area) before it is used
// as the convecting flux F. Momentum convects mass, so it passes 1.0. A passive
// scalar (species concentration) convects with the VOLUMETRIC flux u*area, so it
// passes 1/rho to divide the density out and stay consistent with the kinematic
// diffusivity (f.D) used by addDiffusionCoefficient.
__global__
void addConvectionCoefficient(
	FVMeshDevice mesh,
	VariablesSimple simple,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	double fluxScale
);

// One-shot post-solve diagnostic for reactive (Michaelis-Menten / Hill) walls.
// Accumulates per-face contributions into diag[0..7] via atomics; the host then
// reports wall consumption, the mass-transfer ceiling, and depletion. Layout:
//   [0] total wall OCR      [amount/s]   [1] inlet substrate flux   [amount/s]
//   [2] mass-transfer ceil  [amount/s]   [3] reactive face count
//   [4] sum(cw)  [5] sum(dPF)  [6] sum(h)  [7] sum(cp)
__global__
void wallConsumptionDiagnostic(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice bc,
	const double* phi,
	double D,
	double* diag
);

__global__
void clearCoefficients(Coefficients coeff);

__global__
void underRelaxEquation(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* x,
	double alpha
);
