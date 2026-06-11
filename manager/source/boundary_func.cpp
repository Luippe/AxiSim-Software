#include "boundary_func.h"
#include "printer.h"

namespace BoundaryGet {

	// get a specific boundary group from group id
	BoundarySegmentGroup* getBoundaryGroupByID(
		std::vector<BoundarySegmentGroup>& boundaryGroups,
		int id
	) {
		for (BoundarySegmentGroup& group : boundaryGroups) {
			if (group.id == id) {
				return &group;
			}
		}
		return nullptr;
	}

	const BoundarySegmentGroup* getBoundaryGroupByID(
		const std::vector<BoundarySegmentGroup>& boundaryGroups,
		int id
	) {
		for (const BoundarySegmentGroup& group : boundaryGroups) {
			if (group.id == id) {
				return &group;
			}
		}
		return nullptr;
	}
}

namespace BoundaryDefaults {

	BoundaryCondition makeDefaultBC(
		const BoundarySegmentGroup& group,
		const BoundaryVariable& var
	) {
		BoundaryCondition bc{};
		bc.enabled = true;
		bc.type = getDefaultBCType(group.type, var);
		bc.value = getDefaultBCValue(var);

		return bc;
	}

	BCType getDefaultBCType(
		BoundaryType boundaryType,
		BoundaryVariable var
	) {
		switch (boundaryType) {

		case BoundaryType::VELOCITY_INLET:
			switch (var) {
			case BoundaryVariable::UVelocity:
				return BCType::DIRICHLET; // fully developed inlet condition
			case BoundaryVariable::VVelocity:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return BCType::DIRICHLET;

			case BoundaryVariable::Pressure:
				return BCType::NEUMANN;

			default:
				return BCType::NONE;
			}

		case BoundaryType::PRESSURE_OUTLET:
			switch (var) {
			case BoundaryVariable::Pressure:
				return BCType::DIRICHLET;

			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return BCType::NEUMANN;

			default:
				return BCType::NONE;
			}

		case BoundaryType::WALL:
			switch (var) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
				return BCType::DIRICHLET; // no-slip, value = 0

			case BoundaryVariable::Pressure:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return BCType::NEUMANN;

			default:
				return BCType::NONE;
			}

		case BoundaryType::SYMMETRY:
			switch (var) {
			case BoundaryVariable::VVelocity:
				return BCType::DIRICHLET; // normal velocity = 0, depending on orientation

			case BoundaryVariable::UVelocity:
			case BoundaryVariable::Pressure:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return BCType::NEUMANN;

			default:
				return BCType::NONE;
			}
		}

