#include "multigrid.cuh"


#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include "residuals.cuh"

#include "memory_manager.h"
#include "printer.h"

// CUDA_CHECK arrives via multigrid.cuh -> solver_struct.h -> gpu_utils.h. This TU
// used to re-#define a token-identical copy, which is a legal (hence silent)
// redefinition -- and one that would have quietly kept the old behaviour the day
// the shared macro changed.

MultigridSolver::MultigridSolver(ConfigMultigrid& cfg, MemoryConfig& mem, GridLevel& grid) :
	cfg(cfg),
	mem(mem) {

	// init once
	buildLevels(std::move(grid));
	CUDA_CHECK(cudaGetLastError());
	print(cfg.linearPostSweep, cfg.linearSweep, cfg.linearPreSweep);

}

MultigridSolver::~MultigridSolver() {
	destroyRunGraph();

	for (MultigridLevel& level : levels) {
		freeMultigridLevel(level);
	}
}

GridLevel makeFinestGridLevel(const FVMesh& mesh) {

	GridLevel level;

	const int N = mesh.numCells();

	level.nCells = N;
	buildCellFaceCSR(mesh, level.faceStart, level.faceNeighbor);

	return level;
}

// Stop coarsening below this many cells: the coarsest level is solved by brute
// smoothing, so shrinking further buys nothing and costs a level of memory.
static const int minCoarseCells = 64;

// A pass must shrink the graph by at least this factor to be worth keeping. A
// pass that barely shrinks means agglomeration stalled, and another level would
// add work without improving convergence.
static const double maxShrinkRatio = 0.8;

// ============================================================================
// PASS 1 -- grouping. Greedy agglomeration over the face graph: every unclaimed
// cell seeds a coarse cell and pulls in up to (target - 1) unclaimed neighbours.
//
// This is the ONLY mesh-type-specific step in the hierarchy build, and it is
// written generically so multiblock and unstructured both work -- both reach the
// solver as a plain cell/face graph. Swap in an index-based map here (per-block
// i/2, j/2 + block offset) if deterministic square agglomerates are wanted on
// multiblock; nothing downstream needs to change.
//
// Every cell is agglomerated -- the mesh no longer carries an inactive/solid
// mask, so there is no class of row that has to be kept out of a coarse cell.
//
// Returns the coarse cell count. cellToCoarse comes out dense over [0, nCoarse):
// the coarse arrays are indexed by it directly, so gaps are not allowed, which is
// what the compaction pass at the bottom guarantees.
// ============================================================================
static int buildAgglomerationMap(const GridLevel& fine, std::vector<int>& cellToCoarse) {

	// 4 keeps the coarsening ratio near the 2x2 blocks the structured version
	// used, so hierarchy depth stays comparable.
	const int target = 4;

	cellToCoarse.assign(fine.nCells, -1);

	int nCoarse = 0;

	// Cells claimed by the agglomerate currently being grown. Declared out here and
	// reused so growing a coarse cell does not allocate.
	std::vector<int> members;
	members.reserve(target);

	for (int n = 0; n < fine.nCells; n++) {

		if (cellToCoarse[n] >= 0) continue;   // already pulled into an earlier seed

		const int c = nCoarse++;
		cellToCoarse[n] = c;

		members.clear();
		members.push_back(n);

		// Grow breadth-first: walk the cells claimed so far and keep taking their
		// unclaimed neighbours until the agglomerate reaches `target`.
		//
		// Scanning only the SEED's own neighbours is not enough. In the CSR order a
		// mesh actually produces, the neighbours "behind" cell n have already been
		// claimed by earlier seeds by the time n seeds, so a seed finds only one or
		// two free neighbours. That capped the real coarsening ratio at ~2x instead
		// of the 4x `target` asks for -- which doubled the level count and let the
		// coarse graph degree climb past the fine mesh's, since a 2-cell agglomerate
		// inherits nearly every neighbour of both its members.
		for (int m = 0; m < (int)members.size() && (int)members.size() < target; m++) {

			const int cell = members[m];

			for (int k = fine.faceStart[cell]; k < fine.faceStart[cell + 1] && (int)members.size() < target; k++) {

				const int nb = fine.faceNeighbor[k];

				if (nb < 0) continue;                                            // boundary slot
				if (cellToCoarse[nb] >= 0) continue;                             // already claimed

				cellToCoarse[nb] = c;
				members.push_back(nb);
			}
		}
	}

	// ---- singleton cleanup --------------------------------------------------
	// Sequential seeding strands cells whose neighbours were all claimed by
	// earlier seeds. A one-cell agglomerate coarsens nothing AND inherits every
	// one of its fine neighbours, so it is what drives the coarse graph degree
	// up -- the damage is bigger than the cell count suggests.
	//
	// Merge each stranded cell into its SMALLEST adjacent agglomerate. Picking
	// the smallest keeps growth self-limiting: an agglomerate only absorbs a
	// singleton while it is the least-bad option, so no cap is needed.
	std::vector<int> agglomSize(nCoarse, 0);
	for (int n = 0; n < fine.nCells; n++) {
		agglomSize[cellToCoarse[n]]++;
	}

	for (int n = 0; n < fine.nCells; n++) {

		const int c = cellToCoarse[n];
		if (agglomSize[c] != 1) continue;

		int best = -1;
		int bestSize = 0;

		for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

			const int nb = fine.faceNeighbor[k];

			if (nb < 0) continue;

			const int cnb = cellToCoarse[nb];
			if (cnb == c) continue;   // unreachable for a singleton, but cheap

			if (best < 0 || agglomSize[cnb] < bestSize) {
				best = cnb;
				bestSize = agglomSize[cnb];
			}
		}

		if (best < 0) continue;   // genuinely isolated -- leave it as its own cell

		cellToCoarse[n] = best;
		agglomSize[best]++;
		agglomSize[c] = 0;
	}

	// ---- compaction ---------------------------------------------------------
	// Merging empties coarse cells, and cellToCoarse must stay dense over
	// [0, nCoarse) because the coarse arrays are indexed by it directly. Renumber
	// by first appearance, which also happens to improve coarse-level locality.
	std::vector<int> remap(nCoarse, -1);
	int nCompact = 0;

	for (int n = 0; n < fine.nCells; n++) {

		const int c = cellToCoarse[n];

		if (remap[c] < 0) {
			remap[c] = nCompact++;
		}

		cellToCoarse[n] = remap[c];
	}

	return nCompact;
}

