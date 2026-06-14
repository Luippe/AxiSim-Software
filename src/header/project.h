#pragma once
#include "mesh.h"
#include "solver.h"
#include "results.h"

#include "graphics_struct.h"

class Project {
public:
	//Project() {};
	Config config;

	Mesh mesh{ config };
	Solver solver{ config };
	Results results{ config };

	ViewTab currentTab = ViewTab::TAB_MESH;

};