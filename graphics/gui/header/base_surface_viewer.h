#pragma once
#include "imgui.h"

#include <optional>
#include <unordered_set>
#include <algorithm>

#include "graphics_struct.h"
#include "shader.h"
#include "printer.h"

// ======================================================================
// -----------------------PUBLIC HELPER FUNCTIONS------------------------
// ======================================================================

template<typename TypeT>
bool drawNamingPopup(const char* label, TypeT& target, std::vector<TypeT>& groups) {

	bool enterPressed = false;
	bool exitPopup = false;

	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {


		std::string renameError = "Name already exists";

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

		bool nameExists = std::any_of(groups.begin(), groups.end(), [&](const TypeT& group) {return group.name == target.name; });


		if (justOpened) {
			ImGui::SetKeyboardFocusHere(-1);
		}

		if (clickedOutside || pressedEscape) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();

		if (nameExists) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
				"%s",
				renameError.c_str()
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

	DockingSpace(const char* name);

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
		tab.name = dockName + std::to_string(tab.id);
		tab.newlyCreated = true;
		tab.targetDockID = targetDockID;

		tabs.push_back(std::move(tab));
		activeTabID = (int)tabs.size() - 1;
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
					activeTabID = i;
				}

				// check double click for renaming tab
				if (isCurrentDockTabDoubleClicked()) {
					std::snprintf(tab.nameBuffer,
						sizeof(tab.nameBuffer),
						"%s",
						tab.name.c_str());

					ImGui::OpenPopup("Rename Tab");
				}
				drawNamingPopup("Rename Tab", tab, tabs);

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
			tabs.erase(tabs.begin() + tabToClose);
			activeTabID = (int)tabs.size() - 1; // move to the right tab after closing a tab
		}

		// add tab if plus button was pressed
		if (pendingAddDockID != 0 || tabs.empty()) {
			addTab(tabs, pendingAddDockID != 0 ? pendingAddDockID : dockspaceID);
		}
	}

private:

	std::string dockName;
	int nextTabID = 1;
	int activeTabID = 1;

	bool isCurrentDockTabDoubleClicked();

};
















class BaseSurfaceViewer {
public:

	BaseSurfaceViewer(const char* vertexShaderPath, const char* fragmentShaderPath);

	// public copy variables
	bool pendingCopy = false;
	bool consoleCopy = false;

protected:

	// copy variables
	int pendingCopyWidth = 1600;
	int pendingCopyHeight = 420;

	// simple 2D buffers
	FrameBuffer frameBuffer;	
	FrameBuffer offScreenFBO;
	
	// select data
	const float circleRadius = 3.0f;
	std::vector<SurfacePoint> points;

	// rectangle selection
	bool toggleSelect = false;
	bool isRectReady = false;

	ImVec2 initMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMousePos = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos1 = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos2 = ImVec2(0.0f, 0.0f);

	// shader
	Shader shader;

	// popup variables
	bool openPopUp = false;

	// image dimensions
	int imageWidth, imageHeight;
	ImVec2 zoomCenter = ImVec2(0.5f, 0.5f);
	float zoom = 1.0f;

	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;

	// ======================================================================
	// -----------------------HELPER FUNCTION--------------------------------
	// ======================================================================
	void setImagePadding(ImVec2& padding);

	std::optional<ImVec2> mouseToGridPoint(int nrBase, int nzBase);

	void updateUV(float halfW, float halfH);

	void clampZoomCenter(float& halfW, float& halfH);

	void toggleSelectedPoint(std::vector<SurfacePoint>& points, ImVec2& dataPos, ImVec2& mousePos, float value);

	void resetView();

	void addHighlightCell(std::unordered_set<int>& indices, int n);

	void highlightCellsInRect(std::unordered_set<int>& indices, ImVec2 cellA, ImVec2 cellB, int nzBase, int nrBase);

	// get physical z and r coordinates at mouse position
	void getMousePhysicalCoord(ImVec2& mousePos, const std::vector<double>& rFace, const std::vector<double> zFace, double& r, double& z);

	ImVec2 screenToUV(const ImVec2& mousePos);

	ImVec2 uvToScreen(const ImVec2& uv);

	ImVec2 gridFaceToScreen(int jFace, int iFace, const std::vector<double>& zFace, const std::vector<double>& rFace);

	ImVec2 getMouseIndex(const std::vector<double>& rFace, const std::vector<double> zFace);

	// turns i,j coordinates to pixel coordinates
	ImVec2 gridToScreen(int jFace, int iFace, const std::vector<double>& rFace, const std::vector<double>& zFace);

	// add tooltip to image button when hovered
	void setToolTip(const char* text);

	// ======================================================================
	// -----------------------HANDLE INPUT-----------------------------------
	// ======================================================================
	void handleZoom(ImGuiIO& io);

	void handlePan(ImGuiIO& io);

	void handleRectSelection(ImGuiIO& io);

	void handlePopup(const char* text);

	void resizeImage(int padx, int pady);

	// ======================================================================
	// -----------------------DRAW CALLS-------------------------------------
	// ======================================================================
	// draw rectangle when mouse is dragged
	void displayRect(int nrBase, int nzBase);

	void drawHighlightedCells(std::unordered_set<int>& indices, const std::vector<double>& rFace, const std::vector<double>& zFace);



	// ======================================================================
	// -----------------------MENU ITEM HANDLES------------------------------
	// ======================================================================
	// add menu items
	void addMenuItemResetView(const char* text);

	void addMenuItemClearPoints(const char* text);

	void addMenuItemCopyToClipboard(const char* text);

	void addMenuItemToggleBool(const char* text, bool& toggle);

	// add image buttons
	void addImageButtonResetView(TextureBuffer& icon, ImVec2 buttonSize);

	void addImageButtonNewTab(TextureBuffer& icon, ImVec2 buttonSize, ImGuiID currentDockID, ImGuiID& pendingAddDockID, ImGuiID dockspaceID);

	void addImageButtonToggleBool(const char* id, TextureBuffer& icon, ImVec2 buttonSize, bool& toggle);

	void addImageButtonCopyToClipboard(const char* id, TextureBuffer& icon, ImVec2 buttonSize);

	template<typename T>
	void addImageButtonClearVector(const char* id, TextureBuffer& icon, ImVec2 buttonSize, std::vector<T>& vec) {
		if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
			vec.clear();
		}
	}

	template<typename... Sets>
	void addImageButtonClearSet(const char* id, TextureBuffer& icon, ImVec2 buttonSize, Sets&... sets) {
		if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
			(sets.clear(), ...);
		}
	}

	template <typename... Funcs>
	void addImageButtonRunCustomFuncs(const char* id, TextureBuffer& icon, ImVec2 buttonSize, Funcs&&... funcs) {
		if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
			(funcs(), ...);
		}
	}
};
