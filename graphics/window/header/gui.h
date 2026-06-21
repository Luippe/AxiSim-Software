#pragma once

#include "console.h"
#include "inspector.h"
#include "geometry_gui.h"
#include "solver_gui.h"
#include "results_gui.h"
#include "mesh_gui.h"
#include "menu.h"
#include "animation_gui.h"
#include "residual_plot.h"
#include "mesh_inspector.h"
#include "sketch_view.h"
#include "scene_view.h"

class Mesh;
class Solver;
class Results;
class Project;
struct Config;
class Display;

enum class ViewTab;

// class for managing GUI
class GUI {
public:
	Menu menu;			// menu must come before anything else since it loads the mesh/solver/results when constructed
	
	SketchView sketch;
	SceneView scene;
	Inspector inspector;
	MeshInspector meshInspector;
	Console console;
	AppConfig appConfig;
	ResidualPlot residualPlot;

	Project& project;
	Mesh& mesh;
	Solver& solver;
	Results& results;
	GeometryGUI geometryGUI;
	MeshGUI meshGUI;
	SolverGUI solverGUI;
	ResultsGUI resultsGUI;
	AnimationGUI animationGUI;
	Config& config;

	GUI(Project& project, Display& disp);

	void newFrame();

	// render the entire UI
	void render();

private:

	const float statusBarHeight = 26.0f;

	// main context
	ImGuiContext* mainImGuiContext = nullptr;
	ImPlotContext* mainImPlotContext = nullptr;

	// a different context to copy images, so it doesnt affect the main app ui
	ImGuiContext* exportImGuiContext = nullptr;
	ImPlotContext* exportImPlotContext = nullptr;


	// draw main ui on screen
	void drawUI();

	// draw status bar at the bottom of screen
	void drawStatusBar();


};