GridLevel MultigridSolver::coarsenGrid(GridLevel& fine) {

	GridLevel coarse;

	// ---- pass 1: group fine cells into coarse cells -------------------------
	const int nCoarse = buildAgglomerationMap(fine, fine.cellToCoarse);

	coarse.nCells = nCoarse;

	// ---- pass 2: fine slots that cross an agglomerate boundary --------------
	// A fine face contributes a coarse off-diagonal only when its two ends land
	// in DIFFERENT coarse cells. Faces internal to an agglomerate fold into the
	// coarse diagonal, and boundary slots contribute nothing (their effect is
	// already in the fine AC / b).
	//
	// The fine slot index rides along as a third field so the coarse CSR and
	// fine.fineSlotToCoarseSlot come out of ONE traversal. This used to be two
	// walks of the whole fine graph carrying the same three skip conditions, the
	// second recovering each slot's coarse index with a std::lower_bound over the
	// finished coarse row. Sorting by (coarse cell, coarse neighbour) already
	// groups every reference to a given coarse off-diagonal, so pass 3 can emit
	// the entry once and hand its index to every fine slot that feeds it -- the
	// same number the binary search returned, without the search or the second
	// walk, and without the lockstep-drift hazard of two loops that had to keep
	// identical skip conditions.
	struct SlotRef {
		int coarseCell;
		int coarseNeighbor;
		int fineSlot;
	};

	std::vector<SlotRef> refs;
	refs.reserve(fine.nFaceRefs());

	for (int n = 0; n < fine.nCells; n++) {

		const int cn = fine.cellToCoarse[n];

		for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

			const int nb = fine.faceNeighbor[k];
			if (nb < 0) continue;

			const int cnb = fine.cellToCoarse[nb];
			if (cnb != cn) {
				refs.push_back(SlotRef{ cn, cnb, k });
			}
		}
	}

	// Ordered on the pair only. Slots sharing a pair may come out in any relative
	// order because they all receive the same coarse slot index anyway.
	std::sort(refs.begin(), refs.end(), [](const SlotRef& a, const SlotRef& b) {
		if (a.coarseCell != b.coarseCell) return a.coarseCell < b.coarseCell;
		return a.coarseNeighbor < b.coarseNeighbor;
	});

	// ---- pass 3: emit the coarse CSR and the fine -> coarse slot map --------
	// fineSlotToCoarseSlot is -1 for "feeds no coarse off-diagonal", covering two
	// different cases the coarse-operator kernel tells apart via faceNeighbor[k]:
	//   faceNeighbor[k] >= 0 -> internal to an agglomerate, folds into the diagonal
	//   faceNeighbor[k] <  0 -> boundary slot, contributes nothing
	fine.fineSlotToCoarseSlot.assign(fine.nFaceRefs(), -1);

	coarse.faceStart.assign(nCoarse + 1, 0);
	coarse.faceNeighbor.clear();
	coarse.faceNeighbor.reserve(refs.size());

	for (size_t i = 0; i < refs.size(); i++) {

		const bool newEntry =
			i == 0 ||
			refs[i].coarseCell != refs[i - 1].coarseCell ||
			refs[i].coarseNeighbor != refs[i - 1].coarseNeighbor;

		// Distinct pairs arrive in ascending coarse-cell order, so pushing them as
		// they appear lays faceNeighbor out row by row, matching the prefix sum
		// below -- and ascending in coarse neighbour within each row.
		if (newEntry) {
			coarse.faceNeighbor.push_back(refs[i].coarseNeighbor);
			coarse.faceStart[refs[i].coarseCell + 1]++;
		}

		fine.fineSlotToCoarseSlot[refs[i].fineSlot] = (int)coarse.faceNeighbor.size() - 1;
	}

	for (int c = 0; c < nCoarse; c++) {
		coarse.faceStart[c + 1] += coarse.faceStart[c];
	}

	return coarse;
}

