#pragma once
#include "colorbar.h"
#include "console.h"
#include "inspector.h"
#include "solver_gui.h"
#include "results_gui.h"
#include "mesh_gui.h"
#include "menu.h"
#include "animation_gui.h"
#include "residual_plot.h"
#include "mesh_inspector.h"

class Results;
class SceneView;
class Bounding;
class Mesh;
class Renderer;
class Solver;
enum class ViewTab;
struct Config;


// class for managing GUI
class GUI {
public:
	Menu menu;			// menu must come before anything else since it loads the mesh/solver/results
	Inspector inspector;
	MeshInspector meshInspector;
	Console console;
	AppAssets assets;
	ResidualPlot residualPlot;
	Mesh& mesh;
	Solver& solver;
	Results& results;
	Renderer& renderer;
	GLFWwindow* window;
	Bounding& bound;
	SceneView& scene;
	Colormap& colormap;
	MeshGUI meshGUI;
	SolverGUI solverGUI;
	ResultsGUI resultsGUI;
	AnimationGUI animationGUI;
	Config& config;

	GUI(GLFWwindow* window, SceneView& scene);

	void newFrame();

	// render the entire UI
	void render();

private:
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


