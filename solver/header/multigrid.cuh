#pragma once
#include "solver_struct.h"
#include "boundary_struct.h"


struct GridLevel {

	int nr = 0;
	int nz = 0;
	int N = 0;

	std::vector<double> rFace;
	std::vector<double> zFace;
	std::vector<uint8_t> active;

};

struct MultigridLevel {

	GridLevel grid;

	double* x = nullptr;
	double* xTemp = nullptr;
	double* d_rFace = nullptr;
	double* d_zFace = nullptr;
	uint8_t* d_active = nullptr;

	// per-level residual vector. Previously read off Coefficients::res, which was
	// removed when residual state moved to ConfigResidual; a multigrid level owns
	// its own residual for restriction/smoothing. Allocated when MG is wired in.
	double* res = nullptr;

	Coefficients coeff;

};


class MultigridSolver {

public:

	MultigridSolver(MemoryConfig& mem, GridLevel& grid);
	~MultigridSolver();

	// each level owns raw device pointers freed in the destructor; a copy would
	// alias then double-free them. non-copyable (emplaced into std::optional, so
	// no copy/move is needed anyway).
	MultigridSolver(const MultigridSolver&) = delete;
	MultigridSolver& operator=(const MultigridSolver&) = delete;

	std::vector<GridLevel> grids;
	std::vector<MultigridLevel> levels;

	// run multigrid. this will be called in the solver
	void run(Coefficients& coeff, cudaStream_t& stream, double* x);

	MemoryConfig& mem;

private:

	double jacobiWeight = 0.6;
	int jacobiSweep = 75;
	int jacobiPrePostSweep = 3;


	// coarsen the grid by halve. this assumes the canCoarsen(grid) return true
	GridLevel coarsenGrid(const GridLevel& grid);

	void computeResidual(MultigridLevel& level, cudaStream_t& stream);

	// smoothen the field
	void smoothen(MultigridLevel& level, cudaStream_t& stream, int iteration);

	void buildHierarchy(GridLevel grid);

	// prolongation from coarse -> fine grid
	void buildProlongation(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// restriction from fine -> coarse grid
	void buildRestriction(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// populate levels
	void buildLevels();

	// Route A coarse operator: build the coarse level's 5-point stencil by
	// averaging the fine level's face coefficients (which already carry d = A/aP)
	void buildCoarseOperator(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream);

	// create multigrid level
	MultigridLevel createMultigridLevel(GridLevel& grid);

	// two grid cycles
	void twoGridCycle(cudaStream_t& stream);
};