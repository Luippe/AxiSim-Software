#include "jacobi_pcg.cuh"
#include "device_launch_parameters.h"
#include "printer.h"

__global__
void init(const Config config, VariablesBiCGStab vars, double conc, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	int i = n / config.g.nz;
	int j = n % config.g.nz;
	get_oxy_coeffj(config, vars, i, j, n);
	get_oxy_rhsj(config, vars, conc, i, j, n);

}

__global__
void init_alpha(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	double* alpha = vars.alpha;
	double* kf = g.kf;
	double kl = g.kl;
	double d = f.d;

	if (d < 1e-12) {
		alpha[n] = kf[n];
	}
	else {
		//printf("%e\n", d);
		alpha[n] = kf[n] * kl / (kf[n] + kl);
	}
}


__global__
void get_wall_varj(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;
	const IterationConfig& itr = config.itr;

	double cs_iter = itr.cs_iter;
	double cs_tol = itr.cs_tol;
	double* cs = vars.cs;
	double* oxy = vars.oxy;
	double* alpha = vars.alpha;
	double* beta = vars.beta;
	double* cp = vars.cp;
	double m_MM, m_dMM, F, dF;
	int* surf_index = g.surf_index;
	int num = surf_index[n];

	cp[n] = oxy[num];
	cp[n] = fmax(0.0, cp[n]);
	//cp[n] = fmin(1.0, cp[n]);

	//if (n == 0) {
	//	printf("cs: %e\n", cs[n]);
	//}
	double relax = 0.5;
	// start iteration for cs
	for (int outer = 0; outer < cs_iter; ++outer) {
		m_MM = MM(f, cs[n]);
		m_dMM = dMM(f, cs[n]);
		F = alpha[n] * (cp[n] - cs[n]) - m_MM;
		dF = -alpha[n] - m_dMM;

		if (fabs(F / dF) < cs_tol) break;
		cs[n] -= (F / dF);
		cs[n] = fmax(0.0, cs[n]);

		//if (cs[n] == 0) {
		//	printf("cs is 0 at %d\n", n);
		//}
		//if (fabs(F / dF) < cs_tol) break;
		//cs[n] = ((1 - relax) * cs[n]) + (relax * (cs[n] - (F / dF)));
		//cs[n] = fmax(0.0, cs[n]);
		//if (outer == cs_iter - 1) {
		//	printf("Warning: cs not converged at cell %d, cs = %e, cp = %e, res = %e\n", n, cs[n], cp[n], fabs(F / dF));
		//}
	}

	beta[n] = -alpha[n] * cs[n];
}

__device__
void get_oxy_coeffj(const Config& config, VariablesBiCGStab& vars, int i, int j, int n) {

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	int nr = g.nr;
	int nz = g.nz;
	//printf("%d, %d\n", nr, nz);
	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;

	double* ACnew = vars.ACnew;
	double* AKE = vars.AKE;
	double* AKW = vars.AKW;
	double* AKN = vars.AKN;
	double* AKS = vars.AKS;
	double* u = f.u;
	double* v = f.v;
	int* cell = g.c_cell;
	double D = f.D;

	// fill ACnew and AK
	ACnew[n] = 0.0;
	AKE[n] = 0.0;
	AKW[n] = 0.0;
	AKN[n] = 0.0;
	AKS[n] = 0.0;

	if (cell[n] != 1) {
		// calculate radius and surfACnewe area
		double r1 = r[i] - (0.5 * dr);
		double r2 = r[i] + (0.5 * dr);
		double Az = PI * (r2 * r2 - r1 * r1);
		double Ar2 = 2 * PI * r2 * dz;
		double Ar1 = 2 * PI * r1 * dz;

		// east
		if (j == nz - 1) {
			AKE[n] = 0.0;
		}
		else if (cell[n + 1] == 1) {
			AKE[n] = 0.0;
		}
		else {
			double me = Az * u[n + i + 1];
			AKE[n] += -(D * Az / dz) - fmax(-me, 0.0);
		}

		// west
		if (j == 0) {
			AKW[n] = 0.0;
			double mw = Az * u[n + i];
			ACnew[n] += (D * Az / (0.5 * dz)) + fmax(mw, 0.0);
		}
		else if (cell[n - 1] == 1) {
			AKW[n] = 0.0;
		}
		else {
			double mw = Az * u[n + i];
			AKW[n] += -(D * Az / dz) - fmax(mw, 0.0);
		}

		// north
		if (i == nr - 1) {
			AKN[n] = 0.0;
		}
		else if (cell[n + nz] == 1) {
			AKN[n] = 0.0;
		}
		else {
			double mn = Ar2 * v[n + nz];
			AKN[n] += -(D * Ar2 / dr) - fmax(-mn, 0.0);
		}

		// south
		if (i == 0) {
			AKS[n] = 0.0;
		}
		else if (cell[n - nz] == 1) {
			AKS[n] = 0.0;
		}
		else {
			double ms = Ar1 * v[n];
			AKS[n] += -(D * Ar1 / dr) - fmax(ms, 0.0);
		}
		ACnew[n] += -(AKE[n] + AKW[n] + AKN[n] + AKS[n]);
	}
}

