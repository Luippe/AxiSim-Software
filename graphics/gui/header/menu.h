#pragma once

#include "imgui.h"

class SceneView;
class Project;
class GUI;

class Menu {
public:
	Menu(Project& project, GUI& gui);

	void render();

private:

	bool openShortcutModal = false;

	Project& project;
	GUI& gui;

	// open selected file
	void drawOpen();

	// open selected files at launch
	void drawOpenAtLaunch();
	
	// save selected object as .bin file
	void drawSave();

	void drawEditShortcut();

	// open popup when edit shortcut is pressed
	void drawShortcutModal();

};
