#include "linear_solver.cuh"
#include "device_launch_parameters.h"
#include <utility>

// ==============================================================
// ====================LINEAR SOLVERS============================
// ==============================================================

__global__
void jacobi(
	Coefficients coeff,
	const double* xOld,
	double* xNew
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;

	const double invAC = coeff.invAC[n];
	double val = coeff.b[n];

	const int start = coeff.faceStart[n];
	const int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		const int nb = coeff.faceNeighbor[k];
		if (nb >= 0) {
			val -= coeff.AF[k] * xOld[nb];
		}
	}

	xNew[n] = (invAC != 0.0) ? val * invAC : xOld[n];
}

// Gauss-Seidel over ONE color of the multicolor ordering (see MeshColoring).
//
// One thread per cell of that color. No two cells of a color share a face, so x is
// updated in place safely: every neighbour read here belongs to a different color
// and so is not being written by this launch. Colors already swept this iteration
// contribute their NEW values, which is exactly what makes this Gauss-Seidel and
// not Jacobi -- and why there is no xTemp.
__global__
void gaussSeidelColorSweep(
	Coefficients coeff,
	double* x,
	const int* cellOrder,
	int colorBegin,
	int colorCount
) {

	int t = blockIdx.x * blockDim.x + threadIdx.x;

	if (t >= colorCount) return;

	// buildMeshColoring counting-sorts every id in [0, nCells) into cellOrder, and
	// the caller checks nCells == coeff.N, so n is in range by construction
	const int n = cellOrder[colorBegin + t];

	const double AC = coeff.AC[n];

	if (fabs(AC) < 1.0e-30) return;

	double val = coeff.b[n];

	const int start = coeff.faceStart[n];
	const int end = coeff.faceStart[n + 1];

	for (int k = start; k < end; k++) {
		const int nb = coeff.faceNeighbor[k];
		if (nb >= 0) {
			val -= coeff.AF[k] * x[nb];
		}
	}

	x[n] = val / AC;
}

// ==============================================================
// ====================DISPATCH==================================
// ==============================================================

// Enqueue the CONFIGURED solve onto `stream`. A pure enqueue -- no synchronization,
// no host read of device memory -- so it is equally valid submitted directly
// (solveLinearSystem) or recorded into a stream capture (LinearSolver::prepare).
//
// Both callers going through one function is the whole point. The colored-GS loop
// used to be written out twice, once eagerly and once for capture, and the capture
// copy recorded ONLY that branch. Under the default LINEAR_JACOBI config the
// coloring is never built (solver.cpp only builds it for LINEAR_GS_RB), so nColors
// was 0, the capture recorded zero nodes, and the replayed graph silently solved
// nothing at all -- no error, no NaN, just a field that never moved.
//
// x / xTemp are by reference because the Jacobi branch ping-pongs them: on return,
// x names whichever buffer holds the answer.
static void enqueueSolve(
	Coefficients& coeff,
	const ConfigSolver& config,
	const MeshColoring& coloring,
	cudaStream_t stream,
	double*& x,
	double*& xTemp,
	int threadsPerBlock
) {

	const int N = coeff.N;
	const int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;

	// Floored at 1 for the same reason multigrid floors its cycle count: a config of
	// 0 makes the solve a no-op, and a no-op recorded into a graph is an EMPTY graph
	// that replays forever without touching the field.

	LinearSolverType type = config.type;

	// Gauss-Seidel needs a coloring to sweep.
	const bool faceColored =
		coloring.valid() && coloring.nCells == N &&
		coeff.AF && coeff.faceStart && coeff.faceNeighbor;

	// Jacobi and multicolor GS are the only two schemes implemented. Anything else
	// (BiCGStab / GMRES, or GS without a usable coloring) falls back to Jacobi rather
	// than dropping through the switch and silently running zero iterations.
	if (!(type == LinearSolverType::LINEAR_GS_RB && faceColored)) {
		type = LinearSolverType::LINEAR_JACOBI;
	}

	switch (type) {
	case LinearSolverType::LINEAR_JACOBI:
		for (int k = 0; k < config.linearMaxIter; k++) {
			jacobi << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, xTemp);
			std::swap(x, xTemp);
		}
		break;

	// Reached only when faceColored, per the normalization above.
	case LinearSolverType::LINEAR_GS_RB:
		for (int k = 0; k < config.linearMaxIter; k++) {
			for (int c = 0; c < coloring.nColors; c++) {

				const int begin = coloring.colorStart[c];
				const int count = coloring.colorStart[c + 1] - begin;

				if (count <= 0) continue;

				const int colorBlocks = (count + threadsPerBlock - 1) / threadsPerBlock;

				gaussSeidelColorSweep << <colorBlocks, threadsPerBlock, 0, stream >> > (
					coeff, x, coloring.d_cellOrder, begin, count);
			}
		}
		break;
	}
}

