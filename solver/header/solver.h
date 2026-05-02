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
	const char* linearSolverType[1] = { "Jacobi" };
	const char* velocitySolverType[1] = { "SIMPLE" };
	const char* bcTypeNames[2] = {"Dirichlet","Neumann"};


	FieldType currentField = FIELD_AXIAL_VELOCITY;
	LinearSolverType currentLinearSolver = LINEAR_JACOBI;
	VelocitySolverType currentVelocitySolver = SOLVER_SIMPLE;
	ResidualType currentResidual = RESIDUAL_RAW;
	ResidualNormType currentResidualNorm = RESIDUAL_LINF;
	ResidualScalingType currentResidualScaling = RESIDUAL_SCALING_NONE;

	Solution sol;
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
	Console* console = nullptr;
	bool running = false;
	bool continueSolver = false;

	// iteration configs
	ConfigSimple configSimple;

	// variable configs
	VariablesSimple simple;

private:

	// check if the solver can run
	bool runCheck();

	// set all variables to default values
	void setDefault();

	// calculate the residual
	void solveResidual();
	VariablesSimple simpleCoeff;
	MemoryConfig mem;

};