#pragma once
#include "imgui.h"

#include "base_gui.h"
#include "graphics_struct.h"

class Project;
struct AppAssets;

class GeometryGUI : public BaseGUI {

public:
	GeometryGUI(Project& project, AppAssets& assets);

	void drawPropertiesPanel();
	void draw();

private:

	Project& project;
	AppAssets& assets;
};
