#pragma once
#include "jacobi_pcg.cuh"
#include <string>
#include "solver_struct.h"
#include "boundary_struct.h"
#include "residual_plot.h"
#include <thread>

class Console;
class SceneView;
class Mesh;
struct VariableUnits;

class Solver {
public:

	Solver(Config& config);
	
	const char* boundaryType[4] = { "Wall", "Velocity Inlet", "Pressure Outlet", "Symmetry"};
	const char* residualType[3] = { "Raw Residual", "RMS", "Custom Residual"};
	const char* residualNormType[3] = { "L1 Norm", "L2 Norm", "Linf Norm" };
	const char* residualScalingType[3] = { "None", "N", "sqrt(N)" };
	const char* linearSolverType[2] = { "Jacobi", "Red Black Gauss Seidel"};
	const char* velocitySolverType[1] = { "SIMPLE" };
	const char* bcTypeNames[2] = {"Constant", "Flux"};
	const char* bcInletTypeNames[3]{ "Constant", "Flux", "Fully Developed" };
	const char* convectionDiscretizationType[3] = { "First Order Upwind", "Central Difference", "Second Order Upwind"};
	const char* residualPlotType[6] = { "U", "V", "P", "Continuity", "Temperature", "Concentration" };
	std::vector<std::string> fieldType = {
		"Axial Velocity",
		"Radial Velocity",
		"Pressure"
	};

	VelocitySolverType currentVelocitySolver = SOLVER_SIMPLE;
	ResidualType currentResidual = RESIDUAL_RAW;
	ResidualNormType currentResidualNorm = RESIDUAL_LINF;
	ResidualScalingType currentResidualScaling = RESIDUAL_SCALING_NONE;
	ConvectionScheme convectionScheme = CONV_UPWIND;

	bool addConvectionTerm = false;
	bool transient = false;
	int saveKeyFrameIter = 2;

	std::thread solverThread;
	cudaStream_t stream = nullptr;

	bool solverRunning = false;
	bool continueSolver = false;
	bool isReady = false;

	double dt = 0.1;
	double tEnd = 2.0;

	void run(const Mesh& mesh);
	void runSimple(const Mesh& mesh);
	void runBiCGStab();
	void shutdown();

	Config& config;
	GridConfig& g;
	FluidPropertyConfig& f;
	IterationConfig& itr;
	VariableUnits& varUnits;

	std::vector<BoundaryGroupBC> boundaryGroupBCs;

	// struct to determine which fields to solve for
	SolverFieldOption fieldOption;

	// gui classes
	Console* console = nullptr;
	ResidualPlot* residualPlot = nullptr;

	// velocity fields
	SolutionField uSol;
	SolutionField vSol;
	SolutionField pSol;
	SolutionField concSol;
	SolutionField tempSol;

	// fvMesh
	FVMesh fvMesh;

	// which variables will the residual plot show?
	EnabledResiduals enabledResiduals;

	// config for simple algorithm
	ConfigSimple configSimple;

	// variable configs
	VariablesSimple simple;

	LinearSolverConfig linearSolverConfig;

	// set all variables to default values
	void setDefault();

	BoundaryGroupBC* getBoundaryGroupBCFromID(int id);

private:

	// store all the coeffs which will be printed
	std::vector<ResidualPrintItem> residualsToPrint;

	int currentIteration = 0;
	// check if the solver can run
	bool runCheck();

	void createResidualPrintItems();

	void addFieldType();

	Coefficients uCoeff, vCoeff, ppCoeff, massFluxCoeff, tempCoeff;
	MemoryConfig mem;

};