#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"


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
void phiGradientCell(
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

// Deferred (explicit) non-orthogonal correction flux of the pressure-correction
// p' through an interior face, seen as outward from cellID:
//
//     rho * Df * (T . grad(p')_face),   T = A*n - (A/(n.d)) d
//
// where n is the outward face normal, d = c_neighbor - c_cell, and A the face
// area. The orthogonal part (n.d direction) is handled implicitly by the matrix;
// this returns only the cross/tangential part. Returns 0 on boundary faces.
__device__
double nonOrthoPressureCorrFlux(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple,
	const double* gradPPZ,
	const double* gradPPR,
	double rho
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

__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

__global__
void addEnergyDiffusionCoefficient(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc
);

__global__
void addDiffusionCoefficient(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	const double* phi,
	const double* coupledPhi,
	int component,
	int applyNonOrtho
);

__global__
void addRadialMomentumCylindricalSource(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients vCoeff
);

__global__
void addUTransientCoefficient(ConfigSolver config, Coefficients uCoeff, VariablesSimple simple);

__global__
void addVTransientCoefficient(ConfigSolver config, Coefficients vCoeff, VariablesSimple simple);

__global__
void addMomentumConvectionCoefficient(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple,
	BoundarySolverDevice bc
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
