#include "simple.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "solver_util.cuh"


__global__
void createPPCoeff(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	double rho = config.f.rho;

	double* AC = coeff.AC;

	int nz = mesh.nz;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		double area = mesh.faces.area[faceID];

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, n, faceID, normalZ, normalR);

		double dPF = getDistanceCellToFace(mesh, n, faceID, normalZ, normalR);

		if (dPF <= 1.0e-30) continue;

		double Df = interpolateNormalCorrectionCoeffToFace(
			n,
			faceID,
			mesh,
			simple
		);

		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			double dPN = getDistanceCellToCell(mesh, n, nb, normalZ, normalR);

			if (dPN <= 1.0e-30) continue;

			double K = rho * area * Df / dPN;

			// Style A pressure-correction matrix assembly
			AC[n] += K;

			addStructuredNeighborCoeff(n, nb, nz, -K, coeff);

		}

		// ------------------------------------------------------------
		// Boundary face
		// ------------------------------------------------------------
		else {

			int groupID = mesh.faces.boundaryGroupID[faceID];

			if (groupID < 0 || groupID >= pBC.nGroups) {
				// Default pressure-correction BC:
				// zero-gradient p', so no matrix contribution.
				continue;
			}

			uint8_t bcType = pBC.typeByGroup[groupID];

			if (isDirichletType(bcType)) {
				// Fixed pressure boundary:
				// p_boundary is fixed, so p'_boundary = 0.
				//
				// This adds K * pP' to the equation.
				double K = rho * area * Df / dPF;

				AC[n] += K;

				// No b contribution because p'_boundary = 0.
			}
			else if (isNeumannType(bcType)) {
				// Zero-gradient pressure correction:
				// no coefficient contribution.
				continue;
			}
		}
	}
}



__global__
void computePressureGradient(
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* p,
	double* gradPZ,
	double* gradPR
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	gradPZ[n] = 0.0;
	gradPR[n] = 0.0;

	if (!mesh.cells.active[n]) return;

	phiGradientCell(
		n,
		mesh,
		pBC,
		p,
		gradPZ[n],
		gradPR[n]
	);
}

__global__
void createPPRhs(
	FVMeshDevice mesh,
	Coefficients ppCoeff,
	VariablesSimple simple
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	ppCoeff.b[n] = 0.0;
	simple.pp[n] = 0.0;
	simple.ppTemp[n] = 0.0;

	if (!mesh.cells.active[n]) return;

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

	ppCoeff.b[n] = -imbalance;
}

__global__
void createMomentumPressureRhs(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	double gradPZ = 0.0;
	double gradPR = 0.0;

	phiGradientCell(
		n,
		mesh,
		pBC,
		simple.p,
		gradPZ,
		gradPR
	);

	double volume = mesh.cells.volume[n];

	uCoeff.b[n] += -volume * gradPZ;
	vCoeff.b[n] += -volume * gradPR;
}

__global__
void updateVelocity(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	double* u = simple.u;
	double* v = simple.v;
	double* pp = simple.pp;

	double* DU = simple.DU;
	double* DV = simple.DV;

	double gradPP_Z = 0.0;
	double gradPP_R = 0.0;

	phiGradientCell(
		n,
		mesh,
		pBC,
		pp,
		gradPP_Z,
		gradPP_R
	);
	
	u[n] -= DU[n] * gradPP_Z;
	v[n] -= DV[n] * gradPP_R;
	//if (n == 0) {
	//	printf("%f\n", u[0]);
	//}
}


__global__
void updatePressure(
	FVMeshDevice mesh,
	VariablesSimple simple
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	double* pp = simple.pp;
	double* p = simple.p;

	double pressureRelaxation = simple.pressureRelaxation;

	p[n] += pressureRelaxation * pp[n];

}

__global__
void updateMassFlux(
	ConfigSolver config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int f = blockIdx.x * blockDim.x + threadIdx.x;

	if (f >= mesh.faces.nFaces) return;

	int owner = mesh.faces.owner[f];
	int neighbor = mesh.faces.neighbor[f];

	if (owner < 0) return;

	double area = mesh.faces.area[f];

	if (area <= 1.0e-30) {
		simple.mDot[f] = 0.0;
		return;
	}

	double rho = config.f.rho;

	double normalZ = mesh.faces.normalZ[f];
	double normalR = mesh.faces.normalR[f];

	double Df = interpolateNormalCorrectionCoeffToFace(
		owner,
		f,
		mesh,
		simple
	);

	if (neighbor >= 0) {

		double dPN = getDistanceCellToCell(mesh, owner, neighbor, normalZ, normalR);


		if (dPN <= 1.0e-30) return;

		double ppP = simple.pp[owner];
		double ppN = simple.pp[neighbor];

		simple.mDot[f] -= rho * area * Df * (ppN - ppP) / dPN;
		return;
	}

	// Boundary face
	int groupID = mesh.faces.boundaryGroupID[f];

	if (groupID < 0 || groupID >= pBC.nGroups) {
		return;
	}

	uint8_t pType = pBC.typeByGroup[groupID];

	if (isDirichletType(pType)) {
		// fixed pressure boundary -> p'_b = 0
		double dPB = getDistanceCellToFace(mesh, owner, f, normalZ, normalR);

		if (dPB <= 1.0e-30) return;

		double ppB = 0.0;
		double ppP = simple.pp[owner];

		simple.mDot[f] -= rho * area * Df * (ppB - ppP) / dPB;
	}
	else {
		// velocity-specified wall/inlet/symmetry/mass-flux boundary:
		// m'_b = 0, so do not correct boundary mass flux.
		return;
	}
}

__global__
void underRelaxEquation(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* x,
	double alpha
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	if (alpha <= 0.0 || alpha > 1.0) return;

	double AC_old = coeff.AC[n];

	if (fabs(AC_old) < 1.0e-30) return;

	coeff.AC[n] = AC_old / alpha;

	coeff.b[n] += ((1.0 - alpha) / alpha) * AC_old * x[n];
}