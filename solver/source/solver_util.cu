#include "solver_util.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "printer.h"

// ==============================================================
// ==================HELPER FUNCTIONS============================
// ==============================================================

__device__
void addNeighborCoeff(
	int n,
	int nb,
	FVMeshDevice mesh,
	double aNb,
	Coefficients coeff
) {
	if (nb < 0) {
		return;
	}

	if (coeff.useFaceCoeffs &&
		coeff.AF &&
		coeff.faceStart &&
		coeff.faceNeighbor) {
		int start = coeff.faceStart[n];
		int end = coeff.faceStart[n + 1];

		for (int k = start; k < end; k++) {
			if (coeff.faceNeighbor[k] == nb) {
				coeff.AF[k] += aNb;
				return;
			}
		}

		return;
	}

	int nz = coeff.nz;

	if (nz > 0) {
		if (nb == n + 1) {
			coeff.AE[n] += aNb;
		}
		else if (nb == n - 1) {
			coeff.AW[n] += aNb;
		}
		else if (nb == n + nz) {
			coeff.AN[n] += aNb;
		}
		else if (nb == n - nz) {
			coeff.AS[n] += aNb;
		}
	}
}

__global__
void getCorrectionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	double* D
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	D[n] = 0.0;

	if (!mesh.cells.active[n]) return;

	double aP = coeff.AC[n];

	if (fabs(aP) < 1.0e-30) {
		D[n] = 0.0;
		return;
	}

	double volume = mesh.cells.volume[n];
	D[n] = volume / aP;

}

__device__
double getDistanceCellToCell(
const FVMeshDevice& mesh,
int owner,
int neighbor,
double normalZ,
double normalR
) {
	double zP = mesh.cells.centerZ[owner];
	double rP = mesh.cells.centerR[owner];

	double zN = mesh.cells.centerZ[neighbor];
	double rN = mesh.cells.centerR[neighbor];

	return fabs((zN - zP) * normalZ + (rN - rP) * normalR);		// distance from cell to cell dotted with normal vector
}


__device__
double getDistanceCellToFace(
	const FVMeshDevice& mesh,
	int cellID,
	int faceID,
	double normalZ,
	double normalR
) {

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	return fabs((zF - zP) * normalZ + (rF - rP) * normalR);	// distance from cell to face dotted with normal vector

}

__device__
void getOutwardNormalForCell(
	FVMeshDevice mesh,
	int cellID,
	int faceID,
	double& normalZ,
	double& normalR
) {
	normalZ = mesh.faces.normalZ[faceID];
	normalR = mesh.faces.normalR[faceID];

	if (mesh.faces.neighbor[faceID] == cellID) {
		normalZ = -normalZ;
		normalR = -normalR;
	}
}


__device__
double interpolateFieldToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice fieldBC,
	const double* phi
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double phiP = phi[cellID];

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double dPF = getDistanceCellToFace(mesh, cellID, faceID, normalZ, normalR);

	// ---------------- interior face ----------------
	if (neighbor >= 0) {
		int nb = (owner == cellID) ? neighbor : owner;

		double phiN = phi[nb];

		double dNF = getDistanceCellToFace(mesh, nb, faceID, normalZ, normalR);

		double denom = dPF + dNF;

		if (denom <= 0.0) {
			return 0.5 * (phiP + phiN);
		}

		// Linear interpolation to face
		return (dNF * phiP + dPF * phiN) / denom;
	}

	// ---------------- boundary face ----------------
	int groupID = mesh.faces.boundaryGroupID[faceID];

	if (groupID < 0 || groupID >= fieldBC.nGroups) {
		// Default: zero-gradient
		return phiP;
	}

	uint8_t bcType = fieldBC.typeByGroup[groupID];
	double bcValue = fieldBC.valueByGroup[groupID];

	if (isDirichletType(bcType)) {
		return bcValue;
	}
	else if (isNeumannType(bcType)) {
		// dphi/dn = bcValue
		// zero-gradient means bcValue = 0, so phiF = phiP
		return phiP + bcValue * dPF;
	}

	return phiP;
}

// find value of varaible at the adjacent cell. also finds coord, the coordinate of the cell
__device__
double phiAtSide(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice phiBC,
	const double* phi,
	bool useZCoord,
	double& coord
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	if (neighbor >= 0) {
		int nb = (owner == cellID) ? neighbor : owner;

		coord = useZCoord
			? mesh.cells.centerZ[nb]
			: mesh.cells.centerR[nb];

		return phi[nb];
	}

	coord = useZCoord
		? mesh.faces.centerZ[faceID]
		: mesh.faces.centerR[faceID];

	return interpolateFieldToFace(
		cellID,
		faceID,
		mesh,
		phiBC,
		phi
	);
}


