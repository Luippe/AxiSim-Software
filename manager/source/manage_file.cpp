#include "manage_file.h"
#include "solver.h"
#include "mesh.h"
#include "memory_manager.h"
#include "tinyfiledialogs.h"
#include "solver_struct.h"
#include "results.h"

// save one scalar value, such as double, int, float, size_t, etc.
template<typename T>
void readVector(std::ifstream& in, std::vector<T>& vec) {
	size_t size = 0;
	in.read((char*)&size, sizeof(size));
	vec.resize(size);
	if (size > 0) { // avoid empty vector
		in.read((char*)vec.data(), size * sizeof(T));
	}
}

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

void loadVelocity(GridConfig& g, FluidPropertyConfig& f) {

	std::ifstream in("data.bin", std::ios::binary);

	// initialize variables
	int Nu = g.nr * (g.nz + 1);
	int Nv = (g.nr + 1) * g.nz;
	std::vector<double> u(Nu,0.0);
	std::vector<double> v(Nv,0.0);

	// load and allocate memory for velocity
	readVector(in, u);
	readVector(in, v);
	f.u = copyHostToDevice(u.data(), Nu);
	f.v = copyHostToDevice(v.data(), Nv);
	//printf("%f\n", u[0]);
	//printf("%f\n", v[0]);
	in.close();

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

void saveFromPathSolver(const char* path, Solver& solver) {

	std::ofstream out(path, std::ios::binary);
	saveBoundaryConditionConfigs(out, solver.uBC, solver.vBC, solver.pBC, solver.concBC);
	writeAll(out, solver.configSimple);
	out.close();

}

//void saveFromPathSolver(const char* path, Results& results) {
//	std::ofstream out(path, std::ios::binary);
//	writeAll(
//		out,
//	g = mesh.g;
//	nseg = mesh.nseg;
//	cv = mesh.cv;
//	vertices = mesh.vertices;
//	verticesCV = mesh.verticesCV;
//	indicesCV = mesh.indicesCV;
//	);
//	writeAll(out, solver.configSimple);
//	out.close();
//}

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
	readValue(in, mesh.nseg);
	readValue(in, mesh.g.R);
	readValue(in, mesh.g.L);
	readValue(in, mesh.g.nr);
	readValue(in, mesh.g.nz);
	readValue(in, mesh.g.dr);
	readValue(in, mesh.g.dz);
	readValue(in, mesh.g.cell_top);
	readValue(in, mesh.g.cell_left);
	readValue(in, mesh.g.cell_thickness);
	readValue(in, mesh.g.cell_right);

	// load vectors
	readVector(in, mesh.cv);

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

void loadFromPathSolver(const char* path, Solver& solver) {
	std::ifstream in(path, std::ios::binary);
	loadBoundaryConditionConfigs(in, solver.uBC, solver.vBC, solver.pBC, solver.concBC);
	readValue(in, solver.configSimple);
}

void saveLaunchMesh(Mesh& mesh) {
	const char* path = "openAtLaunchMesh.bin";
	saveFromPathMesh(path, mesh);
}

void saveLaunchSolver(Solver& solver) {
	const char* path = "openAtLaunchSolver.bin";
	saveFromPathSolver(path, solver);
}
//
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

void readBoundaryConditionConfig(std::ifstream& in, BoundaryConditionConfig& bcConfig) {
	readBoundaryCondition(in, bcConfig.inlet);
	readBoundaryCondition(in, bcConfig.outlet);
	readBoundaryCondition(in, bcConfig.outer);
	readBoundaryCondition(in, bcConfig.centerline);
}