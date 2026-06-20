#include "file_manager.h"

#include "tinyfiledialogs.h"

#include "project.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "keyboard_manager.h"
#include "memory_manager.h"
#include "printer.h"

using namespace Shortcuts;

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

void writeString(std::ofstream& out, const std::string& value) {
	size_t size = value.size();
	out.write((const char*)(&size), sizeof(size));
	out.write(value.data(), size);
}

bool readString(std::ifstream& in, std::string& value) {
	size_t size = 0;
	if (!in.read((char*)(&size), sizeof(size))) {
		return false;
	}

	value.resize(size);
	if (size == 0) {
		return true;
	}

	return (bool)in.read(value.data(), size);
}

void writeSketchNamedSegment(
	std::ofstream& out,
	const SketchNamedSegment& segment
) {
	writeAll(
		out,
		segment.sourceType,
		segment.entityID,
		segment.edgeIndex,
		segment.startT,
		segment.endT
	);
}

bool readSketchNamedSegment(
	std::ifstream& in,
	SketchNamedSegment& segment
) {
	return readAll(
		in,
		segment.sourceType,
		segment.entityID,
		segment.edgeIndex,
		segment.startT,
		segment.endT
	);
}

void writeSketchNamedSelection(
	std::ofstream& out,
	const SketchNamedSelection& selection
) {
	writeAll(out, selection.id);
	writeString(out, selection.name);
	writeAll(out, selection.nameBuffer);

	size_t size = selection.segments.size();
	out.write((const char*)(&size), sizeof(size));
	for (const SketchNamedSegment& segment : selection.segments) {
		writeSketchNamedSegment(out, segment);
	}
}

bool readSketchNamedSelection(
	std::ifstream& in,
	SketchNamedSelection& selection
) {
	if (!readAll(in, selection.id) ||
		!readString(in, selection.name) ||
		!readAll(in, selection.nameBuffer)) {
		return false;
	}

	size_t size = 0;
	if (!in.read((char*)(&size), sizeof(size))) {
		return false;
	}

	selection.segments.resize(size);
	for (SketchNamedSegment& segment : selection.segments) {
		if (!readSketchNamedSegment(in, segment)) {
			return false;
		}
	}

	return true;
}

void writeSketchNamedSelections(
	std::ofstream& out,
	const std::vector<SketchNamedSelection>& selections
) {
	size_t size = selections.size();
	out.write((const char*)(&size), sizeof(size));

	for (const SketchNamedSelection& selection : selections) {
		writeSketchNamedSelection(out, selection);
	}
}

bool readSketchNamedSelections(
	std::ifstream& in,
	std::vector<SketchNamedSelection>& selections
) {
	size_t size = 0;
	if (!in.read((char*)(&size), sizeof(size))) {
		return false;
	}

	selections.resize(size);
	for (SketchNamedSelection& selection : selections) {
		if (!readSketchNamedSelection(in, selection)) {
			return false;
		}
	}

	return true;
}

// ====================================================
// -------------------KEYBOARD-------------------------
// ====================================================
void saveKeyboardShortcuts(std::ofstream& out) {

	writeAll(
		out,
		undoShortcut,
		redoShortcut,
		resetViewShortcut,
		selectToolShortcut,
		rulerToolShortcut,
		trimToolShortcut,
		eraseToolShortcut,
		lineToolShortcut,
		rectangleToolShortcut,
		circleToolShortcut
	);
}

// ====================================================
// -------------------PROJECT--------------------------
// ====================================================
void saveFromPathProject(const char* path, Project& project) {

	std::ofstream out(path, std::ios::binary);
	saveFromPathGeometry(out, project.geometry);
	saveFromPathMesh(out, project.mesh);
	saveFromPathSolver(out, project.solver);
	saveFromPathResults(out, project.results);
	saveKeyboardShortcuts(out);
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
	loadFromPathGeometry(in, project.geometry);
	loadFromPathMesh(in, project.mesh);
	loadFromPathSolver(in, project.solver);

}

// ====================================================
// -------------------GEOMETRY-------------------------
// ====================================================
void saveFromExplorerGeometry(Geometry& geometry) {
	const char* path = tinyfd_saveFileDialog(
		"Save Geometry",          // dialog title
		"geometry.bin",           // default filename
		0,                    // number of filters
		nullptr,              // filter patterns
		nullptr               // filter description
	);

	if (!path) return;

	std::ofstream out(path, std::ios::binary);
	saveFromPathGeometry(out, geometry);
}

void saveFromPathGeometry(std::ofstream& out, Geometry& geometry) {

	const SketchModel& sketch = geometry.sketch;

	writeAll(
		out,
		sketch.points,
		sketch.lines,
		sketch.circles,
		sketch.arcs,
		sketch.rectangles,
		sketch.dimensions
	);

	writeSketchNamedSelections(out, sketch.namedSelections);

	writeAll(
		out,
		sketch.nextPointID,
		sketch.nextLineID,
		sketch.nextCircleID,
		sketch.nextArcID,
		sketch.nextRectangleID,
		sketch.nextDimensionID,
		sketch.nextNamedSelectionID
	);
}

void loadFromPathGeometry(std::ifstream& in, Geometry& geometry) {

	SketchModel& sketch = geometry.sketch;

	// load geometry
	readAll(
		in,
		sketch.points,
		sketch.lines,
		sketch.circles,
		sketch.arcs,
		sketch.rectangles,
		sketch.dimensions
	);

	readSketchNamedSelections(in, sketch.namedSelections);

	readAll(
		in,
		sketch.nextPointID,
		sketch.nextLineID,
		sketch.nextCircleID,
		sketch.nextArcID,
		sketch.nextRectangleID,
		sketch.nextDimensionID,
		sketch.nextNamedSelectionID
	);
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

