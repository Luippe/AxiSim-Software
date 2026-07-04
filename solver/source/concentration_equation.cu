#include "concentration_equation.cuh"
#include "device_launch_parameters.h"

#include <math_constants.h>

__device__
double MichaelisMenten(const BoundaryFieldDevice& bc, int groupID, double c) {

	double Vmax = bc.vmaxByGroup ? bc.vmaxByGroup[groupID] : 0.0;
	double Km = bc.kmByGroup ? bc.kmByGroup[groupID] : 0.0;

	return (Vmax * c) / (Km + c);

}

__device__
double dMichaelisMenten(const BoundaryFieldDevice& bc, int groupID, double c) {

	double Vmax = bc.vmaxByGroup ? bc.vmaxByGroup[groupID] : 0.0;
	double Km = bc.kmByGroup ? bc.kmByGroup[groupID] : 0.0;

	double inv = 1.0 / (Km + c);

	return Vmax * Km * inv * inv;

}

__device__
double Inhibition(const BoundaryFieldDevice& bc, int groupID, double c) {

	double V2 = bc.v2ByGroup ? bc.v2ByGroup[groupID] : 0.0;

	if (V2 == 0.0) return 1.0;

	double K2 = bc.k2ByGroup ? bc.k2ByGroup[groupID] : 0.0;
	double K2m = bc.k2MByGroup ? bc.k2MByGroup[groupID] : 0.0;
	double m = bc.mByGroup ? bc.mByGroup[groupID] : 1.0;

	double cm = pow(c, m);

	return (1.0 - (V2 * cm / (K2m + cm)));
}

__device__
double dInhibition(const BoundaryFieldDevice& bc, int groupID, double c) {

	double V2 = bc.v2ByGroup ? bc.v2ByGroup[groupID] : 0.0;

	// Inhibition() is the constant 1.0 when V2 == 0, so its derivative is 0.0.
	// Returning 1.0 here corrupts the Newton Jacobian in wallConcentration*(),
	// stalling the solve so the wall barely consumes regardless of Vmax.
	if (V2 == 0.0) return 0.0;

	double K2 = bc.k2ByGroup ? bc.k2ByGroup[groupID] : 0.0;
	double K2m = bc.k2MByGroup ? bc.k2MByGroup[groupID] : 0.0;
	double m = bc.mByGroup ? bc.mByGroup[groupID] : 1.0;

	double cm = pow(c, m);
	double cm1 = pow(c, m - 1.0);

	return (-(m * V2 * K2m) * (cm1) / ((K2m + cm) * (K2m + cm)));
}

__device__
double Hill(const BoundaryFieldDevice& bc, int groupID, double c) {

	double Vmax = bc.vmaxByGroup ? bc.vmaxByGroup[groupID] : 0.0;
	double Kmn = bc.kmNByGroup ? bc.kmNByGroup[groupID] : 0.0;
	double n = bc.nByGroup ? bc.nByGroup[groupID] : 1.0;

	double cn = pow(c, n);

	return (Vmax * cn) / (Kmn + cn);

}

__device__
double dHill(const BoundaryFieldDevice& bc, int groupID, double c) {

	double Vmax = bc.vmaxByGroup ? bc.vmaxByGroup[groupID] : 0.0;
	double Kmn = bc.kmNByGroup ? bc.kmNByGroup[groupID] : 0.0;
	double n = bc.nByGroup ? bc.nByGroup[groupID] : 1.0;

	double cn = pow(c, n);
	double cn1 = pow(c, n - 1.0);

	return ((n * Vmax * Kmn) * (cn1) / ((Kmn + cn) * (Kmn + cn)));

}

__device__
void wallConcentrationMichaelisMenten(const BoundaryFieldDevice& bc, int groupID, double cp, double& cw, double h) {

	cw = cp;

	int n = 100;
	const double relTol = 1e-10;
	const double absTol = 1e-14;

	// #pragma unroll 1: this loop almost always converges (breaks) in a
	// handful of iterations; the compiler doesn't know that statically and
	// was fully unrolling all 100 iterations into one function, blowing up
	// register usage enough to fail launch on some GPUs. Force a real loop.
	#pragma unroll 1
	for (int i = 0; i < n; i++) {
		double MM = MichaelisMenten(bc, groupID, cw);
		double inhib = Inhibition(bc, groupID, cw);

		double J = MM * inhib;
		double dJ = MM * dInhibition(bc, groupID, cw) + dMichaelisMenten(bc, groupID, cw) * inhib;

		double F = h * (cp - cw) - J;
		double dF = -h - dJ;

		double step = F / dF;
		cw -= step;
		cw = fmax(0.0, cw);

		// Mixed tolerance: absTol guards convergence near cw = 0 (where a
		// pure relative check would never be satisfied), relTol scales with
		// cw so the check stays meaningful across concentration unit ranges.
		if (fabs(step) < absTol + relTol * fabs(cw)) {
			break;
		}
	}
}

__device__
void wallConcentrationHill(const BoundaryFieldDevice& bc, int groupID, double cp, double& cw, double h) {

	cw = cp;

	int n = 100;
	const double relTol = 1e-10;
	const double absTol = 1e-14;

	// #pragma unroll 1: see wallConcentrationMichaelisMenten above.
	#pragma unroll 1
	for (int i = 0; i < n; i++) {
		double hill = Hill(bc, groupID, cw);
		double inhib = Inhibition(bc, groupID, cw);

		double J = hill * inhib;
		double dJ = hill * dInhibition(bc, groupID, cw) + dHill(bc, groupID, cw) * inhib;

		double F = h * (cp - cw) - J;
		double dF = -h - dJ;

		double step = F / dF;
		cw -= step;
		cw = fmax(0.0, cw);

		// Mixed tolerance: absTol guards convergence near cw = 0 (where a
		// pure relative check would never be satisfied), relTol scales with
		// cw so the check stays meaningful across concentration unit ranges.
		if (fabs(step) < absTol + relTol * fabs(cw)) {
			break;
		}
	}
}

