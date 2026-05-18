#pragma once
#include "setting.cuh"
#include "gpu_utils.h"

// holds solvers for solving system of linear equations

enum FieldType {
    FIELD_AXIAL_VELOCITY  = 0,
    FIELD_RADIAL_VELOCITY = 1,
    FIELD_PRESSURE        = 2,
    FIELD_TEMPERATURE     = 3,
    FIELD_CONCENTRATION   = 4
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

enum BCType {
	DIRICHLET,
	NEUMANN,
	FULLY_DEVELOPED
};

enum ConvectionScheme {
	CONV_UPWIND,
	CONV_CENTRAL,
	CONV_SECOND_ORDER_UPWIND,
	CONV_QUICK
};

struct BoundaryCondition {
	BCType type = DIRICHLET;
	double val = 0.0;
};

struct BoundaryConditionConfig {
	BoundaryCondition inlet;
	BoundaryCondition outlet;
	BoundaryCondition outer;
	BoundaryCondition centerline;
};

struct SolutionField {
	std::vector<double> field;
	int nr, nz;
	double dr, dz;
	CellStoreType type;
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
	uint8_t* activeCell = nullptr;
	uint8_t* activeBC = nullptr;
	int nr, nz, N;
	double resVal = 0.0;
	CellStoreType storeType;

	void free() {
		freeAllDev(AE, AW, AN, AS, AC, b, res, activeCell, activeBC, initRes);
	}
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

	double* uTemp = nullptr;
	double* vTemp = nullptr;
	double* ppTemp = nullptr;

	double* uOld = nullptr;
	double* vOld = nullptr;

	double momentumRelaxation = 0.7;
	double correctionRelaxation = 0.2;
	double pressureRelaxation = 0.3;

	void free() {
		freeAllDev(DU, DV, p, pp, u, v, uTemp, vTemp, ppTemp, uOld, vOld);
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