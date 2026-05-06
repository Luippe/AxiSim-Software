#include <cuda_runtime.h>
#include "solver_struct.h"


// -------------axial velocity--------------
__global__
void createURhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple);

// ------------radial velocity-------------
__global__
void createVRhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple);

// ---------------pressure correction----------------
__global__
void createPPCoeff(ConfigSolver config, Coefficients coeff, VariablesSimple simple);

__global__
void createPPRhs(ConfigSolver config, Coefficients coeff, VariablesSimple simple);

// ---------------------update variables----------------
__global__
void updateUVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple, int N);

__global__
void updateVVelocity(ConfigSolver config, Coefficients coeff, VariablesSimple simple, int N);

__global__
void updatePressure(ConfigSolver config, Coefficients coeff, VariablesSimple simple, int N);

__global__
void getCorrectionCoefficient(ConfigSolver config, Coefficients coeff, VariablesSimple simple, double* D);
