#include "residuals.cuh"

#include <math_constants.h>

#include "device_launch_parameters.h"

__device__
void residualRaw(uint8_t* activeCell, bool sign, ResidualPairs& pairs, int n) {

	Coefficients coeff = pairs.coeff;
	const double* x = pairs.x;

	if (n >= coeff.N) return;

	if (!activeCell[n]) {
		if (coeff.res) {
			coeff.res[n] = 0.0;
		}
		return;
	}

	int nr = coeff.nr;
	int nz = coeff.nz;

	int j = n % nz;
	int i = n / nz;

	double Ax = coeff.AC[n] * x[n];

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

	coeff.res[n] = sign ? r : fabs(r);

}


__global__
void continuityResidual(FVMeshDevice mesh, ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) {
		coeff.res[n] = 0.0;
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

	coeff.res[n] = imbalance;

}

void residualL1Host(Coefficients& coeff) {

	std::vector<double> h_vec(coeff.N);

	cudaMemcpy(h_vec.data(), coeff.res, coeff.N * sizeof(double), cudaMemcpyDeviceToHost);

	double sum = 0.0;

	for (double& x : h_vec) {
		sum += std::abs(x);
	}

	coeff.resVal = sum;
}


void residualL2Host(Coefficients& coeff) {

	std::vector<double> h_vec(coeff.N);

	cudaMemcpy(h_vec.data(), coeff.res, coeff.N * sizeof(double), cudaMemcpyDeviceToHost);

	double sum = 0.0;

	for (double& x : h_vec) {
		sum += x * x;
	}

	coeff.resVal = sqrt(sum);
}

// get maximum absolute value of a residual vector
void residualLInfHost(Coefficients& coeff) {

	std::vector<double> h_vec(coeff.N);

	cudaMemcpy(h_vec.data(), coeff.res, coeff.N * sizeof(double), cudaMemcpyDeviceToHost);

	for (double& x : h_vec) {
		x = std::abs(x);
	}

	coeff.resVal = *std::max_element(h_vec.begin(), h_vec.end());
}

