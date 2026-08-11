#pragma once
#include "setting.cuh"
#include "gpu_utils.h"
#include "unit_manager.h"


// holds solvers for solving system of linear equations

struct SolverFieldOption {
	bool solveEnergy = false;
	bool solveConcentration = false;
};

// Every enum below is a scoped, uint8_t-backed enum: they are stored in config
// structs that go to disk, so a byte each keeps those structs small, and scoping
// keeps names like NONE out of the global namespace. They are NOT interchangeable
// with int -- write (int)x / (Enum)i explicitly, and never reinterpret one as an
// int& (the GUI combos have an enum overload for exactly that reason).
enum class ResidualType : uint8_t {
	RESIDUAL_SCALED = 0,
	RESIDUAL_RAW = 1,
	RESIDUAL_RMS = 2
};

enum class ResidualNormType : uint8_t {
	RESIDUAL_L1 = 0,
	RESIDUAL_L2 = 1,
	RESIDUAL_LINF = 2
};

enum class ResidualScalingType : uint8_t {
	RESIDUAL_SCALING_NONE = 0,
	RESIDUAL_SCALING_N = 1,
	RESIDUAL_SCALING_SQRT_N = 2,
	RESIDUAL_SCALING_DIAGONAL = 3,
	RESIDUAL_SCALING_CONTINUITY = 4
};

enum class LinearSolverType : uint8_t {
    LINEAR_JACOBI         = 0,
	LINEAR_GS_RB		  = 1
};

enum class VelocitySolverType : uint8_t {
    SOLVER_SIMPLE         = 0
};

enum class ConvectionScheme : uint8_t {
	CONV_UPWIND,
	CONV_CENTRAL,
	CONV_SECOND_ORDER_UPWIND,
	CONV_QUICK
};

// Cell gradient scheme, used for EVERY computeGradient call -- u, v, p, the
// pressure correction, temperature and concentration alike. The GUI labels it
// "Pressure Gradient" because that is where it is felt most, but it is not scoped
// to pressure, and the OpenFOAM export relies on that: it maps straight onto
// `gradSchemes default`, which is likewise global.
enum class GradientScheme : uint8_t {
	GRAD_GREEN_GAUSS = 0,
	GRAD_LSQ = 1
};

struct CudaTimer {

	cudaEvent_t startTime, stopTime;
	float ms = 0.0f;
	
	CudaTimer() {
		cudaEventCreate(&startTime);
		cudaEventCreate(&stopTime);
	}

	~CudaTimer() {
		destroyEvent();
	}

	void startTimer(cudaStream_t& stream) {
		cudaStreamSynchronize(stream);
		cudaEventRecord(startTime, stream);
	}

	void endTimer(cudaStream_t& stream) {
		cudaEventRecord(stopTime, stream);
		cudaEventSynchronize(stopTime);
	}

	float getElapsedTime() {
		cudaEventElapsedTime(&ms, startTime, stopTime);
		return ms;
	}

	void destroyEvent() {
		cudaEventDestroy(startTime);
		cudaEventDestroy(stopTime);
	}
};

// faceStart/faceNeighbor come from the same walk over cell.faceIDs that builds
// mesh.cells.faceStart/faceIDs, so slot k IS mesh.cells.faceIDs[k]: an assembly
// kernel already looping over a cell's faces writes AF[k] with no lookup.
struct Coefficients {
	double* AF = nullptr;
	double* AC = nullptr;
	double* b = nullptr;
	double* invAC = nullptr;

	int* faceStart = nullptr;
	int* faceNeighbor = nullptr;

	int N = 0;
	int nFaceRefs = 0;


	void free() {
		freeAllDev(AF, AC, b, invAC);
		freeAllDev(faceStart, faceNeighbor);
	}
};

// Multicolor ordering of the mesh cell graph, so Gauss-Seidel can run on the
// face path (multiblock / unstructured).
//
// Red-black colors by (i+j)%2, which needs a real nr x nz grid -- on the face path
// nr = nz = 0, so that kernel divides by zero. A general cell graph is not even
// guaranteed to be 2-colorable (three triangles round a vertex is an odd cycle),
// so the colors come from greedy graph coloring instead. On a single-block
// structured mesh greedy reproduces the checkerboard exactly: 2 colors.
//
// Cells sharing a color share no face, so one color can be swept updating x IN
// PLACE with no read/write conflict -- which is what makes it Gauss-Seidel rather
// than Jacobi, and why no xTemp buffer is needed.
struct MeshColoring {