MultigridLevel MultigridSolver::createMultigridLevel(GridLevel grid) {

	MultigridLevel level;
	level.grid = std::move(grid);
	allocateMultigridLevel(level);
	return level;

}

void MultigridSolver::buildLevels(GridLevel fine) {

	// Each GridLevel is moved into its MultigridLevel as it is finished, so the
	// host connectivity exists in exactly one place. `fine` is the level being
	// coarsened FROM and is not yet owned by `levels` -- coarsenGrid writes its
	// cellToCoarse / fineSlotToCoarseSlot, so it can only be handed over once the
	// coarse level below it is known.
	while (true) {

		if (fine.nCells <= minCoarseCells) break;

		// built speculatively: the shrink test needs the coarse cell count. On
		// reject, the maps coarsenGrid just wrote are cleared again so `fine`
		// correctly reports isCoarsest().
		GridLevel coarse = coarsenGrid(fine);

		if (coarse.nCells >= (int)(fine.nCells * maxShrinkRatio)) {
			fine.cellToCoarse.clear();
			fine.fineSlotToCoarseSlot.clear();
			break;
		}

		levels.push_back(createMultigridLevel(std::move(fine)));
		fine = std::move(coarse);
	}

	levels.push_back(createMultigridLevel(std::move(fine)));
}



