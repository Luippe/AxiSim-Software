#include "solver.h"

#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "printer.h"
#include "file_manager.h"

#include "multigrid.cuh"
#include "linear_solver.cuh"
#include "console.h"
#include "simple.cuh"
#include "mesh.h"
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


void printResidualConsole(int currentIteration, const std::vector<ResidualPrintItem>& residualsToPrint, Console* console) {
    std::ostringstream line;

    line << "ITERAITON: " << currentIteration;
    line << std::scientific << std::setprecision(6);

    for (const ResidualPrintItem& item : residualsToPrint) {
        if (!item.enabled) continue;

        line << "  " << item.name << ": " << item.coeff->resVal;

    }

    line << "\n";

    console->addLine(line.str());
}

Solver::Solver(Config& config) :
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
    dt = 0.1;
    tEnd = 2.0;
    configSimple.maxIter = 10;
    linearSolverConfig.maxIter = 10;

}

void Solver::run(const Mesh& mesh) {

    if (!runCheck()) return;

    if (!continueSolver) {
        residualPlot->resetState();
    }

    shutdown();                 // end any previous solver threads
    addFieldType();             // add all solving field
    createResidualPrintItems(); // must be called before residualPlot->setName

    solverRunning = true;
    solverThread = std::thread([&]() {
        runSimple(mesh);
        solverRunning = false;
        });

}

void Solver::createResidualPrintItems() {

    residualsToPrint.clear();

    residualsToPrint.push_back({ "U", enabledResiduals.plotU, &uCoeff });
    residualsToPrint.push_back({ "V", enabledResiduals.plotV, &vCoeff });
    residualsToPrint.push_back({ "Continuity", enabledResiduals.plotCont, &massFluxCoeff});

    if (enabledResiduals.plotTemp) {
        residualsToPrint.push_back({ "Temperature", enabledResiduals.plotTemp, &tempCoeff });
    }

    if (enabledResiduals.plotConc) {
        residualsToPrint.push_back({ "Concentration", enabledResiduals.plotConc, nullptr});
    }
}


void Solver::createSolutions(int N) {

 
    solutions["Axial Velocity"] = SolutionField{ copyDeviceToHostVector(simple.u, N), g.dr, g.dz, BoundaryVariable::UVelocity };
    solutions["Radial Velocity"] = SolutionField{ copyDeviceToHostVector(simple.v, N), g.dr, g.dz, BoundaryVariable::VVelocity };
    solutions["Pressure"] = SolutionField{ copyDeviceToHostVector(simple.p, N), g.dr, g.dz, BoundaryVariable::Pressure };

    if (fieldOption.solveEnergy) {
        solutions["Temperature"] = SolutionField{copyDeviceToHostVector(simple.temp, N), g.dr, g.dz, BoundaryVariable::StaticTemperature};
    }
    
    if (fieldOption.solveConcentration) {

    }

}

void Solver::addFieldType() {

    fieldType.clear();

    fieldType.push_back("Axial Velocity");
    fieldType.push_back("Radial Velocity");
    fieldType.push_back("Pressure");

    if (fieldOption.solveEnergy) {
        fieldType.push_back("Temperature");
    }

    if (fieldOption.solveConcentration) {
        fieldType.push_back("Concentration");
    }
}

