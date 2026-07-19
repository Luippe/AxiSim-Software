#include "linear_solver.cuh"
#include "device_launch_parameters.h"
#include <utility>

__global__
void jacobi(
	Coefficients coeff,
	const double* xOld,
	double* xNew,
	uint8_t* active
) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (active && !active[n]) {
		xNew[n] = xOld[n];
		return;
	}

	double AC = coeff.AC[n];

	if (fabs(AC) < 1.0e-30) {
		xNew[n] = xOld[n];
		return;
	}

	double val = coeff.b[n];

	if (coeff.useFaceCoeffs &&
		coeff.AF &&
		coeff.faceStart &&
		coeff.faceNeighbor) {
		int start = coeff.faceStart[n];
		int end = coeff.faceStart[n + 1];

		for (int k = start; k < end; k++) {
			int nb = coeff.faceNeighbor[k];
			if (nb >= 0) {
				val -= coeff.AF[k] * xOld[nb];
			}
		}

		xNew[n] = val / AC;
		return;
	}

	int nz = coeff.nz;
	int nr = coeff.nr;

	int j = n % nz;
	int i = n / nz;

	// East neighbor
	if (j < nz - 1) {
		val -= coeff.AE[n] * xOld[n + 1];
	}

	// West neighbor
	if (j > 0) {
		val -= coeff.AW[n] * xOld[n - 1];
	}

	// North neighbor
	if (i < nr - 1) {
		val -= coeff.AN[n] * xOld[n + nz];
	}

	// South neighbor
	if (i > 0) {
		val -= coeff.AS[n] * xOld[n - nz];
	}

	xNew[n] = val / AC;
}

__global__
void gaussSeidelRB(Coefficients coeff, uint8_t* active, double* x, int color) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (active && !active[n]) {
		return;
	}

	int nr = coeff.nr;
	int nz = coeff.nz;

	int j = n % nz;
	int i = n / nz;

	if ((i + j) % 2 != color) return;

	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;
	double* b = coeff.b;

	double val = b[n];

	if (j != nz - 1) {
		val -= AE[n] * x[n + 1];
	}

	if (j != 0) {
		val -= AW[n] * x[n - 1];
	}

	if (i != nr - 1) {
		val -= AN[n] * x[n + nz];
	}

	if (i != 0) {
		val -= AS[n] * x[n - nz];
	}

	val /= AC[n];

	x[n] = val;
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
	uint8_t* active,
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

	if (active && !active[n]) return;

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

void solveLinearSystem(
	Coefficients& coeff,
	const ConfigSolver& config,
	cudaStream_t stream,
	double*& x,
	double*& xTemp,
	uint8_t*& active,
	int threadsPerBlock,
	const MeshColoring& coloring
) {

	int N = coeff.N;
	int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;

	LinearSolverType type = config.type;

	// The face path can run Gauss-Seidel only with a coloring to sweep; the
	// structured gaussSeidelRB kernel is unusable there because it derives the
	// checkerboard from coeff.nr / coeff.nz, which are 0.
	const bool faceColored =
		coeff.useFaceCoeffs &&
		coloring.valid() && coloring.nCells == N &&
		coeff.AF && coeff.faceStart && coeff.faceNeighbor;

	// Jacobi and multicolor GS are the only two schemes implemented on the face
	// path. Anything else (BiCGStab / GMRES, or GS without a usable coloring) falls
	// back to Jacobi rather than dropping through the switch and silently running
	// zero iterations.
	if (coeff.useFaceCoeffs && !(type == LINEAR_GS_RB && faceColored)) {
		type = LINEAR_JACOBI;
	}

	switch (type) {
	case LINEAR_JACOBI:
		for (int k = 0; k < config.maxIter; k++) {
			jacobi << <blocks, threadsPerBlock, 0, stream >> > (coeff, x, xTemp, active);
			std::swap(x, xTemp);
		}
		break;

	case LINEAR_GS_RB:

		if (faceColored) {

			for (int k = 0; k < config.maxIter; k++) {
				for (int c = 0; c < coloring.nColors; c++) {

					const int begin = coloring.colorStart[c];
					const int count = coloring.colorStart[c + 1] - begin;

					if (count <= 0) continue;

					const int colorBlocks = (count + threadsPerBlock - 1) / threadsPerBlock;

					gaussSeidelColorSweep << <colorBlocks, threadsPerBlock, 0, stream >> > (
						coeff, active, x, coloring.d_cellOrder, begin, count);
				}
			}

			break;
		}

		for (int k = 0; k < config.maxIter; k++) {
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, active, x, 0);
			gaussSeidelRB << <blocks, threadsPerBlock, 0, stream >> > (coeff, active, x, 1);
		}
		break;
	}
}

