#pragma once
#include "imgui.h"

#include <optional>
#include <unordered_set>
#include <algorithm>

#include "graphics_struct.h"
#include "core_struct.h"
#include "buffer_manager.h"	// TextureBuffer / FrameBuffer members and params below

#include "camera.h"
#include "shader.h"
#include "printer.h"

// ======================================================================
// -----------------------PUBLIC HELPER FUNCTIONS------------------------
// ======================================================================
template<typename TypeT>
bool drawRenamingPopup(
	const char* label,
	TypeT& target,
	std::vector<TypeT>& groups,
	bool* canceled = nullptr
) {
	bool confirmed = false;

	if (canceled) {
		*canceled = false;
	}

	if (ImGui::BeginPopupModal(
		label,
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
	)) {
		bool justOpened = ImGui::IsWindowAppearing();

		if (justOpened) {
			std::snprintf(
				target.nameBuffer,
				sizeof(target.nameBuffer),
				"%s",
				target.name.c_str()
			);

			ImGui::SetKeyboardFocusHere();
		}

		bool clickedOutside =
			!justOpened &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

		bool pressedEscape = ImGui::IsKeyPressed(ImGuiKey_Escape);

		ImGui::SetNextItemWidth(250.0f);

		bool enterPressed = ImGui::InputText(
			"##NameInput",
			target.nameBuffer,
			sizeof(target.nameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_AutoSelectAll
		);

		std::string newName = target.nameBuffer;

		bool emptyName = newName.empty();

		bool nameExists = std::any_of(
			groups.begin(),
			groups.end(),
			[&](const TypeT& group) {
				return group.id != target.id &&
					group.name == newName;
			}
		);

		bool invalidName = emptyName || nameExists;

		if (clickedOutside || pressedEscape) {
			if (canceled) {
				*canceled = true;
			}

			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();

		if (emptyName) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"Name cannot be empty"
			);
		}
		else if (nameExists) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"Name already exists"
			);
		}

		if (enterPressed && !invalidName) {
			target.name = newName;
			confirmed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	return confirmed;
}

template<typename TypeT>
bool drawNamingPopup(const char* label, TypeT& target, std::vector<TypeT>& groups, bool* canceled = nullptr) {

	bool enterPressed = false;
	bool exitPopup = false;

	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {


		bool justOpened = ImGui::IsWindowAppearing();

		bool clickedOutside =
			!justOpened &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

		bool pressedEscape = ImGui::IsKeyPressed(ImGuiKey_Escape);


		ImGui::SetNextItemWidth(250.0f);

		enterPressed = ImGui::InputText(
			"##NameInput",
			target.nameBuffer,
			sizeof(target.nameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_AutoSelectAll
		);

		if (target.nameBuffer[0] != '\0') {
			target.name = target.nameBuffer;
		}

		std::string newName = target.nameBuffer;

		bool emptyName = newName.empty();

		bool nameExists = std::any_of(
			groups.begin(),
			groups.end(),
			[&](const TypeT& group)
			{return group.name == target.name; }
		);

		if (justOpened) {
			ImGui::SetKeyboardFocusHere(-1);
		}

		if (clickedOutside || pressedEscape) {
			if (canceled) {
				*canceled = true;
			}
			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();

		if (emptyName) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"Name cannot be empty"
			);
		}
		else if (nameExists) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"Name already exists"
			);
		}
		if (enterPressed && !nameExists) {
			exitPopup = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
	return exitPopup;
}

// ======================================================================
// -----------------------DOCKING SPACE CLASS----------------------------
// ======================================================================
class DockingSpace {
public:

	// windowTitle is the ImGui window the dockspace is drawn into; tabBaseName is
	// what new tabs are named after. They are separate because the host is a shared
	// viewport window (see UIViewport) while tabs keep their own readable names.
	DockingSpace(const char* windowTitle, const char* tabBaseName);

	// DockTab::id of the selected tab, 0 if none. Resolve to a slot with a
	// lookup -- this is an id, not an index.
	int getActiveTabID();