// ============================================================================
// Galerkin coarse operator, A_H = R A_h P with P = piecewise-constant injection
// and R = P^T. That reduces to one rule: every fine matrix entry A[row][col]
// adds into the coarse entry A_H[c(row)][c(col)]. Which gives four cases:
//
//   AC[n]                          -> AC_H[c(n)]
//   AF[k], c(nb) == c(n)           -> AC_H[c(n)]   (internal, folds into diagonal)
//   AF[k], c(nb) != c(n)           -> AF_H[fineSlotToCoarseSlot[k]]
//   boundary slot (faceNeighbor<0) -> nothing; already folded into fine AC / b
//
// One thread per FINE cell, scattering with atomicAdd -- the coarse row a fine
// entry lands in was resolved on the host, so there is no search here.
//
// This replaces Route A (average the two fine face coeffs, then force
// AC = -(sum of neighbours)). Summing is the correct Galerkin operator AND it
// preserves Dirichlet anchoring: a pinned fine cell carries extra weight in AC
// with no matching off-diagonal, which forcing row-sum-zero would discard,
// leaving the coarse level singular.
//
// The caller must zero coarse AC / AF first -- this accumulates.
// ============================================================================
__global__
void buildCoarseOperatorKernel(
	Coefficients fine,
	Coefficients coarse,
	const int* cellToCoarse,
	const int* fineSlotToCoarseSlot
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;

	const int cn = cellToCoarse[n];

	// This row's whole contribution to the coarse DIAGONAL -- its AC plus every
	// face internal to the agglomerate -- accumulates in a register and lands as
	// ONE atomic. Every member of an agglomerate targets the same coarse.AC[cn],
	// and BFS growth tends to make those consecutive cell indices, so issuing an
	// atomic per internal face was serializing same-warp lanes on one address.
	// This regroups the floating-point summation; it does not make the result any
	// less reproducible, since the remaining cross-thread atomicAdd was already
	// order-dependent.
	double diag = fine.AC[n];

	for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

		const int nb = fine.faceNeighbor[k];
		if (nb < 0) continue;             // boundary slot

		const int slot = fineSlotToCoarseSlot[k];

		if (slot >= 0) {
			atomicAdd(&coarse.AF[slot], fine.AF[k]);
		}
		else {
			diag += fine.AF[k];
		}
	}

	atomicAdd(&coarse.AC[cn], diag);
}


// Restriction, R = P^T for piecewise-constant injection: a plain SUM of the fine
// residuals over each agglomerate. No 1/4 averaging -- with the Galerkin operator
// above, the sum is the consistent pairing (b is a volume-integrated source, and
// a coarse cell is the union of its members).
//
// The caller must zero coarse b first -- this accumulates.
__global__
void buildRestrictionKernel(
	Coefficients fine,
	Coefficients coarse,
	const double* fineRes,
	const int* cellToCoarse
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;

	atomicAdd(&coarse.b[cellToCoarse[n]], fineRes[n]);
}


// Prolongation, P = piecewise-constant injection: every fine cell picks up the
// correction of the coarse cell it belongs to.
__global__
void buildProlongationKernel(
	Coefficients fine,
	double* xf,
	const double* xc,
	const int* cellToCoarse
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;

	xf[n] += xc[cellToCoarse[n]];
}

