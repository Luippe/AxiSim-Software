#include "scene_view.h"

#include "project.h"
#include "gui.h"

#include "results.h"

#include "console.h"

#include "memory_manager.h"
#include "unit_manager.h"
#include "time_manager.h"
#include "math_func.h"
#include "printer.h"


SceneView::SceneView(Project& project, GUI& gui) :
	console(gui.console),
	shaderResults("graphics/shaders/results.vert", "graphics/shaders/results.frag"),
	shaderLine("graphics/shaders/line.vert", "graphics/shaders/line.frag"),
	results(project.results),
	project(project),
	picker(project, *this)
{
	frameBuffer.createBuffer(500, 500, samples);
	createBuffer();
};


bool SceneView::compareFloat(float value, FilterValues& filterValues) {

	constexpr float eps = 1e-6f;

	switch (results.currentCompareType) {

	case CompareType::LessThan:
		return value < filterValues.valueAt;

	case CompareType::EqualTo:
		return std::abs(value - filterValues.valueAt) < eps;

	case CompareType::GreaterThan:
		return value > filterValues.valueAt;

	case CompareType::Between:
		return value > filterValues.valueLower && value < filterValues.valueUpper;

	case CompareType::Exclude:
		return value < filterValues.valueLower || value > filterValues.valueUpper;

	}

	return false;
}

void SceneView::updateSceneScale() {

	results.sceneScale.value = (float)(1.0 / Units::lengthUnits[results.sceneScale.index].toBase);

}

void SceneView::handleMouse() {

	// check if the image is hovered or the window is focused
	hovered = ImGui::IsItemHovered();
	focused = ImGui::IsWindowFocused();

	if (!(hovered && focused)) return;

	ImGuiIO& io = ImGui::GetIO();

	// ------------ Mouse Clicking -----------------
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		initX = io.MousePos.x;
		initY = io.MousePos.y;

		dragging = true;
		leftMouseDown = true;

	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {

		dragging = false;
		leftMouseDown = false;

		ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

		bool wasClick = abs(drag.x) < 3.0f && abs(drag.y) < 3.0f;	// check if the mouse movement is small enough to be considered a click

		if (wasClick) {
			check();
			picker.pick();
		}

		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
	}

	// ------------ Camera Panning -----------------
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		camera.calculatePan(io.MouseDelta.x, -io.MouseDelta.y);
	}

	// ------------ Camera Rotation-----------------
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
		rotating = true;
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
		rotating = false;
		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {

		glm::vec2 currentMouse(io.MousePos.x, io.MousePos.y);
		glm::vec2 previousMouse = currentMouse - glm::vec2(io.MouseDelta.x, io.MouseDelta.y);

		camera.calculateRotation(previousMouse, currentMouse);
	}

	// ------------ Camera Zooming -----------------
	if (io.MouseWheel != 0.0f) {
		camera.calculateZoom(io.MouseWheel);
	}
}

void SceneView::uploadUniforms() {

	shaderResults.use();
	shaderResults.SetFloat("vmin", results.currentField->vmin);
	shaderResults.SetFloat("vmax", results.currentField->vmax);
	shaderResults.SetFloat("R", results.g.R);
	shaderResults.SetFloat("L", results.g.L);
	shaderResults.SetInt("fieldTex", 0);
	shaderResults.SetInt("uColormap", 1);

}

std::vector<CylinderInstance> SceneView::createRowMergedCylinderInstances(
	std::vector<float>& field,
	FilterValues& filterValues
) {

	std::vector<CylinderInstance> instances;

	for (int i = 0; i < results.nr; i++) {
		int j = 0;

		while (j < results.nz) {
			int n = i * results.nz + j;

			if (!compareFloat(field[n], filterValues)) {
				j++;
				continue;
			}

			// Start selected run
			int j0 = j;

			while (j < results.nz && compareFloat(field[i * results.nz + j], filterValues)) {
				j++;
			}

			// j1 is one past the last selected cell
			int j1 = j;

			CylinderInstance inst{};

			// Axial bounds from face locations
			inst.x0 = (float)(results.g.zFace[j0]);
			inst.x1 = (float)(results.g.zFace[j1]);

			// Radial bounds from face locations
			inst.innerR = (float)(results.g.rFace[i]);
			inst.outerR = (float)(results.g.rFace[i + 1]);

			instances.push_back(inst);
		}
	}

	return instances;
}