	// define structs
	struct DockSpaceInfo {
		ImGuiID dockspaceID = 0;
		ImGuiWindowClass windowClass{};
	};

	struct DockTab {

		// docking variables
		int id = 0;					// unique id per tab
		ImGuiID targetDockID = 0;   // current dock id

		bool newlyCreated = true;
		bool resetView = false;
		bool copyImageNextFrame = false;

		// renaming variables
		std::string name;
		char nameBuffer[128] = {};
	};

	//  render dock space onto screen and return its info
	DockSpaceInfo renderDockSpace();

	// add a new tab
	template <typename TabT>
	void addTab(std::vector<TabT>& tabs, ImGuiID targetDockID) {
		TabT tab;

		tab.id = nextTabID++;
		tab.name = tabBaseName + std::to_string(tab.id);
		tab.newlyCreated = true;
		tab.targetDockID = targetDockID;

		activeTabID = tab.id; // read the id before `tab` is moved from
		tabs.push_back(std::move(tab));
	}

	// draw tabs
	template<typename TabT, typename DrawTabContent>
	void drawTabs(std::vector<TabT>& tabs, ImGuiID dockspaceID, const ImGuiWindowClass& windowClass, DrawTabContent&& drawTabContent) {
		int tabToClose = -1;
		ImGuiID pendingAddDockID = 0;

		for (int i = 0; i < tabs.size(); i++) {
			TabT& tab = tabs[i];

			bool open = true;

			// set window class and dock id. make newly created tabs always docked next the other tabs
			ImGui::SetNextWindowClass(&windowClass);

			if (tabs[i].newlyCreated) {
				ImGui::SetNextWindowDockID(tab.targetDockID, ImGuiCond_Always);
				tabs[i].newlyCreated = false;
			}
			else {
				ImGui::SetNextWindowDockID(dockspaceID, ImGuiCond_FirstUseEver);
			}
			// create windowTitle so when tab.name changes, it keeps the same tab layout format
			std::string windowTitle = tab.name + "###Tab_" + std::to_string(tab.id);
			// draw plot
			if (ImGui::Begin(windowTitle.c_str(), &open)) {
				ImGuiID currentDockID = ImGui::GetWindowDockID();

				ImGui::PushID(tab.id);

				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
					activeTabID = tab.id;
				}

				// check double click for renaming tab
				if (isCurrentDockTabDoubleClicked()) {
					std::snprintf(tab.nameBuffer,
						sizeof(tab.nameBuffer),
						"%s",
						tab.name.c_str());

					ImGui::OpenPopup("Rename Tab");
				}
				drawRenamingPopup("Rename Tab", tab, tabs);

				drawTabContent(
					tab,
					i,
					currentDockID,
					pendingAddDockID,
					dockspaceID
				);

				ImGui::PopID();
			}

			ImGui::End();

			if (!open) {
				tabToClose = i;
			}
		}

		// remove tabs
		if (tabToClose != -1) {
			const int closedID = tabs[tabToClose].id;
			tabs.erase(tabs.begin() + tabToClose);

			// ids survive the erase, so a selection on any other tab still
			// resolves -- only rehome when the selected tab is the one closed
			if (activeTabID == closedID) {
				activeTabID = tabs.empty() ? 0 : tabs.back().id;
			}
		}

		// add tab if plus button was pressed
		if (pendingAddDockID != 0 || tabs.empty()) {
			addTab(tabs, pendingAddDockID != 0 ? pendingAddDockID : dockspaceID);
		}
	}

private:

	std::string windowTitle;
	std::string tabBaseName;
	ImGuiWindowClass hostWindowClass;
	int nextTabID = 1;

	// DockTab::id of the selected tab, 0 = none. This is the tab's stable unique
	// id, NOT its slot in the caller's vector -- ids are 1-based and survive a
	// close, indices don't. Resolve it with a lookup before indexing.
	int activeTabID = 0;

	bool isCurrentDockTabDoubleClicked();

};






















