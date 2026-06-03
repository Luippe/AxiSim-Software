#pragma once
//#include "device_launch_parameters.h"
#include <vector>
#include <unordered_set>

#ifdef __INTELLISENSE__
void __syncthreads();
#endif

constexpr double PI = 3.14159265358979323846;

// define domain and cell spacing
struct GridConfig {

	int nr = 170;
	int nz = 100;
	double R = 1.7;
	double L = 10.0;
	std::vector<double> dz;
	std::vector<double> dr;
	std::vector<double> r;
	std::vector<double> z;
	std::vector<double> rFace;
	std::vector<double> zFace;

	std::vector<double> Az;
	std::vector<double> Ar;
	std::vector<double> Vcell;

	std::vector<uint8_t> activeCell;
	std::unordered_set<int> obstacleIndices;

	double zBias = 1.0;
	double rBias = 1.0;

	int N = 0;
	int n_cell = 0;
	double A_tot = 0.0;
	double kl = 0.0;

	int* c_cell;
	int* z_cell;
	int* r_cell;

	double* d_dr;
	double* d_dz;
	double* d_r;
	double* d_z;
	double* d_rFace;
	double* d_zFace;
	double* z_dz;
	double* r_dr;

	double* d_Az;
	double* d_Ar;
	double* d_Vcell;
	uint8_t* d_activeCell = nullptr;

	double* A;	// cell surface area
	int* surf_index;	// list of n indices which belong to cell adjacent cells
	double* dist;	// distance from adjacent cell to cell surface
	int* wall_cell;	// -1 = fluid cell. store index number of surf_index on cell adjacent cells
	double* kf;

};

struct MemoryConfig {
	int threadsPerBlock = 512;
	int shmem = threadsPerBlock * sizeof(double);
};

// fluid variables
struct FluidPropertyConfig {

	double rho = 998.0 / 1e9;
	double mu = 0.0010518 / 1000;
	double Vmax = 2e-4;
	double Km = 5e-4;
	double n = 1.0;
	double m = 1.0;
	double K2 = 0.4907382388;
	double V2 = 0.0;
	double d = 0.313;

	double D_isf = 1e-4;
	double D = 3.0277e-3;
	double Umax = 8.444199e-2;

};