__device__
double getNormalCorrectionCoeff(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple
) {
	double normalZ = mesh.faces.normalZ[faceID];
	double normalR = mesh.faces.normalR[faceID];

	// DU corrects axial velocity, DV corrects radial velocity.
	// For axis-aligned faces, this naturally selects DU or DV.
	// Axial face: normalZ^2 = 1, normalR^2 = 0 -> DU
	// Radial face: normalZ^2 = 0, normalR^2 = 1 -> DV
	// branchless if statement
	return simple.DU[cellID] * normalZ * normalZ
		+ simple.DV[cellID] * normalR * normalR;
}

__device__
double getFaceCenterAlongOrientation(
	FVMeshDevice mesh,
	int faceID
) {
	double normalZ = mesh.faces.normalZ[faceID];
	double normalR = mesh.faces.normalR[faceID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	return rF * normalZ * normalZ
		+ zF * normalR * normalR;
}

__device__
double interpolateNormalCorrectionCoeffToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double dPF = getDistanceCellToFace(mesh, cellID, faceID, normalZ, normalR);

	double DP = getNormalCorrectionCoeff(
		cellID,
		faceID,
		mesh,
		simple
	);

	// boundary face: use owner/current cell correction coefficient
	if (neighbor < 0) {
		return DP;
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double dNF = getDistanceCellToFace(mesh, nb, faceID, normalZ, normalR);

	double DN = getNormalCorrectionCoeff(
		nb,
		faceID,
		mesh,
		simple
	);

	double denom = dPF + dNF;

	if (denom <= 0.0) {
		return 0.5 * (DP + DN);
	}

	// Linear interpolation to face
	return (dNF * DP + dPF * DN) / denom;
}


__device__
int findFaceOnSide(
	FVMeshDevice mesh,
	int cellID,
	double targetZ,
	double targetR
) {
	int start = mesh.cells.faceStart[cellID];
	int end = mesh.cells.faceStart[cellID + 1];

	for (int k = start; k < end; k++) {
		int faceID = mesh.cells.faceIDs[k];

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

		double dot = normalZ * targetZ + normalR * targetR;

		if (dot > 0.9) {	// WARNING may have to calibrate if faces are not perfectly aligned
			return faceID;
		}
	}

	return -1;
}

__device__
void phiGradientCell(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
) {
	gradZ = 0.0;
	gradR = 0.0;

	double volume = mesh.cells.volume[cellID];
	if (volume <= 1.0e-30) return;

	int start = mesh.cells.faceStart[cellID];
	int end = mesh.cells.faceStart[cellID + 1];

	for (int k = start; k < end; k++) {
		int faceID = mesh.cells.faceIDs[k];

		double normalZ = 0.0;
		double normalR = 0.0;
		getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

		double phiF = interpolateFieldToFace(
			cellID,
			faceID,
			mesh,
			bc,
			phi
		);

		double area = mesh.faces.area[faceID];
		gradZ += phiF * normalZ * area;
		gradR += phiF * normalR * area;
	}

	gradZ /= volume;
	gradR /= volume;
}

__device__
double rhieChowNormalVelocityToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundarySolverDevice bc
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double normalZ = 0.0;
	double normalR = 0.0;

	getOutwardNormalForCell(
		mesh,
		cellID,
		faceID,
		normalZ,
		normalR
	);

	// Linear/interpolated face velocity
	double uFace = interpolateFieldToFace(
		cellID,
		faceID,
		mesh,
		bc.u,
		simple.u
	);

	double vFace = interpolateFieldToFace(
		cellID,
		faceID,
		mesh,
		bc.v,
		simple.v
	);

	double unLinear = uFace * normalZ + vFace * normalR;

	// Boundary faces: use the boundary/interpolated velocity directly for now
	if (neighbor < 0) {
		return unLinear;
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double dPN = getDistanceCellToCell(mesh, cellID, nb, normalZ, normalR);

	if (dPN <= 1.0e-30) {
		return unLinear;
	}

	double pP = simple.p[cellID];
	double pN = simple.p[nb];

	// Direct pressure gradient between cell centers
	double gradPN = (pN - pP) / dPN;

	// Interpolate precomputed Green-Gauss pressure gradients to the face
	double dPF = getDistanceCellToFace(mesh, cellID, faceID, normalZ, normalR);
	double dNF = getDistanceCellToFace(mesh, nb,	 faceID, normalZ, normalR);

	double denom = dPF + dNF;

	double gradPzF = 0.5 * (simple.gradPZ[cellID] + simple.gradPZ[nb]);
	double gradPrF = 0.5 * (simple.gradPR[cellID] + simple.gradPR[nb]);

	if (denom > 1.0e-30) {
		gradPzF =
			(dNF * simple.gradPZ[cellID] + dPF * simple.gradPZ[nb]) / denom;

		gradPrF =
			(dNF * simple.gradPR[cellID] + dPF * simple.gradPR[nb]) / denom;
	}

	double gradPFaceNormal =
		gradPzF * normalZ +
		gradPrF * normalR;

	double Df = interpolateNormalCorrectionCoeffToFace(
		cellID,
		faceID,
		mesh,
		simple
	);

	double unRC = unLinear - Df * (gradPN - gradPFaceNormal);

	return unRC;
}

__global__
void computeFaceMassFluxRhieChow(
	ConfigSolver config,
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundarySolverDevice bc
) {
	int f = blockIdx.x * blockDim.x + threadIdx.x;

	if (f >= mesh.faces.nFaces) return;

	int owner = mesh.faces.owner[f];
	int neighbor = mesh.faces.neighbor[f];

	if (owner < 0) return;

	double normalZ = mesh.faces.normalZ[f]; // outward from owner
	double normalR = mesh.faces.normalR[f];

	double area = mesh.faces.area[f];

	if (area <= 1.0e-30) {
		simple.mDot[f] = 0.0;
		return;
	}

	double rho = config.f.rho;

	double unFace = rhieChowNormalVelocityToFace(
		owner,
		f,
		mesh,
		simple,
		bc
	);

	simple.mDot[f] = rho * unFace * area;

}


__device__
bool isDirichletType(uint8_t type) {
	return type == (uint8_t)(DIRICHLET);
}

__device__
bool isNeumannType(uint8_t type) {
	return type == (uint8_t)(NEUMANN);
}

__device__
bool isFullyDevelopedType(uint8_t type) {
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
void addEnergyDiffusionCoefficient(
	ConfigSolver config,
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	const FluidPropertyConfig& f = config.f;

	double k = f.k;
	double cp = f.cp;
	double rho = f.rho;
	double thermDiffusivity = k / (rho * cp);

	double* AC = coeff.AC;
	double* b = coeff.b;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		double area = mesh.faces.area[faceID];

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, n, faceID, normalZ, normalR);


		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			double dPN = getDistanceCellToCell(mesh, n, nb, normalZ, normalR);

			if (dPN <= 0.0) continue;

			double K = thermDiffusivity * area / dPN;

			// Add diagonal contribution
			AC[n] += K;

			addNeighborCoeff(n, nb, mesh, -K, coeff);

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

			double dPF = getDistanceCellToFace(mesh, n, faceID, normalZ, normalR);

			if (dPF <= 0.0) continue;

			double K = thermDiffusivity * area / dPF;

			if (isDirichletType(bcType)) {
				AC[n] += K;
				b[n] += K * bcValue;
			}
			else if (isNeumannType(bcType)) {
				// For zero-gradient Neumann, bcValue = 0, so this adds nothing.
				// If bcValue = du/dn, then this adds prescribed diffusive flux.

				b[n] += thermDiffusivity * area * bcValue;
			}
		}
	}
}

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
	double* b = coeff.b;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		double area = mesh.faces.area[faceID];

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, n, faceID, normalZ, normalR);


		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			double dPN = getDistanceCellToCell(mesh, n, nb, normalZ, normalR);

			if (dPN <= 0.0) continue;

			double K = mu * area / dPN;

			// Add diagonal contribution
			AC[n] += K;

			addNeighborCoeff(n, nb, mesh, -K, coeff);

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
			double totalLength = bc.lengthByGroup[groupID];

			double dPF = getDistanceCellToFace(mesh, n, faceID, normalZ, normalR);

			if (dPF <= 0.0) continue;

			double K = mu * area / dPF;

			if (isDirichletType(bcType)) {
				AC[n] += K;
				b[n] += K * bcValue;
			}
			else if (isNeumannType(bcType)) {
				// For zero-gradient Neumann, bcValue = 0, so this adds nothing.
				// If bcValue = du/dn, then this adds prescribed diffusive flux.

				b[n] += mu * area * bcValue;
			}
			else if (isFullyDevelopedType(bcType)) {
				double length = getFaceCenterAlongOrientation(mesh, faceID);
				AC[n] += K;
				b[n] += K * bcValue * (1 - ((length * length) / (totalLength * totalLength)));
			}
		}
	}
}

