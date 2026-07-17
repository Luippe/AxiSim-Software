#pragma once

#include "app_struct.h"	// AppConfig member below
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
	AppConfig appConfig;
	Menu menu;			// loads mesh/solver/results before the view objects are constructed
	
	SketchView sketch;
	SceneView scene;
	Inspector inspector;
	MeshInspector meshInspector;
	Console console;
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

	bool showingTutorial = false;

private:

	const float statusBarHeight = 26.0f;


	// main context
	ImGuiContext* mainImGuiContext = nullptr;
	ImPlotContext* mainImPlotContext = nullptr;

	// a different context to copy images, so it doesnt affect the main app ui
	ImGuiContext* exportImGuiContext = nullptr;
	ImPlotContext* exportImPlotContext = nullptr;


	// handle key inputs
	void handleKeyInput();

	// App-wide toolbar strip, in its own window pinned across the top of the
	// viewport. Only one view is live at a time (see render()'s currentTab
	// dispatch), so this just forwards to that view's toolbar. Call it from
	// render() AFTER drawUI(), which is what sets currentTab; newFrame() has
	// already reserved the space by shrinking the dockspace.
	void drawAppToolbar();

	// draw main ui on screen
	void drawUI();

	// Owns the shared viewport window (see UIViewport) while the Results tab is live,
	// and hosts the scene/inspector split in a dockspace of its own. That split must
	// NOT be nodes of the main dockspace: those are updated every frame, so a panel
	// that only exists on one tab drops out a frame late and makes the viewport above
	// it jump on the way in and out. An inner dockspace goes dormant while its host
	// window is unsubmitted, so the split survives untouched between visits and comes
	// back already laid out. Submitted even with no results so the window is never
	// missing for a frame.
	void drawResultsViewport();
	ImGuiWindowClass viewportWindowClass;

	// draw status bar at the bottom of screen
	void drawStatusBar();

	// draw tutorial when the showingTutorial is set to true
	void drawTutorial();
};


