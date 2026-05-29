#include "memory_manager.h"
#include "setting.cuh"

#include <unordered_set>

#include "math_func.h"
#include "solver.h"
#include "printer.h"

bool isObstacleCell(
	const std::unordered_set<int>& obstacleSet,
	int nrBase,
	int nzBase,
	int iCell,
	int jCell
) {
	if (iCell < 0 || iCell >= nrBase) return false;
	if (jCell < 0 || jCell >= nzBase) return false;

	int nCell = iCell * nzBase + jCell;

	return obstacleSet.find(nCell) != obstacleSet.end();
}

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
	std::vector<double> dz = config.g.dz;
	std::vector<double> dr = config.g.dr;
	std::vector<double> r = config.g.r;
	std::vector<double> z = config.g.z;
	std::vector<double> rFace = config.g.rFace;
	std::vector<double> zFace = config.g.zFace;
	std::unordered_set<int> obstacleIndices = config.g.obstacleIndices;

	coeff.AE = deviceAlloc<double>(N);
	coeff.AW = deviceAlloc<double>(N);
	coeff.AN = deviceAlloc<double>(N);
	coeff.AS = deviceAlloc<double>(N);
	coeff.AC = deviceAlloc<double>(N);
	coeff.b = deviceAlloc<double>(N);
	coeff.res = deviceAlloc<double>(N);
	coeff.initRes = deviceAlloc<double>(N);

	CUDA_CHECK(cudaMemset(coeff.AE, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.AW, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.AN, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.AS, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.AC, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.b, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.res, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(coeff.initRes, 0, N * sizeof(double)));

	// find which cells should be active or not. 1 = active, 0 = deactive
	std::vector<uint8_t> activeCell (N, 1);
	std::vector<uint8_t> activeBC(N, 1);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;

			if (storeType == CellStoreType::AXIAL) {

				// inlet u boundary
				if (j == 0 && (bc.inlet.type == BCType::DIRICHLET || bc.inlet.type == FULLY_DEVELOPED)) {
					activeBC[n] = 0;
				}

				// outlet u boundary
				if (j == nz - 1 && bc.outlet.type == BCType::DIRICHLET) {
					activeBC[n] = 0;
				}
			}

			else if (storeType == CellStoreType::RADIAL) {

				// centerline v boundary
				if (i == 0 && bc.centerline.type == BCType::DIRICHLET) {
					activeBC[n] = 0;
				}

				// outer v boundary
				if (i == nr - 1 && (bc.outer.type == BCType::DIRICHLET || bc.outer.type == BCType::NEUMANN)) {
					activeBC[n] = 0;
				}
			}

			// obstacles
			bool isSolid = false;
			if (storeType == CellStoreType::CENTER) {
				isSolid = isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j);
			}
			else if (storeType == CellStoreType::AXIAL) {
				isSolid = isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j - 1) || isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j);
			}
			else if (storeType == CellStoreType::RADIAL) {
				isSolid = isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i - 1, j) || isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j);
			}

			if (isSolid) {
				activeCell[n] = 0;
			}

		}
	}
	coeff.activeCell = copyHostToDevice(activeCell.data(), activeCell.size());
	coeff.activeBC = copyHostToDevice(activeBC.data(), activeBC.size());

}

std::vector<double> getInitializedVelocity(ConfigSolver& config, BoundaryConditionConfig& uBC) {

	int nz = config.g.nz;
	int nr = config.g.nr;
	std::vector<double> dr = config.g.dr;
	std::vector<double> dz = config.g.dz;
	std::vector<double> r = config.g.r;
	std::vector<double> zFace = config.g.zFace;
	std::unordered_set<int> obstacleIndices = config.g.obstacleIndices;

	double R = config.g.R;
	double Umax = config.f.Umax;

	// initialize axial velocity
	std::vector<double> u(nr * (nz + 1), 0.0);

	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz + 1; ++j) {

			int n = i * (nz + 1) + j;

			//if (j != 0) continue;
			bool isSolid;

			double localZ = zFace[j];
			double localR = r[i];

			if (uBC.inlet.type == BCType::DIRICHLET) {
				u[n] = uBC.inlet.val;
			}
			else if (uBC.inlet.type == BCType::FULLY_DEVELOPED) {
				u[n] = uBC.inlet.val * (1.0 - (localR / R) * (localR / R));
			}

			// check if u face is on a solid boundary
			isSolid = isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j - 1) || isObstacleCell(obstacleIndices, config.g.nr, config.g.nz, i, j);
			if (isSolid) {
				u[n] = 0.0;
			}
		}
	}
	return u;
}

