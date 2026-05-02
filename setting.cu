#include "setting.cuh"
#include "math_func.h"
#include <cuda.h>

#define CUDA_CHECK(x) do { \
  cudaError_t err = (x); \
  if (err != cudaSuccess) { \
    printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    std::abort(); \
  } \
} while(0)

template<typename T>
T* deviceAlloc(size_t count) {
	T* ptr = nullptr;
	cudaMalloc(&ptr, count * sizeof(T));
	return ptr;
}

template <typename T>
T* deviceCopy(const T* host, size_t count) {
	T* ptr = deviceAlloc<T>(count);
	cudaMemcpy(ptr, host, count * sizeof(T), cudaMemcpyHostToDevice);
	return ptr;
}

// initialize variables
void allocate_GridConfig(GridConfig& g, FluidPropertyConfig& f) {

	int nr = g.nr;
	int nz = g.nz;
	int N = nr * nz;
	double dz = g.L / nz;
	double dr = g.R / nr;

	double Umax = f.Umax;
	double R = g.R;
	double cell_top = g.cell_top;
	double cell_left = g.cell_left;
	double cell_right = g.cell_right;
	double D = f.D;
	double D_isf = f.D_isf;
	double d = f.d;

	// radial location
	std::vector<double> r = linspace(dr / 2, R - (dr / 2), nr);

	// fill in cell data
	std::vector<int> c_cell(nr * nz, 0);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = j * nr + i;
			//int n = i * nz + j;
			double xf = j * dz;
			double yf = i * dr;
			double xc = (dz / 2) + xf;
			double yc = (dr / 2) + yf;

			if (xc >= cell_left && xc <= cell_right && yc <= cell_top && i != nr && j != nz) {
				c_cell[n] = 1;
			}
		}
	}

	// fill in wall vertex data 0 = fluid, 1 = cell, 2 = cell_wall
	//for (int i = 0; i < nr + 1; ++i) {
	//	for (int j = 0; j < nz + 1; ++j) {
	//		//int n = j * nr + i;
	//		int n = i * nz + j;
	//		double xf = j * dz;
	//		double yf = i * dr;

	//	}
	//}
	
	// get cell adjacent surface area and its index
	double A_tot = 0.0;
	std::vector<double> A;
	std::vector<int> surf_index;
	std::vector<double> dist;
	std::vector<double> kf;
	std::vector<int> wall_cell(nr * nz, -1);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = j * nr + i;
			//int n = i * nz + j;
			if (c_cell[n] != 1) {
				double r1 = r[i] - (dr / 2);
				double r2 = r[i] + (dr / 2);

				double Az = PI * (r2 * r2 - r1 * r1);
				double Ar2 = 2 * PI * r2 * dz;
				double Ar1 = 2 * PI * r1 * dz;

				// east
				if (j != nz - 1) {
					if (c_cell[n + nr] == 1) {
					//if (c_cell[n + 1] == 1) {
						A_tot += Az;
						A.push_back(Az);
						surf_index.push_back(n);
						dist.push_back(0.5 * dz);
						kf.push_back(D / (0.5 * dz));
					}
				}

				// west
				if (j != 0) {
					if (c_cell[n - nr] == 1) {
					//if (c_cell[n - 1] == 1) {
						A_tot += Az;
						A.push_back(Az);
						surf_index.push_back(n);
						dist.push_back(0.5 * dz);
						kf.push_back(D / (0.5 * dz));
					}
				}

				// south
				if (i != 0) {
					if (c_cell[n - 1] == 1) {
					//if (c_cell[n - nz] == 1) {
						A_tot += Ar1;
						A.push_back(Ar1);
						surf_index.push_back(n);
						dist.push_back(0.5 * dr);
						kf.push_back(D / (0.5 * dr));
					}
				}
			}
		}
	}

	g.N = N;
	g.dr = dr;
	g.dz = dz;

	int n_cell = surf_index.size();
	g.c_cell_vec = c_cell;
	g.A_tot = A_tot;
	g.n_cell = n_cell;
	double kl = D_isf / d;
	g.kl = kl;
	for (int num = 0; num < n_cell; ++num) {
		int cell = surf_index[num];
		wall_cell[cell] = num;
	}

	// allocate GridConfig variables and kf
	g.r = deviceCopy(r.data(), r.size());
	g.c_cell = deviceCopy(c_cell.data(), c_cell.size());
	g.wall_cell = deviceCopy(wall_cell.data(), wall_cell.size());
	g.A = deviceCopy(A.data(), A.size());
	g.surf_index = deviceCopy(surf_index.data(), surf_index.size());
	g.dist = deviceCopy(dist.data(), dist.size());
	g.kf = deviceCopy(kf.data(), kf.size());

}

