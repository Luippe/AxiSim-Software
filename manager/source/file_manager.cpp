#include "file_manager.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <commdlg.h>
#include <string>
#include <cmath>
#include <cstdint>

#include "project.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "keyboard_manager.h"
#include "memory_manager.h"
#include "printer.h"

using namespace Shortcuts;

namespace {
	constexpr std::uint32_t solverFileMagic = 0x53585641u; // "AXVS" little-endian
	constexpr std::uint32_t solverFileVersion = 1u;

	struct LegacyConfigSimple {
		int maxIter = 50;
		int checkConv = 1;
		double momTol = 1e-8;
		double ppTol = 1e-5;
	};

	std::streamoff remainingBytes(std::ifstream& in) {
		std::streampos pos = in.tellg();
		if (pos == std::streampos(-1)) {
			return 0;
		}

		in.seekg(0, std::ios::end);
		std::streampos end = in.tellg();
		in.seekg(pos);

		if (end == std::streampos(-1) || end < pos) {
			return 0;
		}

		return end - pos;
	}

	std::streamoff legacySolverPayloadSize() {
		return
			sizeof(VariableUnits) +
			sizeof(SolverFieldOption) +
			sizeof(EnabledResiduals) +
			sizeof(ConfigSolver) +
			sizeof(VelocitySolverType) +
			sizeof(ResidualType) +
			sizeof(ResidualNormType) +
			sizeof(ResidualScalingType) +
			sizeof(bool) +
			sizeof(bool) +
			sizeof(double) +
			sizeof(double) +
			sizeof(int) +
			sizeof(LegacyConfigSimple);
	}

	std::streamoff currentSolverPayloadSize() {
		return
			sizeof(VariableUnits) +
			sizeof(SolverFieldOption) +
			sizeof(EnabledResiduals) +
			sizeof(ConfigSolver) +
			sizeof(VelocitySolverType) +
			sizeof(ResidualType) +
			sizeof(ResidualNormType) +
			sizeof(ResidualScalingType) +
			sizeof(ConvectionScheme) +
			sizeof(bool) +
			sizeof(bool) +
			sizeof(double) +
			sizeof(double) +
			sizeof(int) +
			sizeof(FluidPropertyConfig) +
			sizeof(ConfigSimple);
	}

	void sanitizeSolverConfig(Solver& solver) {
		if (solver.configSolver.maxIter < 1) {
			solver.configSolver.maxIter = 20;
		}

		if (solver.configSimple.maxIter < 1) {
			solver.configSimple.maxIter = 50;
		}

		if (solver.configSimple.checkConv < 1) {
			solver.configSimple.checkConv = 1;
		}

		if (solver.configSimple.nNonOrthCorrectors < 0) {
			solver.configSimple.nNonOrthCorrectors = 0;
		}
		else if (solver.configSimple.nNonOrthCorrectors > 4) {
			solver.configSimple.nNonOrthCorrectors = 4;
		}

		if (!std::isfinite(solver.configSimple.momTol) ||
			solver.configSimple.momTol <= 0.0) {
			solver.configSimple.momTol = 1e-8;
		}

		if (!std::isfinite(solver.configSimple.ppTol) ||
			solver.configSimple.ppTol <= 0.0) {
			solver.configSimple.ppTol = 1e-5;
		}

		if ((int)solver.configSolver.type < 0 ||
			(int)solver.configSolver.type > (int)LINEAR_GS_RB) {
			solver.configSolver.type = LINEAR_JACOBI;
		}

		if ((int)solver.currentVelocitySolver < 0 ||
			(int)solver.currentVelocitySolver > (int)SOLVER_SIMPLE) {
			solver.currentVelocitySolver = SOLVER_SIMPLE;
		}

		if ((int)solver.currentResidual < (int)RESIDUAL_RAW ||
			(int)solver.currentResidual > (int)RESIDUAL_RMS) {
			solver.currentResidual = RESIDUAL_RAW;
		}

		if ((int)solver.currentResidualNorm < (int)RESIDUAL_L1 ||
			(int)solver.currentResidualNorm > (int)RESIDUAL_LINF) {
			solver.currentResidualNorm = RESIDUAL_LINF;
		}

		if ((int)solver.currentResidualScaling < (int)RESIDUAL_SCALING_NONE ||
			(int)solver.currentResidualScaling > (int)RESIDUAL_SCALING_SQRT_N) {
			solver.currentResidualScaling = RESIDUAL_SCALING_NONE;
		}

		if ((int)solver.convectionScheme < (int)CONV_UPWIND ||
			(int)solver.convectionScheme > (int)CONV_SECOND_ORDER_UPWIND) {
			solver.convectionScheme = CONV_UPWIND;
		}

		FluidPropertyConfig defaults;
		bool resetFluid =
			!std::isfinite(solver.f.rho) || solver.f.rho < 1.0e-12 ||
			!std::isfinite(solver.f.mu) || solver.f.mu < 1.0e-12 ||
			!std::isfinite(solver.f.cp) || solver.f.cp <= 0.0 ||
			!std::isfinite(solver.f.k) || solver.f.k < 0.0 ||
			!std::isfinite(solver.f.D) || solver.f.D < 0.0;

		if (resetFluid) {
			solver.f = defaults;
		}
	}

