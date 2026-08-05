#include "file_manager.h"

#include <nfd.h>

#include <string>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <vector>

#include "project.h"
#include "mesh.h"


#include "solver_struct.h"
#include "boundary_struct.h"
#include "app_struct.h"		// AppSettings (complete type for serialization)

#include "file_converter.h"
#include "keyboard_manager.h"
#include "printer.h"
#include "resource_path.h"

using namespace Shortcuts;

namespace {
	constexpr std::uint32_t solverFileMagic = 0x53585641u; // "AXVS" little-endian
	// v3: residual display settings (type/norm/scaling/enabled) are stored per-residual.
	// v4: adds gradientScheme, which v3 never wrote -- the Pressure Gradient combo
	//     silently reverted to the default on every load.
	// v5: the velocity solver, the two schemes, saveKeyFrameIter and (out of
	//     ConfigSimple) useNonOrthCorrector all moved INTO ConfigSolver, and every
	//     solver enum became uint8_t-backed. That reshapes both config blobs -- one
	//     grew fields, the other lost its trailing bool and shrank 32 -> 24 -- so v5
	//     is a new format rather than an appended field.
	// v3 and v4 are still readable (see readSolverPayloadLegacy): dropping them would
	// throw away the rest of the solver setup in every project saved before this.
	// Legacy (v1/v2/pre-magic) loaders were removed.
	constexpr std::uint32_t solverFileVersion = 5u;
	constexpr std::uint32_t solverFileVersionSplitConfig = 4u;
	constexpr std::uint32_t solverFileVersionNoGradientScheme = 3u;
	constexpr std::uint32_t meshRegionFileMagic = 0x494F5241u; // "AROI" little-endian
	constexpr std::uint32_t meshRegionFileVersion = 1u;
	constexpr std::uint32_t sceneViewFileMagic = 0x57565641u;  // "AVVW" little-endian
	constexpr std::uint32_t sceneViewFileVersion = 1u;

	// The 32-byte ConfigSolver a v3/v4 solver file carries, in that layout. The live
	// struct has grown five fields and narrowed its enums to a byte each, so it can no
	// longer be read over those bytes -- this stands in, and readSolverPayloadLegacy
	// maps it across along with the loose fields that followed it.
	struct LegacyConfigSolverV4 {
		std::int32_t  type = 0;                 // LinearSolverType, int-width back then
		std::int32_t  maxIter = 20;             // now ConfigSolver::linearMaxIter
		bool          addConvectionTerm = true;
		bool          transient = false;
		std::uint8_t  timeScheme = 0;
		std::uint8_t  pad0 = 0;
		std::uint32_t pad1 = 0;
		double        dt = 0.1;
		double        tEnd = 2.0;
	};
	static_assert(sizeof(LegacyConfigSolverV4) == 32,
		"LegacyConfigSolverV4 must match the v3/v4 on-disk ConfigSolver exactly");

	// Likewise for ConfigSimple, which carried the non-orthogonal corrector until v5
	// and so was 32 bytes rather than the 24 it is now.
	//
	// That trailing byte is read as a uint8_t, not a bool, on purpose: it started life
	// as `int nNonOrthCorrectors`, a pass count, so a v3/v4 file can legitimately hold
	// 2 or 3 there. Loading such a byte straight into a bool is UB, hence the explicit
	// `!= 0` in readSolverPayloadLegacy -- any nonzero count means "corrector on".
	struct LegacyConfigSimpleV4 {
		std::int32_t  maxIter = 50;
		std::int32_t  checkConv = 1;
		double        momTol = 1e-8;
		double        ppTol = 1e-5;
		std::uint8_t  useNonOrthCorrector = 0;
		std::uint8_t  pad0[7] = {};
	};
	static_assert(sizeof(LegacyConfigSimpleV4) == 32,
		"LegacyConfigSimpleV4 must match the v3/v4 on-disk ConfigSimple exactly");

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

	// True when `value` is one of the enumerators of E, given that E starts at 0 and
	// runs contiguously up to `last`. Every enum these loaders touch is declared that
	// way, and all of them are uint8_t-backed now, so a stored byte only has to be
	// checked against the top -- it cannot come back negative.
	template <typename E>
	bool enumInRange(E value, E last) {
		return (int)value >= 0 && (int)value <= (int)last;
	}

