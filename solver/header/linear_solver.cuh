#pragma once
#include <tuple>
#include <cuda_runtime.h>
#include "solver_struct.h"

#include "cuda_graph_manager.cuh"

// solve linear system using selected linear solver.
//
// `coloring` is only consulted for Gauss-Seidel on the face path (multiblock /
// unstructured); an unbuilt one (MeshColoring::valid) falls back to Jacobi.

class LinearSolver {
public:

	LinearSolver(ConfigSolver& config, MemoryConfig& mem, MeshColoring& coloring);

	void solveLinearSystem(
		Coefficients& coeff,
		const ConfigSolver& config,
		cudaStream_t stream,
		double*& x,
		double*& xTemp,
		int threadsPerBlock,
		const MeshColoring& coloring
	);

	void run();


	// prepare cudaGraphs and Keys
	void prepare(
		Coefficients& coeff,
		cudaStream_t& stream,
		double* x
	);

private:

	ConfigSolver& config;
	MemoryConfig& mem;
	MeshColoring& coloring;

	CudaGraph graph;

	int blocksFor(int n) const { return (n + mem.threadsPerBlock - 1) / mem.threadsPerBlock; };

	void enqueueGaussSeidel(
		Coefficients& coeff,
		cudaStream_t& stream,
		double* x
	);

	void captureRunGraph(
		Coefficients& coeff,
		cudaStream_t& stream,
		double* x
	);

	bool runGraphMatches(
		const Coefficients& coeff,
		const double* x,
		cudaStream_t stream
	) const;

	struct RunGraphKey {

		double* AC = nullptr;
		double* b = nullptr;
		double* AF = nullptr;
		const double* x = nullptr;

		int* d_cell_order = nullptr;

		cudaStream_t stream = nullptr;
		int threadsPerBlock = 0;

		int linearMaxIter = 0;

		std::vector<int> colorStart;
		int nColors = 0;

		int* faceStart = nullptr;
		int* faceNeighbor = nullptr;

		auto tie() const{
			return std::tie(AC, b, AF, d_cell_order, stream, linearMaxIter, colorStart, nColors, x, faceStart, faceNeighbor);
		}

		bool operator==(const RunGraphKey& other) const { return tie() == other.tie(); };

	};

	RunGraphKey currentKey(
		const Coefficients& coeff,
		const double* x,
		cudaStream_t stream
	) const;

	RunGraphKey runGraphKey;


};
