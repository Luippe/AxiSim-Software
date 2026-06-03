#include "solver_util.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "printer.h"

// ==============================================================
// ==================HELPER FUNCTIONS============================
// ==============================================================

__device__
bool isDirichletType(uint8_t type) {
	return type == (uint8_t)(DIRICHLET);
}

__device__
bool isNeumannType(uint8_t type) {
	return type == (uint8_t)(NEUMANN);
}

__device__
bool isFullyDeveloped(uint8_t type) {
	return type == (uint8_t)(FULLY_DEVELOPED);
}

__global__
void copyVector(double* vec1, double* vec2, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) return;

	vec1[n] = vec2[n];
}

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC) {
	double gC = dFf / dFC;
	return phiC * gC + (1 - gC) * phiF;
}

__global__
void applyOuterNeumannV(double* v, int nr, int nz) {

	int j = blockIdx.x * blockDim.x + threadIdx.x;
	if (j >= nz) return;

	int boundary = (nr - 1) * nz + j;
	int inside = (nr - 2) * nz + j;

	v[boundary] = v[inside];
}

__device__
void imposeDirichletRow(
	Coefficients coeff,
	int n,
	double value
) {
	coeff.AE[n] = 0.0;
	coeff.AW[n] = 0.0;
	coeff.AN[n] = 0.0;
	coeff.AS[n] = 0.0;

	coeff.AC[n] = 1.0;
	coeff.b[n] = value;
}


// ==============================================================
// ==================DEFERRED CORRECTION=========================
// ==============================================================
__device__
double centralCorrection(double F, double phiP, double phiNb) {
	double phiUpwind = (F >= 0.0) ? phiP : phiNb;
	double phiCentral = 0.5 * (phiP + phiNb);

	return F * (phiCentral - phiUpwind);
}

__device__
double secondOrderUpwindCorrection(double F, double phiLL, double phiL, double phiR, double phiRR, bool hasLL, bool hasRR) {

	if (F >= 0.0) {
		if (!hasLL) return 0.0;
		return 0.5 * F * (phiL - phiLL);
	}
	else {
		if (!hasRR) return 0.0;
		return 0.5 * F * (phiR - phiRR);
	}
	
}

// ==============================================================
// ==================DIFFUSION TERM==============================
// ==============================================================
__global__
void addDiffusionCoefficient(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	const FluidPropertyConfig& f = config.f;

	double mu = f.mu;

	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* b = coeff.b;

	int nz = mesh.nz;

	double zP = mesh.cells.centerZ[n];
	double rP = mesh.cells.centerR[n];

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		double area = mesh.faces.area[faceID];

		double nzHat = mesh.faces.normalZ[faceID];
		double nrHat = mesh.faces.normalR[faceID];

		// Face normal is stored outward from owner.
		// If this cell is the neighbor, flip the normal so it is outward from n.
		if (neighbor == n) {
			nzHat = -nzHat;
			nrHat = -nrHat;
		}

		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			double zN = mesh.cells.centerZ[nb];
			double rN = mesh.cells.centerR[nb];

			double dzPN = zN - zP;
			double drPN = rN - rP;

			double dPN = fabs(dzPN * nzHat + drPN * nrHat);
			//if (n == 0) {
			//	printf("%f\n", dPN);
			//}
			if (dPN <= 0.0) continue;

			double K = mu * area / dPN;

			// Add diagonal contribution
			AC[n] += K;

			// Add neighbor contribution
			if (nb == n + 1) {
				AE[n] += -K;
			}
			else if (nb == n - 1) {
				AW[n] += -K;
			}
			else if (nb == n + nz) {
				AN[n] += -K;
			}
			else if (nb == n - nz) {
				AS[n] += -K;
			}
		}

		// ------------------------------------------------------------
		// Boundary face
		// ------------------------------------------------------------
		else {

			int groupID = mesh.faces.boundaryGroupID[faceID];

			if (groupID < 0 || groupID >= bc.nGroups) {
				// Unassigned boundary face.
				// Usually you should avoid this by assigning all boundary faces
				// to a boundary group.
				continue;
			}

			uint8_t bcType = bc.typeByGroup[groupID];
			double bcValue = bc.valueByGroup[groupID];

			double zF = mesh.faces.centerZ[faceID];
			double rF = mesh.faces.centerR[faceID];

			double dzPF = zF - zP;
			double drPF = rF - rP;

			double dPF = fabs(dzPF * nzHat + drPF * nrHat);

			if (dPF <= 0.0) continue;

			double K = mu * area / dPF;
			//if (n == 0) {
			//	printf("%f\n", dPF);
			//}
			if (isDirichletType(bcType)) {
				//if (n == 0) {
				//	printf(
				//		"n=%d faceID=%d groupID=%d normal=(%f,%f) area=%e K=%e bcType=%d bcValue=%f\n",
				//		n,
				//		faceID,
				//		groupID,
				//		nzHat,
				//		nrHat,
				//		area,
				//		K,
				//		(int)bcType,
				//		bcValue
				//	);
				//}
				AC[n] += K;
				b[n] += K * bcValue;
			}
			else if (isNeumannType(bcType)) {
				//if (n == 0) {
				//	printf(
				//		"n=%d faceID=%d groupID=%d normal=(%f,%f) area=%e K=%e bcType=%d bcValue=%f\n",
				//		n,
				//		faceID,
				//		groupID,
				//		nzHat,
				//		nrHat,
				//		area,
				//		K,
				//		(int)bcType,
				//		bcValue
				//	);
				//}
				// For zero-gradient Neumann, bcValue = 0, so this adds nothing.
				// If bcValue = du/dn, then this adds prescribed diffusive flux.

				b[n] += mu * area * bcValue;
			}
		}
	}
	//int i = n / nz;
	//int j = n % nz;

	//if (i == 0 && j == 0) {
	//	printf("%e, %e\n", AC[n], b[n]);
	//}
	//if (i == mesh.nr - 1 && j == 0) {
	//	printf("%e, %e\n", AC[n], b[n]);
	//}

}

