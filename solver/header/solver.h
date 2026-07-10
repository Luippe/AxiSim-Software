#pragma once
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jacobi_pcg.cuh"
#include "residual_plot.h"

#include "solver_struct.h"
#include "boundary_struct.h"

class Console;
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
	const char* gradientSchemeType[2] = { "Green-Gauss", "Least Squares" };
	const char* residualPlotType[5] = { "U", "V", "Continuity", "Temperature", "Concentration" };
	const char* coefficientNames[6] = { "U", "V", "PP", "Continuity", "Temperature", "Concentration" };

	std::vector<std::string> fieldType;

	VelocitySolverType currentVelocitySolver = SOLVER_SIMPLE;
	ConvectionScheme convectionScheme = CONV_UPWIND;
	GradientScheme gradientScheme = GRAD_LSQ;

	int saveKeyFrameIter = 2;

	std::thread solverThread;
	cudaStream_t stream = nullptr;

	bool solverRunning = false;
	bool continueSolver = false;
	bool isReady = false;
	bool useMultigrid = false;

	// run solver
	void run(const Mesh& mesh);
	void runSimple(const Mesh& mesh);
	void runBiCGStab();

	// check if solver can be continued
	bool canContinue(const Mesh& mesh, std::string* reason = nullptr) const;

	// shutdown by syncing solverThread with the main system. A synchronization point
	// if this is called while the solver is running, the app will block until the solver finishes
	void shutdown();

	Config& config;
	GridConfig& g;
	FluidPropertyConfig& f;
	IterationConfig& itr;
	VariableUnits& varUnits;

	// struct to determine which fields to solve for
	SolverFieldOption fieldOption;

	// gui classes
	Console* console = nullptr;
	ResidualPlot* residualPlot = nullptr;

	// solution fields
	std::unordered_map<std::string, SolutionField> solutions;

	// residual
	std::unordered_map<std::string, ConfigResidual> configResiduals;

	// coefficients
	std::unordered_map<std::string, Coefficients> coefficients;

	// scalar solution
	SolutionScalar scalarSolutions;

	// fvMesh
	FVMesh fvMesh;

	// host copy of the per-face mass flux (indexed like fvMesh.faces), filled
	// after a solve so the inspector can report face fluxes and per-cell
	// continuity imbalance.
	std::vector<double> mDotHost;

	// config for simple algorithm
	ConfigSimple configSimple;

	// config for each solver
	ConfigSolver configSolver;

	// variable configs
	VariablesSimple simple;


	// set all variables to default values
	void setDefault();

private:

	struct ContinuationState {
		bool valid = false;
		int cells = 0;
		int faces = 0;
		int faceRefs = 0;
		int nr = 0;
		int nz = 0;
		bool useFaceCoefficients = false;
		bool solveEnergy = false;
		bool solveConcentration = false;
	};

	int currentIteration = 0;

	// check if the solver can run
	bool runCheck(const Mesh& mesh);

	// solve for mass imbalance
	std::vector<double> getMassImbalance(int N);

	// initialize config residuals
	void initConfigResiduals();

	void initCoefficients();

	bool buildContinuationState(
		const Mesh& mesh,
		ContinuationState& state,
		std::string* reason = nullptr
	) const;

	std::vector<uint8_t> buildStructuredActiveCells(
		const Mesh& mesh,
		std::string* reason = nullptr
	) const;

	// determine what field we're solving for
	void addFieldType();

	// create solution map
	void createSolutions(int N);

	MemoryConfig mem;
	ContinuationState continuationState;

};