	bool readCurrentSolverPayload(std::ifstream& in, Solver& solver) {
		return readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.enabledResiduals,
			solver.configSolver,
			solver.currentVelocitySolver,
			solver.currentResidual,
			solver.currentResidualNorm,
			solver.currentResidualScaling,
			solver.convectionScheme,
			solver.saveKeyFrameIter,
			solver.f,
			solver.configSimple
		);
	}

	bool readLegacySolverPayload(std::ifstream& in, Solver& solver) {
		LegacyConfigSimple legacySimple;

		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.enabledResiduals,
			solver.configSolver,
			solver.currentVelocitySolver,
			solver.currentResidual,
			solver.currentResidualNorm,
			solver.currentResidualScaling,
			solver.saveKeyFrameIter,
			legacySimple
		);

		if (!ok) {
			return false;
		}

		solver.convectionScheme = CONV_UPWIND;
		solver.configSimple.maxIter = legacySimple.maxIter;
		solver.configSimple.checkConv = legacySimple.checkConv;
		solver.configSimple.momTol = legacySimple.momTol;
		solver.configSimple.ppTol = legacySimple.ppTol;
		solver.configSimple.nNonOrthCorrectors = 0;

		return true;
	}
}

// ====================================================
// ----------FILE DIALOG-------------------------------
// ====================================================
std::wstring saveFileDialog() {
	wchar_t filePath[MAX_PATH] = L"";

	OPENFILENAMEW ofn{};
	GLFWwindow* window = glfwGetCurrentContext();

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = window ? glfwGetWin32Window(window) : nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter =
		L"Binary Files\0*.bin\0"
		L"All Files\0*.*\0";

	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = L"bin";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (GetSaveFileNameW(&ofn)) {
		return filePath;
	}

	return L"";
}

std::wstring loadFileDialog() {
	wchar_t filePath[MAX_PATH] = L"";

	OPENFILENAMEW ofn{};
	GLFWwindow* window = glfwGetCurrentContext();

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = window ? glfwGetWin32Window(window) : nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter =
		L"Binary Files\0*.bin\0"
		L"All Files\0*.*\0";

	ofn.nFilterIndex = 1;
	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_FILEMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameW(&ofn)) {
		return filePath;
	}

	return L"";
}

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
		group.sizing,
		group.bcs,
		group.layers
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
		group.sizing,
		group.bcs,
		group.layers
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

// ----------BOUNDARY SEGMENTS--------------------
// BoundarySegment holds nested std::vectors (controlPoints, edgeIDs), so it is
// NOT trivially copyable and must be serialized field by field rather than with
// the generic raw-memcpy std::vector overload.
void writeBoundarySegment(std::ofstream& out, const BoundarySegment& seg) {

	writeAll(out,
		seg.id,
		seg.controlPoints,
		seg.edgeIDs,
		seg.sizing,
		seg.groupID,
		seg.loopID,
		seg.source
	);
}

