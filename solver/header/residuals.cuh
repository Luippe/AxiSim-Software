#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "solver_struct.h"
#include "boundary_struct.h"

struct ResidualPairs {
	Coefficients coeff;
	const double* x = nullptr;
	double* res = nullptr;   // per-cell residual output (owned by ConfigResidual)
	double* scale = nullptr;
	ResidualScalingType scaleType = RESIDUAL_SCALING_NONE;
};


__device__ __forceinline__
void residualRaw(const uint8_t* activeCell, bool sign, const ResidualPairs& pairs, int n) {

	const Coefficients& coeff = pairs.coeff;
	const double* x = pairs.x;
	double* res = pairs.res;
	double* scale = pairs.scale;


	if (n >= coeff.N) return;

	if (activeCell && !activeCell[n]) {
		if (res) {
			res[n] = 0.0;
		}
		return;
	}

	double Ax = coeff.AC[n] * x[n];


	if (pairs.scaleType == RESIDUAL_SCALING_DIAGONAL) {
		scale[n] = Ax;
	}

	if (coeff.useFaceCoeffs &&
		coeff.AF &&
		coeff.faceStart &&
		coeff.faceNeighbor) {
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
		return;
	}

	int nr = coeff.nr;
	int nz = coeff.nz;

	int j = n % nz;
	int i = n / nz;

	if (j < nz - 1) {
		Ax += coeff.AE[n] * x[n + 1];
	}

	if (j > 0) {
		Ax += coeff.AW[n] * x[n - 1];
	}

	if (i < nr - 1) {
		Ax += coeff.AN[n] * x[n + nz];
	}

	if (i > 0) {
		Ax += coeff.AS[n] * x[n - nz];
	}

	double r = coeff.b[n] - Ax;

	res[n] = sign ? r : fabs(r);

}

__global__
void continuityResidual(FVMeshDevice mesh, VariablesSimple simple, double* res);


template <typename... Systems>
__global__
void residualAll(uint8_t* activeCell, bool sign, Systems...systems) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	(residualRaw(activeCell, sign, systems, n), ...);
}

// reduce a field's per-cell residual vector (cfg.res) to a single value (cfg.resVal).
// coeff supplies the cell count N used for the norm/scaling.
void residualAllHost(std::unordered_map<std::string, ConfigResidual>& cfgs, int N, int currentIteration);
