#pragma once
#include "colorbar.h"
#include "console.h"
#include "inspector.h"
#include "solver_gui.h"
#include "results_gui.h"
#include "mesh_gui.h"
#include "menu.h"

class Results;
class SceneView;
class Bounding;
class Mesh;
class Renderer;
class Solver;
enum ViewTab;
struct Config;


// class for managing GUI
class GUI {
public:
	Inspector inspector;
	Console console;
	Menu menu;
	Mesh& mesh;
	Solver& solver;
	Results& results;
	Renderer& renderer;
	GLFWwindow* window;
	Bounding& bound;
	SceneView& scene;
	Colormap& colormap;
	Colorbar colorbar;
	MeshGUI meshGUI;
	SolverGUI solverGUI;
	ResultsGUI resultsGUI;
	Config& config;

	GUI(GLFWwindow* window, SceneView& scene);

	void newFrame();

	// render the entire UI
	void render();

private:
	// change the cursor when hovering over certain UI elements
	void changeCursorOnHover();

	void drawUI();

	void initGUIBuffer(GLFWwindow* window);
};


