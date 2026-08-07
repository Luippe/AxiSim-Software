#pragma once
#include <tuple>
#include <vector>
#include <cuda_runtime.h>
#include "solver_struct.h"

#include "cuda_graph_manager.cuh"

// ONE captured solve of one linear system, replayed as a CUDA graph. There is no
// eager entry point any more: every field goes through prepare() + run().
//
// Which scheme gets recorded is decided inside the .cu, from config.type -- Jacobi
// or multicolor Gauss-Seidel. `coloring` is only consulted for Gauss-Seidel; an
// unbuilt one (MeshColoring::valid) falls back to Jacobi.
//
// A graph bakes the coefficient pointers and the solution vector in as by-value
// kernel arguments, so u's graph physically cannot solve v -- a solver per field is
// unavoidable, and the caller owns that multiplicity (solver.cpp keeps an
// unordered_map<std::string, LinearSolver> holding only the fields a given run
// actually solves).
//
// Neither copyable nor movable: the reference members rule out assignment, and
// CudaGraph owns handles it would double-destroy. That is fine for a node-based
// container -- use unordered_map::try_emplace, which constructs in place. It rules
// out operator[] (no default constructor) and any contiguous container that
// relocates its elements.
class LinearSolver {
public:

	LinearSolver(ConfigSolver& config, MemoryConfig& mem, MeshColoring& coloring);

	// Capture the solve, or keep the existing graph when nothing it baked in has
	// changed. A hit costs one key comparison, so this is meant to be called every
	// iteration -- calling it once before the loop is what makes the key dead
	// weight, since a graph can only be found stale by asking.
	//
	// `xTemp` is the Jacobi ping-pong partner, and must be a distinct buffer of at
	// least coeff.N doubles. The graph absorbs the parity internally, so after run()
	// the answer is always in `x` regardless of the sweep count -- neither pointer is
	// reseated, unlike the eager solveLinearSystem above.
	void prepare(
		Coefficients& coeff,
		cudaStream_t& stream,
		double* x,
		double* xTemp
	);

	// Replay the prepared graph.
	void run();

private:

	ConfigSolver& config;
	MemoryConfig& mem;
	MeshColoring& coloring;

	struct RunGraphKey {

		double* AC = nullptr;
		double* b = nullptr;
		double* AF = nullptr;
		const double* x = nullptr;
		const double* xTemp = nullptr;

		int* d_cell_order = nullptr;

		cudaStream_t stream = nullptr;
		int threadsPerBlock = 0;

		int linearMaxIter = 0;

		std::vector<int> colorStart;
		int nColors = 0;

		int* faceStart = nullptr;
		int* faceNeighbor = nullptr;

		// Inputs to the dispatch enqueueSolve resolves on the host. These decide WHICH
		// kernels are recorded, not just their arguments, so a change here needs a new
		// graph rather than a replay with different numbers.
		LinearSolverType type = LinearSolverType::LINEAR_JACOBI;
		int N = 0;
		int nCells = 0;

		auto tie() const{
			return std::tie(AC, b, AF, d_cell_order, stream, threadsPerBlock, linearMaxIter, colorStart, nColors, x, xTemp, faceStart, faceNeighbor, type, N, nCells);
		}

		bool operator==(const RunGraphKey& other) const { return tie() == other.tie(); };

	};

	// ~CudaGraph tears the graph down, and this class owns no device buffers the
	// graph points into (the coefficient arrays and x belong to Solver, which
	// outlives it), so there is nothing for a ~LinearSolver to do.
	CudaGraph graph;
	RunGraphKey runGraphKey;

	void captureRunGraph(
		Coefficients& coeff,
		cudaStream_t& stream,
		double* x,
		double* xTemp
	);

	bool runGraphMatches(
		const Coefficients& coeff,
		const double* x,
		const double* xTemp,
		cudaStream_t stream
	) const;

	RunGraphKey currentKey(
		const Coefficients& coeff,
		const double* x,
		const double* xTemp,
		cudaStream_t stream
	) const;

};
