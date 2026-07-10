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
#include <filesystem>
#include <iostream>

#include "project.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"

#include "keyboard_manager.h"
#include "memory_manager.h"
#include "printer.h"

using namespace Shortcuts;

std::string wStringToString(std::wstring path) {

	// Calculating the length of the multibyte string
	size_t len = wcstombs(nullptr, path.c_str(), 0) + 1;

	// Creating a buffer to hold the multibyte string
	char* buffer = new char[len];

	// Converting wstring to string
	wcstombs(buffer, path.c_str(), len);

	// Creating std::string from char buffer
	std::string str(buffer);

	// Cleaning up the buffer
	delete[] buffer;

	return str;

}

namespace {
	constexpr std::uint32_t solverFileMagic = 0x53585641u; // "AXVS" little-endian
	// v2: residual display settings (type/norm/scaling/enabled) are stored per-residual
	// instead of as three global values. v1 files are still read and migrated.
	constexpr std::uint32_t solverFileVersion = 2u;

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

	// Fixed on-disk order of the named residuals for the v2 payload. Do NOT reorder:
	// the file format depends on it. Each name must match a key inserted by
	// Solver::initConfigResiduals so the loaded settings land on the right entry.
	static const char* const kResidualOrder[] = {
		"U", "V", "Continuity", "Temperature", "Concentration"
	};

	void clampResidualSettings(ResidualType& type, ResidualNormType& norm, ResidualScalingType& scale) {
		if ((int)type < (int)RESIDUAL_RAW || (int)type > (int)RESIDUAL_RMS) {
			type = RESIDUAL_RAW;
		}
		if ((int)norm < (int)RESIDUAL_L1 || (int)norm > (int)RESIDUAL_LINF) {
			norm = RESIDUAL_LINF;
		}
		if ((int)scale < (int)RESIDUAL_SCALING_NONE || (int)scale > (int)RESIDUAL_SCALING_SQRT_N) {
			scale = RESIDUAL_SCALING_NONE;
		}
	}

	// Write one residual's settings into its live map entry, if present. The map is
	// rebuilt with every name by Solver::initConfigResiduals before a load, and the
	// coeff reference each entry holds must stay bound, so we only touch value fields.
	void applyResidualSettings(Solver& solver, const char* name,
		ResidualType type, ResidualNormType norm, ResidualScalingType scale, bool enabled) {

		clampResidualSettings(type, norm, scale);

		auto it = solver.configResiduals.find(name);
		if (it == solver.configResiduals.end()) {
			return;
		}

		it->second.residualType = type;
		it->second.residualNormType = norm;
		it->second.residualScaleType = scale;
		it->second.enabled = enabled;
	}

	// Migrate an old single global residual setting onto every residual, taking each
	// residual's enabled flag from the matching plot toggle in enabledResiduals.
	void applyGlobalResidualToAll(Solver& solver,
		ResidualType type, ResidualNormType norm, ResidualScalingType scale) {

		const EnabledResiduals& en = solver.enabledResiduals;
		applyResidualSettings(solver, "U",             type, norm, scale, en.plotU);
		applyResidualSettings(solver, "V",             type, norm, scale, en.plotV);
		applyResidualSettings(solver, "Continuity",    type, norm, scale, en.plotCont);
		applyResidualSettings(solver, "Temperature",   type, norm, scale, en.plotTemp);
		applyResidualSettings(solver, "Concentration", type, norm, scale, en.plotConc);
	}

	// v2 residual block: type / norm / scaling / enabled for each residual, in kResidualOrder.
	void writeResidualConfigs(std::ofstream& out, const Solver& solver) {
		for (const char* name : kResidualOrder) {
			ResidualType        type    = RESIDUAL_RAW;
			ResidualNormType    norm    = RESIDUAL_LINF;
			ResidualScalingType scale   = RESIDUAL_SCALING_NONE;
			bool                enabled = false;

			auto it = solver.configResiduals.find(name);
			if (it != solver.configResiduals.end()) {
				type    = it->second.residualType;
				norm    = it->second.residualNormType;
				scale   = it->second.residualScaleType;
				enabled = it->second.enabled;
			}

			writeAll(out, type, norm, scale, enabled);
		}
	}

	bool readResidualConfigs(std::ifstream& in, Solver& solver) {
		for (const char* name : kResidualOrder) {
			ResidualType        type    = RESIDUAL_RAW;
			ResidualNormType    norm    = RESIDUAL_LINF;
			ResidualScalingType scale   = RESIDUAL_SCALING_NONE;
			bool                enabled = false;

			if (!readAll(in, type, norm, scale, enabled)) {
				return false;
			}

			applyResidualSettings(solver, name, type, norm, scale, enabled);
		}

		return true;
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

		// residual display settings are now per-residual; clamp each entry in place
		for (auto& entry : solver.configResiduals) {
			ConfigResidual& cfg = entry.second;
			clampResidualSettings(cfg.residualType, cfg.residualNormType, cfg.residualScaleType);
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

	// v2: per-residual display settings follow the common block (see writeResidualConfigs).
	bool readCurrentSolverPayload(std::ifstream& in, Solver& solver) {
		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.enabledResiduals,
			solver.configSolver,
			solver.currentVelocitySolver,
			solver.convectionScheme,
			solver.saveKeyFrameIter,
			solver.f,
			solver.configSimple
		);

		if (!ok) {
			return false;
		}

		return readResidualConfigs(in, solver);
	}

	// v1 (and pre-magic files of the same shape): a single global residual
	// type/norm/scaling. Read it into temporaries and migrate onto the per-residual map.
	bool readV1SolverPayload(std::ifstream& in, Solver& solver) {
		ResidualType        type  = RESIDUAL_RAW;
		ResidualNormType    norm  = RESIDUAL_LINF;
		ResidualScalingType scale = RESIDUAL_SCALING_NONE;

		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.enabledResiduals,
			solver.configSolver,
			solver.currentVelocitySolver,
			type,
			norm,
			scale,
			solver.convectionScheme,
			solver.saveKeyFrameIter,
			solver.f,
			solver.configSimple
		);

		if (!ok) {
			return false;
		}

		applyGlobalResidualToAll(solver, type, norm, scale);
		return true;
	}

