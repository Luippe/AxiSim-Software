#include "linear_solver.cuh"
#include "device_launch_parameters.h"
#include <utility>

void solveLinearSystem(Coefficients& coeff, const LinearSolverConfig& config, cudaStream_t stream, double*& x, double*& xTemp, int threadsPerBlock, double relaxation) {

	int blocks = (coeff.N + threadsPerBlock - 1) / threadsPerBlock;

	switch (config.type) {
	case LINEAR_JACOBI:

		if (relaxation == 1.0) {
			for (int k = 0; k < config.maxIter; k++) {
				jacobiPP << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, xTemp, relaxation);
				std::swap(x, xTemp);
			}
		}
		else {
			for (int k = 0; k < config.maxIter; k++) {
				jacobi << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, xTemp, relaxation);
				std::swap(x, xTemp);
			}
		}
		break;

	case LINEAR_GS_RB:
		for (int k = 0; k < config.maxIter; k++) {
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, relaxation, 0);
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, relaxation, 1);
		}
		break;
	}
}

__global__
void jacobi(Coefficients coeff, double* xOld, double* xNew, double relaxation)  {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	int nr = coeff.nr;
	int nz = coeff.nz;
	int j = n % nz;
	int i = n / nz;

	double* b = coeff.b;
	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

	double val = b[n];

	val += ((1 - relaxation) / (relaxation)) * AC[n] * xOld[n];

	if (j != nz - 1) {
		val -= AE[n] * xOld[n + 1];
	}

	if (j != 0) {
		val -= AW[n] * xOld[n - 1];
	}

	if (i != nr - 1) {
		val -= AN[n] * xOld[n + nz];
	}

	if (i != 0) {
		val -= AS[n] * xOld[n - nz];
	}

	val *= (relaxation / AC[n]);

	xNew[n] = val;

}

__global__
void gaussSeidelRB(Coefficients coeff, double* x, double relaxation, int color) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

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

	val += ((1 - relaxation) / (relaxation)) * AC[n] * x[n];

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

	val *= (relaxation / AC[n]);

	x[n] = val;
}

__global__
void jacobiPP(Coefficients coeff, double* xOld, double* xNew, double relaxation) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	int nr = coeff.nr;
	int nz = coeff.nz;
	int j = n % nz;
	int i = n / nz;

	double* b = coeff.b;
	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

	double val = b[n];

	if (j != nz - 1) {
		val -= AE[n] * xOld[n + 1];
	}

	if (j != 0) {
		val -= AW[n] * xOld[n - 1];
	}

	if (i != nr - 1) {
		val -= AN[n] * xOld[n + nz];
	}

	if (i != 0) {
		val -= AS[n] * xOld[n - nz];
	}

	val = (val - AC[n] * xOld[n]) / AC[n];

	xNew[n] += relaxation * val;

}