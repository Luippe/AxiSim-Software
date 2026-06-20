#pragma once
#include "imgui.h"

#include "base_gui.h"
#include "graphics_struct.h"

class Project;
class GUI;

class GeometryGUI : public BaseGUI {

public:
	GeometryGUI(Project& project, GUI& gui);

	void drawPropertiesPanel();
	void draw();

private:

	Project& project;
	GUI& gui;
};
