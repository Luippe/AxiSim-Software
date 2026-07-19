#include "solver_util.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "printer.h"

#include "concentration_equation.cuh"
// ==============================================================
// ==================REDUCTION KERNEL============================
// ==============================================================
__global__
void sumBlock(int N, double* __restrict__ in, double* __restrict__ out) {
	extern __shared__ double s[];
	int n = blockIdx.x * blockDim.x + threadIdx.x;
	int tid = threadIdx.x;		// thread id within the block

	s[tid] = (n < N) ? in[n] : 0.0;
	__syncthreads();

	// if you have [0, 1, 2, 3] as your input, the next iteration will give [2, 4]
	// the first element adds the third, and the second element adds the fourth to itself.
	for (int stride = blockDim.x / 2; stride > 0; stride /= 2) {
		if (tid < stride) {
			s[tid] += s[tid + stride];
		}
		__syncthreads();
	}

	if (tid == 0) {// store result for each block
		out[blockIdx.x] = s[0];
	}
}

void reduction(int N, int threadsPerBlock, size_t shmem, cudaStream_t stream, double* tmpA, double* tmpB, double* in, double* store) {
	int m = N;
	double* out = tmpA;
	double* alt = tmpB;

	while (m > 1) {
		int blocks = (m + threadsPerBlock - 1) / threadsPerBlock;

		sumBlock << <blocks, threadsPerBlock, shmem, stream >> > (m, in, out);

		in = out;
		std::swap(out, alt);
		m = blocks;
	}

	cudaMemcpyAsync(store, in, sizeof(double), cudaMemcpyDeviceToHost, stream);

}

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

	double dz = zN - zP;
	double dr = rN - rP;

	double proj = fabs(dz * normalZ + dr * normalR); // over-relaxed projected distance
	double full = sqrt(dz * dz + dr * dr);           // true centroid separation

	// Clamp the projection so a highly non-orthogonal / near-axis cell can't
	// collapse n.d toward zero and blow up coefficients of the form A/(n.d)
	// (Rhie-Chow face gradient, p' Laplacian, momentum diffusion). On well
	// shaped cells proj ~ full, so this leaves them untouched.
	double minProj = 0.3 * full;

	return fmax(proj, minProj);
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
double getCellToCellDotNorm(
	FVMeshDevice mesh,
	int cellID,
	int nb,
	double normalZ,
	double normalR
) {
	double dz = mesh.cells.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.cells.centerR[nb] - mesh.cells.centerR[cellID];

	double len = sqrt(dz * dz + dr * dr);
	if (len <= 1.0e-30) return 0.0;

	return (normalZ * dz + normalR * dr) / len;	// cos(theta) = n . d / |d|  (n is unit)
}

__device__
double getCellToFaceDotNorm(
	FVMeshDevice mesh,
	int cellID,
	int nb,
	double normalZ,
	double normalR
) {
	double dz = mesh.faces.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.faces.centerR[nb] - mesh.cells.centerR[cellID];

	double len = sqrt(dz * dz + dr * dr);
	if (len <= 1.0e-30) return 0.0;

	return (normalZ * dz + normalR * dr) / len;	// cos(theta) = n . d / |d|
}

