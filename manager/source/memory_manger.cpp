#include "memory_manager.h"
#include "setting.cuh"
#include "math_func.h"
#include "solver.h"


void allocateCoefficients(ConfigSolver& config, Coefficients& coeff, BoundaryConditionConfig& bc, CellStoreType storeType) {

	// get size of nr and nz
	int nr = 0;
	int nz = 0;

	if (storeType == CellStoreType::AXIAL) {
		nr = config.g.nr;
		nz = config.g.nz + 1;
	}
	else if (storeType == CellStoreType::RADIAL) {
		nr = config.g.nr + 1;
		nz = config.g.nz;
	}
	else if (storeType == CellStoreType::CENTER) {
		nr = config.g.nr;
		nz = config.g.nz;
	}
	coeff.nr = nr;
	coeff.nz = nz;
	coeff.N = nr * nz;
	coeff.storeType = storeType;

	int N = nr * nz;
	double x = 0.0;
	double y = 0.0;
	double dz = config.g.dz;
	double dr = config.g.dr;
	double cell_left = config.g.cell_left;
	double cell_right = config.g.cell_right;
	double cell_top = config.g.cell_top;

	coeff.AE = deviceAlloc<double>(N);
	coeff.AW = deviceAlloc<double>(N);
	coeff.AN = deviceAlloc<double>(N);
	coeff.AS = deviceAlloc<double>(N);
	coeff.AC = deviceAlloc<double>(N);
	coeff.b = deviceAlloc<double>(N);
	coeff.res = deviceAlloc<double>(N);
	coeff.initRes = deviceAlloc<double>(N);

	// find which cells should be active or not. 0 = active, 1 = deactive
	std::vector<int> active(N, 0);
	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;

			// inlet
			if (j == 0 && storeType == CellStoreType::AXIAL && bc.inlet.type == BCType::DIRICHLET) {
				active[n] = 1;
			}

			// outlet
			if (j == nz - 1 && storeType == CellStoreType::AXIAL && bc.outlet.type == BCType::DIRICHLET) {
				active[n] = 1;
			}

			// centerline
			if (i == 0 && storeType == CellStoreType::RADIAL && bc.centerline.type == BCType::DIRICHLET) {
				active[n] = 1;
			}

			// outer
			if (i == nr - 1 && storeType == CellStoreType::RADIAL && bc.outer.type == BCType::DIRICHLET) {
				active[n] = 1;
			}

			// cells
			if (storeType == CellStoreType::AXIAL) {
				x = j * dz;
				y = i * dr + 0.5 * dr;
			}
			else if (storeType == CellStoreType::RADIAL) {
				x = j * dz + 0.5 * dz;
				y = i * dr;
			}
			else if (storeType == CellStoreType::CENTER) {
				x = j * dz + 0.5 * dz;
				y = i * dr + 0.5 * dr;
			}

			if (x >= cell_left && x <= cell_right && y <= cell_top) {
				active[n] = 1;
			}
		}
	}
	coeff.active = copyHostToDevice(active.data(), active.size());

}

void allocateSimple(ConfigSolver& config, VariablesSimple& vars) {

	int nr = config.g.nr;
	int nz = config.g.nz;

	vars.DU = deviceAlloc<double>(nr * (nz + 1));
	vars.DV = deviceAlloc<double>((nr + 1) * nz);

	vars.uTemp = deviceAlloc<double>(nr * (nz + 1));
	vars.vTemp = deviceAlloc<double>((nr + 1) * nz);
	vars.ppTemp = deviceAlloc<double>(nr * nz);

	// initialize uOld and vOld before sending it to device
	std::vector<double> h_u = getInitializedVelocity(config);
	std::vector<double> h_v((nr + 1) * nz, 0.0);
	std::vector<double> h_pp(nr * nz, 0.0);
	std::vector<double> h_p(nr * nz, 0.0);

	vars.u = copyHostToDevice(h_u.data(), h_u.size());
	vars.v = copyHostToDevice(h_v.data(), h_v.size());
	vars.pp = copyHostToDevice(h_pp.data(), h_pp.size());
	vars.p = copyHostToDevice(h_p.data(), h_p.size());


}

