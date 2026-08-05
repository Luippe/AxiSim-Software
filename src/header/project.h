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
	Results results;

	ViewTab currentTab = ViewTab::TAB_MESH;

	// Tab key requests a programmatic switch to requestedTab; the matching setup
	// tab consumes it via ImGuiTabItemFlags_SetSelected, then drawUI clears it.
	bool tabSwitchRequested = false;
	ViewTab requestedTab = ViewTab::TAB_GEOMETRY;

	LengthScale lengthScale;

	// set true when a project is loaded (units determined); the GUI consumes it
	// to recenter/re-zoom every surface inspector to the loaded project's units.
	bool resetInspectorViews = false;

	// Simple view: every panel but the live viewport is hidden (setup tabs, console,
	// toolbar strip, status bar), leaving the inspector full-window. Toggled from
	// View > GUI; the GUI latches it once per frame (see GUI::simpleViewThisFrame).
	bool simpleView = false;

	// path and name of current project
	std::wstring path;
	std::string name;

	void createNew();



};