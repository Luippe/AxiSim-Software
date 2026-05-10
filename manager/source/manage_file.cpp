#include "manage_file.h"
#include "solver.h"
#include "mesh.h"
#include "memory_manager.h"
#include "tinyfiledialogs.h"
#include "solver_struct.h"
#include "results.h"
#include "printer.h"


template<typename... Args>
void saveBoundaryConditionConfigs(std::ofstream& out, const Args&... configs) {
	(writeBoundaryConditionConfig(out, configs), ...);
}

template<typename... Args>
void loadBoundaryConditionConfigs(std::ifstream& in, Args&... configs) {
	(readBoundaryConditionConfig(in, configs), ...);
}

bool fileExists(const std::string& filename) {
	std::ifstream file(filename);
	return file.good();
}

void saveFromExplorerMesh(Mesh& mesh) {
	const char* path = tinyfd_saveFileDialog(
		"Save Mesh",          // dialog title
		"mesh.bin",           // default filename
		0,                    // number of filters
		nullptr,              // filter patterns
		nullptr               // filter description
	);

	if (!path) return;

	saveFromPathMesh(path, mesh);
}

void saveFromPathMesh(const char* path, Mesh& mesh) {
	std::ofstream out(path, std::ios::binary);

	// save user specific input
	writeAll(
		out,
		mesh.nseg,
		mesh.g.R,
		mesh.g.L,
		mesh.g.nr,
		mesh.g.nz,
		mesh.g.dr,
		mesh.g.dz,
		mesh.g.cell_top,
		mesh.g.cell_left,
		mesh.g.cell_thickness,
		mesh.g.cell_right,
		mesh.cv
	);
	out.close();
}



void loadFromExplorerMesh(Mesh& mesh) {
	const char* filters[] = { "*.bin" };

	const char* path = tinyfd_openFileDialog(
		"Load Mesh",
		"",
		1,
		filters,
		"Binary Mesh Files",
		0 // 0 = single file, 1 = multiple files
	);

	if (!path) return;

	loadFromPathMesh(path, mesh);
}

void loadFromPathMesh(const char* path, Mesh& mesh) {

	std::ifstream in(path, std::ios::binary);

	// load dimensions
	readBinary(path,
		mesh.nseg,
		mesh.g.R,
		mesh.g.L,
		mesh.g.nr,
		mesh.g.nz,
		mesh.g.dr,
		mesh.g.dz,
		mesh.g.cell_top,
		mesh.g.cell_left,
		mesh.g.cell_thickness,
		mesh.g.cell_right,
		mesh.cv);

}

void saveFromPathSolver(const char* path, Solver& solver) {

	std::ofstream out(path, std::ios::binary);
	saveBinary(out, solver.addConvectionTerm, solver.transient);
	saveBinary(out, solver.dt, solver.tEnd, solver.saveKeyFrameIter);
	saveBoundaryConditionConfigs(out, solver.uBC, solver.vBC, solver.pBC, solver.concBC);
	writeAll(out, solver.configSimple);
	out.close();

}

void saveFromExplorerSolver(Solver& solver) {
	const char* path = tinyfd_saveFileDialog(
		"Save Solver",          // dialog title
		"solver.bin",           // default filename
		0,                    // number of filters
		nullptr,              // filter patterns
		nullptr               // filter description
	);

	if (!path) return;

	saveFromPathSolver(path, solver);
}

void loadFromPathSolver(const char* path, Solver& solver) {
	std::ifstream in(path, std::ios::binary);
	readBinary(in, solver.addConvectionTerm, solver.transient);
	readBinary(in, solver.dt, solver.tEnd, solver.saveKeyFrameIter);
	loadBoundaryConditionConfigs(in, solver.uBC, solver.vBC, solver.pBC, solver.concBC);
	readVar(in, solver.configSimple);
}

void loadFromExplorerSolver(Solver& solver) {

	const char* filters[] = { "*.bin" };

	const char* path = tinyfd_openFileDialog(
		"Load Solver",
		"",
		1,
		filters,
		"Binary Solver Files",
		0 // 0 = single file, 1 = multiple files
	);

	if (!path) return;

	loadFromPathSolver(path, solver);
}



void saveLaunchMesh(Mesh& mesh) {
	const char* path = "openAtLaunchMesh.bin";
	saveFromPathMesh(path, mesh);
}

void saveLaunchSolver(Solver& solver) {
	const char* path = "openAtLaunchSolver.bin";
	//check();
	saveFromPathSolver(path, solver);
}

//void saveFromExplorerResults(Results& results) {
//	const char* path = "openAtLaunchResults.bin";
//	saveFromPathResults(path, results);
//}

//void saveLaunchResults(Results& results) {
//	const char* path = "openAtLaunchResults.bin";
//	if (!path) return;
//	saveFromPathMesh(path, results);
//}

void loadAtLaunch(Mesh& mesh, Solver& solver, Results& results) {

	const char* meshFile = "openAtLaunchMesh.bin";
	const char* solverFile = "openAtLaunchSolver.bin";
	const char* resultsFile = "openAtLaunchResults.bin";

	// load mesh file if it exists
	{
		std::ifstream in(meshFile, std::ios::binary);
		if (in) {
			loadFromPathMesh(meshFile, mesh);

			mesh.updateAfterLoadingFile();
		}
	}

	// load solver file if it exists
	{
		std::ifstream in(solverFile, std::ios::binary);
		if (in) {
			loadFromPathSolver(solverFile, solver);
		}
	}


}

void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc) {
	int type = (int)(bc.type);
	out.write((const char*)&type, sizeof(type));
	out.write((const char*)&bc.val, sizeof(bc.val));
}

void writeBoundaryConditionConfig(std::ofstream& out, const BoundaryConditionConfig& bcConfig) {
	writeBoundaryCondition(out, bcConfig.inlet);
	writeBoundaryCondition(out, bcConfig.outlet);
	writeBoundaryCondition(out, bcConfig.outer);
	writeBoundaryCondition(out, bcConfig.centerline);
}

void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc) {
	int type = 0;

	in.read((char*)&type, sizeof(type));
	in.read((char*)&bc.val, sizeof(bc.val));

	bc.type = (BCType)(type);
}

void readOneBoundaryCondition(std::ifstream& in, BoundaryConditionConfig& bcConfig) {
	readBoundaryCondition(in, bcConfig.inlet);
	readBoundaryCondition(in, bcConfig.outlet);
	readBoundaryCondition(in, bcConfig.outer);
	readBoundaryCondition(in, bcConfig.centerline);
}

std::ofstream openBinaryFile(const char* path) {
	return std::ofstream(path, std::ios::binary);
}