std::vector<double> getInitializedVelocity(ConfigSolver& config) {

	int nz = config.g.nz;
	int nr = config.g.nr;
	double dr = config.g.dr;
	double dz = config.g.dz;
	double R = config.g.R;
	double Umax = config.f.Umax;

	double cell_left = config.g.cell_left;
	double cell_right = config.g.cell_right;
	double cell_top = config.g.cell_top;

	// initialize axial velocity
	std::vector<double> u(nr * (nz + 1), 0.0);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz + 1; ++j) {

			//if (j != 0) continue;

			int n = i * (nz + 1) + j;

			double x = j * dz;
			double r = 0.5 * dr + i * dr;

			u[n] = Umax * (1 - (r / R) * (r / R));

			if (x >= cell_left && x <= cell_right && r <= cell_top) {
				u[n] = 0.0;
			}

		}
	}

	return u;
}

// initialize variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f) {

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
	std::vector<int> z_cell(nr * (nz + 1), 0.0);
	std::vector<int> r_cell((nr + 1) * nz, 0.0);
	std::vector<int> c_cell(nr * nz, 0.0);

	// cell center
	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;
			double xf = j * dz;
			double yf = i * dr;
			double xc = (dz / 2) + xf;
			double yc = (dr / 2) + yf;
			if (xc >= cell_left && xc <= cell_right && yc <= cell_top && i != nr && j != nz) {
				c_cell[n] = 1;
			}
		}
	}

	// axial face
	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz + 1; ++j) {
			int n = i * (nz + 1) + j;

			double xf = j * dz;
			double yc = i * dr + 0.5 * dr;

			if (xf >= cell_left && xf <= cell_right && yc <= cell_top) {
				z_cell[n] = 1;
			}
		}
	}

	// radial face
	for (int i = 0; i < nr + 1; ++i) {
		for (int j = 0; j < nz; ++j) {

			int n = i * nz + j;

			double xc = j * dz + 0.5 * dz;
			double yf = i * dr;

			if (xc >= cell_left && xc <= cell_right && yf <= cell_top) {
				r_cell[n] = 1;
			}
		}
	}

	// get cell adjacent surface area and its index
	double A_tot = 0.0;
	std::vector<double> A;
	std::vector<int> surf_index;
	std::vector<double> dist;
	std::vector<double> kf;
	std::vector<int> wall_cell(nr * nz, -1);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;
			if (c_cell[n] != 1) {
				double r1 = r[i] - (dr / 2);
				double r2 = r[i] + (dr / 2);

				double Az = PI * (r2 * r2 - r1 * r1);
				double Ar2 = 2 * PI * r2 * dz;
				double Ar1 = 2 * PI * r1 * dz;

				// east
				if (j != nz - 1) {
					if (c_cell[n + 1] == 1) {
						A_tot += Az;
						A.push_back(Az);
						surf_index.push_back(n);
						dist.push_back(0.5 * dz);
						kf.push_back(D / (0.5 * dz));
					}
				}

				// west
				if (j != 0) {
					if (c_cell[n - 1] == 1) {
						A_tot += Az;
						A.push_back(Az);
						surf_index.push_back(n);
						dist.push_back(0.5 * dz);
						kf.push_back(D / (0.5 * dz));
					}
				}

				// south
				if (i != 0) {
					if (c_cell[n - nz] == 1) {
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
	g.r = copyHostToDevice(r.data(), r.size());
	g.c_cell = copyHostToDevice(c_cell.data(), c_cell.size());
	g.z_cell = copyHostToDevice(z_cell.data(), z_cell.size());
	g.r_cell = copyHostToDevice(r_cell.data(), r_cell.size());
	g.wall_cell = copyHostToDevice(wall_cell.data(), wall_cell.size());
	g.A = copyHostToDevice(A.data(), A.size());
	g.surf_index = copyHostToDevice(surf_index.data(), surf_index.size());
	g.dist = copyHostToDevice(dist.data(), dist.size());
	g.kf = copyHostToDevice(kf.data(), kf.size());

}

void allocateBiCGStab(GridConfig& g, FluidPropertyConfig& f, VariablesBiCGStab& vars) {

	int nr = g.nr;
	int nz = g.nz;
	int n = nr * nz;
	int n_cell = g.n_cell;
	double conc = vars.conc;

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

	vars.tmpA = deviceAlloc<double>(n);
	vars.tmpB = deviceAlloc<double>(n);
	//cudaMalloc(&vars.tmpA, n * sizeof(double));
	//cudaMalloc(&vars.tmpB, n * sizeof(double));

	vars.ACC = copyHostToDevice(h_ACC.data(), h_ACC.size());
	vars.AKE = copyHostToDevice(h_AKE.data(), h_AKE.size());
	vars.AKW = copyHostToDevice(h_AKW.data(), h_AKW.size());
	vars.AKN = copyHostToDevice(h_AKN.data(), h_AKN.size());
	vars.AKS = copyHostToDevice(h_AKS.data(), h_AKS.size());
	vars.foxy = copyHostToDevice(h_foxy.data(), h_foxy.size());
	vars.ACnew = copyHostToDevice(h_ACnew.data(), h_ACnew.size());
	vars.foxynew = copyHostToDevice(h_foxynew.data(), h_foxynew.size());
	vars.res_t = copyHostToDevice(h_res_t.data(), h_res_t.size());
	vars.jrho = copyHostToDevice(h_jrho.data(), h_jrho.size());
	vars.jv = copyHostToDevice(h_jv.data(), h_jv.size());
	vars.jp_t = copyHostToDevice(h_jp_t.data(), h_jp_t.size());
	vars.js = copyHostToDevice(h_js.data(), h_js.size());
	vars.js_t = copyHostToDevice(h_js_t.data(), h_js_t.size());
	vars.jt = copyHostToDevice(h_jt.data(), h_jt.size());
	vars.snorm = copyHostToDevice(h_snorm.data(), h_snorm.size());
	vars.resnorm = copyHostToDevice(h_resnorm.data(), h_resnorm.size());
	vars.oxy = copyHostToDevice(h_oxy.data(), h_oxy.size());
	vars.beta = copyHostToDevice(h_beta.data(), h_beta.size());
	vars.alpha = copyHostToDevice(h_alpha.data(), h_alpha.size());
	vars.cs = copyHostToDevice(h_cs.data(), h_cs.size());
	vars.cw = copyHostToDevice(h_cw.data(), h_cw.size());
	vars.cp = copyHostToDevice(h_cp.data(), h_cp.size());
	vars.alpha_den = copyHostToDevice(h_alpha_den.data(), h_alpha_den.size());
	vars.w_num = copyHostToDevice(h_w_num.data(), h_w_num.size());
	vars.w_den = copyHostToDevice(h_w_den.data(), h_w_den.size());
	vars.res = copyHostToDevice(h_res.data(), h_res.size());
	vars.jp = copyHostToDevice(h_jp.data(), h_jp.size());
	vars.jw = copyHostToDevice(h_jw.data(), h_jw.size());
	vars.OCR_num = copyHostToDevice(h_OCR_num.data(), h_OCR_num.size());

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


	vars.jw_num_val = copyHostToDevice(&h_jw_num_val, 1);
	vars.jw_den_val = copyHostToDevice(&h_jw_den_val, 1);
	vars.jalpha_val = copyHostToDevice(&h_jalpha_val, 1);
	vars.jalpha_den_val = copyHostToDevice(&h_jalpha_den_val, 1);
	vars.jbeta_val = copyHostToDevice(&h_jbeta_val, 1);
	vars.jrho_val = copyHostToDevice(&h_jrho_val, 1);
	vars.jrho_val_prev = copyHostToDevice(&h_jrho_val_prev, 1);
	vars.jw_val = copyHostToDevice(&h_jw_val, 1);

	double* d_snorm_val, * d_resnorm_val, * d_OCR_num_val;
	cudaMallocHost(&d_snorm_val, sizeof(double));
	cudaMallocHost(&d_OCR_num_val, sizeof(double));
	cudaMallocHost(&d_resnorm_val, sizeof(double));

	cudaMemcpy(d_snorm_val, &h_snorm_val, sizeof(double), cudaMemcpyHostToDevice);
	cudaMemcpy(d_resnorm_val, &h_resnorm_val, sizeof(double), cudaMemcpyHostToDevice);
	cudaMemcpy(d_OCR_num_val, &h_OCR_num_val, sizeof(double), cudaMemcpyHostToDevice);
	vars.snorm_val = d_snorm_val;
	vars.resnorm_val = d_resnorm_val;
	vars.OCR_num_val = d_OCR_num_val;
}

void free_GridConfig(GridConfig& g) {

	// Device arrays
	freeDev(g.r);
	freeDev(g.c_cell);
	freeDev(g.z_cell);
	freeDev(g.r_cell);
	freeDev(g.wall_cell);
	freeDev(g.A);
	freeDev(g.surf_index);
	freeDev(g.dist);
	freeDev(g.kf);

	g.c_cell_vec.clear();
	g.v_cell_vec.clear();

}
