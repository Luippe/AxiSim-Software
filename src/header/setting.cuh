#pragma once

#include <vector>
#include <unordered_set>
#include <numbers>

#ifdef __INTELLISENSE__
void __syncthreads();
#endif

constexpr double PI = std::numbers::pi;

// define domain and cell spacing
struct GridConfig {

	int nr = 170;
	int nz = 100;
	double R = 0.0017;
	double L = 0.01;
	std::vector<double> dz;
	std::vector<double> dr;
	std::vector<double> r;
	std::vector<double> z;
	std::vector<double> rFace;
	std::vector<double> zFace;

	std::vector<double> Az;
	std::vector<double> Ar;
	std::vector<double> Vcell;

	double zBias = 1.0;
	double rBias = 1.0;

	int n_cell = 0;
	double A_tot = 0.0;

};

struct MemoryConfig {

	int threadsPerBlock = 256;
	int faceThreads = 128;

	int shmem = 0;
	int shmemFace = 0;
	int blocks = 0;
	int faceBlocks = 0;

	void init(int N, int NFaces) {

		shmem = threadsPerBlock * sizeof(double);
		shmemFace = faceThreads * sizeof(double);

		blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
		faceBlocks = (NFaces + faceThreads - 1) / faceThreads;

	}
};

// fluid variables
struct FluidPropertyConfig {

	double rho = 998.0;
	double mu = 0.0010518;

	double cp = 4180.0;
	double k = 0.6;

	double D = 3.0277e-9;

};


