#include "solver.h"

#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "printer.h"
#include "file_manager.h"

#include "linear_solver.cuh"
#include "console.h"
#include "simple.cuh"
#include "scene_view.h"
#include "residuals.cuh"
#include "solver_util.cuh"

#include "memory_manager.h"
#include "unit_manager.h"

#define CUDA_CHECK(x) do { \
  cudaError_t err = (x); \
  if (err != cudaSuccess) { \
    printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    std::abort(); \
  } \
} while(0)

template <size_t N>
void printResidualConsole(int currentIteration, const std::array<ResidualPrintItem, N>& residualsToPrint, Console* console) {
    std::ostringstream line;

    line << "ITERAITON: " << currentIteration;
    line << std::scientific << std::setprecision(6);

    for (const ResidualPrintItem& item : residualsToPrint) {
        if (!item.enabled) continue;

        line << "  " << item.name << ": " << *item.residual;

    }

    line << "\n";

    console->addLine(line.str());
}

Solver::Solver(SceneView & scene, Config & config) :
    scene(scene),
    config(config),
    g(config.g),
    f(config.f),
    itr(config.itr),
    varUnits(config.varUnits){

    cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
    //setDefault();
}


void Solver::setDefault() {

    addConvectionTerm = false;
    transient = false;
    energyEquation = false;
    dt = 0.1;
    tEnd = 2.0;
    configSimple.maxIter = 10;
    linearSolverConfig.maxIter = 10;


}