// ==============================================================
// ==================CONVECTION TERM=============================
// ==============================================================
__device__
void addConvectionContribution(
	int n,
	int nb,
	FVMeshDevice mesh,
	double F,
	bool isBoundary,
	int groupID,
	Coefficients coeff,
	BoundaryFieldDevice fieldBC
) {
	// ------------------------------------------------------------
	// Interior face
	// ------------------------------------------------------------
	if (!isBoundary) {

		// First-order upwind:
		//
		// F > 0: flow leaves current cell, phi_f = phi_P
		// F < 0: flow enters current cell, phi_f = phi_N
		coeff.AC[n] += fmax(F, 0.0);

		double aNb = fmin(F, 0.0);

		addNeighborCoeff(
			n,
			nb,
			mesh,
			aNb,
			coeff
		);

		return;
	}

	// ------------------------------------------------------------
	// Boundary face
	// ------------------------------------------------------------
	if (groupID < 0 || groupID >= fieldBC.nGroups) {
		// Default zero-gradient:
		// phi_f = phi_P
		coeff.AC[n] += F;
		return;
	}

	uint8_t bcType = fieldBC.typeByGroup[groupID];
	double bcValue = fieldBC.valueByGroup[groupID];

	if (isDirichletType(bcType)) {

		if (F < 0.0) {
			// Inflow boundary:
			// convection contribution is F * phi_b.
			// Move known value to RHS:
			coeff.b[n] += -F * bcValue;
		}
		else {
			// Outflow boundary:
			// use current cell value.
			coeff.AC[n] += F;
		}
	}
	else if (isNeumannType(bcType) || isFullyDevelopedType(bcType)) {
		// zero-gradient / fully developed:
		// phi_f = phi_P
		coeff.AC[n] += F;
	}
}

