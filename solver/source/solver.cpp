#include "solver.h"
#include "printer.h"
#include "manage_file.h"
#include "memory_manager.h"
#include "linear_solver.cuh"
#include "console.h"
#include "simple.cuh"
#include "scene_view.h"
#include "residuals.cuh"
#include "solver_util.cuh"
#include <chrono>

#define CUDA_CHECK(x) do { \
  cudaError_t err = (x); \
  if (err != cudaSuccess) { \
    printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    std::abort(); \
  } \
} while(0)


Solver::Solver(SceneView& scene, Config& config) :
    scene(scene),
    config(config),
    g(config.g),
    f(config.f),
    itr(config.itr),
    residualPlot(*this, { "Axial", "Radial" ,"Continuity"}) {
    cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
}


void Solver::setDefault() {
    uBC.inlet = { BCType::DIRICHLET, 0.0 };
    uBC.outlet = { BCType::NEUMANN, 0.0 };
    uBC.outer = { BCType::DIRICHLET, 0.0 };
    uBC.centerline = { BCType::NEUMANN, 0.0 };
    vBC.inlet = { BCType::DIRICHLET, 0.0 };
    vBC.outlet = { BCType::NEUMANN, 0.0 };
    vBC.outer = { BCType::DIRICHLET, 0.0 };
    vBC.centerline = { BCType::DIRICHLET, 0.0 };
    pBC.inlet = { BCType::NEUMANN, 0.0 };
    pBC.outlet = { BCType::DIRICHLET, 0.0 };
    pBC.outer = { BCType::NEUMANN, 0.0 };
    pBC.centerline = { BCType::NEUMANN, 0.0 };
    concBC.inlet = { BCType::DIRICHLET, 0.0 };
    concBC.outlet = { BCType::NEUMANN, 0.0 };
    concBC.outer = { BCType::NEUMANN, 0.0 };
    concBC.centerline = { BCType::NEUMANN, 0.0 };

    dt = 0.1;
    tEnd = 2.0;
}

void Solver::run() {

    if (!runCheck()) return;

    if (!continueSolver) {
        residualPlot.resetState();
    }

    shutdown();                 // end any previous solver threads

    solverRunning = true;
    solverThread = std::thread([&]() {
        runSimple();
        solverRunning = false;
        });
}

bool Solver::runCheck() {
    
    if (solverRunning) {
        console->addLine("Solver still running");
        return false;
    }

    if (!scene.mesh.isReady) {
        console->addLine("No mesh exists");
        return false;
    }

    return true;
}

void Solver::shutdown() {
    if (solverThread.joinable()) {
        solverThread.join();
    }
}

