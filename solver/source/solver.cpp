#include "solver.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
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

void printResidualConsole(int currentIteration, const std::unordered_map<std::string, ConfigResidual>& cfg, Console* console) {
    std::ostringstream line;

    line << "ITERATION: " << currentIteration;
    line << std::scientific << std::setprecision(6);

    for (auto& [name, configResidual] : cfg) {
        if (!configResidual.enabled) continue;

        line << "  " << name << ": " << *configResidual.resVal;

    }

    line << "\n";

    console->addLine(line.str());
}


// True once every enabled residual sits at or below its own tolerance.
//
// Disabled residuals are skipped on purpose: residualAllHost only refreshes the
// ones that are enabled, so a disabled field keeps a stale resVal (0.0 until the
// first solve) and would pass the test for free. If nothing is enabled there is
// no convergence criterion at all, so report false and let the solver run out
// its iteration budget.
//
// The comparison is written as !(res <= tol) rather than (res > tol) so that a
// NaN residual counts as NOT converged. A diverged solve produces NaN, and
// NaN > tol is false -- the naive form would report convergence on a blown-up
// solution.
bool residualsConverged(const std::unordered_map<std::string, ConfigResidual>& cfg) {

    bool anyEnabled = false;

    for (auto& [name, configResidual] : cfg) {
        if (!configResidual.enabled) continue;

        anyEnabled = true;

        if (!(*configResidual.resVal <= configResidual.tol)) return false;
    }

    return anyEnabled;
}


void initCoefficients(std::unordered_map<std::string, Coefficients>& coeffs) {

    // No "Continuity" entry: continuity is a residual, not a linear system. Its
    // residual comes straight off the mass fluxes in continuityResidual.
    coeffs.emplace("U", Coefficients{});
    coeffs.emplace("V", Coefficients{});
    coeffs.emplace("PP", Coefficients{});
    coeffs.emplace("Temperature", Coefficients{});
    coeffs.emplace("Concentration", Coefficients{});

}


void initConfigResiduals(std::unordered_map<std::string, ConfigResidual>& cfg) {

    cfg.clear();

    cfg.emplace("U", ConfigResidual{});
    cfg.emplace("V", ConfigResidual{});
    cfg.emplace("Continuity", ConfigResidual{});
    cfg.emplace("Temperature", ConfigResidual{});
    cfg.emplace("Concentration", ConfigResidual{});

    cfg.at("U").enabled = true;
    cfg.at("V").enabled = true;
    cfg.at("Continuity").enabled = true;

}

Solver::Solver(Config& config) :
    config(config),
    g(config.g),
    f(config.f),
    varUnits(config.varUnits){

    cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);

    initCoefficients(coeffs);
    initConfigResiduals(cfg);
}



void Solver::reset() {

    // Join any running solve before wiping the state it touches.
    shutdown();

    solverRunning = false;
    stopRequested = false;
    continueSolver = false;
    isReady = false;

    // solved data and derived fields
    solutions.clear();
    timeFrames.clear();
    fieldType.clear();
    fvMesh = FVMesh{};
    continuationState = ContinuationState{};
    currentIteration = 0;

    // run configuration back to defaults
    fieldOption = SolverFieldOption{};
    configSolver = ConfigSolver{};
    configSimple = ConfigSimple{};
    configMultigrid = ConfigMultigrid{};

    // default per-residual display settings (rebinds cfg the same way the ctor does)
    initConfigResiduals(cfg);
}

void Solver::run(Mesh& mesh) {

    // Bring the cached FVMesh up to date before anything reads it -- boundary
    // groups can be created after generate(), and their IDs are baked into
    // FVFace::boundaryGroupID. Everything below (runCheck's continuation state and
    // runSimple on the worker thread) then reads that one instance.
    mesh.refreshFVMesh();

    if (!runCheck(mesh)) return;

    if (!continueSolver) {
        residualPlot->resetState();
    }

    shutdown();                 // end any previous solver threads
    addFieldType();             // add all solving field

    // Clear before the thread starts, or a stop requested against the previous
    // run would immediately end this one.
    stopRequested = false;
    solverRunning = true;

    solverThread = std::thread([&]() {
        runSimple(mesh);
        solverRunning = false;
        });

}

