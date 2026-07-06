#pragma once

#include <string>

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
	LengthScale lengthScale;

	// set true when a project is loaded (units determined); the GUI consumes it
	// to recenter/re-zoom every surface inspector to the loaded project's units.
	bool resetInspectorViews = false;

	std::wstring path;
	std::string name;

};

struct AppSettings {

	std::wstring quickLaunch;

};