__device__
void get_oxy_rhsj(const Config& config, VariablesBiCGStab& vars, double oxy_in, int i, int j, int n) {

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	double dr = g.dr;
	double dz = g.dz;
	double* r = g.r;
	double* rhsnew = vars.foxynew;
	double* u = f.u;
	double D = f.D;

	rhsnew[n] = 0.0;

	// add inlet contribution
	if (j == 0) {

		double r1 = r[i] - (0.5 * dr);
		double r2 = r[i] + (0.5 * dr);

		double Az = PI * (r2 * r2 - r1 * r1);

		double m_in = Az * u[n + i];
		rhsnew[n] += ((D * Az / (0.5 * dz)) + m_in) * oxy_in;

	}
}

__global__
void get_rho(const Config config, VariablesBiCGStab vars, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int* cell = g.c_cell;
	double* jrho = vars.jrho;
	double* res = vars.res;
	double* res_t = vars.res_t;

	if (cell[n] != 1) {
		jrho[n] = res[n] * res_t[n];
	}
}

__global__
void update_r_x(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	double jalpha_val = *vars.jalpha_val;
	double jw_val = *vars.jw_val;
	double* js = vars.js;
	double* js_t = vars.js_t;
	double* jt = vars.jt;
	double* oxy = vars.oxy;
	double* res = vars.res;
	double* jp_t = vars.jp_t;
	double* jv = vars.jv;
	double* resnorm = vars.resnorm;
	int* cell = g.c_cell;

	if (cell[n] != 1) {
		oxy[n] += (jalpha_val * jp_t[n]) + (jw_val * js_t[n]);
		res[n] = js[n] - (jw_val * jt[n]);
	}
}

__global__
void get_jp(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}
	const GridConfig& g = config.g;

	double* jp = vars.jp;
	double* res = vars.res;
	double* jv = vars.jv;
	double jw_val = *vars.jw_val;
	double jbeta_val = *vars.jbeta_val;
	int* cell = g.c_cell;

	if (cell[n] != 1) {
		jp[n] = res[n] + jbeta_val * (jp[n] - (jw_val * jv[n]));
	}
}

__global__
void get_jp_t(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	double* jp = vars.jp;
	double* jv = vars.jv;
	double* AC = vars.ACC;
	double* jp_t = vars.jp_t;
	int* cell = g.c_cell;

	if (cell[n] != 1) {

		jp_t[n] = jp[n] / AC[n];
		//jp_t[n] = jp[n];

	}
}