void SceneView::updateSelectedInstances() {	// might be heavy on the cpu, optimize if AxiSim starts lagging

	selectedInstances = createRowMergedCylinderInstances(results.currentField->cellValues, results.filterValues);
	cvInstanceBuffer.bufferSubData(selectedInstances.size() * sizeof(CylinderInstance), selectedInstances.data());

}

void SceneView::createBuffer() {

	cvBuffer.createBuffer(results.verticesCV.size() * sizeof(CylinderTemplateVertex), results.verticesCV.data());

	cvBuffer.bind(); // bind the VAO you will draw with

	cvElementBuffer.createBuffer(results.indicesCV.size() * sizeof(unsigned int), results.indicesCV.data());

	// Per-vertex attributes: locations 0, 1, 2
	cvBuffer.enableAttribute(0, 3, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, dir));
	cvBuffer.enableAttribute(1, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, xCoord));
	cvBuffer.enableAttribute(2, 1, GL_FLOAT, sizeof(CylinderTemplateVertex), (void*)offsetof(CylinderTemplateVertex, radialCoord));

	// ---------------- Instance VBO ----------------
	cvInstanceBuffer.createBuffer(results.nr * results.nz * sizeof(CylinderInstance), nullptr);

	// IMPORTANT:
	cvBuffer.bind(); // bind the VAO you draw with

	glBindBuffer(GL_ARRAY_BUFFER, cvInstanceBuffer.getVBO());
	cvBuffer.enableAttribute(3, 4, GL_FLOAT, sizeof(CylinderInstance), (void*)0);
	glVertexAttribDivisor(3, 1);

	cvBuffer.unbind();

}

void SceneView::draw3DPreview() {

	// draw results
	if (!results.isReady) return;
	updateSelectedInstances();

	shaderResults.use();
	uploadUniforms();

	cvInstanceBuffer.bufferSubData(
		selectedInstances.size() * sizeof(CylinderInstance),
		selectedInstances.data()
	);

	glActiveTexture(GL_TEXTURE0);
	results.currentField->textureBuffer.bind();

	glActiveTexture(GL_TEXTURE1);
	colormap.bind();

	cvBuffer.bind();
	glDrawElementsInstanced(
		GL_TRIANGLES,
		(GLsizei)(results.indicesCV.size()),
		GL_UNSIGNED_INT,
		0,
		(GLsizei)(selectedInstances.size())
	);

	cvBuffer.unbind();

	glActiveTexture(GL_TEXTURE1);
	colormap.unbind();

	glActiveTexture(GL_TEXTURE0);
	results.currentField->textureBuffer.unbind();

	// draw coordinate axes
	renderer.renderAxis(shaderLine);

	// draw bounding box
	bound.renderBB(shaderLine);

}

void SceneView::render() {

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene");

	rectSize = ImGui::GetContentRegionAvail();
	int viewportWidth = (int)rectSize.x;
	int viewportHeight = (int)rectSize.y;

	if (viewportWidth != frameBuffer.width || viewportHeight != frameBuffer.height) {

		// resize scene framebuffer
		frameBuffer.createBuffer(viewportWidth, viewportHeight, samples);

		//update camera and picker width and height and position
		rectPos = ImGui::GetCursorScreenPos();

		camera.setDimensions(viewportWidth, viewportHeight, rectPos);
		picker.setDimensions(viewportWidth, viewportHeight, rectPos);
	}

	// update transformation matrix and snap camera
	camera.updateTransformationMatrix();
	camera.snapCamera();

	// draw and render calls
	frameBuffer.bind();

	glEnable(GL_DEPTH_TEST);
	glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// load transformation matrix for solution shader
	glm::mat4 model = scaleMat4(camera.model, results.sceneScale.value);
	shaderLine.loadTransformationMatrix(model, camera.view, camera.projection);
	shaderResults.loadTransformationMatrix(model, camera.view, camera.projection);


	// update picker
	picker.update();

	draw3DPreview();

	// end draw and render calls
	frameBuffer.unbind();

	// downscale the framebuffer
	frameBuffer.resolve();

	ImGui::Image(
		(ImTextureID)(intptr_t)frameBuffer.getTextureID(),
		ImVec2((float)viewportWidth, (float)viewportHeight),
		ImVec2(0, 1),
		ImVec2(1, 0)
	);

	handleMouse();

	//printf("RUNNING IN SCENE RENDER\n");
	ImGui::End();
	ImGui::PopStyleVar();
	//printMemoryUsage();
}