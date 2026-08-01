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

	const char* boundaryTypeToString(BoundaryType type);

	const char* boundaryVariableToString(BoundaryVariable var);

	const char* bcTypeToString(BCType type);
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

	double getDefaultBCValue();

	BoundaryCondition makeDefaultBC(
		const BoundarySegmentGroup& group,
		const BoundaryVariable& var
	);

	// The condition a group ACTUALLY applies to `var`: its own stored entry when the
	// group's boundary type lets the user edit that variable, and the generated
	// default otherwise.
	//
	// Both halves of that are load-bearing. group.bcs is filled lazily by
	// SolverGUI::getOrCreateBC, so a group whose row was never expanded holds nothing
	// and needs the default; and bcs is pruned only while that group's tree is being
	// drawn, so a group whose type changed off-screen still holds the old type's
	// entries -- which isVariableInBoundaryType is what rejects.
	//
	// Every consumer of a group's conditions must resolve them through here. The
	// solver, the device upload and the OpenFOAM export reading the same project
	// differently is the failure mode this exists to prevent.
	BoundaryCondition getEffectiveBC(
		const BoundarySegmentGroup& group,
		BoundaryVariable var
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