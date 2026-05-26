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
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* AC = coeff.AC;

	D[n] = 0.0;

	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	int j = n % nz;
	int i = n / nz;

	double r1 = 0.0;
	double r2 = 0.0;

	if (isStoredAxial(coeff.storeType)) {
		r1 = r[i] - (dr[i] / 2.0);
		r2 = r[i] + (dr[i] / 2.0);
	}
	else if (isStoredRadial(coeff.storeType)) {
		r1 = r[i - 1];
		r2 = r[i];
	}

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	D[n] = Az * dz[j] / AC[n];
}

__global__
void createURhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nz = coeff.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* rFace = g.d_rFace;

	double* b = coeff.b;
	double* p = simple.p;
	double* u = simple.u;
	double mu = f.mu;

	int j = n % nz;
	int i = n / nz;

	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	double r1 = rFace[i];
	double r2 = rFace[i + 1];
	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	// outlet
	if (j == nz - 1) {
		b[n] += -Az * (-2.0 * p[n - i - 1]);
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

	int nr = coeff.nr;
	int nz = coeff.nz;
	double* dz = g.d_dz;
	double* dr = g.d_dr;
	double* r = g.d_r;
	double* b = coeff.b;
	double* p = simple.p;



	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	int i = n / nz;
	int j = n % nz;

	double r1 = r[i - 1];
	double r2 = r[i];
	double Ar = 2.0 * CUDART_PI * (0.5 * (r1 + r2)) * dz[j];
	b[n] += -Ar * (p[n] - p[n - nz]);

}

__global__
void createPPCoeff(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = coeff.nr;
	int nz = coeff.nz;
	double* dz = g.d_dz;
	double* dr = g.d_dr;
	double* r = g.d_r;
	double* rFace = g.d_rFace;
	double* r_dr = g.r_dr;
	double* z_dz = g.z_dz;

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

	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	double r1 = rFace[i];
	double r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2.0 * CUDART_PI * r2 * dz[j];
	double Ar1 = 2.0 * CUDART_PI * r1 * dz[j];

	// east
	if (j == nz - 1) {
		AC[n] += (2.0 * rho * DU[n + i + 1] * Az / z_dz[j + 1]);
	}
	else {
		AE[n] = -rho * DU[n + i + 1] * Az / z_dz[j + 1];
	}

	// west
	AW[n] = -rho * DU[n + i] * Az / z_dz[j];

	// north
	AN[n] = -rho * DV[n + nz] * Ar2 / r_dr[i + 1];

	// south
	AS[n] = -rho * DV[n] * Ar1 / r_dr[i];

}

__global__
void createPPRhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nz = g.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* rFace = g.d_rFace;
	double rho = f.rho;
	double* b = coeff.b;
	double* pp = simple.pp;
	double* ppTemp = simple.ppTemp;
	double* u = simple.u;
	double* v = simple.v;

	int i = n / nz;
	int j = n % nz;

	pp[n] = 0.0;
	ppTemp[n] = 0.0;

	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	double r1 = rFace[i];
	double r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2.0 * CUDART_PI * r2 * dz[j];
	double Ar1 = 2.0 * CUDART_PI * r1 * dz[j];

	// east
	double me = rho * u[n + i + 1] * Az;

	// west
	double mw = rho * u[n + i] * Az;

	// north
	double mn = rho * v[n + nz] * Ar2;

	// south
	double ms = rho * v[n] * Ar1;

	b[n] += -(me - mw + mn - ms);

}

__global__
void updateUVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	const GridConfig& g = config.g;

	int nz = coeff.nz;
	double* dz = g.d_dz;

	double* u = simple.u;
	double* pp = simple.pp;
	double* DU = simple.DU;

	int j = n % nz;
	int i = n / nz;


	if (j == nz - 1) {
		double dzHalf = 0.5 * dz[j - 1];
		u[n] -= (DU[n] / dzHalf) * (0.0 - pp[n - i - 1]);
	}
	else {
		double dzCenter = 0.5 * (dz[j] + dz[j + 1]);
		u[n] -= (DU[n] / dzCenter) * (pp[n - i] - pp[n - i - 1]);
	}
}

__global__
void updateVVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	const GridConfig& g = config.g;

	int nr = coeff.nr;
	int nz = coeff.nz;
	double* dr = g.d_dr;

	double* v = simple.v;
	double* pp = simple.pp;
	double* DV = simple.DV;

	int i = n / nz;

	double drCenter = 0.5 * (dr[i] + dr[i - 1]);
	v[n] -= (DV[n] / drCenter) * (pp[n] - pp[n - nz]);
}

__global__
void updatePressure(Coefficients coeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;

	double* pp = simple.pp;
	double* p = simple.p;
	double pressureRelaxation = simple.pressureRelaxation;

	p[n] += pressureRelaxation * pp[n];
}

__global__
void underRelaxEquation(Coefficients coeff, double* xOld, double alpha) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (!coeff.activeCell[n] || !coeff.activeBC[n]) return;


	double AC_old = coeff.AC[n];

	coeff.AC[n] = AC_old / alpha;
	coeff.b[n] += ((1.0 - alpha) / alpha) * AC_old * xOld[n];
}