void allocateSimple(ConfigSolver& config, VariablesSimple& vars, BoundaryConditionConfig& uBC) {

	int nr = config.g.nr;
	int nz = config.g.nz;

	int Nu = nr * (nz + 1);
	int Nv = (nr + 1) * nz;
	int N = nr * nz;

	vars.DU = deviceAlloc<double>(Nu);
	vars.DV = deviceAlloc<double>(Nv);
	vars.uTemp = deviceAlloc<double>(Nu);
	vars.vTemp = deviceAlloc<double>(Nv);
	vars.ppTemp = deviceAlloc<double>(N);

	CUDA_CHECK(cudaMemset(vars.DU, 0, Nu * sizeof(double)));
	CUDA_CHECK(cudaMemset(vars.DV, 0, Nv * sizeof(double)));

	std::vector<double> h_u = getInitializedVelocity(config, uBC);
	std::vector<double> h_v(Nv, 0.0);
	std::vector<double> h_pp(N, 0.0);
	std::vector<double> h_p(N, 0.0);

	vars.uOld = copyHostToDevice(h_u.data(), h_u.size());
	vars.vOld = copyHostToDevice(h_v.data(), h_v.size());

	vars.u = copyHostToDevice(h_u.data(), h_u.size());
	vars.v = copyHostToDevice(h_v.data(), h_v.size());
	vars.pp = copyHostToDevice(h_pp.data(), h_pp.size());
	vars.p = copyHostToDevice(h_p.data(), h_p.size());

	CUDA_CHECK(cudaMemcpy(vars.uTemp, vars.u, Nu * sizeof(double), cudaMemcpyDeviceToDevice));
	CUDA_CHECK(cudaMemcpy(vars.vTemp, vars.v, Nv * sizeof(double), cudaMemcpyDeviceToDevice));
	CUDA_CHECK(cudaMemcpy(vars.ppTemp, vars.pp, N * sizeof(double), cudaMemcpyDeviceToDevice));
}



