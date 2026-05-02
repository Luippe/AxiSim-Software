#pragma once
//#include "device_launch_parameters.h"
#include <vector>
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
	double dz;
	double dr;
	int N;

	int n_cell;
	double cell_top = 1.5;
	double cell_left = 4.0;
	double cell_thickness = 0.5;
	double cell_right = 4.5;
	double A_tot;
	double kl;

	int* c_cell;
	int* z_cell;
	int* r_cell;
	double* r;
	double* A;	// cell surface area
	int* surf_index;	// list of n indices which belong to cell adjacent cells
	double* dist;	// distance from adjacent cell to cell surface
	int* wall_cell;	// -1 = fluid cell. store index number of surf_index on cell adjacent cells
	double* kf;

	std::vector<int> c_cell_vec;
	std::vector<int> v_cell_vec;

};

struct MemoryConfig {
	int threadsPerBlock = 512;
	int shmem = threadsPerBlock * sizeof(double);
};



// fluid variables
struct FluidPropertyConfig {

	double rho = 998 / 1000 ^ 3;
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

	double* u;
	double* v;

};


