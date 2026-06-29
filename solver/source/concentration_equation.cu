#include "concentration_equation.cuh"
#include "device_launch_parameters.h"

#include <math_constants.h>


__device__
void inhibition(FluidPropertyConfig& f) {

	double Vmax = f.Vmax;
	double Km = f.Km;
	double n = f.n;
	double m = f.m;
	double K2 = f.K2;
	double V2 = f.V2;


	if (c == 0.0) {	// if c = 0, the equation simplifies significantly. it will cause NaN if we try solving below
		return (n * Vmax / pow(Km, n));
	}
	else {
		double c_n = pow(c, n);
		double c_m = pow(c, m);
		double K2_m = pow(K2, m);
		double Km_n = pow(Km, n);

		return ((Vmax * c_n) / (Km_n + c_n)) *
			(-(m * V2 * K2_m) * (c_m) / (c * (K2_m + c_m) * (K2_m + c_m))) +
			(1.0 - (V2 * c_m / (K2_m + c_m))) *
			((n * Vmax * Km_n) * (c_n) / (c * (Km_n + c_n) * (Km_n + c_n)));
	}

}