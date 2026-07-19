#include <cuda_runtime.h>
#include "solver_struct.h"

// solve linear system using selected linear solver.
//
// `coloring` is only consulted for Gauss-Seidel on the face path (multiblock /
// unstructured); an unbuilt one (MeshColoring::valid) falls back to Jacobi.
void solveLinearSystem(
	Coefficients& coeff,
	const ConfigSolver& config,
	cudaStream_t stream,
	double*& x,
	double*& xTemp,
	uint8_t*& active,
	int threadsPerBlock,
	const MeshColoring& coloring
);