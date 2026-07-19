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

	const char* boundaryTypeToString(BoundaryType type) {
		switch (type) {
		case BoundaryType::WALL:
			return "Wall";

		case BoundaryType::VELOCITY_INLET:
			return "Velocity Inlet";

		case BoundaryType::PRESSURE_OUTLET:
			return "Pressure Outlet";

		case BoundaryType::SYMMETRY:
			return "Symmetry";

		case BoundaryType::FAR_FIELD:
			return "Far Field";

		default:
			return "Wall";
		}
	}

	const char* boundaryVariableToString(BoundaryVariable var) {

		switch (var) {
		case BoundaryVariable::UVelocity:
			return "U Velocity";

		case BoundaryVariable::VVelocity:
			return "V Velocity";

		case BoundaryVariable::Pressure:
			return "Pressure";

		case BoundaryVariable::StaticTemperature:
			return "Static Temperature";

		case BoundaryVariable::Concentration:
			return "Concentration";

		default:
			return "Unknown";
		}
	}

	const char* bcTypeToString(BCType type) {
		switch (type) {
		case BCType::DIRICHLET:			return "Dirichlet";
		case BCType::NEUMANN:			return "Neumann";
		case BCType::FULLY_DEVELOPED:	return "Fully Developed";
		case BCType::MICHAELIS_MENTEN:	return "Michaelis Menten";
		case BCType::HILL:				return "Hill";
		case BCType::NONE:				return "None";
		case BCType::PULSATILE:			return "Pulsatile";
		default:						return "Unknown";
		}
	}

}

namespace BoundaryDefaults {

	BoundaryCondition makeDefaultBC(
		const BoundarySegmentGroup& group,
		const BoundaryVariable& var
	) {
		BoundaryCondition bc{};
		bc.enabled = true;
		bc.setType(getDefaultBCType(group.type, var));
		bc.setValue(getDefaultBCValue());

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

		case BoundaryType::FAR_FIELD:
			switch (var) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
				return BCType::DIRICHLET;

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


	double getDefaultBCValue() {
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
					return { BCType::DIRICHLET, BCType::FULLY_DEVELOPED, BCType::PULSATILE };
				case EdgeOrient::Horizontal:
					return { BCType::DIRICHLET };
				case EdgeOrient::Both:
					return { BCType::DIRICHLET };
				default:
					return { BCType::NONE };
				}

			case BoundaryVariable::VVelocity:
				switch (group.includesOrientation) {
				case EdgeOrient::Horizontal:
					return { BCType::DIRICHLET, BCType::FULLY_DEVELOPED, BCType::PULSATILE };
				case EdgeOrient::Vertical:
					return { BCType::DIRICHLET };
				case EdgeOrient::Both:
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
				return { BCType::NEUMANN };
			case BoundaryVariable::Concentration:
				return { BCType::DIRICHLET, BCType::NEUMANN, BCType::MICHAELIS_MENTEN, BCType::HILL };
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

		case BoundaryType::FAR_FIELD:
			switch (var) {
			case BoundaryVariable::UVelocity:
			case BoundaryVariable::VVelocity:
				return { BCType::DIRICHLET };

			case BoundaryVariable::Pressure:
			case BoundaryVariable::StaticTemperature:
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
			// Locked presets use their generated defaults rather than storing
			// user-editable per-variable boundary conditions.
			return false;

		case BoundaryType::FAR_FIELD:
			// Far-field velocity values are user-specified Dirichlet data. Pressure
			// and scalars remain locked to their generated zero-gradient defaults.
			return variable == BoundaryVariable::UVelocity ||
				variable == BoundaryVariable::VVelocity;

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

		case BoundaryType::FAR_FIELD:
			variables.push_back(BoundaryVariable::UVelocity);
			variables.push_back(BoundaryVariable::VVelocity);
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
