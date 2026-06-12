#include "multigrid.cuh"

#include "device_launch_parameters.h"
#include <stdexcept>

#include "mesh.h"

#include "linear_solver.cuh"
#include "residuals.cuh"

#include "memory_manager.h"

inline int id(int i, int j, int nz) {
    return i * nz + j;
}

// course cell should become active if it contains at least one active fine child
void buildCoarseActiveAndVolume(const GridLevel& fine, GridLevel& coarse) {
    coarse.active.resize(coarse.N, 0);
    coarse.volume.resize(coarse.N, 0.0);

    for (int I = 0; I < coarse.nr; I++) {
        for (int J = 0; J < coarse.nz; J++) {
            int nc = I * coarse.nz + J;

            double volumeSum = 0.0;
            bool hasActive = false;

            for (int di = 0; di < 2; di++) {
                for (int dj = 0; dj < 2; dj++) {
                    int i = 2 * I + di;
                    int j = 2 * J + dj;

                    int nf = i * fine.nz + j;

                    if (fine.active[nf]) {
                        hasActive = true;
                        volumeSum += fine.volume[nf];
                    }
                }
            }

            coarse.active[nc] = hasActive ? 1 : 0;
            coarse.volume[nc] = volumeSum;
        }
    }
}

// build current grid level
void buildGridMetrics(GridLevel& g) {
    g.nr = (int)(g.rFace.size()) - 1;
    g.nz = (int)(g.zFace.size()) - 1;
    g.N = g.nr * g.nz;

    g.r.resize(g.nr);
    g.z.resize(g.nz);
    g.dr.resize(g.nr);
    g.dz.resize(g.nz);
    g.volume.resize(g.N);

    for (int i = 0; i < g.nr; i++) {
        g.dr[i] = g.rFace[i + 1] - g.rFace[i];
        g.r[i] = 0.5 * (g.rFace[i + 1] + g.rFace[i]);
    }

    for (int j = 0; j < g.nz; j++) {
        g.dz[j] = g.zFace[j + 1] - g.zFace[j];
        g.z[j] = 0.5 * (g.zFace[j + 1] + g.zFace[j]);
    }

    for (int i = 0; i < g.nr; i++) {
        double rInner = g.rFace[i];
        double rOuter = g.rFace[i + 1];

        for (int j = 0; j < g.nz; j++) {
            int n = id(i, j, g.nz);

            if (!g.active.empty() && g.active[n] == 0) {
                g.volume[n] = 0.0;
            }
            else {
                g.volume[n] =
                    PI * (rOuter * rOuter - rInner * rInner) * g.dz[j];
            }
        }
    }
}

GridLevel createFineGrid(const GridConfig& g) {

	GridLevel level;

	level.rFace = g.rFace;
	level.zFace = g.zFace;
    level.active = g.activeCell;

    buildGridMetrics(level);

	return level;

}

GridLevel createCoarseGrid(const GridLevel& fine) {

	if (fine.nr % 2 != 0 || fine.nz % 2 != 0) {
		throw std::runtime_error("Grid dimensions must be divisible by 2.");
	}

	GridLevel coarse;

	coarse.nr = fine.nr / 2;
	coarse.nz = fine.nz / 2;
	coarse.N = coarse.nr * coarse.nz;

    coarse.rFace.resize(coarse.nr + 1);
    coarse.zFace.resize(coarse.nz + 1);

	for (int I = 0; I <= coarse.nr; I++) {
		coarse.rFace[I] = fine.rFace[2 * I];
	}

	for (int J = 0; J <= coarse.nz; J++) {
		coarse.zFace[J] = fine.zFace[2 * J];
	}

    coarse.active.assign(coarse.N, 1);

    buildGridMetrics(coarse);

    buildCoarseActiveAndVolume(fine, coarse);

    return coarse;
}


std::vector<GridLevel> createGridHierarchy(
    const GridLevel& fine,
    int minNr = 4,
    int minNz = 4
) {
    std::vector<GridLevel> levels;
    levels.push_back(fine);

    while (true) {
        const GridLevel& current = levels.back();

        // can the grid become coarser?
        // if yes, add a new coarser mesh
        // if no, then exit loop
        bool canCoarsen =
            current.nr % 2 == 0 &&
            current.nz % 2 == 0 &&
            current.nr / 2 >= minNr &&
            current.nz / 2 >= minNz;


        if (!canCoarsen) {
            break;
        }

        levels.push_back(createCoarseGrid(current));
    }

    return levels;
}