__global__
void get_v_alpha(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int nr = g.nr;
	int nz = g.nz;
	int* cell = g.c_cell;

	double* AC = vars.ACC;
	double* AKE = vars.AKE;
	double* AKW = vars.AKW;
	double* AKN = vars.AKN;
	double* AKS = vars.AKS;

	double* jp_t = vars.jp_t;
	double* jv = vars.jv;
	double* alpha_den = vars.alpha_den;
	double* res_t = vars.res_t;

	if (cell[n] != 1) {
		int i = n / nz;
		int j = n % nz;
		double jvnew = 0.0;
		// east
		if (j != nz - 1) {
			jvnew += AKE[n] * jp_t[n + 1];
		}
		// west
		if (j != 0) {
			jvnew += AKW[n] * jp_t[n - 1];
		}
		// north
		if (i != nr - 1) {
			jvnew += AKN[n] * jp_t[n + nz];
		}
		// south
		if (i != 0) {
			jvnew += AKS[n] * jp_t[n - nz];
		}
		//printf("%e, %e, %e, %e\n", jp_t[n + nr], jp_t[n - nr], jp_t[n + 1], jp_t[n - 1]);
		jvnew += AC[n] * jp_t[n];
		jv[n] = jvnew;
		alpha_den[n] = res_t[n] * jvnew;
	}
}

__global__
void get_s_s_t(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int* cell = g.c_cell;

	double* js = vars.js;
	double* js_t = vars.js_t;
	double* res = vars.res;
	double jalpha_val = *vars.jalpha_val;
	double* jv = vars.jv;
	double* AC = vars.ACC;
	double* snorm = vars.snorm;


	if (cell[n] != 1) {
		js[n] = res[n] - (jalpha_val * jv[n]);
		js_t[n] = js[n] / AC[n];
		//js_t[n] = js[n];
		snorm[n] = js[n] * js[n];
	}
}

__global__
void get_t_w(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int nr = g.nr;
	int nz = g.nz;
	int* cell = g.c_cell;

	double* js = vars.js;
	double* js_t = vars.js_t;
	double* jt = vars.jt;

	double* AC = vars.ACC;
	double* AKE = vars.AKE;
	double* AKW = vars.AKW;
	double* AKN = vars.AKN;
	double* AKS = vars.AKS;

	double* w_num = vars.w_num;
	double* w_den = vars.w_den;

	if (cell[n] != 1) {

		int i = n / nz;
		int j = n % nz;
		double jtnew = 0.0;

		// east
		if (j != nz - 1) {
			jtnew += AKE[n] * js_t[n + 1];
		}
		// west
		if (j != 0) {
			jtnew += AKW[n] * js_t[n - 1];
		}
		// north
		if (i != nr - 1) {
			jtnew += AKN[n] * js_t[n + nz];
		}
		// south
		if (i != 0) {
			jtnew += AKS[n] * js_t[n - nz];
		}
		jtnew += AC[n] * js_t[n];
		//printf("%e, %e, %e, %e\n", js_t[n + nr], js_t[n - nr], js_t[n + 1], js_t[n - 1]);
		jt[n] = jtnew;
		w_num[n] = js[n] * jtnew;
		w_den[n] = jtnew * jtnew;

	}
}


__global__
void get_cw(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;
	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	double* kf = g.kf;
	double kl = g.kl;
	int* cell = g.c_cell;

	double* cp = vars.cp;
	double* cw = vars.cw;
	double* cs = vars.cs;


	if (cell[n] != 1) {
		cw[n] = (kf[n] * cp[n] + kl * cs[n]) / (kf[n] + kl);
	}
}

__global__
void get_res_init(const Config config, VariablesBiCGStab vars, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int nr = g.nr;
	int nz = g.nz;
	double* A = g.A;
	int* cell = g.c_cell;

	double* rhs = vars.foxy;
	double* oxy = vars.oxy;
	double* ACnew = vars.ACnew;
	double* rhsnew = vars.foxynew;
	double* alpha = vars.alpha;
	double* beta = vars.beta;
	double* AC = vars.ACC;
	double* AKE = vars.AKE;
	double* AKW = vars.AKW;
	double* AKN = vars.AKN;
	double* AKS = vars.AKS;

	double* res = vars.res;
	double* res_t = vars.res_t;
	double* jp = vars.jp;
	double* resnorm = vars.resnorm;

	// solve for res and res_t
	if (cell[n] != 1) {
		int i = n / nz;
		int j = n % nz;
		double oxynew = rhs[n];

		// east
		if (j != nz - 1) {
			oxynew -= AKE[n] * oxy[n + 1];
		}
		// west
		if (j != 0) {
			oxynew -= AKW[n] * oxy[n - 1];
		}
		// north
		if (i != nr - 1) {
			oxynew -= AKN[n] * oxy[n + nz];
		}
		// south
		if (i != 0) {
			oxynew -= AKS[n] * oxy[n - nz];
		}
		res[n] = oxynew - (AC[n] * oxy[n]);
		res_t[n] = res[n];
		jp[n] = res[n];
		resnorm[n] = res[n] * res[n];
	}
}

