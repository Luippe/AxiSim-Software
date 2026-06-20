#include "memory_manager.h"
#include "setting.cuh"

#include <unordered_set>

#include "multigrid.cuh"

#include "math_func.h"
#include "solver.h"
#include "printer.h"

#include "boundary_func.h"

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

BoundaryFieldHost createBoundaryFieldHost(
	const std::vector<BoundarySegmentGroup>& boundaryGroups,
	BoundaryVariable variable
) {
	int maxGroupID = -1;

	for (const BoundarySegmentGroup& group : boundaryGroups) {
		maxGroupID = std::max(maxGroupID, group.id);
	}

	BoundaryFieldHost h{};

	if (maxGroupID < 0) {
		return h;
	}

	int nGroups = maxGroupID + 1;

	h.typeByGroup.resize(nGroups, (uint8_t)(BCType::NONE));
	h.valueByGroup.resize(nGroups, 0.0);
	h.lengthByGroup.resize(nGroups, 0.0);

	for (const BoundarySegmentGroup& group : boundaryGroups) {

		if (group.id < 0) {
			continue;
		}

		// make default bc. if the bc already exists, replace the bc and use that instead
		BoundaryCondition bc =
			BoundaryDefaults::makeDefaultBC(group, variable);

		// find boundary variable inside the group, if it is, check if we should use the user specified or default value
		auto it = group.bcs.find(variable);		

		if ((it != group.bcs.end()) && (BoundaryDefaults::isVariableInBoundaryType(variable, group.type))) {
			bc = it->second;
		}

		if (variable == BoundaryVariable::UVelocity && group.type == BoundaryType::PRESSURE_OUTLET) {
			printf("%f\n", bc.value);
		}

		h.typeByGroup[group.id] =
			(uint8_t)(bc.type);

		h.valueByGroup[group.id] =
			bc.value;

		h.lengthByGroup[group.id] =
			group.totalLength;
	}

	return h;
}

BoundaryFieldDevice createBoundaryFieldDevice(
	const BoundaryFieldHost& h
) {
	BoundaryFieldDevice d{};

	d.nGroups = (int)(h.typeByGroup.size());

	copyHostToDevice(d.typeByGroup, h.typeByGroup);
	copyHostToDevice(d.valueByGroup, h.valueByGroup);
	copyHostToDevice(d.lengthByGroup, h.lengthByGroup);

	return d;
}

BoundarySolverDevice createBoundarySolverDevice(
	const std::vector<BoundarySegmentGroup>& boundaryGroups,
	const SolverFieldOption& option
) {
	BoundarySolverDevice dBC{};


	BoundaryFieldHost hU = createBoundaryFieldHost(
		boundaryGroups,
		BoundaryVariable::UVelocity
	);
	dBC.u = createBoundaryFieldDevice(hU);


	BoundaryFieldHost hV = createBoundaryFieldHost(
		boundaryGroups,
		BoundaryVariable::VVelocity
	);
	dBC.v = createBoundaryFieldDevice(hV);
	


	BoundaryFieldHost hP = createBoundaryFieldHost(
		boundaryGroups,
		BoundaryVariable::Pressure
	);
	dBC.p = createBoundaryFieldDevice(hP);
	

	if (option.solveEnergy) {
		BoundaryFieldHost hEnergy = createBoundaryFieldHost(
			boundaryGroups,
			BoundaryVariable::StaticTemperature
		);
		dBC.temp = createBoundaryFieldDevice(hEnergy);
	}

	if (option.solveConcentration) {
		BoundaryFieldHost hConcentration = createBoundaryFieldHost(
			boundaryGroups,
			BoundaryVariable::Concentration
		);
		dBC.concentration = createBoundaryFieldDevice(hConcentration);
	}

	return dBC;
}

void allocateCoefficients(Coefficients& coeff, int nr, int nz) {

	// get N
	int N = nr * nz;
	coeff.N = N;
	coeff.nr = nr;
	coeff.nz = nz;
	coeff.nFaceRefs = 0;
	coeff.useFaceCoeffs = 0;

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


}

