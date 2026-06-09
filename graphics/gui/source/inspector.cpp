#include "inspector.h"

#include <format>

#include "scene_view.h"
#include "project.h"
#include "colorbar.h"

#include "flag_manager.h"
#include "printer.h"
#include "math_func.h"

Inspector::Inspector(Project& project, SceneView& scene, AppAssets& assets) :
		scene(scene),
		project(project),
		mesh(project.mesh),
		results(project.results),
		g(mesh.g),
		assets(assets),
		colorbar(scene.colormap, project.results),
		BaseSurfaceViewer("graphics/shaders/inspector.vert", "graphics/shaders/inspector.frag") {

	// radial location
	frameBuffer.create2DBuffer(100, 100, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);
	createFullScreenQuad();

}

// ======================================================================
// -----------------------HELPER FUNCTIONS-------------------------------
// ======================================================================
void Inspector::generate() {
	nrBase = g.nr;
	nzBase = g.nz;
}

void Inspector::createFullScreenQuad() {

	quadVertices = {
		// x, y, u, v
		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 0.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,

		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f, 0.0f, 1.0f
	};

	vertexBuffer.createBuffer(quadVertices.size() * sizeof(float), quadVertices.data());
	vertexBuffer.bind();
	vertexBuffer.enableAttribute(0, 2, GL_FLOAT, 4 * sizeof(float), (void*)0);
	vertexBuffer.enableAttribute(1, 2, GL_FLOAT, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	vertexBuffer.unbind();

}

void Inspector::uploadUniforms() {

	shader.use();
	shader.SetFloat("vmin", results.currentField->vmin);
	shader.SetFloat("vmax", results.currentField->vmax);
	shader.SetInt("fieldTex", 0);
	shader.SetInt("uColormap", 1);

}


// ======================================================================
// -----------------------MOUSE HANDLES----------------------------------
// ======================================================================
void Inspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();
	currentMouseIndex = getMouseIndex(g.rFace, g.zFace);		// constantly update current mouse index
	currentMousePos = gridToScreen((int)currentMouseIndex.x, (int)currentMouseIndex.y, g.rFace, g.zFace);

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {
		handleZoom(io);
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && toggleDrawCell) {

		rectPos1 = screenToUV(currentMousePos);
		rectPos2 = rectPos1;
		initMouseIndex = getMouseIndex(g.rFace, g.zFace);
		isRectReady = true;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

		openPopUp = true;
		ImGui::OpenPopup("Inspector Popup");

	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && toggleDrawCell) {

		handleRectSelection(io);

		// update displaying region
		startX = std::min(initMouseIndex.x, currentMouseIndex.x);
		startY = std::min(initMouseIndex.y, currentMouseIndex.y);

		endX = std::max(initMouseIndex.x, currentMouseIndex.x);
		endY = std::max(initMouseIndex.y, currentMouseIndex.y);

		results.colFront = startX;
		results.colBack = endX;
		results.rowTop = endY;
		results.rowBot = startY;

		float currentDz = (float)g.dz[currentMouseIndex.x];
		float currentDr = (float)g.dr[currentMouseIndex.y];
		results.currentFront = (float)startX * currentDz;
		results.currentBack = (float)endX * currentDz;
		results.currentOuter = (float)endY * currentDr;
		results.currentInner = (float)startY * currentDr;

	}


	if (toggleDrawCell) return;

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		handlePan(io);
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(currentMousePos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
	drawList->AddCircle(currentMousePos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		float currentDz = (float)g.dz[currentMouseIndex.x];
		float currentDr = (float)g.dr[currentMouseIndex.y];
		toggleSelectedPoint(points, currentMouseIndex, currentMousePos, results.currentField->getData(glm::vec2(currentDz * currentMouseIndex.x, currentDr * currentMouseIndex.y)));
	}
}

void Inspector::copyActiveSurfaceToClipboard() {

	GLint oldFBO, oldViewport[4];
	ImVec2 oldDisplaySize, oldFramebufferSize;
	offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

	// build imgui draw commands
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);
	
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));
	
	ImGui::End();
	ImGui::PopStyleVar();

	offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void Inspector::displayTextValue() {

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
			"i: {:.0f}\nj: {:.0f}\nvalue: {:.5f}",
			point.dataPos.y,
			point.dataPos.x,
			point.value
		);

		drawList->AddCircleFilled(screenPos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
		drawList->AddCircle(screenPos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
		drawList->AddText(ImVec2(screenPos.x + 10.0f, screenPos.y), IM_COL32(0, 0, 0, 255), label.c_str());

	}
}

void Inspector::drawToolBar() {
	float toolbarHeight = 32.0f;

	ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

	if (addImageButton("Reset", "Reset View", assets.houseIcon, buttonSize)) {
		resetView();
	}
	ImGui::SameLine();

	if (addImageButton("Clear", "Clear all points", assets.clearIcon, buttonSize)) {
		points.clear();
	}
	ImGui::SameLine();

	addImageButtonToggle("Select", "Select", assets.selectRegionIcon, ImVec2(22.0f, 22.0f), toggleDrawCell);
	ImGui::SameLine();

	if (addImageButton("Copy", "Copy to clipboard", assets.copyIcon, buttonSize) || consoleCopy) {
		pendingCopyWidth = frameBuffer.width;
		pendingCopyHeight = frameBuffer.height;
		pendingCopy = true;
		consoleCopy = false;
	}
	ImGui::SameLine();

	ImGui::EndChild();
	ImGui::Separator();
}

void Inspector::drawPopup() {

	if (!openPopUp) return;

	if (ImGui::BeginPopup("Inspector Popup")) {

		addMenuItemCopyToClipboard("Copy to clipboard");

		addMenuItemClearPoints("Clear points");
		
		addMenuItemResetView("Reset view");


		ImGui::EndPopup();
	}
}

void Inspector::renderPreview() {

	frameBuffer.bind();
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	shader.use();
	uploadUniforms();

	glActiveTexture(GL_TEXTURE0);
	results.currentField->textureBuffer.bind();

	glActiveTexture(GL_TEXTURE1);
	scene.colormap.bind();

	vertexBuffer.bind();
	glDrawArrays(GL_TRIANGLES, 0, 6);
	vertexBuffer.unbind();

	glActiveTexture(GL_TEXTURE1);
	scene.colormap.unbind();

	glActiveTexture(GL_TEXTURE0);
	results.currentField->textureBuffer.unbind();

	frameBuffer.unbind();

}

// ======================================================================
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void Inspector::render() {

	ImGui::Begin("Inspector");

	drawToolBar();

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();

	Rect surfaceRect = makePaddedRect(
		pos,
		size,
		0.0f,
		colorbar.width,
		0.0f,
		0.0f
	);

	resizeImage(surfaceRect.size());
	
	renderPreview();

	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));

	handleMouse();
	displayRect(nrBase, nzBase);
	displayTextValue();
	drawPopup();

	ImGui::SameLine();
	colorbar.render();

	ImGui::End();
}