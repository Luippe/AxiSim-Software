#include "multigrid.cuh"

#include <math_constants.h>
#include <cstdio>
#include <vector>

#include "residuals.cuh"

#include "memory_manager.h"

__host__ __device__ inline int id(int i, int j, int nz) {
	return i * nz + j;
}

MultigridSolver::MultigridSolver(MemoryConfig& mem, GridLevel& grid) :
	mem(mem) {

	// init once
	buildHierarchy(grid);
	buildLevels();

}
bool canCoarsen(const GridLevel& grid) {
	return grid.nr % 2 == 0 && grid.nz % 2 == 0		// make sure grid size is divisible by 2
		&& grid.nr / 2 >= 4 && grid.nz / 2 >= 4;		// make sure grid size does not go below 4x4
}

GridLevel MultigridSolver::coarsenGrid(const GridLevel& grid) {

	GridLevel tempGrid;

	int nr = grid.nr;
	int nz = grid.nz;

	// populate grid dimensions
	tempGrid.nr = nr / 2;
	tempGrid.nz = nz / 2;
	tempGrid.N = tempGrid.nr * tempGrid.nz;

	// populate rFace and zFace
	for (int I = 0; I <= tempGrid.nr; I++) {
		tempGrid.rFace.push_back(grid.rFace[2 * I]);
	}

	for (int J = 0; J <= tempGrid.nz; J++) {
		tempGrid.zFace.push_back(grid.zFace[2 * J]);
	}

	// populate active. check 2x2 grid. if any of the cells are active, then the coarsened cell is also active
	tempGrid.active.assign(tempGrid.N, 0);
	for (int I = 0; I < tempGrid.nr; I++) {
		for (int J = 0; J < tempGrid.nz; J++) {

			int nTemp = I * tempGrid.nz + J;

			int n1 = (2 * I) * nz + 2 * J;
			int n2 = (2 * I + 1) * nz + 2 * J;
			int n3 = (2 * I) * nz + 2 * J + 1;
			int n4 = (2 * I + 1) * nz + 2 * J + 1;

			bool isActive = grid.active[n1] || grid.active[n2] || grid.active[n3] || grid.active[n4];

			tempGrid.active[nTemp] = (uint8_t)isActive;
		}
	}

	return tempGrid;

}

MultigridLevel MultigridSolver::createMultigridLevel(GridLevel& grid) {


	MultigridLevel level;
	level.grid = grid;
	allocateMultigridLevel(level);

	return level;

}

void MultigridSolver::buildLevels() {

	for (GridLevel& grid : grids) {

		levels.push_back(createMultigridLevel(grid));

	}

}

void MultigridSolver::buildHierarchy(GridLevel fine) {

	grids.push_back(fine);
	while (canCoarsen(grids.back())) {
		grids.push_back(coarsenGrid(grids.back()));
	}
}

