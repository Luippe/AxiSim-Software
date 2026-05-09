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
void addUDiffusionCoefficient(ConfigSolver config, Coefficients coeff, BoundaryConditionConfig bc) {

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

	r1 = r[i] - (dr / 2);
	r2 = r[i] + (dr / 2);

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

	// ----------ADD Diffusion Term--------------
	// east
	if (j == nz - 1) {
		AE[n] = 0.0;
		if (isBCDirichlet(bc.outlet.type)) {
			double K = mu * Az / (0.5 * dz);
			AC[n] += K;
			b[n] += K * bc.outlet.val;
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
		if (isBCDirichlet(bc.inlet.type)) {
			double K = mu * Az / (0.5 * dz);
			AC[n] += K;
			b[n] += K * bc.inlet.val;
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
		if (isBCDirichlet(bc.outer.type)) {
			double K = mu * Ar2 / (0.5 * dr);
			AC[n] += K;
			b[n] += K * bc.outer.val;
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
		if (isBCDirichlet(bc.centerline.type)) {
			double K = mu * Ar1 / (0.5 * dr);
			AC[n] += K;
			b[n] += K * bc.centerline.val;
		}
	}
	else if (cell[n - nz] == 1) {
		AS[n] = 0.0;
		AC[n] += (mu * Ar1 / (0.5 * dr));
	}
	else {
		AS[n] = -(mu * Ar1 / dr);
	}

	//AC[n] += -(AE[n] + AW[n] + AN[n] + AS[n]);
}

__global__
void addVDiffusionCoefficient(ConfigSolver config, Coefficients coeff, BoundaryConditionConfig bc) {

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

	r1 = r[i - 1];
	r2 = r[i];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

	// east
	if (j == nz - 1) {
		AE[n] = 0.0;
		if (isBCDirichlet(bc.outlet.type)) {
			double K = mu * Az / (0.5 * dz);
			AC[n] += K;
			b[n] += K * bc.outlet.val;
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
		if (isBCDirichlet(bc.inlet.type)) {
			double K = mu * Az / (0.5 * dz);
			AC[n] += K;
			b[n] += K * bc.inlet.val;
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
		if (isBCDirichlet(bc.outer.type)) {
			double K = mu * Ar2 / (0.5 * dr);
			AC[n] += K;
			b[n] += K * bc.outer.val;
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
		if (isBCDirichlet(bc.centerline.type)) {
			double K = mu * Ar1 / (0.5 * dr);
			AC[n] += K;
			b[n] += K * bc.centerline.val;
		}
	}
	else if (cell[n - nz] == 1) {
		AS[n] = 0.0;
		AC[n] += (mu * Ar1 / (0.5 * dr));
	}
	else {
		AS[n] = -(mu * Ar1 / dr);
	}


	//AC[n] += -(AE[n] + AW[n] + AN[n] + AS[n]);
}

__global__
void addUConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, BoundaryConditionConfig uBC, BoundaryConditionConfig vBC) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= uCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = uCoeff.nr;
	int nz = uCoeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = uCoeff.AC;
	double* AE = uCoeff.AE;
	double* AW = uCoeff.AW;
	double* AN = uCoeff.AN;
	double* AS = uCoeff.AS;
	double* b = uCoeff.b;
	int* cell = uCoeff.active;
	int j = n % nz;
	int i = n / nz;

	if (cell[n]) return;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i] - (dr / 2);
	r2 = r[i] + (dr / 2);

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2.0 * CUDART_PI * r2 * dz;
	double Ar1 = 2.0 * CUDART_PI * r1 * dz;

	double ue = 0.0;
	double uw = 0.0;
	double vn = 0.0;
	double vs = 0.0;

	// ----------ADD Convection Term--------------
	// east
	if (j == nz - 1) {
		ue = u[n];	// not correct but just for test
	}
	else {
		ue = 0.5 * (u[n] + u[n + 1]);
	}

	// west
	if (j == 0) {
		uw = u[n];	// not correct but just for test
	}
	else {
		uw = 0.5 * (u[n] + u[n - 1]);
	}

	// north
	if (j == nz - 1) {
		int vN2 = (i + 1) * g.nz + j - 1;
		vn = v[vN2];
	}
	else if (j == 0) {
		int vN1 = (i + 1) * g.nz + j;
		vn = v[vN1];
	}
	else {
		int vN1 = (i + 1) * g.nz + j;
		int vN2 = vN1 - 1;
		vn = 0.5 * (v[vN1] + v[vN2]);
	}

	// south
	if (j == nz - 1) {
		int vS2 = i * g.nz + j - 1;
		vs = v[vS2];
	}
	else if (j == 0) {
		int vS1 = i * g.nz + j;
		vs = v[vS1];
	}
	else {
		int vS1 = i * g.nz + j;
		int vS2 = vS1 - 1;
		vs = 0.5 * (v[vS1] + v[vS2]);
	}

	double Fe = rho * ue * Az;
	double Fw = rho * uw * Az;
	double Fn = rho * vn * Ar2;
	double Fs = rho * vs * Ar1;

	AE[n] += -fmax(-Fe, 0.0);
	AW[n] += -fmax(Fw, 0.0);
	AN[n] += -fmax(-Fn, 0.0);
	AS[n] += -fmax(Fs, 0.0);

	AC[n] += (Fe - Fw + Fn - Fs);

}

__global__
void addVConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, BoundaryConditionConfig uBC, BoundaryConditionConfig vBC) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= vCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = vCoeff.nr;
	int nz = vCoeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = vCoeff.AC;
	double* AE = vCoeff.AE;
	double* AW = vCoeff.AW;
	double* AN = vCoeff.AN;
	double* AS = vCoeff.AS;
	double* b = vCoeff.b;
	int* cell = vCoeff.active;
	int j = n % nz;
	int i = n / nz;

	if (cell[n]) return;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i - 1];
	r2 = r[i];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz;
	double Ar1 = 2 * CUDART_PI * r1 * dz;

	double ue = 0.0;
	double uw = 0.0;
	double vn = 0.0;
	double vs = 0.0;

	// ----------ADD Convection Term--------------
	// east
	if (i == 0) {

		int uE2 = i * (g.nz + 1) + j + 1;
		ue = u[uE2];
	}
	else if (i == nr - 1) {
		int uE1 = (i - 1) * (g.nz + 1) + j + 1;
		ue = u[uE1];
	}
	else {
		int uE2 = i * (g.nz + 1) + j + 1;
		int uE1 = (i - 1) * (g.nz + 1) + j + 1;
		ue = 0.5 * (u[uE1] + u[uE2]);
	}

	// west
	if (i == 0) {
		int uW2 = i * (g.nz + 1) + j;
		uw = u[uW2];
	}
	else if (i == nr - 1) {
		int uW1 = (i - 1) * (g.nz + 1) + j;
		uw = u[uW1];
	}
	else {
		int uW2 = i * (g.nz + 1) + j;
		int uW1 = (i - 1) * (g.nz + 1) + j;
		uw = 0.5 * (u[uW1] + u[uW2]);
	}

	// north
	vn = 0.5 * (v[n] + v[n + nz]);

	// south
	vs = 0.5 * (v[n] + v[n - nz]);

	double Fe = rho * ue * Az;
	double Fw = rho * uw * Az;
	double Fn = rho * vn * Ar2;
	double Fs = rho * vs * Ar1;

	AE[n] += -fmax(-Fe, 0.0);
	AW[n] += -fmax(Fw, 0.0);
	AN[n] += -fmax(-Fn, 0.0);
	AS[n] += -fmax(Fs, 0.0);

	AC[n] += (Fe - Fw + Fn - Fs);
}

