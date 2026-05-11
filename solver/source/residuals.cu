#include "residuals.cuh"

#include <math_constants.h>

#include "device_launch_parameters.h"
#include "solver_struct.h"


__device__
void residualRaw(ResidualPairs& pairs, int n){

	Coefficients& coeff = pairs.coeff;
	const double* x = pairs.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	double* b = coeff.b;
	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

	int nr = coeff.nr;
	int nz = coeff.nz;
	int j = n % nz;
	int i = n / nz;

	double* res = coeff.res;

	double Ax = coeff.AC[n] * x[n];

	if (j != nz - 1) {
		Ax += coeff.AE[n] * x[n + 1];
	}

	if (j != 0) {
		Ax += coeff.AW[n] * x[n - 1];
	}

	if (i != nr - 1) {
		Ax += coeff.AN[n] * x[n + nz];
	}

	if (i != 0) {
		Ax += coeff.AS[n] * x[n - nz];
	}

	res[n] = fabs(coeff.b[n] - Ax);

}

__global__
void continuityResidual(ConfigSolver config, Coefficients coeff, VariablesSimple simple, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nz = g.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double rho = f.rho;
	double* u = simple.u;
	double* v = simple.v;
	double* res = coeff.res;

	int i = n / nz;

	double r1 = r[i] - (dr / 2);
	double r2 = r[i] + (dr / 2);

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

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

