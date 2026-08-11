#pragma once
#include "setting.cuh"

// FoamCaseSetup, for the OpenFOAM case export below.
#include "file_struct.h"

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <codecvt>
#include <locale>
#include <type_traits>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class Project;
class Solution;
class SceneView;
class Geometry;
class Mesh;
class Solver;
class Results;
struct AppSettings;
struct Config;
struct SolutionField;

// ====================================================
// -------------------FILE DIALOG----------------------
// ====================================================

// which kind of file a dialog is for -- selects the extension filter and the
// default extension appended when the user types a bare name
enum class FileKind {
	Project,	// .axi
	Geometry,	// .axigeom
	Mesh,		// .aximesh
	Solver,		// .axislv

	// Names the animation export. An .mp4 target is written directly; a .png one
	// names a numbered frame sequence written into a folder derived from it.
	Animation,

	// Names the analysis export. Like the .png sequence above, the target is a
	// folder derived from the chosen name, not the single .npy the dialog shows.
	Solution
};

// open file dialog for saving
std::wstring saveFileDialog(FileKind kind);

// open file dialog for loading
std::wstring loadFileDialog(FileKind kind);
// ====================================================
// -------------------SETTINGS-------------------------
// ====================================================
void saveSettings(Project& project, AppSettings& settings);

// ====================================================
// -------------------PROJECT--------------------------
// ====================================================

// save project when save hotkey is pressed
bool saveHotkeyPressed(Project& project);

// save project given a path
void saveFromPathProject(const std::wstring& path, Project& project);

// save project by opening explorer
void saveFromExplorerProject(Project& project);

// load project from a given path
void loadFromPathProject(std::ifstream& in, Project& project);

// load project from explorer
void loadFromExplorerProject(Project& project);

// load a bundled preset project from the exe's presets/ folder (no file dialog).
// no-op if the preset file is missing.
void loadPresetProject(const std::string& fileName, Project& project);

// ======================================================================
// -----------------------GEOMETRY---------------------------------------
// ======================================================================
void saveFromExplorerGeometry(Geometry& geometry);

void saveFromPathGeometry(std::ofstream& out, Geometry& geometry);

void loadFromExplorerGeometry(Geometry& geometry);

void loadFromPathGeometry(std::ifstream& in, Geometry& geometry);

// ======================================================================
// -----------------------MESH-------------------------------------------
// ======================================================================
// Ask for a name, then write the mesh. The Save-as-type dropdown chooses between the
// native .aximesh/.bin save and an OpenFOAM export; picking "OpenFOAM Case" routes to
// saveFoamCase below and produces a folder, not the file the dialog named.
void saveFromExplorerMesh(Mesh& mesh, const Solver& solver);

// Export the project as a complete, runnable OpenFOAM case:
//
//   system/    controlDict, fvSchemes, fvSolution (+ blockMeshDict, see below)
//   constant/  transportProperties, turbulenceProperties (+ polyMesh, see below)
//   0/         U, p, and T/C for whichever scalars are being solved
//
// The mesh supplies the geometry and the boundary conditions; `setup` supplies the
// fluid and the numerics, and decides between simpleFoam and pimpleFoam.
//
// How the MESH goes out depends on what kind it is, and the two are not
// interchangeable -- a blockMeshDict is a list of hexes, which a triangulated mesh
// has none of:
//
//   - multi-block: system/blockMeshDict, so the case needs a `blockMesh` run first.
//     The blocks, their interfaces and the per-edge boundary groups all come from
//     the trellis decomposition.
//   - unstructured: constant/polyMesh written straight out, revolved into a wedge
//     from the same FVMesh the solver runs on. There is no meshing step -- running
//     blockMesh on it would only fail on a dict that is not there.
//
// The console line at the end says which, and quotes the exact commands to run.
//
// Returns false and logs if the mesh is neither (a plain single-block structured
// grid has no vertex ids to revolve), if a folder cannot be created, or if a write
// fails.
bool saveFoamCase(const std::filesystem::path& dir, const Mesh& mesh,
                  const FoamCaseSetup& setup);

// Flatten Solver into the FoamCaseSetup the case writers want: fluid properties,
// the two field checkboxes, transient/dt/tEnd, the convection and gradient schemes,
// and the SIMPLE relaxation factors.
FoamCaseSetup foamCaseSetupFromSolver(const Solver& solver);

// save mesh given a path
void saveFromPathMesh(std::ofstream& out, Mesh& mesh);

// load mesh from explorer
void loadFromExplorerMesh(Mesh& mesh);

// load mesh from given path
void loadFromPathMesh(std::ifstream& in, Mesh& mesh);

// ======================================================================
// -----------------------SOLVER-----------------------------------------
// ======================================================================
// save solver by opening explorer
void saveFromExplorerSolver(Solver& solver);

