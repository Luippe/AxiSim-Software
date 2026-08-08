#include "solver_util.cuh"
#include "device_launch_parameters.h"
#include <math_constants.h>
#include "printer.h"

#include "concentration_equation.cuh"

// ==============================================================
// ====================TREE REDUCTION============================
// ==============================================================

// The kernels stay in this TU rather than the header: they are __global__, so the
// separable-compilation inlining argument that keeps the face helpers in
// solver_util.cuh does not apply, and reduction() below is their only caller.

// Applied on load, exactly once, to the raw input -- the tree below then combines
// the transformed values with Op.
template <ReductionMethod M>
__device__ __forceinline__
double reductionLoad(double v) {
	if constexpr (M == ReductionMethod::ABSOLUTE) return fabs(v);
	else if constexpr (M == ReductionMethod::SQUARED) return v * v;
	else return v;
}

template <ReductionOp Op>
__device__ __forceinline__
double reductionCombine(double a, double b) {
	if constexpr (Op == ReductionOp::MAX) return fmax(a, b);
	else return a + b;
}

// Padding for the threads past N and for the tail of a partly filled block. It has
// to be the identity of Op or it contaminates the result -- 0.0 is not the identity
// of max over a field that is entirely negative, which the NONE transform allows.
template <ReductionOp Op>
__device__ __forceinline__
double reductionIdentity() {
	if constexpr (Op == ReductionOp::MAX) return -CUDART_INF;
	else return 0.0;
}

template <ReductionMethod M, ReductionOp Op>
__global__
void reduceBlock(int N, const double* __restrict__ in, double* __restrict__ out) {
	extern __shared__ double s[];
	int n = blockIdx.x * blockDim.x + threadIdx.x;
	int tid = threadIdx.x;		// thread id within the block

	s[tid] = (n < N) ? reductionLoad<M>(in[n]) : reductionIdentity<Op>();
	__syncthreads();

	// if you have [0, 1, 2, 3] as your input, the next iteration will give [2, 4]
	// the first element combines with the third, the second with the fourth.
	for (int stride = blockDim.x / 2; stride > 0; stride /= 2) {
		if (tid < stride) {
			s[tid] = reductionCombine<Op>(s[tid], s[tid + stride]);
		}
		__syncthreads();
	}

	if (tid == 0) {// store result for each block
		out[blockIdx.x] = s[0];
	}
}

// One pass of the tree. Split out of reduction() so Op stays a compile-time
// argument without writing the 3x2 launch matrix once per op.
template <ReductionOp Op>
static void launchReductionPass(
	int m,
	int blocks,
	const MemoryConfig& mem,
	cudaStream_t stream,
	const double* in,
	double* dst,
	ReductionMethod method
) {
	switch (method) {
	case ReductionMethod::ABSOLUTE:
		reduceBlock<ReductionMethod::ABSOLUTE, Op> << <blocks, mem.threadsPerBlock, mem.shmem, stream >> > (m, in, dst);
		break;
	case ReductionMethod::SQUARED:
		reduceBlock<ReductionMethod::SQUARED, Op> << <blocks, mem.threadsPerBlock, mem.shmem, stream >> > (m, in, dst);
		break;
	default:
		reduceBlock<ReductionMethod::NONE, Op> << <blocks, mem.threadsPerBlock, mem.shmem, stream >> > (m, in, dst);
		break;
	}
}

void reduction(
	int N,
	const MemoryConfig& mem,
	cudaStream_t stream,
	double* tmpA,
	double* tmpB,
	const double* in,
	double* store,
	ReductionMethod method,
	ReductionOp op
) {
	for (int m = N; m > 1; ) {
		int blocks = (m + mem.threadsPerBlock - 1) / mem.threadsPerBlock;
		double* dst = (blocks == 1) ? store : tmpA;

		if (op == ReductionOp::MAX) {
			launchReductionPass<ReductionOp::MAX>(m, blocks, mem, stream, in, dst, method);
		}
		else {
			launchReductionPass<ReductionOp::SUM>(m, blocks, mem, stream, in, dst, method);
		}

		// The transform is first-pass only; `op` deliberately is not reset, since
		// every level of the tree has to combine the level below it the same way.
		method = ReductionMethod::NONE;
		in = dst;
		std::swap(tmpA, tmpB);
		m = blocks;
	}
}

// ==============================================================
// ==================HELPER FUNCTIONS============================
// ==============================================================

__global__
void getCorrectionCoefficient(
	FVMeshDevice mesh,
	Coefficients coeff,
	double* D
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	D[n] = 0.0;

	double aP = coeff.AC[n];

	if (fabs(aP) < 1.0e-30) {
		D[n] = 0.0;
		return;
	}

	double volume = mesh.cells.volume[n];
	D[n] = volume / aP;

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


__global__
void copyVector(double* vec1, double* vec2, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) return;

	vec1[n] = vec2[n];
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

			double invDPN = mesh.faces.invCellToCell[faceID];

			double K = constVar * area * invDPN;

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
				AC[n] += K;
				b[n] += K * prescribedBoundaryFaceValue(
					mesh,
					faceID,
					bcType,
					bcValue,
					totalLength
				);
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

// ==============================================================
// ==================CONVECTION TERM=============================
// ==============================================================

__global__
void addRadialMomentumCylindricalSource(
	FVMeshDevice mesh,
	Coefficients coeff,
	double scalar
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;

	double volume = mesh.cells.volume[n];
	double r = mesh.cells.centerR[n];

	coeff.AC[n] += scalar * volume / (r * r);

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
void underRelaxEquation(
	FVMeshDevice mesh,
	Coefficients coeff,
	const double* x,
	double alpha
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= mesh.cells.nCells) return;


	if (alpha <= 0.0 || alpha > 1.0) return;

	double AC_old = coeff.AC[n];

	if (fabs(AC_old) < 1.0e-30) return;

	coeff.AC[n] = AC_old / alpha;

	coeff.b[n] += ((1.0 - alpha) / alpha) * AC_old * x[n];
}
