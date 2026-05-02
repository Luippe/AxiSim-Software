#pragma once
#include "setting.cuh"
#include "solver_struct.h"

// initialize
__global__
void init(const Config config, VariablesBiCGStab c, double conc, int N);

// get wall variables
__global__
void get_wall_varj(const Config config, VariablesBiCGStab c, int N);

// solve rhs of concentration field
__device__
void get_oxy_rhsj(const Config& config, VariablesBiCGStab& c, double oxy_in, int i, int j, int n);

// solve coefficients
__device__
void get_oxy_coeffj(const Config& config, VariablesBiCGStab& c, int i, int j, int n);

__global__
void init_alpha(const Config config, VariablesBiCGStab c, int N);

// calculate numerator and denominator of alpha
__global__
void get_rho(const Config config, VariablesBiCGStab c, int N);

// update residual and jz and new guess for x
__global__
void update_r_x(const Config config, VariablesBiCGStab c, int N);

// calculate s and s_t
__global__
void get_s_s_t(const Config config, VariablesBiCGStab c, int N);

__global__
void get_jp(const Config config, VariablesBiCGStab c, int N);

__global__
void get_jp_t(const Config config, VariablesBiCGStab c, int N);

__global__
void get_v_alpha(const Config config, VariablesBiCGStab c, int N);

__global__
void get_t_w(const Config config, VariablesBiCGStab c, int N);

__global__
void get_cw(const Config config, VariablesBiCGStab c, int N);

// solve initial residual
__global__
void get_res_init(const Config config, VariablesBiCGStab c, int N);

__global__
void set_x(const Config config, VariablesBiCGStab c, int N);

__global__
void sum_block(int N, double* __restrict__ in, double* __restrict__ out);

// get sum of vectors. reduction kernel continues to reduce vector until a scalar remains
void reduce_vec(VariablesBiCGStab& c, int N, int threadsPerBlock,  size_t shmem, double* in, double* store, cudaStream_t stream);

// calculate OCR
__global__
void get_OCR(const Config config, VariablesBiCGStab c, double oxy_in, int N);

__global__
void calc_beta(VariablesBiCGStab c);

__global__
void calc_alpha(VariablesBiCGStab c);

__global__
void calc_w(VariablesBiCGStab c);

__global__
void init_val(VariablesBiCGStab c);

__global__
void testing(VariablesBiCGStab c);

__global__
void update_preconditioner(const Config config, VariablesBiCGStab c, int N);

// solve MM equation
__device__
double MM(const FluidPropertyConfig& f, double conc);

// solve derivative of MM equation
__device__
double dMM(const FluidPropertyConfig& f, double conc);