void allocateCoefficients(Coefficients& coeff, const FVMesh& mesh) {
	int N = mesh.numCells();

	coeff.N = N;
	coeff.nr = mesh.nr;
	coeff.nz = mesh.nz;
	coeff.useFaceCoeffs = 1;

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

	std::vector<int> faceStart(N + 1, 0);
	std::vector<int> faceNeighbor;

	int totalFaceRefs = 0;
	for (int c = 0; c < N; c++) {
		faceStart[c] = totalFaceRefs;
		totalFaceRefs += static_cast<int>(mesh.cells[c].faceIDs.size());
	}

	faceStart[N] = totalFaceRefs;
	faceNeighbor.reserve(totalFaceRefs);

	for (int c = 0; c < N; c++) {
		for (int faceID : mesh.cells[c].faceIDs) {
			int neighbor = -1;

			if (faceID >= 0 && faceID < mesh.numFaces()) {
				const FVFace& face = mesh.faces[faceID];
				if (face.owner == c) {
					neighbor = face.neighbor;
				}
				else if (face.neighbor == c) {
					neighbor = face.owner;
				}
			}

			faceNeighbor.push_back(neighbor);
		}
	}

	coeff.nFaceRefs = static_cast<int>(faceNeighbor.size());

	if (coeff.nFaceRefs > 0) {
		coeff.AF = deviceAlloc<double>(coeff.nFaceRefs);
		CUDA_CHECK(cudaMemset(coeff.AF, 0, coeff.nFaceRefs * sizeof(double)));
	}
	else {
		coeff.AF = nullptr;
	}

	copyHostToDevice(coeff.faceStart, faceStart);
	copyHostToDevice(coeff.faceNeighbor, faceNeighbor);
}

FVMeshHostPacked packFVMeshForDevice(const FVMesh& mesh) {
	FVMeshHostPacked h{};

	h.nr = mesh.nr;
	h.nz = mesh.nz;

	h.nCells = static_cast<int>(mesh.cells.size());
	h.nFaces = static_cast<int>(mesh.faces.size());

	h.faceOwner.resize(h.nFaces);
	h.faceNeighbor.resize(h.nFaces);
	h.faceNormalZ.resize(h.nFaces);
	h.faceNormalR.resize(h.nFaces);
	h.faceCenterZ.resize(h.nFaces);
	h.faceCenterR.resize(h.nFaces);
	h.faceArea.resize(h.nFaces);
	h.faceBoundaryGroupID.resize(h.nFaces);

	for (int f = 0; f < h.nFaces; f++) {
		const FVFace& face = mesh.faces[f];

		h.faceOwner[f] = face.owner;
		h.faceNeighbor[f] = face.neighbor;

		h.faceNormalZ[f] = face.normal.z;
		h.faceNormalR[f] = face.normal.r;

		h.faceCenterZ[f] = face.center.z;
		h.faceCenterR[f] = face.center.r;

		h.faceArea[f] = face.area;

		h.faceBoundaryGroupID[f] = face.boundaryGroupID;
	}

	h.cellCenterZ.resize(h.nCells);
	h.cellCenterR.resize(h.nCells);
	h.cellVolume.resize(h.nCells);
	h.cellActive.resize(h.nCells);
	h.cellSolid.resize(h.nCells);

	h.cellFaceStart.resize(h.nCells + 1);

	int totalFaceRefs = 0;

	for (int c = 0; c < h.nCells; c++) {
		const FVCell& cell = mesh.cells[c];

		h.cellCenterZ[c] = cell.center.z;
		h.cellCenterR[c] = cell.center.r;

		h.cellVolume[c] = cell.volume;

		h.cellActive[c] = cell.active ? 1 : 0;
		h.cellSolid[c] = cell.solid ? 1 : 0;

		h.cellFaceStart[c] = totalFaceRefs;
		totalFaceRefs += static_cast<int>(cell.faceIDs.size());
	}

	h.cellFaceStart[h.nCells] = totalFaceRefs;

	h.cellFaceIDs.resize(totalFaceRefs);

	int k = 0;

	for (int c = 0; c < h.nCells; c++) {
		const FVCell& cell = mesh.cells[c];

		for (int faceID : cell.faceIDs) {
			h.cellFaceIDs[k++] = faceID;
		}
	}

	return h;
}



