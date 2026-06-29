#include "linear_solver.cuh"
#include "device_launch_parameters.h"
#include <utility>

__global__
void jacobi(
	Coefficients coeff,
	const double* xOld,
	double* xNew,
	uint8_t* active
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (active && !active[n]) {
		xNew[n] = xOld[n];
		return;
	}

	double AC = coeff.AC[n];

	if (fabs(AC) < 1.0e-30) {
		xNew[n] = xOld[n];
		return;
	}

	double val = coeff.b[n];

	if (coeff.useFaceCoeffs &&
		coeff.AF &&
		coeff.faceStart &&
		coeff.faceNeighbor) {
		int start = coeff.faceStart[n];
		int end = coeff.faceStart[n + 1];

		for (int k = start; k < end; k++) {
			int nb = coeff.faceNeighbor[k];
			if (nb >= 0) {
				val -= coeff.AF[k] * xOld[nb];
			}
		}

		xNew[n] = val / AC;
		return;
	}

	int nz = coeff.nz;
	int nr = coeff.nr;

	int j = n % nz;
	int i = n / nz;

	// East neighbor
	if (j < nz - 1) {
		val -= coeff.AE[n] * xOld[n + 1];
	}

	// West neighbor
	if (j > 0) {
		val -= coeff.AW[n] * xOld[n - 1];
	}

	// North neighbor
	if (i < nr - 1) {
		val -= coeff.AN[n] * xOld[n + nz];
	}

	// South neighbor
	if (i > 0) {
		val -= coeff.AS[n] * xOld[n - nz];
	}

	xNew[n] = val / AC;
}

__global__
void gaussSeidelRB(Coefficients coeff, uint8_t* active, double* x, int color) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (active && !active[n]) {
		return;
	}

	int nr = coeff.nr;
	int nz = coeff.nz;

	int j = n % nz;
	int i = n / nz;

	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* b = coeff.b;

	if ((i + j) % 2 != color) return;

	double val = b[n];

	if (j != nz - 1) {
		val -= AE[n] * x[n + 1];
	}

	if (j != 0) {
		val -= AW[n] * x[n - 1];
	}

	if (i != nr - 1) {
		val -= AN[n] * x[n + nz];
	}

	if (i != 0) {
		val -= AS[n] * x[n - nz];
	}

	val /= AC[n];

	x[n] = val;
}

void solveLinearSystem(
	Coefficients& coeff,
	const ConfigSolver& config,
	cudaStream_t stream,
	double*& x,
	double*& xTemp,
	uint8_t*& active,
	int threadsPerBlock
) {

	int N = coeff.N;
	int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
	LinearSolverType type = coeff.useFaceCoeffs ? LINEAR_JACOBI : config.type;

	switch (type) {
	case LINEAR_JACOBI:
		for (int k = 0; k < config.maxIter; k++) {
			jacobi << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, xTemp, active);
			std::swap(x, xTemp);
		}
		break;

	case LINEAR_GS_RB:
		for (int k = 0; k < config.maxIter; k++) {
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, active, x, 0);
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, active, x, 1);
		}
		break;
	}
}