__global__
void buildCoarsePPCoefficients(
    Coefficients coeff,
    const double* rFace,
    const double* zFace,
    const double* r,
    const double* dr,
    const double* dz,
    const uint8_t* active,
    int nr,
    int nz
) {
    int n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= nr * nz) return;

    int i = n / nz;
    int j = n % nz;

    coeff.AC[n] = 0.0;
    coeff.AE[n] = 0.0;
    coeff.AW[n] = 0.0;
    coeff.AN[n] = 0.0;
    coeff.AS[n] = 0.0;

    if (!active[n]) {
        coeff.AC[n] = 1.0;
        coeff.b[n] = 0.0;
        return;
    }

    double aP = 0.0;

    // axial east/west neighbors, depending on your naming convention
    if (j + 1 < nz) {
        int e = i * nz + (j + 1);

        if (active[e]) {
            double dzE = zFace[j + 2] - zFace[j + 1];
            double dzC = zFace[j + 1] - zFace[j];

            double d = 0.5 * (dzC + dzE);

            double area = PI * (rFace[i + 1] * rFace[i + 1] - rFace[i] * rFace[i]);

            double a = area / d;

            coeff.AE[n] = -a;
            aP += a;
        }
    }

    if (j - 1 >= 0) {
        int w = i * nz + (j - 1);

        if (active[w]) {
            double dzW = zFace[j] - zFace[j - 1];
            double dzC = zFace[j + 1] - zFace[j];

            double d = 0.5 * (dzC + dzW);

            double area = PI * (rFace[i + 1] * rFace[i + 1] - rFace[i] * rFace[i]);

            double a = area / d;

            coeff.AW[n] = -a;
            aP += a;
        }
    }

    // radial outer neighbor
    if (i + 1 < nr) {
        int no = (i + 1) * nz + j;

        if (active[no]) {
            double rF = rFace[i + 1];
            double area = 2.0 * PI * rF * dz[j];

            double d = r[i + 1] - r[i];

            double a = area / d;

            coeff.AN[n] = -a;
            aP += a;
        }
    }

    // radial inner neighbor
    if (i - 1 >= 0) {
        int ni = (i - 1) * nz + j;

        if (active[ni]) {
            double rF = rFace[i];
            double area = 2.0 * PI * rF * dz[j];

            double d = r[i] - r[i - 1];

            double a = area / d;

            coeff.AS[n] = -a;
            aP += a;
        }
    }

    coeff.AC[n] = aP;
}

__global__
void restrictResidualSum(
    const double* fineResidual,
    const uint8_t* fineActive,
    const uint8_t* coarseActive,
    double* coarseB,
    int fineNr,
    int fineNz,
    int coarseNr,
    int coarseNz
) {
    int n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= coarseNr * coarseNz) return;

    if (!coarseActive[n]) {
        coarseB[n] = 0.0;
        return;
    }

    int I = n / coarseNz;
    int J = n % coarseNz;

    int i0 = 2 * I;
    int j0 = 2 * J;

    double sumR = 0.0;

    for (int di = 0; di < 2; di++) {
        for (int dj = 0; dj < 2; dj++) {
            int i = i0 + di;
            int j = j0 + dj;

            int nf = i * fineNz + j;

            if (!fineActive[nf]) continue;

            sumR += fineResidual[nf];
        }
    }

    coarseB[n] = sumR;
}


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
) {
    int nf = blockIdx.x * blockDim.x + threadIdx.x;
    if (nf >= fineNr * fineNz) return;

    if (!fineActive[nf]) return;

    int i = nf / fineNz;
    int j = nf % fineNz;

    int I = i / 2;
    int J = j / 2;

    if (I >= coarseNr || J >= coarseNz) return;

    int nc = I * coarseNz + J;

    if (!coarseActive[nc]) return;

    finePP[nf] += coarsePP[nc];
}

void MultigridSolver::solve(Coefficients& coeff, double* x, cudaStream_t stream, int threadsPerBlock) {

    if (levels.empty()) return;

    MultigridLevel& finest = levels[0];

    // Attach the finest level to the current pressure-correction matrix.
    // This copies the coefficient pointers from your current ppCoeff.
    finest.coeff = coeff;

    // Start pressure correction from zero.
    cudaMemsetAsync(finest.x, 0, finest.grid.N * sizeof(double), stream);

    // IMPORTANT:
    // Coarse levels must already have valid coeff.AC, AE, AW, AN, AS.
    // Their coeff.b will be overwritten by restriction during the V-cycle.

    for (int cycle = 0; cycle < numCycles; cycle++) {
        vCycle(0, stream, threadsPerBlock);
    }

    // Copy multigrid pressure correction back to SIMPLE pp.
    cudaMemcpyAsync(
        x,
        finest.x,
        finest.grid.N * sizeof(double),
        cudaMemcpyDeviceToDevice,
        stream
    );

}

