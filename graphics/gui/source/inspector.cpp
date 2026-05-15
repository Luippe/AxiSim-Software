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

glm::ivec2 Inspector::getMouseIndex() {

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

	int i = glm::clamp((int)std::round(texV * (nrBase + 1)), 0,  nrBase + 1);
	int j = glm::clamp((int)std::round(texU * (nzBase + 1)), 0,  nzBase + 1);

	return { j, i };
}

ImVec2 Inspector::gridToScreen(int j, int i) {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	float texU = (float)j / (float)(nzBase + 1);
	float texV = (float)i / (float)(nrBase + 1);

	float localU = (texU - u0) / (u1 - u0);
	float localV = (texV - v0) / (v1 - v0);

	float x = itemMin.x + localU * imageWidth;
	float y = itemMax.y - localV * imageHeight;

	return ImVec2(x, y);
}

void Inspector::drawRect() {

	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ImVec2 initMousePos = gridToScreen(initMouseIndex.x, initMouseIndex.y);
	ImVec2 currentMousePos = gridToScreen(currentMouseIndex.x, currentMouseIndex.y);
	
	drawList->AddRect(
		ImVec2(initMousePos.x, initMousePos.y),
		ImVec2(currentMousePos.x, currentMousePos.y),
		IM_COL32(255, 255, 255, 255));

}

void Inspector::displayValue() {



}

void Inspector::handleMouse() {

	if (!ImGui::IsItemHovered()) return;

	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		initMouseIndex = getMouseIndex();
		currentMouseIndex = initMouseIndex;
	}


	if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {

		selectedIndices.clear();

		currentMouseIndex = getMouseIndex();

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

		zoomCenter.x = glm::clamp(zoomCenter.x, halfW, 1.0f - halfW);
		zoomCenter.y = glm::clamp(zoomCenter.y, halfH, 1.0f - halfH);

		u0 = glm::clamp(zoomCenter.x - halfW, 0.0f, 1.0f);
		u1 = glm::clamp(zoomCenter.x + halfW, 0.0f, 1.0f);
		v0 = glm::clamp(zoomCenter.y - halfH, 0.0f, 1.0f);
		v1 = glm::clamp(zoomCenter.y + halfH, 0.0f, 1.0f);
	}

	// handle zooming in/out
	if (io.MouseWheel != 0.0f) {

		zoom *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
		zoom = glm::clamp(zoom, 1.0f, 20.0f);

		float viewW = 1.0f / zoom;
		float viewH = 1.0f / zoom;

		u0 = glm::clamp(zoomCenter.x - viewW * 0.5f, 0.0f, 1.0f);
		u1 = glm::clamp(zoomCenter.x + viewW * 0.5f, 0.0f, 1.0f);
		v0 = glm::clamp(zoomCenter.y - viewH * 0.5f, 0.0f, 1.0f);
		v1 = glm::clamp(zoomCenter.y + viewH * 0.5f, 0.0f, 1.0f);


		//printFloat(u0, u1, v0, v1);
	}

	displayValue();
}

void Inspector::renderPreview() {

	frameBuffer.bind();

	glViewport(0, 0, imageWidth, imageHeight);
	//glDisable(GL_DEPTH_TEST);
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

	//glEnable(GL_DEPTH_TEST);
}



void Inspector::render() {
	ImGui::Begin("Inspector");
	resizeImage();

	updateTextureBuffer(results.currentField->processedData.data());
	renderPreview();

	frameBuffer.resolve();
	ImGui::Image((ImTextureID)(intptr_t)frameBuffer.getTextureID(), ImVec2((float)imageWidth, (float)imageHeight), ImVec2(u0, v1), ImVec2(u1, v0));

	handleMouse();
	drawRect();


	ImGui::End();
}