__global__
void set_x(const Config config, VariablesBiCGStab vars, int N) {
	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}
	const GridConfig& g = config.g;

	int* cell = g.c_cell;

	double* oxy = vars.oxy;
	double jalpha_val = *vars.jalpha_val;
	double* jp_t = vars.jp_t;

	if (cell[n] != 1) {
		oxy[n] += jalpha_val * jp_t[n];
	}
}

__global__
void sum_block(int N, double* __restrict__ in, double* __restrict__ out) {
	extern __shared__ double s[];
	int n = blockIdx.x * blockDim.x + threadIdx.x;
	int tid = threadIdx.x;		// thread id within the block

	s[tid] = (n < N) ? in[n] : 0.0;
	__syncthreads();

	// if you have [0, 1, 2, 3] as your input, the next iteration will give [2, 4]
	// the first element adds the third, and the second element adds the fourth to itself.
	for (int stride = blockDim.x / 2; stride > 0; stride /= 2) {
		if (tid < stride) {
			s[tid] += s[tid + stride];
		}
		__syncthreads();
	}

	if (tid == 0) {// store result for each block
		out[blockIdx.x] = s[0];
	}
}


void reduce_vec(VariablesBiCGStab& vars, int N, int threadsPerBlock, size_t shmem, double* in, double* store, cudaStream_t stream) {
	int m = N;
	double* out = vars.tmpA;
	double* alt = vars.tmpB;

	while (m > 1) {
		int blocks = (m + threadsPerBlock - 1) / threadsPerBlock;
		double* out_ptr = (blocks == 1) ? store : out;

		sum_block << <blocks, threadsPerBlock, shmem, stream >> > (m, in, out_ptr);
		//in = out;
		//std::swap(out, alt);
		in = out_ptr;
		if (blocks != 1) std::swap(out, alt);
		m = blocks;
	}
}

__global__
void get_OCR(const Config config, VariablesBiCGStab vars, double oxy_in, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;
	const FluidPropertyConfig& f = config.f;

	double* A = g.A;
	double* OCR_num = vars.OCR_num;
	double* cs = vars.cs;
	//double* cp = vars.cp;

	//printf("%f\n", cs[N]);
	OCR_num[n] = A[n] * MM(f, cs[n]);
	//OCR_num[n] = cs[n];
	//printf("%d\n", OCR_num[n]);
	//OCR_num[n] = cs[n];
	//if (n == 0) {
	//	int j = 1;
	//	int nr = g.nr;
	//	int nz = g.nz;
	//	double dr = g.dr;
	//	double* r = g.r;
	//	double OCR_in_out = 0.0;
	//	double tot_flux = 0.0;
	//	for (int i = 0; i < nr; ++i) {
	//		double r1 = r[i] - (0.5 * dr);
	//		double r2 = r[i] + (0.5 * dr);
	//		double Az = PI * (r2 * r2 - r1 * r1);
	//		int n_in = i;
	//		int n_out_u = (nz * nr) + i;
	//		int n_out_c = ((nz - 1) * nr) + i;
	//		double flux_in = Az * f.u[n_in] * oxy_in;
	//		double flux_out = Az * f.u[n_out_u] * vars.oxy[n_out_c];
	//		//printf("%f\n", flux_in - flux_out);
	//		tot_flux += (flux_in - flux_out);
	//	}
	//	//printf("%f\n", tot_flux);
	//	OCR_in_out = tot_flux / (g.A_tot * f.Vmax);
	//	//OCR_num[n] = tot_flux;
	//	printf("%f\n", OCR_in_out);
	//	//*vars.OCR_num_val = OCR_in_out;
	//}

}

