#include "residuals.cuh"
#include "device_launch_parameters.h"
#include "solver_struct.h"


__device__
void residualRaw(ResidualPairs& pairs, int n){

	Coefficients& coeff = pairs.coeff;
	const double* x = pairs.x;

	if (n >= pairs.coeff.N) return;
	if (pairs.coeff.active[n]) return;

	double* b = coeff.b;
	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

	int nr = coeff.nr;
	int nz = coeff.nz;
	int j = n % nz;
	int i = n / nz;

	double* res = coeff.res;

	double Ax = coeff.AC[n] * x[n];

	if (j != nz - 1) {
		Ax += coeff.AE[n] * x[n + 1];
	}

	if (j != 0) {
		Ax += coeff.AW[n] * x[n - 1];
	}

	if (i != nr - 1) {
		Ax += coeff.AN[n] * x[n + nz];
	}

	if (i != 0) {
		Ax += coeff.AS[n] * x[n - nz];
	}

	res[n] = fabs(coeff.b[n] - Ax);

}