__global__
void addMomentumConvectionCoefficient(
	FVMeshDevice mesh,
	Coefficients uCoeff,
	Coefficients vCoeff,
	VariablesSimple simple,
	BoundarySolverDevice bc
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		// mDot is stored positive outward from owner.
		double Fowner = simple.mDot[faceID];

		double F = 0.0;

		if (owner == n) {
			F = Fowner;
		}
		else if (neighbor == n) {
			F = -Fowner;
		}
		else {
			continue;
		}

		if (fabs(F) <= 1.0e-30) {
			continue;
		}

		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			addConvectionContribution(
				n,
				nb,
				mesh,
				F,
				false,
				-1,
				uCoeff,
				bc.u
			);

			addConvectionContribution(
				n,
				nb,
				mesh,
				F,
				false,
				-1,
				vCoeff,
				bc.v
			);
		}

		// ------------------------------------------------------------
		// Boundary face
		// ------------------------------------------------------------
		else {

			int groupID = mesh.faces.boundaryGroupID[faceID];

			addConvectionContribution(
				n,
				-1,
				mesh,
				F,
				true,
				groupID,
				uCoeff,
				bc.u
			);

			addConvectionContribution(
				n,
				-1,
				mesh,
				F,
				true,
				groupID,
				vCoeff,
				bc.v
			);
		}
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

	AC[n] += (rho * Az * dz[j]) / dt;
	b[n] += (rho * Az * dz[j] * uOld[n]) / dt;
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
	double* rFace = g.d_rFace;
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

	r1 = rFace[i];
	r2 = rFace[i + 1];

	double Az = CUDART_PI * (r2 * r2 - r1 * r1);

	AC[n] += (rho * Az * dz[j]) / config.dt;
	b[n] += (rho * Az * dz[j] * vOld[n]) / config.dt;
}

__global__
void clearCoefficients(Coefficients coeff) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;

	if (coeff.AE) coeff.AE[n] = 0.0;
	if (coeff.AW) coeff.AW[n] = 0.0;
	if (coeff.AN) coeff.AN[n] = 0.0;
	if (coeff.AS) coeff.AS[n] = 0.0;
	if (coeff.AC) coeff.AC[n] = 0.0;
	if (coeff.b) coeff.b[n] = 0.0;
	if (coeff.res) coeff.res[n] = 0.0;

	if (coeff.AF && coeff.faceStart) {
		int start = coeff.faceStart[n];
		int end = coeff.faceStart[n + 1];

		for (int k = start; k < end; k++) {
			coeff.AF[k] = 0.0;
		}
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
