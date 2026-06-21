#pragma once
#include "base_gui.h"


class Project;
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
	MeshGUI(Project& project, GUI& gui);

	void draw();

	void getGridConfigEdits();

	void setGridConfigEdits();

private:

	// draw properties panel when a tree node is clicked on
	void drawOverview();

	// draw each boundary groups
	void drawBoundaryGroupGUI();

	void drawRegionOfInfluenceGUI();

	int selectedBoundaryGroupID = -1;

	GridConfigEdits gridConfigEdits;
	Project& project;
	GUI& gui;

	Colormap& colormap;
	Mesh& mesh;
	Config& config;
};
