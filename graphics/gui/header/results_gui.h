#pragma once
#include "base_gui.h"

class SceneView;
class GUI;
class Results;
class Colormap;
class Colorbar;
class Project;

class ResultsGUI : public BaseGUI {
public:

	ResultsGUI(Project& project, GUI& gui);
	void drawPropertiesPanel();
	void draw();

private:

	// two-region drag-and-drop picker (Available | Shown) for choosing which fields
	// the inspector displays as tabs. Drawn in the Graphics panel.
	void drawFieldSelector();

	Project& project;
	SceneView& scene;
	GUI& gui;
	Results& results;
	Colormap& colormap;
	Colorbar& colorbar;

};
