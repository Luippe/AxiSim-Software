#include "base_surface_viewer.h"

#include <algorithm>
#include <glm/glm.hpp>

#include "printer.h"

BaseSurfaceViewer::BaseSurfaceViewer(const char* vertexPath, const char* fragmentPath) :
	shader(vertexPath, fragmentPath) {
}



// ======================================================================
// -----------------------HELPER FUNCTION--------------------------------
// ======================================================================
int findCellIndex(const std::vector<double>& face, double x) {
	int nCells = static_cast<int>(face.size()) - 1;

	if (x <= face.front()) return 0;
	if (x >= face.back())  return nCells - 1;

	auto it = std::upper_bound(face.begin(), face.end(), x);

	int idx = static_cast<int>(it - face.begin()) - 1;
	return std::clamp(idx, 0, nCells - 1);
}

void BaseSurfaceViewer::updateUV(float halfW, float halfH) {
	u0 = glm::clamp(zoomCenter.x - halfW, 0.0f, 1.0f);
	u1 = glm::clamp(zoomCenter.x + halfW, 0.0f, 1.0f);
	v0 = glm::clamp(zoomCenter.y - halfH, 0.0f, 1.0f);
	v1 = glm::clamp(zoomCenter.y + halfH, 0.0f, 1.0f);
}

void BaseSurfaceViewer::clampZoomCenter(float& halfW, float& halfH) {
	zoomCenter.x = glm::clamp(zoomCenter.x, halfW, 1.0f - halfW);
	zoomCenter.y = glm::clamp(zoomCenter.y, halfH, 1.0f - halfH);
}

void BaseSurfaceViewer::toggleSelectedPoint(std::vector<SurfacePoint>& points, ImVec2& dataPos, ImVec2& vecValue, float value) {
	auto it = std::find_if(points.begin(), points.end(),
		[&](const SurfacePoint& p) {
			return (p.dataPos.x == dataPos.x && p.dataPos.y == dataPos.y);
		});

	if (it != points.end()) {
		points.erase(it);
	}
	else {
		points.push_back({ dataPos, vecValue,  value });
	}
}


ImVec2 BaseSurfaceViewer::gridToScreen(int jFace, int iFace, const std::vector<double>& rFace, const std::vector<double> zFace) {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	int nzBase = static_cast<int>(zFace.size()) - 1;
	int nrBase = static_cast<int>(rFace.size()) - 1;

	jFace = std::clamp(jFace, 0, nzBase);
	iFace = std::clamp(iFace, 0, nrBase);

	double z = zFace[jFace];
	double r = rFace[iFace];

	double L = zFace.back();
	double R = rFace.back();

	float texU = static_cast<float>(z / L);
	float texV = static_cast<float>(r / R);

	float localU = (texU - u0) / (u1 - u0);
	float localV = (texV - v0) / (v1 - v0);

	float x = itemMin.x + localU * imageWidth;
	float y = itemMax.y - localV * imageHeight;

	return ImVec2(x, y);
}

void BaseSurfaceViewer::getMousePhysicalCoord(ImVec2& mousePos, const std::vector<double>& rFace, const std::vector<double> zFace, double& r, double& z) {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	// Mouse position relative to image
	float localX = mousePos.x - itemMin.x;
	float localY = itemMax.y - mousePos.y; // bottom-up

	float localU = localX / imageWidth;
	float localV = localY / imageHeight;

	localU = glm::clamp(localU, 0.0f, 1.0f);
	localV = glm::clamp(localV, 0.0f, 1.0f);

	// Current zoomed/panned normalized texture coordinates
	float texU = u0 + localU * (u1 - u0);
	float texV = v0 + localV * (v1 - v0);

	texU = glm::clamp(texU, 0.0f, 1.0f);
	texV = glm::clamp(texV, 0.0f, 1.0f);

	// Convert normalized texture coordinate to physical coordinate
	double L = zFace.back();
	double R = rFace.back();

	z = texU * L;
	r = texV * R;
}

ImVec2 BaseSurfaceViewer::getMouseIndex(const std::vector<double>& rFace, const std::vector<double> zFace) {

	double z;
	double r;

	// get closest index to current mouse position
	ImVec2 mousePos = ImGui::GetMousePos();
	getMousePhysicalCoord(mousePos, rFace, zFace, r, z);

	// Find cell containing physical coordinate
	int j = findCellIndex(zFace, z);
	int i = findCellIndex(rFace, r);

	return ImVec2(j, i);
}

