#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "solver_struct.h"

struct ResidualPairs {
	Coefficients coeff;
	const double* x = nullptr;
	const double* xTemp = nullptr;
};

template <typename... Systems>
__global__
void residualAll(ResidualType residualType, Systems...systems) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	(residualRaw(systems, n), ...);

}

__device__
void residualRaw(ResidualPairs& pairs, int n);

__device__
void residualRelative(ResidualPairs& pairs, int n);


template <typename... Coefficients>
void residualAllHost(ResidualNormType residualNormType, Coefficients&...coeff) {

	// norm of residual
	if (residualNormType == RESIDUAL_L1) {
		(residualL1Host(coeff), ...);
	}
	else if (residualNormType == RESIDUAL_L2) {
		(residualL2Host(coeff), ...);
	}
	else if (residualNormType == RESIDUAL_LINF) {
		(residualLInfHost(coeff), ...);
	}


	//if (residualType == RESIDUAL_RELATIVE) {
	//	residualRelativeAllHost(systems...);
	//	return;
	//}
	//else if (residualType == RESIDUAL_RAW) {
	//	residualRawAllHost(systems...);
	//	return;
	//}
}

void residualL1Host(Coefficients& coeff);

void residualL2Host(Coefficients& coeff);

void residualLInfHost(Coefficients& coeff);