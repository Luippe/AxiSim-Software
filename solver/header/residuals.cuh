#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "solver_struct.h"
#include "boundary_struct.h"
#include "solver_util.cuh"

struct ResidualPairs {
	Coefficients coeff;
	const double* x = nullptr;
	double* res = nullptr;   // per-cell residual output (owned by ConfigResidual)
	double* scale = nullptr;
	ResidualScalingType scaleType = ResidualScalingType::RESIDUAL_SCALING_NONE;
};


__device__ __forceinline__
void residualRaw(bool sign, const ResidualPairs& pairs, int n) {

	const Coefficients& coeff = pairs.coeff;
	const double* x = pairs.x;
	double* res = pairs.res;
	double* scale = pairs.scale;

	if (n >= coeff.N) return;

	double Ax = coeff.AC[n] * x[n];


	if (pairs.scaleType == ResidualScalingType::RESIDUAL_SCALING_DIAGONAL) {
		scale[n] = Ax;
	}

	int start = coeff.faceStart[n];
	int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		int nb = coeff.faceNeighbor[k];
		if (nb >= 0) {
			Ax += coeff.AF[k] * x[nb];
		}
	}

	double r = coeff.b[n] - Ax;

	res[n] = sign ? r : fabs(r);

}

__global__
void continuityResidual(FVMeshDevice mesh, VariablesSimple simple, double* res);


template <typename... Systems>
__global__
void residualAll(bool sign, Systems...systems) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	(residualRaw(sign, systems, n), ...);
}

// reduce a field's per-cell residual vector (cfg.res) to a single value (cfg.resVal).
// N is the cell count used for the norm/scaling; tmpA/tmpB are reduction scratch and
// must hold at least ceil(N / mem.threadsPerBlock) doubles each.
//
// scaleIteration drives the continuity normalization ONLY: the scale is captured
// while it is < 5, and every later residual is divided by it. It therefore has to
// count from the start of the interval continuity is being measured over -- the run
// for a steady solve, but each TIME STEP for a transient one, since every step
// starts with a fresh imbalance from the unsteady term. Passing a run-global count
// in a transient solve pins the scale to the first step's startup and makes the
// reported continuity residual meaningless from step 2 on.
void residualAllHost(
	std::unordered_map<std::string, ConfigResidual>& cfgs,
	int N,
	int scaleIteration,
	const MemoryConfig& mem,
	cudaStream_t stream,
	double* tmpA,
	double* tmpB
);