__device__
double nonOrthoScalarDiffusionFlux(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	const double* gradPhiZ,
	const double* gradPhiR,
	double gamma
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	if (neighbor < 0 || !gradPhiZ || !gradPhiR) {
		return 0.0;
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double area = mesh.faces.area[faceID];

	// check if cell-cell or cell-face has greater non-orthogonality. fix the one that has the most non-orthogonality
	double ndCellCell = getCellToCellDotNorm(mesh, cellID, nb, normalZ, normalR);
	//double ndCellFace = getCellToFaceDotNorm(mesh, cellID, faceID, normalZ, normalR);

	double dz = mesh.cells.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.cells.centerR[nb] - mesh.cells.centerR[cellID];
	double dOrth = getDistanceCellToCell(mesh, cellID, nb, normalZ, normalR);

	

	if (dOrth <= 1.0e-30) {
		return 0.0;
	}

	//double signedDOrth = (nd < 0.0) ? -dOrth : dOrth;
	double aOverNd = area / dOrth;
	double Tz = area * normalZ - aOverNd * dz;
	double Tr = area * normalR - aOverNd * dr;

	// Face gradient from the precomputed cell-centered gradients (built once
	// per iteration with the user-selected scheme), distance-weighted to the
	// face. The closer cell gets more weight (same convention as
	// interpolateFieldToFace), staying second-order on stretched cells where a
	// plain average would not. Symmetric under owner<->neighbor swap, so the
	// pressure-correction RHS and the mass-flux correction stay consistent.
	double dPF = getDistanceCellToFace(mesh, cellID, faceID, normalZ, normalR);
	double dNF = getDistanceCellToFace(mesh, nb,     faceID, normalZ, normalR);
	double denom = dPF + dNF;

	double gradFaceZ, gradFaceR;
	if (denom <= 1.0e-30) {
		gradFaceZ = 0.5 * (gradPhiZ[cellID] + gradPhiZ[nb]);
		gradFaceR = 0.5 * (gradPhiR[cellID] + gradPhiR[nb]);
	}
	else {
		gradFaceZ = (dNF * gradPhiZ[cellID] + dPF * gradPhiZ[nb]) / denom;
		gradFaceR = (dNF * gradPhiR[cellID] + dPF * gradPhiR[nb]) / denom;
	}

	return gamma * (Tz * gradFaceZ + Tr * gradFaceR);
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
void phiGradientGreenGauss(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
) {
	gradZ = 0.0;
	gradR = 0.0;

	// The stored face areas and cell volumes are the *revolved* (axisymmetric)
	// metrics: area = 2*pi*rf*L2D and volume = 2*pi*rc*A2D. Green-Gauss for the
	// meridional-plane gradient (d/dz, d/dr) is a purely 2D operation, so it must
	// use the planar face length L2D and cell area A2D -- feeding the revolved
	// area/volume in directly biases the radial gradient (a constant field would
	// give grad_r = c/rc, worst near the axis). Recover the planar metrics from
	// the revolved ones: L2D = area/(2*pi*rf), A2D = volume/(2*pi*rc).
	double volume = mesh.cells.volume[cellID];
	double rc = mesh.cells.centerR[cellID];
	if (volume <= 1.0e-30 || rc <= 1.0e-30) return;

	const double twoPi = 6.28318530717958647692;

	int start = mesh.cells.faceStart[cellID];
	int end = mesh.cells.faceStart[cellID + 1];

	// A face lying on the axis has rf = 0 (revolved area = 0), so its L2D cannot be
	// recovered by the area/(2*pi*rf) division. Instead use the fact that the cell's
	// 2D face polygon closes -- sum(n * L2D) = 0 -- so the axis face's (n * L2D)
	// equals minus the running sum over all the other faces. A cell touches the
	// axis on at most one face.
	double closureZ = 0.0;
	double closureR = 0.0;

	bool hasAxisFace = false;
	double axisPhiF = 0.0;

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

		double rf = mesh.faces.centerR[faceID];

		if (rf > 1.0e-30) {
			double length2D = mesh.faces.area[faceID] / (twoPi * rf);

			gradZ += phiF * normalZ * length2D;
			gradR += phiF * normalR * length2D;

			closureZ += normalZ * length2D;
			closureR += normalR * length2D;
		}
		else {
			// axis face: resolve its n * L2D from polygon closure below
			hasAxisFace = true;
			axisPhiF = phiF;
		}
	}

	if (hasAxisFace) {
		// n_axis * L2D_axis = -(sum over the other faces)
		gradZ += axisPhiF * (-closureZ);
		gradR += axisPhiF * (-closureR);
	}

	// divide by the planar cell area A2D = volume / (2*pi*rc)
	double invA2D = twoPi * rc / volume;
	gradZ *= invA2D;
	gradR *= invA2D;
}

__device__
void phiGradientLeastSquare(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
) {

	gradZ = 0.0;
	gradR = 0.0;

	double zP = mesh.cells.centerZ[cellID];
	double rP = mesh.cells.centerR[cellID];
	double phiP = phi[cellID];

	// weighted least-squares normal equations:  M * grad = rhs
	double Szz = 0.0, Szr = 0.0, Srr = 0.0;
	double bz = 0.0, br = 0.0;

	int start = mesh.cells.faceStart[cellID];
	int end = mesh.cells.faceStart[cellID + 1];

	for (int k = start; k < end; k++) {
		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		double dz, dr, dphi;

		if (neighbor >= 0) {
			int nb = (owner == cellID) ? neighbor : owner;
			dz = mesh.cells.centerZ[nb] - zP;
			dr = mesh.cells.centerR[nb] - rP;
			dphi = phi[nb] - phiP;
		}
		else {
			// boundary face: sample the BC value at the face center. For a
			// zero-gradient (e.g. symmetry) pressure face this gives dphi = 0
			// along the face direction, so LSQ respects symmetry directly.
			double phiF = interpolateFieldToFace(cellID, faceID, mesh, bc, phi);
			dz = mesh.faces.centerZ[faceID] - zP;
			dr = mesh.faces.centerR[faceID] - rP;
			dphi = phiF - phiP;
		}

		double d2 = dz * dz + dr * dr;
		if (d2 <= 1.0e-30) continue;

		double w = 1.0 / d2; // inverse-distance-squared weighting

		Szz += w * dz * dz;
		Szr += w * dz * dr;
		Srr += w * dr * dr;
		bz += w * dz * dphi;
		br += w * dr * dphi;
	}

	double det = Szz * Srr - Szr * Szr;

	if (fabs(det) <= 1.0e-30) return;

	gradZ = (Srr * bz - Szr * br) / det;
	gradR = (-Szr * bz + Szz * br) / det;
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
void computeGradient(
	FVMeshDevice mesh, 
	BoundaryFieldDevice bc,
	double* phi,
	double* gradZ,
	double* gradR,
	GradientScheme scheme
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	gradZ[n] = 0.0;
	gradR[n] = 0.0;
	if (!mesh.cells.active[n]) return;

	if (scheme == GradientScheme::GRAD_GREEN_GAUSS) {
		phiGradientGreenGauss(n, mesh, bc, phi, gradZ[n], gradR[n]);
	}
	else if (scheme == GradientScheme::GRAD_LSQ) {
		phiGradientLeastSquare(n, mesh, bc, phi, gradZ[n], gradR[n]);
	}
}

__global__
void computeFaceMassFluxRhieChow(
	Config config,
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

__device__
bool isMichaelisMentenType(uint8_t type) {
	return type == (uint8_t)(MICHAELIS_MENTEN);
}

__device__
bool isHillType(uint8_t type) {
	return type == (uint8_t)(HILL);
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
__global__ void
addDiffusionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	int applyNonOrtho,
	double constVar
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

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

			double K = constVar * area / dPN;

			// Add diagonal contribution
			AC[n] += K;

			addNeighborCoeff(n, nb, mesh, -K, coeff);

			// Deferred non-orthogonal correction. The orthogonal part above is
			// implicit in the matrix; this explicit cross-diffusion flux is added
			// to the RHS using the current velocity field. Gated by the same flag
			// as the pressure non-orthogonal correction so it can be disabled
			// (the Green-Gauss gradients it uses are noisy on near-axis cells).
			if (applyNonOrtho) {
				b[n] += nonOrthoScalarDiffusionFlux(
					n,
					faceID,
					mesh,
					gradPhiZ,
					gradPhiR,
					constVar
				);
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
			double totalLength = bc.lengthByGroup[groupID];
			uint8_t boundaryType = bc.boundaryTypeByGroup
				? bc.boundaryTypeByGroup[groupID]
				: (uint8_t)(BoundaryType::WALL);

			double dPF = getDistanceCellToFace(mesh, n, faceID, normalZ, normalR);

			if (dPF <= 0.0) continue;

			double K = constVar * area / dPF;

			if (isDirichletType(bcType)) {
				AC[n] += K;
				b[n] += K * bcValue;

			}
			else if (isNeumannType(bcType)) {
				// For zero-gradient Neumann, bcValue = 0, so this adds nothing.
				// If bcValue = du/dn, then this adds prescribed diffusive flux.
				b[n] += constVar * area * bcValue;
			}
			else if (isFullyDevelopedType(bcType)) {
				double length = getFaceCenterAlongOrientation(mesh, faceID);
				AC[n] += K;
				b[n] += K * bcValue * (1 - ((length * length) / (totalLength * totalLength)));
			}
			else if (isMichaelisMentenType(bcType)) {

				double Rtot = (dPF / constVar) + bc.RtotByGroup[groupID];
				double h = 1 / Rtot;
				double& cw = mesh.faces.cw[faceID];

				wallConcentrationMichaelisMenten(bc, groupID, phi[n], cw, h);
				mesh.faces.ocrWall[faceID] = area * MichaelisMenten(bc, groupID, cw) * Inhibition(bc, groupID, cw);

				//printf("%e, %e, %e, %e, %e, %e\n",cw, area, Rtot, dPF, constVar, bc.RtotByGroup[groupID]);
				AC[n] += area * h;
				b[n] += area * h * cw;

			}
			else if (isHillType(bcType)) {

				double Rtot = (dPF / constVar) + bc.RtotByGroup[groupID];
				double h = 1 / Rtot;
				double& cw = mesh.faces.cw[faceID];

				wallConcentrationHill(bc, groupID, phi[n], cw, h);
				mesh.faces.ocrWall[faceID] = area * Hill(bc, groupID, cw) * Inhibition(bc, groupID, cw);

				AC[n] += area * h;
				b[n] += area * h * cw;
			}
		}
	}
}

__global__
void addRadialMomentumCylindricalSource(
	Config config,
	FVMeshDevice mesh,
	Coefficients vCoeff
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	double r = mesh.cells.centerR[n];
	double volume = mesh.cells.volume[n];
	double mu = config.f.mu;

	double r2 = r * r;
	if (r2 <= 1.0e-30 || volume <= 0.0) return;

	// Cylindrical radial momentum uses the vector Laplacian:
	// mu * (laplacian(V) - V / r^2). The scalar Laplacian part is assembled
	// by addDiffusionCoefficient; moving diffusion to the matrix leaves this
	// extra term as a positive implicit diagonal contribution.
	vCoeff.AC[n] += mu * volume / r2;
}

// ==============================================================
// ==================CONVECTION TERM=============================
// ==============================================================
// Higher-order face value for the deferred correction. Returns the first-order
// upwind value itself when the scheme is upwind or the data it needs is missing,
// which makes the correction below vanish.
__device__
double higherOrderFaceValue(
	int n,
	int nb,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice fieldBC,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	ConvectionScheme scheme,
	double F,
	double phiUD
) {
	if (scheme == CONV_CENTRAL) {
		// Linear interpolation between the two cell centers. Always a convex
		// combination of phiP/phiN, so it needs no limiting -- but see the
		// boundedness note at the call site: central is second order and
		// oscillatory above cell Peclet ~2 regardless of the face value's range.
		return interpolateFieldToFace(n, faceID, mesh, fieldBC, phi);
	}

	// Second-order (linear) upwind, and QUICK which is not reachable from the UI
	// and falls back here: extrapolate from the UPWIND cell along the vector to
	// the face using that cell's gradient.
	//
	// phi_f = phi_U + grad(phi)_U . (r_f - r_U)
	//
	// The classic QUICK stencil needs a far-upwind cell, which a general face list
	// does not provide; the gradient form is the standard unstructured equivalent.
	if (!gradPhiZ || !gradPhiR) {
		return phiUD;			// gradients not available -> stay first order
	}

	int up = (F > 0.0) ? n : nb;

	double dz = mesh.faces.centerZ[faceID] - mesh.cells.centerZ[up];
	double dr = mesh.faces.centerR[faceID] - mesh.cells.centerR[up];

	double phiHO = phi[up] + gradPhiZ[up] * dz + gradPhiR[up] * dr;

	// Clip the extrapolation into the range spanned by the two cells sharing the
	// face. Unlimited linear upwind overshoots near sharp gradients, and for
	// concentration an overshoot below zero would feed a negative value into the
	// Michaelis-Menten pow() at the wall. Clipping reduces the scheme to upwind at
	// local extrema, which is what makes it bounded.
	double lo = fmin(phi[n], phi[nb]);
	double hi = fmax(phi[n], phi[nb]);

	return fmin(fmax(phiHO, lo), hi);
}

__device__
void addConvectionContribution(
	int n,
	int nb,
	int faceID,
	FVMeshDevice mesh,
	double F,
	bool isBoundary,
	int groupID,
	Coefficients coeff,
	BoundaryFieldDevice fieldBC,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	ConvectionScheme scheme
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

		addNeighborCoeff(n,	nb,	mesh, aNb, coeff);

		// ---- deferred higher-order correction ----
		//
		// The convection term contributes F*phi_f to the LHS. Split it as
		//
		//     F*phi_HO = F*phi_UD + F*(phi_HO - phi_UD)
		//
		// and keep only the upwind part implicit. The matrix therefore stays
		// exactly the first-order upwind operator -- diagonally dominant, positive
		// off-diagonals, an M-matrix -- which is what Jacobi, Gauss-Seidel and the
		// multigrid coarse operator all rely on. Assembling central or linear
		// upwind directly into the matrix would break that and diverge.
		//
		// The difference goes to the RHS, lagged one outer iteration. At
		// convergence phi satisfies the full higher-order scheme.
		if (scheme != CONV_UPWIND && phi) {

			double phiUD = (F > 0.0) ? phi[n] : phi[nb];

			double phiHO = higherOrderFaceValue(
				n, nb, faceID, mesh, fieldBC,
				phi, gradPhiZ, gradPhiR, scheme, F, phiUD
			);

			coeff.b[n] -= F * (phiHO - phiUD);
		}

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
		// zero-gradient / fully developed types:
		// phi_f = phi_P.
		coeff.AC[n] += F;
	}
}

__global__
void addConvectionCoefficient(
	FVMeshDevice mesh,
	VariablesSimple simple,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	ConvectionScheme scheme,
	double fluxScale
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	int start = mesh.cells.faceStart[n];
	int end = mesh.cells.faceStart[n + 1];

	// Net outward mass flux through this cell's faces. For a converged flow this
	// is the discrete continuity residual (~0); before convergence it is nonzero
	// and is what lets an unbounded upwind row overshoot its boundary values.
	double netF = 0.0;

	for (int k = start; k < end; k++) {

		int faceID = mesh.cells.faceIDs[k];

		int owner = mesh.faces.owner[faceID];
		int neighbor = mesh.faces.neighbor[faceID];

		// mDot is stored positive outward from owner. It is a MASS flux
		// (rho*u*area); fluxScale converts it to the flux this field actually
		// convects (1.0 for momentum, 1/rho for a passive scalar -> volumetric).
		double Fowner = simple.mDot[faceID] * fluxScale;

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

		// Accumulate only the faces that actually contribute to the matrix, so
		// the correction below cancels the assembled row sum exactly.
		netF += F;

		// ------------------------------------------------------------
		// Interior face
		// ------------------------------------------------------------
		if (neighbor >= 0) {

			int nb = (owner == n) ? neighbor : owner;

			addConvectionContribution(
				n,
				nb,
				faceID,
				mesh,
				F,
				false,
				-1,
				coeff,
				bc,
				phi,
				gradPhiZ,
				gradPhiR,
				scheme
			);
		}

		// ------------------------------------------------------------
		// Boundary face
		// ------------------------------------------------------------
		else {

			// Boundary faces stay first order. A Dirichlet boundary already
			// supplies the exact face value, and the zero-gradient/outflow cases
			// have no downstream cell to extrapolate from.
			int groupID = mesh.faces.boundaryGroupID[faceID];
			addConvectionContribution(
				n,
				-1,
				faceID,
				mesh,
				F,
				true,
				groupID,
				coeff,
				bc,
				phi,
				gradPhiZ,
				gradPhiR,
				scheme
			);
		}
	}

	// Bounded-convection correction (OpenFOAM's -Sp(div(phi), phi)). Upwind is
	// only bounded when the face fluxes are divergence-free (netF == 0); while
	// the SIMPLE flow is still converging netF != 0 acts as a spurious source
	// that lets phi over/undershoot its boundary values. Subtracting phi_P*netF
	// forces the convection row sum to zero, restoring the M-matrix property.
	// netF -> 0 at convergence, so the final field is unchanged.
	coeff.AC[n] -= netF;
}

// ==============================================================
// ============ WALL CONSUMPTION DIAGNOSTIC =====================
// ==============================================================
// Run once after the solve converges. For each reactive wall face it recomputes
// the same dPF / h the assembly used, and accumulates totals so the host can
// answer: how much substrate the wall removes, whether it is reaction- or
// mass-transfer-limited (OCR vs area*h*cp), and what fraction of the inlet
// supply that represents.
__global__
void wallConsumptionDiagnostic(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice bc,
	const double* phi,
	double D,
	double* diag
) {
	int f = blockIdx.x * blockDim.x + threadIdx.x;

	if (f >= mesh.faces.nFaces) return;
	if (mesh.faces.neighbor[f] >= 0) return;			// interior face

	int owner = mesh.faces.owner[f];
	if (owner < 0) return;
	if (!mesh.cells.active[owner]) return;

	int groupID = mesh.faces.boundaryGroupID[f];
	if (groupID < 0 || groupID >= bc.nGroups) return;

	uint8_t bcType = bc.typeByGroup[groupID];

	// mDot is stored positive outward from the owner, so F < 0 is inflow. Count
	// substrate carried in through Dirichlet-concentration inlet faces.
	double F = simple.mDot[f];
	if (isDirichletType(bcType) && F < 0.0) {
		atomicAdd(&diag[1], -F * bc.valueByGroup[groupID]);
	}

	if (!(isMichaelisMentenType(bcType) || isHillType(bcType))) return;

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, owner, f, normalZ, normalR);

	double dPF = getDistanceCellToFace(mesh, owner, f, normalZ, normalR);
	if (dPF <= 0.0) return;

	double h = 1.0 / (dPF / D + bc.RtotByGroup[groupID]);
	double area = mesh.faces.area[f];
	double cp = phi[owner];
	double cw = mesh.faces.cw[f];

	atomicAdd(&diag[0], mesh.faces.ocrWall[f]);			// total wall OCR
	atomicAdd(&diag[2], area * h * cp);					// mass-transfer ceiling
	atomicAdd(&diag[3], 1.0);							// reactive face count
	atomicAdd(&diag[4], cw);
	atomicAdd(&diag[5], dPF);
	atomicAdd(&diag[6], h);
	atomicAdd(&diag[7], cp);
	// Kinetics as the device actually sees them (base units), to catch a stale
	// or mis-scaled Vmax / Km that wouldn't match what the GUI displays.
	atomicAdd(&diag[8], bc.vmaxByGroup ? bc.vmaxByGroup[groupID] : 0.0);
	atomicAdd(&diag[9], bc.kmByGroup ? bc.kmByGroup[groupID] : 0.0);
}

