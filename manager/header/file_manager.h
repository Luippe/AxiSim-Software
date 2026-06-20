#pragma once
#include "setting.cuh"
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

class Project;
class Solution;
class SceneView;
class Geometry;
class Mesh;
class Solver;
class Results;
struct Config;
struct BoundaryCondition;
struct BoundaryConditionConfig;

// check if file exists
bool fileExists(const std::string& filename);

// open .bin file and return stream
std::ofstream openBinaryFile(const char* path);

// ====================================================
// -------------------PROJECT--------------------------
// ====================================================
// save project by opening explorer
void saveFromExplorerProject(Project& project);

// load project at startup
void saveLaunchProject(Project& project);

// load project from explorer
void loadFromExplorerProject(Project& project);

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
void loadAtLaunch(Project& project);

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
	return (bool)in.read((char*)&val, sizeof(T));
}

// load one vector
template<typename T>
bool readVar(std::ifstream& in, std::vector<T>& vec) {
	size_t size = 0;

	if (!(bool)in.read((char*)&size, sizeof(size))) {
		return false;
	}

	vec.resize(size);
	return (bool)in.read((char*)vec.data(), size * sizeof(T));
}

// load a std::string (length-prefixed). must be declared before readAll so the
// variadic dispatch picks this overload instead of raw-copying the string object.
inline bool readVar(std::ifstream& in, std::string& value) {
	size_t size = 0;

	if (!in.read((char*)&size, sizeof(size))) {
		return false;
	}

	value.resize(size);

	if (size == 0) {
		return true;
	}

	return (bool)in.read(value.data(), size);
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

	map.reserve(size);

	for (size_t i = 0; i < size; i++) {
		Key key{};
		Value value{};

		if (!readVar(in, key) || !readVar(in, value)) return false;

		map.emplace(key, value);
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
	out.write((const char*)(&value), sizeof(T));
}

// save std::vector
template <typename T>
void writeVar(std::ofstream& out, const std::vector<T>& vec) {
	size_t size = vec.size();
	out.write((const char*)&size, sizeof(size));
	out.write((const char*)vec.data(), size * sizeof(T));
}

// save a std::string (length-prefixed). must be declared before writeAll so the
// variadic dispatch picks this overload instead of raw-copying the string object.
inline void writeVar(std::ofstream& out, const std::string& value) {
	size_t size = value.size();
	out.write((const char*)&size, sizeof(size));
	out.write(value.data(), size);
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