// ==============================================================
// ==================CONVECTION TERM=============================
// ==============================================================
__global__
void addUConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, ConvectionScheme scheme) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= uCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = uCoeff.nr;
	int nz = uCoeff.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* z = g.d_z;
	double* rFace = g.d_rFace;
	double* zFace = g.d_zFace;
	double* r_dr = g.r_dr;
	double* z_dz = g.z_dz;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = uCoeff.AC;
	double* AE = uCoeff.AE;
	double* AW = uCoeff.AW;
	double* AN = uCoeff.AN;
	double* AS = uCoeff.AS;
	double* b = uCoeff.b;

	int j = n % nz;
	int i = n / nz;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = rFace[i];
	r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2.0 * CUDART_PI * r2 * z_dz[j];
	double Ar1 = 2.0 * CUDART_PI * r1 * z_dz[j];

	double ue = 0.0;
	double uw = 0.0;
	double vn = 0.0;
	double vs = 0.0;

	// ----------Add Convection Term--------------
	// east

	ue = faceValue(u[n], u[n+1], 0.5 * dz[j], dz[j]);


	// west
	if (j == 0) {
		uw = u[n];	// not correct but just for test
	}
	else {
		uw = faceValue(u[n], u[n - 1], 0.5 * dz[j - 1], dz[j - 1]);
	}

	// north
	if (j == nz - 1) {
		int vN1 = (i + 1) * g.nz + j - 1;
		vn = v[vN1];
	}
	else if (j == 0) {
		int vN2 = (i + 1) * g.nz + j;
		vn = v[vN2];
	}
	else {
		int vN2 = (i + 1) * g.nz + j;
		int vN1 = vN2 - 1;
		vn = faceValue(v[vN1], v[vN2], 0.5 * dz[j], z_dz[j]);
	}

	// south
	if (j == nz - 1) {
		int vS1 = i * g.nz + j - 1;
		vs = v[vS1];
	}
	else if (j == 0) {
		int vS2 = i * g.nz + j;
		vs = v[vS2];
	}
	else {
		int vS2 = i * g.nz + j;
		int vS1 = vS2 - 1;
		vs = faceValue(v[vS1], v[vS2], 0.5 * dz[j], z_dz[j]);
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

	// ----------ADD Deferred Correction Term--------------
	switch (scheme) {
	case CONV_UPWIND:
		break;
	case CONV_CENTRAL:
		b[n] -= centralCorrection(Fe, u[n], u[n + 1]);
		b[n] += centralCorrection(Fw, u[n], u[n - 1]);
		b[n] -= centralCorrection(Fn, u[n], u[n + nz]);
		b[n] += centralCorrection(Fs, u[n], u[n - nz]);
		break;
	case CONV_SECOND_ORDER_UPWIND:

		if (j > 0 && j < nz - 2) 		b[n] -= secondOrderUpwindCorrection(Fe, u[n - 1], u[n], u[n + 1], u[n + 2], j > 0, j < nz - 2);
		if (j > 1 && j < nz - 1)		b[n] += secondOrderUpwindCorrection(Fw, u[n - 2], u[n - 1], u[n], u[n + 1], j > 1, j < nz - 1);
		if (i > 0 && i < nr - 2)		b[n] -= secondOrderUpwindCorrection(Fn, u[n - nz], u[n], u[n + nz], u[n + 2 * nz], i > 0, i < nr - 2);
		if (i > 1 && i < nr - 1)		b[n] += secondOrderUpwindCorrection(Fs, u[n - 2 * nz], u[n - nz], u[n], u[n + nz], i > 1, i < nr - 1);
		break;
	}
}

