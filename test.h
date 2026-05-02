#include <stdio.h>
#include <vector>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

struct testing {

};
__device__
void test_func(double a);