void MultigridSolver::run(Coefficients& coeff, cudaStream_t& stream, double* x) {

	cudaMemcpyAsync(levels[0].x, x, coeff.N * sizeof(double), cudaMemcpyDeviceToDevice, stream);

	// load the fine operator's DATA into level 0's own buffers (not `= coeff`,
	// which would leak level 0's buffers and alias the solver's arrays)
	copyCoefficients(levels[0].coeff, coeff, levels[0].grid.N, stream);

	// start at index 1, as the 0th index contains the fine level
	for (int l = 1; l < levels.size(); l++) {
		buildCoarseOperator(levels[l - 1], levels[l], stream);
	}

	twoGridCycle(stream);

	cudaMemcpyAsync(x, levels[0].x, coeff.N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
	cudaStreamSynchronize(stream);
}

// ============================================================================
// Route A coarse operator: build A_H by AVERAGING the fine face coefficients.
// A coarse face is made of two fine faces; parallel conductances add and the
// doubled cell spacing halves them, so coarse coeff = average of the two fine.
// The fine AE/AW/AN/AS already carry d = A/aP, so this needs no geometry.
// One thread per coarse cell.
// ============================================================================
__global__
void buildCoarseOperatorKernel(Coefficients fine, Coefficients coarse, const uint8_t* coarseActive) {

	int nc = blockIdx.x * blockDim.x + threadIdx.x;
	if (nc >= coarse.N) return;

	int I = nc / coarse.nz;
	int J = nc % coarse.nz;

	// inactive coarse cell -> trivial identity row so its correction stays 0
	if (!coarseActive[nc]) {
		coarse.AE[nc] = 0.0;
		coarse.AW[nc] = 0.0;
		coarse.AN[nc] = 0.0;
		coarse.AS[nc] = 0.0;
		coarse.AC[nc] = 1.0;
		coarse.b[nc]  = 0.0;
		return;
	}

	int fnz = fine.nz;

	// coarse cell (I,J) owns fine cells i in {2I, 2I+1}, j in {2J, 2J+1}.
	// each coarse face averages the two fine faces of the same type on that side:
	double AE = 0.5 * (fine.AE[id(2 * I,     2 * J + 1, fnz)] +    // east  = right column
	                   fine.AE[id(2 * I + 1, 2 * J + 1, fnz)]);
	double AW = 0.5 * (fine.AW[id(2 * I,     2 * J,     fnz)] +    // west  = left column
	                   fine.AW[id(2 * I + 1, 2 * J,     fnz)]);
	double AN = 0.5 * (fine.AN[id(2 * I + 1, 2 * J,     fnz)] +    // north = top row
	                   fine.AN[id(2 * I + 1, 2 * J + 1, fnz)]);
	double AS = 0.5 * (fine.AS[id(2 * I,     2 * J,     fnz)] +    // south = bottom row
	                   fine.AS[id(2 * I,     2 * J + 1, fnz)]);

	coarse.AE[nc] = AE;
	coarse.AW[nc] = AW;
	coarse.AN[nc] = AN;
	coarse.AS[nc] = AS;

	// diagonal from the coarse neighbours keeps the row sum zero (constants in null space)
	coarse.AC[nc] = -(AE + AW + AN + AS);
}


__global__
void buildRestrictionKernel(Coefficients fine, Coefficients coarse, const uint8_t* coarseActive) {

	int nc = blockIdx.x * blockDim.x + threadIdx.x;
	if (nc >= coarse.N) return;

	int I = nc / coarse.nz;
	int J = nc % coarse.nz;

	if (!coarseActive[nc]) {
		coarse.b[nc] = 0.0;
		return;
	}

	int fnz = fine.nz;

	// average over 4 cells
	double r1 = fine.res[id(2 * I, 2 * J + 1, fnz)];
	double r2 = fine.res[id(2 * I, 2 * J, fnz)];
	double r3 = fine.res[id(2 * I + 1, 2 * J, fnz)];
	double r4 = fine.res[id(2 * I + 1, 2 * J + 1, fnz)];

	coarse.b[nc] = 0.25 * (r1 + r2 + r3 + r4);

}

__global__
void buildProlongationKernel(Coefficients fine, Coefficients coarse, double* xf, double* xc, const uint8_t* fineActive) {

	int nf = blockIdx.x * blockDim.x + threadIdx.x;
	if (nf >= fine.N) return;

	int I = nf / fine.nz;
	int J = nf % fine.nz;

	if (!fineActive[nf]) {
		return;
	}

	int cnz = coarse.nz;

	xf[nf] += xc[id(I / 2, J / 2, cnz)];

}

__global__
void jacobiSmoother(Coefficients coeff, double* x, const uint8_t* active, double weight) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= coeff.N) return;
	if (!active[n]) return;

	x[n] += weight * coeff.res[n] / coeff.AC[n];
}

void MultigridSolver::buildCoarseOperator(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	int blocks = (coarse.grid.N + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildCoarseOperatorKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (fine.coeff, coarse.coeff, coarse.d_active);

}

void MultigridSolver::buildRestriction(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	int blocks = (coarse.grid.N + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildRestrictionKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (fine.coeff, coarse.coeff, coarse.d_active);

}

void MultigridSolver::buildProlongation(const MultigridLevel& fine, MultigridLevel& coarse, cudaStream_t& stream) {

	int blocks = (fine.grid.N + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	buildProlongationKernel << <blocks, mem.threadsPerBlock, 0, stream >> > (fine.coeff, coarse.coeff, fine.x, coarse.x, fine.d_active);
}

void MultigridSolver::computeResidual(MultigridLevel& level, cudaStream_t& stream) {

	int blocks = (level.grid.N + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	residualAll << <blocks, mem.threadsPerBlock, 0, stream >> > (
		level.d_active,
		true,
		ResidualPairs{level.coeff, level.x}
		);

}


void MultigridSolver::twoGridCycle(cudaStream_t& stream) {
	MultigridLevel& fine = levels[0];
	MultigridLevel& coarse = levels[1];

	smoothen(fine, stream);
	computeResidual(fine, stream);
	buildRestriction(fine, coarse, stream);
	cudaMemsetAsync(coarse.x, 0, coarse.grid.N * sizeof(double), stream);
	smoothen(coarse, stream);
	buildProlongation(fine, coarse, stream);
	smoothen(fine, stream);
}

void MultigridSolver::vCycle() {

}

void MultigridSolver::smoothen(MultigridLevel& level, cudaStream_t& stream) {

	int blocks = (level.grid.N + mem.threadsPerBlock - 1) / mem.threadsPerBlock;

	for (int n = 0; n < jacobiSweep; n++) {
		computeResidual(level, stream);
		jacobiSmoother << <blocks, mem.threadsPerBlock, 0, stream >> > (level.coeff, level.x, level.d_active, jacobiWeight);
	}
}