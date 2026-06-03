#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"


// computational helper functions
__device__
void addStructuredNeighborCoeff(
	int n,
	int nb,
	int nz,
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
void addDiffusionCoefficient(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc
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