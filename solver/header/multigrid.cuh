#pragma once
#include <string>

#include "solver_struct.h"
#include "boundary_struct.h"


// ---------------------------------------------------------------------------
// One level of the hierarchy, described purely as a cell/face graph.
//
// Replaces the old structured descriptor (nr x nz + rFace/zFace). Multiblock and
// unstructured meshes both reach the solver through the face-based coefficient
// path, where there is no single logical grid -- multiblock.h deliberately sets
// nr = nz = 0. The Galerkin coarse operator, restriction and prolongation are all
// graph work over this connectivity, so no geometry is stored here.
//
// The CSR layout mirrors Coefficients exactly, so a level's arrays upload
// straight into Coefficients::faceStart / faceNeighbor:
//
//   cell n owns slots [faceStart[n], faceStart[n + 1])
//   slot k pairs faceNeighbor[k] with that row's coefficient AF[k]
//
// An interior face appears twice, once in each adjacent cell's row. A boundary
// slot has faceNeighbor[k] < 0 and contributes only to AC / b.
// ---------------------------------------------------------------------------
struct GridLevel {

	int nCells = 0;

	// size nCells + 1
	std::vector<int> faceStart;

	// < 0 marks a boundary slot
	std::vector<int> faceNeighbor;

	// Total CSR slots. Derived rather than stored so it cannot drift from the
	// arrays it describes; mirrored into Coefficients::nFaceRefs, which sizes AF
	// and faceNeighbor on the device.
	int nFaceRefs() const { return (int)faceNeighbor.size(); }

	// size nCells; 0 = inactive (solid / outside the domain). The smoother and the
	// residual both skip inactive cells, so an inactive row is never solved and its
	// correction stays 0. The row is left empty (AC = 0), not made identity.
	std::vector<uint8_t> active;

	// Agglomeration map INTO the next coarser level: cellToCoarse[n] is the coarse
	// cell that fine cell n belongs to. Size nCells. Empty on the coarsest level.
	//
	// It lives on the FINE level because both transfers are driven from fine cells:
	// restriction scatters (fine residual atomicAdd'd into the coarse b) and
	// prolongation gathers (x[n] += xCoarse[cellToCoarse[n]]). Neither direction
	// needs the inverse coarse->fine list, so that is deliberately not stored.
	std::vector<int> cellToCoarse;

	// Maps each of THIS level's CSR slots to the coarse slot it feeds. Size
	// nFaceRefs(), empty on the coarsest level. For slot k of cell n:
	//
	//   >= 0                    -> index into the COARSE level's AF
	//   <  0, faceNeighbor >= 0 -> both ends agglomerate together; the fine
	//                              coefficient folds into the coarse DIAGONAL
	//   <  0, faceNeighbor <  0 -> boundary slot; contributes nothing
	//
	// Precomputed on the host while the hierarchy is built so the per-iteration
	// coarse-operator kernel stays a pure scatter, with no device-side search of
	// the coarse row for a matching neighbour.
	std::vector<int> fineSlotToCoarseSlot;

	bool isCoarsest() const { return cellToCoarse.empty(); }

};

struct MultigridLevel {

	GridLevel grid;

	double* x = nullptr;

	// Ping-pong partner for x, used only by the fused smoother. jacobiFused reads
	// neighbour values while writing its own cell, so the two cannot be the same
	// buffer -- with one array a thread could read a neighbour another thread had
	// already advanced this sweep, which is chaotic relaxation, not Jacobi.
	//
	// smoothen() swaps these two members after every sweep, so `x` always names
	// the live vector once it returns and every other consumer (prolongation, the
	// memset in vCycle, run()'s copy in/out) needs no parity awareness.
	double* xNew = nullptr;

	// per-level residual vector. Previously read off Coefficients::res, which was
	// removed when residual state moved to ConfigResidual; a multigrid level owns
	// its own residual for restriction/smoothing.
	//
	// Written ONLY by computeResidual() now -- the fused smoother keeps its
	// residual in a register and never lands it.
	double* res = nullptr;

	uint8_t* d_active = nullptr;

	// device copies of the two fine->coarse maps; both null on the coarsest level
	int* d_cellToCoarse = nullptr;
	int* d_fineSlotToCoarseSlot = nullptr;

	// NOTE: the device CSR topology is deliberately NOT duplicated here --
	// coeff.faceStart / coeff.faceNeighbor already hold it, and residualRaw and
	// the jacobi smoother read it from there. Uploading the level's
	// faceStart/faceNeighbor into coeff is enough.
	Coefficients coeff;

};


