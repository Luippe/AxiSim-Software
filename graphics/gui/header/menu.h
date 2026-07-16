#pragma once

#include "imgui.h"
#include "project.h"
#include "app_struct.h"	// AppSettings member below

class GUI;
struct ImGuiWindow;


class Menu {
public:
	Menu(Project& project, GUI& gui);


	void render();

private:

	bool openShortcutModal = false;
	bool openUnitsModal = false;

	static constexpr float menuIconScale = 1.0f;
	static constexpr const char* menuIconPlaceholder = "    ";

	AppSettings settings;
	AppAssets& assets;
	Project& project;

	// open selected file
	void drawOpen();

	// draw the view menu
	void drawView();
	
	// save selected object as .bin file
	void drawSave();

	void drawEditShortcut();

	void drawExportImport();

	// open popup when edit shortcut is pressed
	void drawShortcutModal();

	// open popup to edit display units
	void drawUnitsModal();

	bool beginMenu(const char* label, TextureBuffer& icon, bool enabled = true);

	bool beginMenu(const char* label, bool enabled = true);

	bool menuItem(
		const char* label,
		TextureBuffer& icon,
		const char* shortcut = nullptr,
		bool selected = false,
		bool enabled = true
	);

	bool menuItem(
		const char* label,
		const char* shortcut = nullptr,
		bool selected = false,
		bool enabled = true
	);

	void drawLastMenuIcon(TextureBuffer& icon, ImGuiWindow* itemWindow);

};
