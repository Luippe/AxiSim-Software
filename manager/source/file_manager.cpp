#include "file_manager.h"

#include "tinyfiledialogs.h"

#include "project.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "memory_manager.h"
#include "printer.h"


bool fileExists(const std::string& filename) {

	std::ifstream file(filename);
	return file.good();

}
// ====================================================
// ----------BOUNDARY GROUP AND BCS--------------------
// ====================================================

void writeBoundaryGroup(std::ofstream& out, const BoundarySegmentGroup& group) {

	writeAll(out, 
		group.id,
		group.name,
		group.nameBuffer,
		group.type,
		group.segmentIDs,
		group.edges,
		group.includesOrientation,
		group.totalLength,
		group.bcs
	);

}

void writeBoundaryGroups(std::ofstream& out, const std::vector<BoundarySegmentGroup>& groups) {

	size_t size = groups.size();

	out.write((const char*)(&size), sizeof(size));

	for (const BoundarySegmentGroup& group : groups) {
		writeBoundaryGroup(out, group);
	}

}

void readBoundaryGroup(std::ifstream& in, BoundarySegmentGroup& group) {

	readAll(in,
		group.id,
		group.name,
		group.nameBuffer,
		group.type,
		group.segmentIDs,
		group.edges,
		group.includesOrientation,
		group.totalLength,
		group.bcs
	);

}

void readBoundaryGroups(std::ifstream& in, std::vector<BoundarySegmentGroup>& groups) {

	size_t size = 0;

	in.read((char*)(&size), sizeof(size));
	groups.resize(size);

	for (BoundarySegmentGroup& group : groups) {
		readBoundaryGroup(in, group);
	}

}

// ====================================================
// -------------------PROJECT--------------------------
// ====================================================
void saveFromPathProject(const char* path, Project& project) {

	std::ofstream out(path, std::ios::binary);
	saveFromPathMesh(out, project.mesh);
	saveFromPathSolver(out, project.solver);
	saveFromPathResults(out, project.results);
	out.close();
}

void saveLaunchProject(Project& project) {
	const char* path = "openAtLaunchProject.bin";
	saveFromPathProject(path, project);
}

void saveFromExplorerProject(Project& project) {
	const char* path = tinyfd_saveFileDialog(
		"Save Project",          // dialog title
		"project.bin",           // default filename
		0,                    // number of filters
		nullptr,              // filter patterns
		nullptr               // filter description
	);

	if (!path) return;

	saveFromPathProject(path, project);
}

void loadFromExplorerProject(Project& project) {

	const char* filters[] = { "*.bin" };

	const char* path = tinyfd_openFileDialog(
		"Load Project",
		"",
		1,
		filters,
		"Binary Project Files",
		0 // 0 = single file, 1 = multiple files
	);

	if (!path) return;

	std::ifstream in(path, std::ios::binary);
	loadFromPathMesh(in, project.mesh);
	loadFromPathSolver(in, project.solver);

}

// ====================================================
// -------------------MESH-----------------------------
// ====================================================
void saveFromExplorerMesh(Mesh& mesh) {
	const char* path = tinyfd_saveFileDialog(
		"Save Mesh",          // dialog title
		"mesh.bin",           // default filename
		0,                    // number of filters
		nullptr,              // filter patterns
		nullptr               // filter description
	);

	if (!path) return;

	std::ofstream out(path, std::ios::binary);
	saveFromPathMesh(out, mesh);
}

void saveFromPathMesh(std::ofstream& out, Mesh& mesh) {

	// save user specific input
	writeAll(
		out,
		mesh.nseg,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
		mesh.boundarySegments,
		mesh.nextGroupID,
		mesh.g.obstacleIndices,
		mesh.g.R,
		mesh.g.L,
		mesh.g.nr,
		mesh.g.nz,
		mesh.g.dr,
		mesh.g.dz,
		mesh.g.rBias,
		mesh.g.zBias,
		mesh.g.r,
		mesh.g.z,
		mesh.g.rFace,
		mesh.g.zFace
	);

	writeBoundaryGroups(out, mesh.boundaryGroups);


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
	std::ifstream in(path, std::ios::binary);
	loadFromPathMesh(in, mesh);
}

void loadFromPathMesh(std::ifstream& in, Mesh& mesh) {

	// load dimensions
	readAll(in,
		mesh.nseg,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
		mesh.boundarySegments,
		mesh.nextGroupID,
		mesh.g.obstacleIndices,
		mesh.g.R,
		mesh.g.L,
		mesh.g.nr,
		mesh.g.nz,
		mesh.g.dr,
		mesh.g.dz,
		mesh.g.rBias,
		mesh.g.zBias,
		mesh.g.r,
		mesh.g.z,
		mesh.g.rFace,
		mesh.g.zFace
	);

	readBoundaryGroups(in, mesh.boundaryGroups);
}

// ====================================================
// -------------------SOLVER---------------------------
// ====================================================
void saveFromPathSolver(std::ofstream& out, Solver& solver) {

	saveBinary(out, solver.varUnits, solver.fieldOption, solver.enabledResiduals);
	saveBinary(out, 
		solver.linearSolverConfig, 
		solver.currentVelocitySolver,
		solver.currentResidual,
		solver.currentResidualNorm,
		solver.currentResidualScaling);
	saveBinary(out, solver.addConvectionTerm, solver.transient);
	saveBinary(out, solver.dt, solver.tEnd, solver.saveKeyFrameIter);
	writeAll(out, solver.configSimple);

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
	std::ofstream out(path, std::ios::binary);
	saveFromPathSolver(out, solver);
}

void loadFromPathSolver(std::ifstream& in, Solver& solver) {

	readBinary(in, solver.varUnits, solver.fieldOption, solver.enabledResiduals);
	readBinary(in,
		solver.linearSolverConfig,
		solver.currentVelocitySolver,
		solver.currentResidual,
		solver.currentResidualNorm,
		solver.currentResidualScaling);
	readBinary(in, solver.addConvectionTerm, solver.transient);
	readBinary(in, solver.dt, solver.tEnd, solver.saveKeyFrameIter);
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

	std::ifstream in(path, std::ios::binary);
	loadFromPathSolver(in, solver);
}

// ====================================================
// -------------------REUSLTS--------------------------
// ====================================================
void saveFromPathResults(std::ofstream& out, const Results& results) {

	saveBinary(out, results.sceneScale);

}

void loadFromPathResults(std::ifstream& in, Results& results) {

	readBinary(in, results.sceneScale);

}

void loadAtLaunch(Project& project) {

	const char* projectFile = "openAtLaunchProject.bin";

	std::ifstream in(projectFile, std::ios::binary);
	{
		if (in) {
			loadFromPathMesh(in, project.mesh);
			project.mesh.updateAfterLoadingFile();
		}
	}

	// load solver file if it exists
	{
		if (in) {
			loadFromPathSolver(in, project.solver);
		}
	}

	// load results file if it exists
	{
		if (in) {
			loadFromPathResults(in, project.results);
		}
	}
}

void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc) {
	int type = (int)(bc.type);

	out.write((const char*)&type, sizeof(type));
	out.write((const char*)&bc.value, sizeof(bc.value));
}

void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc) {
	int type = 0;

	in.read((char*)&type, sizeof(type));
	in.read((char*)&bc.value, sizeof(bc.value));

	bc.type = (BCType)(type);
}

std::ofstream openBinaryFile(const char* path) {
	return std::ofstream(path, std::ios::binary);
}

