#pragma once
#include "base_gui.h"

class SceneView;
class GUI;
class Results;
class Colormap;
class Mesh;
class Config;

struct GridConfigEdits {
	int nseg;
	double L;
	double R;
	int nr;
	int nz;
};

class MeshGUI : public BaseGUI {
public:
	MeshGUI(GUI& gui, SceneView& scene);
	void draw();

	void getGridConfigEdits();

	void setGridConfigEdits();

private:

	GridConfigEdits GridConfigEdits;
	SceneView& scene;
	GUI& gui;
	Results& results;
	Colormap& colormap;
	Mesh& mesh;
	Config& config;
};