// One cell's fused residual + weighted-Jacobi update: form r = b - A*x for row n
// and apply x += weight * r / AC, holding r in a register instead of round-tripping
// it through level.res. Versus the old computeResidual + pointwise jacobiSmoother
// pair this drops one launch and one N-double store plus reload per sweep, which is
// most of what the separate smoother cost.
//
// Both smoothers share this body. xOld/xNew are plain pointers so the caller
// decides where they live -- jacobiFused passes global memory, jacobiSingleBlock
// passes its shared-memory ping-pong buffers, and generic addressing binds both to
// the same parameters. It is one function because it was previously written out
// twice: the single-block copy runs only on small coarse levels, so a change
// applied to one and not the other degrades convergence rather than failing, which
// is close to undiagnosable.
//
// Face path only, deliberately: multigrid is constructed solely when
// useFaceCoefficients is set (solver.cpp), and coarse levels come from the
// face-path allocator, so AF / faceStart / faceNeighbor are always live here.
// No structured AE/AW/AN/AS fallback and no useFaceCoeffs branch -- residualRaw
// keeps those for the structured mesh path.
//
// xOld and xNew MUST be distinct buffers. This reads neighbour values while
// writing its own cell, so sharing one array would let a thread see a neighbour
// another thread had already advanced this sweep -- chaotic relaxation, not
// Jacobi, and nondeterministic run to run.
__device__ __forceinline__
void jacobiRow(
	const Coefficients& coeff,
	const double* xOld,
	double* xNew,
	int n,
	double weight
) {

	const double AC = coeff.AC[n];

	// Empty row -- carry the value through rather than dividing. This used to be
	// covered by the active mask, since an unassembled row was exactly the AC == 0
	// case, and it has to be explicit now the mask is gone: a cell whose momentum aP
	// collapses gets D = 0 out of getCorrectionCoefficient, which zeroes every K in
	// createPPCoeff and leaves that pp row empty. Dividing there writes a NaN, and
	// the very next sweep spreads it to the whole level through the face loop. Every
	// other smoother in the project already guards this (linear_solver.cu's jacobi
	// and gaussSeidelColorSweep, underRelaxEquation); jacobiRow was the one hole.
	if (fabs(AC) < 1.0e-30) {
		xNew[n] = xOld[n];
		return;
	}

	double Ax = AC * xOld[n];

	const int start = coeff.faceStart[n];
	const int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		const int nb = coeff.faceNeighbor[k];
		if (nb >= 0) {
			Ax += coeff.AF[k] * xOld[nb];
		}
	}

	xNew[n] = xOld[n] + weight * (coeff.b[n] - Ax) / AC;
}

__global__
void jacobiFused(Coefficients coeff, const double* xOld, double* xNew, double weight) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;

	jacobiRow(coeff, xOld, xNew, n, weight);
}

// Small-level weighted Jacobi, with every sweep performed inside one block.
// The whole level must fit in that block: this is what makes __syncthreads() a
// grid-wide barrier and preserves Jacobi's read-old/write-new semantics between
// sweeps. x is loaded once, ping-ponged in shared memory, and written back once;
// coefficients/topology remain in global memory and should be cache-hot after
// the first sweep because the system is small.
__global__
void jacobiSingleBlock(
	Coefficients coeff,
	double* x,
	double weight,
	int iterations
) {

	extern __shared__ double sharedX[];

	double* xOld = sharedX;
	double* xNew = sharedX + coeff.N;

	const int n = threadIdx.x;

	if (n < coeff.N) {
		xOld[n] = x[n];
	}

	__syncthreads();

	#pragma unroll 1
	for (int iteration = 0; iteration < iterations; iteration++) {

		if (n < coeff.N) {
			jacobiRow(coeff, xOld, xNew, n, weight);
		}

		// No thread may return before this barrier: even threads beyond coeff.N
		// participate so every cell has finished writing before the buffers swap.
		__syncthreads();

		double* swap = xOld;
		xOld = xNew;
		xNew = swap;
	}

	if (n < coeff.N) {
		x[n] = xOld[n];
	}
}

// NOTE: all three transfers are now driven from the FINE level (one thread per
// fine cell, scattering down), so every launch below sizes on fine.grid.nCells.
// The old versions launched over the coarse level and gathered.

void MultigridSolver::buildCoarseOperator(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	// the kernel accumulates, and run() rebuilds the hierarchy's operators on
	// every call -- without this it would sum onto the previous call's operator
	CUDA_CHECK(cudaMemsetAsync(coarse.coeff.AC, 0, coarse.grid.nCells * sizeof(double), stream));

	if (coarse.coeff.AF && coarse.coeff.nFaceRefs > 0) {
		CUDA_CHECK(cudaMemsetAsync(coarse.coeff.AF, 0, coarse.coeff.nFaceRefs * sizeof(double), stream));
	}

	const int blocks = blocksFor(fine.grid.nCells);

	buildCoarseOperatorKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		coarse.coeff,
		fine.d_cellToCoarse,
		fine.d_fineSlotToCoarseSlot
		);

}