void allocate_cell(GridConfig& g, FluidPropertyConfig& f, Cell& c) {

	int nr = g.nr;
	int nz = g.nz;
	int n = nr * nz;
	int n_cell = g.n_cell;
	double conc = c.conc;

	//std::vector<double> h_oxy(nr * nz, 0.0);
//std::vector<double> h_cs(n_cell, 0.0);
//std::vector<double> h_cw(n_cell, 0.0);
//std::vector<double> h_cp(n_cell, 0.0);

	// vectors
	std::vector<double> h_ACC(n, 0.0);
	std::vector<double> h_AKE(n, 0.0);
	std::vector<double> h_AKW(n, 0.0);
	std::vector<double> h_AKN(n, 0.0);
	std::vector<double> h_AKS(n, 0.0);
	std::vector<double> h_foxy(n, 0.0);
	std::vector<double> h_oxy(n, conc);
	std::vector<double> h_cs(n_cell, conc);
	std::vector<double> h_cw(n_cell, conc);
	std::vector<double> h_cp(n_cell, conc);
	std::vector<double> h_beta(n_cell, 0.0);
	std::vector<double> h_alpha(n_cell, 0.0);
	std::vector<double> h_res(n, 0.0);
	std::vector<double> h_res_t(n, 0.0);
	std::vector<double> h_jp(n, 0.0);
	std::vector<double> h_jp_t(n, 0.0);
	std::vector<double> h_jw(n, 0.0);
	std::vector<double> h_alpha_den(n, 0.0);
	std::vector<double> h_w_num(n, 0.0);
	std::vector<double> h_w_den(n, 0.0);
	std::vector<double> h_ACnew(n, 0.0);
	std::vector<double> h_foxynew(n, 0.0);
	std::vector<double> h_jrho(n, 0.0);
	std::vector<double> h_jv(n, 0.0);
	std::vector<double> h_js(n, 0.0);
	std::vector<double> h_js_t(n, 0.0);
	std::vector<double> h_jt(n, 0.0);
	std::vector<double> h_snorm(n, 0.0);
	std::vector<double> h_resnorm(n, 0.0);
	std::vector<double> h_OCR_num(n_cell, 0.0);

	// allocate memory
	cudaMalloc(&c.tmpA, n * sizeof(double));
	cudaMalloc(&c.tmpB, n * sizeof(double));

	c.ACC = deviceCopy(h_ACC.data(), h_ACC.size());
	c.AKE = deviceCopy(h_AKE.data(), h_AKE.size());
	c.AKW = deviceCopy(h_AKW.data(), h_AKW.size());
	c.AKN = deviceCopy(h_AKN.data(), h_AKN.size());
	c.AKS = deviceCopy(h_AKS.data(), h_AKS.size());
	c.foxy = deviceCopy(h_foxy.data(), h_foxy.size());
	c.ACnew = deviceCopy(h_ACnew.data(), h_ACnew.size());
	c.foxynew = deviceCopy(h_foxynew.data(), h_foxynew.size());
	c.res_t = deviceCopy(h_res_t.data(), h_res_t.size());
	c.jrho = deviceCopy(h_jrho.data(), h_jrho.size());
	c.jv = deviceCopy(h_jv.data(), h_jv.size());
	c.jp_t = deviceCopy(h_jp_t.data(), h_jp_t.size());
	c.js = deviceCopy(h_js.data(), h_js.size());
	c.js_t = deviceCopy(h_js_t.data(), h_js_t.size());
	c.jt = deviceCopy(h_jt.data(), h_jt.size());
	c.snorm = deviceCopy(h_snorm.data(), h_snorm.size());
	c.resnorm = deviceCopy(h_resnorm.data(), h_resnorm.size());
	c.oxy = deviceCopy(h_oxy.data(), h_oxy.size());
	c.beta = deviceCopy(h_beta.data(), h_beta.size());
	c.alpha = deviceCopy(h_alpha.data(), h_alpha.size());
	c.cs = deviceCopy(h_cs.data(), h_cs.size());
	c.cw = deviceCopy(h_cw.data(), h_cw.size());
	c.cp = deviceCopy(h_cp.data(), h_cp.size());
	c.alpha_den = deviceCopy(h_alpha_den.data(), h_alpha_den.size());
	c.w_num = deviceCopy(h_w_num.data(), h_w_num.size());
	c.w_den = deviceCopy(h_w_den.data(), h_w_den.size());
	c.res = deviceCopy(h_res.data(), h_res.size());
	c.jp = deviceCopy(h_jp.data(), h_jp.size());
	c.jw = deviceCopy(h_jw.data(), h_jw.size());
	c.OCR_num = deviceCopy(h_OCR_num.data(), h_OCR_num.size());

	// constant values
	double h_jw_num_val = 0.0;
	double h_jw_den_val = 0.0;
	double h_jalpha_val = 1.0;
	double h_jalpha_den_val = 1.0;
	double h_jbeta_val = 0.0;
	double h_jrho_val_prev = 1.0;
	double h_jrho_val = 1.0;
	double h_jw_val = 1.0;
	double h_snorm_val = 1.0;
	double h_resnorm_val = 0.0;
	double h_OCR_num_val = 0.0;

	double* d_snorm_val, * d_resnorm_val, * d_OCR_num_val;
	c.jw_num_val = deviceCopy(&h_jw_num_val, 1);
	c.jw_den_val = deviceCopy(&h_jw_den_val, 1);
	c.jalpha_val = deviceCopy(&h_jalpha_val, 1);
	c.jalpha_den_val = deviceCopy(&h_jalpha_den_val, 1);
	c.jbeta_val = deviceCopy(&h_jbeta_val, 1);
	c.jrho_val = deviceCopy(&h_jrho_val, 1);
	c.jrho_val_prev = deviceCopy(&h_jrho_val_prev, 1);
	c.jw_val = deviceCopy(&h_jw_val, 1);

	cudaMallocHost(&d_snorm_val, sizeof(double));
	cudaMallocHost(&d_OCR_num_val, sizeof(double));
	cudaMallocHost(&d_resnorm_val, sizeof(double));
	cudaMemcpy(d_snorm_val, &h_snorm_val, sizeof(double), cudaMemcpyHostToDevice);
	cudaMemcpy(d_resnorm_val, &h_resnorm_val, sizeof(double), cudaMemcpyHostToDevice);
	cudaMemcpy(d_OCR_num_val, &h_OCR_num_val, sizeof(double), cudaMemcpyHostToDevice);
	c.snorm_val = d_snorm_val;
	c.resnorm_val = d_resnorm_val;
	c.OCR_num_val = d_OCR_num_val;
}

