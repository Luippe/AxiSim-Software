#include "simple.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "solver_util.cuh"

__global__
void getCorrectionCoefficient(ConfigSolver config, Coefficients coeff, VariablesSimple simple, double* D) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = coeff.nr;
	int nz = coeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double* AC = coeff.AC;

	D[n] = 0.0;

	if (coeff.active[n]) return;

	int j = n % nz;
	int i = n / nz;

	double r1 = 0.0;
	double r2 = 0.0;

	if (isStoredAxial(coeff.storeType)) {
		r1 = r[i] - (dr / 2);
		r2 = r[i] + (dr / 2);
	}
	else if (isStoredRadial(coeff.storeType)) {
		r1 = r[i - 1];
		r2 = r[i];
	}

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	D[n] = Az * dz * simple.momentumRelaxation / AC[n];

	//if (isStoredAxial(coeff.storeType)) {
	//	if (j == 0) {
	//		D[n] = 0.0;
	//		return;
	//	}
	//}

	//if (isStoredRadial(coeff.storeType)) {
	//	if (i == 0 || i == g.nr) {
	//		D[n] = 0.0;
	//		return;
	//	}
	//}
	//if (!isfinite(AC[n]) || fabs(AC[n]) < 1e-30) {
	//	printf("Bad V AC: n=%d i=%d j=%d AC=%e\n", n, i, j, AC[n]);
	//	D[n] = 0.0;
	//	return;
	//}

	//if (!isfinite(AC[n]) || fabs(AC[n]) < 1e-30) {
	//	printf(
	//		"Bad AC: storeType=%d n=%d i=%d j=%d nz=%d AC=%e\n",
	//		coeff.storeType, n, i, j, nz, AC[n]
	//	);
	//	D[n] = 0.0;
	//	return;
	//}

}

__global__
void createURhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;

	int nz = g.nz;
	double dr = g.dr;
	double* r = g.r;

	double* b = coeff.b;
	double* p = simple.p;

	int j = n % (nz + 1);
	int i = n / (nz + 1);

	if (coeff.active[n]) return;

	double r1 = r[i] - (dr / 2);
	double r2 = r[i] + (dr / 2);
	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	// outlet
	if (j == nz) {
		b[n] += -Az * (-2 * p[n - i - 1]);
	}
	else {
		b[n] += -Az * (p[n - i] - p[n - i - 1]);
	}
}


__global__
void createVRhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;

	int nr = g.nr;
	int nz = g.nz;
	double dz = g.dz;
	double* r = g.r;
	double* b = coeff.b;
	double* p = simple.p;

	int i = n / nz;

	if (coeff.active[n]) return;

	double r1 = r[i - 1];
	double r2 = r[i];
	double Ar = 2 * CUDART_PI * (0.5 * (r1 + r2)) * dz;

	b[n] += -Ar * (p[n] - p[n - nz]);
}

__global__
void createPPCoeff(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = g.nr;
	int nz = g.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* DU = simple.DU;
	double* DV = simple.DV;

	int j = n % nz;
	int i = n / nz;

	AE[n] = 0.0;
	AW[n] = 0.0;
	AN[n] = 0.0;
	AS[n] = 0.0;
	AC[n] = 0.0;

	if (coeff.active[n]) return;

	double r1 = r[i] - (dr / 2);
	double r2 = r[i] + (dr / 2);

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

	// east
	if (j == nz - 1) {
		AC[n] += (2 * rho * DU[n + i + 1] * Az / dz);
	}
	else {
		AE[n] = -rho * DU[n + i + 1] * Az / dz;
	}

	// west
	AW[n] = -rho * DU[n + i] * Az / dz;

	// north
	AN[n] = -rho * DV[n + nz] * Ar2 / dr;

	// south
	AS[n] = -rho * DV[n] * Ar1 / dr;

	AC[n] += -(AE[n] + AW[n] + AN[n] + AS[n]);

}

__global__
void createPPRhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nz = g.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double rho = f.rho;
	double* b = coeff.b;
	double* pp = simple.pp;
	double* u = simple.u;
	double* v = simple.v;

	int i = n / nz;

	b[n] = 0.0;
	pp[n] = 0.0;

	if (coeff.active[n]) return;

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

	b[n] = -(me - mw + mn - ms);

}

__global__
void updateUVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (coeff.active[n]) return;

	const GridConfig& g = config.g;

	int nz = coeff.nz;
	double dz = g.dz;

	double* u = simple.u;
	double* pp = simple.pp;
	double* DU = simple.DU;

	int j = n % nz;
	int i = n / nz;

	if (j == nz - 1) {
		u[n] -= (DU[n] / dz) * (-2.0 * pp[n - i - 1]);
	}
	else {
		u[n] -= (DU[n] / dz) * (pp[n - i] - pp[n - i - 1]);
	}
}

__global__
void updateVVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (coeff.active[n]) return;

	const GridConfig& g = config.g;

	int nr = g.nr;
	int nz = g.nz;
	double dr = g.dr;

	double* v = simple.v;
	double* pp = simple.pp;
	double* DV = simple.DV;

	int i = n / nz;

	v[n] -= (DV[n] / dr) * (pp[n] - pp[n - nz]);
}

__global__
void updatePressure(Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (coeff.active[n]) return;

	double* pp = simple.pp;
	double* p = simple.p;
	double pressureRelaxation = simple.pressureRelaxation;

	p[n] += pressureRelaxation * pp[n];
}

