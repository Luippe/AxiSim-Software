#pragma once
#include "setting.cuh"
#include "gpu_utils.h"
#include "unit_manager.h"


// holds solvers for solving system of linear equations

struct SolverFieldOption {
	bool solveEnergy = true;
	bool solveConcentration = true;
};

enum ResidualType {
	RESIDUAL_RAW = 0,
	RESIDUAL_RMS = 1
};

enum ResidualNormType {
	RESIDUAL_L1 = 0,
	RESIDUAL_L2 = 1,
	RESIDUAL_LINF = 2
};

enum ResidualScalingType {
	RESIDUAL_SCALING_NONE = 0,
	RESIDUAL_SCALING_N = 1,
	RESIDUAL_SCALING_SQRT_N = 2
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

struct EnabledResiduals {
	bool plotU = true;
	bool plotV = true;
	bool plotP = false;
	bool plotCont = true;
	bool plotTemp = false;
	bool plotConc = false;
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
	double* AC = nullptr;
	double* b = nullptr;
	double* res = nullptr;
	double* initRes = nullptr;

	int nr, nz, N;
	double resVal = 0.0;

	void free() {
		freeAllDev(AE, AW, AN, AS, AC, b, res, initRes);
	}
};

struct ResidualPrintItem {
	const char* name;
	bool enabled;
	Coefficients* coeff;
};

struct LinearSolverConfig {
	LinearSolverType type = LINEAR_JACOBI;
	int maxIter = 20;
};

struct IterationConfig {

	double outer_tol = 1e-8;
	double inner_tol = 1e-10;
	double cs_tol = 1e-10;
	int outer_iter = 250;
	int inner_iter = 250;
	int cs_iter = 150;
	int check_iter = 15;

};

struct ConfigSimple {
	int maxIter = 50;
	int checkConv = 1;
	double momTol = 1e-8;
	double ppTol = 1e-5;
};


struct ConfigSolver {
	GridConfig g;
	FluidPropertyConfig f;

	bool addConvectionTerm = false;
	bool transient = false;

	double dt = 0.1;
};


struct ConfigResidual {
	ResidualType residualType;
	ResidualNormType residualNormType;
	ResidualScalingType residualScaleType;
};


struct VariablesSimple {

	double* DU = nullptr;
	double* DV = nullptr;
	double* p = nullptr;
	double* pp = nullptr;
	double* u = nullptr;
	double* v = nullptr;
	double* temp = nullptr;

	double* uTemp = nullptr;
	double* vTemp = nullptr;
	double* ppTemp = nullptr;
	double* tempTemp = nullptr;

	double* uOld = nullptr;
	double* vOld = nullptr;
	double* tempOld = nullptr;


	double* gradPZ = nullptr;
	double* gradPR = nullptr;

	double momentumRelaxation = 0.7;
	double correctionRelaxation = 1.7;
	double pressureRelaxation = 0.3;

	double* mDot = nullptr;

	void free() {
		freeAllDev(DU, DV, p, pp, u, v, uTemp, vTemp, ppTemp, uOld, vOld, gradPZ, gradPR, mDot);
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
	IterationConfig itr;
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
	double* lengthByGroup = nullptr;
	double* valueByGroup = nullptr;
	int nGroups = 0;
};

struct BoundarySolverDevice {
	BoundaryFieldDevice u;
	BoundaryFieldDevice v;
	BoundaryFieldDevice p;
	BoundaryFieldDevice temp;
	BoundaryFieldDevice concentration;
};

struct BoundaryFieldHost {
	std::vector<uint8_t> typeByGroup;
	std::vector<double> valueByGroup;
	std::vector<double> lengthByGroup;
};