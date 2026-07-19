#pragma once
#include "setting.cuh"
#include "gpu_utils.h"
#include "unit_manager.h"


// holds solvers for solving system of linear equations

struct SolverFieldOption {
	bool solveEnergy = false;
	bool solveConcentration = false;
};

enum ResidualType {
	RESIDUAL_SCALED = 0,
	RESIDUAL_RAW = 1,
	RESIDUAL_RMS = 2
};

enum ResidualNormType {
	RESIDUAL_L1 = 0,
	RESIDUAL_L2 = 1,
	RESIDUAL_LINF = 2
};

enum ResidualScalingType {
	RESIDUAL_SCALING_NONE = 0,
	RESIDUAL_SCALING_N = 1,
	RESIDUAL_SCALING_SQRT_N = 2,
	RESIDUAL_SCALING_DIAGONAL = 3,
	RESIDUAL_SCALING_CONTINUITY = 4
};

enum LinearSolverType {
    LINEAR_JACOBI         = 0,
	LINEAR_GS_RB		  = 1,
    LINEAR_BICGSTAB       = 2,
    LINEAR_GMRES          = 3
};

enum VelocitySolverType {
    SOLVER_SIMPLE         = 0,
    SOLVER_SIMPLER        = 1
};

enum class CellStoreType {
	CENTER,
	AXIAL,
	RADIAL
};

enum ConvectionScheme {
	CONV_UPWIND,
	CONV_CENTRAL,
	CONV_SECOND_ORDER_UPWIND,
	CONV_QUICK
};

// Cell gradient scheme used for the pressure / pressure-correction gradients.
enum GradientScheme {
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

// coefficients required for each field variable
struct Coefficients {
	double* AE = nullptr;
	double* AW = nullptr;
	double* AN = nullptr;
	double* AS = nullptr;
	double* AF = nullptr;
	double* AC = nullptr;
	double* b = nullptr;

	int* faceStart = nullptr;
	int* faceNeighbor = nullptr;

	int nr = 0;
	int nz = 0;
	int N = 0;
	int nFaceRefs = 0;
	int useFaceCoeffs = 0;


	void free() {
		freeAllDev(AE, AW, AN, AS, AF, AC, b);
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

struct ConfigSimple {
	int maxIter = 50;
	int checkConv = 1;
	double momTol = 1e-8;
	double ppTol = 1e-5;

	// Number of deferred non-orthogonal corrector passes for the pressure
	// correction. 0 = orthogonal only (stable default). The deferred cross term
	// can destabilize on skewed/axis cells, so it is opt-in; raise to 1-2 once
	// a limiter is in place.
	int nNonOrthCorrectors = 0;
};

struct ConfigMultigrid {

	// Cycles per pressure-correction solve. This is an INNER solve inside SIMPLE's
	// outer loop, so it does not need to converge -- a few cycles is the usual
	// trade. Was 50 while the field was unread; wiring that value up unchanged
	// would have made every pp solve ~50x its previous cost.
	int maxIter = 3;

	// Diagnostic: measure the level-0 residual norm either side of the cycle so the
	// pp solve can be judged on its own rather than through SIMPLE's outer
	// convergence. Costs two extra residual evaluations AND two blocking host syncs
	// per solve, so it is off by default -- this sits inside SIMPLE's inner loop.
	bool logConvergence = false;

};

struct ConfigSolver {

	LinearSolverType type = LINEAR_JACOBI;
	int maxIter = 20;

	bool addConvectionTerm = true;
	bool transient = false;

	double dt = 0.1;
	double tEnd = 2.0;
};

struct ConfigResidual {

	ResidualType type				= RESIDUAL_SCALED;

	ResidualNormType normType		= RESIDUAL_LINF;
	ResidualScalingType scaleType	= RESIDUAL_SCALING_DIAGONAL;

	bool enabled = false;

	// residuals
	double* res = nullptr;
	double* scale = nullptr;
	double resVal = 0.0;
	double scaleVal = 0.0;

	// tolerance
	double tol = 0.001;

	void free() {
		freeAllDev(res, scale);
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
	double correctionRelaxation = 1.0;
	double pressureRelaxation = 0.3;

	double* mDot = nullptr;

	void free() {
		freeAllDev(DU, DV, p, pp, u, v, temp, conc, uTemp, vTemp, ppTemp, tempTemp, concTemp, uOld, vOld, tempOld, concOld, gradPZ, gradPR, gradUZ, gradUR, gradVZ, gradVR, gradTZ, gradTR, gradCZ, gradCR, mDot);
	}
};


struct VariablesBiCGStab {

	double conc;

	double* ACC = nullptr;
	double* ACnew = nullptr;
	double* AKE = nullptr;
	double* AKW = nullptr;
	double* AKN = nullptr;
	double* AKS = nullptr;
	double* foxy = nullptr;
	double* foxynew = nullptr;

	double* oxy = nullptr;
	double* beta = nullptr;
	double* alpha = nullptr;
	double* cs = nullptr;
	double* cw = nullptr;
	double* cp = nullptr;
	double* res = nullptr;
	double* res_t = nullptr;
	double* jp = nullptr;
	double* jp_t = nullptr;
	double* js = nullptr;
	double* js_t = nullptr;
	double* jrho = nullptr;
	double* jv = nullptr;
	double* jt = nullptr;

	double* snorm = nullptr;
	double* resnorm = nullptr;
	double* jw = nullptr;
	double* alpha_den = nullptr;
	double* w_num = nullptr;
	double* w_den = nullptr;
	double* OCR_num = nullptr;

	double* jw_num_val = nullptr;
	double* jw_den_val = nullptr;
	double* jalpha_val = nullptr;

	double* jalpha_den_val = nullptr;
	double* jbeta_val = nullptr;
	double* jrho_val_prev = nullptr;
	double* jrho_val = nullptr;
	double* jw_val = nullptr;
	double* snorm_val = nullptr;
	double* resnorm_val = nullptr;
	double* OCR_num_val = nullptr;

	double* tmpA = nullptr;
	double* tmpB = nullptr;

	void free() {
		freeAllDev(ACC, ACnew, AKE, AKW, AKN, AKS);
		freeAllDev(foxy, foxynew, oxy);
		freeAllDev(beta, alpha, cs, cw, cp);
		freeAllDev(res, res_t, jp, jp_t, js, js_t, jrho, jv, jt);
		freeAllDev(snorm, resnorm, jw, alpha_den, w_num, w_den, OCR_num);
		freeAllDev(
			jw_num_val,
			jw_den_val,
			jalpha_val,
			jalpha_den_val,
			jbeta_val,
			jrho_val_prev,
			jrho_val,
			jw_val,
			snorm_val,
			resnorm_val,
			OCR_num_val
		);
		freeAllDev(tmpA, tmpB);
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

	uint8_t* active = nullptr;
	uint8_t* solid = nullptr;

	// CSR-style face connectivity
	int* faceStart = nullptr;
	int* faceIDs = nullptr;
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
};

struct FVMeshDevice {
	int nr = 0;
	int nz = 0;

	FVCellDevice cells;
	FVFaceDevice faces;
};

// struct used to store mesh data, which will be sent to device
struct FVMeshHostPacked {
	int nr = 0;
	int nz = 0;

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

	std::vector<double> cellVolume;

	std::vector<uint8_t> cellActive;
	std::vector<uint8_t> cellSolid;

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


