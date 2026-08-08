#pragma once
#include "cuda_runtime.h"
#include "solver_struct.h"
#include "boundary_struct.h"

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

// ==============================================================
// ====================TREE REDUCTION============================
// ==============================================================

// ReductionMethod lives in solver_struct.h, next to the residual norm/scaling enums.

// Reduce `in` (N elements) to one value in `store`, using tmpA/tmpB as ping-pong
// scratch. Each must hold at least ceil(N / mem.threadsPerBlock) doubles.
//
// The final kernel writes `store` directly from the device, so `store` must be
// device-accessible -- device memory or pinned host memory (cudaMallocHost). A
// plain host double will fault. Everything is enqueued on `stream`; the caller
// must synchronize before reading `store`.
//
// `method` applies to the FIRST pass only. Every later pass consumes partials, and
// the transforms do not survive that: a sum of squares squared again is not a sum
// of squares. So the transform degrades to NONE after the first launch.
//
// `op` is the opposite -- it holds for every pass, since the tree is only correct
// if each level combines the one below it the same way. ABSOLUTE + MAX gives the
// L-Inf norm; SUM (the default) gives every other norm the residuals use.
void reduction(
	int N,
	const MemoryConfig& mem,
	cudaStream_t stream,
	double* tmpA,
	double* tmpB,
	const double* in,
	double* store,
	ReductionMethod method,
	ReductionOp op = ReductionOp::SUM
);


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

// Weight on phi[cellID] for a linear interpolation to the face:
//   phiF = w * phi[cellID] + (1 - w) * phi[nb]
//
// faces.wP is stored owner-relative, but every interior face sits in BOTH cells'
// CSR lists, so a cell loop reaches it from either side. Flip when cellID is the
// neighbour -- same reason getOutwardNormalForCell above negates the normal.
__device__ __forceinline__
double getFaceWeightForCell(
	const FVMeshDevice& mesh,
	int cellID,
	int faceID
) {
	double w = mesh.faces.wP[faceID];

	return (mesh.faces.owner[faceID] == cellID) ? w : 1.0 - w;
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

	double DN = getNormalCorrectionCoeff(
		nb,
		faceID,
		mesh,
		simple
	);

	double w = getFaceWeightForCell(mesh, cellID, faceID);

	// Linear interpolation to face
	return w * DP + (1.0 - w) * DN;
}

// boolean helper functions
__device__ __forceinline__
bool isDirichletType(uint8_t type) {
	return type == (uint8_t)(BCType::DIRICHLET);
}

__device__ __forceinline__
bool isNeumannType(uint8_t type) {
	return type == (uint8_t)(BCType::NEUMANN);
}

__device__ __forceinline__
bool isFullyDevelopedType(uint8_t type) {
	return type == (uint8_t)(BCType::FULLY_DEVELOPED);
}

__device__ __forceinline__
bool isMichaelisMentenType(uint8_t type) {
	return type == (uint8_t)(BCType::MICHAELIS_MENTEN);
}

__device__ __forceinline__
bool isHillType(uint8_t type) {
	return type == (uint8_t)(BCType::HILL);
}

// Position of a face centre along the boundary it sits on, measured from the
// axis for a vertical boundary and from z = 0 for a horizontal one. normalZ^2 /
// normalR^2 pick the in-plane coordinate branchlessly, same trick as
// getNormalCorrectionCoeff.
__device__ __forceinline__
double getFaceCenterAlongOrientation(
	const FVMeshDevice& mesh,
	int faceID
) {
	double normalZ = mesh.faces.normalZ[faceID];
	double normalR = mesh.faces.normalR[faceID];

	double zF = mesh.faces.centerZ[faceID];
	double rF = mesh.faces.centerR[faceID];

	return rF * normalZ * normalZ
		+ zF * normalR * normalR;
}

// Face value for a boundary that prescribes one. DIRICHLET is uniform, but
// FULLY_DEVELOPED is a Dirichlet BC whose value VARIES along the boundary: a
// parabola peaking at bcValue on the axis and vanishing at totalLength.
//
// Every site needing a prescribed boundary face value must come through here.
// The profile used to be spelled out inline in the diffusion assembly and
// nowhere else, so the convection term and interpolateFieldToFace -- and
// therefore the Rhie-Chow face mass flux -- silently treated a fully-developed
// inlet as zero-gradient. The prescribed profile never reached the mass flux, so
// the realised inlet ran ~4% under the requested flow rate and the flow rate was
// an outcome of the solve rather than a boundary condition.
//
// totalLength is a float on the host and stays 0 for a group with no segments,
// so the degenerate case returns the uniform value instead of dividing by zero
// and seeding the whole solve with a NaN.
__device__ __forceinline__
double prescribedBoundaryFaceValue(
	const FVMeshDevice& mesh,
	int faceID,
	uint8_t bcType,
	double bcValue,
	double totalLength
) {
	if (!isFullyDevelopedType(bcType)) {
		return bcValue;
	}

	double x = getFaceCenterAlongOrientation(mesh, faceID) / totalLength;

	return bcValue * (1.0 - x * x);
}