// Build the finest GridLevel from the solver's FVMesh.
//
// Shares buildCellFaceCSR with allocateCoefficients, so level 0's connectivity
// matches the Coefficients the solver hands to prepare() slot for slot by
// construction -- which fineSlotToCoarseSlot depends on, since it indexes exactly
// those slots.
GridLevel makeFinestGridLevel(const FVMesh& mesh);


class MultigridSolver {

public:

	MultigridSolver(ConfigMultigrid& cfg, MemoryConfig& mem, GridLevel& grid);
	~MultigridSolver();

	// each level owns raw device pointers freed in the destructor; a copy would
	// alias then double-free them. non-copyable (emplaced into std::optional, so
	// no copy/move is needed anyway).
	MultigridSolver(const MultigridSolver&) = delete;
	MultigridSolver& operator=(const MultigridSolver&) = delete;

	std::vector<MultigridLevel> levels;

	// Capture, instantiate and upload the complete solve before the SIMPLE loop.
	void prepare(Coefficients& coeff, cudaStream_t& stream, double* x);

	// Replay the prepared multigrid solve.
	//
	// Convergence expected of THIS method (piecewise-constant aggregation,
	// unsmoothed): the two-grid factor is a mesh-independent ~0.5, but the
	// recursive V-cycle degrades with problem size -- measured ~0.80 per cycle at
	// 1k cells, ~0.90 at 4k, ~0.96 at 16k, ~0.98 at 65k. That is the method, not a
	// defect: the coarse operator here is exactly P^T A P, and neither an exact
	// coarsest solve nor perfect agglomeration moves those numbers much. So a
	// per-cycle ratio near 1.0 on a large mesh does NOT mean the operator or a
	// transfer broke. A mesh-independent V-cycle needs smoothed aggregation
	// (P <- (I - w D^-1 A) P) or Krylov acceleration wrapped around the cycle.
	void run(cudaStream_t& stream);

	// One line per level: cell count, active count, average graph degree and the
	// coarsening ratio. Host-side only and cheap -- meant to be logged once after
	// construction to sanity-check that agglomeration actually did something.
	std::string describeHierarchy() const;

	ConfigMultigrid& cfg;
	MemoryConfig& mem;

private:

	double jacobiWeight = 0.6;
	int jacobiSweep = 75;
	int jacobiPrePostSweep = 3;

	// One executable graph owns a complete run(): copy the current fine system
	// into level 0, rebuild all coarse operators, perform every configured
	// V-cycle, then copy the live level-0 vector back to the caller. prepare()
	// captures, instantiates and uploads it before the SIMPLE loop; run() only
	// replays it for the rest of this MultigridSolver's lifetime.
	cudaGraph_t runGraph = nullptr;
	cudaGraphExec_t runGraphExec = nullptr;

	// Kernel arguments and memcpy endpoints are snapshotted during capture. The
	// numbers stored in those allocations may change freely, but a new allocation
	// or a topology/configuration change requires a new graph.
	struct RunGraphKey {
		int N = 0;
		int nFaceRefs = 0;
		int nCycles = 0;
		int threadsPerBlock = 0;
		int useFaceCoeffs = 0;

		double* externalX = nullptr;
		double* AC = nullptr;
		double* b = nullptr;
		double* AF = nullptr;
		cudaStream_t stream = nullptr;
	};

	RunGraphKey runGraphKey;

	// Submit the complete multigrid solve to a stream. Called once while the
	// stream is being captured; graph replays do not execute this host code.
	void enqueueRun(Coefficients& coeff, cudaStream_t& stream, double* x);

	void captureRunGraph(Coefficients& coeff, cudaStream_t& stream, double* x);
	bool runGraphMatches(const Coefficients& coeff, const double* x, cudaStream_t stream) const;
	void destroyRunGraph();

	// Build the next coarser level from `fine`, and fill fine's cellToCoarse /
	// fineSlotToCoarseSlot describing the link down to it. NOT const: coarsening
	// produces both the coarse level and the fine level's maps.
	GridLevel coarsenGrid(GridLevel& fine);

	void computeResidual(MultigridLevel& level, cudaStream_t& stream);

	// smoothen the field
	void smoothen(MultigridLevel& level, cudaStream_t& stream, int iteration);

	// Coarsen `grid` repeatedly and allocate a MultigridLevel for each result.
	// Consumes the seed level: nothing keeps a second host copy of the hierarchy.
	void buildLevels(GridLevel grid);

	// prolongation from coarse -> fine grid
	void buildProlongation(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// restriction from fine -> coarse grid
	void buildRestriction(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// Galerkin coarse operator, A_H = P^T A_h P with piecewise-constant P: scatter
	// every fine entry into the coarse entry its two ends agglomerate into.
	void buildCoarseOperator(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// create multigrid level
	MultigridLevel createMultigridLevel(GridLevel grid);

	void vCycle(int l, cudaStream_t& stream);
};