__global__
void calc_beta(VariablesBiCGStab vars) {
	*vars.jbeta_val = (*vars.jrho_val / *vars.jrho_val_prev) * (*vars.jalpha_val / *vars.jw_val);
}


__global__
void calc_alpha(VariablesBiCGStab vars) {
	*vars.jalpha_val = *vars.jrho_val / *vars.jalpha_den_val;
}

__global__
void calc_w(VariablesBiCGStab vars) {
	*vars.jw_val = *vars.jw_num_val / *vars.jw_den_val;
}

__global__
void init_val(VariablesBiCGStab vars) {
	*vars.jrho_val_prev = 1.0;
	*vars.jrho_val = 1.0;
	*vars.jw_val = 1.0;
	*vars.jbeta_val = 0.0;
	*vars.jalpha_val = 1.0;
	*vars.jw_num_val = 0.0;
	*vars.jw_den_val = 0.0;
	*vars.jalpha_den_val = 0.0;
	*vars.snorm_val = 0.0;
}


__global__
void testing(VariablesBiCGStab vars) {
	//vars.jrho_val = nullptr;
	//*vars.jrho_val_prev = *vars.jrho_val;
	printf("%e, %e, %e, %e\n", *vars.jrho_val, *vars.jrho_val_prev, *vars.jalpha_val, *vars.jbeta_val);
	//printf("%e\n", sqrt(*vars.resnorm_val));
	//printf("at testing %p, %p, %p\n", vars.jrho_val, vars.jrho_val_prev, vars.jrho);
}

__global__
void update_preconditioner(const Config config, VariablesBiCGStab vars, int N) {

	int n = blockIdx.x * blockDim.x + threadIdx.x;


	if (n >= N) {    // make sure extra threads do nothing. or else it will do extra computations
		return;
	}

	const GridConfig& g = config.g;

	int* wall_cell = g.wall_cell;
	int* cell = g.c_cell;
	double* A = g.A;

	double* rhs = vars.foxy;
	double* ACnew = vars.ACnew;
	double* rhsnew = vars.foxynew;
	double* alpha = vars.alpha;
	double* beta = vars.beta;
	double* AC = vars.ACC;

	AC[n] = 0.0;
	rhs[n] = 0.0;

	// add fluid contribution
	if (cell[n] != 1) {
		AC[n] = ACnew[n];
		rhs[n] = rhsnew[n];
	}

	// add cell wall contribution
	if (wall_cell[n] != -1) {
		int num = wall_cell[n];
		AC[n] += alpha[num] * A[num];
		rhs[n] += -beta[num] * A[num];
	}
}

__device__
double MM(const FluidPropertyConfig& f, double c) {
	double Vmax = f.Vmax;
	double Km = f.Km;
	double n = f.n;
	double m = f.m;
	double K2 = f.K2;
	double V2 = f.V2;

	double c_n = pow(c, n);
	double c_m = pow(c, m);
	double K2_m = pow(K2, m);
	double Km_n = pow(Km, n);

	return ((Vmax * c_n) / (Km_n + c_n)) * (1.0 - ((V2 * c_m / (K2_m + c_m))));
}

__device__
double dMM(const FluidPropertyConfig& f, double c) {
	double Vmax = f.Vmax;
	double Km = f.Km;
	double n = f.n;
	double m = f.m;
	double K2 = f.K2;
	double V2 = f.V2;


	if (c == 0.0) {	// if c = 0, the equation simplifies significantly. it will cause NaN if we try solving below
		return (n * Vmax / pow(Km, n));
	}
	else {
		double c_n = pow(c, n);
		double c_m = pow(c, m);
		double K2_m = pow(K2, m);
		double Km_n = pow(Km, n);

		return ((Vmax * c_n) / (Km_n + c_n)) *
			(-(m * V2 * K2_m) * (c_m) / (c * (K2_m + c_m) * (K2_m + c_m))) +
			(1.0 - (V2 * c_m / (K2_m + c_m))) *
			((n * Vmax * Km_n) * (c_n) / (c * (Km_n + c_n) * (Km_n + c_n)));
	}
}