	int nCells = 0;
	int nColors = 0;

	// size nColors + 1; color c owns cellOrder[colorStart[c] .. colorStart[c + 1]).
	// Stays on the host because the per-color launch geometry is computed there.
	std::vector<int> colorStart;

	// device, size nCells: cell ids grouped by color
	int* d_cellOrder = nullptr;

	bool valid() const { return nColors > 0 && d_cellOrder != nullptr; }

	void free() {
		freeAllDev(d_cellOrder);
		colorStart.clear();
		nColors = 0;
		nCells = 0;
	}
};

// Convergence control for the SIMPLE outer loop. Which numerics it runs is
// ConfigSolver's business -- useNonOrthCorrector lives there now.
struct ConfigSimple {
	int maxIter = 50;
	int checkConv = 1;
	double momTol = 1e-8;
	double ppTol = 1e-5;
};

static_assert(sizeof(ConfigSimple) == 24, "ConfigSimple size changed -- see file_manager solverFileVersion");

struct ConfigMultigrid {

	// Cycles per pressure-correction solve. This is an INNER solve inside SIMPLE's
	// outer loop, so it does not need to converge -- a few cycles is the usual
	// trade. Was 50 while the field was unread; wiring that value up unchanged
	// would have made every pp solve ~50x its previous cost.
	int maxIter = 3;

	// Damping on the weighted-Jacobi smoother. Under-relaxation is what makes
	// Jacobi smooth the high-frequency error the coarse grid cannot see; at 1.0 it
	// stops being a smoother and the V-cycle stalls.
	double weight = 0.6;

	// Sweeps used as the coarsest-level solve. There is nothing below that level to
	// correct from, so the smoother has to stand in for a direct solve, which is why
	// this is an order of magnitude above the pre/post count.
	int linearSweep = 30;

	// Sweeps before restriction and again after prolongation, on every level that
	// has a coarser one beneath it.
	int linearPreSweep = 1;
	int linearPostSweep = 1;

};

// Time discretization for a transient run.
enum class TimeScheme : uint8_t {
	TIME_FIRST_ORDER  = 0,		// backward Euler
	TIME_SECOND_ORDER = 1		// BDF2
};

// The whole run configuration: which algorithms to use, and the transient
// settings. This is the one struct the solver, the Solver tab and the .axislv
// payload all read -- Solver used to carry its own copies of the velocity solver,
// the two schemes, the multigrid flag and saveKeyFrameIter, which meant the GUI
// and the save file could disagree about which one the solve had actually used.
struct ConfigSolver {

	VelocitySolverType velocitySolver = VelocitySolverType::SOLVER_SIMPLE;
	ConvectionScheme convectionScheme = ConvectionScheme::CONV_UPWIND;
	GradientScheme gradientScheme = GradientScheme::GRAD_LSQ;
	LinearSolverType type = LinearSolverType::LINEAR_JACOBI;
	TimeScheme timeScheme = TimeScheme::TIME_FIRST_ORDER;


	int linearMaxIter = 20;
	bool useMultigrid = true;

	bool addConvectionTerm = true;
	bool transient = false;

	// Deferred non-orthogonal corrector for the pressure correction. Off =
	// orthogonal only (stable default); on = one extra corrector pass. The
	// deferred cross term can destabilize on skewed/axis cells, so it is opt-in,
	// and it is meaningless on a structured mesh (orthogonal by construction).
	//
	// Sits with the other three bools deliberately: it lands in the padding that
	// already followed `transient`, so moving it here out of ConfigSimple left
	// sizeof(ConfigSolver) unchanged.
	bool useNonOrthCorrector = false;

	double dt = 0.1;
	double tEnd = 2.0;
	int saveKeyFrameIter = 2;
};

// Solver files store this struct as a raw byte blob, so its size and layout are
// part of the .axislv / .axi format. Changing either -- adding a field, or
// widening one of the enums above -- means bumping solverFileVersion in
// file_manager.cpp and teaching readSolverPayload to map the old bytes across
// (see LegacyConfigSolverV4 there). Do not just update the number here.
static_assert(sizeof(ConfigSolver) == 40, "ConfigSolver layout changed -- see file_manager solverFileVersion");

