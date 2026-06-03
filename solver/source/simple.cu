#include "simple.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "solver_util.cuh"

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

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double dPF = fabs((zF - zP) * normalZ + (rF - rP) * normalR);

	// ---------------- interior face ----------------
	if (neighbor >= 0) {
		int nb = (owner == cellID) ? neighbor : owner;

		double phiN = phi[nb];

		double zN = mesh.cells.centerZ[nb];
		double rN = mesh.cells.centerR[nb];

		double dNF = fabs((zN - zF) * normalZ + (rN - rF) * normalR);

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
double interpolateNormalCorrectionCoeffToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	VariablesSimple simple
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double dPF = fabs((zF - zP) * normalZ + (rF - rP) * normalR);

	double DP = getNormalCorrectionCoeff(
		cellID,
		faceID,
		mesh,
		simple
	);

	// Boundary face: use owner/current cell correction coefficient
	if (neighbor < 0) {
		return DP;
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double zN = mesh.cells.centerZ[nb];
	double rN = mesh.cells.centerR[nb];

	double dNF = fabs((zN - zF) * normalZ + (rN - rF) * normalR);

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
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

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

		double normalZ, normalR;
		getOutwardNormalForCell(mesh, n, faceID, normalZ, normalR);

		double zF = mesh.faces.centerZ[faceID];
		double rF = mesh.faces.centerR[faceID];

		double dPF = fabs((zF - zP) * normalZ + (rF - rP) * normalR);

		if (dPF <= 1.0e-30) continue;

		double Df = interpolateNormalCorrectionCoeffToFace(
			n,
			faceID,
			mesh,
			simple
		);
		//if (n == 0) {
		//	printf("Df: %f\n", Df);
		//}
		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			double zN = mesh.cells.centerZ[nb];
			double rN = mesh.cells.centerR[nb];

			double dPN = fabs((zN - zP) * normalZ + (rN - rP) * normalR);

			if (dPN <= 1.0e-30) continue;

			double K = rho * area * Df / dPN;

			// Style A pressure-correction matrix assembly
			AC[n] += K;

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

		if (dot > 0.9) {
			return faceID;
		}
	}

	return -1;
}

__device__
double pressureAtSide(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* p,
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

		return p[nb];
	}

	coord = useZCoord
		? mesh.faces.centerZ[faceID]
		: mesh.faces.centerR[faceID];

	return interpolateFieldToFace(
		cellID,
		faceID,
		mesh,
		pBC,
		p
	);
}

__device__
void computePressureGradientCell(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* p,
	double& gradZ,
	double& gradR
) {
	gradZ = 0.0;
	gradR = 0.0;

	// East/west in z
	int eastFace = findFaceOnSide(mesh, cellID, 1.0, 0.0);
	int westFace = findFaceOnSide(mesh, cellID, -1.0, 0.0);

	if (eastFace >= 0 && westFace >= 0) {
		double zE = 0.0;
		double zW = 0.0;

		double pE = pressureAtSide(
			cellID, eastFace, mesh, pBC, p, true, zE
		);

		double pW = pressureAtSide(
			cellID, westFace, mesh, pBC, p, true, zW
		);

		double dz = zE - zW;

		if (fabs(dz) > 1.0e-30) {
			gradZ = (pE - pW) / dz;
		}
	}

	// North/south in r
	int northFace = findFaceOnSide(mesh, cellID, 0.0, 1.0);
	int southFace = findFaceOnSide(mesh, cellID, 0.0, -1.0);

	if (northFace >= 0 && southFace >= 0) {
		double rN = 0.0;
		double rS = 0.0;

		double pN = pressureAtSide(
			cellID, northFace, mesh, pBC, p, false, rN
		);

		double pS = pressureAtSide(
			cellID, southFace, mesh, pBC, p, false, rS
		);

		double dr = rN - rS;

		if (fabs(dr) > 1.0e-30) {
			gradR = (pN - pS) / dr;
		}
	}
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

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];

	double zN = mesh.cells.centerZ[nb];
	double rN = mesh.cells.centerR[nb];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	double dPN = fabs(
		(zN - zP) * normalZ +
		(rN - rP) * normalR
	);

	if (dPN <= 1.0e-30) {
		return unLinear;
	}

	double pP = simple.p[cellID];
	double pN = simple.p[nb];

	// Direct pressure gradient between cell centers
	double gradPN = (pN - pP) / dPN;

	// Interpolate precomputed Green-Gauss pressure gradients to the face
	double dPF = fabs(
		(zF - zP) * normalZ +
		(rF - rP) * normalR
	);

	double dNF = fabs(
		(zN - zF) * normalZ +
		(rN - rF) * normalR
	);

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

	computePressureGradientCell(
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

__device__
double interpolatePressureCorrectionToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* pp
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double ppP = pp[cellID];

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double dPF = fabs((zF - zP) * normalZ + (rF - rP) * normalR);

	// Interior face
	if (neighbor >= 0) {
		int nb = (owner == cellID) ? neighbor : owner;

		double ppN = pp[nb];

		double zN = mesh.cells.centerZ[nb];
		double rN = mesh.cells.centerR[nb];

		double dNF = fabs((zN - zF) * normalZ + (rN - rF) * normalR);

		double denom = dPF + dNF;

		if (denom <= 0.0) {
			return 0.5 * (ppP + ppN);
		}

		return (dNF * ppP + dPF * ppN) / denom;
	}

	// Boundary face
	int groupID = mesh.faces.boundaryGroupID[faceID];

	if (groupID < 0 || groupID >= pBC.nGroups) {
		// default: zero-gradient pp
		return ppP;
	}

	uint8_t bcType = pBC.typeByGroup[groupID];

	if (isDirichletType(bcType)) {
		// fixed pressure boundary: p is fixed, so p' = 0
		return 0.0;
	}
	else if (isNeumannType(bcType)) {
		// zero-gradient pressure correction
		return ppP;
	}

	return ppP;
}


