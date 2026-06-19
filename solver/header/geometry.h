#pragma once

#include "sketch_struct.h"

struct Config;

class Geometry {
public:

	Geometry(Config& config);

	SketchModel sketch;

};