struct ConfigResidual {

	ResidualType type				= ResidualType::RESIDUAL_SCALED;

	ResidualNormType normType		= ResidualNormType::RESIDUAL_L1;
	ResidualScalingType scaleType	= ResidualScalingType::RESIDUAL_SCALING_DIAGONAL;

	bool enabled = false;

	// residuals
	double* res = nullptr;
	double* scale = nullptr;
	double* resVal = nullptr;
	double* scaleVal = nullptr;

	// tolerance
	double tol = 0.001;

	void free() {
		freeAllDev(res, scale);
		freeAllHost(resVal, scaleVal);
	}

};

struct VariablesSimple {

	double* DU = nullptr;
	double* DV = nullptr;
	double* p = nullptr;
	double* pp = nullptr;
	double* u = nullptr;
	double* v = nullptr;
	double* temp = nullptr;
	double* conc = nullptr;

	double* uTemp = nullptr;
	double* vTemp = nullptr;
	double* ppTemp = nullptr;
	double* tempTemp = nullptr;
	double* concTemp = nullptr;

	double* uOld = nullptr;
	double* vOld = nullptr;
	double* tempOld = nullptr;
	double* concOld = nullptr;

	// Time level n-1, needed only by BDF2 (the first-order scheme reads uOld alone).
	// Allocated unconditionally so switching the scheme between runs does not
	// require a reallocation.
	double* uOld2 = nullptr;
	double* vOld2 = nullptr;
	double* tempOld2 = nullptr;
	double* concOld2 = nullptr;

	double* gradPZ = nullptr;
	double* gradPR = nullptr;

	// Cell-centered velocity gradients, recomputed once per SIMPLE iteration
	// with the user-selected scheme (Green-Gauss or LSQ) and consumed by the
	// momentum non-orthogonal (cross-diffusion) correction.
	double* gradUZ = nullptr;
	double* gradUR = nullptr;
	double* gradVZ = nullptr;
	double* gradVR = nullptr;
	double* gradTZ = nullptr;
	double* gradTR = nullptr;
	double* gradCZ = nullptr;
	double* gradCR = nullptr;

	// SIMPLE requires under-relaxation to be stable. 1.0/1.0 (no relaxation)
	// diverges; the standard pairing is momentum ~0.7 with pressure ~0.3.
	double momentumRelaxation = 0.7;
	double pressureRelaxation = 0.3;

	double* mDot = nullptr;

	void free() {
		freeAllDev(DU, DV, p, pp, u, v, temp, conc, uTemp, vTemp, ppTemp, tempTemp, concTemp, uOld, vOld, tempOld, concOld, gradPZ, gradPR, gradUZ, gradUR, gradVZ, gradVR, gradTZ, gradTR, gradCZ, gradCR, mDot);
		freeAllDev(uOld2, vOld2, tempOld2, concOld2);
	}
};


// holds all the config structs
struct Config {

	FluidPropertyConfig f;
	GridConfig g;
	VariableUnits varUnits;

};

struct FVCellDevice {
	int nCells = 0;

	double* centerZ = nullptr;
	double* centerR = nullptr;

	double* volume = nullptr;

	int* faceStart = nullptr;
	int* faceIDs = nullptr;

	// precomputed values
	// 1 / A2D, the planar (pre-revolve) cell area: 2*pi*centerR / volume.
	double* invA2D = nullptr;

	// Inverted least-squares normal matrix, so the gradient is a plain
	// mat-vec against the rhs: [ZZ ZR; ZR RR] * (bz, br). Zero on a cell whose
	// normal matrix is singular, which reproduces the old bail-out gradient of 0.
	double* lsqInvZZ = nullptr;
	double* lsqInvZR = nullptr;
	double* lsqInvRR = nullptr;
};

struct FVFaceDevice {
	int nFaces = 0;

	int* owner = nullptr;
	int* neighbor = nullptr;

	double* normalZ = nullptr;
	double* normalR = nullptr;

	double* centerZ = nullptr;
	double* centerR = nullptr;

	double* area = nullptr;

	int* boundaryGroupID = nullptr;