void MultigridSolver::allocateLevels(const std::vector<GridLevel>& gridLevels) {

    levels.resize(gridLevels.size());

    for (int l = 0; l < (int)(gridLevels.size()); l++) {

        levels[l].grid = gridLevels[l];

        allocateMultigridLevel(levels[l]);

    }

}

std::vector<BoundarySegmentGroup> coarsenBoundaryGroups(
    const std::vector<BoundarySegmentGroup>& fineGroups
) {
    std::vector<BoundarySegmentGroup> coarseGroups;

    for (const BoundarySegmentGroup& fineGroup : fineGroups) {
        BoundarySegmentGroup coarseGroup = fineGroup;

        coarseGroup.edges.clear();

        std::unordered_set<MeshEdge, MeshEdgeHash> uniqueEdges;

        for (const MeshEdge& fineEdge : fineGroup.edges) {
            MeshEdge coarseEdge = fineEdge;

            if (fineEdge.orient == EdgeOrient::Vertical) {
                coarseEdge.i = fineEdge.i / 2;
                coarseEdge.j = fineEdge.j / 2;
            }
            else if (fineEdge.orient == EdgeOrient::Horizontal) {
                coarseEdge.i = fineEdge.i / 2;
                coarseEdge.j = fineEdge.j / 2;
            }

            uniqueEdges.insert(coarseEdge);
        }

        coarseGroup.edges.assign(uniqueEdges.begin(), uniqueEdges.end());

        coarseGroups.push_back(std::move(coarseGroup));
    }

    return coarseGroups;
}




void MultigridSolver::vCycle(int level, cudaStream_t stream, int threadsPerBlock) {

    MultigridLevel& L = levels[level];

    bool coarsest = level == (int)(levels.size()) - 1;

    if (coarsest) {
        smooth(L, coarseIterations, stream, threadsPerBlock);
        return;
    }


    MultigridLevel& C = levels[level + 1];

    smooth(L, preSmooth, stream, threadsPerBlock);

    computeResidual(L, stream, threadsPerBlock);

    restrictResidual(L, C, stream, threadsPerBlock);

    cudaMemsetAsync(C.x, 0, C.grid.N * sizeof(double), stream);

    vCycle(level + 1, stream, threadsPerBlock);

    prolongateAndCorrect(C, L, stream, threadsPerBlock);

    smooth(L, postSmooth, stream, threadsPerBlock);

}

void MultigridSolver::smooth(
    MultigridLevel& L,
    int iterations,
    cudaStream_t stream,
    int threadsPerBlock
) {
    LinearSolverConfig config = smootherConfig;
    config.maxIter = iterations;

    solveLinearSystem(
        L.coeff,
        config,
        stream,
        L.x,
        L.xTemp,
        L.d_active,
        threadsPerBlock
    );
}

void MultigridSolver::computeResidual(
    MultigridLevel& L,
    cudaStream_t stream,
    int threadsPerBlock
) {

    const int blocks = (L.grid.N + threadsPerBlock - 1) / threadsPerBlock;
    residualAll << <blocks, threadsPerBlock, 0, stream >> > (
        L.d_active,
        true,
        ResidualPairs{ L.coeff, L.x }
        );

}

void MultigridSolver::restrictResidual(
    MultigridLevel& fine,
    MultigridLevel& coarse,
    cudaStream_t stream,
    int threadsPerBlock
) {
    int blocks = (coarse.grid.N + threadsPerBlock - 1) / threadsPerBlock;

    restrictResidualSum << <blocks, threadsPerBlock, 0, stream >> > (
        fine.coeff.res,
        fine.d_active,
        coarse.d_active,
        coarse.coeff.b,
        fine.grid.nr,
        fine.grid.nz,
        coarse.grid.nr,
        coarse.grid.nz
        );
}

void MultigridSolver::prolongateAndCorrect(
    MultigridLevel& coarse,
    MultigridLevel& fine,
    cudaStream_t stream,
    int threadsPerBlock
) {
    int blocks = (fine.grid.N + threadsPerBlock - 1) / threadsPerBlock;

    prolongateCorrectionInjection << <blocks, threadsPerBlock, 0, stream >> > (
        fine.x,
        coarse.x,
        fine.d_active,
        coarse.d_active,
        fine.grid.nr,
        fine.grid.nz,
        coarse.grid.nr,
        coarse.grid.nz
        );
}