#include "residuals.cuh"



__global__
void continuityResidual(FVMeshDevice mesh, VariablesSimple simple, double* res) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

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

void residualAllHost(
	std::unordered_map<std::string, ConfigResidual>& cfgs,
	int N,
	int scaleIteration,
	const MemoryConfig& mem,
	cudaStream_t stream,
	double* tmpA,
	double* tmpB
) {

	auto reduce = [&](const double* in, double* store, ReductionMethod method,
		ReductionOp op = ReductionOp::SUM) {
		reduction(N, mem, stream, tmpA, tmpB, in, store, method, op);
	};

	for (auto& [name, cfg] : cfgs) {
		if (!cfg.enabled) continue;

		// treat continuity equation differently
		if (name == "Continuity") {
			reduce(cfg.res, cfg.resVal, ReductionMethod::ABSOLUTE);
		}
		else {
			// reduce the per-cell residual vector (cfg.res) to a single value
			switch (cfg.normType) {

			case ResidualNormType::RESIDUAL_L1:   reduce(cfg.res, cfg.resVal, ReductionMethod::ABSOLUTE);					break;
			case ResidualNormType::RESIDUAL_L2:   reduce(cfg.res, cfg.resVal, ReductionMethod::SQUARED);					break;
			case ResidualNormType::RESIDUAL_LINF: reduce(cfg.res, cfg.resVal, ReductionMethod::ABSOLUTE, ReductionOp::MAX);	break;
			}

			// scale the residual
			switch (cfg.scaleType) {

			case ResidualScalingType::RESIDUAL_SCALING_NONE:     *cfg.scaleVal = 1.0;										break;
			case ResidualScalingType::RESIDUAL_SCALING_N:        *cfg.scaleVal = N;											break;
			case ResidualScalingType::RESIDUAL_SCALING_SQRT_N:   *cfg.scaleVal = sqrt((double)N);							break;
			case ResidualScalingType::RESIDUAL_SCALING_DIAGONAL: reduce(cfg.scale, cfg.scaleVal, ReductionMethod::ABSOLUTE);	break;
			}
		}
	}

	CUDA_CHECK(cudaStreamSynchronize(stream));


	for (auto& [name, cfg] : cfgs) {

		// A scale of zero -- an imbalance already zero at capture, or a diagonal that
		// summed to nothing -- would divide by zero here.
		if (name == "Continuity" && scaleIteration < 5) {
			*cfg.scaleVal = std::max(*cfg.resVal, 0.0);
		}

		if (*cfg.scaleVal == 0.0) continue;

		// L2 finishes here rather than next to its reduction: the sum of squares
		// is only readable on the host once the stream has drained.
		if (cfg.normType == ResidualNormType::RESIDUAL_L2) {
			*cfg.resVal = sqrt(*cfg.resVal);
		}

		*cfg.resVal /= *cfg.scaleVal;
	}
}