// ==============================================================
// ==================TRANSIENT TERM==============================
// ==============================================================
__global__
void addTransientCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* phiOld,
	const double* phiOld2,
	double capacity,
	double dt
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;
	if (!mesh.cells.active[n]) return;

	// A null phiOld means the field is not being solved this run, and a
	// non-positive dt would divide by zero -- both leave the equation steady
	// rather than poisoning the diagonal.
	if (!phiOld || dt <= 0.0) return;

	double cV = capacity * mesh.cells.volume[n];

	// BDF2:  d(phi)/dt ~ (3*phi - 4*phiOld + phiOld2) / (2*dt)
	//
	// The 3/2 diagonal is LARGER than backward Euler's 1, so this stays
	// diagonally dominant. phiOld2 null means the caller has only one time level
	// available (the very first step of a run), which falls back to BDF1 -- the
	// standard startup for a multistep scheme, since there is no n-1 level yet.
	if (phiOld2) {

		double a = cV / (2.0 * dt);

		coeff.AC[n] += 3.0 * a;
		coeff.b[n] += a * (4.0 * phiOld[n] - phiOld2[n]);
		return;
	}

	// Backward Euler: d(phi)/dt ~ (phi - phiOld) / dt
	double a = cV / dt;

	coeff.AC[n] += a;
	coeff.b[n] += a * phiOld[n];
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