bool Solver::runCheck() {
    
    if (solverRunning) {
        console->addLine("Solver still running");
        return false;
    }

    //if (!mesh.isReady) {
    //    console->addLine("No mesh exists");
    //    return false;
    //}

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

// TODO(diagnostic): temporary boundary-condition sanity check.
// Reports how many boundary faces actually received a group ID and what
// BC value each group carries. If "grouped" is 0 the gmsh boundary-node
// remap failed and no inlet BC reaches the momentum source term.
static void printBoundaryDiagnostics(
    const FVMesh& fvMesh,
    const std::vector<BoundarySegmentGroup>& boundaryGroups,
    const std::vector<BoundaryEdge>& boundaryEdges,
    Console* console
) {
    if (!console) return;

    int totalFaces = static_cast<int>(fvMesh.faces.size());
    int boundaryFaces = 0;
    int groupedBoundaryFaces = 0;

    for (const FVFace& face : fvMesh.faces) {
        if (face.neighbor < 0) {
            boundaryFaces++;
            if (face.boundaryGroupID >= 0) {
                groupedBoundaryFaces++;
            }
        }
    }

    int totalEdges = static_cast<int>(boundaryEdges.size());
    int groupedEdges = 0;
    for (const BoundaryEdge& edge : boundaryEdges) {
        if (edge.groupID >= 0) {
            groupedEdges++;
        }
    }

    auto bcTypeName = [](BCType t) -> const char* {
        switch (t) {
        case DIRICHLET:       return "Dirichlet";
        case NEUMANN:         return "Neumann";
        case FULLY_DEVELOPED: return "FullyDeveloped";
        default:              return "None";
        }
    };

    auto boundaryTypeName = [](BoundaryType t) -> const char* {
        switch (t) {
        case BoundaryType::WALL:            return "Wall";
        case BoundaryType::VELOCITY_INLET:  return "VelocityInlet";
        case BoundaryType::PRESSURE_OUTLET: return "PressureOutlet";
        case BoundaryType::SYMMETRY:        return "Symmetry";
        default:                            return "Unknown";
        }
    };

    std::ostringstream line;
    line << "----- Boundary diagnostics -----\n";
    line << "Faces: total=" << totalFaces
         << " boundary=" << boundaryFaces
         << " grouped=" << groupedBoundaryFaces << "\n";
    line << "Boundary edges: total=" << totalEdges
         << " withGroup=" << groupedEdges << "\n";

    if (boundaryFaces > 0 && groupedBoundaryFaces == 0) {
        line << "WARNING: no boundary face received a group ID -> "
                "inlet BC never applied (gmsh boundary-node remap failed)\n";
    }

    for (const BoundarySegmentGroup& group : boundaryGroups) {
        int faceCount = 0;
        for (const FVFace& face : fvMesh.faces) {
            if (face.neighbor < 0 && face.boundaryGroupID == group.id) {
                faceCount++;
            }
        }

        line << "Group " << group.id << " '" << group.name << "'"
             << " type=" << boundaryTypeName(group.type)
             << " faces=" << faceCount << "\n";

        for (const auto& kv : group.bcs) {
            const char* varName = "?";
            switch (kv.first) {
            case BoundaryVariable::UVelocity:         varName = "U"; break;
            case BoundaryVariable::VVelocity:         varName = "V"; break;
            case BoundaryVariable::Pressure:          varName = "P"; break;
            case BoundaryVariable::StaticTemperature: varName = "T"; break;
            case BoundaryVariable::Concentration:     varName = "C"; break;
            default: break;
            }
            line << "    " << varName << ": " << bcTypeName(kv.second.type)
                 << " = " << kv.second.value << "\n";
        }
    }

    line << "--------------------------------\n";
    console->addLine(line.str());
}

void Solver::runSimple(const Mesh& mesh) {

    bool solveEnergy = fieldOption.solveEnergy;

    // create configs for solver and residual
    ConfigSolver configSolver{ g, f, addConvectionTerm, transient, dt};
    ConfigResidual configResidual{ currentResidual, currentResidualNorm, currentResidualScaling };

    const bool isStructuredMesh = mesh.currentMeshType == MeshType::Structured;
    const bool useFaceCoefficients = !isStructuredMesh;

    std::vector<uint8_t> emptyActiveCell;

    if (isStructuredMesh) {
        allocateGridConfig(configSolver.g, configSolver.f);
    }

    fvMesh = mesh.createFVMesh(isStructuredMesh ? configSolver.g.activeCell : emptyActiveCell);
    int N = fvMesh.numCells();
    configSolver.g.N = N;

    if (N <= 0) {
        if (console) {
            console->addLine("Solver needs a generated mesh before it can run.\n");
        }
        return;
    }

	FVMeshDevice fvMeshDevice = createFVMeshDevice(fvMesh);
	BoundarySolverDevice bcDevice = createBoundarySolverDevice(mesh.boundaryGroups, fieldOption);

	printBoundaryDiagnostics(fvMesh, mesh.boundaryGroups, mesh.boundaryEdges, console);


    // allocate memory
    int expectedFaceRefs = 0;
    for (const FVCell& cell : fvMesh.cells) {
        expectedFaceRefs += static_cast<int>(cell.faceIDs.size());
    }

    const bool needsAllocation =
        !isReady ||
        !continueSolver ||
        uCoeff.N != N ||
        uCoeff.useFaceCoeffs != (useFaceCoefficients ? 1 : 0) ||
        (useFaceCoefficients && uCoeff.nFaceRefs != expectedFaceRefs) ||
        (!useFaceCoefficients && (uCoeff.nr != configSolver.g.nr || uCoeff.nz != configSolver.g.nz));

    if (needsAllocation) {

        residualPlot->setName(residualsToPrint);

        uCoeff.free();
        vCoeff.free();
        ppCoeff.free();
        massFluxCoeff.free();
        tempCoeff.free();
        simple.free();

        if (useFaceCoefficients) {
            allocateCoefficients(uCoeff, fvMesh);
            allocateCoefficients(vCoeff, fvMesh);
            allocateCoefficients(ppCoeff, fvMesh);
            allocateCoefficients(massFluxCoeff, fvMesh);
            allocateCoefficients(tempCoeff, fvMesh);
        }
        else {
            int nr = configSolver.g.nr;
            int nz = configSolver.g.nz;

            allocateCoefficients(uCoeff, nr, nz);
            allocateCoefficients(vCoeff, nr, nz);
            allocateCoefficients(ppCoeff, nr, nz);
            allocateCoefficients(massFluxCoeff, nr, nz);
            allocateCoefficients(tempCoeff, nr, nz);
        }

        allocateSimple(configSolver, simple, fvMesh);

        currentIteration = 0;
    }

    const int threadsPerBlock = mem.threadsPerBlock;
    const int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
    int faceThreads = 128;
    int faceBlocks = (fvMeshDevice.faces.nFaces + faceThreads - 1) / faceThreads;
    uint8_t* activeCells = fvMeshDevice.cells.active;

    // open file if transient is turned on
    std::ofstream out;
    const bool canSaveStructuredTransient = transient && isStructuredMesh;
    if (transient && !isStructuredMesh && console) {
        console->addLine("Transient binary export is only supported for structured meshes; continuing without export.\n");
    }

    if (canSaveStructuredTransient) {

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

    if (isStructuredMesh) {
        GridLevel fineGrid = createFineGrid(configSolver.g);
        std::vector<GridLevel> gridLevels = createGridHierarchy(fineGrid, 4, 4);

        MultigridSolver mg;
        mg.allocateLevels(gridLevels);
        mg.smootherConfig = linearSolverConfig;
        mg.smootherConfig.maxIter = 3;
    }

    // Dedicated, stronger solve for the pressure-correction Poisson.
    // Jacobi (the only smoother available on unstructured meshes here) needs
    // far more sweeps than the momentum equations to propagate the global
    // pressure mode across the domain. Under-solving it leaves continuity
    // unenforced and makes the SIMPLE loop oscillate, so scale the sweep count
    // with the cell count.
    LinearSolverConfig pressureSolverConfig = linearSolverConfig;
    int pressureIterations = (N < 500) ? N : 500;
    if (pressureIterations < linearSolverConfig.maxIter) {
        pressureIterations = linearSolverConfig.maxIter;
    }
    pressureSolverConfig.maxIter = pressureIterations;

    for (int tCount = 0; tCount < numSteps; tCount++) {

        cudaMemcpyAsync(simple.uOld, simple.u, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
        cudaMemcpyAsync(simple.vOld, simple.v, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);

        for (int k = 0; k < configSimple.maxIter; k++) {

            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (uCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (vCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (tempCoeff);
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
            solveLinearSystem(uCoeff, linearSolverConfig, stream, simple.u, simple.uTemp, activeCells, threadsPerBlock);
            solveLinearSystem(vCoeff, linearSolverConfig, stream, simple.v, simple.vTemp, activeCells, threadsPerBlock);

            // solve pressure correction
            createPPCoeff << <blocks, threadsPerBlock, 0, stream >> > (configSolver, fvMeshDevice, ppCoeff, simple, bcDevice.p);
            
            computePressureGradient << <blocks, threadsPerBlock, 0, stream >> > (
                fvMeshDevice,
                bcDevice.p,
                simple.p,
                simple.gradPZ,
                simple.gradPR
                );

            computeFaceMassFluxRhieChow << <faceBlocks, faceThreads, 0, stream >> > (
                configSolver,
                fvMeshDevice,
                simple,
                bcDevice
                );

            createPPRhs << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppCoeff, simple);
            solveLinearSystem(ppCoeff, pressureSolverConfig, stream, simple.pp, simple.ppTemp, activeCells, threadsPerBlock);

            // update field variables
            updateVelocity << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, bcDevice.p);
			updateMassFlux << <faceBlocks, faceThreads, 0, stream >> > (configSolver, fvMeshDevice, simple, bcDevice.p);
            updatePressure << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // ======================================================================
            // -----------------------ENERGY EQUATION--------------------------------
            // ======================================================================
            
            if (solveEnergy) {

                addEnergyDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (configSolver, fvMeshDevice, tempCoeff, bcDevice.temp);

                solveLinearSystem(tempCoeff, linearSolverConfig, stream, simple.temp, simple.tempTemp, activeCells, threadsPerBlock);

            }

            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaStreamSynchronize(stream));
            // ======================================================================
            // -----------------------RESIDUAL---------------------------------------
            // ======================================================================
            // check for convergence and print residual to console
            if (k % configSimple.checkConv == 0) {

                residualAll << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice.cells.active,
                    false,
                    ResidualPairs{ uCoeff, simple.u },
                    ResidualPairs{ vCoeff, simple.v },
                    ResidualPairs{ tempCoeff, simple.temp}
                    );
                
                CUDA_CHECK(cudaGetLastError());
                CUDA_CHECK(cudaStreamSynchronize(stream));
                continuityResidual << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice,
                    configSolver,
                    massFluxCoeff,
                    simple
                    );
                
                CUDA_CHECK(cudaStreamSynchronize(stream));
                residualAllHost(configResidual, uCoeff, vCoeff, massFluxCoeff, tempCoeff);
                residualPlot->add(currentIteration, residualsToPrint);
                printResidualConsole(currentIteration, residualsToPrint, console);
                //if (contRes < configSimple.ppTol && uRes < configSimple.momTol && vRes < configSimple.momTol) break;
            }
            currentIteration++;
        }

        if (canSaveStructuredTransient && tCount % saveKeyFrameIter == 0) {
            CUDA_CHECK(cudaStreamSynchronize(stream));

            saveBinary(out, (double)(tCount + 1) * dt, 
                copyDeviceToHostVector(simple.u, N), 
                copyDeviceToHostVector(simple.v, N),
                copyDeviceToHostVector(simple.p, N));
        }
    }

    if (canSaveStructuredTransient) {
        out.close();
	}

    // end timer and print to console
    timer.endTimer(stream);
    float ms = timer.getElapsedTime();

    console->addCompletionTime("Solver", ms);

    // copy all necessary variables back to host
    CUDA_CHECK(cudaStreamSynchronize(stream));

    createSolutions(N);
    uSol = SolutionField{ copyDeviceToHostVector(simple.u, N), g.dr, g.dz, BoundaryVariable::UVelocity};
    vSol = SolutionField{ copyDeviceToHostVector(simple.v, N), g.dr, g.dz, BoundaryVariable::VVelocity};
    pSol = SolutionField{ copyDeviceToHostVector(simple.p, N), g.dr, g.dz, BoundaryVariable::Pressure};
    tempSol = SolutionField{ copyDeviceToHostVector(simple.temp, N), g.dr, g.dz, BoundaryVariable::StaticTemperature};

    //mFlux = SolutionField{copyDevice}
    isReady = true;

    // free memory
    //uCoeff.free();
    //vCoeff.free();
    //ppCoeff.free();
    //massFluxCoeff.free();
    //simple.free();
    //free_GridConfig(configSolver.g);

}
