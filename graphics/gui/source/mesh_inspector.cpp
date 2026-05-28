#include "mesh_inspector.h"

#include <format>
#include <glm/glm.hpp>

#include "scene_view.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"

MeshInspector::MeshInspector(Mesh& mesh, AppAssets& assets) :
	mesh(mesh),
	g(mesh.g),
	assets(assets),
	BaseSurfaceViewer("graphics/shaders/mesh.vert", "graphics/shaders/mesh.frag") {

	// radial location
	frameBuffer.create2DBuffer(500, 500, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	createGridBuffer();
}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void MeshInspector::setBaseNrNz() {
	nrBase = g.nr;
	nzBase = g.nz;
}

void MeshInspector::createGridBuffer() {

	vertexBuffer.createBuffer(mesh.gridLineVertices.size() * sizeof(float), mesh.gridLineVertices.data());
	vertexBuffer.bind();
	vertexBuffer.enableAttribute(0, 2, GL_FLOAT, 2 * sizeof(float), (void*)0);
	vertexBuffer.unbind();

}

// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void MeshInspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();

	// constantly update current mouse position and mouse index (cell centered, i think)
	currentMouseIndex = getMouseIndex(g.rFace,g.zFace);
	currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && toggleSelect) {

		addHighlightCell(g.obstacleIndices, (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x);
		initMouseIndex = currentMouseIndex;

	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && toggleSelect) {
		addHighlightCell(g.obstacleIndices, (int)currentMouseIndex.y * nzBase + (int)currentMouseIndex.x);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && toggleSelect && ImGui::IsKeyPressed(ImGuiKey_LeftCtrl)) {
		highlightCellsInRect(g.obstacleIndices, initMouseIndex, currentMouseIndex, nzBase, nrBase);
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		handlePopup();
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !toggleSelect) {
		handlePan(io);
	}

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {
		handleZoom(io);
	}

	// handle mouse hovering.
	if (toggleSelect) return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(currentMousePos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
	drawList->AddCircle(currentMousePos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		double zPos, rPos;
		getMousePhysicalCoord(currentMousePos, g.rFace, g.zFace, rPos, zPos);
		ImVec2 vecValue = ImVec2((float)zPos, (float)rPos);

		toggleSelectedPoint(points, currentMouseIndex, vecValue, 0.0f);	// value is 0.0f for now
	}
}

void MeshInspector::copyActiveSurfaceToClipboard() {

	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	// build imgui draw commands
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));
	drawHighlightedCells(g.obstacleIndices, g.zFace, g.rFace);

	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void MeshInspector::drawToolBar() {
	float toolbarHeight = 32.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	addImageButtonResetView(assets.houseIcon, ImVec2(22.0f, 22.0f));
	setToolTip("Reset view");
	ImGui::SameLine();

	addImageButtonClearVector("##ClearPoints", assets.clearIcon, ImVec2(22.0f, 22.0f), points);
	setToolTip("Clear all selected points");
	ImGui::SameLine();

	addImageButtonClearVector("##ClearObstacle", assets.clearIcon, ImVec2(22.0f, 22.0f), g.obstacleIndices);
	setToolTip("Clear all selected cells");
	ImGui::SameLine();

	addImageButtonToggleBool(assets.selectRegionIcon, ImVec2(22.0f, 22.0f), toggleSelect);
	setToolTip("Select");
	ImGui::SameLine();

	addImageButtonCopyToClipboard(assets.copyIcon, ImVec2(22.0f, 22.0f));
	setToolTip("Copy to clipboard");
	ImGui::SameLine();

	ImGui::EndChild();
	ImGui::Separator();
}

void MeshInspector::drawTextAtSurfacePoint() {

	ImVec2 imageMin = ImGui::GetItemRectMin();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const SurfacePoint& point : points) {

		// convert grid index to normalized texture coordinate
		int j = static_cast<int>(point.dataPos.x);
		int i = static_cast<int>(point.dataPos.y);

		float u = static_cast<float>(g.zFace[j] / g.L);
		float v = static_cast<float>(g.rFace[i] / g.R);

		// skip points outside the current zoomed/panned view
		if (u < u0 || u > u1 || v < v0 || v > v1) {
			continue;
		}

		// convert normalized texture coordinate to image-local position
		float sx = (u - u0) / (u1 - u0);

		// use this if your image is drawn with ImVec2(u0, v1), ImVec2(u1, v0)
		float sy = (v1 - v) / (v1 - v0);

		ImVec2 screenPos(
			imageMin.x + sx * imageWidth,
			imageMin.y + sy * imageHeight
		);

		std::string label = std::format(
			"z: {:.2f}\nr: {:.2f}",
			point.vecValue.x,
			point.vecValue.y
		);

		drawList->AddCircleFilled(screenPos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
		drawList->AddCircle(screenPos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
		drawList->AddText(ImVec2(screenPos.x + 10.0f, screenPos.y), IM_COL32(255, 255, 255, 255), label.c_str());

	}
}

void MeshInspector::drawPopup() {

	if (!openPopUp) return;

	if (ImGui::BeginPopup("MeshInspector Popup")) {

		//addMenuItem
		addMenuItemCopyToClipboard("Copy to clipboard");

		addMenuItemClearPoints("Clear points");

		addMenuItemResetView("Reset view");

		ImGui::EndPopup();
	}
}

void MeshInspector::renderPreview() {

	frameBuffer.bind();

	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	shader.use();

	float viewX0 = 2.0f * u0 - 1.0f;
	float viewX1 = 2.0f * u1 - 1.0f;

	float viewY0 = 2.0f * v0 - 1.0f;
	float viewY1 = 2.0f * v1 - 1.0f;

	
	shader.SetVec2("viewMin", viewX0, viewY0);
	shader.SetVec2("viewMax", viewX1, viewY1);

	vertexBuffer.bind();

	glLineWidth(1.0f); // change this value
	int lineVertexCount = (int)(mesh.gridLineVertices.size() / 2);
	glDrawArrays(GL_LINES, 0, lineVertexCount);
	glLineWidth(1.0f); // reset after drawing

	vertexBuffer.unbind();

	frameBuffer.unbind();

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(0.0, 1.0f), ImVec2(1.0f, 0.0f));


}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void MeshInspector::render() {

	setBaseNrNz();

	ImGui::Begin("Mesh Inspector");

	vertexBuffer.bufferSubData(mesh.gridLineVertices.size() * sizeof(float), mesh.gridLineVertices.data());

	drawToolBar();
	resizeImage(0.0f, 0.0f);
	renderPreview();
	handleMouse();
	drawHighlightedCells(g.obstacleIndices, g.zFace, g.rFace);
	drawTextAtSurfacePoint();
	drawPopup();

	ImGui::End();
}