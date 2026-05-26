#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"

__device__
bool isStoredCenter(CellStoreType& storeType);

__device__
bool isStoredAxial(CellStoreType& storeType);

__device__
bool isStoredRadial(CellStoreType& storeType);

__device__
bool isBCDirichlet(BCType& type);

__device__
bool isFullyDeveloped(BCType& type);

__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

__global__
void finalizeCoefficients(Coefficients coeff);

__global__
void addVDiffusionCoefficient(ConfigSolver config, Coefficients coeff, BoundaryConditionConfig bc);

__global__
void addUDiffusionCoefficient(ConfigSolver config, Coefficients coeff, BoundaryConditionConfig bc);

__global__
void addUTransientCoefficient(ConfigSolver config, Coefficients uCoeff, VariablesSimple simple);

__global__
void addVTransientCoefficient(ConfigSolver config, Coefficients vCoeff, VariablesSimple simple);

__global__
void addUConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, BoundaryConditionConfig uBC, BoundaryConditionConfig vBC, ConvectionScheme scheme);

__global__
void addVConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, BoundaryConditionConfig uBC, BoundaryConditionConfig vBC, ConvectionScheme scheme);

__global__
void clearCoefficients(Coefficients coeff);