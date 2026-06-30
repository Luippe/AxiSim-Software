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

	if (V2 == 0.0) return 1.0;

	double K2 = bc.k2ByGroup ? bc.k2ByGroup[groupID] : 0.0;
	double K2m = bc.k2MByGroup ? bc.k2MByGroup[groupID] : 0.0;
	double m = bc.mByGroup ? bc.mByGroup[groupID] : 1.0;

	double cm = pow(c, m);

	return (-(m * V2 * K2m) * (cm) / (c * (K2m + cm) * (K2m + cm)));
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

	return ((n * Vmax * Kmn) * (cn) / (c * (Kmn + cn) * (Kmn + cn)));

}

__device__
void wallConcentration(const BoundaryFieldDevice& bc, int groupID, double cp, double& cw, double h) {

	cw = cp;

	int n = 20;
	for (int i = 0; i < n; i++) {
		double MM = MichaelisMenten(bc, groupID, cw);
		double inhib = Inhibition(bc, groupID, cw);
		double J = MM * inhib;
		double dJ = MM * dInhibition(bc, groupID, cw) + dMichaelisMenten(bc, groupID, cw) * inhib;

		double F = h * (cp - cw) - J;
		double dF = -h - dJ;

		if (fabs(F / dF) < 1e-9) break;
		cw -= (F / dF);
		cw = fmax(0.0, cw);
	}
}
