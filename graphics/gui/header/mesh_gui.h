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

	void drawSections();

	void getGridConfigEdits();

	void setGridConfigEdits();

private:

	// draw properties panel when a tree node is clicked on
	void drawOverview();

	// draw each boundary groups
	void drawBoundaryGroupGUI();

	GridConfigEdits gridConfigEdits;
	SceneView& scene;
	GUI& gui;
	Results& results;
	Colormap& colormap;
	Mesh& mesh;
	Config& config;
};
