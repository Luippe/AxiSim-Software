#include "solver.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <cuda_profiler_api.h>
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
#include "boundary_func.h"

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

    configSolver.addConvectionTerm = false;
    configSolver.transient = false;
    configSolver.dt = 0.1;
    configSolver.tEnd = 2.0;
    configSolver.maxIter = 10;

    configSimple.maxIter = 10;

}

void Solver::run(const Mesh& mesh) {

    if (!runCheck(mesh)) return;

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
        residualsToPrint.push_back({ "Concentration", enabledResiduals.plotConc, &concCoeff});
    }
}


void Solver::createSolutions(int N) {

    solutions["Axial Velocity"] = SolutionField{ copyDeviceToHostVector(simple.u, N), g.dr, g.dz, BoundaryVariable::UVelocity };
    solutions["Radial Velocity"] = SolutionField{ copyDeviceToHostVector(simple.v, N), g.dr, g.dz, BoundaryVariable::VVelocity };
    solutions["Pressure"] = SolutionField{ copyDeviceToHostVector(simple.p, N), g.dr, g.dz, BoundaryVariable::Pressure };

    solutions["dU/dz"] = SolutionField{ copyDeviceToHostVector(simple.gradUZ, N), g.dr, g.dz, BoundaryVariable::None };
    solutions["dU/dr"] = SolutionField{ copyDeviceToHostVector(simple.gradUR, N), g.dr, g.dz, BoundaryVariable::None };
    solutions["dV/dz"] = SolutionField{ copyDeviceToHostVector(simple.gradVZ, N), g.dr, g.dz, BoundaryVariable::None };
    solutions["dV/dr"] = SolutionField{ copyDeviceToHostVector(simple.gradVR, N), g.dr, g.dz, BoundaryVariable::None };
    solutions["dP/dz"] = SolutionField{ copyDeviceToHostVector(simple.gradPZ, N), g.dr, g.dz, BoundaryVariable::None };
    solutions["dP/dr"] = SolutionField{ copyDeviceToHostVector(simple.gradPR, N), g.dr, g.dz, BoundaryVariable::None };

    if (fieldOption.solveEnergy) {
        solutions["Temperature"] = SolutionField{ copyDeviceToHostVector(simple.temp, N), g.dr, g.dz, BoundaryVariable::StaticTemperature };
        solutions["dT/dz"] = SolutionField{ copyDeviceToHostVector(simple.gradTZ, N), g.dr, g.dz, BoundaryVariable::StaticTemperature };
        solutions["dT/dr"] = SolutionField{ copyDeviceToHostVector(simple.gradTR, N), g.dr, g.dz, BoundaryVariable::StaticTemperature };
    }
    
    if (fieldOption.solveConcentration) {
        solutions["Concentration"] = SolutionField{ copyDeviceToHostVector(simple.conc, N), g.dr, g.dz, BoundaryVariable::Concentration };
        solutions["dC/dz"] = SolutionField{ copyDeviceToHostVector(simple.gradCZ, N), g.dr, g.dz, BoundaryVariable::Concentration };
        solutions["dC/dr"] = SolutionField{ copyDeviceToHostVector(simple.gradCR, N), g.dr, g.dz, BoundaryVariable::Concentration };
    }

    // per-face mass flux for inspector probing (continuity + face fluxes)
    mDotHost = copyDeviceToHostVector(simple.mDot, (size_t)fvMesh.numFaces());

}

void Solver::addFieldType() {

    fieldType.clear();

    fieldType.push_back("Axial Velocity");
    fieldType.push_back("Radial Velocity");
    fieldType.push_back("Pressure");

    if (fieldOption.solveEnergy) {
        fieldType.push_back("Temperature");
        fieldType.push_back("dT/dz");
        fieldType.push_back("dT/dr");
    }

    if (fieldOption.solveConcentration) {
        fieldType.push_back("Concentration");
        fieldType.push_back("dC/dz");
        fieldType.push_back("dC/dr");
    }

    fieldType.push_back("dU/dz");
    fieldType.push_back("dU/dr");
    fieldType.push_back("dV/dz");
    fieldType.push_back("dV/dr");
    fieldType.push_back("dP/dz");
    fieldType.push_back("dP/dr");
}