	void clampResidualSettings(ResidualType& type, ResidualNormType& norm, ResidualScalingType& scale) {
		if (!enumInRange(type, ResidualType::RESIDUAL_RMS)) {
			type = ResidualType::RESIDUAL_RAW;
		}
		if (!enumInRange(norm, ResidualNormType::RESIDUAL_LINF)) {
			norm = ResidualNormType::RESIDUAL_LINF;
		}
		if (!enumInRange(scale, ResidualScalingType::RESIDUAL_SCALING_DIAGONAL)) {
			scale = ResidualScalingType::RESIDUAL_SCALING_NONE;
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

	// Residual block: type / norm / scaling / enabled / tolerance for each residual, in
	// kResidualOrder. The three enums are one byte each as of v5 (they were four in
	// v3/v4 -- see readResidualConfigsLegacy).
	void writeResidualConfigs(std::ofstream& out, const Solver& solver) {
		for (const char* name : kResidualOrder) {
			ResidualType        type    = ResidualType::RESIDUAL_RAW;
			ResidualNormType    norm    = ResidualNormType::RESIDUAL_LINF;
			ResidualScalingType scale   = ResidualScalingType::RESIDUAL_SCALING_NONE;
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
			ResidualType        type    = ResidualType::RESIDUAL_SCALED;
			ResidualNormType    norm    = ResidualNormType::RESIDUAL_LINF;
			ResidualScalingType scale   = ResidualScalingType::RESIDUAL_SCALING_DIAGONAL;
			bool                enabled = false;
			double              tol     = 0.001;

			if (!readAll(in, type, norm, scale, enabled, tol)) {
				return false;
			}

			applyResidualSettings(solver, name, type, norm, scale, enabled, tol);
		}

		return true;
	}

	// Same block as above, but with the int-width enums a v3/v4 file wrote. Reading it
	// with the v5 layout would take 3 bytes per record too few and desync the rest.
	// applyResidualSettings clamps whatever the narrowing produces.
	bool readResidualConfigsLegacy(std::ifstream& in, Solver& solver) {
		for (const char* name : kResidualOrder) {
			std::int32_t type    = 0;
			std::int32_t norm    = 0;
			std::int32_t scale   = 0;
			bool         enabled = false;
			double       tol     = 0.001;

			if (!readAll(in, type, norm, scale, enabled, tol)) {
				return false;
			}

			applyResidualSettings(
				solver, name,
				(ResidualType)type,
				(ResidualNormType)norm,
				(ResidualScalingType)scale,
				enabled, tol
			);
		}

		return true;
	}

	void sanitizeSolverConfig(Solver& solver) {
		if (solver.configSolver.linearMaxIter < 1) {
			solver.configSolver.linearMaxIter = 20;
		}

		if (solver.configSolver.saveKeyFrameIter < 1) {
			solver.configSolver.saveKeyFrameIter = 2;
		}

		if (solver.configSimple.maxIter < 1) {
			solver.configSimple.maxIter = 50;
		}

		if (solver.configSimple.checkConv < 1) {
			solver.configSimple.checkConv = 1;
		}

		// The corrector flag needs no normalizing here any more: v5 writes it as a
		// real bool out of ConfigSolver, and the pass-count byte a v3/v4 file can put
		// there is folded to 0/1 by readSolverPayloadLegacy before it ever lands in
		// the struct.

		if (!std::isfinite(solver.configSimple.momTol) ||
			solver.configSimple.momTol <= 0.0) {
			solver.configSimple.momTol = 1e-8;
		}

		if (!std::isfinite(solver.configSimple.ppTol) ||
			solver.configSimple.ppTol <= 0.0) {
			solver.configSimple.ppTol = 1e-5;
		}

		// Every clamp below is against what the GUI can actually OFFER, not against
		// the enum's last enumerator: LINEAR_BICGSTAB / GMRES / SOLVER_SIMPLER /
		// CONV_QUICK exist in the enums but have no implementation behind them, so a
		// file naming one has to come back as something the solver can run.
		if (!enumInRange(solver.configSolver.type, LinearSolverType::LINEAR_GS_RB)) {
			solver.configSolver.type = LinearSolverType::LINEAR_JACOBI;
		}

		if (!enumInRange(solver.configSolver.velocitySolver, VelocitySolverType::SOLVER_SIMPLE)) {
			solver.configSolver.velocitySolver = VelocitySolverType::SOLVER_SIMPLE;
		}

		if (!enumInRange(solver.configSolver.gradientScheme, GradientScheme::GRAD_LSQ)) {
			solver.configSolver.gradientScheme = GradientScheme::GRAD_LSQ;
		}

		// residual display settings are now per-residual; clamp each entry in place
		for (auto& entry : solver.cfg) {
			ConfigResidual& cfg = entry.second;
			clampResidualSettings(cfg.type, cfg.normType, cfg.scaleType);
		}

		if (!enumInRange(solver.configSolver.convectionScheme, ConvectionScheme::CONV_SECOND_ORDER_UPWIND)) {
			solver.configSolver.convectionScheme = ConvectionScheme::CONV_UPWIND;
		}

		// timeScheme occupied a byte that was plain struct padding before it existed,
		// so a project saved by an older build supplies whatever the writer's padding
		// held. Anything outside the enum falls back to first order, which is exactly
		// the behavior those projects were saved with.
		if (!enumInRange(solver.configSolver.timeScheme, TimeScheme::TIME_SECOND_ORDER)) {
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
	bool readSolverPayload(std::ifstream& in, Solver& solver) {
		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			solver.configSolver,
			solver.f,
			solver.configSimple
		);

		if (!ok) {
			return false;
		}

		return readResidualConfigs(in, solver);
	}

	// v3/v4 payload. ConfigSolver was 32 bytes of int-width enums back then, and the
	// four settings it has since absorbed were written as loose ints on either side of
	// the fluid/SIMPLE blocks -- so this reads the old shape and assembles the current
	// struct from the pieces rather than trying to overlay them.
	//
	// `hasGradientScheme` distinguishes v4 from v3. The payload is positional, so a v3
	// file has nothing where gradientScheme sits and reading one would desync every
	// field after it; v3 keeps the default instead.
	bool readSolverPayloadLegacy(std::ifstream& in, Solver& solver, bool hasGradientScheme) {

		LegacyConfigSolverV4 legacy;
		LegacyConfigSimpleV4 legacySimple;
		std::int32_t velocitySolver  = 0;
		std::int32_t convectionScheme = 0;
		std::int32_t saveKeyFrameIter = 2;

		bool ok = readAll(
			in,
			solver.varUnits,
			solver.fieldOption,
			legacy,
			velocitySolver,
			convectionScheme,
			saveKeyFrameIter,
			solver.f,
			legacySimple
		);

		if (!ok) {
			return false;
		}

		std::int32_t gradientScheme = (std::int32_t)GradientScheme::GRAD_LSQ;

		if (hasGradientScheme && !readVar(in, gradientScheme)) {
			return false;
		}

		// Anything out of range here is caught by sanitizeSolverConfig, which the two
		// callers of this function run afterwards.
		solver.configSolver = ConfigSolver{};
		solver.configSolver.velocitySolver   = (VelocitySolverType)velocitySolver;
		solver.configSolver.convectionScheme = (ConvectionScheme)convectionScheme;
		solver.configSolver.gradientScheme   = (GradientScheme)gradientScheme;
		solver.configSolver.type             = (LinearSolverType)legacy.type;
		solver.configSolver.timeScheme       = (TimeScheme)legacy.timeScheme;
		solver.configSolver.linearMaxIter    = legacy.maxIter;
		solver.configSolver.addConvectionTerm = legacy.addConvectionTerm;
		solver.configSolver.transient        = legacy.transient;
		solver.configSolver.dt               = legacy.dt;
		solver.configSolver.tEnd             = legacy.tEnd;
		solver.configSolver.saveKeyFrameIter = saveKeyFrameIter;

		// Crosses structs: the corrector was ConfigSimple's until v5. Nonzero rather
		// than == 1, since the byte may be an old pass count (see LegacyConfigSimpleV4).
		solver.configSolver.useNonOrthCorrector = legacySimple.useNonOrthCorrector != 0;

		solver.configSimple = ConfigSimple{};
		solver.configSimple.maxIter   = legacySimple.maxIter;
		solver.configSimple.checkConv = legacySimple.checkConv;
		solver.configSimple.momTol    = legacySimple.momTol;
		solver.configSimple.ppTol     = legacySimple.ppTol;

		// useMultigrid is new in v5 and keeps the ConfigSolver default: these files
		// predate the flag, and the runs that made them all went through multigrid.

		return readResidualConfigsLegacy(in, solver);
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
	// The third entry is an EXPORT, not a save format: picking it writes an OpenFOAM
	// case folder rather than an AxiSim file, and nothing reads it back. ".foam" is
	// the marker extension ParaView uses for a case directory -- the dict itself is
	// extensionless by OpenFOAM convention, so there is nothing better to filter on,
	// and the chosen stem only ever names the folder.
	const nfdu8filteritem_t meshFilters[] = {
		{ "AxiSim Mesh", "aximesh" },
		{ "Legacy Binary", "bin" },
		{ "OpenFOAM Case (blockMeshDict)", "foam" }
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
	const nfdu8filteritem_t solutionFilters[] = {
		{ "NumPy Solution Export", "npy" }
	};

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
			return { meshFilters, 3, "aximesh" };

		case FileKind::Solver:
			return { solverFilters, 2, "axislv" };

		case FileKind::Animation:
		#ifdef _WIN32
			return { animationFilters, 2, "mp4" };
		#else
			return { animationFilters, 1, "png" };
		#endif

		case FileKind::Solution:
			return { solutionFilters, 1, "npy" };

		case FileKind::Project:
		default:
			return { projectFilters, 2, "axi" };
		}
	}

	// Rewrite a leading \\wsl$\ to \\wsl.localhost\.
	//
	// Both name the same WSL share, but they are separate mounts with separate
	// caches in the P9 redirector, and the \\wsl$ one poisons individual paths: once
	// a create there fails, that exact path keeps answering ERROR_ACCESS_DENIED --
	// surviving the parent being deleted and recreated, and surviving the directory
	// then being made from inside Linux. An OpenFOAM export that lost a race on
	// <case>\0 could therefore never write its 0/ directory again, under that name,
	// ever, while a case one character away exported fine.
	//
	// \\wsl$ is only the deprecated alias for \\wsl.localhost anyway, so preferring
	// the current name costs nothing. The shell hands back whichever one the user's
	// shortcut or typed path used; they should not have to know the difference.
	std::wstring preferWslLocalhost(std::wstring path) {

		const std::wstring legacy = L"\\\\wsl$\\";

		if (path.size() < legacy.size()) {
			return path;
		}

		// UNC host names are not case-sensitive, and every character being matched
		// is either already lower case or unaffected by towlower.
		for (std::size_t i = 0; i < legacy.size(); i++) {
			if ((wchar_t)std::towlower(path[i]) != legacy[i]) {
				return path;
			}
		}

		return path.replace(0, legacy.size(), L"\\\\wsl.localhost\\");
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

		// Every save and open in the app comes through here, so this is the one place
		// the substitution has to happen.
		return preferWslLocalhost(path.wstring());
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
//
// Both fields go out as their own uint8_t underlying value, so the ENUMERATOR
// ORDER of ProjectionType / RotationType (camera.h) is part of the file format:
// append to them, never insert or reorder, or bump sceneViewFileVersion and
// translate on the way in. Today's order matches the byte values the old
// SceneViewSettings mirror wrote (Perspective = 0, Turntable = 0), so projects
// saved before Results owned these still load.
void saveSceneView(std::ofstream& out, const Project& project) {

	writeAll(out, sceneViewFileMagic, sceneViewFileVersion);
	writeAll(out, project.results.projectionType, project.results.rotationType);
}

void loadSceneView(std::ifstream& in, Project& project) {

	// whatever happens, the camera gets told what to use -- an older project
	// that has no block means the defaults, not whatever the last one left
	auto useDefaults = [&]() {
		project.results.projectionType = ProjectionType::Orthographic;
		project.results.rotationType = RotationType::Turntable;
	};

	useDefaults();
	project.results.applySceneViewSettings = true;

	const std::streampos start = in.tellg();

	auto bail = [&]() {
		in.clear();
		if (start != std::streampos(-1)) {
			in.seekg(start);
		}
		useDefaults();
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

	// Read the raw bytes, not the enums: a truncated or hand-edited file can hold
	// any value, and an out-of-range enum would reach both the scene camera and
	// the Advanced Options combo, which indexes its label array by enumerator.
	// The bounds are the last enumerator of each -- extend them if one gains a
	// member.
	std::uint8_t projection = 0;
	std::uint8_t rotation = 0;

	if (!readAll(in, projection, rotation) ||
		projection > static_cast<std::uint8_t>(ProjectionType::Orthographic) ||
		rotation > static_cast<std::uint8_t>(RotationType::Arcball)) {
		bail();
		return;
	}

	project.results.projectionType = static_cast<ProjectionType>(projection);
	project.results.rotationType = static_cast<RotationType>(rotation);
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
	std::filesystem::path path = Resources::executableDir() / "presets" / fileName;

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
bool saveFoamCase(const std::filesystem::path& dir, const Mesh& mesh,
                  const FoamCaseSetup& setup) {

	// Which of the two mesh formats this project can be exported as. They are not
	// interchangeable: a blockMeshDict is a list of HEXES, so only a mesh that
	// decomposes into structured blocks has one, and a triangulated mesh has to go
	// out as a polyMesh instead. Every structured mesh is multiblock, so the mesh
	// type picks the writer -- the blocks are only checked for emptiness.
	const bool multiBlock = mesh.currentMeshType == MeshType::Structured &&
		!mesh.multiBlock.blocks.empty();

	const bool unstructured = !multiBlock &&
		mesh.currentMeshType == MeshType::Unstructured &&
		!mesh.unstructuredTriangles.empty();

	// What is left has nothing to spell: a structured project whose blocks were never
	// built, or an unstructured one with no triangles. Both writers reject an empty
	// mesh far less informatively.
	if (!multiBlock && !unstructured) {
		std::cerr << "saveFoamCase: nothing to export -- generate a multi-block or"
			" unstructured mesh first\n";
		return false;
	}

	// blockMesh looks for the dict at <case>/system/blockMeshDict and nowhere else,
	// so the folder IS the deliverable. Same shape as the .npy and PNG-sequence
	// exports: the dialog names one thing, the export lays out a directory beside it.
	const std::filesystem::path systemDir = dir / "system";

	// The three directories an OpenFOAM case is made of: system/ for the dicts that
	// control the run, constant/ for the fluid, 0/ for the initial and boundary
	// conditions. blockMesh reads only system/; everything else is for the solver.
	const std::filesystem::path constantDir = dir / "constant";
	const std::filesystem::path zeroDir = dir / "0";

	std::error_code ec;
	for (const std::filesystem::path& sub : { systemDir, constantDir, zeroDir }) {
		std::filesystem::create_directories(sub, ec);
		if (ec) {
			std::cerr << "saveFoamCase: cannot create " << sub.string()
				<< " -- " << ec.message() << '\n';
			return false;
		}
	}

	// The patch list is what 0/ is written against, and it comes out of whichever
	// mesh writer ran -- the sanitize + de-collide pass that produced the patch names
	// is not reversible, so the fields have to be built from its result rather than
	// from the boundary groups again.
	std::vector<FoamField> fields;

	if (multiBlock) {

		// boundaryEdges + boundaryVertices are the sketch boundary, and they are what
		// carries the inlet/outlet/wall tags. Block::edgeGroup does not: the trellis
		// decomposition never writes it, so classifying from the blocks alone put every
		// external edge in one untagged patch and the case had no BCs at all.
		const BlockMeshDict dict = blockMeshDictFromMultiblock(
			mesh.multiBlock, mesh.boundaryGroups, mesh.boundaryEdges, mesh.boundaryVertices);

		if (!writeBlockMeshDict(systemDir / "blockMeshDict", dict)) {
			return false;
		}

		fields = initialFieldsFromDict(dict, mesh.boundaryGroups, setup);
	}
	else {

		// The SAME FVMesh the solver runs on -- literally the same instance now, not
		// an identically-built copy. That is the whole point: the exported patches cut
		// the boundary exactly where AxiSim's BC groups do, so a difference between the
		// two solutions is physics and not a face on the wrong patch.
		const FVMesh& fvMesh = mesh.getFVMesh();

		const PolyMesh polyMesh = polyMeshFromUnstructured(
			mesh.unstructuredPoints, mesh.unstructuredTriangles,
			fvMesh, mesh.boundaryGroups);

		if (!writePolyMesh(constantDir / "polyMesh", polyMesh)) {
			return false;
		}

		fields = initialFieldsFromPolyMesh(polyMesh, mesh.boundaryGroups, setup);
	}

	// blockMesh reads system/controlDict before it ever opens the mesh dict, so this
	// is not solver-only paperwork -- without it the export cannot even be meshed.
	if (!writeControlDict(systemDir / "controlDict", setup) ||
		!writeFvSchemes(systemDir / "fvSchemes", setup) ||
		!writeFvSolution(systemDir / "fvSolution", setup) ||
		!writeTransportProperties(constantDir / "transportProperties", setup) ||
		!writeTurbulenceProperties(constantDir / "turbulenceProperties")) {
		return false;
	}

	if (!writeInitialFields(zeroDir, fields)) {
		return false;
	}

	// A polyMesh case has no meshing step: the mesh is already on disk, so running
	// blockMesh on it would fail on a dict that is not there.
	std::cout << "Exported OpenFOAM case to " << dir.string() << " -- run: "
		<< (multiBlock ? "blockMesh -case \"" + dir.string() + "\" && " : "")
		<< (setup.transient ? "pimpleFoam" : "simpleFoam")
		<< " -case \"" << dir.string() << "\"\n";
	return true;
}

// Flatten the run's physics and numerics out of Solver, which is where the export
// stops being a mesh export: everything above this line comes from the geometry,
// everything below from the Solver tab.
FoamCaseSetup foamCaseSetupFromSolver(const Solver& solver) {

	FoamCaseSetup setup;

	setup.rho = solver.f.rho;
	setup.mu  = solver.f.mu;
	setup.D   = solver.f.D;
	setup.cp  = solver.f.cp;
	setup.k   = solver.f.k;

	setup.solveEnergy        = solver.fieldOption.solveEnergy;
	setup.solveConcentration = solver.fieldOption.solveConcentration;

	setup.transient = solver.configSolver.transient;
	setup.secondOrderTime =
		solver.configSolver.timeScheme == TimeScheme::TIME_SECOND_ORDER;
	setup.dt   = solver.configSolver.dt;
	setup.tEnd = solver.configSolver.tEnd;

	// Raised to AxiSim's cap, never lowered to it -- see FoamCaseSetup. `setup` is
	// still default-initialized here, so the right-hand side is the struct's floor.
	setup.steadyIterations =
		std::max(solver.configSimple.maxIter, setup.steadyIterations);

	switch (solver.configSolver.convectionScheme) {
		case ConvectionScheme::CONV_CENTRAL:             setup.convection = FoamConvection::Linear;       break;
		case ConvectionScheme::CONV_SECOND_ORDER_UPWIND: setup.convection = FoamConvection::LinearUpwind; break;

		// LinearUpwind, not Quick: AxiSim has no QUICK. Selecting it runs second-order
		// upwind (the console line says so), so exporting `Gauss QUICK` would give
		// OpenFOAM a scheme AxiSim never ran. FoamConvection::Quick is kept for the
		// day the kernel gains a real QUICK -- this line is the only one to change
		// back. Unreachable today: the GUI combo offers three schemes and
		// sanitizeSolverConfig clamps anything past CONV_SECOND_ORDER_UPWIND.
		case ConvectionScheme::CONV_QUICK:               setup.convection = FoamConvection::LinearUpwind; break;

		case ConvectionScheme::CONV_UPWIND:              setup.convection = FoamConvection::Upwind;       break;
	}

	setup.leastSquaresGradient =
		solver.configSolver.gradientScheme == GradientScheme::GRAD_LSQ;

	setup.addConvection    = solver.configSolver.addConvectionTerm;
	setup.nonOrthCorrector = solver.configSolver.useNonOrthCorrector;

	setup.momentumRelaxation = solver.simple.momentumRelaxation;
	setup.pressureRelaxation = solver.simple.pressureRelaxation;

	// Matches the literal 1.0 the concentration equation's underRelaxEquation call
	// passes. Not momentumRelaxation, which is what temperature gets.
	setup.concentrationRelaxation = 1.0;

	return setup;
}

void saveFromExplorerMesh(Mesh& mesh, const Solver& solver) {

	std::wstring path = saveFileDialog(FileKind::Mesh);
	if (path.empty()) return;

	const std::filesystem::path target(path);

	// The Save-as-type dropdown decides which writer runs, the same way the animation
	// export picks mp4 or a PNG sequence off the extension. ".foam" is the only one
	// that is an export rather than a save, and it consumes the stem as a folder name
	// instead of writing the file the dialog appeared to name.
	//
	// Folded to lower case first: path comparison is case-sensitive even on Windows,
	// where the filesystem is not, so a hand-typed "case.FOAM" would miss this branch
	// and get a binary .aximesh written under an OpenFOAM name.
	std::wstring extension = target.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](wchar_t c) { return (wchar_t)std::towlower(c); });

	if (extension == L".foam") {
		// Bring the mesh's cached FVMesh up to date first: the exported patches have to
		// cut the boundary exactly where AxiSim's BC groups do, and those group IDs are
		// baked into FVFace::boundaryGroupID when the cache is built. A group created
		// after generate() would otherwise export onto the wrong patch.
		mesh.refreshFVMesh();

		saveFoamCase(
			target.parent_path() / (target.stem().wstring() + L"_case"),
			mesh,
			foamCaseSetupFromSolver(solver)
		);
		return;
	}

	std::ofstream out(target, std::ios::binary);
	saveFromPathMesh(out, mesh);
}

void saveFromPathMesh(std::ofstream& out, Mesh& mesh) {

	// GridConfig no longer carries obstacleIndices, but this block is positional --
	// no magic, no version -- so the set's slot has to stay on disk. Dropping it
	// would shift every field behind it, and a build that still reads the field
	// would take mesh.g.R out of the obstacle count. Always written empty now.
	const std::unordered_set<int> obstacleIndicesSlot;

	// save user specific input
	writeAll(
		out,
		mesh.nseg,
		mesh.currentMeshType,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
		mesh.nextGroupID,
		obstacleIndicesSlot,
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

	// Dead slot (see saveFromPathMesh): every save still carries the obstacle set
	// here, so its bytes must be consumed to keep the rest of the block aligned.
	// Read and thrown away -- the mesh raster no longer has a solid-cell mask.
	std::unordered_set<int> obstacleIndicesSlot;

	// load dimensions
	readAll(in,
		mesh.nseg,
		mesh.currentMeshType,
		mesh.gridVertices,
		mesh.gridLineVertices,
		mesh.selectableOuterEdges,
		mesh.nextGroupID,
		obstacleIndicesSlot,
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
		solver.configSolver,	// v5: carries the schemes and saveKeyFrameIter too
		solver.f,
		solver.configSimple
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
				ok = readSolverPayload(in, solver);
			}
			else if (version == solverFileVersionSplitConfig) {
				ok = readSolverPayloadLegacy(in, solver, true);
			}
			else if (version == solverFileVersionNoGradientScheme) {
				// Pre-gradientScheme save: everything else still reads, and the
				// scheme keeps its default rather than costing the user the whole
				// solver setup.
				ok = readSolverPayloadLegacy(in, solver, false);
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
	// mesh.currentMeshType elsewhere in this file.
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

// ====================================================
// -------------------NUMPY EXPORT---------------------
// ====================================================

namespace {

	// The container is identical for every dtype we export -- only the descr string
	// and the element width change -- so the header padding and length rules live
	// here once instead of being copied per dtype.
	bool writeNpyRaw(
		const std::filesystem::path& path,
		const char* descr,
		const void* data,
		std::size_t elemSize,
		std::size_t rows,
		std::size_t cols
	) {
		std::ofstream out(path, std::ios::binary);
		if (!out) {
			std::cerr << "writeNpy: cannot open " << path.string() << '\n';
			return false;
		}

		// The shape is always written as a 2-tuple, so a single-column export loads
		// as (N, 1) rather than (N,). Python side does a[:, 0] -- cheaper than
		// teaching this writer a 1-D spelling that needs the "(N,)" trailing comma.
		std::string header = std::string("{'descr': '") + descr
			+ "', 'fortran_order': False, 'shape': ("
			+ std::to_string(rows) + ", " + std::to_string(cols) + "), }";

		// Spec: magic(6) + version(2) + headerLen(2) + header must be a multiple of
		// 64 so the payload lands 64-byte aligned. The header counts its own
		// terminating newline, so reserve it before rounding up.
		constexpr std::size_t prefix = 6 + 2 + 2;
		const std::size_t unpadded = prefix + header.size() + 1;
		const std::size_t total = ((unpadded + 63) / 64) * 64;

		header.append(total - unpadded, ' ');
		header.push_back('\n');

		// v1.0 stores the header length as uint16; the dict above is ~60 bytes and
		// grows only with the digit count of the shape, so it cannot reach the cap.
		const std::size_t headerLen = header.size();

		out.write("\x93NUMPY", 6);
		out.put('\x01');	// major version
		out.put('\x00');	// minor version

		// written byte-by-byte instead of as a uint16 so the little-endian order is
		// the format's, not the host's
		out.put((char)(std::uint8_t)(headerLen & 0xFFu));
		out.put((char)(std::uint8_t)((headerLen >> 8) & 0xFFu));

		out.write(header.data(), (std::streamsize)headerLen);

		if (rows * cols != 0) {
			out.write((const char*)data, (std::streamsize)(rows * cols * elemSize));
		}

		if (!out) {
			std::cerr << "writeNpy: failed while writing " << path.string() << '\n';
			return false;
		}

		return true;
	}

}

bool writeNpy(
	const std::filesystem::path& path,
	const std::vector<double>& data,
	std::size_t rows,
	std::size_t cols
) {
	// '<f8' below promises little-endian IEEE-754 binary64, and the payload is
	// blitted straight out of the vector -- so assert the host actually matches
	// rather than letting a reader silently get byte-swapped garbage.
	static_assert(sizeof(double) == 8, "writeNpy: '<f8' requires an 8-byte double");
	static_assert(std::endian::native == std::endian::little,
		"writeNpy: '<f8' requires a little-endian host");

	if (data.size() != rows * cols) {
		std::cerr << "writeNpy: expected " << rows * cols << " values for a "
			<< rows << "x" << cols << " array, got " << data.size() << '\n';
		return false;
	}

	return writeNpyRaw(path, "<f8", data.data(), sizeof(double), rows, cols);
}

bool writeNpyInt32(
	const std::filesystem::path& path,
	const std::vector<std::int32_t>& data,
	std::size_t rows,
	std::size_t cols
) {
	static_assert(std::endian::native == std::endian::little,
		"writeNpyInt32: '<i4' requires a little-endian host");

	if (data.size() != rows * cols) {
		std::cerr << "writeNpyInt32: expected " << rows * cols << " values for a "
			<< rows << "x" << cols << " array, got " << data.size() << '\n';
		return false;
	}

	return writeNpyRaw(path, "<i4", data.data(), sizeof(std::int32_t), rows, cols);
}

namespace {

	// Flatten per-cell corner quads into a shared point array plus 4 indices per
	// cell. Blocks own their own node arrays, so a vertex lying on a block
	// interface exists once per block that touches it -- welding those duplicates
	// is the whole reason cells.npy carries indices instead of four coordinate
	// pairs: without it a "shared" edge is shared by nothing and every topological
	// query on the export comes back empty.
	void weldCellCorners(
		const std::vector<std::array<Vec2, 4>>& quads,
		std::vector<double>& pointXY,
		std::vector<std::int32_t>& cellIdx
	) {
		// Scaled off the geometry rather than fixed: the same duct modelled in
		// metres and in microns has to weld identically.
		double extent = 1.0;
		for (const std::array<Vec2, 4>& q : quads) {
			for (const Vec2& p : q) {
				extent = std::max(extent, std::max(std::fabs(p.z), std::fabs(p.r)));
			}
		}
		const double tol = extent * 1e-9;

		struct Key {
			long long z, r;
			bool operator==(const Key& o) const { return z == o.z && r == o.r; }
		};

		struct Hash {
			std::size_t operator()(const Key& k) const {
				const std::size_t h = std::hash<long long>{}(k.z);
				return h ^ (std::hash<long long>{}(k.r) + 0x9e3779b97f4a7c15ULL
					+ (h << 6) + (h >> 2));
			}
		};

		std::unordered_map<Key, std::int32_t, Hash> seen;
		seen.reserve(quads.size() * 2);

		pointXY.clear();
		pointXY.reserve(quads.size() * 2);

		cellIdx.clear();
		cellIdx.reserve(quads.size() * 4);

		for (const std::array<Vec2, 4>& q : quads) {
			for (const Vec2& p : q) {

				const Key key{ std::llround(p.z / tol), std::llround(p.r / tol) };

				// Two coordinates closer than tol can still round into adjacent
				// buckets, so probe the neighbours before calling this vertex new.
				// Cell spacing is many orders above tol, so a probe can never reach
				// a genuinely different vertex.
				std::int32_t id = -1;
				for (int dz = -1; dz <= 1 && id < 0; dz++) {
					for (int dr = -1; dr <= 1 && id < 0; dr++) {
						const auto it = seen.find(Key{ key.z + dz, key.r + dr });
						if (it != seen.end()) {
							id = it->second;
						}
					}
				}

				if (id < 0) {
					id = (std::int32_t)(pointXY.size() / 2);
					pointXY.push_back(p.z);
					pointXY.push_back(p.r);
					seen.emplace(key, id);
				}

				cellIdx.push_back(id);
			}
		}
	}

	// Field names are ours ("dP/dz" and friends), so only the two characters JSON
	// always forbids can turn up. Kept anyway so a future field name with a quote
	// in it produces valid JSON instead of an unparseable file.
	std::string jsonEscape(const std::string& s) {

		std::string out;
		out.reserve(s.size());

		for (char c : s) {
			if (c == '"' || c == '\\') {
				out.push_back('\\');
			}
			out.push_back(c);
		}

		return out;
	}

	// Round-trip precision. std::to_string would give 6 decimals, quietly
	// truncating mu (~1e-3) and D (~3e-9) in the metadata.
	std::string jsonNumber(double v) {

		// JSON has no NaN or Infinity literal; null at least parses, and reads as
		// "the solver produced no usable value here" rather than a plausible number.
		if (!std::isfinite(v)) {
			return "null";
		}

		char buf[40];
		std::snprintf(buf, sizeof(buf), "%.17g", v);
		return buf;
	}

	// Names of the fields that can go out as columns, in fieldType order. Taken
	// from fieldType rather than by iterating solutions, whose unordered_map order
	// is not reproducible between runs -- a shifting column layout would silently
	// invalidate any Python script that cached indices.
	//
	// A field sized to anything but the cell count cannot sit alongside the
	// geometry columns, so it is dropped with a warning rather than making the
	// table ragged.
	std::vector<std::string> solutionColumnNames(
		const std::vector<std::string>& fieldType,
		const std::unordered_map<std::string, SolutionField>& solutions,
		std::size_t nCells
	) {
		std::vector<std::string> names;

		for (const std::string& name : fieldType) {

			auto it = solutions.find(name);
			if (it == solutions.end()) {
				continue;
			}

			if (it->second.field.size() != nCells) {
				std::cerr << "saveSolutionNpy: skipping '" << name << "' -- "
					<< it->second.field.size() << " values for " << nCells << " cells\n";
				continue;
			}

			names.push_back(name);
		}

		return names;
	}

	// Row-major pack of the named fields, so Python slices a column as a[:, k].
	// leadCols reserves that many columns at the front of each row for the caller
	// to fill with geometry; frames pass 0.
	std::vector<double> packSolutionTable(
		const std::vector<std::string>& names,
		const std::unordered_map<std::string, SolutionField>& solutions,
		std::size_t nCells,
		std::size_t leadCols
	) {
		const std::size_t nCols = leadCols + names.size();
		std::vector<double> table(nCells * nCols, 0.0);

		for (std::size_t k = 0; k < names.size(); k++) {

			auto it = solutions.find(names[k]);
			if (it == solutions.end() || it->second.field.size() != nCells) {
				continue;
			}

			const std::vector<double>& field = it->second.field;
			const std::size_t col = leadCols + k;

			for (std::size_t c = 0; c < nCells; c++) {
				table[c * nCols + col] = field[c];
			}
		}

		return table;
	}
}

bool saveSolutionNpy(const std::filesystem::path& dir, const Project& project) {

	const Solver& solver = project.solver;
	const Mesh& mesh = project.mesh;
	const Results& results = project.results;

	const FVMesh& fvMesh = solver.fvMesh;
	const std::size_t nCells = (std::size_t)fvMesh.numCells();

	if (nCells == 0) {
		std::cerr << "saveSolutionNpy: no cells to export -- mesh and solve first\n";
		return false;
	}

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec) {
		std::cerr << "saveSolutionNpy: cannot create " << dir.string()
			<< " -- " << ec.message() << '\n';
		return false;
	}

	// Both cell measures go out. volume is the revolved 2*pi*r*area2D, which is
	// the right weight for an axisymmetric integral but is ZERO on the axis --
	// weighting an error norm by it drops the centerline cells entirely, exactly
	// where a Poiseuille peak lives. area2D is the r-z measure to use instead
	// when the norm is meant to be over the meridional plane.
	const std::vector<std::string> geomColumns = { "z", "r", "volume", "area2D"};
	const std::size_t nGeom = geomColumns.size();

	const std::vector<std::string> fieldColumns =
		solutionColumnNames(results.fieldType, results.solutions, nCells);

	std::vector<double> table =
		packSolutionTable(fieldColumns, results.solutions, nCells, nGeom);

	const std::size_t nCols = nGeom + fieldColumns.size();

	for (std::size_t c = 0; c < nCells; c++) {

		const FVCell& cell = fvMesh.cells[c];
		double* row = table.data() + c * nCols;

		row[0] = cell.center.z;
		row[1] = cell.center.r;
		row[2] = cell.volume;
		row[3] = cell.area2D;

	}

	if (!writeNpy(dir / "solution.npy", table, nCells, nCols)) {
		return false;
	}

	// Cell outlines. Centers alone leave a Python reader nothing but a scatter:
	// triangulating them puts vertices at cell CENTERS (half a cell off the real
	// ones), turns each quad into two triangles on an arbitrary diagonal, and
	// convex-hulls away every obstacle and concavity. points.npy + cells.npy carry
	// the actual polygons, so a reader can draw and integrate on the mesh that was
	// solved on.
	//
	// Both mesh paths have corners; they just have a different NUMBER of them, which
	// is why meta carries cellCorners rather than the reader assuming 4. The plain
	// single-block structured path is the one that has none -- createStructuredFVFaces
	// leaves FVFace::v0/v1 at -1, and the packed multiblock faces carry no vertex ids
	// either, so the corners come from the block quads instead. The meta keys below
	// are omitted rather than zeroed when the pair is absent, so a reader that assumes
	// them raises KeyError instead of silently reshaping nothing -- the same rule
	// nr/nz follow.
	std::size_t nPoints = 0;
	std::size_t cellCorners = 0;

	std::vector<std::array<Vec2, 4>> quads;

	if (mesh.multiBlockCellCorners(quads) && quads.size() == nCells) {

		std::vector<double> pointXY;
		std::vector<std::int32_t> cellIdx;
		weldCellCorners(quads, pointXY, cellIdx);

		nPoints = pointXY.size() / 2;
		cellCorners = 4;

		if (!writeNpy(dir / "points.npy", pointXY, nPoints, 2) ||
			!writeNpyInt32(dir / "cells.npy", cellIdx, nCells, 4)) {
			return false;
		}
	}
	else if (mesh.currentMeshType == MeshType::Unstructured &&
		mesh.unstructuredTriangles.size() == nCells &&
		!mesh.unstructuredPoints.empty()) {

		// No weld here: gmsh already hands back indexed geometry, and
		// createUnstructuredMesh makes triangle c into cell c, so the triangles ARE
		// the cell polygons in the order the solution rows are in.
		std::vector<double> pointXY;
		pointXY.reserve(mesh.unstructuredPoints.size() * 2);

		for (const Vec2& p : mesh.unstructuredPoints) {
			pointXY.push_back(p.z);
			pointXY.push_back(p.r);
		}

		std::vector<std::int32_t> cellIdx;
		cellIdx.reserve(nCells * 3);

		for (const Triangle& tri : mesh.unstructuredTriangles) {
			cellIdx.push_back((std::int32_t)tri.v0);
			cellIdx.push_back((std::int32_t)tri.v1);
			cellIdx.push_back((std::int32_t)tri.v2);
		}

		nPoints = mesh.unstructuredPoints.size();
		cellCorners = 3;

		if (!writeNpy(dir / "points.npy", pointXY, nPoints, 2) ||
			!writeNpyInt32(dir / "cells.npy", cellIdx, nCells, 3)) {
			return false;
		}
	}

	// Transient frames: field columns only. Geometry is in solution.npy and does
	// not move between frames, so repeating it would cost five columns per frame
	// across a run that can be hundreds of frames long.
	std::vector<std::pair<std::string, double>> frameIndex;

	if (results.hasAnimation()) {

		for (std::size_t i = 0; i < results.animationFrames.size(); i++) {

			const Results::AnimationFrame& frame = results.animationFrames[i];

			char name[32];
			std::snprintf(name, sizeof(name), "frame_%04zu.npy", i);

			const std::vector<double> frameTable =
				packSolutionTable(fieldColumns, frame.solutions, nCells, 0);

			if (!writeNpy(dir / name, frameTable, nCells, fieldColumns.size())) {
				return false;
			}

			frameIndex.emplace_back(name, frame.time);
		}
	}

	std::ofstream meta(dir / "meta.json");
	if (!meta) {
		std::cerr << "saveSolutionNpy: cannot open " << (dir / "meta.json").string() << '\n';
		return false;
	}

	// A single raster exists only on a structured single-block mesh. The trellis
	// path reports MeshType::Structured too but numbers cells per block, leaving
	// fvMesh.nr/nz at 0 -- so the reshape is gated on those, not on the mesh type.
	// nr/nz are omitted rather than written as 0 when there is no raster, so a
	// Python reader that assumes one raises KeyError instead of reshaping to
	// nothing.
	const bool reshapable = fvMesh.nr > 0 && fvMesh.nz > 0;

	meta << "{\n";
	meta << "  \"app\": \"AxiSim\",\n";
	meta << "  \"format\": 1,\n";
	meta << "  \"note\": \"cell-centered values, base SI units\",\n";
	meta << "  \"cells\": " << nCells << ",\n";

	meta << "  \"meshType\": \""
		<< (mesh.currentMeshType == MeshType::Structured ? "Structured" : "Unstructured")
		<< "\",\n";
	meta << "  \"multiBlock\": " << (mesh.isMultiBlock ? "true" : "false") << ",\n";
	meta << "  \"structured\": " << (reshapable ? "true" : "false") << ",\n";

	if (reshapable) {
		meta << "  \"nr\": " << fvMesh.nr << ",\n";
		meta << "  \"nz\": " << fvMesh.nz << ",\n";
	}

	if (cellCorners > 0) {
		meta << "  \"points\": " << nPoints << ",\n";
		meta << "  \"cellCorners\": " << cellCorners << ",\n";
	}

	meta << "  \"fluid\": {"
		<< "\"rho\": " << jsonNumber(solver.f.rho)
		<< ", \"mu\": " << jsonNumber(solver.f.mu)
		<< ", \"cp\": " << jsonNumber(solver.f.cp)
		<< ", \"k\": " << jsonNumber(solver.f.k)
		<< ", \"D\": " << jsonNumber(solver.f.D)
		<< "},\n";

	meta << "  \"transient\": " << (solver.configSolver.transient ? "true" : "false") << ",\n";
	meta << "  \"dt\": " << jsonNumber(solver.configSolver.dt) << ",\n";

	meta << "  \"columns\": [";
	for (std::size_t i = 0; i < geomColumns.size(); i++) {
		meta << (i ? ", " : "") << '"' << jsonEscape(geomColumns[i]) << '"';
	}
	for (const std::string& name : fieldColumns) {
		meta << ", \"" << jsonEscape(name) << '"';
	}
	meta << "],\n";

	meta << "  \"frameColumns\": [";
	for (std::size_t i = 0; i < fieldColumns.size(); i++) {
		meta << (i ? ", " : "") << '"' << jsonEscape(fieldColumns[i]) << '"';
	}
	meta << "],\n";

	meta << "  \"frames\": [";
	for (std::size_t i = 0; i < frameIndex.size(); i++) {
		meta << (i ? ",\n    " : "\n    ")
			<< "{\"file\": \"" << frameIndex[i].first
			<< "\", \"time\": " << jsonNumber(frameIndex[i].second) << "}";
	}
	meta << (frameIndex.empty() ? "]\n" : "\n  ]\n");

	meta << "}\n";

	if (!meta) {
		std::cerr << "saveSolutionNpy: failed while writing meta.json\n";
		return false;
	}

	std::cout << "saveSolutionNpy: wrote " << nCells << " cells x " << nCols
		<< " columns";
	if (cellCorners > 0) {
		std::cout << " + " << nPoints << " corner points ("
			<< cellCorners << " per cell)";
	}
	if (!frameIndex.empty()) {
		std::cout << " + " << frameIndex.size() << " frames";
	}
	std::cout << " to " << dir.string() << '\n';

	return true;
}

void saveFromExplorerSolution(const Project& project) {

	// The menu item is already gated on this, but the check is repeated here so
	// a call from anywhere else cannot walk into saveSolutionNpy with an empty
	// mesh and get the less obvious "no cells to export".
	if (!project.results.isReady) {
		std::cerr << "saveFromExplorerSolution: nothing to export -- run a solve first\n";
		return;
	}

	const std::wstring path = saveFileDialog(FileKind::Solution);
	if (path.empty()) {
		return;
	}

	// The dialog names one .npy, but an export is solution.npy + meta.json plus a
	// file per transient frame, so they get a folder of their own beside the name
	// that was picked instead of being tipped into whatever is already there --
	// the same rule the PNG sequence export follows (AnimationGUI::beginExport).
	const std::filesystem::path target(path);

	saveSolutionNpy(
		target.parent_path() / (target.stem().wstring() + L"_solution"),
		project
	);
}
