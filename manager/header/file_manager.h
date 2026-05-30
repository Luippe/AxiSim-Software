#pragma once
#include "setting.cuh"
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_set>

class Solution;
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

// ======================================================================
// -----------------------MESH-------------------------------------------
// ======================================================================
// save mesh by opening explorer
void saveFromExplorerMesh(Mesh& mesh);

// save mesh given a path
void saveFromPathMesh(const char* path, Mesh& mesh);

// load mesh from explorer
void loadFromExplorerMesh(Mesh& mesh);

// load mesh from given path
void loadFromPathMesh(const char* path, Mesh& mesh);

// save mesh which will be launched when application opens
void saveLaunchMesh(Mesh& mesh);

// ======================================================================
// -----------------------SOLVER-----------------------------------------
// ======================================================================
// save solver by opening explorer
void saveFromExplorerSolver(Solver& solver);

// save solver given a path
void saveFromPathSolver(const char* path, Solver& solver);

// load all necessary variables for the solver config and boundary conditions
void loadFromPathSolver(const char* path, Solver& solver);

// load solver after opening explorer
void loadFromExplorerSolver(Solver& solver);

// save solver which will be launched when application oepns
void saveLaunchSolver(Solver& solver);

// write boundarycondtion struct to save file
void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc);

// write boundary condition config to save file
void writeBoundaryConditionConfig(std::ofstream& out, const BoundaryConditionConfig& bcConfig);

// read boundary condition from save file
void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc);

void readOneBoundaryCondition(std::ifstream& in, BoundaryConditionConfig& bcConfig);

// load selected files when the application launches
void loadAtLaunch(Mesh& mesh, Solver& solver, Results& results);

// read boundary condition config from save file
template<typename...Args>
void readBoundaryConditionConfig(std::ifstream& in, Args&... args) {
	(readOneBoundaryCondition(in, args),...);
}


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

// save std::unordered_set
template <typename T,
		typename Hash,
		typename KeyEqual,
		typename Allocator>
void writeVar(std::ofstream& out, const std::unordered_set<T, Hash, KeyEqual, Allocator>& set) {
	size_t size = set.size();

	out.write((const char*)&size, sizeof(size));

	for (const T& value : set) {
		out.write((const char*)&value, sizeof(T));
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