// save solver given a path
void saveFromPathSolver(std::ofstream& out, Solver& solver);

// load all necessary variables for the solver config and boundary conditions
void loadFromPathSolver(std::ifstream& in, Solver& solver);

// load solver after opening explorer
void loadFromExplorerSolver(Solver& solver);

// load selected files when the application launches
void loadAtLaunch(Project& project, AppSettings& settings);

// ======================================================================
// -----------------------RESULTS----------------------------------------
// ======================================================================
void saveFromPathResults(std::ofstream& out, const Results& results);

void loadFromPathResults(std::ifstream& in, Results& results);


// ====================================================
// -------------------READING FILE---------------------
// ====================================================

// load a value
template<typename T>
bool readVar(std::ifstream& in, T& val) {
	// This overload accepts anything, so a type that owns memory silently reaching it
	// would read raw pointers off disk. The declaration order of the overloads below
	// is what prevents that; this turns a mistake there into a compile error instead.
	static_assert(std::is_trivially_copyable_v<T>,
		"readVar: type owns memory -- declare a readVar overload for it above the container templates");
	return (bool)in.read((char*)&val, sizeof(T));
}

// Bytes left in the stream from the current position. Used to sanity-check a length
// prefix BEFORE allocating against it.
inline std::streamoff bytesLeft(std::ifstream& in) {
	std::streampos pos = in.tellg();
	if (pos == std::streampos(-1)) {
		return 0;
	}

	in.seekg(0, std::ios::end);
	std::streampos end = in.tellg();
	in.seekg(pos);

	// A failed end-seek leaves tellg at -1, which would otherwise come back as a
	// huge negative "space remaining" and defeat every caller's ceiling check.
	if (end == std::streampos(-1) || end < pos) {
		return 0;
	}

	return end - pos;
}

// True when a container claiming `size` elements of at least `minBytesPerElement`
// each cannot possibly fit in what is left of the file.
//
// A truncated, mismatched or hand-edited file hands us an arbitrary size_t, and
// resize/reserve would try to honour it before the first failed read -- a multi-
// terabyte allocation that throws bad_alloc and takes the process down. That is a
// crash rather than the clean "this block is not for me" the callers are written to
// handle, and it happens before any of their error paths get a chance to run. The
// file length is a hard ceiling no honest count can exceed.
inline bool sizeExceedsFile(std::ifstream& in, size_t size, size_t minBytesPerElement) {
	const std::streamoff left = bytesLeft(in);
	return (std::uintmax_t)size > (std::uintmax_t)(left / (std::streamoff)minBytesPerElement);
}

// SolutionField owns vectors, so it can't be raw-copied. Declared here (defined in
// file_manager.cpp, which has the complete type) so the vector/map templates below
// resolve to it at their definition context rather than falling back to the generic
// memcpy overload -- ADL alone would not find it for a std:: container element.
bool readVar(std::ifstream& in, SolutionField& value);

// load a std::string (length-prefixed). must be declared before the vector/map
// templates and readAll so the dispatch picks this overload instead of raw-copying
// the string object.
inline bool readVar(std::ifstream& in, std::string& value) {
	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	if (sizeExceedsFile(in, size, sizeof(char))) {
		return false;
	}

	value.resize(size);

	if (size == 0) {
		return true;
	}

	return (bool)in.read(value.data(), size);
}


namespace FileEncoding {
	// Project files historically stored Windows UTF-16 wchar_t bytes. Keep that
	// exact representation on every platform so a Linux build can exchange .axi
	// files with the existing Windows release (Linux wchar_t is normally UTF-32).
	inline std::u16string wideToUtf16(const std::wstring& value) {
		if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
			return std::u16string(
				reinterpret_cast<const char16_t*>(value.data()),
				value.size()
			);
		}
		else {
			std::wstring_convert<std::codecvt_utf8<wchar_t>> wideUtf8;
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf16Utf8;
			return utf16Utf8.from_bytes(wideUtf8.to_bytes(value));
		}
	}

	inline std::wstring utf16ToWide(const std::u16string& value) {
		if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
			return std::wstring(
				reinterpret_cast<const wchar_t*>(value.data()),
				value.size()
			);
		}
		else {
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf16Utf8;
			std::wstring_convert<std::codecvt_utf8<wchar_t>> wideUtf8;
			return wideUtf8.from_bytes(utf16Utf8.to_bytes(value));
		}
	}
}

