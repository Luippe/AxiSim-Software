#include "file_manager.h"

#include <nfd.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#include "project.h"
#include "mesh.h"

#include "solver_struct.h"
#include "boundary_struct.h"
#include "app_struct.h"		// AppSettings (complete type for serialization)

#include "keyboard_manager.h"
#include "memory_manager.h"
#include "printer.h"

using namespace Shortcuts;

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
	constexpr std::uint32_t sceneViewFileMagic = 0x57565641u;  // "AVVW" little-endian
	constexpr std::uint32_t sceneViewFileVersion = 1u;

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
	#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) {
			return std::filesystem::current_path();
		}
		return std::filesystem::path(buffer).parent_path();
	#elif defined(__APPLE__)
		std::uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::vector<char> buffer(size);
		if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
			return std::filesystem::current_path();
		}
		return std::filesystem::weakly_canonical(buffer.data()).parent_path();
	#elif defined(__linux__)
		std::vector<char> buffer(1024);
		for (;;) {
			const ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size());
			if (len < 0) {
				return std::filesystem::current_path();
			}
			if (static_cast<size_t>(len) < buffer.size()) {
				return std::filesystem::path(
					std::string(buffer.data(), static_cast<size_t>(len))
				).parent_path();
			}
			buffer.resize(buffer.size() * 2);
		}
	#else
		return std::filesystem::current_path();
	#endif
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
namespace {

	struct DialogSpec {
		const nfdu8filteritem_t* filters;
		nfdfiltersize_t filterCount;
		const char* defaultExtension;
	};

	const nfdu8filteritem_t projectFilters[] = {
		{ "AxiSim Project", "axi" },
		{ "Legacy Binary", "bin" }
	};
	const nfdu8filteritem_t geometryFilters[] = {
		{ "AxiSim Geometry", "axigeom" },
		{ "Legacy Binary", "bin" }
	};
	const nfdu8filteritem_t meshFilters[] = {
		{ "AxiSim Mesh", "aximesh" },
		{ "Legacy Binary", "bin" }
	};
	const nfdu8filteritem_t solverFilters[] = {
		{ "AxiSim Solver", "axislv" },
		{ "Legacy Binary", "bin" }
	};
	#ifdef _WIN32
	const nfdu8filteritem_t animationFilters[] = {
		{ "MP4 Video", "mp4" },
		{ "PNG Sequence", "png" }
	};
	#else
	const nfdu8filteritem_t animationFilters[] = {
		{ "PNG Sequence", "png" }
	};
	#endif

	class NfdSession {
	public:
		NfdSession() : initialized(NFD_Init() == NFD_OKAY) {
			if (!initialized) {
				const char* error = NFD_GetError();
				std::cerr << "Failed to initialize file dialogs: "
					<< (error ? error : "unknown error") << '\n';
			}
		}

		~NfdSession() {
			if (initialized) {
				NFD_Quit();
			}
		}

		bool ready() const { return initialized; }

	private:
		bool initialized = false;
	};

	NfdSession& nfdSession() {
		static NfdSession session;
		return session;
	}

	// Each kind gets its own extension so a project, geometry, mesh and solver file
	// are distinguishable in explorer, and so the load dialog stops offering files of
	// the wrong type -- the loaders read raw structs and never validate what they got.
	// ".bin" stays as a second filter entry: every save made before this change used it.
	DialogSpec dialogSpec(FileKind kind) {

		switch (kind) {

		case FileKind::Geometry:
			return { geometryFilters, 2, "axigeom" };

		case FileKind::Mesh:
			return { meshFilters, 2, "aximesh" };

		case FileKind::Solver:
			return { solverFilters, 2, "axislv" };

		case FileKind::Animation:
		#ifdef _WIN32
			return { animationFilters, 2, "mp4" };
		#else
			return { animationFilters, 1, "png" };
		#endif

		case FileKind::Project:
		default:
			return { projectFilters, 2, "axi" };
		}
	}

