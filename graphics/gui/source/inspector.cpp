#include "inspector.h"
#include "printer.h"
#include "scene_view.h"
#include "math_func.h"
#include <format>

Inspector::Inspector(SceneView& scene) :
		scene(scene),
		mesh(scene.mesh),
		results(scene.results),
		colormap(scene.colormap),
		g(mesh.g),
		inspectorShader("graphics/shaders/inspector.vert", "graphics/shaders/inspector.frag"){
	aspect = (float)(g.L) / (float)(g.R);
	// radial location
	frameBuffer.createBuffer(100, 100);
}

void updateUV(float& u0, float& u1, float& v0, float& v1, ImVec2& zoomCenter, float halfW, float halfH) {
	u0 = glm::clamp(zoomCenter.x - halfW, 0.0f, 1.0f);
	u1 = glm::clamp(zoomCenter.x + halfW, 0.0f, 1.0f);
	v0 = glm::clamp(zoomCenter.y - halfH, 0.0f, 1.0f);
	v1 = glm::clamp(zoomCenter.y + halfH, 0.0f, 1.0f);
}

void clampZoomCenter(ImVec2& zoomCenter, float& halfW, float& halfH) {
	zoomCenter.x = glm::clamp(zoomCenter.x, halfW, 1.0f - halfW);
	zoomCenter.y = glm::clamp(zoomCenter.y, halfH, 1.0f - halfH);
}

void toggleSelectedPoint(std::vector<InspectorPoint>& points, ImVec2& dataPos, ImVec2& mousePos, float value) {
	auto it = std::find_if(points.begin(), points.end(),
		[&](const InspectorPoint& p) {
			return (p.dataPos.x == dataPos.x && p.dataPos.y == dataPos.y);
		});

	if (it != points.end()) {
		points.erase(it);
	}
	else {
		points.push_back({ mousePos, dataPos, value });
	}
}