__device__ __forceinline__
double interpolateFieldToFace(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const BoundaryFieldDevice& fieldBC,
	const double* phi
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	double phiP = phi[cellID];

	// ---------------- interior face ----------------
	if (neighbor >= 0) {
		int nb = (owner == cellID) ? neighbor : owner;

		double wP = getFaceWeightForCell(mesh, cellID, faceID);

		// Linear interpolation to face
		return wP * phiP + (1.0 - wP) * phi[nb];
	}


	// ---------------- boundary face ----------------
	int groupID = mesh.faces.boundaryGroupID[faceID];

	if (groupID < 0 || groupID >= fieldBC.nGroups) {
		// Default: zero-gradient
		return phiP;
	}

	uint8_t bcType = fieldBC.typeByGroup[groupID];
	double bcValue = fieldBC.valueByGroup[groupID];

	// Prescribed-value boundaries. DIRICHLET is uniform, FULLY_DEVELOPED varies
	// along the boundary; the helper resolves which.
	if (isDirichletType(bcType) || isFullyDevelopedType(bcType)) {
		return prescribedBoundaryFaceValue(
			mesh,
			faceID,
			bcType,
			bcValue,
			fieldBC.lengthByGroup[groupID]
		);
	}
	else if (isNeumannType(bcType)) {
		// dphi/dn = bcValue
		// zero-gradient means bcValue = 0, so phiF = phiP
		return phiP + bcValue * mesh.faces.dPB[faceID];
	}

	// Only NONE and the kinetics walls (Michaelis-Menten / Hill) land here. Note
	// the face value at a reactive wall is really mesh.faces.cw, which the
	// diffusion assembly uses directly -- so a gradient taken through this
	// function sees a reactive wall as zero-gradient.
	return phiP;
}

// ======================================================================
// Computational helpers, moved here from solver_util.cu
//
// Same reason as the block above. Under CUDA_SEPARABLE_COMPILATION each of
// these compiled to a standalone ABI function: every call marshalled its
// by-value structs through local memory, paid a callee-save prologue, and
// blocked the compiler from hoisting the repeated mesh.faces.* loads across
// the call boundary. `cuobjdump -res-usage` and ptxas -v showed all eleven
// carrying a 32-byte frame with 32 bytes of save/restore traffic each.
//
// Same rule as above applies to anything added here: hot-loop helpers taking
// the big structs by const reference. Anything called once per cell can stay
// in the .cu.
// ======================================================================

