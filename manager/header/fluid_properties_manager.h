#pragma once
#include <array>
#include <cstddef>

#include "setting.cuh"		// FluidPropertyConfig

// Named fluid property presets offered by the Solver tab's Fluid Properties
// section. Applying one overwrites rho / mu / cp / k / D; nothing else about the
// project is touched.
//
// UNITS: every value here is in **base SI**, matching how the solver stores fluid
// properties internally (kg/m^3, Pa.s, J/(kg.K), W/(m.K), m^2/s). The unit combos
// in the Units modal only affect how these are displayed, so a preset does not
// need to know which display units are selected.
//
// ABOUT D: diffusivity is a property of a solute IN a solvent, not of the solvent
// alone -- there is no single "diffusion coefficient of water". Each preset
// carries a representative value for the solute named in its `note` (oxygen for
// the aqueous presets, since that is what this solver is usually pointed at).
// Change D by hand for any other species; the rest of the preset still holds.
namespace FluidPresets {

	struct FluidPreset {
		const char* name;

		double rho;		// density                kg/m^3
		double mu;		// dynamic viscosity      Pa.s
		double cp;		// specific heat          J/(kg.K)
		double k;		// thermal conductivity   W/(m.K)
		double D;		// mass diffusivity       m^2/s

		// which solute D refers to, plus any modelling caveat. Documentation for
		// whoever edits this table -- the UI does not display it.
		const char* note;
	};

	// Index 0 is deliberately not a fluid: it is the state the combo sits in when
	// the current values do not (or no longer) match any preset, so the control
	// never claims properties are something they are not. Applying it is a no-op.
	inline constexpr std::array<FluidPreset, 6> presets = { {
		{
			"Custom",
			0.0, 0.0, 0.0, 0.0, 0.0,
			"Values entered by hand; no preset applied."
		},
		{
			"Water (20 C)",
			998.2, 1.002e-3, 4182.0, 0.598, 2.0e-9,
			"D = oxygen in water at 20 C."
		},
		{
			"Water (37 C)",
			993.3, 6.92e-4, 4178.0, 0.628, 3.0e-9,
			"D = oxygen in water at 37 C."
		},
		{
			"Blood (37 C)",
			1060.0, 3.5e-3, 3617.0, 0.52, 2.18e-9,
			"Newtonian approximation - real blood is shear-thinning. D = oxygen in plasma."
		},
		{
			"Air (20 C, 1 atm)",
			1.204, 1.813e-5, 1005.0, 0.0257, 2.42e-5,
			"D = water vapour in air at 20 C."
		},
		{
			"Glycerol-water 50% (20 C)",
			1126.0, 6.0e-3, 3430.0, 0.415, 5.0e-10,
			"Viscosity-matched lab fluid. D = small solute, order of magnitude only."
		}
	} };

	// Index 0 ("Custom") applies nothing -- see the comment on `presets`.
	constexpr int customIndex = 0;

	inline void apply(int index, FluidPropertyConfig& f) {

		if (index <= customIndex || index >= (int)presets.size()) {
			return;
		}

		const FluidPreset& p = presets[(size_t)index];

		f.rho = p.rho;
		f.mu = p.mu;
		f.cp = p.cp;
		f.k = p.k;
		f.D = p.D;
	}

	// Index of the preset whose values all match `f`, or customIndex if none does.
	// The combo derives its label from this rather than caching a selection, so
	// editing any property by hand immediately (and correctly) reads as "Custom"
	// instead of leaving the control claiming a preset it no longer matches.
	//
	// Exact comparison is deliberate: a preset's values are copied verbatim into f,
	// so they stay bit-identical until something else writes them.
	inline int matchingIndex(const FluidPropertyConfig& f) {

		for (size_t i = customIndex + 1; i < presets.size(); i++) {

			const FluidPreset& p = presets[i];

			if (f.rho == p.rho && f.mu == p.mu && f.cp == p.cp &&
				f.k == p.k && f.D == p.D) {
				return (int)i;
			}
		}

		return customIndex;
	}
}
