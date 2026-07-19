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
#include <cstring>
#include <filesystem>
#include <iostream>

#include "project.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"
#include "app_struct.h"		// AppSettings (complete type for serialization)

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
	// v3: residual display settings (type/norm/scaling/enabled) are stored per-residual.
	// v4: adds gradientScheme, which v3 never wrote -- the Pressure Gradient combo
	//     silently reverted to the default on every load.
	// v3 is still readable (see readSolverPayload): dropping it would throw away the
	// rest of the solver setup in every project saved before this.
	// Legacy (v1/v2/pre-magic) loaders were removed.
	constexpr std::uint32_t solverFileVersion = 4u;
	constexpr std::uint32_t solverFileVersionNoGradientScheme = 3u;
	constexpr std::uint32_t meshRegionFileMagic = 0x494F5241u; // "AROI" little-endian
	constexpr std::uint32_t meshRegionFileVersion = 1u;

	// Region vectors were originally raw-copied, including this struct's padding.
	// Keep the old layout available so existing project files can be upgraded to
	// the field-wise format that stores overrideBoundarySpacing explicitly.
	struct LegacyMeshRegionOfInfluence {
		int id = -1;
		bool enabled = true;
		MeshRegionShape shape = MeshRegionShape::Circle;
		Vec2 center{ 0.0, 0.0 };
		double radius = 0.1;
		Vec2 min{ 0.0, 0.0 };
		Vec2 max{ 0.0, 0.0 };
		double targetSpacing = 0.01;
		double outsideSpacing = 0.0;
		double transitionThickness = 0.0;
	};
	static_assert(sizeof(LegacyMeshRegionOfInfluence) == 96);

	// Directory that holds the running executable. Bundled resources (presets) are
	// resolved against this rather than the current working directory, so loading
	// works no matter where the app is launched from. Falls back to the CWD if the
	// exe path can't be read.
	std::filesystem::path executableDir() {
		wchar_t buffer[MAX_PATH];
		DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) {
			return std::filesystem::current_path();
		}
		return std::filesystem::path(buffer).parent_path();
	}

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

	// Fixed on-disk order of the named residuals for the v2 payload. Do NOT reorder:
	// the file format depends on it. Each name must match a key inserted by
	// Solver::initConfigResiduals so the loaded settings land on the right entry.
	static const char* const kResidualOrder[] = {
		"U", "V", "Continuity", "Temperature", "Concentration"
	};

	void clampResidualSettings(ResidualType& type, ResidualNormType& norm, ResidualScalingType& scale) {
		if ((int)type < (int)RESIDUAL_SCALED || (int)type > (int)RESIDUAL_RMS) {
			type = RESIDUAL_RAW;
		}
		if ((int)norm < (int)RESIDUAL_L1 || (int)norm > (int)RESIDUAL_LINF) {
			norm = RESIDUAL_LINF;
		}
		if ((int)scale < (int)RESIDUAL_SCALING_NONE || (int)scale > (int)RESIDUAL_SCALING_DIAGONAL) {
			scale = RESIDUAL_SCALING_NONE;
		}
	}

	// Write one residual's settings into its live map entry, if present. The map is
	// rebuilt with every name by Solver::initConfigResiduals before a load, and the
	// coeff reference each entry holds must stay bound, so we only touch value fields.
	void applyResidualSettings(Solver& solver, const char* name,
		ResidualType type, ResidualNormType norm, ResidualScalingType scale, bool enabled, double tol) {

		clampResidualSettings(type, norm, scale);

		auto it = solver.cfg.find(name);
		if (it == solver.cfg.end()) {
			return;
		}

		it->second.type = type;
		it->second.normType = norm;
		it->second.scaleType = scale;
		it->second.enabled = enabled;
		it->second.tol = tol;
	}

	// v3 residual block: type / norm / scaling / enabled / tolerance for each residual, in kResidualOrder.
	void writeResidualConfigs(std::ofstream& out, const Solver& solver) {
		for (const char* name : kResidualOrder) {
			ResidualType        type    = RESIDUAL_RAW;
			ResidualNormType    norm    = RESIDUAL_LINF;
			ResidualScalingType scale   = RESIDUAL_SCALING_NONE;
			bool                enabled = false;
			double              tol     = 0.001;

			auto it = solver.cfg.find(name);
			if (it != solver.cfg.end()) {
				type    = it->second.type;
				norm    = it->second.normType;
				scale   = it->second.scaleType;
				enabled = it->second.enabled;
				tol     = it->second.tol;
			}

			writeAll(out, type, norm, scale, enabled, tol);
		}
	}

	bool readResidualConfigs(std::ifstream& in, Solver& solver) {
		for (const char* name : kResidualOrder) {
			ResidualType        type    = RESIDUAL_SCALED;
			ResidualNormType    norm    = RESIDUAL_LINF;
			ResidualScalingType scale   = RESIDUAL_SCALING_DIAGONAL;
			bool                enabled = false;
			double              tol     = 0.001;

			if (!readAll(in, type, norm, scale, enabled, tol)) {
				return false;
			}

			applyResidualSettings(solver, name, type, norm, scale, enabled, tol);
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

		// useNonOrthCorrector used to be `int nNonOrthCorrectors` at this same
		// offset, so an old save can leave a byte other than 0/1 here (a saved
		// pass count of 2 lands as 0x02). Reading such a bool directly is UB, so
		// normalize it through a byte copy: any nonzero count means "corrector on".
		{
			unsigned char raw = 0;
			std::memcpy(&raw, &solver.configSimple.useNonOrthCorrector, 1);
			const bool on = raw != 0;
			std::memcpy(&solver.configSimple.useNonOrthCorrector, &on, 1);
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

		if ((int)solver.gradientScheme < 0 ||
			(int)solver.gradientScheme > (int)GRAD_LSQ) {
			solver.gradientScheme = GRAD_LSQ;
		}

		// residual display settings are now per-residual; clamp each entry in place
		for (auto& entry : solver.cfg) {
			ConfigResidual& cfg = entry.second;
			clampResidualSettings(cfg.type, cfg.normType, cfg.scaleType);
		}

		if ((int)solver.convectionScheme < (int)CONV_UPWIND ||
			(int)solver.convectionScheme > (int)CONV_SECOND_ORDER_UPWIND) {
			solver.convectionScheme = CONV_UPWIND;
		}

		// timeScheme occupies a byte that was plain struct padding before it existed,
		// so a project saved by an older build supplies whatever the writer's padding
		// held. Anything outside the enum falls back to first order, which is exactly
		// the behavior those projects were saved with.
		if ((int)solver.configSolver.timeScheme < (int)TimeScheme::TIME_FIRST_ORDER ||
			(int)solver.configSolver.timeScheme > (int)TimeScheme::TIME_SECOND_ORDER) {
			solver.configSolver.timeScheme = TimeScheme::TIME_FIRST_ORDER;
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

	// Per-residual display settings follow the common block (see writeResidualConfigs).
	// The v2 EnabledResiduals block is gone — plot-enable rides along per residual.
	//
	// `hasGradientScheme` distinguishes v4 from v3. The payload is positional, so a
	// v3 file has nothing where gradientScheme sits and reading one would desync
	// every field after it; v3 keeps the constructor default instead.
	bool readSolverPayload(std::ifstream& in, Solver& solver, bool hasGradientScheme) {
		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
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

		if (hasGradientScheme && !readVar(in, solver.gradientScheme)) {
			return false;
		}

		return readResidualConfigs(in, solver);
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

void writeMeshRegions(
	std::ofstream& out,
	int nextRegionID,
	const std::vector<MeshRegionOfInfluence>& regions
) {
	writeAll(
		out,
		nextRegionID,
		meshRegionFileMagic,
		meshRegionFileVersion
	);

	size_t size = regions.size();
	writeVar(out, size);

	for (const MeshRegionOfInfluence& region : regions) {
		writeAll(
			out,
			region.id,
			region.enabled,
			region.shape,
			region.center,
			region.radius,
			region.min,
			region.max,
			region.targetSpacing,
			region.outsideSpacing,
			region.transitionThickness,
			region.overrideBoundarySpacing
		);
	}
}

bool readMeshRegions(
	std::ifstream& in,
	int& nextRegionID,
	std::vector<MeshRegionOfInfluence>& regions
) {
	std::streampos start = in.tellg();
	std::uint32_t storedNextRegionID = 0;

	if (!readVar(in, storedNextRegionID)) {
		nextRegionID = 0;
		regions.clear();
		return false;
	}

	// Projects from before ROI persistence continue directly with the solver
	// payload. Leave its magic untouched for loadFromPathSolver.
	if (storedNextRegionID == solverFileMagic) {
		in.clear();
		in.seekg(start);
		nextRegionID = 0;
		regions.clear();
		return true;
	}

	nextRegionID = static_cast<int>(storedNextRegionID);
	std::streampos payloadStart = in.tellg();
	std::uint32_t marker = 0;

	if (!readVar(in, marker)) {
		regions.clear();
		return false;
	}

	if (marker == meshRegionFileMagic) {
		std::uint32_t version = 0;
		size_t size = 0;

		if (!readAll(in, version, size) ||
			version != meshRegionFileVersion ||
			size > 1000000) {
			regions.clear();
			return false;
		}

		regions.resize(size);
		for (MeshRegionOfInfluence& region : regions) {
			if (!readAll(
				in,
				region.id,
				region.enabled,
				region.shape,
				region.center,
				region.radius,
				region.min,
				region.max,
				region.targetSpacing,
				region.outsideSpacing,
				region.transitionThickness,
				region.overrideBoundarySpacing
			)) {
				regions.clear();
				return false;
			}
		}

		return true;
	}

	// Legacy format: nextRegionID, vector size, then raw v1 structs.
	in.clear();
	in.seekg(payloadStart);

	size_t size = 0;
	if (!readVar(in, size) || size > 1000000) {
		regions.clear();
		return false;
	}

	regions.clear();
	regions.reserve(size);

	for (size_t i = 0; i < size; i++) {
		LegacyMeshRegionOfInfluence legacy{};
		if (!readVar(in, legacy)) {
			regions.clear();
			return false;
		}

		MeshRegionOfInfluence region{};
		region.id = legacy.id;
		region.enabled = legacy.enabled;
		region.shape = legacy.shape;
		region.center = legacy.center;
		region.radius = legacy.radius;
		region.min = legacy.min;
		region.max = legacy.max;
		region.targetSpacing = legacy.targetSpacing;
		region.outsideSpacing = legacy.outsideSpacing;
		region.transitionThickness = legacy.transitionThickness;
		region.overrideBoundarySpacing = false;
		regions.push_back(region);
	}

	return true;
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

	// Multiblock per-band resolution. The blocks themselves are rebuilt from the
	// sketch on load, but these cell counts are user-edited state, so persist them.
	// Appended at the very end of the file (nothing follows), so the guarded read in
	// loadEtc lets older saves that lack it still load.
	writeAll(
		out,
		project.mesh.zBandCells,
		project.mesh.rBandCells
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

	// Multiblock per-band resolution (appended after lengthScale; guard so older
	// saves without it still load -- empty vectors let ensureBandSizes default to 20).
	if (remainingBytes(in) >= static_cast<std::streamoff>(2 * sizeof(size_t))) {
		readAll(in, project.mesh.zBandCells, project.mesh.rBandCells);
	}
	else {
		project.mesh.zBandCells.clear();
		project.mesh.rBandCells.clear();
	}
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

	//// Reconstruct the (non-serialized) multiblock from the now-loaded sketch and
	//// per-band cell counts (loadEtc), so the inspector and solver see the multiblock
	//// instead of falling back to the raster grid. Must run after loadEtc, which loads
	//// the band cells buildStructuredMultiBlock consumes.
	project.mesh.rebuildMultiBlockAfterLoad(project.geometry.sketch);
}

void loadFromExplorerProject(Project& project) {

	std::wstring path = loadFileDialog();
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathProject(in, project);

}

void loadPresetProject(const std::string& fileName, Project& project) {

	// presets ship next to the exe (see the POST_BUILD copy in CMakeLists.txt),
	// so anchor to the exe directory instead of the working directory.
	std::filesystem::path path = executableDir() / "presets" / fileName;

	std::ifstream in(path, std::ios::binary);

	if (!in) return;

	loadFromPathProject(in, project);

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

	writeMeshRegions(
		out,
		mesh.nextRegionOfInfluenceID,
		mesh.regionsOfInfluence
	);
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

	if (!readMeshRegions(
		in,
		mesh.nextRegionOfInfluenceID,
		mesh.regionsOfInfluence
	)) {
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
		solver.configSolver,
		solver.currentVelocitySolver,
		solver.convectionScheme,
		solver.saveKeyFrameIter,
		solver.f,
		solver.configSimple,
		solver.gradientScheme	// v4; readSolverPayload skips this for a v3 file
	);

	// per-residual display settings: type / norm / scaling / enabled for each
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
				ok = readSolverPayload(in, solver, true);
			}
			else if (version == solverFileVersionNoGradientScheme) {
				// Pre-gradientScheme save: everything else still reads, and the
				// scheme keeps its default rather than costing the user the whole
				// solver setup.
				ok = readSolverPayload(in, solver, false);
			}
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

}

void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc) {
	int type = (int)(bc.type());
	double value = bc.value();

	out.write((const char*)&type, sizeof(type));
	out.write((const char*)&value, sizeof(value));

	if (const auto* pulsatile = std::get_if<PulsatileParams>(&bc.params)) {
		out.write((const char*)&pulsatile->amplitude, sizeof(pulsatile->amplitude));
		out.write((const char*)&pulsatile->frequency, sizeof(pulsatile->frequency));
	}
}

void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc) {
	int type = 0;
	double value = 0.0;

	in.read((char*)&type, sizeof(type));
	in.read((char*)&value, sizeof(value));

	bc.setType((BCType)(type));
	bc.setValue(value);

	if (auto* pulsatile = std::get_if<PulsatileParams>(&bc.params)) {
		in.read((char*)&pulsatile->amplitude, sizeof(pulsatile->amplitude));
		in.read((char*)&pulsatile->frequency, sizeof(pulsatile->frequency));
	}
}

std::ofstream openBinaryFile(const char* path) {
	return std::ofstream(path, std::ios::binary);
}