	std::wstring dialogPathToWide(nfdu8char_t* outPath, const char* defaultExtension) {
		if (!outPath) {
			return L"";
		}

		std::filesystem::path path = std::filesystem::u8path(outPath);
		NFD_FreePathU8(outPath);

		if (defaultExtension && !path.has_extension()) {
			path.replace_extension(std::string(".") + defaultExtension);
		}

		return path.wstring();
	}

	void reportDialogError() {
		const char* error = NFD_GetError();
		std::cerr << "File dialog failed: " << (error ? error : "unknown error") << '\n';
	}
}

std::wstring saveFileDialog(FileKind kind) {
	const DialogSpec spec = dialogSpec(kind);
	if (!nfdSession().ready()) {
		return L"";
	}

	nfdu8char_t* outPath = nullptr;
	const nfdresult_t result = NFD_SaveDialogU8(
		&outPath,
		spec.filters,
		spec.filterCount,
		nullptr,
		nullptr
	);

	if (result == NFD_OKAY) {
		return dialogPathToWide(outPath, spec.defaultExtension);
	}
	if (result == NFD_ERROR) {
		reportDialogError();
	}
	return L"";
}

std::wstring loadFileDialog(FileKind kind) {
	const DialogSpec spec = dialogSpec(kind);
	if (!nfdSession().ready()) {
		return L"";
	}

	nfdu8char_t* outPath = nullptr;
	const nfdresult_t result = NFD_OpenDialogU8(
		&outPath,
		spec.filters,
		spec.filterCount,
		nullptr
	);

	if (result == NFD_OKAY) {
		return dialogPathToWide(outPath, nullptr);
	}
	if (result == NFD_ERROR) {
		reportDialogError();
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

// Projection and rotation style for the results scene. Written last, after the
// results block, and read back the same way. It carries its own magic rather
// than relying on "is there anything left": everything before it is
// variable-length, so byte counting cannot tell an absent block from the tail
// of the previous one. A project saved before this existed simply fails the
// magic check and keeps the defaults.
void saveSceneView(std::ofstream& out, const Project& project) {

	writeAll(out, sceneViewFileMagic, sceneViewFileVersion);
	writeAll(out, project.sceneView.projection, project.sceneView.rotationStyle);
}

void loadSceneView(std::ifstream& in, Project& project) {

	// whatever happens, the camera gets told what to use -- an older project
	// that has no block means the defaults, not whatever the last one left
	project.sceneView = SceneViewSettings{};
	project.applySceneViewSettings = true;

	const std::streampos start = in.tellg();

	auto bail = [&]() {
		in.clear();
		if (start != std::streampos(-1)) {
			in.seekg(start);
		}
		project.sceneView = SceneViewSettings{};
	};

	constexpr std::streamoff blockBytes =
		2 * sizeof(std::uint32_t) + 2 * sizeof(std::uint8_t);

	if (start == std::streampos(-1) || remainingBytes(in) < blockBytes) {
		bail();
		return;
	}

	std::uint32_t magic = 0;
	std::uint32_t version = 0;

	if (!readAll(in, magic, version) ||
		magic != sceneViewFileMagic ||
		version != sceneViewFileVersion) {
		bail();
		return;
	}

	if (!readAll(in, project.sceneView.projection, project.sceneView.rotationStyle)) {
		bail();
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
	saveFromPathResults(out, project.results);
	saveSceneView(out, project);
	//saveKeyboardShortcuts(out);
	out.close();
}


void saveFromExplorerProject(Project& project) {

	std::wstring path = saveFileDialog(FileKind::Project);
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

	// Must run after rebuildMultiBlockAfterLoad: the results rebuild resamples a
	// multiblock solution through mesh.buildMultiBlockRasterMap(), which needs the
	// blocks to exist. Only restores CPU data and raises pendingRebuild -- the GUI
	// does the GL-dependent half on its next frame.
	loadFromPathResults(in, project.results);

	// last block in the file, so it reads from wherever the results block left
	// the stream -- including the rewound position an absent one leaves behind
	loadSceneView(in, project);
}

void loadFromExplorerProject(Project& project) {

	std::wstring path = loadFileDialog(FileKind::Project);
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

	std::wstring path = saveFileDialog(FileKind::Geometry);
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

void loadFromExplorerGeometry(Geometry& geometry) {

	std::wstring path = loadFileDialog(FileKind::Geometry);
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathGeometry(in, geometry);

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

	std::wstring path = saveFileDialog(FileKind::Mesh);
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

	std::wstring path = loadFileDialog(FileKind::Mesh);
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

	// mesh.nextGroupID has always been in this block, but nothing allocated from it
	// until boundary group IDs were made monotonic -- so every save written before
	// that carries 0 while its groups are numbered 0..n. Raising the counter past the
	// loaded groups migrates those files in place (no format change) and keeps the
	// invariant honest for new ones: the next group created after a load must not
	// reuse an ID some segment, face, or GUI selection already refers to.
	for (const BoundarySegmentGroup& group : mesh.boundaryGroups) {
		if (group.id >= mesh.nextGroupID) {
			mesh.nextGroupID = group.id + 1;
		}
	}

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

	std::wstring path = saveFileDialog(FileKind::Solver);
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

	std::wstring path = loadFileDialog(FileKind::Solver);
	if (path.empty()) return;

	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	loadFromPathSolver(in, solver);
}

// ====================================================
// -------------------RESULTS--------------------------
// ====================================================

// SolutionField owns three vectors, so the generic memcpy writeVar would persist
// pointers. Declared in file_manager.h ahead of the container templates.
void writeVar(std::ofstream& out, const SolutionField& value) {
	writeAll(out, value.field, value.dr, value.dz, value.boundaryVariable);
}

bool readVar(std::ifstream& in, SolutionField& value) {
	return readAll(in, value.field, value.dr, value.dz, value.boundaryVariable);
}

namespace {
	constexpr std::uint32_t resultsFileMagic = 0x53525641u;   // "AVRS" little-endian
	constexpr std::uint32_t resultsFileVersion = 2u;

	// Version 1 stored the grid and the dimensions derived from it ahead of the field
	// tables. Both are re-taken from the live mesh by Results::rebuildAfterLoad, so v2
	// stopped writing them -- but a v1 file still has those bytes sitting there, and
	// reading it with the v2 layout would take the grid's nr/nz as the fieldType length
	// prefix. Kept so projects saved before the format changed still load.
	constexpr std::uint32_t resultsFileVersionWithGrid = 1u;

	// Step the stream past a version-1 grid block and throw the values away.
	bool skipLegacyResultsGrid(std::ifstream& in) {

		GridConfig g;
		int nseg = 0;
		int nr = 0;
		int nz = 0;
		std::vector<double> dr;
		std::vector<double> dz;

		return readAll(in, g.nr, g.nz, g.R, g.L, g.r, g.z, g.rFace, g.zFace, g.dr, g.dz)
			&& readAll(in, nseg, nr, nz, dr, dz);
	}

	// Clamp an enum loaded from disk into the range of the GUI name table it indexes.
	// Pass the table itself so the bound cannot drift from the array the combo draws.
	template <typename E, typename Table>
	void clampEnum(E& value, const Table& nameTable) {
		if ((int)value < 0 || (int)value >= (int)std::size(nameTable)) {
			value = (E)0;
		}
	}
}

void saveFromPathResults(std::ofstream& out, const Results& results) {

	writeAll(out, resultsFileMagic, resultsFileVersion);

	// Nothing solved yet: write the header and stop, so the reader still finds a
	// well-formed (empty) block instead of having to guess from the byte count.
	const std::uint8_t hasResults = (results.isReady && !results.fieldType.empty()) ? 1u : 0u;
	writeAll(out, hasResults);

	if (!hasResults) {
		return;
	}

	// The grid (results.g) and everything derived from it -- nseg, nr, nz, dr, dz --
	// is deliberately NOT written. buildField resamples against the LIVE mesh.g, so
	// rebuildAfterLoad re-takes all of it from the mesh exactly as copyData does;
	// anything stored here would be read and then immediately overwritten.
	writeAll(
		out,
		results.fieldType,
		results.shownFields,
		results.solutions
	);

	// display state. The enums are plain enum class over int and the flags are bool,
	// so the generic trivially-copyable writeVar handles them directly -- same as
	// mesh.currentMeshType and solver.currentVelocitySolver elsewhere in this file.
	writeAll(
		out,
		results.currentItem,
		results.currentShadingType,
		results.currentCompareType,
		results.currentColorRangeMode,
		results.filterValues,
		results.show,
		results.showOutline,
		results.isMultipleInstancing
	);

	// Transient playback. Only each frame's solutions are written; its Fields are
	// derived (and hold no GL texture anyway), so rebuildAfterLoad regenerates them
	// through the same buildField path createAnimationFrames used.
	writeAll(out, results.currentAnimationFrame, results.animationRanges);

	const size_t frameCount = results.animationFrames.size();
	writeVar(out, frameCount);

	for (const Results::AnimationFrame& frame : results.animationFrames) {
		writeAll(out, frame.time, frame.solutions);
	}
}

void loadFromPathResults(std::ifstream& in, Results& results) {

	results.reset();

	// Projects saved before results were persisted end right after saveEtc. Rewind and
	// leave the stream where it was so those still load with an empty Results panel.
	const std::streampos start = in.tellg();

	auto bail = [&]() {
		in.clear();
		in.seekg(start);
		results.reset();
	};

	// A failed tellg gives bail() nothing to seek back to, so treat it as "no block"
	// up front -- same guard loadFromPathSolver uses.
	if (start == std::streampos(-1) ||
		remainingBytes(in) < (std::streamoff)(2 * sizeof(std::uint32_t))) {
		bail();
		return;
	}

	std::uint32_t magic = 0;
	std::uint32_t version = 0;

	if (!readAll(in, magic, version) ||
		magic != resultsFileMagic ||
		version == 0 ||
		version > resultsFileVersion) {
		bail();
		return;
	}

	std::uint8_t hasResults = 0;
	if (!readAll(in, hasResults) || hasResults == 0) {
		return;
	}

	// Everything from here on is common to both versions; only the grid block that v1
	// wrote ahead of it has to be stepped over.
	if (version == resultsFileVersionWithGrid && !skipLegacyResultsGrid(in)) {
		bail();
		return;
	}

	size_t frameCount = 0;

	const bool ok =
		readAll(
			in,
			results.fieldType,
			results.shownFields,
			results.solutions
		) &&
		readAll(
			in,
			results.currentItem,
			results.currentShadingType,
			results.currentCompareType,
			results.currentColorRangeMode,
			results.filterValues,
			results.show,
			results.showOutline,
			results.isMultipleInstancing
		) &&
		readAll(in, results.currentAnimationFrame, results.animationRanges) &&
		readVar(in, frameCount);

	if (!ok) {
		bail();
		return;
	}

	// A truncated or hand-edited file must not index past the name tables the GUI
	// draws from.
	clampEnum(results.currentShadingType, results.shadingType);
	clampEnum(results.currentCompareType, results.compareType);
	clampEnum(results.currentColorRangeMode, results.colorRangeModeType);

	if (results.currentItem < 0 || results.currentItem >= (int)results.fieldType.size()) {
		results.currentItem = 0;
	}

	// A truncated file can hand us a garbage frame count, and each frame is large --
	// resizing to it up front would try to allocate the whole bogus amount before the
	// first failed read. Every frame costs at least a time and a map size on disk, so
	// the bytes left are a hard ceiling; grow one frame at a time under it.
	constexpr std::streamoff minFrameBytes = (std::streamoff)(sizeof(double) + sizeof(size_t));

	if ((std::streamoff)frameCount > remainingBytes(in) / minFrameBytes) {
		bail();
		return;
	}

	results.animationFrames.reserve(frameCount);

	for (size_t i = 0; i < frameCount; i++) {
		Results::AnimationFrame frame;
		if (!readAll(in, frame.time, frame.solutions)) {
			bail();
			return;
		}
		results.animationFrames.push_back(std::move(frame));
	}

	// Fields, GL textures and the cylinder template are rebuilt on the GUI thread.
	results.pendingRebuild = true;
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
