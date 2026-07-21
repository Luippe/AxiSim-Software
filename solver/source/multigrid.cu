#include "multigrid.cuh"


#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include "residuals.cuh"

#include "memory_manager.h"

#define CUDA_CHECK(x) do { \
  cudaError_t err = (x); \
  if (err != cudaSuccess) { \
    printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    std::abort(); \
  } \
} while(0)

MultigridSolver::MultigridSolver(ConfigMultigrid& cfg, MemoryConfig& mem, GridLevel& grid) :
	cfg(cfg),
	mem(mem) {

	// init once
	buildLevels(std::move(grid));
	CUDA_CHECK(cudaGetLastError());

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

	// match the mask the solver kernels use (mesh.cells.active), not
	// active && !solid -- disagreeing here would agglomerate cells the
	// solver is skipping
	level.active.assign(N, 0);
	for (int c = 0; c < N; c++) {
		level.active[c] = mesh.cells[c].active ? 1 : 0;
	}

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
// Active and inactive cells are never merged: an inactive cell carries an
// identity row, which would poison whatever coarse diagonal it folded into.
//
// Returns the coarse cell count. cellToCoarse comes out dense over
// [0, nCoarse), including inactive cells -- the coarse arrays are indexed by it
// directly, so gaps are not allowed. `active` does the filtering instead.
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
				if ((fine.active[nb] != 0) != (fine.active[n] != 0)) continue;   // never mix

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
			if ((fine.active[nb] != 0) != (fine.active[n] != 0)) continue;

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

	// a coarse cell is active if ANY member is active (same rule the structured
	// 2x2 coarsening used)
	coarse.active.assign(nCoarse, 0);
	for (int n = 0; n < fine.nCells; n++) {
		if (fine.active[n]) {
			coarse.active[fine.cellToCoarse[n]] = 1;
		}
	}

	// ---- pass 2: distinct coarse neighbours of each coarse cell -------------
	// A fine face contributes a coarse off-diagonal only when its two ends land
	// in DIFFERENT coarse cells. Faces internal to an agglomerate fold into the
	// coarse diagonal, and boundary slots contribute nothing (their effect is
	// already in the fine AC / b).
	//
	// Collected as one flat (coarse cell, coarse neighbour) list rather than a
	// set per coarse cell: sort + unique gives the same deduplicated, row-sorted
	// result -- which the binary search below depends on -- in one allocation
	// instead of nCoarse red-black trees.
	std::vector<std::pair<int, int>> pairs;
	pairs.reserve(fine.nFaceRefs());

	for (int n = 0; n < fine.nCells; n++) {

		const int cn = fine.cellToCoarse[n];

		for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

			const int nb = fine.faceNeighbor[k];
			if (nb < 0) continue;

			const int cnb = fine.cellToCoarse[nb];
			if (cnb != cn) {
				pairs.emplace_back(cn, cnb);
			}
		}
	}

	std::sort(pairs.begin(), pairs.end());
	pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());

	// ---- pass 3: flatten the pair list into CSR -----------------------------
	coarse.faceStart.assign(nCoarse + 1, 0);
	for (const auto& p : pairs) {
		coarse.faceStart[p.first + 1]++;
	}

	for (int c = 0; c < nCoarse; c++) {
		coarse.faceStart[c + 1] += coarse.faceStart[c];
	}

	coarse.faceNeighbor.resize(pairs.size());
	for (size_t i = 0; i < pairs.size(); i++) {
		coarse.faceNeighbor[i] = pairs[i].second;
	}

	// ---- fine slot -> coarse slot -------------------------------------------
	// -1 means "feeds no coarse off-diagonal", covering two different cases the
	// coarse-operator kernel tells apart via faceNeighbor[k]:
	//   faceNeighbor[k] >= 0 -> internal to an agglomerate, folds into the diagonal
	//   faceNeighbor[k] <  0 -> boundary slot, contributes nothing
	fine.fineSlotToCoarseSlot.assign(fine.nFaceRefs(), -1);

	for (int n = 0; n < fine.nCells; n++) {

		const int cn = fine.cellToCoarse[n];

		for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

			const int nb = fine.faceNeighbor[k];
			if (nb < 0) continue;

			const int cnb = fine.cellToCoarse[nb];
			if (cnb == cn) continue;

			// pass 2 built row cn from these same pairs, so the hit always exists
			const auto rowBegin = coarse.faceNeighbor.begin() + coarse.faceStart[cn];
			const auto rowEnd = coarse.faceNeighbor.begin() + coarse.faceStart[cn + 1];

			const auto hit = std::lower_bound(rowBegin, rowEnd, cnb);

			fine.fineSlotToCoarseSlot[k] = (int)(hit - coarse.faceNeighbor.begin());
		}
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
	const int* fineSlotToCoarseSlot,
	const uint8_t* fineActive
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;

	// an inactive fine cell is never assembled (createPPCoeff returns early, leaving
	// the row at AC = 0) and never solved, so folding that empty row into a coarse
	// cell would only dilute the coarse diagonal
	if (!fineActive[n]) return;

	const int cn = cellToCoarse[n];

	atomicAdd(&coarse.AC[cn], fine.AC[n]);

	for (int k = fine.faceStart[n]; k < fine.faceStart[n + 1]; k++) {

		const int nb = fine.faceNeighbor[k];
		if (nb < 0) continue;             // boundary slot

		// coupling to an inactive cell is inert in the fine solve (its x stays 0),
		// so it must not create a coarse coupling either. Leaving the matching
		// weight in AC makes the coarse row diagonally dominant, which is the
		// stable choice.
		if (!fineActive[nb]) continue;

		const int slot = fineSlotToCoarseSlot[k];

		if (slot >= 0) {
			atomicAdd(&coarse.AF[slot], fine.AF[k]);
		}
		else {
			atomicAdd(&coarse.AC[cn], fine.AF[k]);
		}
	}
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
	const int* cellToCoarse,
	const uint8_t* fineActive
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;
	if (!fineActive[n]) return;

	atomicAdd(&coarse.b[cellToCoarse[n]], fineRes[n]);
}