void free_cell(Cell& c) {
	auto free_dev = [&](double*& p) {
		if (p) {CUDA_CHECK(cudaFree(p)); p = nullptr; }
		};
	auto free_host = [&](double*& p) {
		if (p) { CUDA_CHECK(cudaFreeHost(p)); p = nullptr; }
		};
	if (c.stream) {
		CUDA_CHECK(cudaStreamSynchronize(c.stream));
		CUDA_CHECK(cudaStreamDestroy(c.stream));
		c.stream = nullptr;
	}

	// Device arrays
	free_dev(c.ACC);   free_dev(c.AKE);   free_dev(c.AKW);   free_dev(c.AKN);   free_dev(c.AKS);
	free_dev(c.foxy);  free_dev(c.oxy);
	free_dev(c.beta);  free_dev(c.alpha);
	free_dev(c.cs);    free_dev(c.cw);    free_dev(c.cp);

	free_dev(c.res);   free_dev(c.res_t);
	free_dev(c.jp);    free_dev(c.jp_t);
	free_dev(c.jw);
	free_dev(c.alpha_den);
	free_dev(c.w_num); free_dev(c.w_den);
	free_dev(c.ACnew); free_dev(c.foxynew);

	free_dev(c.jrho);  free_dev(c.jv);
	free_dev(c.js);    free_dev(c.js_t);
	free_dev(c.jt);
	free_dev(c.snorm);
	free_dev(c.resnorm);

	free_dev(c.OCR_num);

	// “constant value” device scalars you cudaMalloc’d
	free_dev(c.jw_num_val);
	free_dev(c.jw_den_val);
	free_dev(c.jalpha_val);
	free_dev(c.jalpha_den_val);
	free_dev(c.jbeta_val);
	free_dev(c.jrho_val);
	free_dev(c.jw_val);
	free_dev(c.jrho_val_prev);
	free_host(c.snorm_val);
	free_host(c.resnorm_val);
	free_host(c.OCR_num_val);

	// temp buffers
	free_dev(c.tmpA);
	free_dev(c.tmpB);

}

void free_GridConfig(GridConfig& g) {
	auto free_dev = [&](auto*& p) {
		if (p) { CUDA_CHECK(cudaFree(p)); p = nullptr; }
		};

	// Device arrays
	free_dev(g.r);
	free_dev(g.c_cell);
	free_dev(g.wall_cell);
	free_dev(g.A);
	free_dev(g.surf_index);
	free_dev(g.dist);
	free_dev(g.kf);

}