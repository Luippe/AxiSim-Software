#pragma once
#include <array>

struct VariableUnits {

    // mesh
    std::uint8_t LUnit = 0;
    std::uint8_t RUnit = 0;

    // solver
    std::uint8_t axialUnit = 0;
    std::uint8_t radialUnit = 0;
    std::uint8_t pressureUnit = 0;
    std::uint8_t temperatureUnit = 0;
    std::uint8_t concentrationUnit = 0;
    std::uint8_t  rhoUnit = 0;
    std::uint8_t  muUnit = 0;
    std::uint8_t  DUnit = 0;
    
    std::uint8_t specificHeatUnit = 0;
    std::uint8_t heatCondUnit = 0;

    std::uint8_t VmaxUnit = 0;

    // wall layers
    std::uint8_t layerKUnit = 0;
    std::uint8_t layerDUnit = 0;

};

struct LinearUnitOption {
    const char* name;

    double toBase;   // multiply first
    double offsetTo;  // then add
};

struct UnitOption {
    const char* name;
    double toBase;
};

struct UnitValue {
    double baseValue = 0.0;
    std::uint8_t unitIndex = 0;
};

inline double toBaseValue(double displayValue, const UnitOption& unit) {
    return displayValue * unit.toBase;
}

inline double fromBaseValue(double baseValue, const UnitOption& unit) {
    return baseValue / unit.toBase;
}

inline double toBaseValue(double displayValue, const LinearUnitOption& unit) {
    return displayValue * unit.toBase + unit.offsetTo;
}

inline double fromBaseValue(double baseValue, const LinearUnitOption& unit) {
    return (baseValue - unit.offsetTo) / unit.toBase;
}



namespace Units {

    inline constexpr std::array<UnitOption, 4> velocityUnits = { {
        { "m/s",  1.0     },
        { "mm/s", 1.0e-3  },
        { "cm/s", 1.0e-2  },
        { "um/s", 1.0e-6  }
    } };

    inline constexpr std::array<UnitOption, 6> pressureUnits = { {
        { "Pa",             1.0      },
        { "kPa",            1.0e3    },
        { "MPa",            1.0e6    },
        { "bar",            1.0e5    },
        { "kg*m^-1*s^-2",   1.0      },
        { "atm",            101325.0 }
    } };

    inline constexpr std::array<UnitOption, 4> specificHeatUnits = { {
        { "J/(kg K)",    1.0    },
        { "kJ/(kg K)",   1.0e3  },
        { "J/(g K)",     1.0e3  },
        { "cal/(g K)",   4184.0 }
    } };

    inline constexpr std::array<UnitOption, 4> thermalConductivityUnits = { {
        { "W/(m K)",     1.0    },
        { "W/(cm K)",    100.0  },
        { "W/(mm K)",    1000.0 },
        { "mW/(mm K)",   1.0    }
    } };

    inline constexpr std::array<LinearUnitOption, 3> temperatureUnits = { {
        { "K",  1.0,        0.0 },
        { "C",  1.0,        273.15 },
        { "F",  5.0 / 9.0,  255.3722222222222 }
    } };

    inline constexpr std::array<UnitOption, 4> diffusionUnits = { {
        { "m^2/s",   1.0     },
        { "mm^2/s",  1.0e-6  },
        { "cm^2/s",  1.0e-4  },
        { "um^2/s",  1.0e-12 }
    } };

    // Base unit is nmol/m^3. 1 mol = 1e9 nmol, so 1 mol/m^3 = 1e9 nmol/m^3.
    // Molar units are per-litre: 1 mol/L = 1e3 mol/m^3 = 1e12 nmol/m^3.
    inline constexpr std::array<UnitOption, 8> concentrationUnits = { {
        { "nmol/m^3",  1.0    },   // base unit
        { "mol/m^3",   1.0e9  },
        { "M",         1.0e12 },   // mol/L  (molar)
        { "mM",        1.0e9  },   // mmol/L (millimolar) == mol/m^3
        { "uM",        1.0e6  },   // umol/L (micromolar)
        { "nM",        1.0e3  },   // nmol/L (nanomolar)
        { "nmol/mL",   1.0e6  },   // == uM, lab-native volumetric
        { "nmol/mm^3", 1.0e9  }    // == mol/m^3 == mM
    } };

    // Base unit is nmol/(m^2 * s). 1 cm^2 = 1e-4 m^2, so a per-cm^2 flux is
    // 1e4x larger numerically; 1 mm^2 = 1e-6 m^2 makes a per-mm^2 flux 1e6x
    // larger; molar prefixes scale the amount term.
    inline constexpr std::array<UnitOption, 6> VmaxUnits = { {
        { "nmol/(m^2*s)",  1.0    },   // base unit
        { "mol/(m^2*s)",   1.0e9  },
        { "umol/(m^2*s)",  1.0e3  },
        { "pmol/(m^2*s)",  1.0e-3 },
        { "nmol/(cm^2*s)", 1.0e4  },
        { "nmol/(mm^2*s)", 1.0e6  }
    } };

    inline constexpr std::array<UnitOption, 4> dynamicViscosityUnits = { {
        { "Pa*s",           1.0    },
        { "kg*m^-1*s^-1",   1.0    },
        { "kg*mm^-1*s^-1",  1000.0 },
        { "mPa*s",          1.0e-3 }
    } };

    inline constexpr std::array<UnitOption, 2> densityUnits = { {
        { "kg/m^3",   1.0    },
        { "kg/mm^3",  1.0e9  }
    } };

    inline constexpr std::array<UnitOption, 4> diffusionCoefficientUnits = { {
        { "m^2/s",   1.0     },
        { "mm^2/s",  1.0e-6  },
        { "cm^2/s",  1.0e-4  },
        { "um^2/s",  1.0e-12 }
    } };

    inline constexpr std::array<UnitOption, 3> lengthUnits = { {
        { "m",   1.0    },
        { "mm",  1.0e-3 },
        { "um",  1.0e-6 }
    } };

    inline constexpr std::array<UnitOption, 2> timeUnits = { {
        { "s",    1.0  },
        { "min",  60.0 }
    } };

}
