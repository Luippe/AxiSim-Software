#pragma once
#include "base_gui.h"

class SceneView;
class GUI;
class Results;
class Colormap;
class Mesh;
class Colorbar;

class ResultsGUI : public BaseGUI {
public:

	ResultsGUI(GUI& gui, SceneView& scene);
	void drawPropertiesPanel();
	void draw();

private:

	SceneView& scene;
	GUI& gui;
	Results& results;
	Colormap& colormap;
	Colorbar& colorbar;
	Mesh& mesh;

};