// Load a std::wstring stored as UTF-16 code units. Must be declared before
// readAll so variadic dispatch picks this overload instead of raw-copying it.
inline bool readVar(std::ifstream& in, std::wstring& value) {
	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	if (sizeExceedsFile(in, size, sizeof(char16_t))) {
		return false;
	}

	std::u16string utf16(size, u'\0');

	if (size == 0) {
		value.clear();
		return true;
	}

	if (!in.read(reinterpret_cast<char*>(utf16.data()), size * sizeof(char16_t))) {
		return false;
	}

	try {
		value = FileEncoding::utf16ToWide(utf16);
		return true;
	}
	catch (const std::range_error&) {
		value.clear();
		return false;
	}
}

// load one vector. Trivially-copyable elements are read as one bulk block (the
// original on-disk layout, unchanged); anything that owns memory -- std::string,
// SolutionField -- is read element-wise through its own overload, which is why this
// template sits below them.
template<typename T>
bool readVar(std::ifstream& in, std::vector<T>& vec) {
	size_t size = 0;

	if (!(bool)in.read((char*)&size, sizeof(size))) {
		return false;
	}

	// Trivially-copyable elements occupy exactly sizeof(T) on disk; a memory-owning
	// element still costs at least its own length prefix.
	constexpr size_t minBytes =
		std::is_trivially_copyable_v<T> ? sizeof(T) : sizeof(size_t);

	if (sizeExceedsFile(in, size, minBytes)) {
		return false;
	}

	vec.resize(size);

	if constexpr (std::is_trivially_copyable_v<T>) {
		return (bool)in.read((char*)vec.data(), size * sizeof(T));
	}
	else {
		for (T& value : vec) {
			if (!readVar(in, value)) {
				return false;
			}
		}
		return true;
	}
}

template<typename T,
		typename Hash,
		typename KeyEqual,
		typename Allocator>
bool readVar(std::ifstream& in, std::unordered_set<T, Hash, KeyEqual, Allocator>& set) {

	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	// Elements are read raw below, so each costs exactly sizeof(T) on disk.
	if (sizeExceedsFile(in, size, sizeof(T))) {
		return false;
	}

	set.clear();
	set.reserve(size);

	for (size_t k = 0; k < size; k++) {
		T value{};

		if (!in.read((char*)(&value), sizeof(T))) {
			return false;
		}
		set.insert(value);
	}

	return true;

}

template <
	typename Key,
	typename Value,
	typename Hash,
	typename KeyEqual,
	typename Allocator
>
bool readVar(std::ifstream& in,	std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>& map) {
	map.clear();

	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	// Same guard as the vector reader: an entry costs at least a key length prefix on
	// disk, so a count beyond that ceiling is garbage and must not reach reserve().
	if (sizeExceedsFile(in, size, sizeof(size_t))) {
		return false;
	}

	map.reserve(size);

	for (size_t i = 0; i < size; i++) {
		Key key{};
		Value value{};

		if (!readVar(in, key) || !readVar(in, value)) return false;

		// move, not copy -- Value can own vectors (SolutionField), and copying here
		// would deep-copy every loaded solution into the node and throw the original away.
		map.emplace(std::move(key), std::move(value));
	}

	return true;
}

// load several values
template<typename... Args>
bool readAll(std::ifstream& in, Args&... args) {
	return (readVar(in, args) && ...);
}

template <typename... Args>
bool readBinary(const std::string& filename, Args&... args) {
	std::ifstream in(filename, std::ios::binary);

	if (!in) {
		throw std::runtime_error("Could not open file: " + filename);
	}

	return readAll(in, args...);

}

template <typename... Args>
bool readBinary(std::ifstream& in, Args&... args) {

	if (!in) {
		throw std::runtime_error("Invalid ifstream");
	}

	return readAll(in, args...);
}


// ====================================================
// -------------------NUMPY EXPORT---------------------
// ====================================================

// Write a rows x cols double table as a NumPy .npy file (format v1.0, C order,
// dtype '<f8'), readable with a bare np.load() -- no dependency on our .axi
// reader and no CSV parsing cost on the Python side.
//
// This is for analysis export only, NOT a save format: it carries no version
// tag and is never read back by the app, so unlike the .axi writers above it
// can change shape freely without breaking old projects.
//
// data must hold exactly rows*cols values laid out row-major. Returns false on
// a size mismatch or an unwritable path.
bool writeNpy(
	const std::filesystem::path& path,
	const std::vector<double>& data,
	std::size_t rows,
	std::size_t cols
);

// Same container, dtype '<i4'. Connectivity is indices, and writing them as
// doubles would both double the file and invite a reader to do arithmetic on
// them -- np.load gives an integer array that can index directly.
bool writeNpyInt32(
	const std::filesystem::path& path,
	const std::vector<std::int32_t>& data,
	std::size_t rows,
	std::size_t cols
);

