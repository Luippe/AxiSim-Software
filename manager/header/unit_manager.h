#pragma once
#include <array>

struct VariableUnits {

    // mesh
    std::uint8_t LUnit = 0;
    std::uint8_t RUnit = 0;

    // solver
    std::uint8_t  rhoUnit = 0;
    std::uint8_t  muUnit = 0;
    std::uint8_t  DUnit = 0;
};

struct UnitOption {
	const char* name;
	double toBase;
};

struct UnitValue {
	double baseValue = 0.0;
	std::uint8_t unitIndex = 0;
};

namespace Units {
    
    inline constexpr std::array<UnitOption, 4> velocityUnits = { {
        { "mm/s", 1.0    },
        { "m/s",  1000.0 },
        { "cm/s", 10.0   },
        { "um/s", 0.001  }
    } };

    inline constexpr std::array<UnitOption, 4> diffusionUnits = { {
        { "mm^2/s", 1.0    },
        { "m^2/s",  1.0e6  },
        { "cm^2/s", 100.0  },
        { "um^2/s", 1.0e-6 }
    } };

    inline constexpr std::array<UnitOption, 4> dynamicViscosityUnits = { {
        { "kg*mm^-1*s^-1", 1.0    },
        { "kg*m^-1*s^-1",  1.0e-3 },
        { "Pa*s",          1.0e-3 },
        { "mPa*s",         1.0e-6 }
    } };

    inline constexpr std::array<UnitOption, 2> densityUnits = { {
        { "kg/mm^3", 1.0    },
        { "kg/m^3",  1.0e-9 }
    } };

    inline constexpr std::array<UnitOption, 4> diffusionCoefficientUnits = { {
        { "mm^2/s", 1.0     },
        { "m^2/s",  1.0e6   },
        { "cm^2/s", 1.0e2   },
        { "um^2/s", 1.0e-6  }
    } };

    inline constexpr std::array<UnitOption, 3> lengthUnits = { {
        {"mm",	1.0},
        {"m",	1000.0},
        {"um",	0.001}
    } };

    inline constexpr std::array<UnitOption, 3> timeUnits = { {
        {"s",	1.0},
        {"min", 1.0}
    } };

}