ImVec2 BaseSurfaceViewer::uvToScreen(const ImVec2& uv) {
	ImVec2 imageMin = ImGui::GetItemRectMin();

	float sx = (uv.x - u0) / (u1 - u0);

	float sy = (v1 - uv.y) / (v1 - v0);

	return ImVec2(
		imageMin.x + sx * imageWidth,
		imageMin.y + sy * imageHeight
	);
}

ImVec2 BaseSurfaceViewer::screenToUV(const ImVec2& mousePos) {
	ImVec2 imageMin = ImGui::GetItemRectMin();

	float sx = (mousePos.x - imageMin.x) / imageWidth;
	float sy = (mousePos.y - imageMin.y) / imageHeight;

	float u = u0 + sx * (u1 - u0);

	float v = v1 - sy * (v1 - v0);

	return ImVec2(u, v);
}

void BaseSurfaceViewer::resizeImage(int padx, int pady) {

	ImVec2 avail = ImGui::GetContentRegionAvail();
	imageWidth = (int)avail.x - padx;
	imageHeight = (int)avail.y - pady;

	if (imageWidth != frameBuffer.width || imageHeight != frameBuffer.height) {
		frameBuffer.create2DBuffer(imageWidth, imageHeight, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	}
}

void BaseSurfaceViewer::addHighlightCell(std::vector<int>& indices, int n) {
	auto it = std::find(indices.begin(), indices.end(), n);

	if (it == indices.end()) {
		indices.push_back(n);
	}
}

void BaseSurfaceViewer::highlightCellsInRect(std::vector<int>& indices, ImVec2 cellA, ImVec2 cellB, int nzBase, int nrBase) {
	int j0 = static_cast<int>(cellA.x);
	int i0 = static_cast<int>(cellA.y);

	int j1 = static_cast<int>(cellB.x);
	int i1 = static_cast<int>(cellB.y);

	int jMin = std::min(j0, j1);
	int jMax = std::max(j0, j1);

	int iMin = std::min(i0, i1);
	int iMax = std::max(i0, i1);

	jMin = std::clamp(jMin, 0, nzBase - 1);
	jMax = std::clamp(jMax, 0, nzBase - 1);

	iMin = std::clamp(iMin, 0, nrBase - 1);
	iMax = std::clamp(iMax, 0, nrBase - 1);

	for (int i = iMin; i <= iMax; i++) {
		for (int j = jMin; j <= jMax; j++) {
			int n = i * nzBase + j;
			addHighlightCell(indices, n);
		}
	}
}

ImVec2 BaseSurfaceViewer::gridFaceToScreen(int jFace, int iFace, const std::vector<double>& zFace, const std::vector<double>& rFace) {
	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	int nzBase = static_cast<int>(zFace.size()) - 1;
	int nrBase = static_cast<int>(rFace.size()) - 1;

	jFace = std::clamp(jFace, 0, nzBase);
	iFace = std::clamp(iFace, 0, nrBase);

	double z = zFace[jFace];
	double r = rFace[iFace];

	double L = zFace.back();
	double R = rFace.back();

	float texU = static_cast<float>(z / L);
	float texV = static_cast<float>(r / R);

	float localU = (texU - u0) / (u1 - u0);
	float localV = (texV - v0) / (v1 - v0);

	float x = itemMin.x + localU * imageWidth;
	float y = itemMax.y - localV * imageHeight;

	return ImVec2(x, y);
}

// ======================================================================
// -----------------------HANDLE INPUT-----------------------------------
// ======================================================================
void BaseSurfaceViewer::handlePan(ImGuiIO& io) {

	ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
	float dx = drag.x;
	float dy = drag.y;

	float distance = sqrtf(dx * dx + dy * dy);
	if (distance > 1.0) {
		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
	}

	ImVec2 delta = io.MouseDelta;

	float viewW = 1.0f / zoom;
	float viewH = 1.0f / zoom;

	zoomCenter.x -= (delta.x / imageWidth) * viewW;
	zoomCenter.y += (delta.y / imageHeight) * viewH;

	float halfW = viewW * 0.5f;
	float halfH = viewH * 0.5f;

	clampZoomCenter(halfW, halfH);
	updateUV(halfW, halfH);
	
}

void BaseSurfaceViewer::handleZoom(ImGuiIO& io) {

	if (io.MouseWheel == 0.0f) return;

	zoom *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
	zoom = glm::clamp(zoom, 1.0f, 20.0f);

	float halfW = 0.5f / zoom;
	float halfH = 0.5f / zoom;

	clampZoomCenter(halfW, halfH);

	updateUV(halfW, halfH);
}

void BaseSurfaceViewer::handleRectSelection(ImGuiIO& io) {
	rectPos2 = screenToUV(currentMousePos);
}

void BaseSurfaceViewer::handlePopup() {
	openPopUp = true;
	ImGui::OpenPopup("MeshInspector Popup");
}

void BaseSurfaceViewer::resetView() {

	zoom = 1.0f;
	zoomCenter = ImVec2(0.5f, 0.5f);
	updateUV(0.5, 0.5);

}

void BaseSurfaceViewer::setToolTip(const char* text) {
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", text);
	}
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void BaseSurfaceViewer::displayRect(int nrBase, int nzBase) {

	if (!isRectReady) return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ImVec2 p1 = uvToScreen(rectPos1);
	ImVec2 p2 = uvToScreen(rectPos2);

	//drawList->AddRect(ImVec2(rectPos1.x/zoom,rectPos1.y/zoom), ImVec2(rectPos2.x, rectPos2.y), IM_COL32(255, 255, 255, 255));
	drawList->AddRect(p1, p2, IM_COL32(255, 255, 255, 255));
}

void BaseSurfaceViewer::drawHighlightedCells(std::vector<int>& indices, const std::vector<double>& zFace, const std::vector<double>& rFace) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ImVec2 imageMin = ImGui::GetItemRectMin();
	ImVec2 imageMax = ImGui::GetItemRectMax();

	int nzBase = static_cast<int>(zFace.size()) - 1;
	int nrBase = static_cast<int>(rFace.size()) - 1;

	// Prevent highlights from drawing outside the image
	drawList->PushClipRect(imageMin, imageMax, true);
	//printInt(highlightedCells.size());
	for (int n : indices) {

		int i = n / nzBase;
		int j = n % nzBase;

		if (j < 0 || j >= nzBase) continue;
		if (i < 0 || i >= nrBase) continue;

		ImVec2 p0 = gridFaceToScreen(j, i, zFace, rFace);
		ImVec2 p1 = gridFaceToScreen(j + 1, i + 1, zFace, rFace);

		ImVec2 rectMin(
			std::min(p0.x, p1.x),
			std::min(p0.y, p1.y)
		);

		ImVec2 rectMax(
			std::max(p0.x, p1.x),
			std::max(p0.y, p1.y)
		);

		drawList->AddRectFilled(
			rectMin,
			rectMax,
			IM_COL32(255, 255, 0, 70)
		);

		drawList->AddRect(
			rectMin,
			rectMax,
			IM_COL32(255, 255, 0, 180),
			0.0f,
			0,
			1.0f
		);
	}

	drawList->PopClipRect();
}