FVMeshDevice createFVMeshDevice(const FVMesh& mesh) {

	FVMeshHostPacked h = packFVMeshForDevice(mesh);

	FVMeshDevice d{};

	// normal scalar values: assign directly
	d.nr = h.nr;
	d.nz = h.nz;

	d.cells.nCells = h.nCells;
	d.faces.nFaces = h.nFaces;

	// face arrays
	copyHostToDevice(d.faces.owner, h.faceOwner);
	copyHostToDevice(d.faces.neighbor, h.faceNeighbor);

	copyHostToDevice(d.faces.normalZ, h.faceNormalZ);
	copyHostToDevice(d.faces.normalR, h.faceNormalR);

	copyHostToDevice(d.faces.centerZ, h.faceCenterZ);
	copyHostToDevice(d.faces.centerR, h.faceCenterR);

	copyHostToDevice(d.faces.area, h.faceArea);

	copyHostToDevice(d.faces.boundaryGroupID, h.faceBoundaryGroupID);

	// cell arrays
	copyHostToDevice(d.cells.centerZ, h.cellCenterZ);
	copyHostToDevice(d.cells.centerR, h.cellCenterR);

	copyHostToDevice(d.cells.volume, h.cellVolume);

	copyHostToDevice(d.cells.active, h.cellActive);
	copyHostToDevice(d.cells.solid, h.cellSolid);

	copyHostToDevice(d.cells.faceStart, h.cellFaceStart);
	copyHostToDevice(d.cells.faceIDs, h.cellFaceIDs);

	return d;
}


