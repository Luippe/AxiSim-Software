#pragma once

#include "imgui.h"
#include "project.h"
#include "app_struct.h"	// AppSettings member below

class GUI;


class Menu {
public:
	Menu(Project& project);

	void render();

private:

	bool openShortcutModal = false;
	bool openUnitsModal = false;


	AppSettings settings;
	Project& project;

	// open selected file
	void drawOpen();

	// draw the view menu
	void drawView();
	
	// save selected object as .bin file
	void drawSave();

	void drawEditShortcut();

	// open popup when edit shortcut is pressed
	void drawShortcutModal();

	// open popup to edit display units
	void drawUnitsModal();

};
