#include "inspector.h"
#include "printer.h"
#include "scene_view.h"

Inspector::Inspector(SceneView& scene) :
		scene(scene),
		mesh(scene.mesh),
		results(scene.results),
		colormap(scene.colormap),
		g(mesh.g){
	aspect = (float)(g.L) / (float)(g.R);
}

void Inspector::generate() {
	aspect = (float)(g.L) / (float)(g.R);
	createScalarImage();
	createBuffer();
}

//void Inspector::updateBuffer(int nr, int nz, std::vector<float> data) {
//	textureBuffer.updateBuffer(nz, nr, )
//}

void Inspector::createBuffer() {
	textureBuffer.createBuffer(GL_RGBA32F, g.nz + 1, g.nr + 1, GL_RGB, GL_FLOAT, pixels.data());	// initialize buffer
}

void Inspector::createScalarImage() {
	//printf("%d\n", pixels.size());
	std::vector<float>& currentData = results.currentField->processedData;
	pixels.clear();
	pixels.resize(3 * currentData.size());

	for (int n = 0; n < currentData.size(); n++) {

		glm::vec3 color = colormap.getColor((double)currentData[n], results.currentField->vmin, results.currentField->vmax);
		pixels[3 * n + 0] = color.r;
		pixels[3 * n + 1] = color.g;
		pixels[3 * n + 2] = color.b;
	}
}

void Inspector::drawField() {

	imageSize = ImGui::GetContentRegionAvail();

	// fit inside available region while preserving aspect ratio
	if (keepAspectRatio) {
		if (imageSize.x / imageSize.y > aspect) {
			imageSize.x = imageSize.y * aspect;
		}
		else {
			imageSize.y = imageSize.x / aspect;
		}
	}
	ImGui::Image((ImTextureID)(intptr_t)textureBuffer.getTextureID(), imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
}

glm::vec2 Inspector::getMouseIndex() {

	ImVec2 mousePos = ImGui::GetIO().MousePos;
	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();

	ImVec2 localPos(mousePos.x - itemMin.x, itemMax.y - mousePos.y);

	boxWidth = (imageSize.x / (float)g.nz);
	boxHeight = (imageSize.y / (float)g.nr);

	int i = glm::clamp(0, (int)(localPos.y / boxHeight), g.nr);
	int j = glm::clamp(0, (int)(localPos.x / boxWidth), g.nz);

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

	if (!ImGui::IsWindowHovered()) return;

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

void Inspector::render() {
	ImGui::Begin("Inspector");
	drawField();
	updateSelectedIndices();
	drawRect();
	//drawGridConfig();
	ImGui::End();
}