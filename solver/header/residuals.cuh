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
void residualAllHost(ResidualScalingType residualScaleType, ResidualNormType residualNormType, Coefficients&...coeff) {

	// get residual values
	switch (residualNormType) {

	case RESIDUAL_L1:
		(residualL1Host(coeff), ...);
		break;

	case RESIDUAL_L2:
		(residualL2Host(coeff), ...);
		break;

	case RESIDUAL_LINF:
		(residualLInfHost(coeff), ...);
		break;

	}

	// scale the residual
	switch (residualScaleType) {

	case RESIDUAL_SCALING_NONE:
		break;

	case RESIDUAL_SCALING_N:
		((coeff.resVal /= coeff.N), ...);
		break;

	case RESIDUAL_SCALING_SQRT_N:
		((coeff.resVal /= sqrt(coeff.N)), ...);
		break;

	}
}

void residualL1Host(Coefficients& coeff);

void residualL2Host(Coefficients& coeff);

void residualLInfHost(Coefficients& coeff);