void MultigridSolver::buildRestriction(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	// same reason as above: the scatter accumulates into b
	CUDA_CHECK(cudaMemsetAsync(coarse.coeff.b, 0, coarse.grid.nCells * sizeof(double), stream));

	const int blocks = blocksFor(fine.grid.nCells);

	buildRestrictionKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		coarse.coeff,
		fine.res,
		fine.d_cellToCoarse
		);

}

void MultigridSolver::buildProlongation(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	const int blocks = blocksFor(fine.grid.nCells);

	buildProlongationKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		fine.x,
		coarse.x,
		fine.d_cellToCoarse
		);
}

void MultigridSolver::computeResidual(MultigridLevel& level, cudaStream_t& stream) {

	const int blocks = blocksFor(level.grid.nCells);

	residualAll << <blocks, mem.threadsPerBlock, 0, stream >> > (
		true,
		ResidualPairs{ level.coeff, level.x, level.res }
		);

}

void MultigridSolver::vCycle(int l, cudaStream_t& stream) {

	// Cast to int rather than comparing against levels.size() directly: size() is
	// size_t, so the comparison would promote l to unsigned and a negative l would
	// pass the bounds check as a huge positive.
	const int nLevels = (int)levels.size();

	if (l < 0 || l >= nLevels) return;

	// Coarsest level: nothing below to correct from, so just smooth hard. Asked of
	// the level's own data rather than as `l == nLevels - 1`, because isCoarsest()
	// is exactly the property that decides whether restriction and prolongation are
	// legal -- an empty cellToCoarse is uploaded as a null d_cellToCoarse, which
	// those two kernels would dereference. The two tests agree only because
	// buildLevels clears the maps on the level it stops at.
	if (levels[l].grid.isCoarsest()) {
		smoothen(levels[l], stream, cfg.linearSweep);
		return;
	}

	smoothen(levels[l], stream, cfg.linearPreSweep);

	// smoothen() keeps its residual in a register and never writes level.res, so
	// this is the ONLY thing that fills it -- restriction would otherwise read
	// whatever the previous cycle left. Not redundant; do not fold it into
	// smoothen(), which would also make the fused smoother pay for a store that
	// only the last sweep of the loop actually needs.
	//
	// Unconditional on purpose. With linearPreSweep == 0 a level below 0 enters
	// with x still zeroed by the memset before the recursive call, so its residual
	// is exactly b and this pass only copies b into res -- skippable, but ONLY if
	// buildRestriction is told to read coeff.b in the same change. Gating this call
	// alone leaves restriction reading an all-zero res, which zeroes the coarse
	// source and quietly drops the levels below it out of the cycle. No crash, no
	// NaN, no diagnostic. Level 0 is not skippable either way: its x is the live
	// solution carried across cycles, not a correction starting from zero.
	computeResidual(levels[l], stream);

	buildRestriction(levels[l], levels[l + 1], stream);

	// The coarse solve is for a CORRECTION, so it starts from zero. This must
	// happen every cycle: on the second and later V-cycles the coarse x still
	// holds the previous cycle's correction, which would prolongate back up.
	CUDA_CHECK(cudaMemsetAsync(
		levels[l + 1].x,
		0,
		levels[l + 1].grid.nCells * sizeof(double),
		stream
	));

	vCycle(l + 1, stream);

	buildProlongation(levels[l], levels[l + 1], stream);
	smoothen(levels[l], stream, cfg.linearPostSweep);

}

void MultigridSolver::smoothen(MultigridLevel& level, cudaStream_t& stream, int iteration) {

	const int N = level.grid.nCells;

	if (N <= 0 || iteration <= 0) return;

	// A single block can synchronize the entire level between Jacobi sweeps. Use
	// that persistent kernel whenever it can eliminate at least one launch; the
	// regular smoother remains the fallback for larger levels and single sweeps.
	if (iteration > 1 && N <= mem.threadsPerBlock) {
		smoothenSingleBlock(level, stream, iteration);
		return;
	}

	smoothenRegular(level, stream, iteration);
}

