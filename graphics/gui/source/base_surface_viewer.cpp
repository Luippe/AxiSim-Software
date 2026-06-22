#include "base_surface_viewer.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include "imgui_internal.h"

DockingSpace::DockingSpace(const char* name) : dockName(name) {


}

int DockingSpace::getActiveTabID() {
	return activeTabID;
}

DockingSpace::DockSpaceInfo DockingSpace::renderDockSpace() {

	ImGui::Begin(dockName.c_str());

	ImGuiID dockspaceID = ImGui::GetID("ResidualPlotDockSpace");
	ImGuiID classID = ImGui::GetID("ResidualPlotDockClass");

	ImGuiWindowClass windowClass{};
	windowClass.ClassId = classID;
	windowClass.DockingAlwaysTabBar = true;
	windowClass.DockingAllowUnclassed = false;

	ImGui::DockSpace(
		dockspaceID,
		ImGui::GetContentRegionAvail(),
		0,
		&windowClass
	);

	ImGui::End();

	return DockSpaceInfo{ dockspaceID, windowClass };
}

bool DockingSpace::isCurrentDockTabDoubleClicked() {
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();

	if (!window) return false;

	// case 1: docked window tab
	if (window->DockNode && window->DockNode->TabBar) {
		ImGuiTabBar* tabBar = window->DockNode->TabBar;

		for (int i = 0; i < tabBar->Tabs.Size; i++) {
			ImGuiTabItem* tab = &tabBar->Tabs[i];

			if (tab->ID == window->TabId) {

				ImRect tabRect;
				tabRect.Min.x = tabBar->BarRect.Min.x + tab->Offset;
				tabRect.Min.y = tabBar->BarRect.Min.y;
				tabRect.Max.x = tabRect.Min.x + tab->Width;
				tabRect.Max.y = tabBar->BarRect.Max.y;

				bool hovered = ImGui::IsMouseHoveringRect(tabRect.Min, tabRect.Max, false);

				return hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			}
		}
	}

	// case 2: undocked window
	ImRect titleBarRect = window->TitleBarRect();

	bool hoveredTitleBar = ImGui::IsMouseHoveringRect(
		titleBarRect.Min,
		titleBarRect.Max,
		true
	);

	return hoveredTitleBar && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
}

BaseSurfaceViewer::BaseSurfaceViewer(const char* vertexPath, const char* fragmentPath) :
	shader(vertexPath, fragmentPath) {



}

// ======================================================================
// -----------------------HELPER FUNCTION--------------------------------
// ======================================================================

bool BaseSurfaceViewer::isMouseNearImage(ImGuiIO& io) {

	ImVec2 imageMin = canvasRect.min;
	ImVec2 imageMax = canvasRect.max;

	float clickPadding = 10.0f;

	ImVec2 hitMin = ImVec2(imageMin.x - clickPadding, imageMin.y - clickPadding);
	ImVec2 hitMax = ImVec2(imageMax.x + clickPadding, imageMax.y + clickPadding);

	ImVec2 mouse = ImGui::GetMousePos();

	bool mouseNearImage =
		mouse.x >= hitMin.x && mouse.x <= hitMax.x &&
		mouse.y >= hitMin.y && mouse.y <= hitMax.y;

	return mouseNearImage;
}

BaseSurfaceViewer::Rect BaseSurfaceViewer::makePaddedRect(
	const ImVec2& pos,
	const ImVec2& size,
	float left,
	float right,
	float top,
	float bottom
) {
	return {
		ImVec2(pos.x + left,           pos.y + top),
		ImVec2(pos.x + size.x - right, pos.y + size.y - bottom),
		ImVec2(size.x - right - left, size.y - bottom - top)
	};
}

void BaseSurfaceViewer::updateInitialLeftClick(ImGuiIO& io) {

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		initLeftMouse = io.MousePos;
	}
}