namespace {
    void setContinuationReason(std::string* reason, const std::string& text) {
        if (reason) {
            *reason = text;
        }
    }
}

std::vector<uint8_t> Solver::buildStructuredActiveCells(
    const Mesh& mesh,
    std::string* reason
) const {
    const int nr = mesh.g.nr;
    const int nz = mesh.g.nz;

    if (nr <= 0 || nz <= 0) {
        setContinuationReason(reason, "Structured mesh has invalid grid dimensions.");
        return {};
    }

    const int N = nr * nz;

    if ((int)mesh.g.r.size() != nr ||
        (int)mesh.g.z.size() != nz ||
        (int)mesh.g.rFace.size() != nr + 1 ||
        (int)mesh.g.zFace.size() != nz + 1) {
        setContinuationReason(reason, "Structured mesh spacing has not been generated.");
        return {};
    }

    std::vector<uint8_t> activeCell(static_cast<size_t>(N), 1);

    for (int n : mesh.g.obstacleIndices) {
        if (n < 0 || n >= N) {
            setContinuationReason(reason, "Structured mesh obstacle data no longer matches the grid.");
            return {};
        }
        activeCell[n] = 0;
    }

    return activeCell;
}

bool Solver::buildContinuationState(
    const Mesh& mesh,
    ContinuationState& state,
    std::string* reason
) const {
    state = ContinuationState{};

    if (!mesh.isReady) {
        setContinuationReason(reason, "Generate a mesh first.");
        return false;
    }

    const bool isStructuredMesh = mesh.currentMeshType == MeshType::Structured;
    std::vector<uint8_t> activeCell;
    std::vector<uint8_t> emptyActiveCell;

    if (isStructuredMesh) {
        activeCell = buildStructuredActiveCells(mesh, reason);
        if (activeCell.empty()) {
            return false;
        }
    }

    FVMesh candidate = mesh.createFVMesh(isStructuredMesh ? activeCell : emptyActiveCell);

    if (candidate.numCells() <= 0 || candidate.numFaces() <= 0) {
        setContinuationReason(reason, "Generate a valid mesh first.");
        return false;
    }

    int faceRefs = 0;
    for (const FVCell& cell : candidate.cells) {
        faceRefs += static_cast<int>(cell.faceIDs.size());
    }

    state.valid = true;
    state.cells = candidate.numCells();
    state.faces = candidate.numFaces();
    state.faceRefs = faceRefs;
    state.nr = candidate.nr;
    state.nz = candidate.nz;
    state.useFaceCoefficients = !isStructuredMesh;
    state.solveEnergy = fieldOption.solveEnergy;
    state.solveConcentration = fieldOption.solveConcentration;

    return true;
}

bool Solver::canContinue(const Mesh& mesh, std::string* reason) const {
    if (solverRunning) {
        setContinuationReason(reason, "Solver is already running.");
        return false;
    }

    if (!isReady || !continuationState.valid) {
        setContinuationReason(reason, "Run the solver once before continuing.");
        return false;
    }

    if (!simple.u || !simple.v || !simple.p || !simple.pp || !simple.mDot) {
        setContinuationReason(reason, "Solver fields are not allocated.");
        return false;
    }

    if (fieldOption.solveEnergy != continuationState.solveEnergy) {
        setContinuationReason(reason, "Energy field selection changed.");
        return false;
    }

    if (fieldOption.solveConcentration != continuationState.solveConcentration) {
        setContinuationReason(reason, "Concentration field selection changed.");
        return false;
    }

    if (fieldOption.solveEnergy &&
        (!simple.temp || !simple.tempTemp || !simple.gradTZ || !simple.gradTR)) {
        setContinuationReason(reason, "Temperature fields are not allocated.");
        return false;
    }

    if (fieldOption.solveConcentration &&
        (!simple.conc || !simple.concTemp || !simple.gradCZ || !simple.gradCR)) {
        setContinuationReason(reason, "Concentration fields are not allocated.");
        return false;
    }

    ContinuationState current;
    if (!buildContinuationState(mesh, current, reason)) {
        return false;
    }

    if (current.useFaceCoefficients != continuationState.useFaceCoefficients) {
        setContinuationReason(reason, "Mesh type changed.");
        return false;
    }

    if (current.cells != continuationState.cells) {
        setContinuationReason(reason, "Mesh cell count changed.");
        return false;
    }

    if (current.faces != continuationState.faces) {
        setContinuationReason(reason, "Mesh face count changed.");
        return false;
    }

    if (current.faceRefs != continuationState.faceRefs) {
        setContinuationReason(reason, "Mesh connectivity changed.");
        return false;
    }

    if (!current.useFaceCoefficients &&
        (current.nr != continuationState.nr || current.nz != continuationState.nz)) {
        setContinuationReason(reason, "Structured grid dimensions changed.");
        return false;
    }

    return true;
}