	// Computed wall concentration per face (solver output; 0 on interior faces).
	double* cw = nullptr;

	// Wall oxygen-consumption rate per face (solver output; 0 on interior faces).
	double* ocrWall = nullptr;

	// precomputed values
	double* invCellToCell = nullptr;
	double* wP = nullptr;
	double* dPB = nullptr;

	// Least-squares gradient weight w*(dz, dr), oriented owner -> neighbor
	// (owner -> face center on a boundary face).
	double* lsqWZ = nullptr;
	double* lsqWR = nullptr;

	double* length2D = nullptr;

};

// No nr/nz: the device path is purely face-based. The logical grid still exists
// on FVMesh for the raster-based results views, but no kernel ever needed it.
struct FVMeshDevice {
	FVCellDevice cells;
	FVFaceDevice faces;
};

// struct used to store mesh data, which will be sent to device
struct FVMeshHostPacked {
	int nCells = 0;
	int nFaces = 0;

	std::vector<int> faceOwner;
	std::vector<int> faceNeighbor;

	std::vector<double> faceNormalZ;
	std::vector<double> faceNormalR;

	std::vector<double> faceCenterZ;
	std::vector<double> faceCenterR;

	std::vector<double> faceArea;

	std::vector<int> faceBoundaryGroupID;

	std::vector<double> cellCenterZ;
	std::vector<double> cellCenterR;

	// r-z cross-section, before the axisymmetric revolve. Carried alongside the
	// volume rather than divided back out of it: the revolve zeroes inactive cells
	// and degrades near the axis, neither of which the cross-section does.
	std::vector<double> cellArea2D;

	std::vector<double> cellVolume;

	std::vector<int> cellFaceStart;
	std::vector<int> cellFaceIDs;
};

struct BoundaryFieldDevice {
	uint8_t* typeByGroup = nullptr;
	uint8_t* boundaryTypeByGroup = nullptr;
	double* lengthByGroup = nullptr;
	double* valueByGroup = nullptr;
	// Kinetics (Michaelis-Menten / Hill) parameters per group.
	double* vmaxByGroup = nullptr;
	double* kmByGroup = nullptr;
	double* nByGroup = nullptr;
	double* mByGroup = nullptr;
	// Substrate-inhibition parameters per group (active only when the BC enabled
	// inhibition; otherwise V2 = 0 leaves the inhibition factor inert).
	double* k2ByGroup = nullptr;
	double* v2ByGroup = nullptr;
	// Precomputed Km^n and K2^m per group (config-only, hoisted out of the
	// per-cell kinetics evaluation to avoid recomputing pow() every call).
	double* kmNByGroup = nullptr;
	double* k2MByGroup = nullptr;
	// Total wall-layer resistance per group (sum of each layer's R = d/k), used
	// as the series resistance in the wall flux / wall-concentration balance.
	double* RtotByGroup = nullptr;

	int nGroups = 0;
};

struct BoundarySolverDevice {
	BoundaryFieldDevice u;
	BoundaryFieldDevice v;
	BoundaryFieldDevice p;
	BoundaryFieldDevice temp;
	BoundaryFieldDevice conc;
};

struct BoundaryFieldHost {
	std::vector<uint8_t> typeByGroup;
	std::vector<uint8_t> boundaryTypeByGroup;
	std::vector<double> valueByGroup;
	std::vector<double> lengthByGroup;
	std::vector<double> vmaxByGroup;
	std::vector<double> kmByGroup;
	std::vector<double> nByGroup;
	std::vector<double> mByGroup;
	std::vector<double> k2ByGroup;
	std::vector<double> v2ByGroup;
	std::vector<double> RtotByGroup;
};


// Element transform applied on load by the block reduction, before anything is
// combined. Applies to the FIRST pass only -- see reduction() in solver_util.cuh.
enum class ReductionMethod {
	NONE,
	ABSOLUTE,
	SQUARED
};

// How the block reduction combines the transformed values. Unlike the transform
// above, this holds for EVERY pass of the tree: a max of partial maxima is still
// the max, but a max of partial sums is meaningless. ABSOLUTE + MAX is the L-Inf
// norm, which is why it no longer needs a host-side pass.
enum class ReductionOp {
	SUM,
	MAX
};
