#include <cuda_runtime.h>
#include "solver_struct.h"

// solve linear system using selected linear solver
void solveLinearSystem(
	FVMeshDevice& mesh,
	Coefficients& coeff,
	const LinearSolverConfig& config,
	cudaStream_t stream,
	double*& xTemp,
	double*& x,
	int threadsPerBlock
);