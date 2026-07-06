#include "base_surface_viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
void BaseSurfaceViewer::updateLengthScale(double currentScale, const char* unitName) {

	currentUnitName = unitName;

	if (currentScale <= 0.0) {
		return;
	}

	if (!lengthScaleInitialized) {
		lastLengthScale = currentScale;
		lengthScaleInitialized = true;
		return;
	}

	if (currentScale == lastLengthScale) {
		return;
	}

	// snap to an absolute zoom where one grid cell reads exactly one display
	// unit (1 mm, 1 m, etc)
	camera.setZoom(zoomForUnitGrid(currentScale));

	lastLengthScale = currentScale;
}

double BaseSurfaceViewer::zoomForUnitGrid(double scale) const {

	if (!(scale > 0.0)) {
		return camera.unitsPerPixel; // leave the zoom unchanged if scale is invalid
	}

	// gridWorldStep() rounds "gridTargetPixelSpacing worth of world, expressed
	// in display units" to a 1/2/5 x 10^n step; any value in [1, 2) rounds to 1.
	// Aim for 1.25 so the result sits safely inside that band regardless of
	// float rounding, giving a ~48 px cell of exactly one unit.
	//
	// scale = 1/toBase, so one display unit is 1/scale in world meters.
	const double targetDisplayStep = 1.25;
	double worldPerUnit = 1.0 / scale;

	return worldPerUnit * targetDisplayStep / gridTargetPixelSpacing;
}

void BaseSurfaceViewer::resetView() {

	// recenter, then match the zoom to the project's current display unit so one
	// grid cell reads exactly one unit
	camera.home();
	camera.setZoom(zoomForUnitGrid(lastLengthScale));
}

void BaseSurfaceViewer::applyPendingResetView() {
	if (pendingResetView) {
		resetView();
		pendingResetView = false;
	}
}

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

double BaseSurfaceViewer::gridWorldStep() const {

	double targetWorldStep = camera.unitsPerPixel * gridTargetPixelSpacing;

	if (!(targetWorldStep > 0.0) || !std::isfinite(targetWorldStep)) {
		return 0.0;
	}

	// pick the "nice" 1/2/5 x 10^n step in the CURRENT DISPLAY UNIT (not raw
	// world meters), so the grid reads as round numbers in whatever unit is
	// currently selected (e.g. 1/2/5 mm when on mm) instead of always
	// rounding in meters and just relabeling.
	double targetDisplayStep = targetWorldStep * lastLengthScale;

	double magnitude = std::pow(10.0, std::floor(std::log10(targetDisplayStep)));
	double residual = targetDisplayStep / magnitude;

	double niceFactor = (residual < 2.0) ? 1.0 : (residual < 5.0) ? 2.0 : 5.0;

	double niceDisplayStep = niceFactor * magnitude;

	return niceDisplayStep / lastLengthScale; // back to world meters for drawing
}

Vec2 BaseSurfaceViewer::snapToGridVertex(Vec2 world) const {

	double step = gridWorldStep();

	if (!(step > 0.0)) {
		return world;
	}

	return Vec2{
		std::round(world.z / step) * step,
		std::round(world.r / step) * step
	};
}

void BaseSurfaceViewer::drawGrid(ImDrawList* drawList) {

	if (!toggleGrid) {
		return;
	}

	double step = gridWorldStep();
	if (!(step > 0.0)) {
		return;
	}

	ImVec2 canvasMin = canvasRect.min;
	ImVec2 canvasMax = canvasRect.max;

	Vec2 corner0 = camera.screenToWorld(canvasMin);
	Vec2 corner1 = camera.screenToWorld(canvasMax);

	double zMin = std::min(corner0.z, corner1.z);
	double zMax = std::max(corner0.z, corner1.z);
	double rMin = std::min(corner0.r, corner1.r);
	double rMax = std::max(corner0.r, corner1.r);

	const int maxLines = 500; // safety cap
	const ImU32 gridColor = IM_COL32(255, 255, 255, 35);

	drawList->PushClipRect(canvasMin, canvasMax, true);

	double zStart = std::floor(zMin / step) * step;
	int nZ = (int)((zMax - zStart) / step) + 1;
	nZ = std::min(nZ, maxLines);
	for (int i = 0; i <= nZ; i++) {
		double z = zStart + i * step;
		float x = camera.worldToScreen(Vec2{ z, rMin }).x;
		drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), gridColor, 1.0f);
	}

	double rStart = std::floor(rMin / step) * step;
	int nR = (int)((rMax - rStart) / step) + 1;
	nR = std::min(nR, maxLines);
	for (int i = 0; i <= nR; i++) {
		double r = rStart + i * step;
		float y = camera.worldToScreen(Vec2{ zMin, r }).y;
		drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), gridColor, 1.0f);
	}

	drawList->PopClipRect();

	// label the grid spacing in the project's current display unit, so the
	// user knows how much world-space each grid box represents
	char label[64];
	std::snprintf(
		label,
		sizeof(label),
		"grid: %.4g %s",
		step * lastLengthScale,
		currentUnitName
	);

	drawList->AddText(
		ImVec2(canvasMin.x + 8.0f, canvasMax.y - 20.0f),
		IM_COL32(210, 215, 225, 200),
		label
	);
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
