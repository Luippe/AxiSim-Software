#pragma once

#include "boundary_struct.h"


// helper functions for boundary conditions
namespace BoundaryDefaults {

	BCType getDefaultBCType(
		BoundaryType boundaryType,
		BoundaryVariable var
	);

	double getDefaultBCValue(
		BoundaryType boundaryType,
		BoundaryVariable var
	);

	BoundaryCondition makeDefaultBC(
		BoundaryType boundaryType,
		BoundaryVariable var
	);

}