#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"



__device__
bool isDirichletType(uint8_t type);

__device__
bool isNeumannType(uint8_t type);


__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

__global__
void applyOuterNeumannV(double* v, int nr, int nz);

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
void addUConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, ConvectionScheme scheme);

__global__
void addVConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, ConvectionScheme scheme);

__global__
void clearCoefficients(Coefficients coeff);