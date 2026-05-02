#include <cuda_runtime.h>
#include "solver_struct.h"


// -------------axial velocity--------------

__global__
void createURhs(Config config, Coefficients coeff, VariablesSimple simple);

// ------------radial velocity-------------

__global__
void createVRhs(Config config, Coefficients coeff, VariablesSimple simple);

// ---------------pressure correction----------------
__global__
void createPPCoeff(Config config, Coefficients coeff, VariablesSimple simple);

__global__
void createPPRhs(Config config, Coefficients coeff, VariablesSimple simple);

// ---------------------update variables----------------
__global__
void updateUVelocity(Config config, Coefficients coeff, VariablesSimple simple, int N);

__global__
void updateVVelocity(Config config, Coefficients coeff, VariablesSimple simple, int N);

__global__
void updatePressure(Config config, Coefficients coeff, VariablesSimple simple, int N);

double getMax(Coefficients& coeff);

__global__
void getCorrectionCoefficient(Config config, Coefficients coeff, VariablesSimple simple, double* D);