void Solver::runBiCGStab() {

    //loadVelocity(g, f);

    // create streams
    cudaStream_t stream;
    VariablesBiCGStab vars;
    vars.conc = concBC.inlet.val;

    allocateGridConfig(g, f);
    allocateBiCGStab(g, f, vars);

    int N = g.N;

    cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);

    // launch M CPU threads
    double OCR;
    const int threadsPerBlock = mem.threadsPerBlock;
    const int shmem = mem.shmem;

    // record time
    CudaTimer timer;
    timer.startTimer(stream);

    double conc = vars.conc;
    const int blocks = (g.N + threadsPerBlock - 1) / threadsPerBlock;
    const int cell_blocks = (g.n_cell + threadsPerBlock - 1) / threadsPerBlock;

    // start loop
    init << <blocks, threadsPerBlock, 0, stream >> > (config, vars, conc, N);
    init_alpha << <cell_blocks, threadsPerBlock, 0, stream >> > (config, vars, g.n_cell);

    //for (int out = 0; out < outer_iter; ++out) {
    for (int out = 0; out < itr.outer_iter; ++out) {
        get_wall_varj << <cell_blocks, threadsPerBlock, 0, stream >> > (config, vars, g.n_cell);
        update_preconditioner << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
        get_res_init << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);

        // check outer residual
        reduce_vec(vars, N, threadsPerBlock, shmem, vars.resnorm, vars.resnorm_val, stream);
        cudaStreamSynchronize(stream);
        if (sqrt(*vars.resnorm_val) < itr.outer_tol) break;

        // start Preconditioned BiCGStab
        for (int in = 0; in < itr.inner_iter; ++in) {
            // calculate rho
            get_rho << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            reduce_vec(vars, N, threadsPerBlock, shmem, vars.jrho, vars.jrho_val, stream);

            if (in > 0) {
                calc_beta << <1, 1, 0, stream >> > (vars);
                get_jp << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            }

            get_jp_t << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            get_v_alpha << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            reduce_vec(vars, N, threadsPerBlock, shmem, vars.alpha_den, vars.jalpha_den_val, stream);
            calc_alpha << <1, 1, 0, stream >> > (vars);

            // calculate s and shat
            get_s_s_t << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);

            // check inner residual
            if (in % itr.check_iter == 0) {
                reduce_vec(vars, N, threadsPerBlock, shmem, vars.snorm, vars.snorm_val, stream);
                cudaStreamSynchronize(stream);
                //printf("%d, %d\n", in, out);
                if (sqrt(*vars.snorm_val) < itr.inner_tol) {
                    set_x << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
                    break;
                }
            }

            // calculate t and w
            get_t_w << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            reduce_vec(vars, N, threadsPerBlock, shmem, vars.w_den, vars.jw_den_val, stream);
            reduce_vec(vars, N, threadsPerBlock, shmem, vars.w_num, vars.jw_num_val, stream);
            calc_w << <1, 1, 0, stream >> > (vars);

            // update residual and cocentration
            update_r_x << <blocks, threadsPerBlock, 0, stream >> > (config, vars, N);
            std::swap(vars.jrho_val_prev, vars.jrho_val);
        }
        init_val << <1, 1, 0, stream >> > (vars);
    }

    // calculate cw
    get_cw << <cell_blocks, threadsPerBlock, 0, stream >> > (config, vars, g.n_cell);

    // calculate OCR
    get_wall_varj << <cell_blocks, threadsPerBlock, 0, stream >> > (config, vars, g.n_cell);
    get_OCR << <cell_blocks, threadsPerBlock, 0, stream >> > (config, vars, conc, g.n_cell);
    reduce_vec(vars, g.n_cell, threadsPerBlock, shmem, vars.OCR_num, vars.OCR_num_val, stream);
    cudaStreamSynchronize(stream);

    double OCR_num_host = 0.0;
    cudaMemcpyAsync(&OCR_num_host, vars.OCR_num_val, sizeof(double), cudaMemcpyDeviceToHost, stream);

    OCR_num_host / (g.A_tot * f.Vmax);
    cudaDeviceSynchronize();

    timer.endTimer(stream);
    float ms = timer.getElapsedTime();
    console->addCompletionTime("Solver", ms);

    // copy variables to host
    concSol = SolutionField{ copyDeviceToHostVector(vars.oxy, N), g.nr, g.nz, g.dr, g.dz, CellStoreType::CENTER};

    // free memory
    free_GridConfig(g);

}


