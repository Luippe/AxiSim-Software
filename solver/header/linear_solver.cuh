#include <cuda_runtime.h>
#include "solver_struct.h"

// solve linear system using selected linear solver
void solveLinearSystem(Coefficients& coeff, const LinearSolverConfig& config, cudaStream_t stream, double*& xTemp, double*& x, int threadsPerBlock);

__global__
void jacobi(Coefficients coeff, double* xTemp, double* x);

__global__
void jacobiPP(Coefficients coeff, double* xTemp, double* x);


__global__
void gaussSeidelRB(Coefficients coeff, double* x, int color);