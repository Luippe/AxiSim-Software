#pragma once
#include "boundary_struct.h"

namespace BoundaryGet {

	BoundarySegmentGroup* getBoundaryGroupByID(
		std::vector<BoundarySegmentGroup>& boundaryGroups,
		int id
	);

	const BoundarySegmentGroup* getBoundaryGroupByID(
		const std::vector<BoundarySegmentGroup>& boundaryGroups,
		int id
	);
}

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
		BoundaryType type,
		bool solveEnergy,
		bool solveConcentration
	);
}