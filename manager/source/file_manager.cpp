#include "file_manager.h"

#include "tinyfiledialogs.h"

#include "solver.h"
#include "mesh.h"
#include "results.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "memory_manager.h"
#include "printer.h"


bool fileExists(const std::string& filename) {

	std::ifstream file(filename);
	return file.good();

}

void writeBoundarySegmentGroup(std::ofstream& out, const BoundarySegmentGroup& group) {
	//printInt(group.type);

	writeAll(out, 
		group.id, 
		group.name, 
		group.nameBuffer,
		group.segmentIDs,
		group.edges,
		group.bcs,
		group.type);

}

void writeBoundarySegmentGroups(std::ofstream& out, const std::vector<BoundarySegmentGroup>& groups) {

	size_t size = groups.size();

	out.write((const char*)(&size), sizeof(size));

	for (const BoundarySegmentGroup& group : groups) {
		writeBoundarySegmentGroup(out, group);
	}

}

void readBoundarySegmentGroup(std::ifstream& in, BoundarySegmentGroup& group) {

	readAll(in,
		group.id,
		group.name,
		group.nameBuffer,
		group.segmentIDs,
		group.edges,
		group.bcs,
		group.type);

}

void readBoundarySegmentGroups(std::ifstream& in, std::vector<BoundarySegmentGroup>& groups) {

	size_t size = 0;

	in.read((char*)(&size), sizeof(size));
	groups.resize(size);

	for (BoundarySegmentGroup& group : groups) {
		readBoundarySegmentGroup(in, group);
	}

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

	saveFromPathMesh(path, mesh);
}

void saveFromPathMesh(const char* path, Mesh& mesh) {
	std::ofstream out(path, std::ios::binary);

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

	writeBoundarySegmentGroups(out, mesh.boundaryGroups);

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

	readBoundarySegmentGroups(in, mesh.boundaryGroups);
}

void saveLaunchMesh(Mesh& mesh) {
	const char* path = "openAtLaunchMesh.bin";
	saveFromPathMesh(path, mesh);
}

// ====================================================
// -------------------SOLVER---------------------------
// ====================================================
void saveFromPathSolver(const char* path, Solver& solver) {

	std::ofstream out(path, std::ios::binary);

	saveBinary(out, solver.varUnits, solver.fieldOption);
	saveBinary(out, 
		solver.linearSolverConfig, 
		solver.currentVelocitySolver,
		solver.currentResidual,
		solver.currentResidualNorm,
		solver.currentResidualScaling);
	saveBinary(out, solver.addConvectionTerm, solver.transient);
	saveBinary(out, solver.dt, solver.tEnd, solver.saveKeyFrameIter);
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

	readBinary(in, solver.varUnits, solver.fieldOption);
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

	loadFromPathSolver(path, solver);
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
	out.write((const char*)&bc.value, sizeof(bc.value));
}

void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc) {
	int type = 0;

	in.read((char*)&type, sizeof(type));
	in.read((char*)&bc.value, sizeof(bc.value));

	bc.type = (BCType)(type);
}

void readOneBoundaryCondition(std::ifstream& in) {

}

std::ofstream openBinaryFile(const char* path) {
	return std::ofstream(path, std::ios::binary);
}

