#pragma once
#include "setting.cuh"
#include <string>
#include <fstream>
#include <filesystem>

class Solution;
class Mesh;
class Solver;
class Results;
struct Config;
struct BoundaryCondition;
struct BoundaryConditionConfig;

// check if file exists
bool fileExists(const std::string& filename);

// load variables from a file
void loadVelocity(GridConfig& g, FluidPropertyConfig& f);

// save mesh by opening explorer
void saveFromExplorerMesh(Mesh& mesh);

// save mesh given a path
void saveFromPathMesh(const char* path, Mesh& mesh);

// load mesh from explorer
void loadFromExplorerMesh(Mesh& mesh);

// load mesh from given path
void loadFromPathMesh(const char* path, Mesh& mesh);

// save solver by opening explorer
void saveFromExplorerSolver(Solver& solver);

// save solver given a path
void saveFromPathSolver(const char* path, Solver& solver);

// load all necessary variables for the solver config and boundary conditions
void loadFromPathSolver(const char* path, Solver& solver);

// load solver after opening explorer
void loadFromExplorerSolver(Solver& solver);

// save mesh which will be launched when application opens
void saveLaunchMesh(Mesh& mesh);

// save solver which will be launched when application oepns
void saveLaunchSolver(Solver& solver);

// write boundarycondtion struct to save file
void writeBoundaryCondition(std::ofstream& out, const BoundaryCondition& bc);

// write boundary condition config to save file
void writeBoundaryConditionConfig(std::ofstream& out, const BoundaryConditionConfig& bcConfig);

// read boundary condition from save file
void readBoundaryCondition(std::ifstream& in, BoundaryCondition& bc);

// read boundary condition config from save file
void readBoundaryConditionConfig(std::ifstream& in, BoundaryConditionConfig& bcConfig);

// load selected files when the application launches
void loadAtLaunch(Mesh& mesh, Solver& solver, Results& results);

template <typename T>
void writeVar(std::ofstream& out, const T& value) {
	out.write((const char*)(&value), sizeof(T));
}

// save one std::vector
template <typename T>
void writeVar(std::ofstream& out, const std::vector<T>& vec) {
	size_t size = vec.size();
	out.write((const char*)&size, sizeof(size));
	out.write((const char*)vec.data(), size * sizeof(T));
}

template<typename T>
void readValue(std::ifstream& in, T& val) {
	in.read((char*)&val, sizeof(T));
}

template <typename... Args>
void writeAll(std::ofstream& out, const Args&... args) {
	(writeVar(out, args), ...);
}

// main save function
template <typename... Args>
void saveBinary(const std::string& filename, const Args&... args) {
	std::ofstream out(filename, std::ios::binary);

	if (!out) {
		throw std::runtime_error("Could not open file: " + filename);
	}

	writeAll(out, args...);
}