// ==============================================================
// ====================LINEAR SOLVER MEMBERS=====================
// ==============================================================
LinearSolver::LinearSolver(ConfigSolver& config, MemoryConfig& mem, MeshColoring& coloring) :
	config(config),
	mem(mem),
	coloring(coloring) {

}

LinearSolver::RunGraphKey LinearSolver::currentKey(
	const Coefficients& coeff,
	const double* x,
	const double* xTemp,
	cudaStream_t stream
) const {
	RunGraphKey key;

	key.stream = stream;
	key.threadsPerBlock = mem.threadsPerBlock;

	key.AC = coeff.AC;
	key.AF = coeff.AF;
	key.b = coeff.b;
	key.x = x;
	key.xTemp = xTemp;

	key.colorStart = coloring.colorStart;
	key.nColors = coloring.nColors;

	key.linearMaxIter = config.linearMaxIter;
	key.d_cell_order = coloring.d_cellOrder;

	key.faceStart = coeff.faceStart;
	key.faceNeighbor = coeff.faceNeighbor;

	// The dispatch inside enqueueSolve is resolved on the host and frozen into the
	// recorded node list, so every input to that decision has to key the graph.
	// Switching the GUI from Gauss-Seidel to Jacobi changes which kernels exist in
	// the graph, not just their arguments.
	key.type = config.type;
	key.N = coeff.N;
	key.nCells = coloring.nCells;

	return key;

}

bool LinearSolver::runGraphMatches(
	const Coefficients& coeff,
	const double* x,
	const double* xTemp,
	cudaStream_t stream
) const {

	return graph.exec != nullptr && runGraphKey == currentKey(coeff, x, xTemp, stream);

}

void LinearSolver::captureRunGraph(
	Coefficients& coeff,
	cudaStream_t& stream,
	double* x,
	double* xTemp
) {

	graph.stream = stream;

	graph.beginCapture();

	// Local copies, so the Jacobi ping-pong never reaches the caller. The host swaps
	// run once -- here, during capture -- and that is what gives every recorded node
	// its fixed, alternating input/output address. Replay does not repeat them.
	double* live = x;
	double* spare = xTemp;

	enqueueSolve(coeff, config, coloring, stream, live, spare, mem.threadsPerBlock);

	// An odd Jacobi sweep count leaves the answer in xTemp. Land it back in x from
	// inside the graph so that `x` always names the solution after run(), whatever
	// the sweep count's parity, and the caller never has to track which buffer is
	// live. One extra node, recorded only when the parity is actually odd.
	if (live != x) {
		CUDA_CHECK(cudaMemcpyAsync(
			x,
			live,
			(size_t)coeff.N * sizeof(double),
			cudaMemcpyDeviceToDevice,
			stream
		));
	}

	graph.endCapture();
	graph.instantiate();

	runGraphKey = currentKey(coeff, x, xTemp, stream);

}


void LinearSolver::prepare(
	Coefficients& coeff,
	cudaStream_t& stream,
	double* x,
	double* xTemp
) {
	if (runGraphMatches(coeff, x, xTemp, stream)) return;

	// Unconditional: destroy() null-checks both handles, and it synchronizes
	// graph.stream -- which captureRunGraph set to the stream the existing exec was
	// actually recorded on. No separate sync here, and in particular not one on the
	// key's stream, which is only meaningful once a capture has happened.
	graph.destroy();

	captureRunGraph(coeff, stream, x, xTemp);
	graph.upload();
}


void LinearSolver::run() {
	graph.launch();
}