// Everything needed to draw one toolbar strip, split out of BaseSurfaceViewer so
// the residual plot — which has no camera or framebuffer and so can't be a
// surface viewer — draws the same strip from the same code.
class ToolbarHost {
public:

	// Open/close the toolbar strip. beginToolbar pushes the shared toolbar style
	// and opens a full-bleed child band (spans the window edge to edge, ignoring
	// WindowPadding); endToolbar draws the bottom rule and restores everything.
	// Put the sections between them. Always pair these.
	void beginToolbar();
	void endToolbar();

	// A ribbon-style group of related buttons, named by a caption centered beneath
	// them: submit the buttons between beginSection() and endSection(name). The
	// rules between sections are drawn for you, and every name shares one baseline
	// however tall the buttons above it are. Always pair these, inside a toolbar.
	//
	// Buttons stack into rows the ImGui way: SameLine() continues the current row,
	// omitting it starts a new one. A section gets toolbarButtonHeight() of room,
	// which is one captioned button or two rows of smallIconSize() ones.
	void beginSection();
	void endSection(const char* name);

	// add tooltip to image button when hovered
	void setToolTip(const char* text);

	// Toolbar buttons render the icon with a short caption centered underneath so
	// the button reads as the tool it represents. `label` is that caption (keep it
	// short, e.g. "Ruler"), or null for none — which leaves the section name to say
	// what the buttons are. `tooltip` is the fuller hover description.
	bool addImageButton(const char* id, const char* label, const char* tooltip, TextureBuffer& icon, ImVec2 buttonSize = iconSize());

	bool addImageButtonToggle(const char* id, const char* label, const char* tooltip, TextureBuffer& icon, bool& toggle, ImVec2 buttonSize = iconSize());

	// Default square size (px) of a toolbar icon button. Single knob for every
	// inspector/sketch toolbar — bump to enlarge all of them.
	static constexpr float toolbarIconSize = 40.0f;

	// Square size (px) of the compact buttons a two-row section uses, trading each
	// button's caption for a denser grid.
	static constexpr float toolbarSmallIconSize = 24.0f;

	static constexpr ImVec2 iconSize() { return ImVec2(toolbarIconSize, toolbarIconSize); }
	static constexpr ImVec2 smallIconSize() { return ImVec2(toolbarSmallIconSize, toolbarSmallIconSize); }

	// The style beginToolbar() pushes. Named so the heights below are derived from
	// the same numbers rather than duplicating them.
	static constexpr float toolbarPadX = 8.0f;
	static constexpr float toolbarPadY = 4.0f;
	static constexpr float toolbarFramePadX = 6.0f;
	static constexpr float toolbarFramePadY = 2.0f;
	static constexpr float toolbarItemSpacingY = 2.0f;

	// Height (px) of one captioned toolbar button — the icon button plus the
	// caption line under it. Also the height every section's buttons get, since
	// each section name is parked directly below this.
	static constexpr float toolbarButtonHeight() {
		return toolbarIconSize
			+ toolbarFramePadY * 2.0f
			+ toolbarItemSpacingY
			+ captionFontSize;
	}

	// Total height (px) of the strip: the buttons, the section names under them,
	// and the band's padding. Constant and known without pushing any style, so a
	// caller can reserve space for the strip before it is drawn (GUI shrinks the
	// dockspace by exactly this).
	static constexpr float toolbarStripHeight() {
		return toolbarButtonHeight() + sectionNameGap + sectionFontSize + toolbarPadY * 2.0f;
	}

private:

	// draws the icon button plus its centered caption as one vertical group.
	// Assumes the caller has already pushed the button id, frame rounding, and
	// button colors. `active` brightens the caption for a toggled-on tool.
	bool drawImageButtonWithCaption(const char* buttonID, const char* label, const char* tooltip, TextureBuffer& icon, ImVec2 buttonSize, bool active);

	// vertical rule dividing two sections, drawn at the cursor and spanning the
	// band; advances the cursor past it.
	void addSectionRule();