void allocateSimple(
	ConfigSolver& config,
	VariablesSimple& vars,
	FVMesh& mesh
) {

	int N = mesh.numCells();
	config.g.N = N;

	vars.DU = deviceAlloc<double>(N);
	vars.DV = deviceAlloc<double>(N);
	vars.gradPZ = deviceAlloc<double>(N);
	vars.gradPR = deviceAlloc<double>(N);
	vars.uTemp = deviceAlloc<double>(N);
	vars.vTemp = deviceAlloc<double>(N);
	vars.ppTemp = deviceAlloc<double>(N);
	vars.tempTemp = deviceAlloc<double>(N);

	CUDA_CHECK(cudaMemset(vars.DU, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(vars.DV, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(vars.gradPZ, 0, N * sizeof(double)));
	CUDA_CHECK(cudaMemset(vars.gradPR, 0, N * sizeof(double)));

	std::vector<double> h_u(N, 0.0);
	std::vector<double> h_v(N, 0.0);
	std::vector<double> h_pp(N, 0.0);
	std::vector<double> h_p(N, 0.0);
	std::vector<double> h_temp(N, 0.0);
	std::vector<double> h_mDot(mesh.numFaces(), 0.0);

	copyHostToDevice(vars.uOld, h_u);
	copyHostToDevice(vars.vOld, h_v);
	copyHostToDevice(vars.tempOld, h_temp);
	copyHostToDevice(vars.mDot, h_mDot);

	copyHostToDevice(vars.u, h_u);
	copyHostToDevice(vars.v, h_v);
	copyHostToDevice(vars.pp, h_pp);
	copyHostToDevice(vars.p, h_p);
	copyHostToDevice(vars.temp, h_temp);

	CUDA_CHECK(cudaMemcpy(vars.uTemp, vars.u, N * sizeof(double), cudaMemcpyDeviceToDevice));
	CUDA_CHECK(cudaMemcpy(vars.vTemp, vars.v, N * sizeof(double), cudaMemcpyDeviceToDevice));
	CUDA_CHECK(cudaMemcpy(vars.ppTemp, vars.pp, N * sizeof(double), cudaMemcpyDeviceToDevice));
	CUDA_CHECK(cudaMemcpy(vars.tempTemp, vars.temp, N * sizeof(double), cudaMemcpyDeviceToDevice));
}


std::vector<uint8_t> createActiveCell(int N, const std::unordered_set<int>& obstacleIndices) {

	std::vector<uint8_t> activeCell(N, 1);
	
	for (int n : obstacleIndices) {
		activeCell[n] = 0;
	}

	return activeCell;

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

	// active cell
	std::vector<uint8_t> activeCell = createActiveCell(N, obstacleIndices);

	g.activeCell = activeCell;

	// fill in cell data
	std::vector<int> c_cell(nr * nz, 0);

	// cell center check
	for (int i = 0; i < nr; ++i) {
		for (int j = 0; j < nz; ++j) {
			int n = i * nz + j;

			double zc = 0.5 * (zFace[j] + zFace[j + 1]);
			double rc = 0.5 * (rFace[i] + rFace[i + 1]);

			if (obstacleIndices.contains(n)) {
				c_cell[n] = 1;
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
	copyHostToDevice(g.z_dz, z_dz);
	copyHostToDevice(g.r_dr, r_dr);

	copyHostToDevice(g.d_r, r);
	copyHostToDevice(g.d_z, z);

	copyHostToDevice(g.d_rFace, rFace);
	copyHostToDevice(g.d_zFace, zFace);

	copyHostToDevice(g.d_dz, dz);
	copyHostToDevice(g.d_dr, dr);

	copyHostToDevice(g.d_activeCell, activeCell);

	copyHostToDevice(g.c_cell, c_cell);
	copyHostToDevice(g.wall_cell, wall_cell);

	copyHostToDevice(g.A, A);
	copyHostToDevice(g.surf_index, surf_index);
	copyHostToDevice(g.dist, dist);
	copyHostToDevice(g.kf, kf);

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

	copyHostToDevice(vars.ACC, h_ACC);
	copyHostToDevice(vars.AKE, h_AKE);
	copyHostToDevice(vars.AKW, h_AKW);
	copyHostToDevice(vars.AKN, h_AKN);
	copyHostToDevice(vars.AKS, h_AKS);

	copyHostToDevice(vars.foxy, h_foxy);
	copyHostToDevice(vars.ACnew, h_ACnew);
	copyHostToDevice(vars.foxynew, h_foxynew);

	copyHostToDevice(vars.res_t, h_res_t);

	copyHostToDevice(vars.jrho, h_jrho);
	copyHostToDevice(vars.jv, h_jv);
	copyHostToDevice(vars.jp_t, h_jp_t);
	copyHostToDevice(vars.js, h_js);
	copyHostToDevice(vars.js_t, h_js_t);
	copyHostToDevice(vars.jt, h_jt);

	copyHostToDevice(vars.snorm, h_snorm);
	copyHostToDevice(vars.resnorm, h_resnorm);

	copyHostToDevice(vars.oxy, h_oxy);

	copyHostToDevice(vars.beta, h_beta);
	copyHostToDevice(vars.alpha, h_alpha);

	copyHostToDevice(vars.cs, h_cs);
	copyHostToDevice(vars.cw, h_cw);
	copyHostToDevice(vars.cp, h_cp);

	copyHostToDevice(vars.alpha_den, h_alpha_den);

	copyHostToDevice(vars.w_num, h_w_num);
	copyHostToDevice(vars.w_den, h_w_den);

	copyHostToDevice(vars.res, h_res);

	copyHostToDevice(vars.jp, h_jp);
	copyHostToDevice(vars.jw, h_jw);

	copyHostToDevice(vars.OCR_num, h_OCR_num);

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

	copyHostToDevice(vars.jw_num_val, h_jw_num_val);
	copyHostToDevice(vars.jw_den_val, h_jw_den_val);

	copyHostToDevice(vars.jalpha_val, h_jalpha_val);
	copyHostToDevice(vars.jalpha_den_val, h_jalpha_den_val);

	copyHostToDevice(vars.jbeta_val, h_jbeta_val);

	copyHostToDevice(vars.jrho_val, h_jrho_val);
	copyHostToDevice(vars.jrho_val_prev, h_jrho_val_prev);

	copyHostToDevice(vars.jw_val, h_jw_val);

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


void allocateMultigridLevel(MultigridLevel& level) {

	int N = level.grid.N;

	allocateCoefficients(level.coeff, level.grid.nr, level.grid.nz);

	cudaMalloc(&level.x, N * sizeof(double));
	cudaMalloc(&level.xTemp, N * sizeof(double));
	cudaMalloc(&level.d_volume, N * sizeof(double));
	cudaMalloc(&level.d_active, N * sizeof(uint8_t));

	cudaMemset(level.x, 0, N * sizeof(double));
	cudaMemset(level.xTemp, 0, N * sizeof(double));
	cudaMemset(level.d_volume, 0, N * sizeof(double));
	cudaMemset(level.d_active, 0, N * sizeof(uint8_t));

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