void MultigridSolver::smoothenRegular(
	MultigridLevel& level,
	cudaStream_t& stream,
	int iteration
) {

	const int blocks = blocksFor(level.grid.nCells);

	for (int n = 0; n < iteration; n++) {
		jacobiFused << <blocks, mem.threadsPerBlock, 0, stream >> > (
			level.coeff, level.x, level.xNew, cfg.weight
			);

		// Swap the members rather than copying back, so `x` names the live vector
		// on return whatever the sweep count's parity. During CUDA Graph capture
		// this host swap runs once and gives every recorded kernel node its fixed,
		// alternating input/output addresses. It is deliberately NOT repeated on
		// graph replay: enqueueRun's captured entry and exit copies make the whole
		// fixed pointer schedule self-contained.
		std::swap(level.x, level.xNew);
	}
}

void MultigridSolver::smoothenSingleBlock(
	MultigridLevel& level,
	cudaStream_t& stream,
	int iteration
) {

	const int N = level.grid.nCells;

	// Round to warps without exceeding the configured block size. Idle lanes must
	// still reach every __syncthreads() in jacobiSingleBlock.
	const int warpSize = 32;
	const int roundedThreads = ((N + warpSize - 1) / warpSize) * warpSize;
	const int threads = std::min(mem.threadsPerBlock, roundedThreads);
	const size_t sharedBytes = 2ull * (size_t)N * sizeof(double);

	jacobiSingleBlock << <1, threads, sharedBytes, stream >> > (
		level.coeff,
		level.x,
		cfg.weight,
		iteration
		);
}

std::string MultigridSolver::describeHierarchy() const {

	std::ostringstream out;

	out << "Multigrid: " << levels.size() << " level(s)\n";

	for (size_t l = 0; l < levels.size(); l++) {

		const GridLevel& g = levels[l].grid;

		// average number of neighbours per cell. The fine mesh sits near 4 on
		// quads; if this climbs sharply going coarse, the agglomerates are ragged
		// and the coarse operator will be denser and worse-conditioned than it
		// should be, which shows up as a V-cycle that barely beats Jacobi.
		const double degree = g.nCells > 0
			? (double)g.nFaceRefs() / (double)g.nCells
			: 0.0;

		out << "  L" << l
			<< "  cells " << g.nCells
			<< std::fixed << std::setprecision(2)
			<< "  degree " << degree;

		if (l > 0) {
			const int prev = levels[l - 1].grid.nCells;
			const double ratio = g.nCells > 0 ? (double)prev / (double)g.nCells : 0.0;
			out << "  (" << ratio << "x)";
		}

		out << "\n";
	}

	if (levels.size() < 2) {
		out << "  WARNING: single level -- no coarse correction, this degrades to plain Jacobi.\n";
	}

	return out.str();
}

void MultigridSolver::enqueueRun(Coefficients& coeff, cudaStream_t& stream, double* x) {

	// Graph entry: initialize the exact level-0 buffer from which the captured
	// first smoother node reads. This also makes odd sweep parity safe on a
	// single-level hierarchy, where the captured exit buffer can be different.
	CUDA_CHECK(cudaMemcpyAsync(
		levels[0].x,
		x,
		levels[0].grid.nCells * sizeof(double),
		cudaMemcpyDeviceToDevice,
		stream
	));

	// load the fine operator's DATA into level 0's own buffers (not `= coeff`,
	// which would leak level 0's buffers and alias the solver's arrays)
	copyCoefficients(levels[0].coeff, coeff, levels[0].grid.nCells, stream);

	// start at index 1, as the 0th index contains the fine level
	for (int l = 1; l < (int)levels.size(); l++) {
		buildCoarseOperator(levels[l - 1], levels[l], stream);
	}

	// Repeat the cycle on the SAME operator: A does not change between cycles, only
	// x does, so the hierarchy built above is reused and each pass simply starts
	// from the residual the previous pass left.
	//
	// There is deliberately no residual-based early exit: testing it would mean a
	// blocking host sync per cycle, which costs more than the cycles it saves.
	const int nCycles = cycleCount();

	for (int cycle = 0; cycle < nCycles; cycle++) {
		vCycle(0, stream);
	}

	// Graph exit: consumers outside multigrid always see their stable allocation,
	// regardless of which level-0 ping-pong buffer is live after the fixed sweep
	// sequence captured above.
	CUDA_CHECK(cudaMemcpyAsync(
		x,
		levels[0].x,
		levels[0].grid.nCells * sizeof(double),
		cudaMemcpyDeviceToDevice,
		stream
	));

}