	// Window-space Y of the section names. Fixed rather than "wherever the buttons
	// ended", so names line up across sections whose buttons differ in height.
	// beginToolbar's child starts its cursor at WindowPadding, so every section's
	// buttons start at toolbarPadY.
	static constexpr float sectionNameY() {
		return toolbarPadY + toolbarButtonHeight() + sectionNameGap;
	}

	const float imageButtonRounding = 6.0f;

	// caption font size (px) under toolbar icons; smaller than the 18px UI font
	// so the label stays subordinate to the icon.
	static constexpr float captionFontSize = 13.0f;

	// Section name: smaller again than a button caption, and dimmed, so the group
	// reads as a heading rather than as another tool.
	static constexpr float sectionFontSize = 12.0f;
	static constexpr float sectionNameGap = 3.0f;

	// horizontal room (px) one section rule takes, the line itself centered in it
	static constexpr float sectionRuleWidth = 24.0f;

	// laid out by beginSection/endSection: the open section's left edge, and how
	// many sections the strip has drawn so far (the first one needs no rule).
	float sectionStartX = 0.0f;
	int sectionCount = 0;

	// cursor stashed by beginToolbar so endToolbar can undo the full-bleed shift
	// and park the next widget flush against the band's bottom edge
	float toolbarSavedCursorX = 0.0f;
	float toolbarNextCursorY = 0.0f;
};

// Two rows of compact buttons have to fit the height a section gets, or they would
// spill over its name and out of the strip. Out here rather than inside the class,
// where toolbarButtonHeight() is not defined yet and so can't be called.
static_assert(
	2.0f * (ToolbarHost::toolbarSmallIconSize + ToolbarHost::toolbarFramePadY * 2.0f) + ToolbarHost::toolbarItemSpacingY
		<= ToolbarHost::toolbarButtonHeight(),
	"toolbarSmallIconSize is too large for a two-row section"
);

class BaseSurfaceViewer : public ToolbarHost {
public:

	BaseSurfaceViewer(const char* vertexShaderPath, const char* fragmentShaderPath);

	// public copy variables
	bool pendingCopy = false;
	bool consoleCopy = false;

	// request that this viewer recenter and re-zoom to the project's current
	// length unit on its next render (e.g. after a project is loaded). Deferred
	// to render so it runs once the camera size and length scale are current.
	void requestResetView() { pendingResetView = true; }

protected:

	// window class used for the viewer
	ImGuiWindowClass windowClass;


	// store mouse position of where the user left clicked
	ImVec2 initLeftMouse = ImVec2(0.0f, 0.0f);

	// copy variables
	int pendingCopyWidth = 1600;
	int pendingCopyHeight = 420;

	// simple 2D buffers
	FrameBuffer frameBuffer;	
	FrameBuffer offScreenFBO;
	
	// select data
	const float circleRadius = 3.0f;
	std::vector<SurfacePoint> points;

	// tool toggles
	bool toggleDrawLine = false;
	bool toggleDrawCircle = false;
	bool toggleDrawRect = false;
	bool toggleRuler = false;
	bool toggleGrid = false;

	ImVec2 currentMousePos = ImVec2(0.0f, 0.0f);

	// shader
	Shader shader;

	// popup variables
	bool openPopUp = false;

	// rect variables
	struct Rect {
		ImVec2 min;
		ImVec2 max;
		ImVec2 size;
	};

	Rect canvasRect;
	Camera2D camera;

	// tracks the last project display scale seen by updateLengthScale(), so a
	// change can be detected and the camera zoom snapped accordingly. Also
	// used to label the grid spacing in the project's chosen display unit.
	double lastLengthScale = 1.0;
	const char* currentUnitName = "m";
	bool lengthScaleInitialized = false;

	// set by requestResetView(); consumed by applyPendingResetView() during
	// render to trigger a one-shot resetView().
	bool pendingResetView = false;

	// on-screen spacing (px) that gridWorldStep() aims for when picking a nice
	// 1/2/5 grid step. Shared with the zoom-snap in updateLengthScale() so the
	// grid can be made to read exactly one display unit per cell.
	static constexpr double gridTargetPixelSpacing = 60.0;