// find value of varaible at the adjacent cell. also finds coord, the coordinate of the cell
__device__ __forceinline__
double phiAtSide(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const BoundaryFieldDevice& phiBC,
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

__device__ __forceinline__
double getCellToCellDotNorm(
	const FVMeshDevice& mesh,
	int cellID,
	int nb,
	double normalZ,
	double normalR
) {
	double dz = mesh.cells.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.cells.centerR[nb] - mesh.cells.centerR[cellID];

	double len = sqrt(dz * dz + dr * dr);

	return (normalZ * dz + normalR * dr) / len;	// cos(theta) = n . d / |d|  (n is unit)
}

__device__ __forceinline__
double getCellToFaceDotNorm(
	const FVMeshDevice& mesh,
	int cellID,
	int nb,
	double normalZ,
	double normalR
) {
	double dz = mesh.faces.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.faces.centerR[nb] - mesh.cells.centerR[cellID];

	double len = sqrt(dz * dz + dr * dr);

	return (normalZ * dz + normalR * dr) / len;	// cos(theta) = n . d / |d|
}

__device__ __forceinline__
double nonOrthoScalarDiffusionFlux(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const double* gradPhiZ,
	const double* gradPhiR,
	double gamma
) {
	int owner = mesh.faces.owner[faceID];
	int neighbor = mesh.faces.neighbor[faceID];

	if (neighbor < 0) {
		return 0.0;
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double normalZ, normalR;
	getOutwardNormalForCell(mesh, cellID, faceID, normalZ, normalR);

	double area = mesh.faces.area[faceID];

	// check if cell-cell or cell-face has greater non-orthogonality. fix the one that has the most non-orthogonality
	double ndCellCell = getCellToCellDotNorm(mesh, cellID, nb, normalZ, normalR);

	double dz = mesh.cells.centerZ[nb] - mesh.cells.centerZ[cellID];
	double dr = mesh.cells.centerR[nb] - mesh.cells.centerR[cellID];
	double invDOrth = mesh.faces.invCellToCell[faceID];

	//double signedDOrth = (nd < 0.0) ? -dOrth : dOrth;
	double aOverNd = area * invDOrth;
	double Tz = area * normalZ - aOverNd * dz;
	double Tr = area * normalR - aOverNd * dr;

	// Face gradient from the precomputed cell-centered gradients (built once
	// per iteration with the user-selected scheme), distance-weighted to the
	// face. The closer cell gets more weight (same convention as
	// interpolateFieldToFace), staying second-order on stretched cells where a
	// plain average would not. Symmetric under owner<->neighbor swap, so the
	// pressure-correction RHS and the mass-flux correction stay consistent.
	double wP = getFaceWeightForCell(mesh, cellID, faceID);

	double gradFaceZ = wP * gradPhiZ[cellID] + (1.0 - wP) * gradPhiZ[nb];
	double gradFaceR = wP * gradPhiR[cellID] + (1.0 - wP) * gradPhiR[nb];

	return gamma * (Tz * gradFaceZ + Tr * gradFaceR);
}

__device__ __forceinline__
int findFaceOnSide(
	const FVMeshDevice& mesh,
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

__device__ __forceinline__
void phiGradientGreenGauss(
	int cellID,
	const FVMeshDevice& mesh,
	const BoundaryFieldDevice& bc,
	const double* phi,
	double& gradZ,
	double& gradR
) {
	// Accumulate in locals, not through gradZ/gradR. Those are references to global memory
	// (computeGradient binds them to gradZ[n]/gradR[n]), and since phi and the gradient
	// arrays are unqualified double* the compiler must assume they alias -- so it cannot
	// hold the running sum in a register and every += below compiles to a global
	// load-modify-store, per face. Locals stay in registers and commit once at the end.
	double gz = 0.0;
	double gr = 0.0;

	// The stored face areas and cell volumes are the *revolved* (axisymmetric)
	// metrics: area = 2*pi*rf*L2D and volume = 2*pi*rc*A2D. Green-Gauss for the
	// meridional-plane gradient (d/dz, d/dr) is a purely 2D operation, so it must
	// use the planar face length L2D and cell area A2D -- feeding the revolved
	// area/volume in directly biases the radial gradient (a constant field would
	// give grad_r = c/rc, worst near the axis). Recover the planar metrics from
	// the revolved ones: L2D = area/(2*pi*rf), A2D = volume/(2*pi*rc) -- the latter
	// precomputed per cell as invA2D.
	constexpr double twoPi = 6.28318530717958647692;

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

			gz += phiF * normalZ * length2D;
			gr += phiF * normalR * length2D;

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
		gz += axisPhiF * (-closureZ);
		gr += axisPhiF * (-closureR);
	}

	// divide by the planar cell area A2D
	double invA2D = mesh.cells.invA2D[cellID];
	gradZ = gz * invA2D;
	gradR = gr * invA2D;
}

__device__ __forceinline__
void phiGradientLeastSquare(
	int cellID,
	const FVMeshDevice& mesh,
	const BoundaryFieldDevice& bc,
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

__device__ __forceinline__
double rhieChowNormalVelocityToFace(
	int cellID,
	int faceID,
	const FVMeshDevice& mesh,
	const VariablesSimple& simple,
	const BoundarySolverDevice& bc
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

	// ---------------- boundary face ----------------
	if (neighbor < 0) {
		// Only a fixed-pressure boundary (pressure outlet) couples its face flux
		// to the pressure field. Every other boundary type carries zero-gradient
		// p and has its flux set by the velocity BC alone; adding a pressure term
		// there would inject a flux the p' equation never accounts for, since
		// createPPCoeff and updateMassFlux both skip non-Dirichlet p faces.
		//
		// Without this branch the outlet flux was rho*A*u_P.n, so the prescribed
		// p_b reached the solution only through the momentum body force. That
		// left the two halves of SIMPLE inconsistent: the p' equation already
		// assembles d(mDot_b)/d(p_P) = +rho*A*Df/dPB for this face (the exact
		// sensitivity the term below produces), while the flux it corrects had
		// no pressure dependence at all. A case driven purely by a pressure
		// difference therefore never felt the outlet pressure.
		int groupID = mesh.faces.boundaryGroupID[faceID];

		if (groupID < 0 || groupID >= bc.p.nGroups) {
			return unLinear;
		}

		if (!isDirichletType(bc.p.typeByGroup[groupID])) {
			return unLinear;
		}

		double dPB = mesh.faces.dPB[faceID];

		// Same Rhie-Chow form as the interior face below, with the neighbour cell
		// replaced by the boundary face: compact pressure difference across
		// P -> b, minus the smooth cell-centred gradient. There is no second cell
		// to interpolate that gradient with, so P's own value is used directly.
		//
		// The two gradients cancel to leading order for a smooth pressure field,
		// so this reduces to unLinear once p_P sits where the prescribed p_b
		// implies -- including on a cold start, where p = 0 everywhere but
		// grad(p) already carries p_b through interpolateFieldToFace.
		double DfB = interpolateNormalCorrectionCoeffToFace(
			cellID,
			faceID,
			mesh,
			simple
		);

		double gradPCompact = (bc.p.valueByGroup[groupID] - simple.p[cellID]) / dPB;

		double gradPCellNormal =
			simple.gradPZ[cellID] * normalZ +
			simple.gradPR[cellID] * normalR;

		return unLinear - DfB * (gradPCompact - gradPCellNormal);
	}

	int nb = (owner == cellID) ? neighbor : owner;

	double invDPN = mesh.faces.invCellToCell[faceID];

	double pP = simple.p[cellID];
	double pN = simple.p[nb];

	// Direct pressure gradient between cell centers
	double gradPN = (pN - pP) * invDPN;

	// Interpolate precomputed Green-Gauss pressure gradients to the face
	double w = getFaceWeightForCell(mesh, cellID, faceID);

	double gradPzF = w * simple.gradPZ[cellID] + (1.0 - w) * simple.gradPZ[nb];
	double gradPrF = w * simple.gradPR[cellID] + (1.0 - w) * simple.gradPR[nb];

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

__device__ __forceinline__
double faceValue(double phiC, double phiF, double dFf, double dFC) {
	double gC = dFf / dFC;
	return phiC * gC + (1 - gC) * phiF;
}

// Higher-order face value for the deferred correction. Returns the first-order
// upwind value itself when the scheme is upwind or the data it needs is missing,
// which makes the correction below vanish.
__device__ __forceinline__
double higherOrderFaceValue(
	int n,
	int nb,
	int faceID,
	const FVMeshDevice& mesh,
	const BoundaryFieldDevice& fieldBC,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	ConvectionScheme scheme,
	double F,
	double phiUD
) {
	switch (scheme) {
	case ConvectionScheme::CONV_CENTRAL:
		// central difference is second order
		// oscillatory above cell Peclet ~2 regardless of the face value's range
		return interpolateFieldToFace(n, faceID, mesh, fieldBC, phi);

	case ConvectionScheme::CONV_SECOND_ORDER_UPWIND: {

		// Second-order (linear) upwind extrapolate from the UPWIND cell along the vector to
		// the face using that cell's gradient.
		//
		// phi_f = phi_U + grad(phi)_U . (r_f - r_U)

		int up = (F > 0.0) ? n : nb;

		double dz = mesh.faces.centerZ[faceID] - mesh.cells.centerZ[up];
		double dr = mesh.faces.centerR[faceID] - mesh.cells.centerR[up];

		double phiHO = phi[up] + gradPhiZ[up] * dz + gradPhiR[up] * dr;

		// Clipping reduces the scheme to upwind at local extrema, which is what makes it bounded.
		double lo = fmin(phi[n], phi[nb]);
		double hi = fmax(phi[n], phi[nb]);

		return fmin(fmax(phiHO, lo), hi);
	}

	default:
		return phiUD;
	}
}

__device__ __forceinline__
void addConvectionContribution(
	int n,
	int nb,
	int faceID,
	int slot,
	const FVMeshDevice& mesh,
	double F,
	bool isBoundary,
	int groupID,
	const Coefficients& coeff,
	const BoundaryFieldDevice& fieldBC,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	ConvectionScheme scheme
) {
	// ------------------------------------------------------------
	// Interior face
	// ------------------------------------------------------------
	if (!isBoundary) {

		// First-order upwind
		coeff.AC[n] += fmax(F, 0.0);

		double aNb = fmin(F, 0.0);

		coeff.AF[slot] += aNb;

		if (scheme != ConvectionScheme::CONV_UPWIND && phi) {

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
	else if (isNeumannType(bcType)) {
		// zero-gradient:
		// phi_f = phi_P.
		coeff.AC[n] += F;
	}
	else if (isFullyDevelopedType(bcType)) {
		// Prescribed parabolic inlet, so the same inflow/outflow split as
		// Dirichlet above: the profile is only known on the way IN. Reverse flow
		// out through a velocity inlet happens on a cold start, and the RHS form
		// would flip the source sign there and leave AC without its outflow
		// term, costing the row its diagonal dominance.
		if (F < 0.0) {
			coeff.b[n] += -F * prescribedBoundaryFaceValue(
				mesh,
				faceID,
				bcType,
				bcValue,
				fieldBC.lengthByGroup[groupID]
			);
		}
		else {
			coeff.AC[n] += F;
		}
	}
}

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

// Assembles diffusion AND convection in ONE pass over the cell's faces. These
// were two kernels walking the identical cells.faceStart CSR and accumulating
// into the same AC[n] / b[n] / AF[k]; fusing them halves the traversal and the
// shared per-face loads (faceID, owner, neighbor, area). Every contribution is a
// `+=`, so the merged loop assembles the same matrix -- up to the summation order
// on AC[n], which now interleaves the two terms face by face instead of running
// diffusion to completion first.
//
// `constVar` is the diffusivity: mu for momentum, a kinematic diffusivity for a
// scalar. `addConvection` = 0 assembles diffusion alone and leaves `scheme` and
// `fluxScale` unread.
//
// fluxScale multiplies the face MASS flux (mDot = rho*u*area) before it is used
// as the convecting flux F. Momentum convects mass, so it passes 1.0. A passive
// scalar (species concentration) convects with the VOLUMETRIC flux u*area, so it
// passes 1/rho to divide the density out and stay consistent with the kinematic
// diffusivity it is handed as constVar.
//
// `scheme` selects the face interpolation, applied by DEFERRED CORRECTION: the
// matrix is always the first-order upwind operator and the higher-order difference
// is added to the RHS, lagged one outer iteration. That keeps the system an
// M-matrix for Jacobi/Gauss-Seidel/multigrid while converging to the higher-order
// solution.
//
// CONV_SECOND_ORDER_UPWIND and CONV_QUICK read gradPhiZ/gradPhiR at the upwind
// cell, and applyNonOrtho reads them at this cell, so the caller MUST have filled
// them for this field before launching. Passing null gradients silently degrades
// those schemes to upwind. CONV_UPWIND and CONV_CENTRAL ignore them.
__global__ void
addTransportCoefficients(
	FVMeshDevice mesh,
	VariablesSimple simple,
	Coefficients coeff,
	BoundaryFieldDevice bc,
	const double* phi,
	const double* gradPhiZ,
	const double* gradPhiR,
	int applyNonOrtho,
	double constVar,
	int addConvection,
	ConvectionScheme scheme,
	double fluxScale
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

__global__
void addRadialMomentumCylindricalSource(
	FVMeshDevice mesh,
	Coefficients coeff,
	double scalar
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


__device__ __forceinline__
void clearCoefficients(Coefficients& coeff) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;

	coeff.AC[n] = 0.0;
	coeff.b[n] = 0.0;

	int start = coeff.faceStart[n];
	int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		coeff.AF[k] = 0.0;
	}
}

template <typename...Args>
__global__
void clearAllCoefficients(Args... args) {
	(clearCoefficients(args), ...);
}

// Reciprocal of the diagonal, so the smoothers multiply instead of divide -- an
// FP64 divide is ~20x a multiply on sm_86 and ran once per cell per sweep.
//
// Launch after EVERY kernel that touches AC (under-relaxation included), once per
// coefficient set that gets solved. pp is assembled by createPPCoeff and is never
// under-relaxed, so it needs its own launch -- folding this into another kernel is
// what kept leaving one field behind.
//
// Writing every cell every time is deliberate: a row skipped here keeps the
// PREVIOUS outer iteration's reciprocal, and the smoothers cannot tell that stale
// nonzero from a live one.
//
// invAC == 0 encodes a collapsed row (|AC| ~ 0), so no consumer needs its own test.
__global__
void buildInverseDiagonal(Coefficients coeff);

__global__
void underRelaxEquation(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* x,
	double alpha
);