// Export the current solution for offline analysis (validation against analytic
// solutions, grid-convergence studies). Writes into dir, creating it if needed:
//
//   solution.npy   nCells x nColumns, one row per cell
//   meta.json      column names, mesh shape, fluid properties, frame index
//   frame_NNNN.npy one per captured transient frame, field columns only
//   points.npy     nPoints x 2 (z, r) cell corners            } corner-bearing
//   cells.npy      nCells x cellCorners indices into points   } meshes only
//
// Read meta.json "columns" for the layout rather than assuming one -- the field
// set follows whatever the run solved. Frames carry "frameColumns" only, since
// the geometry columns are in solution.npy and do not move between frames.
//
// points/cells give the real cell outlines, in cell order, so a reader can draw
// and integrate on the mesh that was solved on instead of triangulating the cell
// centers -- which would put vertices half a cell off and hull away obstacles.
// Both files, and the "points"/"cellCorners" keys, are absent on the paths that
// have no corners to give; test for the key, do not assume.
//
// Cell values are exported raw and in base SI, on every mesh type. Nothing is
// resampled onto the render raster: that path zeroes cells no block covers, and
// a zero is indistinguishable from a result once it is in NumPy.
bool saveSolutionNpy(const std::filesystem::path& dir, const Project& project);

// Ask for a name, then export beside it. The dialog names one .npy but an export
// is a folder (see saveSolutionNpy), so the chosen stem becomes "<stem>_solution"
// in the same directory -- the rule the PNG sequence export already follows.
void saveFromExplorerSolution(const Project& project);

// ====================================================
// -------------------WRITING FILE---------------------
// ====================================================

// save any single object
template <typename T>
void writeVar(std::ofstream& out, const T& value) {
	// See the readVar counterpart: catches a memory-owning type falling through to the
	// raw-copy path at compile time rather than writing pointers into the .bin.
	static_assert(std::is_trivially_copyable_v<T>,
		"writeVar: type owns memory -- declare a writeVar overload for it above the container templates");
	out.write((const char*)(&value), sizeof(T));
}

// See the readVar counterpart: declared ahead of the vector/map templates so they
// resolve to it instead of memcpy-ing a struct that owns vectors.
void writeVar(std::ofstream& out, const SolutionField& value);

// save a std::string (length-prefixed). must be declared before the vector/map
// templates and writeAll so the dispatch picks this overload instead of raw-copying
// the string object.
inline void writeVar(std::ofstream& out, const std::string& value) {
	size_t size = value.size();
	out.write((const char*)&size, sizeof(size));
	out.write(value.data(), size);
}

inline void writeVar(std::ofstream& out, const std::wstring& value) {
	const std::u16string utf16 = FileEncoding::wideToUtf16(value);
	size_t size = utf16.size();
	out.write((const char*)&size, sizeof(size));
	out.write((const char*)utf16.data(), size * sizeof(char16_t));
}

// save std::vector. Trivially-copyable elements go out as one bulk block (the
// original on-disk layout, unchanged); memory-owning elements are written
// element-wise through their own overload declared above.
template <typename T>
void writeVar(std::ofstream& out, const std::vector<T>& vec) {
	size_t size = vec.size();
	out.write((const char*)&size, sizeof(size));

	if constexpr (std::is_trivially_copyable_v<T>) {
		out.write((const char*)vec.data(), size * sizeof(T));
	}
	else {
		for (const T& value : vec) {
			writeVar(out, value);
		}
	}
}

// save std::unordered_set
template <typename T,
		typename Hash,
		typename KeyEqual,
		typename Allocator>
void writeVar(std::ofstream& out, const std::unordered_set<T, Hash, KeyEqual, Allocator>& set) {
	size_t size = set.size();

	out.write((const char*)&size, sizeof(size));

	for (const T& value : set) {
		writeVar(out, value);
	}
}

// save std::unordered_map
template <
	typename Key,
	typename Value,
	typename KeyEqual,
	typename Hash,
	typename Allocator
>
void writeVar(std::ofstream& out, const std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>& map) {
	size_t size = map.size();

	out.write((const char*)&size, sizeof(size));

	for (const auto& [key, value] : map) {
		writeVar(out, key);
		writeVar(out, value);
	}
}

template <typename... Args>
void writeAll(std::ofstream& out, const Args&... args) {
	(writeVar(out, args), ...);
}

template <typename... Args>
void saveBinary(const std::string& filename, const Args&... args) {
	std::ofstream out(filename, std::ios::binary);

	if (!out) {
		throw std::runtime_error("Could not open file: " + filename);
	}

	writeAll(out, args...);
}

template <typename... Args>
void saveBinary(std::ofstream& out, const Args&... args) {
	if (!out) {
		throw std::runtime_error("Output stream is not open.");
	}
	writeAll(out, args...);
}
