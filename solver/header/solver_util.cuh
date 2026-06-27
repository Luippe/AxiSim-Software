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

__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

__global__
void addDiffusionCoefficient(
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
void addConvectionCoefficient(
	FVMeshDevice mesh,
	VariablesSimple simple,
	Coefficients coeff,
	BoundaryFieldDevice bc
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
