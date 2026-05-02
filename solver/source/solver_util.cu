#include "solver_util.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>

__device__
bool isStoredCenter(CellStoreType& storeType) {
	return storeType == CellStoreType::CENTER;
}

__device__
bool isStoredAxial(CellStoreType& storeType) {
	return storeType == CellStoreType::AXIAL;
}

__device__
bool isStoredRadial(CellStoreType& storeType) {
	return storeType == CellStoreType::RADIAL;
}

__device__
bool isBCDirichlet(BCType& type) {
	return type == BCType::DIRICHLET;
}

__device__
bool isBCNeumann(BCType& type) {
	return type == BCType::NEUMANN;
}

__global__
void copyVector(double* vec1, double* vec2, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) return;

	vec1[n] = vec2[n];
}

__global__
void createCoefficients(Config config, Coefficients coeff, BoundaryConditionConfig bc) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = coeff.nr;
	int nz = coeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double mu = f.mu;

	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* b = coeff.b;
	int* cell = coeff.active;
	int j = n % nz;
	int i = n / nz;

	AE[n] = 0.0;
	AW[n] = 0.0;
	AN[n] = 0.0;
	AS[n] = 0.0;
	AC[n] = 0.0;
	b[n] = 0.0;

	if (cell[n]) return;

	double r1 = 0.0;
	double r2 = 0.0;

	if (isStoredCenter(coeff.storeType) || isStoredAxial(coeff.storeType)) {
		r1 = r[i] - (dr / 2);
		r2 = r[i] + (dr / 2);
	}
	else {
		r1 = r[i - 1];
		r2 = r[i];
	}

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

	// east
	if (j == nz - 1) {
		AE[n] = 0.0;
		if (isStoredCenter(coeff.storeType) || isStoredRadial(coeff.storeType)) {
			if (isBCDirichlet(bc.outlet.type)) {
				double K = mu * Az / (0.5 * dz);
				AC[n] += K;
				b[n] += K * bc.outlet.val;
			}
		}
	}
	else if (cell[n + 1] == 1) {
		AE[n] = 0.0;
		AC[n] += (mu * Az / dz);
	}
	else {
		AE[n] = -(mu * Az / dz);
	}

	// west
	if (j == 0) {
		AW[n] = 0.0;
		if (isStoredCenter(coeff.storeType) || isStoredRadial(coeff.storeType)) {
			if (isBCDirichlet(bc.inlet.type)) {
				double K = mu * Az / (0.5 * dz);
				AC[n] += K;
				b[n] += K * bc.inlet.val;
			}
		}
	}
	else if (cell[n - 1] == 1) {
		AW[n] = 0.0;
		AC[n] += (mu * Az / dz);
	}
	else {
		AW[n] = -(mu * Az / dz);
	}

	// north
	if (i == nr - 1) {
		AN[n] = 0.0;
		if (isStoredCenter(coeff.storeType) || isStoredAxial(coeff.storeType)) {
			if (isBCDirichlet(bc.outer.type)) {
				double K = mu * Ar2 / (0.5 * dr);
				AC[n] += K;
				b[n] += K * bc.outer.val;
			}
		}
	}
	else if (cell[n + nz] == 1) {
		AN[n] = 0.0;
		AC[n] += (mu * Ar2 / (0.5 * dr));
	}
	else {
		AN[n] = -(mu * Ar2 / dr);
	}

	// south
	if (i == 0) {
		AS[n] = 0.0;
		if (isStoredCenter(coeff.storeType) || isStoredAxial(coeff.storeType)) {
			if (isBCDirichlet(bc.centerline.type)) {
				double K = mu * Ar1 / (0.5 * dr);
				AC[n] += K;
				b[n] += K * bc.centerline.val;
			}
		}
	}
	else if (cell[n - nz] == 1) {
		AS[n] = 0.0;
		AC[n] += (mu * Ar1 / (0.5 * dr));
	}
	else {
		AS[n] = -(mu * Ar1 / dr);
	}

	AC[n] += -(AE[n] + AW[n] + AN[n] + AS[n]);

}