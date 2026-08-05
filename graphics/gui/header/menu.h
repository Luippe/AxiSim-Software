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

	// Fixed size of the two Advanced Options panes. The modal is AlwaysAutoResize,
	// so without a fixed body it would resize on every page switch and every
	// branch the tree opens. Both panes share the height so their borders line up,
	// the way the Visual Studio Options dialog this copies has them.
	static constexpr float advancedNavWidth = 250.0f;
	static constexpr float advancedBodyWidth = 700.0f;
	static constexpr float advancedBodyHeight = 560.0f;

	// What the Advanced Options tree has selected. A branch is one page, and its
	// leaves are that page's sections; the two together are the whole of the
	// modal's navigation state.
	enum class AdvancedPage {
		Geometry,
		Mesh,
		Solver,
		Results
	};

	AdvancedPage advancedPage = AdvancedPage::Geometry;

	// Which leaf, or null for the branch itself -- selecting a page shows every
	// section it has, stacked. Always points at one of the static section labels
	// in menu.cpp, so it never dangles.
	const char* advancedSection = nullptr;

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
	void drawAdvancedGeometryPage();
	void drawAdvancedMeshPage();
	void drawAdvancedSolverPage();
	void drawAdvancedResultsPage();

	// left-hand tree: one branch per page, one leaf per section. what it has
	// selected decides which drawAdvanced*Page() the body runs and which of that
	// page's sections beginAdvancedSection() lets through.
	void drawAdvancedNav();

	// one branch and its leaves. sections is a null-terminated list of section
	// labels, or null for a page that has none (drawn as a leaf, no arrow).
	void drawAdvancedBranch(const char* label, AdvancedPage page, const char* const* sections);

	// fixed-size scrolling body beside the category list. must be paired, and
	// unlike ImGui::Begin the end call is unconditional.
	void beginAdvancedPageBody();
	void endAdvancedPageBody();

	// one titled section inside an Advanced Options page. returns true when the
	// tree has it selected -- rows may only be submitted, and endAdvancedSection()
	// may only be called, when it does. col opens a first table for you (the usual
	// 2-column label/value one); pass 0 to open none and start with advancedTable().
	bool beginAdvancedSection(const char* label, int col = 2);
	void endAdvancedSection();

	// true when this section is what the tree has selected, either as the leaf
	// itself or through its page branch. label must match the leaf in the tree.
	bool advancedSectionVisible(const char* label) const;

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

	// stand-in for a page that has no settings wired up yet.
	void advancedEmptyPage(const char* what);

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
