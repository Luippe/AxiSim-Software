#pragma once
#include "setting.cuh"

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <unordered_set>
#include <unordered_map>

class Project;
class Solution;
class SceneView;
class Geometry;
class Mesh;
class Solver;
class Results;
struct AppSettings;
struct Config;
struct BoundaryCondition;
struct BoundaryConditionConfig;
struct SolutionField;

// ====================================================
// -------------------FILED DIALOG---------------------
// ====================================================

// check if file exists
bool fileExists(const std::string& filename);

// open .bin file and return stream
std::ofstream openBinaryFile(const char* path);

// open file dialog for saving
std::wstring saveFileDialog();

// open file dialog for loading
std::wstring loadFileDialog();
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

void loadFromPathGeometry(std::ifstream& in, Geometry& geometry);

// ======================================================================
// -----------------------MESH-------------------------------------------
// ======================================================================
// save mesh by opening explorer
void saveFromExplorerMesh(Mesh& mesh);

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

// write boundarycondtion struct to save file
void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc);

// read boundary condition from save file
void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc);

// load selected files when the application launches
void loadAtLaunch(Project& project, AppSettings& settings);

// read boundary condition config from save file
template<typename...Args>
void readBoundaryConditionConfig(std::ifstream& in, Args&... args) {
	(readOneBoundaryCondition(in, args),...);
}
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


// load a std::wstring (length-prefixed by CHARACTER count). wchar_t is 2 bytes on
// Windows, so we read size*sizeof(wchar_t) bytes. Must be declared before readAll so
// the variadic dispatch picks this overload instead of raw-copying the object.
inline bool readVar(std::ifstream& in, std::wstring& value) {
	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	if (sizeExceedsFile(in, size, sizeof(wchar_t))) {
		return false;
	}

	value.resize(size);

	if (size == 0) {
		return true;
	}

	return (bool)in.read((char*)value.data(), size * sizeof(wchar_t));
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
	size_t size = value.size();
	out.write((const char*)&size, sizeof(size));
	out.write((const char*)value.data(), size * sizeof(wchar_t));
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