void Solver::runSimple() {

    // create configs for solver and residual
    ConfigSolver configSolver{ g, f, convectionScheme, addConvectionTerm, transient, dt};
    ConfigResidual configResidual{ currentResidual, currentResidualNorm, currentResidualScaling };

    // allocate memory
    Coefficients uCoeff, vCoeff, ppCoeff, contCoeff;
    allocateGridConfig(configSolver.g, configSolver.f);
    allocateCoefficients(configSolver, uCoeff, uBC, CellStoreType::AXIAL);
    allocateCoefficients(configSolver, vCoeff, vBC, CellStoreType::RADIAL);
    allocateCoefficients(configSolver, ppCoeff, pBC, CellStoreType::CENTER);
    allocateCoefficients(configSolver, contCoeff, pBC, CellStoreType::CENTER);
    allocateSimple(configSolver, simple);

    int Nu = g.nr * (g.nz + 1);
    int Nv = (g.nr + 1) * g.nz;
    int N = g.nr * g.nz;
    const int threadsPerBlock = mem.threadsPerBlock;
    const int shmem = mem.shmem;
    const int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
    const int uBlocks = (Nu + threadsPerBlock - 1) / threadsPerBlock;
    const int vBlocks = (Nv + threadsPerBlock - 1) / threadsPerBlock;
    const int maxBlocks = (std::max({ uCoeff.N,vCoeff.N,ppCoeff.N }) + threadsPerBlock - 1) / threadsPerBlock;
    double uRes, vRes, contRes;

    // open file if transient is turned on
    std::ofstream out;
    if (transient) {

        out.open("flow_motion.bin", std::ios::binary);
        saveBinary(out, g.nr, g.nz, g.dr, g.dz);
        writeBoundaryConditionConfig(out, uBC);
        writeBoundaryConditionConfig(out, vBC);
        writeBoundaryConditionConfig(out, pBC);

    }

    // record time
    CudaTimer timer;
    timer.startTimer(stream);

    int numSteps = transient ? (int)std::ceil(tEnd / dt) : 1;
    int counter = 0;

    for (int tCount = 0; tCount < numSteps; tCount++) {

        cudaMemcpyAsync(simple.uOld, simple.u, Nu * sizeof(double), cudaMemcpyDeviceToDevice, stream);
        cudaMemcpyAsync(simple.vOld, simple.v, Nv * sizeof(double), cudaMemcpyDeviceToDevice, stream);

        for (int k = 0; k < configSimple.maxIter; k++) {

            // create coefficients for velocity and pressure correction equations
            addUDiffusionCoefficient << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, uBC);
            addVDiffusionCoefficient << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, vBC);

            if (configSolver.addConvectionTerm) {
                addUConvectionCoefficient << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, vCoeff, simple.u, simple.v, uBC, vBC);
                addVConvectionCoefficient << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, vCoeff, simple.u, simple.v, uBC, vBC);
            }

            if (configSolver.transient) {
				addUTransientCoefficient << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, simple);
				addVTransientCoefficient << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, simple);
            }

            finalizeCoefficients << <uBlocks, threadsPerBlock, 0, stream >> > (uCoeff);
            finalizeCoefficients << <vBlocks, threadsPerBlock, 0, stream >> > (vCoeff);

            getCorrectionCoefficient << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, simple, simple.DU);
            getCorrectionCoefficient << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, simple, simple.DV);

            // solve velocity
            createURhs << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, simple);
            createVRhs << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, simple);

            cudaMemcpyAsync(simple.uTemp, simple.u, Nu * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            cudaMemcpyAsync(simple.vTemp, simple.v, Nv * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            jacobi << <uBlocks, threadsPerBlock, 0, stream >> > (uCoeff, simple.uTemp, simple.u, simple.momentumRelaxation);
            jacobi << <vBlocks, threadsPerBlock, 0, stream >> > (vCoeff, simple.vTemp, simple.v, simple.momentumRelaxation);

            // solve pressure correction
            createPPCoeff << <blocks, threadsPerBlock, 0, stream >> > (configSolver, ppCoeff, simple);
            createPPRhs << <blocks, threadsPerBlock, 0, stream >> > (configSolver, ppCoeff, simple);
            cudaMemcpyAsync(simple.ppTemp, simple.pp, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            jacobi << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff, simple.ppTemp, simple.pp, simple.correctionRelaxation);

            // update field variables
            updateUVelocity << <uBlocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, simple);
            updateVVelocity << <vBlocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, simple);
            updatePressure << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff, simple);

            // check for convergence and print residual to console
            if (k % configSimple.checkConv == 0) {

                residualAll << <maxBlocks, threadsPerBlock, 0, stream >> > (
                    ResidualPairs{ uCoeff,simple.u,nullptr },
                    ResidualPairs{ vCoeff,simple.v,nullptr });

                continuityResidual << <blocks, threadsPerBlock, 0, stream >> > (configSolver, contCoeff, simple, N);
                
                CUDA_CHECK(cudaStreamSynchronize(stream));

                residualAllHost(configResidual, uCoeff, vCoeff, contCoeff);

                uRes = uCoeff.resVal;
                vRes = vCoeff.resVal;
                contRes = contCoeff.resVal;

                residualPlot.add(counter, uRes, vRes, contRes);

                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "ITERATION: %d   U: %e  V: %e  Continuity: %e\n", k, uRes, vRes, contRes);
                std::string line(buffer);
                console->addLine(line);
                //if (contRes < configSimple.ppTol && uRes < configSimple.momTol && vRes < configSimple.momTol) break;
            }
            counter++;
        }

        if (tCount % saveKeyFrameIter == 0) {
            CUDA_CHECK(cudaStreamSynchronize(stream));

			std::vector<double> u = copyDeviceToHostVector(simple.u, Nu);
			std::vector<double> v = copyDeviceToHostVector(simple.v, Nv);
            std::vector<double> p = copyDeviceToHostVector(simple.p, N);

            saveBinary(out, (double)tCount * dt, u, v, p);
        }
    }

    if (transient) {
        out.close();
	}

    // end timer and print to console
    timer.endTimer(stream);
    float ms = timer.getElapsedTime();

    console->addCompletionTime("Solver", ms);

    // copy all necessary variables back to host
    cudaStreamSynchronize(stream);
    uSol = SolutionField{ copyDeviceToHostVector(simple.u, Nu), g.nr, g.nz + 1, g.dr, g.dz, CellStoreType::AXIAL };
    vSol = SolutionField{ copyDeviceToHostVector(simple.v, Nv), g.nr + 1, g.nz, g.dr, g.dz, CellStoreType::RADIAL };
    pSol = SolutionField{ copyDeviceToHostVector(simple.p, N), g.nr, g.nz, g.dr, g.dz, CellStoreType::CENTER };

    // free memory
    uCoeff.free();
    vCoeff.free();
    ppCoeff.free();
    contCoeff.free();
    simple.free();
    free_GridConfig(configSolver.g);
}