bool Solver::runCheck(const Mesh& mesh) {
    
    if (solverRunning) {
        console->addLine("Solver still running");
        return false;
    }

    //if (!mesh.isReady) {
    //    console->addLine("No mesh exists");
    //    return false;
    //}

    if (configSimple.maxIter < 1) {
        configSimple.maxIter = 50;
    }

    if (configSimple.checkConv < 1) {
        configSimple.checkConv = 1;
    }

    if (configSolver.maxIter < 1) {
        configSolver.maxIter = 20;
    }

    if (configSimple.nNonOrthCorrectors < 0) {
        configSimple.nNonOrthCorrectors = 0;
    }

    if (!std::isfinite(f.rho) || !std::isfinite(f.mu) ||
        f.rho <= 0.0 || f.mu <= 0.0) {
        if (console) {
            console->addLine("Solver needs positive density and viscosity.\n");
        }
        return false;
    }

    if (continueSolver) {
        std::string reason;
        if (!canContinue(mesh, &reason)) {
            continueSolver = false;

            if (console) {
                console->addLine(
                    "Continue Solver disabled: " + reason +
                    " Starting from a fresh solve.\n"
                );
            }
        }
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

    // Resolve the BC the solver actually uses for a variable, mirroring
    // createBoundaryFieldHost: stored value only when the variable belongs to
    // the boundary type, otherwise the type's default. (Symmetry has no stored
    // variables, so its U/P resolve to Neumann and V to Dirichlet 0 here.)
    auto effectiveBC = [](const BoundarySegmentGroup& group, BoundaryVariable var) -> BoundaryCondition {
        BoundaryCondition bc = BoundaryDefaults::makeDefaultBC(group, var);
        auto it = group.bcs.find(var);
        if (it != group.bcs.end() &&
            BoundaryDefaults::isVariableInBoundaryType(var, group.type)) {
            bc = it->second;
        }
        return bc;
    };

    std::ostringstream line;
    line << "----- Boundary diagnostics (effective BCs) -----\n";
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

        const BoundaryVariable printVars[] = {
            BoundaryVariable::UVelocity,
            BoundaryVariable::VVelocity,
            BoundaryVariable::Pressure
        };
        const char* printNames[] = { "U", "V", "P" };

        for (int vi = 0; vi < 3; vi++) {
            BoundaryCondition bc = effectiveBC(group, printVars[vi]);
            line << "    " << printNames[vi] << ": " << bcTypeName(bc.type())
                 << " = " << bc.value() << "\n";
        }
    }

    line << "--------------------------------\n";
    console->addLine(line.str());
}