void BaseSurfaceViewer::resizeImage() {

	ImVec2 size = canvasRect.size;

	int imageWidth = (int)size.x;
	int imageHeight = (int)size.y;

	if (imageWidth != frameBuffer.width || imageHeight != frameBuffer.height) {
		frameBuffer.create2DBuffer(imageWidth, imageHeight, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	}
}

void BaseSurfaceViewer::updateCurrentMousePos() {
	currentMousePos = ImGui::GetMousePos();
}

void BaseSurfaceViewer::setToolTip(const char* text) {
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", text);
	}
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void BaseSurfaceViewer::drawCanvas(
	ImDrawList* drawList,
	const Rect& rect,
	const float rounding,
	ImColor fillColor,
	ImColor outlineColor
) {

	drawList->AddRectFilled(
		rect.min,
		rect.max,
		fillColor,
		rounding
	);

	drawList->AddRect(
		rect.min,
		rect.max,
		outlineColor,
		rounding,
		0,
		1.5f
	);

}
void BaseSurfaceViewer::addToolbarSeparator(float height) {
	ImGui::SameLine();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 pos = ImGui::GetCursorScreenPos();

	float y0 = pos.y + 4.0f;
	float y1 = y0 + height;

	drawList->AddLine(
		ImVec2(pos.x + 6.0f, y0),
		ImVec2(pos.x + 6.0f, y1),
		IM_COL32(120, 120, 120, 180),
		2.0f
	);

	ImGui::Dummy(ImVec2(12.0f, height));
	ImGui::SameLine();
}

void BaseSurfaceViewer::drawSurface(const Rect& rect) {
	ImGui::SetCursorScreenPos(rect.min);
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), rect.size, ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));
	imageMin = ImGui::GetItemRectMin();
	imageMax = ImGui::GetItemRectMax();
	imageSize = {
		imageMax.x - imageMin.x,
		imageMax.y - imageMin.y
	};
}

void BaseSurfaceViewer::drawAxes(ImDrawList* drawList) {
	ImVec2 origin = camera.worldToScreen(Vec2{ 0.0, 0.0 });

	ImVec2 canvasMin = canvasRect.min;
	ImVec2 canvasMax = canvasRect.max;

	if (origin.y >= canvasMin.y && origin.y <= canvasMax.y) {
		drawList->AddLine(
			ImVec2(canvasMin.x, origin.y),
			ImVec2(canvasMax.x, origin.y),
			IM_COL32(210, 55, 55, 255),
			1.5f
		);

		drawList->AddText(
			ImVec2(canvasMax.x - 18.0f, origin.y + 6.0f),
			IM_COL32(230, 80, 80, 255),
			"z"
		);
	}

	if (origin.x >= canvasMin.x && origin.x <= canvasMax.x) {
		drawList->AddLine(
			ImVec2(origin.x, canvasMin.y),
			ImVec2(origin.x, canvasMax.y),
			IM_COL32(55, 190, 95, 255),
			1.5f
		);

		drawList->AddText(
			ImVec2(origin.x + 6.0f, canvasMin.y + 6.0f),
			IM_COL32(80, 220, 120, 255),
			"r"
		);
	}

	drawList->AddCircleFilled(origin, 3.5f, IM_COL32(235, 235, 235, 255));

}

// ======================================================================
// -----------------------MENU ITEM HANDLES------------------------------
// ======================================================================
void BaseSurfaceViewer::addMenuItemCopyToClipboard(const char* text) {
	if (ImGui::MenuItem(text)) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
}

// ======================================================================
// -----------------------IMAGE BUTTON HANDLES---------------------------
// ======================================================================
bool BaseSurfaceViewer::addImageButton(const char* id, const char* tooltip, TextureBuffer& icon, ImVec2 buttonSize) {
	ImGui::PushID(id);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, imageButtonRounding);
	bool clicked = ImGui::ImageButton("##addImageButton", (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize);

	setToolTip(tooltip);

	ImGui::PopID();
	ImGui::PopStyleVar();
	return clicked;
}

bool BaseSurfaceViewer::addImageButtonToggle(const char* id, const char* tooltip, TextureBuffer& icon, ImVec2 buttonSize, bool& toggle) {

	ImGui::PushID(id);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, imageButtonRounding);
	bool pushed = false;

	// highlight button when toggle is on
	bool pushedStyle = toggle;

	if (pushedStyle) {
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 140, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 160, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 120, 235, 255));
	}

	if (ImGui::ImageButton("##toggleDrawCell", (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
		toggle = !toggle;
		pushed = true;
	}

	if (pushedStyle) {
		ImGui::PopStyleColor(3);
	}

	setToolTip(tooltip);

	ImGui::PopID();
	ImGui::PopStyleVar();

	return pushed;
}
