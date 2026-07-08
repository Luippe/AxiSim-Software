#pragma once
#include "base_gui.h"


class Project;
class GUI;
class Mesh;
class Config;

class MeshGUI : public BaseGUI {
public:
	MeshGUI(Project& project, GUI& gui);

	void draw();

private:

	// draw properties panel when a tree node is clicked on
	void drawPropertiesPanel();

	// draw each boundary groups
	void drawBoundaryGroupGUI();

	void drawRegionOfInfluenceGUI();

	int selectedBoundaryGroupID = -1;

	Project& project;
	GUI& gui;

	Mesh& mesh;
	Config& config;
};
