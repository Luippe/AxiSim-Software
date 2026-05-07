#include "inspector.h"
#include "printer.h"
#include "scene_view.h"

Inspector::Inspector(SceneView& scene) :
		scene(scene),
		mesh(scene.mesh),
		results(scene.results),
		colormap(scene.colormap),
		g(mesh.g),
		inspectorShader("graphics/shaders/inspector.vert", "graphics/shaders/inspector.frag"){
	aspect = (float)(g.L) / (float)(g.R);
	frameBuffer.createBuffer(100, 100);
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

void Inspector::updateTextureBuffer(int nr, int nz, const std::vector<float>& data) {
	textureBuffer.updateBuffer(nz, nr, GL_RED, GL_FLOAT, data.data());
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

glm::vec2 Inspector::getMouseIndex() {

	ImVec2 mousePos = ImGui::GetIO().MousePos;
	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	ImVec2 localPos(mousePos.x - itemMin.x, itemMax.y - mousePos.y);

	boxWidth = (imageWidth / nzBase);
	boxHeight = (imageHeight / nrBase);

	int i = glm::clamp(0, (int)(localPos.y / boxHeight), nrBase);
	int j = glm::clamp(0, (int)(localPos.x / boxWidth), nzBase);

	return glm::vec2(j, i);
}

void Inspector::drawRect() {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	drawList->AddRect(
		ImVec2(itemMin.x + initMousePos.x * boxWidth, itemMax.y - initMousePos.y * boxHeight),
		ImVec2(itemMin.x + currentMousePos.x * boxWidth, itemMax.y - currentMousePos.y * boxHeight),
		IM_COL32(255, 255, 255, 255));

}

void Inspector::updateSelectedIndices() {

	if (!ImGui::IsItemHovered()) return;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		initMousePos = getMouseIndex();
		currentMousePos = initMousePos;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {

		selectedIndices.clear();

		currentMousePos = getMouseIndex();

		startX = std::min(initMousePos.x, currentMousePos.x);
		startY = std::min(initMousePos.y, currentMousePos.y);

		endX = std::max(initMousePos.x, currentMousePos.x);
		endY = std::max(initMousePos.y, currentMousePos.y);

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
}

void Inspector::renderPreview() {

	frameBuffer.bind();

	glViewport(0, 0, imageWidth, imageHeight);
	//glDisable(GL_DEPTH_TEST);
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	inspectorShader.use();
	//uploadUniforms();

	glActiveTexture(GL_TEXTURE0);
	//results.currentTextureBuffer->bind();
	textureBuffer.bind();

	glActiveTexture(GL_TEXTURE1);
	//results.colormap.bind();
	colormap.bind();

	vertexBuffer.bind();
	glDrawArrays(GL_TRIANGLES, 0, 6);
	vertexBuffer.unbind();

	colormap.unbind();
	textureBuffer.unbind();
	
	//results.colormap.unbind();
	//results.currentTextureBuffer->unbind();

	frameBuffer.unbind();

	//glEnable(GL_DEPTH_TEST);
}

void Inspector::render() {
	ImGui::Begin("Inspector");
	resizeImage();

	updateTextureBuffer(nrBase + 1, nzBase + 1, results.currentField->processedData);
	renderPreview();

	frameBuffer.resolve();
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

	updateSelectedIndices();
	drawRect();

	ImGui::End();
}