__global__
void addUTransientCoefficient(ConfigSolver config, Coefficients uCoeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= uCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = uCoeff.nr;
	int nz = uCoeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double rho = f.rho;

	double* AC = uCoeff.AC;
	double* b = uCoeff.b;
	int* cell = uCoeff.active;
	double* uOld = simple.uOld;

	int j = n % nz;
	int i = n / nz;

	if (cell[n]) return;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i] - (dr / 2);
	r2 = r[i] + (dr / 2);

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	AC[n] += (rho * Az * dz) / config.dt;
	b[n] += (rho * Az * dz * uOld[n]) / config.dt;
}

__global__
void addVTransientCoefficient(ConfigSolver config, Coefficients vCoeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= vCoeff.N) return;
	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = vCoeff.nr;
	int nz = vCoeff.nz;
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = vCoeff.AC;
	double* b = vCoeff.b;
	int* cell = vCoeff.active;
	double* vOld = simple.vOld;

	int j = n % nz;
	int i = n / nz;

	if (cell[n]) return;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i - 1];
	r2 = r[i];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	AC[n] += (rho * Az * dz) / config.dt;
	b[n] += (rho * Az * dz * vOld[n]) / config.dt;
}

__global__
void finalizeCoefficients(Coefficients coeff) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* AC = coeff.AC;

	int* cell = coeff.active;
	if (cell[n]) return;

	AC[n] += -(AE[n] + AW[n] + AN[n] + AS[n]);

}

