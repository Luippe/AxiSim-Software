#include "boundary_func.h"


namespace BoundaryDefaults {

	BoundaryCondition makeDefaultBC(
		BoundaryType boundaryType,
		BoundaryVariable var
	) {
		BoundaryCondition bc{};
		bc.enabled = true;
		bc.type = getDefaultBCType(boundaryType, var);
		bc.value = getDefaultBCValue(boundaryType, var);

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
			case BoundaryVariable::TurbulenceIntensity:
			case BoundaryVariable::TurbulentViscosityRatio:
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
		BoundaryType boundaryType,
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

		case BoundaryVariable::TurbulenceIntensity:
			return 5.0;

		case BoundaryVariable::TurbulentViscosityRatio:
			return 10.0;
		}

		return 0.0;
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
		const BoundarySegmentGroup& group,
		bool solveEnergy,
		bool solveConcentration
	) {
		std::vector<BoundaryVariable> variables;

		switch (group.type) {

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

			variables.push_back(BoundaryVariable::TurbulenceIntensity);
			variables.push_back(BoundaryVariable::TurbulentViscosityRatio);
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