// Prolongation, P = piecewise-constant injection: every fine cell picks up the
// correction of the coarse cell it belongs to.
__global__
void buildProlongationKernel(
	Coefficients fine,
	double* xf,
	const double* xc,
	const int* cellToCoarse,
	const uint8_t* fineActive
) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= fine.N) return;
	if (!fineActive[n]) return;

	xf[n] += xc[cellToCoarse[n]];
}

// Fused residual + weighted-Jacobi update: form r = b - A*x and apply
// x += weight * r / AC in a single pass, holding r in a register instead of
// round-tripping it through level.res. Versus the old computeResidual +
// pointwise jacobiSmoother pair this drops one launch and one N-double store
// plus reload per sweep, which is most of what the separate smoother cost.
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
__global__
void jacobiFused(Coefficients coeff, const double* xOld, double* xNew, const uint8_t* active, double weight) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;

	// Inactive cells must still be carried across. Returning early here would
	// leave xNew[n] holding the value from two sweeps ago; since these cells sit
	// outside the domain the residual would keep dropping normally while the
	// solution beside the mask quietly rotted.
	if (!active[n]) {
		xNew[n] = xOld[n];
		return;
	}

	double Ax = coeff.AC[n] * xOld[n];

	const int start = coeff.faceStart[n];
	const int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		const int nb = coeff.faceNeighbor[k];
		if (nb >= 0) {
			Ax += coeff.AF[k] * xOld[nb];
		}
	}

	xNew[n] = xOld[n] + weight * (coeff.b[n] - Ax) / coeff.AC[n];
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
	const uint8_t* active,
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
			if (active && !active[n]) {
				xNew[n] = xOld[n];
			}
			else {
				double Ax = coeff.AC[n] * xOld[n];

				const int start = coeff.faceStart[n];
				const int end = coeff.faceStart[n + 1];

				for (int k = start; k < end; k++) {
					const int nb = coeff.faceNeighbor[k];
					if (nb >= 0) {
						Ax += coeff.AF[k] * xOld[nb];
					}
				}

				xNew[n] = xOld[n] + weight * (coeff.b[n] - Ax) / coeff.AC[n];
			}
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

	int blocks = (fine.grid.nCells + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildCoarseOperatorKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		coarse.coeff,
		fine.d_cellToCoarse,
		fine.d_fineSlotToCoarseSlot,
		fine.d_active
		);

}

void MultigridSolver::buildRestriction(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	// same reason as above: the scatter accumulates into b
	CUDA_CHECK(cudaMemsetAsync(coarse.coeff.b, 0, coarse.grid.nCells * sizeof(double), stream));

	int blocks = (fine.grid.nCells + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildRestrictionKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		coarse.coeff,
		fine.res,
		fine.d_cellToCoarse,
		fine.d_active
		);

}

