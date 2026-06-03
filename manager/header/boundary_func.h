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

	bool isVariableInBoundaryType(
		BoundaryVariable variable,
		BoundaryType type
	);

	std::vector<BoundaryVariable> getVariableFromBoundaryType(
		const BoundarySegmentGroup& group,
		bool solveEnergy,
		bool solveConcentration
	);
}