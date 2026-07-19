#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"

// reduction kernel
void reduction(int N, int threadsPerBlock, size_t shmem, cudaStream_t stream, double* tmpA, double* tmpB, double* in, double* store);

// ======================================================================
// Inline face helpers
//
// These live in the header, not solver_util.cu, because the project builds
// with CUDA_SEPARABLE_COMPILATION ON: a __device__ function defined in another
// translation unit cannot be inlined, so each call becomes a real ABI call and
// every by-value struct parameter is copied through local memory. createPPCoeff
// makes ~9 such calls per face and paid 1.87 ms per launch for it, against
// 20 us for the structurally identical createPPRhs -- a 616-byte per-thread
// stack frame, all of it parameter copies.
//
// Defined here and taking the big structs by const reference, they inline at
// the call site and the stack frame goes away. Keep new hot-loop helpers here
// for the same reason; anything called once per cell can stay in the .cu.
// ======================================================================

__device__ __forceinline__
void getOutwardNormalForCell(
	const FVMeshDevice& mesh,
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

__device__ __forceinline__
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

__device__ __forceinline__
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

__device__ __forceinline__
double getNormalCorrectionCoeff(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const VariablesSimple& simple
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

__device__ __forceinline__
double interpolateNormalCorrectionCoeffToFace(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const VariablesSimple& simple
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

// Scatters aNb into the matrix slot linking cell n to neighbour nb.
//
// The face path searches n's own neighbour list for nb, which is O(faces per
// cell) per call. Callers that are already walking that list know the slot
// index and should write coeff.AF[k] directly instead of calling this.
__device__ __forceinline__
void addNeighborCoeff(
	int n,
	int nb,
	const FVMeshDevice& mesh,
	double aNb,
	const Coefficients& coeff
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

// boolean helper functions
__device__ __forceinline__
bool isDirichletType(uint8_t type) {
	return type == (uint8_t)(DIRICHLET);
}

__device__ __forceinline__
bool isNeumannType(uint8_t type) {
	return type == (uint8_t)(NEUMANN);
}

__device__ __forceinline__
bool isFullyDevelopedType(uint8_t type) {
	return type == (uint8_t)(FULLY_DEVELOPED);
}

__device__ __forceinline__
bool isMichaelisMentenType(uint8_t type) {
	return type == (uint8_t)(MICHAELIS_MENTEN);
}

__device__ __forceinline__
bool isHillType(uint8_t type) {
	return type == (uint8_t)(HILL);
}

// computational helper functions
__device__
void phiGradientGreenGauss(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
);

__device__
void phiGradientLeastSquare(
	int cellID,
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	const double* phi,
	double& gradZ,
	double& gradR
);


__device__
double interpolateFieldToFace(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	BoundaryFieldDevice fieldBC,
	const double* phi
);

__device__
double nonOrthoScalarDiffusionFlux(
	int cellID,
	int faceID,
	FVMeshDevice mesh,
	const double* gradPhiZ,
	const double* gradPhiR,
	double gamma
);

__global__
void computeGradient(
	FVMeshDevice mesh,
	BoundaryFieldDevice bc,
	double* phi,
	double* gradZ,
	double* gradR,
	GradientScheme scheme
);

__global__
void copyVector(double* vec1, double* vec2, int N);

__device__
double faceValue(double phiC, double phiF, double dFf, double dFC);

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
);

// Implicit unsteady term, assembled so the system solves for phi at the NEW time
// level. phiOld2 (time level n-1) selects the scheme:
//
//   null      backward Euler, first order:  AC += c*V/dt
//   non-null  BDF2, second order:           AC += 3/2 * c*V/dt
//
// Pass null on the first step of a run even in BDF2 mode -- there is no n-1 level
// to difference against yet, which is the standard multistep startup.
//
// `capacity` is whatever multiplies d(phi)/dt in the conservation equation, and must
// match the flux scaling the rest of the equation uses. Momentum convects the MASS
// flux and diffuses with mu, so it passes rho. Temperature and concentration convect
// the VOLUMETRIC flux (fluxScale = 1/rho) and diffuse with a kinematic diffusivity,
// so they pass 1.0 -- passing rho there would silently rescale their time constant.
//
// Cell volume comes from the mesh, so this works on every mesh type, unlike the
// structured-index version it replaces (which indexed g.d_rFace/d_dz through nr/nz
// and could never run on the face path).
__global__
void addTransientCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* phiOld,
	const double* phiOld2,
	double capacity,
	double dt
);

// fluxScale multiplies the face MASS flux (mDot = rho*u*area) before it is used
// as the convecting flux F. Momentum convects mass, so it passes 1.0. A passive
// scalar (species concentration) convects with the VOLUMETRIC flux u*area, so it
// passes 1/rho to divide the density out and stay consistent with the kinematic
// diffusivity (f.D) used by addDiffusionCoefficient.
//
// `scheme` selects the face interpolation, applied by DEFERRED CORRECTION: the
// matrix is always the first-order upwind operator and the higher-order difference
// is added to the RHS, lagged one outer iteration. That keeps the system an
// M-matrix for Jacobi/Gauss-Seidel/multigrid while converging to the higher-order
// solution.
//
// CONV_SECOND_ORDER_UPWIND and CONV_QUICK read gradPhiZ/gradPhiR at the upwind
// cell, so the caller MUST have filled them for this field before launching this
// kernel. Passing null gradients silently degrades those schemes to upwind.
// CONV_UPWIND and CONV_CENTRAL ignore them.
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
);

// One-shot post-solve diagnostic for reactive (Michaelis-Menten / Hill) walls.
// Accumulates per-face contributions into diag[0..7] via atomics; the host then
// reports wall consumption, the mass-transfer ceiling, and depletion. Layout:
//   [0] total wall OCR      [amount/s]   [1] inlet substrate flux   [amount/s]
//   [2] mass-transfer ceil  [amount/s]   [3] reactive face count
//   [4] sum(cw)  [5] sum(dPF)  [6] sum(h)  [7] sum(cp)
__global__
void wallConsumptionDiagnostic(
	FVMeshDevice mesh,
	VariablesSimple simple,
	BoundaryFieldDevice bc,
	const double* phi,
	double D,
	double* diag
);

__global__
void clearCoefficients(Coefficients coeff);

__global__
void underRelaxEquation(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* x,
	double alpha
);
