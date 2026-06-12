#include <cuda_runtime.h>
#include "solver_struct.h"

// solve linear system using selected linear solver
void solveLinearSystem(
	Coefficients& coeff,
	const LinearSolverConfig& config,
	cudaStream_t stream,
	double*& x,
	double*& xTemp,
	uint8_t*& active,
	int threadsPerBlock
);