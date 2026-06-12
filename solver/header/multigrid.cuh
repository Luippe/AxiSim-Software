#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"


struct GridLevel {
	int nr;
	int nz;
	int N;
	
	std::vector<double> rFace; // size nr + 1
	std::vector<double> zFace; // size nz + 1

	std::vector<double> r;     // size nr
	std::vector<double> z;     // size nz

	std::vector<double> dr;    // size nr
	std::vector<double> dz;    // size nz

	std::vector<double> volume; // size nr * nz

	std::vector<uint8_t> active;

};

struct MultigridLevel {

	FVMeshDevice fvMesh;
	BoundaryFieldDevice bc;



	GridLevel grid;

	Coefficients coeff;
	double* d_volume = nullptr;
	uint8_t* d_active = nullptr;
	double* x = nullptr;


	// for jacobi solver
	double* xTemp = nullptr;

};

GridLevel createFineGrid(const GridConfig& g);

GridLevel createCoarseGrid(const GridLevel& fine);

// create a vector of coarser grids, given the initial fine grid
std::vector<GridLevel> createGridHierarchy(
	const GridLevel& fine,
	int minNr,
	int minNz
);


__global__
void restrictResidualSum(
	const double* fineResidual,
	const double* fineVolume,
	const uint8_t* fineActive,
	const uint8_t* coarseActive,
	double* coarseB,
	int fineNr,
	int fineNz,
	int coarseNr,
	int coarseNz
);

__global__
void prolongateCorrectionInjection(
	double* finePP,
	const double* coarsePP,
	const uint8_t* fineActive,
	const uint8_t* coarseActive,
	int fineNr,
	int fineNz,
	int coarseNr,
	int coarseNz
);

class MultigridSolver {
public:
	std::vector<MultigridLevel> levels;
	std::vector<std::vector<BoundarySegmentGroup>> boundaryGroupsByLevel;

	int numCycles = 2;
	int preSmooth = 3;
	int postSmooth = 3;
	int coarseIterations = 50;

	LinearSolverConfig smootherConfig;

	void allocateLevels(const std::vector<GridLevel>& gridLevels);

	void solve(Coefficients& coeff, double* x, cudaStream_t stream, int threadsPerBlock);

	void buildCoarseCoefficients(cudaStream_t stream, int threadsPerBlock);

	void coarsenBoundaryGroups(const std::vector<BoundarySegmentGroup>& fineGroups);

private:
	void vCycle(int level, cudaStream_t stream, int threadsPerBlock);

	void smooth(
		MultigridLevel& L,
		int iterations,
		cudaStream_t stream,
		int threadsPerBlock
	);

	void computeResidual(
		MultigridLevel& L,
		cudaStream_t stream,
		int threadsPerBlock
	);

	void restrictResidual(
		MultigridLevel& fine,
		MultigridLevel& coarse,
		cudaStream_t stream,
		int threadsPerBlock
	);

	void prolongateAndCorrect(
		MultigridLevel& coarse,
		MultigridLevel& fine,
		cudaStream_t stream,
		int threadsPerBlock
	);
};