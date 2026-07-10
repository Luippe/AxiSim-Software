#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "solver_struct.h"
#include "boundary_struct.h"

struct ResidualPairs {
	Coefficients coeff;
	const double* x = nullptr;
	double* res = nullptr;   // per-cell residual output (owned by ConfigResidual)
};

__global__
void continuityResidual(FVMeshDevice mesh, VariablesSimple simple, double* res);


template <typename... Systems>
__global__
void residualAll(uint8_t* activeCell, bool sign, Systems...systems) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	(residualRaw(activeCell, sign, systems, n), ...);

}

__device__
void residualRaw(uint8_t* activeCell, bool sign, ResidualPairs& pairs, int n);


// reduce a field's per-cell residual vector (cfg.res) to a single value (cfg.resVal).
// coeff supplies the cell count N used for the norm/scaling.
void residualAllHost(ConfigResidual& cfg, const Coefficients& coeff);

// absolute sum
void residualL1Host(ConfigResidual& cfg, int N);

// least square
void residualL2Host(ConfigResidual& cfg, int N);

// get maximum absolute value of a residual vector
void residualLInfHost(ConfigResidual& cfg, int N);