__device__
double pressureCorrectionAtSide(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* pp,
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

		return pp[nb];
	}

	coord = useZCoord
		? mesh.faces.centerZ[faceID]
		: mesh.faces.centerR[faceID];

	return interpolatePressureCorrectionToFace(
		cellID,
		faceID,
		mesh,
		pBC,
		pp
	);
}


__device__
void computePressureCorrectionGradient(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice pBC,
	const double* pp,
	double& gradZ,
	double& gradR
) {
	gradZ = 0.0;
	gradR = 0.0;

	int eastFace = findFaceOnSide(mesh, cellID, 1.0, 0.0);
	int westFace = findFaceOnSide(mesh, cellID, -1.0, 0.0);

	if (eastFace >= 0 && westFace >= 0) {
		double zE = 0.0;
		double zW = 0.0;

		double ppE = pressureCorrectionAtSide(
			cellID, eastFace, mesh, pBC, pp, true, zE
		);

		double ppW = pressureCorrectionAtSide(
			cellID, westFace, mesh, pBC, pp, true, zW
		);

		double dz = zE - zW;

		if (fabs(dz) > 1.0e-30) {
			gradZ = (ppE - ppW) / dz;
		}
	}

	int northFace = findFaceOnSide(mesh, cellID, 0.0, 1.0);
	int southFace = findFaceOnSide(mesh, cellID, 0.0, -1.0);

	if (northFace >= 0 && southFace >= 0) {
		double rN = 0.0;
		double rS = 0.0;

		double ppN = pressureCorrectionAtSide(
			cellID, northFace, mesh, pBC, pp, false, rN
		);

		double ppS = pressureCorrectionAtSide(
			cellID, southFace, mesh, pBC, pp, false, rS
		);

		double dr = rN - rS;

		if (fabs(dr) > 1.0e-30) {
			gradR = (ppN - ppS) / dr;
		}
	}
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

	computePressureGradientCell(
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

	computePressureCorrectionGradient(
		n,
		mesh,
		pBC,
		pp,
		gradPP_Z,
		gradPP_R
	);
	
	u[n] -= DU[n] * gradPP_Z;
	v[n] -= DV[n] * gradPP_R;
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

	double zP = mesh.cells.centerZ[owner];
	double rP = mesh.cells.centerR[owner];

	double zF = mesh.faces.centerZ[f];
	double rF = mesh.faces.centerR[f];

	double Df = interpolateNormalCorrectionCoeffToFace(
		owner,
		f,
		mesh,
		simple
	);

	if (neighbor >= 0) {
		double zN = mesh.cells.centerZ[neighbor];
		double rN = mesh.cells.centerR[neighbor];

		double dPN = fabs(
			(zN - zP) * normalZ +
			(rN - rP) * normalR
		);

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
		double zB = mesh.faces.centerZ[f];
		double rB = mesh.faces.centerR[f];

		double dPB = fabs(
			(zB - zP) * normalZ +
			(rB - rP) * normalR
		);

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