// initialize variables
void allocateGridConfig(GridConfig& g, FluidPropertyConfig& f) {

	int nr = g.nr;
	int nz = g.nz;
	int N = nr * nz;

	std::vector<double> dz = g.dz;
	std::vector<double> dr = g.dr;
	std::vector<double> zFace = g.zFace;
	std::vector<double> rFace = g.rFace;
	std::vector<double> r = g.r;
	std::vector<double> z = g.z;
	std::unordered_set<int> obstacleIndices = g.obstacleIndices;

	double Umax = f.Umax;
	double R = g.R;
	//double cell_top = g.cell_top;
	//double cell_left = g.cell_left;
	//double cell_right = g.cell_right;
	double D = f.D;
	double D_isf = f.D_isf;
	double d = f.d;
	
	// dz and dr for u and v
	std::vector<double> z_dz;
	std::vector<double> r_dr;

	for (int i = 0; i < nr + 1; ++i) {
		if (i == 0) {
			r_dr.push_back(dr[i]);
		}
		else if (i == nr) {
			r_dr.push_back(dr[i - 1]);
		}
		else {
			r_dr.push_back(0.5 * (dr[i] + dr[i - 1]));
		}
	}

	for (int j = 0; j < nz + 1; ++j) {
		if (j == 0) {
			z_dz.push_back(dz[j]);
		}
		else if (j == nz) {
			z_dz.push_back(dz[j - 1]);
		}
		else {
			z_dz.push_back(0.5 * (dz[j] + dz[j - 1]));
		}
	}

	// fill in cell data
	std::vector<int> c_cell(nr * nz, 0);

	// cell center check
	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;

			double zc = 0.5 * (zFace[j] + zFace[j + 1]);
			double rc = 0.5 * (rFace[i] + rFace[i + 1]);

			//if (zc >= cell_left && zc <= cell_right && rc <= cell_top) {
			//	c_cell[n] = 1;
			//}

			if (obstacleIndices.contains(n)) {
				c_cell[n] = 1;
			}
		}
	}



	//// axial face
	//for (int i = 0; i < nr; ++i) {
	//	for (int j = 0; j < nz + 1; ++j) {
	//		int n = i * (nz + 1) + j;

	//		double xf = j * dz;
	//		double yc = i * dr + 0.5 * dr;

	//		if (xf >= cell_left && xf <= cell_right && yc <= cell_top) {
	//			z_cell[n] = 1;
	//		}
	//	}
	//}

	//// radial face
	//for (int i = 0; i < nr + 1; ++i) {
	//	for (int j = 0; j < nz; ++j) {

	//		int n = i * nz + j;

	//		double xc = j * dz + 0.5 * dz;
	//		double yf = i * dr;

	//		if (xc >= cell_left && xc <= cell_right && yf <= cell_top) {
	//			r_cell[n] = 1;
	//		}
	//	}
	//}

	// get cell adjacent surface area and its index
	double A_tot = 0.0;
	std::vector<double> A;
	std::vector<int> surf_index;
	std::vector<double> dist;
	std::vector<double> kf;
	std::vector<int> wall_cell(nr * nz, -1);


	//for (int i = 0; i < nr; ++i) {
	//	for (int j = 0; j < nz; ++j) {
	//		int n = i * nz + j;
	//		if (c_cell[n] != 1) {

	//			double localDr = dr[i];
	//			double localDz = dz[i];

	//			double r1 = rFace[i];
	//			double r2 = rFace[i + 1];

	//			double Az = PI * (r2 * r2 - r1 * r1);
	//			double Ar2 = 2 * PI * r2 * localDr;
	//			double Ar1 = 2 * PI * r1 * localDz;

	//			// east
	//			if (j != nz - 1) {
	//				if (c_cell[n + 1] == 1) {
	//					A_tot += Az;
	//					A.push_back(Az);
	//					surf_index.push_back(n);
	//					dist.push_back(cell_left - z[j]);
	//					kf.push_back(D / (cell_left - z[j]));
	//				}
	//			}

	//			// west
	//			if (j != 0) {
	//				if (c_cell[n - 1] == 1) {
	//					A_tot += Az;
	//					A.push_back(Az);
	//					surf_index.push_back(n);
	//					dist.push_back(z[j] - cell_right);
	//					kf.push_back(D / (cell_right - z[j]));
	//				}
	//			}

	//			// south
	//			if (i != 0) {
	//				if (c_cell[n - nz] == 1) {
	//					A_tot += Ar1;
	//					A.push_back(Ar1);
	//					surf_index.push_back(n);
	//					dist.push_back(r[i] - cell_top);
	//					kf.push_back(D / (r[i] - cell_top));
	//				}
	//			}
	//		}
	//	}
	//}

	g.N = N;


	int n_cell = surf_index.size();
	g.A_tot = A_tot;
	g.n_cell = n_cell;
	double kl = D_isf / d;
	g.kl = kl;
	for (int num = 0; num < n_cell; ++num) {
		int cell = surf_index[num];
		wall_cell[cell] = num;
	}


	// allocate GridConfig variables and kf
	g.z_dz = copyHostToDevice(z_dz.data(), z_dz.size());
	g.r_dr = copyHostToDevice(r_dr.data(), r_dr.size());
	g.d_r = copyHostToDevice(r.data(), r.size());
	g.d_z = copyHostToDevice(z.data(), z.size());
	g.d_rFace = copyHostToDevice(rFace.data(), rFace.size());
	g.d_zFace = copyHostToDevice(zFace.data(), zFace.size());
	g.d_dz = copyHostToDevice(dz.data(), dz.size());
	g.d_dr = copyHostToDevice(dr.data(), dr.size());

	g.c_cell = copyHostToDevice(c_cell.data(), c_cell.size());
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
	freeDev(g.d_r);
	freeDev(g.d_z);
	freeDev(g.d_dr);
	freeDev(g.d_dz);
	freeDev(g.d_rFace);
	freeDev(g.d_zFace);
	freeDev(g.z_dz);
	freeDev(g.r_dr);

	freeDev(g.c_cell);
	freeDev(g.z_cell);
	freeDev(g.r_cell);

	freeDev(g.wall_cell);
	freeDev(g.A);
	freeDev(g.surf_index);
	freeDev(g.dist);
	freeDev(g.kf);

	g.n_cell = 0;
	g.A_tot = 0.0;
	g.kl = 0.0;
}