	// screen-space bounds of the drawn surface image, updated by drawSurface()
	// and read by hit-testing (e.g. sketch trimming)
	ImVec2 imageMin = ImVec2(0.0f, 0.0f);
	ImVec2 imageMax = ImVec2(0.0f, 0.0f);

	// ======================================================================
	// -----------------------HELPER FUNCTION--------------------------------
	// ======================================================================
	// when the project's display length unit changes, snap the 2D camera to an
	// absolute zoom where one grid cell reads exactly one display unit (1 mm,
	// 1 m, etc). No-op the first time it is called, since there is no previous
	// scale to compare against yet. Also records unitName for labeling the grid.
	void updateLengthScale(double currentScale, const char* unitName);

	// units-per-pixel that makes one grid cell read exactly one display unit at
	// the given display scale (scale = 1/toBase). Returns the current zoom
	// unchanged when scale is invalid.
	double zoomForUnitGrid(double scale) const;

	// reset the view for the "home" / reset-view action: recenter and snap the
	// zoom so one grid cell matches the project's current length unit.
	void resetView();

	// if a reset was requested via requestResetView(), apply it now. Call after
	// the camera dimensions and length scale have been updated for the frame.
	void applyPendingResetView();

	bool isMouseNearImage(ImGuiIO& io);

	// create rect that is padded on all sides
	Rect makePaddedRect(
		const ImVec2& pos,
		const ImVec2& size,
		float left = 0.0,
		float right = 0.0,
		float top = 0.0,
		float bottom = 0.0
	);

	// update initLeftMouse when the user clicks. relative to the last drawn ImGui item
	void updateInitialLeftClick(ImGuiIO& io);

	void updateCurrentMousePos();

	void resizeImage();

	// ======================================================================
	// -----------------------DRAW CALLS-------------------------------------
	// ======================================================================
	// draw the axes in 2d space
	void drawAxes(ImDrawList* drawList);

	// draw grid lines across the visible canvas when toggleGrid is set.
	// spacing adapts with zoom so lines stay a reasonable pixel distance apart.
	void drawGrid(ImDrawList* drawList);

	// world-space spacing between grid lines/vertices at the current zoom
	// (a "nice" 1/2/5 x 10^n step). Shared by drawGrid() and grid snapping.
	double gridWorldStep() const;

	// round a world point to the nearest grid vertex
	Vec2 snapToGridVertex(Vec2 world) const;

	// draws the main surface. also updates imageMin and imageMax
	void drawSurface(const Rect& rect);

	// draws rectangular canvas
	void drawCanvas(
		ImDrawList* drawList,
		const Rect& rect,
		const float rounding,
		ImColor fillColor = IM_COL32(19, 27, 37, 255),
		ImColor outlineColor = IM_COL32(76, 105, 140, 200)
	);

	// ======================================================================
	// -----------------------MENU ITEM HANDLES------------------------------
	// ======================================================================
	void addMenuItemCopyToClipboard(const char* text);

	// Fill + border of a 2D canvas. Shared by the sketch view and the mesh
	// inspector so both read as the same drawing surface; pass them to
	// drawCanvas() (its defaults are the darker 3D/results palette).
	const ImU32 canvasBgColor = IM_COL32(102, 102, 102, 255);
	const ImU32 canvasOutlineColor = IM_COL32(150, 150, 150, 255);

	// Sketch geometry as drawn on a 2D canvas: near-black at rest, highlighter
	// blue when hovered or selected. Shared so the sketch view and the mesh
	// inspector's boundary segments read as the same lines.
	const ImU32 sketchLineColor = IM_COL32(28, 28, 30, 255);
	const ImU32 hoverLineColor = IM_COL32(50, 145, 255, 255);

	// Stroke width (px) for those same lines. Shared for the same reason the
	// colors are: a mesh boundary segment must read as the sketch entity it
	// came from, so the two canvases can't drift apart.
	const float sketchLineThickness = 2.0f;
	const float hoverLineThickness = 3.5f;

};
