#pragma once

#include "imgui.h"
#include "project.h"

class GUI;


class Menu {
public:
	Menu(Project& project);

	void render();

private:

	bool openShortcutModal = false;

	AppSettings settings;
	Project& project;

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
