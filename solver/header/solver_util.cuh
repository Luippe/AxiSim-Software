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

__global__
void copyVector(double* vec1, double* vec2, int N);

__global__
void createCoefficients(Config config, Coefficients coeff, BoundaryConditionConfig bc);