void Solver::runSimple(const Mesh& mesh) {

    // create configs for solver and residual
    Config config{ f, g, itr, varUnits };
    ConfigResidual configResidual{ currentResidual, currentResidualNorm, currentResidualScaling };

    const bool isStructuredMesh = mesh.currentMeshType == MeshType::Structured;
    const bool useFaceCoefficients = !isStructuredMesh;

    std::vector<uint8_t> emptyActiveCell;

    if (isStructuredMesh) {
        allocateGridConfig(config.g, config.f);
    }

    fvMesh = mesh.createFVMesh(isStructuredMesh ? config.g.activeCell : emptyActiveCell);
    int N = fvMesh.numCells();
    int Nface = fvMesh.numFaces();
    config.g.N = N;

    if (N <= 0) {
        if (console) {
            console->addLine("Solver needs a generated mesh before it can run.\n");
        }
        return;
    }

	FVMeshDevice fvMeshDevice = createFVMeshDevice(fvMesh);
	BoundarySolverDevice bcDevice = createBoundarySolverDevice(mesh.boundaryGroups, fieldOption);

	// Pressure-correction (p') boundary field: same boundary *types* as the
	// pressure field, but with all Dirichlet values forced to 0 (a fixed
	// pressure means a zero pressure correction). Used when taking grad(p').
	BoundaryFieldDevice ppBC = bcDevice.p;          // shares type/length arrays
	if (bcDevice.p.nGroups > 0) {
		CUDA_CHECK(cudaMalloc(&ppBC.valueByGroup, bcDevice.p.nGroups * sizeof(double)));
		CUDA_CHECK(cudaMemset(ppBC.valueByGroup, 0, bcDevice.p.nGroups * sizeof(double)));
	}

	printBoundaryDiagnostics(fvMesh, mesh.boundaryGroups, mesh.boundaryEdges, console);


    // allocate memory
    int expectedFaceRefs = 0;
    for (const FVCell& cell : fvMesh.cells) {
        expectedFaceRefs += static_cast<int>(cell.faceIDs.size());
    }

    const bool needsAllocation =
        !isReady ||
        !continuationState.valid ||
        !continueSolver ||
        continuationState.cells != N ||
        continuationState.faces != Nface ||
        continuationState.faceRefs != expectedFaceRefs ||
        continuationState.useFaceCoefficients != useFaceCoefficients ||
        continuationState.solveEnergy != fieldOption.solveEnergy ||
        continuationState.solveConcentration != fieldOption.solveConcentration ||
        uCoeff.N != N ||
        uCoeff.useFaceCoeffs != (useFaceCoefficients ? 1 : 0) ||
        (useFaceCoefficients && uCoeff.nFaceRefs != expectedFaceRefs) ||
        (!useFaceCoefficients && (uCoeff.nr != config.g.nr || uCoeff.nz != config.g.nz));

    if (needsAllocation) {

        residualPlot->setName(residualsToPrint);

        uCoeff.free();
        vCoeff.free();
        ppCoeff.free();
        massFluxCoeff.free();
        tempCoeff.free();
        concCoeff.free();
        simple.free();

        if (useFaceCoefficients) {
            allocateCoefficients(uCoeff, fvMesh);
            allocateCoefficients(vCoeff, fvMesh);
            allocateCoefficients(ppCoeff, fvMesh);
            allocateCoefficients(massFluxCoeff, fvMesh);
            allocateCoefficients(tempCoeff, fvMesh);
            allocateCoefficients(concCoeff, fvMesh);
        }
        else {
            int nr = config.g.nr;
            int nz = config.g.nz;

            allocateCoefficients(uCoeff, nr, nz);
            allocateCoefficients(vCoeff, nr, nz);
            allocateCoefficients(ppCoeff, nr, nz);
            allocateCoefficients(massFluxCoeff, nr, nz);
            allocateCoefficients(tempCoeff, nr, nz);
            allocateCoefficients(concCoeff, nr, nz);
        }

        allocateSimple(config, simple, fvMesh, fieldOption);

        continuationState.valid = true;
        continuationState.cells = N;
        continuationState.faces = Nface;
        continuationState.faceRefs = expectedFaceRefs;
        continuationState.nr = config.g.nr;
        continuationState.nz = config.g.nz;
        continuationState.useFaceCoefficients = useFaceCoefficients;
        continuationState.solveEnergy = fieldOption.solveEnergy;
        continuationState.solveConcentration = fieldOption.solveConcentration;

        currentIteration = 0;
    }

    // initialize threads, blocks and shared memory
    mem.init(N, fvMeshDevice.faces.nFaces);
    const int threadsPerBlock = mem.threadsPerBlock;
    const int blocks = mem.blocks;
    const int faceThreads = mem.faceThreads;
    const int faceBlocks = mem.faceBlocks;
    const int shmem = mem.shmem;
    const int shmemFace = mem.shmemFace;


    uint8_t* activeCells = fvMeshDevice.cells.active;
    bool transient = configSolver.transient;
    bool addConvectionTerm = configSolver.addConvectionTerm;
    bool solveEnergy = fieldOption.solveEnergy;
    bool solveConcentration = fieldOption.solveConcentration;
    const int applyNonOrtho = (configSimple.nNonOrthCorrectors > 0) ? 1 : 0;

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
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaStreamSynchronize(stream));

    cudaProfilerStart();

    // record time
    CudaTimer timer;
    timer.startTimer(stream);

    int numSteps = transient ? (int)std::ceil(configSolver.tEnd / configSolver.dt) : 1;

    double k = f.k;
    double cp = f.cp;
    double rho = f.rho;
    double thermDiffusivity = k / (rho * cp);

    //print(Nface, N, threadsPerBlock, blocks);
    for (int tCount = 0; tCount < numSteps; tCount++) {

        cudaMemcpyAsync(simple.uOld, simple.u, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
        cudaMemcpyAsync(simple.vOld, simple.v, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);

        for (int k = 0; k < configSimple.maxIter; k++) {

            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (uCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (vCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (tempCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (concCoeff);
            clearCoefficients << <blocks, threadsPerBlock, 0, stream >> > (massFluxCoeff);

            // Cell-centered velocity gradients for the momentum non-orthogonal
            // (cross-diffusion) correction, recomputed once per iteration with the
            // user-selected scheme so the correction uses the same gradient as the
            // rest of the solver instead of hard-wired Green-Gauss. Same stream as
            // the assembly below, so they are ready before it reads them.
            if (applyNonOrtho) {
                computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.u, simple.u, simple.gradUZ, simple.gradUR, gradientScheme);
                computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.v, simple.v, simple.gradVZ, simple.gradVR, gradientScheme);
            }

            // create coefficients for velocity and pressure correction equations
            addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, bcDevice.u, simple.v, simple.gradUZ, simple.gradUR, applyNonOrtho, f.mu);
            addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, bcDevice.v,  simple.u, simple.gradVZ, simple.gradVR, applyNonOrtho, f.mu);
            
            //addRadialMomentumCylindricalSource << <blocks, threadsPerBlock, 0, stream >> > (config, fvMeshDevice, vCoeff);
            if (configSolver.addConvectionTerm) {
                addConvectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, uCoeff, bcDevice.u);
                addConvectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, vCoeff, bcDevice.v);
            }

    //        if (config.transient) {
				//addUTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (config, uCoeff, simple);
				//addVTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (config, vCoeff, simple);
    //        }

            // grad(p) for the momentum body force AND Rhie-Chow, computed once
            // with the selected scheme. Held in gradPZ/gradPR until the p'
            // correctors overwrite it.
            computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.p, simple.p, simple.gradPZ, simple.gradPR, gradientScheme);

            createMomentumPressureRhs << <blocks, threadsPerBlock, 0, stream >> > (
                fvMeshDevice,
                uCoeff,
                vCoeff,
                simple
                );

            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.u, simple.momentumRelaxation);
            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.v, simple.momentumRelaxation);

            getCorrectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.DU);
            getCorrectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.DV);

            // solve velocity
            solveLinearSystem(uCoeff, configSolver, stream, simple.u, simple.uTemp, activeCells, threadsPerBlock);
            solveLinearSystem(vCoeff, configSolver, stream, simple.v, simple.vTemp, activeCells, threadsPerBlock);

            // solve pressure correction
            createPPCoeff << <blocks, threadsPerBlock, 0, stream >> > (config, fvMeshDevice, ppCoeff, simple, bcDevice.p);

            // grad(p) for Rhie-Chow is still in gradPZ/gradPR from the momentum
            // step above (nothing overwrote it), so no recompute is needed here.
            computeFaceMassFluxRhieChow << <faceBlocks, faceThreads, 0, stream >> > (config, fvMeshDevice, simple, bcDevice);

            // ---- pressure correction with deferred non-orthogonal correctors ----
            // gradPZ/gradPR are reused below to hold grad(p'); they are no longer
            // needed for Rhie-Chow until the next outer iteration. p' starts at 0,
            // so the first corrector pass has a zero cross term (pure orthogonal).
            const int nNonOrth = configSimple.nNonOrthCorrectors;

            cudaMemsetAsync(simple.pp, 0, N * sizeof(double), stream);
            cudaMemsetAsync(simple.ppTemp, 0, N * sizeof(double), stream);

            for (int corr = 0; corr <= nNonOrth; corr++) {
                computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppBC, simple.pp, simple.gradPZ, simple.gradPR, gradientScheme);
                createPPRhs << <blocks, threadsPerBlock, 0, stream >> > (config, fvMeshDevice, ppCoeff, simple, applyNonOrtho);
                solveLinearSystem(ppCoeff, configSolver, stream, simple.pp, simple.ppTemp, activeCells, threadsPerBlock);
            }

            // final grad(p') (with p' BCs) for the velocity and mass-flux corrections
            computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppBC, simple.pp, simple.gradPZ, simple.gradPR, gradientScheme);

            // update field variables
            updateVelocity << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, bcDevice.p);
			updateMassFlux << <faceBlocks, faceThreads, 0, stream >> > (config, fvMeshDevice, simple, bcDevice.p, applyNonOrtho);
            updatePressure << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple);


            // ======================================================================
            // -----------------------ENERGY EQUATION--------------------------------
            // ======================================================================
            if (solveEnergy) {
                if (applyNonOrtho) {
                    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.temp, simple.temp, simple.gradTZ, simple.gradTR, gradientScheme);
                }
                addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, tempCoeff, bcDevice.temp, simple.temp, simple.gradTZ, simple.gradTR, applyNonOrtho, thermDiffusivity);
                if (addConvectionTerm) {
                    addConvectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, tempCoeff, bcDevice.temp);
                }
                underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, tempCoeff, simple.temp, simple.momentumRelaxation);
                solveLinearSystem(tempCoeff, configSolver, stream, simple.temp, simple.tempTemp, activeCells, threadsPerBlock);
            }

            // ======================================================================
            // -----------------------CONCENTRATION EQUATION-------------------------
            // ======================================================================
            if (solveConcentration) {
                if (applyNonOrtho) {
                    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.conc, simple.conc, simple.gradCZ, simple.gradCR, gradientScheme);
                }
                addDiffusionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, concCoeff, bcDevice.conc, simple.conc, simple.gradCZ, simple.gradCR, applyNonOrtho, f.D);
                if (addConvectionTerm) {
                    addConvectionCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, concCoeff, bcDevice.conc);
                }
                underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, concCoeff, simple.conc, 1.0);
                solveLinearSystem(concCoeff, configSolver, stream, simple.conc, simple.concTemp, activeCells, threadsPerBlock);
            }

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
                    ResidualPairs{ tempCoeff, simple.temp},
                    ResidualPairs{ concCoeff, simple.conc}
                    );
                if (cudaError_t errResidualAll = cudaGetLastError(); errResidualAll != cudaSuccess) {
                    printf("residualAll launch error: %s\n", cudaGetErrorString(errResidualAll));
                }

                continuityResidual << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice,
                    massFluxCoeff,
                    simple
                    );
                if (cudaError_t errContinuity = cudaGetLastError(); errContinuity != cudaSuccess) {
                    printf("continuityResidual launch error: %s\n", cudaGetErrorString(errContinuity));
                }
                CUDA_CHECK(cudaGetLastError());
                CUDA_CHECK(cudaStreamSynchronize(stream));
                residualAllHost(configResidual, uCoeff, vCoeff, massFluxCoeff, tempCoeff, concCoeff);
                residualPlot->add(currentIteration, residualsToPrint);
                printResidualConsole(currentIteration, residualsToPrint, console);
                //if (contRes < configSimple.ppTol && uRes < configSimple.momTol && vRes < configSimple.momTol) break;
            }
            currentIteration++;
        }

        if (canSaveStructuredTransient && tCount % saveKeyFrameIter == 0) {
            CUDA_CHECK(cudaStreamSynchronize(stream));

            saveBinary(out, (double)(tCount + 1) * configSolver.dt, 
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

    cudaProfilerStop();

    console->addCompletionTime("Solver", ms);

    // copy all necessary variables back to host
    CUDA_CHECK(cudaStreamSynchronize(stream));

    // Pressure gradient for inspector probing, computed two ways from the final
    // pressure field. gradPZ/gradPR are free to reuse here (they held grad(p')
    // during the solve and are no longer needed).
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.u, simple.u, simple.gradUZ, simple.gradUR, gradientScheme);
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.v, simple.v, simple.gradVZ, simple.gradVR, gradientScheme);
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.p, simple.p, simple.gradPZ, simple.gradPR, gradientScheme);

    if (solveEnergy) {
        computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.temp, simple.temp, simple.gradTZ, simple.gradTR, gradientScheme);
    }

    if (solveConcentration) {
        computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.conc, simple.conc, simple.gradCZ, simple.gradCR, gradientScheme);
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));
    createSolutions(N);

    double* tmpA = deviceAlloc<double>(Nface);
    double* tmpB = deviceAlloc<double>(Nface);

    reduction(Nface, faceThreads, shmemFace, stream, tmpA, tmpB, fvMeshDevice.faces.ocrWall, &scalarSolutions.ocr);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    // Reactive-wall diagnostic: reports whether the wall is reaction- or
    // mass-transfer-limited and what fraction of the inlet supply it consumes.
    if (solveConcentration) {
        double* diag = deviceAlloc<double>(10);
        CUDA_CHECK(cudaMemsetAsync(diag, 0, 10 * sizeof(double), stream));

        int diagBlocks = (Nface + faceThreads - 1) / faceThreads;
        wallConsumptionDiagnostic << <diagBlocks, faceThreads, 0, stream >> > (
            fvMeshDevice, simple, bcDevice.conc, simple.conc, f.D, diag);

        double hd[10] = {};
        CUDA_CHECK(cudaMemcpyAsync(hd, diag, 10 * sizeof(double), cudaMemcpyDeviceToHost, stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));
        cudaFree(diag);

        double totalOCR = hd[0], inlet = hd[1], ceiling = hd[2], nWall = hd[3];

        std::ostringstream d;
        d << "\n==== Wall consumption diagnostic (concentration) ====\n";
        if (nWall < 0.5) {
            d << "  no reactive (Michaelis-Menten / Hill) wall faces found\n";
        }
        else {
            d << std::scientific << std::setprecision(4);
            d << "  reactive wall faces : " << (long long)nWall << "\n";
            d << "  Vmax (solver sees)  : " << hd[8] / nWall << " (base nmol/m^2/s)\n";
            d << "  Km   (solver sees)  : " << hd[9] / nWall << " (base nmol/m^3)\n";
            d << "  mean dPF            : " << hd[5] / nWall << " m\n";
            d << "  mean h (D/dPF)      : " << hd[6] / nWall << " m/s\n";
            d << "  mean cw / cp        : " << hd[4] / nWall << " / " << hd[7] / nWall
              << "   (cw->0 => mass-transfer limited)\n";
            d << "  total wall OCR      : " << totalOCR << " (amount/s)\n";
            d << "  mass-transfer ceil  : " << ceiling << " (amount/s)";
            if (ceiling > 0.0) {
                d << std::fixed << std::setprecision(1)
                  << "   [OCR/ceil = " << 100.0 * totalOCR / ceiling << " %]";
            }
            d << "\n" << std::scientific << std::setprecision(4);
            d << "  inlet supply        : " << inlet << " (amount/s)\n";
            if (inlet > 0.0) {
                d << std::fixed << std::setprecision(2)
                  << "  >> depletion (OCR/inlet) : " << 100.0 * totalOCR / inlet << " %\n";
            }
        }
        d << "=====================================================\n";
        console->addLine(d.str());
    }

    //scalarSolutions.ocr = getOCR();
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