		return BCType::NONE;
	}


	double getDefaultBCValue(
		BoundaryVariable var
	) {
		switch (var) {

		case BoundaryVariable::UVelocity:
		case BoundaryVariable::VVelocity:
			return 0.0;

		case BoundaryVariable::Pressure:
			return 0.0;

		case BoundaryVariable::StaticTemperature:
			return 300.0;

		case BoundaryVariable::Concentration:
			return 0.0;

		}
		return 0.0;
	}

	std::vector<BCType> getAllowedBCType(
		const BoundaryVariable& var,
		const BoundarySegmentGroup& group
	) {

		switch (group.type) {

		case BoundaryType::VELOCITY_INLET:
			switch (var) {
			case BoundaryVariable::UVelocity:
				switch (group.includesOrientation) {
				case EdgeOrient::Vertical:
					return { BCType::DIRICHLET, BCType::FULLY_DEVELOPED };
				case EdgeOrient::Horizontal:
					return { BCType::DIRICHLET };
				default:
					return { BCType::NONE };
				}

			case BoundaryVariable::VVelocity:
				switch (group.includesOrientation) {
				case EdgeOrient::Horizontal:
					return { BCType::DIRICHLET, BCType::FULLY_DEVELOPED };
				case EdgeOrient::Vertical:
					return { BCType::DIRICHLET };
				default:
					return { BCType::NONE };
				}

			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return { BCType::DIRICHLET };
			case BoundaryVariable::Pressure:
				return { BCType::NEUMANN };
			default:
				return { BCType::NONE };
			}

		case BoundaryType::PRESSURE_OUTLET:
			switch (var) {
			case BoundaryVariable::Pressure:
				return { BCType::DIRICHLET };
			case BoundaryVariable::StaticTemperature:
				return { BCType::DIRICHLET, BCType::NEUMANN };
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
			case BoundaryVariable::Concentration:
				return { BCType::NEUMANN };
			default:
				return { BCType::NONE };
			}

		case BoundaryType::WALL:
			switch (var) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
				return { BCType::DIRICHLET }; // no-slip, value = 0
			case BoundaryVariable::StaticTemperature:
				return { BCType::DIRICHLET, BCType::NEUMANN };
			case BoundaryVariable::Pressure:
			case BoundaryVariable::Concentration:
				return { BCType::NEUMANN};
			default:
				return { BCType::NONE };
			}

		case BoundaryType::SYMMETRY:
			switch (var) {
			case BoundaryVariable::UVelocity:

				switch (group.includesOrientation) {
				case EdgeOrient::Vertical:
					return { BCType::DIRICHLET };
				case EdgeOrient::Horizontal:
					return { BCType::NEUMANN };
				case EdgeOrient::Both:
					return { BCType::NONE };
				default:
					return { BCType::NONE };
				}

			case BoundaryVariable::VVelocity:
				switch (group.includesOrientation) {
				case EdgeOrient::Horizontal:
					return { BCType::DIRICHLET };
				case EdgeOrient::Vertical:
					return { BCType::NEUMANN };
				case EdgeOrient::Both:
					return { BCType::NONE };
				}
				return { BCType::NONE };

			case BoundaryVariable::StaticTemperature:
				return { BCType::DIRICHLET, BCType::NEUMANN };

			case BoundaryVariable::Pressure:
			case BoundaryVariable::Concentration:
				return { BCType::NEUMANN };
			default:
				return { BCType::NONE };
			}
		}
		return { BCType::NONE };
	}

	bool isVariableInBoundaryType(BoundaryVariable variable, BoundaryType type) {
		switch (type) {

		case BoundaryType::WALL:
			switch (variable) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return true;
			default:
				return false;
			}

		case BoundaryType::SYMMETRY:
			return false;

		case BoundaryType::VELOCITY_INLET:
			switch (variable) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return true;
			default:
				return false;
			}

		case BoundaryType::PRESSURE_OUTLET:
			switch (variable) {
			case BoundaryVariable::Pressure:
			case BoundaryVariable::StaticTemperature:
			case BoundaryVariable::Concentration:
				return true;
			default:
				return false;
			}
		}
		

		return false;
	}

	std::vector<BoundaryVariable> getVariableFromBoundaryType(
		BoundaryType type,
		bool solveEnergy,
		bool solveConcentration
	) {
		std::vector<BoundaryVariable> variables;

		switch (type) {

		case BoundaryType::WALL:
			variables.push_back(BoundaryVariable::UVelocity);
			variables.push_back(BoundaryVariable::VVelocity);

			if (solveEnergy) {
				variables.push_back(BoundaryVariable::StaticTemperature);
			}

			if (solveConcentration) {
				variables.push_back(BoundaryVariable::Concentration);
			}

			break;

		case BoundaryType::SYMMETRY:
			break;

		case BoundaryType::VELOCITY_INLET:
			variables.push_back(BoundaryVariable::UVelocity);
			variables.push_back(BoundaryVariable::VVelocity);

			if (solveEnergy) {
				variables.push_back(BoundaryVariable::StaticTemperature);
			}

			if (solveConcentration) {
				variables.push_back(BoundaryVariable::Concentration);
			}

			break;

		case BoundaryType::PRESSURE_OUTLET:
			variables.push_back(BoundaryVariable::Pressure);

			if (solveEnergy) {
				variables.push_back(BoundaryVariable::StaticTemperature);
			}

			if (solveConcentration) {
				variables.push_back(BoundaryVariable::Concentration);
			}

			break;
		}

		return variables;
	}
}
