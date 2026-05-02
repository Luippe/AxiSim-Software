#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"

struct ResidualPairs {
	Coefficients coeff;
	const double* x = nullptr;
	const double* xOld = nullptr;
};

template <typename... Systems>
__global__
void residualAll(ResidualType residualType, Systems...systems) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (residualType == ResidualType::RESIDUAL_RAW) {
		(residualRaw(systems, n), ...);
	}
	else if (residualType == ResidualType::RESIDUAL_RELATIVE) {

	}

}

__device__
void residualRaw(ResidualPairs& pairs, int n);

