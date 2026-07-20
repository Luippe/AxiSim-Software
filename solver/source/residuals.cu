#include "residuals.cuh"

#include <math_constants.h>

#include "device_launch_parameters.h"




__global__
void continuityResidual(FVMeshDevice mesh, VariablesSimple simple, double* res) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) {
		res[n] = 0.0;
		return;
	}

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	double imbalance = 0.0;

	for (int k = start; k < end; k++) {
		int f = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[f];
		int neighbor = mesh.faces.neighbor[f];

		double mDotOwner = simple.mDot[f];

		if (owner == n) {
			imbalance += mDotOwner;
		}
		else if (neighbor == n) {
			imbalance -= mDotOwner;
		}
	}

	res[n] = imbalance;

}

double residualScaleSum(ConfigResidual& cfg, int N) {

	std::vector<double> h_vec(N);

	cudaMemcpy(h_vec.data(), cfg.scale, N * sizeof(double), cudaMemcpyDeviceToHost);

	double sum = 0.0;

	for (double& x : h_vec) {
		sum += std::abs(x);
	}

	return sum;

}

void residualL1Host(ConfigResidual& cfg, int N) {

	std::vector<double> h_vec(N);

	cudaMemcpy(h_vec.data(), cfg.res, N * sizeof(double), cudaMemcpyDeviceToHost);

	double sum = 0.0;

	for (double& x : h_vec) {
		sum += std::abs(x);
	}

	cfg.resVal = sum;
}


void residualL2Host(ConfigResidual& cfg, int N) {

	std::vector<double> h_vec(N);

	cudaMemcpy(h_vec.data(), cfg.res, N * sizeof(double), cudaMemcpyDeviceToHost);

	double sum = 0.0;

	for (double& x : h_vec) {
		sum += x * x;
	}

	cfg.resVal = sqrt(sum);
}


void residualLInfHost(ConfigResidual& cfg, int N) {

	std::vector<double> h_vec(N);

	cudaMemcpy(h_vec.data(), cfg.res, N * sizeof(double), cudaMemcpyDeviceToHost);

	for (double& x : h_vec) {
		x = std::abs(x);
	}

	cfg.resVal = *std::max_element(h_vec.begin(), h_vec.end());
}


void residualAllHost(std::unordered_map<std::string, ConfigResidual>& cfgs, int N, int currentIteration) {

	for (auto& [name, cfg] : cfgs) {
		if (cfg.enabled) {

			// treat continuity equation differently
			if (name == "Continuity") {
				residualL1Host(cfg, N);
				if (currentIteration < 5) {
					cfg.scaleVal = std::max(cfg.resVal, 0.0);
				}

				cfg.resVal /= cfg.scaleVal;
				continue;
			}

			// reduce the per-cell residual vector (cfg.res) to a single value
			switch (cfg.normType) {

			case RESIDUAL_L1:   residualL1Host(cfg, N);   break;
			case RESIDUAL_L2:   residualL2Host(cfg, N);   break;
			case RESIDUAL_LINF: residualLInfHost(cfg, N); break;
			}

			// scale the residual
			switch (cfg.scaleType) {

			case RESIDUAL_SCALING_NONE:     cfg.scaleVal = 1.0;							break;
			case RESIDUAL_SCALING_N:        cfg.scaleVal = N;							break;
			case RESIDUAL_SCALING_SQRT_N:   cfg.scaleVal = sqrt((double)N);				break;
			case RESIDUAL_SCALING_DIAGONAL:	cfg.scaleVal = residualScaleSum(cfg, N);    break;
			}

			if (cfg.scaleVal == 0.0) continue;

			cfg.resVal /= cfg.scaleVal;
		}
	}
}