void Inspector::generate() {
	aspect = (float)(g.L) / (float)(g.R);
	nrBase = g.nr;
	nzBase = g.nz;

	createFullScreenQuad();
	createBuffer();
	uploadUniforms();
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

void Inspector::updateTextureBuffer(const void* data) {
	textureBuffer.updateBuffer(nzBase + 1, nrBase + 1, GL_RED, GL_FLOAT, data);
}

void Inspector::uploadUniforms() {
	inspectorShader.use();
	inspectorShader.SetFloat("vmin", results.currentField->vmin);
	inspectorShader.SetFloat("vmax", results.currentField->vmax);
	inspectorShader.SetInt("fieldTex", 0);
	inspectorShader.SetInt("uColormap", 1);
}

void Inspector::createBuffer() {

	textureBuffer.createBuffer(GL_R32F, nzBase + 1, nrBase + 1, GL_RED, GL_FLOAT, results.currentField->processedData.data());

}

void Inspector::resizeImage() {

	ImVec2 avail = ImGui::GetContentRegionAvail();
	imageWidth = (int)avail.x;
	imageHeight = (int)avail.y;
	if (imageWidth != frameBuffer.width || imageHeight != frameBuffer.height) {
		frameBuffer.createBuffer(imageWidth, imageHeight);
	}
}

ImVec2 Inspector::getMouseIndex() {

	ImVec2 mousePos = ImGui::GetIO().MousePos;
	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	float viewW = 1.0f / zoom;
	float viewH = 1.0f / zoom;


	boxWidth = (float)imageWidth / (float)(nzBase + 1);
	boxHeight = (float)imageHeight / (float)(nrBase + 1);


	ImVec2 localPos = ImVec2(mousePos.x - itemMin.x, itemMax.y - mousePos.y);

	float localU = localPos.x / imageWidth;
	float localV = localPos.y / imageHeight;

	float texU = u0 + localU * (u1 - u0);
	float texV = v0 + localV * (v1 - v0);

	float i = (float)glm::clamp((int)std::round(texV * (nrBase + 1)), 0,  nrBase + 1);
	float j = (float)glm::clamp((int)std::round(texU * (nzBase + 1)), 0,  nzBase + 1);

	return { j , i };
}

ImVec2 Inspector::gridToScreen(float j, float i) {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	float texU = j / (float)(nzBase + 1);
	float texV = i / (float)(nrBase + 1);

	float localU = (texU - u0) / (u1 - u0);
	float localV = (texV - v0) / (v1 - v0);

	float x = itemMin.x + localU * imageWidth;
	float y = itemMax.y - localV * imageHeight;

	return ImVec2(x, y);
}

void Inspector::displayRect() {

	if (!isRectReady) return;

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawList->AddRect(rectPos1, rectPos2, IM_COL32(255, 255, 255, 255));
}

void Inspector::displayTextValue() {

	ImVec2 imageMin = ImGui::GetItemRectMin();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	for (const InspectorPoint& point : points) {

		// convert grid index to normalized texture coordinate
		float u = point.dataPos.x / float(nzBase);
		float v = point.dataPos.y / float(nrBase);

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
			"i: {:.0f}\nj: {:.0f}\nvalue: {:.3f}",
			point.dataPos.x,
			point.dataPos.y,
			point.value
		);

		drawList->AddCircleFilled(screenPos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
		drawList->AddCircle(screenPos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
		drawList->AddText(ImVec2(screenPos.x + 10.0f, screenPos.y), IM_COL32(0,0,0, 255), label.c_str());

	}
}

void Inspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();
	currentMouseIndex = getMouseIndex();		// constantly update current mouse index
	currentMousePos = gridToScreen(currentMouseIndex.x, currentMouseIndex.y);

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		rectPos1 = currentMousePos;
		rectPos2 = currentMousePos;
		initMouseIndex = getMouseIndex();
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {

		selectedIndices.clear();

		// update rect points
		isRectReady = true;
		rectPos2 = currentMousePos;

		// update displaying region
		startX = std::min(initMouseIndex.x, currentMouseIndex.x);
		startY = std::min(initMouseIndex.y, currentMouseIndex.y);

		endX = std::max(initMouseIndex.x, currentMouseIndex.x);
		endY = std::max(initMouseIndex.y, currentMouseIndex.y);

		results.colFront = startX;
		results.colBack = endX;
		results.rowTop = endY;
		results.rowBot = startY;

		results.currentFront = (float)startX * (float)g.dz;
		results.currentBack = (float)endX * (float)g.dz;
		results.currentOuter = (float)endY * (float)g.dr;
		results.currentInner = (float)startY * (float)g.dr;

		results.updateModel();

	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {

		ImVec2 delta = io.MouseDelta;

		float viewW = 1.0f / zoom;
		float viewH = 1.0f / zoom;

		zoomCenter.x -= (delta.x / imageWidth) * viewW;
		zoomCenter.y += (delta.y / imageHeight) * viewH;

		float halfW = viewW * 0.5f;
		float halfH = viewH * 0.5f;

		clampZoomCenter(zoomCenter, halfW, halfH);

		updateUV(u0, u1, v0, v1, zoomCenter, halfW, halfH);
	}

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {

		zoom *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
		zoom = glm::clamp(zoom, 1.0f, 20.0f);

		float halfW = 0.5f / zoom;
		float halfH = 0.5f / zoom;

		clampZoomCenter(zoomCenter, halfW, halfH);

		updateUV(u0, u1, v0, v1, zoomCenter, halfW, halfH);

	}

	// handle mouse hovering
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(currentMousePos, circleRadius, IM_COL32(150, 150, 150, 255), 16);
	drawList->AddCircle(currentMousePos, circleRadius, IM_COL32(200, 200, 200, 255), 16, 1.0f);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		//printf("%f\n", results.currentField->getData(dataPos));
		toggleSelectedPoint(points, currentMouseIndex, currentMousePos, results.currentField->getData(glm::vec2(g.dz * currentMouseIndex.x, g.dr * currentMouseIndex.y)));
	}
}

void Inspector::renderPreview() {

	frameBuffer.bind();

	glViewport(0, 0, imageWidth, imageHeight);
	glDisable(GL_DEPTH_TEST);
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	inspectorShader.use();
	uploadUniforms();

	glActiveTexture(GL_TEXTURE0);
	results.currentField->textureBuffer.bind();

	glActiveTexture(GL_TEXTURE1);
	results.colormap.bind();

	vertexBuffer.bind();
	glDrawArrays(GL_TRIANGLES, 0, 6);
	vertexBuffer.unbind();

	results.colormap.unbind();
	results.currentField->textureBuffer.unbind();

	frameBuffer.unbind();

	glEnable(GL_DEPTH_TEST);
}



void Inspector::render() {

	ImGui::Begin("Inspector");
	resizeImage();

	updateTextureBuffer(results.currentField->processedData.data());
	renderPreview();

	frameBuffer.resolve();
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));

	handleMouse();
	displayRect();
	displayTextValue();

	ImGui::End();
}