__global__
void addVConvectionCoefficient(ConfigSolver config, Coefficients uCoeff, Coefficients vCoeff, double* u, double* v, ConvectionScheme scheme) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= vCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = vCoeff.nr;
	int nz = vCoeff.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double* z = g.d_z;
	double* rFace = g.d_rFace;
	double* zFace = g.d_zFace;
	double* r_dr = g.r_dr;
	double* z_dz = g.z_dz;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = vCoeff.AC;
	double* AE = vCoeff.AE;
	double* AW = vCoeff.AW;
	double* AN = vCoeff.AN;
	double* AS = vCoeff.AS;
	double* b = vCoeff.b;

	int j = n % nz;
	int i = n / nz;

	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i - 1];
	r2 = r[i];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2 * CUDART_PI * r2 * dz[j];
	double Ar1 = 2 * CUDART_PI * r1 * dz[j];

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
		ue = faceValue(u[uE1], u[uE2], 0.5 * dr[i], r_dr[i]);
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
		uw = faceValue(u[uW1], u[uW2], 0.5 * dr[i], r_dr[i]);
	}

	// north


	// south
	vs = faceValue(v[n], v[n - nz], 0.5 * dr[i - 1], dr[i - 1]);

	double Fe = rho * ue * Az;
	double Fw = rho * uw * Az;
	double Fn = rho * vn * Ar2;
	double Fs = rho * vs * Ar1;

	AE[n] += -fmax(-Fe, 0.0);
	AW[n] += -fmax(Fw, 0.0);
	AN[n] += -fmax(-Fn, 0.0);
	AS[n] += -fmax(Fs, 0.0);

	AC[n] += (Fe - Fw + Fn - Fs);

	// ----------ADD Deferred Correction Term--------------
	switch (scheme) {
	case CONV_UPWIND:

		break;

	case CONV_CENTRAL:

		b[n] -= centralCorrection(Fe, v[n], v[n + 1]);
		b[n] += centralCorrection(Fw, v[n], v[n - 1]);
		b[n] -= centralCorrection(Fn, v[n], v[n + nz]);
		b[n] += centralCorrection(Fs, v[n], v[n - nz]);
		break;

	case CONV_SECOND_ORDER_UPWIND:

		if (j > 0 && j < nz - 2) 		b[n] -= secondOrderUpwindCorrection(Fe, v[n - 1], v[n], v[n + 1], v[n + 2], j > 0, j < nz - 2);
		if (j > 1 && j < nz - 1)		b[n] += secondOrderUpwindCorrection(Fw, v[n - 2], v[n - 1], v[n], v[n + 1], j > 1, j < nz - 1);
		if (i > 0 && i < nr - 2)		b[n] -= secondOrderUpwindCorrection(Fn, v[n - nz], v[n], v[n + nz], v[n + 2 * nz], i > 0, i < nr - 2);
		if (i > 1 && i < nr - 1)		b[n] += secondOrderUpwindCorrection(Fs, v[n - 2 * nz], v[n - nz], v[n], v[n + nz], i > 1, i < nr - 1);
		break;

	}
}


// ==============================================================
// ==================TRANSIENT TERM==============================
// ==============================================================
__global__
void addUTransientCoefficient(ConfigSolver config, Coefficients uCoeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= uCoeff.N) return;

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = uCoeff.nr;
	int nz = uCoeff.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* rFace = g.d_rFace;
	double* z_dz = g.z_dz;
	double* r = g.d_r;
	double rho = f.rho;

	double* AC = uCoeff.AC;
	double* b = uCoeff.b;
	double* uOld = simple.uOld;
	double dt = config.dt;

	int j = n % nz;
	int i = n / nz;

	double r1 = 0.0;
	double r2 = 0.0;


	r1 = rFace[i];
	r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);
	double Ar2 = 2.0 * CUDART_PI * r2 * z_dz[j];
	double Ar1 = 2.0 * CUDART_PI * r1 * z_dz[j];

	AC[n] += (rho * Az * z_dz[j]) / dt;
	b[n] += (rho * Az * z_dz[j] * uOld[n]) / dt;
}

__global__
void addVTransientCoefficient(ConfigSolver config, Coefficients vCoeff, VariablesSimple simple) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= vCoeff.N) return;
	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = vCoeff.nr;
	int nz = vCoeff.nz;
	double* dr = g.d_dr;
	double* dz = g.d_dz;
	double* r = g.d_r;
	double mu = f.mu;
	double rho = f.rho;

	double* AC = vCoeff.AC;
	double* b = vCoeff.b;
	double* vOld = simple.vOld;

	int j = n % nz;
	int i = n / nz;


	double r1 = 0.0;
	double r2 = 0.0;

	r1 = r[i - 1];
	r2 = r[i];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	AC[n] += (rho * Az * dz[j]) / config.dt;
	b[n] += (rho * Az * dz[j] * vOld[n]) / config.dt;
}

__global__
void clearCoefficients(Coefficients coeff) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;

	coeff.AE[n] = 0.0;
	coeff.AW[n] = 0.0;
	coeff.AN[n] = 0.0;
	coeff.AS[n] = 0.0;
	coeff.AC[n] = 0.0;
	coeff.b[n] = 0.0;
	coeff.res[n] = 0.0;

}