void writeBoundarySegments(std::ofstream& out, const std::vector<BoundarySegment>& segments) {

	size_t size = segments.size();
	out.write((const char*)(&size), sizeof(size));

	for (const BoundarySegment& seg : segments) {
		writeBoundarySegment(out, seg);
	}
}

void readBoundarySegment(std::ifstream& in, BoundarySegment& seg) {

	readAll(in,
		seg.id,
		seg.controlPoints,
		seg.edgeIDs,
		seg.sizing,
		seg.groupID,
		seg.loopID,
		seg.source
	);
}

void readBoundarySegments(std::ifstream& in, std::vector<BoundarySegment>& segments) {

	size_t size = 0;
	in.read((char*)(&size), sizeof(size));
	segments.resize(size);

	for (BoundarySegment& seg : segments) {
		readBoundarySegment(in, seg);
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
void saveFromPathProject(const std::wstring& path, Project& project) {

	std::ofstream out(std::filesystem::path(path), std::ios::binary);
	saveFromPathGeometry(out, project.geometry);
	saveFromPathMesh(out, project.mesh);
	saveFromPathSolver(out, project.solver);
	//saveFromPathResults(out, project.results);
	//saveKeyboardShortcuts(out);
	out.close();
}

void saveLaunchProject(Project& project) {
	std::wstring path = L"openAtLaunchProject.bin";
	saveFromPathProject(path, project);
}

void saveFromExplorerProject(Project& project) {

	std::wstring path = saveFileDialog();
	if (path.empty()) return;

	saveFromPathProject(path, project);
}

void loadFromExplorerProject(Project& project) {

	std::wstring path = loadFileDialog();
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathGeometry(in, project.geometry);
	loadFromPathMesh(in, project.mesh);
	loadFromPathSolver(in, project.solver);

}

// ====================================================
// -------------------GEOMETRY-------------------------
// ====================================================
void saveFromExplorerGeometry(Geometry& geometry) {

	std::wstring path = saveFileDialog();
	if (path.empty()) return;

	std::ofstream out(std::filesystem::path(path), std::ios::binary);
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
		sketch.dimensions,

		sketch.nextPointID,
		sketch.nextLineID,
		sketch.nextCircleID,
		sketch.nextArcID,
		sketch.nextRectangleID,
		sketch.nextDimensionID
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
		sketch.dimensions,

		sketch.nextPointID,
		sketch.nextLineID,
		sketch.nextCircleID,
		sketch.nextArcID,
		sketch.nextRectangleID,
		sketch.nextDimensionID
	);
}


// ====================================================
// -------------------MESH-----------------------------
// ====================================================
void saveFromExplorerMesh(Mesh& mesh) {

	std::wstring path = saveFileDialog();
	if (path.empty()) return;

	std::ofstream out(std::filesystem::path(path), std::ios::binary);
	saveFromPathMesh(out, mesh);
}

void saveFromPathMesh(std::ofstream& out, Mesh& mesh) {

	// save user specific input
	writeAll(
		out,
		mesh.nseg,
		mesh.currentMeshType,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
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
		mesh.g.zFace,

		// unstructured (gmsh) mesh data
		mesh.unstructuredPoints,
		mesh.unstructuredTriangles,
		mesh.boundaryVertices,
		mesh.boundaryEdges
	);

	// non-trivially-copyable collections need element-wise serialization
	writeBoundarySegments(out, mesh.boundarySegments);
	writeBoundaryGroups(out, mesh.boundaryGroups);

	writeAll(out, mesh.nextRegionOfInfluenceID, mesh.regionsOfInfluence);
}

void loadFromExplorerMesh(Mesh& mesh) {

	std::wstring path = loadFileDialog();
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathMesh(in, mesh);
}

void loadFromPathMesh(std::ifstream& in, Mesh& mesh) {

	// load dimensions
	readAll(in,
		mesh.nseg,
		mesh.currentMeshType,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
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
		mesh.g.zFace,

		// unstructured (gmsh) mesh data
		mesh.unstructuredPoints,
		mesh.unstructuredTriangles,
		mesh.boundaryVertices,
		mesh.boundaryEdges
	);

	readBoundarySegments(in, mesh.boundarySegments);
	readBoundaryGroups(in, mesh.boundaryGroups);

	if (remainingBytes(in) >=
		static_cast<std::streamoff>(sizeof(mesh.nextRegionOfInfluenceID) + sizeof(size_t))) {
		readAll(in, mesh.nextRegionOfInfluenceID, mesh.regionsOfInfluence);
	}
	else {
		mesh.nextRegionOfInfluenceID = 0;
		mesh.regionsOfInfluence.clear();
	}

	// rebuild render buffers / FV connectivity from the loaded data
	mesh.updateAfterLoadingFile();
}

// ====================================================
// -------------------SOLVER---------------------------
// ====================================================
void saveFromPathSolver(std::ofstream& out, Solver& solver) {

	sanitizeSolverConfig(solver);

	writeAll(out, solverFileMagic, solverFileVersion);
	writeAll(
		out,
		solver.varUnits,
		solver.fieldOption,
		solver.enabledResiduals,
		solver.configSolver,
		solver.currentVelocitySolver,
		solver.currentResidual,
		solver.currentResidualNorm,
		solver.currentResidualScaling,
		solver.convectionScheme,
		solver.saveKeyFrameIter,
		solver.f,
		solver.configSimple
	);

}

void saveFromExplorerSolver(Solver& solver) {

	std::wstring path = saveFileDialog();
	if (path.empty()) return;

	std::ofstream out(std::filesystem::path(path), std::ios::binary);
	saveFromPathSolver(out, solver);
}

void loadFromPathSolver(std::ifstream& in, Solver& solver) {

	if (!in) {
		return;
	}

	std::streampos start = in.tellg();
	if (start == std::streampos(-1) || remainingBytes(in) <= 0) {
		return;
	}

	std::uint32_t magic = 0;
	if (!readVar(in, magic)) {
		in.clear();
		in.seekg(start);
		return;
	}

	bool ok = false;

	if (magic == solverFileMagic) {
		std::uint32_t version = 0;
		if (readVar(in, version) && version == solverFileVersion) {
			ok = readCurrentSolverPayload(in, solver);
		}
	}
	else {
		in.clear();
		in.seekg(start);

		const std::streamoff bytesLeft = remainingBytes(in);

		if (bytesLeft == currentSolverPayloadSize()) {
			ok = readCurrentSolverPayload(in, solver);
		}
		else if (bytesLeft >= legacySolverPayloadSize()) {
			ok = readLegacySolverPayload(in, solver);
		}
	}

	if (!ok) {
		in.clear();
		in.seekg(start);
		return;
	}

	sanitizeSolverConfig(solver);
}

void loadFromExplorerSolver(Solver& solver) {

	std::wstring path = loadFileDialog();
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
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
			loadFromPathGeometry(in, project.geometry);
		}
	}

	{
		if (in) {
			loadFromPathMesh(in, project.mesh);
			project.mesh.updateAfterLoadingFile();
		}
	}

	{
		if (in) {
			loadFromPathSolver(in, project.solver);
		}
	}

	//// load results file if it exists
	//{
	//	if (in) {
	//		loadFromPathResults(in, project.results);
	//	}
	//}
}

void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc) {
	int type = (int)(bc.type());
	double value = bc.value();

	out.write((const char*)&type, sizeof(type));
	out.write((const char*)&value, sizeof(value));
}

void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc) {
	int type = 0;
	double value = 0.0;

	in.read((char*)&type, sizeof(type));
	in.read((char*)&value, sizeof(value));

	bc.setType((BCType)(type));
	bc.setValue(value);
}

std::ofstream openBinaryFile(const char* path) {
	return std::ofstream(path, std::ios::binary);
}