void Solver::requestStop() {

    if (!solverRunning) return;

    stopRequested = true;

    if (console) {
        console->addLine("Stop requested: finishing the current iteration...\n");
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

    solutions["Continuity"] = SolutionField{
        getMassImbalance(N),
        g.dr,
        g.dz,
        BoundaryVariable::None
    };

    createDerivedVelocitySolutions(N);
}

void Solver::createDerivedVelocitySolutions(int N) {

    // Both are functions of the final u/v, so they are derived here rather than
    // solved for -- and, like the gradient fields above, they are not captured per
    // time step, so a transient run leaves them at the final state.
    const std::vector<double>& u = solutions.at("Axial Velocity").field;
    const std::vector<double>& v = solutions.at("Radial Velocity").field;

    std::vector<double> speed((size_t)N, 0.0);
    std::vector<double> cellRe((size_t)N, 0.0);

    const int nCells = std::min(N, (int)fvMesh.cells.size());

    for (int c = 0; c < nCells; c++) {
        const FVCell& cell = fvMesh.cells[c];

        speed[c] = std::sqrt(u[c] * u[c] + v[c] * v[c]);

        // Re_cell = rho |V| h / mu, with h the cell's own length scale: the square
        // root of its r-z cross-section, which is the one measure that means the
        // same thing on a structured quad, a trellis block quad and a triangle.
        // This is the Reynolds number the DISCRETIZATION sees, not the flow's --
        // it says whether a cell is small enough for the convection scheme, so it
        // is read against the mesh rather than against the physics.
        const double h = std::sqrt(std::max(cell.area2D, 0.0));

        cellRe[c] = (f.mu > 0.0) ? f.rho * speed[c] * h / f.mu : 0.0;
    }

    solutions["Velocity Magnitude"] = SolutionField{
        std::move(speed),
        g.dr,
        g.dz,
        BoundaryVariable::None
    };

    solutions["Cell Reynolds Number"] = SolutionField{
        std::move(cellRe),
        g.dr,
        g.dz,
        BoundaryVariable::None
    };
}

void Solver::captureTimeFrame(double time, int N) {

    if ((int)timeFrames.size() >= maxTimeFrames) {

        // Warn once, on the frame that trips the cap, then stay silent.
        if ((int)timeFrames.size() == maxTimeFrames && console) {
            std::ostringstream msg;
            msg << "Animation frame limit (" << maxTimeFrames
                << ") reached; later frames are not captured. "
                   "Raise the keyframe interval to cover the whole run.\n";
            console->addLine(msg.str());
        }
        return;
    }

    TimeFrame frame;
    frame.time = time;

    frame.fields["Axial Velocity"] = copyDeviceToHostVector(simple.u, N);
    frame.fields["Radial Velocity"] = copyDeviceToHostVector(simple.v, N);
    frame.fields["Pressure"] = copyDeviceToHostVector(simple.p, N);

    if (fieldOption.solveEnergy) {
        frame.fields["Temperature"] = copyDeviceToHostVector(simple.temp, N);
    }

    if (fieldOption.solveConcentration) {
        frame.fields["Concentration"] = copyDeviceToHostVector(simple.conc, N);
    }

    timeFrames.push_back(std::move(frame));
}

std::vector<double> Solver::getMassImbalance(int N) {

    const std::vector<double> mDotHost =
        copyDeviceToHostVector(simple.mDot, (size_t)fvMesh.numFaces());

    std::vector<double> mContinuity((size_t)N, 0.0);

    for (int c = 0; c < N && c < (int)fvMesh.cells.size(); c++) {
        const FVCell& cell = fvMesh.cells[c];

        double imbalance = 0.0;

        for (int fid : cell.faceIDs) {
            if (fid < 0 || fid >= (int)mDotHost.size() || fid >= (int)fvMesh.faces.size()) {
                continue;
            }

            const FVFace& face = fvMesh.faces[fid];
            double mDotOwner = mDotHost[fid];

            if (face.owner == c) {
                imbalance += mDotOwner;
            }
            else if (face.neighbor == c) {
                imbalance -= mDotOwner;
            }
        }

        mContinuity[c] = imbalance;
    }

    return mContinuity;

}

void Solver::addFieldType() {

    fieldType.clear();

    fieldType.push_back("Axial Velocity");
    fieldType.push_back("Radial Velocity");
    fieldType.push_back("Velocity Magnitude");
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

    fieldType.push_back("Continuity");
    fieldType.push_back("Cell Reynolds Number");
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

    // Pulsatile conditions are uploaded once per physical time step. All device
    // kernels see them as ordinary Dirichlet conditions (set in memory_manager),
    // with only valueByGroup changing as physical time advances.
    void updatePulsatileBoundaryValues(
        const std::vector<BoundarySegmentGroup>& groups,
        BoundaryVariable variable,
        double time,
        BoundaryFieldDevice& deviceField,
        cudaStream_t stream
    ) {
        if (deviceField.nGroups <= 0 || !deviceField.valueByGroup) {
            return;
        }

        std::vector<double> values((size_t)deviceField.nGroups, 0.0);
        bool hasPulsatile = false;

        for (const BoundarySegmentGroup& group : groups) {
            if (group.id < 0 || group.id >= deviceField.nGroups) {
                continue;
            }

            BoundaryCondition bc = BoundaryDefaults::getEffectiveBC(group, variable);
            values[(size_t)group.id] = bc.valueAtTime(time);
            hasPulsatile = hasPulsatile || bc.type() == BCType::PULSATILE;
        }

        if (hasPulsatile) {
            // The previous time step's kernels still read valueByGroup, and they
            // run on `stream`, which is cudaStreamNonBlocking -- so the null-stream
            // copy below does NOT wait for them. Without this sync the new boundary
            // value can land while the tail of the previous step is still reading
            // the old one. Same hazard residualAllHost guards against with its own
            // stream sync before reading cfg.res.
            //
            // Sync sits inside the hasPulsatile branch so a run with no pulsatile
            // BC pays nothing for it.
            CUDA_CHECK(cudaStreamSynchronize(stream));

            // The group list is tiny and this happens once per time step, so a
            // synchronous copy avoids pageable-host lifetime hazards without
            // affecting the cost of the inner SIMPLE iterations. It completes
            // before this returns, and every kernel that reads the new value is
            // launched on `stream` afterwards, so host program order is enough to
            // keep the write ordered against those reads.
            CUDA_CHECK(cudaMemcpy(
                deviceField.valueByGroup,
                values.data(),
                values.size() * sizeof(double),
                cudaMemcpyHostToDevice
            ));
        }
    }
}

// Total cell->face references, i.e. the length of the face-CSR the coefficient
// systems are sized against. Compared across runs to detect a connectivity change,
// so both the continuation check and the allocation check must count it the same way.
static int countFaceRefs(const FVMesh& mesh) {
    int faceRefs = 0;
    for (const FVCell& cell : mesh.cells) {
        faceRefs += static_cast<int>(cell.faceIDs.size());
    }
    return faceRefs;
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

    // The cached mesh, refreshed by Solver::run before it got here. This used to
    // build an entire FVMesh just to read the counts below.
    const FVMesh& candidate = mesh.getFVMesh();

    if (candidate.numCells() <= 0 || candidate.numFaces() <= 0) {
        setContinuationReason(reason, "Generate a valid mesh first.");
        return false;
    }

    state.valid = true;
    state.cells = candidate.numCells();
    state.faces = candidate.numFaces();
    state.faceRefs = countFaceRefs(candidate);
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

    return true;
}

bool Solver::runCheck(const Mesh& mesh) {
    
    if (solverRunning) {
        console->addLine("Solver still running");
        return false;
    }

    if (configSimple.maxIter < 1) {
        configSimple.maxIter = 50;
    }

    if (configSimple.checkConv < 1) {
        configSimple.checkConv = 1;
    }

    if (configSolver.linearMaxIter < 1) {
        configSolver.linearMaxIter = 20;
    }

    // A structured mesh is orthogonal by construction, so the cross term is
    // identically zero -- running the corrector there only buys extra gradient
    // kernels. The GUI greys the option out for the same reason; this keeps a
    // project loaded with the flag already set from paying for it.
    if (mesh.currentMeshType == MeshType::Structured) {
        configSolver.useNonOrthCorrector = false;
    }

    if (configSolver.saveKeyFrameIter < 1) {
        configSolver.saveKeyFrameIter = 1;
    }

    // A non-positive dt divides by zero in the unsteady term, and tEnd <= 0 gives
    // zero time steps -- neither is recoverable by clamping to a guessed value.
    if (configSolver.transient) {

        if (!std::isfinite(configSolver.dt) || configSolver.dt <= 0.0) {
            if (console) {
                console->addLine("Transient run needs a positive time step.\n");
            }
            return false;
        }

        if (!std::isfinite(configSolver.tEnd) || configSolver.tEnd <= 0.0) {
            if (console) {
                console->addLine("Transient run needs a positive end time.\n");
            }
            return false;
        }
    }

    bool hasPulsatileInlet = false;
    double maxPulsatileFrequency = 0.0;

    for (const BoundarySegmentGroup& group : mesh.boundaryGroups) {
        if (group.type != BoundaryType::VELOCITY_INLET) {
            continue;
        }

        const BoundaryVariable velocityVariables[] = {
            BoundaryVariable::UVelocity,
            BoundaryVariable::VVelocity
        };

        for (BoundaryVariable variable : velocityVariables) {
            BoundaryCondition bc = BoundaryDefaults::getEffectiveBC(group, variable);
            const auto* pulsatile = std::get_if<PulsatileParams>(&bc.params);

            if (!pulsatile) {
                continue;
            }

            hasPulsatileInlet = true;

            if (!std::isfinite(pulsatile->value) ||
                !std::isfinite(pulsatile->amplitude) || pulsatile->amplitude < 0.0 ||
                !std::isfinite(pulsatile->frequency) || pulsatile->frequency <= 0.0) {
                if (console) {
                    console->addLine(
                        "Pulsatile inlet '" + group.name +
                        "' needs a finite mean velocity, A >= 0, and f > 0 Hz.\n"
                    );
                }
                return false;
            }

            maxPulsatileFrequency =
                std::max(maxPulsatileFrequency, pulsatile->frequency);
        }
    }

    if (hasPulsatileInlet && !configSolver.transient) {
        if (console) {
            console->addLine(
                "Pulsatile inlet requires Transient Solver to be enabled.\n"
            );
        }
        return false;
    }

    if (hasPulsatileInlet &&
        configSolver.dt * maxPulsatileFrequency > 0.05 && console) {
        std::ostringstream warning;
        warning << "Warning: pulsatile inlet has fewer than 20 time steps per "
                   "period. For temporal resolution, use dt <= "
                << (1.0 / (20.0 * maxPulsatileFrequency)) << " s.\n";
        console->addLine(warning.str());
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

    // Per-group counts are accumulated in this same pass; counting them per group
    // instead would rescan every face once per group.
    std::unordered_map<int, int> facesPerGroup;

    for (const FVFace& face : fvMesh.faces) {
        if (face.neighbor < 0) {
            boundaryFaces++;
            if (face.boundaryGroupID >= 0) {
                groupedBoundaryFaces++;
                facesPerGroup[face.boundaryGroupID]++;
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

    const BoundaryVariable printVars[] = {
        BoundaryVariable::UVelocity,
        BoundaryVariable::VVelocity,
        BoundaryVariable::Pressure
    };

    for (const BoundarySegmentGroup& group : boundaryGroups) {

        const auto found = facesPerGroup.find(group.id);

        line << "Group " << group.id << " '" << group.name << "'"
             << " type=" << BoundaryGet::boundaryTypeToString(group.type)
             << " faces=" << (found == facesPerGroup.end() ? 0 : found->second) << "\n";

        for (const BoundaryVariable var : printVars) {
            BoundaryCondition bc = BoundaryDefaults::getEffectiveBC(group, var);
            line << "    " << BoundaryGet::boundaryVariableToString(var)
                 << ": " << BoundaryGet::bcTypeToString(bc.type())
                 << " = " << bc.value();

            if (const auto* pulsatile = std::get_if<PulsatileParams>(&bc.params)) {
                line << " (mean), A=" << pulsatile->amplitude
                     << ", f=" << pulsatile->frequency << " Hz";
            }

            line << "\n";
        }
    }

    line << "--------------------------------\n";
    console->addLine(line.str());
}

void Solver::runSimple(const Mesh& mesh) {

    // Field coefficient systems live in the `coeffs` map (created by
    // initCoefficients). Bind readable aliases into it so the body below still
    // reads uCoeff/vCoeff/... unordered_map never relocates a value, so these stay
    // valid across the allocation below.
    Coefficients& uCoeff    = coeffs.at("U");
    Coefficients& vCoeff    = coeffs.at("V");
    Coefficients& ppCoeff   = coeffs.at("PP");
    Coefficients& tempCoeff = coeffs.at("Temperature");
    Coefficients& concCoeff = coeffs.at("Concentration");

    // Host-side only: allocateSimple needs the grid. Never hand this to a kernel --
    // GridConfig owns nine std::vectors, so a by-value kernel parameter would
    // deep-copy all nine per launch and put host heap pointers on the device.
    Config config{ f, g, varUnits };

    // Copy the mesh's cache into our own snapshot rather than rebuilding it. Still a
    // copy, deliberately: this runs on the solver thread, and owning the mesh for the
    // duration of the run keeps a GUI-side rebuild from pulling it out from under us.
    // Solver::run refreshed the cache on the GUI thread just before spawning.
    fvMesh = mesh.getFVMesh();
    int N = fvMesh.numCells();
    int Nface = fvMesh.numFaces();

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
    const int expectedFaceRefs = countFaceRefs(fvMesh);

    const bool needsAllocation =
        !isReady ||
        !continuationState.valid ||
        !continueSolver ||
        continuationState.cells != N ||
        continuationState.faces != Nface ||
        continuationState.faceRefs != expectedFaceRefs ||
        continuationState.solveEnergy != fieldOption.solveEnergy ||
        continuationState.solveConcentration != fieldOption.solveConcentration ||
        uCoeff.N != N ||
        uCoeff.AC == nullptr ||
        uCoeff.nFaceRefs != expectedFaceRefs;

    if (needsAllocation) {

        residualPlot->setName(cfg);

        for (auto& [name, coeff] : coeffs) {
            coeff.free();
        }

        for (auto& [name, configResidual] : cfg) {
            configResidual.free();
        }
        simple.free();

        allocateCoefficients(coeffs, fvMesh);

        coloring.free();

        if (configSolver.type == LinearSolverType::LINEAR_GS_RB) {
            buildMeshColoring(coloring, fvMesh);

            if (console) {
                std::ostringstream gs;
                gs << "Gauss-Seidel: " << coloring.nColors << " colors over "
                   << coloring.nCells << " cells\n";
                console->addLine(gs.str());
            }
        }

        // per-field residual vectors (res/scale) live in cfg now
        allocateResiduals(cfg, fvMesh);

        allocateSimple(config, simple, fvMesh, fieldOption);

        continuationState.valid = true;
        continuationState.cells = N;
        continuationState.faces = Nface;
        continuationState.faceRefs = expectedFaceRefs;
        continuationState.solveEnergy = fieldOption.solveEnergy;
        continuationState.solveConcentration = fieldOption.solveConcentration;

        currentIteration = 0;
    }

    // allocate scratch memory used for reduction kernels
    double* tmpA = deviceAlloc<double>(Nface);
    double* tmpB = deviceAlloc<double>(Nface);

    // initialize threads, blocks and shared memory
    mem.init(N, fvMeshDevice.faces.nFaces);
    const int threadsPerBlock = mem.threadsPerBlock;
    const int blocks = mem.blocks;
    const int faceThreads = mem.faceThreads;
    const int faceBlocks = mem.faceBlocks;

    bool transient = configSolver.transient;
    const bool useSecondOrderTime =
        transient && configSolver.timeScheme == TimeScheme::TIME_SECOND_ORDER;
    bool addConvectionTerm = configSolver.addConvectionTerm;

    // Both are handed to kernels on every SIMPLE iteration, so they are read out of
    // the config once here rather than through configSolver at each launch site.
    const ConvectionScheme convectionScheme = configSolver.convectionScheme;
    const GradientScheme gradientScheme = configSolver.gradientScheme;

    // Second-order upwind extrapolates from the upwind cell's gradient, so those
    // gradients must exist even when the non-orthogonal corrector is off.
    const bool convectionNeedsGradient =
        addConvectionTerm &&
        (convectionScheme == ConvectionScheme::CONV_SECOND_ORDER_UPWIND ||
         convectionScheme == ConvectionScheme::CONV_QUICK);
    bool solveEnergy = fieldOption.solveEnergy;
    bool solveConcentration = fieldOption.solveConcentration;
    const int applyNonOrtho = configSolver.useNonOrthCorrector ? 1 : 0;

    // Transient snapshots live in memory; Results reads timeFrames directly to build
    // the animation. Cleared for a steady run too, so switching Transient off drops
    // the previous run's frames instead of leaving the player showing stale ones.
    if (!transient || !continueSolver) {
        timeFrames.clear();
    }

    // Continuing a transient run appends to the frames already captured, so its
    // steps carry on from where the previous run stopped instead of relabelling
    // them from t = 0 and giving the animation two frames with the same time.
    const double timeOffset = timeFrames.empty() ? 0.0 : timeFrames.back().time;

    CUDA_CHECK(cudaGetLastError());

    cudaProfilerStart();

    // record time
    CudaTimer timer;
    timer.startTimer(stream);

    int numSteps = transient ? (int)std::ceil(configSolver.tEnd / configSolver.dt) : 1;

    const double thermDiffusivity = f.k / (f.rho * f.cp);

    std::unordered_map<std::string, LinearSolver> linearSolvers;

    linearSolvers.try_emplace("u", configSolver, mem, coloring);
    linearSolvers.try_emplace("v", configSolver, mem, coloring);

    // pp is handed to multigrid when that is enabled, so it only needs a linear
    // solver when it is not.
    if (!configSolver.useMultigrid) {
        linearSolvers.try_emplace("pp", configSolver, mem, coloring);
    }

    if (solveEnergy) {
        linearSolvers.try_emplace("temp", configSolver, mem, coloring);
    }

    if (solveConcentration) {
        linearSolvers.try_emplace("conc", configSolver, mem, coloring);
    }

    // Resolved once, outside the SIMPLE loop: at() hashes a string, and these run
    // every iteration. Safe to hold because unordered_map never relocates a value
    // once inserted.
    //
    // The three conditional fields are pointers rather than references because their
    // map entries only exist when the run solves them. None is ever dereferenced
    // while null -- the same condition guards the entry, the prepare and the call
    // site.
    LinearSolver& uSolver = linearSolvers.at("u");
    LinearSolver& vSolver = linearSolvers.at("v");

    // Same reasoning for the residual configs, read on every convergence check.
    ConfigResidual& uRes          = cfg.at("U");
    ConfigResidual& vRes          = cfg.at("V");
    ConfigResidual& tempRes       = cfg.at("Temperature");
    ConfigResidual& concRes       = cfg.at("Concentration");
    ConfigResidual& continuityRes = cfg.at("Continuity");

    LinearSolver* ppSolver = nullptr;
    LinearSolver* tempSolver = nullptr;
    LinearSolver* concSolver = nullptr;

    // Capturing here rather than per-iteration only bakes in ADDRESSES -- the
    // coefficient arrays are still empty at this point, and the graph replays
    // whatever numbers they hold at launch.
    uSolver.prepare(uCoeff, stream, simple.u, simple.uTemp);
    vSolver.prepare(vCoeff, stream, simple.v, simple.vTemp);

    if (!configSolver.useMultigrid) {
        ppSolver = &linearSolvers.at("pp");
        ppSolver->prepare(ppCoeff, stream, simple.pp, simple.ppTemp);
    }

    if (solveEnergy) {
        tempSolver = &linearSolvers.at("temp");
        tempSolver->prepare(tempCoeff, stream, simple.temp, simple.tempTemp);
    }

    if (solveConcentration) {
        concSolver = &linearSolvers.at("conc");
        concSolver->prepare(concCoeff, stream, simple.conc, simple.concTemp);
    }

    // Energy and species are the same scalar transport equation -- identical
    // assembly sequence, differing only in the data below. Collected once here so
    // the SIMPLE loop assembles both from one code path and they cannot drift.
    //
    // Both convect with the VOLUMETRIC flux (fluxScale 1/rho) and diffuse with a
    // kinematic diffusivity (k/(rho*cp) and f.D), so both equations are already
    // divided through by rho and the unsteady term takes capacity 1.0. f.rho > 0 is
    // enforced in runCheck.
    struct ScalarTransport {
        Coefficients& coeff;
        BoundaryFieldDevice bc;
        double* phi;
        double* gradZ;
        double* gradR;
        const double* phiOld;
        const double* phiOld2;
        double diffusivity;
        bool underRelax;      // temperature relaxes with the momentum factor, species does not
        LinearSolver* solver;
    };

    std::vector<ScalarTransport> scalarTransport;

    if (solveEnergy) {
        scalarTransport.push_back({ tempCoeff, bcDevice.temp, simple.temp,
            simple.gradTZ, simple.gradTR, simple.tempOld, simple.tempOld2,
            thermDiffusivity, true, tempSolver });
    }

    if (solveConcentration) {
        scalarTransport.push_back({ concCoeff, bcDevice.conc, simple.conc,
            simple.gradCZ, simple.gradCR, simple.concOld, simple.concOld2,
            f.D, false, concSolver });
    }

    // Multigrid coarsens the cell/face GRAPH rather than a logical nr x nz grid, so
    // it runs on any face-based mesh -- which, since every structured mesh is built
    // as multiblock, means every mesh the Generate path produces.
    std::optional<MultigridSolver> multigrid;
    if (configSolver.useMultigrid) {
        GridLevel grid = makeFinestGridLevel(fvMesh);
        multigrid.emplace(configMultigrid, mem, grid);
        multigrid->prepare(ppCoeff, stream, simple.pp);
        CUDA_CHECK(cudaGetLastError());

        if (console) {
            console->addLine(multigrid->describeHierarchy());
        }
    }

    // Initial state, so the animation opens on the field the run started from
    // rather than jumping straight to the end of the first step. Skipped when
    // continuing, where the previous run already captured this instant.
    if (transient && timeFrames.empty()) {
        CUDA_CHECK(cudaStreamSynchronize(stream));
        captureTimeFrame(timeOffset, N);
    }

    for (int tCount = 0; tCount < numSteps; tCount++) {

        // The equations for this step are implicit at t_(n+1), so prescribe the
        // inlet at that same physical time. timeOffset keeps the sine phase
        // continuous when Continue Solver appends another transient interval.
        if (transient) {
            const double stepTime =
                timeOffset + (double)(tCount + 1) * configSolver.dt;
            updatePulsatileBoundaryValues(
                mesh.boundaryGroups,
                BoundaryVariable::UVelocity,
                stepTime,
                bcDevice.u,
                stream
            );
            updatePulsatileBoundaryValues(
                mesh.boundaryGroups,
                BoundaryVariable::VVelocity,
                stepTime,
                bcDevice.v,
                stream
            );
        }

        // Freeze the previous time levels. Every SIMPLE iteration within this step
        // reassembles against the SAME old levels, so this must sit outside the inner
        // loop -- copying it per iteration would make the unsteady term chase the
        // current solution and quietly reduce the run to a relaxed steady solve.
        //
        // Shift n-1 <- n BEFORE n <- current, or both levels end up holding the same
        // field and BDF2's (4*phiOld - phiOld2) collapses to 3*phi.
        if (transient) {

            // A swap, not a copy: the n-1 <- n shift only has to RENAME two buffers,
            // and the n <- current memcpy below overwrites whatever the swap left in
            // the old n-1 slot. Safe because the old levels reach kernels only as
            // explicit by-value arguments -- unlike simple.u/uTemp, whose addresses
            // the linear solvers bake into a captured graph in prepare().
            if (useSecondOrderTime) {
                std::swap(simple.uOld2, simple.uOld);
                std::swap(simple.vOld2, simple.vOld);

                if (solveEnergy) {
                    std::swap(simple.tempOld2, simple.tempOld);
                }

                if (solveConcentration) {
                    std::swap(simple.concOld2, simple.concOld);
                }
            }

            cudaMemcpyAsync(simple.uOld, simple.u, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            cudaMemcpyAsync(simple.vOld, simple.v, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);

            if (solveEnergy) {
                cudaMemcpyAsync(simple.tempOld, simple.temp, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            }

            if (solveConcentration) {
                cudaMemcpyAsync(simple.concOld, simple.conc, N * sizeof(double), cudaMemcpyDeviceToDevice, stream);
            }
        }

        // BDF2 needs two stored levels. On the first step of a run only level n
        // exists, so that step runs backward Euler and the scheme engages from the
        // second step on. Passing null here is what selects BDF1 in the kernel.
        const bool bdf2ThisStep = useSecondOrderTime && tCount > 0;

        const double* uOld2 = bdf2ThisStep ? simple.uOld2 : nullptr;
        const double* vOld2 = bdf2ThisStep ? simple.vOld2 : nullptr;

        // Reset per time step: a transient run drives each step to convergence
        // on its own before advancing time.
        bool converged = false;

        for (int k = 0; k < configSimple.maxIter; k++) {

            // Checked at the top of the iteration so a stop never leaves the
            // fields half-updated: the previous iteration completed the full
            // SIMPLE sequence, so what is on the device is a consistent state.
            if (stopRequested) break;

            clearAllCoefficients << <blocks, threadsPerBlock, 0, stream >> > (
                uCoeff,
                vCoeff,
                ppCoeff,
                tempCoeff,
                concCoeff
            );

            // Gradients feed two consumers now: the non-orthogonal diffusion
            // correction, and the second-order-upwind convection extrapolation. The
            // deferred correction reads the gradient at the UPWIND cell, which may be
            // any neighbor, so the whole field has to be filled before the convection
            // kernel launches -- not just when applyNonOrtho is set.
            if (applyNonOrtho || convectionNeedsGradient) {
                computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.u, simple.u, simple.gradUZ, simple.gradUR, gradientScheme);
                computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.v, simple.v, simple.gradVZ, simple.gradVR, gradientScheme);
            }

            // create coeffs for velocity and pressure correction equations
            addTransportCoefficients << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, uCoeff, bcDevice.u, simple.u, simple.gradUZ, simple.gradUR, applyNonOrtho, f.mu, addConvectionTerm ? 1 : 0, convectionScheme, 1.0);
            addTransportCoefficients << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, vCoeff, bcDevice.v, simple.v, simple.gradVZ, simple.gradVR, applyNonOrtho, f.mu, addConvectionTerm ? 1 : 0, convectionScheme, 1.0);

            // add mu * ur / r^2 contribution
            addRadialMomentumCylindricalSource << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, f.mu);
            
            // Unsteady term, added before under-relaxation and before DU/DV are read
            // off the diagonal: rho*V/dt belongs to the momentum diagonal, so the
            // Rhie-Chow interpolation and the pressure-correction equation both have
            // to see it. Adding it any later would leave pp solving a steady operator.
            if (transient) {
                addTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.uOld, uOld2, f.rho, configSolver.dt);
                addTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.vOld, vOld2, f.rho, configSolver.dt);
            }

            computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.p, simple.p, simple.gradPZ, simple.gradPR, gradientScheme);

            createMomentumPressureRhs << <blocks, threadsPerBlock, 0, stream >> > (
                fvMeshDevice,
                uCoeff,
                vCoeff,
                simple
                );

            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, simple.u, simple.momentumRelaxation);
            underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, vCoeff, simple.v, simple.momentumRelaxation);

            // last touch of AC, so the reciprocals below are the relaxed diagonal --
            // which is what both DU/DV and the momentum smoothers must see
            buildInverseDiagonal << <blocks, threadsPerBlock, 0, stream >> > (uCoeff);
            buildInverseDiagonal << <blocks, threadsPerBlock, 0, stream >> > (vCoeff);

            getCorrectionCoefficients << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, uCoeff, vCoeff, simple.DU, simple.DV);

            // solve velocity
            uSolver.run();
            vSolver.run();

            // solve pressure correction
            createPPCoeff << <blocks, threadsPerBlock, 0, stream >> > (f, fvMeshDevice, ppCoeff, simple, bcDevice.p);

            // pp never reaches underRelaxEquation, so this is its only reciprocal.
            // Outside the corrector loop below because createPPRhs rewrites only b.
            buildInverseDiagonal << <blocks, threadsPerBlock, 0, stream >> > (ppCoeff);

            // grad(p) for Rhie-Chow is still in gradPZ/gradPR from the momentum
            // step above (nothing overwrote it), so no recompute is needed here.
            computeFaceMassFluxRhieChow << <faceBlocks, faceThreads, 0, stream >> > (f, fvMeshDevice, simple, bcDevice);

            // ---- pressure correction with deferred non-orthogonal correctors ----
            // gradPZ/gradPR are reused below to hold grad(p'); they are no longer
            // needed for Rhie-Chow until the next outer iteration. p' starts at 0,
            // so the first corrector pass has a zero cross term (pure orthogonal).
            // corr = 0 is the pure orthogonal pass. Since p' starts at 0, its
            // gradient and therefore the deferred cross term are also zero, so
            // do not launch a gradient kernel just to write zeros. Each later
            // corrector computes grad(p') from the preceding solve and adds the
            // lagged non-orthogonal term to the RHS.
            cudaMemsetAsync(simple.pp, 0, N * sizeof(double), stream);
            CUDA_CHECK(cudaGetLastError());

            for (int corr = 0; corr <= applyNonOrtho; corr++) {
                const int applyCrossTerm = corr > 0 ? 1 : 0;

                if (applyCrossTerm) {
                    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppBC, simple.pp, simple.gradPZ, simple.gradPR, gradientScheme);
                }

                createPPRhs << <blocks, threadsPerBlock, 0, stream >> > (f, fvMeshDevice, ppCoeff, simple, applyCrossTerm);
                if (multigrid) {
                    multigrid->run();
                }
                else {
                    ppSolver->run();
                }
            }

            // final grad(p') (with p' BCs) for the velocity and mass-flux corrections
            computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, ppBC, simple.pp, simple.gradPZ, simple.gradPR, gradientScheme);

            // update field variables
            updateMomentumPressure << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, bcDevice.p);
            updateMassFlux << <faceBlocks, faceThreads, 0, stream >> > (f, fvMeshDevice, simple, bcDevice.p, applyNonOrtho);

            // ======================================================================
            // -----------------ENERGY / CONCENTRATION EQUATIONS---------------------
            // ======================================================================
            for (const ScalarTransport& s : scalarTransport) {

                if (applyNonOrtho || convectionNeedsGradient) {
                    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, s.bc, s.phi, s.gradZ, s.gradR, gradientScheme);
                }

                addTransportCoefficients << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, simple, s.coeff, s.bc, s.phi, s.gradZ, s.gradR, applyNonOrtho, s.diffusivity, addConvectionTerm ? 1 : 0, convectionScheme, 1.0 / f.rho);

                if (transient) {
                    addTransientCoefficient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, s.coeff, s.phiOld, bdf2ThisStep ? s.phiOld2 : nullptr, 1.0, configSolver.dt);
                }

                underRelaxEquation << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, s.coeff, s.phi, s.underRelax ? simple.momentumRelaxation : 1.0);
                buildInverseDiagonal << <blocks, threadsPerBlock, 0, stream >> > (s.coeff);
                s.solver->run();
            }

            // ======================================================================
            // -----------------------RESIDUAL---------------------------------------
            // ======================================================================
            // check for convergence and print residual to console
            if (k % configSimple.checkConv == 0) {

                residualAll << <blocks, threadsPerBlock, 0, stream >> > (
                    false,
                    ResidualPairs{ uCoeff,    simple.u,    uRes.res, uRes.scale, uRes.scaleType },
                    ResidualPairs{ vCoeff,    simple.v,    vRes.res, vRes.scale, vRes.scaleType },
                    ResidualPairs{ tempCoeff, simple.temp, tempRes.res, tempRes.scale, tempRes.scaleType },
                    ResidualPairs{ concCoeff, simple.conc, concRes.res, concRes.scale, concRes.scaleType }
                    );

                if (cudaError_t errResidualAll = cudaGetLastError(); errResidualAll != cudaSuccess) {
                    printf("residualAll launch error: %s\n", cudaGetErrorString(errResidualAll));
                }
                continuityResidual << <blocks, threadsPerBlock, 0, stream >> > (
                    fvMeshDevice,
                    simple,
                    continuityRes.res
                    );

                if (cudaError_t errContinuity = cudaGetLastError(); errContinuity != cudaSuccess) {
                    printf("continuityResidual launch error: %s\n", cudaGetErrorString(errContinuity));
                }

                residualAllHost(cfg, N, transient ? k : currentIteration, mem, stream, tmpA, tmpB);
                residualPlot->add(currentIteration, cfg);
                printResidualConsole(currentIteration, cfg, console);

                CUDA_CHECK(cudaGetLastError());

                converged = residualsConverged(cfg);
            }
            currentIteration++;

            // Exit once the tolerances are met. Tested after the increment so the
            // reported iteration count matches the residual line just printed.
            if (converged) {
                std::ostringstream msg;
                msg << "Converged: all residuals below tolerance at iteration " << currentIteration << "\n";
                console->addLine(msg.str());
                break;
            }
        }

        // Capture on the keyframe interval, and always on the last step so the
        // animation ends on the same state the steady results show. A stopped
        // run counts as ending here, so capture that step too rather than
        // dropping the interval the user actually watched.
        const bool lastStep = (tCount == numSteps - 1) || stopRequested;

        if (transient && (tCount % configSolver.saveKeyFrameIter == 0 || lastStep)) {
            CUDA_CHECK(cudaStreamSynchronize(stream));
            captureTimeFrame(timeOffset + (double)(tCount + 1) * configSolver.dt, N);
        }

        if (stopRequested) break;
    }

    // Fall through to the copy-back below rather than returning: the fields are
    // at a consistent iteration, so the partial solution is still worth showing
    // and Continue Solver can resume from currentIteration.
    if (stopRequested && console) {
        std::ostringstream stopMsg;
        stopMsg << "Solver stopped by user at iteration " << currentIteration << "\n";
        console->addLine(stopMsg.str());
    }

    if (transient && console) {
        std::ostringstream frameMsg;
        frameMsg << "Transient: " << (useSecondOrderTime ? "second" : "first")
                 << " order in time, captured " << timeFrames.size()
                 << " animation frame(s) over " << numSteps << " time step(s).\n";
        console->addLine(frameMsg.str());
    }

    // end timer and print to console
    timer.endTimer(stream);
    float ms = timer.getElapsedTime();

    cudaProfilerStop();

    console->addCompletionTime("Solver", ms);

    // copy all necessary variables back to host

    // Pressure gradient for inspector probing, computed two ways from the final
    // pressure field. gradPZ/gradPR are free to reuse here (they held grad(p')
    // during the solve and are no longer needed).
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.u, simple.u, simple.gradUZ, simple.gradUR, gradientScheme);
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.v, simple.v, simple.gradVZ, simple.gradVR, gradientScheme);
    computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, bcDevice.p, simple.p, simple.gradPZ, simple.gradPR, gradientScheme);

    for (const ScalarTransport& s : scalarTransport) {
        computeGradient << <blocks, threadsPerBlock, 0, stream >> > (fvMeshDevice, s.bc, s.phi, s.gradZ, s.gradR, gradientScheme);
    }

    CUDA_CHECK(cudaStreamSynchronize(stream));
    createSolutions(N);

    // Reactive-wall diagnostic: reports whether the wall is reaction- or
    // mass-transfer-limited and what fraction of the inlet supply it consumes.
    if (solveConcentration) {
        double* diag = deviceAlloc<double>(10);
        CUDA_CHECK(cudaMemsetAsync(diag, 0, 10 * sizeof(double), stream));

        wallConsumptionDiagnostic << <faceBlocks, faceThreads, 0, stream >> > (
            fvMeshDevice, simple, bcDevice.conc, simple.conc, f.D, diag);

        double hd[10] = {};
        CUDA_CHECK(cudaMemcpyAsync(hd, diag, 10 * sizeof(double), cudaMemcpyDeviceToHost, stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));
        freeDev(diag);

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

    isReady = true;

    // free memory. bcDevice and ppBC are rebuilt from the boundary groups on every
    // run, so they are released here rather than carried over -- ppBC only owns its
    // own valueByGroup, the rest of it aliases bcDevice.p.
    freeAllDev(tmpA, tmpB);
    freeDev(ppBC.valueByGroup);
    freeBoundarySolverDevice(bcDevice);
}
