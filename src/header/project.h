#pragma once
#include "geometry.h"
#include "mesh.h"
#include "solver.h"
#include "results.h"

#include "graphics_struct.h"

class Project {
public:
	//Project() {};
	Config config;

	Geometry geometry{ config };
	Mesh mesh{ config };
	Solver solver{ config };
	Results results{ config };

	ViewTab currentTab = ViewTab::TAB_MESH;

};