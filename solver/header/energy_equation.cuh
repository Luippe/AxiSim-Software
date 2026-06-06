#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"
#include "boundary_struct.h"

__global__
void addEnergyRhs(
	FVMeshDevice mesh,
	VariablesSimple simple
);