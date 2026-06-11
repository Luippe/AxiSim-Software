#pragma once

#include "boundary_struct.h"

// getters
namespace BoundaryGet {

	// get a specific boundary group bc from group id
	BoundaryGroupBC* getBoundaryGroupBCByID(
		std::vector<BoundaryGroupBC>& boundaryGroupBCs,
		int id
	);

	const BoundaryGroupBC* getBoundaryGroupBCByID(
		const std::vector<BoundaryGroupBC>& boundaryGroupBCs,
		int id
	);

	// get a specific boundary group from group id
	BoundaryGroup* getBoundaryGroupByID(
		std::vector<BoundaryGroup>& boundaryGroups,
		int id
	);

	const BoundaryGroup* getBoundaryGroupByID(
		const std::vector<BoundaryGroup>& boundaryGroups,
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
		const BoundaryGroup& group,
		const BoundaryGroupBC& groupBC
	);

	double getDefaultBCValue(
		BoundaryVariable var
	);

	BoundaryCondition makeDefaultBC(
		const BoundaryGroupBC& groupBC,
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