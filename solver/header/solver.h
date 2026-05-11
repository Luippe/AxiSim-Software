#pragma once
#include "jacobi_pcg.cuh"
#include <string>
#include "solver_struct.h"
#include "residual_plot.h"
#include <thread>

class Console;
class SceneView;

class Solver {
public:

	Solver(SceneView& scene, Config& config);
	
	const char* fieldType[5] = { "Axial Velocity", "Radial Velocity", "Pressure", "Concentration", "Temperature"};
	const char* residualType[3] = { "Raw Residual", "RMS", "Custom Residual"};
	const char* residualNormType[3] = { "L1 Norm", "L2 Norm", "Linf Norm" };
	const char* residualScalingType[3] = { "None", "N", "sqrt(N)" };
	const char* linearSolverType[2] = { "Jacobi", "Red Black Gauss Seidel"};
	const char* velocitySolverType[1] = { "SIMPLE" };
	const char* bcTypeNames[2] = {"Constant", "Flux"};
	const char* bcInletTypeNames[3]{ "Constant", "Flux", "Fully Developed" };
	const char* convectionDiscretizationType[2] = { "First Order Upwind", "Second Order Central" };

	FieldType currentField = FIELD_AXIAL_VELOCITY;
	LinearSolverType currentLinearSolver = LINEAR_JACOBI;
	VelocitySolverType currentVelocitySolver = SOLVER_SIMPLE;
	ResidualType currentResidual = RESIDUAL_RAW;
	ResidualNormType currentResidualNorm = RESIDUAL_LINF;
	ResidualScalingType currentResidualScaling = RESIDUAL_SCALING_NONE;
	ConvectionScheme convectionScheme = FIRST_ORDER_UPWIND;

	bool addConvectionTerm = false;
	bool transient = true;
	int saveKeyFrameIter = 2;

	double dt = 0.1;
	double tEnd = 2.0;

	void run();
	void runSimple();
	void runBiCGStab();
	void shutdown();

	SceneView& scene;
	Config& config;
	GridConfig& g;
	FluidPropertyConfig& f;
	IterationConfig& itr;
	ResidualPlot residualPlot;

	// config for boundary conditions
	BoundaryConditionConfig uBC;
	BoundaryConditionConfig vBC;
	BoundaryConditionConfig pBC;
	BoundaryConditionConfig concBC;

	// velocity fields
	SolutionField uSol;
	SolutionField vSol;
	SolutionField pSol;
	SolutionField concSol;

	std::thread solverThread;
	cudaStream_t stream = nullptr;
	Console* console = nullptr;
	bool solverRunning = false;
	bool continueSolver = false;
	bool solutionReady = false;

	// iteration configs
	ConfigSimple configSimple;

	// variable configs
	VariablesSimple simple;

	LinearSolverConfig linearSolverConfig;

	// set all variables to default values
	void setDefault();

private:

	int currentIteration = 0;
	// check if the solver can run
	bool runCheck();
	Coefficients uCoeff, vCoeff, ppCoeff, contCoeff;
	MemoryConfig mem;

};