#include "simple.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "solver_util.cuh"


__global__
void createPPCoeff(
	Config config,
	FVMeshDevice mesh,
	Coefficients coeff,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	double rho = config.f.rho;

	double* AC = coeff.AC;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int neighbor = mesh.faces.neighbor[faceID];

		double area = mesh.faces.area[faceID];

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, n, faceID, normalZ, normalR);

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

			double K = rho * area * Df * mesh.faces.invCellToCell[faceID];;

			// pressure-correction matrix assembly
			AC[n] += K;
			coeff.AF[k] -= K;

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
				double dPB = mesh.faces.dPB[faceID];
				double K = rho * area * Df / dPB;
				AC[n] += K;

				// No b contribution because p'_boundary = 0.
			}
			else if (isNeumannType(bcType)) {
				continue;
			}
		}
	}
}

__global__
void createPPRhs(
	Config config,
	FVMeshDevice mesh,
	Coefficients ppCoeff,
	VariablesSimple simple,
	int applyNonOrtho
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	ppCoeff.b[n] = 0.0;

	// NOTE: pp / ppTemp are NOT reset here. They are zeroed once before the
	// non-orthogonal corrector loop so that later passes can warm-start from,
	// and take the gradient of, the previous pass's p'.



	double rho = config.f.rho;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	double imbalance = 0.0;
	double crossSum = 0.0;

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

		// Deferred non-orthogonal correction (explicit). grad(p') is held in
		// simple.gradPZ/gradPR; it is zero on the first corrector pass.
		if (applyNonOrtho) {
			double Df = interpolateNormalCorrectionCoeffToFace(n, f, mesh, simple);
			crossSum += nonOrthoScalarDiffusionFlux(n, f, mesh, simple.gradPZ, simple.gradPR, rho * Df);
		}
	}

	ppCoeff.b[n] = -imbalance + crossSum;
}

__global__
void createMomentumPressureRhs(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;


	// grad(p) has been precomputed into simple.gradPZ/gradPR (LSQ), so the
	// momentum body force uses the same gradient as Rhie-Chow instead of an
	// independent Green-Gauss gradient (which is spurious on near-axis cells).
	double volume = mesh.cells.volume[n];

	uCoeff.b[n] += -volume * simple.gradPZ[n];
	vCoeff.b[n] += -volume * simple.gradPR[n];

}

__global__
void updateMomentumPressure(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	simple.u[n] -= simple.DU[n] * simple.gradPZ[n];
	simple.v[n] -= simple.DV[n] * simple.gradPR[n];
	simple.p[n] += simple.pressureRelaxation * simple.pp[n];
}

__global__
void updateMassFlux(
	Config config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice pBC,
	int applyNonOrtho
) {
	int faceID = blockIdx.x * blockDim.x + threadIdx.x;

	if (faceID >= mesh.faces.nFaces) return;

	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double area = mesh.faces.area[faceID];

	if (area <= 1.0e-30) {
		simple.mDot[faceID] = 0.0;
		return;
	}

	double rho = config.f.rho;

	double normalZ = mesh.faces.normalZ[faceID];
	double normalR = mesh.faces.normalR[faceID];

	double Df = interpolateNormalCorrectionCoeffToFace(
		owner,
		faceID,
		mesh,
		simple
	);

	if (neighbor >= 0) {

		double invDPN = mesh.faces.invCellToCell[faceID];

		double ppP = simple.pp[owner];
		double ppN = simple.pp[neighbor];

		// Orthogonal correction (implicit part of the p' equation).
		simple.mDot[faceID] -= rho * area * Df * (ppN - ppP) * invDPN;

		// Deferred non-orthogonal correction. Only applied when the p' equation
		// was solved with the matching cross term (i.e. correctors were run),
		// otherwise it would inject a divergence the solve never accounted for.
		if (applyNonOrtho) {
			double Df = interpolateNormalCorrectionCoeffToFace(owner, faceID, mesh, simple);
			simple.mDot[faceID] -= nonOrthoScalarDiffusionFlux(owner, faceID, mesh, simple.gradPZ, simple.gradPR, rho * Df);
		}
		return;
	}

	// Boundary face
	int groupID = mesh.faces.boundaryGroupID[faceID];

	if (groupID < 0 || groupID >= pBC.nGroups) {
		return;
	}

	uint8_t pType = pBC.typeByGroup[groupID];

	if (isDirichletType(pType)) {
		// fixed pressure boundary -> p'_b = 0
		double dPB = getDistanceCellToFace(mesh, owner, faceID, normalZ, normalR);

		double ppB = 0.0;
		double ppP = simple.pp[owner];

		simple.mDot[faceID] -= rho * area * Df * (ppB - ppP) / dPB;
	}
	else {
		// velocity-specified wall/inlet/symmetry/mass-flux boundary:
		// m'_b = 0, so do not correct boundary mass flux.
		return;
	}
}

