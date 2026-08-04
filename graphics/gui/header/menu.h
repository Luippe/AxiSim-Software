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
	bool openSnappingModal = false;
	bool openAdvancedOptionsModal = false;


	static constexpr float menuIconScale = 1.0f;
	static constexpr const char* menuIconPlaceholder = "    ";

	// Fixed size of the body under the Advanced Options tab strip. The modal is
	// AlwaysAutoResize, so without a fixed body it would resize on every tab
	// switch and every collapsing header toggle.
	static constexpr float advancedBodyWidth = 520.0f;
	static constexpr float advancedBodyHeight = 420.0f;

	// which table of the open Advanced Options section is being filled. the index
	// only has to make the table IDs unique inside the section, so it is reset per
	// section; -1 means no table is open and there is no EndTable() owing.
	int advancedTableIndex = -1;

	AppSettings settings;
	AppAssets& assets;
	Project& project;

	// Menu is constructed before the view objects (it loads mesh/solver/results
	// first), so this is bound but not dereferenced until a menu is drawn.
	GUI& gui;

	// create new project
	void drawNew();

	// open selected file
	void drawOpen();

	// draw the view menu
	void drawView();
	
	// save selected object as .bin file
	void drawSave();

	void drawEditShortcut();

	void drawExport();

	// open popup when edit shortcut is pressed
	void drawShortcutModal();

	// open popup when editing snapping priorities
	void drawSnappingModal();

	// open popup to edit display units
	void drawUnitsModal();

	// open popup to edit advanced options
	void drawAdvancedOptionModal();

	// one page of the Advanced Options modal. each one is a list of
	// beginAdvancedSection(...) / rows / endAdvancedSection() blocks.
	void drawAdvancedGeometryTab();
	void drawAdvancedMeshTab();
	void drawAdvancedSolverTab();
	void drawAdvancedResultsTab();

	// fixed-size scrolling page under the tab strip. must be paired, and unlike
	// ImGui::Begin the end call is unconditional.
	void beginAdvancedTabBody();
	void endAdvancedTabBody();

	// one collapsing section inside an Advanced Options tab. returns true when the
	// section is expanded -- rows may only be submitted, and endAdvancedSection()
	// may only be called, when it does. col opens a first table for you (the usual
	// 2-column label/value one); pass 0 to open none and start with advancedTable().
	bool beginAdvancedSection(const char* label, int col = 2);
	void endAdvancedSection();

	// start another table inside the open section, closing whichever one is open.
	// a section may hold any number of tables, each with its own column count, so
	// rows submitted after this go to the new table.
	void advancedTable(int col);

	// start a settings row: writes the label cell and leaves the cursor in the
	// value cell with the next widget stretched to fill it. follow it with one
	// widget (InputInt, Checkbox, Combo, TextUnformatted...).
	void advancedRow(const char* label);

	// move to the next value cell of the current row, set up like the one
	// advancedRow() left the cursor in. one call per extra column past the first.
	void advancedCell();

	// stand-in for a tab that has no settings wired up yet.
	void advancedEmptyTab(const char* what);

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
