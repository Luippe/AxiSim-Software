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

	Project& project;
	SceneView& scene;
	GUI& gui;
	Results& results;
	Colormap& colormap;
	Colorbar& colorbar;

};
