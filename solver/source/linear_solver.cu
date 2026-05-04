#include "linear_solver.cuh"
#include "device_launch_parameters.h"


__global__
void jacobi(Coefficients coeff, double* xTemp, double* x, double relaxation)  {

	int n = blockIdx.x * blockDim.x + threadIdx.x;

	if (n >= coeff.N) return;
	if (coeff.active[n]) return;

	int nr = coeff.nr;
	int nz = coeff.nz;
	int j = n % nz;
	int i = n / nz;

	double* b = coeff.b;
	double* AC = coeff.AC;
	double* AE = coeff.AE;
	double* AW = coeff.AW;
	double* AN = coeff.AN;
	double* AS = coeff.AS;

	double val = b[n];
	if (j != nz - 1) {
		val -= AE[n] * xTemp[n + 1];
	}

	if (j != 0) {
		val -= AW[n] * xTemp[n - 1];
	}

	if (i != nr - 1) {
		val -= AN[n] * xTemp[n + nz];
	}

	if (i != 0) {
		val -= AS[n] * xTemp[n - nz];
	}

	val /= AC[n];

	x[n] = relaxation * val + (1 - relaxation) * x[n];

}