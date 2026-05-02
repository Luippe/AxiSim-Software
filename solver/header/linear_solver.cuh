#include <cuda_runtime.h>
#include "solver_struct.h"

__global__
void jacobi(Coefficients coeff, double* xTemp, double* x, double relaxation);