void Solver::run() {

    if (!runCheck()) return;

    if (!continueSolver) {
        residualPlot->resetState();
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
    //concSol = SolutionField{ copyDeviceToHostVector(vars.oxy, N), g.nr, g.nz, g.dr, g.dz};

    // free memory
    free_GridConfig(g);

}

void Solver::runSimple() {


    // create configs for solver and residual
    ConfigSolver configSolver{ g, f, addConvectionTerm, transient, dt};
    ConfigResidual configResidual{ currentResidual, currentResidualNorm, currentResidualScaling };
    allocateGridConfig(configSolver.g, configSolver.f);

    fvMesh = scene.mesh.createStructuredMesh(configSolver.g.activeCell);
	FVMeshDevice fvMeshDevice = createFVMeshDevice(fvMesh);
	BoundarySolverDevice bcDevice = createBoundarySolverDevice(scene.mesh.boundaryGroups, fieldOption);

    // initialize residuals
    std::array<ResidualPrintItem, 6> residualsToPrint = { {
        {"U",             enabledResiduals.plotU,   &uCoeff.resVal},
        {"V",             enabledResiduals.plotV,   &vCoeff.resVal},
        {"P",             enabledResiduals.plotP,   nullptr},
        {"Continuity",    enabledResiduals.plotCont, &massFluxCoeff.resVal},
        {"Temperature",   enabledResiduals.plotTemp, nullptr},
        {"Concentration", enabledResiduals.plotConc, nullptr}
    } };

    // allocate memory
    if (!solutionReady || !continueSolver) {

        residualPlot->setName(residualsToPrint);

        allocateCoefficients(configSolver, uCoeff);
        allocateCoefficients(configSolver, vCoeff);
        allocateCoefficients(configSolver, ppCoeff);
        allocateCoefficients(configSolver, massFluxCoeff);
        allocateSimple(configSolver, simple, fvMesh);
        currentIteration = 0;
    }

    int N = g.nr * g.nz;
    const int threadsPerBlock = mem.threadsPerBlock;
    const int shmem = mem.shmem;
    const int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
    const int maxBlocks = (std::max({ uCoeff.N,vCoeff.N,ppCoeff.N }) + threadsPerBlock - 1) / threadsPerBlock;
    int faceThreads = 128;
    int faceBlocks = (fvMeshDevice.faces.nFaces + faceThreads - 1) / faceThreads;

    // open file if transient is turned on
    std::ofstream out;
    if (transient) {

        out.open("flow_motion.bin", std::ios::binary);
        saveBinary(out, g.nr, g.nz, g.dr, g.dz);
        
        //// save initial field
        //saveBinary(out, (double)0 * dt,
        //    copyDeviceToHostVector(simple.u, N),
        //    copyDeviceToHostVector(simple.v, N),
        //    copyDeviceToHostVector(simple.p, N));

    }


    // record time
    CudaTimer timer;
    timer.startTimer(stream);

    int numSteps = transient ? (int)std::ceil(tEnd / dt) : 1;

    for (int tCount = 0; tCount < numSteps; tCount++) {

        cudaMemcpyAsync(simple.uOld, simple.u, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
        cudaMemcpyAsync(simple.vOld, simple.v, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);

        for (int k = 0; k < configSimple.maxIter; k++) {

            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (uCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (vCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (massFluxCoeff);
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // create coefficients for velocity and pressure correction equations
            addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (configSolver, fvMeshDevice, uCoeff, bcDevice.u);
            addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (configSolver, fvMeshDevice, vCoeff, bcDevice.v);
            CUDA_CHECK(cudaStreamSynchronize(stream));

            if (configSolver.addConvectionTerm) {
                addMomentumConvectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice,
                    uCoeff,
                    vCoeff,
                    simple,
                    bcDevice
                    );
            }

    //        if (configSolver.transient) {
				//addUTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (configSolver, uCoeff, simple);
				//addVTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (configSolver, vCoeff, simple);
    //        }

            createMomentumPressureRhs << <blocks, threadsPerBlock, 0, stream >> > (
                fvMeshDevice,
                uCoeff,
                vCoeff,
                simple,
                bcDevice.p
                );


            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.u, simple.momentumRelaxation);
            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.v, simple.momentumRelaxation);
            CUDA_CHECK(cudaStreamSynchronize(stream));

            getCorrectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.DU);
            getCorrectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.DV);
            CUDA_CHECK(cudaStreamSynchronize(stream));
            

            // solve velocity
            solveLinearSystem(fvMeshDevice, uCoeff, linearSolverConfig, stream, simple.u, simple.uTemp, threadsPerBlock);
            solveLinearSystem(fvMeshDevice, vCoeff, linearSolverConfig, stream, simple.v, simple.vTemp, threadsPerBlock);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // solve pressure correction
            createPPCoeff << <blocks, threadsPerBlock, 0, stream >> > (configSolver, fvMeshDevice, ppCoeff, simple, bcDevice.p);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));
            

            computePressureGradient << <blocks, threadsPerBlock, 0, stream >> > (
                fvMeshDevice,
                bcDevice.p,
                simple.p,
                simple.gradPZ,
                simple.gradPR
                );

            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));

            computeFaceMassFluxRhieChow << <faceBlocks, faceThreads, 0, stream >> > (
                configSolver,
                fvMeshDevice,
                simple,
                bcDevice
                );

            createPPRhs << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppCoeff, simple);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));
            solveLinearSystem(fvMeshDevice, ppCoeff, linearSolverConfig, stream, simple.pp, simple.ppTemp, threadsPerBlock);
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // update field variables
            updateVelocity << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, bcDevice.p);
			updateMassFlux << <faceBlocks, faceThreads, 0, stream >> > (configSolver, fvMeshDevice, simple, bcDevice.p);
            updatePressure << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // check for convergence and print residual to console
            if (k % configSimple.checkConv == 0) {

                residualAll << <maxBlocks, threadsPerBlock, 0, stream >> > (
                    ResidualPairs{ fvMeshDevice, uCoeff, simple.u},
                    ResidualPairs{ fvMeshDevice, vCoeff, simple.v});

                continuityResidual << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice,
                    configSolver,
                    massFluxCoeff,
                    simple
                    );
                
                CUDA_CHECK(cudaStreamSynchronize(stream));
                residualAllHost(configResidual, uCoeff, vCoeff, massFluxCoeff);
                residualPlot->add(currentIteration, residualsToPrint);
                printResidualConsole(currentIteration, residualsToPrint, console);
                //if (contRes < configSimple.ppTol && uRes < configSimple.momTol && vRes < configSimple.momTol) break;
            }
            currentIteration++;
        }

        if (tCount % saveKeyFrameIter == 0) {
            CUDA_CHECK(cudaStreamSynchronize(stream));

            saveBinary(out, (double)(tCount + 1) * dt, 
                copyDeviceToHostVector(simple.u, N), 
                copyDeviceToHostVector(simple.v, N),
                copyDeviceToHostVector(simple.p, N));
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
    CUDA_CHECK(cudaStreamSynchronize(stream));
    uSol = SolutionField{ copyDeviceToHostVector(simple.u, N), g.dr, g.dz, BoundaryVariable::UVelocity};
    vSol = SolutionField{ copyDeviceToHostVector(simple.v, N), g.dr, g.dz, BoundaryVariable::VVelocity};
    pSol = SolutionField{ copyDeviceToHostVector(simple.p, N), g.dr, g.dz, BoundaryVariable::Pressure};
    
    solutionReady = true;

    // free memory
    //uCoeff.free();
    //vCoeff.free();
    //ppCoeff.free();
    //massFluxCoeff.free();
    //simple.free();
    //free_GridConfig(configSolver.g);

}