	bool readLegacySolverPayload(std::ifstream& in, Solver& solver) {
		LegacyConfigSimple legacySimple;

		ResidualType        type  = RESIDUAL_RAW;
		ResidualNormType    norm  = RESIDUAL_LINF;
		ResidualScalingType scale = RESIDUAL_SCALING_NONE;

		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.enabledResiduals,
			solver.configSolver,
			solver.currentVelocitySolver,
			type,
			norm,
			scale,
			solver.saveKeyFrameIter,
			legacySimple
		);

		if (!ok) {
			return false;
		}

		applyGlobalResidualToAll(solver, type, norm, scale);

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
// -------------------SETTINGS-------------------------
// ====================================================
void saveSettings(Project& project, AppSettings& settings) {

	std::wstring path = L"project_settings.bin";
	std::ofstream out(std::filesystem::path(path), std::ios::binary);
	settings.quickLaunch = project.path;

	writeAll(
		out,
		settings.quickLaunch
	);
}

bool loadSettings(std::ifstream& in, AppSettings& settings) {

	return readAll(
		in,
		settings.quickLaunch
	);

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
		circleToolShortcut,
		saveProjectShortcut
	);
}

// ====================================================
// -------------------PROJECT--------------------------
// ====================================================
void saveEtc(std::ofstream& out, const Project& project) {

	writeAll(
		out,
		project.name,
		project.path,
		project.lengthScale
	);
}

void loadEtc(std::ifstream& in, Project& project) {

	readAll(
		in,
		project.name,
		project.path,
		project.lengthScale
	);

	// units are now known: ask the GUI to reset every inspector's view so the
	// grid/zoom matches the loaded project's length unit.
	project.resetInspectorViews = true;
}

bool saveHotkeyPressed(Project& project) {

	if (!project.name.empty()) {
		saveFromPathProject(project.path, project);
	}
	else {
		saveFromExplorerProject(project);
	}
	return true;
}

void saveFromPathProject(const std::wstring& path, Project& project) {

	std::ofstream out(std::filesystem::path(path), std::ios::binary);
	saveFromPathGeometry(out, project.geometry);
	saveFromPathMesh(out, project.mesh);
	saveFromPathSolver(out, project.solver);
	saveEtc(out, project);
	//saveFromPathResults(out, project.results);
	//saveKeyboardShortcuts(out);
	out.close();
}

void saveLaunchProject(Project& project) {
	if (project.path.empty()) return;

	std::wstring path = L"openAtLaunchProject.bin";
	saveFromPathProject(path, project);
}

void saveFromExplorerProject(Project& project) {

	std::wstring path = saveFileDialog();
	if (path.empty()) return;

	project.path = path;

	std::filesystem::path p(path);

	project.name = p.stem().string();

	saveFromPathProject(path, project);
}

void loadFromPathProject(std::ifstream& in, Project& project) {

	loadFromPathGeometry(in, project.geometry);
	loadFromPathMesh(in, project.mesh);
	project.mesh.updateAfterLoadingFile();
	loadFromPathSolver(in, project.solver);
	loadEtc(in, project);

}

void loadFromExplorerProject(Project& project) {

	std::wstring path = loadFileDialog();
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathGeometry(in, project.geometry);
	loadFromPathMesh(in, project.mesh);
	loadFromPathSolver(in, project.solver);
	loadEtc(in, project);
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
		solver.convectionScheme,
		solver.saveKeyFrameIter,
		solver.f,
		solver.configSimple
	);

	// per-residual display settings (v2): type / norm / scaling / enabled for each
	writeResidualConfigs(out, solver);

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
		if (readVar(in, version)) {
			if (version == solverFileVersion) {
				ok = readCurrentSolverPayload(in, solver);   // v2: per-residual settings
			}
			else if (version == 1u) {
				ok = readV1SolverPayload(in, solver);        // v1: global residual, migrated
			}
		}
	}
	else {
		in.clear();
		in.seekg(start);

		const std::streamoff bytesLeft = remainingBytes(in);

		// pre-magic files use the v1 (single global residual) layout
		if (bytesLeft == currentSolverPayloadSize()) {
			ok = readV1SolverPayload(in, solver);
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

}

void loadFromPathResults(std::ifstream& in, Results& results) {

}

void loadAtLaunch(Project& project, AppSettings& settings) {

	const char* projectFile = "project_settings.bin";
	std::ifstream in(projectFile, std::ios::binary);

	if (!in) return;

	loadSettings(in ,settings);

	in.close();
	//std::printf("%ls\n", settings.quickLaunch.c_str());

	if (!settings.quickLaunch.empty())
	{	
		std::ifstream in(std::filesystem::path(settings.quickLaunch), std::ios::binary);
		loadFromPathProject(in, project);
		
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

