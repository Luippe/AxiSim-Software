#include "residuals.cuh"

#include <math_constants.h>

#include "device_launch_parameters.h"
#include "solver_struct.h"


__device__
void residualRaw(ResidualPairs& pairs, int n) {

	FVMeshDevice fvMesh = pairs.fvMesh;
	Coefficients coeff = pairs.coeff;
	const double* x = pairs.x;

	if (n >= fvMesh.cells.nCells) return;

	if (!fvMesh.cells.active[n]) {
		if (coeff.res) {
			coeff.res[n] = 0.0;
		}
		return;
	}

	int nr = fvMesh.nr;
	int nz = fvMesh.nz;

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

	coeff.res[n] = fabs(coeff.b[n] - Ax);
}

__global__
void continuityResidual(ConfigSolver config, Coefficients coeff, VariablesSimple simple, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) return;
	//if (!coeff.activeCell[n]) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nz = g.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* rFace = g.d_rFace;
	double rho = f.rho;
	double* u = simple.u;
	double* v = simple.v;
	double* res = coeff.res;

	int i = n / nz;
	int j = n % nz;

	double r1 = rFace[i];
	double r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz[j];
	double Ar1 = 2 * CUDART_PI * r1 * dz[j];

	// east
	double me = rho * u[n + i + 1] * Az;

	// west
	double mw = rho * u[n + i] * Az;

	// north
	double mn = rho * v[n + nz] * Ar2;

	// south
	double ms = rho * v[n] * Ar1;

	res[n] = (me - mw + mn - ms);

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