MultigridSolver::RunGraphKey MultigridSolver::currentKey(
	const Coefficients& coeff,
	const double* x,
	cudaStream_t stream
) const {

	RunGraphKey key;

	key.N = coeff.N;
	key.nFaceRefs = coeff.nFaceRefs;
	key.nCycles = cycleCount();
	key.threadsPerBlock = mem.threadsPerBlock;
	key.useFaceCoeffs = coeff.useFaceCoeffs;
	key.linearSweep = cfg.linearSweep;
	key.linearPreSweep = cfg.linearPreSweep;
	key.linearPostSweep = cfg.linearPostSweep;
	key.weight = cfg.weight;
	key.externalX = x;
	key.AC = coeff.AC;
	key.b = coeff.b;
	key.AF = coeff.AF;
	key.stream = stream;

	return key;

}

bool MultigridSolver::runGraphMatches(
	const Coefficients& coeff,
	const double* x,
	cudaStream_t stream
) const {

	return runGraphExec != nullptr && runGraphKey == currentKey(coeff, x, stream);

}

void MultigridSolver::prepare(
	Coefficients& coeff,
	cudaStream_t& stream,
	double* x
) {
	// Level 0's buffers were sized from the FVMesh makeFinestGridLevel saw, while
	// everything enqueueRun copies is sized from `coeff`. The two are built at
	// different times -- solver.cpp rebuilds the GridLevel on every solve but only
	// reallocates ppCoeff when the mesh counts change -- so pin the invariant here
	// instead of letting a mismatch run off the end of level 0.
	assert(coeff.N == levels[0].grid.nCells);
	assert(coeff.nFaceRefs == levels[0].grid.nFaceRefs());

	if (!runGraphMatches(coeff, x, stream)) {
		if (runGraphExec || runGraph) {
			// Synchronize the stream that owns the existing executable before
			// destroying graph resources which one of its launches may still use.
			CUDA_CHECK(cudaStreamSynchronize(runGraphKey.stream));
			destroyRunGraph();
		}

		captureRunGraph(coeff, stream, x);
		CUDA_CHECK(cudaGraphUpload(runGraphExec, stream));
	}
}

void MultigridSolver::captureRunGraph(Coefficients& coeff, cudaStream_t& stream, double* x) {

	// Thread-local mode avoids imposing capture restrictions on unrelated GUI
	// threads which may also make CUDA/graphics API calls.
	CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));

	// CUDA work is recorded rather than submitted. Host loops, recursion and the
	// smoother's std::swap calls still execute here, flattening the complete solve
	// and fixing the ping-pong address used by every graph node.
	enqueueRun(coeff, stream, x);

	CUDA_CHECK(cudaStreamEndCapture(stream, &runGraph));
	CUDA_CHECK(cudaGraphInstantiate(&runGraphExec, runGraph, nullptr, nullptr, 0));

	runGraphKey = currentKey(coeff, x, stream);

}

void MultigridSolver::destroyRunGraph() {

	// Destroy the executable first: it snapshots nodes from runGraph and both
	// objects retain device addresses owned by the multigrid levels.
	if (runGraphExec) {
		CUDA_CHECK(cudaGraphExecDestroy(runGraphExec));
		runGraphExec = nullptr;
	}

	if (runGraph) {
		CUDA_CHECK(cudaGraphDestroy(runGraph));
		runGraph = nullptr;
	}

	runGraphKey = {};

}

void MultigridSolver::run(cudaStream_t& stream) {

	// check to make sure the executable and stream matches what was captured
	assert(runGraphExec != nullptr);
	assert(runGraphKey.stream == stream);

	CUDA_CHECK(cudaGraphLaunch(runGraphExec, stream));

}
