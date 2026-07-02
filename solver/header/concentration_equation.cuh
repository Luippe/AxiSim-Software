#pragma once
#include <cuda_runtime.h>
#include "solver_struct.h"
#include "boundary_struct.h"
#include "solver_struct.h"
#include "boundary_struct.h"

__device__
double Inhibition(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
double dInhibition(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
double MichaelisMenten(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
double dMichaelisMenten(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
double Hill(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
double dHill(const BoundaryFieldDevice& bc, int groupID, double c);

__device__
void wallConcentration(const BoundaryFieldDevice& bc, int groupID, double cp, double& cw, double h);

//__device__
//void wallOCR(const BoundaryFieldDevice& bc, int groupID, double cp, double& cw, double h)