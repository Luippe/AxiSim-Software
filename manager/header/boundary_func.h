#pragma once

#include "boundary_struct.h"


// helper functions for boundary conditions
namespace BoundaryDefaults {

	BCType getDefaultBCType(
		BoundaryType boundaryType,
		BoundaryVariable var
	);

	// given a boundary variable and boundary type, get all the allowed BCTypes
	std::vector<BCType> getAllowedBCType(
		const BoundaryVariable& var,
		const BoundarySegmentGroup& group
	);

	double getDefaultBCValue(
		BoundaryType boundaryType,
		BoundaryVariable var
	);

	BoundaryCondition makeDefaultBC(
		const BoundarySegmentGroup& group,
		const BoundaryVariable& var
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