#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"
#include "boundary_struct.h"
#include "solver_struct.h"

__device__
void inhibition(FluidPropertyConfig& f);