void MultigridSolver::buildProlongation(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	int blocks = (fine.grid.nCells + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildProlongationKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (
		fine.coeff,
		fine.x,
		coarse.x,
		fine.d_cellToCoarse,
		fine.d_active
		);
}

void MultigridSolver::computeResidual(MultigridLevel& level, cudaStream_t& stream) {

	int blocks = (level.grid.nCells + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	residualAll << <blocks, mem.threadsPerBlock, 0, stream >> > (
		level.d_active,
		true,
		ResidualPairs{ level.coeff, level.x, level.res }
		);

}

void MultigridSolver::vCycle(int l, cudaStream_t& stream) {

	// Cache as int. levels.size() is size_t, so `l == levels.size() - 1` would
	// promote l to unsigned, and on an empty hierarchy size() - 1 wraps to
	// SIZE_MAX -- the base case never matches and the recursion runs away.
	const int nLevels = (int)levels.size();

	if (l < 0 || l >= nLevels) return;

	// coarsest level: nothing below to correct from, so just smooth hard
	if (l == nLevels - 1) {
		smoothen(levels[l], stream, cfg.linearSweep);
		return;
	}

	smoothen(levels[l], stream, cfg.linearPrePostSweep);

	// smoothen() keeps its residual in a register and never writes level.res, so
	// this is the ONLY thing that fills it -- restriction would otherwise read
	// whatever the previous cycle left. Not redundant; do not fold it into
	// smoothen(), which would also make the fused smoother pay for a store that
	// only the last sweep of the loop actually needs.
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
	smoothen(levels[l], stream, cfg.linearPrePostSweep);

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

	const int blocks =
		(level.grid.nCells + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	for (int n = 0; n < iteration; n++) {
		jacobiFused << <blocks, mem.threadsPerBlock, 0, stream >> > (
			level.coeff, level.x, level.xNew, level.d_active, cfg.weight
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
		level.d_active,
		cfg.weight,
		iteration
		);
}

std::string MultigridSolver::describeHierarchy() const {

	std::ostringstream out;

	out << "Multigrid: " << levels.size() << " level(s)\n";

	for (size_t l = 0; l < levels.size(); l++) {

		const GridLevel& g = levels[l].grid;

		int nActive = 0;
		for (uint8_t a : g.active) {
			if (a) nActive++;
		}

		// average number of neighbours per cell. The fine mesh sits near 4 on
		// quads; if this climbs sharply going coarse, the agglomerates are ragged
		// and the coarse operator will be denser and worse-conditioned than it
		// should be, which shows up as a V-cycle that barely beats Jacobi.
		const double degree = g.nCells > 0
			? (double)g.nFaceRefs() / (double)g.nCells
			: 0.0;

		out << "  L" << l
			<< "  cells " << g.nCells
			<< "  active " << nActive
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
		coeff.N * sizeof(double),
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
	// from the residual the previous pass left. Floored at 1 so a bad config can
	// never turn the pp solve into a no-op.
	//
	// There is deliberately no residual-based early exit: testing it would mean a
	// blocking host sync per cycle, which costs more than the cycles it saves.
	const int nCycles = std::max(1, cfg.maxIter);

	for (int cycle = 0; cycle < nCycles; cycle++) {
		vCycle(0, stream);
	}

	// Graph exit: consumers outside multigrid always see their stable allocation,
	// regardless of which level-0 ping-pong buffer is live after the fixed sweep
	// sequence captured above.
	CUDA_CHECK(cudaMemcpyAsync(
		x,
		levels[0].x,
		coeff.N * sizeof(double),
		cudaMemcpyDeviceToDevice,
		stream
	));

}

bool MultigridSolver::runGraphMatches(
	const Coefficients& coeff,
	const double* x,
	cudaStream_t stream
) const {

	return
		runGraphExec != nullptr &&
		runGraphKey.N == coeff.N &&
		runGraphKey.nFaceRefs == coeff.nFaceRefs &&
		runGraphKey.nCycles == std::max(1, cfg.maxIter) &&
		runGraphKey.threadsPerBlock == mem.threadsPerBlock &&
		runGraphKey.useFaceCoeffs == coeff.useFaceCoeffs &&
		runGraphKey.linearSweep == cfg.linearSweep &&
		runGraphKey.linearPrePostSweep == cfg.linearPrePostSweep &&
		runGraphKey.weight == cfg.weight &&
		runGraphKey.externalX == x &&
		runGraphKey.AC == coeff.AC &&
		runGraphKey.b == coeff.b &&
		runGraphKey.AF == coeff.AF &&
		runGraphKey.stream == stream;

}

void MultigridSolver::prepare(
	Coefficients& coeff,
	cudaStream_t& stream,
	double* x
) {
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

	runGraphKey.N = coeff.N;
	runGraphKey.nFaceRefs = coeff.nFaceRefs;
	runGraphKey.nCycles = std::max(1, cfg.maxIter);
	runGraphKey.threadsPerBlock = mem.threadsPerBlock;
	runGraphKey.useFaceCoeffs = coeff.useFaceCoeffs;
	runGraphKey.linearSweep = cfg.linearSweep;
	runGraphKey.linearPrePostSweep = cfg.linearPrePostSweep;
	runGraphKey.weight = cfg.weight;
	runGraphKey.externalX = x;
	runGraphKey.AC = coeff.AC;
	runGraphKey.b = coeff.b;
	runGraphKey.AF = coeff.AF;
	runGraphKey.stream = stream;

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