// ======================================================================
// -----------------------MENU ITEM HANDLES------------------------------
// ======================================================================
void BaseSurfaceViewer::addMenuItemResetView(const char* text) {
	if (ImGui::MenuItem(text)) {
		resetView();
	}
}

void BaseSurfaceViewer::addMenuItemClearPoints(const char* text) {
	if (ImGui::MenuItem(text)) {
		points.clear();
	}
}

void BaseSurfaceViewer::addMenuItemCopyToClipboard(const char* text) {
	if (ImGui::MenuItem(text)) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
}

void BaseSurfaceViewer::addMenuItemToggleBool(const char* text, bool& toggle) {
	if (ImGui::MenuItem(text)) {
		toggle = !toggle;
	}
}

// ======================================================================
// -----------------------IMAGE BUTTON HANDLES---------------------------
// ======================================================================
void BaseSurfaceViewer::addImageButtonResetView(TextureBuffer& icon, ImVec2 buttonSize) {
	if (ImGui::ImageButton("##ResetView", (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
		resetView();
	}
}

void BaseSurfaceViewer::addImageButtonToggleBool(TextureBuffer& icon, ImVec2 buttonSize, bool& toggle) {

	// highlight button when toggle is on
	bool pushedStyle = toggle;

	if (pushedStyle) {
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 140, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 160, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 120, 235, 255));
	}

	if (ImGui::ImageButton("##ToggleSelect", (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize)) {
		toggle = !toggle;
	}

	if (pushedStyle) {
		ImGui::PopStyleColor(3);
	}
}

void BaseSurfaceViewer::addImageButtonCopyToClipboard(TextureBuffer& icon, ImVec2 buttonSize) {
	if (ImGui::ImageButton("##CopyToClipboard", (ImTextureID)(intptr_t)icon.getTextureID(), buttonSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
}